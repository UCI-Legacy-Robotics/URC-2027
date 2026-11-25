# GUI backend

FastAPI server on the base laptop. Runs the base station MediaMTX and probes the rover's video link; will also own the 900 MHz radio and stream telemetry.

```bash
uv sync                          # install dependencies
scripts/install_mediamtx.sh      # download the pinned MediaMTX into bin/ (needs internet)
uv run uvicorn app.main:app --host 127.0.0.1 --port 8000
uv run ruff check . && uv run ruff format --check .
uv run pytest -q
scripts/gen_mavlink.sh           # after changing mavlink/rover.xml
```

Settings are `BASE_*` environment variables or `.env` here, e.g. `BASE_ROVER_HOST=192.168.1.67`.

Docs: [setup, endpoints and base station video](../../docs/setup/gui.md#backend) · [architecture](../../docs/gui/architecture.md) · [video](../../docs/system/video.md) · [MAVLink dialect](../../docs/system/mavlink.md)
