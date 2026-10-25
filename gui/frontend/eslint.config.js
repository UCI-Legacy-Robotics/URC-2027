// ESLint covers what Biome doesn't: React hooks rules and type-aware TypeScript rules.
// Formatting and overlapping rules are left to Biome (see biome.json).
import js from '@eslint/js'
import { defineConfig, globalIgnores } from 'eslint/config'
import biome from 'eslint-config-biome'
import reactHooks from 'eslint-plugin-react-hooks'
import reactRefresh from 'eslint-plugin-react-refresh'
import globals from 'globals'
import tseslint from 'typescript-eslint'

export default defineConfig([
  globalIgnores(['dist', 'src/generated']),
  {
    files: ['**/*.{ts,tsx}'],
    extends: [
      js.configs.recommended,
      tseslint.configs.strictTypeChecked,
      reactHooks.configs.flat.recommended,
      reactRefresh.configs.vite,
    ],
    languageOptions: {
      globals: globals.browser,
      parserOptions: {
        projectService: true,
        tsconfigRootDir: import.meta.dirname,
      },
    },
  },
  biome,
])
