import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: { host: '0.0.0.0', port: 5173, allowedHosts: ['.e2b.app', 'localhost'] },
  preview: { host: '0.0.0.0', port: 4173, allowedHosts: ['.e2b.app', 'localhost'] },
  build: { chunkSizeWarningLimit: 2000 },
});
