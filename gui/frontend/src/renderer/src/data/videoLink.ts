import { useEffect } from 'react'
import { create } from 'zustand'
import { backendUrl } from './backend'

/**
 * The rover's video side as the backend's probe sees it:
 * - `ok`: the rover answered and is streaming the probed path
 * - `no-stream`: the rover answered over 5 GHz but has no stream (ROS video not running)
 * - `unreachable`: no answer from the rover (5 GHz link down or rover off)
 * - `unknown`: the backend hasn't probed yet
 */
export type RoverVideoState = 'ok' | 'no-stream' | 'unreachable' | 'unknown'

/** `GET /api/video/link` from the backend. */
export interface VideoLinkStatus {
  state: RoverVideoState
  detail: string | null
  roverHost: string
  /** Time since the rover last answered, ms. Null if it hasn't answered since the backend started. */
  lastSeenMs: number | null
  /** Whether the base MediaMTX is running. */
  mediamtx: boolean
}

interface VideoLinkStore {
  /** `down` when the backend doesn't answer. */
  backend: 'unknown' | 'up' | 'down'
  link?: VideoLinkStatus
}

export const useVideoLinkStore = create<VideoLinkStore>()(() => ({ backend: 'unknown' }))

const pollIntervalMs = 1000
let users = 0
let timer: ReturnType<typeof setInterval> | undefined

async function poll(): Promise<void> {
  try {
    const res = await fetch(`${backendUrl()}/api/video/link`, {
      signal: AbortSignal.timeout(pollIntervalMs * 0.9)
    })
    if (!res.ok) throw new Error(String(res.status))
    useVideoLinkStore.setState({ backend: 'up', link: (await res.json()) as VideoLinkStatus })
  } catch {
    useVideoLinkStore.setState({ backend: 'down', link: undefined })
  }
}

/**
 * The 5 GHz video link status, polled from the backend about once a second while any
 * component in this window uses it.
 */
export function useVideoLink(): VideoLinkStore {
  useEffect(() => {
    users += 1
    if (users === 1) {
      void poll()
      timer = setInterval(() => void poll(), pollIntervalMs)
    }
    return () => {
      users -= 1
      if (users === 0) clearInterval(timer)
    }
  }, [])
  return useVideoLinkStore()
}
