import { useMode } from './app/mode'
import { WindowControls } from './app/WindowControls'

// Placeholder shell: the top bar, which is also the window's title bar in Electron.
// The real TopBar and panel layout come with the design components and widget registry.
function App(): React.JSX.Element {
  const mode = useMode()
  const isMac = window.baseStation?.platform === 'darwin'

  return (
    <header
      className={`flex h-9 items-center justify-between border-b border-border bg-panel drag-region select-none ${
        isMac ? 'pl-20' : 'pl-3' // room for the native macOS window buttons
      }`}
    >
      <span className="text-xs font-semibold tracking-wider">LEGACY ROBOTICS</span>
      <div className="flex items-center gap-3 self-stretch pr-3">
        <span
          className={`border px-2 py-0.5 font-mono text-2xs tracking-wider ${
            mode === 'control' ? 'text-primary' : 'text-rover'
          }`}
        >
          {mode.toUpperCase()}
        </span>
        <WindowControls />
      </div>
    </header>
  )
}

export default App
