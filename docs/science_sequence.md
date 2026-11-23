# Science sequence and control

The science sequencer runs the elevator, Bradford, and NPK operations through
ROS 2 Jazzy. The operator requests each Bradford stage, then requests retraction
and blending separately. The sequencer checks whether each request can start,
waits for the embedded driver to confirm what happened, and reports progress,
faults, and readings for the operator to use. Camera capture is future work.

## Build and run

On Ubuntu 24.04 with ROS 2 Jazzy, from `rover_ros2_ws/`:

```bash
rosdep install --from-paths src --ignore-src -y
colcon build --symlink-install --packages-up-to science_sequence
source install/setup.bash
ros2 run science_sequence sequencer --ros-args --params-file /path/to/calibrated.yaml
```

Put the measured values under `science_sequencer.ros__parameters` in the ROS
parameter file. The defaults in `Config` are example values. Set
`hardware_calibrated: true` once the hardware values are confirmed, then
restart the node to load them.

### What to record during individual parts testing

Use the bench team's controls for the individual components. The sequencer's
`home` action combines several mechanisms, so save it for integrated testing.

| Part | What to record | Where it goes |
| --- | --- | --- |
| Bradford elevator | How the driver confirms home and that both sides reached the target. Record the step counts for near ground, 10 cm below the soil surface, and maximum depth. Count the full travel from home, including the air gap before the auger touches soil. | `near_ground_steps`, `ten_cm_steps`, `max_depth_steps`, and the driver's completion report. |
| NPK platform and probe | How the driver confirms home and the measuring position. Record the travel from NPK home and the order and units of the three readings. | `npk_lower_steps` and the `npk_read` result. |
| Auger, hatch, cache, and blender lid | Confirm which physical state each command produces and what tells the driver it finished. Ask what the encoder measures and whether elevator position stays valid if the auger jams. | Driver command mapping. `AUGER_JAM` and `elevator_position_valid` are provisional until that feedback is agreed. |
| Blender, pump, carousel, and spectrophotometer | Record the mix time, the pump time for the intended dose, the first cuvette position and slot spacing, the absorbance result and unit, and any time the instrument needs to settle before reading. | `blender_seconds` and `pump_seconds`. The current four-slot, 90° indexing and immediate read after pumping are assumptions to check. Different indexing or a required settling delay needs a code or driver change. |
| Completion and stop | Record normal completion and stop times, the precision of reported positions, and how the driver confirms that motion and pending work have stopped. | Review `command_timeout`, `recovery_timeout`, and `position_tolerance` against the driver behavior. The position tolerance is in motor steps. |

Keep the recovery decisions open for the combined test. In particular, confirm
whether the drill can retract with the auger stopped and when the hatch can open.
Reverse auger motion is commented out. A forward jog after failed retraction
still needs an agreed direction, travel limit, and position confirmation.

## Run the workflow in simulation

From `rover_ros2_ws/`, with ROS 2 Jazzy sourced:

```bash
colcon build --symlink-install --packages-up-to science_sequence
source install/setup.bash
python3 -m unittest discover -s src/science/science_sequence/test -v
```

The ROS tests start the production sequencer, an operator action client, and a
simulated embedded driver in the same unique namespace. The simulated driver
returns example sensor readings and can hold or fail a drill move on demand.
The tests check action feedback, the result payloads, and `/science/state`.

| Scenario | Expected result |
| --- | --- |
| Complete Bradford and NPK | Publish absorbance and N/P/K results. Reject `ready_to_drive` while NPK is lowered. After retraction, stow the bottom platform and publish drive readiness. |
| Cancel the Bradford approach from `BASE` | Cancel the in-flight driver goal, confirm a stop, return to `BASE`, and invalidate the sample. |
| Simulated auger jam after hatch closure | Confirm a stop, reopen the hatch, then retract to `NEAR_GROUND` if elevator position is valid. |
| Emergency stop during descent | Confirm the stop, publish `ESTOP`, and block further motion requests. |
| Drill move fails and stop cannot be confirmed | Publish `FAULT` and reject retraction without commanding further travel. |

For a controller-only run from `rover_ros2_ws/`, use:

```bash
PYTHONPATH=src/science/science_sequence python3 -m unittest discover -s src/science/science_sequence/test -p test_controller.py -v
```

This also covers a simulated drill failure followed by a fresh collection,
measurement, and stow. Both suites use example depths and readings.

## Operation requests

Send `science_interfaces/action/ScienceSequence` goals to `/science/sequence`.
One operation runs at a time; invalid or overlapping requests are rejected.

| Command             | When to use it and what happens                                                                                                                                                                                                                                                                             |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `home`              | Start a new cycle. Stop activity, return NPK and the bottom platform to their home positions, and align the first cuvette. Close the cache and blender lid, open the hatch, and reset the cycle status. Samples and cuvettes must be prepared separately.                                                   |
| `lower_platform`    | After homing, with the bottom platform at `BASE` and NPK raised, lower to `NEAR_GROUND`. If lowering fails or is cancelled, return to base and wait for the operator to try again.                                                                                                                          |
| `approach_bradford` | With the platform at `BASE` or `NEAR_GROUND` and NPK raised, spin up the auger, lower to 10 cm, and stop the auger at the checkpoint. |
| `drill_bradford` | At 10 cm, close the hatch, open the cache and blender lid, spin up the auger, lower to maximum depth, and stop the auger. |
| `finish_bradford` | At maximum depth, confirm the auger is stopped, open the hatch, and mark collection complete. |
| `collect_bradford` | Run the combined Bradford sequence from `NEAR_GROUND`. Available for existing callers that start collection in one request. |
| `retract_bradford`  | With NPK raised and the bottom platform at a known lowered position, stop activity and confirm the stop. Close the cache, raise the platform to near ground, and open the hatch.                                                                                                                            |
| `blend_and_measure` | After collection, with the first unused cuvette aligned, close the blender lid and mix for `blender_seconds`. Stop the blender, pump for `pump_seconds`, and take an absorbance reading.                                                                                                                    |
| `next_cuvette`      | After mixing and using the current cuvette, turn 90 degrees to the next one, pump, and measure. There are four positions. The controller does not return to a used cuvette or refill one that might already contain liquid.                                                                                 |
| `measure_npk`       | With the bottom platform near ground and NPK raised, lower the separate NPK platform and take N/P/K readings. Leave NPK lowered until the operator requests retraction.                                                                                                                                     |
| `retract_npk`       | With the bottom platform near ground, raise the separate NPK platform to its home position.                                                                                                                                                                                                                 |
| `ready_to_drive`    | After collection, with Bradford back near ground and NPK raised, close the cache and blender lid, stop activity, and raise the bottom platform to base. Report ready only after this finishes. The same command can confirm readiness again if the platform is already at base.                             |

`BASE` is travel height; `NEAR_GROUND` is the initial lowered height. They are
different positions. The hatch controls the soil path so continued drilling
pushes soil upward into collection. The software preserves the requested hatch
order; it does not model soil flow or independently detect collected soil.

The science controller acknowledges collection itself; it does not wait for a
reply from a separate state machine. Each operation still needs its own request.
For example, finishing collection does not automatically start blending or
retraction. To cancel a running action, the caller uses its accepted goal handle's
`cancel_goal_async()` method.

### Operator stop controls

| Control | ROS request | Behavior |
| --- | --- | --- |
| Stop and retract during Bradford | Cancel the accepted action goal | Wait for the driver to stop. An approach launched from `BASE` returns to `BASE`. An approach launched from `NEAR_GROUND` or a later Bradford operation returns to `NEAR_GROUND`. Wait for the final result and check `state.fault` and `state.platform_position`. |
| Retract at a checkpoint | `retract_bradford` action goal | Stop activity, close the cache, retract to `NEAR_GROUND`, and open the hatch. |
| Sequencer emergency stop | `/science/emergency_stop` with `std_srvs/srv/Trigger` | Ask the driver to stop and latch the sequencer in `ESTOP`. Available during any operation, during recovery, and while idle. Normal goals are blocked, including `home`. The response reports whether the driver confirmed the stop. |

After a sequencer emergency stop, check the hardware and clear any physical stop
before restarting the node and explicitly requesting `home`. The ROS service
depends on the driver and ROS transport. The physical bench E-stop is handled by
the hardware team.

Action feedback and `/science/state` report the current step, platform positions,
whether collection and retraction finished, faults, the cuvette number, and drive
readiness. Measurements arrive in the final action result: the name, value, and
unit at the same index belong together. Absorbance is returned as measured; the
controller does not calculate protein concentration.

Use the final result and latest state when deciding what to do next. Even after
collection reaches maximum depth, stopping or retraction can still fail. Sending
these reports to the base station, displaying them, and saving them belong to
the other teams.

## Interfaces to agree on with the embedded team

The adapter in `ros_hardware.py` expects `/science/hardware/execute` using
`science_interfaces/action/ScienceCommand`, plus `/science/hardware/stop` using
`std_srvs/srv/Trigger`. These define the requests our code sends and the replies
it expects. Agree on these names and message types with the embedded team;
the actual hardware drivers are not included here.

| Command                          | Value and required completion                                                                                                  |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `platform_home`, `npk_home`      | 0; confirm the corresponding raised origin.                                                                                    |
| `platform_move`, `npk_move`      | Absolute motor steps; confirm target reached and vertical motion stopped. Synchronization/jam detection belongs to the driver. |
| `auger`, `blender`               | 1 running / 0 stopped; confirm windup or stopping.                                                                             |
| `hatch`, `cache`, `blender_lid`  | 1 open / 0 closed; confirm physical state.                                                                                     |
| `pump`                           | Duration in seconds; return only after delivery finishes and pump stops.                                                       |
| `carousel_home`, `carousel_move` | 0 for home; absolute degrees for indexing (90, 180, 270). Confirm alignment.                                                   |
| `npk_read`, `spectrometer_read`  | 0; return three N/P/K values or one absorbance value, respectively, with documented units.                                     |

The driver should report success only after completing the requested change.
For position and on/off commands, return the confirmed value in `actual_value`.
Sensor readings must be finite numbers with units.

For simulation, the encoder hook is an aborted result with `fault_code` set to
`AUGER_JAM`. Set `elevator_position_valid` only if the driver can still verify
the elevator position after the jam. This is a placeholder for embedded's fault
report, including which encoder provides it. The embedded driver is responsible
for stopping the auger when it detects a jam. The sequencer then requests a
confirmed stop from the stop service.

The stop service must work even while another command is waiting. It must stop
activity, cancel pending work so it cannot start later, and keep enough motor
holding torque to support the platforms. When the driver reports a command as
cancelled or aborted, that command must no longer be running.

## Recovery and remaining integration

| Event | Sequencer behavior |
| --- | --- |
| Operator cancels Bradford | Confirm a stop, then return to the checkpoint specified above. |
| Auger jam | Confirm a stop and open the hatch if closed or its closure was requested. With verified elevator position, attempt the same return as cancellation. If the position is unknown, report `FAULT`. Reverse auger motion is commented out pending hardware testing. |
| Other fault in the staged Bradford sequence | Confirm a stop and report `FAULT` while the per-fault policy is open. |
| Retraction fails | Stop again and report `FAULT`. Keep operator-requested forward movement pending confirmation of the allowed direction, travel limits, and valid position feedback. |
| NPK or analysis cancellation | Home the NPK probe, or stop blender/pump/measurement without automatic refill. Interrupted carousel motion requires rehoming. |

The sequencer emergency stop can also interrupt recovery.

If recovery fails, or we cannot tell whether a command is still running, the
controller stays in `FAULT`, marks positions unknown, and blocks normal requests.
Check the hardware and establish its positions again with `home`. If a command's
acceptance reply was lost, first confirm that the driver has stopped all old
commands, then restart the sequencer and run `home`. A late reply triggers an
attempt to cancel the old command, but does not allow new movement.

The current retraction sequence assumes the drill can be raised while the auger
is stopped. Requiring NPK to be raised and allowing only one operation at a time
are assumptions to check with the mechanical team. Blending requires
collection completion but does not require Bradford retraction or cache closure.
Reagent timing, instrument settling, and calibration remain to be specified.
This package reports whether science is ready to drive. The drive team must use
that report to allow or block driving, and the embedded team handles local stop
watchdogs. Positions start unknown after a restart. Starting another operation
clears drive readiness; request `ready_to_drive` again afterward.
