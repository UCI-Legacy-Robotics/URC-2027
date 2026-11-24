/**
 * Data the video-feed widget shows. None of it comes from the backend or the message
 * schema: video goes straight from MediaMTX to the UI over WebRTC, and the widget's own
 * WHEP client (whep.ts) produces this.
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
  /** RTP packets lost since this connection started. */
  packetsLost?: number
  /** Average time frames wait in the jitter buffer, milliseconds. */
  jitterBufferMs?: number
}

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
}
