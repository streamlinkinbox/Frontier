// Screenshot the game with filename watermark + console-logged state.
// Usage: LD_LIBRARY_PATH=/tmp/al2023-lib/lib node scripts/screenshot.mjs [url] [prefix]
import puppeteer from 'puppeteer-core';
import chromium from '@sparticuz/chromium';

const url = process.argv[2] || 'http://localhost:5173/';
const prefix = process.argv[3] || 'shot';

const browser = await puppeteer.launch({
  args: [...chromium.args, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
  executablePath: await chromium.executablePath(),
  headless: 'shell',
  defaultViewport: { width: 1280, height: 720 },
});

const page = await browser.newPage();
page.on('console', (m) => console.log(`[console.${m.type()}]`, m.text()));
page.on('pageerror', (e) => console.log('[pageerror]', e.message));

const snap = async (name) => {
  const state = await page.evaluate((label) => {
    let el = document.getElementById('watermark');
    if (!el) {
      el = document.createElement('div');
      el.id = 'watermark';
      el.style.cssText = 'position:fixed;top:0;left:0;right:0;z-index:999;background:#c00;color:#fff;font:700 28px monospace;text-align:center;padding:6px;pointer-events:none';
      document.body.appendChild(el);
    }
    el.textContent = label;
    const g = window.__game, cam = window.__camera;
    return {
      label,
      player: g ? g.state.player.pos.toArray().map((v) => +v.toFixed(1)) : null,
      yaw: g ? +g.state.player.yaw.toFixed(2) : null,
      pitch: g ? +g.state.player.pitch.toFixed(2) : null,
      cam: cam ? cam.position.toArray().map((v) => +v.toFixed(1)) : null,
      playing: g ? g.state.playing : null, frames: window.__frames,
      car: window.__car ? { pos: window.__car.position.toArray().map((v) => +v.toFixed(1)), visible: window.__car.visible } : null,
    };
  }, name);
  await new Promise((r) => setTimeout(r, 200));
  await page.screenshot({ path: `${name}.png` });
  console.log('SNAP', JSON.stringify(state));
};

await page.goto(url, { waitUntil: 'networkidle0', timeout: 60000 });
await new Promise((r) => setTimeout(r, 3000));
await snap(`${prefix}-1-menu`);

await page.click('#start-btn');
await new Promise((r) => setTimeout(r, 1200));
await snap(`${prefix}-2-spawn`);

await page.evaluate(() => {
  window.__game.state.player.yaw = -2.48;
  window.__game.state.player.pitch = -0.05;
});
await new Promise((r) => setTimeout(r, 600));
await snap(`${prefix}-3-carview`);

await page.evaluate(() => {
  window.__game.state.player.yaw = Math.PI;
  window.__game.state.player.pitch = -0.02;
});
await page.keyboard.down('KeyW');
await new Promise((r) => setTimeout(r, 4500));
await page.keyboard.up('KeyW');
await snap(`${prefix}-4-forward`);

await browser.close();
