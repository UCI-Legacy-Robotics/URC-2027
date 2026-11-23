"""Receive science requests through ROS 2 and report progress and results."""

from dataclasses import asdict, fields
from threading import Event, Lock, Thread

import rclpy
from rclpy.action import ActionServer, CancelResponse, GoalResponse
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import ExternalShutdownException, MultiThreadedExecutor
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from rclpy.signals import SignalHandlerOptions
from science_interfaces.action import ScienceSequence
from science_interfaces.msg import ScienceState
from std_srvs.srv import Trigger

from .controller import Config, ScienceController
from .hardware import Cancellation
from .ros_hardware import RosHardware


class ScienceSequencer(Node):
    def __init__(self, **node_options):
        super().__init__("science_sequencer", **node_options)
        self.declare_parameter("hardware_calibrated", False)
        parameters = {}
        for field in fields(Config):
            parameters[field.name] = self.declare_parameter(
                field.name, field.default
            ).value
        config = Config(**parameters)
        if not self.get_parameter("hardware_calibrated").value:
            raise RuntimeError(
                "Configure calibrated step counts and set hardware_calibrated:=true "
                "before running the sequencer"
            )
        self.controller = ScienceController(RosHardware(self), config)
        self._reservation = Lock()
        self._status_lock = Lock()
        self._active = False
        self._accepting = True
        self._idle = Event()
        self._idle.set()
        self._token = None
        self._group = ReentrantCallbackGroup()
        qos = QoSProfile(
            depth=1,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.TRANSIENT_LOCAL,
        )
        self._status = self.create_publisher(ScienceState, "science/state", qos)
        self._server = ActionServer(
            self,
            ScienceSequence,
            "science/sequence",
            self._execute,
            goal_callback=self._goal,
            cancel_callback=self._cancel,
            callback_group=self._group,
        )
        self._stop_service = self.create_service(
            Trigger, "science/emergency_stop", self._emergency_stop, callback_group=self._group,
        )
        self._timer = self.create_timer(
            0.5, lambda: self._publish_state(), callback_group=self._group
        )
        self._publish_state()

    @staticmethod
    def _message(state):
        return ScienceState(**asdict(state))

    def _publish_state(self, snapshot=None):
        with self._status_lock:
            current = self.controller.state
            if snapshot is None or current.phase == "ESTOP":
                snapshot = current
            message = self._message(snapshot)
            self._status.publish(message)
            return message

    def _emergency_stop(self, _request, response):
        response.success, response.message = self.controller.emergency_stop()
        self._publish_state()
        return response

    def _goal(self, request):
        # Mark this request as active now so another request cannot start before it runs.
        with self._reservation:
            reason = self.controller.rejection_reason(request.command)
            if not self._accepting or self._active or reason:
                self.get_logger().warning(reason or "Another science operation is still running")
                return GoalResponse.REJECT
            self._active = True
            self._idle.clear()
            self._token = Cancellation()
            return GoalResponse.ACCEPT

    def _cancel(self, goal_handle):
        with self._reservation:
            if self._token is not None and self._token.request():
                return CancelResponse.ACCEPT
        return CancelResponse.REJECT

    def _execute(self, goal_handle):
        def feedback(state):
            message = self._publish_state(state)
            if goal_handle.is_active:
                goal_handle.publish_feedback(ScienceSequence.Feedback(state=message))

        try:
            outcome = self.controller.run(
                goal_handle.request.command, self._token, feedback
            )
            result = ScienceSequence.Result(
                success=outcome.success,
                cancelled=outcome.cancelled,
                message=outcome.message,
                state=self._message(outcome.state),
                measurement_names=list(outcome.names),
                measurement_values=list(outcome.values),
                measurement_units=list(outcome.units),
            )
            if outcome.cancelled and goal_handle.is_cancel_requested:
                goal_handle.canceled()
            elif outcome.success:
                goal_handle.succeed()
            else:
                goal_handle.abort()
            return result
        finally:
            with self._reservation:
                self._token = None
                self._active = False
                self._idle.set()

    def stop_for_shutdown(self):
        with self._reservation:
            self._accepting = False
            if self._token:
                self._token.request()
        # Keep processing ROS replies while the running operation returns to safety.
        # Wait for recovery to finish before sending the final stop request.
        budget = (
            self.controller.config.command_timeout
            + 6 * self.controller.config.recovery_timeout
        )
        if not self._idle.wait(timeout=budget):
            self.get_logger().error("Timed out waiting for science recovery during shutdown")
        # Stop any activity left running between operations; this does not retract platforms.
        try:
            self.controller.hardware.stop(self.controller.config.recovery_timeout)
        except Exception as error:
            self.get_logger().error(f"Could not confirm the hardware stopped during shutdown: {error}")


def main(args=None):
    # Keep ROS available after Ctrl-C so we can still receive the stop confirmation.
    rclpy.init(args=args, signal_handler_options=SignalHandlerOptions.NO)
    node = None
    spin_thread = None
    executor = MultiThreadedExecutor(num_threads=4)
    try:
        node = ScienceSequencer()
        executor.add_node(node)

        def spin():
            try:
                executor.spin()
            except ExternalShutdownException:
                pass

        spin_thread = Thread(target=spin, name="science_ros_executor", daemon=True)
        spin_thread.start()
        while spin_thread.is_alive():
            spin_thread.join(timeout=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        if node is not None:
            node.stop_for_shutdown()
        executor.shutdown()
        if spin_thread is not None:
            spin_thread.join(timeout=1.0)
        if node is not None:
            node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
