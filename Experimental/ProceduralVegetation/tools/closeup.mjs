// Close-up inspection of junctions.
// Usage: node tools/closeup.mjs out_dir preset seed "level:kind:index:mode[:distMult]" ...
import puppeteer from 'puppeteer-core';
import fs from 'node:fs';
import path from 'node:path';
const [out = '/tmp/shots', presetName = 'English Oak', seedS = '1', ...shots] = process.argv.slice(2);
const seed = Number(seedS);
fs.mkdirSync(out, { recursive: true });
const browser = await puppeteer.launch({
  executablePath: process.env.CHROME_PATH || '/tmp/chromium', headless: true,
  args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--disable-dev-shm-usage', '--single-process', '--no-zygote'],
  defaultViewport: { width: 1100, height: 800, deviceScaleFactor: 1 },
});
const page = await browser.newPage();
page.on('pageerror', (e) => console.log('[pageerror]', e.message));
await page.goto('http://localhost:5173/', { waitUntil: 'networkidle0', timeout: 120000 });
await page.waitForFunction(() => window.frontier && window.frontier.ready(), { timeout: 120000 });
await page.evaluate((p, s) => { window.frontier.setPreset(p); window.frontier.setSeed(s); window.frontier.setView({ windEnabled: false, showLeaves: false, showGrid: false }); }, presetName, seed);
await new Promise((r) => setTimeout(r, 400));
await page.waitForFunction(() => window.frontier.ready(), { timeout: 120000 });
const norm = (v) => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
for (const spec of shots) {
  const [levelS, kind, idxS, mode = 'wireframe', multS = '0'] = spec.split(':');
  const level = Number(levelS), idx = Number(idxS);
  await page.evaluate((m) => { window.frontier.setMode(m); window.frontier.setView({ showWire: m !== 'wireframe' }); }, mode);
  const sample = await page.evaluate((lvl, i, k) => window.frontier.junctionSamples(lvl, 400, k)[i], level, idx, kind);
  if (!sample) { console.log('no sample for', spec); continue; }
  const mult = Number(multS) || 10;
  const dist = (sample.parentRadius + sample.radius) * mult;
  // View perpendicular to the plane spanned by parent and child directions, slightly toward the child side.
  let n = cross(sample.parentDir, sample.dir);
  if (Math.hypot(...n) < 1e-3) n = [1, 0, 0];
  n = norm(n);
  if (n[1] < 0) n = [-n[0], -n[1], -n[2]];
  const target = [sample.pos[0] + sample.dir[0] * sample.radius * 2, sample.pos[1] + sample.dir[1] * sample.radius * 2, sample.pos[2] + sample.dir[2] * sample.radius * 2];
  const pos = [target[0] + n[0] * dist + sample.dir[0] * dist * 0.15, target[1] + n[1] * dist + 0.15 * dist, target[2] + n[2] * dist + sample.dir[2] * dist * 0.15];
  await page.evaluate((p, t) => window.frontier.setCamera(p, t), pos, target);
  await new Promise((r) => setTimeout(r, 700));
  const file = path.join(out, `closeup_${presetName.replace(/\s+/g, '_')}_${seed}_L${level}_${kind}_${idx}_${mode}.png`);
  await page.screenshot({ path: file });
  console.log(file, JSON.stringify(sample));
}
await browser.close();
