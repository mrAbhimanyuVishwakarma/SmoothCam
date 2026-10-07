import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  build: {
    // three.js is split into its own lazy chunk; it's big by nature.
    chunkSizeWarningLimit: 1600,
  },
})
