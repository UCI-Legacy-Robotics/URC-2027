import os
import xacro
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import AppendEnvironmentVariable, EmitEvent, IncludeLaunchDescription, LogInfo, RegisterEventHandler
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    ld = LaunchDescription()

    arm_package = get_package_share_directory('arm')
    control_package = get_package_share_directory('arm_control_config')
    gazebo_package = get_package_share_directory('ros_gz_sim')

    xacro_path = os.path.join(control_package, 'models', 'arm.sim.urdf.xacro')
    controllers_path = os.path.join(control_package, 'config', 'ros2_controllers.yaml')
    gazebo_launch_path = os.path.join(gazebo_package, 'launch', 'gz_sim.launch.py')

    for required_file in (xacro_path, controllers_path):
        if not os.path.isfile(required_file):
            raise FileNotFoundError(f'Required installed file is missing: {required_file}')

    robot_document = xacro.process_file(xacro_path, mappings={'controllers_file': controllers_path})
    robot_description = ParameterValue(robot_document.toxml(), value_type=str)
    set_env_resources = AppendEnvironmentVariable('GZ_SIM_RESOURCE_PATH', os.path.dirname(arm_package))

    gazebo_launch_object = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(gazebo_launch_path),
        launch_arguments={'gz_args': '-r empty.sdf'}.items())

    clock_bridge_node = Node(
        package='ros_gz_bridge',
        executable='parameter_bridge',
        arguments=['/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock'],
        output='screen')

    robot_state_pub_node = Node(
        package='robot_state_publisher',
        executable='robot_state_publisher',
        parameters=[{'robot_description': robot_description, 'use_sim_time': True}],
        output='screen')

    spawn_arm_node = Node(
        package='ros_gz_sim',
        executable='create',
        parameters=[{'robot_description': robot_description}],
        arguments=[
            '-name', 'arm', '-param', 'robot_description',
            '-x', '0.0', '-y', '0.0', '-z', '0.3',
            '-R', '0.0', '-P', '0.0', '-Y', '0.0'
        ],
        output='screen')

    controller_spawner = Node(
        package='controller_manager',
        executable='spawner',
        arguments=[
            'joint_state_broadcaster', 'arm_controller', 'solenoid_controller',
            '--controller-manager', '/controller_manager',
            '--controller-manager-timeout', '60'
        ],
        output='screen')

    def after_spawn(event, context):
        if event.returncode != 0:
            return [EmitEvent(event=Shutdown(reason='Arm entity spawning failed'))]

        return [
            LogInfo(msg='Arm spawned; waiting for its controller manager.'),
            controller_spawner]

    controller_start_handler = RegisterEventHandler(
        OnProcessExit(target_action=spawn_arm_node, on_exit=after_spawn))

    rviz_node = Node(
        package='rviz2',
        executable='rviz2',
        parameters=[{'use_sim_time': True}],
        output='screen')

    ld.add_action(set_env_resources)
    ld.add_action(gazebo_launch_object)
    ld.add_action(clock_bridge_node)
    ld.add_action(robot_state_pub_node)
    ld.add_action(controller_start_handler)
    ld.add_action(spawn_arm_node)
    ld.add_action(rviz_node)

    return ld
