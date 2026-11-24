# Rover ROS 2 workspace

ROS 2 Jazzy on Ubuntu 24.04 (WSL on Windows). Setup and layout are in `docs/setup/rover.md`; the system is in `docs/system/overview.md`.

Run colcon from `rover_ros2_ws/`, never the repo root, so `build/`, `install/` and `log/` land here where they're ignored. Source `/opt/ros/jazzy/setup.bash` (and `install/setup.bash` after a build) in the same shell call.

```bash
rosdep install --from-paths src --ignore-src -y
colcon build --symlink-install --packages-up-to <package>
colcon test --packages-select <package> && colcon test-result --verbose
```

- `src/` folders group packages by subsystem (`drive/`, `arm/`, `cv/`, …). Put a package directly inside its folder, e.g. `src/drive/drive_teleop/package.xml`.
- Shared msgs, srvs and actions go in `src/interfaces/`. Renaming a topic or a message field is a breaking change (`!` in the commit).
- List every dependency in `package.xml`; a build that works only on your machine usually means a missing one.
- The base station has no ROS. Anything that crosses the radio goes through `rover_telemetry` as MAVLink (`mavlink/rover.xml`), not ROS topics.
- On base link loss the rover decides for itself: in teleop it stops drive and arm; in autonomy it keeps going and logs locally. See the failsafe table in `docs/gui/architecture.md`.
- Say whether a change was run in sim, on hardware, or only built.
