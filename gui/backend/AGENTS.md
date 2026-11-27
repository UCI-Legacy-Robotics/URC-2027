# GUI backend

FastAPI server on the base laptop: owns the 900 MHz radio, decodes MAVLink, streams telemetry over WebSockets and serves the built UI to viewers. See `docs/gui/architecture.md` for the control model and failsafes.

Run everything from `gui/backend/` through uv (`uv run ...`); bare `python` may not be the project venv.

```bash
uv sync
uv run ruff check . && uv run ruff format --check .
uv run pytest -q
scripts/gen_mavlink.sh        # after changing mavlink/rover.xml
```

- Add dependencies with `uv add <pkg>` (or `uv add --dev`). Never hand-edit versions in `pyproject.toml` or `uv.lock`.
- `app/generated/` is mavgen output. Don't hand-edit it; regenerate and commit it with the XML change.
- **Control vs viewer:** commands are accepted only on the localhost control entry point. The viewer entry point serves UI files, telemetry and video, and must never accept a command.
- **Failsafe:** if the Electron app's heartbeats stop during teleop, the backend sends the rover a stop. During autonomy it sends nothing.
- System IDs (rover `1`, base `255`) come from settings, not literals.
- Annotate new functions' parameters and return types.
