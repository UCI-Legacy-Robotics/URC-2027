# Rover setup

Set up and build the ROS 2 workspace (`rover_ros2_ws`).

**ROS 2 distro:** Jazzy Jalisco (Ubuntu 24.04 Noble)

## Install

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

### Third-party packages (Jetson only)

Some packages aren't committed to this repo. [`rover.repos`](../../rover_ros2_ws/rover.repos) pins them to a version, and `vcs` (from `ros-dev-tools`) fetches them into `src/external/`, which is git-ignored. Right now that's only the [ZED ROS 2 wrapper](https://github.com/stereolabs/zed-ros2-wrapper). It needs the ZED SDK and CUDA, so only import it on the Jetson. On other machines, skip this and the workspace builds without it.

On the Jetson, before the build steps above:

```bash
cd rover_ros2_ws
vcs import src < rover.repos
```

The wrapper's version must match the installed ZED SDK (`v5.6.0` for SDK 5.6). When the SDK is upgraded, update the version in `rover.repos` and run `vcs import src < rover.repos` again.

The wrapper is a heavy build. On the 8 GB Orin Nano, limit parallel jobs so it doesn't run out of memory:

```bash
MAKEFLAGS=-j4 colcon build --symlink-install --parallel-workers 2 --cmake-args -DCMAKE_BUILD_TYPE=Release
```

## ROS domain

The rover runs with `ROS_DOMAIN_ID=42`, set in the Jetson's `~/.bashrc`. Without it, any machine on the rover's network on the default domain (0) joins the rover's ROS graph: it sees the rover's nodes, and a `ros2 topic echo` on a camera topic pulls frames over the 5 GHz link. The base station runs no ROS, so nothing there needs this domain.

For local tests on a laptop that's on the rover's network, use another domain, e.g. `export ROS_DOMAIN_ID=77`. To talk to the rover's ROS graph on purpose, use `42`.

## Workspace layout

```
rover_ros2_ws/src/
├── interfaces/       # shared custom msgs, srvs, actions
├── description/      # URDF/xacro, meshes, ros2_control tags
├── bringup/          # top-level launch + per-scenario configs
├── embedded/         # hardware drivers: motors, cameras, sensors, science payload
├── drive/            # teleop, ros2_control configs, Nav2
├── arm/              # teleop, ros2_control configs, MoveIt, autonomous pickup
├── science/          # science sequences, panorama
├── cv/               # gesture, keyboard, object detection, ArUco
├── rover_telemetry/  # package: data to base station, controls from base station
└── external/         # git-ignored: third-party packages from rover.repos
```

Each folder under `src/` groups related ROS 2 packages. colcon finds packages recursively, so put packages directly inside the matching folder, e.g. `src/drive/drive_teleop/package.xml`. Shared messages, services, and actions go in `src/interfaces/`.

## Checks

Before marking a PR ready, from `rover_ros2_ws/`:

```bash
colcon build --symlink-install --packages-up-to <your_package>
colcon test --packages-select <your_package>
colcon test-result --verbose
```

Make sure it builds, tests pass, and you've run it in sim or on hardware.
