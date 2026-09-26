import puppeteer from 'puppeteer-core';
import chromium from '@sparticuz/chromium';
const browser = await puppeteer.launch({
  args: [...chromium.args, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
  executablePath: await chromium.executablePath(),
  headless: 'shell',
  defaultViewport: { width: 1280, height: 720 },
});
const page = await browser.newPage();
await page.goto('http://localhost:5173/', { waitUntil: 'networkidle0', timeout: 60000 });
await new Promise((r) => setTimeout(r, 2500));
await page.click('#start-btn');
await new Promise((r) => setTimeout(r, 800));
await page.evaluate(() => {
  const g = window.__game.state.player;
  g.pos.set(10, 0, 52);      // 6m west of the intact truck at (16,52)
  g.yaw = -Math.PI / 2;      // face +x
  g.pitch = -0.08;
});
await new Promise((r) => setTimeout(r, 500));
await page.screenshot({ path: 'truck-studio.png' });
console.log('done');
await browser.close();
