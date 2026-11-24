import type { VideoFeedData } from './data'

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
    packetsLost: 0,
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
    packetsLost: 412,
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
