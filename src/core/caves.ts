// ---------------------------------------------------------------------------
// Frontier core / cave & carve volumes
// Voids are kept as their OWN narrow-band SDF (negative inside the void).
// The combiner does  ground = max(ground, -void)  which is a sharp boolean:
// cave walls and ceilings keep exact distances, so no blur, ever.
// ---------------------------------------------------------------------------

import { SDFVolume } from './volume';
import { voronoi3, simplex3, fbm2 } from './noise';
import { reinitBand } from './reinit';

export interface CaveParams {
  seed: number;
  tunnelScale: number;    // metres between tunnel centres
  tunnelRadius: number;   // metres
  worminess: number;      // 0..1 warp of the cellular field
  depthBias: number;      // 0..1 favour caves below mid-height
  coverage: number;       // 0..1 threshold on the cellular field
  verticality: number;    // 0..1 stretch tunnels vertically (shafts)
}

export const CAVE_DEFAULTS: CaveParams = {
  seed: 7,
  tunnelScale: 90,
  tunnelRadius: 9,
  worminess: 0.55,
  depthBias: 0.55,
  coverage: 0.5,
  verticality: 0.25,
};

/**
 * Worley-tunnel cave void: distance to cellular edges forms a connected
 * tunnel network; thresholding the F2-F1 field gives tube walls.
 */
export function buildCaveVolume(vol: SDFVolume, p: CaveParams): SDFVolume {
  const out = new SDFVolume(vol.nx, vol.ny, vol.nz, vol.h);
  out.fillEmpty();
  const { nx, ny, nz, h } = out;
  const worldY = ny * h;
  const sy = 1 - p.verticality * 0.7;

  for (let z = 0; z < nz; z++) {
    for (let y = 0; y < ny; y++) {
      const wy = y * h;
      // depth bias: caves concentrate below a fraction of terrain height
      const depthT = 1 - Math.min(1, wy / (worldY * 0.75));
      const bias = Math.pow(depthT, 0.6 + p.depthBias * 2.2);
      for (let x = 0; x < nx; x++) {
        const fx = (x * h) / p.tunnelScale;
        const fy = (wy * sy) / p.tunnelScale;
        const fz = (z * h) / p.tunnelScale;
        const w = p.worminess * 0.6;
        const wxp = fx + w * simplex3(fx * 1.7, fy * 1.7, fz * 1.7, p.seed + 101);
        const wyp = fy + w * simplex3(fx * 1.7 + 31, fy * 1.7, fz * 1.7, p.seed + 202);
        const wzp = fz + w * simplex3(fx * 1.7, fy * 1.7 + 17, fz * 1.7, p.seed + 303);
        const v = voronoi3(wxp, wyp, wzp, p.seed);
        const edge = (v.f2 - v.f1);                    // 0 on cell walls
        const tube = (edge * p.tunnelScale * 0.9) - p.tunnelRadius; // metres-ish
        const gate = bias * (0.55 + 0.45 * fbm2(x * 0.02, z * 0.02, { octaves: 3, lacunarity: 2.1, gain: 0.5, frequency: 1, seed: p.seed + 55 }));
        const d = gate > p.coverage * 0.55 ? tube : out.band;
        if (d < out.get(x, y, z)) out.set(x, y, z, d);
      }
    }
  }
  out.saturate();
  reinitBand(out, 1.5, 1);
  return out;
}

/** analytic carve primitives expressed directly as void volumes */
export function carvePrimitive(vol: SDFVolume, kind: 'sphere' | 'box' | 'tube', cx: number, cy: number, cz: number, r: [number, number, number]): SDFVolume {
  const out = new SDFVolume(vol.nx, vol.ny, vol.nz, vol.h);
  out.fillEmpty();
  const { nx, ny, nz, h } = out;
  for (let z = 0; z < nz; z++) {
    for (let y = 0; y < ny; y++) {
      for (let x = 0; x < nx; x++) {
        const px = x * h, py = y * h, pz = z * h;
        let d: number;
        if (kind === 'sphere') {
          d = Math.sqrt((px - cx) ** 2 + (py - cy) ** 2 + (pz - cz) ** 2) - r[0];
        } else if (kind === 'box') {
          const qx = Math.abs(px - cx) - r[0], qy = Math.abs(py - cy) - r[1], qz = Math.abs(pz - cz) - r[2];
          const ax = Math.max(qx, 0), ay = Math.max(qy, 0), az = Math.max(qz, 0);
          d = Math.sqrt(ax * ax + ay * ay + az * az) + Math.min(Math.max(qx, Math.max(qy, qz)), 0);
        } else {
          // horizontal tube along X at (cy, cz)
          d = Math.hypot(py - cy, pz - cz) - r[0];
        }
        if (d < out.get(x, y, z)) out.set(x, y, z, d);
      }
    }
  }
  out.saturate();
  reinitBand(out, 1.5, 1);
  return out;
}
