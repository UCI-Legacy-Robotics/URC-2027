"""Read an Xbox gamepad and publish stamped rover velocity commands."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    device_id = ParameterValue(LaunchConfiguration('device_id'), value_type=int)
    use_sim_time = ParameterValue(LaunchConfiguration('use_sim_time'), value_type=bool)

    return LaunchDescription([
        DeclareLaunchArgument(
            'device_id', default_value='0',
            description='SDL device ID from ros2 run joy joy_enumerate_devices.'),
        DeclareLaunchArgument(
            'use_sim_time', default_value='false',
            description='Use the simulator clock for both nodes.'),
        Node(
            package='joy',
            executable='game_controller_node',
            name='game_controller',
            parameters=[{
                'device_id': device_id,
                'deadzone': 0.1,
                # Keep commands flowing while a stick is held steady.
                'autorepeat_rate': 20.0,
                'use_sim_time': use_sim_time,
            }],
            remappings=[('joy', '/joy')],
            output='screen',
        ),
        Node(
            package='teleop',
            executable='arcade',
            name='arcade',
            parameters=[{'use_sim_time': use_sim_time}],
            output='screen',
        ),
    ])
