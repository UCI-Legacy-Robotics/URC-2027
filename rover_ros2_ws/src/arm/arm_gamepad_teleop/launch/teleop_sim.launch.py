from pathlib import Path
import yaml

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import (DeclareLaunchArgument, EmitEvent, IncludeLaunchDescription,
                            OpaqueFunction, RegisterEventHandler)
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
    clock = {'use_sim_time': use_sim_time}
    package = Path(get_package_share_directory('arm_gamepad_teleop'))
    moveit_package = Path(get_package_share_directory('moveit_config'))
    servo_package = Path(get_package_share_directory('moveit_servo'))
    if not (servo_package / 'JAZZY_SUB6_PATCH.md').is_file():
        raise RuntimeError(f'Source this workspace to select patched five-DOF Servo; selected: {servo_package}')
    controller = LaunchConfiguration('controller').perform(context)
    config = (MoveItConfigsBuilder('arm', package_name='moveit_config')
              .robot_description(file_path='config/arm.urdf.xacro',
                                 mappings={'use_gazebo': str(use_sim_time).lower()})
              .robot_description_semantic(file_path='config/arm.srdf')
              .robot_description_kinematics(file_path='config/kinematics.yaml')
              .joint_limits(file_path='config/joint_limits.yaml')
              .planning_pipelines(pipelines=['ompl'], default_planning_pipeline='ompl')
              .to_moveit_configs())
    config.robot_description['robot_description'] = ParameterValue(
        config.robot_description['robot_description'], value_type=str)
    config.robot_description_semantic['robot_description_semantic'] = ParameterValue(
        config.robot_description_semantic['robot_description_semantic'], value_type=str)
    servo_settings = yaml.safe_load((package / 'config' / 'servo.yaml').read_text())
    servo = Node(package='moveit_servo', executable='servo_node', name='servo_node',
                 parameters=[config.robot_description, config.robot_description_semantic,
                             config.robot_description_kinematics, config.joint_limits,
                             {'moveit_servo': servo_settings}, clock],
                 arguments=['--ros-args', '--log-level', LaunchConfiguration('servo_log_level')],
                 output='screen')
    teleop = Node(package='arm_gamepad_teleop', executable='arm_teleop', name='arm_teleop',
                  parameters=[str(package / 'config' / f'teleop_{controller}.yaml'), clock], output='screen')
    joy = Node(package='joy', executable='joy_node', parameters=[{
        'device_id': 0, 'deadzone': 0.1, 'autorepeat_rate': 30.0}, clock], output='screen')
    ready = Node(package='moveit_config', executable='check_pipeline.py',
                 arguments=['--controllers-only', '--timeout', '120'], output='screen')

    def after_ready(event, _context):
        if event.returncode != 0:
            return [EmitEvent(event=Shutdown(reason='Control pipeline did not become ready.'))]
        return [servo, teleop, joy]

    return [
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(str(moveit_package / 'launch' / 'bringup.launch.py')),
            launch_arguments={'backend': backend, 'planning': 'false',
                              'rviz': LaunchConfiguration('rviz'),
                              'gz_args': LaunchConfiguration('gz_args')}.items()),
        RegisterEventHandler(OnProcessExit(target_action=ready, on_exit=after_ready)),
        RegisterEventHandler(OnProcessExit(target_action=lambda action: action in (servo, teleop),
            on_exit=[EmitEvent(event=Shutdown(reason='Servo or teleop exited.'))])),
        ready,
    ]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('backend', default_value='gazebo', choices=['mock', 'gazebo']),
        DeclareLaunchArgument('controller', default_value='playstation5',
                              choices=['playstation5', 'playstation4', 'xbox']),
        DeclareLaunchArgument('rviz', default_value='true', choices=['true', 'false']),
        DeclareLaunchArgument('gz_args', default_value='-r empty.sdf'),
        DeclareLaunchArgument('servo_log_level', default_value='info', choices=['debug', 'info', 'warn', 'error']),
        OpaqueFunction(function=launch_setup),
    ])
