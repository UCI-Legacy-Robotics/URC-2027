import { contextBridge, ipcRenderer, type IpcRendererEvent } from 'electron'
import type { BaseStationBridge } from './index.d'

// Everything the Electron app exposes to the UI. The UI treats a missing
// `window.baseStation` as viewer mode, so only expose this from the Electron app.
// Keep it narrow: named functions only, never ipcRenderer itself.
const baseStation: BaseStationBridge = {
  mode: 'control',
  platform: process.platform,
  window: {
    minimize: () => ipcRenderer.send('window:minimize'),
    toggleMaximize: () => ipcRenderer.send('window:toggle-maximize'),
    close: () => ipcRenderer.send('window:close'),
    onMaximizedChange: (callback) => {
      const listener = (_event: IpcRendererEvent, maximized: boolean): void => callback(maximized)
      ipcRenderer.on('window:maximized', listener)
      return () => ipcRenderer.removeListener('window:maximized', listener)
    }
  }
}

contextBridge.exposeInMainWorld('baseStation', baseStation)
