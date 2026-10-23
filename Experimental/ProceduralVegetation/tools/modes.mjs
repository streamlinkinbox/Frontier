// Screenshot the display modes + wind deformation for a preset.
import puppeteer from 'puppeteer-core';
import fs from 'node:fs';
import path from 'node:path';
const [out = '/tmp/shots', presetName = 'Quaking Aspen', seedS = '1'] = process.argv.slice(2);
fs.mkdirSync(out, { recursive: true });
const browser = await puppeteer.launch({
  executablePath: process.env.CHROME_PATH || '/tmp/chromium', headless: true,
  args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--disable-dev-shm-usage', '--single-process', '--no-zygote'],
  defaultViewport: { width: 1000, height: 800, deviceScaleFactor: 1 },
});
const page = await browser.newPage();
page.on('pageerror', (e) => console.log('[pageerror]', e.message));
page.on('console', (m) => { if (m.type() === 'error') console.log('[console]', m.text().slice(0, 200)); });
await page.goto('http://localhost:5173/', { waitUntil: 'networkidle0', timeout: 120000 });
await page.waitForFunction(() => window.frontier && window.frontier.ready(), { timeout: 120000 });
await page.evaluate(() => { document.querySelector('.panel').style.display = 'none'; document.querySelector('#app').style.gridTemplateColumns = '1fr 0px'; window.dispatchEvent(new Event('resize')); });
await page.evaluate((p, s) => { window.frontier.setPreset(p); window.frontier.setSeed(Number(s)); window.frontier.setView({ windEnabled: false, showLeaves: true, showGrid: false }); }, presetName, seedS);
await new Promise((r) => setTimeout(r, 500));
await page.waitForFunction(() => window.frontier.ready(), { timeout: 120000 });
const h = await page.evaluate(() => window.frontier.result.summary.height);
await page.evaluate((hh) => window.frontier.setCamera([hh * 0.2, hh * 0.5, hh * 1.7], [0, hh * 0.5, 0]), h);
for (const mode of ['levels', 'junctions', 'wind', 'shaded']) {
  await page.evaluate((m) => { window.frontier.setMode(m); window.frontier.setView({ showLeaves: m === 'shaded' }); }, mode);
  await new Promise((r) => setTimeout(r, 600));
  await page.screenshot({ path: path.join(out, `mode_${mode}.png`) });
}
// Wind: strong, frozen at two different times by toggling
await page.evaluate(() => window.frontier.setView({ windEnabled: true, windStrength: 3, trunkFlex: 2, limbFlex: 2 }));
await new Promise((r) => setTimeout(r, 1500));
await page.screenshot({ path: path.join(out, `wind_a.png`) });
await new Promise((r) => setTimeout(r, 1500));
await page.screenshot({ path: path.join(out, `wind_b.png`) });
console.log('done');
await browser.close();
