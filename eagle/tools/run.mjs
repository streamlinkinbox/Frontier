// Headless harness: loads the game, waits until the cave is ready, then runs a list of actions.
//   node run.mjs <url> <outPrefix> <actions.json | probe.js>
// A .js argument is evaluated in the page and its return value printed as JSON.
// A .json argument is a list of steps: {"eval": "js expression"} | {"shot": "name"} | {"wait": ms}
// Screenshots are written to <outPrefix>_<name>.png.
import fs from 'node:fs';
import puppeteer from 'puppeteer-core';
import chromium from '@sparticuz/chromium';

const [url = 'http://localhost:5174/?manual&nohud', prefix = 'out/run', script] = process.argv.slice(2);
if (!script) { console.error('usage: node run.mjs <url> <outPrefix> <actions.json|probe.js>'); process.exit(1); }
const actions = script.endsWith('.js') ? [{ eval: fs.readFileSync(script, 'utf8') }] : JSON.parse(fs.readFileSync(script, 'utf8'));
fs.mkdirSync(prefix.replace(/[^/]*$/, '') || '.', { recursive: true });

const browser = await puppeteer.launch({
  executablePath: process.env.CHROME || await chromium.executablePath(),
  args: [...chromium.args, '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  defaultViewport: { width: 800, height: 450 }, headless: true, protocolTimeout: 1800000,
});
const page = await browser.newPage();
page.on('pageerror', (e) => console.log('pageerror', e.message));
page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') console.log('console.' + m.type(), m.text().slice(0, 300)); });
await page.goto(url, { waitUntil: 'load', timeout: 120000 });
await page.waitForFunction('window.__game && window.__game.ready', { timeout: 600000, polling: 500 });
for (const a of actions) {
  if (a.eval) console.log('eval -> ' + JSON.stringify(await page.evaluate(a.eval)));
  if (a.shot) { await page.screenshot({ path: `${prefix}_${a.shot}.png` }); console.log('shot ' + a.shot); }
  if (a.wait) await new Promise((r) => setTimeout(r, a.wait));
}
await browser.close();
