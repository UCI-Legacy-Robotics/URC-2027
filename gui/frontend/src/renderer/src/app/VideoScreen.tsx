import { WidgetArea } from './WidgetArea'

/**
 * Video tiles filling their own window, meant for another monitor: every tile, or the
 * ones in `views` (chosen with the top bar's camera toggles).
 */
export function VideoScreen({ views }: { views?: readonly string[] }): React.JSX.Element {
  return (
    <main className="min-h-0 flex-1 overflow-auto">
      <WidgetArea
        at={{ screen: 'video', area: 'grid' }}
        instances={views}
        className="grid h-full auto-rows-fr grid-cols-[repeat(auto-fit,minmax(24rem,1fr))] gap-px bg-border"
      />
    </main>
  )
}
