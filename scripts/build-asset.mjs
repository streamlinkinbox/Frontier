import fs from 'node:fs';
import { launchBrowser } from './browser.mjs';
import { normalizeGLB } from './normalize-glb.mjs';
const browser = await launchBrowser();
try {
  const page = await browser.newPage({ viewport: { width: 1440, height: 960 }, deviceScaleFactor: 1 });
  page.on('console', msg => { if (msg.type() === 'error' || msg.type() === 'warn') console.log(msg.type(), msg.text()); });
  page.on('pageerror', err => console.error(err));
  await page.goto('http://127.0.0.1:5173/?build=1', { waitUntil: 'networkidle' });
  await page.waitForFunction(() => window.mantisReady || window.mantisError, null, { timeout: 120000 });
  const error = await page.evaluate(() => window.mantisError); if (error) throw new Error(error);
  console.log('Geometry:', await page.evaluate(() => window.mantis.stats));
  const base64 = await page.evaluate(async () => {
    const buffer = await window.mantis.exportGLB();
    return new Promise(resolve => { const fr = new FileReader(); fr.onload = () => resolve(fr.result.split(',')[1]); fr.readAsDataURL(new Blob([buffer])); });
  });
  fs.mkdirSync('public/models', { recursive: true });
  fs.writeFileSync('public/models/mantis.glb', normalizeGLB(Buffer.from(base64, 'base64')));
  console.log('GLB saved:', fs.statSync('public/models/mantis.glb').size, 'bytes');
  await page.evaluate(() => window.mantis.poseAt('Idle', 0));
  fs.mkdirSync('.screenshots', { recursive: true });
  if(process.argv.includes('--screenshot')) await page.screenshot({ path: '.screenshots/generator.png', fullPage: true, timeout: 90000, animations: 'disabled' });
} finally { await browser.close(); }
