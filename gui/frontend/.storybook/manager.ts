import { addons } from 'storybook/manager-api'
import { themes } from 'storybook/theming'

// Dark Storybook UI to match the app.
addons.setConfig({ theme: themes.dark })
