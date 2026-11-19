import type { StorybookConfig } from '@storybook/react-vite'

const config: StorybookConfig = {
  stories: ['../src/renderer/src/**/*.mdx', '../src/renderer/src/**/*.stories.@(ts|tsx)'],
  addons: ['@storybook/addon-docs'],
  framework: {
    name: '@storybook/react-vite',
    options: {
      // Same Vite setup as `pnpm dev:web`: React, Tailwind and the @renderer alias.
      builder: { viteConfigPath: 'vite.web.config.ts' }
    }
  },
  core: {
    // Works offline: no usage telemetry, no update or "what's new" checks.
    disableTelemetry: true,
    disableWhatsNewNotifications: true
  }
}

export default config
