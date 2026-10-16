#!/usr/bin/env node
/**
 * Headless GPU smoke test.
 *
 * Serves tyre-physics/ locally, opens the demo in a headless Chromium with WebGPU, and checks:
 *   - both WGSL modules compile (the page shows a fatal panel otherwise)
 *   - no uncaptured GPU errors are logged
 *   - frames run and telemetry is read back and is finite
 *   - the shape-matching solver also runs
 *   - the benchmark completes
 * The page runs with ?offscreen=1 (renders to an offscreen texture). Some headless WebGPU builds
 * lose the device on canvas presentation; compute and render pipelines still run in that mode.
 *
 * Environment:
 *   CHROME_PATH       path to a Chrome/Chromium binary with WebGPU (required)
 *   PUPPETEER_CORE    module specifier or path for puppeteer-core (default: 'puppeteer-core')
 *   CHROME_ARGS       extra space-separated Chromium flags (optional)
 *   SMOKE_PRESET      low | medium | high | ultra (default: low)
 *
 * Usage:  CHROME_PATH=/usr/bin/chromium node tests/gpu_smoke.mjs
 */
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wgsl': 'text/plain', '.css': 'text/css' };

const server = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  let p = path.join(root, decodeURIComponent(url.pathname === '/' ? '/index.html' : url.pathname));
  if (!p.startsWith(root) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) {
    res.writeHead(404).end('not found');
    return;
  }
  res.writeHead(200, { 'content-type': TYPES[path.extname(p)] ?? 'application/octet-stream' });
  fs.createReadStream(p).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const port = server.address().port;

const chromePath = process.env.CHROME_PATH;
if (!chromePath) {
  console.error('CHROME_PATH is not set');
  process.exit(2);
}
const puppeteer = (await import(process.env.PUPPETEER_CORE ?? 'puppeteer-core')).default;
const preset = process.env.SMOKE_PRESET ?? 'low';
const extra = (process.env.CHROME_ARGS ?? '').split(' ').filter(Boolean);

const browser = await puppeteer.launch({
  executablePath: chromePath,
  headless: 'shell',
  args: ['--enable-unsafe-webgpu', '--enable-features=WebGPU', '--ignore-gpu-blocklist', ...extra],
});

const results = { preset, checks: {}, errors: [] };
const check = (name, ok, detail = '') => {
  results.checks[name] = { ok: !!ok, detail };
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  ' + detail : ''}`);
};

try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1280, height: 800 });
  page.on('console', (m) => {
    if (m.type() === 'error' || m.text().startsWith('[gpu]')) results.errors.push(m.text());
  });
  page.on('pageerror', (e) => results.errors.push(`pageerror: ${e.message}`));

  await page.goto(`http://127.0.0.1:${port}/index.html?preset=${preset}&solver=xpbd&offscreen=1`, { waitUntil: 'load' });

  // wait for either frames or a fatal panel
  const state = await page.waitForFunction(
    () => {
      const fatal = document.getElementById('fatal');
      if (fatal && fatal.style.display === 'block') return { fatal: fatal.innerText };
      const t = window.__tyre;
      if (t && t.frameNo >= 60 && t.latest && !t.busy) return { ok: true };
      return null;
    },
    { timeout: 240000, polling: 250 }
  ).then((h) => h.jsonValue());

  if (state.fatal) {
    check('app starts without fatal error', false, state.fatal.slice(0, 500));
  } else {
    check('both WGSL modules compile and app starts', true);
    const lat = await page.evaluate(() => window.__tyre.latest);
    const finite = Object.values(lat).every((v) => {
      if (typeof v === 'number') return Number.isFinite(v);
      if (v && typeof v === 'object') return Object.values(v).every((x) => Number.isFinite(x));
      return true;
    });
    check('telemetry readback is finite (xpbd)', finite, `Fz=${lat.Fz.toFixed(1)} N target=${lat.targetLoad.toFixed(1)} N`);
    check('volume and pressure readback are finite', Number.isFinite(lat.V) && Number.isFinite(lat.pressure), `V=${lat.V.toFixed(4)} m^3 p=${(lat.pressure / 1e5).toFixed(2)} bar`);
    check('hub is moving forward and the tyre contacts the road', lat.hubVel.x > 0 && lat.contacts > 0, `vx=${lat.hubVel.x.toFixed(2)} contacts=${lat.contacts}`);

    // shape matching path
    await page.evaluate(() => window.__tyre.setSolver('shape'));
    const f0 = await page.evaluate(() => window.__tyre.latest.frameIndex);
    await page.waitForFunction(
      (f) => window.__tyre.latest && window.__tyre.latest.frameIndex > f + 30,
      { timeout: 120000, polling: 250 },
      f0
    );
    const shp = await page.evaluate(() => window.__tyre.latest);
    const shpFinite = [shp.Fz, shp.Fx, shp.FzLat, shp.V, shp.pressure, shp.hub.y].every(Number.isFinite);
    check('shape-matching solver runs and telemetry is finite', shpFinite, `Fz=${shp.Fz.toFixed(1)} N, hub y=${shp.hub.y.toFixed(3)} m`);

    // benchmark path
    await page.evaluate(() => window.__tyre.runBenchmark());
    await page.waitForFunction(() => !window.__tyre.busy, { timeout: 900000, polling: 1000 });
    const bench = await page.evaluate(() => document.getElementById('benchTable').innerText);
    check('benchmark completes for both solvers', /XPBD/.test(bench) && /Shape/.test(bench), bench.replace(/\s+/g, ' ').slice(0, 1200));
  }

  const gpuErrors = results.errors.filter((e) => e.startsWith('[gpu]') || e.startsWith('pageerror'));
  check('no uncaptured GPU or page errors', gpuErrors.length === 0, gpuErrors.slice(0, 3).join(' | '));
} catch (err) {
  check('smoke run completed', false, String(err?.message ?? err).slice(0, 400));
} finally {
  await browser.close();
  server.close();
}

const failed = Object.values(results.checks).filter((c) => !c.ok).length;
console.log(failed ? `\n${failed} check(s) failed` : '\nall checks passed');
process.exit(failed ? 1 : 0);
