# GUI design direction

The base station GUI is a dark, dense, map-centered operator console, inspired by the Anduril Lattice UI. It's our own design: we use our own colors, logo and names, and no Anduril branding or assets.

## Look

- Near-black background, dark gray panels, 1px subtle borders, square corners, dense spacing.
- Small UPPERCASE section headers with collapse arrows.
- Label-over-value data grids.
- Monospace UTC clock and numeric readouts in the top bar.

## Color

Semantic colors, used sparingly:

| Color | Meaning |
| ----- | ------- |
| Green | OK, online, executing |
| Amber | Primary action, selected item, caution |
| Red | Stop, cancel, fault |
| Cyan | Our rover and our assets |

- One amber primary-action button per panel.
- Status is shown as small outlined pills, e.g. `ONLINE`, `AUTONOMOUS`.

## Layout

- Map first: the map takes the center; everything else is a side panel or overlay.
- Split across screens, one window each, so they can go on separate monitors: the main screen (top status bar, left panel, map, right panel) and a video screen. See [architecture.md](architecture.md#shell-and-screens).
- Every screen shows link state and the current mode (Teleop / Autonomous / Idle) prominently.

## Design tokens

Design tokens are named values (colors, spacing, font sizes, borders) that every widget uses instead of raw values. They're defined once in the Tailwind theme, so the whole app can be restyled in one place. Widgets never hard-code hex values or pixel sizes.

To see every token and base component rendered, open the **style guide** in Storybook (`pnpm storybook`, then *Design / Style guide*). It reads the live values, so it always matches the theme.

The theme is the `@theme` block in `gui/frontend/src/renderer/src/assets/main.css`. Tailwind's default color palette is removed, so only these colors exist: `bg-panel` works, `bg-zinc-900` doesn't. The values are placeholders until task A2.

| Token | Tailwind classes | Use |
| ----- | ---------------- | --- |
| `bg` | `bg-bg` | App background |
| `panel` | `bg-panel` | Panels, top bar |
| `panel-raised` | `bg-panel-raised` | Hover, selected rows |
| `border` | `border-border` | 1px borders |
| `fg` | `text-fg` | Body text |
| `fg-muted` | `text-fg-muted` | Labels, secondary text |
| `fg-strong` | `text-fg-strong` | Values, emphasis |
| `ok` | `text-ok`, `bg-ok` | Green: OK, online, executing |
| `primary` | `text-primary`, `bg-primary` | Amber: primary action, selected item |
| `caution` | `text-caution`, `bg-caution` | Amber: caution |
| `fault` | `text-fault`, `bg-fault` | Red: stop, cancel, fault |
| `rover` | `text-rover`, `bg-rover` | Cyan: our rover and our assets |

Fonts are system stacks (`font-sans`, `font-mono`), because the field has no internet for web fonts. Spacing and text sizes use Tailwind's default scales (`p-2`, `gap-3`, `text-xs`).

## shadcn components

[shadcn](https://ui.shadcn.com/) components are copied into the repo, not installed as a package. To add one, from `gui/frontend`:

```bash
pnpm dlx shadcn@latest add <component>
```

- The source lands in `src/renderer/src/design/ui/` and is committed. It's ours from then on: restyle it there.
- Any packages it needs (Radix primitives, `cn`) are added to `package.json` and bundled at build time, so the app still runs offline. Only adding a component needs internet.
- shadcn's color names (`bg-background`, `bg-card`, `text-muted-foreground`, `bg-destructive`, …) are aliases of our tokens in `main.css`, so its components follow our theme. Use our token names in our own code.
- `cn()` (from shadcn's `cn` package) joins class names and lets later Tailwind classes override earlier ones: `cn('px-2', isWide && 'px-4')`.
- Corner radii are all 0, so `rounded-*` classes give square corners.

## Base components

Widgets are built from these, which live in `gui/frontend/src/renderer/src/design/` (shadcn/Radix, restyled):

| Component | Use |
| --------- | --- |
| Panel | Container for a widget, with border and header |
| SectionHeader | Small uppercase header with collapse arrow |
| StatusPill | Outlined status label |
| Button | Primary (amber), destructive (red) and plain variants |
| DataField | Label-over-value cell for data grids |
| TopBar | The top status bar |
