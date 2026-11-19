import { cn } from 'cn'
import { StatusPill } from '@renderer/design/StatusPill'
import { useMode } from './mode'
import { WidgetArea } from './WidgetArea'
import { WindowControls } from './WindowControls'

/**
 * The top status bar. In Electron it's also the window's title bar: drag it to move
 * the window. Top-bar widgets render in the middle.
 */
export function TopBar(): React.JSX.Element {
  const mode = useMode()
  const isMac = window.baseStation?.platform === 'darwin'

  return (
    <header
      className={cn(
        'flex h-9 shrink-0 items-center gap-4 border-b border-border bg-panel drag-region select-none',
        isMac ? 'pl-20' : 'pl-3' // room for the native macOS window buttons
      )}
    >
      <span className="text-xs font-semibold tracking-wider">LEGACY ROBOTICS</span>
      <WidgetArea area="top-bar" className="flex flex-1 items-center gap-2" />
      <div className="flex items-center gap-3 self-stretch pr-3">
        <StatusPill tone={mode === 'control' ? 'primary' : 'rover'}>{mode}</StatusPill>
        <WindowControls />
      </div>
    </header>
  )
}
