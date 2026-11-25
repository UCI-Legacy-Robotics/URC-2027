import type { StatsSample, VideoFeedData } from './data'

const whepUrl = 'http://localhost:8889/rover/front/whep'

/**
 * Fake states for Storybook. They have no `stream`; the stories add a moving test
 * pattern to the playing ones.
 */

/** Playing at the rover's settings: 1280×720, 30 fps, 2 Mbit/s. */
export const normal: VideoFeedData = {
  state: 'playing',
  whepUrl,
  stats: {
    fps: 30,
    width: 1280,
    height: 720,
    bitrateKbps: 2010,
    freezeCount: 0,
    framesDropped: 0,
    jitterBufferMs: 18
  }
}

/** Playing, but the 5 GHz link is struggling: low frame rate, freezes, lost packets. */
export const warning: VideoFeedData = {
  state: 'playing',
  whepUrl,
  stats: {
    fps: 9,
    width: 1280,
    height: 720,
    bitrateKbps: 640,
    freezeCount: 7,
    framesDropped: 4,
    jitterBufferMs: 140
  }
}

/** The stream was playing and dropped, e.g. the rover pipeline restarted. */
export const linkLost: VideoFeedData = {
  state: 'reconnecting',
  whepUrl,
  error: 'Video stalled'
}

/** MediaMTX is up but nothing publishes the path yet (it answers 404). */
export const noPublisher: VideoFeedData = {
  state: 'failed',
  whepUrl,
  error: 'No stream at rover/front yet',
  retryAt: Date.now() + 4000
}

/** Nothing answers at the server address. */
export const serverUnreachable: VideoFeedData = {
  state: 'failed',
  whepUrl,
  error: "Can't reach localhost:8889",
  retryAt: Date.now() + 5000
}

/** First attempt, nothing received yet. */
export const missingData: VideoFeedData = {
  state: 'connecting',
  whepUrl
}

function history(freezeAt?: number): StatsSample[] {
  const start = 1_795_000_000
  return Array.from({ length: 120 }, (_, i) => {
    const wobble = Math.sin(i / 3)
    const sample: StatsSample = {
      t: start + i,
      fps: 30 + Math.round(wobble * 0.6),
      bitrateKbps: 1900 + wobble * 150,
      maxFrameGapMs: 40 + Math.abs(wobble) * 12,
      jitterBufferMs: 12 + Math.abs(wobble) * 4,
      framesDropped: 0,
      radioPacketsLost: i % 11 === 0 ? 1 : 0,
      radioFramesInError: 0
    }
    if (freezeAt !== undefined && i >= freezeAt && i < freezeAt + 2) {
      // Radio holds packets for ~1.5 s, then delivers them in a burst.
      return { ...sample, fps: 6, bitrateKbps: 120, maxFrameGapMs: 1450, radioPacketsLost: 0 }
    }
    if (freezeAt !== undefined && i === freezeAt + 2) {
      return { ...sample, fps: 52, bitrateKbps: 4100, maxFrameGapMs: 4, jitterBufferMs: 95 }
    }
    return sample
  })
}

/** Two minutes of steady playback with occasional single lost packets. */
export const statsNormal = history()

/** As normal, with the radio holding packets for 1.5 s and then delivering them at once. */
export const statsFreeze = history(80)

/** Just opened: no samples yet. */
export const statsEmpty: StatsSample[] = []
