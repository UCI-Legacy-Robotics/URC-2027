#!/usr/bin/env python3
"""Exercise the real teleop executable through ROS topics, without a gamepad/Gazebo."""
import os
import subprocess
import sys
import tempfile
import time
import unittest

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import JointState, Joy
from std_msgs.msg import Float64MultiArray


EXECUTABLE = sys.argv.pop(1)
os.environ['ROS_DOMAIN_ID'] = str(120 + os.getpid() % 80)
FINGERS = ['claw_to_left_grip', 'claw_to_right_grip']


class GripperRosTest(unittest.TestCase):
    def setUp(self):
        rclpy.init()
        self.node = Node('gripper_regression_test')
        self.joy = self.node.create_publisher(Joy, '/joy', 1)
        self.state = self.node.create_publisher(JointState, '/joint_states', 1)
        self.commands = []
        self.subscription = self.node.create_subscription(
            Float64MultiArray, '/gripper_controller/commands',
            lambda msg: self.commands.append(list(msg.data)), 1)
        self.log = tempfile.TemporaryFile(mode='w+')
        self.process = subprocess.Popen([
            EXECUTABLE, '--ros-args', '-p', 'gripper_enabled:=true',
            '-p', 'joy_timeout:=0.25'], stdout=self.log, stderr=subprocess.STDOUT)
        self.addCleanup(self.cleanup)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            rclpy.spin_once(self.node, timeout_sec=0.02)
            if self.joy.get_subscription_count() and self.state.get_subscription_count() and self.commands:
                return
            self.assertIsNone(self.process.poll(), 'arm_teleop exited before discovery')
        self.fail('Timed out waiting for the real teleop node')

    def cleanup(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        self.log.seek(0)
        print(self.log.read())
        self.log.close()
        self.node.destroy_node()
        rclpy.shutdown()

    def drive(self, open_trigger, close_trigger, positions=(0.015, 0.015), *,
              seconds=0.3, feedback='full', joystick=True):
        self.commands.clear()
        deadline = time.monotonic() + seconds
        tick = 0
        while time.monotonic() < deadline:
            if feedback != 'none':
                state = JointState()
                state.header.stamp = self.node.get_clock().now().to_msg()
                if feedback == 'split':
                    # Exercise both partial finger messages and unrelated arm feedback.
                    index = tick % 3
                    state.name = [FINGERS[index]] if index < 2 else ['shoulder_to_elbow']
                    state.position = [float(positions[index])] if index < 2 else [0.0]
                else:
                    state.name = FINGERS
                    state.position = list(positions)
                self.state.publish(state)
            if joystick:
                joy = Joy()
                joy.header.stamp = self.node.get_clock().now().to_msg()
                joy.axes = [0.0, 0.0, 0.0, 0.0, float(open_trigger), float(close_trigger)]
                joy.buttons = [0] * 15
                self.joy.publish(joy)
            tick += 1
            next_tick = time.monotonic() + 0.02
            while time.monotonic() < next_tick:
                rclpy.spin_once(self.node, timeout_sec=0.005)
            self.assertIsNone(self.process.poll(), 'arm_teleop exited during test')
        self.assertGreaterEqual(len(self.commands), 3, 'No command heartbeat')

    def expect_tail(self, expected):
        # Allow DDS/executor delivery before judging the settled command stream.
        for actual in self.commands[-3:]:
            self.assertEqual(len(actual), 2)
            for value, target in zip(actual, expected):
                self.assertAlmostEqual(value, target, places=8)

    def test_release_conflict_and_reverse(self):
        self.drive(1, -1)
        self.expect_tail([0, 0])  # Held at startup.
        self.drive(1, 1)
        self.drive(1, -1)
        self.expect_tail([0.015, 0.015])
        self.drive(1, 1)
        self.expect_tail([0, 0])
        self.drive(-1, 1)
        self.expect_tail([-0.015, -0.015])
        self.drive(-1, -1)
        self.expect_tail([0, 0])

    def test_endpoint_latch_and_immediate_reversal(self):
        self.drive(1, 1)
        self.drive(1, -1, (0.029, 0.015))
        self.expect_tail([0, 0.015])
        self.drive(1, -1, (0.0285, 0.015))
        self.expect_tail([0, 0.015])
        self.drive(-1, 1, (0.0285, 0.015))
        self.expect_tail([-0.015, -0.015])
        self.drive(-1, 1, (0.001, 0.001))
        self.expect_tail([0, 0])
        self.drive(1, -1, (0.001, 0.001))
        self.expect_tail([0.015, 0.015])

    def test_partial_joint_states(self):
        self.drive(1, 1, feedback='split')
        self.drive(1, -1, feedback='split', seconds=0.6)
        self.expect_tail([0.015, 0.015])
        self.drive(1, 1, feedback='split')
        self.expect_tail([0, 0])

    def test_input_and_feedback_watchdogs(self):
        for feedback, joystick in [('full', False), ('none', True)]:
            self.drive(1, 1)
            self.drive(1, -1)
            self.expect_tail([0.015, 0.015])
            self.drive(1, -1, seconds=0.6, feedback=feedback, joystick=joystick)
            self.expect_tail([0, 0])
            self.drive(1, -1)
            self.expect_tail([0, 0])  # Recovery cannot restart a held trigger.
            self.drive(1, 1)
            self.drive(1, -1)
            self.expect_tail([0.015, 0.015])


if __name__ == '__main__':
    unittest.main()
