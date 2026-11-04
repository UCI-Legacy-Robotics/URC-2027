# drive_description

## Run

```bash
colcon build --symlink-install --packages-up-to drive_description
source install/setup.bash
ros2 launch drive_description display.launch.py   # RViz2 + joint sliders
ros2 launch drive_description gazebo.launch.py    # Gazebo Harmonic, spawn only
```

## Model

- Frames follow REP 103: x forward (steering end), y left, z up.
- `base_link` is the massless root link. Nav2, robot_localization and KDL all expect it at the root with no inertia. `base_footprint` is a child of it, on the ground directly below.
- `chassis_link` is the chassis body (mass, meshes) and sits at `base_link` with zero offset. The rockers attach to it. Each rocker, bogie, and steering knuckle has its frame on its pivot bore, and each wheel's frame is at its hub.
- With all joints at zero, the rover stands on flat ground with all six wheels touching and the front wheels pointing straight. The CAD pose had the bogies and steering at arbitrary angles, so the zero pose was redefined.
- `right_rocker_joint` mimics `left_rocker_joint` with a multiplier of -1 to model the differential. Drive the left rocker only.

- Sensor frames hang off `chassis_link`:
  - `imu_link` is the standalone IMU.
  - `gnss_link` is the GNSS antenna phase center. Use it as the NavSatFix `frame_id`.
  - `zed_camera_link` is the ZED 2i on its pole on the rear half, pitched down. Run zed-ros2-wrapper with `camera_name:=zed` and `publish_urdf:=true` so its optical and IMU frames attach under this link.

## Known gaps

- The sensor poses (`imu_link`, `gnss_link`, `zed_camera_link`) are placeholders. This includes the camera pole height and its downward tilt. Measure them on the hardware.

- The rocker (±0.6 rad), bogie (±0.6 rad), and steering (±0.8 rad) limits are placeholders, not the real hardware stops.
- The chassis mass from the export is 159 kg, which is likely a wrong material assignment in SolidWorks.
- The differential bar is missing because it wasn't in the SolidWorks export. Only its pivot post on the chassis is there. The mimic joint models what the bar does.
- The right rocker's bogie-pivot clamp blocks were missing from the export. They're mirrored copies of the left rocker's blocks.
- Meshes are simplified with 2 mm vertex clustering (max vertex shift 2.5 mm). Full-detail meshes are in the original export on SharePoint.
- Collision geometry reuses the visual meshes, which are slow in Gazebo. There are no drive plugins yet.
