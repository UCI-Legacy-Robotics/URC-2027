# drive_description

## Run

```bash
colcon build --symlink-install --packages-up-to drive_description
source install/setup.bash
ros2 launch drive_description display.launch.py   # RViz2 + joint sliders
ros2 launch drive_description gazebo.launch.py    # Gazebo Harmonic, spawn only
```

## Model

- Frames follow REP 103: x forward (steering end), y left, z up. `base_footprint` is on the ground under `base_link`.
- `base_link` is the chassis. Each rocker, bogie, and steering knuckle has its frame on its pivot bore, and each wheel's frame is at its hub.
- With all joints at zero, the rover stands on flat ground with all six wheels touching and the front wheels pointing straight. The CAD pose had the bogies and steering at arbitrary angles, so the zero pose was redefined.
- `right_rocker_joint` mimics `left_rocker_joint` with a multiplier of -1 to model the differential. Drive the left rocker only.

## Known gaps

- The rocker (±0.6 rad), bogie (±0.6 rad), and steering (±0.8 rad) limits are placeholders, not the real hardware stops.
- The chassis mass from the export is 159 kg, which is likely a wrong material assignment in SolidWorks.
- The differential bar is missing because it wasn't in the SolidWorks export. Only its pivot post on the chassis is there. The mimic joint models what the bar does.
- The right rocker's bogie-pivot clamp blocks were missing from the export. They're mirrored copies of the left rocker's blocks.
- Meshes are simplified with 2 mm vertex clustering (max vertex shift 2.5 mm). Full-detail meshes are in the original export on SharePoint.
- Collision geometry reuses the visual meshes, which are slow in Gazebo. There are no drive plugins yet.
