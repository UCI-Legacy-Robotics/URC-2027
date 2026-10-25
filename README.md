# URC-2027

UCI Legacy Robotics rover software for URC 2027.

**ROS 2 distro:** Jazzy Jalisco (Ubuntu 24.04 Noble)

## Layout

```
rover_ros2_ws/
└── src/
    ├── interfaces/       # shared custom msgs, srvs, actions
    ├── description/      # URDF/xacro, meshes, ros2_control tags
    ├── bringup/          # top-level launch + per-scenario configs
    ├── embedded/         # hardware drivers: motors, cameras, sensors, science payload
    ├── drive/            # teleop, ros2_control configs, Nav2
    ├── arm/              # teleop, ros2_control configs, MoveIt, autonomous pickup
    ├── science/          # science sequences, panorama
    ├── cv/               # gesture, keyboard, object detection, ArUco
    └── rover_telemetry/  # package: data to base station, controls from base station
```

Each folder under `src/` groups related ROS 2 packages. colcon finds packages
recursively, so put packages directly inside the matching folder, e.g.
`src/drive/drive_teleop/package.xml`.

## Setup

ROS 2 Jazzy requires **Ubuntu 24.04**. On Windows, install a 24.04 WSL distro:

```powershell
wsl --install -d Ubuntu-24.04
```

Inside Ubuntu 24.04, install ROS 2 Jazzy by following the
[official guide](https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html)
(`ros-jazzy-desktop` plus `ros-dev-tools`), then:

```bash
sudo rosdep init   # first time only
rosdep update
echo "source /opt/ros/jazzy/setup.bash" >> ~/.bashrc
```

## Build

```bash
cd rover_ros2_ws
rosdep install --from-paths src --ignore-src -y
colcon build --symlink-install
source install/setup.bash
```
