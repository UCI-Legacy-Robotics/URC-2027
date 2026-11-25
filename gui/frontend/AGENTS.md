# GUI frontend

Read `docs/gui/architecture.md`, `docs/gui/widgets.md` and `docs/gui/design.md` before changing the shell or writing a widget.

Run from `gui/frontend/`: `pnpm dev` (Electron, control mode), `pnpm dev:web` (browser, viewer mode, port 5174), `pnpm storybook` (port 6006), `pnpm typecheck`, `pnpm lint`, `pnpm format`, `pnpm format:check`, `pnpm build`. CI runs typecheck, lint, format check, `electron-vite build` and `build-storybook`; lefthook runs ESLint and Prettier on commit and typecheck and lint on push.

There's no test suite. UI changes are verified in Storybook or the running app, so say so when you haven't looked at one.

## Running from an agent shell

- Node comes from nvm and may not be on `PATH`: run `export NVM_DIR=$HOME/.nvm; . $NVM_DIR/nvm.sh` in the same shell call.
- If `ELECTRON_RUN_AS_NODE=1` is set (inherited from VS Code), `pnpm dev` fails with `Cannot read properties of undefined (reading 'isPackaged')`. Launch with `env -u ELECTRON_RUN_AS_NODE pnpm dev`.
- The user often has their own `pnpm dev` or Storybook running. Check the port first (`ss -ltn`), and only stop processes you started; never `pkill electron` or kill whatever holds a port.

## Rules

- **Widgets are self-contained folders** under `src/renderer/src/widgets/<kebab-name>/` with `index.ts`, the component, stories, `data.ts`, `fixtures.ts` and `README.md`. They register through `defineWidget` in `app/registry.ts` and never import each other. Copy `widgets/link-status/`.
- **`data.ts` is the widget's request to the message schema.** Comment every field with units and update rate where known; make fields that may be missing optional.
- **Viewer mode never controls the rover.** Read the mode with `useMode()` and disable controls in viewer mode.
- **Theme tokens only.** Colors come from the `@theme` block in `src/renderer/src/assets/main.css`; Tailwind's default palette is removed. No hex values or arbitrary pixel values.
- **Build from `design/`** (StatusPill, `ui/` shadcn components, …) rather than raw elements. shadcn components are copied in with `pnpm dlx shadcn@latest add <component>` and restyled.
- **State** is Zustand in `data/`. **Map** is MapLibre GL with offline tiles; **plots** are uPlot. Don't add libraries outside the tech stack.
- **Offline:** nothing loads from outside the build folder. The CSP in `src/renderer/index.html` blocks it; don't loosen it.
- Prettier: single quotes, no semicolons, 100 columns, no trailing commas. Import renderer code through the `@renderer/` alias.
