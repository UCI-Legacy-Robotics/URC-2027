import { contextBridge } from 'electron'

// Everything the Electron app exposes to the UI. The UI treats a missing
// `window.baseStation` as viewer mode, so only expose this from the Electron app.
const baseStation = Object.freeze({
  mode: 'control' as const
})

contextBridge.exposeInMainWorld('baseStation', baseStation)
