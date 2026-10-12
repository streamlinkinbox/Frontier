import { defineConfig } from 'vite';

export default defineConfig({
  server: {
    host: '0.0.0.0',
    port: 5173,
    strictPort: true,
    // Live previews are served under *.e2b.app hosts; allow them explicitly.
    allowedHosts: true,
  },
});
