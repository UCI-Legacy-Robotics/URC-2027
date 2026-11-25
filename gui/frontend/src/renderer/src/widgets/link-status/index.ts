import { defineWidget } from '@renderer/app/registry'
import { useVideoLink, type VideoLinkStatus } from '@renderer/data/videoLink'
import type { LinkStatusData, RadioLink } from './data'
import { LinkStatus } from './LinkStatus'

/** The rover answering at all means the 5 GHz link is up, whether or not it's streaming. */
function videoRadioLink(link: VideoLinkStatus | undefined): RadioLink | undefined {
  if (!link || link.state === 'unknown') return undefined
  return {
    state: link.state === 'unreachable' ? 'lost' : 'ok',
    lastSeenMs: link.lastSeenMs ?? undefined
  }
}

function useLinkStatusData(): LinkStatusData {
  return { video: videoRadioLink(useVideoLink().link) }
}

export default defineWidget<LinkStatusData>({
  id: 'link-status',
  title: 'Link status',
  area: 'top-bar', // top-bar widgets show on every screen
  order: 10,
  component: LinkStatus,
  // 5 GHz from the backend's rover probe. 900 MHz telemetry has no backend source yet.
  useData: useLinkStatusData
})
