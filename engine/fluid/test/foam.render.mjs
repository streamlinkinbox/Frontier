// Foam close-ups for visual tuning and before/after comparisons (Node + Dawn, headless).
//   node test/launch.mjs test/foam.render.mjs [width] [height]      env: SHOTS=ford,beach,rapids TAG=name
// Writes test/out/foam_<TAG>_<shot>.png. Simulation frames run without rendering (fast); only the
// shots are rendered, from fixed free-camera poses relative to the car / scenario.
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';
import { getDevice, flush } from './gpu.mjs';
import { App } from '../demo/App.js';

const W = +(process.argv[2] ?? 960), H = +(process.argv[3] ?? 540);
const TAG = process.env.TAG ?? 'new';
const SHOTS = (process.env.SHOTS ?? 'ford,beach,rapids').split(',');
const outDir = path.join(path.dirname(new URL(import.meta.url).pathname), 'out');
fs.mkdirSync(outDir, { recursive: true });

const { device } = await getDevice();
const app = new App(device, { quality: process.env.Q ?? 'low', width: W, height: H, outputFormat: 'rgba8unorm', srgbOutput: true });
const target = device.createTexture({ size: [W, H], format: 'rgba8unorm', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
const view = target.createView();
const bpr = Math.ceil((W * 4) / 256) * 256;
const readBuf = device.createBuffer({ size: bpr * H, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
const t0 = performance.now();
const log = (...a) => console.log(`[${((performance.now() - t0) / 1000).toFixed(0)}s]`, ...a);

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
  const file = `foam_${TAG}_${name}.png`;
  fs.writeFileSync(path.join(outDir, file), PNG.sync.write(png));
  log('saved', file, `t=${app.sim.time.toFixed(2)}s particles=${app.sim.stats.particles} speed=${(app.vehicle.speed * 3.6).toFixed(0)}km/h`);
}
function f16(h) {
  const s = (h & 0x8000) ? -1 : 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
  if (e === 0) return s * Math.pow(2, -14) * (m / 1024);
  if (e === 31) return m ? NaN : s * Infinity;
  return s * Math.pow(2, e - 15) * (1 + m / 1024);
}
// statistics of the screen-space foam buffer: splat weight W per pixel (calibration aid)
async function foamStats() {
  const tex = app.fluidR.foamTex?.[0];
  if (!tex) return '';
  const bpr2 = Math.ceil((tex.width * 8) / 256) * 256;
  const buf = device.createBuffer({ size: bpr2 * tex.height, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const enc = device.createCommandEncoder();
  enc.copyTextureToBuffer({ texture: tex }, { buffer: buf, bytesPerRow: bpr2 }, [tex.width, tex.height]);
  device.queue.submit([enc.finish()]);
  await buf.mapAsync(GPUMapMode.READ);
  const u16 = new Uint16Array(buf.getMappedRange());
  const ws = [];
  for (let y = 0; y < tex.height; y++) for (let x = 0; x < tex.width; x++) { const w = f16(u16[(y * bpr2) / 2 + x * 4]) / 16; if (w > 1e-3) ws.push(w); }
  buf.unmap(); buf.destroy();
  ws.sort((a, b) => a - b);
  const q = (f) => (ws.length ? ws[Math.floor(f * (ws.length - 1))].toFixed(2) : '-');
  return `foam px ${(100 * ws.length / (tex.width * tex.height)).toFixed(1)}%  W p10 ${q(0.1)} p50 ${q(0.5)} p90 ${q(0.9)} max ${q(1)}`;
}
const idle = { throttle: 0, brake: 0, steer: 0, handbrake: 0 };
async function simulate(n, input = idle, dt = 1 / 30) {
  for (let i = 0; i < n; i++) {
    app.frame(dt, typeof input === 'function' ? input(i) : { ...idle, ...input }, null);
    if (i % 4 === 3) await flush(device);
  }
  await flush(device);
}
// render a still from a free-camera pose (a couple of frames so async readbacks/TAA-free state settle)
async function shot(name, eye, look, n = 2) {
  app.setCameraMode('free');
  app.editorCam.setLookAt(eye, look);
  for (let i = 0; i < n; i++) { app.frame(1 / 60, idle, view); await flush(device); }
  await save(name);
  console.log('   ', await foamStats());
}
const carFrame = () => {
  const b = app.vehicle.body, p = b.position, f = b.dirToWorld([0, 0, 1]);
  const fwd = [f[0], 0, f[2]], l = Math.hypot(fwd[0], fwd[2]) || 1;
  fwd[0] /= l; fwd[2] /= l;
  return { p: p.slice(), fwd, left: [fwd[2], 0, -fwd[0]] };
};
const at = (c, back, side, up) => [c.p[0] - c.fwd[0] * back + c.left[0] * side, c.p[1] + up, c.p[2] - c.fwd[2] * back + c.left[2] * side];

app.warmup(+(process.env.WARMUP ?? 3));
await flush(device);
log('warm');

if (SHOTS.includes('ford')) {
  app.teleport('ford');
  await simulate(10);
  await simulate(Number(process.env.DRIVE ?? 80), { throttle: 0.75 });
  let c = carFrame();
  await shot('ford_close', at(c, 4.5, 3.2, 1.9), at(c, -0.5, 0, -0.6));
  await simulate(45, { throttle: 0.55 });
  c = carFrame();
  await shot('ford_trail', at(c, -3, 9, 7.5), at(c, 4, 1.5, -1.0));
}
if (SHOTS.includes('beach')) {
  app.teleport('beach');
  await simulate(Number(process.env.SURF ?? 150));
  const c = carFrame();
  await shot('beach', at(c, 6, 4, 2.4), at(c, -14, -2, -1.5));
}
if (SHOTS.includes('splash')) {
  // volumetric splashes: the car charging into the ford (with a run-up), then a cannonball crown
  const road = app.scene.road, ford = app.scene.fords[1];
  const st = road.pointAt(ford.t - Number(process.env.RUNUP ?? 0.085));
  app.vehicle.reset([st.point[0], app.scene.heightAt(st.point[0], st.point[1]) + 0.9, st.point[1]], Math.atan2(st.tangent[0], st.tangent[1]));
  await simulate(6);
  let n = 0;
  while (n < 240 && !app.vehicle.wheels.some((w) => w.depth > 0.12)) { await simulate(4, { throttle: 1 }); n += 4; }
  console.log(`    reached the water after ${n} frames at ${(app.vehicle.speed * 3.6).toFixed(0)} km/h`);
  await simulate(Number(process.env.CHARGE ?? 10), { throttle: 1 });
  console.log('    splash particles alive:', await app.splash.readAlive(), ' car', (app.vehicle.speed * 3.6).toFixed(0), 'km/h, in water', (app.vehicle.inWater * 100).toFixed(0) + '%');
  let c = carFrame();
  await shot('splash_car', at(c, -4.2, 3.6, 1.3), at(c, -0.6, 0.9, -0.3));
  await simulate(6, { throttle: 1 });
  c = carFrame();
  await shot('splash_car_side', at(c, 0.5, 5.5, 1.6), at(c, 0.3, 0.6, -0.2));
  // cannonball into the ford in front of the car, shot ~0.2 s after it hits the water
  await simulate(40);   // let the car's splash settle
  c = carFrame();
  const drop = [c.p[0] + c.fwd[0] * 6, 0, c.p[2] + c.fwd[2] * 6];
  const ws = app.surfaceAt(drop[0], drop[2]);
  const ball = app.props.spawn('ball', [drop[0], app.scene.heightAt(drop[0], drop[2]) + 9, drop[2]], { velocity: [0, -6, 0] });
  app.balls.push({ it: ball, t: 0 });
  let k = 0;
  while (k < 90 && !app.balls.find((b) => b.it === ball)?.splashed) { await simulate(1, idle, 1 / 60); k++; }
  await simulate(Number(process.env.BALL ?? 12), idle, 1 / 60);
  console.log(`    ball hit the water after ${k} frames; splash particles alive:`, await app.splash.readAlive());
  const eye = [drop[0] + c.left[0] * 6.5 - c.fwd[0] * 2.5, ws + 2.4, drop[2] + c.left[2] * 6.5 - c.fwd[2] * 2.5];
  await shot('splash_cannonball', eye, [drop[0], ws + 0.7, drop[2]]);
}
if (SHOTS.includes('rapids')) {
  // boulder rapids (river s = 0.30–0.42): from the bank just below them, looking upstream
  await simulate(Number(process.env.RAPIDS ?? 60));
  const river = app.scene.river;
  const a = river.pointAt(0.36), b = river.pointAt(0.45);
  const wy = (p) => app.surfaceAt(p[0], p[1]);
  const side = [-b.tangent[1], b.tangent[0]];
  const eye = [b.point[0] + side[0] * 7, wy(b.point) + 4.5, b.point[1] + side[1] * 7];
  await shot('rapids', eye, [a.point[0], wy(a.point) - 0.5, a.point[1]]);
}
console.log('errors:', device.errors.length ? device.errors.slice(0, 3) : 'none');
process.exit(device.errors.length ? 1 : 0);
