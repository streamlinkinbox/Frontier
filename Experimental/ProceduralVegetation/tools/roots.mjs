// Headless review of the root system: low camera on the trunk base, optional
// see-through ground, optional custom object layout.
// Usage: node tools/roots.mjs out_dir [preset] [seed] [mode] [json-overrides]
//   overrides: { view: {...}, roots: {...}, obstacles: [...], camera: [dist, height, azimuthDeg] }
import puppeteer from 'puppeteer-core';
import fs from 'node:fs';
import path from 'node:path';

const out = process.argv[2] || '/tmp/shots';
const presetName = process.argv[3] || 'English Oak';
const seed = Number(process.argv[4] || 1);
const mode = process.argv[5] || 'shaded';
const extra = process.argv[6] ? JSON.parse(process.argv[6]) : {};
fs.mkdirSync(out, { recursive: true });

const browser = await puppeteer.launch({
  executablePath: process.env.CHROME_PATH || '/tmp/chromium',
  headless: true,
  args: ['--no-sandbox', '--disable-setuid-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--disable-dev-shm-usage', '--single-process', '--no-zygote'],
  defaultViewport: { width: 1600, height: 1000, deviceScaleFactor: 1 },
});
const page = await browser.newPage();
page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') console.log('[console]', m.type(), m.text().slice(0, 300)); });
page.on('pageerror', (e) => console.log('[pageerror]', e.message));
await page.goto('http://localhost:5173/', { waitUntil: 'networkidle0', timeout: 120000 });
await page.waitForFunction(() => window.frontier && window.frontier.ready(), { timeout: 120000 });
await page.evaluate((p, s, m, ex) => {
  window.frontier.setPreset(p);
  window.frontier.setSeed(s);
  window.frontier.setMode(m);
  window.frontier.setView(Object.assign({ windEnabled: false, showLeaves: false }, ex.view || {}));
  const partial = {};
  if (ex.roots) partial.roots = ex.roots;
  if (ex.mesh) partial.mesh = ex.mesh;
  if (ex.botany) partial.botany = ex.botany;
  if (Object.keys(partial).length) window.frontier.setParams(partial);
  if (ex.obstacles) window.frontier.setObstacles(ex.obstacles);
}, presetName, seed, mode, extra);
await new Promise((r) => setTimeout(r, 500));
await page.waitForFunction(() => window.frontier.ready(), { timeout: 120000 });
const cam = extra.camera || null;
await page.evaluate((cam) => {
  if (cam) {
    // [dist, height, azimuthDeg, targetY, targetX?, targetZ?] – orbit about the target
    const [dist, height, az] = cam;
    const a = (az * Math.PI) / 180;
    const tx = cam[4] ?? 0;
    const tz = cam[5] ?? 0;
    window.frontier.setCamera([tx + Math.cos(a) * dist, height, tz + Math.sin(a) * dist], [tx, cam[3] ?? 0.3, tz]);
  } else window.frontier.frameBase();
}, cam);
await new Promise((r) => setTimeout(r, 1500));
const tag = extra.tag ? `_${extra.tag}` : '';
const file = path.join(out, `roots_${presetName.replace(/\s+/g, '_')}_${seed}_${mode}${tag}.png`);
await page.screenshot({ path: file });
const info = await page.evaluate(() => { const r = window.frontier.result; return { report: { closed: r.report.closed, manifold: r.report.manifold, genus: r.report.genus, faces: r.report.faces }, stats: r.stats, summary: r.summary, timings: r.timings }; });
console.log(file);
console.log(JSON.stringify(info));
await browser.close();
