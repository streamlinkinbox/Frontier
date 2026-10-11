/**
 * Portable sheet-metal dents: bake once on a flat steel sheet, stamp anywhere.
 *
 * The per-site bakes in `carrig.ts` are tied to one spot on one body, which is
 * right for structural crush (a front-end crumple depends on where the rails
 * and the bumper beam are) but wrong for ordinary panel damage. On a panel, a
 * dent is *translation invariant*: hit the middle of a door or the middle of a
 * roof with the same object at the same speed and you get the same dent, just
 * somewhere else.
 *
 * So the dent is solved ONCE, on a flat clamped steel sheet, in its own
 * tangent frame, and stored as a displacement-map sequence:
 *
 *     dentAtlas[type][frame][v][u]  ->  (du, dv, dn, plastic strain)
 *
 * At runtime a dent instance is just a position, a tangent frame, a scale, a
 * type and a cursor along the damage sequence. The vertex shader projects each
 * vertex into the instance's frame, samples the map bilinearly and adds the
 * displacement. That gives arbitrary hit positions, arbitrary orientation and
 * unlimited dents from a 1-2 MB bake, on any mesh - no per-asset bake at all.
 *
 * This is the piece you would actually ship on vehicles: one steel-sheet
 * library (a few impactor shapes x a few angles), stamped wherever the
 * collision says something hit the bodywork.
 */

import { v3, clamp } from '../core/math';
import { ShellTopo, bakeDent, DentSite } from './dent';

export interface DentType {
  label: string;
  /** impactor radius as a fraction of the patch half-size */
  radius: number;
  /** full-damage depth [m] */
  depth: number;
  /** patch half-size [m] - the footprint the map covers */
  half: number;
  shape?: 'sphere' | 'barrier';
  /** barrier only: half-width of the contact bar */
  span?: number;
  /** barrier only: how elongated the bar is (1 = round punch, 3 = a beam) */
  elong?: number;
  /** in-plane draw-in as a fraction of depth - see DentSite.drawIn */
  drawIn?: number;
}

export interface DentAtlas {
  res: number;
  frames: number;
  types: DentType[];
  /** frames*res*res per type, RGBA = (du, dv, dn, strain) */
  pos: Float32Array;
  /** frames*res*res per type, RGBA = local-frame normal */
  nrm: Float32Array;
  ms: number;
}

/** Flat welded grid with a pinned rim, used as the bake substrate. */
function sheetTopo(half: number, res: number): { topo: ShellTopo; n: number } {
  const n = res * res;
  const x0 = new Float32Array(n * 3);
  for (let j = 0; j < res; j++) {
    for (let i = 0; i < res; i++) {
      const k = j * res + i;
      x0[k * 3] = (i / (res - 1) - 0.5) * 2 * half;
      x0[k * 3 + 1] = (j / (res - 1) - 0.5) * 2 * half;
      x0[k * 3 + 2] = 0;
    }
  }
  const tris: number[] = [];
  for (let j = 0; j < res - 1; j++) {
    for (let i = 0; i < res - 1; i++) {
      const a = j * res + i, b = a + 1, c = a + res + 1, d = a + res;
      tris.push(a, b, c, a, c, d);
    }
  }
  const edge: number[] = [], bend: number[] = [];
  for (let j = 0; j < res; j++) {
    for (let i = 0; i < res; i++) {
      const a = j * res + i;
      if (i + 1 < res) edge.push(a, a + 1);
      if (j + 1 < res) edge.push(a, a + res);
      // diagonals keep the quad grid from shearing into nothing
      if (i + 1 < res && j + 1 < res) edge.push(a, a + res + 1);
      // Bending pairs at SEVERAL scales. A single two-step stencil only
      // resists curvature over two cells, so the cheapest buckling mode is a
      // wrinkle every couple of cells and the sheet turns into corduroy. Real
      // plate bending resists curvature at every wavelength, and it is the
      // long-wavelength part that decides how big the facets are, so the
      // stencil has to span several cells too.
      if (i + 2 < res) bend.push(a, a + 2);
      if (j + 2 < res) bend.push(a, a + 2 * res);
      // diagonal hinges, so a fold is not forced to run along a grid axis
      if (i + 2 < res && j + 2 < res) bend.push(a, a + 2 * res + 2);
      if (i >= 2 && j + 2 < res) bend.push(a, a + 2 * res - 2);
    }
  }
  const dist = (p: number, q: number) => Math.hypot(
    x0[p * 3] - x0[q * 3], x0[p * 3 + 1] - x0[q * 3 + 1], x0[p * 3 + 2] - x0[q * 3 + 2]);
  const edgeRest = new Float32Array(edge.length / 2);
  for (let i = 0; i < edgeRest.length; i++) edgeRest[i] = dist(edge[i * 2], edge[i * 2 + 1]);
  const bendRest = new Float32Array(bend.length / 2);
  for (let i = 0; i < bendRest.length; i++) bendRest[i] = dist(bend[i * 2], bend[i * 2 + 1]);
  return {
    topo: {
      n, x0, tris: new Uint32Array(tris),
      edge: new Uint32Array(edge), edgeRest,
      bend: new Uint32Array(bend), bendRest,
    },
    n,
  };
}

/** The shipped dent library. Impactor size is what really changes the shape. */
export const DENT_TYPES: DentType[] = [
  // A low-energy hit does not collapse anything around it, so no material is
  // fed in and the panel really does just dish. This one is SUPPOSED to be
  // smooth - it is a trolley, a knee, a hailstone.
  { label: 'dish (trolley, knee, hail)', radius: 0.19, depth: 0.055, half: 0.40, drawIn: 0 },
  // A pole or another car's corner: the structure behind it gives, the panel
  // is fed inward, and it crumples into facets around the contact.
  { label: 'crush (pole, car corner)', radius: 0.13, depth: 0.075, half: 0.36, drawIn: 0.34 },
  // A beam-shaped contact drawn in hard: a long buckle with sharp fold lines
  // running off it, which is the bonnet in every front-end crash photo.
  {
    label: 'fold (beam, bonnet buckle)', radius: 0.10, depth: 0.062, half: 0.44,
    shape: 'barrier', span: 0.085, elong: 3.2, drawIn: 0.75,
  },
];

/**
 * Run the plastic solve for each dent type on a flat sheet and pack the
 * results into displacement maps.
 *
 * The resolution is deliberately coarse (~2 cm cells). Crumpled sheet is made
 * of nearly FLAT facets joined by narrow ridges, so it is piecewise linear,
 * and a piecewise-linear surface is represented EXACTLY on a coarse grid and
 * reconstructed exactly by the bilinear fetch in the shader. Going finer does
 * not buy sharper creases, it just lets the solver find shorter-wavelength
 * buckling modes, and the panel turns to corduroy. The grid spacing is a
 * physical choice here: it is the facet size.
 */
export function bakeDentAtlas(res = 32, frames = 12, types = DENT_TYPES): DentAtlas {
  const t0 = (typeof performance !== 'undefined' ? performance.now() : Date.now());
  const pos = new Float32Array(types.length * frames * res * res * 4);
  const nrm = new Float32Array(types.length * frames * res * res * 4);

  types.forEach((ty, ti) => {
    const { topo, n } = sheetTopo(ty.half, res);
    const site: DentSite = {
      label: ty.label,
      p: v3(0, 0, 0),
      d: v3(0, 0, -1),                 // press along -normal
      radius: ty.radius,
      depth: ty.depth,
      shape: ty.shape ?? 'sphere',
      span: ty.span,
      elong: ty.elong,
      drawIn: ty.drawIn,
      zone: ty.span ? ty.span * 1.4 : undefined,
    };
    const b = bakeDent(topo, site, {
      frames,
      // the sheet is small and clamped at the rim, so it can take more
      // iterations than a whole car body for the same cost
      iters: 34, release: 22,
    });
    // scatter the sparse patch back onto the dense grid
    const base = ti * frames * res * res;
    for (let f = 0; f < frames; f++) {
      for (let k = 0; k < b.count; k++) {
        const v = b.verts[k];
        const src = (f * b.count + k) * 4;
        const dst = (base + f * res * res + v) * 4;
        pos[dst] = b.pos[src];
        pos[dst + 1] = b.pos[src + 1];
        pos[dst + 2] = b.pos[src + 2];
        pos[dst + 3] = b.pos[src + 3];
      }
      // Normals come from the DEFORMED MESH, not from finite differences of
      // the depth channel. That shortcut assumes the dent is a heightfield
      // z = f(u, v), which was true before the sheet was allowed to draw
      // inward and is badly false afterwards: a crumple moves metal sideways
      // as much as down, and a fold is vertical, where a heightfield gradient
      // is undefined. Using it left the geometry folding while the lighting
      // stayed flat - creases you cannot see are creases you did not make.
      for (let k = 0; k < b.count; k++) {
        const v = b.verts[k];
        const src = (f * b.count + k) * 4;
        const dst = (base + f * res * res + v) * 4;
        nrm[dst] = b.nrm[src];
        nrm[dst + 1] = b.nrm[src + 1];
        nrm[dst + 2] = b.nrm[src + 2];
      }
      // texels outside the solved patch are undamaged sheet
      for (let k = 0; k < res * res; k++) {
        const dst = (base + f * res * res + k) * 4;
        if (nrm[dst] === 0 && nrm[dst + 1] === 0 && nrm[dst + 2] === 0) nrm[dst + 2] = 1;
      }
    }
    void n;
  });

  const t1 = (typeof performance !== 'undefined' ? performance.now() : Date.now());
  return { res, frames, types, pos, nrm, ms: t1 - t0 };
}
