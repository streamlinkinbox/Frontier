/**
 * Dev server: bundles the app on request and serves index.html plus the CSS.
 * Binds 0.0.0.0 so the sandbox preview proxy can reach it.
 *
 *   node tools/dev-server.mjs [--port 5173]
 */
import { createServer } from 'node:http';
import { readFileSync, existsSync } from 'node:fs';
import { resolve, extname } from 'node:path';
import { build } from 'esbuild';

const root = resolve(import.meta.dirname, '..');
const portArg = process.argv.indexOf('--port');
const port = Number(portArg > -1 ? process.argv[portArg + 1] : process.env.PORT ?? 5173);
const host = '0.0.0.0';

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
};

async function bundleApp() {
  const result = await build({
    entryPoints: [resolve(root, 'src/app/main.ts')],
    bundle: true,
    write: false,
    format: 'esm',
    target: 'es2022',
    platform: 'browser',
    sourcemap: 'inline',
    define: { 'import.meta.env.DEV': 'true' },
    logLevel: 'silent',
  });
  return result.outputFiles[0].text;
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? 'localhost'}`);
  const path = url.pathname;
  try {
    if (path === '/' || path === '/index.html') {
      res.writeHead(200, { 'content-type': MIME['.html'], 'cache-control': 'no-store' });
      res.end(readFileSync(resolve(root, 'index.html')));
      return;
    }
    if (path === '/dist/app.js') {
      const code = await bundleApp();
      res.writeHead(200, { 'content-type': MIME['.js'], 'cache-control': 'no-store' });
      res.end(code);
      return;
    }
    if (path === '/preview.html') {
      res.writeHead(200, { 'content-type': MIME['.html'], 'cache-control': 'no-store' });
      res.end(readFileSync(resolve(root, 'preview.html')));
      return;
    }
    const file = resolve(root, '.' + path);
    if (!file.startsWith(root) || !existsSync(file)) {
      res.writeHead(404, { 'content-type': 'text/plain' });
      res.end('not found');
      return;
    }
    res.writeHead(200, { 'content-type': MIME[extname(file)] ?? 'application/octet-stream', 'cache-control': 'no-store' });
    res.end(readFileSync(file));
  } catch (err) {
    res.writeHead(500, { 'content-type': 'text/plain' });
    res.end(`build error:\n${err instanceof Error ? err.stack ?? err.message : String(err)}`);
  }
});

server.listen(port, host, () => {
  console.log(`Frontier Road Studio on http://${host}:${port} (preview at /preview.html)`);
});
