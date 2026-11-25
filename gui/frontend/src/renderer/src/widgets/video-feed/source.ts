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
 * The default is MediaMTX's WebRTC port (8889) on the machine serving the UI: the base
 * laptop for browser viewers, `localhost` in the Electron app. Today that's the rover's
 * MediaMTX run locally for testing; for the real rover, set the rover's IP. Once the base
 * station MediaMTX exists, the default becomes its port (18889).
 */
export const defaultSource = {
  baseUrl: `http://${window.location.hostname || 'localhost'}:8889`,
  path: 'rover/front'
}

export type VideoSource = typeof defaultSource

interface VideoSourceStore extends VideoSource {
  setSource: (source: VideoSource) => void
  /** Bumped by `reload()`; the player restarts whenever it changes. Not saved. */
  reloads: number
  /** Drops the connection and opens a new one at once, skipping any retry wait. */
  reload: () => void
}

/** The chosen source, saved in this browser or Electron profile (localStorage). */
export const useVideoSource = create<VideoSourceStore>()(
  persist(
    (set) => ({
      ...defaultSource,
      setSource: (source) => set(source),
      reloads: 0,
      reload: () => set((state) => ({ reloads: state.reloads + 1 }))
    }),
    {
      name: 'video-feed.source',
      partialize: ({ baseUrl, path }) => ({ baseUrl, path })
    }
  )
)

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

/** Plays the chosen source and keeps it playing. The widget's `useData` hook. */
export function useVideoFeed(): VideoFeedData {
  const { baseUrl, path, reloads } = useVideoSource()
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
