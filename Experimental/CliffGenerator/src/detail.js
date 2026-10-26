// Surface detail shared by the heightfield mesh and the true-3D SDF chunks: fbm relief, rocky
// facets (Gaea "Rocky"-style angular blocks) and crags (larger joint-bounded blocks that break a
// cliff face into rugged volumes). Both evaluate the same functions, so the detail continues
// across the seam between the two representations.

import { GradientNoise3 } from './noise.js';

export function makeDetail(field, v, opts = {}) {
  const amp = v.detailRelief || 0;
  const scale = Math.max(0.5, v.detailScale || 6);
  const cliffBias = v.detailCliffBias == null ? 0.8 : v.detailCliffBias;
  // rocky facets (Gaea "Rocky"-style): angular, joint-bounded blocks on steep hard rock
  const rocky = v.rockyAmount || 0;
  // feature sizes are clamped to what the representation can resolve (voxels / grid cells)
  const minScale = opts.minScale || 0.5;
  const rockyScale = Math.max(minScale, v.rockyScale || 3);
  const rockyAngular = v.rockyAngular == null ? 0.7 : v.rockyAngular;
  // crags: bigger blocks bounded by the joint sets, only on steep hard rock
  const crag = v.cragAmount || 0;
  const cragScale = Math.max(minScale, v.cragScale || 12);
  const cragAngular = Math.min(1, rockyAngular + 0.15);
  if (amp <= 0 && rocky <= 0 && crag <= 0) return { amp: 0, at: () => 0 };
  const noise = new GradientNoise3((v.seed || 1) * 31 + 11);
  const seed = ((v.seed || 1) * 7919) >>> 0;
  const cell = field.worldSize / (field.resolution - 1);
  const limit = opts.limit || cell * 0.9;
  // cellular field: nearest jittered feature point in the 2×2×2 cells around p; the metric blends
  // Euclidean (cones) with Chebyshev (boxes) → pyramids / blocks
  const facet = (px, py, pz, angular) => {
    const bx = Math.floor(px - 0.5), by = Math.floor(py - 0.5), bz = Math.floor(pz - 0.5);
    let best = 4;
    for (let k = 0; k < 8; k++) {
      const ix = bx + (k & 1), iy = by + ((k >> 1) & 1), iz = bz + (k >> 2);
      const h = hash3(ix, iy, iz, seed);
      const fx = ix + 0.5 + ((h & 1023) / 1023 - 0.5) * 0.8, fy = iy + 0.5 + (((h >> 10) & 1023) / 1023 - 0.5) * 0.8, fz = iz + 0.5 + (((h >> 20) & 1023) / 1023 - 0.5) * 0.8;
      const dx = Math.abs(px - fx), dy = Math.abs(py - fy), dz = Math.abs(pz - fz);
      const eu = Math.sqrt(dx * dx + dy * dy + dz * dz), ch = Math.max(dx, dy, dz);
      const d = eu + (ch - eu) * angular;
      if (d < best) best = d;
    }
    return best;
  };
  return {
    amp: amp + rocky + crag,
    at(x, y, z, ny, hardness) {
      const steep = Math.min(1, Math.max(0, (1 - ny - 0.2) / 0.4));
      const weight = (1 - cliffBias) + cliffBias * steep;
      if (weight <= 0.001) return 0;
      let d = 0;
      if (amp > 0) {
        const n = noise.fbm(x / scale, y / scale, z / scale, 3, 2.1, 0.55);
        // harder beds knobbly, softer beds smoother
        d += n * amp * weight * (0.6 + 0.6 * hardness);
      }
      if (rocky > 0 && steep > 0) {
        // blocks are wider than tall (bedding) and only hard rock is blocky; soft beds stay smooth
        const f = facet(x / (rockyScale * 1.4), y / rockyScale, z / (rockyScale * 1.4), rockyAngular);
        const block = Math.max(0, 0.75 - f) / 0.75; // 1 at the block centre → 0 at the joints
        d += (block - 0.45) * rocky * steep * (0.25 + 0.75 * hardness);
      }
      if (crag > 0 && steep > 0) {
        // crags: wide blocks with a plateau top and open joints between them; every other block
        // is set back so the face is a stack of protruding and recessed masses
        const f = facet(x / (cragScale * 1.3) + 17.3, y / cragScale - 5.1, z / (cragScale * 1.3) + 9.7, cragAngular);
        const plateau = Math.min(1, Math.max(0, (0.8 - f) / 0.35));
        const cx = Math.floor(x / (cragScale * 1.3) + 17.3), cy = Math.floor(y / cragScale - 5.1), cz = Math.floor(z / (cragScale * 1.3) + 9.7);
        const setback = ((hash3(cx, cy, cz, seed ^ 0x9e37) >>> 8) & 1023) / 1023; // per-block protrusion
        d += (plateau * (0.35 + 0.65 * setback) - 0.4) * crag * steep * (0.3 + 0.7 * hardness);
      }
      return Math.max(-limit, Math.min(limit, d));
    },
  };
}

function hash3(x, y, z, seed) {
  let h = (Math.imul(x | 0, 374761393) ^ Math.imul(y | 0, 668265263) ^ Math.imul(z | 0, 2147483647) ^ seed) >>> 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0;
  return (h ^ (h >>> 16)) >>> 0;
}

