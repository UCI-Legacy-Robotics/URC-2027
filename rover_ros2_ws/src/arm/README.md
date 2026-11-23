# Arm + gripper + solenoid integration

Replacement for `URC-2027/rover_ros2_ws/src/arm`, targeting **ROS 2 Jazzy and Gazebo Harmonic**. Includes the five existing packages, meshes, and the existing patched five-DOF MoveIt Servo.

Based on branch `aidan/arm/teleop`, commit `ffba676ef941e6ba903f8245c82eb896abc84bb9`, and the supplied new MoveIt configuration. The physical model includes both fingers, the camera, and the solenoid attached to the **left** finger. The three bounded revolute joints have limits of −π to +π; the original two continuous joints remain continuous.

## Install

Extract the ZIP. Replace the entire old `src/arm` directory with the extracted `arm_stack` directory, naming the destination `arm`. Do not merge the directories: that would retain obsolete launches/configuration. Keep your backup outside `src` so colcon does not discover duplicate packages.

For an archive extracted to `~/Downloads/arm_stack`:

```bash
cd ~/URC-2027/rover_ros2_ws
mv src/arm "$HOME/arm-stack-backup-$(date +%Y%m%d-%H%M%S)"
cp -a ~/Downloads/arm_stack src/arm
source /opt/ros/jazzy/setup.bash
rosdep install --from-paths src/arm --ignore-src -r -y --rosdistro jazzy
colcon build --symlink-install --packages-up-to arm_gamepad_teleop --allow-overriding moveit_servo
source install/setup.bash
```

Use a terminal sourced with this workspace for every command below. Build the included `moveit_servo`; the teleop launch checks its patch marker to catch accidentally selecting the stock package.

## First run: MoveIt and mock ros2_control

```bash
ros2 launch moveit_config bringup.launch.py backend:=mock
```

In RViz, select `arm`, `Gripper`, or `Solenoid` in the MotionPlanning panel. Set a joint target or named state, then use **Plan & Execute**. The tool groups intentionally use joint targets and do not have Cartesian IK solvers.

In another sourced terminal:

```bash
# Read-only: verifies activation, claimed interfaces, joint feedback, and action servers.
ros2 run moveit_config check_pipeline.py

# Mock only: plans and executes a small motion for each group, checks feedback, and returns it.
ros2 run moveit_config check_pipeline.py --execute
```

The second command tests the complete MoveGroup → trajectory action → controller → joint feedback path for each of the three groups. It refuses to move a Gazebo or physical backend. On failure it stops with the failing group/error; a failed motion is not guaranteed to return to its starting state. There are no physical hardware plugins in this bundle.

## Gazebo

Stop the mock launch before starting Gazebo:

```bash
ros2 launch moveit_config bringup.launch.py backend:=gazebo
```

For a headless Gazebo server, append `gz_args:='-r -s empty.sdf'`; append `rviz:=false` to omit RViz. The same three groups, actions, and controller YAML are used. A `/clock` bridge supplies simulation time. The Gazebo plugin creates the controller manager; the launch does not create a second one.

Run `ros2 run moveit_config check_pipeline.py` to check the live Gazebo control interfaces. Use RViz to plan and execute on this backend. The automated moving test is intentionally mock-only. Gazebo world obstacles are not automatically copied into MoveIt's planning scene; add matching collision objects when planning around the floor or other objects.

## Existing gamepad teleop

Stop the planning launch, then run:

```bash
ros2 launch arm_gamepad_teleop teleop_sim.launch.py controller:=playstation5
```

This defaults to Gazebo; `backend:=mock` also works. `playstation4` and `xbox` retain their existing mappings. The gamepad C++ node and patched Servo implementation are unchanged. Servo starts only after the controllers and all eight joint states are ready. This launch uses a robot-only RViz display and does not start MoveGroup: the existing gamepad node continuously publishes arm commands, which would compete with planned arm trajectories.

The gamepad still commands the five arm joints. Both tool controllers run and expose their own trajectory actions; no unrequested button mappings were invented. For planned tool motion, use the planning launch above. Run only one arm bringup at a time.

## Group-to-controller mapping

Group capitalization matches the supplied SRDF. Controller names are lowercase.

| Planning group | Controlled joints | FollowJointTrajectory action |
| --- | --- | --- |
| `arm` | `base_mount_to_upper_base`, `upper_base_to_shoulder`, `shoulder_to_elbow`, `elbow_to_wrist`, `wrist_to_claw` | `/arm_controller/follow_joint_trajectory` |
| `Gripper` | `claw_to_left_grip`, `claw_to_right_grip` | `/gripper_controller/follow_joint_trajectory` |
| `Solenoid` | `grip_to_solenoid` | `/solenoid_controller/follow_joint_trajectory` |

All eight joints expose a **position command** interface plus **position and velocity state** interfaces. `joint_state_broadcaster` publishes their feedback on `/joint_states`; robot_state_publisher publishes the transforms. MoveIt uses the Simple Controller Manager to send action goals. Only the arm trajectory controller accepts nonzero final velocity, as required by the existing Servo output.

The solenoid is a separate planning group controlling the URDF's 0–15 mm prismatic stroke. It is not registered as a second nested end effector. The `Gripper` group remains the arm's end effector. Every link still participates in the robot model and collision checking regardless of which group is being planned.

## Presets, limits, and model assumptions

| Group | Named states |
| --- | --- |
| `arm` | `home`: all five joints at zero |
| `Gripper` | `open`: both joints at 0 m; `partially_closed`: both at 0.03 m |
| `Solenoid` | `retracted`: 0 m; `extended`: 0.015 m |

**The supplied finger geometry intersects when both joints reach 0.05 m.** Mesh checks show about 15 mm minimum finger clearance at 0.03 m, about 2.2 mm at 0.04 m, and intersection by 0.045 m. The original 0–0.05 m joint limits are retained; MoveIt must reject colliding configurations. The full-stroke target is not offered as a preset, and finger-to-finger collision checking remains enabled. Reconcile the physical stroke/axes with CAD before changing those limits or allowing contact.

The two finger joints remain independent, matching the supplied URDF. Selecting a common named state commands equal strokes. If one actuator mechanically couples the fingers, encode the actual mimic/transmission relationship before hardware use; this bundle does not assume that relationship.

Zero effort/velocity limits were replaced with explicit simulation defaults:

| Joint set | URDF maximum velocity | MoveIt maximum acceleration | URDF effort limit |
| --- | --- | --- | --- |
| Five arm joints | 1 rad/s | 2 rad/s² | 100 N·m |
| Two fingers | 0.02 m/s | 0.1 m/s² | 40 N |
| Solenoid | 0.05 m/s | 0.2 m/s² | 20 N |

Default MoveIt velocity/acceleration scaling is 0.2. These values are **simulation configuration values, not measured hardware ratings**. The existing Servo speed settings are preserved. The repository does not supply a physical driver for these eight joints: real motor commands, feedback, limits, finger coupling, and electrical solenoid activation still require that hardware implementation. A trajectory controller models solenoid position; it does not itself switch a real solenoid coil.

The arm group is now the serial chain `base_mount` → `claw`; the camera branch and world anchoring joint are excluded from its IK chain. KDL is configured only for that group. Five DOF cannot independently satisfy arbitrary position plus three-axis orientation goals; use reachable poses or joint targets. The existing Servo patch does not change this physical limitation.

## Files to maintain

| Purpose | File |
| --- | --- |
| Physical model, geometry, hard joint limits | `arm/description/arm.urdf` |
| All eight ros2_control joints and backend selection | `arm_control_config/models/arm.ros2_control.xacro` |
| Shared model wrapper | `arm_control_config/models/arm.sim.urdf.xacro` |
| Gazebo plugin | `arm_control_config/models/arm.gazebo.xacro` |
| Controller definitions and joint lists | `arm_control_config/config/ros2_controllers.yaml` |
| Startup joint positions for both backends | `arm_control_config/config/initial_positions.yaml` |
| Groups, end effector, named states, collision exemptions | `moveit_config/config/arm.srdf` |
| MoveIt action/controller mapping | `moveit_config/config/moveit_controllers.yaml` |
| Planning limits and planners | `moveit_config/config/joint_limits.yaml`, `ompl_planning.yaml` |
| Single planning/control launch | `moveit_config/launch/bringup.launch.py` |

The MoveIt URDF wrapper includes the canonical control model; there is no separate copy of its eight control interfaces. Both backends use the same controller YAML. Controller parameters are explicitly passed to the spawner for Jazzy, and time settings are passed to each loaded controller. Startup failures halt the launch instead of silently leaving a partially controlled arm.

The redundant generated rsp, move_group, controller, warehouse, and static-TF launch files were removed. `demo.launch.py` remains a small alias for bringup, and `setup_assistant.launch.py` remains for editing the configuration. `arm_display.launch.py` is display-only; do not run its joint-state GUI beside a control launch.

## Validation performed

See `VALIDATION.md`. Xacro expansion, cross-file consistency, meshes, inertias, and sampled collision checks passed. **ROS nodes, C++ compilation, MoveGroup execution, and Gazebo dynamics could not be run in the preparation environment**, so live integration is not yet verified. The included checker provides the next validation step on your Jazzy installation.

Offline configuration checks on a sourced ROS installation:

```bash
python3 src/arm/tests/validate_config.py
```

The optional `tests/validate_geometry.py` additionally requires `numpy`, `trimesh`, `python-fcl`, and `PyYAML`; those are validation-only dependencies, not runtime package dependencies.

Implementation references: [Jazzy controller manager](https://control.ros.org/jazzy/doc/ros2_control/controller_manager/doc/userdoc.html), [Jazzy migration and spawner parameters](https://control.ros.org/jazzy/doc/ros2_control/doc/migration.html), [gz_ros2_control](https://control.ros.org/jazzy/doc/gz_ros2_control/doc/index.html), [MoveIt controller configuration](https://moveit.picknik.ai/main/doc/examples/controller_configuration/controller_configuration_tutorial.html).
