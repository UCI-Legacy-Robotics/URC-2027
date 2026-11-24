import { cn } from 'cn'
import { widgetsIn, type AreaRef, type WidgetDefinition } from './registry'

/** Renders one widget, feeding it from its data hook. */
function WidgetSlot({ widget }: { widget: WidgetDefinition }): React.JSX.Element {
  const data = widget.useData()
  const Widget = widget.component
  return <Widget data={data} />
}

/**
 * Renders every widget registered for an area. Shows a faint label when the area has no
 * widgets yet, so the layout stays visible.
 */
export function WidgetArea({
  at,
  className
}: {
  at: AreaRef
  className?: string
}): React.JSX.Element {
  const widgets = widgetsIn(at)

  if (widgets.length === 0) {
    return (
      <div
        className={cn(
          'grid h-full place-items-center text-xs tracking-wider text-fg-muted uppercase opacity-40',
          className,
          // Keep the layout classes, but not a background meant to show between widgets.
          'bg-transparent'
        )}
      >
        {'screen' in at ? `${at.screen} · ${at.area}` : at.area}
      </div>
    )
  }

  return (
    <div className={className}>
      {widgets.map((widget) => (
        <WidgetSlot key={widget.id} widget={widget} />
      ))}
    </div>
  )
}
