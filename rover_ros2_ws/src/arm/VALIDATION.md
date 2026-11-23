# Gripper endpoint and release correction — 2026-11-23

Follow-up to `5316a2a` on `codex/arm/fix-velocity-gripper`.

Reproduced in the old production helper: at q=0.0002 m an opening command is
-0.02 m/s, so one 20 ms update crosses the hard lower limit. At the upper endpoint,
a 0.1 mm feedback fluctuation restarts full speed while the trigger remains held.
The previous state callback also invalidates both fingers on an arm-only message.
These are reproducible code defects; they do not prove the user's installed
Gazebo/DART version is the cause of every reported motion after release.

Changes: cruise at 0.015 m/s, retain 1 mm endpoint margins, taper over 10 mm,
latch each finger's endpoint until the requested direction changes, send release
commands directly from the joystick callback, keep only the latest queued Joy,
and track feedback/freshness separately for each finger. Velocity interfaces,
URDF hard limits, and the arm's joystick mapping are unchanged.

Passed locally:

- Native production-helper tests, compiled as C++17 with warnings as errors and
  AddressSanitizer/UndefinedBehaviorSanitizer. LeakSanitizer was disabled because
  it cannot run under this environment's tracing; other sanitizer checks passed.
- Release/conflicting-trigger stops, direct reversal even from outside the range,
  independent endpoint latches, autorepeat and feedback jitter, stale/malformed
  input, partial feedback, individual finger expiration, and frozen feedback near
  both endpoints.
- Twenty open/close cycles with 80 ms sampled feedback and 50 Hz commands stayed
  inside the guarded travel and stopped at both ends. This uses kinematic
  integration, not Gazebo physics.
- Real mock/Gazebo Xacro/configuration checks, Python syntax, nine existing
  pipeline regressions, and `git diff --check`.

Added a CTest ROS integration test that launches the actual `arm_teleop`
executable, injects Joy/JointState messages, and observes gripper commands. It
covers release, reversal, endpoints, partial feedback, and both watchdogs. The
existing Jazzy CI job runs it via `colcon test`; it was NOT run locally because
this environment has no ROS/Gazebo runtime. No new Gazebo or controller-version
verification is claimed. Fully restart simulation after rebuilding and test both
endpoints, reversal, and mid-travel release on the actual PS5 controller.

---

# Velocity gripper correction — 2026-11-23

Based on `aidan/arm/teleop` at `33bf4abf058b67af2a9d8c185f704bba78955fe7`.

The previous record below describes the older all-position stack. Its three-action
and all-group execution claims do not describe the current velocity gripper.

Current local checks:

- Real Xacro expansion for mock and Gazebo, unique YAML keys, Python syntax,
  package dependency graph, eight-joint ownership, six position/two velocity
  interfaces, and the two supported MoveIt action mappings.
- Nine offline checker regressions using stubbed ROS transports, including the
  reported startup failure, missing/stale feedback, missing actions, and mock-only
  execution. These do not test ROS transport or message import compatibility.
- C++17 compilation of the production gripper logic with warnings as errors;
  release-to-arm, trigger conflicts, independent endpoints, stale input/state,
  and invalid-number cases.
- Mesh/inertia validation passed for 58 sampled configurations; the 50 mm
  finger-to-finger collision was reproduced. The default closing target is now
  the existing 30 mm preset.

No ROS/Gazebo runtime is installed in this editing environment. The full C++ node,
patched Servo, ROS launches, and physical joystick mapping were not run locally.
The added GitHub Actions workflow is intended to build on Jazzy and run mock
arm/solenoid execution; its outcome must be checked on the PR. `--execute` now
explicitly skips the direct-command gripper. Test PS5 gripper motion in simulation
before marking the PR ready. The PR should remain a draft until these checks pass.

---

# Historical validation record (2026-11-19)

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
