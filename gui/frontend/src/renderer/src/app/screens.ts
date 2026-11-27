import { useSyncExternalStore } from 'react'

/**
 * A screen is a full-window layout. Each screen opens in its own Electron window (or
 * browser tab), so it can go on its own monitor. The window's URL hash picks the screen:
 * `#/video` is the video screen, no hash is `main`.
 *
 * Planned: a `debug` screen that can only be opened in debug mode, never in mission
 * mode (see docs/gui/architecture.md).
 */
export const screenIds = ['main', 'video'] as const
export type ScreenId = (typeof screenIds)[number]

export const screenTitles: Record<ScreenId, string> = {
  main: 'Main',
  video: 'Video'
}

function screenFromHash(): ScreenId {
  const id = window.location.hash.replace(/^#\/?/, '')
  return (screenIds as readonly string[]).includes(id) ? (id as ScreenId) : 'main'
}

function subscribe(onChange: () => void): () => void {
  window.addEventListener('hashchange', onChange)
  return () => window.removeEventListener('hashchange', onChange)
}

/** The screen this window shows. Updates if the URL hash changes (e.g. in a browser tab). */
export function useScreen(): ScreenId {
  return useSyncExternalStore(subscribe, screenFromHash)
}

/** Opens a screen: a new Electron window, or a new tab in a browser. */
export function openScreen(screen: ScreenId): void {
  if (window.baseStation) {
    window.baseStation.window.openScreen(screen)
  } else {
    window.open(`#/${screen}`, '_blank')
  }
}
