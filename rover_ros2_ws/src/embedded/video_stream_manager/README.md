# video_stream_manager

Encodes rover camera images to H.264 and publishes them to the rover's MediaMTX.

## Build and run

```bash
colcon build --symlink-install --packages-up-to video_stream_manager
source install/setup.bash
ros2 run video_stream_manager stream_manager_node --ros-args -r image:=/zed/zed_node/rgb/color/rect/image
```

See [docs/rover/video-stream-manager.md](../../../../docs/rover/video-stream-manager.md) for parameters, testing without a camera, and how it fits in the video pipeline.
