/**
 * Material table shared by the GPU renderer and the headless probes.
 *
 * MAT_ORDER is the vertex material index space; each entry carries an albedo, a
 * roughness and a "style" selector the surface shader uses to pick its procedural
 * detail (asphalt aggregate, slab joints, paving bonds, grass clumps, ...).
 */

import { MAT_ORDER, type MatKind } from '../core/mesh';

export interface MatDef {
  /** linear-ish base colour 0..1 */
  color: [number, number, number];
  roughness: number;
  style: number;
}

export const MAT_STYLE = {
  asphalt: 0,
  concrete: 1,
  paving: 2,
  cast: 3,
  soft: 4,
  hard: 5,
} as const;

export const MAT_DEFS: Record<MatKind, MatDef> = {
  // linear-space albedo, roughness and procedural style.
  // Asphalt is genuinely dark (0.02-0.04 linear); concrete and kerbs sit near
  // 0.2; planting near 0.05. Values here are what make the frame read correctly
  // through the filmic curve.
  asphalt: { color: [0.022, 0.022, 0.024], roughness: 0.8, style: MAT_STYLE.asphalt },
  'asphalt-worn': { color: [0.036, 0.035, 0.033], roughness: 0.88, style: MAT_STYLE.asphalt },
  'asphalt-fresh': { color: [0.014, 0.014, 0.016], roughness: 0.62, style: MAT_STYLE.asphalt },
  concrete: { color: [0.19, 0.185, 0.175], roughness: 0.74, style: MAT_STYLE.concrete },
  'concrete-panel': { color: [0.15, 0.15, 0.147], roughness: 0.76, style: MAT_STYLE.concrete },
  pavers: { color: [0.12, 0.055, 0.042], roughness: 0.82, style: MAT_STYLE.paving },
  'pavers-mixed': { color: [0.17, 0.15, 0.13], roughness: 0.8, style: MAT_STYLE.paving },
  slabs: { color: [0.22, 0.215, 0.205], roughness: 0.72, style: MAT_STYLE.concrete },
  cobble: { color: [0.085, 0.082, 0.078], roughness: 0.86, style: MAT_STYLE.paving },
  tactile: { color: [0.3, 0.21, 0.045], roughness: 0.7, style: MAT_STYLE.cast },
  gravel: { color: [0.13, 0.12, 0.11], roughness: 0.92, style: MAT_STYLE.soft },
  soil: { color: [0.032, 0.026, 0.019], roughness: 0.95, style: MAT_STYLE.soft },
  grass: { color: [0.045, 0.075, 0.028], roughness: 0.9, style: MAT_STYLE.soft },
  ballast: { color: [0.075, 0.072, 0.07], roughness: 0.95, style: MAT_STYLE.soft },
  steel: { color: [0.28, 0.3, 0.33], roughness: 0.35, style: MAT_STYLE.hard },
  paint: { color: [0.72, 0.72, 0.7], roughness: 0.5, style: MAT_STYLE.hard },
  wood: { color: [0.08, 0.05, 0.028], roughness: 0.85, style: MAT_STYLE.hard },
  rubber: { color: [0.012, 0.012, 0.013], roughness: 0.94, style: MAT_STYLE.hard },
  water: { color: [0.012, 0.022, 0.03], roughness: 0.06, style: MAT_STYLE.hard },
  kerb: { color: [0.24, 0.235, 0.225], roughness: 0.7, style: MAT_STYLE.cast },
  'road-plate': { color: [0.05, 0.052, 0.058], roughness: 0.4, style: MAT_STYLE.hard },
};

/** 32 × vec4 albedo + params, laid out for the shader's uniform array. */
export function materialTable(): Float32Array {
  const data = new Float32Array(32 * 8);
  MAT_ORDER.forEach((kind, i) => {
    const def = MAT_DEFS[kind] ?? MAT_DEFS.asphalt;
    data[i * 8] = def.color[0];
    data[i * 8 + 1] = def.color[1];
    data[i * 8 + 2] = def.color[2];
    data[i * 8 + 3] = 1;
    data[i * 8 + 4] = def.roughness;
    data[i * 8 + 5] = def.style;
  });
  return data;
}

/** Debug / software-raster colour (0-255). */
export function matColor(kind: string): [number, number, number] {
  const def = (MAT_DEFS as Record<string, MatDef>)[kind] ?? MAT_DEFS.asphalt;
  return [def.color[0] * 255, def.color[1] * 255, def.color[2] * 255];
}

export { MAT_ORDER };
export type { MatKind };
