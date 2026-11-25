import { useMemo, useSyncExternalStore } from 'react'

/**
 * A screen is a full-window layout. Each screen opens in its own Electron window (or
 * browser tab), so it can go on its own monitor. The window's URL hash picks the screen
 * and its tiles: `#/video` is the video screen with every tile, `#/video/arm` only the
 * arm camera's, no hash is `main`.
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

/** A window's screen, and which of its tiles it shows. */
export interface ScreenView {
  screen: ScreenId
  /**
   * The instances of the screen's widgets to show, e.g. `['arm']`; every one when
   * undefined. Kept in the URL hash, so a reload keeps the window's choice.
   */
  views?: readonly string[]
}

const viewPattern = /^[a-z0-9-]+$/

/** `#/video/front,arm` → `{ screen: 'video', views: ['front', 'arm'] }`; no or unknown hash → main. */
function parseHash(hash: string): ScreenView {
  const [id, list] = hash.replace(/^#\/?/, '').split('/')
  if (!(screenIds as readonly string[]).includes(id) || id === 'main') return { screen: 'main' }
  const views = (list ?? '').split(',').filter((view) => viewPattern.test(view))
  return { screen: id as ScreenId, views: views.length ? views : undefined }
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

/** "Video", or "Video · Arm" when the window shows only some tiles. */
export function screenTitle({ screen, views }: ScreenView): string {
  const title = screenTitles[screen]
  if (!views) return title
  return `${title} · ${views.map((view) => view[0].toUpperCase() + view.slice(1)).join(', ')}`
}

/**
 * Changes which tiles this window shows, keeping it in the URL hash without adding a
 * history entry. Undefined shows every tile.
 */
export function showViews(screen: ScreenId, views: readonly string[] | undefined): void {
  window.location.replace(`#/${screen}${views ? `/${views.join(',')}` : ''}`)
}

/**
 * Opens a screen: the main window is focused if it's open; any other screen opens a new
 * Electron window each time (or a new browser tab), so each monitor can have one.
 */
export function openScreen(screen: ScreenId): void {
  if (window.baseStation) {
    window.baseStation.window.openScreen(screen)
  } else {
    window.open(`#/${screen}`, '_blank')
  }
}
