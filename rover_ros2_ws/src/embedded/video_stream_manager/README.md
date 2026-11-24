# video_stream_manager

Encodes rover camera images to H.264 and publishes them to the rover's MediaMTX.

## Build and run

```bash
colcon build --symlink-install --packages-up-to rover_bringup
source install/setup.bash
ros2 launch rover_bringup video.launch.py   # with the ZED wrapper, in one container
```

See [docs/rover/video-stream-manager.md](../../../../docs/rover/video-stream-manager.md) for parameters, testing without a camera, and how it fits in the video pipeline.
