import '../src/renderer/src/assets/main.css'

import type { Preview } from '@storybook/react-vite'
import { themes } from 'storybook/theming'
import type { Mode } from '../src/renderer/src/app/mode'
import { ModeProvider } from '../src/renderer/src/app/ModeProvider'

const preview: Preview = {
  // Toolbar toggle: preview every story as the Electron control app or a browser viewer.
  globalTypes: {
    mode: {
      description: 'Control (Electron) or viewer (browser) mode',
      toolbar: {
        title: 'Mode',
        icon: 'eye',
        items: [
          { value: 'control', title: 'Control' },
          { value: 'viewer', title: 'Viewer' }
        ],
        dynamicTitle: true
      }
    }
  },
  initialGlobals: {
    mode: 'control'
  },
  decorators: [
    (Story, context) => (
      <ModeProvider mode={context.globals.mode as Mode}>
        <Story />
      </ModeProvider>
    )
  ],
  parameters: {
    // The canvas background comes from main.css (bg token); docs pages use the dark theme.
    backgrounds: { disable: true },
    docs: { theme: themes.dark },
    layout: 'padded'
  }
}

export default preview
