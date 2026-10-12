// Ramp ↔ mainline blend: the gore wedge, its paint and its terminals.
//
// For a ramp to "merge into the road" three things have to be true at once: its pavement must meet the
// mainline's pavement edge with no gap and no overlap, the joint must converge on a single point (the
// nose) along a taper of roughly 1:12, and the neutral area between the two roads must be paved,
// marked and terminated with hardware. Everything here is derived from the two centrelines, so any
// ramp shape — 270° loop, direct connector, diagonal or a simple parallel deceleration lane — gets the
// same treatment without the layout code having to know about it.
//
// Data, written by the generator on the ramp edge:
//   edge.merge = { main: <mainline edge id>, end: 'a'|'b', nose?: bool, chevrons?: bool }
// `end` names the ramp end that touches the mainline.

import { strip, yawFor } from './out.js';
import { getProfile, layout, RANK } from '../profiles.js';

const GROUND = -0.06;
const PAINT = 0.014;
const STEP = 4;

// Which side of the mainline the ramp sits on, and the two edge lines that must meet. Both are the
// deck edges (outer face of the outboard barrier lane, or the pavement edge if there is no barrier) so
// the surfaces touch exactly — never overlap, which would z-fight, never leave a slot.
function sides(out, net, edge, mainEdge, rc, mc, fromA) {
  const ml = layout(getProfile(mainEdge)), rl = layout(getProfile(edge));
  const [r0, r1] = range(out, rc);
  const probe = rc.frameAt(fromA ? Math.min(rc.length, r0 + 8) : Math.max(0, r1 - 8));
  const q = mc.nearest(probe.x, probe.z), mf = mc.frameAt(q.s);
  const mainSide = ((probe.x - mf.x) * mf.nx + (probe.z - mf.z) * mf.nz) >= 0 ? 1 : -1;
  const outer = ml.lanes[ml.lanes.length - 1], inner = ml.lanes[0];
  const oMain = mainSide > 0
    ? (outer.meta.type === 'barrier' ? outer.o1 : ml.curbR)
    : (inner.meta.type === 'barrier' ? inner.o0 : ml.curbL);
  const oT = (mf.x - probe.x) * probe.nx + (mf.z - probe.z) * probe.nz;
  const rampSide = oT < 0 ? -1 : 1;
  return { ml, rl, mainSide, rampSide, oMain, oRamp: rampSide > 0 ? rl.right : rl.left };
}

// The built range of the element (junction setbacks already applied by buildEdge).
function range(out, c) { return out.range || [0, c.length]; }

// Walk the ramp from the mainline end, pairing each ramp edge point with the mainline edge point it
// faces. The void between the two roads widens through the taper and then plateaus; the returned
// `end` index is just past that plateau, which is where a real gore stops being a gore.
export function sampleGore(out, net, edge, mainEdge, rc, mc, fromA, sd, maxLen = 300) {
  const [r0, r1] = range(out, rc);
  const sTip = fromA ? r0 : r1, dir = fromA ? 1 : -1;
  const span = Math.min(maxLen, r1 - r0);
  const n = Math.floor(span / STEP);
  const P = [];
  let prev = -1, plateau = -1;
  for (let i = 0; i <= n; i++) {
    const s = sTip + dir * i * STEP;
    if (fromA ? s > r1 + 1e-6 : s < r0 - 1e-6) break;
    const g = rc.frameAt(s);
    const bx = g.x + g.nx * sd.oRamp, bz = g.z + g.nz * sd.oRamp;
    const q = mc.nearest(bx, bz), mf = mc.frameAt(q.s);
    const ax = mf.x + mf.nx * sd.oMain, az = mf.z + mf.nz * sd.oMain;
    const w = Math.abs((bx - ax) * mf.nx + (bz - az) * mf.nz);
    P.push({ s, ms: q.s, ax, ay: mf.y, az, bx, by: g.y, bz, w, nx: mf.nx, nz: mf.nz, tx: mf.tx, tz: mf.tz });
    if (plateau < 0 && prev >= 0 && w > 0.5 && w < prev + 0.02) plateau = i;
    // a gore stops being a gore the moment the two roads stop running parallel: beyond that the
    // widening is the ramp's own curve, not a taper, and painting it would be nonsense
    if (plateau < 0 && i > 1 && Math.abs(g.tx * mf.tz - g.tz * mf.tx) > 0.25) plateau = i;
    prev = w;
  }
  // The nose is where the pavement gap stops being ~0: a ramp runs flush alongside the mainline for
  // its deceleration / acceleration section, so the element tip is usually NOT the nose.
  let nose = -1;
  for (let i = 0; i < P.length; i++) if (P[i].w > 0.06) { nose = Math.max(0, i - 1); break; }
  const end = plateau > 3 ? Math.min(P.length - 1, plateau + 4) : P.length - 1;
  return { P, end, nose, width: P[Math.max(0, end)]?.w || 0, taper: plateau > 0 ? plateau * STEP : 0 };
}

// A painted band following a polyline: centre points, half width, at +PAINT above the surface.
function band(out, mat, pts, half, key) {
  if (pts.length < 2) return;
  const pc = out.piece(mat), base = pc.p.length / 3;
  for (let k = 0; k < pts.length; k++) {
    const p = pts[k];
    const a = pts[Math.max(0, k - 1)], b = pts[Math.min(pts.length - 1, k + 1)];
    let tx = b[key[0]] - a[key[0]], tz = b[key[1]] - a[key[1]];
    const l = Math.hypot(tx, tz) || 1; tx /= l; tz /= l;
    const nx = -tz, nz = tx;
    out.vert(pc, p[key[0]] + nx * half, p[key[2]] + PAINT, p[key[1]] + nz * half, 0, p.s);
    out.vert(pc, p[key[0]] - nx * half, p[key[2]] + PAINT, p[key[1]] - nz * half, 1, p.s);
  }
  for (let k = 0; k + 1 < pts.length; k++) {
    const a = base + k * 2;
    pc.i.push(a, a + 1, a + 2, a + 1, a + 3, a + 2);
  }
}

// Vertical skirt between two equal-length polylines (bottom → top).
function skirt(out, mat, Top, Bot, faceOut) {
  if (Top.length < 2) return;
  const pc = out.piece(mat), base = pc.p.length / 3;
  let u = 0;
  for (let k = 0; k < Top.length; k++) {
    if (k) u += Math.hypot(Top[k][0] - Top[k - 1][0], Top[k][2] - Top[k - 1][2]);
    out.vert(pc, Top[k][0], Bot[k][1], Top[k][2], u, 0);
    out.vert(pc, Top[k][0], Top[k][1], Top[k][2], u, 1);
  }
  for (let k = 0; k + 1 < Top.length; k++) {
    const b0 = base + k * 2, t0 = b0 + 1, b1 = b0 + 2, t1 = b0 + 3;
    if (faceOut > 0) pc.i.push(b0, b1, t0, t0, b1, t1);
    else pc.i.push(b0, t0, b1, b1, t0, t1);
  }
}

// Diagonal hatch bars across the wedge — the marked neutral area. Bars stop once the wedge is wide
// enough for a V-nose pattern, which is then drawn as two converging lines instead.
function markGore(out, seg, up) {
  void up;
  const A = seg.map(p => [p.ax, p.ay, p.az]), B = seg.map(p => [p.bx, p.by, p.bz]);
  // closing bar across the wide end, then diagonal bars toward the nose
  const pc = out.piece('markWhite'), base = pc.p.length / 3;
  const w = 0.22;
  const push = (x, y, z, u) => out.vert(pc, x, y + PAINT, z, u, 0);
  let quads = 0;
  const bar = (a, b) => {
    let dx = b[0] - a[0], dz = b[2] - a[2];
    const l = Math.hypot(dx, dz) || 1; dx /= l; dz /= l;
    const nx = -dz * w, nz = dx * w;
    const i0 = base + quads * 4;
    push(a[0] + nx, a[1], a[2] + nz, 0); push(a[0] - nx, a[1], a[2] - nz, 0);
    push(b[0] + nx, b[1], b[2] + nz, 1); push(b[0] - nx, b[1], b[2] - nz, 1);
    pc.i.push(i0, i0 + 1, i0 + 2, i0 + 1, i0 + 3, i0 + 2);
    quads++;
  };
  const last = seg.length - 1;
  bar(A[last], B[last]);
  for (let k = last - 3; k >= 1; k -= 3) {
    const back = Math.max(0, k - 3);
    bar(A[k], B[back]);
    if (seg[k].w > 5.5) break;
  }
  if (!quads) out.piece('markWhite');
}

// Build the blend for one ramp end. Returns the sampled stations (used by tests) or null.
export function buildMerge(out, net, edge, ctx) {
  const list = edge.merge ? (Array.isArray(edge.merge) ? edge.merge : [edge.merge]) : [];
  let res = null;
  for (const M of list) {
    const r = buildOne(out, net, edge, ctx, M);
    if (r && !res) res = r;
  }
  return res;
}

function buildOne(out, net, edge, ctx, M) {
  if (!M) return null;
  const mainEdge = net.edges.get(M.main); if (!mainEdge) return null;
  const mc = net.centerline(mainEdge), rc = net.centerline(edge);
  const fromA = M.end !== 'b';
  const sd = sides(out, net, edge, mainEdge, rc, mc, fromA);
  const { P, end, nose } = sampleGore(out, net, edge, mainEdge, rc, mc, fromA, sd);
  const from = Math.max(0, nose);
  if (end - from < 3) return null;              // flush the whole way: a seam, nothing to fill
  const seg = P.slice(from, end + 1);
  const A = seg.map(p => [p.ax, p.ay, p.az]), B = seg.map(p => [p.bx, p.by, p.bz]);
  const up = isUpward(A, B);

  // --- paved neutral area between the two roadways
  const surf = getProfile(mainEdge).surface || 'asphalt';
  strip(out, surf, A, B, !up);

  // --- slab soffit when the pair is on a viaduct, kerb return when it is at grade
  const deck = RANK[mainEdge.profile] >= 8 ? 1.6 : 1.1;
  const onDeck = seg[0].ay > 2.6;
  if (onDeck) {
    const botA = A.map(p => [p[0], p[1] - deck, p[2]]);
    const botB = B.map((p, k) => [p[0], Math.min(p[1], A[k][1]) - deck, p[2]]);
    strip(out, 'concrete', botA, botB, up);
    skirt(out, 'concrete', B, botB, sd.rampSide);
    skirt(out, 'concrete', A, botA, -sd.mainSide);
  } else {
    const botA = A.map(p => [p[0], GROUND, p[2]]);
    const botB = B.map(p => [p[0], GROUND, p[2]]);
    skirt(out, 'curb', B, botB, sd.rampSide);
    skirt(out, 'curb', A, botA, -sd.mainSide);
  }

  // --- paint: fog line along the ramp edge (the mainline already paints its own edge line here)
  band(out, 'markWhite', seg, 0.11, ['bx', 'bz', 'by']);
  markGore(out, seg, up);

  // --- hardware
  if (ctx.props !== false) {
    const m0 = mc.frameAt(seg[0].ms);
    if (M.nose !== false && seg[seg.length - 1].w > 1.1 && seg[0].w < 0.6) {
      const y = Math.max(seg[0].ay, seg[0].by);
      out.prop('crashCushion', (seg[0].ax + seg[0].bx) / 2, y, (seg[0].az + seg[0].bz) / 2,
        yawFor(m0.tx * sd.mainSide, m0.tz * sd.mainSide));
    }
    if (seg[seg.length - 1].w > 3.2) {
      const f = mc.frameAt(seg[Math.min(2, seg.length - 1)].ms);
      out.prop('signKeepRight', f.x + f.nx * sd.oMain * 0.5, f.y + 1.0, f.z + f.nz * sd.oMain * 0.5,
        yawFor(f.tx * sd.mainSide, f.tz * sd.mainSide));
    }
    for (let k = 3; k < seg.length - 1; k += 4) {
      const f = rc.frameAt(seg[k].s);
      out.prop('delineator', seg[k].bx, seg[k].by, seg[k].bz, yawFor(f.tx, f.tz));
    }
  }
  return seg;
}

// Winding of a strip built from two parallel polylines: upward facing when the cross term is positive.
function isUpward(A, B) {
  let sum = 0;
  for (let k = 0; k + 1 < A.length; k++) {
    const ax = A[k + 1][0] - A[k][0], az = A[k + 1][2] - A[k][2];
    const bx = B[k][0] - A[k][0], bz = B[k][2] - A[k][2];
    sum += az * bx - ax * bz;
  }
  return sum >= 0;
}

// Exposed for tests and tooling: the raw taper sampling without emitting geometry.
export function mergeStats(net, edge) {
  const list = edge.merge ? (Array.isArray(edge.merge) ? edge.merge : [edge.merge]) : [];
  return list.map(M => statsOne(net, edge, M)).filter(Boolean);
}

function statsOne(net, edge, M) {
  if (!M) return null;
  const mainEdge = net.edges.get(M.main); if (!mainEdge) return null;
  const mc = net.centerline(mainEdge), rc = net.centerline(edge);
  const fromA = M.end !== 'b';
  const box = { range: [0, rc.length] };
  const sd = sides(box, net, edge, mainEdge, rc, mc, fromA);
  const g = sampleGore(box, net, edge, mainEdge, rc, mc, fromA, sd, 320);
  return { ...g, sd, mainId: M.main, fromA, width: g.width, taper: g.taper };
}
