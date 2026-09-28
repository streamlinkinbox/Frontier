import { defineConfig } from 'vite';

export default defineConfig({
  server: { host: '0.0.0.0', port: 5174, allowedHosts: true },
  preview: { host: '0.0.0.0', port: 4174, allowedHosts: true },
  build: { target: 'es2022', chunkSizeWarningLimit: 2000 },
});
