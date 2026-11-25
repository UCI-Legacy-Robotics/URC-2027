import { cn } from 'cn'
import { Button } from '@renderer/design/ui/button'
import { StatusPill } from '@renderer/design/StatusPill'
import { useMode } from './mode'
import { widgetsIn } from './registry'
import { openScreen, screenIds, screenTitle, type ScreenId, type ScreenView } from './screens'
import { WidgetArea } from './WidgetArea'
import { WindowControls } from './WindowControls'

/**
 * The screens and tiles the main screen can open in their own windows: each screen whole,
 * then each of its tiles (e.g. `Video · Front`), so every camera can get its own monitor.
 */
function openableViews(): ScreenView[] {
  return screenIds
    .filter((id): id is Exclude<ScreenId, 'main'> => id !== 'main')
    .flatMap((screen) => [
      { screen },
      ...widgetsIn({ screen, area: 'grid' }).flatMap((widget) =>
        (widget.instances ?? []).map((view) => ({ screen, view }))
      )
    ])
}

/**
 * The top status bar, on every screen. In Electron it's also the window's title bar:
 * drag it to move the window. Top-bar widgets render in the middle. The main screen
 * has buttons to open the other screens, or single tiles of them, in their own windows.
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
          openableViews().map((target) => (
            <Button
              key={`${target.screen}/${target.view ?? ''}`}
              variant="outline"
              size="xs"
              className="no-drag-region"
              title={`Open ${screenTitle(target)} in its own window`}
              onClick={() => openScreen(target.screen, target.view)}
            >
              {screenTitle(target).toUpperCase()}
            </Button>
          ))}
        <StatusPill tone={mode === 'control' ? 'primary' : 'neutral'}>{mode}</StatusPill>
        <WindowControls />
      </div>
    </header>
  )
}
