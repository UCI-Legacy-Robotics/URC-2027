# Validation record

Prepared 2026-11-19 from UCI-Legacy-Robotics/URC-2027 branch `aidan/arm/teleop` at `ffba676ef941e6ba903f8245c82eb896abc84bb9` and the supplied new MoveIt files.

## Passed locally

- Expanded both mock and Gazebo Xacros using the real Python Xacro processor and the Jazzy ament index implementation. Each has exactly one ros2_control system and eight joints. Only the Gazebo expansion has the Gazebo control plugin.
- Verified unique link/joint names, one connected world-rooted URDF tree, all mesh paths, positive joint velocity/effort limits, −π/+π limits for the three bounded revolute joints, and in-range startup/named positions.
- Verified exact joint coverage and exclusive command ownership across URDF, ros2_control, joint-limit YAML, SRDF groups, three JointTrajectoryControllers, all three MoveIt action mappings, and OMPL group configurations.
- Checked that only the serial arm group has an IK solver, and that the existing gamepad/Servo joint lists and output topic match its controller.
- Checked YAML duplicate keys, Python syntax, and absence of cycles in the five local package dependencies.
- Loaded the original collision STL meshes with trimesh/python-fcl and the URDF transforms, honoring the supplied SRDF collision exemptions. Verified positive-definite inertia tensors and 58 sampled initial/named/test configurations without unexpected self-collision or geometry below the ground plane (0.1 mm numerical tolerance).
- Reproduced finger-to-finger intersection at the supplied 50 mm full-stroke target. It is excluded from named targets; collision checking for that pair remains enabled.
- Compared file hashes: all 23 patched Servo package files, all 10 mesh files, and the gamepad C++ source match the repository exactly.

## Not run here

This environment has no ROS 2 or Gazebo runtime. The C++ packages were not compiled, ROS launch descriptions were not executed, and live planning, trajectory execution, clock behavior, and Gazebo dynamics were not tested. Static checks cannot establish those outcomes.

Run the build in README.md, start mock bringup, then run:

```bash
ros2 run moveit_config check_pipeline.py --execute
```

That command checks controller activation and claimed interfaces, all eight fresh joint positions, all three trajectory action servers, and MoveGroup availability. It then requests a small joint-space plan and execution for each group, checks the reported endpoint, and requests a return. This full-path test is implemented but was not executed during preparation. Gazebo remains a separate live check.

The retained SRDF collision exemptions and URDF joint axes originate in the user's configuration; the sampled checks are not an exhaustive geometric or physical model validation. Motion limits are labeled simulation defaults. No physical motor driver is included.
