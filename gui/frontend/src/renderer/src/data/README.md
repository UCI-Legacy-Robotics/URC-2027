# data

Zustand stores that widgets read through their `useData` hook, and how they're filled from the backend. Later also the WebSocket client.

- `backend.ts`: the backend's URL.
- `videoLink.ts`: the 5 GHz video link and the rover's stream as the backend probes them (`GET /api/video/link`). Used by the link-status badge and the video feed.
