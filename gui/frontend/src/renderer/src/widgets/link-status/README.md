# link-status

Health of the two radio links as pills in the top bar: **900 MHz** (MAVLink telemetry and commands) and **5 GHz** (video). Green when OK, amber when degraded, red when lost; once a link isn't OK, the pill also shows the time since the last packet.

- **Area:** `top-bar`
- **Data:** [`data.ts`](data.ts), about 1 Hz
  - **5 GHz:** the backend probes the rover's MediaMTX every 2 s (`GET /api/video/link`, read through `data/videoLink.ts`). Lost when the rover doesn't answer; OK when it answers, even if it isn't streaming.
  - **900 MHz:** no source yet, so it shows `—` until the backend decodes telemetry.
- **Commands:** none
