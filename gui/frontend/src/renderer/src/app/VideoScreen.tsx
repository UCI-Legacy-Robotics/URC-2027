import { WidgetArea } from './WidgetArea'

/**
 * Video tiles filling their own window, meant for a second monitor: every tile, or just
 * the one named by `view` (e.g. `arm`), so each camera can have a monitor of its own.
 */
export function VideoScreen({ view }: { view?: string }): React.JSX.Element {
  return (
    <main className="min-h-0 flex-1 overflow-auto">
      <WidgetArea
        at={{ screen: 'video', area: 'grid' }}
        instance={view}
        className="grid h-full auto-rows-fr grid-cols-[repeat(auto-fit,minmax(24rem,1fr))] gap-px bg-border"
      />
    </main>
  )
}
