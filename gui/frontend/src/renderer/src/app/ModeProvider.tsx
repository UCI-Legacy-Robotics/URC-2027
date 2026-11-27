import type { ReactNode } from 'react'
import { ModeContext, type Mode } from './mode'

/** Provides the mode to the UI. Storybook wraps stories in this to preview either mode. */
export function ModeProvider({ mode, children }: { mode: Mode; children: ReactNode }): ReactNode {
  return <ModeContext.Provider value={mode}>{children}</ModeContext.Provider>
}
