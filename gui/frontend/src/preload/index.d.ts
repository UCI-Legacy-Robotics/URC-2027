/** What the Electron preload script exposes to the UI. */
export interface BaseStationBridge {
  readonly mode: 'control'
  /** Node's `process.platform`, e.g. `linux`, `win32`, `darwin`. */
  readonly platform: string
  /** Buttons for the frameless window's top bar. */
  readonly window: {
    minimize: () => void
    toggleMaximize: () => void
    close: () => void
    /**
     * Focuses the main window, or opens a new window for any other screen (one per
     * click, so each monitor can have one).
     */
    openScreen: (screen: 'main' | 'video') => void
    /** Called when the window is maximized or restored. Returns an unsubscribe function. */
    onMaximizedChange: (callback: (maximized: boolean) => void) => () => void
  }
}

declare global {
  interface Window {
    /** Set by the Electron preload script. Missing in a plain browser (viewer mode). */
    baseStation?: BaseStationBridge
  }
}
