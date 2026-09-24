import { defineConfig } from 'vite';
import { copyFileSync, cpSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('.', import.meta.url));
// Keep all browser experiments together; never resolve assets against the engine root.
export default defineConfig({
  root,
  base: './',
  server: { host: '0.0.0.0', allowedHosts: ['.e2b.app'], proxy: { '/api/construct': 'http://127.0.0.1:5191' } },
  plugins: [{
    name: 'standalone-experimental-pages',
    closeBundle() {
      for (const page of ['icons.html', 'collection-icon-options.html']) {
        copyFileSync(resolve(root, page), resolve(root, 'dist', page));
      }
      for (const directory of ['custom-icons', 'ui-icons']) {
        cpSync(resolve(root, directory), resolve(root, 'dist', directory), { recursive: true });
      }
    },
  }],
});
