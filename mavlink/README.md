# MAVLink dialect

`rover.xml` defines every message sent over the 900 MHz telemetry radio between the rover and the base station. It includes the standard `common.xml`.

**Both the rover (`rover_ros2_ws/src/rover_telemetry`) and the base station (`gui/backend`) generate code from this file.** A change here changes the wire format on both sides, so PRs touching `mavlink/` need review from both the rover telemetry and GUI owners, and both sides must regenerate in the same PR.

## Files

| File | What it is |
| ---- | ---------- |
| `rover.xml` | Our dialect: custom enums, commands, and messages |
| `common.xml`, `standard.xml`, `minimal.xml` | Upstream MAVLink definitions, copied unchanged from pymavlink 2.4.50 so both sides build from identical XML. Don't edit them. |

## IDs

| What | Value |
| ---- | ----- |
| Rover system ID | `1` |
| Base station system ID | `255` (ground station convention) |
| Custom message IDs | `48000`–`48999` |
| Custom `MAV_CMD` values | `48000`–`48999` |

System IDs are configured in each side's settings (for the backend, `gui/backend/app/config.py`), not hardcoded. No dialect bundled with pymavlink uses the 48000–48999 range.

Allocated so far:

| ID | Name | Kind |
| -- | ---- | ---- |
| 48000 | `ROVER_STATUS` | message (placeholder fields) |
| 48001 | `ROVER_VIDEO_STATUS` | message |
| 48000 | `MAV_CMD_ROVER_VIDEO_SELECT_CAMERA` | command |
| 48001 | `MAV_CMD_ROVER_VIDEO_SET_BITRATE` | command |
| 48002 | `MAV_CMD_ROVER_VIDEO_SET_RESOLUTION` | command |

## Adding a message

1. Take the next free ID in the range and add the `<message>` to `rover.xml`. Add it to the table above.
2. Keep it small. The radio carries tens of kbps shared in both directions, so prefer scaled integers (`cdegC`, `mV`, `degE7`) over floats, and decide on a send rate.
3. Use a standard `common.xml` message instead if one fits.
4. Prefer `COMMAND_LONG` with a new `MAV_CMD` for commands that need an acknowledgement. The receiver replies with `COMMAND_ACK`.
5. Regenerate on both sides (below) and commit the generated code.

Never change the fields or the order of a message that's already in use. Add a new message instead, or coordinate a breaking change across both sides.

## Regenerating

Base station (generated code is committed, in `gui/backend/app/generated/`):

```bash
cd gui && make mavlink     # or: gui/backend/scripts/gen_mavlink.sh
```

The rover's `rover_telemetry` package will generate from the same `rover.xml` when it's written.
