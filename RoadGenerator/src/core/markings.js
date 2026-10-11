// Road markings generated as thin paint geometry (quads, +4 mm above the surface, rendered with the
// paint flag → depth-biased + procedural wear). Dimensions follow common practice:
//   EU/UK (TSRGD / Vienna convention): white lines 0.10–0.15 m, lane dashes 3 m / 9 m gap (≥60 km/h) or
//   2 m / 4 m urban; solid approach lines 20–30 m before junctions; stop line 0.3–0.5 m; zebra stripes 0.5 m.
//   US (MUTCD): yellow centre lines (double solid 0.1 m + 0.1 m gap), white 0.1–0.15 m lane lines 3 m / 9 m.
import { MeshBuilder } from "./mesh.js";
import { roadPoint, lanes, stationRange, approachSide, trafficLanes } from "./query.js";
import { earClip } from "./junction.js";
import { madd, mul, add, sub, norm, normXZ, mix, distXZ, rightXZ, clamp, lerp } from "./math.js";

const LIFT = 0.004;

export function defaultMarkingSettings() {
  return { enabled: true, standard: "eu", lineWidth: 0.12, edgeLineWidth: 0.15, dashLength: 3, dashGap: 6, solidApproach: 20, stopLine: true, stopLineWidth: 0.4, zebra: true, zebraStripe: 0.5, zebraDepth: 3.0, arrows: true, bikeSymbols: true, parkingBays: true, goreChevrons: true, wear: 0.45 };
}

class Painter {
  constructor(mb, settings) { this.mb = mb; this.S = settings; this.white = mb.material("paint"); this.yellow = mb.material("paintYellow"); }
  /** Longitudinal line following the road at lateral x from s0→s1. Dashed when `dash` is given [len, gap]. */
  line(R, x, s0, s1, w, { dash = null, yellow = false, wear = 0.2, startSolid = 0, endSolid = 0 } = {}) {
    if (s1 - s0 < 0.2) return;
    const mat = yellow ? this.yellow : this.white;
    const pieces = [];
    if (!dash) pieces.push([s0, s1]);
    else {
      const [len, gap] = dash;
      let s = s0 + startSolid;
      if (startSolid > 0) pieces.push([s0, s0 + startSolid]);
      const end = s1 - endSolid;
      // centre the dash pattern so both ends get a gap
      const period = len + gap, nP = Math.max(1, Math.floor((end - s) / period));
      let off = ((end - s) - nP * period + gap) / 2;
      s += off;
      for (let k = 0; k < nP; k++) { const a = s + k * period, b = Math.min(a + len, end); if (b > a + 0.2) pieces.push([a, b]); }
      if (endSolid > 0) pieces.push([end, s1]);
    }
    for (const [a, b] of pieces) this.strip(R, x - w / 2, x + w / 2, a, b, mat, wear);
  }
  /** Rectangle in road parameter space (s∈[a,b], x∈[x0,x1]) following the surface. */
  strip(R, x0, x1, a, b, mat, wear = 0.2) {
    const n = Math.max(1, Math.ceil((b - a) / 1.0));
    const mX = Math.max(1, Math.ceil((x1 - x0) / 1.2));
    const rows = [];
    for (let i = 0; i <= n; i++) {
      const s = lerp(a, b, i / n), row = [];
      for (let j = 0; j <= mX; j++) { const x = lerp(x0, x1, j / mX); row.push(this.mb.vertex(roadPoint(R, s, x, LIFT), [s, x], [wear, 1, 0.5, 0])); }
      rows.push(row);
    }
    this.mb.gridAuto(mat, rows, R.road.id);
  }
  /** Arbitrary polygon given in local (u along road, v lateral) coordinates anchored at (s, x). */
  polygon(R, s, x, pts2, mat, wear = 0.25, flipU = false) {
    const ids = pts2.map(([u, v]) => this.mb.vertex(roadPoint(R, s + (flipU ? -u : u), x + (flipU ? -v : v), LIFT), [s + u, x + v], [wear, 1, 0.5, 0]));
    const P = pts2.map(([u, v]) => [u, 0, v]);
    const tris = earClip(P);
    for (const t of tris) this.mb.tri(mat, ids[t[0]], ids[t[1]], ids[t[2]], R.road.id);
  }
}

// ----- shapes (local coords: u = along travel direction, v = lateral; metres) -----
function arrowStraight(len = 4) { const w = 0.6, hw = 0.15, head = len * 0.35; return [[0, -hw], [len - head, -hw], [len - head, -w / 2], [len, 0], [len - head, w / 2], [len - head, hw], [0, hw]]; }
function arrowTurn(len = 4, side = 1) { // straight shaft with a side head half-way
  const hw = 0.15, w = 0.6, head = 1.2, ty = side * 1.0;
  const shaft = [[0, -hw], [len, -hw], [len, hw], [0, hw]];
  const turn = [[len * 0.45, side * hw], [len * 0.45, ty - side * 0.1], [len * 0.45 - head * 0.5, ty - side * 0.1], [len * 0.45 + hw * 2, ty + side * 0.5], [len * 0.45 + head * 1.2, ty - side * 0.1], [len * 0.45 + hw * 2.5, ty - side * 0.1], [len * 0.45 + hw * 2.5, side * hw]];
  return { shaft, turn };
}
function bikeSymbol() { // simplified pictogram, 1.8 m long × 1.0 wide: two wheel rings + a frame polygon
  return { wheelCentres: [-0.55, 0.55], frame: [[-0.55, -0.04], [0.05, 0.45], [0.55, -0.04], [0.55, 0.04], [0.1, 0.52], [-0.55, 0.04]] };
}
function ringQuads(painter, R, s, x, cx, r, w, mat) {
  const n = 18, rows = [[], []];
  for (let i = 0; i <= n; i++) { const a = (i / n) * Math.PI * 2; rows[0].push(painter.mb.vertex(roadPoint(R, s + cx + Math.cos(a) * (r - w), x + Math.sin(a) * (r - w), LIFT), [0, 0], [0.2, 1, 0.5, 0])); rows[1].push(painter.mb.vertex(roadPoint(R, s + cx + Math.cos(a) * r, x + Math.sin(a) * r, LIFT), [0, 0], [0.2, 1, 0.5, 0])); }
  painter.mb.gridAuto(mat, rows, R.road.id);
}

/** Build all markings for a plan. Returns a MeshBuilder with paint materials. */
export function buildMarkings(plan, settings = defaultMarkingSettings()) {
  const mb = new MeshBuilder("markings");
  const S = { ...defaultMarkingSettings(), ...settings };
  const P = new Painter(mb, S);
  const us = S.standard === "us";
  for (const R of plan.roads.values()) {
    const prof = R.road.profile;
    const { s0, s1 } = stationRange(R);
    if (s1 - s0 < 1) continue;
    const NA = plan.nodes.get(R.road.a), NB = plan.nodes.get(R.road.b);
    const jA = NA && NA.kind === "junction", jB = NB && NB.kind === "junction";
    const urban = prof.edge === "curb";
    const fast = prof.speed >= 80;
    const lw = fast ? 0.15 : S.lineWidth;
    const dash = fast ? [3, 9] : [S.dashLength, S.dashGap];
    const L = lanes(R), T = trafficLanes(R);
    const sec = R.section;
    // approach zones (stop/zebra) consume the first metres after each cut
    const cwA = NA.node.crosswalks || "auto", cwB = NB.node.crosswalks || "auto";
    const zebraA = S.zebra && cwA !== "none" && (cwA === "all" || (urban && jA && NA.arms.length >= 3)), zebraB = S.zebra && cwB !== "none" && (cwB === "all" || (urban && jB && NB.arms.length >= 3));
    const stopA = S.stopLine && jA && !fast, stopB = S.stopLine && jB && !fast;
    const headA = (zebraA ? 0.8 + S.zebraDepth + 0.6 : 0) + (stopA ? S.stopLineWidth + 0.3 : 0.3);
    const headB = (zebraB ? 0.8 + S.zebraDepth + 0.6 : 0) + (stopB ? S.stopLineWidth + 0.3 : 0.3);
    const m0 = s0 + (jA ? headA : 0.3), m1 = s1 - (jB ? headB : 0.3);
    const solidA = jA ? Math.min(S.solidApproach, (m1 - m0) * 0.3) : 0, solidB = jB ? Math.min(S.solidApproach, (m1 - m0) * 0.3) : 0;
    const wear = S.wear;
    if (m1 - m0 > 2) {
      // --- centre line / median edges ---
      const twoWay = prof.lanesF > 0 && prof.lanesB > 0;
      if (twoWay && sec.halfMedian < 0.3) {
        if (us) { P.line(R, -0.1, m0, m1, 0.1, { yellow: true, wear }); P.line(R, 0.1, m0, m1, 0.1, { yellow: true, wear }); }
        else P.line(R, 0, m0, m1, lw, { dash: prof.speed <= 50 ? [2, 4] : dash, startSolid: solidA, endSolid: solidB, wear });
      } else if (sec.halfMedian >= 0.3) {
        // hatched / painted median or edge lines along a raised median
        const hm = sec.halfMedian;
        P.line(R, hm - lw / 2 - 0.1, m0, m1, lw, { yellow: us, wear: 0.1 });
        P.line(R, -hm + lw / 2 + 0.1, m0, m1, lw, { yellow: us, wear: 0.1 });
        if (prof.medianType === "paint") { // chevron hatch between the two lines
          const step = 3; for (let s = m0 + 1; s < m1 - 2; s += step) P.polygon(R, s, 0, [[0, -hm + 0.3], [1.5, -hm + 0.3], [1.9, hm - 0.3], [0.4, hm - 0.3]], us ? P.yellow : P.white, 0.1);
        }
      }
      // --- lane lines between same-direction lanes ---
      for (const side of [1, -1]) {
        const ls = T.filter((l) => l.side === side).sort((a, b) => Math.abs(a.xc) - Math.abs(b.xc));
        for (let i = 0; i + 1 < ls.length; i++) P.line(R, side > 0 ? ls[i].x1 : ls[i].x0, m0, m1, lw, { dash, startSolid: solidA, endSolid: solidB, wear });
        // outer edge of the traffic lanes: bike lane line, parking line, shoulder edge line or kerb edge line
        if (!ls.length) continue;
        const outer = ls[ls.length - 1];
        const xEdge = side > 0 ? outer.x1 : outer.x0;
        const beyond = L.filter((l) => l.side === side && l.kind !== "lane" && Math.abs(l.xc) > Math.abs(outer.xc)).sort((a, b) => Math.abs(a.xc) - Math.abs(b.xc));
        const next = beyond[0];
        if (next && next.kind === "bike") {
          P.line(R, xEdge, m0, m1, 0.15, { dash: urban ? [1, 1] : null, wear: 0.15 });
          if (S.bikeSymbols) { const sym = bikeSymbol(); for (let s = m0 + 6; s < m1 - 4; s += 40) { const flip = side < 0; for (const cx of sym.wheelCentres) ringQuads(P, R, s, next.xc, flip ? -cx : cx, 0.32, 0.08, P.white); P.polygon(R, s, next.xc, sym.frame, P.white, 0.15, flip); } }
          const after = beyond[1];
          if (after && after.kind === "parking") P.line(R, side > 0 ? next.x1 : next.x0, m0, m1, 0.1, { wear: 0.1 });
        } else if (next && next.kind === "parking") {
          P.line(R, xEdge, m0, m1, 0.1, { wear: 0.15 });
        } else if (next && next.kind === "shoulder") {
          P.line(R, xEdge, m0, m1, fast ? 0.2 : S.edgeLineWidth, { wear: 0.1 });
        } else if (!urban) {
          P.line(R, xEdge, m0, m1, S.edgeLineWidth, { wear: 0.1 });
        }
        // parking bay ticks
        const park = L.find((l) => l.side === side && l.kind === "parking");
        if (park && S.parkingBays) {
          const bay = 6.0; const n = Math.floor((m1 - m0 - 2) / bay);
          const xIn = side > 0 ? park.x0 : park.x1, xOut = side > 0 ? park.x1 : park.x0;
          for (let k = 0; k <= n; k++) { const s = m0 + 1 + k * bay; P.strip(R, Math.min(xIn, xOut) + 0.05, Math.max(xIn, xOut) - 0.05, s - 0.05, s + 0.05, P.white, 0.1); }
        }
      }
    }
    // --- junction approaches: zebra, stop line, arrows ---
    for (const end of [0, 1]) {
      const node = end === 0 ? NA : NB, isJ = end === 0 ? jA : jB;
      if (!isJ) continue;
      const nodeId = end === 0 ? R.road.a : R.road.b;
      const cut = end === 0 ? s0 : s1, dir = end === 0 ? 1 : -1; // dir: +1 = stations increase away from the node
      const appSide = approachSide(R, nodeId);
      const appLanes = T.filter((l) => l.side === appSide).sort((a, b) => Math.abs(a.xc) - Math.abs(b.xc));
      const zebra = end === 0 ? zebraA : zebraB, stop = end === 0 ? stopA : stopB;
      let s = cut + dir * 0.8;
      const xMin = -sec.widthL + 0.05, xMax = sec.widthR - 0.05;
      if (zebra) {
        // stripes parallel to the road direction, 0.5 m wide with 0.5 m gaps, centred on the carriageway
        const w = S.zebraStripe, span = xMax - xMin, n = Math.floor((span + w) / (2 * w));
        const start = xMin + (span - (n * 2 * w - w)) / 2;
        for (let k = 0; k < n; k++) { const x0 = start + k * 2 * w; P.strip(R, x0, x0 + w, Math.min(s, s + dir * S.zebraDepth), Math.max(s, s + dir * S.zebraDepth), P.white, 0.3); }
        s += dir * (S.zebraDepth + 0.6);
      }
      if (stop && appLanes.length) {
        const x0 = Math.min(...appLanes.map((l) => l.x0)), x1 = Math.max(...appLanes.map((l) => l.x1));
        P.strip(R, x0, x1, Math.min(s, s + dir * S.stopLineWidth), Math.max(s, s + dir * S.stopLineWidth), P.white, 0.35);
        s += dir * S.stopLineWidth;
      }
      // give-way markings at T-junction minor arm (no stop line needed when it's the through road)
      if (S.arrows && appLanes.length && !fast && (m1 - m0) > 25) {
        const len = prof.speed >= 60 ? 5 : 3.5;
        const sArrow = s + dir * 4;
        const n = appLanes.length;
        appLanes.forEach((l, i) => {
          // outer lane turns outward (right for right-hand traffic), inner lane turns inward (left), middle straight
          const isOuter = i === n - 1, isInner = i === 0;
          const turnRight = isOuter && node.arms.length >= 3, turnLeft = isInner && n > 1 && node.arms.length >= 3;
          const flip = dir > 0; // travel direction points toward the node
          // local u points in travel direction (toward node): travel dir = -dir in station space
          const base = [sArrow + dir * len, l.xc];
          const tr = (pts) => pts.map(([u, v]) => [flip ? -u : u, flip ? -v : v]); // travel direction is -s when flip
          P.polygon(R, base[0], base[1], tr(arrowStraight(len)), P.white, 0.3);
          if (turnRight || turnLeft) { const t = arrowTurn(len, turnRight ? 1 : -1); P.polygon(R, base[0], base[1], tr(t.turn), P.white, 0.3); }
        });
      }
    }
  }
  // --- gore chevrons at acute merges/diverges (ramps / fast roads only) ---
  if (S.goreChevrons) for (const J of plan.junctions || []) {
    for (const c of J.corners || []) {
      if (c.kind !== "fillet" || c.theta > 0.62 || !c.path) continue;
      const fast = (arm) => arm.R.road.profile.speed >= 70 || arm.R.road.profile.id === "ramp" || arm.R.road.profile.oneWay;
      if (!(fast(c.A) || fast(c.B))) continue;
      const Q = c.path.pts, Nn = c.path.normals, m = Q.length, K = (m - 1) >> 1;
      const lineW = 0.3, inset = 0.35;
      const vtx = (q) => mb.vertex([q[0], q[1] + LIFT, q[2]], [0, 0], [0.1, 1, 0.5, 0]);
      // continuous edge line hugging the nose on both sides
      const rows = [];
      for (let k = 0; k < m; k++) { const inw = mul(Nn[k], -1); rows.push([vtx(madd(Q[k], inw, inset)), vtx(madd(Q[k], inw, inset + lineW))]); }
      mb.gridAuto(P.white, rows, J.node.id);
      // painted gore: a hatched triangle on the carriageway in front of the physical nose (never on the island)
      // base = the two edge lines just past the fillet arc, apex = where the two edges would meet towards the node
      let j = 1; while (j < K - 1 && distXZ(Q[K - j], Q[K + j]) < 3.0) j++; // where the island is ~3 m wide the paint stops
      const TA = madd(Q[K - j], mul(Nn[K - j], -1), inset + lineW), TB = madd(Q[K + j], mul(Nn[K + j], -1), inset + lineW);
      // the through-lane edge continues straight back from the nose; the ramp-side edge tapers in to meet it at the
      // apex (a 1:8…1:15 taper), so the hatched triangle lies on the carriageway between the two departing lanes
      // direction of an arm's carriageway pointing into the junction (towards the node)
      const intoNode = (arm) => { const R = arm.R, F = R.frameAt(arm.isStart ? R.cutA : R.length - R.cutB); const d = normXZ(F.dxz); return arm.isStart ? mul(d, -1) : d; };
      const base = distXZ(TA, TB);
      if (base < lineW * 2) continue;
      const widthOf = (arm) => arm.R.section.widthL + arm.R.section.widthR;
      const straightB = widthOf(c.B) > widthOf(c.A) + 0.01; // the wider carriageway is the through road
      const T0 = straightB ? TB : TA, d0 = intoNode(straightB ? c.B : c.A);
      if (!isFinite(d0[0])) continue;
      const g = clamp(base * 10, 12, 36);
      const apex = madd(T0, d0, g); apex[1] = T0[1];
      // boundary lines base-corner -> apex
      for (const T of [TA, TB]) {
        const dir = normXZ(sub(apex, T)), side = rightXZ(dir), flip = T === TA ? -1 : 1;
        mb.gridAuto(P.white, [[vtx(T), vtx(madd(T, side, flip * lineW))], [vtx(apex), vtx(madd(apex, side, flip * lineW))]], J.node.id);
      }
      // hatch bars every 2.4 m, spanning the triangle width at that station, stopping 1.5 m short of the apex
      for (let t = 1.2; t < g - 1.5; t += 2.4) {
        const f0 = t / g, f1 = Math.min((t + 0.3) / g, 1);
        const a0 = mix(TA, apex, f0), b0 = mix(TB, apex, f0), a1 = mix(TA, apex, f1), b1 = mix(TB, apex, f1);
        if (distXZ(a0, b0) < lineW * 2.5) break;
        mb.gridAuto(P.white, [[vtx(a0), vtx(b0)], [vtx(a1), vtx(b1)]], J.node.id);
      }
    }
  }
  mb.compact();
  return mb;
}
