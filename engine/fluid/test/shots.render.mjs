// Render a few named views of the demo (Node + Dawn, headless) for visual checks.
//   node test/launch.mjs test/shots.render.mjs [width] [height]   env: SHOTS=bay,coast,ford,rapids,aerial TAG=name
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';
import { getDevice, flush } from './gpu.mjs';
import { App } from '../demo/App.js';

const W = +(process.argv[2] ?? 640), H = +(process.argv[3] ?? 360);
const TAG = process.env.TAG ?? 'water';
const outDir = path.join(path.dirname(new URL(import.meta.url).pathname), 'out');
fs.mkdirSync(outDir, { recursive: true });
const { device } = await getDevice();
const app = new App(device, { quality: process.env.Q ?? 'low', width: W, height: H, outputFormat: 'rgba8unorm', srgbOutput: true });
console.log(`scene ${app.buildMs.toFixed(0)} ms · fine ${app.sim.nx}²@${app.sim.dx.toFixed(2)}m · coarse ${app.coarse.nx}²@${app.coarse.dx.toFixed(2)}m · lod ${app.fluidR.lodN}²`);
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
  const file = path.join(outDir, `water_${TAG}_${name}.png`);
  fs.writeFileSync(file, PNG.sync.write(png));
  console.log('wrote', file);
}
async function frame(dt = 1 / 30) {
  app.frame(dt, { throttle: 0, brake: 0, steer: 0, handbrake: 0 }, view);
  await flush(device);
}
function look(eye, at) {
  app.setCameraMode('free');
  app.editorCam.setLookAt(eye, at);
  app.cam.pos = eye; app.cam.target = at;
}

app.warmup(+(process.env.WARMUP ?? 2));
await flush(device);
const shots = (process.env.SHOTS ?? 'bay,coast,ford,rapids,aerial').split(',');
for (const shot of shots) {
  if (shot === 'bay') look([22, 16, -30], [0, 0, -120]);
  else if (shot === 'coast') look([0, 28, -8], [0, 0, -260]);
  else if (shot === 'ford') { app.teleport('ford'); look([-10, 14, 18], [-15, 0, 0]); }
  else if (shot === 'rapids') { app.teleport('rapids'); look([-23, 12, 20], [-25, 1, 4]); }
  else if (shot === 'aerial') look([-90, 90, -20], [0, 0, -80]);
  else if (shot === 'car') { app.teleport('beach'); look([10, 6, -25], [0, 0, -30]); }
  await frame(); await frame();
  await save(shot);
}
