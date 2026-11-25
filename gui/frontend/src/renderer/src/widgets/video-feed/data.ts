/**
 * Data the video-feed widget shows. None of it comes from the message schema: video goes
 * straight from MediaMTX to the UI over WebRTC and the widget's own WHEP client (whep.ts)
 * produces this, apart from the radio numbers in `StatsSample`, which come from the
 * backend's `/api/video/stats/<path>`.
 */

/**
 * - `connecting`: first attempt since the widget opened or its source changed
 * - `playing`: frames are arriving and being decoded
 * - `reconnecting`: the stream was playing and dropped; trying again
 * - `failed`: the last attempt failed; another starts at `retryAt`
 */
export type VideoConnectionState = 'connecting' | 'playing' | 'reconnecting' | 'failed'

/** Receiver-side numbers from `RTCPeerConnection.getStats()`, refreshed about once a second. */
export interface VideoStats {
  /** Decoded frames per second. */
  fps?: number
  /** Decoded frame size, pixels. */
  width?: number
  height?: number
  /** Received video bitrate, kbit/s. */
  bitrateKbps?: number
  /** Freezes the browser detected since this connection started. */
  freezeCount?: number
  /** Frames the browser received but didn't show, in the last second. */
  framesDropped?: number
  /** Average time frames waited in the jitter buffer in the last second, milliseconds. */
  jitterBufferMs?: number
}

/**
 * One second of the stats panel's history. `null` where there's no value, e.g. while the
 * stream isn't playing or the backend isn't reachable.
 */
export interface StatsSample {
  /** Seconds since epoch. */
  t: number
  fps: number | null
  /** kbit/s. */
  bitrateKbps: number | null
  /** Longest time between two frames shown on screen, milliseconds. 33 at a steady 30 fps. */
  maxFrameGapMs: number | null
  jitterBufferMs: number | null
  framesDropped: number | null
  /** RTP packets lost between the rover and the base MediaMTX, from the backend. */
  radioPacketsLost: number | null
  /** Frames the base MediaMTX received damaged, from the backend. */
  radioFramesInError: number | null
}

/**
 * Why there's no video, from the backend's probe of the base and the rover:
 * - `backend-down`: the backend doesn't answer, so the base MediaMTX isn't running either
 * - `base-down`: the backend is up but its MediaMTX isn't
 * - `rover-unreachable`: the rover doesn't answer over the 5 GHz link
 * - `no-stream`: the rover answers but isn't streaming (ROS video not running)
 */
export type VideoFaultCause = 'backend-down' | 'base-down' | 'rover-unreachable' | 'no-stream'

export interface VideoFeedData {
  state: VideoConnectionState
  /** WHEP endpoint being played, e.g. `http://localhost:8889/rover/front/whep`. */
  whepUrl: string
  /** The incoming video. Missing until the first track arrives. */
  stream?: MediaStream
  /** Why the last attempt failed or the stream dropped. */
  error?: string
  /** When the next attempt starts (ms since epoch), while `failed`. */
  retryAt?: number
  stats?: VideoStats
  /** Set while not playing, if the backend knows why. */
  cause?: VideoFaultCause
  /** The rover's 5 GHz address, for `rover-unreachable`. */
  roverHost?: string
}
