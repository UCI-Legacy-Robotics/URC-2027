"""Rover video: ZED wrapper + video_stream_manager in one camera container.

Both run in one multi-threaded component container with intra-process
communication, so ZED frames reach the stream manager without serialization.
MediaMTX is not started here: it runs on its own (see docs/rover/video-stream-manager.md).

    ros2 launch rover_bringup video.launch.py
    ros2 launch rover_bringup video.launch.py test_source:=true   # no ZED needed
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, Shutdown
from launch.conditions import IfCondition, UnlessCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution, PythonExpression
from launch_ros.actions import ComposableNodeContainer, LoadComposableNodes
from launch_ros.descriptions import ComposableNode
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare

# The ZED launch file loads into /<camera_name>/<container_name>, so the
# container lives in the camera's namespace.
CAMERA_NAME = 'zed'
CONTAINER_NAME = 'camera_container'
CONTAINER = f'/{CAMERA_NAME}/{CONTAINER_NAME}'

ZED_IMAGE_TOPIC = f'/{CAMERA_NAME}/zed_node/rgb/color/rect/image'  # left camera
TEST_IMAGE_TOPIC = '/test/image'


def _shutdown_on_container_exit(event, context):
    # End the launch if the camera process dies, so a supervisor can restart it.
    # Not while the launch is already shutting down (Ctrl+C): a second shutdown
    # raises an exception.
    if getattr(context, 'is_shutdown', False):
        return None
    return [Shutdown(reason='camera container exited')]


def generate_launch_description():
    config = PathJoinSubstitution([FindPackageShare('rover_bringup'), 'config'])
    test_source = LaunchConfiguration('test_source')
    intra_process = LaunchConfiguration('intra_process')
    use_intra_process = [{'use_intra_process_comms': ParameterValue(intra_process, value_type=bool)}]

    image_topic = PythonExpression([
        f"'{TEST_IMAGE_TOPIC}' if '", test_source, f"' == 'true' else '{ZED_IMAGE_TOPIC}'"])

    stream_manager = ComposableNode(
        package='video_stream_manager',
        plugin='video_stream_manager::StreamManager',
        name='video_stream_manager',
        parameters=[PathJoinSubstitution([config, 'video_stream_manager.yaml'])],
        remappings=[('image', image_topic)],
        extra_arguments=use_intra_process,
    )

    container = ComposableNodeContainer(
        name=CONTAINER_NAME,
        namespace=CAMERA_NAME,
        package='rclcpp_components',
        executable='component_container_mt',
        arguments=[
            '--ros-args', '--log-level', LaunchConfiguration('log_level'),
            # cam2image logs every frame it publishes.
            '--log-level', 'test.test_camera:=warn',
        ],
        output='screen',
        composable_node_descriptions=[stream_manager],
        on_exit=_shutdown_on_container_exit,
    )

    # The ZED wrapper's own launch file, loading into our container. It also
    # starts a robot_state_publisher for the camera's frames.
    zed = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([FindPackageShare('zed_wrapper'), 'launch', 'zed_camera.launch.py'])),
        launch_arguments={
            'camera_model': LaunchConfiguration('camera_model'),
            'camera_name': CAMERA_NAME,
            'container_name': CONTAINER_NAME,
            'enable_ipc': intra_process,
            'ros_params_override_path': PathJoinSubstitution([config, 'zed_video.yaml']),
            # Positional tracking is off, and odometry will come from elsewhere.
            'publish_tf': 'false',
            'publish_map_tf': 'false',
        }.items(),
        condition=UnlessCondition(test_source),
    )

    # Moving test images instead of the ZED, for machines without the camera or SDK.
    test_camera = LoadComposableNodes(
        target_container=CONTAINER,
        composable_node_descriptions=[ComposableNode(
            package='image_tools',
            plugin='image_tools::Cam2Image',
            name='test_camera',
            namespace='test',
            # Intra-process needs keep-last history; cam2image defaults to keep-all.
            parameters=[{'burger_mode': True, 'frequency': 30.0, 'history': 'keep_last'}],
            extra_arguments=use_intra_process,
        )],
        condition=IfCondition(test_source),
    )

    return LaunchDescription([
        DeclareLaunchArgument(
            'camera_model', default_value='zed2i',
            description='ZED model, passed to the ZED wrapper.'),
        DeclareLaunchArgument(
            'test_source', default_value='false', choices=['true', 'false'],
            description='Stream test images instead of the ZED. No ZED SDK needed.'),
        DeclareLaunchArgument(
            'intra_process', default_value='true', choices=['true', 'false'],
            description='Pass frames in memory between the camera nodes. false copies them '
                        'through DDS like separate processes, for debugging.'),
        DeclareLaunchArgument(
            'log_level', default_value='info',
            description='Log level for the camera container, e.g. debug.'),
        container,
        zed,
        test_camera,
    ])
