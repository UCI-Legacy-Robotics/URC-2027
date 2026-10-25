import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { BrowserRouter, Navigate, Route, Routes } from 'react-router'
import './index.css'
import App from './App.tsx'
import Cameras from './routes/Cameras.tsx'
import Drive from './routes/Drive.tsx'
import Telemetry from './routes/Telemetry.tsx'

const root = document.getElementById('root')
if (!root) throw new Error('missing #root element')

createRoot(root).render(
  <StrictMode>
    <BrowserRouter>
      <Routes>
        <Route element={<App />}>
          <Route index element={<Navigate to="/telemetry" replace />} />
          <Route path="drive" element={<Drive />} />
          <Route path="cameras" element={<Cameras />} />
          <Route path="telemetry" element={<Telemetry />} />
        </Route>
      </Routes>
    </BrowserRouter>
  </StrictMode>,
)
