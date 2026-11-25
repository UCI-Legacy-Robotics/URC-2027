import type { VideoFeedData, VideoStats } from './data'

/** Longest wait between attempts. The rover pipeline restarts in a few seconds. */
const maxRetryDelayMs = 5000
/** First wait after a failed attempt; doubles each failure up to the max. */
const firstRetryDelayMs = 500
/** An attempt that hasn't decoded a frame by then counts as failed. */
const firstFrameTimeoutMs = 10_000
/** A playing stream whose frame count hasn't moved for this long is dropped and reopened. */
const stallTimeoutMs = 4000
/** ICE `disconnected` often recovers by itself; drop the stream if it lasts longer. */
const disconnectGraceMs = 3000
/** ICE gathering usually finishes at once (host candidates only, no STUN offline). */
const iceGatheringTimeoutMs = 2000
/**
 * Gives up on the WHEP request after this long. Without it, a host that's off (no TCP
 * answer at all) leaves the request hanging for minutes while the OS retries. Longer than
 * the base MediaMTX's on-demand start timeout (10 s), which holds the request while it
 * pulls from the rover.
 */
const requestTimeoutMs = 12_000
const statsIntervalMs = 1000

/** Fields of `inbound-rtp` stats we read. Not all are in TypeScript's DOM types yet. */
interface InboundVideoStats {
  type: 'inbound-rtp'
  kind: string
  framesDecoded?: number
  framesPerSecond?: number
  frameWidth?: number
  frameHeight?: number
  bytesReceived?: number
  freezeCount?: number
  framesDropped?: number
  jitterBufferDelay?: number
  jitterBufferEmittedCount?: number
}

/** Counters from the previous stats poll, to turn running totals into per-second values. */
interface Counters {
  at: number
  bytes: number
  dropped: number
  jitterDelay: number
  jitterEmitted: number
}

/** The WHEP endpoint for a MediaMTX base URL and path. */
export function whepUrlFor(baseUrl: string, path: string): string {
  return `${baseUrl.replace(/\/+$/, '')}/${path.replace(/^\/+|\/+$/g, '')}/whep`
}

function hostOf(url: string): string {
  try {
    return new URL(url).host
  } catch {
    return url
  }
}

/** Resolves once ICE gathering is complete, or after a timeout, whichever comes first. */
function iceGatheringDone(pc: RTCPeerConnection): Promise<void> {
  if (pc.iceGatheringState === 'complete') return Promise.resolve()
  return new Promise((resolve) => {
    const done = (): void => {
      clearTimeout(timer)
      pc.removeEventListener('icegatheringstatechange', onChange)
      resolve()
    }
    const onChange = (): void => {
      if (pc.iceGatheringState === 'complete') done()
    }
    const timer = setTimeout(done, iceGatheringTimeoutMs)
    pc.addEventListener('icegatheringstatechange', onChange)
  })
}

async function describeHttpError(res: Response, path: string): Promise<string> {
  if (res.status === 404) return `No stream at ${path} yet`
  const body = (await res.text().catch(() => '')).trim()
  // MediaMTX answers errors as JSON: {"status":"error","error":"..."}
  let message = body
  try {
    message = (JSON.parse(body) as { error?: string }).error ?? body
  } catch {
    // not JSON: show the body as it is
  }
  return message ? message.slice(0, 120) : `Server answered ${res.status}`
}

/**
 * Plays one MediaMTX stream over WebRTC (WHEP) and keeps it playing: it retries with
 * backoff while the stream isn't up (MediaMTX answers 404 until a publisher exists) and
 * reopens it when it drops or stalls. Receive-only video, no audio.
 *
 * Plain TypeScript so it can run outside React; `useVideoFeed` wraps it.
 */
export class WhepPlayer {
  private data: VideoFeedData
  private stopped = false
  private hasPlayed = false
  private failures = 0

  // The current attempt.
  private pc?: RTCPeerConnection
  private sessionUrl?: string
  private abort?: AbortController
  private attemptStartedAt = 0
  private lastFrames = 0
  private lastFrameAt = 0
  private last?: Counters

  private retryTimer?: ReturnType<typeof setTimeout>
  private disconnectTimer?: ReturnType<typeof setTimeout>
  private statsTimer?: ReturnType<typeof setInterval>

  constructor(
    private readonly whepUrl: string,
    private readonly path: string,
    private readonly onChange: (data: VideoFeedData) => void
  ) {
    this.data = { state: 'connecting', whepUrl }
  }

  start(): void {
    void this.connect()
  }

  /** Closes the connection and ends the WHEP session. The player can't be restarted. */
  stop(): void {
    this.stopped = true
    clearTimeout(this.retryTimer)
    this.teardown()
  }

  private update(patch: Partial<VideoFeedData>): void {
    if (this.stopped) return
    this.data = { ...this.data, ...patch }
    this.onChange(this.data)
  }

  private async connect(): Promise<void> {
    // Keeps the last error on screen until the stream plays again.
    this.update({
      state: this.hasPlayed ? 'reconnecting' : 'connecting',
      retryAt: undefined,
      stats: undefined
    })

    // No ICE servers: the field has no internet, and the base and rover share a network.
    const pc = new RTCPeerConnection()
    const abort = new AbortController()
    this.pc = pc
    this.abort = abort
    this.sessionUrl = undefined
    this.attemptStartedAt = performance.now()
    this.lastFrames = 0
    this.lastFrameAt = this.attemptStartedAt
    this.last = undefined

    pc.addTransceiver('video', { direction: 'recvonly' })
    pc.addEventListener('track', (event) => {
      // Ask for the smallest jitter buffer the browser allows (Chromium supports this).
      if ('jitterBufferTarget' in event.receiver) event.receiver.jitterBufferTarget = 0
      this.update({ stream: event.streams[0] ?? new MediaStream([event.track]) })
    })
    pc.addEventListener('connectionstatechange', () => this.onConnectionState(pc))

    try {
      await pc.setLocalDescription(await pc.createOffer())
      await iceGatheringDone(pc)
      const res = await fetch(this.whepUrl, {
        method: 'POST',
        headers: { 'Content-Type': 'application/sdp' },
        body: pc.localDescription!.sdp,
        signal: AbortSignal.any([abort.signal, AbortSignal.timeout(requestTimeoutMs)])
      })
      if (!res.ok) throw new Error(await describeHttpError(res, this.path))
      const location = res.headers.get('Location')
      if (location) this.sessionUrl = new URL(location, this.whepUrl).href
      await pc.setRemoteDescription({ type: 'answer', sdp: await res.text() })
    } catch (err) {
      if (abort.signal.aborted || this.pc !== pc) return
      const host = hostOf(this.whepUrl)
      if (err instanceof DOMException && err.name === 'TimeoutError') {
        // Nothing answered: the host is off, unreachable, or the address is wrong.
        this.fail(`No answer from ${host}`)
      } else if (err instanceof TypeError) {
        // Refused (nothing listening on that port), blocked by CORS, or the address isn't
        // allowed by the page's Content-Security-Policy.
        this.fail(`Can't reach ${host}`)
      } else {
        this.fail(err instanceof Error ? err.message : String(err))
      }
      return
    }

    this.statsTimer = setInterval(() => void this.pollStats(pc), statsIntervalMs)
  }

  private onConnectionState(pc: RTCPeerConnection): void {
    if (this.pc !== pc) return
    clearTimeout(this.disconnectTimer)
    switch (pc.connectionState) {
      case 'failed':
      case 'closed':
        this.drop('Connection lost')
        break
      case 'disconnected':
        this.disconnectTimer = setTimeout(() => this.drop('Connection lost'), disconnectGraceMs)
        break
    }
  }

  private async pollStats(pc: RTCPeerConnection): Promise<void> {
    let video: InboundVideoStats | undefined
    try {
      const report = await pc.getStats()
      report.forEach((s: { type: string; kind?: string }) => {
        if (s.type === 'inbound-rtp' && s.kind === 'video') video = s as InboundVideoStats
      })
    } catch {
      return
    }
    if (this.pc !== pc) return

    const now = performance.now()
    const frames = video?.framesDecoded ?? 0
    if (frames > this.lastFrames) {
      this.lastFrames = frames
      this.lastFrameAt = now
    }

    if (frames === 0) {
      if (now - this.attemptStartedAt > firstFrameTimeoutMs) this.drop('No video received')
      return
    }
    if (now - this.lastFrameAt > stallTimeoutMs) {
      this.drop('Video stalled')
      return
    }

    const stats: VideoStats = {
      fps: video?.framesPerSecond,
      width: video?.frameWidth,
      height: video?.frameHeight,
      freezeCount: video?.freezeCount
    }
    const counters: Counters = {
      at: now,
      bytes: video?.bytesReceived ?? 0,
      dropped: video?.framesDropped ?? 0,
      jitterDelay: video?.jitterBufferDelay ?? 0,
      jitterEmitted: video?.jitterBufferEmittedCount ?? 0
    }
    const last = this.last
    if (last) {
      // bits per millisecond = kbit/s
      stats.bitrateKbps = ((counters.bytes - last.bytes) * 8) / (now - last.at)
      stats.framesDropped = counters.dropped - last.dropped
      const emitted = counters.jitterEmitted - last.jitterEmitted
      if (emitted > 0) {
        stats.jitterBufferMs = ((counters.jitterDelay - last.jitterDelay) / emitted) * 1000
      }
    }
    this.last = counters

    this.hasPlayed = true
    this.failures = 0
    this.update({ state: 'playing', error: undefined, stats })
  }

  /** The stream dropped: reopen it at once if it was playing, otherwise back off. */
  private drop(reason: string): void {
    if (this.data.state === 'playing') {
      this.teardown()
      this.update({ error: reason })
      void this.connect()
    } else {
      this.fail(reason)
    }
  }

  private fail(reason: string): void {
    this.teardown()
    const delay = Math.min(firstRetryDelayMs * 2 ** this.failures, maxRetryDelayMs)
    this.failures += 1
    this.update({ state: 'failed', error: reason, retryAt: Date.now() + delay, stats: undefined })
    clearTimeout(this.retryTimer)
    this.retryTimer = setTimeout(() => void this.connect(), delay)
  }

  private teardown(): void {
    clearInterval(this.statsTimer)
    clearTimeout(this.disconnectTimer)
    this.abort?.abort()
    if (this.sessionUrl) {
      // Closing the peer connection usually ends the server's session too, but not if the
      // link is down; DELETE makes sure. If the close got there first, it answers 404.
      fetch(this.sessionUrl, { method: 'DELETE', keepalive: true }).catch(() => {})
      this.sessionUrl = undefined
    }
    this.pc?.close()
    this.pc = undefined
    this.update({ stream: undefined })
  }
}
