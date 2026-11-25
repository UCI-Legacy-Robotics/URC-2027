/** The backend's port, on the base laptop. */
const backendPort = 8000

/**
 * Base URL of the backend. It runs on the machine that serves the UI: `localhost` in the
 * Electron app, the base laptop for browser viewers.
 */
export function backendUrl(): string {
  return `http://${window.location.hostname || '127.0.0.1'}:${backendPort}`
}
