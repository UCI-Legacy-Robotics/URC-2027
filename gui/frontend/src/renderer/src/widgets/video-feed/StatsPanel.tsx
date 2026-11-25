import 'uplot/dist/uPlot.min.css'
import { useEffect, useRef, useState } from 'react'
import uPlot from 'uplot'
import type { StatsSample } from './data'

type Token = 'ok' | 'primary' | 'caution' | 'fault' | 'fg-strong'

interface Line {
  label: string
  value: (sample: StatsSample) => number | null
  color: Token
}

interface ChartSpec {
  title: string
  unit: string
  lines: Line[]
  digits?: number
}

const charts: ChartSpec[] = [
  {
    title: 'Radio loss',
    unit: '/s',
    lines: [
      { label: 'packets', value: (s) => s.radioPacketsLost, color: 'fault' },
      { label: 'bad frames', value: (s) => s.radioFramesInError, color: 'caution' }
    ]
  },
  {
    title: 'Bitrate',
    unit: 'Mb/s',
    digits: 1,
    lines: [
      {
        label: 'in',
        value: (s) => (s.bitrateKbps === null ? null : s.bitrateKbps / 1000),
        color: 'fg-strong'
      }
    ]
  },
  {
    title: 'Frame rate',
    unit: 'fps',
    lines: [{ label: 'decoded', value: (s) => s.fps, color: 'ok' }]
  },
  {
    title: 'Longest frame gap',
    unit: 'ms',
    lines: [{ label: 'gap', value: (s) => s.maxFrameGapMs, color: 'primary' }]
  },
  {
    title: 'Jitter buffer',
    unit: 'ms',
    lines: [{ label: 'buf', value: (s) => s.jitterBufferMs, color: 'fg-strong' }]
  },
  {
    title: 'Dropped frames',
    unit: '/s',
    lines: [{ label: 'dropped', value: (s) => s.framesDropped, color: 'caution' }]
  }
]

/** Theme colors, read from the CSS variables the Tailwind theme defines. */
function themeColor(token: Token | 'fg-muted' | 'border'): string {
  return getComputedStyle(document.documentElement).getPropertyValue(`--color-${token}`).trim()
}

function toData(spec: ChartSpec, samples: StatsSample[]): uPlot.AlignedData {
  return [samples.map((s) => s.t), ...spec.lines.map((line) => samples.map(line.value))]
}

function Chart({ spec, samples }: { spec: ChartSpec; samples: StatsSample[] }): React.JSX.Element {
  const plotRef = useRef<HTMLDivElement>(null)
  const chartRef = useRef<uPlot>(null)
  const samplesRef = useRef(samples)

  useEffect(() => {
    samplesRef.current = samples
    chartRef.current?.setData(toData(spec, samples))
  }, [spec, samples])

  useEffect(() => {
    const el = plotRef.current
    if (!el) return
    const muted = themeColor('fg-muted')
    const grid = themeColor('border')
    const axis: uPlot.Axis = {
      stroke: muted,
      grid: { stroke: grid, width: 1 },
      ticks: { show: false },
      font: '10px ui-monospace, monospace',
      size: 40
    }
    const chart = new uPlot(
      {
        width: el.clientWidth,
        height: el.clientHeight,
        legend: { show: false },
        cursor: { sync: { key: 'video-stats' }, points: { size: 4 } },
        scales: { y: { range: (_u, _min, max) => [0, Math.max(max ?? 0, 1) * 1.1] } },
        axes: [
          { ...axis, size: 18, space: 60, values: (_u, ticks) => ticks.map(() => '') },
          { ...axis, space: 20 }
        ],
        series: [
          {},
          ...spec.lines.map((line) => ({
            label: line.label,
            stroke: themeColor(line.color),
            width: 1.5,
            points: { show: false }
          }))
        ]
      },
      toData(spec, samplesRef.current),
      el
    )
    chartRef.current = chart
    const resize = new ResizeObserver(() =>
      chart.setSize({ width: el.clientWidth, height: el.clientHeight })
    )
    resize.observe(el)
    return () => {
      resize.disconnect()
      chart.destroy()
      chartRef.current = null
    }
  }, [spec])

  const latest = samples.at(-1)
  return (
    <div className="flex aspect-video min-w-0 flex-col bg-panel">
      <div className="flex items-baseline gap-2 px-2 pt-1 text-xs">
        <span className="truncate tracking-wider text-fg-muted uppercase">{spec.title}</span>
        <span className="ml-auto font-mono whitespace-nowrap text-fg-strong">
          {spec.lines
            .map((line) => {
              const value = latest ? line.value(latest) : null
              return value === null ? '—' : value.toFixed(spec.digits ?? 0)
            })
            .join(' / ')}{' '}
          <span className="text-fg-muted">{spec.unit}</span>
        </span>
      </div>
      <div ref={plotRef} className="min-h-0 min-w-0 flex-1 overflow-hidden" />
    </div>
  )
}

const widthKey = 'video-feed.stats-width'
const defaultWidth = 360
const minWidth = 240
/** Room the video keeps when the panel is dragged wide. */
const minVideoWidth = 240

function savedWidth(): number {
  const saved = Number(localStorage.getItem(widthKey))
  return saved >= minWidth ? saved : defaultWidth
}

/**
 * The last two minutes of video stats, one point per second, in a panel to the right of
 * the video. Drag its left edge to resize it; the width is remembered. Radio loss is
 * measured by the base MediaMTX; the rest by this window's player.
 */
export function StatsPanel({ samples }: { samples: StatsSample[] }): React.JSX.Element {
  const panelRef = useRef<HTMLDivElement>(null)
  const [width, setWidth] = useState(savedWidth)

  const startDrag = (event: React.PointerEvent<HTMLDivElement>): void => {
    const panel = panelRef.current
    const parent = panel?.parentElement
    if (!panel || !parent) return
    const right = panel.getBoundingClientRect().right
    const maxWidth = Math.max(minWidth, parent.clientWidth - minVideoWidth)
    const handle = event.currentTarget
    handle.setPointerCapture(event.pointerId)
    let latest = width
    const onMove = (move: PointerEvent): void => {
      latest = Math.min(maxWidth, Math.max(minWidth, right - move.clientX))
      setWidth(latest)
    }
    const onUp = (): void => {
      handle.removeEventListener('pointermove', onMove)
      handle.removeEventListener('pointerup', onUp)
      localStorage.setItem(widthKey, String(Math.round(latest)))
    }
    handle.addEventListener('pointermove', onMove)
    handle.addEventListener('pointerup', onUp)
  }

  return (
    <div ref={panelRef} className="relative flex shrink-0 border-l border-border" style={{ width }}>
      <div
        role="separator"
        aria-orientation="vertical"
        aria-label="Resize stats panel"
        className="absolute inset-y-0 -left-1 z-10 w-2 cursor-col-resize hover:bg-primary/40"
        onPointerDown={startDrag}
      />
      <div className="grid min-w-0 flex-1 grid-cols-2 content-start gap-px overflow-y-auto bg-border">
        {charts.map((spec) => (
          <Chart key={spec.title} spec={spec} samples={samples} />
        ))}
      </div>
    </div>
  )
}
