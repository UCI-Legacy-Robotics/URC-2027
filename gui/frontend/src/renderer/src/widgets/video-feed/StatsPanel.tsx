import 'uplot/dist/uPlot.min.css'
import { useEffect, useRef, useState } from 'react'
import { cn } from 'cn'
import uPlot from 'uplot'
import type { StatsSample } from './data'
import { historySeconds } from './history'

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
  /** Counts keep 0 at the bottom; other graphs fit the y-axis to the data. */
  zeroBased?: boolean
  /** Smallest y-axis span, so steady values with tiny noise don't look like big swings. */
  minSpan: number
}

const charts: ChartSpec[] = [
  {
    title: 'Radio loss',
    unit: '/s',
    zeroBased: true,
    minSpan: 1,
    lines: [
      { label: 'packets', value: (s) => s.radioPacketsLost, color: 'fault' },
      { label: 'bad frames', value: (s) => s.radioFramesInError, color: 'caution' }
    ]
  },
  {
    title: 'Bitrate',
    unit: 'Mb/s',
    minSpan: 0.5,
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
    minSpan: 5,
    lines: [{ label: 'decoded', value: (s) => s.fps, color: 'ok' }]
  },
  {
    title: 'Longest frame gap',
    unit: 'ms',
    minSpan: 20,
    lines: [{ label: 'gap', value: (s) => s.maxFrameGapMs, color: 'primary' }]
  },
  {
    title: 'Jitter buffer',
    unit: 'ms',
    minSpan: 10,
    lines: [{ label: 'buf', value: (s) => s.jitterBufferMs, color: 'fg-strong' }]
  },
  {
    title: 'Dropped frames',
    unit: '/s',
    zeroBased: true,
    minSpan: 1,
    lines: [{ label: 'dropped', value: (s) => s.framesDropped, color: 'caution' }]
  }
]

/** Theme colors, read from the CSS variables the Tailwind theme defines. */
function themeColor(token: Token | 'fg-muted' | 'border'): string {
  return getComputedStyle(document.documentElement).getPropertyValue(`--color-${token}`).trim()
}

/** The y-axis range for the visible data. All values are non-negative. */
function fitRange(spec: ChartSpec, min: number | null, max: number | null): uPlot.Range.MinMax {
  if (min === null || max === null) return [0, spec.minSpan]
  if (spec.zeroBased) return [0, Math.max(max, spec.minSpan) * 1.1]
  const span = Math.max(max - min, spec.minSpan)
  const pad = span * 0.15
  const low = Math.max(0, (min + max) / 2 - span / 2 - pad)
  return [low, low + span + 2 * pad]
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
        scales: {
          // A fixed window that scrolls, so a short history isn't stretched across the plot.
          x: { range: (_u, _min, max) => (max === null ? [0, 1] : [max - historySeconds, max]) },
          y: { range: (_u, min, max) => fitRange(spec, min, max) }
        },
        axes: [
          { ...axis, size: 18, space: 60, values: (_u, ticks) => ticks.map(() => '') },
          { ...axis, space: 14 }
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

/** Which side of the video the panel sits on. */
export type StatsSide = 'right' | 'bottom'

/** Per side: where the size is saved, its default and minimum, and the room the video keeps. */
const sizing: Record<StatsSide, { key: string; initial: number; min: number; videoMin: number }> = {
  right: { key: 'video-feed.stats-width', initial: 360, min: 240, videoMin: 240 },
  bottom: { key: 'video-feed.stats-height', initial: 240, min: 160, videoMin: 160 }
}

/** Graphs per row and rows when the panel is underneath the video. */
const bottomColumns = 3
const bottomRows = Math.ceil(charts.length / bottomColumns)

/**
 * Underneath, the width of each 16:9 graph: as large as both rows fit in the panel's
 * height, but no wider than a third of the panel. Leftover width stays as a gap.
 */
function useBottomChartWidth(
  gridRef: React.RefObject<HTMLDivElement | null>,
  enabled: boolean
): number | undefined {
  const [width, setWidth] = useState<number>()
  useEffect(() => {
    const grid = gridRef.current
    if (!grid || !enabled) return
    const gap = 1 // gap-px
    const update = (): void => {
      const byWidth = (grid.clientWidth - gap * (bottomColumns - 1)) / bottomColumns
      const byHeight = ((grid.clientHeight - gap * (bottomRows - 1)) / bottomRows) * (16 / 9)
      setWidth(Math.max(0, Math.floor(Math.min(byWidth, byHeight))))
    }
    update()
    const observer = new ResizeObserver(update)
    observer.observe(grid)
    return () => observer.disconnect()
  }, [gridRef, enabled])
  return width
}

function savedSize(side: StatsSide): number {
  const { key, initial, min } = sizing[side]
  const saved = Number(localStorage.getItem(key))
  return saved >= min ? saved : initial
}

/**
 * The last minute of video stats, one point per second, in a panel to the right of or
 * under the video. Drag its inner edge to resize it; each side's size is remembered.
 * Radio loss is measured by the base MediaMTX; the rest by this window's player.
 */
export function StatsPanel({
  samples,
  side
}: {
  samples: StatsSample[]
  side: StatsSide
}): React.JSX.Element {
  const panelRef = useRef<HTMLDivElement>(null)
  const gridRef = useRef<HTMLDivElement>(null)
  const [sizes, setSizes] = useState(() => ({
    right: savedSize('right'),
    bottom: savedSize('bottom')
  }))
  const size = sizes[side]
  const horizontal = side === 'right'
  const bottomChartWidth = useBottomChartWidth(gridRef, !horizontal)

  const startDrag = (event: React.PointerEvent<HTMLDivElement>): void => {
    const panel = panelRef.current
    const parent = panel?.parentElement
    if (!panel || !parent) return
    const { min, videoMin, key } = sizing[side]
    const edge = horizontal
      ? panel.getBoundingClientRect().right
      : panel.getBoundingClientRect().bottom
    const max = Math.max(min, (horizontal ? parent.clientWidth : parent.clientHeight) - videoMin)
    const handle = event.currentTarget
    handle.setPointerCapture(event.pointerId)
    let latest = size
    const onMove = (move: PointerEvent): void => {
      latest = Math.min(max, Math.max(min, edge - (horizontal ? move.clientX : move.clientY)))
      setSizes((current) => ({ ...current, [side]: latest }))
    }
    const onUp = (): void => {
      handle.removeEventListener('pointermove', onMove)
      handle.removeEventListener('pointerup', onUp)
      localStorage.setItem(key, String(Math.round(latest)))
    }
    handle.addEventListener('pointermove', onMove)
    handle.addEventListener('pointerup', onUp)
  }

  return (
    <div
      ref={panelRef}
      className={cn('relative flex shrink-0 border-border', horizontal ? 'border-l' : 'border-t')}
      style={horizontal ? { width: size } : { height: size }}
    >
      <div
        role="separator"
        aria-orientation={horizontal ? 'vertical' : 'horizontal'}
        aria-label="Resize stats panel"
        className={cn(
          'absolute z-10 hover:bg-primary/40',
          horizontal
            ? 'inset-y-0 -left-1 w-2 cursor-col-resize'
            : 'inset-x-0 -top-1 h-2 cursor-row-resize'
        )}
        onPointerDown={startDrag}
      />
      <div
        ref={gridRef}
        className={cn(
          'grid min-w-0 flex-1 content-start gap-px bg-border',
          // On the right, graphs scale with the panel's width and it scrolls. Underneath,
          // they scale with its height (see useBottomChartWidth), leaving a gap beside them.
          horizontal ? 'grid-cols-2 overflow-y-auto' : 'min-h-0 justify-start'
        )}
        style={
          horizontal || bottomChartWidth === undefined
            ? undefined
            : { gridTemplateColumns: `repeat(${bottomColumns}, ${bottomChartWidth}px)` }
        }
      >
        {charts.map((spec) => (
          <Chart key={spec.title} spec={spec} samples={samples} />
        ))}
      </div>
    </div>
  )
}
