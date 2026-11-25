"""Arm camera: OV9281 mjpeg_camera + video_stream_manager in one container, to rover/arm.

mjpeg_camera publishes the camera's JPEGs undecoded and the stream manager
decodes them (jpeg_input), both in one multi-threaded component container with
intra-process communication. It's a separate process from the ZED's container,
so a crash here can't stop the front stream. mjpeg_camera reopens the camera
itself when it fails or is unplugged. video.launch.py starts this with the
front camera; MediaMTX runs on its own (see docs/rover/video-stream-manager.md).

    ros2 launch rover_bringup arm_camera.launch.py
    ros2 launch rover_bringup arm_camera.launch.py device:=/dev/video2
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import ComposableNodeContainer
from launch_ros.descriptions import ComposableNode
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare

CAMERA_NAME = 'arm_camera'
DEVICE = '/dev/v4l/by-id/usb-openaicam_openaicam_5423534724-video-index0'


def generate_launch_description():
    params = PathJoinSubstitution([FindPackageShare('rover_bringup'), 'config', 'arm_camera.yaml'])
    use_intra_process = [{'use_intra_process_comms': ParameterValue(
        LaunchConfiguration('intra_process'), value_type=bool)}]

    camera = ComposableNode(
        package='mjpeg_camera',
        plugin='mjpeg_camera::MjpegCamera',
        name='camera',
        namespace=CAMERA_NAME,
        parameters=[params, {'device': LaunchConfiguration('device')}],
        extra_arguments=use_intra_process,
    )
    stream_manager = ComposableNode(
        package='video_stream_manager',
        plugin='video_stream_manager::StreamManager',
        name='video_stream_manager',
        namespace=CAMERA_NAME,
        parameters=[params],
        remappings=[('image/compressed', 'image_raw/compressed')],
        extra_arguments=use_intra_process,
    )
    container = ComposableNodeContainer(
        name='camera_container',
        namespace=CAMERA_NAME,
        package='rclcpp_components',
        executable='component_container_mt',
        arguments=['--ros-args', '--log-level', LaunchConfiguration('log_level')],
        output='screen',
        composable_node_descriptions=[camera, stream_manager],
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'device', default_value=DEVICE,
            description='V4L2 device of the arm camera. Prefer a /dev/v4l/by-id/ path: '
                        '/dev/videoN can change between boots.'),
        DeclareLaunchArgument(
            'intra_process', default_value='true', choices=['true', 'false'],
            description='Pass frames in memory between the camera nodes. false copies them '
                        'through DDS like separate processes, for debugging.'),
        DeclareLaunchArgument(
            'log_level', default_value='info',
            description='Log level for the camera container, e.g. debug.'),
        container,
    ])
