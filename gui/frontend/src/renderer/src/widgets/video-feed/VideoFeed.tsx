import { useEffect, useRef, useState } from 'react'
import type { WidgetProps } from '@renderer/app/registry'
import { StatusPill, type StatusTone } from '@renderer/design/StatusPill'
import { Button } from '@renderer/design/ui/button'
import type { VideoConnectionState, VideoFaultCause, VideoFeedData, VideoStats } from './data'
import { useStatsHistory } from './history'
import { defaultSource, useVideoSource, type VideoSource } from './source'
import { StatsPanel } from './StatsPanel'

const tone: Record<VideoConnectionState, StatusTone> = {
  connecting: 'neutral',
  playing: 'ok',
  reconnecting: 'caution',
  failed: 'fault'
}

/** "30 fps · 1280×720 · 2.0 Mb/s · 3 freezes" */
function formatStats(stats: VideoStats): string {
  const parts: string[] = []
  if (stats.fps !== undefined) parts.push(`${Math.round(stats.fps)} fps`)
  if (stats.width && stats.height) parts.push(`${stats.width}×${stats.height}`)
  if (stats.bitrateKbps !== undefined) parts.push(`${(stats.bitrateKbps / 1000).toFixed(1)} Mb/s`)
  if (stats.jitterBufferMs !== undefined) parts.push(`buf ${Math.round(stats.jitterBufferMs)} ms`)
  if (stats.freezeCount)
    parts.push(`${stats.freezeCount} freeze${stats.freezeCount > 1 ? 's' : ''}`)
  return parts.join(' · ')
}

/** Seconds until `at`, re-rendering every half second while it's in the future. */
function useSecondsUntil(at: number | undefined): number | undefined {
  const [now, setNow] = useState(() => Date.now())
  useEffect(() => {
    if (at === undefined) return
    const timer = setInterval(() => setNow(Date.now()), 500)
    return () => clearInterval(timer)
  }, [at])
  return at === undefined ? undefined : Math.max(0, Math.ceil((at - now) / 1000))
}

/** Edits the MediaMTX address and stream path. Only changes what this screen plays. */
function SourceForm({ onDone }: { onDone: () => void }): React.JSX.Element {
  const { baseUrl, path, setSource } = useVideoSource()
  const [draft, setDraft] = useState<VideoSource>({ baseUrl, path })

  let invalid: string | undefined
  try {
    const url = new URL(draft.baseUrl)
    if (url.protocol !== 'http:' && url.protocol !== 'https:') invalid = 'Use http:// or https://'
  } catch {
    invalid = 'Not a URL'
  }
  if (!invalid && !draft.path.trim()) invalid = 'Path is empty'

  const input =
    'min-w-0 border border-border bg-bg px-2 py-1 font-mono text-xs text-fg-strong outline-none focus:border-primary'

  return (
    <form
      className="flex flex-wrap items-center gap-2 border-b border-border bg-panel px-2 py-1.5"
      onSubmit={(event) => {
        event.preventDefault()
        if (invalid) return
        setSource({ baseUrl: draft.baseUrl.trim(), path: draft.path.trim() })
        onDone()
      }}
    >
      <label className="flex min-w-0 flex-[2] items-center gap-2 text-xs text-fg-muted">
        Server
        <input
          className={`${input} flex-1`}
          value={draft.baseUrl}
          placeholder={defaultSource.baseUrl}
          spellCheck={false}
          onChange={(event) => setDraft({ ...draft, baseUrl: event.target.value })}
        />
      </label>
      <label className="flex min-w-0 flex-1 items-center gap-2 text-xs text-fg-muted">
        Path
        <input
          className={`${input} flex-1`}
          value={draft.path}
          placeholder={defaultSource.path}
          spellCheck={false}
          onChange={(event) => setDraft({ ...draft, path: event.target.value })}
        />
      </label>
      {invalid && <span className="text-xs text-fault">{invalid}</span>}
      <Button type="submit" size="xs" disabled={!!invalid}>
        APPLY
      </Button>
      <Button type="button" variant="outline" size="xs" onClick={() => setDraft(defaultSource)}>
        DEFAULT
      </Button>
    </form>
  )
}

/** What's wrong and what to check, for each cause the backend can tell apart. */
function describeCause(cause: VideoFaultCause, roverHost?: string): [string, string] {
  switch (cause) {
    case 'backend-down':
      return ['Backend not running', 'Start it from gui/backend: uv run uvicorn app.main:app']
    case 'base-down':
      return ['Base MediaMTX not running', "The backend is up but MediaMTX isn't; see its log"]
    case 'rover-unreachable':
      return [
        `Rover unreachable${roverHost ? ` at ${roverHost}` : ''}`,
        'Check the 5 GHz link and that the rover is powered'
      ]
    case 'no-stream':
      return ['Rover has no camera stream', 'The rover answers; is the ROS video launch running?']
  }
}

/** What to show over the video while it isn't playing. */
function StatusOverlay({ data }: { data: VideoFeedData }): React.JSX.Element {
  const retryIn = useSecondsUntil(data.state === 'failed' ? data.retryAt : undefined)
  const headline = {
    connecting: 'Connecting…',
    playing: '',
    reconnecting: 'Reconnecting…',
    failed: data.error ?? 'Failed'
  }[data.state]
  const detail =
    data.state === 'failed' ? (retryIn ? `Retrying in ${retryIn}s` : 'Retrying…') : data.error

  const cause = data.cause && describeCause(data.cause, data.roverHost)

  return (
    <div className="absolute inset-0 grid place-content-center gap-1 text-center">
      {cause && (
        <>
          <span className="text-sm text-fault">{cause[0]}</span>
          <span className="mb-2 text-xs text-fg">{cause[1]}</span>
        </>
      )}
      <span
        className={
          cause
            ? 'text-xs text-fg-muted'
            : data.state === 'failed'
              ? 'text-sm text-fault'
              : 'text-sm text-fg-strong'
        }
      >
        {headline}
      </span>
      {detail && <span className="text-xs text-fg-muted">{detail}</span>}
      <span className="font-mono text-xs text-fg-muted opacity-60">{data.whepUrl}</span>
    </div>
  )
}

/**
 * One camera stream from MediaMTX over WebRTC, with its connection state and receive
 * stats. Read-only: sends no commands. The source can be changed in both control and
 * viewer mode, since it only changes what this screen plays.
 */
export function VideoFeed({ data }: WidgetProps<VideoFeedData>): React.JSX.Element {
  const videoRef = useRef<HTMLVideoElement>(null)
  const [editing, setEditing] = useState(false)
  const [showStats, setShowStats] = useState(false)
  const samples = useStatsHistory(data, videoRef)
  const path = useVideoSource((source) => source.path)
  const stream = data?.stream

  useEffect(() => {
    const video = videoRef.current
    if (video && video.srcObject !== (stream ?? null)) video.srcObject = stream ?? null
  }, [stream])

  const playing = data?.state === 'playing'

  return (
    <section className="flex h-full min-h-0 flex-col bg-panel">
      <header className="flex h-8 shrink-0 items-center gap-2 border-b border-border px-2">
        <span className="truncate text-xs font-semibold tracking-wider text-fg-strong uppercase">
          {path}
        </span>
        {data && <StatusPill tone={tone[data.state]}>{data.state}</StatusPill>}
        <span className="min-w-0 flex-1 truncate text-right font-mono text-xs text-fg-muted">
          {playing && data.stats ? formatStats(data.stats) : ''}
        </span>
        <Button
          variant={showStats ? 'secondary' : 'outline'}
          size="xs"
          aria-expanded={showStats}
          onClick={() => setShowStats(!showStats)}
        >
          STATS
        </Button>
        <Button
          variant={editing ? 'secondary' : 'outline'}
          size="xs"
          aria-expanded={editing}
          onClick={() => setEditing(!editing)}
        >
          SOURCE
        </Button>
      </header>
      {editing && <SourceForm onDone={() => setEditing(false)} />}
      <div className="flex min-h-0 flex-1">
        <div className="relative min-w-0 flex-1 bg-bg">
          {/* Muted, inline autoplay: browsers allow it without a click, and it adds no buffering. */}
          <video
            ref={videoRef}
            className="absolute inset-0 h-full w-full object-contain"
            autoPlay
            muted
            playsInline
            disablePictureInPicture
          />
          {data && !playing && <StatusOverlay data={data} />}
        </div>
        {showStats && <StatsPanel samples={samples} />}
      </div>
    </section>
  )
}
