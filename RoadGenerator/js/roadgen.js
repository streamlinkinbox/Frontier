// Road network -> render meshes.
//
// Topology rules this file enforces:
//  * Road strips are quads, sampled along the spline at ~1 m, lateral bands as quads.
//  * Every intersection is ONE merged patch: each approach is trimmed to a shared
//    cross-section; the approaches' edge points are joined by corner points (line/line
//    intersections, midpoint fallback). The carriage polygon is filled with concentric
//    quad rings and a single triangle-fan core. Curbs, pavements and drop faces are bands
//    between the polygon levels, so there is no overlapping or z-fighting geometry.
//  * Markings and crossings are decals (depth-biased); they never add a second surface.

import { MeshBuilder, normalize, cross } from './mesh.js';
import { buildCurve, pointAt, sampleRange, closestPoint } from './spline.js';
import { CURB_HEIGHT, CURB_WIDTH, ROAD_CLASSES, roadCtrl, nodeArms, indexNodes, BOLLARD_DESIGNS } from './model.js';
import { rng, smoothstep } from './math.js';

export const MAT = { asphalt: 0, concrete: 1, pavers: 2, vertex: -1, emissive: -2, ground: -3, overlay: -4 };
const UP = [0, 1, 0];
const SHOULDER_W = 1.2;
const CROWN_RATIO = 0.018; // crown height relative to carriage half-width
const JUNCTION_FADE = 6;   // metres over which crown ramps to zero at a junction seam
const SEG_ARC = 8;         // arc segments for dead-end caps

const COL = {
  carriage: [0.9, 0.9, 0.9],
  curbFace: [0.72, 0.72, 0.72],
  curbTop: [0.95, 0.95, 0.95],
  sidewalk: [1, 1, 1],
  drop: [0.66, 0.66, 0.66],
  median: [0.62, 0.7, 0.55],
  white: [0.94, 0.94, 0.92],
  yellow: [0.95, 0.74, 0.12],
  dark: [0.1, 0.1, 0.11],
};

// ---------- per-road dimensions --------------------------------------------------------------
export function roadDims(road) {
  const laneSide = road.lanesPerDir * road.laneWidth;
  const mh = road.median ? road.medianWidth / 2 : 0;
  const hw = laneSide + mh;
  const sw = road.sidewalk;
  const swL = sw === 'both' || sw === 'left' ? road.sidewalkWidth : SHOULDER_W;
  const swR = sw === 'both' || sw === 'right' ? road.sidewalkWidth : SHOULDER_W;
  const h1 = hw + CURB_WIDTH;
  const dims = {
    laneSide, mh, hw, laneWidth: road.laneWidth, lanesPerDir: road.lanesPerDir,
    swL, swR, h1, h2L: h1 + swL, h2R: h1 + swR,
    matL: sw === 'none' || (sw === 'right') ? 0 : MAT[road.sidewalkMat] ?? 1,
    matR: sw === 'none' || (sw === 'left') ? 0 : MAT[road.sidewalkMat] ?? 1,
    surface: MAT[road.surface] ?? 0,
  };
  dims.outer = Math.max(dims.h2L, dims.h2R);
  return dims;
}

function crownAt(dims, lat) {
  const r = lat / Math.max(dims.hw, 0.01);
  return CROWN_RATIO * dims.hw * (1 - r * r);
}

// ---------- profile of a straight road cross-section ----------------------------------------
// Ordered left (outside) -> right (outside). Each segment: a horizontal band or a vertical face.
// nl/nu: intended normal in (lateral, up) basis.
export function roadProfile(road, dims) {
  const segs = [];
  const c = CURB_HEIGHT;
  const seg = (l0, y0, l1, y1, mat, col, nl, nu, crown = false) => segs.push({ l0, y0, l1, y1, mat, col, nl, nu, crown });
  const { hw, mh, h1, h2L, h2R, matL, matR, surface } = dims;
  const CONC = 1;
  // left pavement
  seg(-h2L, 0, -h2L, c, CONC, COL.drop, -1, 0);
  seg(-h2L, c, -h1, c, matL, COL.sidewalk, 0, 1);
  seg(-h1, c, -hw, c, CONC, COL.curbTop, 0, 1);
  seg(-hw, c, -hw, 0, CONC, COL.curbFace, 1, 0);
  // carriageway (left half)
  if (road.median && mh > 0) {
    seg(-hw, 0, -mh, 0, surface, COL.carriage, 0, 1);
    seg(-mh, 0, -mh, c, CONC, COL.median, -1, 0);
    seg(-mh, c, mh, c, CONC, COL.median, 0, 1);
    seg(mh, c, mh, 0, CONC, COL.median, 1, 0);
    seg(mh, 0, hw, 0, surface, COL.carriage, 0, 1);
  } else {
    const q = [-hw, -hw / 2, 0, hw / 2, hw];
    for (let i = 0; i < 4; i++) seg(q[i], 0, q[i + 1], 0, surface, COL.carriage, 0, 1, true);
  }
  // right pavement
  seg(hw, 0, hw, c, CONC, COL.curbFace, -1, 0);
  seg(hw, c, h1, c, CONC, COL.curbTop, 0, 1);
  seg(h1, c, h2R, c, matR, COL.sidewalk, 0, 1);
  seg(h2R, c, h2R, 0, CONC, COL.drop, 1, 0);
  return segs;
}

// ---------- junction arms ----------------------------------------------------------------
// Builds every road's curve + dims + trimmed range + node junction data.
export function buildNetwork(project, opts = {}) {
  const mb = new MeshBuilder();          // main lit geometry
  const decal = new MeshBuilder();       // depth-biased markings / patches
  const nodes = indexNodes(project);
  const arms = nodeArms(project);
  const curves = new Map();
  const roadInfo = new Map();

  // Approach direction at each node end (angle), used to size each junction so that
  // neighbouring approaches' outer edges do not cross.
  const nodeDirs = new Map();
  for (const road of project.roads) {
    if (road.hidden) continue;
    const ctrl = roadCtrl(project, road);
    if (!ctrl) continue;
    const a = ctrl[0], a2 = ctrl[1], b = ctrl[ctrl.length - 1], b2 = ctrl[ctrl.length - 2];
    const push = (nid, dx, dz, h2, atStart) => {
      if (!nodeDirs.has(nid)) nodeDirs.set(nid, []);
      nodeDirs.get(nid).push({ road, ang: Math.atan2(dz, dx), h2, atStart });
    };
    const dimsR = roadDims(road);
    push(road.a, a2.x - a.x, a2.z - a.z, dimsR.outer, true);
    push(road.b, b2.x - b.x, b2.z - b.z, dimsR.outer, false);
  }
  const sizeFor = (nid, road, atStart, node, L, dims) => {
    const list = nodeDirs.get(nid) || [];
    if (list.length < 2) return 0;
    const me = list.find(e => e.road === road && e.atStart === atStart);
    let base = Math.max(6, dims.outer * 1.6) * (node.scale || 1);
    let worst = Infinity, hNb = 0;
    for (const e of list) {
      if (e === me) continue;
      let g = Math.abs(e.ang - me.ang) % (Math.PI * 2);
      if (g > Math.PI) g = Math.PI * 2 - g;
      if (g < worst) { worst = g; hNb = e.h2; }
    }
    if (worst > 1e-3) {
      const need = 1.15 * Math.max(dims.outer, hNb) / Math.max(Math.sin(worst / 2), 0.2);
      base = Math.max(base, Math.min(need, 0.42 * L));
    }
    return base;
  };

  for (const road of project.roads) {
    if (road.hidden) continue;
    const ctrl = roadCtrl(project, road);
    if (!ctrl) continue;
    const curve = buildCurve(ctrl, 1.0);
    if (curve.length < 2) continue;
    const dims = roadDims(road);
    const degA = (arms.get(road.a) || []).length;
    const degB = (arms.get(road.b) || []).length;
    const L = curve.length;
    let D0 = degA >= 2 ? sizeFor(road.a, road, true, nodes.get(road.a), L, dims) : 0;
    let D1 = degB >= 2 ? sizeFor(road.b, road, false, nodes.get(road.b), L, dims) : 0;
    if (D0 + D1 > 0.8 * L) { const k = 0.8 * L / (D0 + D1); D0 *= k; D1 *= k; }
    const info = { road, curve, dims, s0: D0, s1: L - D1, D0, D1, degA, degB, L };
    curves.set(road.id, curve);
    roadInfo.set(road.id, info);
  }

  const junctionReports = [];
  // 1) approach strips, markings and road-level props
  for (const info of roadInfo.values()) {
    emitRoadStrip(mb, info);
    if (opts.markings !== false && info.road.markings) emitMarkings(decal, mb, info);
    emitRoadFeatures(mb, decal, info, project, opts);
  }
  // 2) junctions
  for (const [nodeId, list] of arms) {
    const node = nodes.get(nodeId);
    if (!node) continue;
    const live = list.filter(a => roadInfo.has(a.road.id));
    if (live.length === 0) continue;
    const report = emitJunction(mb, node, live.map(a => {
      const info = roadInfo.get(a.road.id);
      return { info, atStart: a.atStart };
    }));
    junctionReports.push({ nodeId, name: node.name, degree: live.length, ...report });
  }
  // 3) manual props
  for (const p of project.props || []) emitManualProp(mb, decal, p, roadInfo);

  return { main: mb, decal, curves, roadInfo, junctionReports, arms };
}

// ---------- road strip ------------------------------------------------------------------------
function emitRoadStrip(mb, info) {
  const { curve, dims, s0, s1, D0, D1 } = info;
  const profile = roadProfile(info.road, dims);
  const samples = sampleRange(curve, s0, s1, 1.0);
  if (samples.length < 2) return;
  const cfOf = s => {
    let f = 1;
    if (D0 > 0) f = Math.min(f, smoothstep(0, JUNCTION_FADE, s - s0));
    if (D1 > 0) f = Math.min(f, smoothstep(0, JUNCTION_FADE, info.L - D1 - s + 0 * s));
    return f;
  };
  const pos = (smp, lat, y, crown, cf) => {
    const nx = -smp.tz, nz = smp.tx;
    const yy = y + (crown ? crownAt(dims, lat) * cf : 0);
    return [smp.x + nx * lat, yy, smp.z + nz * lat];
  };
  const cfs = samples.map(smp => cfOf(smp.s));
  for (let k = 0; k < samples.length - 1; k++) {
    const A = samples[k], B = samples[k + 1];
    const cfa = cfs[k], cfb = cfs[k + 1];
    const nxA = -A.tz, nzA = A.tx;
    for (const sg of profile) {
      const a = pos(A, sg.l0, sg.y0, sg.crown, cfa);
      const b = pos(A, sg.l1, sg.y1, sg.crown, cfa);
      const c = pos(B, sg.l1, sg.y1, sg.crown, cfb);
      const d = pos(B, sg.l0, sg.y0, sg.crown, cfb);
      const expect = [nxA * sg.nl, sg.nu, nzA * sg.nl];
      const nrm = sg.nu ? UP : expect;
      mb.quad(a, b, c, d, { col: sg.col, mat: sg.mat, expect: nrm });
    }
  }
}

// ---------- junction --------------------------------------------------------------------------
// arms: [{ info, atStart }] for every road meeting at the node.
function emitJunction(mb, node, arms) {
  const N = [node.x, node.z];
  const stats0 = { ...mb.stats };
  const report = { quads: 0, tris: 0, concave: 0, approaches: arms.length, debugPolys: null, radialFallback: false };
  // Each arm: cross-section point P at distance D, unit tangent T pointing away from the node.
  const A_ = arms.map(({ info, atStart }) => {
    const { curve, dims, L } = info;
    const D = atStart ? info.D0 : info.D1;
    const sPt = atStart ? D : L - D;
    const p = pointAt(curve, sPt);
    let T = [p.tx, p.tz];
    if (!atStart) T = [-T[0], -T[1]];
    const nl = [-T[1], T[0]];
    return { info, atStart, D, P: [p.x, p.z], T, nl, dims, sideOf: null };
  });
  // angle-sorted CCW
  A_.sort((a, b) => Math.atan2(a.P[1] - N[1], a.P[0] - N[0]) - Math.atan2(b.P[1] - N[1], b.P[0] - N[0]));
  const n = A_.length;

  const levelPoly = (k, mode = 'line') => {
    // k = 0 carriage, 1 curb top, 2 pavement edge
    const pts = [];
    const tags = [];
    if (n === 1) {
      const a = A_[0];
      const hL = [a.dims.hw, a.dims.h1, a.dims.h2L][k];
      const hR = [a.dims.hw, a.dims.h1, a.dims.h2R][k];
      const P = a.P, nl = a.nl, T = a.T;
      const hm = (hL + hR) / 2;
      const K = SEG_ARC;
      pts.push([P[0] + nl[0] * hL, P[1] + nl[1] * hL]); tags.push({ arm: 0, side: 'L' });
      for (let i = 1; i < K; i++) {
        const al = Math.PI * i / K;
        const cw = Math.cos(al), sw = Math.sin(al);
        const lat = cw >= 0 ? hL * cw : hR * cw;
        pts.push([P[0] + nl[0] * lat - T[0] * hm * sw, P[1] + nl[1] * lat - T[1] * hm * sw]);
        tags.push({ arm: 0, side: al < Math.PI / 2 ? 'L' : 'R' });
      }
      pts.push([P[0] - nl[0] * hR, P[1] - nl[1] * hR]); tags.push({ arm: 0, side: 'R', seam: true });
      return { pts, tags };
    }
    for (let i = 0; i < n; i++) {
      const a = A_[i];
      const hL = [a.dims.hw, a.dims.h1, a.dims.h2L][k];
      const hR = [a.dims.hw, a.dims.h1, a.dims.h2R][k];
      pts.push([a.P[0] - a.nl[0] * hR, a.P[1] - a.nl[1] * hR]); tags.push({ arm: i, side: 'R', seam: true });
      pts.push([a.P[0] + a.nl[0] * hL, a.P[1] + a.nl[1] * hL]); tags.push({ arm: i, side: 'L', seam: true });
      const b = A_[(i + 1) % n];
      const hbR = [b.dims.hw, b.dims.h1, b.dims.h2R][k];
      const Aq = [a.P[0] + a.nl[0] * hL, a.P[1] + a.nl[1] * hL];
      const Bq = [b.P[0] - b.nl[0] * hbR, b.P[1] - b.nl[1] * hbR];
      const hMax = Math.max(hL, hbR, 1);
      pts.push(cornerPoint(Aq, a.T, Bq, b.T, hMax, a.D, N, mode)); tags.push({ arm: i, side: 'L' });
    }
    // Tags: vertex j, edge j->j+1. Seam edges: B_i -> A_i (vertex 3i -> 3i+1) for n>=2.
    // Recompute edge sides for banding: edge from A_i (3i+1) to C_i (3i+2) belongs to arm i's L side;
    // edge from C_i (3i+2) to B_{i+1} (3i+3) belongs to arm i+1's R side.
    return { pts, tags };
  };

  let polys = [levelPoly(0), levelPoly(1), levelPoly(2)];
  if (n >= 2 && polys.some(pl => !polygonSimple(pl.pts))) {
    // Radial corners: every vertex is on a ray from the node, so the ring cannot self-cross.
    polys = [levelPoly(0, 'radial'), levelPoly(1, 'radial'), levelPoly(2, 'radial')];
    report.radialFallback = true;
  }
  // Dense junctions can still overlap at the outer levels; report instead of hiding it.
  report.overlap = polys.some(pl => !polygonSimple(pl.pts));
  report.debugPolys = polys.map(p => p.pts);
  const M = polys[0].pts.length;

  // Boundary edges: skip seam edges (where the road strip continues).
  const isSeam = j => (n === 1 ? j === M - 1 : j % 3 === 0);
  const sideOfEdge = j => {
    if (n === 1) {
      const a = A_[0];
      return { arm: 0, side: j < (M - 1) / 2 ? 'L' : 'R', dims: a.dims };
    }
    const armIdx = Math.floor(j / 3);
    if (j % 3 === 1) return { arm: armIdx, side: 'L', dims: A_[armIdx].dims };
    const next = (armIdx + 1) % n;
    return { arm: next, side: 'R', dims: A_[next].dims };
  };
  // centroid of carriage polygon
  const cx = polys[0].pts.reduce((s, p) => s + p[0], 0) / M;
  const cz = polys[0].pts.reduce((s, p) => s + p[1], 0) / M;
  const C = [cx, 0, cz];

  const P3 = (p, y) => [p[0], y, p[1]];
  const outward = (p0, p1) => {
    // horizontal normal of a boundary edge pointing away from the node centre
    const e = [p1[0] - p0[0], p1[1] - p0[1]];
    let nrm = [e[1], -e[0]];
    const mid = [(p0[0] + p1[0]) / 2 - cx, (p0[1] + p1[1]) / 2 - cz];
    if (nrm[0] * mid[0] + nrm[1] * mid[1] < 0) nrm = [-nrm[0], -nrm[1]];
    const l = Math.hypot(nrm[0], nrm[1]) || 1;
    return [nrm[0] / l, 0, nrm[1] / l];
  };
  const inward = (p0, p1) => {
    const o = outward(p0, p1);
    return [-o[0], 0, -o[2]];
  };

  // Carriage: concentric rings + triangle-fan core (quads first, triangles at the centre).
  const surfMat = arms[0].info.dims.surface;
  const r1 = polys[0].pts.map(p => [cx + (p[0] - cx) * 0.56, cz + (p[1] - cz) * 0.56]);
  const r2 = polys[0].pts.map(p => [cx + (p[0] - cx) * 0.22, cz + (p[1] - cz) * 0.22]);
  for (let j = 0; j < M; j++) {
    const jn = (j + 1) % M;
    const p0 = polys[0].pts[j], p1 = polys[0].pts[jn];
    mb.quad(P3(p0, 0), P3(p1, 0), P3(r1[jn], 0), P3(r1[j], 0), { col: COL.carriage, mat: surfMat, expect: UP });
    mb.quad(P3(r1[j], 0), P3(r1[jn], 0), P3(r2[jn], 0), P3(r2[j], 0), { col: COL.carriage, mat: surfMat, expect: UP });
    mb.tri(P3(r2[j], 0), P3(r2[jn], 0), C, { col: COL.carriage, mat: surfMat, expect: UP });
  }

  // Curbs, curb tops, pavements, drop faces: bands on each boundary edge.
  const sw = {};
  for (let j = 0; j < M; j++) {
    if (isSeam(j)) continue;
    const jn = (j + 1) % M;
    const p0 = polys[0].pts[j], p1 = polys[0].pts[jn];
    const q0 = polys[1].pts[j], q1 = polys[1].pts[jn];
    const r0 = polys[2].pts[j], r1p = polys[2].pts[jn];
    const { side, dims } = sideOfEdge(j);
    const pavMat = side === 'L' ? dims.matL : dims.matR;
    // curb face (vertical, road side)
    const inw = inward(p0, p1);
    mb.quad(P3(p0, 0), P3(p1, 0), P3(p1, CURB_HEIGHT), P3(p0, CURB_HEIGHT), { col: COL.curbFace, mat: 1, expect: inw });
    // curb top
    mb.quad(P3(p0, CURB_HEIGHT), P3(p1, CURB_HEIGHT), P3(q1, CURB_HEIGHT), P3(q0, CURB_HEIGHT), { col: COL.curbTop, mat: 1, expect: UP });
    // pavement
    mb.quad(P3(q0, CURB_HEIGHT), P3(q1, CURB_HEIGHT), P3(r1p, CURB_HEIGHT), P3(r0, CURB_HEIGHT), { col: COL.sidewalk, mat: pavMat, expect: UP });
    // outer drop to ground
    const out = outward(p0, p1);
    mb.quad(P3(r0, CURB_HEIGHT), P3(r1p, CURB_HEIGHT), P3(r1p, 0), P3(r0, 0), { col: COL.drop, mat: 1, expect: out });
    sw[pavMat] = (sw[pavMat] || 0) + 1;
  }

  // Stats for this junction (delta since start).
  report.quads = mb.stats.quads - stats0.quads;
  report.tris = mb.stats.tris - stats0.tris;
  report.concave = mb.stats.concave - stats0.concave;
  report.boundaryEdges = M - n; // seams excluded
  report.vertexCount = M;
  report.centre = [node.x, node.z];
  report.debugPolys = polys.map(p => p.pts);
  return report;
}

export function polygonSimple(pts) {
  const m = pts.length;
  const cr = (p, q, r) => (q[0] - p[0]) * (r[1] - q[1]) - (q[1] - p[1]) * (r[0] - q[0]);
  for (let i = 0; i < m; i++) for (let j = i + 2; j < m; j++) {
    if (i === 0 && j === m - 1) continue;
    const a = pts[i], b = pts[(i + 1) % m], c = pts[j], d = pts[(j + 1) % m];
    const d1 = cr(a, b, c), d2 = cr(a, b, d), d3 = cr(c, d, a), d4 = cr(c, d, b);
    if (d1 * d2 < 0 && d3 * d4 < 0) return false;
  }
  return true;
}

// Corner between the left edge of arm i (A, direction Ta) and the right edge of arm i+1 (B, Tb).
// 1) line/line intersection when it lies ahead of both cross-sections,
// 2) otherwise a radial point on the bisector around the node centre (keeps the ring star-shaped),
// 3) midpoint only for near-antiparallel arms where no better point exists.
function cornerPoint(A, Ta, B, Tb, hMax, D, N, mode = 'line') {
  const d = Ta[0] * Tb[1] - Ta[1] * Tb[0];
  const mid = [(A[0] + B[0]) / 2, (A[1] + B[1]) / 2];
  if (mode === 'line' && Math.abs(d) > 1e-3) {
    const w = [B[0] - A[0], B[1] - A[1]];
    const t = (w[0] * Tb[1] - w[1] * Tb[0]) / d;
    const u = (w[0] * Ta[1] - w[1] * Ta[0]) / d;
    const tMax = 3 * hMax + D + 6;
    if (t >= -0.5 && u >= -0.5 && t <= tMax) return [A[0] + t * Ta[0], A[1] + t * Ta[1]];
  }
  if (N) {
    // Radial corner on the CCW sweep from A to B (arms are sorted CCW around the node).
    const va = [A[0] - N[0], A[1] - N[1]], vb = [B[0] - N[0], B[1] - N[1]];
    const la = Math.hypot(va[0], va[1]), lb = Math.hypot(vb[0], vb[1]);
    if (la > 1e-6 && lb > 1e-6) {
      const ta = Math.atan2(va[1], va[0]);
      let sweep = Math.atan2(vb[1], vb[0]) - ta;
      while (sweep <= 0) sweep += Math.PI * 2;
      while (sweep > Math.PI * 2) sweep -= Math.PI * 2;
      const phi = ta + sweep / 2;
      const R = Math.max(la, lb) + 0.25 * hMax;
      return [N[0] + Math.cos(phi) * R, N[1] + Math.sin(phi) * R];
    }
  }
  return mid;
}

// ---------- markings (decals) ------------------------------------------------------------------
// Quads along the curve between lateral offsets lat0..lat1 and arc length sa..sb.
function ribbon(out, curve, sa, sb, lat0, lat1, yOff, col, opts = {}) {
  const span = sb - sa;
  if (span <= 0.01) return;
  const steps = Math.max(1, Math.ceil(span / 1.0));
  const dims = opts.dims;
  const cfOf = () => 1;
  let prev = null;
  for (let k = 0; k <= steps; k++) {
    const s = sa + (span * k) / steps;
    const p = pointAt(curve, s);
    const nx = -p.tz, nz = p.tx;
    const yAt = lat => yOff + (dims && opts.crown ? crownAt(dims, lat) * cfOf() : 0);
    const cur = [
      [p.x + nx * lat0, yAt(lat0), p.z + nz * lat0],
      [p.x + nx * lat1, yAt(lat1), p.z + nz * lat1],
    ];
    if (prev) out.quad(prev[0], prev[1], cur[1], cur[0], { col, mat: -1, expect: UP });
    prev = cur;
  }
}

function emitMarkings(decal, mb, info) {
  const { curve, dims, s0, s1, road, degA, degB, D0, D1 } = info;
  if (s1 - s0 < 2) return;
  const hw = dims.hw, mh = dims.mh, lw = dims.laneWidth;
  const crown = !road.median;
  const edgeW = 0.14, dashLen = 3, dashGap = 9;
  const line = (lat, col, w = edgeW, dashed = false) => {
    if (!dashed) return ribbon(decal, curve, s0, s1, lat - w / 2, lat + w / 2, 0.012, col, { dims, crown });
    for (let s = s0; s < s1; s += dashLen + dashGap) {
      ribbon(decal, curve, s, Math.min(s + dashLen, s1), lat - w / 2, lat + w / 2, 0.012, col, { dims, crown });
    }
  };
  // outer edge lines
  line(-(hw - 0.3), COL.white);
  line(hw - 0.3, COL.white);
  if (road.median && mh > 0) {
    line(-(mh + 0.15), COL.yellow, 0.12);
    line(mh + 0.15, COL.yellow, 0.12);
  } else {
    // double yellow centre line
    line(-0.16, COL.yellow, 0.1);
    line(0.16, COL.yellow, 0.1);
  }
  // lane dividers
  for (let k = 1; k < dims.lanesPerDir; k++) {
    const lat = mh + k * lw;
    line(-lat, COL.white, 0.12, true);
    line(lat, COL.white, 0.12, true);
  }
  // stop lines and zebra crossings on approaches that meet a junction
  const cross = (sa, sb, stop) => {
    if (stop) ribbon(decal, curve, sa, sa + 0.45, -hw + 0.1, hw - 0.1, 0.013, COL.white, { dims, crown });
    for (let lat = -hw + 0.2; lat < hw - 0.4; lat += 1.1) {
      ribbon(decal, curve, sa + 1.5, sa + 4.6, lat, lat + 0.55, 0.013, COL.white, { dims, crown: false });
    }
  };
  if (degA >= 2 && road.crosswalks) cross(s0 + 0.2, s0, true);
  if (degB >= 2 && road.crosswalks) {
    // zebra reversed from the far end: compute with start index at s1 side
    const sa = s1 - 4.6;
    ribbon(decal, curve, s1 - 0.65, s1 - 0.2, -hw + 0.1, hw - 0.1, 0.013, COL.white, { dims, crown });
    for (let lat = -hw + 0.2; lat < hw - 0.4; lat += 1.1) {
      ribbon(decal, curve, sa, sa + 3.1, lat, lat + 0.55, 0.013, COL.white, { dims, crown: false });
    }
  }
}

// ---------- road-level features (lights, bollards, rails, bumps, potholes, construction) ----
function emitRoadFeatures(mb, decal, info, project, opts) {
  const { road, curve, dims, s0, s1 } = info;
  const r = rng(road.seed || 1);
  const hw = dims.hw, h2L = dims.h2L, h2R = dims.h2R, h1 = dims.h1;
  const span = s1 - s0;
  if (span < 4) return;
  // Street lights: alternating sides on the pavement, arms reaching over the carriage.
  if (road.lightSpacing > 0 && (opts.lights !== false)) {
    let k = 0;
    for (let s = s0 + road.lightSpacing * 0.5; s < s1 - 2; s += road.lightSpacing, k++) {
      const side = k % 2 === 0 ? -1 : 1;
      const lat = side * (side < 0 ? h2L - 0.7 : h2R - 0.7);
      const p = pointAt(curve, s);
      emitStreetlight(mb, p, lat, road.lightSpacing > 0 ? 0 : 0, side);
    }
  }
  // Bollards on the kerb line
  if (road.bollards && road.bollards.on) {
    const sp = Math.max(1.5, road.bollards.spacing || 4);
    for (let s = s0 + 1; s < s1 - 1; s += sp) {
      for (const side of [-1, 1]) {
        const lat = side * (h1 + 0.35);
        const p = pointAt(curve, s);
        emitBollard(mb, p.x + (-p.tz) * lat, p.z + p.tx * lat, 0, road.bollards.design, 1, [p.tx, p.tz]);
      }
    }
  }
  // Guard rails (W-beam on the shoulder)
  if (road.railings && road.railings.on) {
    for (const side of [-1, 1]) {
      const lat = side * ((side < 0 ? h2L : h2R) - 0.25);
      emitRailing(mb, curve, s0 + 0.3, s1 - 0.3, lat);
    }
  }
  // Speed bumps: across the carriage
  for (let i = 0; i < (road.speedBumps || 0); i++) {
    const s = s0 + span * (i + 1) / (road.speedBumps + 1);
    emitSpeedBump(mb, curve, dims, s);
  }
  // Potholes: decals, random placement away from junctions
  for (let i = 0; i < (road.potholes || 0); i++) {
    const s = r.range(s0 + 3, s1 - 3);
    const lat = r.range(-hw + 1.0, hw - 1.0);
    const rad = r.range(0.25, 0.5);
    emitPotholeDecal(decal, curve, s, lat, rad, r, dims);
  }
  // Construction zone: barriers, cones and dug patch in the outermost lane on one side
  if (road.construction && road.construction.on) {
    const c = road.construction;
    const sA = s0 + span * c.from, sB = s0 + span * c.to;
    const sign = c.side === 'left' ? 1 : -1;
    const lane = dims.laneWidth;
    const innerEdge = sign * (hw - lane);
    emitJerseyBarrier(mb, curve, sA, sB, innerEdge, dims);
    emitConeLine(mb, curve, sA - 6, sB + 6, sign * (hw - lane - 1.2 > dims.mh ? hw - lane - 1.2 : hw - lane - 0.4), 5);
    // dug patch on the closed lane
    const lo = Math.min(sign * hw, innerEdge), hi = Math.max(sign * hw, innerEdge);
    ribbon(decal, curve, sA, sB, lo + 0.1, hi - 0.1, 0.006, [0.16, 0.15, 0.14], { dims, crown: road.median ? false : true });
    for (let s = sA; s < sB; s += 2.5) {
      const rr = r.range(0.3, 0.55);
      emitPotholeDecal(decal, curve, s + 1, sign * (hw - lane * 0.45), rr, r, dims, [0.22, 0.21, 0.2]);
    }
  }
}

function emitRailing(mb, curve, sa, sb, lat) {
  // Posts every 2 m
  for (let s = sa; s <= sb; s += 2.0) {
    const p = pointAt(curve, s);
    const x = p.x + (-p.tz) * lat, z = p.z + p.tx * lat;
    prismPoly(mb, squareXZ(x, z, 0.07, p.tx, p.tz), 0, 0.82, [0.6, 0.62, 0.65], 1);
  }
  // Profile swept along the curve, W-beam approximated by a hexagonal rail
  const prof = [[-0.03, 0.55], [0.03, 0.55], [0.05, 0.62], [0.03, 0.7], [-0.03, 0.7], [-0.05, 0.62]];
  sweepClosed(mb, curve, sa, sb, lat, prof, [0.78, 0.8, 0.84], -1, 1.0);
}

function emitSpeedBump(mb, curve, dims, s) {
  const prof = [[-0.45, 0], [-0.2, 0.09], [0.2, 0.09], [0.45, 0]];
  const latSpans = [[-dims.hw, -dims.mh], [dims.mh, dims.hw]];
  for (const [la, lb] of latSpans) {
    const lats = [la, (la + lb) / 2, lb];
    for (let li = 0; li < 2; li++) {
      for (let pi = 0; pi < prof.length - 1; pi++) {
        const top = (prof[pi][1] + prof[pi + 1][1]) > 0.05;
        const col = top ? COL.yellow : [0.12, 0.12, 0.12];
        const quadAt = (ds0, h0, ds1, h1, lat0, lat1) => {
          const pt = (ds, h, lat) => {
            const p = pointAt(curve, s + ds);
            const cfy = crownAt(dims, lat);
            return [p.x + (-p.tz) * lat, cfy + h, p.z + p.tx * lat];
          };
          return [pt(ds0, h0, lat0), pt(ds0, h0, lat1), pt(ds1, h1, lat1), pt(ds1, h1, lat0)];
        };
        const q = quadAt(prof[pi][0], prof[pi][1], prof[pi + 1][0], prof[pi + 1][1], lats[li], lats[li + 1]);
        mb.quad(q[0], q[1], q[2], q[3], { col, mat: -1, expect: top ? UP : [0, 1, 0] });
      }
    }
  }
}

function emitPotholeDecal(decal, curve, s, lat, rad, r, dims, colOverride) {
  const p = pointAt(curve, s);
  const cx = p.x + (-p.tz) * lat, cz = p.z + p.tx * lat;
  const y = crownAt(dims, lat) + 0.01;
  const seg = 14;
  const rings = [
    { f: 0.0, col: [0.02, 0.02, 0.025] },
    { f: 0.55, col: [0.04, 0.04, 0.045] },
    { f: 0.85, col: colOverride || [0.11, 0.11, 0.12] },
    { f: 1.0, col: colOverride || [0.16, 0.16, 0.17] },
  ];
  const ang = i => (i / seg) * Math.PI * 2;
  for (let k = 0; k < rings.length - 1; k++) {
    const a = rings[k], b = rings[k + 1];
    for (let i = 0; i < seg; i++) {
      const j = (i + 1) % seg;
      const ring = (f, idx, jitter) => {
        const rr = rad * f * (1 + jitter[idx]);
        return [cx + Math.cos(ang(idx)) * rr, y, cz + Math.sin(ang(idx)) * rr];
      };
      const jit = Array.from({ length: seg }, () => (r() - 0.5) * 0.12);
      if (k === 0) {
        decal.tri([cx, y, cz], ring(b.f, j, jit), ring(b.f, i, jit), { col: b.col, mat: -1, expect: UP });
      } else {
        decal.quad(ring(a.f, i, jit), ring(a.f, j, jit), ring(b.f, j, jit), ring(b.f, i, jit), { col: b.col, mat: -1, expect: UP });
      }
    }
  }
}

function emitJerseyBarrier(mb, curve, sa, sb, lat, dims) {
  const prof = [[-0.3, 0], [-0.12, 0.8], [0.12, 0.8], [0.3, 0]];
  sweepClosed(mb, curve, sa, sb, lat, prof, [0.8, 0.8, 0.78], 1, 1.0);
}

function emitConeLine(mb, curve, sa, sb, lat, spacing) {
  for (let s = sa; s <= sb; s += spacing) {
    const p = pointAt(curve, s);
    emitCone(mb, p.x + (-p.tz) * lat, p.z + p.tx * lat);
  }
}

// ---------- props -----------------------------------------------------------------------------
function emitStreetlight(mb, p, lat, _unused, side) {
  const x = p.x + (-p.tz) * lat, z = p.z + p.tx * lat;
  const poleH = 8.4;
  const profile = [
    [0.24, 0], [0.24, 0.22], [0.15, 0.3], [0.11, 0.36], [0.095, 0.5], [0.085, 2.0],
    [0.08, poleH - 0.4], [0.07, poleH - 0.1], [0.07, poleH], [0, poleH],
  ];
  lathe(mb, [x, 0, z], profile, 14, () => [0.42, 0.44, 0.47], 1);
  // curved arm toward the carriage
  const armLat = -side * 2.4;
  const path = [];
  for (let i = 0; i <= 8; i++) {
    const t = i / 8;
    const l = lat + (armLat - lat) * t;
    const h = poleH - 0.1 + 0.55 * Math.sin(Math.PI * t * 0.5) - 0.15 * t;
    path.push([p.x + (-p.tz) * l, h, p.z + p.tx * l]);
  }
  tube(mb, path, 0.05, 6, [0.42, 0.44, 0.47], 1);
  // luminaire head + emissive lens
  const hx = p.x + (-p.tz) * armLat, hz = p.z + p.tx * armLat;
  const hh = poleH - 0.3;
  const hd = 0.22, hl = 0.55;
  const top = poleH - 0.2;
  const sq = [[hx - p.tx * hl / 2 - (-p.tz) * hd / 2, hz - p.tz * hl / 2 + p.tx * hd / 2],
    [hx + p.tx * hl / 2 - (-p.tz) * hd / 2, hz + p.tz * hl / 2 + p.tx * hd / 2],
    [hx + p.tx * hl / 2 + (-p.tz) * hd / 2, hz + p.tz * hl / 2 - p.tx * hd / 2],
    [hx - p.tx * hl / 2 + (-p.tz) * hd / 2, hz - p.tz * hl / 2 - p.tx * hd / 2]];
  prismFromRing(mb, sq, hh - 0.12, top, [0.3, 0.32, 0.36], 1);
  const lens = sq.map(q => [q[0], hh - 0.125, q[1]]);
  mb.quad(lens[0], lens[1], lens[2], lens[3], { col: [1.0, 0.86, 0.6], mat: -2, expect: [0, -1, 0] });
}

function emitBollard(mb, x, z, y0, design, scale = 1, tangent = [1, 0]) {
  if (design === 'square') {
    const w = 0.22 * scale;
    prismPoly(mb, squareXZ(x, z, w, tangent[0], tangent[1]), y0, y0 + 0.9 * scale, [0.62, 0.61, 0.58], 1);
    return;
  }
  if (design === 'retract') {
    const prof = [[0, 0], [0.13, 0], [0.13, 0.12], [0.12, 0.2], [0.12, 0.42], [0.12, 0.44], [0.12, 0.6], [0.12, 0.62], [0.11, 0.7], [0.105, 0.78], [0, 0.8]];
    lathe(mb, [x, y0, z], prof, 12, y => (y > 0.3 && y < 0.5 ? COL.yellow : [0.08, 0.08, 0.09]), 1);
    return;
  }
  if (design === 'ornate') {
    const prof = [[0, 0], [0.16, 0], [0.15, 0.05], [0.08, 0.1], [0.075, 0.2], [0.095, 0.24], [0.075, 0.28], [0.075, 0.6], [0.095, 0.66], [0.075, 0.7], [0.11, 0.75], [0.1, 0.8], [0.0, 0.92]];
    lathe(mb, [x, y0, z], prof, 14, y => (y > 0.5 && y < 0.7 ? [0.3, 0.32, 0.27] : [0.22, 0.23, 0.22]), 1);
    return;
  }
  // post with dome
  const prof = [[0, 0], [0.11, 0], [0.11, 0.02], [0.1, 0.1], [0.1, 0.8], [0.11, 0.84], [0.1, 0.9], [0.07, 0.94], [0, 0.96]];
  lathe(mb, [x, y0, z], prof, 14, () => [0.7, 0.72, 0.74], 1);
}

function emitCone(mb, x, z) {
  const prof = [[0.2, 0], [0.19, 0.02], [0.12, 0.2], [0.075, 0.42], [0.05, 0.5], [0.04, 0.6], [0.03, 0.7], [0, 0.72]];
  lathe(mb, [x, 0, z], prof, 10, y => (y > 0.3 && y < 0.5 ? [0.96, 0.96, 0.94] : [1.0, 0.42, 0.05]), -1);
  const b = 0.18;
  prismPoly(mb, squareXZ(x, z, b, 1, 0), 0, 0.012, [0.12, 0.12, 0.12], -1);
}

function emitManualProp(mb, decal, p, roadInfo) {
  const dirX = Math.sin(p.rot || 0), dirZ = Math.cos(p.rot || 0);
  switch (p.kind) {
    case 'streetlight': {
      // place the pole at the point; arm points along the road normal (left by default)
      const lat = -2.0;
      const tp = { x: p.x, z: p.z, tx: dirX, tz: dirZ };
      emitStreetlight(mb, { x: p.x, z: p.z, tx: dirX, tz: dirZ }, 0, 0, -1);
      return;
    }
    case 'bollard':
      emitBollard(mb, p.x, p.z, 0, p.design || 'post', p.scale || 1, [dirX, dirZ]);
      return;
    case 'cone':
      emitCone(mb, p.x, p.z);
      return;
    case 'pothole': {
      const r = rng(p.id.length * 7919 + Math.floor(Math.abs(p.x) * 13));
      emitPotholeDecalXY(decal, p.x, p.z, 0.35 + r() * 0.2);
      return;
    }
    case 'barrier': {
      const cs = [[-0.3, 0], [-0.12, 0.8], [0.12, 0.8], [0.3, 0]];
      const x0 = p.x, z0 = p.z;
      const nx = dirZ, nz = -dirX;
      const ring = cs.map(([l, y]) => [x0 + nx * l, y, z0 + nz * l]);
      for (let i = 0; i < cs.length; i++) {
        const j = (i + 1) % cs.length;
        const a = [ring[i][0] - dirX * 1.2, ring[i][1], ring[i][2] - dirZ * 1.2];
        const b = [ring[j][0] - dirX * 1.2, ring[j][1], ring[j][2] - dirZ * 1.2];
        const c = [ring[j][0] + dirX * 1.2, ring[j][1], ring[j][2] + dirZ * 1.2];
        const d = [ring[i][0] + dirX * 1.2, ring[i][1], ring[i][2] + dirZ * 1.2];
        mb.quad(a, b, c, d, { col: [0.8, 0.8, 0.78], mat: 1, expect: [nx * 0 + 0, 0, 0] });
      }
      return;
    }
    default:
      return;
  }
}

function emitPotholeDecalXY(decal, x, z, rad) {
  const y = 0.012;
  const seg = 14;
  const ring = (f, i) => [x + Math.cos((i / seg) * Math.PI * 2) * rad * f, y, z + Math.sin((i / seg) * Math.PI * 2) * rad * f];
  for (let i = 0; i < seg; i++) {
    const j = (i + 1) % seg;
    decal.tri([x, y, z], ring(0.6, j), ring(0.6, i), { col: [0.02, 0.02, 0.025], mat: -1, expect: UP });
    decal.quad(ring(0.6, i), ring(0.6, j), ring(1, j), ring(1, i), { col: [0.1, 0.1, 0.11], mat: -1, expect: UP });
  }
}

// ---------- generic geometry helpers ----------------------------------------------------------
export function squareXZ(x, z, w, tx, tz) {
  const h = w / 2;
  const nx = -tz, nz = tx;
  return [
    [x - tx * h - nx * h, z - tz * h - nz * h],
    [x + tx * h - nx * h, z + tz * h - nz * h],
    [x + tx * h + nx * h, z + tz * h + nz * h],
    [x - tx * h + nx * h, z - tz * h + nz * h],
  ];
}

function prismFromRing(mb, ring, y0, y1, col, mat) {
  const n = ring.length;
  const cx = ring.reduce((s, p) => s + p[0], 0) / n, cz = ring.reduce((s, p) => s + p[1], 0) / n;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    const a = [ring[i][0], y0, ring[i][1]], b = [ring[j][0], y0, ring[j][1]];
    const c = [ring[j][0], y1, ring[j][1]], d = [ring[i][0], y1, ring[i][1]];
    const e = [b[0] - a[0], 0, b[2] - a[2]];
    const out = normalize([e[2], 0, -e[0]]) || [0, 0, 1];
    const mid = [(a[0] + b[0]) / 2 - cx, 0, (a[2] + b[2]) / 2 - cz];
    const o = out[0] * mid[0] + out[2] * mid[2] < 0 ? [-out[0], 0, -out[2]] : out;
    mb.quad(a, b, c, d, { col, mat, expect: o });
  }
  const top = ring.map(p => [p[0], y1, p[1]]);
  for (let i = 1; i < n - 1; i++) mb.tri(top[0], top[i], top[i + 1], { col, mat, expect: UP });
}

export function prismPoly(mb, ring, y0, y1, col, mat) { prismFromRing(mb, ring, y0, y1, col, mat); }

// Revolve a (r,y) profile around a vertical axis at c.
export function lathe(mb, c, profile, seg, colorOf, mat) {
  const pts = profile;
  for (let i = 0; i < pts.length - 1; i++) {
    const [r0, y0] = pts[i], [r1, y1] = pts[i + 1];
    if (r0 === 0 && r1 === 0) continue;
    const ca = colorOf((y0 + y1) / 2);
    // outward normal of the profile segment
    const dr = r1 - r0, dy = y1 - y0;
    const nOut = normalize([dy, 0, -dr]) || [0, 1, 0];
    for (let k = 0; k < seg; k++) {
      const a0 = (k / seg) * Math.PI * 2, a1 = ((k + 1) / seg) * Math.PI * 2;
      const P = (r, y, a) => [c[0] + Math.cos(a) * r, c[1] + y, c[2] + Math.sin(a) * r];
      const N = (a) => normalize([Math.cos(a) * nOut[0], nOut[1] * 0 + (dy >= 0 ? -dr : dr) * 0, Math.sin(a) * nOut[2]]);
      const ny = (-dr) === 0 ? 0 : 0;
      const nrmAt = (a) => { const rr = Math.hypot(nOut[0], nOut[2]) || 1; return normalize([Math.cos(a) * dy, -dr, Math.sin(a) * dy]) || [0, 1, 0]; };
      const aa = P(r0, y0, a0), bb = P(r1, y1, a0), cc = P(r1, y1, a1), dd = P(r0, y0, a1);
      mb.quad(aa, bb, cc, dd, { col: ca, mat, normals: [nrmAt(a0), nrmAt(a0), nrmAt(a1), nrmAt(a1)] });
    }
  }
}

// Tube along a 3D polyline.
export function tube(mb, path, radius, sides, col, mat) {
  for (let i = 0; i < path.length - 1; i++) {
    const a = path[i], b = path[i + 1];
    const t = normalize([b[0] - a[0], b[1] - a[1], b[2] - a[2]]) || [0, 1, 0];
    let ref = Math.abs(t[1]) > 0.9 ? [1, 0, 0] : [0, 1, 0];
    const u = normalize(cross(t, ref)) || [1, 0, 0];
    const v = cross(u, t);
    const ring = (p, k) => {
      const a0 = (k / sides) * Math.PI * 2;
      const dx = Math.cos(a0) * u[0] + Math.sin(a0) * v[0];
      const dy = Math.cos(a0) * u[1] + Math.sin(a0) * v[1];
      const dz = Math.cos(a0) * u[2] + Math.sin(a0) * v[2];
      return { p: [p[0] + dx * radius, p[1] + dy * radius, p[2] + dz * radius], n: [dx, dy, dz] };
    };
    for (let k = 0; k < sides; k++) {
      const a1 = ring(a, k), a2 = ring(a, k + 1), b1 = ring(b, k), b2 = ring(b, k + 1);
      mb.quad(a1.p, b1.p, b2.p, a2.p, { col, mat, normals: [a1.n, b1.n, b2.n, a2.n] });
    }
  }
}

// Sweep a closed (lateral, height) profile along the curve from sa to sb at lateral offset `lat`.
export function sweepClosed(mb, curve, sa, sb, lat, prof, col, mat, step = 1.0) {
  const samples = sampleRange(curve, sa, sb, step);
  const n = prof.length;
  for (let k = 0; k < samples.length - 1; k++) {
    const A = samples[k], B = samples[k + 1];
    for (let i = 0; i < n; i++) {
      const j = (i + 1) % n;
      const P = (smp, pl, py) => [smp.x + (-smp.tz) * (lat + pl), py, smp.z + smp.tx * (lat + pl)];
      const a = P(A, prof[i][0], prof[i][1]), b = P(A, prof[j][0], prof[j][1]);
      const c = P(B, prof[j][0], prof[j][1]), d = P(B, prof[i][0], prof[i][1]);
      mb.quad(a, b, c, d, { col, mat, expect: null });
    }
  }
}

// Vertical sweep of a 2D (lateral, y) ring along a polyline in XZ (used for helpers).
export function prismFromPolyline() {}

// ---------- decal helpers exposed for the editor (selection rings etc.) -----------------------
export function nearestOnNetwork(net, x, z) {
  let best = null;
  for (const [id, curve] of net.curves) {
    const c = closestPoint(curve, x, z);
    if (!best || c.dist < best.dist) best = { roadId: id, ...c };
  }
  return best;
}

export { ROAD_CLASSES, BOLLARD_DESIGNS, buildCurve, pointAt, sampleRange };
