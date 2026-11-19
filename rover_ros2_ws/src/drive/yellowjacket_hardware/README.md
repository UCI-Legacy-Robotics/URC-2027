# Yellow Jacket hardware plugin scaffold

This package builds a `hardware_interface::SystemInterface` plugin named
`yellowjacket_hardware/YellowjacketSystem`. It is not a functional motor driver yet:
CAN framing, RoboClaw packet transactions and encoder acquisition are unfinished.
Configuration deliberately fails and no CAN connection is opened or motor command sent.
`read()` and `write()` return errors, rather than fabricate feedback or successful commands.

## Build and test

From `rover_ros2_ws`:

```bash
source /opt/ros/jazzy/setup.bash
colcon build --packages-select yellowjacket_hardware --symlink-install
source install/setup.bash
colcon test --packages-select yellowjacket_hardware --event-handlers console_direct+
colcon test-result --test-result-base build/yellowjacket_hardware --verbose
```

## How the code connects

- `package.xml` declares ROS dependencies.
- `CMakeLists.txt` builds a shared library and installs its plugin description.
- `hardware_plugins.xml` maps the Xacro plugin name to the C++ class.
- `yellowjacket_system.hpp` declares lifecycle and read/write callbacks.
- `yellowjacket_system.cpp` validates and exports interfaces, then defines transport TODOs.
- `encoder_units.hpp` converts decoded wheel counts to/from radians. It is a helper for
  the future transport implementation, not yet connected to read/write.

Each wheel must have exactly one `velocity` command interface and `position` plus
`velocity` state interfaces. Unknown measurements initially contain NaN.
The interface vectors are allocated once during initialization; exported handles point
into them and the controller manager reads/writes those same values.

## Selecting the plugin later

Replace the hardware block in the robot description you actually launch with:

```xml
<hardware>
  <plugin>yellowjacket_hardware/YellowjacketSystem</plugin>
  <param name="can_interface">can0</param>
</hardware>
```

`can0` is a configurable interface name, not a verified device on this computer.
The teleop mock launch still selects mock hardware. Do not expect this scaffold to activate.
There are two control Xacro files in the workspace; the existing teleop launch expands
`teleop/description/drive_description.urdf`, which includes `teleop/description/ros2_control.xacro`.

## Implement the transport next

1. Confirm laptop CAN adapter/API, bitrate, classic CAN versus CAN FD, TX/RX IDs,
   frame segmentation and ordering, and any bridge buffering/timeouts. Unchanged payload
   bytes alone do not define how a complete serial transaction is transported.
2. Define per-joint mapping: bridge node/IDs, RoboClaw packet address, motor channel,
   decoded counts per wheel revolution, direction, and speed/current limits. Parse these
   as Xacro parameters; do not ship placeholder addresses as actual hardware defaults.
3. Implement `open_transport()` and `close_transport()`. First verify a non-motion
   RoboClaw query with a complete reply and CRC check.
4. Implement `read()` with bounded transactions, reply checks, encoder wrap handling,
   unit conversions and detection of stale/disconnected feedback.
5. Implement `stop_motors()` and `write()`, including finite/range checks, speed scaling,
   RoboClaw command CRC/acknowledgements, and bridge frame fragmentation.
6. Before enabling activation, add cleanup for partially opened connections and best-effort
   stopping on write/read failures and destruction. Configure a device-side command watchdog;
   a laptop-side stop cannot guarantee delivery after the link is lost.
7. Test one motor with a dedicated bench URDF and velocity controller before connecting
   the six-wheel differential-drive controller. Tune the RoboClaw velocity loop with encoder
   feedback; sending a speed target is different from sending open-loop PWM.

Reference: [Jazzy hardware component guide](https://control.ros.org/jazzy/doc/ros2_control/hardware_interface/doc/writing_new_hardware_component.html).
