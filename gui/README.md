# Base station GUI

- `frontend/`: React + TypeScript UI. Runs in Electron on the base laptop (control) and in browsers on the base Wi-Fi (viewer).
- `backend/`: Python (FastAPI) server. Decodes MAVLink and streams telemetry over WebSockets.

Docs: [setup](../docs/setup/gui.md) · [architecture](../docs/gui/architecture.md) · [writing a widget](../docs/gui/widgets.md)
