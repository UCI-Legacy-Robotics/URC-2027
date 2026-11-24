import { useEffect, useState } from 'react'

const baseButton = 'grid w-11 place-items-center text-fg-muted hover:text-fg-strong'
const buttonClass = `${baseButton} hover:bg-panel-raised`
const closeButtonClass = `${baseButton} hover:bg-fault`

/**
 * Minimize / maximize / close for the frameless Electron window, at the right end of
 * the top bar. Renders nothing in a browser, and nothing on macOS, which keeps its
 * own buttons.
 */
export function WindowControls(): React.JSX.Element | null {
  const bridge = window.baseStation
  const [maximized, setMaximized] = useState(false)

  useEffect(() => bridge?.window.onMaximizedChange(setMaximized), [bridge])

  if (!bridge || bridge.platform === 'darwin') return null

  return (
    // -mr-3 cancels the top bar's right padding so the buttons sit on the window edge.
    <div className="-mr-3 flex self-stretch no-drag-region [&_svg]:size-2.5 [&_svg]:fill-none [&_svg]:stroke-current">
      <button
        type="button"
        aria-label="Minimize"
        className={buttonClass}
        onClick={bridge.window.minimize}
      >
        <svg viewBox="0 0 10 10" aria-hidden="true">
          <path d="M1 5h8" />
        </svg>
      </button>
      <button
        type="button"
        aria-label={maximized ? 'Restore' : 'Maximize'}
        className={buttonClass}
        onClick={bridge.window.toggleMaximize}
      >
        <svg viewBox="0 0 10 10" aria-hidden="true">
          {maximized ? (
            <path d="M1.5 3.5h5v5h-5zM3.5 3.5v-2h5v5h-2" />
          ) : (
            <path d="M1.5 1.5h7v7h-7z" />
          )}
        </svg>
      </button>
      <button
        type="button"
        aria-label="Close"
        className={closeButtonClass}
        onClick={bridge.window.close}
      >
        <svg viewBox="0 0 10 10" aria-hidden="true">
          <path d="M1.5 1.5l7 7M8.5 1.5l-7 7" />
        </svg>
      </button>
    </div>
  )
}
