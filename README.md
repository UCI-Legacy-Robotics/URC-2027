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
└── frontend/             # Electron + React + TypeScript app (control) / browser viewer
mavlink/                  # MAVLink dialect XML shared by the rover and gui/backend
docs/                     # all docs, start at docs/README.md
```

The rover runs ROS 2. The base station doesn't: `gui/backend` decodes MAVLink from the radio and streams it to the GUI, which runs as an Electron app (the only control client) on the base laptop and as a read-only viewer in browsers on the base Wi-Fi.

## Docs

All docs are in [`docs/`](docs/README.md). Start with:

- [Rover setup](docs/setup/rover.md): Ubuntu/WSL, ROS 2 Jazzy, building the workspace
- [GUI setup](docs/setup/gui.md): native on macOS, Windows or Linux
- [System overview](docs/system/overview.md)

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a PR.
