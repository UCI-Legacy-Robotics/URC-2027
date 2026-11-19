# link-status

Health of the two radio links as pills in the top bar: **900 MHz** (MAVLink telemetry and commands) and **5 GHz** (video). Green when OK, amber when degraded, red when lost; once a link isn't OK, the pill also shows the time since the last packet.

- **Area:** `top-bar`
- **Data:** [`data.ts`](data.ts), about 1 Hz
- **Commands:** none
