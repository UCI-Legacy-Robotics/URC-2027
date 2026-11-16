# Science sequence and control

Only have elevator, bradford, and npk sequences right now. <-- still need to add camera

Bradford should have everything from lower the drill to max depth, start blender and spectrophotometer.
everything should also have retract in case theres an error

also i assumed embedded or GUI will do soemthing to let us know there's an error, so i had it as buttons to manually retract stuff like drill

This package runs the Bradford and NPK sequences through ROS 2 Jazzy. It decides
which steps can start, sends commands to the hardware drivers, waits for their
replies, and reports progress and measurements. It also handles cancellation,
retraction, and whether science is ready for the rover to drive.

## Build and run

On Ubuntu 24.04 with ROS 2 Jazzy, from `rover_ros2_ws/`:

```bash
rosdep install --from-paths src --ignore-src -y
colcon build --symlink-install --packages-up-to science_sequence
source install/setup.bash
ros2 run science_sequence sequencer --ros-args --params-file <calibrated.yaml>
```

Set the fields from `Config` in `controller.py` in your ROS parameter file, and
set `hardware_calibrated: true` after entering the measured values. The defaults
are placeholders. Setting the flag does not calibrate anything by itself.

Travel targets are measured from each platform's home position. For example,
`ten_cm_steps` includes the travel through the air before the drill touches soil,
as well as the travel into the soil. Restart the node after changing parameters.

## Operation requests

Send `science_interfaces/action/ScienceSequence` goals to `/science/sequence`.
One operation runs at a time; invalid or overlapping requests are rejected.

| Command             | When to use it and what happens                                                                                                                                                                                                                                                                             |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `home`              | Start a new cycle. Stop activity, return NPK and the bottom platform to their home positions, and align the first cuvette. Close the cache and blender lid, open the hatch, and reset the cycle status. Samples and cuvettes must be prepared separately.                                                   |
| `lower_platform`    | After homing, with the bottom platform at `BASE` and NPK raised, lower to `NEAR_GROUND`. If lowering fails or is cancelled, return to base and wait for the operator to try again.                                                                                                                          |
| `collect_bradford`  | With the platform near ground, NPK raised, and no completed sample: spin up the auger, lower to 10 cm, close the hatch, and open the cache and blender lid. Lower to maximum depth, mark collection complete, then stop the auger and open the hatch. The controller acknowledges collection automatically. |
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

### Connecting a stop and retract button

The operator uses the GUI to spot problems, such as a drill that has stopped
moving. The GUI team can connect a stop and retract button to these requests:

- While `collect_bradford` is active, cancel its accepted action goal. The
  cancellation handler stops the hardware, closes cache, returns the bottom
  platform to `NEAR_GROUND`, and opens the hatch. Wait for the final result and
  check `state.fault` and `state.platform_position`. An accepted cancel request
  does not mean the drill has finished retracting. Wait for that result before
  sending another request.
- When no science operation is active and the platform is deployed, send
  `retract_bradford` to `/science/sequence`. This also stops activity before
  retraction. Read the result and state for completion.

This returns to near-ground height, not travel height. It does not retry or
resume drilling. Cancelling initial `lower_platform` instead returns to `BASE`,
as specified for that operation. NPK and mixing have their own cancellation
behavior, so the caller must check which operation is running. The GUI team
handles the button and keeps track of its active action goal.

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

The stop service must work even while another command is waiting. It must stop
activity, cancel pending work so it cannot start later, and keep enough motor
holding torque to support the platforms. When the driver reports a command as
cancelled or aborted, that command must no longer be running.

## Recovery and remaining integration

Cancel and ordinary failures first confirm a stop, then attempt recovery:
Bradford returns near ground with cache closed and hatch reopened; NPK homes its
probe; blender/pump/measurement stops without automatic refill. Interrupted
carousel motion requires rehoming. Recovery ignores the original cancellation
request so it can finish returning the mechanism.

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
