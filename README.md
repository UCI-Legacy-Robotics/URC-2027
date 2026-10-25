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
gui/                      # base station GUI (no ROS)
├── backend/              # MAVLink <-> WebSocket server: owns the radio, logs telemetry
└── frontend/             # Vite + React + TypeScript dashboard
mavlink/                  # MAVLink dialect XML shared by the rover and gui/backend
docs/                     # subsystem docs
```

The rover runs ROS 2. The base station does not: `gui/backend` decodes MAVLink from the radio and streams it to the frontend over WebSockets. Both sides generate their MAVLink code from the dialect in `mavlink/`, so message changes go there first.

Each folder under `src/` groups related ROS 2 packages. colcon finds packages recursively, so put packages directly inside the matching folder, e.g. `src/drive/drive_teleop/package.xml`. Shared messages, services, and actions go in `src/interfaces/`.

## Setup

ROS 2 Jazzy requires **Ubuntu 24.04**. On Windows, install a 24.04 WSL distro:

```powershell
wsl --install -d Ubuntu-24.04
```

Inside Ubuntu 24.04, install ROS 2 Jazzy by following the [official guide](https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html) (`ros-jazzy-desktop` plus `ros-dev-tools`), then:

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

If a build works for you but not for others, check that every dependency is listed in the package's `package.xml`.

## Base station GUI

The base station GUI lives in `gui/` and doesn't need ROS. See [gui/README.md](gui/README.md) for setup and the tech stack, and [mavlink/README.md](mavlink/README.md) for the shared MAVLink dialect.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a PR.

## Docs

Software docs are in [`docs/`](docs/).