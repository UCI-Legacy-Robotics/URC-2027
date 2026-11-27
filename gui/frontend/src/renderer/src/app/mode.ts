import { createContext, useContext } from 'react'

/**
 * - `control`: running in the Electron app on the base laptop. Commands allowed.
 * - `viewer`: running in a browser on the base Wi-Fi. Read-only.
 */
export type Mode = 'control' | 'viewer'

/** Control mode only if the Electron preload script set the flag; anything else is a viewer. */
export function detectMode(): Mode {
  return window.baseStation?.mode === 'control' ? 'control' : 'viewer'
}

export const ModeContext = createContext<Mode>('viewer')

/** The current mode. Widgets disable their controls when this is `viewer`. */
export function useMode(): Mode {
  return useContext(ModeContext)
}
