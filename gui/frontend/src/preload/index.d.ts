/** What the Electron preload script exposes to the UI. */
export interface BaseStationBridge {
  readonly mode: 'control'
}

declare global {
  interface Window {
    /** Set by the Electron preload script. Missing in a plain browser (viewer mode). */
    baseStation?: BaseStationBridge
  }
}
