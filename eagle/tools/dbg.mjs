import puppeteer from '/home/user/Frontier/tarantula/tools/node_modules/puppeteer-core/lib/esm/puppeteer/puppeteer-core.js';
import chromium from '/home/user/Frontier/tarantula/tools/node_modules/@sparticuz/chromium/build/esm/index.js';
const url = process.argv[2] || 'http://localhost:5174/?manual';
const wait = +(process.argv[3] || 60000);
const browser = await puppeteer.launch({ executablePath: await chromium.executablePath(), args: [...chromium.args, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'], defaultViewport: { width: 800, height: 450 }, headless: true, protocolTimeout: 1800000 });
const page = await browser.newPage();
page.on('console', (m) => console.log('console', m.type(), m.text().slice(0, 400)));
page.on('pageerror', (e) => console.log('pageerror', e.message));
const t0 = Date.now();
await page.goto(url, { waitUntil: 'load', timeout: 120000 });
for (let i = 0; i < wait / 2000; i++) {
  await new Promise((r) => setTimeout(r, 2000));
  const s = await page.evaluate(() => ({ ready: !!(window.__game && window.__game.ready), err: document.getElementById('err')?.textContent, msg: document.getElementById('loadmsg')?.textContent }));
  if (s.ready || s.err) { console.log(((Date.now() - t0) / 1000).toFixed(1) + 's', JSON.stringify(s)); break; }
  if (i % 5 === 0) console.log(((Date.now() - t0) / 1000).toFixed(1) + 's', JSON.stringify(s));
}
await browser.close();
