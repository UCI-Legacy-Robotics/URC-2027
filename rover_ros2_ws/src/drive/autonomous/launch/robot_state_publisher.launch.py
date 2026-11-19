"""Publish the rover's joint transforms for autonomous bringup."""

from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    description_share = Path(get_package_share_directory('drive_description'))
    urdf_path = description_share / 'urdf' / 'drive_description.urdf'

    return LaunchDescription([
        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            parameters=[{'robot_description': urdf_path.read_text(encoding='utf-8')}],
            output='screen',
        ),
    ])
