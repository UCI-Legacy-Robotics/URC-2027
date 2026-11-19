import { useMode } from './app/mode'
import { WindowControls } from './app/WindowControls'

// Placeholder shell: the top bar, which is also the window's title bar in Electron.
// The real TopBar and panel layout come with the design components and widget registry.
function App(): React.JSX.Element {
  const mode = useMode()
  const isMac = window.baseStation?.platform === 'darwin'

  return (
    <header className={`top-bar${isMac ? ' top-bar--mac' : ''}`}>
      <span className="top-bar__title">LEGACY ROBOTICS</span>
      <div className="top-bar__right">
        <span className={`mode-pill mode-pill--${mode}`}>{mode.toUpperCase()}</span>
        <WindowControls />
      </div>
    </header>
  )
}

export default App
