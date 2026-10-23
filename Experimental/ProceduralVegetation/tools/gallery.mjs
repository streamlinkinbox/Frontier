// Full-tree gallery: node tools/gallery.mjs out_dir mode leaves(0/1) "Preset A" "Preset B" ...
import puppeteer from 'puppeteer-core';
import fs from 'node:fs';
import path from 'node:path';
const [out = '/tmp/shots', mode = 'matcap', leavesS = '0', ...presets] = process.argv.slice(2);
fs.mkdirSync(out, { recursive: true });
const browser = await puppeteer.launch({
  executablePath: process.env.CHROME_PATH || '/tmp/chromium', headless: true,
  args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--disable-dev-shm-usage', '--single-process', '--no-zygote'],
  defaultViewport: { width: 900, height: 900, deviceScaleFactor: 1 },
});
const page = await browser.newPage();
page.on('pageerror', (e) => console.log('[pageerror]', e.message));
await page.goto('http://localhost:5173/', { waitUntil: 'networkidle0', timeout: 120000 });
await page.waitForFunction(() => window.frontier && window.frontier.ready(), { timeout: 120000 });
await page.evaluate(() => { document.querySelector('.panel').style.display = 'none'; document.querySelector('#app').style.gridTemplateColumns = '1fr 0px'; window.dispatchEvent(new Event('resize')); });
for (const spec of presets) {
  const [name, seedS = '1'] = spec.split('@');
  await page.evaluate((p, s, m, lv) => { window.frontier.setPreset(p); window.frontier.setSeed(Number(s)); window.frontier.setMode(m); window.frontier.setView({ windEnabled: false, showLeaves: lv === '1', showGrid: false, showWire: false }); }, name, seedS, mode, leavesS);
  await new Promise((r) => setTimeout(r, 500));
  await page.waitForFunction(() => window.frontier.ready(), { timeout: 120000 });
  const h = await page.evaluate(() => window.frontier.result.summary.height);
  await page.evaluate((hh) => window.frontier.setCamera([hh * 0.1, hh * 0.45, hh * 1.75], [0, hh * 0.5, 0]), h);
  await new Promise((r) => setTimeout(r, 800));
  const file = path.join(out, `tree_${name.replace(/\s+/g, '_')}_${seedS}_${mode}_${leavesS}.png`);
  await page.screenshot({ path: file });
  const info = await page.evaluate(() => { const r = window.frontier.result; return { faces: r.report.faces, closed: r.report.closed, chi: r.report.eulerCharacteristic, dropped: r.stats.droppedStems, height: r.summary.height, stems: r.summary.stemsPerLevel }; });
  console.log(file, JSON.stringify(info));
}
await browser.close();
