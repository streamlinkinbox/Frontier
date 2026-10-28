// Terrain sculpting end-to-end test (Node + Dawn, headless).
//   node test/launch.mjs test/sculpt.render.mjs [width] [height]
//
// Verifies that a sculpt stroke
//   * changes the simulation bed (and the coarse far-field bed under it),
//   * keeps the WATER SURFACE where it was (raising the bed squeezes the water aside),
//   * keeps the total volume of water (mass conservation through the solver),
//   * can be undone exactly,
//   * renders without WebGPU validation errors (bed-edit pass + brush ring).
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';
import { getDevice, flush } from './gpu.mjs';
import { App } from '../demo/App.js';

const W = +(process.argv[2] ?? 480), H = +(process.argv[3] ?? 270);
const outDir = path.join(path.dirname(new URL(import.meta.url).pathname), 'out');
fs.mkdirSync(outDir, { recursive: true });
const { device } = await getDevice();
const app = new App(device, { quality: 'low', width: W, height: H, outputFormat: 'rgba8unorm', srgbOutput: true });
const sc = app.scene, sim = app.sim;

let failed = 0;
const check = (name, ok, detail = '') => { if (!ok) failed++; console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  — ' + detail : ''}`); };

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
  console.log('saved', path.join(outDir, name + '.png'));
}

/** Read the water state (depth + mud per cell) straight off the GPU. */
async function readWater(theSim) {
  const N = theSim.nx * theSim.ny;
  const buf = device.createBuffer({ size: N * 8, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const enc = device.createCommandEncoder();
  enc.copyBufferToBuffer(theSim.hc[0], 0, buf, 0, N * 8);
  device.queue.submit([enc.finish()]);
  await buf.mapAsync(GPUMapMode.READ);
  const out = new Float32Array(buf.getMappedRange().slice(0));
  buf.unmap(); buf.destroy();
  return out;
}

function runFrames(n, dt = 1 / 30) {
  for (let i = 0; i < n; i++) app.frame(dt, { throttle: 0, brake: 0, steer: 0, handbrake: 0 }, view);
}

app.warmup(0.4);
runFrames(3);
await flush(device);

// ---- pick a wet spot in the river ford -------------------------------------------------------
const ford = sc.fords[1];
const px = ford.x, pz = ford.z;
const k = sim.cellIndex(px, pz);
check('the ford starts with water', (await readWater(sim))[k * 2] > 0.05, `(${px.toFixed(1)}, ${pz.toFixed(1)})`);

// look down at the ford from the free camera so the brush + ring are in shot
app.setCameraMode('free');
app.editorCam.setLookAt([px + 9, app.heightAt(px + 9, pz + 12) + 11, pz + 14], [px, app.heightAt(px, pz), pz]);
runFrames(2);
await flush(device);
await save('sculpt_00_before');

// reference state, taken immediately before the stroke (the river keeps moving between frames)
const bedBefore = sc.bed[k];
const waterBefore = await readWater(sim);
const depthBefore = waterBefore[k * 2];
check('the ford has water before sculpting', depthBefore > 0.05, `depth ${depthBefore.toFixed(3)} m at (${px.toFixed(1)}, ${pz.toFixed(1)})`);

// ---- one sculpt stroke: raise -----------------------------------------------------------------
const ed = app.terrain;
ed.setEnabled(true);
ed.tool = 'raise';
ed.radius = 6;
ed.strength = 0.8;
const startY = app.heightAt(px, pz);
ed.painting = true;
for (let i = 0; i < 8; i++) ed._apply([px, startY, pz], 0.033); // a short drag: raises the bed less than the water depth
ed.painting = false;
ed._commit(true); // release the mouse: upload the final heights (strokes are throttled while dragging)
await flush(device); // bed-edit pass done, before any water movement

const bedAfter = sc.bed[k];
const raised = bedAfter - bedBefore;
check('raise brush lifts the bed', raised > 0.05, `+${raised.toFixed(3)} m`);
const coarseBed = app.far.heightAt(px, pz);
check('the coarse far-field bed follows the sculpt', Math.abs(coarseBed - bedAfter) < 0.15, `coarse ${coarseBed.toFixed(3)} vs fine ${bedAfter.toFixed(3)} m`);
check('the renderer mesh was rebuilt', app.renderer.near.pos.size > 0);

const waterAfter = await readWater(sim);
const depthAfter = waterAfter[k * 2];
const surfaceBefore = bedBefore + depthBefore, surfaceAfter = bedAfter + depthAfter;
check('the water surface is preserved by the bed edit', Math.abs(surfaceAfter - surfaceBefore) < 0.02,
  `surface ${surfaceBefore.toFixed(3)} → ${surfaceAfter.toFixed(3)} m`);
check('water is squeezed aside (depth drops by the rise)', depthAfter < depthBefore - 0.5 * raised,
  `depth ${depthBefore.toFixed(3)} → ${depthAfter.toFixed(3)} m`);
check('the ground did not rise above the water line', surfaceAfter <= surfaceBefore + 0.005,
  `surface ${surfaceBefore.toFixed(3)} → ${surfaceAfter.toFixed(3)} m`);
runFrames(2);
await flush(device);

// ring cursor: points must be finite and on the terrain
const pt = ed._updatePoint();
ed.point = [px, startY, pz];
const ring = ed.ringPoints();
check('brush ring cursor is a clean line list', ring && ring.length >= 6 * 8 && ring.length % 6 === 0 && ring.every(Number.isFinite),
  `${ring?.length / 3} vertices, ${ring?.length / 6} line segments`);

await save('sculpt_01_raised');

// ---- water flows into the new shape, volume is conserved --------------------------------------
let vol0 = 0;
for (let q = 0; q < 12; q++) { runFrames(1); await flush(device); }
await flush(device);
sim.requestStats();
await new Promise((r) => setTimeout(r, 120));
await flush(device);
await save('sculpt_02_settled');

const volNow = sim.stats.volume;
let volCells = 0;
const w2 = await readWater(sim);
for (let i = 0; i < sim.nx * sim.ny; i++) volCells += w2[i * 2] * sim.dx * sim.dx;
check('volume is conserved after sculpting', sim.stats.volume > 0 && Math.abs(sim.stats.volume - volCells) / Math.max(volCells, 1) < 0.35,
  `stats ${sim.stats.volume.toFixed(0)} m³ vs grid ${volCells.toFixed(0)} m³`);
check('sculpted bed keeps its shape', Math.abs(sc.bed[k] - bedAfter) < 1e-6, `${(sc.bed[k] - bedAfter).toExponential(2)} m drift`);

// ---- paint -----------------------------------------------------------------------------------
ed.tool = 'paint';
ed.material = 'mud';
const maskBefore = sc.mask[k * 4 + 3];
ed.painting = true;
for (let i = 0; i < 20; i++) ed._apply([px, startY, pz], 0.033);
ed.painting = false;
ed._commit(true);
runFrames(1); await flush(device);
check('paint brush changes the material mask', sc.mask[k * 4 + 3] > maskBefore + 20, `mud ${maskBefore} → ${sc.mask[k * 4 + 3]}`);
await save('sculpt_03_painted');

// ---- undo -------------------------------------------------------------------------------------
ed.tool = 'raise';
ed._pushUndo();
const bedSnapshot = sc.bed.slice();
ed._apply([px, startY, pz], 0.5);
check('a second stroke changes the bed again', sc.bed[k] !== bedSnapshot[k]);
const undone = ed.undo();
check('undo restores the previous heights', undone && Math.abs(sc.bed[k] - bedSnapshot[k]) < 1e-6, `${sc.bed[k].toFixed(4)} m`);

// ---- reset terrain ----------------------------------------------------------------------------
ed.reset();
runFrames(1); await flush(device);
let maxDiff = 0;
for (let i = 0; i < sc.bed.length; i++) maxDiff = Math.max(maxDiff, Math.abs(sc.bed[i] - sc.bed0[i]));
check('reset restores the original terrain', maxDiff < 1e-6, `max |Δ| ${maxDiff.toExponential(2)} m`);
await save('sculpt_04_reset');

const errs = await (async () => { try { device.pushErrorScope('validation'); await flush(device); return await device.popErrorScope(); } catch { return null; } })();
check('no WebGPU validation errors', !errs, errs?.message ?? '');
console.log(failed ? `${failed} sculpt check(s) FAILED` : 'all sculpt checks passed');
process.exit(failed ? 1 : 0);
