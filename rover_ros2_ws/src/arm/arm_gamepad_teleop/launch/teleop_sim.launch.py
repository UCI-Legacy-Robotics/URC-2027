from pathlib import Path
import xacro
import yaml
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import EmitEvent, DeclareLaunchArgument, OpaqueFunction, LogInfo, IncludeLaunchDescription, RegisterEventHandler
from launch.event_handlers import OnProcessExit
from launch.events import Shutdown
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue

# Settings
MOVEIT_PACKAGE = "moveit_config"
SRDF_FILE = "arm.srdf"
USE_SIM_TIME = True


def load_yaml(path):
    with open(path, encoding="utf-8") as file:
        return yaml.safe_load(file) or {}


def launch_setup(context):
    # Package directories
    arm_package = Path(get_package_share_directory("arm"))
    control_package = Path(get_package_share_directory("arm_control_config"))
    teleop_package = Path(get_package_share_directory("arm_gamepad_teleop"))
    moveit_package = Path(get_package_share_directory(MOVEIT_PACKAGE))

    # File paths
    gazebo_launch_file = arm_package / "launch" / "gazebo_control.launch.py"

    xacro_file = control_package / "models" / "arm.sim.urdf.xacro"
    controllers_file = control_package / "config" / "ros2_controllers.yaml"

    controller = LaunchConfiguration("controller").perform(context)
    teleop_file = teleop_package / "config" / f"teleop_{controller}.yaml"
    servo_package = Path(get_package_share_directory("moveit_servo"))
    if not (servo_package / "JAZZY_SUB6_PATCH.md").is_file():
        raise RuntimeError(
            "Patched moveit_servo is not selected. Source your base workspace, "
            "then this bundle's install/local_setup.bash. "
            f"Currently selected: {servo_package}"
        )
    servo_file = teleop_package / "config" / "servo.yaml"

    srdf_file = moveit_package / "config" / SRDF_FILE
    kinematics_file = moveit_package / "config" / "kinematics.yaml"
    limits_file = moveit_package / "config" / "joint_limits.yaml"

    # Use the same xacro and arguments as gazebo_control.
    robot_description = xacro.process_file(
        str(xacro_file),
        mappings={"controllers_file": str(controllers_file)},
    ).toxml()

    robot_semantic = srdf_file.read_text(encoding="utf-8")
    kinematics = load_yaml(kinematics_file)
    joint_limits = load_yaml(limits_file)
    servo_settings = load_yaml(servo_file)

    # MoveIt expects floating-point values for these limits.
    for limits in joint_limits.get("joint_limits", {}).values():
        for key in ("max_velocity", "max_acceleration", "max_jerk"):
            if key in limits:
                limits[key] = float(limits[key])

    # Existing Gazebo launch handles simulation, robot_state_publisher,
    # and your manually configured controllers.
    gazebo_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(str(gazebo_launch_file)),
    )

    joy_node = Node(
        package='joy',
        executable='joy_node',
        name='joy_node',
        parameters=[{
            'device_id': 0,
            'deadzone': 0.1,
            'autorepeat_rate': 30.0,
        }],
        output='screen',
    )

    # MoveIt Servo converts velocity commands into joint trajectories.
    servo_node = Node(
        package="moveit_servo",
        executable="servo_node",
        name="servo_node",
        arguments=["--ros-args", "--log-level", LaunchConfiguration("servo_log_level")],
        output="screen",
        parameters=[
            {
                "robot_description": ParameterValue(
                    robot_description,
                    value_type=str,
                ),
                "robot_description_semantic": ParameterValue(
                    robot_semantic,
                    value_type=str,
                ),
                "robot_description_kinematics": kinematics,
                "robot_description_planning": joint_limits,
                "moveit_servo": servo_settings,
                "use_sim_time": USE_SIM_TIME,
            }
        ],
    )

    # Teleop converts joystick input into Servo velocity commands.
    teleop_node = Node(
        package="arm_gamepad_teleop",
        executable="arm_teleop",
        name="arm_teleop",
        output="screen",
        parameters=[
            str(teleop_file),
            {"use_sim_time": USE_SIM_TIME},
        ],
    )

    # Stop the launch if Servo or teleop exits.
    # Normal controller-spawner exits must not trigger shutdown.
    stop_on_exit = RegisterEventHandler(
        OnProcessExit(
            target_action=lambda action: action in (servo_node, teleop_node),
            on_exit=[
                EmitEvent(
                    event=Shutdown(reason="Teleop or Servo exited")
                )
            ],
        )
    )

    ld = LaunchDescription()

    ld.add_action(joy_node)
    ld.add_action(stop_on_exit)
    ld.add_action(gazebo_launch)
    ld.add_action(servo_node)
    ld.add_action(teleop_node)

    return [LogInfo(msg=f"Using patched Servo from: {servo_package}")] + list(ld.entities)


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument("controller", default_value="playstation", choices=["playstation", "xbox"]),
        DeclareLaunchArgument("servo_log_level", default_value="info", choices=["debug", "info", "warn", "error"]),
        OpaqueFunction(function=launch_setup),
    ])
