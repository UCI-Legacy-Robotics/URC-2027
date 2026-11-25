import { useCallback, useEffect, useRef, useState, type RefObject } from 'react'
import type { StatsSample, VideoFeedData } from './data'

/** How much history the stats panel keeps, seconds. */
export const historySeconds = 120

/** The backend runs on the same machine as the base MediaMTX. */
const backendPort = 8000

interface RadioTotals {
  rtpPacketsLost: number
  inboundFramesInError?: number
}

/** The backend's stats URL for the stream a WHEP URL plays, or undefined if unparsable. */
export function radioStatsUrlFor(whepUrl: string): string | undefined {
  try {
    const url = new URL(whepUrl)
    const path = url.pathname.replace(/\/whep$/, '')
    return `${url.protocol}//${url.hostname}:${backendPort}/api/video/stats${path}`
  } catch {
    return undefined
  }
}

/**
 * Tracks the longest gap between frames shown by `video`, using the browser's per-frame
 * callback. `take()` returns the longest gap since the last call, counting a frame that's
 * still overdue, and starts a new interval.
 */
function useFrameGaps(videoRef: RefObject<HTMLVideoElement | null>): () => number | null {
  const state = useRef({ lastFrameAt: 0, maxGap: 0 })

  useEffect(() => {
    const video = videoRef.current
    if (!video) return
    let handle = 0
    const onFrame = (now: DOMHighResTimeStamp): void => {
      const s = state.current
      if (s.lastFrameAt) s.maxGap = Math.max(s.maxGap, now - s.lastFrameAt)
      s.lastFrameAt = now
      handle = video.requestVideoFrameCallback(onFrame)
    }
    handle = video.requestVideoFrameCallback(onFrame)
    return () => video.cancelVideoFrameCallback(handle)
  }, [videoRef])

  return useCallback(() => {
    const s = state.current
    if (!s.lastFrameAt) return null
    const gap = Math.max(s.maxGap, performance.now() - s.lastFrameAt)
    s.maxGap = 0
    return gap
  }, [])
}

/** One sample per second of the widget's stats, the last `historySeconds` of them. */
export function useStatsHistory(
  data: VideoFeedData | undefined,
  videoRef: RefObject<HTMLVideoElement | null>
): StatsSample[] {
  const [samples, setSamples] = useState<StatsSample[]>([])
  const dataRef = useRef(data)
  const takeGap = useFrameGaps(videoRef)

  useEffect(() => {
    dataRef.current = data
  })

  useEffect(() => {
    let lastRadio: RadioTotals | undefined
    let lastRadioUrl: string | undefined

    const fetchRadio = async (url: string | undefined): Promise<RadioTotals | undefined> => {
      if (!url) return undefined
      try {
        const res = await fetch(url, { signal: AbortSignal.timeout(900) })
        return res.ok ? ((await res.json()) as RadioTotals) : undefined
      } catch {
        return undefined
      }
    }

    const tick = async (): Promise<void> => {
      const current = dataRef.current
      const playing = current?.state === 'playing'
      const stats = playing ? current.stats : undefined
      const gap = takeGap()
      const url = current ? radioStatsUrlFor(current.whepUrl) : undefined
      const radio = await fetchRadio(url)
      const sameSource = url === lastRadioUrl && lastRadio && radio
      const sample: StatsSample = {
        t: Date.now() / 1000,
        fps: stats?.fps ?? null,
        bitrateKbps: stats?.bitrateKbps ?? null,
        maxFrameGapMs: playing ? gap : null,
        jitterBufferMs: stats?.jitterBufferMs ?? null,
        framesDropped: stats?.framesDropped ?? null,
        radioPacketsLost: sameSource ? radio.rtpPacketsLost - lastRadio!.rtpPacketsLost : null,
        radioFramesInError:
          sameSource && radio.inboundFramesInError !== undefined
            ? radio.inboundFramesInError - (lastRadio!.inboundFramesInError ?? 0)
            : null
      }
      lastRadio = radio
      lastRadioUrl = url
      setSamples((prev) => [...prev.slice(1 - historySeconds), sample])
    }

    const timer = setInterval(() => void tick(), 1000)
    return () => clearInterval(timer)
  }, [takeGap])

  return samples
}
