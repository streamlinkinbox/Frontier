// Headless review harness: opens the running dev server, waits for generation,
// and captures screenshots for the requested presets / modes.
// Usage: node tools/screenshot.mjs out_dir [preset] [mode] [seed]
import puppeteer from 'puppeteer-core';
import fs from 'node:fs';
import path from 'node:path';

const out = process.argv[2] || '/tmp/shots';
const presetName = process.argv[3] || 'English Oak';
const mode = process.argv[4] || 'shaded';
const seed = Number(process.argv[5] || 1);
const extra = process.argv[6] ? JSON.parse(process.argv[6]) : {};
fs.mkdirSync(out, { recursive: true });

const execPath = process.env.CHROME_PATH || '/tmp/chromium';
const browser = await puppeteer.launch({
  executablePath: execPath,
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
  window.frontier.setView(Object.assign({ windEnabled: false }, ex));
}, presetName, seed, mode, extra);
await new Promise((r) => setTimeout(r, 400));
await page.waitForFunction(() => window.frontier.ready(), { timeout: 120000 });
await page.evaluate(() => window.frontier.frame());
await new Promise((r) => setTimeout(r, 1200));
const file = path.join(out, `${presetName.replace(/\s+/g, '_')}_${mode}_${seed}.png`);
await page.screenshot({ path: file });
const info = await page.evaluate(() => { const r = window.frontier.result; return { report: r.report, stats: r.stats, summary: r.summary, timings: r.timings }; });
console.log(file);
console.log(JSON.stringify(info));
await browser.close();
