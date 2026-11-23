from pathlib import Path

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import (AppendEnvironmentVariable, DeclareLaunchArgument, EmitEvent,
                            IncludeLaunchDescription, LogInfo, OpaqueFunction, RegisterEventHandler)
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from moveit_configs_utils import MoveItConfigsBuilder


def launch_setup(context):
    backend = LaunchConfiguration('backend').perform(context)
    use_sim_time = backend == 'gazebo'
    planning = LaunchConfiguration('planning').perform(context) == 'true'
    show_rviz = LaunchConfiguration('rviz').perform(context) == 'true'
    config_package = Path(get_package_share_directory('moveit_config'))
    control_package = Path(get_package_share_directory('arm_control_config'))
    controllers_file = str(control_package / 'config' / 'ros2_controllers.yaml')

    config = (MoveItConfigsBuilder('arm', package_name='moveit_config')
              .robot_description(file_path='config/arm.urdf.xacro',
                                 mappings={'use_gazebo': str(use_sim_time).lower()})
              .robot_description_semantic(file_path='config/arm.srdf')
              .robot_description_kinematics(file_path='config/kinematics.yaml')
              .joint_limits(file_path='config/joint_limits.yaml')
              .trajectory_execution(file_path='config/moveit_controllers.yaml',
                                    moveit_manage_controllers=False)
              .planning_pipelines(pipelines=['ompl'], default_planning_pipeline='ompl')
              .planning_scene_monitor(publish_robot_description=False,
                                      publish_robot_description_semantic=True)
              .to_moveit_configs())
    config.robot_description['robot_description'] = ParameterValue(
        config.robot_description['robot_description'], value_type=str)
    config.robot_description_semantic['robot_description_semantic'] = ParameterValue(
        config.robot_description_semantic['robot_description_semantic'], value_type=str)
    clock = {'use_sim_time': use_sim_time}

    rsp = Node(package='robot_state_publisher', executable='robot_state_publisher',
               parameters=[config.robot_description, clock], output='screen')
    spawner = Node(
        package='controller_manager', executable='spawner', output='screen',
        arguments=['joint_state_broadcaster', 'arm_controller', 'gripper_controller',
                   'solenoid_controller', '--controller-manager', '/controller_manager',
                   '--param-file', controllers_file, '--controller-manager-timeout', '120',
                   '--service-call-timeout', '30', '--switch-timeout', '30',
                   '--activate-as-group', '--controller-ros-args',
                   f'--ros-args -p use_sim_time:={str(use_sim_time).lower()}'])

    ready_actions = []
    if planning:
        ready_actions.append(Node(package='moveit_ros_move_group', executable='move_group',
                                  parameters=[config.to_dict(), clock], output='screen'))
    if show_rviz:
        rviz_file = config_package / 'config' / ('moveit.rviz' if planning else 'robot.rviz')
        ready_actions.append(Node(package='rviz2', executable='rviz2',
                                  arguments=['-d', str(rviz_file)],
                                  parameters=[config.to_dict(), clock], output='screen'))

    def after_controllers(event, _context):
        if event.returncode != 0:
            return [EmitEvent(event=Shutdown(reason='Controller activation failed; see spawner output.'))]
        return [LogInfo(msg='All three joint controllers and the state broadcaster are active.'),
                *ready_actions]

    actions = [RegisterEventHandler(OnProcessExit(target_action=spawner, on_exit=after_controllers)), rsp]
    if not use_sim_time:
        # Jazzy's controller manager subscribes to the description published by rsp.
        manager = Node(package='controller_manager', executable='ros2_control_node',
                       parameters=[controllers_file, clock],
                       remappings=[('robot_description', '/robot_description')], output='screen')
        actions += [RegisterEventHandler(OnProcessExit(
            target_action=manager,
            on_exit=[EmitEvent(event=Shutdown(reason='Controller manager exited.'))])), manager, spawner]
    else:
        # gz_ros2_control creates the only controller manager in this mode.
        arm_package = Path(get_package_share_directory('arm'))
        gz_package = Path(get_package_share_directory('ros_gz_sim'))
        gazebo = IncludeLaunchDescription(
            PythonLaunchDescriptionSource(str(gz_package / 'launch' / 'gz_sim.launch.py')),
            launch_arguments={'gz_args': LaunchConfiguration('gz_args'), 'on_exit_shutdown': 'true'}.items())
        spawn = Node(package='ros_gz_sim', executable='create',
                     arguments=['-name', 'arm', '-topic', '/robot_description'], output='screen')

        def after_spawn(event, _context):
            if event.returncode != 0:
                return [EmitEvent(event=Shutdown(reason='Gazebo could not spawn the arm.'))]
            return [spawner]

        actions += [AppendEnvironmentVariable('GZ_SIM_RESOURCE_PATH', str(arm_package.parent)),
                    gazebo,
                    Node(package='ros_gz_bridge', executable='parameter_bridge',
                         arguments=['/clock@rosgraph_msgs/msg/Clock[gz.msgs.Clock'], output='screen'),
                    RegisterEventHandler(OnProcessExit(target_action=spawn, on_exit=after_spawn)), spawn]
    return actions


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('backend', default_value='mock', choices=['mock', 'gazebo']),
        DeclareLaunchArgument('planning', default_value='true', choices=['true', 'false']),
        DeclareLaunchArgument('rviz', default_value='true', choices=['true', 'false']),
        DeclareLaunchArgument('gz_args', default_value='-r empty.sdf'),
        OpaqueFunction(function=launch_setup),
    ])
