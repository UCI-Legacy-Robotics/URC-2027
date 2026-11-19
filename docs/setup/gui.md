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

### Backend checks

From `gui/backend`:

```bash
uv run ruff check .
```

### Regenerate MAVLink code

After changing `mavlink/rover.xml`, see [MAVLink dialect](../system/mavlink.md#regenerating).

## Offline

The field has no internet. Install all dependencies before you leave, and never add runtime downloads: no CDNs, web fonts or online map tiles. The production build loads nothing from outside its own folder, and the page's Content-Security-Policy (in `src/renderer/index.html`) blocks anything else.
