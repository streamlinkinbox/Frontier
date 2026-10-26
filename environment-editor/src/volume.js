// True 3D signed-distance volume: the terrain's single source of truth.
//
//   sdf < 0  solid rock     sdf > 0  air     sdf = 0  surface
//
// Unlike the CliffGenerator (heightfield + SDF chunks only on steep faces),
// EVERYTHING here is the volume: overhangs, cornices, cave roofs and arch
// undersides are ordinary iso-surface. The heightfield only ever reappears
// as a derived product (top-surface bed for the 2.5D water phase, drainage
// graph for stream-power incision).
//
// Per-node fields: sdf, bed index, hardness, joint proximity, bed thickness,
// loose sediment. Chunked 16³ cells; every edit marks dirty chunks and only
// those are remeshed.

import { clamp, lerp, smoothstep, smin, smax, mulberry32, fbm2, value2 } from './noise.js';

export const DEFAULT_DIMS = { nx: 97, ny: 65, nz: 97, spacing: 1, origin: [-48, 0, -48], chunk: 16 };

export class SdfVolume {
  constructor(dims = DEFAULT_DIMS) {
    this.nx = dims.nx; this.ny = dims.ny; this.nz = dims.nz;
    this.spacing = dims.spacing;
    this.origin = dims.origin.slice();
    this.chunk = dims.chunk;
    const n = this.nx * this.ny * this.nz;
    this.sdf = new Float32Array(n).fill(10);
    this.bed = new Uint8Array(n);
    this.hardness = new Uint8Array(n).fill(180);
    this.joint = new Uint8Array(n);
    this.bedThick = new Uint8Array(n).fill(30);
    this.sediment = new Float32Array(n);
    // 2D derived maps (per XZ column): drainage / wetness for water + vegetation phases
    this.flow = new Float32Array(this.nx * this.nz);
    this.moisture = new Float32Array(this.nx * this.nz);
    this.suitability = new Float32Array(this.nx * this.nz);
    this.geology = null;
    this.landmarks = {};
    this.chunksX = Math.ceil((this.nx - 1) / this.chunk);
    this.chunksY = Math.ceil((this.ny - 1) / this.chunk);
    this.chunksZ = Math.ceil((this.nz - 1) / this.chunk);
    this.dirty = new Set();
    this.markAllDirty();
  }

  index(ix, iy, iz) { return (iz * this.ny + iy) * this.nx + ix; }
  nodeX(ix) { return this.origin[0] + ix * this.spacing; }
  nodeY(iy) { return this.origin[1] + iy * this.spacing; }
  nodeZ(iz) { return this.origin[2] + iz * this.spacing; }
  get sizeX() { return (this.nx - 1) * this.spacing; }
  get sizeY() { return (this.ny - 1) * this.spacing; }
  get sizeZ() { return (this.nz - 1) * this.spacing; }

  chunkKey(cx, cy, cz) { return (cz * this.chunksY + cy) * this.chunksX + cx; }
  chunkCoords(key) {
    const cx = key % this.chunksX;
    const cy = ((key / this.chunksX) | 0) % this.chunksY;
    const cz = (key / (this.chunksX * this.chunksY)) | 0;
    return [cx, cy, cz];
  }
  markAllDirty() {
    this.dirty.clear();
    for (let cz = 0; cz < this.chunksZ; cz++) for (let cy = 0; cy < this.chunksY; cy++) for (let cx = 0; cx < this.chunksX; cx++)
      this.dirty.add(this.chunkKey(cx, cy, cz));
  }
  markDirtyNodeRange(x0, y0, z0, x1, y1, z1) {
    const c = this.chunk;
    const cx0 = clamp(Math.floor(x0 / c), 0, this.chunksX - 1), cx1 = clamp(Math.floor(x1 / c), 0, this.chunksX - 1);
    const cy0 = clamp(Math.floor(y0 / c), 0, this.chunksY - 1), cy1 = clamp(Math.floor(y1 / c), 0, this.chunksY - 1);
    const cz0 = clamp(Math.floor(z0 / c), 0, this.chunksZ - 1), cz1 = clamp(Math.floor(z1 / c), 0, this.chunksZ - 1);
    for (let cz = cz0; cz <= cz1; cz++) for (let cy = cy0; cy <= cy1; cy++) for (let cx = cx0; cx <= cx1; cx++)
      this.dirty.add(this.chunkKey(cx, cy, cz));
  }
  markDirtySphere(center, r) {
    const inv = 1 / this.spacing;
    this.markDirtyNodeRange(
      Math.floor((center[0] - r - this.origin[0]) * inv) - 1, Math.floor((center[1] - r - this.origin[1]) * inv) - 1, Math.floor((center[2] - r - this.origin[2]) * inv) - 1,
      Math.ceil((center[0] + r - this.origin[0]) * inv) + 1, Math.ceil((center[1] + r - this.origin[1]) * inv) + 1, Math.ceil((center[2] + r - this.origin[2]) * inv) + 1);
  }

  // ------------------------------------------------------------ sampling
  sampleScalar(arr, x, y, z) {
    const u = clamp((x - this.origin[0]) / this.spacing, 0, this.nx - 1.00001);
    const v = clamp((y - this.origin[1]) / this.spacing, 0, this.ny - 1.00001);
    const w = clamp((z - this.origin[2]) / this.spacing, 0, this.nz - 1.00001);
    const ix = u | 0, iy = v | 0, iz = w | 0;
    const fx = u - ix, fy = v - iy, fz = w - iz;
    const { nx, ny } = this;
    const b = (iz * ny + iy) * nx + ix;
    const c000 = arr[b], c100 = arr[b + 1], c010 = arr[b + nx], c110 = arr[b + nx + 1];
    const b2 = b + nx * ny;
    const c001 = arr[b2], c101 = arr[b2 + 1], c011 = arr[b2 + nx], c111 = arr[b2 + nx + 1];
    return lerp(lerp(lerp(c000, c100, fx), lerp(c010, c110, fx), fy),
                lerp(lerp(c001, c101, fx), lerp(c011, c111, fx), fy), fz);
  }
  sampleSdf(x, y, z) { return this.sampleScalar(this.sdf, x, y, z); }
  sampleHardness(x, y, z) { return this.sampleScalar(this.hardness, x, y, z) / 255; }
  sampleJoint(x, y, z) { return this.sampleScalar(this.joint, x, y, z) / 255; }
  sampleSediment(x, y, z) { return this.sampleScalar(this.sediment, x, y, z); }
  nearestBed(x, y, z) {
    const ix = clamp(Math.round((x - this.origin[0]) / this.spacing), 0, this.nx - 1);
    const iy = clamp(Math.round((y - this.origin[1]) / this.spacing), 0, this.ny - 1);
    const iz = clamp(Math.round((z - this.origin[2]) / this.spacing), 0, this.nz - 1);
    return this.bed[this.index(ix, iy, iz)];
  }

  nodeGradient(ix, iy, iz, out) {
    const { sdf, nx, ny, nz, spacing } = this;
    const x0 = Math.max(0, ix - 1), x1 = Math.min(nx - 1, ix + 1);
    const y0 = Math.max(0, iy - 1), y1 = Math.min(ny - 1, iy + 1);
    const z0 = Math.max(0, iz - 1), z1 = Math.min(nz - 1, iz + 1);
    const b = (iz * ny + iy) * nx + ix;
    out[0] = (sdf[b + (x1 - ix)] - sdf[b - (ix - x0)]) / ((x1 - x0) * spacing);
    out[1] = (sdf[b + (y1 - iy) * nx] - sdf[b - (iy - y0) * nx]) / ((y1 - y0) * spacing);
    out[2] = (sdf[b + (z1 - iz) * nx * ny] - sdf[b - (iz - z0) * nx * ny]) / ((z1 - z0) * spacing);
    return out;
  }
  /** Trilinearly interpolated grid gradient (= surface normal near the iso-surface). */
  gradientAt(x, y, z, out) {
    const u = clamp((x - this.origin[0]) / this.spacing, 0, this.nx - 1.00001);
    const v = clamp((y - this.origin[1]) / this.spacing, 0, this.ny - 1.00001);
    const w = clamp((z - this.origin[2]) / this.spacing, 0, this.nz - 1.00001);
    const ix = u | 0, iy = v | 0, iz = w | 0;
    const fx = u - ix, fy = v - iy, fz = w - iz;
    const g = [0, 0, 0];
    out[0] = 0; out[1] = 0; out[2] = 0;
    for (let c = 0; c < 8; c++) {
      const dx = c & 1, dy = (c >> 1) & 1, dz = (c >> 2) & 1;
      const wgt = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy) * (dz ? fz : 1 - fz);
      if (wgt === 0) continue;
      this.nodeGradient(ix + dx, iy + dy, iz + dz, g);
      out[0] += g[0] * wgt; out[1] += g[1] * wgt; out[2] += g[2] * wgt;
    }
    return out;
  }
  normalAt(x, y, z, out) {
    this.gradientAt(x, y, z, out);
    const l = Math.hypot(out[0], out[1], out[2]) || 1;
    out[0] /= l; out[1] /= l; out[2] /= l;
    return out;
  }

  // ------------------------------------------------------------ edits
  _nodesInSphere(center, r, fn) {
    const sp = this.spacing;
    const x0 = clamp(Math.floor((center[0] - r - this.origin[0]) / sp), 0, this.nx - 1);
    const x1 = clamp(Math.ceil((center[0] + r - this.origin[0]) / sp), 0, this.nx - 1);
    const y0 = clamp(Math.floor((center[1] - r - this.origin[1]) / sp), 0, this.ny - 1);
    const y1 = clamp(Math.ceil((center[1] + r - this.origin[1]) / sp), 0, this.ny - 1);
    const z0 = clamp(Math.floor((center[2] - r - this.origin[2]) / sp), 0, this.nz - 1);
    const z1 = clamp(Math.ceil((center[2] + r - this.origin[2]) / sp), 0, this.nz - 1);
    for (let iz = z0; iz <= z1; iz++) for (let iy = y0; iy <= y1; iy++) for (let ix = x0; ix <= x1; ix++) {
      const px = this.nodeX(ix), py = this.nodeY(iy), pz = this.nodeZ(iz);
      const d = Math.hypot(px - center[0], py - center[1], pz - center[2]);
      if (d <= r + sp) fn(this.index(ix, iy, iz), px, py, pz, d, ix, iy, iz);
    }
    this.markDirtySphere(center, r + sp);
  }

  /** Add rock: smooth union with a sphere (true CSG, can build overhangs/arches). */
  brushAdd(center, r, strength = 1, smooth = 1.2, opts = {}) {
    const a = clamp(strength, 0, 1);
    this._nodesInSphere(center, r + smooth, (idx, px, py, pz, d) => {
      const target = smin(this.sdf[idx], d - r, smooth);
      this.sdf[idx] = lerp(this.sdf[idx], target, a);
      if (opts.sediment != null && this.sdf[idx] < 0.4) this.sediment[idx] = Math.max(this.sediment[idx], opts.sediment);
      if (opts.bed != null && this.sdf[idx] < 0.4) this.bed[idx] = opts.bed;
    });
  }
  /** Remove rock: smooth subtraction of a sphere (digs caves/tunnels). */
  brushRemove(center, r, strength = 1, smooth = 1.2) {
    const a = clamp(strength, 0, 1);
    this._nodesInSphere(center, r + smooth, (idx, px, py, pz, d) => {
      const target = smax(this.sdf[idx], r - d, smooth);
      this.sdf[idx] = lerp(this.sdf[idx], target, a);
      if (this.sdf[idx] > 0) this.sediment[idx] = 0;
    });
  }
  /**
   * Rounded-box CSG aligned to an orthonormal frame (axes = [tangent, up-ish, normal]).
   * Block fall uses this instead of spheres: joint-bounded blocks have flat
   * bedding/joint faces, and spherical removals read as pockmarks, not rockfall.
   */
  brushBox(center, half, axes, remove = true, smooth = 0.2, opts = {}) {
    const rounding = Math.min(smooth + 0.12, Math.min(half[0], half[1], half[2]) * 0.6);
    const bound = Math.hypot(half[0], half[1], half[2]) + smooth + this.spacing;
    const [ax, ay, az] = axes;
    this._nodesInSphere(center, bound, (idx, px, py, pz) => {
      const dx = px - center[0], dy = py - center[1], dz = pz - center[2];
      const lx = dx * ax[0] + dy * ax[1] + dz * ax[2];
      const ly = dx * ay[0] + dy * ay[1] + dz * ay[2];
      const lz = dx * az[0] + dy * az[1] + dz * az[2];
      const qx = Math.abs(lx) - (half[0] - rounding), qy = Math.abs(ly) - (half[1] - rounding), qz = Math.abs(lz) - (half[2] - rounding);
      const boxD = Math.hypot(Math.max(qx, 0), Math.max(qy, 0), Math.max(qz, 0)) + Math.min(Math.max(qx, Math.max(qy, qz)), 0) - rounding;
      if (remove) {
        this.sdf[idx] = smax(this.sdf[idx], -boxD, smooth);
        if (this.sdf[idx] > 0) this.sediment[idx] = 0;
      } else {
        this.sdf[idx] = smin(this.sdf[idx], boxD, smooth);
        if (this.sdf[idx] < 0.4) {
          if (opts.sediment != null) this.sediment[idx] = Math.max(this.sediment[idx], opts.sediment);
          if (opts.bed != null) this.bed[idx] = opts.bed;
        }
      }
    });
  }
  brushRemoveBox(center, half, axes, smooth = 0.2) { this.brushBox(center, half, axes, true, smooth); }
  brushAddBox(center, half, axes, smooth = 0.25, opts = {}) { this.brushBox(center, half, axes, false, smooth, opts); }

  /** Blend the field towards a plane inside the brush (flatten / terrace). */
  brushFlatten(center, r, strength, planePoint, planeNormal) {
    this._nodesInSphere(center, r, (idx, px, py, pz, d) => {
      const fall = 0.5 + 0.5 * Math.cos(Math.PI * clamp(d / r, 0, 1));
      const planeD = (px - planePoint[0]) * planeNormal[0] + (py - planePoint[1]) * planeNormal[1] + (pz - planePoint[2]) * planeNormal[2];
      this.sdf[idx] = lerp(this.sdf[idx], planeD, clamp(strength * fall, 0, 1));
    });
  }
  brushSmooth(center, r, strength) {
    const old = this.sdf.slice();
    const { nx, ny } = this;
    this._nodesInSphere(center, r, (idx, px, py, pz, d, ix, iy, iz) => {
      if (ix < 1 || iy < 1 || iz < 1 || ix >= nx - 1 || iy >= ny - 1 || iz >= this.nz - 1) return;
      const avg = (old[idx - 1] + old[idx + 1] + old[idx - nx] + old[idx + nx] + old[idx - nx * ny] + old[idx + nx * ny]) / 6;
      const fall = 0.5 + 0.5 * Math.cos(Math.PI * clamp(d / r, 0, 1));
      this.sdf[idx] = lerp(old[idx], avg, clamp(strength * fall, 0, 1));
    });
  }
  /** Paint/erase loose soil on surface nodes (drives talus + vegetation suitability). */
  brushPaintSediment(center, r, strength, erase = false) {
    this._nodesInSphere(center, r, (idx, px, py, pz, d) => {
      if (Math.abs(this.sdf[idx]) > 1.6) return;
      const fall = 0.5 + 0.5 * Math.cos(Math.PI * clamp(d / r, 0, 1));
      this.sediment[idx] = clamp(this.sediment[idx] + (erase ? -1 : 1) * strength * fall, 0, 1.5);
    });
  }
  /** Erosion stamp: signed SDF delta in a falloff sphere (+ removes, − deposits). */
  stampDelta(center, r, delta, opts = {}) {
    if (delta === 0) return;
    this._nodesInSphere(center, r, (idx, px, py, pz, d) => {
      const w = clamp(1 - d / r, 0, 1);
      this.sdf[idx] += delta * w * w;
      if (delta < 0 && this.sdf[idx] < 0.5) {
        if (opts.sediment != null) this.sediment[idx] = Math.max(this.sediment[idx], opts.sediment);
        if (opts.bed != null) this.bed[idx] = opts.bed;
      }
    });
  }

  /** Two-pass chamfer redistancing: restores |∇sdf| ≈ 1 after CSG/erosion. */
  redistance() {
    const { nx, ny, nz, sdf, spacing } = this;
    const n = nx * ny * nz;
    const dist = new Float32Array(n).fill(1e6);
    const sign = new Int8Array(n);
    const g = [0, 0, 0];
    const layer = nx * ny;
    for (let iz = 0; iz < nz; iz++) for (let iy = 0; iy < ny; iy++) for (let ix = 0; ix < nx; ix++) {
      const idx = (iz * ny + iy) * nx + ix;
      sign[idx] = sdf[idx] >= 0 ? 1 : -1;
      const s = sign[idx];
      const boundary =
        (ix > 0 && (sdf[idx - 1] >= 0 ? 1 : -1) !== s) || (ix < nx - 1 && (sdf[idx + 1] >= 0 ? 1 : -1) !== s) ||
        (iy > 0 && (sdf[idx - nx] >= 0 ? 1 : -1) !== s) || (iy < ny - 1 && (sdf[idx + nx] >= 0 ? 1 : -1) !== s) ||
        (iz > 0 && (sdf[idx - layer] >= 0 ? 1 : -1) !== s) || (iz < nz - 1 && (sdf[idx + layer] >= 0 ? 1 : -1) !== s);
      if (boundary) {
        this.nodeGradient(ix, iy, iz, g);
        const gm = Math.max(0.25, Math.hypot(g[0], g[1], g[2]));
        dist[idx] = clamp(Math.abs(sdf[idx]) / gm, 0, 1.5 * spacing);
      }
    }
    const NB = [];
    for (let dz = -1; dz <= 1; dz++) for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
      if (!dx && !dy && !dz) continue;
      NB.push([dx, dy, dz, Math.hypot(dx, dy, dz) * spacing]);
    }
    const sweep = (forward) => {
      const xR = forward ? [0, nx, 1] : [nx - 1, -1, -1];
      const yR = forward ? [0, ny, 1] : [ny - 1, -1, -1];
      const zR = forward ? [0, nz, 1] : [nz - 1, -1, -1];
      for (let iz = zR[0]; iz !== zR[1]; iz += zR[2]) for (let iy = yR[0]; iy !== yR[1]; iy += yR[2]) for (let ix = xR[0]; ix !== xR[1]; ix += xR[2]) {
        const idx = (iz * ny + iy) * nx + ix;
        let best = dist[idx];
        for (const [dx, dy, dz, w] of NB) {
          const jx = ix + dx, jy = iy + dy, jz = iz + dz;
          if (jx < 0 || jy < 0 || jz < 0 || jx >= nx || jy >= ny || jz >= nz) continue;
          const cand = dist[(jz * ny + jy) * nx + jx] + w;
          if (cand < best) best = cand;
        }
        dist[idx] = best;
      }
    };
    sweep(true); sweep(false); sweep(true);
    for (let i = 0; i < n; i++) sdf[i] = sign[i] * Math.min(dist[i], 60);
  }

  // ------------------------------------------------------------ queries
  raycast(ro, rd, maxDist = 600) {
    // clip to volume AABB first
    const mn = this.origin, mx = [mn[0] + this.sizeX, mn[1] + this.sizeY, mn[2] + this.sizeZ];
    let t0 = 0, t1 = maxDist;
    for (let a = 0; a < 3; a++) {
      if (Math.abs(rd[a]) < 1e-9) { if (ro[a] < mn[a] || ro[a] > mx[a]) return null; continue; }
      let ta = (mn[a] - ro[a]) / rd[a], tb = (mx[a] - ro[a]) / rd[a];
      if (ta > tb) [ta, tb] = [tb, ta];
      t0 = Math.max(t0, ta); t1 = Math.min(t1, tb);
      if (t0 > t1) return null;
    }
    const p = [0, 0, 0];
    const at = (t) => { p[0] = ro[0] + rd[0] * t; p[1] = ro[1] + rd[1] * t; p[2] = ro[2] + rd[2] * t; return this.sampleSdf(p[0], p[1], p[2]); };
    let t = t0, prevT = t, prevV = at(t);
    for (let i = 0; i < 900 && t < t1; i++) {
      const step = clamp(Math.abs(prevV) * 0.85, 0.12, 3);
      t += step;
      const v = at(Math.min(t, t1));
      if ((prevV >= 0 && v < 0) || (prevV < 0 && v >= 0)) {
        let lo = prevT, hi = t, vlo = prevV;
        for (let k = 0; k < 18; k++) {
          const mid = (lo + hi) / 2, vm = at(mid);
          if ((vlo >= 0 && vm < 0) || (vlo < 0 && vm >= 0)) hi = mid; else { lo = mid; vlo = vm; }
        }
        const th = (lo + hi) / 2; at(th);
        const normal = this.normalAt(p[0], p[1], p[2], [0, 0, 0]);
        return { point: p.slice(), normal, distance: th, fromInside: prevV < 0 };
      }
      prevT = t; prevV = v;
    }
    return null;
  }

  /** All surface heights where a vertical column crosses the iso-surface (top first). */
  columnCrossings(x, z) {
    const ix = clamp(Math.round((x - this.origin[0]) / this.spacing), 0, this.nx - 1);
    const iz = clamp(Math.round((z - this.origin[2]) / this.spacing), 0, this.nz - 1);
    const out = [];
    let prevY = this.nodeY(this.ny - 1), prevV = this.sdf[this.index(ix, this.ny - 1, iz)];
    for (let iy = this.ny - 2; iy >= 0; iy--) {
      const y = this.nodeY(iy), v = this.sdf[this.index(ix, iy, iz)];
      if ((prevV >= 0 && v < 0) || (prevV < 0 && v >= 0)) out.push(lerp(prevY, y, prevV / (prevV - v)));
      prevY = y; prevV = v;
    }
    return out;
  }
  topHeight(x, z) { const c = this.columnCrossings(x, z); return c.length ? c[0] : this.origin[1]; }

  extractTopHeight() {
    const out = new Float32Array(this.nx * this.nz);
    for (let iz = 0; iz < this.nz; iz++) for (let ix = 0; ix < this.nx; ix++) {
      let h = this.origin[1];
      let prevV = this.sdf[this.index(ix, this.ny - 1, iz)], prevY = this.nodeY(this.ny - 1);
      for (let iy = this.ny - 2; iy >= 0; iy--) {
        const v = this.sdf[this.index(ix, iy, iz)], y = this.nodeY(iy);
        if (prevV >= 0 && v < 0) { h = lerp(prevY, y, prevV / (prevV - v)); break; }
        prevV = v; prevY = y;
      }
      out[iz * this.nx + ix] = h;
    }
    return out;
  }

  /** Cell-centred bed in exactly the layout FluidSim.setBed() consumes (nx × nz cells). */
  extractBedForFluid() {
    const top = this.extractTopHeight();
    const cx = this.nx - 1, cz = this.nz - 1;
    const heights = new Float32Array(cx * cz);
    for (let iz = 0; iz < cz; iz++) for (let ix = 0; ix < cx; ix++) {
      heights[iz * cx + ix] = (top[iz * this.nx + ix] + top[iz * this.nx + ix + 1] + top[(iz + 1) * this.nx + ix] + top[(iz + 1) * this.nx + ix + 1]) / 4;
    }
    return { nx: cx, ny: cz, cellSize: this.spacing, origin: [this.origin[0], this.origin[2]], heights };
  }

  solidVolume() {
    let n = 0;
    for (let i = 0; i < this.sdf.length; i++) if (this.sdf[i] < 0) n++;
    return n * Math.pow(this.spacing, 3);
  }

  snapshot() { return { sdf: this.sdf.slice(), sediment: this.sediment.slice(), bed: this.bed.slice() }; }
  restore(s) { this.sdf.set(s.sdf); this.sediment.set(s.sediment); this.bed.set(s.bed); this.markAllDirty(); }
}

// ------------------------------------------------------------ opening scene
export function cliffLineX(z, p) {
  const seed = (p.seed | 0) + 21;
  // Plan shape only (never vertical face noise): long headlands/bays plus
  // shorter joint-controlled embayments, then the line wraps west at both
  // ends so the massif reads as a promontory instead of a sliced rectangle.
  const bays = 5.0 * (value2(z * 0.085, 9.7, seed + 4) - 0.5) * 2;
  const wrap = -20 * smoothstep(28, 48, Math.abs(z));
  return (p.cliffX ?? -10)
    + 16 * (fbm2(z * 0.024, 4.4, 3, seed) - 0.5)
    + bays
    + 1.4 * Math.sin(z * 0.07 + 1.3)
    + wrap;
}

function segmentDistance(px, py, pz, a, b) {
  const abx = b[0] - a[0], aby = b[1] - a[1], abz = b[2] - a[2];
  const t = clamp(((px - a[0]) * abx + (py - a[1]) * aby + (pz - a[2]) * abz) / (abx * abx + aby * aby + abz * abz || 1), 0, 1);
  return Math.hypot(px - (a[0] + abx * t), py - (a[1] + aby * t), pz - (a[2] + abz * t));
}
function roundedBoxDistance(px, py, pz, c, half, r, cosA, sinA) {
  const dx = px - c[0], dy = py - c[1], dz = pz - c[2];
  const lx = dx * cosA - dz * sinA, lz = dx * sinA + dz * cosA;
  const qx = Math.abs(lx) - (half[0] - r), qy = Math.abs(dy) - (half[1] - r), qz = Math.abs(lz) - (half[2] - r);
  return Math.hypot(Math.max(qx, 0), Math.max(qy, 0), Math.max(qz, 0)) + Math.min(Math.max(qx, Math.max(qy, qz)), 0) - r;
}

/**
 * Escarpment + through-cave + fallen blocks, built as SDF CSG:
 * a relief distance field is only the *starting solid*; the cave is a capsule
 * subtraction hosted in a real soft bed of the geology, the blocks are rounded
 * -box unions at the cliff foot. From the first frame on, it is one volume.
 */
export async function buildEscarpmentScene(volume, geology, p, onProgress = null, yieldFn = null) {
  const { nx, ny, nz } = volume;
  const seed = p.seed | 0;
  const report = async (phase, f) => { if (onProgress) onProgress(phase, f); if (yieldFn) await yieldFn(); };
  await report('Base relief (SDF solid)', 0);

  // reference relief, used solely to seed the solid
  const H = new Float32Array(nx * nz);
  for (let iz = 0; iz < nz; iz++) for (let ix = 0; ix < nx; ix++) {
    const x = volume.nodeX(ix), z = volume.nodeZ(iz);
    const xc = cliffLineX(z, p);
    const plain = (p.plainHeight ?? 12) + 6.4 * (fbm2(x * 0.030, z * 0.030, 3, seed + 3) - 0.5) - 0.018 * (x + 48);
    const broadRidge = 3.4 * Math.exp(-(Math.pow((x + 28) / 30, 2) + Math.pow((z - 6) / 34, 2)));
    const plateau = (p.plateauHeight ?? 40) + 8.0 * (fbm2(x * 0.026 + 8.0, z * 0.026 - 3.0, 3, seed + 7) - 0.5) + broadRidge;
    // Face steepness varies along strike: hard headlands are near-vertical,
    // embayments open into wider slopes. This is plan/strike variation of the
    // starting solid; erosion then cuts the actual cornices and slots.
    const localWidth = (p.cliffWidth ?? 4.8) * (0.78 + 0.62 * value2(z * 0.058, 7.9, seed + 17));
    const tEsc = smoothstep(localWidth / 2, -localWidth / 2, x - xc);
    // The massif dies away towards the north/south patch edges instead of
    // ending in two more quarry-straight cut faces.
    const t = tEsc * (1 - 0.72 * smoothstep(30, 47, Math.abs(z)));
    let h = lerp(plain, plateau, t);
    // dry channels leaving the cliff foot across the plain (future river beds)
    const chanMask = (1 - tEsc) * smoothstep(xc + 2, xc + 10, x);
    const channels = [
      { base: 16, depth: 2.4, width: 3.6, phase: 0.6 },
      { base: -27, depth: 1.9, width: 3.1, phase: 2.1 },
      { base: 31, depth: 1.6, width: 2.8, phase: 4.0 },
    ];
    for (const ch of channels) {
      const zc = ch.base + 5 * Math.sin(x * 0.045 + ch.phase) + 4 * (fbm2(x * 0.04, ch.phase, 2, seed + 31) - 0.5);
      h -= ch.depth * Math.exp(-Math.pow((z - zc) / ch.width, 2)) * chanMask;
    }
    // small knoll on the plain, south-east
    h += 3.2 * Math.exp(-(Math.pow((x - 26) / 13, 2) + Math.pow((z + 26) / 11, 2))) * (1 - tEsc);
    H[iz * nx + ix] = h;
  }
  const heightAt = (x, z) => {
    const ix = clamp(Math.round((x - volume.origin[0]) / volume.spacing), 0, nx - 1);
    const iz = clamp(Math.round((z - volume.origin[2]) / volume.spacing), 0, nz - 1);
    return H[iz * nx + ix];
  };
  for (let iz = 0; iz < nz; iz++) for (let ix = 0; ix < nx; ix++) {
    const h = H[iz * nx + ix];
    const gx = (H[iz * nx + Math.min(nx - 1, ix + 1)] - H[iz * nx + Math.max(0, ix - 1)]) / (2 * volume.spacing);
    const gz = (H[Math.min(nz - 1, iz + 1) * nx + ix] - H[Math.max(0, iz - 1) * nx + ix]) / (2 * volume.spacing);
    const denom = Math.sqrt(1 + gx * gx + gz * gz);
    for (let iy = 0; iy < ny; iy++) volume.sdf[volume.index(ix, iy, iz)] = (volume.nodeY(iy) - h) / denom;
  }

  // ---- cave: capsule chain hosted in a soft bed, two mouths in the escarpment
  if (p.cave ?? 1) {
    await report('Carving cave (SDF subtraction)', 0.3);
    const xc0 = cliffLineX(-6, p), xc1 = cliffLineX(13, p);
    const soft = geology.findSoftBed(xc0, -6, (p.plainHeight ?? 12) + 8);
    // host the tunnel in that bed, but keep a real roof under the plateau and a floor above the plain
    const caveY = soft
      ? clamp(soft.centreY, (p.plainHeight ?? 12) + 3, Math.max((p.plainHeight ?? 12) + 4, (p.plateauHeight ?? 40) - 7))
      : (p.plainHeight ?? 12) + 8;
    const cr = (p.caveRadius ?? 2.4);
    const path = [
      [xc0 + 5, caveY, -6], [xc0 - 1, caveY, -6], [xc0 - 8, caveY + 0.6, -3.5],
      [xc0 - 14, caveY + 0.8, 1.5], [xc1 - 9, caveY + 0.2, 7], [xc1 - 2, caveY - 0.6, 11], [xc1 + 5, caveY - 1.2, 13],
    ];
    const chamber = [xc0 - 14, caveY + 0.8, 1.5];
    volume.landmarks.cave = { entrance: [xc0 + 7, caveY + 0.5, -6], chamber, exit: path[path.length - 1], path, bedIndex: soft?.index ?? -1, y: caveY };
    const xs = path.map((q) => q[0]), ys = path.map((q) => q[1]), zs = path.map((q) => q[2]);
    const pad = 7;
    const ix0 = clamp(Math.floor((Math.min(...xs) - pad - volume.origin[0])), 0, nx - 1), ix1 = clamp(Math.ceil(Math.max(...xs) + pad - volume.origin[0]), 0, nx - 1);
    const iy0 = clamp(Math.floor(Math.min(...ys) - pad), 0, ny - 1), iy1 = clamp(Math.ceil(Math.max(...ys) + pad), 0, ny - 1);
    const iz0 = clamp(Math.floor((Math.min(...zs) - pad - volume.origin[2])), 0, nz - 1), iz1 = clamp(Math.ceil(Math.max(...zs) + pad - volume.origin[2]), 0, nz - 1);
    for (let iz = iz0; iz <= iz1; iz++) for (let iy = iy0; iy <= iy1; iy++) for (let ix = ix0; ix <= ix1; ix++) {
      const px = volume.nodeX(ix), py = volume.nodeY(iy), pz = volume.nodeZ(iz);
      // The passage widens where the host rock is soft or jointed and pinches
      // through harder, massive rock — cross-section from the geology fields,
      // not a constant-radius tube. (Redistancing afterwards makes it a field.)
      const jointHere = volume.sampleJoint(px, py, pz);
      const hardHere = volume.sampleHardness(px, py, pz);
      const crHere = cr * (0.82 + 0.50 * jointHere + 0.28 * (1 - hardHere));
      let d = Infinity;
      for (let s = 0; s < path.length - 1; s++) d = Math.min(d, segmentDistance(px, py, pz, path[s], path[s + 1]) - crHere);
      d = Math.min(d, Math.hypot(px - chamber[0], (py - chamber[1]) * 0.85, pz - chamber[2]) - cr * 1.9 * (0.9 + 0.25 * jointHere));
      const idx = volume.index(ix, iy, iz);
      volume.sdf[idx] = smax(volume.sdf[idx], -d, 0.5);
    }
    // shallow rock shelter (alcove) further north, same horizon
    const xa = cliffLineX(-26, p);
    volume.brushRemove([xa - 0.5, caveY - 1, -26], cr * 1.5, 1, 0.6);
    volume.landmarks.shelter = [xa + 5, caveY, -26];
  }

  // ---- fallen joint blocks at the foot (rounded-box unions, now part of the volume)
  const nb = Math.round(p.fallenBlocks ?? 6);
  if (nb > 0) {
    await report('Fallen blocks', 0.55);
    const rng = mulberry32(seed * 31 + 77);
    for (let b = 0; b < nb; b++) {
      const z = -38 + rng() * 76;
      const x = cliffLineX(z, p) + 6 + rng() * 8;
      const s = 1.2 + rng() * 2.4;
      const c = [x, heightAt(x, z) + s * 0.38, z];
      const half = [s * (0.8 + rng() * 0.7), s * (0.45 + rng() * 0.35), s * (0.7 + rng() * 0.6)];
      const ang = rng() * Math.PI * 2, cosA = Math.cos(ang), sinA = Math.sin(ang), rr = s * 0.15;
      const sourceBed = geology.bedAtPoint(cliffLineX(z, p) - 2, heightAt(x, z) + 11, z).index;
      const R = Math.hypot(half[0], half[1], half[2]) + 2;
      volume._nodesInSphere(c, R, (idx, px, py, pz) => {
        volume.sdf[idx] = smin(volume.sdf[idx], roundedBoxDistance(px, py, pz, c, half, rr, cosA, sinA), 0.22);
        if (volume.sdf[idx] < 0.3) {
          volume.bed[idx] = sourceBed; // the block keeps the bed it fell from
          volume.sediment[idx] = Math.max(volume.sediment[idx], 0.22);
        }
      });
    }
  }

  // ---- clip to a diorama block so the patch has closed, strata-showing sides
  await report('Clipping patch block', 0.7);
  const m = 1.0;
  for (let iz = 0; iz < nz; iz++) for (let iy = 0; iy < ny; iy++) for (let ix = 0; ix < nx; ix++) {
    const x = volume.nodeX(ix), y = volume.nodeY(iy), z = volume.nodeZ(iz);
    const boxD = Math.max(
      (volume.origin[0] + m) - x, x - (volume.origin[0] + volume.sizeX - m),
      (volume.origin[2] + m) - z, z - (volume.origin[2] + volume.sizeZ - m),
      1.0 - y);
    const idx = volume.index(ix, iy, iz);
    volume.sdf[idx] = Math.max(volume.sdf[idx], boxD);
  }

  await report('Redistancing', 0.85);
  volume.redistance();
  volume.sediment.fill(0);
  volume.flow.fill(0); volume.moisture.fill(0); volume.suitability.fill(0);
  volume.markAllDirty();
  await report('Scene ready', 1);
}
