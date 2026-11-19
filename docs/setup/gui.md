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
| `pnpm storybook` | Opens Storybook to build widgets against fake data |
| `pnpm build` | Production build. Static renderer output: _TBD_ |

You don't need the rover or the backend to work on widgets. See [Writing a widget](../gui/widgets.md).

The Electron window has no OS title bar: the app's top bar is the title bar. Drag it to move the window; the minimize, maximize and close buttons are at its right end (macOS keeps its own buttons on the left).

### Frontend checks

Git hooks run these for you: ESLint and Prettier on staged files at commit, and the full typecheck and lint at push. CI runs them all again on pull requests, because hooks can be skipped.

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

The field has no internet. Install all dependencies before you leave, and never add runtime downloads: no CDNs, web fonts or online map tiles.
