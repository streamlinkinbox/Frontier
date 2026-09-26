import puppeteer from 'puppeteer-core';
import chromium from '@sparticuz/chromium';
const browser = await puppeteer.launch({
  args: [...chromium.args, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
  executablePath: await chromium.executablePath(),
  headless: 'shell',
  defaultViewport: { width: 1280, height: 720 },
});
const page = await browser.newPage();
page.on('pageerror', (e) => console.log('[pageerror]', e.message));
await page.goto('http://localhost:5173/', { waitUntil: 'networkidle0', timeout: 60000 });
await new Promise((r) => setTimeout(r, 2500));
await page.click('#start-btn');
await new Promise((r) => setTimeout(r, 800));
// teleport beside the car and look at its left profile
await page.evaluate(() => {
  const g = window.__game.state.player;
  g.pos.set(8.2, 0, -1);   // 4.8m west of the car at (13,-1)
  g.yaw = -Math.PI / 2;    // face +x
  g.pitch = -0.02;
});
await new Promise((r) => setTimeout(r, 400));
await page.screenshot({ path: 'car-studio-side.png' });
// look down at the roof a bit
await page.evaluate(() => {
  window.__game.state.player.pitch = 0.45;
});
await new Promise((r) => setTimeout(r, 400));
await page.screenshot({ path: 'car-studio-roof.png' });
console.log('done');
await browser.close();
