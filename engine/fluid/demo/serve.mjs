// Static dev server for the fluid demo – no caching, ever.
//   node engine/fluid/demo/serve.mjs [port]        (or: cd engine/fluid && npm run demo)
//
// Python's http.server sends no Cache-Control header, so browsers and preview proxies may keep
// serving stale JavaScript modules long after the files changed. This server
//   * marks every response  Cache-Control: no-store
//   * serves the repository under  /b/<build>/...  where <build> changes whenever any demo or
//     module file changes – every module URL is new after an edit, so nothing stale can be used
//   * redirects  /  and  /engine/fluid/demo/  to the current build of the demo (query kept).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

const here = path.dirname(new URL(import.meta.url).pathname);
const root = path.resolve(here, '../../..');            // repository root
const watched = [path.join(root, 'engine/fluid/src'), path.join(root, 'engine/fluid/demo')];
const port = +(process.argv[2] ?? process.env.PORT ?? 8080);
const host = process.env.HOST ?? '0.0.0.0';
const MIME = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json', '.png': 'image/png', '.jpg': 'image/jpeg',
  '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.wasm': 'application/wasm', '.woff2': 'font/woff2',
  '.md': 'text/plain; charset=utf-8', '.txt': 'text/plain; charset=utf-8',
};
const NO_CACHE = { 'Cache-Control': 'no-store, no-cache, must-revalidate, max-age=0', Pragma: 'no-cache', Expires: '0' };

/** Build id: the newest modification time of any demo / module file (base 36). */
function buildId() {
  let newest = 0;
  const walk = (dir) => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) walk(p);
      else newest = Math.max(newest, fs.statSync(p).mtimeMs);
    }
  };
  for (const d of watched) if (fs.existsSync(d)) walk(d);
  return Math.floor(newest / 1000).toString(36);
}

function send(res, status, headers, body) {
  res.writeHead(status, { ...NO_CACHE, ...headers });
  res.end(body);
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://localhost');
  let p = decodeURIComponent(url.pathname);
  // entry points -> the current build of the demo
  if (p === '/' || p === '/index.html' || p === '/engine/fluid/demo' || p === '/engine/fluid/demo/') {
    send(res, 302, { Location: `/b/${buildId()}/engine/fluid/demo/${url.search}` });
    return;
  }
  // strip the build prefix (any build id maps to the current files)
  const m = p.match(/^\/b\/[0-9a-z]+(\/.*)$/);
  if (m) p = m[1];
  const file = path.resolve(root, '.' + p);
  if (!file.startsWith(root)) { send(res, 403, {}, 'forbidden'); return; }
  fs.stat(file, (err, st) => {
    if (err) { send(res, 404, { 'Content-Type': 'text/plain' }, 'not found'); return; }
    if (st.isDirectory()) {
      if (!url.pathname.endsWith('/')) { send(res, 301, { Location: url.pathname + '/' + url.search }); return; }
      const index = path.join(file, 'index.html');
      if (!fs.existsSync(index)) { send(res, 302, { Location: `/b/${buildId()}/engine/fluid/demo/` }); return; }
      fs.readFile(index, (e2, data) => (e2 ? send(res, 500, {}, 'error') : send(res, 200, { 'Content-Type': MIME['.html'] }, data)));
      return;
    }
    fs.readFile(file, (e2, data) => {
      if (e2) { send(res, 500, {}, 'error'); return; }
      send(res, 200, { 'Content-Type': MIME[path.extname(file).toLowerCase()] ?? 'application/octet-stream' }, data);
    });
  });
});
server.listen(port, host, () => console.log(`fluid demo: http://localhost:${port}/  (serving ${root}, no caching, build ${buildId()})`));
