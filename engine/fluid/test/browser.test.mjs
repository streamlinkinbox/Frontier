// End-to-end browser test: serves the repo, opens the demo in headless Chromium with WebGPU
// (SwiftShader when no GPU is present), drives the car with real key presses and saves screenshots.
//   node test/browser.test.mjs [quality] [width] [height]
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';

const here = path.dirname(new URL(import.meta.url).pathname);
const root = path.resolve(here, '../../..');
const outDir = path.join(here, 'out');
fs.mkdirSync(outDir, { recursive: true });
const quality = process.argv[2] ?? 'low';
const W = +(process.argv[3] ?? 960), H = +(process.argv[4] ?? 540);

// ---- static server ------------------------------------------------------------------------
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.woff2': 'font/woff2' };
const server = http.createServer((req, res) => {
  const p = path.join(root, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  if (!p.startsWith(root)) { res.writeHead(403).end(); return; }
  fs.readFile(p.endsWith('/') ? p + 'index.html' : p, (err, data) => {
    if (err) { res.writeHead(404).end('not found'); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(p)] ?? (p.endsWith('/') ? 'text/html' : 'application/octet-stream') });
    res.end(data);
  });
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const port = server.address().port;

// ---- chromium with WebGPU -----------------------------------------------------------------
const { default: puppeteer } = await import('puppeteer-core');
const { default: chromium } = await import('@sparticuz/chromium');
const exe = await chromium.executablePath();
const pkg = path.dirname(path.dirname(fs.realpathSync(path.join(here, '../node_modules/@sparticuz/chromium/bin/chromium.br'))));
const libDir = path.join(os.tmpdir(), 'frontier-chromium-libs');
if (!fs.existsSync(path.join(libDir, 'libnss3.so'))) {
  fs.mkdirSync(libDir, { recursive: true });
  const buf = zlib.brotliDecompressSync(fs.readFileSync(path.join(pkg, 'bin', 'al2023.tar.br')));
  let off = 0;
  while (off + 512 <= buf.length) {
    const name = buf.toString('utf8', off, off + 100).replace(/\0.*$/, '');
    if (!name) break;
    const size = parseInt(buf.toString('utf8', off + 124, off + 136).replace(/\0.*$/, '').trim() || '0', 8);
    const type = buf[off + 156];
    off += 512;
    if ((type === 48 || type === 0) && name.includes('lib/')) fs.writeFileSync(path.join(libDir, path.basename(name)), buf.subarray(off, off + size), { mode: 0o755 });
    off += Math.ceil(size / 512) * 512;
  }
}
const swDir = path.dirname(exe);
const browser = await puppeteer.launch({
  executablePath: exe,
  headless: 'shell',
  args: ['--no-sandbox', '--no-zygote', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader', '--use-webgpu-adapter=swiftshader',
    '--disable-dev-shm-usage', '--ignore-gpu-blocklist', '--use-vulkan=swiftshader', '--enable-features=Vulkan', '--use-gl=angle', '--use-angle=swiftshader',
    `--window-size=${W},${H}`],
  env: { ...process.env, LD_LIBRARY_PATH: `${libDir}:${swDir}`, VK_ICD_FILENAMES: path.join(swDir, 'vk_swiftshader_icd.json') },
  defaultViewport: { width: W, height: H },
  protocolTimeout: 600000,
});
const page = await browser.newPage();
const errors = [];
page.on('console', (m) => { const t = m.text(); if (m.type() === 'error') errors.push(t); console.log('[page]', t.slice(0, 300)); });
page.on('pageerror', (e) => { errors.push(String(e)); console.log('[pageerror]', e); });

const url = `http://127.0.0.1:${port}/engine/fluid/demo/?q=${quality}&warmup=1&scale=${process.env.SCALE ?? 1}`;
console.log('open', url);
await page.goto(url, { waitUntil: 'load', timeout: 120000 });
await page.waitForFunction(() => window.app && document.getElementById('overlay').style.display === 'none', { timeout: 600000, polling: 1000 });
console.log('demo running');
const shot = async (name) => { await page.screenshot({ path: path.join(outDir, `browser_${name}.png`) }); console.log('screenshot', name, await page.evaluate(() => `t=${app.sim.time.toFixed(2)} fps=${document.getElementById('sFps').textContent} particles=${document.getElementById('sParts').textContent} speed=${document.getElementById('spd').textContent} surf=${document.getElementById('surf').textContent}`)); };
const waitFrames = (n) => page.evaluate((n) => new Promise((r) => { let k = 0; const f = () => (++k >= n ? r() : requestAnimationFrame(f)); requestAnimationFrame(f); }), n);

await waitFrames(3);
await shot('01_start');
await page.click('#scenarios button[data-s="ford"]');
await waitFrames(4);
await page.keyboard.down('KeyW');
await waitFrames(Number(process.env.DRIVE_FRAMES ?? 24));
await shot('02_ford');
await page.keyboard.up('KeyW');
await page.keyboard.press('KeyG');
await waitFrames(14);
await shot('03_cannonball');
console.log('errors:', errors.length ? errors : 'none');
await browser.close();
server.close();
process.exit(errors.length ? 1 : 0);
