# GUI frontend

React + TypeScript UI for the base station. It runs inside Electron on the base laptop in **control** mode, and in a normal browser in read-only **viewer** mode. See [docs/gui/](../../docs/gui/architecture.md) for the architecture and [writing a widget](../../docs/gui/widgets.md).

## Setup

1. Install Node 22 LTS, e.g. with [nvm](https://github.com/nvm-sh/nvm) (or [nvm-windows](https://github.com/coreybutler/nvm-windows)):
   ```bash
   nvm install 22
   ```
2. Enable Corepack. It installs the pnpm version pinned in `package.json`:
   ```bash
   corepack enable
   ```
3. Install dependencies. This also installs the git hooks:
   ```bash
   pnpm install
   ```

## Run

| Command | What it does |
| ------- | ------------ |
| `pnpm dev` | Opens the Electron app in control mode |
| `pnpm dev:web` | Serves the UI to a normal browser in viewer mode |
| `pnpm storybook` | Opens Storybook to build widgets against fake data |
| `pnpm build` | Production build. Static renderer output: _TBD_ |

## Checks

Git hooks run these for you: ESLint and Prettier on staged files at commit, and the full typecheck and lint at push. CI runs them all again on pull requests, because hooks can be skipped.

To run them by hand:

```bash
pnpm typecheck && pnpm lint && pnpm format:check && pnpm build
```

`pnpm format` applies Prettier formatting.
