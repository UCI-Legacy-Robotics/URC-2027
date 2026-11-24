# URC-2027 docs

All project docs live in this folder. READMEs next to the code only hold a one-line description, the commands you run there, and links back here.

## Start here

| I want to… | Read |
| ---------- | ---- |
| Set up and build the rover workspace | [Rover setup](setup/rover.md) |
| Set up and run the GUI | [GUI setup](setup/gui.md) |
| Understand how the system fits together | [System overview](system/overview.md) |
| Build a GUI widget | [Writing a widget](gui/widgets.md) |
| Open a PR | [CONTRIBUTING.md](../CONTRIBUTING.md) |

## Contents

**Setup**

- [Rover](setup/rover.md): Ubuntu/WSL, ROS 2 Jazzy, building and testing the workspace
- [GUI](setup/gui.md): Node, pnpm, uv, running and checking the frontend and backend

**System**

- [Overview](system/overview.md): rover, radio links and base station
- [Video](system/video.md): camera streams over 5 GHz
- [Science](system/science.md): science sequences and panoramas
- [MAVLink dialect](system/mavlink.md): message IDs, adding messages, regenerating code

**Rover**

- [Drive description](rover/drive-description.md): URDF frames, joints and known gaps

**GUI**

- [Architecture](gui/architecture.md): control model, failsafes, shell, tech stack
- [Writing a widget](gui/widgets.md)
- [Design direction](gui/design.md)

## Writing docs

- Put new docs in the matching folder here and add them to the list above.
- Update docs in the same PR as the change.
- Draw diagrams as [Mermaid](https://mermaid.js.org/) code blocks inside the page. GitHub renders them.
- Write each fact once and link to it from elsewhere.
- Use relative links between docs.
