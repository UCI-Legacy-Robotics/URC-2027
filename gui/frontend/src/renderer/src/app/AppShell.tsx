import { TopBar } from './TopBar'
import { WidgetArea } from './WidgetArea'

/**
 * The fixed v1 layout: top bar, left panel, center (map), right panel, and a video
 * strip under the center. Widgets fill the areas from the registry.
 */
export function AppShell(): React.JSX.Element {
  return (
    <div className="flex h-full flex-col">
      <TopBar />
      <div className="flex min-h-0 flex-1">
        <aside className="w-72 shrink-0 overflow-y-auto border-r border-border bg-panel">
          <WidgetArea area="left" />
        </aside>
        <main className="flex min-w-0 flex-1 flex-col">
          <div className="relative min-h-0 flex-1">
            <WidgetArea area="center" className="absolute inset-0" />
          </div>
          <div className="h-48 shrink-0 border-t border-border">
            <WidgetArea area="video" />
          </div>
        </main>
        <aside className="w-80 shrink-0 overflow-y-auto border-l border-border bg-panel">
          <WidgetArea area="right" />
        </aside>
      </div>
    </div>
  )
}
