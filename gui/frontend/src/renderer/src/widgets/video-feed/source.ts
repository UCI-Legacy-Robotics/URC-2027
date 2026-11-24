import { useEffect, useState } from 'react'
import { create } from 'zustand'
import { persist } from 'zustand/middleware'
import type { VideoFeedData } from './data'
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
}

/** The chosen source, saved in this browser or Electron profile (localStorage). */
export const useVideoSource = create<VideoSourceStore>()(
  persist(
    (set) => ({
      ...defaultSource,
      setSource: (source) => set(source)
    }),
    { name: 'video-feed.source' }
  )
)

/** Plays the chosen source and keeps it playing. The widget's `useData` hook. */
export function useVideoFeed(): VideoFeedData {
  const { baseUrl, path } = useVideoSource()
  const whepUrl = whepUrlFor(baseUrl, path)
  const [data, setData] = useState<VideoFeedData>({ state: 'connecting', whepUrl })

  useEffect(() => {
    const player = new WhepPlayer(whepUrl, path, setData)
    player.start()
    return () => player.stop()
  }, [whepUrl, path])

  // Until the new player reports, don't show the old source's state.
  return data.whepUrl === whepUrl ? data : { state: 'connecting', whepUrl }
}
