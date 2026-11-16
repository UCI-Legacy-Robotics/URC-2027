"""Send commands to the embedded team's science driver and wait for its replies."""

from threading import Event
from time import monotonic

from action_msgs.msg import GoalStatus
from rclpy.action import ActionClient
from rclpy.callback_groups import ReentrantCallbackGroup
from science_interfaces.action import ScienceCommand
from std_srvs.srv import Trigger

from .hardware import HardwareError, HardwareUnsafe, Reply, SequenceCancelled


class RosHardware:
    def __init__(self, node, cancel_timeout=3.0):
        self._group = ReentrantCallbackGroup()
        self._client = ActionClient(
            node, ScienceCommand, "science/hardware/execute", callback_group=self._group
        )
        self._stop = node.create_client(
            Trigger, "science/hardware/stop", callback_group=self._group
        )
        self._cancel_timeout = cancel_timeout

        self._uncertain = False

    @staticmethod
    def _wait(future, timeout):
        deadline = monotonic() + timeout
        while not future.done():
            remaining = deadline - monotonic()
            if remaining <= 0:
                raise TimeoutError()
            Event().wait(min(0.01, remaining))
        return future.result()

    def stop(self, timeout):
        deadline = monotonic() + timeout
        if not self._stop.wait_for_service(timeout_sec=timeout):
            raise HardwareUnsafe("Cannot reach the hardware stop service")
        try:
            response = self._wait(
                self._stop.call_async(Trigger.Request()),
                max(0.001, deadline - monotonic()),
            )
        except Exception as error:
            raise HardwareUnsafe("Did not receive confirmation that the hardware stopped") from error
        if not response.success:
            raise HardwareUnsafe(f"Could not confirm the stop: {response.message}")

    def _cancel_and_wait(self, handle, result_future):
        try:
            self._wait(handle.cancel_goal_async(), self._cancel_timeout)
            terminal = self._wait(result_future, self._cancel_timeout)
            if terminal.status not in {
                GoalStatus.STATUS_CANCELED,
                GoalStatus.STATUS_ABORTED,
                GoalStatus.STATUS_SUCCEEDED,
            }:
                raise RuntimeError("The hardware command has not finished")
        except Exception as error:
            self._uncertain = True
            raise HardwareUnsafe("Cannot confirm that the hardware command stopped") from error

    @staticmethod
    def _cancel_late_goal(future):
        # A delayed reply may arrive after we have given up waiting.
        # Try to cancel that command, but keep further movement blocked:
        # sending a cancel request does not confirm that the hardware stopped.
        try:
            handle = future.result()
            if handle.accepted:
                handle.cancel_goal_async()
        except Exception:
            pass

    def execute(self, command, value, cancelled, timeout):
        if self._uncertain:
            raise HardwareUnsafe(
                "A previous command may still be running. Check and stop the hardware, then restart the driver and sequencer"
            )
        deadline = monotonic() + timeout
        while not self._client.wait_for_server(timeout_sec=0.05):
            if cancelled():
                raise SequenceCancelled()
            if monotonic() >= deadline:
                raise HardwareError("Cannot reach the embedded science driver")
        if cancelled():
            raise SequenceCancelled()
        goal_future = self._client.send_goal_async(
            ScienceCommand.Goal(command=command, value=value)
        )
        try:
            cancel_deadline = None
            while not goal_future.done():
                if cancelled() and cancel_deadline is None:
                    cancel_deadline = monotonic() + self._cancel_timeout
                if monotonic() >= min(deadline, cancel_deadline or deadline):
                    raise TimeoutError()
                Event().wait(0.01)
            handle = goal_future.result()
        except Exception as error:
            self._uncertain = True
            goal_future.add_done_callback(self._cancel_late_goal)
            raise HardwareUnsafe("Did not receive a reply confirming whether the driver accepted the command") from error
        if not handle.accepted:
            raise HardwareError(f"The embedded driver rejected {command}")
        result_future = handle.get_result_async()
        while not result_future.done():
            if cancelled() or monotonic() >= deadline:
                was_cancelled = cancelled()
                self._cancel_and_wait(handle, result_future)
                if was_cancelled:
                    raise SequenceCancelled()
                raise HardwareError(f"Hardware command timed out: {command}")
            Event().wait(0.01)
        try:
            wrapped = result_future.result()
            reply = wrapped.result
            if wrapped.status != GoalStatus.STATUS_SUCCEEDED or not reply.success:
                raise HardwareError(f"{command} failed: {reply.message}")
            return Reply(reply.actual_value, tuple(reply.readings), tuple(reply.units))
        except HardwareError:
            raise
        except Exception as error:
            self._uncertain = True
            raise HardwareUnsafe(f"Could not read the hardware result for {command}") from error
