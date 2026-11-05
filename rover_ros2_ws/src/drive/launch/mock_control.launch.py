"""Start mock hardware and six-wheel differential-drive controllers."""
from pathlib import Path
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node
import xacro

def generate_launch_description():
    share = Path(get_package_share_directory('drive_description'))
    description = xacro.process_file(
        str(share / 'description/drive_description.urdf'),
        mappings={'hardware_plugin': 'mock_components/GenericSystem'}).toxml()
    return LaunchDescription([
        Node(package='robot_state_publisher', executable='robot_state_publisher',
             parameters=[{'robot_description': description, 'use_sim_time': False}],
             output='screen'),
        Node(package='controller_manager', executable='ros2_control_node',
             parameters=[str(share / 'config/rover_controllers.yaml')],
             remappings=[('~/robot_description', '/robot_description')], output='screen'),
        Node(package='controller_manager', executable='spawner',
             arguments=['joint_broad', 'diff_cont', '--controller-manager',
                        '/controller_manager', '--controller-manager-timeout', '30'],
             output='screen'),
    ])
