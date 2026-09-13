// ============================================================================
// Frontier SDF Terrain — Volumetric SDF field + attribute volumes.
//
// Core principle: there is NO heightmap anywhere in this engine. The terrain is
// a true 3D signed distance field (solid < 0, air > 0) with per-voxel material
// attributes. Overhangs, arches, caves and cliffs are all representable, and
// every erosion operator works directly on the SDF + attributes in 3D.
//
// Volumes:
//   sdf    Float32Array  signed distance in WORLD units
//   attrA  Uint8Array x4 dynamic : R sediment drape, G wetness/flow trails,
//                                 B loose talus, A rain-emitter paint
//   attrB  Uint8Array x4 material: R hardness, G strata id, B baked cavity,
//                                 A spare (salt/chemical precipitate)
// ============================================================================

export const BRICK = 8; // dirty-tracking granularity (voxels)

export function smin(a, b, k) {
  if (k <= 1e-6) return a < b ? a : b;
  const h = Math.max(0, Math.min(1, 0.5 + 0.5 * (b - a) / k));
  return b * h + a * (1 - h) - k * h * (1 - h);
}
export function smax(a, b, k) {
  if (k <= 1e-6) return a > b ? a : b;
  return -smin(-a, -b, k);
}

export class Volume {
  constructor(N = 128, worldSize = 2) {
    this.N = N;
    this.worldSize = worldSize; // world spans [-worldSize/2, +worldSize/2]
    this.voxel = worldSize / N;
    const n3 = N * N * N;
    this.sdf = new Float32Array(n3);
    this.attrA = new Uint8Array(n3 * 4);
    this.attrB = new Uint8Array(n3 * 4);
    this.sdf.fill(worldSize); // start as all-air
    this.bricksPerSide = Math.ceil(N / BRICK);
    this.dirtyBricks = new Set();
    this.carvedVolume = 0;   // m^3 (world units^3) removed by erosion (net)
    this.depositedVolume = 0;
    this.generation = 0;     // bumped on full regeneration
  }

  get n3() { return this.N * this.N * this.N; }
  idx(i, j, k) { return (k * this.N + j) * this.N + i; }
  inBounds(i, j, k) {
    const N = this.N;
    return i >= 0 && j >= 0 && k >= 0 && i < N && j < N && k < N;
  }
  inBoundsWorld(x, y, z) {
    const h = this.worldSize / 2;
    return x >= -h && x <= h && y >= -h && y <= h && z >= -h && z <= h;
  }

  // voxel center -> world
  gridToWorld(i, j, k, out) {
    const s = this.worldSize / this.N, h = this.worldSize / 2;
    out.x = (i + 0.5) * s - h;
    out.y = (j + 0.5) * s - h;
    out.z = (k + 0.5) * s - h;
    return out;
  }
  // world -> continuous grid coords
  worldToGrid(x, y, z, out) {
    const s = this.worldSize / this.N, h = this.worldSize / 2;
    out.x = (x + h) / s - 0.5;
    out.y = (y + h) / s - 0.5;
    out.z = (z + h) / s - 0.5;
    return out;
  }

  markAllDirty() {
    const B = this.bricksPerSide;
    this.dirtyBricks.clear();
    for (let b = 0; b < B * B * B; b++) this.dirtyBricks.add(b);
  }
  markDirtyBox(i0, j0, k0, i1, j1, k1) {
    const N = this.N, B = this.bricksPerSide;
    i0 = Math.max(0, i0); j0 = Math.max(0, j0); k0 = Math.max(0, k0);
    i1 = Math.min(N - 1, i1); j1 = Math.min(N - 1, j1); k1 = Math.min(N - 1, k1);
    for (let k = (k0 / BRICK) | 0; k <= (k1 / BRICK) | 0; k++)
      for (let j = (j0 / BRICK) | 0; j <= (j1 / BRICK) | 0; j++)
        for (let i = (i0 / BRICK) | 0; i <= (i1 / BRICK) | 0; i++)
          this.dirtyBricks.add((k * B + j) * B + i);
  }
  takeDirtyBricks(max = 4096) {
    const out = [];
    for (const b of this.dirtyBricks) {
      out.push(b);
      if (out.length >= max) break;
    }
    for (const b of out) this.dirtyBricks.delete(b);
    return out;
  }

  // --- sampling ----------------------------------------------------------------
  // Trilinear SDF sample in world coords. Outside volume: positive distance to
  // the box (keeps raymarch/sphere-tracing valid outside).
  sample(x, y, z) {
    const N = this.N, h = this.worldSize / 2;
    if (x < -h || x > h || y < -h || y > h || z < -h || z > h) {
      const dx = Math.max(-h - x, 0, x - h);
      const dy = Math.max(-h - y, 0, y - h);
      const dz = Math.max(-h - z, 0, z - h);
      return Math.sqrt(dx * dx + dy * dy + dz * dz) + 1e-4;
    }
    const gx = (x + h) / this.worldSize * N - 0.5;
    const gy = (y + h) / this.worldSize * N - 0.5;
    const gz = (z + h) / this.worldSize * N - 0.5;
    const x0 = Math.floor(gx), y0 = Math.floor(gy), z0 = Math.floor(gz);
    const fx = gx - x0, fy = gy - y0, fz = gz - z0;
    const x1 = Math.min(N - 1, x0 + 1), y1 = Math.min(N - 1, y0 + 1), z1 = Math.min(N - 1, z0 + 1);
    const X0 = Math.max(0, x0), Y0 = Math.max(0, y0), Z0 = Math.max(0, z0);
    const S = this.sdf;
    const c000 = S[(Z0 * N + Y0) * N + X0], c100 = S[(Z0 * N + Y0) * N + x1];
    const c010 = S[(Z0 * N + y1) * N + X0], c110 = S[(Z0 * N + y1) * N + x1];
    const c001 = S[(z1 * N + Y0) * N + X0], c101 = S[(z1 * N + Y0) * N + x1];
    const c011 = S[(z1 * N + y1) * N + X0], c111 = S[(z1 * N + y1) * N + x1];
    const x00 = c000 + (c100 - c000) * fx, x10 = c010 + (c110 - c010) * fx;
    const x01 = c001 + (c101 - c001) * fx, x11 = c011 + (c111 - c011) * fx;
    const y0v = x00 + (x10 - x00) * fy, y1v = x01 + (x11 - x01) * fy;
    return y0v + (y1v - y0v) * fz;
  }

  sampleNearestIdx(x, y, z) {
    const N = this.N, h = this.worldSize / 2;
    const i = Math.round((x + h) / this.worldSize * N - 0.5);
    const j = Math.round((y + h) / this.worldSize * N - 0.5);
    const k = Math.round((z + h) / this.worldSize * N - 0.5);
    if (!this.inBounds(i, j, k)) return -1;
    return { i, j, k, index: this.idx(i, j, k) };
  }

  // Trilinear attribute sample, returns 0..1. vol = attrA|attrB arrays, ch 0..3.
  sampleAttr(vol, ch, x, y, z) {
    const N = this.N, h = this.worldSize / 2;
    if (x < -h || x > h || y < -h || y > h || z < -h || z > h) return 0;
    const gx = (x + h) / this.worldSize * N - 0.5;
    const gy = (y + h) / this.worldSize * N - 0.5;
    const gz = (z + h) / this.worldSize * N - 0.5;
    const x0 = Math.floor(gx), y0 = Math.floor(gy), z0 = Math.floor(gz);
    const fx = gx - x0, fy = gy - y0, fz = gz - z0;
    const x1 = Math.min(N - 1, x0 + 1), y1 = Math.min(N - 1, y0 + 1), z1 = Math.min(N - 1, z0 + 1);
    const X0 = Math.max(0, x0), Y0 = Math.max(0, y0), Z0 = Math.max(0, z0);
    const at = (X, Y, Z) => vol[((Z * N + Y) * N + X) * 4 + ch] * (1 / 255);
    const c000 = at(X0, Y0, Z0), c100 = at(x1, Y0, Z0);
    const c010 = at(X0, y1, Z0), c110 = at(x1, y1, Z0);
    const c001 = at(X0, Y0, z1), c101 = at(x1, Y0, z1);
    const c011 = at(X0, y1, z1), c111 = at(x1, y1, z1);
    const x00 = c000 + (c100 - c000) * fx, x10 = c010 + (c110 - c010) * fx;
    const x01 = c001 + (c101 - c001) * fx, x11 = c011 + (c111 - c011) * fx;
    return (x00 + (x10 - x00) * fy) * (1 - fz) + (x01 + (x11 - x01) * fy) * fz;
  }

  gradient(x, y, z, out) {
    const e = this.voxel * 0.9;
    out.x = this.sample(x + e, y, z) - this.sample(x - e, y, z);
    out.y = this.sample(x, y + e, z) - this.sample(x, y - e, z);
    out.z = this.sample(x, y, z + e) - this.sample(x, y, z - e);
    const l = Math.hypot(out.x, out.y, out.z) || 1;
    out.x /= l; out.y /= l; out.z /= l;
    return out;
  }

  laplacian(x, y, z) {
    const e = this.voxel * 1.2;
    const c = this.sample(x, y, z);
    return (this.sample(x + e, y, z) + this.sample(x - e, y, z) +
      this.sample(x, y + e, z) + this.sample(x, y - e, z) +
      this.sample(x, y, z + e) + this.sample(x, y, z - e) - 6 * c) / (e * e);
  }

  // --- SDF sculpting stamps ------------------------------------------------------
  // Carve: boolean-subtract (smooth) a solid sphere of air. Returns removed
  // "volume" estimate (world^3) for sediment mass conservation.
  stampCarve(cx, cy, cz, r, polish = 0.35, hardnessScale = 1) {
    if (r <= 0) return 0;
    const N = this.N, h = this.worldSize / 2, vs = this.voxel;
    const R = r * (1 + polish * 0.75);
    const i0 = Math.floor((cx - R + h) / this.worldSize * N);
    const i1 = Math.ceil((cx + R + h) / this.worldSize * N);
    const j0 = Math.floor((cy - R + h) / this.worldSize * N);
    const j1 = Math.ceil((cy + R + h) / this.worldSize * N);
    const k0 = Math.floor((cz - R + h) / this.worldSize * N);
    const k1 = Math.ceil((cz + R + h) / this.worldSize * N);
    const k = R * polish * 0.5;
    let removed = 0;
    const c0x = Math.max(0, i0), c1x = Math.min(N - 1, i1);
    const c0y = Math.max(0, j0), c1y = Math.min(N - 1, j1);
    const c0z = Math.max(0, k0), c1z = Math.min(N - 1, k1);
    if (c1x < c0x || c1y < c0y || c1z < c0z) return 0;
    for (let kk = c0z; kk <= c1z; kk++) {
      const wz = (kk + 0.5) * vs - h;
      for (let jj = c0y; jj <= c1y; jj++) {
        const wy = (jj + 0.5) * vs - h;
        for (let ii = c0x; ii <= c1x; ii++) {
          const wx = (ii + 0.5) * vs - h;
          const dist = Math.sqrt((wx - cx) ** 2 + (wy - cy) ** 2 + (wz - cz) ** 2);
          if (dist > R) continue;
          const id = (kk * N + jj) * N + ii;
          const dOld = this.sdf[id];
          if (dOld > R) continue; // deep air, nothing to remove
          const airSdf = r - dist; // >0 inside carve sphere (air region)
          // hardness resists carving: scale the air-region SDF down for hard rock
          const airEff = airSdf <= 0 ? airSdf : airSdf * hardnessScale;
          const dNew = smax(dOld, airEff, k);
          if (dNew > dOld) {
            this.sdf[id] = dNew;
            // volume bookkeeping: surface advance * voxel face area
            removed += (dNew - dOld) * vs * vs;
          }
        }
      }
    }
    this.markDirtyBox(c0x, c0y, c0z, c1x, c1y, c1z);
    this.carvedVolume += removed;
    return removed;
  }

  // Deposit: boolean-add (smooth) solid. Returns added volume estimate.
  stampDeposit(cx, cy, cz, r, polish = 0.5, amount = 1) {
    if (r <= 0 || amount <= 0) return 0;
    const N = this.N, h = this.worldSize / 2, vs = this.voxel;
    const R = r * (1 + polish * 0.5);
    const i0 = Math.floor((cx - R + h) / this.worldSize * N);
    const i1 = Math.ceil((cx + R + h) / this.worldSize * N);
    const j0 = Math.floor((cy - R + h) / this.worldSize * N);
    const j1 = Math.ceil((cy + R + h) / this.worldSize * N);
    const k0 = Math.floor((cz - R + h) / this.worldSize * N);
    const k1 = Math.ceil((cz + R + h) / this.worldSize * N);
    const k = R * polish * 0.5;
    const c0x = Math.max(0, i0), c1x = Math.min(N - 1, i1);
    const c0y = Math.max(0, j0), c1y = Math.min(N - 1, j1);
    const c0z = Math.max(0, k0), c1z = Math.min(N - 1, k1);
    if (c1x < c0x || c1y < c0y || c1z < c0z) return 0;
    let added = 0;
    for (let kk = c0z; kk <= c1z; kk++) {
      const wz = (kk + 0.5) * vs - h;
      for (let jj = c0y; jj <= c1y; jj++) {
        const wy = (jj + 0.5) * vs - h;
        for (let ii = c0x; ii <= c1x; ii++) {
          const wx = (ii + 0.5) * vs - h;
          const dist = Math.sqrt((wx - cx) ** 2 + (wy - cy) ** 2 + (wz - cz) ** 2);
          if (dist > R) continue;
          const id = (kk * N + jj) * N + ii;
          const dOld = this.sdf[id];
          if (dOld < -R) continue; // deep solid, no-op
          // shrink solid sphere by (1-amount) to allow partial deposition
          const solidSdf = dist - r * amount;
          const dNew = smin(dOld, solidSdf, k);
          if (dNew < dOld) {
            this.sdf[id] = dNew;
            added += (dOld - dNew) * vs * vs;
          }
        }
      }
    }
    this.markDirtyBox(c0x, c0y, c0z, c1x, c1y, c1z);
    this.depositedVolume += added;
    return added;
  }

  // Oriented ellipsoidal carve (impact craters elongated along velocity).
  // dir = unit impact direction (pointing INTO the surface), elong = length/width.
  stampCarveOriented(cx, cy, cz, dx, dy, dz, r, elong = 1.6, polish = 0.3, hardnessScale = 1) {
    if (r <= 0) return 0;
    // build orthonormal frame (u along dir)
    let ux = dx, uy = dy, uz = dz;
    const ul = Math.hypot(ux, uy, uz) || 1;
    ux /= ul; uy /= ul; uz /= ul;
    let vx = -uz, vy = 0, vz = ux;
    let vl = Math.hypot(vx, vy, vz);
    if (vl < 1e-4) { vx = 0; vy = 1; vz = 0; vl = 1; }
    vx /= vl; vy /= vl; vz /= vl;
    const wx = uy * vz - uz * vy, wy = uz * vx - ux * vz, wz = ux * vy - uy * vx;
    const N = this.N, h = this.worldSize / 2, vs = this.voxel;
    const R = r * elong * 1.35;
    const i0 = Math.max(0, Math.floor((cx - R + h) / this.worldSize * N));
    const i1 = Math.min(N - 1, Math.ceil((cx + R + h) / this.worldSize * N));
    const j0 = Math.max(0, Math.floor((cy - R + h) / this.worldSize * N));
    const j1 = Math.min(N - 1, Math.ceil((cy + R + h) / this.worldSize * N));
    const k0 = Math.max(0, Math.floor((cz - R + h) / this.worldSize * N));
    const k1 = Math.min(N - 1, Math.ceil((cz + R + h) / this.worldSize * N));
    const k = r * polish;
    let removed = 0;
    for (let kk = k0; kk <= k1; kk++) {
      const oz = (kk + 0.5) * vs - h - cz;
      for (let jj = j0; jj <= j1; jj++) {
        const oy = (jj + 0.5) * vs - h - cy;
        for (let ii = i0; ii <= i1; ii++) {
          const ox = (ii + 0.5) * vs - h - cx;
          const a = ox * ux + oy * uy + oz * uz;   // along impact dir
          const b = ox * vx + oy * vy + oz * vz;
          const c = ox * wx + oy * wy + oz * wz;
          // crater: hemispherical scoop ahead of impact point + narrow tail
          const along = a / elong;
          const dist = Math.sqrt(along * along + b * b + c * c);
          if (dist > r * 1.35 || a < -r * 0.9) continue;
          const id = (kk * N + jj) * N + ii;
          const dOld = this.sdf[id];
          if (dOld > r) continue;
          const airSdf = (r - dist) * hardnessScale;
          const dNew = smax(dOld, airSdf, k);
          if (dNew > dOld) {
            this.sdf[id] = dNew;
            removed += (dNew - dOld) * vs * vs;
          }
        }
      }
    }
    this.markDirtyBox(i0, j0, k0, i1, j1, k1);
    this.carvedVolume += removed;
    return removed;
  }

  // Paint into an attribute channel with a soft spherical brush (world coords).
  paintAttr(vol, ch, cx, cy, cz, r, value, opacity = 1) {
    const N = this.N, h = this.worldSize / 2, vs = this.voxel;
    const i0 = Math.max(0, Math.floor((cx - r + h) / this.worldSize * N));
    const i1 = Math.min(N - 1, Math.ceil((cx + r + h) / this.worldSize * N));
    const j0 = Math.max(0, Math.floor((cy - r + h) / this.worldSize * N));
    const j1 = Math.min(N - 1, Math.ceil((cy + r + h) / this.worldSize * N));
    const k0 = Math.max(0, Math.floor((cz - r + h) / this.worldSize * N));
    const k1 = Math.min(N - 1, Math.ceil((cz + r + h) / this.worldSize * N));
    const tv = Math.max(0, Math.min(255, Math.round(value * 255)));
    for (let kk = k0; kk <= k1; kk++) {
      const oz = (kk + 0.5) * vs - h - cz;
      for (let jj = j0; jj <= j1; jj++) {
        const oy = (jj + 0.5) * vs - h - cy;
        for (let ii = i0; ii <= i1; ii++) {
          const ox = (ii + 0.5) * vs - h - cx;
          const d = Math.sqrt(ox * ox + oy * oy + oz * oz) / r;
          if (d > 1) continue;
          const fall = 0.5 + 0.5 * Math.cos(d * Math.PI);
          const id = (((kk * N + jj) * N + ii) * 4 + ch);
          const cur = vol[id];
          vol[id] = Math.round(cur + (tv - cur) * fall * opacity);
        }
      }
    }
    this.markDirtyBox(i0, j0, k0, i1, j1, k1);
  }

  // Add (saturating) into an attribute channel — used for flow/sediment trails.
  addAttr(vol, ch, i, j, k, delta) {
    if (!this.inBounds(i, j, k)) return;
    const id = ((k * this.N + j) * this.N + i) * 4 + ch;
    const v = vol[id] + delta * 255;
    vol[id] = v > 255 ? 255 : v < 0 ? 0 : v | 0;
  }

  // Sphere-trace a ray against the SDF. Returns {t, x, y, z, hit}.
  raycast(ox, oy, oz, dx, dy, dz, maxT = 8, out) {
    out = out || {};
    let t = 0;
    for (let i = 0; i < 220; i++) {
      const x = ox + dx * t, y = oy + dy * t, z = oz + dz * t;
      const d = this.sample(x, y, z);
      if (d < this.voxel * 0.35) {
        out.hit = true; out.t = t; out.x = x; out.y = y; out.z = z;
        return out;
      }
      t += Math.max(d * 0.85, this.voxel * 0.4);
      if (t > maxT) break;
    }
    out.hit = false; out.t = maxT;
    out.x = ox + dx * maxT; out.y = oy + dy * maxT; out.z = oz + dz * maxT;
    return out;
  }

  // Fill a horizontal slab / everything below y with solid (for island bases).
  fillBelow(yLevel, hardness = 0.6, strata = 0.5) {
    const N = this.N, h = this.worldSize / 2, vs = this.voxel;
    for (let k = 0; k < N; k++)
      for (let j = 0; j < N; j++) {
        const wy = (j + 0.5) * vs - h;
        const d = wy - yLevel;
        for (let i = 0; i < N; i++) {
          this.sdf[(k * N + j) * N + i] = d;
        }
      }
    const hb = Math.round(hardness * 255), sb = Math.round(strata * 255);
    for (let n = 0; n < this.n3; n++) {
      this.attrB[n * 4] = hb;
      this.attrB[n * 4 + 1] = sb;
    }
    this.markAllDirty();
  }

  clearDynamic() {
    this.attrA.fill(0);
    this.carvedVolume = 0;
    this.depositedVolume = 0;
    this.markAllDirty();
  }
}
