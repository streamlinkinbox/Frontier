/**
 * Run a TypeScript entry point from the command line without a build step.
 *
 *   node tools/run-ts.mjs src/tools/probe.ts
 *
 * Bundles with esbuild (node platform, ESM) into tools/.cache and imports the
 * result, so the same source that ships in the browser runs unchanged in Node.
 */
import { build } from 'esbuild';
import { mkdirSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { dirname, resolve } from 'node:path';

const entry = process.argv[2];
if (!entry) {
  console.error('usage: node tools/run-ts.mjs <entry.ts> [--watch]');
  process.exit(2);
}
const root = resolve(import.meta.dirname, '..');
const abs = resolve(root, entry);
const out = resolve(root, 'tools/.cache', entry.replace(/[\\/]/g, '_').replace(/\.ts$/, '.mjs'));

mkdirSync(dirname(out), { recursive: true });
const watch = process.argv.includes('--watch');

const result = await build({
  entryPoints: [abs],
  outfile: out,
  bundle: true,
  platform: 'node',
  format: 'esm',
  target: 'node22',
  sourcemap: 'inline',
  logLevel: 'error',
  // native bindings must load from node_modules, not from the bundle
  external: ['webgpu', '@sparticuz/chromium'],
  define: { 'import.meta.env.DEV': 'true' },
});
void result;
await import(pathToFileURL(out).href + `?t=${Date.now()}`);
void watch;
