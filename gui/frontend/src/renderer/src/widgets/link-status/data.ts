/**
 * Data the link-status widget needs. This is its request to the message schema:
 * keep it accurate when the widget changes.
 */

/**
 * Health of one radio link, as judged by the backend.
 * - `ok`: packets arriving normally
 * - `degraded`: packets late or lossy (threshold set by the backend)
 * - `lost`: no packets for longer than the timeout
 */
export type LinkState = 'ok' | 'degraded' | 'lost'

export interface RadioLink {
  state: LinkState
  /** Time since the last packet was received, in milliseconds. */
  lastSeenMs: number
  /** Received signal strength in dBm, if the radio reports it (RFD900x does). */
  rssiDbm?: number
}

/** Updated about once per second. */
export interface LinkStatusData {
  /** 900 MHz RFD900x link: MAVLink telemetry and all commands. Missing until first heard. */
  telemetry?: RadioLink
  /** 5 GHz link: video and media files. Missing until first heard. */
  video?: RadioLink
}
