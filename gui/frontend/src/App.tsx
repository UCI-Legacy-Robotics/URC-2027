import { NavLink, Outlet } from 'react-router'

// Shared layout for every route. Each monitor opens one route in its own window.
export default function App() {
  return (
    <>
      {/* TODO: link-loss banner when the backend WebSocket is down or rover heartbeats are stale */}
      <nav>
        <NavLink to="/drive">Drive</NavLink>
        <NavLink to="/cameras">Cameras</NavLink>
        <NavLink to="/telemetry">Telemetry</NavLink>
      </nav>
      <main>
        <Outlet />
      </main>
    </>
  )
}
