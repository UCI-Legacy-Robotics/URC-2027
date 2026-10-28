from pathlib import Path
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import AppendEnvironmentVariable, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

# Simply allows for visualization of the arm URDF file for debugging

def generate_launch_description():
    arm_package = Path(get_package_share_directory('arm'))
    urdf_path = arm_package / 'description' / 'arm.urdf'

    # Load the URDF as a string.
    robot_description = ParameterValue(
        urdf_path.read_text(),
        value_type=str,
    )

    return LaunchDescription([

        # Publish the robot model and link transforms.
        Node(
            package='robot_state_publisher',
            executable='robot_state_publisher',
            parameters=[{
                'robot_description': robot_description,
            }],
            output='screen',
        ),

        # Provide sliders that publish joint positions for RViz.
        Node(
            package='joint_state_publisher_gui',
            executable='joint_state_publisher_gui',
            output='screen',
        ),

        # Open RViz.
        Node(
            package='rviz2',
            executable='rviz2',
            output='screen',
        ),
    ])