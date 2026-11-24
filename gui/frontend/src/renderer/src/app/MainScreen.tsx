import { WidgetArea } from './WidgetArea'

/** The map console: left panel, map in the center, right panel. */
export function MainScreen(): React.JSX.Element {
  return (
    <div className="flex min-h-0 flex-1">
      <aside className="w-72 shrink-0 overflow-y-auto border-r border-border bg-panel">
        <WidgetArea at={{ screen: 'main', area: 'left' }} />
      </aside>
      <main className="relative min-w-0 flex-1">
        <WidgetArea at={{ screen: 'main', area: 'center' }} className="absolute inset-0" />
      </main>
      <aside className="w-80 shrink-0 overflow-y-auto border-l border-border bg-panel">
        <WidgetArea at={{ screen: 'main', area: 'right' }} />
      </aside>
    </div>
  )
}
