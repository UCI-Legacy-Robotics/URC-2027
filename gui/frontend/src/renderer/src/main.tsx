import './assets/main.css'

import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import App from './App'
import { detectMode } from './app/mode'
import { ModeProvider } from './app/ModeProvider'

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <ModeProvider mode={detectMode()}>
      <App />
    </ModeProvider>
  </StrictMode>
)
