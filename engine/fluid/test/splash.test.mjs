// GPU tests for the volumetric splash liquid (SplashFluid, PBF): mass exchange with the grid,
// stability / incompressibility, collision with primitives, foam left on landing.
//   node test/launch.mjs test/splash.test.mjs
import { getDevice, flush } from './gpu.mjs';
import { FluidSim } from '../src/FluidSim.js';
import { SplashFluid } from '../src/SplashFluid.js';

const { device } = await getDevice();
let failures = 0;
function check(name, ok, detail) {
  if (!ok) failures++;
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  — ' + detail : ''}`);
}

function basin(nx, ny, dx, depth, { particles = false } = {}) {
  const sim = new FluidSim(device, { nx, ny, cellSize: dx, origin: [0, 0], maxParticles: 16384 });
  sim.particles.params.enabled = particles;
  sim.params.infiltration = 0;
  sim.setBed(new Float32Array(nx * ny));
  sim.setBoundaries({ west: 'wall', east: 'wall', south: 'wall', north: 'wall' });
  sim.setWater(new Float32Array(nx * ny).fill(depth));
  return sim;
}
async function frames(sim, splash, n, perFrame = null, dt = 1 / 60) {
  for (let f = 0; f < n; f++) {
    if (perFrame) perFrame(f);
    const enc = device.createCommandEncoder();
    sim.step(enc, dt);
    splash.step(enc, dt);
    device.queue.submit([enc.finish()]);
    sim.afterSubmit();
    if (f % 10 === 9) await flush(device);
  }
  await flush(device);
}
async function read(buf, bytes) {
  const rb = device.createBuffer({ size: bytes, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const enc = device.createCommandEncoder();
  enc.copyBufferToBuffer(buf, 0, rb, 0, bytes);
  device.queue.submit([enc.finish()]);
  await rb.mapAsync(GPUMapMode.READ);
  const out = new Float32Array(rb.getMappedRange().slice(0));
  rb.unmap(); rb.destroy();
  return out;
}
const volume = (sim, hc) => { let v = 0; for (let k = 0; k < sim.nx * sim.ny; k++) v += hc[k * 2]; return v * sim.dx * sim.dx; };
async function particles(splash) {
  const N = splash.max;
  const pos = await read(splash.pos, N * 16), vel = await read(splash.vel, N * 16), lam = await read(splash.lambda, N * 8);
  const alive = [];
  for (let i = 0; i < N; i++) if (pos[i * 4 + 3] > 0) alive.push(i);
  return { pos, vel, lam, alive };
}

{ // 1. water thrown up as a splash comes back to the grid: exact volume, all particles land
  const nx = 64, ny = 64, dx = 0.25;
  const sim = basin(nx, ny, dx, 0.5);
  const splash = new SplashFluid(sim, { maxParticles: 16384 });
  const v0 = volume(sim, (await sim.readState()).hc);
  await frames(sim, splash, 12, (f) => splash.emit({ position: [8, 0.5, 8], velocity: [1.5, 3.2, 0.5], spread: 0.8, radius: 0.35, volume: 0.006 }));
  await frames(sim, splash, 1);   // the last withdrawal is applied by the next solver step (nothing has landed yet)
  const mid = await particles(splash);
  const thrown = mid.alive.length * splash.params.vol;
  const vMid = volume(sim, (await sim.readState()).hc);
  check('splash: water leaves the grid when thrown', thrown > 0.05 && Math.abs(v0 - vMid - thrown) < 0.02 * thrown,
    `${mid.alive.length} particles = ${(thrown * 1000).toFixed(1)} l airborne, grid lost ${((v0 - vMid) * 1000).toFixed(1)} l`);
  await frames(sim, splash, 150);
  const end = await particles(splash);
  const v1 = volume(sim, (await sim.readState()).hc);
  check('splash: every parcel lands back in the water', end.alive.length === 0, `${end.alive.length} still airborne after 2.7 s`);
  check('splash: volume conserved exactly', Math.abs(v1 - v0) / v0 < 2e-5, `rel err ${((v1 - v0) / v0).toExponential(2)}`);
  check('splash: no GPU validation errors', device.errors.length === 0, device.errors[0]);
}

{ // 2. a dense stream stays a liquid: incompressible (density near 1), no blow-up, no NaN
  const nx = 64, ny = 64, dx = 0.25;
  const sim = basin(nx, ny, dx, 0.6);
  const splash = new SplashFluid(sim, { maxParticles: 16384 });
  let maxRho = 0, nan = 0, vmax = 0, meanRho = 0;
  await frames(sim, splash, 24, (f) => splash.emit({ position: [8, 0.6, 8], velocity: [0, 4.5, 0], spread: 0.15, radius: 0.12, rate: 0.35 }));
  const p = await particles(splash);
  let n = 0;
  for (const i of p.alive) {
    const rho = p.lam[i * 2 + 1];
    for (let c = 0; c < 3; c++) if (!Number.isFinite(p.pos[i * 4 + c])) nan++;
    vmax = Math.max(vmax, Math.hypot(p.vel[i * 4], p.vel[i * 4 + 1], p.vel[i * 4 + 2]));
    if (rho > 0) { maxRho = Math.max(maxRho, rho); meanRho += rho; n++; }
  }
  meanRho /= Math.max(n, 1);
  check('splash: liquid column stays finite', nan === 0 && p.alive.length > 500, `${p.alive.length} particles, NaN coords ${nan}`);
  check('splash: incompressible (PBF keeps density near rest)', maxRho < 1.6 && meanRho > 0.25 && meanRho < 1.15, `mean ρ/ρ0 ${meanRho.toFixed(2)}, max ${maxRho.toFixed(2)}`);
  check('splash: no blow-up', vmax < 12, `max speed ${vmax.toFixed(2)} m/s (thrown at 4.5)`);
  // the column is a coherent stream, not a cloud: most particles have neighbours
  let lonely = 0;
  for (const i of p.alive) if (p.lam[i * 2 + 1] < 0.12) lonely++;
  check('splash: holds together (cohesion)', lonely < 0.3 * p.alive.length, `${(100 * lonely / p.alive.length).toFixed(0)}% isolated droplets`);
}

{ // 3. it splashes off solid bodies: a box held in the stream is never penetrated
  const nx = 64, ny = 64, dx = 0.25;
  const sim = basin(nx, ny, dx, 0.6);
  const splash = new SplashFluid(sim, { maxParticles: 16384 });
  const box = { type: 'box', position: [8, 1.6, 8], rotation: [0, 0, 0, 1], halfExtents: [0.6, 0.15, 0.6] };
  sim.setPrimitives([box]);
  await frames(sim, splash, 30, () => splash.emit({ position: [8, 0.6, 8], velocity: [0, 5.5, 0], spread: 0.2, radius: 0.2, rate: 0.3 }));
  const p = await particles(splash);
  let inside = 0, above = 0;
  for (const i of p.alive) {
    const x = p.pos[i * 4] - 8, y = p.pos[i * 4 + 1] - 1.6, z = p.pos[i * 4 + 2] - 8;
    if (Math.abs(x) < 0.6 && Math.abs(y) < 0.15 && Math.abs(z) < 0.6) inside++;
    if (y > 0.15 && Math.abs(x) < 0.5 && Math.abs(z) < 0.5) above++;
  }
  check('splash: never inside a solid', inside === 0, `${inside} of ${p.alive.length} inside the box`);
  check('splash: deflected by the body', above < 0.02 * p.alive.length, `${above} got through to the top`);
}

{ // 4. splash landing in water leaves foam rafts
  const nx = 64, ny = 64, dx = 0.25;
  const sim = basin(nx, ny, dx, 0.5, { particles: true });
  sim.particles.params.emitScale = 0;   // only foam from the splash
  const splash = new SplashFluid(sim, { maxParticles: 16384 });
  await frames(sim, splash, 10, () => splash.emit({ position: [8, 0.5, 8], velocity: [0, 3, 0], spread: 1.0, radius: 0.4, volume: 0.008 }));
  await frames(sim, splash, 70);
  sim.requestStats();
  await frames(sim, splash, 2);
  await new Promise((r) => setTimeout(r, 50));
  check('splash: impacts leave foam on the water', (sim.stats.particles ?? 0) > 20, `${sim.stats.particles} foam rafts`);
  check('splash: no GPU validation errors (all tests)', device.errors.length === 0, device.errors[0]);
}

console.log(failures ? `\n${failures} splash check(s) FAILED` : '\nall splash checks passed');
process.exit(failures ? 1 : 0);
