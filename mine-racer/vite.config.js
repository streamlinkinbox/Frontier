import { defineConfig } from 'vite';

export default defineConfig({
  base: './',
  server: {
    host: true,          // bind 0.0.0.0 so the sandbox preview proxy can reach it
    port: 5173,
    strictPort: true,
    allowedHosts: true,  // accept the e2b preview hostname
  },
  build: {
    outDir: 'dist',
    chunkSizeWarningLimit: 1200,
  },
});
