// Bundle RoadForge into a single self-contained HTML file (scripts, stylesheet and fonts inlined).
// Usage: node tools/build-standalone.mjs [out=../RoadForge.html]
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const out = resolve(root, process.argv[2] || 'RoadForge.html');
let html = readFileSync(resolve(root, 'index.html'), 'utf8');
let css = readFileSync(resolve(root, 'styles.css'), 'utf8');
css = css.replace(/url\((["']?)(fonts\/[^"')]+)\1\)/g, (m, q, rel) => {
  const buf = readFileSync(resolve(root, rel));
  return `url(data:font/woff2;base64,${buf.toString('base64')})`;
});
html = html.replace('<link rel="stylesheet" href="styles.css" />', `<style>\n${css}\n</style>`);
html = html.replace(/<script src="([^"]+)"><\/script>/g, (m, src) => `<script>\n${readFileSync(resolve(root, src), 'utf8').replace(/<\/script/gi, '<\\/script')}\n</script>`);
writeFileSync(out, html);
console.log(`wrote ${out} (${(Buffer.byteLength(html) / 1024).toFixed(0)} kB)`);
