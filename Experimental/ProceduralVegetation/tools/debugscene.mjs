// Renders minimal junction test scenes for visual inspection.
// Usage: node tools/debugscene.mjs out_dir scene mode [camera preset]
import puppeteer from 'puppeteer-core';
import fs from 'node:fs';
import path from 'node:path';
const [out = '/tmp/shots', scene = 'side', mode = 'wireframe', cam = 'default'] = process.argv.slice(2);
fs.mkdirSync(out, { recursive: true });
const zero4 = [0, 0, 0, 0];
const SCENES = {
  // one trunk, three side limbs, nothing else
  side: { botany: { levels: 2, scale: 10, scaleV: 0, ratio: 0.03, flare: 0.3, baseSize: [0.3, 0, 0, 0], branches: [1, 3, 0, 0], length: [1, 0.5, 0, 0], curveRes: [6, 4, 1, 1], curve: zero4, curveV: zero4, bendV: zero4, segSplits: zero4, downAngle: [0, 55, 0, 0], downAngleV: [0, 0, 0, 0], rotate: [0, 120, 0, 0], rotateV: zero4, leaves: 0, attractionUp: 0, baseSplits: 0 }, mesh: { rootLobes: 0 } },
  // one trunk that forks once into two
  fork: { botany: { levels: 1, scale: 10, scaleV: 0, ratio: 0.03, flare: 0.3, baseSize: [0.3, 0, 0, 0], branches: [1, 0, 0, 0], length: [1, 0.5, 0, 0], curveRes: [4, 1, 1, 1], curve: zero4, curveV: zero4, bendV: zero4, segSplits: [1, 0, 0, 0], splitAngle: [40, 0, 0, 0], splitAngleV: zero4, leaves: 0, attractionUp: 0, baseSplits: 0 }, mesh: { rootLobes: 0 } },
  // three-way base split
  fork3: { botany: { levels: 1, scale: 10, scaleV: 0, ratio: 0.03, flare: 0.3, baseSize: [0.3, 0, 0, 0], branches: [1, 0, 0, 0], length: [1, 0.5, 0, 0], curveRes: [6, 1, 1, 1], curve: zero4, curveV: zero4, bendV: zero4, segSplits: [0, 0, 0, 0], baseSplits: 2, splitAngle: [35, 0, 0, 0], splitAngleV: zero4, leaves: 0, attractionUp: 0 }, mesh: { rootLobes: 0 } },
  // side branches with sub branches (3 levels)
  twig: { botany: { levels: 3, scale: 10, scaleV: 0, ratio: 0.03, flare: 0.3, baseSize: [0.3, 0.1, 0, 0], branches: [1, 3, 6, 0], length: [1, 0.5, 0.5, 0], curveRes: [6, 4, 3, 1], curve: zero4, curveV: zero4, bendV: zero4, segSplits: zero4, downAngle: [0, 55, 50, 0], downAngleV: zero4, rotate: [0, 120, 140, 0], rotateV: zero4, leaves: 0, attractionUp: 0, baseSplits: 0 }, mesh: { rootLobes: 0 } },
};
const CAMS = {
  default: (h) => [[h * 0.9, h * 0.62, h * 1.1], [0, h * 0.55, 0]],
  junction: (h) => [[h * 0.35, h * 0.62, h * 0.45], [0, h * 0.5, 0]],
  fork: (h) => [[h * 0.55, h * 0.6, h * 0.7], [0, h * 0.62, 0]],
  base: (h) => [[h * 0.3, h * 0.42, h * 0.4], [0, h * 0.33, 0]],
  top: (h) => [[0.01, h * 1.6, 0.01], [0, h * 0.6, 0]],
};
const browser = await puppeteer.launch({
  executablePath: process.env.CHROME_PATH || '/tmp/chromium', headless: true,
  args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--disable-dev-shm-usage', '--single-process', '--no-zygote'],
  defaultViewport: { width: 1100, height: 800, deviceScaleFactor: 1 },
});
const page = await browser.newPage();
page.on('pageerror', (e) => console.log('[pageerror]', e.message));
await page.goto('http://localhost:5173/', { waitUntil: 'networkidle0', timeout: 120000 });
await page.waitForFunction(() => window.frontier && window.frontier.ready(), { timeout: 120000 });
await page.evaluate((sc, m) => { window.frontier.setPreset('Quaking Aspen'); window.frontier.setParams(Object.assign({ seed: 3 }, sc)); window.frontier.setMode(m); window.frontier.setView({ windEnabled: false, showLeaves: false, showGrid: false, showWire: m !== 'wireframe' }); }, SCENES[scene], mode);
await new Promise((r) => setTimeout(r, 500));
await page.waitForFunction(() => window.frontier.ready(), { timeout: 120000 });
const h = await page.evaluate(() => window.frontier.result.summary.height);
let [pos, target] = CAMS[cam] ? CAMS[cam](h) : [null, null];
if (!CAMS[cam]) {
  // cam = "L<level>:<kind>:<index>:<mult>" – look at a specific junction
  const [lv, kind, idx, multS] = cam.split(':');
  const sample = await page.evaluate((l, k, i) => window.frontier.junctionSamples(Number(l), 400, k)[Number(i)], lv.replace('L', ''), kind, idx);
  const mult = Number(multS) || 8;
  const dist = (sample.parentRadius + sample.radius) * mult;
  const norm = (v) => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  let n = cross(sample.parentDir, sample.dir);
  if (Math.hypot(...n) < 1e-3) n = [1, 0, 0];
  n = norm(n);
  if (kind === 'fork') {
    // Fork point is the child's node 0. Look perpendicular to the plane spanned by the two fork siblings.
    const sib = await page.evaluate((l, k, i) => window.frontier.junctionSamples(Number(l), 400, k).find((x, j) => j !== Number(i) && x.pos.every((v, a) => Math.abs(v - window.frontier.junctionSamples(Number(l), 400, k)[Number(i)].pos[a]) < 1e-6)), lv.replace('L', ''), kind, idx);
    if (sib) { n = cross(sample.dir, sib.dir); if (Math.hypot(...n) < 1e-3) n = [1, 0, 0]; n = norm(n); }
    target = [sample.pos[0], sample.pos[1] + sample.radius * 1.5, sample.pos[2]];
    let hn = [n[0], 0, n[2]]; const hl = Math.hypot(hn[0], hn[2]) || 1; hn = [hn[0] / hl, 0, hn[2] / hl];
    pos = [target[0] + hn[0] * dist, target[1] + dist * 0.15, target[2] + hn[2] * dist];
  } else {
    target = [sample.pos[0] + sample.dir[0] * sample.radius * 2, sample.pos[1] + sample.dir[1] * sample.radius * 2, sample.pos[2] + sample.dir[2] * sample.radius * 2];
    pos = [target[0] + n[0] * dist + sample.dir[0] * dist * 0.2, target[1] + n[1] * dist + 0.25 * dist, target[2] + n[2] * dist + sample.dir[2] * dist * 0.2];
  }
  console.log('sample', JSON.stringify(sample));
}
await page.evaluate((p, t) => window.frontier.setCamera(p, t), pos, target);
await new Promise((r) => setTimeout(r, 700));
console.log('cam', JSON.stringify(pos), 'target', JSON.stringify(target), await page.evaluate(() => { const c = window.frontier.viewer.camera.position; const t = window.frontier.viewer.controls.target; return JSON.stringify([[c.x, c.y, c.z], [t.x, t.y, t.z]]); }));
const file = path.join(out, `scene_${scene}_${mode}_${cam.replace(/[^a-z0-9]/gi, '')}.png`);
await page.screenshot({ path: file });
const info = await page.evaluate(() => { const r = window.frontier.result; return { faces: r.report.faces, quads: r.report.quadRatio, closed: r.report.closed, manifold: r.report.manifold, chi: r.report.eulerCharacteristic, dropped: r.stats.droppedStems, height: r.summary.height }; });
console.log(file, JSON.stringify(info));
await browser.close();
