import { readFile, writeFile, stat } from 'node:fs/promises';
import { resolve } from 'node:path';

let html = await readFile('dist/index.html', 'utf8');

// Inline scripts
for (const match of [...html.matchAll(/<script\b[^>]*src="([^"]+)"[^>]*><\/script>/g)]) {
  const assetPath = match[1].startsWith('/') ? match[1].slice(1) : match[1];
  const script = await readFile(resolve('dist', assetPath), 'utf8');
  html = html.replace(
    match[0],
    () => `<script type="module">${script.replace(/<\/script/gi, '<\\/script')}</script>`
  );
}

// Inline CSS
for (const match of [...html.matchAll(/<link\b[^>]*rel="stylesheet"[^>]*href="([^"]+)"[^>]*>/g)]) {
  const assetPath = match[1].startsWith('/') ? match[1].slice(1) : match[1];
  const css = await readFile(resolve('dist', assetPath), 'utf8');
  html = html.replace(match[0], () => `<style>${css}</style>`);
}

html = html.replace(/<link\b[^>]*rel="modulepreload"[^>]*>/g, '');

await writeFile('RoadArchitect.html', html);
console.log(`Standalone RoadArchitect.html generated (${((await stat('RoadArchitect.html')).size / 1024).toFixed(0)} KB)`);
