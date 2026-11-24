import { defineWidget } from '@renderer/app/registry'
import type { VideoFeedData } from './data'
import { useVideoFeed } from './source'
import { VideoFeed } from './VideoFeed'

export default defineWidget<VideoFeedData>({
  id: 'video-feed',
  title: 'Video feed',
  screen: 'video',
  area: 'grid',
  order: 10,
  component: VideoFeed,
  // Live from MediaMTX, not from the backend: the widget runs its own WebRTC client.
  useData: useVideoFeed
})
