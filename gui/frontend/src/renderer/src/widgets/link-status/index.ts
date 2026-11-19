import { defineWidget } from '@renderer/app/registry'
import type { LinkStatusData } from './data'
import { normal } from './fixtures'
import { LinkStatus } from './LinkStatus'

export default defineWidget<LinkStatusData>({
  id: 'link-status',
  title: 'Link status',
  area: 'top-bar',
  order: 10,
  component: LinkStatus,
  // Fixture data until the backend exists; then this reads the store.
  useData: () => normal
})
