// Procedural limestone cave: signed density field + Naive Surface Nets polygonisation.
// Units are centimetres (the tarantula is modelled at true scale, ~14 cm leg span).
// Pure JS (no three.js) so it can run in a Web Worker.

import { Simplex3, mulberry32 } from '../core/noise.js';

export const CAVE_BOUNDS = { min: [-104, -14, -104], max: [104, 90, 128] };

const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
function smin(a, b, k) { const h = clamp(0.5 + 0.5 * (b - a) / k, 0, 1); return b + (a - b) * h - k * h * (1 - h); }
function smax(a, b, k) { return -smin(-a, -b, k); }
function sdSphere(x, y, z, cx, cy, cz, r) { const dx = x - cx, dy = y - cy, dz = z - cz; return Math.sqrt(dx * dx + dy * dy + dz * dz) - r; }
function sdEllipsoid(x, y, z, cx, cy, cz, rx, ry, rz) {
  const px = (x - cx) / rx, py = (y - cy) / ry, pz = (z - cz) / rz;
  const k0 = Math.sqrt(px * px + py * py + pz * pz);
  const qx = px / rx, qy = py / ry, qz = pz / rz;
  const k1 = Math.sqrt(qx * qx + qy * qy + qz * qz) + 1e-9;
  return (k0 * (k0 - 1)) / k1;
}
function sdCapsule(x, y, z, ax, ay, az, bx, by, bz, ra, rb) {
  const pax = x - ax, pay = y - ay, paz = z - az;
  const bax = bx - ax, bay = by - ay, baz = bz - az;
  const h = clamp((pax * bax + pay * bay + paz * baz) / (bax * bax + bay * bay + baz * baz), 0, 1);
  const dx = pax - bax * h, dy = pay - bay * h, dz = paz - baz * h;
  return Math.sqrt(dx * dx + dy * dy + dz * dz) - (ra + (rb - ra) * h);
}

export function createCaveField(seed = 7) {
  const N = new Simplex3(seed);
  const rnd = mulberry32(seed * 91 + 3);

  // --- hanging speleothems (stalactites) and matching stalagmites -----------
  const chamberTop = (x, z) => {
    const q = 1 - (x / 80) ** 2 - (z / 92) ** 2;
    return q > 0 ? 24 + 44 * Math.sqrt(q) : 24;
  };
  const stalactites = [];
  for (let i = 0; i < 60 && stalactites.length < 30; i++) {
    const x = (rnd() * 2 - 1) * 62, z = (rnd() * 2 - 1) * 72;
    if ((x - 20) ** 2 + (z + 18) ** 2 < 22 ** 2) continue; // keep the sun shaft clear
    const top = chamberTop(x, z) + 6;
    const len = 7 + rnd() * 18, r = 1.6 + rnd() * 3.2;
    stalactites.push([x, top, z, len, r]);
  }
  const stalagmites = [];
  for (let i = 0; i < 12; i++) {
    const s = stalactites[i];
    stalagmites.push([s[0] + (rnd() - 0.5) * 3, -2, s[2] + (rnd() - 0.5) * 3, 3 + rnd() * 9, 2.4 + rnd() * 3.5]);
  }
  const boulders = [
    [26, 3, -34, 15, 9, 12], [-36, 1, -8, 11, 7, 10], [42, 0, 38, 9, 6, 12],
    [-8, -1, 58, 13, 6, 9], [58, 2, -8, 8, 9, 8], [-54, 0, 30, 10, 5, 8],
  ];

  const floorH = (x, z) => -3 + 2.2 * N.fbm(x * 0.018, 0.3, z * 0.018, 3) + 0.8 * N.noise(x * 0.07, 5.1, z * 0.07);

  // Low-frequency rock warping + strata + fine ridges (sampled on a coarser grid by generateCave).
  function detail(x, y, z) {
    let r = 7.5 * N.fbm(x * 0.022, y * 0.022, z * 0.022, 4);
    r += 2.4 * N.fbm(x * 0.07 + 11, y * 0.07, z * 0.07, 3);
    const strata = Math.sin(y * 0.42 + 3.0 * N.noise(x * 0.015, y * 0.01, z * 0.015));
    r += 0.9 * Math.sign(strata) * Math.pow(Math.abs(strata), 0.6);
    r += 0.55 * N.ridged(x * 0.16, y * 0.16, z * 0.16, 2);
    return r;
  }

  // Density: positive inside rock, negative in air. `fl` = floor height, `det` = detail(x,y,z).
  function shapes(x, y, z, fl, det) {
    let air = sdEllipsoid(x, y, z, 0, 26, 0, 82, 44, 94);
    air = smin(air, sdCapsule(x, y, z, 30, 20, 62, 62, 16, 118, 24, 17), 16);   // main tunnel
    air = smin(air, sdSphere(x, y, z, -62, 16, -50, 30), 14);                    // side alcove / retreat
    air = smin(air, sdCapsule(x, y, z, -44, 12, 38, -84, 9, 96, 15, 11), 12);     // low crawl-way
    air = smin(air, sdCapsule(x, y, z, 18, 52, -16, 30, 120, -30, 10, 14), 10);  // sunlight shaft (opens to sky)
    if (air > 19 || air < -17) return air; // noise cannot flip the sign this far from the surface
    air = smax(air, fl - y, 7);
    let rock = air;

    for (let i = 0; i < boulders.length; i++) {
      const b = boulders[i];
      let d = sdEllipsoid(x, y, z, b[0], b[1], b[2], b[3], b[4], b[5]);
      if (d > 8) continue;
      d += 2.2 * N.fbm(x * 0.09 + i, y * 0.09, z * 0.09, 2);
      rock = -smin(-rock, d, 3.5);
    }
    // fused column (stalactite + stalagmite) - a climbable pillar
    if (Math.abs(x + 18) < 26 && Math.abs(z - 22) < 26) {
      const cx = -18 + 2.5 * N.noise(y * 0.05, 1.7, 0), cz = 22 + 2.5 * N.noise(y * 0.05, 8.1, 0);
      const t = clamp((y + 2) / 70, 0, 1);
      const r = 5.2 + 7.5 * Math.pow(Math.abs(t - 0.45) * 2, 2.2) + 1.2 * N.noise(x * 0.3, y * 0.12, z * 0.3);
      rock = -smin(-rock, Math.hypot(x - cx, z - cz) - r, 5);
    }
    for (let i = 0; i < stalactites.length; i++) {
      const s = stalactites[i];
      const dy = s[1] - y; if (dy < -2 || dy > s[3] + 10) continue;
      const dx = x - s[0], dz = z - s[2]; if (dx * dx + dz * dz > 144) continue;
      const t = clamp(dy / (s[3] + 6), 0, 1);
      const r = s[4] * Math.pow(1 - t, 1.35) + 0.25;
      rock = -smin(-rock, Math.sqrt(dx * dx + dz * dz) - r, 2.2);
    }
    for (let i = 0; i < stalagmites.length; i++) {
      const s = stalagmites[i];
      const dy = y - s[1]; if (dy < -4 || dy > s[3] + 4) continue;
      const dx = x - s[0], dz = z - s[2]; if (dx * dx + dz * dz > 144) continue;
      const t = clamp(dy / s[3], 0, 1);
      const r = s[4] * (1 - t * t) + 0.3;
      rock = -smin(-rock, Math.max(Math.sqrt(dx * dx + dz * dz) - r, dy - s[3]), 2.5);
    }
    return rock + det;
  }
  const field = (x, y, z) => shapes(x, y, z, floorH(x, z), detail(x, y, z));
  return { field, shapes, detail, floorH, stalactites, stalagmites, boulders };
}

// Surface nets over a sampled grid. Returns typed arrays ready for a BufferGeometry.
export function generateCave(seed = 7, voxel = 1.0, onProgress) {
  const { shapes, detail, floorH } = createCaveField(seed);
  const [x0, y0, z0] = CAVE_BOUNDS.min, [x1, y1, z1] = CAVE_BOUNDS.max;
  const nx = Math.ceil((x1 - x0) / voxel) + 1, ny = Math.ceil((y1 - y0) / voxel) + 1, nz = Math.ceil((z1 - z0) / voxel) + 1;
  const F = new Float32Array(nx * ny * nz);
  const idx = (i, j, k) => i + nx * (j + ny * k);

  // coarse detail grid (2 voxels per cell, trilinear upsampled) - the expensive noise part
  const cs = 2, cnx = Math.ceil((nx - 1) / cs) + 2, cny = Math.ceil((ny - 1) / cs) + 2, cnz = Math.ceil((nz - 1) / cs) + 2;
  const D = new Float32Array(cnx * cny * cnz);
  for (let k = 0; k < cnz; k++) {
    for (let j = 0; j < cny; j++) for (let i = 0; i < cnx; i++)
      D[i + cnx * (j + cny * k)] = detail(x0 + i * cs * voxel, y0 + j * cs * voxel, z0 + k * cs * voxel);
    if (onProgress && (k & 3) === 0) onProgress(0.35 * k / cnz);
  }
  const FL = new Float32Array(nx * nz);
  for (let k = 0; k < nz; k++) for (let i = 0; i < nx; i++) FL[i + nx * k] = floorH(x0 + i * voxel, z0 + k * voxel);

  for (let k = 0; k < nz; k++) {
    const z = z0 + k * voxel;
    const ck = Math.floor(k / cs), tz = (k - ck * cs) / cs;
    for (let j = 0; j < ny; j++) {
      const y = y0 + j * voxel;
      const cj = Math.floor(j / cs), ty = (j - cj * cs) / cs;
      for (let i = 0; i < nx; i++) {
        const ci = Math.floor(i / cs), tx = (i - ci * cs) / cs;
        const b = ci + cnx * (cj + cny * ck);
        const c000 = D[b], c100 = D[b + 1], c010 = D[b + cnx], c110 = D[b + cnx + 1];
        const b2 = b + cnx * cny;
        const c001 = D[b2], c101 = D[b2 + 1], c011 = D[b2 + cnx], c111 = D[b2 + cnx + 1];
        const e0 = c000 + (c100 - c000) * tx, e1 = c010 + (c110 - c010) * tx, e2 = c001 + (c101 - c001) * tx, e3 = c011 + (c111 - c011) * tx;
        const f0 = e0 + (e1 - e0) * ty, f1 = e2 + (e3 - e2) * ty;
        const det = f0 + (f1 - f0) * tz;
        let v = shapes(x0 + i * voxel, y, z, FL[i + nx * k], det);
        // seal the domain everywhere except where the sun shaft leaves through the top
        if (i === 0 || i === nx - 1 || k === 0 || k === nz - 1 || j === 0) v = Math.max(v, 1);
        F[idx(i, j, k)] = v;
      }
    }
    if (onProgress && (k & 7) === 0) onProgress(0.35 + 0.45 * k / nz);
  }

  const sample = (x, y, z) => {
    let fx = (x - x0) / voxel, fy = (y - y0) / voxel, fz = (z - z0) / voxel;
    fx = clamp(fx, 0, nx - 1.001); fy = clamp(fy, 0, ny - 1.001); fz = clamp(fz, 0, nz - 1.001);
    const i = fx | 0, j = fy | 0, k = fz | 0, tx = fx - i, ty = fy - j, tz = fz - k;
    const c000 = F[idx(i, j, k)], c100 = F[idx(i + 1, j, k)], c010 = F[idx(i, j + 1, k)], c110 = F[idx(i + 1, j + 1, k)];
    const c001 = F[idx(i, j, k + 1)], c101 = F[idx(i + 1, j, k + 1)], c011 = F[idx(i, j + 1, k + 1)], c111 = F[idx(i + 1, j + 1, k + 1)];
    const a = c000 + (c100 - c000) * tx, b = c010 + (c110 - c010) * tx, c = c001 + (c101 - c001) * tx, d = c011 + (c111 - c011) * tx;
    const e = a + (b - a) * ty, f = c + (d - c) * ty;
    return e + (f - e) * tz;
  };

  // --- vertices: one per sign-changing cell --------------------------------
  const cellIndex = new Int32Array((nx - 1) * (ny - 1) * (nz - 1)).fill(-1);
  const cidx = (i, j, k) => i + (nx - 1) * (j + (ny - 1) * k);
  const pos = [];
  const cornerOff = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0], [0, 0, 1], [1, 0, 1], [0, 1, 1], [1, 1, 1]];
  const edges = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
  const cv = new Float32Array(8);
  let vcount = 0;
  for (let k = 0; k < nz - 1; k++) {
    for (let j = 0; j < ny - 1; j++) {
      for (let i = 0; i < nx - 1; i++) {
        let mask = 0;
        for (let c = 0; c < 8; c++) {
          const o = cornerOff[c];
          const v = F[idx(i + o[0], j + o[1], k + o[2])];
          cv[c] = v; if (v > 0) mask |= 1 << c;
        }
        if (mask === 0 || mask === 255) continue;
        let sx = 0, sy = 0, sz = 0, n = 0;
        for (let e = 0; e < 12; e++) {
          const a = edges[e][0], b = edges[e][1];
          const va = cv[a], vb = cv[b];
          if ((va > 0) === (vb > 0)) continue;
          const t = va / (va - vb);
          const oa = cornerOff[a], ob = cornerOff[b];
          sx += oa[0] + (ob[0] - oa[0]) * t; sy += oa[1] + (ob[1] - oa[1]) * t; sz += oa[2] + (ob[2] - oa[2]) * t; n++;
        }
        pos.push(x0 + (i + sx / n) * voxel, y0 + (j + sy / n) * voxel, z0 + (k + sz / n) * voxel);
        cellIndex[cidx(i, j, k)] = vcount++;
      }
    }
    if (onProgress && (k & 15) === 0) onProgress(0.8 + 0.1 * k / nz);
  }

  // --- faces: one quad per sign-changing grid edge ---------------------------
  const tris = [];
  for (let k = 1; k < nz - 1; k++) {
    for (let j = 1; j < ny - 1; j++) {
      for (let i = 1; i < nx - 1; i++) {
        const v0 = F[idx(i, j, k)] > 0;
        // x-edge
        if (i < nx - 1 && (F[idx(i + 1, j, k)] > 0) !== v0) {
          const a = cellIndex[cidx(i, j - 1, k - 1)], b = cellIndex[cidx(i, j, k - 1)], c = cellIndex[cidx(i, j, k)], d = cellIndex[cidx(i, j - 1, k)];
          if (a >= 0 && b >= 0 && c >= 0 && d >= 0) v0 ? tris.push(a, b, c, a, c, d) : tris.push(a, c, b, a, d, c);
        }
        if (j < ny - 1 && (F[idx(i, j + 1, k)] > 0) !== v0) {
          const a = cellIndex[cidx(i - 1, j, k - 1)], b = cellIndex[cidx(i, j, k - 1)], c = cellIndex[cidx(i, j, k)], d = cellIndex[cidx(i - 1, j, k)];
          if (a >= 0 && b >= 0 && c >= 0 && d >= 0) v0 ? tris.push(a, c, b, a, d, c) : tris.push(a, b, c, a, c, d);
        }
        if (k < nz - 1 && (F[idx(i, j, k + 1)] > 0) !== v0) {
          const a = cellIndex[cidx(i - 1, j - 1, k)], b = cellIndex[cidx(i, j - 1, k)], c = cellIndex[cidx(i, j, k)], d = cellIndex[cidx(i - 1, j, k)];
          if (a >= 0 && b >= 0 && c >= 0 && d >= 0) v0 ? tris.push(a, b, c, a, c, d) : tris.push(a, c, b, a, d, c);
        }
      }
    }
  }

  // --- normals from the density gradient; AO / cavity from the field ------------
  const positions = new Float32Array(pos);
  const normals = new Float32Array(pos.length);
  const colors = new Float32Array(pos.length);
  const h = voxel * 1.25;
  const aoD = [1.2, 3, 7, 15, 28];
  for (let v = 0; v < vcount; v++) {
    const x = positions[v * 3], y = positions[v * 3 + 1], z = positions[v * 3 + 2];
    let gx = sample(x + h, y, z) - sample(x - h, y, z);
    let gy = sample(x, y + h, z) - sample(x, y - h, z);
    let gz = sample(x, y, z + h) - sample(x, y, z - h);
    const gl = Math.hypot(gx, gy, gz) || 1;
    gx = -gx / gl; gy = -gy / gl; gz = -gz / gl; // point into the air
    normals[v * 3] = gx; normals[v * 3 + 1] = gy; normals[v * 3 + 2] = gz;
    let occ = 0, wsum = 0;
    for (let s = 0; s < aoD.length; s++) {
      const d = aoD[s];
      const r = sample(x + gx * d, y + gy * d, z + gz * d);
      const w = 1 / (1 + s * 0.6);
      occ += w * clamp((d + r) / d, 0, 1); wsum += w;
    }
    const ao = clamp(1 - occ / wsum * 1.35, 0.04, 1);
    // cavity: small-scale concavity (where dust/moisture collects)
    const cav = clamp(0.5 + 0.35 * (sample(x + gx * 0.8, y + gy * 0.8, z + gz * 0.8) + 0.8), 0, 1);
    colors[v * 3] = ao; colors[v * 3 + 1] = cav; colors[v * 3 + 2] = 0;
  }
  if (onProgress) onProgress(1);
  const index = vcount > 65535 ? new Uint32Array(tris) : new Uint16Array(tris);
  return { positions, normals, colors, index, vcount, tcount: tris.length / 3 };
}
