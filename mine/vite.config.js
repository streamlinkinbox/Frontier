import { defineConfig } from 'vite';
export default defineConfig({
  server: { host: '0.0.0.0', port: 6953, strictPort: true, allowedHosts: true },
  preview: { host: '0.0.0.0', port: 6953, strictPort: true, allowedHosts: true },
  build: { chunkSizeWarningLimit: 2000 },
});
