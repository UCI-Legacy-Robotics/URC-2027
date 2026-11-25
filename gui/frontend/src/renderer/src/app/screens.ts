import { useMemo, useSyncExternalStore } from 'react'

/**
 * A screen is a full-window layout. Each screen opens in its own Electron window (or
 * browser tab), so it can go on its own monitor. The window's URL hash picks the screen:
 * `#/video` is the video screen with every tile, `#/video/arm` only the arm camera's
 * tile, no hash is `main`.
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

/** A window's screen, and for the video screen optionally one tile, e.g. `arm`. */
export interface ScreenView {
  screen: ScreenId
  /** Shows only this instance of the screen's widgets; all of them when undefined. */
  view?: string
}

const viewPattern = /^[a-z0-9-]+$/

/** `#/video/arm` → `{ screen: 'video', view: 'arm' }`; no or unknown hash → main. */
function parseHash(hash: string): ScreenView {
  const [id, view] = hash.replace(/^#\/?/, '').split('/')
  if (!(screenIds as readonly string[]).includes(id) || id === 'main') return { screen: 'main' }
  return { screen: id as ScreenId, view: view && viewPattern.test(view) ? view : undefined }
}

function subscribe(onChange: () => void): () => void {
  window.addEventListener('hashchange', onChange)
  return () => window.removeEventListener('hashchange', onChange)
}

/** The screen this window shows. Updates if the URL hash changes (e.g. in a browser tab). */
export function useScreen(): ScreenView {
  const hash = useSyncExternalStore(subscribe, () => window.location.hash)
  return useMemo(() => parseHash(hash), [hash])
}

/** "Video", or "Video · Arm" for one tile. */
export function screenTitle({ screen, view }: ScreenView): string {
  const title = screenTitles[screen]
  return view ? `${title} · ${view[0].toUpperCase()}${view.slice(1)}` : title
}

/**
 * Opens a screen, or one tile of it: a new Electron window (or focuses it if it's
 * already open), or a new tab in a browser.
 */
export function openScreen(screen: ScreenId, view?: string): void {
  if (window.baseStation) {
    window.baseStation.window.openScreen(screen, view)
  } else {
    window.open(`#/${screen}${view ? `/${view}` : ''}`, '_blank')
  }
}
