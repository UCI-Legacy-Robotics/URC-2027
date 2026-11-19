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
- Top status bar, left icon bar, left side panel, right detail panel and a video area. Side panels collapse.
- Every screen shows link state and the current mode (Teleop / Autonomous / Idle) prominently.

## Design tokens

Design tokens are named values (colors, spacing, font sizes, borders) that every widget uses instead of raw values. They're defined once in the Tailwind theme, so the whole app can be restyled in one place. Widgets never hard-code hex values or pixel sizes.

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
