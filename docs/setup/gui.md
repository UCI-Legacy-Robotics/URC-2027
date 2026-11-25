# GUI setup

Set up, run and check the base station GUI (`gui/`). It doesn't run ROS, so work natively on macOS, Windows or Linux. You don't need WSL.

## Frontend

### Frontend install

1. Install Node 22 LTS, e.g. with [nvm](https://github.com/nvm-sh/nvm) (or [nvm-windows](https://github.com/coreybutler/nvm-windows)):
   ```bash
   nvm install 22
   ```
2. Enable Corepack. It installs the pnpm version pinned in `gui/frontend/package.json`:
   ```bash
   corepack enable
   ```
3. Install dependencies. This also installs the git hooks:
   ```bash
   cd gui/frontend
   pnpm install
   ```

### Run

From `gui/frontend`:

| Command | What it does |
| ------- | ------------ |
| `pnpm dev` | Opens the Electron app in control mode |
| `pnpm dev:web` | Serves only the UI at http://localhost:5174, for a normal browser in viewer mode. Also reachable from other devices on your network |
| `pnpm storybook` | Opens Storybook at http://localhost:6006 to build widgets against fake data |
| `pnpm build` | Production build into `out/` (see below) |

You don't need the rover or the backend to work on widgets. See [Writing a widget](../gui/widgets.md).

The Electron window has no OS title bar: the app's top bar is the title bar. Drag it to move the window; the minimize, maximize and close buttons are at its right end (macOS keeps its own buttons on the left).

### Build output

`pnpm build` writes to `gui/frontend/out/`:

| Folder | What it is |
| ------ | ---------- |
| `out/renderer/` | **The UI as static files** (`index.html` + `assets/`). The backend serves this folder to browser viewers. Asset paths are relative, so it works from any URL. |
| `out/main/`, `out/preload/` | Electron's main process and preload script |

Packaging the Electron app for the base laptop (`pnpm build:linux`, AppImage/.deb) is a later release step.

### Frontend checks

[Lefthook](https://lefthook.dev) runs these for you. `pnpm install` installs the hooks, and the config is `lefthook.yml` at the repo root:

| When | What runs | On |
| ---- | --------- | -- |
| `git commit` | Prettier (fixes formatting and re-stages) and ESLint | Staged files in `gui/frontend` |
| `git push` | Full typecheck and lint | Only if the push touches `gui/frontend` |

Commits that don't touch the GUI skip these checks. If you staged only part of a file, only that part is formatted and committed; your unstaged changes stay unstaged.

Hooks can be skipped with `--no-verify`, so CI ([`.github/workflows/gui.yml`](../../.github/workflows/gui.yml)) runs everything again on every pull request that touches the GUI: typecheck, lint, format check, the production build and the Storybook build.

To run them by hand, from `gui/frontend`:

```bash
pnpm typecheck && pnpm lint && pnpm format:check && pnpm build
```

`pnpm format` applies Prettier formatting.

## Backend

### Backend install

Install [uv](https://docs.astral.sh/uv/getting-started/installation/), then:

```bash
cd gui/backend
uv sync
```

### Run the backend

From `gui/backend`:

```bash
uv run uvicorn app.main:app --host 127.0.0.1 --port 8000
```

`GET /api/health` reports whether the base MediaMTX is running. `GET /api/video/stats/<path>` gives running totals for one stream from the base MediaMTX: RTP packets lost between the rover and the base (counted from MediaMTX's log), bytes received and damaged frames. The video widget's STATS panel graphs them.

`GET /api/video/link` reports the rover's side of the 5 GHz link. Every 2 s the backend asks the rover's MediaMTX for `rover/front` (`BASE_ROVER_STREAM_PATH`) with an RTSP `DESCRIBE` on port 8554: no answer is `unreachable`, a 404 is `no-stream` (the rover is up but the ROS video launch isn't streaming), and a 200 is `ok`. The top bar's 5 GHz badge and the video widget's error messages use it.

### Base station video

The backend runs the base station [MediaMTX](../system/video.md), which pulls the rover's camera streams over the 5 GHz link and serves them to the GUI over WebRTC on port 18889.

1. Before going to the field (it needs internet), download the pinned MediaMTX into `gui/backend/bin/`:
   ```bash
   scripts/install_mediamtx.sh
   ```
2. Set the rover's 5 GHz IP if it isn't the default `192.168.1.67`, in `gui/backend/.env` or the environment:
   ```bash
   BASE_ROVER_HOST=192.168.1.67
   ```

The rover sends video to UDP ports that the base MediaMTX picks at random, so the base's firewall must accept UDP from the rover.

### Backend checks

From `gui/backend`:

```bash
uv run ruff check . && uv run ruff format --check .
uv run pytest -q
```

### Regenerate MAVLink code

After changing `mavlink/rover.xml`, see [MAVLink dialect](../system/mavlink.md#regenerating).

## Offline

The field has no internet. Install all dependencies before you leave, and never add runtime downloads: no CDNs, web fonts or online map tiles. The production build loads nothing from outside its own folder, and the page's Content-Security-Policy (in `src/renderer/index.html`) blocks anything else.
