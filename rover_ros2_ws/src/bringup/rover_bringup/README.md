# rover_bringup

Top-level launch files and configs for the rover.

## Run

```bash
colcon build --symlink-install --packages-up-to rover_bringup
source install/setup.bash
ros2 launch rover_bringup video.launch.py                    # ZED + stream manager (Jetson)
ros2 launch rover_bringup video.launch.py test_source:=true  # test images, no ZED
```

See [docs/rover/video-stream-manager.md](../../../../docs/rover/video-stream-manager.md) for the video launch and its configs.
