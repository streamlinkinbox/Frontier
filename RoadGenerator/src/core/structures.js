// Ground, bridges, trenches, cut-and-cover tunnels and grade-separated crossings.
// Dimensions: deck slab 0.25 m; box girder depth 1.6 m (≈ L/20 for 30 m spans); clearance ≥ 5.2 m;
// piers at ≤ 32 m; hammerhead cap 1.2 m deep; parapet 0.9–1.1 m; retaining wall cope 0.3 m.
import { MeshBuilder } from "./mesh.js";
import { roadPoint, footprint, stationRange } from "./query.js";
import { box, loft, lathe } from "./primitives.js";
import { lerp, clamp, madd, mul, add, sub, norm, rightXZ, pointInPolygonXZ, mat4 } from "./math.js";
import { basisAlong } from "./furnish.js";
import { earClip } from "./junction.js";
import { buildTerrain, footprintField, buildGroundMesh, junctionRing, cornerBack } from "./terrain.js";

export const GROUND_Y = -0.02;
export const CLEARANCE = 5.2;

export function defaultStructureSettings() { return { ground: true, groundCell: 12, groundExtent: 160, bridges: true, pierSpacing: 30, trenches: true, xray: false }; }

/** Classify each road into segments [{kind: 'ground'|'bridge'|'trench'|'tunnel', s0, s1}] using elevation and crossings. */
export function classifyRoads(plan, crossings) {
  const out = new Map();
  for (const R of plan.roads.values()) {
    const { s0, s1 } = stationRange(R);
    const segs = [];
    const step = 2;
    let cur = null;
    for (let s = s0; s <= s1 + 1e-6; s += step) {
      const ss = Math.min(s, s1);
      const y = R.frameAt(ss).p[1];
      let kind = "ground";
      // fills (embankments) carry the road up to ~6.5 m; above that, or over an obstacle, it becomes a viaduct
      if (y > 6.5) kind = "bridge"; else if (y < -6.5) kind = "tunnel"; else if (y < -1.2) kind = "trench";
      // crossing over a lower road/rail without a node → bridge span (+ approach margin so abutments clear the obstacle)
      for (const c of crossings) if (c.upper === R && ss >= c.s0 - 8 && ss <= c.s1 + 8) kind = "bridge";
      if (R.road.bridge === "always") kind = "bridge"; if (R.road.bridge === "never" && kind === "bridge") kind = "ground";
      if (!cur || cur.kind !== kind) { cur = { kind, s0: ss, s1: ss }; segs.push(cur); } else cur.s1 = ss;
      if (ss >= s1) break;
    }
    // merge tiny segments
    const merged = [];
    for (const sg of segs) { const prev = merged[merged.length - 1]; if (prev && (sg.s1 - sg.s0 < 6 || prev.kind === sg.kind)) { prev.s1 = sg.s1; } else merged.push({ ...sg }); }
    out.set(R, merged);
  }
  return out;
}

/** Detect plan-view crossings between roads with vertical separation (grade separation without a node). */
export function findCrossings(plan) {
  const roads = [...plan.roads.values()];
  const polys = roads.map((R) => ({ R, pts: R.frames.map((F) => F.p) }));
  const out = [];
  for (let i = 0; i < roads.length; i++) for (let j = i + 1; j < roads.length; j++) {
    const A = roads[i], B = roads[j];
    if (A.road.a === B.road.a || A.road.a === B.road.b || A.road.b === B.road.a || A.road.b === B.road.b) continue;
    // polyline intersection search (XZ)
    const PA = polys[i].pts, PB = polys[j].pts;
    for (let a = 0; a + 1 < PA.length; a++) for (let b = 0; b + 1 < PB.length; b++) {
      const h = segInter(PA[a], PA[a + 1], PB[b], PB[b + 1]);
      if (!h) continue;
      const yA = lerp(PA[a][1], PA[a + 1][1], h.t), yB = lerp(PB[b][1], PB[b + 1][1], h.u);
      if (Math.abs(yA - yB) < 3) continue; // a true conflict → the user should add a node; ignore
      const upper = yA > yB ? A : B, lower = yA > yB ? B : A;
      const sU = upper === A ? lerp(A.stations[a], A.stations[a + 1], h.t) : lerp(B.stations[b], B.stations[b + 1], h.u);
      const sL = lower === A ? lerp(A.stations[a], A.stations[a + 1], h.t) : lerp(B.stations[b], B.stations[b + 1], h.u);
      const fpL = footprint(lower), half = (fpL.right - fpL.left) / 2;
      const dU = upper.frameAt(sU).dxz, dL = lower.frameAt(sL).dxz;
      const sinA = Math.max(0.35, Math.abs(dU[0] * dL[2] - dU[2] * dL[0])); // skew widens the span
      const span = half / sinA + 3;
      out.push({ upper, lower, sU, sL, s0: sU - span, s1: sU + span, lowerHalf: half, point: h.p });
    }
  }
  return out;
}
function segInter(p1, p2, p3, p4) {
  const d = (p2[0] - p1[0]) * (p4[2] - p3[2]) - (p2[2] - p1[2]) * (p4[0] - p3[0]);
  if (Math.abs(d) < 1e-9) return null;
  const t = ((p3[0] - p1[0]) * (p4[2] - p3[2]) - (p3[2] - p1[2]) * (p4[0] - p3[0])) / d;
  const u = ((p3[0] - p1[0]) * (p2[2] - p1[2]) - (p3[2] - p1[2]) * (p2[0] - p1[0])) / d;
  if (t < 0 || t > 1 || u < 0 || u > 1) return null;
  return { t, u, p: [lerp(p1[0], p2[0], t), 0, lerp(p1[2], p2[2], t)] };
}

/**
 * Build structures. Returns { solid: MeshBuilder, ground: MeshBuilder, instances: Map, segments, crossings }.
 * `rails` optional: [{ spline, length, elevated, tunnel, halfWidth }] to cut the ground for rail trenches/subway corridors.
 */
export function buildStructures(plan, S = defaultStructureSettings(), rails = []) {
  S = { ...defaultStructureSettings(), ...S };
  const crossings = findCrossings(plan);
  // roads passing over surface railways
  for (const rl of rails) if (!rl.tunnel) for (const R of plan.roads.values()) {
    const P = R.frames.map((F) => F.p);
    const n = Math.ceil(rl.length / 6), Q = Array.from({ length: n + 1 }, (_, i) => rl.spline.pointAt((i / n) * rl.length));
    for (let a = 0; a + 1 < P.length; a++) for (let b = 0; b + 1 < Q.length; b++) {
      const h = segInter(P[a], P[a + 1], Q[b], Q[b + 1]); if (!h) continue;
      const yA = lerp(P[a][1], P[a + 1][1], h.t), yB = lerp(Q[b][1], Q[b + 1][1], h.u);
      if (yA - yB < 3) continue;
      const sU = lerp(R.stations[a], R.stations[a + 1], h.t), half = rl.halfWidth + 2.5;
      crossings.push({ upper: R, lower: null, rail: rl, sU, s0: sU - half - 3, s1: sU + half + 3, lowerHalf: half, point: h.p });
    }
  }
  const segments = classifyRoads(plan, crossings);
  // surface railways run on a formation 0.7 m below rail level: the ground is cut along the corridor and the
  // terrain hugs the formation edges (rail datum − 0.02) so the ballast shoulders meet the grass cleanly
  const railLines = [], railHoles = [];
  for (const rl of rails) if (!rl.tunnel) {
    const n = Math.max(2, Math.ceil(rl.length / 4)), L = [], Rr = [];
    for (let i = 0; i <= n; i++) { const F = rl.spline.frameAt((i / n) * rl.length), r = rightXZ(F.dxz); const y = F.p[1] - 0.02; L.push([F.p[0] - r[0] * 4.2, y, F.p[2] - r[2] * 4.2]); Rr.push([F.p[0] + r[0] * 4.2, y, F.p[2] + r[2] * 4.2]); }
    railLines.push(L, Rr); railHoles.push(corridorSpline(rl.spline, rl.length, 4.15));
  }
  const terrain = buildTerrain(plan, S, railLines);
  const mb = new MeshBuilder("structures");
  const concrete = mb.material("concrete"), steel = mb.material("metalPainted"), galv = mb.material("steelGalv"), lamp = mb.material("lamp"), grass = mb.material("grass");
  const instances = new Map();
  const inst = (prop, variant, m) => { const k = prop + "|" + variant; (instances.get(k) || instances.set(k, { prop, variant, group: "", mats: [] }).get(k)).mats.push(m); };
  const holes = [...railHoles]; // polygons (XZ) where the ground is removed
  const xrayHoles = [];
  for (const [R, segs] of segments) {
    const fp = footprint(R);
    for (const sg of segs) {
      if (sg.kind === "bridge" && S.bridges) buildBridge(mb, R, sg, fp, S, inst, concrete, steel, galv, terrain);
      if ((sg.kind === "trench" || sg.kind === "tunnel") && S.trenches) {
        buildTrench(mb, R, sg, fp, sg.kind === "tunnel", concrete, lamp, terrain);
        const poly = corridor(R, sg.s0 - 1, sg.s1 + 1, fp.left - 0.6, fp.right + 0.6);
        if (sg.kind === "trench") holes.push(poly); else xrayHoles.push(poly);
      }
      if (sg.kind === "ground") buildEmbankment(mb, R, sg, fp, grass, terrain);
    }
  }
  // junction slabs: viaduct decks when any arm arrives on a bridge (or the node is high), grass skirts otherwise
  for (const J of plan.junctions || []) {
    const onBridge = J.node.p[1] > 6.5 || J.arms.some((arm) => { const R = arm.R, s = arm.isStart ? R.cutA + 0.5 : R.length - R.cutB - 0.5; return (segments.get(R) || []).some((sg) => sg.kind === "bridge" && s >= sg.s0 - 1 && s <= sg.s1 + 1); });
    if (onBridge && S.bridges) buildJunctionDeck(mb, J, concrete, galv, inst, terrain); else buildJunctionSkirt(mb, J, grass, terrain);
  }
  for (const rl of rails) if (rl.tunnel) xrayHoles.push(corridorSpline(rl.spline, rl.length, rl.halfWidth + 1.5));
  const field = footprintField(plan, terrain, holes);
  const ground = S.ground ? buildGroundMesh(plan, terrain, field, xrayHoles, S) : null;
  mb.compact();
  return { solid: mb, ground, instances, segments, crossings, terrain, field };
}

function corridor(R, s0, s1, xl, xr) {
  const left = [], right = [];
  const n = Math.max(2, Math.ceil((s1 - s0) / 4));
  for (let i = 0; i <= n; i++) { const s = clamp(lerp(s0, s1, i / n), 0, R.length); left.push(roadPoint(R, s, xl)); right.push(roadPoint(R, s, xr)); }
  return [...left, ...right.reverse()];
}
function corridorSpline(spline, length, hw) {
  const left = [], right = [];
  const n = Math.max(2, Math.ceil(length / 5));
  for (let i = 0; i <= n; i++) { const F = spline.frameAt((i / n) * length); const r = rightXZ(F.dxz); left.push(madd(F.p, r, -hw)); right.push(madd(F.p, r, hw)); }
  return [...left, ...right.reverse()];
}

/** Grass embankment / cutting skirts (1:2 slope) where a ground segment sits above or below grade. */
function buildEmbankment(mb, R, sg, fp, grass, terrain) {
  const n = Math.max(1, Math.ceil((sg.s1 - sg.s0) / 4));
  for (const side of [-1, 1]) {
    const edge = side > 0 ? fp.right + 0.02 : fp.left - 0.02;
    const rows = [];
    let any = false;
    for (let i = 0; i <= n; i++) {
      const s = lerp(sg.s0, sg.s1, i / n);
      const top = roadPoint(R, s, edge, -0.03);
      const h = top[1] - terrain.heightAt(top[0], top[2]);
      if (Math.abs(h) > 0.02) any = true;
      const run = Math.max(0.05, Math.abs(h) * 2);
      const toe = roadPoint(R, s, edge + side * run, 0); toe[1] = terrain.heightAt(toe[0], toe[2]) - 0.01;
      const mid = roadPoint(R, s, edge + side * run * 0.5, 0); mid[1] = lerp(top[1], toe[1], 0.5);
      rows.push([top, mid, toe].map((q) => mb.vertex(q, [q[0], q[2]], [0, 1, 0.5, 0])));
    }
    if (any) mb.gridAuto(grass, rows, R.road.id);
  }
}

/** Outline of a junction slab 5 cm outside the back of the footway kits (closed polygon). */
function junctionOutline(J) { return junctionRing(J, 0.05); }
/** Viaduct deck under an elevated junction: outline wall (box girder depth), ear-clipped soffit, parapet rail, central pier. */
function buildJunctionDeck(mb, J, concrete, galv, inst, terrain) {
  const ring = junctionOutline(J); if (ring.length < 4) return;
  const depth = 1.8, owner = J.node.id;
  const top = ring.map((q) => [q[0], q[1] - 0.02, q[2]]);
  const bot = ring.map((q) => [q[0], q[1] - depth, q[2]]);
  const centroid = ring.reduce((a, p) => add(a, mul(p, 1 / ring.length)), [0, 0, 0]);
  loft(mb, concrete, [top, bot], { closed: true, owner, facing: (c) => [c[0] - centroid[0], 0, c[2] - centroid[2]] });
  // soffit (faces down): ear-clip the bottom ring then flip
  const ids = bot.map((q) => mb.vertex(q, [q[0], q[2]], [0, 1, 0.5, 0]));
  for (const [a, b, c] of earClip(bot)) mb.tri(concrete, ids[a], ids[c], ids[b], owner);
  // parapet upstand + railing around the outline (skip gaps where arms leave: those are the straight cut rows)
  for (let k = 0; k < ring.length; k++) {
    const a = ring[k], b = ring[(k + 1) % ring.length];
    const len = Math.hypot(b[0] - a[0], b[2] - a[2]);
    if (len > 6) continue; // arm cut row → opening for the road
    const inward = norm([centroid[0] - a[0], 0, centroid[2] - a[2]]);
    const a0 = madd(a, inward, 0.05), b0 = madd(b, inward, 0.05);
    const rows = [[a0, madd(a0, [0, 1, 0], 0.45), madd(madd(a0, [0, 1, 0], 0.45), inward, 0.3), madd(a0, inward, 0.3)], [b0, madd(b0, [0, 1, 0], 0.45), madd(madd(b0, [0, 1, 0], 0.45), inward, 0.3), madd(b0, inward, 0.3)]].map((r) => r.map((q) => mb.vertex(q, [q[0], q[2]], [0, 1, 0.5, 0])));
    mb.gridFacing(concrete, rows, owner, (c) => [c[0] - centroid[0], 0.6, c[2] - centroid[2]]);
    if (len > 1.5) inst("guardrailPanel", "", basisAlong(madd(a0, [0, 1, 0], 0.45), sub(b0, a0)));
  }
  // pier under the centre + one per 30 m of outline perimeter spread over the slab
  const groundY = terrain.heightAt(centroid[0], centroid[2]) - 0.5, deckY = J.node.p[1] - depth;
  if (deckY - groundY > 1.5) {
    const pier = new MeshBuilder("pier");
    box(pier, pier.material("concrete"), [0, deckY - 0.6, 0], [4.5, 1.2, 4.5], { bevel: 0.05 });
    lathe(pier, pier.material("concrete"), [{ r: 1.1, y: groundY }, { r: 1.1, y: deckY - 1.2 }], 16, [0, 0, 0]);
    box(pier, pier.material("concrete"), [0, groundY + 0.2, 0], [5, 0.4, 5]);
    pier.transform((q) => [q[0] + centroid[0], q[1], q[2] + centroid[2]]);
    mb.append(pier);
  }
}
function buildJunctionSkirt(mb, J, grass, terrain) {
  for (const c of J.corners || []) {
    const back = cornerBack(c, 0.02); if (!back.length) continue;
    const Nn = c.path.normals;
    const rows = []; let any = false;
    for (let k = 0; k < back.length; k++) {
      const top = [back[k][0], back[k][1] - 0.03, back[k][2]];
      const h = top[1] - terrain.heightAt(top[0], top[2]); if (Math.abs(h) > 0.02) any = true;
      const run = Math.max(0.05, Math.abs(h) * 2);
      const toe = madd(top, Nn[k], run); toe[1] = terrain.heightAt(toe[0], toe[2]) - 0.01;
      const mid = madd(top, Nn[k], run * 0.5); mid[1] = lerp(top[1], toe[1], 0.5);
      rows.push([top, mid, toe].map((q) => mb.vertex(q, [q[0], q[2]], [0, 1, 0.5, 0])));
    }
    if (any) mb.gridAuto(grass, rows, J.node.id);
  }
}

function buildBridge(mb, R, sg, fp, S, inst, concrete, steel, galv, terrain) {
  const L = sg.s1 - sg.s0; if (L < 4) return;
  const sec = R.section;
  const edgeL = fp.left - 0.05, edgeR = fp.right + 0.05;
  const depth = clamp(L / 18, 1.4, 2.6);
  // deck: under-slab + box girder (trapezoid) as a loft of rings along the span
  const n = Math.max(2, Math.ceil(L / 4));
  const rings = [];
  for (let i = 0; i <= n; i++) {
    const s = lerp(sg.s0, sg.s1, i / n);
    const pL = roadPoint(R, s, edgeL, -0.02), pR = roadPoint(R, s, edgeR, -0.02);
    const slab = 0.28;
    const webL = roadPoint(R, s, edgeL + 1.6, -slab), webR = roadPoint(R, s, edgeR - 1.6, -slab);
    const botL = roadPoint(R, s, edgeL + 2.6, -depth), botR = roadPoint(R, s, edgeR - 2.6, -depth);
    const sL = roadPoint(R, s, edgeL, -slab), sR = roadPoint(R, s, edgeR, -slab);
    rings.push([pR, sR, webR, botR, botL, webL, sL, pL]);
  }
  loft(mb, concrete, rings, { closed: true, owner: R.road.id });
  // parapets: concrete upstand 0.45 m + steel railing panels; both edges
  for (const side of [-1, 1]) {
    const x = side > 0 ? edgeR - 0.25 : edgeL + 0.25;
    const rows = [];
    for (let i = 0; i <= n; i++) { const s = lerp(sg.s0, sg.s1, i / n); rows.push([roadPoint(R, s, x - side * 0.2, 0), roadPoint(R, s, x - side * 0.2, 0.45), roadPoint(R, s, x + side * 0.2, 0.45), roadPoint(R, s, x + side * 0.2, 0)].map((p) => mb.vertex(p, [s, 0], [0, 1, 0.5, 0]))); }
    mb.gridFacing(concrete, rows, R.road.id, (c) => { const q = roadPoint(R, (sg.s0 + sg.s1) / 2, 0, 0); return [-side * (c[0] - q[0]) * 0 + (q[0] - c[0]), 0, q[2] - c[2]]; });
    for (let s = sg.s0; s + 2 <= sg.s1; s += 2) { const a = roadPoint(R, s, x, 0.45), b = roadPoint(R, s + 2, x, 0.45); inst("guardrailPanel", "", basisAlong(side > 0 ? a : b, side > 0 ? sub(b, a) : sub(a, b))); }
  }
  // piers: hammerhead at regular spacing (skip the first/last 6 m so abutments take the ends)
  const spacing = S.pierSpacing, count = Math.floor((L - 8) / spacing);
  const width = edgeR - edgeL;
  for (let k = 1; k <= count; k++) {
    const s = sg.s0 + (L - (count - 1) * spacing) / 2 + (k - 1) * spacing;
    const F = R.frameAt(s);
    const deckY = roadPoint(R, s, 0, -depth)[1];
    const capW = width - 2.4, capH = 1.2, capD = 1.6;
    const cx = (edgeL + edgeR) / 2;
    const pc = roadPoint(R, s, cx, 0);
    const groundY = terrain.heightAt(pc[0], pc[2]) - 0.5;
    if (deckY - groundY < 1.5) continue;
    const m = basisAlong([pc[0], 0, pc[2]], F.dxz);
    // pier cap
    const cap = new MeshBuilder("cap"); box(cap, cap.material("concrete"), [0, deckY - capH / 2, 0], [capW, capH, capD], { bevel: 0.05 });
    // column(s): one wide column for decks < 14 m, two for wider decks
    const cols = width > 16 ? [-capW * 0.3, capW * 0.3] : [0];
    for (const cxl of cols) lathe(cap, cap.material("concrete"), [{ r: 0.9, y: groundY }, { r: 0.9, y: deckY - capH }], 16, [cxl, 0, 0]);
    // footing
    box(cap, cap.material("concrete"), [0, groundY + 0.2, 0], [capW * 0.5 + 2, 0.4, capD + 1.6]);
    cap.transform((p) => mat4.transformPoint(m, p));
    mb.append(cap);
  }
  // abutments / wing walls at the span ends when the deck is still above ground there
  for (const end of [sg.s0, sg.s1]) {
    const y = roadPoint(R, end, 0, 0)[1];
    const F = R.frameAt(end);
    const pc = roadPoint(R, end, (edgeL + edgeR) / 2, 0);
    const gy = terrain.heightAt(pc[0], pc[2]);
    if (y - gy < 1.5) continue;
    const m = basisAlong([pc[0], 0, pc[2]], F.dxz);
    const ab = new MeshBuilder("ab"); box(ab, ab.material("concrete"), [0, (y - depth + gy) / 2, 0], [width + 0.6, y - depth - gy + 0.05, 1.2]);
    ab.transform((p) => mat4.transformPoint(m, p)); mb.append(ab);
  }
}

function buildTrench(mb, R, sg, fp, covered, concrete, lamp, terrain) {
  const L = sg.s1 - sg.s0; if (L < 3) return;
  const n = Math.max(2, Math.ceil(L / 3));
  for (const side of [-1, 1]) {
    const x = side > 0 ? fp.right + 0.3 : fp.left - 0.3;
    const rows = [];
    for (let i = 0; i <= n; i++) {
      const s = lerp(sg.s0, sg.s1, i / n);
      const base = roadPoint(R, s, x, -0.3);
      const topY = covered ? roadPoint(R, s, 0, CLEARANCE + 0.6)[1] : Math.max(terrain.heightAt(base[0], base[2]), base[1] + 1.0);
      const inner = roadPoint(R, s, x, 0);
      // wall face (inner), cope (top), back face (outer)
      const row = [[inner[0], base[1], inner[2]], [inner[0], topY, inner[2]], [inner[0] + side * 0.4 * (R.frameAt(s).r[0]), topY + (covered ? 0 : 0.9), inner[2] + side * 0.4 * R.frameAt(s).r[2]], [inner[0] + side * 0.5 * R.frameAt(s).r[0], base[1], inner[2] + side * 0.5 * R.frameAt(s).r[2]]];
      rows.push(row.map((p) => mb.vertex(p, [s, p[1]], [0, 1, 0.5, 0])));
    }
    mb.gridFacing(concrete, rows, R.road.id, (c) => { const q = roadPoint(R, (sg.s0 + sg.s1) / 2, 0, 0); return [q[0] - c[0], 0, q[2] - c[2]]; });
  }
  if (covered) {
    // roof slab + portal beams + ceiling luminaires
    const rows = [];
    for (let i = 0; i <= n; i++) {
      const s = lerp(sg.s0, sg.s1, i / n);
      const y = CLEARANCE + 0.6;
      rows.push([roadPoint(R, s, fp.left - 0.3, y), roadPoint(R, s, fp.right + 0.3, y)].map((p) => mb.vertex(p, [s, p[0]], [0, 0.6, 0.5, 0])));
    }
    mb.gridFacing(concrete, rows, R.road.id, [0, -1, 0]); // ceiling faces down
    const rows2 = rows.map((r) => r.map((id) => { const p = mb.position(id); return mb.vertex([p[0], p[1] + 0.8, p[2]], [0, 0], [0, 1, 0.5, 0]); }));
    mb.gridFacing(concrete, rows2, R.road.id, [0, 1, 0]);
    for (let s = sg.s0 + 4; s < sg.s1 - 2; s += 8) { const p = roadPoint(R, s, 0, CLEARANCE + 0.5); box(mb, lamp, p, [1.2, 0.08, 0.25]); }
  }
}
