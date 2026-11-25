"""Rover video: the front camera (ZED) and, optionally, the arm camera (OV9281).

Each camera runs in its own component container with its own
video_stream_manager (front_camera.launch.py, arm_camera.launch.py), so one
camera's process failing doesn't stop the other's stream.
MediaMTX is not started here: it runs on its own (see docs/rover/video-stream-manager.md).

    ros2 launch rover_bringup video.launch.py
    ros2 launch rover_bringup video.launch.py test_source:=true   # no ZED needed
    ros2 launch rover_bringup video.launch.py arm_camera:=true    # also the OV9281
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    launch_dir = PathJoinSubstitution([FindPackageShare('rover_bringup'), 'launch'])
    shared = {
        'intra_process': LaunchConfiguration('intra_process'),
        'log_level': LaunchConfiguration('log_level'),
    }

    front = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([launch_dir, 'front_camera.launch.py'])),
        launch_arguments={
            'camera_model': LaunchConfiguration('camera_model'),
            'test_source': LaunchConfiguration('test_source'),
            **shared,
        }.items(),
    )
    arm = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(PathJoinSubstitution([launch_dir, 'arm_camera.launch.py'])),
        launch_arguments={'device': LaunchConfiguration('arm_camera_device'), **shared}.items(),
        condition=IfCondition(LaunchConfiguration('arm_camera')),
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'camera_model', default_value='zed2i',
            description='ZED model, passed to the ZED wrapper.'),
        DeclareLaunchArgument(
            'test_source', default_value='false', choices=['true', 'false'],
            description='Stream test images instead of the ZED. No ZED SDK needed.'),
        DeclareLaunchArgument(
            'arm_camera', default_value='false', choices=['true', 'false'],
            description='Also stream the arm OV9281 camera to rover/arm.'),
        DeclareLaunchArgument(
            'arm_camera_device',
            default_value='/dev/v4l/by-id/usb-openaicam_openaicam_5423534724-video-index0',
            description='V4L2 device of the arm camera (arm_camera.launch.py device).'),
        DeclareLaunchArgument(
            'intra_process', default_value='true', choices=['true', 'false'],
            description='Pass frames in memory between each camera and its stream manager. '
                        'false copies them through DDS like separate processes, for debugging.'),
        DeclareLaunchArgument(
            'log_level', default_value='info',
            description='Log level for the camera containers, e.g. debug.'),
        front,
        arm,
    ])
