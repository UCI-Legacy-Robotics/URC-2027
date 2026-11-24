import { StatusPill, type StatusTone } from '@renderer/design/StatusPill'
import type { WidgetProps } from '@renderer/app/registry'
import type { LinkState, LinkStatusData, RadioLink } from './data'

const tone: Record<LinkState, StatusTone> = {
  ok: 'ok',
  degraded: 'caution',
  lost: 'fault'
}

/** "0.4s", "12s": time since the last packet, shown once a link isn't healthy. */
function formatAge(ms: number): string {
  return ms < 10_000 ? `${(ms / 1000).toFixed(1)}s` : `${Math.round(ms / 1000)}s`
}

function LinkPill({ label, link }: { label: string; link?: RadioLink }): React.JSX.Element {
  if (!link) {
    return <StatusPill tone="neutral">{label} —</StatusPill>
  }
  return (
    <StatusPill tone={tone[link.state]}>
      {label} {link.state}
      {link.state !== 'ok' && <span className="opacity-70">{formatAge(link.lastSeenMs)}</span>}
    </StatusPill>
  )
}

/** Health of the 900 MHz telemetry and 5 GHz video links. Read-only: sends no commands. */
export function LinkStatus({ data }: WidgetProps<LinkStatusData>): React.JSX.Element {
  return (
    <div className="flex items-center gap-2">
      <LinkPill label="900 MHz" link={data?.telemetry} />
      <LinkPill label="5 GHz" link={data?.video} />
    </div>
  )
}
