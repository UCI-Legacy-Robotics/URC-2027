#!/usr/bin/env python3
"""Offline checker regression tests. ROS transports are stubbed, not integration-tested."""
import ast
from contextlib import redirect_stdout
import io
import math
from pathlib import Path
import time
from types import SimpleNamespace as NS
import unittest
import xml.etree.ElementTree as ET

import yaml

ROOT = Path(__file__).resolve().parents[1]


class FakeNode:
    def __init__(self, name):
        pass

    def create_subscription(self, *args):
        pass

    def create_client(self, *args):
        return NS(service_is_ready=lambda: True, call_async=lambda request: None)

    def get_node_names_and_namespaces(self):
        return []


class FakeActionClient:
    def __init__(self, node, kind, topic):
        self.topic = topic

    def server_is_ready(self):
        return True


# Execute the unmodified production class, replacing only its ROS dependencies.
# This keeps these tests runnable without importing/installing ROS message packages.
source = ROOT / 'moveit_config/scripts/check_pipeline.py'
class_node = next(node for node in ast.parse(source.read_text()).body if isinstance(node, ast.ClassDef))
namespace = dict(Node=FakeNode, Path=Path, get_package_share_directory=lambda name: str(ROOT / name),
    yaml=yaml, ActionClient=FakeActionClient, FollowJointTrajectory=object, JointState=object,
    String=object, QoSProfile=lambda **kwargs: None, DurabilityPolicy=NS(TRANSIENT_LOCAL=1),
    ListControllers=NS(Request=lambda: None), MoveGroup=object, time=time, math=math, ET=ET,
    rclpy=NS(ok=lambda: True, spin_once=lambda *args, **kwargs: None))
exec(compile(ast.Module(body=[class_node], type_ignores=[]), str(source), 'exec'), namespace)
PipelineCheck = namespace['PipelineCheck']


class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.node = PipelineCheck(0.01)
        self.node.positions = {joint: 0.0 for joint in self.node.required}
        self.node.received = {joint: time.monotonic() for joint in self.node.required}
        self.node.description = (ROOT / 'arm/description/arm.urdf').read_text()
        self.controllers = [NS(name='joint_state_broadcaster', state='active')]
        # Independent expected runtime response: six position and two velocity claims.
        for name in self.node.control_names:
            interface = 'velocity' if name == 'gripper_controller' else 'position'
            params = self.node.controllers[name]['ros__parameters']
            self.controllers.append(NS(name=name, state='active', type=self.node.controller_types[name],
                claimed_interfaces=[f'{joint}/{interface}' for joint in params['joints']]))
        self.node.future_result = lambda *args: NS(controller=self.controllers)
        self.output = io.StringIO()
        self.redirect = redirect_stdout(self.output)
        self.redirect.__enter__()
        self.addCleanup(self.redirect.__exit__, None, None, None)

    def test_velocity_gripper_ready_without_trajectory_action(self):
        self.assertEqual(set(self.node.trajectory_clients), {'arm_controller', 'solenoid_controller'})
        self.assertEqual(len(self.node.required), 8)
        self.node.ready(False)
        self.assertIn('2 trajectory action servers', self.output.getvalue())

    def test_wrong_interfaces_fail_with_diagnostics(self):
        self.controllers[2].claimed_interfaces = ['claw_to_left_grip/position', 'claw_to_right_grip/position']
        with self.assertRaisesRegex(RuntimeError, 'expected=.*velocity.*actual=.*position'):
            self.node.ready(False)

    def test_inactive_gripper_is_required_even_without_moveit_mapping(self):
        self.controllers[2].state = 'inactive'
        with self.assertRaisesRegex(RuntimeError, 'Inactive or missing controllers.*gripper_controller'):
            self.node.ready(False)

    def test_missing_gripper_joint_feedback_fails(self):
        del self.node.positions['claw_to_left_grip']
        with self.assertRaisesRegex(RuntimeError, 'fresh finite positions'):
            self.node.ready(False)

    def test_stale_feedback_fails(self):
        self.node.received['claw_to_left_grip'] -= 3
        with self.assertRaisesRegex(RuntimeError, 'fresh finite positions'):
            self.node.ready(False)

    def test_missing_arm_action_fails(self):
        self.node.trajectory_clients['arm_controller'].server_is_ready = lambda: False
        with self.assertRaisesRegex(RuntimeError, 'arm_controller FollowJointTrajectory'):
            self.node.ready(False)

    def test_movegroup_required_only_for_planning(self):
        self.node.move_client.server_is_ready = lambda: False
        self.node.ready(False)
        with self.assertRaisesRegex(RuntimeError, '/move_action'):
            self.node.ready(True)

    def test_mock_exercise_tests_arm_solenoid_and_explicitly_skips_gripper(self):
        model = ET.fromstring(self.node.description)
        hardware = ET.SubElement(ET.SubElement(model, 'ros2_control'), 'hardware')
        ET.SubElement(hardware, 'plugin').text = 'mock_components/GenericSystem'
        self.node.description = ET.tostring(model, encoding='unicode')
        requested = []
        self.node.move = lambda group, target: requested.append(group)
        self.node.exercise_mock()
        self.assertEqual(requested, ['arm', 'arm', 'Solenoid', 'Solenoid'])
        self.assertIn('SKIP: Gripper', self.output.getvalue())

    def test_non_mock_exercise_refuses_before_motion(self):
        self.node.move = lambda *args: self.fail('Motion requested')
        with self.assertRaisesRegex(RuntimeError, 'restricted to backend:=mock'):
            self.node.exercise_mock()


if __name__ == '__main__':
    unittest.main()
