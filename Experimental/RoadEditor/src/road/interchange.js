// Interchange geometry: ramp paths built from the roads they join.
//
// A ramp is assembled from up to four parts, in travel order:
//
//   [pinch tip] → [flush run] → [flare] → [body] → [flare] → [flush run] → [pinch tip]
//
// Both ends are *sampled off the anchored roadway's own centreline* (`mergeEnd`), so the ramp's
// pavement is guaranteed to sit exactly flush with that roadway's edge for the flush run and then
// open a taper of the chosen length — the "carefully merges" property is structural, not a tuning
// exercise. merge.js then fills and marks the wedge each flare leaves behind, and `tipTaper` folds the
// cross-section out of existence at the tip so a ramp lane dies as a taper, never as a sliced slab.
//
// The body between the two flares is either a single cubic Hermite (tangency at both ends, so the
// path is G1 by construction) or, for loop ramps, a circular arc — which is what keeps a 35 m loop at
// a constant, signable radius for its whole 270°.

import { getProfile, layout } from './profiles.js';

export const sm = t => t * t * (3 - 2 * t);
export const clampf = (v, a, b) => (v < a ? a : v > b ? b : v);

// Points along the offset curve of a centreline; `lat(s)` may vary (that is how the flare is drawn).
export function offsetPts(c, s0, s1, lat, step = 6) {
  const n = Math.max(1, Math.round(Math.abs(s1 - s0) / step));
  const out = [];
  for (let i = 0; i <= n; i++) {
    const s = s0 + (s1 - s0) * i / n;
    const f = c.frameAt(s), o = lat(s, f);
    out.push({ x: f.x + f.nx * o, z: f.z + f.nz * o, y: f.y, s, o });
  }
  return out;
}

// Tangent heading of a polyline at index k (central difference).
export function headAt(pts, k) {
  const a = pts[Math.max(0, k - 1)], b = pts[Math.min(pts.length - 1, k + 1)];
  return Math.atan2(b.z - a.z, b.x - a.x);
}

// Signed lateral offset of a roadway's edge line (its deck edge, or its gutter if it is curbed).
export function edgeOffset(lay, sg) {
  const on = l => (sg > 0 ? l.o1 >= 0 : l.o0 <= 0);
  const bar = lay.lanes.filter(l => l.type === 'barrier' && on(l));
  if (bar.length) return sg > 0 ? Math.max(...bar.map(l => l.o1)) : Math.min(...bar.map(l => l.o0));
  const driv = lay.lanes.filter(l => on(l) && l.level === 'road');
  if (driv.length) return sg > 0 ? Math.max(...driv.map(l => l.o1)) : Math.min(...driv.map(l => l.o0));
  return sg > 0 ? lay.curbR : lay.curbL;
}

/**
 * One anchored end of a ramp.
 *  at    station on the anchor where the ramp's edge meets the anchor's edge (the nose)
 *  sg    which side of the anchor the ramp sits on (+1 → +normal)
 *  dir   the anchor's s-direction that ramp traffic is travelling in at the nose
 *  flush metres of parallel, zero-gap pavement behind the nose (the deceleration / acceleration lane)
 *  flare metres over which the gap opens to `gap`
 *  gap   the clearance the ramp's inner edge keeps from the anchor's edge beyond the flare
 *  half  the ramp's own half width, so its pavement (not its centreline) is what stays flush
 */
export function mergeEnd(net, anchor, e) {
  const c = net.centerline(anchor), lay = layout(getProfile(anchor));
  const oEdge = edgeOffset(lay, e.sg);
  const half = e.half ?? 4.3;
  const flare = Math.max(1, e.flare || 40);
  // 'in'  = the ramp leaves the anchor: flush upstream of the nose, flare opens downstream
  // 'out' = the ramp joins the anchor: flare closes upstream of the nose, flush continues on
  const out = e.mode === 'out';
  const gapAt = s => (e.gap || 0) * sm(clampf(out ? (e.at - s) * e.dir / flare : (s - e.at) * e.dir / flare, 0, 1));
  const lat = s => oEdge + e.sg * (half + gapAt(s));
  const sFrom = out ? e.at - e.dir * flare : e.at - e.dir * (e.flush || 0);
  const sTo = out ? e.at + e.dir * (e.flush || 0) : e.at + e.dir * flare;
  const pts = offsetPts(c, sFrom, sTo, lat, e.step || 6);
  return { pts, oEdge, lat, c, lay, edgeId: anchor.id, name: anchor.name, at: e.at, dir: e.dir, flare, headIn: headAt(pts, 0), headOut: headAt(pts, pts.length - 1) };
}

// Cubic Hermite between two (point, tangent) ends: G1 at both, curvature bounded for sane handles.
export function hermitePts(p0, th0, p1, th1, step = 8) {
  const d = Math.max(12, Math.hypot(p1.x - p0.x, p1.z - p0.z) * 0.55);
  const ax = p0.x, az = p0.z, bx = ax + Math.cos(th0) * d, bz = az + Math.sin(th0) * d;
  const cx = p1.x - Math.cos(th1) * d, cz = p1.z - Math.sin(th1) * d;
  const dx = p1.x, dz = p1.z;
  const len = Math.hypot(dx - ax, dz - az) + d;
  const n = Math.max(6, Math.round(len / step));
  const out = [];
  for (let i = 1; i < n; i++) {                       // endpoints belong to the neighbouring pieces
    const t = i / n, u = 1 - t;
    out.push({
      x: u * u * u * ax + 3 * u * u * t * bx + 3 * u * t * t * cx + t * t * t * dx,
      z: u * u * u * az + 3 * u * u * t * bz + 3 * u * t * t * cz + t * t * t * dz,
    });
  }
  out.push({ x: dx, z: dz });
  return out;
}

// Circular arc from p0 heading th0, signed radius (positive = turning left), sweeping `turns` radians.
export function arcPts(p0, th0, R, turns, step = 6) {
  const sgn = R >= 0 ? 1 : -1, Ar = Math.abs(R);
  const cxp = p0.x - Math.sin(th0) * Ar * sgn, czp = p0.z + Math.cos(th0) * Ar * sgn;
  const a0 = Math.atan2(p0.z - czp, p0.x - cxp);
  const n = Math.max(3, Math.ceil(Math.abs(turns) * Ar / step));
  const out = [];
  for (let i = 1; i <= n; i++) {
    const a = a0 + sgn * turns * i / n;
    out.push({ x: cxp + Math.cos(a) * Ar, z: czp + Math.sin(a) * Ar });
  }
  return out;
}

// ------------------------------------------------------------------ archetype shapes
// Each archetype lists its ramps. Offsets are in metres measured along the *anchor* roadways:
//   noseAt = cross ± along, and the flare/flush lengths are the ramp's own merge design.
// R is the loop radius, `turn` the total heading change of the body, `to` how the body is finished.
export const ARCHETYPES = {
  diamond: {
    label: 'Diamond', ramps: 4, loops: 0,
    desc: 'Four single-point ramps: a right-turn exit and a right-turn entry on each side. The cheapest grade separation, and the usual answer for a minor crossroad.',
    plan: (sides) => sides.flatMap(s => [
      { kind: 'off', ...s, along: -110, flush: 150, flare: 62, gap: 7.5, body: 'hermite' },
      { kind: 'on', ...s, along: 110, flush: 150, flare: 62, gap: 7.5, body: 'hermite' },
    ]),
  },
  cloverleaf: {
    label: 'Cloverleaf', ramps: 4, loops: 4, R: 78,
    desc: 'Four right-turn ramps plus four 270-degree loops for the left turns; each loop circles a constant-radius quadrant corner.',
    plan: (sides) => [
      ...sides.flatMap(s => [
        { kind: 'off', ...s, along: -110, flush: 150, flare: 62, gap: 8.5, body: 'hermite' },
        { kind: 'on', ...s, along: 110, flush: 150, flare: 62, gap: 8.5, body: 'hermite' },
      ]),
      ...sides.flatMap(s => [
        { kind: 'loop', ...s, along: -150, flush: 120, flare: 82, gap: 10, R: 74, turn: -Math.PI * 1.5, body: 'arc', cross: 1 },
        { kind: 'loop', ...s, along: 150, flush: 120, flare: 82, gap: 10, R: 74, turn: Math.PI * 1.5, body: 'arc', cross: -1 },
      ]),
    ],
  },
  parclo: {
    label: 'Parclo A4', ramps: 6, loops: 2, R: 84,
    desc: 'Three-quarter cloverleaf: two loops are replaced by diagonal ramps, so left turns get a higher design speed and the weaving section disappears.',
    plan: (sides) => [
      ...sides.flatMap(s => [
        { kind: 'off', ...s, along: -120, flush: 160, flare: 70, gap: 9, body: 'hermite' },
        { kind: 'on', ...s, along: 120, flush: 160, flare: 70, gap: 9, body: 'hermite' },
      ]),
      { kind: 'loop', ...sides[0], along: -170, flush: 130, flare: 86, gap: 12, R: 80, turn: -Math.PI * 1.5, body: 'arc', cross: 1 },
      { kind: 'loop', ...sides[1], along: 170, flush: 130, flare: 86, gap: 12, R: 80, turn: Math.PI * 1.5, body: 'arc', cross: -1 },
      { kind: 'diag', ...sides[0], along: -230, flush: 200, flare: 96, gap: 14, R: 150, body: 'hermite', cross: -1, diag: 1 },
      { kind: 'diag', ...sides[1], along: 230, flush: 200, flare: 96, gap: 14, R: 150, body: 'hermite', cross: 1, diag: -1 },
    ],
  },
  turbine: {
    label: 'Turbine', ramps: 6, loops: 0, R: 110,
    desc: 'Six long spiral ramps, all free-flowing: no loops, no signals, no weaving. Used where the crossing carries another freeway.',
    plan: (sides) => sides.flatMap(s => [
      { kind: 'off', ...s, along: -190, flush: 220, flare: 95, gap: 13, body: 'hermite', R: 190, spiral: 1 },
      { kind: 'on', ...s, along: 190, flush: 220, flare: 95, gap: 13, body: 'hermite', R: 190, spiral: 1 },
      { kind: 'diag', ...s, along: s.sg > 0 ? -260 : 260, flush: 240, flare: 110, gap: 17, body: 'hermite', R: 240, diag: s.sg, spiral: 1 },
    ]),
  },
  trumpet: {
    label: 'Trumpet', ramps: 3, loops: 0,
    desc: 'For a road that dead-ends into the freeway: two curved connectors plus one loop, all free-flowing.',
    plan: (sides) => [
      { kind: 'off', ...sides[0], along: -95, flush: 130, flare: 55, gap: 7.5, body: 'hermite' },
      { kind: 'on', ...sides[0], along: 95, flush: 130, flare: 55, gap: 7.5, body: 'hermite' },
      { kind: 'loop', ...sides[1], along: -140, flush: 120, flare: 60, gap: 11, R: 38, turn: -Math.PI * 1.5, body: 'arc', cross: 1 },
    ],
  },
};

export const IC_NOTES = { loop: 34, diag: 150 };
