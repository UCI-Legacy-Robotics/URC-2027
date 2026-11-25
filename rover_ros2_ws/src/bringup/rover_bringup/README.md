# rover_bringup

Top-level launch files and configs for the rover.

## Run

```bash
colcon build --symlink-install --packages-up-to rover_bringup
source install/setup.bash
ros2 launch rover_bringup video.launch.py                    # ZED + stream manager (Jetson)
ros2 launch rover_bringup video.launch.py test_source:=true  # test images, no ZED
ros2 launch rover_bringup video.launch.py arm_camera:=true   # also the arm OV9281
ros2 launch rover_bringup front_camera.launch.py             # one camera on its own
ros2 launch rover_bringup arm_camera.launch.py
```

See [docs/rover/video-stream-manager.md](../../../../docs/rover/video-stream-manager.md) for the video launch and its configs.
