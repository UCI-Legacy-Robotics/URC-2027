# mjpeg_camera

Publishes a UVC camera's MJPEG frames, undecoded, as `sensor_msgs/CompressedImage`. Used for the arm OV9281.

## Build and run

```bash
colcon build --symlink-install --packages-up-to rover_bringup
source install/setup.bash
ros2 launch rover_bringup arm_camera.launch.py   # with its video_stream_manager, in one container
```

See [docs/rover/video-stream-manager.md](../../../../docs/rover/video-stream-manager.md) for parameters and how it fits in the video pipeline.
