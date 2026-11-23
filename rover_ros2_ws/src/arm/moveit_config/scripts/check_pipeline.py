#!/usr/bin/env python3
"""Read-only readiness check; --execute also plans/moves/restores all groups in mock mode."""
import argparse
import math
from pathlib import Path
import sys
import time
import xml.etree.ElementTree as ET

import rclpy
from rclpy.action import ActionClient
from rclpy.node import Node
from rclpy.qos import DurabilityPolicy, QoSProfile
from rclpy.utilities import remove_ros_args
from ament_index_python.packages import get_package_share_directory
from action_msgs.msg import GoalStatus
from control_msgs.action import FollowJointTrajectory
from controller_manager_msgs.srv import ListControllers
from moveit_msgs.action import MoveGroup
from moveit_msgs.msg import Constraints, JointConstraint, MoveItErrorCodes
from sensor_msgs.msg import JointState
from std_msgs.msg import String
import yaml


class PipelineCheck(Node):
    def __init__(self, timeout):
        super().__init__('arm_pipeline_check')
        self.timeout = timeout
        self.positions, self.received, self.description = {}, {}, ''
        control = Path(get_package_share_directory('arm_control_config'))
        config = Path(get_package_share_directory('moveit_config'))
        self.controllers = yaml.safe_load((control / 'config/ros2_controllers.yaml').read_text())
        self.mapping = yaml.safe_load((config / 'config/moveit_controllers.yaml').read_text())[
            'moveit_simple_controller_manager']
        self.required = {joint for name in self.mapping['controller_names']
                         for joint in self.mapping[name]['joints']}
        self.create_subscription(JointState, '/joint_states', self.on_state, 10)
        self.create_subscription(String, '/robot_description', self.on_description,
                                 QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL))
        self.list_client = self.create_client(ListControllers, '/controller_manager/list_controllers')
        self.trajectory_clients = {name: ActionClient(self, FollowJointTrajectory,
            f'/{name}/{self.mapping[name]["action_ns"]}') for name in self.mapping['controller_names']}
        self.move_client = ActionClient(self, MoveGroup, '/move_action')

    def on_state(self, message):
        now = time.monotonic()
        for name, position in zip(message.name, message.position):
            if math.isfinite(position):
                self.positions[name], self.received[name] = position, now

    def on_description(self, message):
        self.description = message.data

    def wait_until(self, predicate, label, timeout=None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while rclpy.ok() and time.monotonic() < deadline:
            if predicate():
                return
            rclpy.spin_once(self, timeout_sec=0.1)
        raise RuntimeError(f'Timed out: {label}')

    def future_result(self, future, label, timeout=None):
        self.wait_until(future.done, label, timeout)
        result = future.result()
        if result is None:
            raise RuntimeError(f'No result: {label}')
        return result

    def ready(self, planning):
        self.wait_until(self.list_client.service_is_ready, 'controller manager service')
        wanted = {'joint_state_broadcaster', *self.mapping['controller_names']}
        deadline = time.monotonic() + self.timeout
        active = set()
        while time.monotonic() < deadline:
            result = self.future_result(self.list_client.call_async(ListControllers.Request()),
                                        'controller list', min(5.0, self.timeout))
            active = {controller.name for controller in result.controller if controller.state == 'active'}
            if wanted <= active:
                break
            rclpy.spin_once(self, timeout_sec=0.2)
        if not wanted <= active:
            raise RuntimeError(f'Inactive or missing controllers: {sorted(wanted - active)}')
        for controller in result.controller:
            if controller.name in self.mapping['controller_names']:
                expected = {f'{name}/position' for name in self.mapping[controller.name]['joints']}
                if set(controller.claimed_interfaces) != expected:
                    raise RuntimeError(f'{controller.name}: incorrect claimed command interfaces')
        self.wait_until(lambda: self.required <= self.positions.keys() and all(
            time.monotonic() - self.received[name] < 2.0 for name in self.required),
            'fresh finite positions for all eight controlled joints')
        self.wait_until(lambda: bool(self.description), '/robot_description')
        for name, client in self.trajectory_clients.items():
            self.wait_until(client.server_is_ready, f'{name} FollowJointTrajectory action')
        if planning:
            self.wait_until(self.move_client.server_is_ready, '/move_action')
        print('PASS: four active controllers, eight claimed position interfaces, all eight joint states, robot description, '
              'three trajectory action servers' + (', and MoveGroup.' if planning else '.'), flush=True)

    def move(self, group, target):
        goal = MoveGroup.Goal()
        request = goal.request
        request.group_name = group
        request.pipeline_id = 'ompl'
        request.num_planning_attempts = 3
        request.allowed_planning_time = 5.0
        request.max_velocity_scaling_factor = 0.2
        request.max_acceleration_scaling_factor = 0.2
        request.start_state.is_diff = True
        constraint = Constraints()
        for name, value in target.items():
            constraint.joint_constraints.append(JointConstraint(
                joint_name=name, position=float(value), tolerance_above=0.0001,
                tolerance_below=0.0001, weight=1.0))
        request.goal_constraints = [constraint]
        goal.planning_options.plan_only = False
        goal.planning_options.planning_scene_diff.is_diff = True
        goal.planning_options.planning_scene_diff.robot_state.is_diff = True
        handle = self.future_result(self.move_client.send_goal_async(goal), f'{group} goal acceptance')
        if not handle.accepted:
            raise RuntimeError(f'{group}: MoveGroup rejected the goal')
        try:
            result = self.future_result(handle.get_result_async(), f'{group} execution', 60.0)
        except Exception:
            self.future_result(handle.cancel_goal_async(), 'cancel timed out motion', 5.0)
            raise
        if result.status != GoalStatus.STATUS_SUCCEEDED or result.result.error_code.val != MoveItErrorCodes.SUCCESS:
            raise RuntimeError(f'{group}: status={result.status}, MoveIt error={result.result.error_code.val}')
        self.wait_until(lambda: all(abs(self.positions.get(name, math.inf) - value) < 0.001
                                   for name, value in target.items()), f'{group} measured endpoint', 5.0)

    def exercise_mock(self):
        model = ET.fromstring(self.description)
        plugins = [item.text for item in model.findall('ros2_control/hardware/plugin')]
        if plugins != ['mock_components/GenericSystem']:
            raise RuntimeError('--execute is restricted to backend:=mock; no movement was requested.')
        if any(name == 'servo_node' for name, _namespace in self.get_node_names_and_namespaces()):
            raise RuntimeError('Stop the Servo launch before testing MoveGroup execution.')
        # Small joint targets test MoveGroup -> correct action -> ros2_control -> measured feedback.
        for group, controller, distance in [('arm', 'arm_controller', 0.05),
                                             ('Gripper', 'gripper_controller', 0.005),
                                             ('Solenoid', 'solenoid_controller', 0.003)]:
            names = self.mapping[controller]['joints']
            start = {name: self.positions[name] for name in names}
            target = dict(start)
            for name in names[:1] if group == 'arm' else names:
                joint = model.find(f"joint[@name='{name}']")
                limit = joint.find('limit')
                if joint.get('type') == 'continuous':
                    target[name] += distance
                else:
                    low, high = float(limit.get('lower')), float(limit.get('upper'))
                    target[name] += distance if start[name] < (low + high) / 2 else -distance
            self.move(group, target)
            self.move(group, start)
            print(f'PASS: {group} planned, executed, reported its endpoint, and returned.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--controllers-only', action='store_true')
    parser.add_argument('--execute', action='store_true', help='Move each group and return; mock backend only')
    parser.add_argument('--timeout', type=float, default=30.0)
    args = parser.parse_args(remove_ros_args(sys.argv)[1:])
    if args.execute and args.controllers_only:
        parser.error('--execute requires MoveGroup; omit --controllers-only')
    rclpy.init()
    node = PipelineCheck(args.timeout)
    try:
        node.ready(planning=not args.controllers_only)
        if args.execute:
            node.exercise_mock()
        return 0
    except (RuntimeError, KeyboardInterrupt) as error:
        print(f'FAIL: {error}', file=sys.stderr, flush=True)
        return 1
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    sys.exit(main())
