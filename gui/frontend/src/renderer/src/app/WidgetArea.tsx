import { cn } from 'cn'
import { widgetsIn, type AreaRef, type WidgetDefinition } from './registry'

/** Renders one widget (or one instance of it), feeding it from its data hook. */
function WidgetSlot({
  widget,
  instance
}: {
  widget: WidgetDefinition
  instance?: string
}): React.JSX.Element {
  const data = widget.useData(instance)
  const Widget = widget.component
  return <Widget data={data} instance={instance} />
}

/**
 * Renders every widget registered for an area. Shows a faint label when the area has no
 * widgets yet, so the layout stays visible.
 */
export function WidgetArea({
  at,
  instances,
  className
}: {
  at: AreaRef
  /** Shows only these instances of widgets that have instances; all of them when undefined. */
  instances?: readonly string[]
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
      {widgets.flatMap((widget) =>
        (
          widget.instances?.filter((each) => !instances || instances.includes(each)) ?? [undefined]
        ).map((each) => (
          <WidgetSlot key={`${widget.id}:${each ?? ''}`} widget={widget} instance={each} />
        ))
      )}
    </div>
  )
}
