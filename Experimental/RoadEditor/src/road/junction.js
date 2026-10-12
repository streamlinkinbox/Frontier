// Junction solver. For every node it determines where each incident road ends (setback) and
// the boundary of the junction surface: curb returns are true circular fillets tangent to both
// curb lines; the sidewalk outer line is a concentric arc, so corner strips are clean quads.
// Roundabouts / cul-de-sacs use fillet → circle → fillet boundaries.
import { getProfile, layout, RANK } from './profiles.js';
import { TAU, wrapAngle, lineIntersect } from './math.js';

const ARC_STEP = Math.PI / 20;

function wrapPi(a) { while (a > Math.PI) a -= TAU; while (a < -Math.PI) a += TAU; return a; }

export function autoRadius(ends) {
  const rank = Math.max(...ends.map(e => e.rank));
  if (rank <= 0) return 2.5;
  if (rank >= 8) return 12;
  if (rank >= 6) return 9;
  if (rank >= 4) return 7;
  return 6;
}

function endInfo(net, edge, nodeId) {
  const c = net.centerline(edge), lay = layout(getProfile(edge));
  const atA = edge.a === nodeId;
  const f = c.frameAt(atA ? 0 : c.length);
  const dx = atA ? f.tx : -f.tx, dz = atA ? f.tz : -f.tz;
  return {
    edge, id: edge.id, atA, c, lay,
    dx, dz, rx: -dz, rz: dx, ang: Math.atan2(dz, dx),
    wl: atA ? -lay.curbL : lay.curbR,      // distance to curb on left-of-d
    wr: atA ? lay.curbR : -lay.curbL,      // distance to curb on right-of-d
    sl: atA ? lay.sideL : lay.sideR,       // raised zone width on left-of-d
    sr: atA ? lay.sideR : lay.sideL,
    curbLeft: atA ? lay.hasCurbL : lay.hasCurbR,
    curbRight: atA ? lay.hasCurbR : lay.hasCurbL,
    rank: RANK[edge.profile] ?? 3,
    setback: 0,
    len: c.length,
  };
}

// Position at distance t along d from node, lateral offset w along right-of-d.
const P = (N, e, t, w) => [N.x + e.dx * t + e.rx * w, N.z + e.dz * t + e.rz * w];

function arcPts(cx, cz, r, a0, sweep) {
  const n = Math.max(2, Math.ceil(Math.abs(sweep) / ARC_STEP));
  const out = [];
  for (let k = 0; k <= n; k++) { const a = a0 + sweep * k / n; out.push([cx + Math.cos(a) * r, cz + Math.sin(a) * r]); }
  return out;
}

export function computeJunction(net, nodeId) {
  const N = net.nodes.get(nodeId);
  const edges = net.nodeEdges(nodeId);
  const J = { id: nodeId, node: N, y: N.y, ends: [], gaps: [], kind: 'none', polygon: null, hole: null };
  if (!edges.length) return J;
  const ends = edges.map(e => endInfo(net, e, nodeId)).sort((a, b) => a.ang - b.ang);
  J.ends = ends;
  const map = new Map(ends.map(e => [e.id + (e.atA ? ':a' : ':b'), e]));
  J.endFor = (edgeId, atA) => map.get(edgeId + (atA ? ':a' : ':b'));

  if (net.isContinuation(nodeId)) { J.kind = 'continuation'; return J; }

  if (ends.length === 1) {
    const e = ends[0];
    const style = N.end || (e.lay.hasCurbL || e.lay.hasCurbR ? 'flat' : 'flush');
    if (style !== 'culdesac') { J.kind = style; return J; }
    J.kind = 'culdesac';
    const Rc = N.rbRadius || Math.max(e.wl, e.wr) + 6.5;
    circular(J, N, ends, Rc, 0);
    return J;
  }
  if (N.kind === 'roundabout' && ends.length >= 3) {
    J.kind = 'roundabout';
    const wmax = Math.max(...ends.map(e => Math.max(e.wl, e.wr)));
    const Rc = N.rbRadius || Math.max(20, wmax + 9);
    circular(J, N, ends, Rc, Math.max(Rc - 9, 5));
    return J;
  }
  J.kind = 'junction';
  const R = N.radius || autoRadius(ends);
  J.radius = R;
  const n = ends.length;
  const gaps = [];
  for (let i = 0; i < n; i++) {
    const A = ends[i], B = ends[(i + 1) % n];
    let phi = wrapAngle(B.ang - A.ang);
    if (n === 1) phi = TAU;
    const swA = A.sr, swB = B.sl;
    const hasCurb = A.curbRight || B.curbLeft;
    if (phi < Math.PI * 0.94) {
      const Reff = Math.max(R, (hasCurb ? Math.max(swA, swB) : 0) + 0.6);
      const sol = lineIntersect(N.x + A.rx * (A.wr + Reff), N.z + A.rz * (A.wr + Reff), A.dx, A.dz,
        N.x - B.rx * (B.wl + Reff), N.z - B.rz * (B.wl + Reff), B.dx, B.dz);
      if (sol) {
        let [tA, sB] = sol;
        tA = Math.max(tA, 0.5); sB = Math.max(sB, 0.5);
        const O = P(N, A, tA, A.wr + Reff);
        const TA = P(N, A, tA, A.wr), TB = P(N, B, sB, -B.wl);
        const a0 = Math.atan2(TA[1] - O[1], TA[0] - O[0]);
        const a1 = Math.atan2(TB[1] - O[1], TB[0] - O[0]);
        gaps.push({ type: 'arc', A, B, O, R: Reff, tA, sB, a0, sweep: wrapPi(a1 - a0), swA, swB, hasCurb });
        A.setback = Math.max(A.setback, tA); B.setback = Math.max(B.setback, sB);
        continue;
      }
    }
    gaps.push({ type: 'straight', A, B, swA, swB, hasCurb, phi });
  }
  // Straight / reflex gaps between near-opposite roads: keep a small transition so the two
  // end cross-sections never overlap.
  for (const g of gaps) if (g.type === 'straight') {
    g.A.setback = Math.max(g.A.setback, 1.0); g.B.setback = Math.max(g.B.setback, 1.0);
  }
  for (const e of ends) e.setback = Math.min(e.setback, Math.max(0, e.len * 0.48));
  J.gaps = gaps.map(g => buildGap(N, g));
  J.polygon = J.gaps.flatMap(g => g.curb);
  return J;
}

// Edge-frame-accurate y for a point at distance t along an end.
function yAt(N, e, t) {
  const s = e.atA ? t : e.len - t;
  return e.c.frameAt(Math.max(0, Math.min(e.len, s))).y;
}

function buildGap(N, g) {
  const { A, B } = g;
  const curb = [], outer = [];
  const yA = yAt(N, A, A.setback), yB = yAt(N, B, B.setback);
  const push = (pc, po, y) => { curb.push([pc[0], y, pc[1]]); outer.push([po[0], y, po[1]]); };
  // A right side from its end inward
  push(P(N, A, A.setback, A.wr), P(N, A, A.setback, A.wr + g.swA), yA);
  if (g.type === 'arc') {
    if (A.setback - g.tA > 0.02) push(P(N, A, g.tA, A.wr), P(N, A, g.tA, A.wr + g.swA), yAt(N, A, g.tA));
    const pts = arcPts(g.O[0], g.O[1], g.R, g.a0, g.sweep);
    const n = pts.length - 1;
    for (let k = 1; k < n; k++) {
      const t = k / n, sw = g.swA + (g.swB - g.swA) * t;
      const a = g.a0 + g.sweep * t, r = Math.max(0.05, g.R - sw);
      push(pts[k], [g.O[0] + Math.cos(a) * r, g.O[1] + Math.sin(a) * r], N.y);
    }
    if (B.setback - g.sB > 0.02) push(P(N, B, g.sB, -B.wl), P(N, B, g.sB, -B.wl - g.swB), yAt(N, B, g.sB));
  }
  push(P(N, B, B.setback, -B.wl), P(N, B, B.setback, -B.wl - g.swB), yB);
  return { curb, outer, swA: g.swA, swB: g.swB, hasCurb: g.hasCurb, type: g.type, A: A.id, B: B.id };
}

// Roundabout / cul-de-sac: roads join an outer circle of radius Rc through fillets.
function circular(J, N, ends, Rc, Ri) {
  const n = ends.length;
  const rfBase = 7;
  const tan = (e, w, rf) => {
    const t = Math.sqrt(Math.max(0, (Rc + rf) ** 2 - (w + rf) ** 2));
    return t;
  };
  for (const e of ends) {
    const rfR = Math.max(rfBase, e.sr + 1), rfL = Math.max(rfBase, e.sl + 1);
    e._rfR = rfR; e._rfL = rfL;
    e._tR = tan(e, e.wr, rfR); e._tL = tan(e, e.wl, rfL);
    e.setback = Math.max(e._tR, e._tL) + 0.5;
  }
  const gaps = [];
  for (let i = 0; i < n; i++) {
    const A = ends[i], B = ends[(i + 1) % n];
    const swA = A.sr, swB = B.sl, hasCurb = A.curbRight || B.curbLeft;
    const curb = [], outer = [];
    const push = (pc, po, y) => { curb.push([pc[0], y, pc[1]]); outer.push([po[0], y, po[1]]); };
    const yA = yAt(N, A, A.setback), yB = yAt(N, B, B.setback);
    push(P(N, A, A.setback, A.wr), P(N, A, A.setback, A.wr + swA), yA);
    // fillet A (right side of A)
    const CA = P(N, A, A._tR, A.wr + A._rfR);
    const TA = P(N, A, A._tR, A.wr);
    const dA = Math.hypot(CA[0] - N.x, CA[1] - N.z);
    const QA = [N.x + (CA[0] - N.x) / dA * Rc, N.z + (CA[1] - N.z) / dA * Rc];
    const CB = P(N, B, B._tL, -B.wl - B._rfL);
    const TB = P(N, B, B._tL, -B.wl);
    const dB = Math.hypot(CB[0] - N.x, CB[1] - N.z);
    const QB = [N.x + (CB[0] - N.x) / dB * Rc, N.z + (CB[1] - N.z) / dB * Rc];
    const fillet = (C, rf, from, to, sw0, sw1) => {
      const a0 = Math.atan2(from[1] - C[1], from[0] - C[0]), a1 = Math.atan2(to[1] - C[1], to[0] - C[0]);
      const sweep = wrapPi(a1 - a0), m = Math.max(2, Math.ceil(Math.abs(sweep) / ARC_STEP));
      for (let k = 0; k <= m; k++) {
        const t = k / m, a = a0 + sweep * t, sw = sw0 + (sw1 - sw0) * t;
        push([C[0] + Math.cos(a) * rf, C[1] + Math.sin(a) * rf], [C[0] + Math.cos(a) * Math.max(0.05, rf - sw), C[1] + Math.sin(a) * Math.max(0.05, rf - sw)], N.y);
      }
    };
    fillet(CA, A._rfR, TA, QA, swA, swA);
    let aQA = Math.atan2(QA[1] - N.z, QA[0] - N.x), aQB = Math.atan2(QB[1] - N.z, QB[0] - N.x);
    let sweep = wrapAngle(aQB - aQA);
    if (n === 1 && sweep < 0.01) sweep = TAU;
    if (sweep > TAU - 0.15 && n > 1) sweep = 0; // fillets overlap: skip circle arc
    if (sweep > 0) {
      const m = Math.max(2, Math.ceil(sweep / (ARC_STEP * 0.7)));
      for (let k = 1; k < m; k++) {
        const t = k / m, a = aQA + sweep * t, sw = swA + (swB - swA) * t;
        push([N.x + Math.cos(a) * Rc, N.z + Math.sin(a) * Rc], [N.x + Math.cos(a) * (Rc + sw), N.z + Math.sin(a) * (Rc + sw)], N.y);
      }
    }
    fillet(CB, B._rfL, QB, TB, swB, swB);
    push(P(N, B, B.setback, -B.wl), P(N, B, B.setback, -B.wl - swB), yB);
    gaps.push({ curb, outer, swA, swB, hasCurb, type: 'circle', A: A.id, B: B.id });
  }
  J.gaps = gaps;
  J.polygon = gaps.flatMap(g => g.curb);
  J.Rc = Rc; J.Ri = Ri;
  if (Ri > 0) {
    const hole = [];
    const m = Math.max(24, Math.ceil(TAU * Ri / 2.5));
    for (let k = 0; k < m; k++) { const a = -k / m * TAU; hole.push([N.x + Math.cos(a) * Ri, N.y, N.z + Math.sin(a) * Ri]); }
    J.hole = hole;
  }
}
