# video-feed

One tile per rover camera (`instances`: front, arm), each playing from MediaMTX over WebRTC with its connection state, why it's missing when it is, and a stats panel of live graphs. See [Video feed widget](../../../../../../../docs/gui/video-feed.md) for how to read it.

- **Area:** `video` screen, `grid`
- **Data:** [`data.ts`](data.ts). Video comes straight from MediaMTX (WHEP), not the message schema; the rover link and radio loss come from the backend (`/api/video/link`, `/api/video/stats/<path>`)
- **Commands:** none. SOURCE and RELOAD only change what this tile plays, so they work in viewer mode too
