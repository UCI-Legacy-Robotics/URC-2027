import type { ReactNode } from 'react'
import { cn } from 'cn'

/** Semantic color of a pill. See docs/gui/design.md for what each means. */
export type StatusTone = 'ok' | 'primary' | 'caution' | 'fault' | 'neutral'

const toneClass: Record<StatusTone, string> = {
  ok: 'text-ok',
  primary: 'text-primary',
  caution: 'text-caution',
  fault: 'text-fault',
  neutral: 'text-fg-muted'
}

/** Small outlined status label, e.g. `ONLINE`, `AUTONOMOUS`. Placeholder until task A2. */
export function StatusPill({
  tone = 'neutral',
  className,
  children
}: {
  tone?: StatusTone
  className?: string
  children: ReactNode
}): React.JSX.Element {
  return (
    <span
      className={cn(
        'inline-flex items-center gap-1.5 border px-2 py-0.5 font-mono text-xs tracking-wider whitespace-nowrap uppercase',
        toneClass[tone],
        className
      )}
    >
      {children}
    </span>
  )
}
