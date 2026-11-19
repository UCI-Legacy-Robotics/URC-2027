import { resolve } from 'path'
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// Renderer only, in a normal browser (viewer mode). Used by `pnpm dev:web`.
// Keep `resolve` and `plugins` in sync with the renderer section of electron.vite.config.ts.
export default defineConfig({
  root: resolve('src/renderer'),
  resolve: {
    alias: {
      '@renderer': resolve('src/renderer/src')
    }
  },
  plugins: [react()],
  server: {
    // Listen on the LAN so phones and tablets on the same network can test viewer mode.
    host: true,
    port: 5174
  }
})
