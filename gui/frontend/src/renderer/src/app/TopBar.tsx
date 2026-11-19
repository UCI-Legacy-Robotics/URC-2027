import { cn } from 'cn'
import { Button } from '@renderer/design/ui/button'
import { StatusPill } from '@renderer/design/StatusPill'
import { useMode } from './mode'
import { openScreen, screenIds, screenTitles, type ScreenId } from './screens'
import { WidgetArea } from './WidgetArea'
import { WindowControls } from './WindowControls'

/**
 * The top status bar, on every screen. In Electron it's also the window's title bar:
 * drag it to move the window. Top-bar widgets render in the middle. The main screen
 * has buttons to open the other screens in their own windows.
 */
export function TopBar({ screen }: { screen: ScreenId }): React.JSX.Element {
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
        {screen !== 'main' && (
          <span className="text-fg-muted"> · {screenTitles[screen].toUpperCase()}</span>
        )}
      </span>
      <WidgetArea at={{ area: 'top-bar' }} className="flex flex-1 items-center gap-2" />
      <div className="flex items-center gap-3 self-stretch pr-3">
        {screen === 'main' &&
          screenIds
            .filter((id) => id !== 'main')
            .map((id) => (
              <Button
                key={id}
                variant="outline"
                size="xs"
                className="no-drag-region"
                onClick={() => openScreen(id)}
              >
                {screenTitles[id].toUpperCase()}
              </Button>
            ))}
        <StatusPill tone={mode === 'control' ? 'primary' : 'rover'}>{mode}</StatusPill>
        <WindowControls />
      </div>
    </header>
  )
}
