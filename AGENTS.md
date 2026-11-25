# URC-2027

UCI Legacy Robotics rover software for the University Rover Challenge 2027. The rover runs ROS 2 Jazzy on Ubuntu 24.04; the base station runs no ROS.

- `rover_ros2_ws/`: the rover's ROS 2 workspace. See `rover_ros2_ws/AGENTS.md`.
- `gui/frontend/`: base station UI, Electron + React + TypeScript, pnpm. Runs as the Electron control app on the base laptop and as a read-only viewer in browsers. See `gui/frontend/AGENTS.md`.
- `gui/backend/`: FastAPI server that owns the radio, decodes MAVLink and streams telemetry over WebSockets. Python with uv. See `gui/backend/AGENTS.md`.
- `mavlink/`: the MAVLink dialect (`rover.xml`) shared by the rover and `gui/backend`.
- `docs/`: all developer docs, indexed in `docs/README.md`. READMEs next to code hold only a one-line description, commands and links back to `docs/`.

Setup, branch naming, commit format and the PR process for humans are in `CONTRIBUTING.md`. Read the doc for the area you're working in before changing it; this file only adds what an agent needs on top.

## Workflow

- Don't commit, push, or open PRs unless asked; the user usually commits themselves. When asked, follow `CONTRIBUTING.md`.
- Branches are `<name>/<subsystem>/<short-description>`, e.g. `ethans/drive/teleop-deadzone`. Subsystems are the `rover_ros2_ws/src/` folders, `gui`, `mavlink` or `docs`.
- Commits and PR titles are Conventional Commits: `type(scope): summary`, lowercase, imperative, no trailing period, scope matching the branch's subsystem. Use `!` plus a body for breaking changes such as a renamed topic or message field.
- Write PR descriptions, commit messages and issue bodies for a reviewer who wasn't in your session: say what changed and why, name the packages, nodes, topics, messages, widgets and files that changed and what changed about each. Don't refer to the conversation, intermediate attempts or your own process.
- Don't add `Co-Authored-By` trailers or AI-attribution lines (such as "Generated with Claude Code" or "Written by Codex") to commits, PRs or issues. Commits are authored by the user's git identity.
- Update `docs/` in the same change as the code it describes. Write each fact once and link to it; use Mermaid for diagrams.
- Do small steps when the user asks for them: finish one, verify it, summarize, and stop for review.

## MAVLink dialect

- `mavlink/rover.xml` is the wire format on both sides. A change needs regenerating code on both sides in the same change (`gui/backend/scripts/gen_mavlink.sh` for the base station) and an update to the ID table in `docs/system/mavlink.md`.
- Never change the fields or order of a message already in use; add a new message instead.
- Custom message and `MAV_CMD` IDs are in 48000–48999. Prefer scaled integers over floats: the radio carries tens of kbps.
- `common.xml`, `standard.xml` and `minimal.xml` are upstream copies. Don't edit them.

## Field constraints

- The competition field has no internet. Never add anything that downloads at runtime: no CDNs, web fonts, online map tiles or package installs on startup.
- Hardware facts that are easy to get wrong: the stereo camera is a **ZED 2i** (`camera_model:=zed2i`, not `zed2`) and must link at USB 3 (5000M in `lsusb -t`); the arm camera is an **OV9281** USB module: monochrome, MJPEG only, 60 fps minimum; the telemetry radio is a 900 MHz RFD900x over USB serial; video goes over a separate 5 GHz link.

## Writing code

- **Match the surrounding code** (naming, structure, idioms) over general preference. Keep a change to what the task needs. Don't refactor neighboring code unless asked; mention it instead.
- **Look for what exists before building.** Search for an existing node, message, helper, component or widget before adding one. Extract shared code only when there's a second real caller.
- **Names describe what a thing is now**, not its history.
- **Comments are rare.** Write one only when it tells the reader something the code can't: a non-obvious invariant, a hardware or competition-rule constraint, units, or why the obvious approach is wrong. Don't narrate the code or record history; that belongs in the commit.
- **Don't add dependencies** without checking with the user. The GUI stack is fixed in `docs/gui/architecture.md#tech-stack`.
- **Verify before claiming.** Confirm root causes and library behavior by reading the code or reproducing. Say what you ran, and say so when a change is untested on hardware or in the UI.
