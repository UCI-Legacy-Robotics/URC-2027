# Writing a GUI widget

Everything you see in the base station GUI, apart from the shell, is a widget. This guide covers how to build one. For how widgets fit into the system, see [architecture.md](architecture.md); for the look, see [design.md](design.md).

You don't need the rover or the backend: widgets are built in Storybook against fake data.

## Folder

Each widget is one folder under `gui/frontend/src/renderer/src/widgets/`:

```
widgets/
└── <widget-name>/
    ├── <WidgetName>.tsx          # the component
    ├── <WidgetName>.stories.tsx  # every state, control + viewer mode
    ├── data.ts                   # type of the data this widget needs
    ├── fixtures.ts               # fake data: normal, warning, link lost, missing data
    └── README.md                 # what it shows, what commands it sends
```

Use kebab-case for the folder (`drive-teleop`) and PascalCase for components (`DriveTeleop.tsx`).

## Files

### `<WidgetName>.tsx`

The component. It takes its data as props or from the store, and reads the current mode (control or viewer). Build it from the base components in `design/` (Panel, SectionHeader, StatusPill, Button, DataField, TopBar) rather than raw elements.

### `data.ts`

A TypeScript type for the data the widget needs. **This is the widget's request to the message schema**: Ethan builds the telemetry messages from these files and the MAVLink dialect. Keep it accurate:

- one field per value the widget shows
- a comment on each field with its units and, if known, update rate
- optional fields (`?`) for values that may be missing, e.g. before the first message arrives

```ts
/** Battery readout for the top status bar. */
export type BatteryData = {
  /** Pack voltage, volts. ~1 Hz. */
  voltage: number
  /** State of charge, 0–100 %. ~1 Hz. */
  percent?: number
}
```

If the widget sends commands, list them in its `README.md` too, with their parameters.

### `fixtures.ts`

Fake data for each state the widget has to handle. At minimum:

| Fixture | What it covers |
| ------- | -------------- |
| normal | Typical values while everything works |
| warning | Values that should draw attention (low battery, high current) |
| link lost | Stale data after the radio link drops |
| missing data | Fields that haven't arrived yet |

Add more if the widget has other states worth seeing (e.g. autonomous vs. teleop).

### `<WidgetName>.stories.tsx`

One story per fixture. Use the Storybook toolbar toggle to check each story in both control and viewer mode.

### `README.md`

A short note: what the widget shows, which commands it sends, and its default panel location.

## Rules

- **Self-contained.** Everything a widget needs lives in its folder. Widgets register themselves with the shell through the widget registry and never import each other.
- **No commands in viewer mode.** Disable controls based on the mode flag. The backend blocks commands from viewers anyway, but the UI must not suggest viewers can control the rover.
- **Theme tokens only.** All colors, spacing and font sizes come from the Tailwind theme. Don't hard-code hex values or arbitrary pixel values.
- **No runtime internet.** No CDNs, web fonts or online map tiles. Bundle everything.
- **No new libraries** outside the [tech stack](architecture.md#tech-stack) without checking with Ethan.

## Checklist

Before opening a PR for a widget:

- [ ] Folder has all five files
- [ ] `data.ts` fields are commented with units (and rates where known)
- [ ] Fixtures for normal, warning, link lost and missing data
- [ ] Each story looks right in both control and viewer mode
- [ ] Controls are disabled in viewer mode
- [ ] No hard-coded colors or sizes
- [ ] Checks pass (see [gui/frontend/README.md](../../gui/frontend/README.md#checks))
