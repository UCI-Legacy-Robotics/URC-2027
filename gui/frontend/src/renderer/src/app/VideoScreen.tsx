import { WidgetArea } from './WidgetArea'

/** Video tiles filling their own window, meant for a second monitor. */
export function VideoScreen(): React.JSX.Element {
  return (
    <main className="min-h-0 flex-1 overflow-auto">
      <WidgetArea
        at={{ screen: 'video', area: 'grid' }}
        className="grid h-full auto-rows-fr grid-cols-[repeat(auto-fit,minmax(24rem,1fr))] gap-px bg-border"
      />
    </main>
  )
}
