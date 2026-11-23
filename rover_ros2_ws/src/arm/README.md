# Arm, gripper, and solenoid

ROS 2 Jazzy / Gazebo Harmonic stack with five arm joints, two gripper joints,
and one solenoid joint. Includes the patched five-DOF MoveIt Servo. Physical
motor drivers are not included; motion limits below are simulation defaults.

## Build from GitHub

Clone this repository or fetch the PR branch into your existing checkout, then:

```bash
cd rover_ros2_ws
source /opt/ros/jazzy/setup.bash
rosdep install --from-paths src/arm --ignore-src -r -y --rosdistro jazzy
colcon build --symlink-install --packages-up-to arm_gamepad_teleop --allow-overriding moveit_servo
source install/setup.bash
```

Build the included `moveit_servo`; the teleop launch checks its patch marker to
catch accidentally selecting the stock package. Source this workspace in each
terminal. Keep backups outside `src` to avoid duplicate packages. Run only one
bringup at a time.

## Gamepad teleoperation

```bash
ros2 launch arm_gamepad_teleop teleop_sim.launch.py controller:=playstation5
```

This defaults to Gazebo. Append `backend:=mock` for mock hardware, `rviz:=false`
for no RViz, or `gz_args:='-r -s empty.sdf'` for headless Gazebo. Servo, teleop,
and the joystick driver start only after all four controllers and eight joint
states are ready. A failed readiness check stops the launch. MoveGroup is not
started, because its arm trajectories would compete with Servo's commands.

The PS5 profile retains the supplied device-specific `joy_node` mapping:

| Control | Input |
| --- | --- |
| Arm enable | Button 9; release at neutral to arm, then hold to move |
| Joint/Cartesian mode | Button 6, at neutral with enable released |
| Arm stick axes | 0, 1, 2, 3 |
| Wrist − / + | Buttons 13 / 14 |
| Cartesian yaw + / − | Buttons 11 / 12 |
| Open / close gripper | L2 axis 4 / R2 axis 5 |

Verify indices with `ros2 topic echo /joy` if your device/driver differs.
Triggers must report +1 released and −1 pressed. The gripper is independent of
the arm enable button and Servo mode: hold one trigger for constant speed;
both pressed or neither pressed stops. Release both triggers after startup,
invalid input, or stale input/feedback before moving again. Stale joystick or
joint-state data commands zero velocity while teleop remains running.

`gripper_enabled` is explicitly true only in the PS5 profile. PS4 and Xbox
retain their arm mappings, with gripper control disabled until dedicated trigger
indices are configured. Enabling overlapping arm and trigger axes is rejected.
A disabled gripper does not publish commands.

The gripper cruises at **0.015 m/s** per finger, below the URDF's 0.02 m/s limit.
It slows over the last **10 mm** and stops **1 mm inside** the configured 0–30 mm
travel (approximately 1.1–28.9 mm with the 0.1 mm stopping tolerance). Each finger
stops independently and stays stopped while that direction is held, even if
feedback jitters. Reverse immediately with the opposite trigger; there is no
extra release requirement at an endpoint. Releasing a single held trigger sends
zero in the joystick callback as well as the regular 50 Hz command heartbeat.

These defaults avoid commanding exactly at Gazebo's speed or position limits,
where velocity-controlled joints have been reported to get stuck
([upstream issue](https://github.com/gazebosim/gz-sim/issues/1684)). This is a
mitigation, not a patch to the physics engine. Fully restart the simulation after
updating, since a joint already stuck in the solver may not recover in place.

`gripper_limit_margin`, `gripper_slowdown_distance`, and `gripper_speed` configure
this behavior. Speed is capped at 0.018 m/s to keep 10% headroom, margin must be
at least 1 mm, and slowdown distance must cover `speed * (joy_timeout + 0.02)`.
The 30 mm closing setting comes from the existing `partially_closed` named state.
The URDF's 50 mm stroke is unchanged, but that target intersects the finger meshes.
Partial `/joint_states` messages are accumulated per finger with independent
freshness checks; unrelated arm feedback cannot disable or refresh the fingers.
These sampled software checks are not a hardware limit mechanism.

Direct gripper velocity commands bypass MoveIt's trajectory collision checking.
The 30 mm preset avoids the known finger-to-finger intersection; it does not
establish collision-free motion for every arm pose or external obstacle.
A forwarding controller retains its last command: the teleop watchdog is not a
hardware watchdog. The simulation launch shuts down if teleop/Servo/joy exits.

## Planning and mock checks

Stop teleop before starting planning:

```bash
ros2 launch moveit_config bringup.launch.py backend:=mock
```

In RViz, `arm` and `Solenoid` support **Plan & Execute**. `Gripper` remains in
the robot model, end-effector definition, named states, and planning groups,
but has no MoveIt execution action with the velocity-forwarding configuration.
Do not use **Execute** for that group. For manual gripper motion use teleop.

In another sourced terminal:

```bash
# Read-only readiness, including gripper activation/velocity claims and feedback.
ros2 run moveit_config check_pipeline.py

# Mock only: move and restore arm and solenoid; explicitly reports Gripper as skipped.
ros2 run moveit_config check_pipeline.py --execute
```

`--execute` refuses non-mock backends and a running Servo. It is not a gripper
motion test. A failed action is not guaranteed to restore its starting position.
`--controllers-only` omits the MoveGroup check and is used by teleop startup.

For Gazebo planning:

```bash
ros2 launch moveit_config bringup.launch.py backend:=gazebo
```

The Gazebo plugin creates the only controller manager; `/clock` is bridged for
simulation time. World obstacles are not automatically copied into MoveIt's
planning scene. Use `check_pipeline.py` without `--execute` on this backend.

## Control interfaces

| Group | Joints | Command path | Command interface |
| --- | --- | --- | --- |
| `arm` | Five arm joints | `/arm_controller/follow_joint_trajectory` (planning) or `/arm_controller/joint_trajectory` (Servo) | position |
| `Gripper` | `claw_to_left_grip`, `claw_to_right_grip` | `/gripper_controller/commands`, `std_msgs/Float64MultiArray` in that joint order | velocity |
| `Solenoid` | `grip_to_solenoid` | `/solenoid_controller/follow_joint_trajectory` | position |

All eight joints expose position and velocity **state** interfaces. The state
broadcaster publishes `/joint_states`. The gripper is deliberately absent from
`moveit_controllers.yaml`; it is a ForwardCommandController, not a trajectory
action server. The offline and live validators still require both gripper joints.
Mock `calculate_dynamics` integrates velocity commands into position feedback.

The fingers remain independent. If real hardware mechanically couples them,
encode that relationship before hardware use. Solenoid position is a simulated
0–15 mm stroke; its trajectory controller does not switch a real coil. Arm
limits are 1 rad/s, 2 rad/s² and 100 N·m; finger limits are 0.02 m/s, 0.1 m/s²
and 40 N; solenoid limits are 0.05 m/s, 0.2 m/s² and 20 N. These are unmeasured
simulation settings. Bounded revolute joints retain ±π limits; continuous joints
remain continuous. Five DOF cannot satisfy arbitrary six-dimensional poses.

## Maintaining and testing

- Model/meshes/limits: `arm/description/arm.urdf`, `arm/meshes`.
- Control interfaces: `arm_control_config/models/arm.ros2_control.xacro`.
- Controller definitions: `arm_control_config/config/ros2_controllers.yaml`.
- MoveIt groups/mappings: `moveit_config/config/arm.srdf`, `moveit_controllers.yaml`.
- Trigger/control settings: `arm_gamepad_teleop/config/teleop_*.yaml`.

From `rover_ros2_ws` on a sourced Jazzy installation:

```bash
python3 src/arm/tests/validate_config.py
python3 -m unittest discover -s src/arm/tests -p 'test_*.py' -v
colcon test --packages-select arm_gamepad_teleop
colcon test-result --verbose
```

The teleop package includes a native control-logic test and a ROS topic test of
the actual executable. The latter injects synthetic Joy/JointState messages to
check release, reversal, endpoint jitter, partial feedback, and input/state
timeouts; it does not start Gazebo or test physics.

After rebuilding, restart `teleop_sim.launch.py` and test opening/closing to each
endpoint, immediately reversing, and releasing halfway through travel. If a
finger still moves after release, inspect the command stream in a sourced terminal:

```bash
ros2 topic echo /gripper_controller/commands
```

Both entries should become zero on release. If they are zero while the finger
keeps moving, the remaining problem is downstream of trigger processing (for
example simulation physics or another command publisher). Save `/joy`,
`/joint_states`, and this command topic together to distinguish those cases.

Optional sampled geometry checks require `numpy scipy trimesh python-fcl PyYAML`:
`python3 src/arm/tests/validate_geometry.py`. They are not exhaustive collision tests.

`.github/workflows/arm-validation.yml` runs offline checks, a Jazzy C++ build,
and headless mock arm/solenoid execution. It does not test a physical gamepad,
Gazebo dynamics, or actual hardware. See `VALIDATION.md` for what was run locally.
