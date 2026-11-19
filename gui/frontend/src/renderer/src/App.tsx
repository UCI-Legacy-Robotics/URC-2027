import { useMode } from './app/mode'

// Placeholder shell: an empty top bar showing the mode. The real TopBar and
// panel layout come with the design components and widget registry.
function App(): React.JSX.Element {
  const mode = useMode()

  return (
    <header className="top-bar">
      <span className="top-bar__title">LEGACY BASE STATION</span>
      <span className={`mode-pill mode-pill--${mode}`}>{mode.toUpperCase()}</span>
    </header>
  )
}

export default App
