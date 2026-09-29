// headless browser smoke test: boot the app, wait for first cook, screenshot
import puppeteer from 'puppeteer';

const url = process.argv[2] ?? 'http://localhost:5173/';
const out = process.argv[3] ?? '/tmp/frontier-shot.png';

const browser = await puppeteer.launch({
  headless: 'shell',
  args: [
    '--no-sandbox',
    '--enable-unsafe-swiftshader',
    '--use-gl=swiftshader',
    '--enable-webgl',
    '--ignore-gpu-blocklist',
    '--window-size=1600,900',
  ],
});
const page = await browser.newPage();
await page.setViewport({ width: 1600, height: 900 });
const errors = [];
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
page.on('pageerror', (e) => errors.push(`PAGEERROR ${e.message}`));

await page.goto(url, { waitUntil: 'networkidle2', timeout: 60000 });
// wait for cook to finish (boot overlay removed + progress 100%)
await page.waitForFunction(
  () => !document.querySelector('#boot') && document.querySelector('#pp-pct')?.textContent === '100%',
  { timeout: 120000 },
);
await new Promise((r) => setTimeout(r, 1500));
const stats = await page.evaluate(() => document.querySelector('#hud-stats')?.innerText);
const nodeCount = await page.evaluate(() => document.querySelectorAll('.node').length);
await page.screenshot({ path: out });
console.log(JSON.stringify({ stats, nodeCount, errors: errors.slice(0, 12) }, null, 2));
await browser.close();
if (errors.length) process.exit(2);
