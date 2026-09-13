// ============================================================================
// Frontier SDF Terrain — Volumetric erosion laboratory.
//
// *** There is no heightmap in this file. *** Every operator reads and writes
// the 3D SDF + attribute volumes directly:
//
//  Hydraulic : ballistic rain particles -> kinetic-impact excavation (oriented
//              crater stamps) -> sediment pickup -> 3D surface flow with
//              transport capacity -> deposition / re-entrainment (rills,
//              gullies, alluvial fans, waterfalls off cliffs). Mass-conserved:
//              excavated SDF volume becomes particle sediment; sediment
//              becomes deposited SDF volume.
//  Thermal   : discrete talus shedding. Over-steep surface voxels move mass
//              downslope to the first stable voxel. This is NOT a blur/smooth:
//              it transports mass and preserves/builds sharp talus cones.
//  Aeolian   : wind abrasion with upwind sheltering + lee-side deposition.
//  Chemical  : dissolution ~ wetness x softness x concavity (karst), plus
//              evaporite precipitate on dry convex rock.
//
// Units: world units, dt in seconds. Tune via node params (collectFx).
// ============================================================================

import { clamp, lerp, mulberry32 } from './noise.js';

const FALLING = 1, FLOWING = 2;

// ---------------------------------------------------------------------------
// Surface / emitter voxel caches (importance sampling for spawning).
// These are plain voxel lists — NOT heightfields — rebuilt by staggered scan.
// ---------------------------------------------------------------------------
export class VoxelCache {
  constructor(volume) {
    this.vol = volume;
    this.surface = [];   // flat indices with solid voxel adjacent to air
    this.emitters = [];  // flat indices with emitter paint > 8
    this._nextS = [];
    this._nextE = [];
    this.cursor = 0;
    this.full = false;
  }
  invalidate() {
    this.cursor = 0; this.full = false;
    this.surface.length = 0; this.emitters.length = 0;
    this._nextS.length = 0; this._nextE.length = 0;
  }
  // Scan a slice of the volume; call every frame. Double-buffered: the live
  // lists stay valid while the next pass accumulates, then swap on wrap.
  update(slice = 1 / 6) {
    const V = this.vol, N = V.N;
    const total = N * N * N;
    const chunk = Math.max(4096, Math.floor(total * slice));
    let end = Math.min(total, this.cursor + chunk);
    const S = V.sdf, A = V.attrA;
    for (let id = this.cursor; id < end; id++) {
      if (S[id] < 0) {
        const i = id % N, j = ((id / N) | 0) % N, k = (id / (N * N)) | 0;
        // adjacent to air?
        if (i === 0 || j === 0 || k === 0 || i === N - 1 || j === N - 1 || k === N - 1 ||
          S[id - 1] >= 0 || S[id + 1] >= 0 || S[id - N] >= 0 || S[id + N] >= 0 ||
          S[id - N * N] >= 0 || S[id + N * N] >= 0) {
          this._nextS.push(id);
        }
      }
      if (A[id * 4 + 3] > 10) this._nextE.push(id);
    }
    this.cursor = end;
    if (end >= total) {
      const tS = this.surface; this.surface = this._nextS; this._nextS = tS; this._nextS.length = 0;
      const tE = this.emitters; this.emitters = this._nextE; this._nextE = tE; this._nextE.length = 0;
      this.full = true;
      this.cursor = 0;
    }
  }
  randomSurface(rand, out) {
    if (!this.surface.length) return false;
    const id = this.surface[(rand() * this.surface.length) | 0];
    const N = this.vol.N;
    out.i = id % N; out.j = ((id / N) | 0) % N; out.k = (id / (N * N)) | 0;
    return true;
  }
  randomEmitter(rand, out) {
    if (!this.emitters.length) return false;
    const id = this.emitters[(rand() * this.emitters.length) | 0];
    const N = this.vol.N;
    out.i = id % N; out.j = ((id / N) | 0) % N; out.k = (id / (N * N)) | 0;
    return true;
  }
}

// ---------------------------------------------------------------------------
// Hydraulic particle system
// ---------------------------------------------------------------------------
export class HydroSim {
  constructor(volume, cache) {
    this.vol = volume;
    this.cache = cache;
    this.cap = 48000;
    const C = this.cap;
    this.px = new Float32Array(C); this.py = new Float32Array(C); this.pz = new Float32Array(C);
    this.vx = new Float32Array(C); this.vy = new Float32Array(C); this.vz = new Float32Array(C);
    this.water = new Float32Array(C); this.sed = new Float32Array(C);
    this.life = new Float32Array(C); this.state = new Uint8Array(C);
    this.alive = 0;           // packed count (swap-remove)
    this.index = new Uint32Array(C); // slot permutation for packed iteration
    for (let i = 0; i < C; i++) this.index[i] = i;
    this.rand = mulberry32(1234567);
    this.processed = 0; this.spawned = 0; this.killed = 0;
    this.ms = 0;
    // overlay export (rain streaks)
    this.overlayCap = 24000;
    this.overlay = new Float32Array(this.overlayCap * 3);
    this.overlayVel = new Float32Array(this.overlayCap * 3);
    this.overlayCount = 0;
    this._tmp = { i: 0, j: 0, k: 0 };
    this._g = { x: 0, y: 0, z: 0 };
    this._w = { x: 0, y: 0, z: 0 };
  }

  reset() {
    this.state.fill(0);
    this.alive = 0;
    for (let i = 0; i < this.cap; i++) this.index[i] = i;
  }

  setFx(fx, seaLevel) {
    this.fx = fx;
    this.seaLevel = seaLevel;
  }

  spawnOne(kind, P) {
    if (this.alive >= Math.min(this.cap, (P && P.maxParts) || this.cap)) return false;
    const V = this.vol, h = V.worldSize / 2;
    const slot = this.index[this.alive];
    const r = this.rand;
    let sx, sy, sz;
    if (kind === 'paintedRain') {
      if (!this.cache.randomEmitter(r, this._tmp)) return false;
      V.gridToWorld(this._tmp.i, this._tmp.j, this._tmp.k, this._w);
      sx = this._w.x + (r() - 0.5) * V.voxel * 3;
      sy = h - r() * 0.15;
      sz = this._w.z + (r() - 0.5) * V.voxel * 3;
    } else {
      if (this.cache.full || this.cache.surface.length > 2000) {
        if (!this.cache.randomSurface(r, this._tmp)) return false;
        V.gridToWorld(this._tmp.i, this._tmp.j, this._tmp.k, this._w);
        sx = this._w.x + (r() - 0.5) * V.voxel * 4;
        sy = h - r() * 0.1;
        sz = this._w.z + (r() - 0.5) * V.voxel * 4;
      } else {
        sx = (r() * 2 - 1) * h * 0.95;
        sy = h - r() * 0.1;
        sz = (r() * 2 - 1) * h * 0.95;
      }
    }
    this.px[slot] = sx; this.py[slot] = sy; this.pz[slot] = sz;
    this.vx[slot] = (r() - 0.5) * 0.1;
    this.vy[slot] = -0.4 - r() * 0.3;
    this.vz[slot] = (r() - 0.5) * 0.1;
    this.water[slot] = 1;
    this.sed[slot] = 0;
    this.life[slot] = 26 + r() * 30;
    this.state[slot] = FALLING;
    this.alive++;
    this.spawned++;
    return true;
  }

  killAt(pi) {
    // swap-remove from packed list
    const last = this.alive - 1;
    const s = this.index[pi];
    this.index[pi] = this.index[last];
    this.index[last] = s;
    this.state[s] = 0;
    this.alive = last;
    this.killed++;
  }

  step(dt, windVec) {
    const t0 = performance.now ? performance.now() : Date.now();
    const P = (this.fx && this.fx.hydro) || {};
    if (P.enabled === false || !this.fx) { this.ms = 0; return; }
    const V = this.vol, vs = V.voxel, h = V.worldSize / 2;
    const r = this.rand;
    const gravity = P.gravity ?? 2.2;
    const excavK = P.excavate ?? 1.25;
    const capK = P.capacity ?? 1.0;
    const depK = P.deposit ?? 1.0;
    const evap = P.evap ?? 0.06;
    const infil = P.infil ?? 0.10;

    // --- spawn ---------------------------------------------------------------
    let toSpawn = P.spawn ?? 2600;
    toSpawn = Math.round(toSpawn * clamp(dt * 60, 0.25, 2.5));
    const rains = this.fx.rain && this.fx.rain.length ? this.fx.rain : [{ kind: 'rain', params: { rate: 0.3, dropSize: 1 } }];
    let totalRate = 0;
    for (const rn of rains) totalRate += Math.max(0, rn.params.rate ?? 0);
    if (totalRate > 0) {
      for (const rn of rains) {
        const share = Math.round(toSpawn * (Math.max(0, rn.params.rate ?? 0) / totalRate));
        for (let s = 0; s < share; s++) {
          if (!this.spawnOne(rn.kind, P)) break;
          const slot = this.index[this.alive - 1];
          this.water[slot] = rn.params.dropSize ?? 1;
        }
      }
    }

    // --- integrate (adaptive: 1 substep per particle per frame) ---------------
    const dragK = 0.12;
    const maxSteps = this.alive;
    let pi = 0;
    const timeBudget = 11; // ms
    const tStart = performance.now ? performance.now() : Date.now();
    let now = tStart;
    while (pi < this.alive) {
      const s = this.index[pi];
      const st = this.state[s];
      if (st === FALLING) {
        // ballistic with wind + drag; big steps in air
        let x = this.px[s], y = this.py[s], z = this.pz[s];
        let vx = this.vx[s], vy = this.vy[s], vz = this.vz[s];
        vx += (windVec.x * 0.6 - vx * dragK) * dt * 3;
        vy += (-gravity - vy * dragK * 0.4) * dt * 3;
        vz += (windVec.z * 0.6 - vz * dragK) * dt * 3;
        const stepLen = Math.min(0.5 * vs + Math.hypot(vx, vy, vz) * dt * 2, vs * 3.2);
        const sp = Math.hypot(vx, vy, vz) || 1;
        const dx = vx / sp, dy = vy / sp, dz = vz / sp;
        // march along velocity until surface contact (max 4 probes)
        let hit = false;
        for (let pr = 0; pr < 4; pr++) {
          const nx = x + dx * stepLen, ny = y + dy * stepLen, nz = z + dz * stepLen;
          x = nx; y = ny; z = nz;
          if (y < -h || y < this.seaLevel - 0.02) { break; }
          const d = V.sample(x, y, z);
          if (d < vs * 0.6) { hit = true; break; }
          if (pr === 3) break;
        }
        this.px[s] = x; this.py[s] = y; this.pz[s] = z;
        this.vx[s] = vx; this.vy[s] = vy; this.vz[s] = vz;
        this.life[s] -= dt * 8;
        if (hit) this.impact(s, excavK, infil);
        else if (y < -h || y < this.seaLevel - 0.03 || this.life[s] <= 0) this.killAt(pi--);
      } else if (st === FLOWING) {
        this.flowStep(s, dt, capK, depK, evap, infil);
        if (this.state[s] === 0) pi--; // killed inside
      } else {
        this.killAt(pi--);
      }
      pi++;
      if ((pi & 1023) === 0) {
        now = performance.now ? performance.now() : Date.now();
        if (now - tStart > timeBudget) break; // resume next frame (packed order rotates)
      }
    }
    // rotate packed order so every particle advances over successive frames
    if (pi < this.alive && this.alive > 1) {
      const cut = pi;
      const head = this.index.slice(0, cut);
      this.index.copyWithin(0, cut, this.alive);
      this.index.set(head, this.alive - cut);
    }
    this.processed = pi;

    // --- overlay export ---------------------------------------------------------
    let oc = 0;
    const stride = Math.max(1, Math.floor(this.alive / this.overlayCap));
    for (let k = 0; k < this.alive && oc < this.overlayCap; k += stride) {
      const s = this.index[k];
      if (this.state[s] !== FALLING) continue;
      this.overlay[oc * 3] = this.px[s];
      this.overlay[oc * 3 + 1] = this.py[s];
      this.overlay[oc * 3 + 2] = this.pz[s];
      this.overlayVel[oc * 3] = this.vx[s];
      this.overlayVel[oc * 3 + 1] = this.vy[s];
      this.overlayVel[oc * 3 + 2] = this.vz[s];
      oc++;
    }
    this.overlayCount = oc;
    const t1 = performance.now ? performance.now() : Date.now();
    this.ms = t1 - t0;
  }

  impact(s, excavK, infil) {
    const V = this.vol, vs = V.voxel;
    const r = this.rand;
    const x = this.px[s], y = this.py[s], z = this.pz[s];
    const vx = this.vx[s], vy = this.vy[s], vz = this.vz[s];
    V.gradient(x, y, z, this._g);
    const nx = this._g.x, ny = this._g.y, nz = this._g.z;
    const sp = Math.hypot(vx, vy, vz);
    const vn = Math.max(0, -(vx * nx + vy * nz + vz * nz));
    const hard = V.sampleAttr(V.attrB, 0, x, y, z);
    const mass = this.water[s];

    // --- kinetic excavation: crater radius ~ cbrt(KE / strength) ---------------
    const KE = 0.5 * mass * sp * sp;
    const strength = 0.22 + hard * 2.4;
    let craterR = excavK * (0.9 * vs + 2.35 * vs * Math.cbrt(Math.max(0, KE) / strength + 1e-5) * (0.55 + 0.45 * (vn / (sp + 1e-4))));
    craterR = Math.min(craterR, vs * 5.5);
    const hardnessScale = 1 / (1 + hard * 1.6);
    const elong = 1.25 + 1.1 * clamp(1 - vn / (sp + 1e-4), 0, 1); // grazing hits dig grooves
    let removed = 0;
    if (craterR > vs * 0.55) {
      removed = V.stampCarveOriented(
        x - nx * vs * 0.4, y - ny * vs * 0.4, z - nz * vs * 0.4,
        vx / (sp + 1e-5), vy / (sp + 1e-5), vz / (sp + 1e-5),
        craterR, elong, 0.32, hardnessScale);
    }
    // --- ejecta rim: 18% of spoil lands around the crater ----------------------
    if (removed > 0 && r() < 0.75) {
      const a = r() * Math.PI * 2;
      const rr = craterR * (1.1 + r() * 0.9);
      // tangent basis
      let tx = -nz, ty = 0, tz = nx;
      const tl = Math.hypot(tx, ty, tz) || 1; tx /= tl; ty /= tl; tz /= tl;
      V.stampDeposit(x + tx * Math.cos(a) * rr + nx * vs, y + ty * Math.cos(a) * rr + ny * vs,
        z + tz * Math.sin(a) * rr * 0.9 + nz * vs,
        craterR * 0.45, 0.7, 0.35);
      const cell = V.sampleNearestIdx(x, y, z);
      if (cell) { V.addAttr(V.attrA, 0, cell.i, cell.j, cell.k, 0.25); V.addAttr(V.attrA, 2, cell.i, cell.j, cell.k, 0.3); }
    }
    // --- sediment uptake (fines fraction is washed away) ------------------------
    const fines = 0.22;
    this.sed[s] += removed * (1 - fines);
    this.water[s] = mass * (1 - infil * 0.7);
    // --- transition to surface flow ----------------------------------------------
    const dot = vx * nx + vy * ny + vz * nz;
    const retain = 0.42;
    this.vx[s] = (vx - dot * nx) * retain;
    this.vy[s] = (vy - dot * ny) * retain;
    this.vz[s] = (vz - dot * nz) * retain;
    this.state[s] = FLOWING;
    this.life[s] = Math.min(this.life[s], 14 + r() * 14);
    const cell = V.sampleNearestIdx(x, y, z);
    if (cell) V.addAttr(V.attrA, 1, cell.i, cell.j, cell.k, 0.35);
  }

  flowStep(s, dt, capK, depK, evap, infil) {
    const V = this.vol, vs = V.voxel;
    const r = this.rand;
    let x = this.px[s], y = this.py[s], z = this.pz[s];
    const d0 = V.sample(x, y, z);
    if (d0 > vs * 2.2) { this.state[s] = FALLING; return; } // ran off an edge -> waterfall
    V.gradient(x, y, z, this._g);
    const nx = this._g.x, ny = this._g.y, nz = this._g.z;
    // steepest descent in 3D: gravity projected on tangent plane, t = g - (g.n)n
    const gDotN = -ny;
    let tx = -nx * gDotN;
    let ty = -1 - ny * gDotN;
    let tz = -nz * gDotN;
    let slope = Math.hypot(tx, ty, tz);
    if (slope < 1e-3) { // flat: diffuse wander (ponding), heavy deposition
      const a = r() * Math.PI * 2;
      tx = Math.cos(a); ty = 0; tz = Math.sin(a);
      slope = 0.02;
    } else { tx /= slope; ty /= slope; tz /= slope; }
    const water = this.water[s];
    const targetSpeed = (0.55 + 2.6 * slope) * (0.35 + 0.65 * Math.min(1, water));
    const blend = 0.35;
    this.vx[s] += (tx * targetSpeed - this.vx[s]) * blend;
    this.vy[s] += (ty * targetSpeed - this.vy[s]) * blend;
    this.vz[s] += (tz * targetSpeed - this.vz[s]) * blend;
    const stepD = clamp(Math.hypot(this.vx[s], this.vy[s], this.vz[s]) * dt * 2.2, vs * 0.3, vs * 2.4);
    const vl = Math.hypot(this.vx[s], this.vy[s], this.vz[s]) || 1;
    x += this.vx[s] / vl * stepD;
    y += this.vy[s] / vl * stepD;
    z += this.vz[s] / vl * stepD;
    // stick to surface
    const d1 = V.sample(x, y, z);
    x -= nx * d1 * 0.92; y -= ny * d1 * 0.92; z -= nz * d1 * 0.92;
    this.px[s] = x; this.py[s] = y; this.pz[s] = z;

    const cell = V.sampleNearestIdx(x, y, z);
    const speed = Math.hypot(this.vx[s], this.vy[s], this.vz[s]);
    // transport capacity ~ discharge x velocity x slope (3D Bagnold-style)
    const cap = capK * water * (0.15 + speed) * (0.06 + slope) * vs * vs * 2.4;
    let sed = this.sed[s];
    if (sed > cap) {
      // drape deposition along the trail -> levees, fans, deltas
      const dep = Math.min(sed - cap, sed * 0.5) * depK;
      if (dep > 1e-9) {
        const rr = vs * (0.9 + 1.3 * Math.min(1, dep / (vs * vs * vs + 1e-9)));
        V.stampDeposit(x + nx * vs * 0.3, y + ny * vs * 0.3, z + nz * vs * 0.3, Math.min(rr, vs * 2.6), 0.75, 0.5);
        sed -= dep;
        if (cell) { V.addAttr(V.attrA, 0, cell.i, cell.j, cell.k, 0.30); V.addAttr(V.attrA, 1, cell.i, cell.j, cell.k, 0.12); }
      }
    } else {
      // entrainment: plough a groove -> rills & gullies (adds detail, NOT smoothing)
      const hard = V.sampleAttr(V.attrB, 0, x, y, z);
      const want = (cap - sed) * 0.35 / (0.4 + hard * 2.0);
      if (want > 1e-10 && speed > 0.25) {
        const removed = V.stampCarve(x - nx * vs * 0.5, y - ny * vs * 0.5, z - nz * vs * 0.5,
          Math.min(vs * 1.5, vs * (0.7 + want / (vs * vs * vs + 1e-9))), 0.4, 1 / (1 + hard * 1.8));
        sed += removed * 0.9;
        if (cell && removed > 0) V.addAttr(V.attrA, 1, cell.i, cell.j, cell.k, 0.22);
      } else if (cell) {
        V.addAttr(V.attrA, 1, cell.i, cell.j, cell.k, 0.05);
      }
    }
    this.sed[s] = sed;
    this.water[s] = water * (1 - clamp(evap * dt * 3 + infil * dt * 2, 0, 0.5));
    this.life[s] -= dt * 6;
    if (this.water[s] < 0.02 || this.life[s] <= 0 || y < this.seaLevel - 0.02) {
      // dry-out: dump remaining load as a fan
      if (sed > vs * vs * vs * 0.02 && y > this.seaLevel - 0.02) {
        V.stampDeposit(x, y, z, Math.min(vs * 2.2, vs * (0.8 + sed / (vs * vs * vs + 1e-9))), 0.8, 0.6);
        if (cell) V.addAttr(V.attrA, 0, cell.i, cell.j, cell.k, 0.4);
      }
      this.state[s] = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// Thermal: discrete talus shedding (mass transport, not blur)
// ---------------------------------------------------------------------------
export class ThermalSim {
  constructor(volume, cache) {
    this.vol = volume; this.cache = cache;
    this.rand = mulberry32(777001);
    this.cursor = 0;
    this.ms = 0;
    this._g = { x: 0, y: 0, z: 0 };
    this._w = { x: 0, y: 0, z: 0 };
  }
  step(dt, P) {
    const t0 = performance.now ? performance.now() : Date.now();
    if (!P || P.enabled === false) { this.ms = 0; return; }
    const V = this.vol, vs = V.voxel, surf = this.cache.surface;
    if (!surf.length) { this.ms = 0; return; }
    const N = V.N;
    const budget = Math.min(P.budget ?? 26000, surf.length);
    const reposeTan = Math.tan((P.repose ?? 34) * Math.PI / 180);
    const rate = (P.rate ?? 0.8) * clamp(dt * 60, 0.25, 2);
    const r = this.rand;
    let moved = 0;
    for (let n = 0; n < budget; n++) {
      this.cursor = (this.cursor + 1 + ((r() * 7) | 0)) % surf.length;
      const id = surf[this.cursor];
      if (V.sdf[id] >= -vs * 0.2) continue; // must be solid near surface
      const i = id % N, j = ((id / N) | 0) % N, k = (id / (N * N)) | 0;
      V.gridToWorld(i, j, k, this._w);
      const x = this._w.x, y = this._w.y, z = this._w.z;
      V.gradient(x, y, z, this._g);
      const nx = this._g.x, ny = this._g.y, nz = this._g.z;
      const horiz = Math.hypot(nx, nz);
      if (horiz < 1e-4) continue;
      const talus = V.attrA[id * 4 + 2] / 255;
      const hard = V.attrB[id * 4] / 255;
      // loose talus rests shallower; hard bedrock stands steeper
      const reposeHere = reposeTan * (1 - talus * 0.35) * (0.75 + hard * 0.9);
      const steep = horiz / Math.max(0.12, ny); // tan of slope angle
      if (steep < reposeHere) continue;
      const excess = Math.min(2.2, (steep - reposeHere) / reposeHere + 0.25);
      // find deposition voxel: walk downhill along surface
      const dx = nx / horiz, dz = nz / horiz;
      let qx = x, qy = y, qz = z, found = false;
      for (let m = 1; m <= 7; m++) {
        qx = x + dx * vs * m * 1.4;
        qz = z + dz * vs * m * 1.4;
        qy = y - vs * m * (0.75 + 0.5 * reposeHere);
        const dq = V.sample(qx, qy, qz);
        if (dq > vs * 0.4) { found = true; break; } // air pocket below
        if (dq < -vs * 3) break; // buried, abort
      }
      if (!found) continue;
      const thick = Math.min(vs * 0.85, vs * 0.34 * rate * excess);
      const removed = V.stampCarve(x, y, z, vs * 1.25, 0.5, 1);
      if (removed <= 0) continue;
      V.stampDeposit(qx, qy, qz, vs * 1.6, 0.8, clamp(removed / (vs * vs * vs) * 0.5 + 0.25, 0.2, 1));
      const c0 = V.sampleNearestIdx(x, y, z);
      const c1 = V.sampleNearestIdx(qx, qy, qz);
      if (c0) V.addAttr(V.attrA, 2, c0.i, c0.j, c0.k, 0.12);
      if (c1) { V.addAttr(V.attrA, 2, c1.i, c1.j, c1.k, 0.5); V.addAttr(V.attrA, 0, c1.i, c1.j, c1.k, 0.2); }
      moved++;
      void thick;
    }
    const t1 = performance.now ? performance.now() : Date.now();
    this.ms = t1 - t0;
    this.moved = moved;
  }
}

// ---------------------------------------------------------------------------
// Aeolian: wind abrasion + saltation + lee deposition
// ---------------------------------------------------------------------------
export class WindSim {
  constructor(volume, cache) {
    this.vol = volume; this.cache = cache;
    this.rand = mulberry32(424242);
    this.cursor = 0;
    this.ms = 0;
    this._g = { x: 0, y: 0, z: 0 };
    this._w = { x: 0, y: 0, z: 0 };
  }
  static dirFromParams(P, out) {
    const a = ((P && P.dir) || 0) * Math.PI / 180;
    out.x = Math.sin(a); out.y = 0; out.z = Math.cos(a);
    return out;
  }
  step(dt, P, windVec) {
    const t0 = performance.now ? performance.now() : Date.now();
    if (!P || P.enabled === false) { this.ms = 0; return; }
    const V = this.vol, vs = V.voxel, surf = this.cache.surface;
    if (!surf.length) { this.ms = 0; return; }
    const N = V.N;
    const budget = Math.min(P.budget ?? 20000, surf.length);
    const speed = P.speed ?? 1.1;
    const abrasion = P.abrasion ?? 0.9;
    const gust = P.gust ?? 0.7;
    const rate = clamp(dt * 60, 0.25, 2);
    const r = this.rand;
    const upx = -windVec.x, upz = -windVec.z; // upwind direction
    for (let n = 0; n < budget; n++) {
      this.cursor = (this.cursor + 1 + ((r() * 5) | 0)) % surf.length;
      const id = surf[this.cursor];
      if (V.sdf[id] >= -vs * 0.2) continue;
      const i = id % N, j = ((id / N) | 0) % N, k = (id / (N * N)) | 0;
      V.gridToWorld(i, j, k, this._w);
      const x = this._w.x, y = this._w.y, z = this._w.z;
      // upwind sheltering march (6 probes)
      let exposure = 1;
      for (let m = 1; m <= 6; m++) {
        const dUp = V.sample(x + upx * vs * m * 2.2, y + vs * m * 0.9, z + upz * vs * m * 2.2);
        if (dUp < 0) { exposure = (m - 1) / 6; break; }
      }
      V.gradient(x, y, z, this._g);
      const nx = this._g.x, ny = this._g.y, nz = this._g.z;
      // wind component tangent to surface (with vertical gust shear)
      const wob = Math.sin(x * 21.7 + y * 13.1 + performanceNow() * 0.0011) * 0.5 + 0.5;
      const local = speed * (0.55 + 0.9 * gust * wob);
      const wdx = windVec.x * local, wdz = windVec.z * local;
      const wdot = wdx * nx + wdz * nz;
      const tx = wdx - wdot * nx, ty = -wdot * ny * 0.4, tz = wdz - wdot * nz;
      const shear = Math.hypot(tx, ty, tz) * (0.2 + 0.8 * exposure);
      const hard = V.attrB[id * 4] / 255;
      const loose = Math.max(V.attrA[id * 4] / 255, V.attrA[id * 4 + 2] / 255);
      if (exposure > 0.42 && shear > 0.35) {
        // windward abrasion / deflation: soft + loose goes first
        const erod = abrasion * shear * shear * (0.35 + 0.65 * (1 - hard)) * (0.5 + 0.5 * loose + 0.25);
        if (erod > 0.05) {
          const removed = V.stampCarve(x, y, z, vs * (1.0 + Math.min(1, erod * 0.4)), 0.55, 1 / (1 + hard * 2.2));
          if (removed > 0 && r() < 0.3) V.addAttr(V.attrA, 2, i, j, k, -0.25);
          void rate;
        }
      } else if (exposure < 0.30 && ny > 0.45) {
        // lee deposition: sand shadow drifts
        const dep = (0.30 - exposure) * local * 0.5;
        if (dep > 0.02 && r() < 0.6) {
          V.stampDeposit(x + nx * vs, y + ny * vs, z + nz * vs, vs * 1.35, 0.85, clamp(dep, 0.1, 0.8));
          V.addAttr(V.attrA, 0, i, j, k, 0.3);
        }
      }
    }
    const t1 = performance.now ? performance.now() : Date.now();
    this.ms = t1 - t0;
  }
}

// ---------------------------------------------------------------------------
// Chemical: karst dissolution + evaporite precipitate
// ---------------------------------------------------------------------------
export class ChemSim {
  constructor(volume, cache) {
    this.vol = volume; this.cache = cache;
    this.rand = mulberry32(900913);
    this.cursor = 0;
    this.ms = 0;
    this._w = { x: 0, y: 0, z: 0 };
  }
  step(dt, P) {
    const t0 = performance.now ? performance.now() : Date.now();
    if (!P || P.enabled === false) { this.ms = 0; return; }
    const V = this.vol, vs = V.voxel, surf = this.cache.surface;
    if (!surf.length) { this.ms = 0; return; }
    const N = V.N;
    const budget = Math.min(P.budget ?? 20000, surf.length);
    const rate = (P.rate ?? 0.7) * clamp(dt * 60, 0.25, 2);
    const karst = P.karst ?? 1.2;
    const r = this.rand;
    for (let n = 0; n < budget; n++) {
      this.cursor = (this.cursor + 1 + ((r() * 5) | 0)) % surf.length;
      const id = surf[this.cursor];
      if (V.sdf[id] >= -vs * 0.3 || V.sdf[id] < -vs * 4) continue;
      const wet = V.attrA[id * 4 + 1] / 255;
      if (wet < 0.04) continue;
      const hard = V.attrB[id * 4] / 255;
      const i = id % N, j = ((id / N) | 0) % N, k = (id / (N * N)) | 0;
      V.gridToWorld(i, j, k, this._w);
      // concavity via laplacian sign (cheap 6-tap)
      const e = vs * 1.4;
      const c = V.sdf[id];
      const lap = (V.sample(this._w.x + e, this._w.y, this._w.z) + V.sample(this._w.x - e, this._w.y, this._w.z) +
        V.sample(this._w.x, this._w.y + e, this._w.z) + V.sample(this._w.x, this._w.y - e, this._w.z) +
        V.sample(this._w.x, this._w.y, this._w.z + e) + V.sample(this._w.x, this._w.y, this._w.z - e) - 6 * c);
      const concave = clamp(-lap / (vs * 2), 0, 1);   // pits dissolve faster
      const convex = clamp(lap / (vs * 2), 0, 1);
      const dissolve = rate * vs * 0.16 * Math.pow(wet, 1.4) * (1 - hard * 0.85) * (1 + karst * concave * 2.2);
      if (dissolve > 1e-7) {
        V.sdf[id] += dissolve;
        V.carvedVolume += dissolve * vs * vs;
        V.markDirtyBox(i, j, k, i, j, k);
        // leached rind softens
        const hb = V.attrB[id * 4];
        if (hb > 4) V.attrB[id * 4] = hb - 1;
      }
      // evaporite precipitate on dry convex brows
      if (convex > 0.4 && wet < 0.3 && r() < 0.02) {
        V.attrB[id * 4 + 3] = Math.min(255, V.attrB[id * 4 + 3] + 6);
      }
    }
    const t1 = performance.now ? performance.now() : Date.now();
    this.ms = t1 - t0;
  }
}

function performanceNow() {
  return (typeof performance !== 'undefined' && performance.now) ? performance.now() : Date.now();
}

// ---------------------------------------------------------------------------
// Master sim: owns all modules, fx wiring, paint API
// ---------------------------------------------------------------------------
export class ErosionSim {
  constructor(volume) {
    this.vol = volume;
    this.cache = new VoxelCache(volume);
    this.hydro = new HydroSim(volume, this.cache);
    this.thermal = new ThermalSim(volume, this.cache);
    this.wind = new WindSim(volume, this.cache);
    this.chem = new ChemSim(volume, this.cache);
    this.fx = null;
    this.windVec = { x: 0.6, y: 0, z: 0.75 };
    this.seaLevel = -0.28;
    this.running = true;
    this.stats = { hydroMs: 0, thermalMs: 0, windMs: 0, chemMs: 0, particles: 0 };
  }
  setFx(fx) {
    this.fx = fx;
    const mat = fx.material || {};
    this.seaLevel = mat.seaLevel ?? -0.28;
    this.hydro.setFx(fx, this.seaLevel);
    if (fx.wind) WindSim.dirFromParams(fx.wind, this.windVec);
    const sp = (fx.wind && fx.wind.speed) || 0;
    const l = Math.hypot(this.windVec.x, this.windVec.z) || 1;
    this.windVec.x = this.windVec.x / l * sp * 0.5;
    this.windVec.z = this.windVec.z / l * sp * 0.5;
  }
  resetParticles() { this.hydro.reset(); }
  onRegenerated() { this.cache.invalidate(); this.hydro.reset(); }
  step(dt) {
    this.cache.update(1 / 5);
    if (!this.running || !this.fx) return;
    const cdt = clamp(dt, 1 / 240, 1 / 20);
    this.hydro.step(cdt, this.windVec);
    this.thermal.step(cdt, this.fx.thermal);
    this.wind.step(cdt, this.fx.wind, this.windVec);
    this.chem.step(cdt, this.fx.chem);
    this.stats.hydroMs = this.hydro.ms;
    this.stats.thermalMs = this.thermal.ms;
    this.stats.windMs = this.wind.ms;
    this.stats.chemMs = this.chem.ms;
    this.stats.particles = this.hydro.alive;
  }
  // Bake N seconds of sim as fast as possible (fixed dt), for offline use.
  bake(seconds, dt = 1 / 30, onProgress) {
    const steps = Math.ceil(seconds / dt);
    for (let s = 0; s < steps; s++) {
      this.cache.update(1 / 3);
      this.step(dt);
      if (onProgress && (s & 7) === 0) onProgress(s / steps);
    }
    if (onProgress) onProgress(1);
  }
}
