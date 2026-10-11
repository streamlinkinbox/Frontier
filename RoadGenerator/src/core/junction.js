// Junction + road geometry. Everything meets in ONE MeshBuilder with shared vertex rows:
//   road quad strip ──shares cut row── arm patch (Coons, all quads) ──shares chord── centre (all quads)
//   kerb/footway strips of roads share their end rows with the swept corner kits.
//
// Trim solving: each corner between adjacent arms is a line/arc/line fillet between the arm kerb lines.
// The required trim grows at acute angles (tan(θ/2) → 0), so cuts are solved per node from the fillet
// geometry instead of using a fixed trim — this keeps tight-angle merges clean (no overlapping slabs).
import { add, sub, mul, madd, normXZ, dotXZ, lineIntersectXZ, clamp, lerp, mix, dist, distXZ, wrapAngle, adaptiveStations, smoothstep, mod, TAU, bezierPoint, bezierTangent, rightXZ, norm } from "./math.js";
import { refineSection, blendKits } from "./profile.js";
import { makeFrameFn } from "./network.js";

export const CORNER_SEGS = 10; // segments per half corner → 20 per corner; K (arc midpoint) sits at index 10
const MARGIN = 1.2; // metres between the fillet tangent point and the road cut row

const between = (a, b) => Math.acos(clamp(dotXZ(a, b), -1, 1));

// ---------------------------------------------------------------------------------------------
// 1. Trim solving
// ---------------------------------------------------------------------------------------------
export function solveTrims(plan) {
  const diagnostics = [];
  // Harmonise opposite arms at 4-way junctions and transitions so centres can be pure quad grids.
  for (let iter = 0; iter < 8; iter++) {
    let changed = false;
    for (const N of plan.nodes.values()) {
      const arms = N.arms;
      const pairs = arms.length === 4 ? [[0, 2], [1, 3]] : arms.length === 2 && !jointIsWeld(arms) ? [[0, 1]] : [];
      for (const [i, j] of pairs) {
        const a = arms[i].R, b = arms[j].R;
        if (a === b) continue;
        const d = a.section.segments - b.section.segments;
        if (d > 0) { refineSection(b.section, d); changed = true; } else if (d < 0) { refineSection(a.section, -d); changed = true; }
      }
    }
    if (!changed) break;
  }
  for (const N of plan.nodes.values()) {
    N.corners = [];
    const arms = N.arms, n = arms.length;
    if (n === 0) continue;
    if (n === 1) { arms[0].cut = 0; continue; }
    if (n === 2 && jointIsWeld(arms)) { arms[0].cut = arms[1].cut = 0; N.weld = true; continue; }
    const minCut = 1.5;
    for (let i = 0; i < n; i++) {
      const A = arms[i], B = arms[(i + 1) % n];
      const o1 = madd(N.node.p, A.r, A.wRight), o2 = madd(N.node.p, B.r, -B.wLeft);
      // true angular gap from arm A to arm B (counter-clockwise in plan ordering)
      const phi = n === 1 ? TAU : mod(B.angle - A.angle, TAU) || TAU;
      const theta = Math.min(phi, TAU - phi);
      const kitMin = Math.max(A.kitRight.width, B.kitLeft.width) + 0.6;
      const corner = { i, j: (i + 1) % n, A, B, theta, phi, kind: "fillet", Rmin: kitMin };
      const X = lineIntersectXZ(o1, A.d, o2, B.d);
      const lateral = Math.abs(A.wRight - B.wLeft);
      if (Math.abs(phi - Math.PI) < Math.PI * 0.06 || !X) {
        corner.kind = "straight";
        const req = Math.max(minCut, 2 + lateral * 4);
        corner.reqA = req; corner.reqB = req;
      } else if (phi > Math.PI) {
        // reflex gap: kerbs diverge → convex sweep around the outside of the bend
        corner.kind = "convex";
        const req = Math.max(minCut, 2 + lateral * 2);
        corner.reqA = req; corner.reqB = req;
      } else {
        const Rreq = Math.max(N.node.radius, kitMin);
        corner.V = X.point; corner.sV = [X.s, X.t];
        corner.R = Rreq;
        corner.T = Rreq / Math.tan(theta / 2);
        corner.reqA = X.s + corner.T + MARGIN;
        corner.reqB = X.t + corner.T + MARGIN;
        if (theta < 0.35) diagnostics.push({ level: "warn", owner: N.node.id, message: `Very acute approach (${((theta * 180) / Math.PI).toFixed(0)}°) at ${N.node.name || N.node.id}; a long merge nose is generated.` });
      }
      N.corners.push(corner);
    }
    for (let i = 0; i < n; i++) {
      const left = N.corners[(i - 1 + n) % n], right = N.corners[i];
      arms[i].cut = Math.max(minCut, right.reqA, left.reqB);
    }
    // Balance: an arm squeezed between two long cuts gets a proportionate cut so its patch stays regular.
    if (n >= 3) for (let pass = 0; pass < 2; pass++) for (let i = 0; i < n; i++) {
      const a = arms[(i - 1 + n) % n].cut, b = arms[(i + 1) % n].cut;
      arms[i].cut = Math.max(arms[i].cut, 0.45 * Math.min(a, b));
    }
  }
  // Budget: a road must keep ≥2.5 m between its two cuts. Shrink fillets if needed.
  for (const R of plan.roads.values()) {
    const NA = plan.nodes.get(R.road.a), NB = plan.nodes.get(R.road.b);
    const armA = NA.arms.find((a) => a.R === R && a.isStart), armB = NB.arms.find((a) => a.R === R && !a.isStart);
    let cA = armA ? armA.cut : 0, cB = armB ? armB.cut : 0;
    const avail = R.length - 2.5;
    if (cA + cB > avail) {
      const k = Math.max(0, avail) / (cA + cB);
      diagnostics.push({ level: "error", owner: R.road.id, message: `${R.road.name || R.road.id} is too short (${R.length.toFixed(1)} m) for its junction fillets; corner radii reduced.` });
      cA *= k; cB *= k;
      if (armA) armA.cut = cA; if (armB) armB.cut = cB;
    }
    R.cutA = cA; R.cutB = cB;
  }
  // Re-fit fillet radii to the (possibly reduced) cuts, then finalise tangent points and arc centres.
  for (const N of plan.nodes.values()) {
    for (const c of N.corners || []) {
      if (c.kind !== "fillet") continue;
      const availA = c.A.cut - MARGIN - c.sV[0], availB = c.B.cut - MARGIN - c.sV[1];
      const T = Math.max(0.2, Math.min(c.T, availA, availB));
      if (T < c.T - 1e-6) {
        c.T = T; c.R = T * Math.tan(c.theta / 2);
        if (c.R < c.Rmin) diagnostics.push({ level: "warn", owner: N.node.id, message: `Corner radius at ${N.node.name || N.node.id} reduced to ${c.R.toFixed(1)} m (footway may pinch).` });
      }
      c.P1 = madd(c.V, c.A.d, c.T); c.P2 = madd(c.V, c.B.d, c.T);
      const bis = normXZ(add(c.A.d, c.B.d));
      c.C = madd(c.V, bis, c.R / Math.sin(c.theta / 2));
    }
  }
  plan.diagnostics = diagnostics;
  return diagnostics;
}

function jointIsWeld(arms) {
  const a = arms[0], b = arms[1];
  const gap = Math.abs(mod(b.angle - a.angle, TAU) - Math.PI);
  if (gap > 0.03) return false;
  return a.R.section.segments === b.R.section.segments && Math.abs(a.wRight - b.wLeft) < 0.01 && Math.abs(a.wLeft - b.wRight) < 0.01 && Math.abs(a.kitRight.width - b.kitLeft.width) < 0.01 && a.R.road.profile.id === b.R.road.profile.id;
}

// ---------------------------------------------------------------------------------------------
// 2. Road sweep (shared builder). Produces rows and keeps end rows for junction stitching.
// ---------------------------------------------------------------------------------------------
export function sweepRoads(plan, mb, settings = {}) {
  const step = settings.step ?? 2.0;
  for (const R of plan.roads.values()) {
    const frameFn = makeFrameFn(plan, R);
    R.frameAt = frameFn;
    const s0 = R.cutA, s1 = R.length - R.cutB;
    const stations = stationsBetween(R, s0, s1, step, settings);
    R.stations = stations;
    R.frames = stations.map(frameFn);
    const sec = R.section, kits = [R.kitL, R.kitR];
    const owner = R.road.id;
    const matIds = sec.cols.map((c) => mb.material(c.mat || "asphalt"));
    const rows = [], kitRows = [kits[0].strips.map(() => []), kits[1].strips.map(() => [])];
    const superFn = superelevationFn(R);
    R.bank = [];
    for (let k = 0; k < stations.length; k++) {
      const s = stations[k], F = R.frames[k];
      const bank = superFn(s);
      R.bank.push(bank);
      const row = [];
      for (let j = 0; j < sec.cols.length; j++) {
        const c = sec.cols[j];
        const y = c.y + bank * c.x;
        row.push(mb.vertex([F.p[0] + F.r[0] * c.x, F.p[1] + y, F.p[2] + F.r[2] * c.x], [s, c.x], [c.wear * R.road.wear, 1, variation(s, c.x), 0]));
      }
      rows.push(row);
      const kerbBase = [sec.cols[0].y + bank * sec.cols[0].x, sec.cols[sec.cols.length - 1].y + bank * sec.cols[sec.cols.length - 1].x];
      const widths = [sec.widthL, sec.widthR];
      for (let side = 0; side < 2; side++) {
        const sign = side === 0 ? -1 : 1;
        kits[side].strips.forEach((strip, si) => {
          const r = [];
          for (const v of strip.verts) {
            const off = sign * (widths[side] + v.d);
            r.push(mb.vertex([F.p[0] + F.r[0] * off, F.p[1] + kerbBase[side] + v.h, F.p[2] + F.r[2] * off], [s, off], [0, 1, variation(s, off), 0]));
          }
          kitRows[side][si].push(r);
        });
      }
    }
    // segment material comes from its OUTER column (left of centre → col j, right → col j+1)
    mb.gridAuto((j) => matIds[sec.cols[j].x + sec.cols[j + 1].x < 0 ? j : j + 1], rows, owner);
    for (let side = 0; side < 2; side++) kits[side].strips.forEach((strip, si) => mb.gridAuto(mb.material(strip.material), kitRows[side][si], owner));
    R.rows = rows;
    R.kitRows = kitRows;
    R.startRow = { cw: rows[0], kitL: kitRows[0].map((s) => s[0]), kitR: kitRows[1].map((s) => s[0]) };
    R.endRow = { cw: rows[rows.length - 1], kitL: kitRows[0].map((s) => s[s.length - 1]), kitR: kitRows[1].map((s) => s[s.length - 1]) };
  }
}

export function variation(s, x) { // low-frequency vertex variation channel for the material shader
  return 0.5 + 0.5 * Math.sin(s * 0.173 + x * 0.61) * Math.cos(s * 0.071 - x * 0.23);
}

function stationsBetween(R, s0, s1, step, settings) {
  if (s1 - s0 < 0.05) return [s0, s1];
  const sub = { length: s1 - s0, frameAt: (s) => R.spline.frameAt(s0 + s) };
  const st = adaptiveStations(sub, step, settings.maxAngle ?? (3 * Math.PI) / 180).map((s) => s0 + s);
  // add forced stations (speed bumps need rows at their edges so the slab carries the bump profile)
  const forced = [];
  for (const b of R.road.speedBumps || []) for (const d of [-2.2, -1.5, -0.6, 0, 0.6, 1.5, 2.2]) { const v = b + d; if (v > s0 + 0.3 && v < s1 - 0.3) forced.push(v); }
  if (forced.length) {
    const all = [...st, ...forced].sort((a, b) => a - b);
    const out = [all[0]];
    for (const v of all) if (v - out[out.length - 1] > 0.08) out.push(v);
    if (out[out.length - 1] < s1 - 1e-6) out.push(s1);
    return out;
  }
  return st;
}

/** Superelevation (banking) on fast roads: fades out near junctions. */
function superelevationFn(R) {
  const P = R.road.profile;
  if (P.speed < 70) return () => 0;
  const L = R.length;
  return (s) => {
    const k = R.spline.frameAt(s).curvature; // +ve turning right
    const e = clamp(-k * 90, -0.06, 0.06); // outer edge raised
    const fade = smoothstep(R.cutA, R.cutA + 25, s) * smoothstep(L - R.cutB, L - R.cutB - 25, s);
    return e * fade;
  };
}

// ---------------------------------------------------------------------------------------------
// 3. Junctions
// ---------------------------------------------------------------------------------------------
/** Arm-oriented cut row: carriageway from the arm's LEFT kerb to RIGHT kerb, kits as strips inner→outer. */
export function armRow(arm) {
  const row = arm.isStart ? arm.R.startRow : arm.R.endRow;
  if (arm.isStart) return { cw: row.cw, kitLeft: row.kitL, kitRight: row.kitR, cols: arm.R.section.cols };
  return { cw: [...row.cw].reverse(), kitLeft: row.kitR, kitRight: row.kitL, cols: [...arm.R.section.cols].reverse().map((c) => ({ ...c, x: -c.x })) };
}

export function buildJunctions(plan, mb, settings = {}) {
  const junctions = [];
  for (const N of plan.nodes.values()) {
    const n = N.arms.length;
    if (n < 2) continue;
    if (N.weld) { buildWeld(N, mb); continue; }
    junctions.push(buildJunction(plan, N, mb, settings));
  }
  mb.applyWelds();
  plan.junctions = junctions;
  return junctions;
}

/** Sample the corner path: E1 (arm A right kerb at its cut) → P1 → arc → P2 → E2 (arm B left kerb at cut). */
function cornerPath(N, c, mb) {
  const rowA = armRow(c.A), rowB = armRow(c.B);
  const E1i = rowA.cw[rowA.cw.length - 1], E2i = rowB.cw[0];
  const E1 = mb.position(E1i), E2 = mb.position(E2i);
  const segs = CORNER_SEGS;
  const pts = [], normals = [], arc = [];
  const y1 = E1[1], y2 = E2[1];
  if (c.kind === "straight") {
    const dir = sub(E2, E1); const nrm = normXZ([-dir[2], 0, dir[0]]);
    const out = dotXZ(nrm, sub(N.node.p, E1)) > 0 ? mul(nrm, -1) : nrm;
    for (let k = 0; k <= segs * 2; k++) { pts.push(mix(E1, E2, k / (segs * 2))); normals.push(out); arc.push(false); }
    return { pts, normals, arc, E1i, E2i, length: dist(E1, E2) };
  }
  if (c.kind === "convex") {
    // cubic from E1 (leaving along -dA) to E2 (arriving along +dB); handles ≈ 0.55·chord gives a near-circular sweep
    const chord = distXZ(E1, E2);
    const h = chord * 0.55;
    const p0 = [E1[0], 0, E1[2]], p3 = [E2[0], 0, E2[2]];
    const p1 = madd(p0, c.A.d, -h), p2 = madd(p3, c.B.d, -h);
    // arc-length resample so K lands at the true midpoint
    const dense = []; let acc = 0;
    for (let k = 0; k <= 200; k++) { const p = bezierPoint(p0, p1, p2, p3, k / 200); if (k) acc += distXZ(p, dense[k - 1].p); dense.push({ p, s: acc, t: k / 200 }); }
    for (let k = 0; k <= segs * 2; k++) {
      const target = (acc * k) / (segs * 2);
      let a = 0; while (a < 199 && dense[a + 1].s < target) a++;
      const f = dense[a + 1].s > dense[a].s ? (target - dense[a].s) / (dense[a + 1].s - dense[a].s) : 0;
      const t = dense[a].t + (dense[a + 1].t - dense[a].t) * f;
      const p = bezierPoint(p0, p1, p2, p3, t);
      const tg = normXZ(bezierTangent(p0, p1, p2, p3, t));
      // outward = away from the node
      let nr = rightXZ(tg); if (dotXZ(nr, sub(N.node.p, p)) > 0) nr = mul(nr, -1);
      p[1] = lerp(y1, y2, k / (segs * 2));
      pts.push(p); normals.push(nr); arc.push(false);
    }
    pts[0] = E1; pts[pts.length - 1] = E2;
    return { pts, normals, arc, E1i, E2i, length: acc };
  }
  const P1 = [c.P1[0], 0, c.P1[2]], P2 = [c.P2[0], 0, c.P2[2]], C = [c.C[0], 0, c.C[2]];
  const a1 = Math.atan2(P1[2] - C[2], P1[0] - C[0]);
  const sweep = wrapAngle(Math.atan2(P2[2] - C[2], P2[0] - C[0]) - a1);
  const arcLen = Math.abs(sweep) * c.R;
  const l1 = distXZ(E1, P1), l2 = distXZ(P2, E2);
  const half1 = l1 + arcLen / 2, half2 = arcLen / 2 + l2;
  const outA = c.A.r, outB = mul(c.B.r, -1);
  const at = (d) => {
    if (d <= l1 + 1e-9) { const t = l1 > 1e-9 ? d / l1 : 1; return [mix([E1[0], 0, E1[2]], P1, t), outA, false]; }
    if (d >= l1 + arcLen - 1e-9) { const t = l2 > 1e-9 ? (d - l1 - arcLen) / l2 : 0; return [mix(P2, [E2[0], 0, E2[2]], t), outB, false]; }
    const ang = a1 + sweep * ((d - l1) / arcLen);
    const p = [C[0] + Math.cos(ang) * c.R, 0, C[2] + Math.sin(ang) * c.R];
    return [p, normXZ(sub(C, p)), true];
  };
  for (let k = 0; k <= segs; k++) { const [p, nr, isArc] = at((half1 * k) / segs); pts.push(p); normals.push(nr); arc.push(isArc); }
  for (let k = 1; k <= segs; k++) { const [p, nr, isArc] = at(half1 + (half2 * k) / segs); pts.push(p); normals.push(nr); arc.push(isArc); }
  for (let k = 0; k < pts.length; k++) pts[k][1] = lerp(y1, y2, k / (pts.length - 1));
  pts[0] = E1; pts[pts.length - 1] = E2;
  return { pts, normals, arc, E1i, E2i, length: half1 + half2, arcLength: arcLen, l1, l2 };
}

/** Coons patch point. Edges are vertex-position arrays; u/v are index fractions so edge vertices are hit exactly. */
export function coons(bottom, top, left, right, u, v) {
  const B = sampleCurve(bottom, u), T = sampleCurve(top, u), L = sampleCurve(left, v), Rr = sampleCurve(right, v);
  const P00 = bottom[0], P10 = bottom[bottom.length - 1], P01 = top[0], P11 = top[top.length - 1];
  const out = [0, 0, 0];
  for (let k = 0; k < 3; k++) {
    out[k] = (1 - v) * B[k] + v * T[k] + (1 - u) * L[k] + u * Rr[k]
      - ((1 - u) * (1 - v) * P00[k] + u * (1 - v) * P10[k] + (1 - u) * v * P01[k] + u * v * P11[k]);
  }
  return out;
}
function sampleCurve(pts, t) {
  const n = pts.length - 1;
  if (n <= 0) return pts[0];
  const f = clamp(t, 0, 1) * n, i = Math.min(n - 1, Math.floor(f)), r = f - i;
  return mix(pts[i], pts[i + 1], r);
}
/**
 * Coons grid of vertex ids. bottom/top: u 0→1, left/right: v 0→1 (left[0]=bottom[0], right[0]=bottom[last],
 * left[last]=top[0], right[last]=top[last]). Returns rows (v) of ids including the given edges.
 */
export function coonsGrid(mb, bottomIds, topIds, leftIds, rightIds, color) {
  if (topIds.length !== bottomIds.length || rightIds.length !== leftIds.length) throw new Error(`coons grid mismatch u:${bottomIds.length}/${topIds.length} v:${leftIds.length}/${rightIds.length}`);
  const bottom = bottomIds.map((i) => mb.position(i)), top = topIds.map((i) => mb.position(i));
  const left = leftIds.map((i) => mb.position(i)), right = rightIds.map((i) => mb.position(i));
  const nu = bottomIds.length - 1, nv = leftIds.length - 1;
  const rows = [bottomIds];
  for (let k = 1; k < nv; k++) {
    const row = [leftIds[k]];
    for (let j = 1; j < nu; j++) {
      const p = coons(bottom, top, left, right, j / nu, k / nv);
      row.push(mb.vertex(p, [p[0], p[2]], color(j, k)));
    }
    row.push(rightIds[k]);
    rows.push(row);
  }
  rows.push(topIds);
  return rows;
}

function buildJunction(plan, N, mb, settings) {
  const owner = N.node.id;
  const asphalt = mb.material("asphalt");
  const arms = N.arms, n = arms.length;
  const corners = N.corners.map((c) => ({ ...c, path: cornerPath(N, c, mb) }));
  for (const c of corners) {
    const P = c.path.pts, ids = [];
    for (let k = 0; k < P.length; k++) {
      if (k === 0) ids.push(c.path.E1i);
      else if (k === P.length - 1) ids.push(c.path.E2i);
      else ids.push(mb.vertex(P[k], [P[k][0], P[k][2]], [0, 1, variation(P[k][0], P[k][2]), 0]));
    }
    c.ids = ids;
    c.K = ids[CORNER_SEGS];
  }
  const chords = [], armPatches = [];
  const yNode = N.node.p[1];
  for (let i = 0; i < n; i++) {
    const arm = arms[i];
    const row = armRow(arm);
    const cRight = corners[i], cLeft = corners[(i - 1 + n) % n];
    const KL = cLeft.K, KR = cRight.K;
    const pL = mb.position(KL), pR = mb.position(KR);
    const xs = row.cols.map((c) => c.x);
    const span = xs[xs.length - 1] - xs[0];
    let chord;
    if (n === 2 && i === 1) chord = [...chords[0]].reverse(); // a bend: both arms meet at one shared chord
    else {
      chord = [KL];
      for (let j = 1; j < row.cw.length - 1; j++) {
        const f = (xs[j] - xs[0]) / span;
        const p = mix(pL, pR, f);
        const crownLift = (yNode - lerp(pL[1], pR[1], f)) * Math.sin(Math.PI * f) * 0.6;
        p[1] += crownLift;
        chord.push(mb.vertex(p, [p[0], p[2]], [row.cols[j].wear * 0.6 * arm.R.road.wear, 1, variation(p[0], p[2]), 0]));
      }
      chord.push(KR);
    }
    chords.push(chord);
    const leftCol = cLeft.ids.slice(CORNER_SEGS).reverse(); // E2 (this arm's left kerb) → K
    const rightCol = cRight.ids.slice(0, CORNER_SEGS + 1); // E1 (this arm's right kerb) → K
    const rows = coonsGrid(mb, row.cw, chord, leftCol, rightCol, (j, k) => [row.cols[j].wear * 0.8 * arm.R.road.wear * (1 - (0.3 * k) / CORNER_SEGS), 1, 0.5, 0]);
    mb.gridAuto(asphalt, rows, owner);
    armPatches.push({ arm, rows, chord, cut: arm.cut, cols: row.cols });
  }
  const counts = chords.map((c) => c.length - 1);
  const centre = n === 2 ? { quads: true, kind: "bend" } : buildCentre(mb, chords, counts, asphalt, owner, yNode);
  if (!centre.quads) plan.diagnostics.push({ level: "warn", owner, message: `Centre of ${N.node.name || owner} uses a triangulated fill (arm counts ${counts.join("/")}).` });
  for (const c of corners) buildCornerKit(mb, c, owner, armRow(c.A).kitRight, armRow(c.B).kitLeft);
  const rec = { node: N.node, arms, corners, armPatches, chords, centre, n, type: n === 2 ? "bend" : n === 3 ? "T" : n === 4 ? "cross" : `${n}-way` };
  N.junction = rec;
  return rec;
}

function buildCentre(mb, chords, counts, mat, owner, yNode) {
  const n = chords.length;
  const colorC = () => [0.15, 1, 0.5, 0];
  if (n === 4 && counts[0] === counts[2] && counts[1] === counts[3]) {
    const bottom = chords[0]; // K30 → K01
    const top = [...chords[2]].reverse(); // K23 → K12
    const left = [...chords[3]].reverse(); // K30 → K23
    const right = chords[1]; // K01 → K12
    const rows = coonsGrid(mb, bottom, top, left, right, colorC);
    mb.gridAuto(mat, rows, owner);
    return { quads: true, rows, kind: "cross" };
  }
  const s = solveSpokes(counts);
  if (s) {
    const C = [0, yNode, 0];
    for (const ch of chords) { const k = mb.position(ch[0]); C[0] += k[0] / n; C[2] += k[2] / n; }
    const Cid = mb.vertex(C, [C[0], C[2]], colorC());
    const x = counts.map((_, i) => s[(i - 1 + n) % n]); // split index on chord i
    const mids = chords.map((ch, i) => ch[x[i]]);
    const spokes = mids.map((mid, i) => {
      const pm = mb.position(mid), ids = [Cid];
      for (let k = 1; k < s[i]; k++) { const p = mix(C, pm, k / s[i]); ids.push(mb.vertex(p, [p[0], p[2]], colorC())); }
      ids.push(mid);
      return ids; // C → M_i, s_i segments
    });
    for (let i = 0; i < n; i++) {
      const prev = (i - 1 + n) % n;
      const bottom = chords[prev].slice(x[prev]); // M_{i-1} → V_i   (s_i segments)
      const right = chords[i].slice(0, x[i] + 1); // V_i → M_i       (s_{i-1} segments)
      const top = spokes[i]; // C → M_i           (s_i segments)
      const left = [...spokes[prev]].reverse(); // M_{i-1} → C (s_{i-1} segments)
      const rows = coonsGrid(mb, bottom, top, left, right, colorC);
      mb.gridAuto(mat, rows, owner);
    }
    return { quads: true, kind: "midpoint", spokes: s };
  }
  const ring = [];
  for (let i = 0; i < n; i++) for (let j = 0; j < chords[i].length - 1; j++) ring.push(chords[i][j]);
  const pts = ring.map((i) => mb.position(i));
  for (const [a, b, c] of earClip(pts)) mb.tri(mat, ring[a], ring[b], ring[c], owner);
  return { quads: false, kind: "fan" };
}

/**
 * Midpoint-subdivision of an N-gon into N quad patches: spoke i (centre → midpoint of side i) has s_i
 * segments, and the quad at polygon vertex V_i pairs the second half of side i-1 with spoke i and the first
 * half of side i with spoke i-1, so   n_i = s_{i-1} + s_{i+1}.   Solve for positive integers.
 */
export function solveSpokes(counts) {
  const n = counts.length;
  const maxN = Math.max(...counts);
  const attempt = (s0, s1) => {
    const s = [s0, s1];
    for (let i = 2; i < n; i++) s.push(counts[i - 1] - s[i - 2]);
    if (counts[n - 1] !== s[n - 2] + s[0]) return null;
    if (counts[0] !== s[n - 1] + s[1]) return null;
    if (s.some((v) => v < 1)) return null;
    return s;
  };
  let best = null, bestMin = 0;
  for (let s0 = 1; s0 < maxN; s0++) for (let s1 = 1; s1 < maxN; s1++) {
    const s = attempt(s0, s1);
    if (s) { const m = Math.min(...s); if (m > bestMin) { bestMin = m; best = s; } }
  }
  return best;
}

export function earClip(pts) {
  const idx = pts.map((_, i) => i);
  const area = (a, b, c) => (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]);
  let total = 0; for (let i = 0; i < pts.length; i++) total += area([0, 0, 0], pts[i], pts[(i + 1) % pts.length]);
  const sign = total >= 0 ? 1 : -1;
  const tris = [];
  let guard = 0;
  while (idx.length > 3 && guard++ < 5000) {
    let clipped = false;
    for (let k = 0; k < idx.length; k++) {
      const i0 = idx[(k - 1 + idx.length) % idx.length], i1 = idx[k], i2 = idx[(k + 1) % idx.length];
      const a = pts[i0], b = pts[i1], c = pts[i2];
      if (area(a, b, c) * sign <= 1e-9) continue;
      let inside = false;
      for (const m of idx) {
        if (m === i0 || m === i1 || m === i2) continue;
        const p = pts[m];
        if (area(a, b, p) * sign >= 0 && area(b, c, p) * sign >= 0 && area(c, a, p) * sign >= 0) { inside = true; break; }
      }
      if (inside) continue;
      tris.push([i0, i1, i2]);
      idx.splice(k, 1); clipped = true; break;
    }
    if (!clipped) break;
  }
  if (idx.length === 3) tris.push([idx[0], idx[1], idx[2]]);
  // orient upward (+y): area>0 in XZ with our handedness means normal down, so swap when needed
  return tris.map(([a, b, c]) => (area(pts[a], pts[b], pts[c]) > 0 ? [a, c, b] : [a, b, c]));
}

/** Sweep kerb/footway/back strips along a corner path, sharing end rows with both roads. */
function buildCornerKit(mb, c, owner, startRows, endRows) {
  const kitA = c.A.kitRight, kitB = c.B.kitLeft;
  const P = c.path.pts, Nn = c.path.normals, m = P.length;
  const stripsIds = kitA.strips.map(() => []);
  for (let k = 0; k < m; k++) {
    const kit = blendKits(kitA, kitB, k / (m - 1));
    const p = P[k], nr = Nn[k];
    kit.strips.forEach((strip, si) => {
      let r;
      if (k === 0) r = startRows[si];
      else if (k === m - 1) r = endRows[si];
      else {
        r = [];
        for (const v of strip.verts) {
          let q;
          if (c.path.arc && c.path.arc[k]) {
            const C = c.C, rr = Math.max(0.05, c.R - v.d) / c.R;
            q = [C[0] + (p[0] - C[0]) * rr, p[1] + v.h, C[2] + (p[2] - C[2]) * rr];
          } else q = [p[0] + nr[0] * v.d, p[1] + v.h, p[2] + nr[2] * v.d];
          r.push(mb.vertex(q, [q[0], q[2]], [0, 1, variation(q[0], q[2]), 0]));
        }
      }
      stripsIds[si].push(r);
    });
  }
  kitA.strips.forEach((strip, si) => mb.gridAuto(mb.material(strip.material), stripsIds[si], owner));
  c.kitRows = stripsIds;
}

/** Collinear 2-arm node with identical sections: the two end rows become one (true vertex weld). */
function buildWeld(N, mb) {
  const [A, B] = N.arms;
  const rowA = armRow(A), rowB = armRow(B);
  const weldRow = (from, to) => { if (from.length === to.length) for (let i = 0; i < from.length; i++) mb.weld(from[i], to[i]); };
  weldRow(rowB.cw, [...rowA.cw].reverse());
  rowB.kitLeft.forEach((r, si) => weldRow(r, rowA.kitRight[si]));
  rowB.kitRight.forEach((r, si) => weldRow(r, rowA.kitLeft[si]));
  N.junction = { node: N.node, arms: N.arms, type: "weld" };
}
