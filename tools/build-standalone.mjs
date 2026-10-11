/**
 * Bundle the studio into ONE self-contained .html with no external requests:
 * JS, CSS and the favicon are all inlined, so the file works from a sandboxed
 * iframe, a file:// URL or raw.githack.
 *
 *   node tools/build-standalone.mjs [outfile]
 */
import { build } from 'esbuild';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const out = resolve(root, process.argv[2] ?? 'preview.html');

const result = await build({
  entryPoints: [resolve(root, 'src/app/main.ts')],
  bundle: true,
  write: false,
  format: 'esm',
  target: 'es2022',
  platform: 'browser',
  minify: true,
  legalComments: 'none',
  define: { 'import.meta.env.DEV': 'false' },
  logLevel: 'error',
});

const js = result.outputFiles[0].text;
const css = readFileSync(resolve(root, 'src/app/style.css'), 'utf8');
const shell = readFileSync(resolve(root, 'index.html'), 'utf8');

const favicon =
  'data:image/svg+xml,' +
  encodeURIComponent(
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#101010"/><path d="M32 6c6 12 6 22 0 32s-6 14 0 20" stroke="#72c8b3" stroke-width="5" fill="none"/><path d="M12 50c8-2 12-6 20-18s12-16 20-18" stroke="#e8b65f" stroke-width="5" fill="none"/></svg>`,
  );

const html = shell
  .replace('<link rel="stylesheet" href="./src/app/style.css" />', `<style>\n${css}\n</style>`)
  .replace('<script type="module" src="./dist/app.js"></script>', `<script type="module">\n${js}\n</script>`)
  .replace('<title>Frontier Road Studio</title>', `<title>Frontier Road Studio</title>\n    <link rel="icon" href="${favicon}" />`);

mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, html);
const kb = (Buffer.byteLength(html) / 1024).toFixed(0);
console.log(`wrote ${out} (${kb} kB, ${js.length > 0 ? 'bundle ok' : 'EMPTY BUNDLE'})`);
