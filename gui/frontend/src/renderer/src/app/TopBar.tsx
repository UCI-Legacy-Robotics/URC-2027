import { cn } from 'cn'
import { Button } from '@renderer/design/ui/button'
import { StatusPill } from '@renderer/design/StatusPill'
import { useMode } from './mode'
import { widgetsIn } from './registry'
import { openScreen, screenIds, screenTitle, showViews, type ScreenView } from './screens'
import { WidgetArea } from './WidgetArea'
import { WindowControls } from './WindowControls'

/**
 * Toggles for which tiles this window shows, one per instance of the screen's widgets
 * (e.g. FRONT and ARM on the video screen). At least one stays on.
 */
function ViewToggles({ current }: { current: ScreenView }): React.JSX.Element | null {
  if (current.screen === 'main') return null
  const screen = current.screen
  const all = widgetsIn({ screen, area: 'grid' }).flatMap((widget) => widget.instances ?? [])
  if (all.length < 2) return null
  const shown = current.views ?? all

  const toggle = (view: string): void => {
    const next = all.filter((each) =>
      each === view ? !shown.includes(each) : shown.includes(each)
    )
    showViews(screen, next.length === all.length ? undefined : next)
  }

  return (
    <div className="flex gap-1 no-drag-region" role="group" aria-label="Shown tiles">
      {all.map((view) => {
        const on = shown.includes(view)
        const last = on && shown.length === 1
        return (
          <Button
            key={view}
            variant={on ? 'default' : 'outline'}
            size="xs"
            // Green while the tile is shown in this window, so it reads at a glance.
            className={on ? 'bg-ok text-bg hover:bg-ok/90' : 'text-fg-muted'}
            aria-pressed={on}
            // Not `disabled`: that would dim the one tile that's showing.
            aria-disabled={last}
            title={last ? 'At least one stays shown' : `${on ? 'Hide' : 'Show'} ${view}`}
            onClick={() => !last && toggle(view)}
          >
            {view.toUpperCase()}
          </Button>
        )
      })}
    </div>
  )
}

/**
 * The top status bar, on every screen. In Electron it's also the window's title bar:
 * drag it to move the window. Top-bar widgets render in the middle. The main screen
 * has buttons that open the other screens in new windows; those screens have toggles for
 * which of their tiles to show.
 */
export function TopBar({ current }: { current: ScreenView }): React.JSX.Element {
  const mode = useMode()
  const isMac = window.baseStation?.platform === 'darwin'

  return (
    <header
      className={cn(
        'flex h-9 shrink-0 items-center gap-4 border-b border-border bg-panel drag-region select-none',
        isMac ? 'pl-20' : 'pl-3' // room for the native macOS window buttons
      )}
    >
      <span className="text-xs font-semibold tracking-wider whitespace-nowrap">
        LEGACY ROBOTICS
        {current.screen !== 'main' && (
          <span className="text-fg-muted"> · {screenTitle(current).toUpperCase()}</span>
        )}
      </span>
      <WidgetArea at={{ area: 'top-bar' }} className="flex flex-1 items-center gap-2" />
      <div className="flex items-center gap-3 self-stretch pr-3">
        {current.screen === 'main' &&
          screenIds
            .filter((id) => id !== 'main')
            .map((id) => (
              <Button
                key={id}
                variant="outline"
                size="xs"
                className="no-drag-region"
                title={`Open a new ${screenTitle({ screen: id }).toLowerCase()} window`}
                onClick={() => openScreen(id)}
              >
                {screenTitle({ screen: id }).toUpperCase()}
              </Button>
            ))}
        <ViewToggles current={current} />
        <StatusPill tone={mode === 'control' ? 'primary' : 'neutral'}>{mode}</StatusPill>
        <WindowControls />
      </div>
    </header>
  )
}
