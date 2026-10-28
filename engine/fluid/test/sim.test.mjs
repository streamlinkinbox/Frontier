// Numerical tests for the GPU shallow-water solver (run via: npm test).
// Each test builds a small domain, runs the real WGSL kernels and checks physical invariants.
import { getDevice, flush } from './gpu.mjs';
import { FluidSim } from '../src/FluidSim.js';

const { device } = await getDevice();
let failures = 0;
const results = [];
function check(name, ok, detail) {
  results.push({ name, ok, detail });
  if (!ok) failures++;
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  — ' + detail : ''}`);
}

async function run(sim, frames, dt = 1 / 60, perFrame = null) {
  for (let f = 0; f < frames; f++) {
    if (perFrame) perFrame(f);
    const enc = device.createCommandEncoder();
    sim.step(enc, dt);
    device.queue.submit([enc.finish()]);
    sim.afterSubmit();
    if (f % 20 === 19) await flush(device);
  }
  await flush(device);
}

function makeSim(nx, ny, dx, opts = {}) {
  const sim = new FluidSim(device, { nx, ny, cellSize: dx, origin: [0, 0], maxParticles: opts.maxParticles ?? 4096, ...opts });
  sim.particles.params.enabled = opts.particles ?? false;
  sim.params.infiltration = 0;
  return sim;
}

function volume(sim, hc) {
  let v = 0;
  for (let k = 0; k < sim.nx * sim.ny; k++) v += hc[k * 2];
  return v * sim.dx * sim.dx;
}
function stats(hc) {
  let mn = Infinity, mx = -Infinity, nan = 0;
  for (let k = 0; k < hc.length; k += 2) { const h = hc[k]; if (!Number.isFinite(h)) nan++; mn = Math.min(mn, h); mx = Math.max(mx, h); }
  return { mn, mx, nan };
}
function maxAbs(a) { let m = 0; for (const x of a) m = Math.max(m, Math.abs(x)); return m; }

// -------------------------------------------------------------------------------------------
{ // 1. Dam break onto a dry bed: mass conservation, positivity, front speed vs Ritter (2*sqrt(g h0))
  const nx = 128, ny = 16, dx = 0.25;
  const sim = makeSim(nx, ny, dx);
  sim.params.manning = 0; sim.params.viscosity = 0;
  sim.setBed(new Float32Array(nx * ny));
  const depth = new Float32Array(nx * ny);
  const x0 = 8;
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) depth[j * nx + i] = (i + 0.5) * dx < x0 ? 1.0 : 0.0;
  sim.setWater(depth);
  const v0 = volume(sim, sim._initialDepth);
  await run(sim, 60);
  const r1 = await sim.readState();
  // Ritter (1892) analytic solution: h = (2c0 - x/t)^2 / 9g,  u = 2/3 (x/t + c0)
  const t = sim.time, c0 = Math.sqrt(9.81 * 1.0);
  let err = 0, cnt = 0, front = 0;
  for (let i = 0; i < nx; i++) {
    const x = (i + 0.5) * dx, xi = (x - x0) / t;
    const hs = r1.hc[((ny >> 1) * nx + i) * 2];
    if (hs > 0.005) front = (i + 1) * dx - x0;
    if (xi > -c0 && xi < 2 * c0) { err += Math.abs(hs - (2 * c0 - xi) ** 2 / (9 * 9.81)); cnt++; }
  }
  const frontA = (2 * c0 - Math.sqrt(9 * 9.81 * 0.005)) * t;
  check('dam break: depth profile matches Ritter', err / cnt < 0.02, `mean |h - h_ritter| ${(err / cnt).toFixed(4)} m`);
  check('dam break: wet front speed vs Ritter', front > 0.85 * frontA, `front ${front.toFixed(2)} m vs analytic ${frontA.toFixed(2)} m (5 mm threshold) @ t=${t.toFixed(2)} s`);
  await run(sim, 240);
  const { hc, vel } = await sim.readState();
  const v1 = volume(sim, hc);
  const s = stats(hc);
  const rel = Math.abs(v1 - v0) / v0;
  check('dam break: volume conserved', rel < 2e-5, `rel err ${rel.toExponential(2)}`);
  check('dam break: no negative depth / NaN', s.mn >= 0 && s.nan === 0, `min ${s.mn.toExponential(2)}, nan ${s.nan}`);
  check('dam break: velocities finite', Number.isFinite(maxAbs(vel)), `max |u| ${maxAbs(vel).toFixed(2)} m/s`);
  check('dam break: no GPU validation errors', device.errors.length === 0, device.errors[0]);
}

// -------------------------------------------------------------------------------------------
{ // 2. Lake at rest over rough terrain with islands (well-balanced wet/dry)
  const nx = 64, ny = 64, dx = 0.5;
  const sim = makeSim(nx, ny, dx);
  const bed = new Float32Array(nx * ny);
  const depth = new Float32Array(nx * ny);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
    const b = 0.35 * Math.sin(i * 0.37) * Math.cos(j * 0.29) + 0.25 * Math.sin(i * 0.11 + j * 0.17) + (Math.hypot(i - 32, j - 32) < 6 ? 0.9 : 0);
    bed[j * nx + i] = b;
    depth[j * nx + i] = Math.max(0, 0.4 - b);
  }
  sim.setBed(bed);
  sim.setWater(depth);
  const v0 = volume(sim, sim._initialDepth);
  await run(sim, 300);
  const { hc, vel } = await sim.readState();
  const mv = maxAbs(vel);
  let maxEtaErr = 0;
  for (let k = 0; k < nx * ny; k++) if (hc[k * 2] > 0.01) maxEtaErr = Math.max(maxEtaErr, Math.abs(bed[k] + hc[k * 2] - 0.4));
  check('lake at rest: stays still', mv < 2e-3, `max |u| ${mv.toExponential(2)} m/s`);
  check('lake at rest: level preserved', maxEtaErr < 2e-3, `max |eta-0.4| ${maxEtaErr.toExponential(2)} m`);
  check('lake at rest: volume conserved', Math.abs(volume(sim, hc) - v0) / v0 < 1e-5);
}

// -------------------------------------------------------------------------------------------
{ // 3. Gravity wave speed c = sqrt(g h)
  const nx = 256, ny = 8, dx = 0.25, h0 = 1.0;
  const sim = makeSim(nx, ny, dx);
  sim.params.viscosity = 0; sim.params.manning = 0;
  sim.setBed(new Float32Array(nx * ny));
  const depth = new Float32Array(nx * ny);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) { const x = i * dx; depth[j * nx + i] = h0 + 0.02 * Math.exp(-((x - 10) ** 2) / 0.8); }
  sim.setWater(depth);
  const T = 4.0;
  await run(sim, Math.round(T * 60));
  const { hc } = await sim.readState();
  // locate the right-going crest
  let best = 0, bi = 0;
  for (let i = Math.round(14 / dx); i < nx; i++) { const h = hc[(ny >> 1) * nx * 2 + i * 2]; if (h > best) { best = h; bi = i; } }
  const c = ((bi + 0.5) * dx - 10) / T;
  const c0 = Math.sqrt(9.81 * h0);
  check('wave speed ~ sqrt(gh)', Math.abs(c - c0) / c0 < 0.08, `measured ${c.toFixed(2)} m/s vs ${c0.toFixed(2)} m/s`);
}

// -------------------------------------------------------------------------------------------
{ // 4. Run-up on a beach with walls: positivity + conservation with moving shoreline
  const nx = 160, ny = 16, dx = 0.25;
  const sim = makeSim(nx, ny, dx);
  const bed = new Float32Array(nx * ny), depth = new Float32Array(nx * ny);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
    const x = i * dx; const b = -1.0 + Math.max(0, x - 15) * 0.12;
    bed[j * nx + i] = b;
    const eta = 0.35 * Math.exp(-((x - 6) ** 2) / 4);
    depth[j * nx + i] = Math.max(0, eta - b);
  }
  sim.setBed(bed); sim.setWater(depth);
  const v0 = volume(sim, sim._initialDepth);
  await run(sim, 400);
  const { hc } = await sim.readState();
  const s = stats(hc);
  const rel = Math.abs(volume(sim, hc) - v0) / v0;
  check('run-up: volume conserved', rel < 2e-5, `rel err ${rel.toExponential(2)}`);
  check('run-up: no negative depth / NaN', s.mn >= 0 && s.nan === 0);
}

// -------------------------------------------------------------------------------------------
{ // 5. Mud (Bingham plastic): gentle mound holds, steep mound slumps to the yield-stress equilibrium
  const nx = 64, ny = 64, dx = 0.1;
  const mk = async (mudC, h0, slope) => {
    const sim = makeSim(nx, ny, dx);
    sim.setBed(new Float32Array(nx * ny));
    const depth = new Float32Array(nx * ny), mud = new Float32Array(nx * ny);
    for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
      const r = Math.hypot(i + 0.5 - 32, j + 0.5 - 32) * dx; depth[j * nx + i] = Math.max(0, h0 - r * slope); mud[j * nx + i] = mudC;
    }
    sim.setWater(depth, mud);
    const v0 = volume(sim, sim._initialDepth);
    let mx0 = 0; for (let k = 0; k < nx * ny; k++) mx0 = Math.max(mx0, depth[k]);
    await run(sim, 300);
    const { hc } = await sim.readState();
    return { mx: stats(hc).mx, mx0, rel: Math.abs(volume(sim, hc) - v0) / v0, v0 };
  };
  const water = await mk(0, 0.25, 0.35);
  const gentle = await mk(1, 0.10, 0.12);
  const steep = await mk(1, 0.25, 0.35);
  // Bingham equilibrium of an axisymmetric mound: h dh/dr = tau_y/(rho g)  ->  h^2 = 2k(R - r)
  const P = { A: 0.0181, B: 25.7, cv: 0.39 };
  const tauY = P.A * (Math.exp(P.B * P.cv) - 1), rho = 1000 + 1650 * P.cv, kk = tauY / (rho * 9.81);
  const R = Math.pow(steep.v0 / (2 * Math.PI * Math.sqrt(2 * kk) * (4 / 15)), 0.4);
  const hEq = Math.sqrt(2 * kk * R);
  check('mud: water mound collapses', water.mx < 0.1, `max depth ${water.mx.toFixed(3)} m (from 0.25)`);
  check('mud: gentle mud mound holds (below yield)', gentle.mx > 0.98 * gentle.mx0, `max depth ${gentle.mx.toFixed(4)} m (initial ${gentle.mx0.toFixed(4)} m)`);
  check('mud: steep mound slumps to Bingham equilibrium', Math.abs(steep.mx - hEq) / hEq < 0.2, `max depth ${steep.mx.toFixed(3)} m, analytic ${hEq.toFixed(3)} m`);
  check('mud: volume conserved', steep.rel < 2e-5 && water.rel < 2e-5 && gentle.rel < 2e-5);
}

// -------------------------------------------------------------------------------------------
{ // 6. Floating box: lowered slowly into a basin. Water under the hull is pushed down to the hull
  //    bottom, and the pressure head the solver reports there (used for buoyancy) equals the
  //    surrounding free-surface level, i.e. Archimedes' principle holds in the coupled system.
  const nx = 64, ny = 64, dx = 0.25;
  const sim = makeSim(nx, ny, dx);
  sim.params.manning = 0.06;
  sim.setBed(new Float32Array(nx * ny).fill(-1));
  sim.setWater(new Float32Array(nx * ny).fill(1.0));
  const v0 = volume(sim, sim._initialDepth);
  const box = (y) => ({ type: 'box', position: [8, y, 8], rotation: [0, 0, 0, 1], halfExtents: [1.5, 0.5, 1.0] });
  await run(sim, 600, 1 / 60, (f) => sim.setPrimitives([box(0.8 - 0.6 * Math.min(1, f / 240))])); // bottom ends at -0.30
  sim.setQueryPoints(new Float32Array([8, 8, 8 + 2.25, 8]), 2, 'q');
  await run(sim, 1);
  for (let t = 0; t < 20 && sim.getQueryResults().count !== 2; t++) await new Promise((r) => setTimeout(r, 20));
  const q = sim.getQueryResults();
  const { hc } = await sim.readState();   // same frame as the query
  const kC = 32 * nx + 32;
  const surfUnder = -1 + hc[kC * 2];
  const rel = Math.abs(volume(sim, hc) - v0) / v0;
  let ring = 0, rn = 0;
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
    const x = (i + 0.5) * dx, z = (j + 0.5) * dx;
    const d = Math.max(Math.abs(x - 8) - 1.5, Math.abs(z - 8) - 1.0);
    if (d > 0.3 && d < 1.0) { ring += -1 + hc[(j * nx + i) * 2]; rn++; }
  }
  ring /= rn;
  check('box: volume conserved', rel < 2e-5, `rel err ${rel.toExponential(2)}`);
  check('box: water pushed down to hull bottom', surfUnder < -0.24, `surface under hull ${surfUnder.toFixed(3)} m (hull bottom -0.30)`);
  if (q.count === 2) {
    check('box: head under hull = surrounding level (buoyancy)', Math.abs(q.data[0] - ring) < 0.03, `head ${q.data[0].toFixed(3)} m vs surrounding ${ring.toFixed(3)} m`);
    check('box: query depth in open water', Math.abs(q.data[9] - (ring + 1)) < 0.03, `depth ${q.data[9].toFixed(3)} m`);
  } else check('box: query results arrived', false, `count ${q.count}`);
}

// -------------------------------------------------------------------------------------------
{ // 7. Moving wheel through a puddle: water is pushed away and comes back, mass conserved
  const nx = 96, ny = 48, dx = 0.1;
  const sim = makeSim(nx, ny, dx);
  sim.setBed(new Float32Array(nx * ny));
  sim.setWater(new Float32Array(nx * ny).fill(0.06));
  const v0 = volume(sim, sim._initialDepth);
  let minUnder = 1;
  await run(sim, 120, 1 / 60, (f) => {
    const x = 1 + f * (7 / 120);
    sim.setPrimitives([{ type: 'cylinder', position: [x, 0.3, 2.4], rotation: [0, 0, 0, 1], radius: 0.32, halfLength: 0.12, velocity: [3.5, 0, 0], angularVelocity: [0, 0, -3.5 / 0.32] }]);
  });
  const { hc } = await sim.readState();
  const s = stats(hc);
  const rel = Math.abs(volume(sim, hc) - v0) / v0;
  const iw = Math.floor(8 / dx), jw = 24;
  minUnder = hc[(jw * nx + iw - 1) * 2];
  check('wheel: volume conserved', rel < 2e-5, `rel err ${rel.toExponential(2)}`);
  check('wheel: no negative depth / NaN', s.mn >= 0 && s.nan === 0);
  check('wheel: bow wave formed (max depth rose)', s.mx > 0.075, `max depth ${s.mx.toFixed(3)} m (rest 0.06)`);
  check('wheel: water expelled from tyre footprint', minUnder < 0.03, `depth under tyre ${minUnder.toFixed(4)} m`);
}

// -------------------------------------------------------------------------------------------
{ // 8. Source adds exactly Q*t of water
  const nx = 48, ny = 48, dx = 0.5;
  const sim = makeSim(nx, ny, dx);
  const bed = new Float32Array(nx * ny);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) bed[j * nx + i] = 0.01 * Math.hypot(i - 24, j - 24);
  sim.setBed(bed); sim.setWater(new Float32Array(nx * ny));
  sim.addSource({ x: 12, z: 12, radius: 2, flow: 0.5, direction: [1, 0], speed: 0.5 });
  const T = 4;
  await run(sim, T * 60);
  const { hc } = await sim.readState();
  const v = volume(sim, hc);
  const simT = sim.time;
  check('source: volume = Q * t', Math.abs(v - 0.5 * simT) / (0.5 * simT) < 0.01, `${v.toFixed(3)} m^3 vs ${(0.5 * simT).toFixed(3)} m^3`);
}

// -------------------------------------------------------------------------------------------
{ // 9. Particles: breaking flow emits spray; spray carries real volume that returns on landing
  const nx = 96, ny = 48, dx = 0.2;
  const sim = makeSim(nx, ny, dx, { particles: true, maxParticles: 65536 });
  sim.setBed(new Float32Array(nx * ny));
  const depth = new Float32Array(nx * ny);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) depth[j * nx + i] = i < nx * 0.4 ? 1.2 : 0.1;
  sim.setWater(depth);
  const v0 = volume(sim, sim._initialDepth);
  let peak = 0;
  for (let f = 0; f < 150; f++) {
    sim.requestStats();
    await run(sim, 1);
    await new Promise((r) => setTimeout(r, 1));
    peak = Math.max(peak, sim.stats.particles || 0);
  }
  check('particles: breaking bore emits particles', peak > 200, `peak alive ${peak}`);
  // let everything settle; the spray returns its volume where it lands
  sim.particles.params.enabled = false;
  await run(sim, 400);
  const { hc } = await sim.readState();
  const rel = Math.abs(volume(sim, hc) - v0) / v0;
  check('particles: water volume returns after spray lands', rel < 5e-4, `rel err ${rel.toExponential(2)}`);
  check('particles: no GPU validation errors', device.errors.length === 0, device.errors[0]);
}

// -------------------------------------------------------------------------------------------
{ // 10. Ocean wave-maker + sponge: waves enter, shoal on a slope, no blow-up
  const nx = 128, ny = 64, dx = 0.5;
  const sim = makeSim(nx, ny, dx);
  const bed = new Float32Array(nx * ny);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) bed[j * nx + i] = -5 + Math.max(0, j * dx - 8) * 0.25;
  sim.setBed(bed);
  const depth = new Float32Array(nx * ny);
  for (let k = 0; k < nx * ny; k++) depth[k] = Math.max(0, -bed[k]);
  sim.setWater(depth);
  sim.setBoundaries({ south: 'ocean', west: 'open', east: 'open', north: 'wall' });
  sim.setWaves([{ amplitude: 0.4, period: 5, direction: Math.PI / 2 }]);
  sim.params.waveRampTime = 2;
  let maxAmp = 0;
  await run(sim, 900, 1 / 60, null);
  const { hc, vel } = await sim.readState();
  const s = stats(hc);
  for (let j = 10; j < 30; j++) maxAmp = Math.max(maxAmp, Math.abs(bed[j * nx + 64] + hc[(j * nx + 64) * 2]));
  check('ocean: stable after 15 s', s.nan === 0 && s.mn >= 0 && maxAbs(vel) < 20, `max |u| ${maxAbs(vel).toFixed(2)}`);
  check('ocean: waves present in domain', maxAmp > 0.1, `max |eta| in nearshore ${maxAmp.toFixed(3)} m`);
}

console.log(`\n${results.length - failures}/${results.length} checks passed`);
process.exit(failures ? 1 : 0);
