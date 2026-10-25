// End-to-end browser test: serves the repo, opens the demo in headless Chromium with WebGPU
// (SwiftShader when no GPU is present), drives the car with real key presses and saves screenshots.
//   node test/browser.test.mjs [quality] [width] [height]
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';

const here = path.dirname(new URL(import.meta.url).pathname);
const root = path.resolve(here, '../../..');
const outDir = path.join(here, 'out');
fs.mkdirSync(outDir, { recursive: true });
const quality = process.argv[2] ?? 'low';
const W = +(process.argv[3] ?? 960), H = +(process.argv[4] ?? 540);

// ---- static server ------------------------------------------------------------------------
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.woff2': 'font/woff2' };
const server = http.createServer((req, res) => {
  const p = path.join(root, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  if (!p.startsWith(root)) { res.writeHead(403).end(); return; }
  fs.readFile(p.endsWith('/') ? p + 'index.html' : p, (err, data) => {
    if (err) { res.writeHead(404).end('not found'); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(p)] ?? (p.endsWith('/') ? 'text/html' : 'application/octet-stream') });
    res.end(data);
  });
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const port = server.address().port;

// ---- chromium with WebGPU -----------------------------------------------------------------
const { default: puppeteer } = await import('puppeteer-core');
const { default: chromium } = await import('@sparticuz/chromium');
const exe = await chromium.executablePath();
const pkg = path.dirname(path.dirname(fs.realpathSync(path.join(here, '../node_modules/@sparticuz/chromium/bin/chromium.br'))));
const libDir = path.join(os.tmpdir(), 'frontier-chromium-libs');
if (!fs.existsSync(path.join(libDir, 'libnss3.so'))) {
  fs.mkdirSync(libDir, { recursive: true });
  const buf = zlib.brotliDecompressSync(fs.readFileSync(path.join(pkg, 'bin', 'al2023.tar.br')));
  let off = 0;
  while (off + 512 <= buf.length) {
    const name = buf.toString('utf8', off, off + 100).replace(/\0.*$/, '');
    if (!name) break;
    const size = parseInt(buf.toString('utf8', off + 124, off + 136).replace(/\0.*$/, '').trim() || '0', 8);
    const type = buf[off + 156];
    off += 512;
    if ((type === 48 || type === 0) && name.includes('lib/')) fs.writeFileSync(path.join(libDir, path.basename(name)), buf.subarray(off, off + size), { mode: 0o755 });
    off += Math.ceil(size / 512) * 512;
  }
}
const swDir = path.dirname(exe);
const browser = await puppeteer.launch({
  executablePath: exe,
  headless: 'shell',
  args: ['--no-sandbox', '--no-zygote', '--enable-unsafe-webgpu', '--enable-unsafe-swiftshader', '--use-webgpu-adapter=swiftshader',
    '--disable-dev-shm-usage', '--ignore-gpu-blocklist', '--use-vulkan=swiftshader', '--enable-features=Vulkan', '--use-gl=angle', '--use-angle=swiftshader',
    `--window-size=${W},${H}`],
  env: { ...process.env, LD_LIBRARY_PATH: `${libDir}:${swDir}`, VK_ICD_FILENAMES: path.join(swDir, 'vk_swiftshader_icd.json') },
  defaultViewport: { width: W, height: H },
  protocolTimeout: 600000,
});
const page = await browser.newPage();
const errors = [];
page.on('console', (m) => { const t = m.text(); if (m.type() === 'error') errors.push(t); console.log('[page]', t.slice(0, 300)); });
page.on('pageerror', (e) => { errors.push(String(e)); console.log('[pageerror]', e); });

const url = `http://127.0.0.1:${port}/engine/fluid/demo/?q=${quality}&warmup=1&scale=${process.env.SCALE ?? 1}`;
console.log('open', url);
await page.goto(url, { waitUntil: 'load', timeout: 120000 });
await page.waitForFunction(() => window.app && document.getElementById('overlay').style.display === 'none', { timeout: 600000, polling: 1000 });
console.log('demo running');
const shot = async (name) => { await page.screenshot({ path: path.join(outDir, `browser_${name}.png`) }); console.log('screenshot', name, await page.evaluate(() => `t=${app.sim.time.toFixed(2)} fps=${document.getElementById('sFps').textContent} particles=${document.getElementById('sParts').textContent} speed=${document.getElementById('spd').textContent} surf=${document.getElementById('surf').textContent}`)); };
const waitFrames = (n) => page.evaluate((n) => new Promise((r) => { let k = 0; const f = () => (++k >= n ? r() : requestAnimationFrame(f)); requestAnimationFrame(f); }), n);

let failed = 0;
const check = (name, ok, detail = '') => { if (!ok) failed++; console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  — ' + detail : ''}`); };
const visible = (sel) => page.evaluate((s) => { const el = document.querySelector(s); const cs = getComputedStyle(el); return cs.display !== 'none' && cs.visibility !== 'hidden' && +cs.opacity > 0.05; }, sel);
const settle = (ms = 450) => new Promise((r) => setTimeout(r, ms)); // CSS transitions
const cam = () => page.evaluate(() => {
  const e = app.editorCam;
  return { mode: app.cam.mode, pos: e.position.slice(), yaw: e.yaw, pitch: e.pitch, fov: e.fov, baseFov: e.baseFov, speed: e.speedSetting, f: e.forward(), ff: e.forwardFlat(),
    pivot: e.pivot && e.pivot.slice(), flying: viewportInput.flying, throttle: app.lastInput?.throttle ?? null, sel: app.selection?.kind ?? null,
    // heading of the rendered view (any camera mode)
    viewYaw: Math.atan2(app.cam.target[0] - app.cam.pos[0], app.cam.target[2] - app.cam.pos[2]) };
});
const sub = (a, b) => a.map((x, i) => x - b[i]);
const dot = (a, b) => a.reduce((s, x, i) => s + x * b[i], 0);
const len = (a) => Math.hypot(...a);
const cx = Math.round(W / 2), cy = Math.round(H / 2);

await waitFrames(3);
await shot('01_start');

// ---- hide / show the menu ---------------------------------------------------------------------
await page.keyboard.press('KeyH');
await settle();
check('H hides the menu', !(await visible('#panel')) && (await visible('#menuBtn')));
await page.click('#menuBtn');
await settle();
check('Menu button brings it back', (await visible('#panel')) && !(await visible('#menuBtn')));
await page.click('#hideMenu');
await settle();
check('hide button in the panel header hides it', !(await visible('#panel')));
await page.keyboard.press('KeyH');
await settle();
check('H shows it again', await visible('#panel'));
await page.keyboard.press('KeyG');
await settle(100);
check('G (game view) hides all UI', !(await visible('#panel')) && !(await visible('.speedo')) && !(await visible('#camhud')) && !(await visible('#menuBtn')));
await page.keyboard.press('KeyG');
await settle(100);
check('G again restores the UI', (await visible('#panel')) && (await visible('.speedo')) && (await visible('#camhud')));

// ---- drive into the ford, cannonball ----------------------------------------------------------
await page.click('#scenarios button[data-s="ford"]');
await waitFrames(4);
await page.keyboard.down('KeyW');
await waitFrames(Number(process.env.DRIVE_FRAMES ?? 24));
await shot('02_ford');
await page.keyboard.up('KeyW');
await page.keyboard.press('KeyB');
await waitFrames(14);
await shot('03_cannonball');

// ---- Unreal camera navigation -----------------------------------------------------------------
check('right-click context menu is suppressed on the viewport', await page.evaluate(() => !document.getElementById('gfx').dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true }))));
let c0 = await cam();
check('starts in the chase camera', c0.mode === 'chase');
await page.mouse.move(cx, cy);
await page.mouse.down({ button: 'right' });
await page.mouse.move(cx + 150, cy, { steps: 6 });
let c1 = await cam();
const dYaw = Math.atan2(Math.sin(c1.yaw - c0.viewYaw), Math.cos(c1.yaw - c0.viewYaw));
check('RMB drag switches to the free camera and turns right', c1.mode === 'free' && Math.abs(dYaw + 150 * 0.0032) < 0.05, `mode ${c1.mode}, Δyaw ${dYaw.toFixed(3)} rad (expected -0.480)`);
c0 = await cam();
await page.keyboard.down('KeyW');
await waitFrames(5);
c1 = await cam();
await page.keyboard.up('KeyW');
const fly = sub(c1.pos, c0.pos);
check('RMB + W flies forward', dot(fly, c0.f) > 0.5 && dot(fly, c0.f) > 0.95 * len(fly), `${len(fly).toFixed(2)} m`);
check('while flying W does not drive the car', c1.flying && c1.throttle === 0, `flying ${c1.flying}, throttle ${c1.throttle}`);
await page.mouse.wheel({ deltaY: -100 });
c1 = await cam();
check('RMB + wheel raises the camera speed', c1.speed === c0.speed + 1, `${c0.speed} → ${c1.speed}`);
await page.keyboard.down('KeyC');
await waitFrames(3);
await page.keyboard.up('KeyC');
c1 = await cam();
check('RMB + C zooms in', c1.fov < c1.baseFov - 0.05 && c1.mode === 'free', `${(c1.fov * 57.3).toFixed(1)}°`);
await page.mouse.up({ button: 'right' });
c1 = await cam();
check('releasing RMB snaps the FOV back', Math.abs(c1.fov - c1.baseFov) < 1e-6);
await waitFrames(2);
await shot('04_free_camera');

c0 = await cam();
await page.mouse.down({ button: 'middle' });
await page.mouse.move(cx, cy - 60, { steps: 4 });
await page.mouse.up({ button: 'middle' });
c1 = await cam();
check('MMB drag up pans the camera up', c1.pos[1] - c0.pos[1] > 1 && Math.abs(c1.yaw - c0.yaw) < 1e-9, `Δy ${(c1.pos[1] - c0.pos[1]).toFixed(2)} m`);
await page.mouse.move(cx, cy);
c0 = await cam();
await page.mouse.down({ button: 'left' });
await page.mouse.move(cx, cy - 80, { steps: 8 });
await page.mouse.up({ button: 'left' });
c1 = await cam();
const lm = sub(c1.pos, c0.pos);
check('LMB drag up moves forward on the ground plane', dot(lm, c0.ff) > 1 && Math.abs(lm[1]) < 1e-6, `${dot(lm, c0.ff).toFixed(2)} m, Δy ${lm[1].toFixed(3)}`);
await page.mouse.move(cx, cy);
await waitFrames(1);

await page.keyboard.down('Alt');
await page.mouse.down({ button: 'left' });
await page.mouse.move(cx + 120, cy, { steps: 6 });
await page.mouse.up({ button: 'left' });
c1 = await cam();
const d1 = c1.pivot && len(sub(c1.pivot, c1.pos));
check('Alt + LMB orbits: still looking at the pivot', c1.pivot && dot(sub(c1.pivot, c1.pos), c1.f) / d1 > 0.999, `pivot ${d1?.toFixed(2)} m away`);
await page.mouse.move(cx, cy);
c0 = await cam();
await page.mouse.down({ button: 'right' });
await page.mouse.move(cx + 100, cy, { steps: 5 });
await page.mouse.up({ button: 'right' });
await page.keyboard.up('Alt');
c1 = await cam();
check('Alt + RMB drag right dollies toward the pivot', len(sub(c1.pivot, c1.pos)) < 0.8 * len(sub(c0.pivot, c0.pos)), `${len(sub(c0.pivot, c0.pos)).toFixed(2)} → ${len(sub(c1.pivot, c1.pos)).toFixed(2)} m`);
await page.mouse.move(cx, cy);

await page.keyboard.press('KeyF');
await page.waitForFunction(() => !app.editorCam.focusing, { polling: 'raf', timeout: 300000 });
const ndc = await page.evaluate(async () => {
  const { projectToNdc, objectBounds } = await import('/engine/fluid/demo/picking.js');
  const e = app.editorCam;
  return projectToNdc(e.position, e.target(1), e.fov, app.aspect, objectBounds(null, app.vehicle).center);
});
check('F focuses the car', ndc && Math.hypot(ndc[0], ndc[1]) < 0.25, `car at ndc ${ndc?.map((x) => x.toFixed(3))}`);
await waitFrames(1);
await page.mouse.click(cx, cy);
c1 = await cam();
check('LMB click selects the car', c1.sel === 'vehicle');
await page.keyboard.press('KeyG');
await waitFrames(2);
await shot('05_selected_game_view');
await page.keyboard.press('KeyG');
await page.keyboard.press('Escape');
c1 = await cam();
check('Esc clears the selection', c1.sel === null);
await page.keyboard.press('KeyC');
c1 = await cam();
check('C returns to the chase camera', c1.mode === 'chase');
await waitFrames(4);
await shot('06_back_to_chase');

console.log('errors:', errors.length ? errors : 'none');
console.log(failed ? `${failed} UI check(s) FAILED` : 'all UI checks passed');
await browser.close();
server.close();
process.exit(errors.length || failed ? 1 : 0);
