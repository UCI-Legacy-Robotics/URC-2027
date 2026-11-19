# Base station GUI

The base station GUI. It doesn't run ROS, so you can work on it natively on macOS, Windows or Linux.

| Folder | What it is |
| ------ | ---------- |
| [`frontend/`](frontend/README.md) | React + TypeScript UI. Runs in an Electron app on the base laptop (control) and in browsers on the base Wi-Fi (read-only viewer). |
| `backend/` | Python (FastAPI) server: decodes MAVLink from the 900 MHz radio, streams telemetry over WebSockets, serves the UI to viewers. |

Docs:

- [Architecture](../docs/gui/architecture.md): system layout, control model, failsafes, tech stack
- [Writing a widget](../docs/gui/widgets.md)
- [Design direction](../docs/gui/design.md)
- [MAVLink dialect](../mavlink/README.md)

## Backend setup

Install [uv](https://docs.astral.sh/uv/getting-started/installation/), then:

```bash
cd gui/backend
uv sync
uv run ruff check .
```

## Regenerate MAVLink code

After changing `mavlink/rover.xml`:

```bash
gui/backend/scripts/gen_mavlink.sh
```

Commit the regenerated `backend/app/generated/rover.py` with the XML change.
