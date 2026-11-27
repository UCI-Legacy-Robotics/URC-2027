# drive_description

## Run

```bash
colcon build --symlink-install --packages-up-to drive_description
source install/setup.bash
ros2 launch drive_description display.launch.py   # RViz2 + joint sliders
ros2 launch drive_description gazebo.launch.py    # Gazebo Harmonic, spawn only
```

See [docs/rover/drive-description.md](../../../../docs/rover/drive-description.md) for the frames, joints and known gaps.
