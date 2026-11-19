import type { ComponentType } from 'react'

/**
 * Where a widget goes. Top-bar widgets appear on every screen; everything else names
 * its screen and an area of that screen's layout. Layouts are fixed for v1.
 */
export type WidgetPlacement =
  | { area: 'top-bar' }
  | { screen: 'main'; area: 'left' | 'center' | 'right' }
  | { screen: 'video'; area: 'grid' }

/** Props every widget component gets: its data, or `undefined` before any arrives. */
export interface WidgetProps<TData> {
  data: TData | undefined
}

/** A widget's registration. Each widget folder default-exports one from its `index.ts`. */
export type WidgetDefinition<TData = unknown> = WidgetPlacement & {
  /** Unique, kebab-case; matches the folder name. */
  id: string
  /** Human-readable name, e.g. for panel headers. */
  title: string
  /** Sort order within the area, lowest first. */
  order: number
  component: ComponentType<WidgetProps<TData>>
  /**
   * Hook that supplies the widget's data. Returns fixture data until the backend
   * exists (Phase B), then reads from the store. Must follow the rules of hooks.
   */
  useData: () => TData | undefined
}

/** Type-checks a widget definition. Use as `export default defineWidget({ ... })`. */
export function defineWidget<TData>(widget: WidgetDefinition<TData>): WidgetDefinition<TData> {
  return widget
}

// Every widgets/<name>/index.ts, found at build time. Adding a widget folder registers it;
// nothing else needs editing.
const modules = import.meta.glob<{ default: WidgetDefinition }>('../widgets/*/index.ts', {
  eager: true
})

/** All registered widgets, sorted by order. */
export const widgets: WidgetDefinition[] = Object.values(modules)
  .map((module) => module.default)
  .sort((a, b) => a.order - b.order)

/** A place in the shell that widgets can fill. */
export type AreaRef = { area: 'top-bar' } | Exclude<WidgetPlacement, { area: 'top-bar' }>

/** The registered widgets for one area. */
export function widgetsIn(ref: AreaRef): WidgetDefinition[] {
  return widgets.filter(
    (widget) =>
      widget.area === ref.area &&
      ('screen' in ref ? 'screen' in widget && widget.screen === ref.screen : true)
  )
}
