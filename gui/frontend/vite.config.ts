import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    strictPort: true,
    // Forward the WebSocket to the backend in development.
    proxy: {
      '/ws': { target: 'ws://localhost:8000', ws: true },
    },
  },
})
