import type { ComponentType } from 'react'

/** Where in the shell a widget goes. The layout is fixed for v1. */
export type ShellArea = 'top-bar' | 'left' | 'center' | 'right' | 'video'

/** Props every widget component gets: its data, or `undefined` before any arrives. */
export interface WidgetProps<TData> {
  data: TData | undefined
}

/** A widget's registration. Each widget folder default-exports one from its `index.ts`. */
export interface WidgetDefinition<TData = unknown> {
  /** Unique, kebab-case; matches the folder name. */
  id: string
  /** Human-readable name, e.g. for panel headers. */
  title: string
  /** Default shell area. */
  area: ShellArea
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

/** All registered widgets, sorted by area order. */
export const widgets: WidgetDefinition[] = Object.values(modules)
  .map((module) => module.default)
  .sort((a, b) => a.order - b.order)

/** The registered widgets for one shell area. */
export function widgetsIn(area: ShellArea): WidgetDefinition[] {
  return widgets.filter((widget) => widget.area === area)
}
