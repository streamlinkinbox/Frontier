/**
 * Headless smoke test — loads the app in Chrome, captures console errors and
 * runtime exceptions, waits for the boot overlay to clear, exercises a few
 * interactions and writes a screenshot.
 *
 *   node tools/smoke.mjs [--shots a,b,c] [--wait 9000]
 */
import puppeteer from 'puppeteer';
import { mkdirSync, writeFileSync } from 'node:fs';

const arg = (k, d) => {
  const i = process.argv.indexOf('--' + k);
  return i > -1 ? process.argv[i + 1] : d;
};
const URL = arg('url', 'http://127.0.0.1:5173/');
const WAIT = +arg('wait', 9000);
const OUT = arg('out', 'tools/shots');
mkdirSync(OUT, { recursive: true });

const browser = await puppeteer.launch({
  headless: 'new',
  args: [
    '--no-sandbox', '--disable-setuid-sandbox',
    '--use-gl=swiftshader', '--enable-unsafe-swiftshader',
    '--enable-webgl', '--ignore-gpu-blocklist',
    '--disable-dev-shm-usage',
    '--window-size=1600,900',
  ],
});
const page = await browser.newPage();
await page.setViewport({ width: 1600, height: 900, deviceScaleFactor: 1 });

const errors = [], logs = [];
page.on('console', (m) => {
  const t = m.type();
  const txt = `[${t}] ${m.text()}`;
  logs.push(txt);
  if (t === 'error') errors.push(txt);
});
page.on('pageerror', (e) => errors.push(`[pageerror] ${e.message}\n${e.stack || ''}`));
page.on('requestfailed', (r) => errors.push(`[404] ${r.url()} ${r.failure()?.errorText}`));

console.log('→ loading', URL);
await page.goto(URL, { waitUntil: 'networkidle2', timeout: 90000 });
await new Promise((r) => setTimeout(r, WAIT));

const bootGone = await page.evaluate(() => !document.querySelector('#boot'));
const diag = await page.evaluate(() => {
  const A = window.ANURA;
  if (!A) return { ok: false, why: 'window.ANURA missing' };
  const th = A.mosq.thorax;
  let tris = 0, meshes = 0, bones = 0, hidden = 0;
  A.mosq.root.traverse((o) => {
    if (o.isMesh) { meshes++; if (!o.visible) hidden++; tris += (o.geometry.index ? o.geometry.index.count : o.geometry.attributes.position.count) / 3; }
    if (o.type === 'Object3D' && o.name) bones++;
  });
  const feet = {};
  for (const k in A.gait.legs) {
    const L = A.gait.legs[k];
    const p = L.solver._footWorld;
    feet[k] = { planted: L.planted, x: +p.x.toFixed(3), y: +p.y.toFixed(3), z: +p.z.toFixed(3) };
  }
  return {
    ok: true, tris: Math.round(tris), meshes, bones, hidden,
    thorax: th.position.toArray().map((v) => +v.toFixed(2)),
    flying: A.brain.flying,
    attached: A.gait.attached,
    up: A.gait.upSmooth.toArray().map((v) => +v.toFixed(3)),
    speed: +A.gait.speed.toFixed(2),
    stride: +A.gait.strideFreq.toFixed(2),
    duty: +A.gait.duty.toFixed(3),
    tripodT: +A.gait.tripodT.toFixed(2),
    maxSlipUm: +(Math.max(...Object.values(A.gait.legs).map((l) => l.slip)) * 1000).toFixed(3),
    flexDeg: Object.fromEntries(Object.entries(A.gait.legs).map(([k, l]) => [k, +(l.solver.flexion * 57.3).toFixed(1)])),
    wingHz: +A.flight.frequency.toFixed(0),
    feedStage: A.feeding.stage,
    renderer: A.renderer.info.render,
    feet,
  };
});
console.log('boot overlay removed:', bootGone);
console.log(JSON.stringify(diag, null, 2));

const shot = async (name) => {
  await page.screenshot({ path: `${OUT}/${name}.png` });
  console.log('  shot →', `${OUT}/${name}.png`);
};
await shot('01-default');

// Interactions
for (const [sel, name, wait] of [
  ['#bClimb', '02-climb', 9000],
  ['#bFeed', '03-feed', 12000],
  ['#cMacro', '04-macro', 3000],
  ['#bWall', '05-wall', 12000],
  ['#bGlass', '06-glass', 12000],
  ['#cWide', '07-wide', 4000],
  ['#rXray', '08-xray', 2500],
  ['#rViz', '09-rigviz', 2500],
]) {
  try {
    await page.click(sel);
    await new Promise((r) => setTimeout(r, wait));
    await shot(name);
  } catch (e) { errors.push(`[click ${sel}] ${e.message}`); }
}

const diag2 = await page.evaluate(() => {
  const A = window.ANURA;
  return {
    up: A.gait.upSmooth.toArray().map((v) => +v.toFixed(3)),
    attached: A.gait.attached, feedStage: A.feeding.stage,
    engorge: +A.feeding.engorge.toFixed(2),
    maxSlipUm: +(Math.max(...Object.values(A.gait.legs).map((l) => l.slip)) * 1000).toFixed(3),
    flexDeg: Object.fromEntries(Object.entries(A.gait.legs).map(([k, l]) => [k, +(l.solver.flexion * 57.3).toFixed(1)])),
    tris: A.renderer.info.render.triangles, calls: A.renderer.info.render.calls,
  };
});
console.log('after interaction:', JSON.stringify(diag2, null, 2));

writeFileSync(`${OUT}/console.log`, logs.join('\n'));
console.log('\n=== ERRORS (' + errors.length + ') ===');
for (const e of errors.slice(0, 40)) console.log(e);

await browser.close();
process.exit(errors.length ? 1 : 0);
