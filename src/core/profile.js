// Cross-section profile of the tunnel: flat road, tiered procedural walls
// (different levels), arched ceiling whose height varies.
//
// The profile is a CLOSED loop of P points with fixed index layout so any two
// mesh pieces that share a ring can stitch with shared vertices:
//
//   loop order:  road(L->R)  rightWall(up)  arc(R->L)  leftWall(down)  [wrap]
//
//   roadIds        : 0 .. A-1            (A pts,  Lm=0, Rm=A-1)
//   rwallBottomUp  : [Rm ... RTop]       (W pts)
//   arcIds         : [RTop ... LTop]     (A pts)
//   lwallBottomUp  : [Lm ... LTop]       (W pts)
//
//   P = A + (W-1) + (A-1) + (W-2) = 2A + 2W - 4,   W = 2*tiers + 1

import { UP, cross, norm, scale, v3 } from './vec.js';
import { snoise2 } from './noise.js';

export const defaultProfileOpts = {
  halfW: 4.2,          // road half width
  roadPts: 5,          // A: road rows across AND ceiling arc points
  tiers: 3,            // wall levels (ledges)
  tierH: 2.3,          // base height of a wall tier
  tierHVar: 0.38,
  ledgeD: 1.15,        // base inward depth of a tier ledge
  ledgeDVar: 0.45,
  archRise: 3.4,       // ceiling rise above the springing points
  archRiseVar: 0.5,    // <- tunnel height varies along the path
  archExp: 0.82,       // <1 = slightly pointed vault
  noiseScale: 0.045,   // world-space noise frequency
  seed: 1,
};

const clamp = (v, a, b) => Math.min(Math.max(v, a), b);

/** Tier stack at a world position: [{h, d} per tier]. */
export function tierStack(x, z, opts) {
  const ns = opts.noiseScale;
  const stack = [];
  for (let t = 0; t < opts.tiers; t++) {
    const hN = snoise2(x * ns + t * 13.7, z * ns + 40.5, opts.seed + 7);
    const dN = snoise2(x * ns + 31.2 + t * 7.1, z * ns - 11.3, opts.seed + 11);
    stack.push({
      h: opts.tierH * clamp(1 + hN * opts.tierHVar, 0.35, 1.9),
      d: opts.ledgeD * clamp(1 + dN * opts.ledgeDVar, 0.3, 1.9),
    });
  }
  return stack;
}

/** Arch rise at a world position. */
export function archRiseAt(x, z, opts) {
  const ns = opts.noiseScale * 0.8;
  const n = snoise2(x * ns + 5.7, z * ns + 9.1, opts.seed + 17);
  return opts.archRise * clamp(1 + n * opts.archRiseVar, 0.45, 1.8);
}

// --- palette -----------------------------------------------------------------

function strataColor(tier, x, z, seed) {
  const bands = [
    [0.42, 0.33, 0.26],
    [0.52, 0.42, 0.32],
    [0.36, 0.31, 0.29],
    [0.55, 0.47, 0.36],
    [0.4, 0.36, 0.33],
  ];
  const b = bands[((tier % bands.length) + bands.length) % bands.length];
  const j = snoise2(x * 0.11 + tier * 3.3, z * 0.11, seed + 23) * 0.09;
  return [clamp(b[0] + j, 0, 1), clamp(b[1] + j, 0, 1), clamp(b[2] + j, 0, 1)];
}

function roadColor(x, z, seed) {
  const j = snoise2(x * 0.13, z * 0.13, seed + 29) * 0.03;
  return [0.17 + j, 0.18 + j, 0.2 + j];
}

function ceilColor(x, z, seed) {
  const j = snoise2(x * 0.1 + 7.7, z * 0.1, seed + 31) * 0.07;
  return [0.46 + j, 0.42 + j, 0.38 + j];
}

// --- builders ----------------------------------------------------------------

/**
 * One wall column (W points, bottom-up) at a world base point. Riser/ledge
 * tiers inset along `insetDir` (horizontal, unit). The last point is the
 * vault springing ("wall top").
 */
export function buildWallColumn(base, insetDir, opts) {
  const stack = tierStack(base.x, base.z, opts);
  const points = [];
  const colors = [];
  let u = 0;
  let v = 0;
  points.push(v3(base.x, base.y, base.z));
  colors.push(roadColor(base.x, base.z, opts.seed));
  for (let t = 0; t < opts.tiers; t++) {
    v += stack[t].h; // riser
    let p = v3(base.x + insetDir.x * u, base.y + v, base.z + insetDir.z * u);
    points.push(p);
    colors.push(strataColor(t * 2, p.x, p.z, opts.seed));
    u += stack[t].d; // ledge
    p = v3(base.x + insetDir.x * u, base.y + v, base.z + insetDir.z * u);
    points.push(p);
    colors.push(strataColor(t * 2 + 1, p.x, p.z, opts.seed));
  }
  return { points, colors, top: points[points.length - 1] };
}

/**
 * Full closed profile at a station. `pos` = road-level centre, `tan` = unit
 * tangent. Returns world points, colors, tier tags and the seam index sets.
 */
export function buildStationProfile(pos, tan, opts) {
  const A = opts.roadPts;
  const T = opts.tiers;
  const W = 2 * T + 1;
  const P = 2 * A + 2 * W - 4;

  let side = cross(UP, tan);
  if (Math.hypot(side.x, side.z) < 1e-5) side = v3(1, 0, 0);
  side = norm(side);

  const points = new Array(P);
  const colors = new Array(P);
  const tiers = new Array(P).fill(-1);
  const at = (u, v) => v3(pos.x + side.x * u, pos.y + v, pos.z + side.z * u);

  // --- road: A points, left (-halfW) -> right (+halfW), flat
  for (let i = 0; i < A; i++) {
    const u = -opts.halfW + (2 * opts.halfW * i) / (A - 1);
    points[i] = at(u, 0);
    colors[i] = roadColor(points[i].x, points[i].z, opts.seed);
    tiers[i] = -1;
  }

  // --- right wall, bottom-up: W-1 new points, Rm (= A-1) already placed
  const stack = tierStack(pos.x, pos.z, opts);
  let u = opts.halfW;
  let v = 0;
  let idx = A - 1;
  for (let t = 0; t < T; t++) {
    v += stack[t].h;
    points[++idx] = at(u, v);
    colors[idx] = strataColor(t * 2, points[idx].x, points[idx].z, opts.seed);
    tiers[idx] = t;
    u -= stack[t].d;
    points[++idx] = at(u, v);
    colors[idx] = strataColor(t * 2 + 1, points[idx].x, points[idx].z, opts.seed);
    tiers[idx] = t;
  }
  const RTopIdx = idx; // A + W - 2
  const rTopWorld = points[RTopIdx];

  // --- left wall column (built once, reused for LTop + interiors)
  const lCol = buildWallColumn(v3(pos.x, pos.y, pos.z), scale(side, -1), opts);
  const lTopWorld = lCol.top;

  // --- arc RTop -> LTop: A-1 new points (RTop exists; last new = LTop)
  const rise = archRiseAt(pos.x, pos.z, opts);
  for (let i = 1; i < A; i++) {
    const s = i / (A - 1);
    const bx = rTopWorld.x + (lTopWorld.x - rTopWorld.x) * s;
    const by = rTopWorld.y + (lTopWorld.y - rTopWorld.y) * s;
    const bz = rTopWorld.z + (lTopWorld.z - rTopWorld.z) * s;
    const bulge = Math.pow(Math.sin(Math.PI * s), opts.archExp) * rise;
    points[++idx] = v3(bx, by + bulge, bz);
    colors[idx] = ceilColor(points[idx].x, points[idx].z, opts.seed);
    tiers[idx] = 100;
  }
  const LTopIdx = idx; // 2A + W - 3

  // --- left wall, top-down interiors: W-2 new points (LTop exists, Lm = 0)
  for (let i = lCol.points.length - 2; i >= 1; i--) {
    points[++idx] = lCol.points[i];
    colors[idx] = lCol.colors[i];
    tiers[idx] = Math.floor((i - 1) / 2);
  }
  if (idx !== P - 1) throw new Error(`profile size mismatch: built ${idx + 1}, expected ${P}`);

  return {
    points,
    colors,
    tiers,
    A,
    W,
    P,
    side,
    seams: {
      roadIds: Array.from({ length: A }, (_, i) => i),              // Lm -> Rm
      rwallBottomUp: Array.from({ length: W }, (_, i) => A - 1 + i), // Rm -> RTop
      arcIds: Array.from({ length: A }, (_, i) => RTopIdx + i),      // RTop -> LTop
      lwallBottomUp: [0, ...Array.from({ length: W - 2 }, (_, i) => P - 1 - i), LTopIdx], // Lm -> LTop
      Lm: 0,
      Rm: A - 1,
      RTop: RTopIdx,
      LTop: LTopIdx,
    },
  };
}
