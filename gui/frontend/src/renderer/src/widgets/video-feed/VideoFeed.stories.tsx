import { useEffect, useState } from 'react'
import type { Decorator, Meta, StoryObj } from '@storybook/react-vite'
import type { VideoFeedData } from './data'
import * as fixtures from './fixtures'
import { useVideoFeed } from './source'
import { StatsPanel } from './StatsPanel'
import { VideoFeed } from './VideoFeed'

/** A moving test pattern as a MediaStream, standing in for the camera. */
function createTestPattern(): { stream: MediaStream; stop: () => void } {
  const canvas = document.createElement('canvas')
  canvas.width = 1280
  canvas.height = 720
  const ctx = canvas.getContext('2d')!
  const styles = getComputedStyle(document.documentElement)
  const bars = ['--color-fg-strong', '--color-primary', '--color-ok', '--color-fault'].map(
    (token) => styles.getPropertyValue(token)
  )
  let frame = 0
  let raf = 0
  const draw = (): void => {
    bars.forEach((color, i) => {
      ctx.fillStyle = color
      ctx.fillRect((i * canvas.width) / bars.length, 0, canvas.width / bars.length, canvas.height)
    })
    ctx.fillStyle = styles.getPropertyValue('--color-bg')
    ctx.fillRect((frame * 8) % canvas.width, 0, 40, canvas.height)
    frame += 1
    raf = requestAnimationFrame(draw)
  }
  draw()
  const stream = canvas.captureStream(30)
  return {
    stream,
    stop: () => {
      cancelAnimationFrame(raf)
      stream.getTracks().forEach((track) => track.stop())
    }
  }
}

/** Widget tile at a video-screen-like size. */
const tile: Decorator = (Story) => (
  <div className="h-96 w-160 border border-border">
    <Story />
  </div>
)

const meta = {
  title: 'Widgets/Video feed',
  component: VideoFeed,
  decorators: [tile],
  parameters: { layout: 'padded' }
} satisfies Meta<typeof VideoFeed>

export default meta
type Story = StoryObj<typeof meta>

/** Playing fixtures get the test pattern as their stream. */
function WithPattern({ data }: { data: VideoFeedData }): React.JSX.Element {
  const [pattern] = useState(createTestPattern)
  useEffect(() => pattern.stop, [pattern])
  return <VideoFeed data={{ ...data, stream: pattern.stream }} />
}

/** Failed fixtures count down from when the story opens. */
function Retrying({ data, inMs }: { data: VideoFeedData; inMs: number }): React.JSX.Element {
  const [retryAt] = useState(() => Date.now() + inMs)
  return <VideoFeed data={{ ...data, retryAt }} />
}

export const Normal: Story = {
  args: { data: fixtures.normal },
  render: (args) => <WithPattern data={args.data!} />
}

export const Warning: Story = {
  args: { data: fixtures.warning },
  render: (args) => <WithPattern data={args.data!} />
}

export const LinkLost: Story = { args: { data: fixtures.linkLost } }

export const NoPublisher: Story = {
  args: { data: fixtures.noPublisher },
  render: (args) => <Retrying data={args.data!} inMs={4000} />
}

export const ServerUnreachable: Story = {
  args: { data: fixtures.serverUnreachable },
  render: (args) => <Retrying data={args.data!} inMs={5000} />
}

export const RoverUnreachable: Story = {
  args: { data: fixtures.roverUnreachable },
  render: (args) => <Retrying data={args.data!} inMs={4000} />
}

export const RoverNoStream: Story = {
  args: { data: fixtures.roverNoStream },
  render: (args) => <Retrying data={args.data!} inMs={4000} />
}

export const BackendDown: Story = {
  args: { data: fixtures.backendDown },
  render: (args) => <Retrying data={args.data!} inMs={2000} />
}

export const MissingData: Story = { args: { data: fixtures.missingData } }

function Live(): React.JSX.Element {
  return <VideoFeed data={useVideoFeed()} />
}

/**
 * Connects for real, to the source set with the SOURCE button (default
 * `http://localhost:18889`, path `rover/front`). Shows "failed" with retries if nothing
 * is running there.
 */
export const LiveStream: Story = {
  args: { data: undefined },
  render: () => <Live />
}

/** The STATS panel on its own, with fixture history, at its default width. */
export const StatsSteady: Story = {
  args: { data: undefined },
  render: () => (
    <div className="flex h-full justify-end">
      <StatsPanel samples={fixtures.statsNormal} side="right" />
    </div>
  )
}

/** The panel underneath, as in a tile taller than its video (e.g. two tiles side by side). */
export const StatsUnderneath: Story = {
  args: { data: undefined },
  render: () => (
    <div className="flex h-full flex-col justify-end">
      <StatsPanel samples={fixtures.statsNormal} side="bottom" />
    </div>
  )
}

export const StatsFreezeThenBurst: Story = {
  args: { data: undefined },
  render: () => (
    <div className="flex h-full justify-end">
      <StatsPanel samples={fixtures.statsFreeze} side="right" />
    </div>
  )
}

export const StatsNoSamplesYet: Story = {
  args: { data: undefined },
  render: () => (
    <div className="flex h-full justify-end">
      <StatsPanel samples={fixtures.statsEmpty} side="right" />
    </div>
  )
}
