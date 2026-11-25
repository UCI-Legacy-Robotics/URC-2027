import { useEffect, useState } from 'react'
import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import { backendUrl } from '@renderer/data/backend'
import { useVideoLink, type VideoLinkStatus } from '@renderer/data/videoLink'
import type { VideoFaultCause, VideoFeedData } from './data'
import { WhepPlayer, whepUrlFor } from './whep'

/**
 * Where the stream comes from: a MediaMTX server's WebRTC address and a stream path.
 *
 * The default is the base station MediaMTX's WebRTC port (18889) on the machine serving
 * the UI: the base laptop for browser viewers, `localhost` in the Electron app. To play
 * straight from a rover MediaMTX instead, set its address with port 8889.
 */
export const defaultSource = {
  baseUrl: `http://${window.location.hostname || 'localhost'}:18889`,
  path: 'rover/front'
}

export type VideoSource = typeof defaultSource

/**
 * The rover's cameras, by MediaMTX path: one video tile each.
 * Keep in step with the backend's `BASE_ROVER_STREAM_PATHS` and the base MediaMTX config.
 */
export const cameras = [
  {
    id: 'front',
    path: 'rover/front',
    name: 'ZED 2i (front)',
    launch: 'Is ros2 launch rover_bringup video.launch.py running?'
  },
  {
    id: 'arm',
    path: 'rover/arm',
    name: 'OV9281 (arm)',
    launch: 'Is the video launch running with arm_camera:=true, and the OV9281 plugged in?'
  }
] as const

/** The video tiles, one per camera to start with. */
export const tiles: readonly string[] = cameras.map((camera) => camera.id)

/** The tile used when none is given, e.g. in stories. */
export const defaultTile = 'front'

/** A tile's source before anyone changes it: its own camera on the default server. */
export function defaultSourceFor(tile: string): VideoSource {
  const camera = cameras.find((c) => c.id === tile)
  return { ...defaultSource, path: camera?.path ?? defaultSource.path }
}

interface VideoSourceStore {
  /** Each tile's chosen source, by tile id. A tile that's missing uses its default. */
  sources: Record<string, VideoSource>
  /** Bumped per tile by `reload`; that tile's player restarts whenever it changes. Not saved. */
  reloads: Record<string, number>
  setSource: (tile: string, source: VideoSource) => void
  /** Drops the tile's connection and opens a new one at once, skipping any retry wait. */
  reload: (tile: string) => void
}

/** The chosen sources, saved in this browser or Electron profile (localStorage). */
const useVideoSources = create<VideoSourceStore>()(
  persist(
    (set) => ({
      sources: {},
      reloads: {},
      setSource: (tile, source) =>
        set((state) => ({ sources: { ...state.sources, [tile]: source } })),
      reload: (tile) =>
        set((state) => ({ reloads: { ...state.reloads, [tile]: (state.reloads[tile] ?? 0) + 1 } }))
    }),
    {
      name: 'video-feed.sources',
      partialize: ({ sources }) => ({ sources })
    }
  )
)

/** One tile's source and the actions on it. */
export function useVideoSource(tile: string = defaultTile): VideoSource & {
  reloads: number
  setSource: (source: VideoSource) => void
  reload: () => void
} {
  const source = useVideoSources((state) => state.sources[tile]) ?? defaultSourceFor(tile)
  const reloads = useVideoSources((state) => state.reloads[tile] ?? 0)
  const setSource = useVideoSources((state) => state.setSource)
  const reload = useVideoSources((state) => state.reload)
  return {
    ...source,
    reloads,
    setSource: (next) => setSource(tile, next),
    reload: () => reload(tile)
  }
}

const loopback = new Set(['localhost', '127.0.0.1', '[::1]'])

function sameHost(a: string, b: string): boolean {
  try {
    const [ha, hb] = [new URL(a).hostname, new URL(b).hostname]
    return ha === hb || (loopback.has(ha) && loopback.has(hb))
  } catch {
    return false
  }
}

/** The likely reason there's no video. Backend causes only count when playing from the base. */
function faultCause(
  whepUrl: string,
  path: string,
  backend: 'unknown' | 'up' | 'down',
  link: VideoLinkStatus | undefined
): VideoFaultCause | undefined {
  const viaBase = sameHost(whepUrl, backendUrl())
  if (backend === 'down') return viaBase ? 'backend-down' : undefined
  if (!link) return undefined
  if (viaBase && !link.mediamtx) return 'base-down'
  if (link.state === 'unreachable') return 'rover-unreachable'
  if (link.streams[path] === false) return 'no-stream'
  return undefined
}

/** Plays the tile's chosen source and keeps it playing. The widget's `useData` hook. */
export function useVideoFeed(tile?: string): VideoFeedData {
  const { baseUrl, path, reloads } = useVideoSource(tile)
  const whepUrl = whepUrlFor(baseUrl, path)
  const [data, setData] = useState<VideoFeedData>({ state: 'connecting', whepUrl })

  useEffect(() => {
    const player = new WhepPlayer(whepUrl, path, setData)
    player.start()
    return () => player.stop()
  }, [whepUrl, path, reloads])

  const { backend, link } = useVideoLink()

  // Until the new player reports, don't show the old source's state.
  const current: VideoFeedData = data.whepUrl === whepUrl ? data : { state: 'connecting', whepUrl }
  if (current.state === 'playing') return current
  return { ...current, cause: faultCause(whepUrl, path, backend, link), roverHost: link?.roverHost }
}
