import { defineConfig } from 'vite';
import { resolve } from 'path';

export default defineConfig({
  server: { host: '0.0.0.0', port: 5173, allowedHosts: true },
  preview: { host: '0.0.0.0', port: 4173, allowedHosts: true },
  worker: { format: 'es' },
  build: {
    target: 'es2022',
    rollupOptions: { input: { main: resolve(__dirname, 'index.html'), studio: resolve(__dirname, 'dev.html') } },
  },
});
