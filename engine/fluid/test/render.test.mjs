// Headless end-to-end render of the demo (Node + Dawn). Writes PNGs to test/out/.
//   node test/launch.mjs test/render.test.mjs [quality] [width] [height]
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';
import { getDevice, flush } from './gpu.mjs';
import { App } from '../demo/App.js';

const quality = process.argv[2] ?? 'low';
const W = +(process.argv[3] ?? 640), H = +(process.argv[4] ?? 360);
const outDir = path.join(path.dirname(new URL(import.meta.url).pathname), 'out');
fs.mkdirSync(outDir, { recursive: true });

const { device } = await getDevice();
const t0 = performance.now();
const app = new App(device, { quality, width: W, height: H, outputFormat: 'rgba8unorm', srgbOutput: true });
console.log(`scene build ${app.buildMs.toFixed(0)} ms, app init ${(performance.now() - t0).toFixed(0)} ms, grid ${app.sim.nx}x${app.sim.ny} dx=${app.sim.dx.toFixed(3)}`);

const target = device.createTexture({ size: [W, H], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
const view = target.createView();
const bpr = Math.ceil((W * 4) / 256) * 256;
const readBuf = device.createBuffer({ size: bpr * H, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });

async function save(name) {
  const enc = device.createCommandEncoder();
  enc.copyTextureToBuffer({ texture: target }, { buffer: readBuf, bytesPerRow: bpr }, [W, H]);
  device.queue.submit([enc.finish()]);
  await readBuf.mapAsync(GPUMapMode.READ);
  const src = new Uint8Array(readBuf.getMappedRange());
  const png = new PNG({ width: W, height: H });
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const s = y * bpr + x * 4, d = (y * W + x) * 4;
    png.data[d] = src[s]; png.data[d + 1] = src[s + 1]; png.data[d + 2] = src[s + 2]; png.data[d + 3] = 255;
  }
  readBuf.unmap();
  fs.writeFileSync(path.join(outDir, name + '.png'), PNG.sync.write(png));
  console.log('saved', name, `t=${app.sim.time.toFixed(2)}s particles=${app.sim.stats.particles} vol=${app.sim.stats.volume.toFixed(1)}m3 substeps=${app.sim.lastSubsteps} speed=${(app.vehicle.speed * 3.6).toFixed(0)}km/h water=${(app.vehicle.inWater * 100).toFixed(0)}%`);
}

function f16(h) {
  const s = (h & 0x8000) ? -1 : 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
  if (e === 0) return s * Math.pow(2, -14) * (m / 1024);
  if (e === 31) return m ? NaN : s * Infinity;
  return s * Math.pow(2, e - 15) * (1 + m / 1024);
}
// write the caustics light map (top-down over the domain) as a PNG
async function dumpCaustics(name) {
  const tex = app.fluidR.causticsTexture, R = tex.width;
  const bpr2 = Math.ceil((R * 8) / 256) * 256;
  const buf = device.createBuffer({ size: bpr2 * R, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const enc = device.createCommandEncoder();
  enc.copyTextureToBuffer({ texture: tex }, { buffer: buf, bytesPerRow: bpr2 }, [R, R]);
  device.queue.submit([enc.finish()]);
  await buf.mapAsync(GPUMapMode.READ);
  const u16 = new Uint16Array(buf.getMappedRange());
  const png = new PNG({ width: R, height: R });
  let mx = 0, sum = 0, n = 0;
  for (let y = 0; y < R; y++) for (let x = 0; x < R; x++) {
    const o = (y * bpr2) / 2 + x * 4, d = (y * R + x) * 4;
    for (let c = 0; c < 3; c++) {
      const v = f16(u16[o + c]);
      if (v > 0) { mx = Math.max(mx, v); sum += v; n++; }
      png.data[d + c] = Math.max(0, Math.min(255, Math.round(255 * Math.pow(v / (1 + v), 1 / 2.2))));
    }
    png.data[d + 3] = 255;
  }
  buf.unmap();
  fs.writeFileSync(path.join(outDir, name + '.png'), PNG.sync.write(png));
  console.log('saved', name, `caustics max ${mx.toFixed(2)} mean(lit) ${(sum / Math.max(n, 1)).toFixed(3)}`);
}

async function frames(n, input = {}, dt = 1 / 30) {
  const inp = { throttle: 0, brake: 0, steer: 0, handbrake: 0, ...input };
  for (let i = 0; i < n; i++) {
    app.frame(dt, typeof input === 'function' ? input(i) : inp, view);
    if (i % 4 === 3) await flush(device);
  }
  await flush(device);
  await new Promise((r) => setTimeout(r, 30));
}

const tw = performance.now();
app.warmup(+(process.env.WARMUP ?? 3));
await flush(device);
console.log(`warmup ${(performance.now() - tw).toFixed(0)} ms`);

const tf = performance.now();
await frames(2);
console.log(`frame time (software GPU) ~${((performance.now() - tf) / 2).toFixed(0)} ms`);
await save('01_start');

const shots = process.env.SHOTS ? process.env.SHOTS.split(',') : ['beach', 'ford', 'mud', 'debug'];
if (shots.includes('beach')) {
  app.cam.mode = 'orbit'; app.cam.orbitYaw = Math.PI * 0.85; app.cam.orbitPitch = 0.05; app.cam.dist = 14;
  await frames(24);
  await save('02_beach_orbit');
  app.cam.mode = 'chase';
}
if (shots.includes('ford')) {
  app.teleport('ford');
  await frames(10);
  await frames(40, { throttle: 1 });
  await save('03_ford_drive');
  await frames(8, { throttle: 1 });
  await save('04_ford_drive2');
}
if (shots.includes('mud')) {
  app.teleport('mud');
  await frames(8);
  await frames(40, (i) => ({ throttle: 1, steer: i > 20 ? 0.3 : 0, brake: 0, handbrake: 0 }));
  await save('05_mud');
}
if (shots.includes('surf')) {
  app.teleport('beach');
  app.cam.mode = 'orbit'; app.cam.orbitYaw = -0.35; app.cam.orbitPitch = -0.12; app.cam.dist = 9;
  await frames(Number(process.env.SURF_FRAMES ?? 30));
  await save('07_surf');
  await dumpCaustics('08_caustics');
  app.cam.mode = 'chase';
}
if (shots.includes('debug')) {
  app.fluidR.settings.debugMode = 1;
  await frames(2);
  await save('06_debug_depth');
  app.fluidR.settings.debugMode = 0;
}
console.log('errors:', device.errors.length ? device.errors.slice(0, 3) : 'none');
process.exit(device.errors.length ? 1 : 0);
