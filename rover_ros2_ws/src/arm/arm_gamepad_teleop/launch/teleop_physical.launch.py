from pathlib import Path
import xml.etree.ElementTree as ET

import xacro
import yaml

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, EmitEvent, OpaqueFunction, RegisterEventHandler
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def check_hardware_description(description, controller_config, controllers):
    model = ET.fromstring(description)
    components = model.findall('ros2_control')
    plugins = [component.findtext('hardware/plugin', '').strip() for component in components]
    if not plugins or any(not plugin or plugin.startswith((
            'mock_components/', 'fake_components/', 'gz_ros2_control/', 'gazebo_ros2_control/'
    )) for plugin in plugins):
        raise RuntimeError('robot_description_file must select physical ros2_control plugins.')

    joints = {joint.attrib['name']: joint for component in components
              for joint in component.findall('joint')}
    missing = []
    for controller in controllers:
        config = controller_config[controller]['ros__parameters']
        commands = config.get('command_interfaces') or [config['interface_name']]
        states = config.get('state_interfaces', ['position'])
        for name in config['joints']:
            joint = joints.get(name)
            if joint is None:
                missing.append(name)
                continue
            for tag, required in [('command_interface', commands), ('state_interface', states)]:
                declared = {interface.attrib['name'] for interface in joint.findall(tag)}
                missing.extend(f'{name}/{interface} ({tag})'
                               for interface in required if interface not in declared)
    if missing:
        raise RuntimeError('Full teleop requires hardware interfaces for: ' + ', '.join(missing))


def launch_setup(context):
    teleop_share = Path(get_package_share_directory('arm_gamepad_teleop'))
    moveit_config = Path(get_package_share_directory('moveit_config')) / 'config'
    control_share = Path(get_package_share_directory('arm_control_config'))
    servo_share = Path(get_package_share_directory('moveit_servo'))
    if not (servo_share / 'JAZZY_SUB6_PATCH.md').is_file():
        raise RuntimeError(f'Source the workspace with patched five-DOF MoveIt Servo: {servo_share}')

    description_file = LaunchConfiguration('robot_description_file').perform(context)
    description = xacro.process_file(str(Path(description_file).expanduser())).toxml()
    controllers_file = control_share / 'config' / 'ros2_controllers.yaml'
    controllers = ['arm_controller', 'gripper_controller', 'solenoid_controller']
    check_hardware_description(description, yaml.safe_load(controllers_file.read_text()), controllers)

    robot_description = {'robot_description': ParameterValue(description, value_type=str)}
    semantic = {'robot_description_semantic': ParameterValue(
        (moveit_config / 'arm.srdf').read_text(), value_type=str)}
    kinematics = {'robot_description_kinematics': yaml.safe_load(
        (moveit_config / 'kinematics.yaml').read_text())}
    planning_limits = {'robot_description_planning': yaml.safe_load(
        (moveit_config / 'joint_limits.yaml').read_text())}
    servo_settings = {'moveit_servo': yaml.safe_load(
        (teleop_share / 'config' / 'servo.yaml').read_text())}
    clock = {'use_sim_time': False}

    state_publisher = Node(package='robot_state_publisher', executable='robot_state_publisher',
                           parameters=[robot_description, clock], output='screen')
    manager = Node(package='controller_manager', executable='ros2_control_node',
                   parameters=[str(controllers_file), clock],
                   remappings=[('robot_description', '/robot_description')], output='screen')
    spawner = Node(package='controller_manager', executable='spawner', output='screen',
                   arguments=['joint_state_broadcaster', *controllers,
                              '--controller-manager', '/controller_manager',
                              '--param-file', str(controllers_file), '--activate-as-group',
                              '--controller-manager-timeout', '120',
                              '--service-call-timeout', '30', '--switch-timeout', '30'])

    controller = LaunchConfiguration('controller').perform(context)
    servo = Node(package='moveit_servo', executable='servo_node', name='servo_node',
                 parameters=[robot_description, semantic, kinematics, planning_limits,
                             servo_settings, clock], output='screen')
    teleop = Node(package='arm_gamepad_teleop', executable='arm_teleop', name='arm_teleop',
                  parameters=[str(teleop_share / 'config' / f'teleop_{controller}.yaml'), clock],
                  output='screen')
    joy = Node(package='joy', executable='joy_node',
               parameters=[{'device_id': 0, 'deadzone': 0.1, 'autorepeat_rate': 30.0}, clock],
               output='screen')

    def after_spawner(event, _context):
        if event.returncode != 0:
            return [EmitEvent(event=Shutdown(reason='Physical controller activation failed.'))]
        return [servo, teleop, joy]

    return [
        RegisterEventHandler(OnProcessExit(target_action=spawner, on_exit=after_spawner)),
        RegisterEventHandler(OnProcessExit(
            target_action=lambda action: action in (manager, servo, teleop, joy),
            on_exit=[EmitEvent(event=Shutdown(reason='A hardware teleop process exited.'))])),
        state_publisher, manager, spawner,
    ]


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('robot_description_file',
                              description='Physical arm URDF or Xacro with ros2_control plugins.'),
        DeclareLaunchArgument('controller', default_value='playstation5',
                              choices=['playstation5', 'playstation4', 'xbox']),
        OpaqueFunction(function=launch_setup),
    ])
