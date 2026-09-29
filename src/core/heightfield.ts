// ---------------------------------------------------------------------------
// Frontier core / heightfield bridge
// Erosion solvers are 2.5D (they need a single-valued surface), while the master
// representation stays a narrow-band SDF. The bridge:
//   extractHeight(): top-most zero crossing per column  (caves below survive)
//   applyHeight():   rebuilds the ground field from the eroded surface, then
//                    re-initialises the band so cliffs stay distance-exact.
// Cave voids live in a separate volume, so erosion can never smear them.
// ---------------------------------------------------------------------------

import { SDFVolume } from './volume';
import { reinitBand, verticalRedistance } from './reinit';

export interface HeightField {
  nx: number;
  nz: number;
  h: number;               // voxel size metres
  data: Float32Array;      // surface height in metres (y up)
}

export function makeHeightField(nx: number, nz: number, h: number): HeightField {
  return { nx, nz, h, data: new Float32Array(nx * nz) };
}

export function cloneHeightField(hf: HeightField): HeightField {
  return { nx: hf.nx, nz: hf.nz, h: hf.h, data: new Float32Array(hf.data) };
}

/** top-most surface height per column by scanning the sign change from above */
export function extractHeight(vol: SDFVolume): HeightField {
  const { nx, ny, nz, h } = vol;
  const d = vol.data;
  const hf = makeHeightField(nx, nz, h);
  for (let z = 0; z < nz; z++) {
    for (let x = 0; x < nx; x++) {
      let yTop = 0;
      let prev = d[(z * ny + ny - 1) * nx + x];
      let found = false;
      for (let y = ny - 2; y >= 0; y--) {
        const cur = d[(z * ny + y) * nx + x];
        if (prev > 0 && cur <= 0) {
          // linear interp of zero crossing between y and y+1
          const t = prev / (prev - cur);
          yTop = (y + t) * h;
          found = true;
          break;
        }
        prev = cur;
      }
      if (!found) yTop = prev <= 0 ? ny * h : 0; // fully solid / fully empty column
      hf.data[z * nx + x] = yTop;
    }
  }
  return hf;
}

/** rebuild ground SDF from a heightfield + exact re-distance in the band */
export function applyHeight(vol: SDFVolume, hf: HeightField): void {
  verticalRedistance(vol, hf.data);
  reinitBand(vol, 1.5, 1);
}

/** slope magnitude (rise/run) at a column, central differences */
export function slopeAt(hf: HeightField, x: number, z: number): number {
  const { nx, nz, h, data } = hf;
  const x0 = Math.max(0, x - 1), x1 = Math.min(nx - 1, x + 1);
  const z0 = Math.max(0, z - 1), z1 = Math.min(nz - 1, z + 1);
  const dx = (data[z * nx + x1] - data[z * nx + x0]) / ((x1 - x0) * h || 1);
  const dz = (data[z1 * nx + x] - data[z0 * nx + x]) / ((z1 - z0) * h || 1);
  return Math.hypot(dx, dz);
}

/** laplacian curvature (convex +, concave -) */
export function curvatureAt(hf: HeightField, x: number, z: number): number {
  const { nx, nz, h, data } = hf;
  const c = data[z * nx + x];
  const l = data[z * nx + Math.max(0, x - 1)];
  const r = data[z * nx + Math.min(nx - 1, x + 1)];
  const u = data[Math.max(0, z - 1) * nx + x];
  const d = data[Math.min(nz - 1, z + 1) * nx + x];
  return (l + r + u + d - 4 * c) / (h * h);
}
