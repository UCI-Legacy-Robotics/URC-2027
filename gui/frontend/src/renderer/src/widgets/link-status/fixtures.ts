import type { LinkStatusData } from './data'

/** Both links healthy. */
export const normal: LinkStatusData = {
  telemetry: { state: 'ok', lastSeenMs: 120, rssiDbm: -72 },
  video: { state: 'ok', lastSeenMs: 40 }
}

/** Telemetry is late and weak; video still fine. */
export const warning: LinkStatusData = {
  telemetry: { state: 'degraded', lastSeenMs: 1800, rssiDbm: -101 },
  video: { state: 'ok', lastSeenMs: 60 }
}

/** Both links dropped, e.g. the rover drove out of line of sight. */
export const linkLost: LinkStatusData = {
  telemetry: { state: 'lost', lastSeenMs: 14_000, rssiDbm: -118 },
  video: { state: 'lost', lastSeenMs: 9_500 }
}

/** Nothing heard yet: right after startup, before the radios connect. */
export const missingData: LinkStatusData = {}
