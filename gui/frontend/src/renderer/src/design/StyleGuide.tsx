import type { ReactNode } from 'react'
import { Button } from './ui/button'
import { StatusPill, type StatusTone } from './StatusPill'

// The style guide shown in Storybook (Design / Style guide). Token values are read live
// from main.css, so this page always matches the theme.

const colorGroups: { title: string; tokens: { name: string; use: string }[] }[] = [
  {
    title: 'Surfaces',
    tokens: [
      { name: 'bg', use: 'App background' },
      { name: 'panel', use: 'Panels, top bar' },
      { name: 'panel-raised', use: 'Hover, selected rows' },
      { name: 'border', use: '1px borders' }
    ]
  },
  {
    title: 'Text',
    tokens: [
      { name: 'fg', use: 'Body text' },
      { name: 'fg-muted', use: 'Labels, secondary text' },
      { name: 'fg-strong', use: 'Values, emphasis' }
    ]
  },
  {
    title: 'Accents (use sparingly)',
    tokens: [
      { name: 'ok', use: 'OK, online, executing' },
      { name: 'primary', use: 'Primary action, selected item' },
      { name: 'caution', use: 'Caution' },
      { name: 'fault', use: 'Stop, cancel, fault' },
      { name: 'rover', use: 'Our rover and our assets' }
    ]
  }
]

const textSizes = ['xs', 'sm', 'base', 'lg', 'xl', '2xl'] as const
const textSizeClass: Record<(typeof textSizes)[number], string> = {
  xs: 'text-xs',
  sm: 'text-sm',
  base: 'text-base',
  lg: 'text-lg',
  xl: 'text-xl',
  '2xl': 'text-2xl'
}

const spacingSteps = [1, 2, 3, 4, 6, 8, 12] as const
const tones: StatusTone[] = ['ok', 'primary', 'caution', 'fault', 'rover', 'neutral']
const buttonVariants = ['default', 'destructive', 'outline', 'secondary', 'ghost', 'link'] as const

/** Reads a CSS variable from the theme. main.css is loaded before anything renders. */
function readCssVar(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim()
}

function Section({ title, children }: { title: string; children: ReactNode }): React.JSX.Element {
  return (
    <section className="flex flex-col gap-3">
      <h2 className="border-b border-border pb-1 text-xs font-semibold tracking-wider text-fg-muted uppercase">
        {title}
      </h2>
      {children}
    </section>
  )
}

function Swatch({ name, use }: { name: string; use: string }): React.JSX.Element {
  const value = readCssVar(`--color-${name}`)
  return (
    <div className="flex items-center gap-3">
      <div
        className="size-10 shrink-0 border border-border"
        style={{ background: `var(--color-${name})` }}
      />
      <div className="flex flex-col">
        <span className="font-mono text-sm text-fg-strong">{name}</span>
        <span className="font-mono text-xs text-fg-muted">
          bg-{name} · text-{name} · {value}
        </span>
        <span className="text-xs">{use}</span>
      </div>
    </div>
  )
}

/** The full style guide page. */
export function StyleGuide(): React.JSX.Element {
  return (
    <div className="flex min-h-screen flex-col gap-10 bg-bg p-8 text-fg">
      <header className="flex flex-col gap-1">
        <h1 className="text-xl font-semibold tracking-wider text-fg-strong uppercase">
          Style guide
        </h1>
        <p className="max-w-2xl text-sm text-fg-muted">
          Tokens and base components for the base station GUI. Use these, never raw hex values or
          pixel sizes. Values are placeholders until task A2. Defined in{' '}
          <code className="font-mono text-fg">src/renderer/src/assets/main.css</code>.
        </p>
      </header>

      <Section title="Colors">
        <div className="grid grid-cols-[repeat(auto-fill,minmax(18rem,1fr))] gap-x-8 gap-y-6">
          {colorGroups.map((group) => (
            <div key={group.title} className="flex flex-col gap-3">
              <h3 className="text-xs text-fg-muted">{group.title}</h3>
              {group.tokens.map((token) => (
                <Swatch key={token.name} {...token} />
              ))}
            </div>
          ))}
        </div>
      </Section>

      <Section title="Type">
        <div className="flex flex-col gap-2">
          {textSizes.map((size) => (
            <div key={size} className="flex items-baseline gap-4">
              <span className="w-24 shrink-0 font-mono text-xs text-fg-muted">text-{size}</span>
              <span className={textSizeClass[size]}>Rover 1 · Autonomous · 12.4 V</span>
            </div>
          ))}
          <div className="mt-2 flex items-baseline gap-4">
            <span className="w-24 shrink-0 font-mono text-xs text-fg-muted">font-sans</span>
            <span className="font-sans text-sm">Labels, headers and body text</span>
          </div>
          <div className="flex items-baseline gap-4">
            <span className="w-24 shrink-0 font-mono text-xs text-fg-muted">font-mono</span>
            <span className="font-mono text-sm">14:32:07Z · 33.4511° N · -72 dBm</span>
          </div>
          <div className="flex items-baseline gap-4">
            <span className="w-24 shrink-0 font-mono text-xs text-fg-muted">header</span>
            <span className="text-xs font-semibold tracking-wider text-fg-muted uppercase">
              Drive telemetry
            </span>
          </div>
        </div>
      </Section>

      <Section title="Spacing">
        <div className="flex flex-col gap-2">
          {spacingSteps.map((step) => (
            <div key={step} className="flex items-center gap-4">
              <span className="w-24 shrink-0 font-mono text-xs text-fg-muted">
                {step} · {step * 4}px
              </span>
              <div className="h-3 bg-primary" style={{ width: `calc(var(--spacing) * ${step})` }} />
            </div>
          ))}
        </div>
        <p className="text-xs text-fg-muted">
          Tailwind&apos;s scale: <code className="font-mono">p-2</code>,{' '}
          <code className="font-mono">gap-3</code>, … Borders are 1px (
          <code className="font-mono">border border-border</code>), corners are square.
        </p>
      </Section>

      <Section title="Status pills">
        <div className="flex flex-wrap gap-2">
          {tones.map((tone) => (
            <StatusPill key={tone} tone={tone}>
              {tone}
            </StatusPill>
          ))}
        </div>
      </Section>

      <Section title="Buttons (shadcn, not yet restyled)">
        <div className="flex flex-wrap items-center gap-2">
          {buttonVariants.map((variant) => (
            <Button key={variant} variant={variant} size="sm">
              {variant}
            </Button>
          ))}
        </div>
      </Section>
    </div>
  )
}
