// Procedural mine builder.
//
// Produces ONE continuous, closed, all-quad surface containing the cave
// (walls + roof), the paved road and the grooved cart tracks, for the whole
// spline network. Junction hubs are stitched to the tunnel rings with
// matching vertex counts, so there are no T-junctions, no n-gons, no seams.
//
// Junction topology (per hub with K arms):
//   * Every corner between two neighbouring arms is a curved wall strip
//     (2k x wallSegs quads). The wall edge-loops of arm i turn the corner and
//     continue into arm i+1  ->  "L" flow instead of a "| + _" T-junction.
//   * The floor and the roof are each filled with K quad sub-patches that meet
//     in ONE pole of valence K (a "Y" for a 3-way, a "+" for a 4-way) whose
//     spokes leave the pole at equal angles and bend smoothly into the arms,
//     instead of a "V + |" layout with a flattened 180 degree corner.
import * as THREE from 'three';
import { EdgeSpline } from './network.js';
import { buildProfile, MAT } from './profile.js';
import { fbm3 } from './noise.js';

const UP = new THREE.Vector3(0, 1, 0);
const _v = new THREE.Vector3();

const smoothstep = (e0, e1, x) => {
  const t = THREE.MathUtils.clamp((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
};

const COLORS = {
  [MAT.ROCK]: [0.105, 0.075, 0.052],
  [MAT.ROAD]: [0.16, 0.15, 0.135],
  [MAT.GROOVE]: [0.025, 0.022, 0.02],
  [MAT.RAIL]: [0.42, 0.40, 0.38],
  [MAT.CURB]: [0.24, 0.215, 0.18],
  [MAT.PLATE]: [0.10, 0.095, 0.09],
};
const METAL = { [MAT.ROCK]: 0, [MAT.ROAD]: 0, [MAT.GROOVE]: 0, [MAT.RAIL]: 1, [MAT.CURB]: 0, [MAT.PLATE]: 0.75 };

class MeshData {
  constructor() {
    this.pos = []; this.col = []; this.metal = []; this.rock = []; this.uv = []; this.surf = [];
    this.quads = []; // flat, 4 per quad
    this.patchInfo = []; // {name, start, count}
  }
  add(p, mat, rock, u, v, colOverride) {
    const i = this.pos.length / 3;
    this.pos.push(p.x, p.y, p.z);
    const c = colOverride || COLORS[mat];
    this.col.push(c[0], c[1], c[2]);
    this.metal.push(colOverride && colOverride[3] !== undefined ? colOverride[3] : METAL[mat]);
    this.rock.push(rock);
    this.uv.push(u, v);
    this.surf.push(mat);
    return i;
  }
  P(i, t = new THREE.Vector3()) { return t.set(this.pos[i * 3], this.pos[i * 3 + 1], this.pos[i * 3 + 2]); }
  set(i, p) { this.pos[i * 3] = p.x; this.pos[i * 3 + 1] = p.y; this.pos[i * 3 + 2] = p.z; }
  // add a grid of quads G[s][t] and orient them so the normal points "inward"
  gridPatch(name, G, inwardFn) {
    const start = this.quads.length / 4;
    const S = G.length - 1, T = G[0].length - 1;
    const local = [];
    for (let s = 0; s < S; s++) for (let t = 0; t < T; t++) local.push([G[s][t], G[s + 1][t], G[s + 1][t + 1], G[s][t + 1]]);
    // decide orientation from the whole patch
    let score = 0;
    const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), d = new THREE.Vector3();
    for (const q of local) {
      this.P(q[0], a); this.P(q[1], b); this.P(q[2], c); this.P(q[3], d);
      const n = new THREE.Vector3().subVectors(c, a).cross(new THREE.Vector3().subVectors(d, b));
      const ctr = a.clone().add(b).add(c).add(d).multiplyScalar(0.25);
      score += n.dot(inwardFn(ctr));
    }
    const flip = score < 0;
    for (const q of local) {
      if (flip) this.quads.push(q[0], q[3], q[2], q[1]);
      else this.quads.push(q[0], q[1], q[2], q[3]);
    }
    this.patchInfo.push({ name, start, count: local.length });
  }
}

function bezier(p0, p1, p2, p3, t, out = new THREE.Vector3()) {
  const it = 1 - t;
  return out.set(0, 0, 0)
    .addScaledVector(p0, it * it * it)
    .addScaledVector(p1, 3 * it * it * t)
    .addScaledVector(p2, 3 * it * t * t)
    .addScaledVector(p3, t * t * t);
}

// Coons patch on an (S+1)x(T+1) grid given 4 boundary index arrays.
function coonsFill(md, bottom, top, left, right, mat, rockFn, uvFn, colFn, posFn = null) {
  // bottom: G[s][0], top: G[s][T], left: G[0][t], right: G[S][t]
  const S = bottom.length - 1, T = left.length - 1;
  const G = [];
  const Pb = bottom.map((i) => md.P(i)), Pt = top.map((i) => md.P(i));
  const Pl = left.map((i) => md.P(i)), Pr = right.map((i) => md.P(i));
  const c00 = Pb[0], c10 = Pb[S], c01 = Pt[0], c11 = Pt[S];
  for (let s = 0; s <= S; s++) {
    G.push([]);
    for (let t = 0; t <= T; t++) {
      if (t === 0) { G[s].push(bottom[s]); continue; }
      if (t === T) { G[s].push(top[s]); continue; }
      if (s === 0) { G[s].push(left[t]); continue; }
      if (s === S) { G[s].push(right[t]); continue; }
      const u = s / S, v = t / T;
      const p = new THREE.Vector3()
        .addScaledVector(Pl[t], 1 - u).addScaledVector(Pr[t], u)
        .addScaledVector(Pb[s], 1 - v).addScaledVector(Pt[s], v)
        .addScaledVector(c00, -(1 - u) * (1 - v)).addScaledVector(c10, -u * (1 - v))
        .addScaledVector(c01, -(1 - u) * v).addScaledVector(c11, -u * v);
      if (posFn) posFn(p);
      const uv = uvFn(p, u, v);
      G[s].push(md.add(p, mat, rockFn(u, v), uv[0], uv[1], colFn ? colFn(p, u, v) : null));
    }
  }
  return G;
}

export function buildMine(net, P, opts = {}) {
  const t0 = performance.now();
  // preview = fast rebuild while dragging in the editor (coarser loops, no rock noise)
  const rockForLayout = P.rockNoise; // junction sizes must not change between preview and full builds
  if (opts.preview) P = { ...P, ringSpacing: P.ringSpacing * 2.2, rockNoise: 0 };
  const prof = buildProfile(P);
  const { a, w, r, N } = prof;
  const md = new MeshData();
  const warnings = [];

  const vOpts = { minCrestRadius: P.minCrestRadius, maxGrade: P.maxGrade };
  // pass 1 only needs the horizontal layout (junction angles) -> skip vertical relaxation
  let splines = net.edges.map((e) => new EdgeSpline(net, e, { ...vOpts, skipVertical: true }));

  // ---------- 1. junction arms & cut distances ----------
  const hubs = net.nodes.map((n) => ({ node: n, arms: [] }));
  net.edges.forEach((e, ei) => {
    if (e.a === e.b) { warnings.push(`edge ${e.id} is a self loop`); return; }
    hubs[e.a].arms.push({ edge: ei, end: 'a' });
    hubs[e.b].arms.push({ edge: ei, end: 'b' });
  });
  const halfW = P.roadHalfWidth + P.wallBulge + rockForLayout * 0.6;
  const cut = net.edges.map(() => ({ a: 10, b: 10 }));
  for (const hub of hubs) {
    const c = new THREE.Vector3(...hub.node.p);
    for (const arm of hub.arms) {
      const sp = splines[arm.edge];
      const s = arm.end === 'a' ? Math.min(9, sp.length * 0.3) : Math.max(sp.length - 9, sp.length * 0.7);
      const p = sp.pointAt(s);
      arm.dir0 = new THREE.Vector3(p.x - c.x, 0, p.z - c.z).normalize();
      arm.angle = Math.atan2(-arm.dir0.z, arm.dir0.x);
    }
    hub.arms.sort((x, y) => x.angle - y.angle);
    const K = hub.arms.length;
    if (K < 3) warnings.push(`node ${hub.node.id} has only ${K} tunnels (needs >= 3)`);
    for (let i = 0; i < K; i++) {
      const prev = hub.arms[(i + K - 1) % K], next = hub.arms[(i + 1) % K], cur = hub.arms[i];
      const ang = (x, y) => { let d = y.angle - x.angle; while (d <= 0) d += Math.PI * 2; return d; };
      const th = Math.min(ang(prev, cur), ang(cur, next));
      let R = Math.max(halfW / Math.tan(Math.min(th, Math.PI * 0.95) / 2) + 3.5, halfW + 4.5);
      cur.R = R;
    }
  }
  // second pass: rebuild splines so each tunnel stays level all the way to its
  // junction mouth (+4 m) -> hub floors meet the tunnels without a slope kink
  net.edges.forEach((e, ei) => {
    const ha = hubs[e.a].arms.find((x) => x.edge === ei && x.end === 'a');
    const hb = hubs[e.b].arms.find((x) => x.edge === ei && x.end === 'b');
    if (!ha || !hb) return;
    splines[ei] = new EdgeSpline(net, e, { ...vOpts, levelA: ha.R + 4, levelB: hb.R + 4 });
  });
  // clamp to edge length
  net.edges.forEach((e, ei) => {
    const L = splines[ei].length;
    const ha = hubs[e.a].arms.find((x) => x.edge === ei && x.end === 'a');
    const hb = hubs[e.b].arms.find((x) => x.edge === ei && x.end === 'b');
    if (!ha || !hb) return;
    let Ra = ha.R, Rb = hb.R;
    const maxTot = L - 6;
    if (Ra + Rb > maxTot) {
      warnings.push(`edge ${e.id} is too short for its junctions (${L.toFixed(1)} m)`);
      const sc = Math.max(0.2, maxTot / (Ra + Rb));
      Ra *= sc; Rb *= sc;
    }
    cut[ei] = { a: Ra, b: L - Rb };
    ha.R = Ra; hb.R = Rb;
  });

  // ---------- 2. tunnels (rings extruded along the splines) ----------
  const frameAt = (sp, s, s0, s1) => {
    const p = sp.pointAt(s);
    const T = sp.tangentAt(s);
    let Nn = new THREE.Vector3().crossVectors(UP, T);
    if (Nn.lengthSq() < 1e-6) Nn.set(1, 0, 0);
    Nn.normalize();
    let B = new THREE.Vector3().crossVectors(T, Nn).normalize();
    const edgeTaper = smoothstep(0, 10, Math.min(s - s0, s1 - s));
    const bank = THREE.MathUtils.clamp(-sp.curvatureAt(s) * 3.2, -P.bankMax, P.bankMax) * edgeTaper;
    if (Math.abs(bank) > 1e-5) {
      const cb = Math.cos(bank), sb = Math.sin(bank);
      const N2 = Nn.clone().multiplyScalar(cb).addScaledVector(B, -sb);
      const B2 = B.clone().multiplyScalar(cb).addScaledVector(Nn, sb);
      Nn = N2; B = B2;
    }
    return { p, T, N: Nn, B };
  };

  const tunnels = [];
  const lanes = []; // {edge, dir, pts:[Vector3], from, to}
  const supports = []; // {p,T,N,B}
  const lamps = [];
  const centerline = [];
  net.edges.forEach((e, ei) => {
    const sp = splines[ei];
    const s0 = cut[ei].a, s1 = cut[ei].b;
    const len = s1 - s0;
    const count = Math.max(2, Math.ceil(len / P.ringSpacing));
    const rings = [];
    const fwd = [], bwd = [];
    const cl = [];
    for (let i = 0; i <= count; i++) {
      const s = s0 + (len * i) / count;
      const f = frameAt(sp, s, s0, s1);
      const gw = smoothstep(1.0, 6.0, Math.min(s - s0, s1 - s)); // groove weight
      const ring = [];
      for (let j = 0; j < N; j++) {
        const q = prof.ring[j];
        let n = q.n, b = q.b, m = q.m;
        let col = null;
        if (j <= a) {
          const u = prof.uniform[j];
          n = u.n + (q.n - u.n) * gw;
          b = u.b + (q.b - u.b) * gw;
          const c0 = COLORS[MAT.PLATE], c1 = COLORS[q.m];
          const ww = smoothstep(0.0, 1.0, gw);
          col = [c0[0] + (c1[0] - c0[0]) * ww, c0[1] + (c1[1] - c0[1]) * ww, c0[2] + (c1[2] - c0[2]) * ww,
            METAL[MAT.PLATE] + (METAL[q.m] - METAL[MAT.PLATE]) * ww];
        }
        _v.copy(f.p).addScaledVector(f.N, n).addScaledVector(f.B, b);
        ring.push(md.add(_v, m, q.rock, prof.arc[j] * 0.5, s * 0.5, col));
      }
      rings.push(ring);
      fwd.push(f.p.clone().addScaledVector(f.N, -P.laneOffset));
      bwd.push(f.p.clone().addScaledVector(f.N, P.laneOffset));
      cl.push({ p: f.p.clone(), T: f.T.clone(), N: f.N.clone(), B: f.B.clone(), s, edge: ei });
    }
    // quads between consecutive rings
    const start = md.quads.length / 4;
    const quadsLocal = [];
    for (let i = 0; i < count; i++) {
      const A = rings[i], B = rings[i + 1];
      for (let j = 0; j < N; j++) {
        const j1 = (j + 1) % N;
        quadsLocal.push([A[j], A[j1], B[j1], B[j]]);
      }
    }
    // orientation: normal must point to the tunnel axis
    const mid = cl[Math.floor(cl.length / 2)];
    const axis = mid.p.clone().addScaledVector(mid.B, P.springHeight * 0.6);
    let score = 0;
    const qa = new THREE.Vector3(), qb = new THREE.Vector3(), qc = new THREE.Vector3(), qd = new THREE.Vector3();
    const iMid = Math.floor(count / 2) * N;
    for (let q = iMid; q < iMid + N; q++) {
      const Q = quadsLocal[q];
      md.P(Q[0], qa); md.P(Q[1], qb); md.P(Q[2], qc); md.P(Q[3], qd);
      const nn = new THREE.Vector3().subVectors(qc, qa).cross(new THREE.Vector3().subVectors(qd, qb));
      const ctr = qa.clone().add(qb).add(qc).add(qd).multiplyScalar(0.25);
      score += nn.dot(axis.clone().sub(ctr));
    }
    for (const Q of quadsLocal) {
      if (score < 0) md.quads.push(Q[0], Q[3], Q[2], Q[1]); else md.quads.push(...Q);
    }
    md.patchInfo.push({ name: `tunnel${ei}`, start, count: quadsLocal.length });
    tunnels.push({ edge: ei, rings, s0, s1 });
    lanes.push({ edge: ei, dir: 1, from: e.a, to: e.b, pts: fwd });
    lanes.push({ edge: ei, dir: -1, from: e.b, to: e.a, pts: bwd.slice().reverse() });
    centerline.push(...cl);

    // supports + lamps
    const sc = Math.floor((len - 6) / P.supportSpacing);
    for (let i = 0; i <= sc; i++) {
      const s = s0 + 3 + i * P.supportSpacing + ((len - 6) - sc * P.supportSpacing) / 2;
      const f = frameAt(sp, s, s0, s1);
      supports.push(f);
      if (i % P.lampEvery === 0) {
        lamps.push({ p: f.p.clone().addScaledVector(f.B, P.springHeight - 0.3), T: f.T.clone(), B: f.B.clone(), hub: false });
      }
    }
  });

  // ---------- 3. junction hubs ----------
  const hubInfo = [];
  for (const hub of hubs) {
    const K = hub.arms.length;
    if (K < 3) continue;
    // corner resolution adapts to the hub size (min = filletSegs)
    const meanR = hub.arms.reduce((s, x) => s + x.R, 0) / K;
    const k = THREE.MathUtils.clamp(Math.round((meanR - halfW) / 1.7), P.filletSegs, 10);
    const node = new THREE.Vector3(...hub.node.p);
    // gather arm rings in "outward" orientation
    const arms = hub.arms.map((arm) => {
      const t = tunnels[arm.edge];
      const ring = arm.end === 'a' ? t.rings[0] : t.rings[t.rings.length - 1];
      const sp = splines[arm.edge];
      const s = arm.end === 'a' ? t.s0 : t.s1;
      const T = sp.tangentAt(s);
      if (arm.end === 'b') T.negate();
      let floorL2R, leftWall, rightWall, roofL2R;
      const idx = (j) => ring[((j % N) + N) % N];
      const range = (i0, i1) => { const o = []; const st = i1 >= i0 ? 1 : -1; for (let i = i0; i !== i1 + st; i += st) o.push(idx(i)); return o; };
      if (arm.end === 'a') {
        floorL2R = range(0, a);
        rightWall = range(a, a + w);
        roofL2R = range(a + w + r, a + w);
        leftWall = range(N, a + w + r); // N wraps to 0
      } else {
        floorL2R = range(a, 0);
        leftWall = range(a, a + w);
        roofL2R = range(a + w, a + w + r);
        rightWall = range(N, a + w + r);
      }
      const center = md.P(floorL2R[a / 2]);
      return { ...arm, T, Th: new THREE.Vector3(T.x, 0, T.z).normalize(), floorL2R, leftWall, rightWall, roofL2R, center };
    });

    const C = new THREE.Vector3();
    arms.forEach((A) => C.add(A.center));
    C.multiplyScalar(1 / K);

    // Junction floor height field: every tunnel's road surface is extended into
    // the hub and blended with inverse-distance weights (power 3) to the tunnel
    // mouths. On a mouth only that tunnel contributes, so height AND slope are
    // continuous -> no crease/ramp where a sloped tunnel meets the junction.
    const armFields = arms.map((A) => {
      const mid = A.center.clone();
      const e0 = md.P(A.floorL2R[0]), e1 = md.P(A.floorL2R[a]);
      const slope = A.T.y / Math.max(1e-4, Math.hypot(A.T.x, A.T.z));
      return { mid, e0, e1, slope, Th: A.Th };
    });
    const segDist = (p, a0, a1) => {
      const ax = a1.x - a0.x, az = a1.z - a0.z;
      const t = THREE.MathUtils.clamp(((p.x - a0.x) * ax + (p.z - a0.z) * az) / (ax * ax + az * az || 1), 0, 1);
      return Math.hypot(p.x - (a0.x + ax * t), p.z - (a0.z + az * t));
    };
    const floorY = (p) => {
      let sw = 0, sy = 0;
      for (const f of armFields) {
        const dOut = (p.x - f.mid.x) * f.Th.x + (p.z - f.mid.z) * f.Th.z;
        const y = f.mid.y + f.slope * dOut;
        const d = segDist(p, f.e0, f.e1);
        if (d < 1e-4) return y;
        const w = 1 / (d * d * d);
        sw += w; sy += w * y;
      }
      return sy / sw;
    };
    const fixFloor = (p) => { p.y = floorY(p); return p; };
    // corner fillets (floor & spring line)
    const floorFillet = [], springFillet = [];
    const hubCenterForWalls = C.clone().add(new THREE.Vector3(0, P.springHeight * 0.5, 0));
    for (let i = 0; i < K; i++) {
      const A = arms[i], B = arms[(i + 1) % K];
      const mk = (ia, ib, liftRock) => {
        const pa = md.P(ia), pb = md.P(ib);
        const L = pa.distanceTo(pb) * 0.42 + 0.5;
        const c1 = pa.clone().addScaledVector(A.Th, -L);
        const c2 = pb.clone().addScaledVector(B.Th, -L);
        const out = [ia];
        for (let q = 1; q < 2 * k; q++) {
          const p = bezier(pa, c1, c2, pb, q / (2 * k));
          if (!liftRock) fixFloor(p);
          out.push(md.add(p, liftRock ? MAT.ROCK : MAT.CURB, liftRock ? 1 : 0, p.x * 0.5, p.z * 0.5));
        }
        out.push(ib);
        return out;
      };
      floorFillet.push(mk(A.floorL2R[0], B.floorL2R[a], false));
      springFillet.push(mk(A.leftWall[w], B.rightWall[w], true));
    }
    // corner walls: rows along fillet, columns up the wall
    for (let i = 0; i < K; i++) {
      const A = arms[i], B = arms[(i + 1) % K];
      const G = coonsFill(md, floorFillet[i], springFillet[i], A.leftWall, B.rightWall, MAT.ROCK,
        (u, v) => v * v * (3 - 2 * v), (p, u, v) => [u * 4, p.y * 0.5]);
      md.gridPatch(`hub${hub.node.id}_corner${i}`, G, (ctr) => hubCenterForWalls.clone().sub(ctr).setY(0));
    }

    // caps (floor and roof):
    //   * each arm continues into the hub as a strip (a x k quads) whose side
    //     edges ARE the halves of the corner fillets -> tunnel edge loops run
    //     straight in, the floor/wall crease bends round the corner (L flow)
    //   * the remaining K-gon (corners = fillet midpoints) is filled with K
    //     sub-patches meeting in a single valence-K pole (Y / + topology)
    const buildCap = (isRoof) => {
      const lines = arms.map((A) => (isRoof ? A.roofL2R : A.floorL2R));
      const segs = isRoof ? r : a;
      const fil = isRoof ? springFillet : floorFillet;
      const half = segs / 2;
      const mat = isRoof ? MAT.ROCK : MAT.PLATE;
      const rockW = isRoof ? 1 : 0;
      const colFn = isRoof ? null : (p) => plateColor(p);
      const uvFn = (p) => [p.x * 0.5, p.z * 0.5];
      const archH = isRoof ? (P.roofHeight - P.springHeight) + 0.35 : 0;
      // inner lines: from corner midpoint P_j (arm-left side) to P_{j-1} (arm-right side)
      const inner = arms.map((A, j) => {
        const jp = (j + K - 1) % K;
        const iL = fil[j][k], iR = fil[jp][k];
        const pL = md.P(iL), pR = md.P(iR);
        const out = [iL];
        for (let q = 1; q < segs; q++) {
          const t = q / segs;
          const p = pL.clone().lerp(pR, t);
          p.y += archH * Math.sin(Math.PI * t);
          if (!isRoof) fixFloor(p);
          out.push(md.add(p, mat, rockW, p.x * 0.5, p.z * 0.5, colFn ? colFn(p) : null));
        }
        out.push(iR);
        return out;
      });
      // arm strips
      arms.forEach((A, j) => {
        const jp = (j + K - 1) % K;
        const leftSide = fil[j].slice(0, k + 1);            // mouth-left  -> P_j
        const rightSide = fil[jp].slice(k).reverse();       // mouth-right -> P_{j-1}
        const G = coonsFill(md, lines[j], inner[j], leftSide, rightSide, mat, () => rockW, uvFn, colFn, isRoof ? null : fixFloor);
        md.gridPatch(`hub${hub.node.id}_${isRoof ? 'roof' : 'floor'}Strip${j}`, G, () => (isRoof ? new THREE.Vector3(0, -1, 0) : UP.clone()));
      });
      // pole
      const mids = inner.map((l) => md.P(l[half]));
      const Cc = new THREE.Vector3();
      mids.forEach((m) => Cc.add(m));
      Cc.multiplyScalar(1 / K);
      if (isRoof) Cc.y += 0.45; else fixFloor(Cc);
      let sx = 0, sy = 0;
      mids.forEach((m, j) => {
        const ang = Math.atan2(-(m.z - Cc.z), m.x - Cc.x) - (2 * Math.PI * j) / K;
        sx += Math.cos(ang); sy += Math.sin(ang);
      });
      const beta0 = Math.atan2(sy, sx);
      const cIdx = md.add(Cc, mat, rockW, Cc.x * 0.5, Cc.z * 0.5, colFn ? colFn(Cc) : null);
      const spokes = inner.map((l, j) => {
        const M = mids[j];
        const b = beta0 + (2 * Math.PI * j) / K;
        const dir = new THREE.Vector3(Math.cos(b), 0, -Math.sin(b));
        const dist = M.distanceTo(Cc);
        const c1 = Cc.clone().addScaledVector(dir, dist * 0.36);
        const c2 = M.clone().addScaledVector(arms[j].Th, -dist * 0.36);
        const out = [cIdx];
        for (let q = 1; q < half; q++) {
          const p = bezier(Cc, c1, c2, M, q / half);
          if (!isRoof) fixFloor(p);
          out.push(md.add(p, mat, rockW, p.x * 0.5, p.z * 0.5, colFn ? colFn(p) : null));
        }
        out.push(l[half]);
        return out;
      });
      for (let j = 0; j < K; j++) {
        const jn = (j + 1) % K;
        const side1 = inner[j].slice(0, half + 1).reverse();   // M_j -> P_j
        const side2 = inner[jn].slice(half);                   // M_{j+1} -> P_j
        const G = coonsFill(md, spokes[j], side2, spokes[jn], side1, mat, () => rockW, uvFn, colFn, isRoof ? null : fixFloor);
        md.gridPatch(`hub${hub.node.id}_${isRoof ? 'roof' : 'floor'}${j}`, G, () => (isRoof ? new THREE.Vector3(0, -1, 0) : UP.clone()));
      }
      return Cc;
    };
    const Cf = buildCap(false);
    const Cr = buildCap(true);
    hubInfo.push({ id: hub.node.id, center: Cf.clone(), roof: Cr.clone(), arms: arms.map((A) => ({ edge: A.edge, end: A.end, T: A.T.clone(), mouth: A.center.clone() })) });
    lamps.push({ p: Cr.clone().add(new THREE.Vector3(0, -1.6, 0)), T: new THREE.Vector3(1, 0, 0), B: UP.clone(), hub: true });
  }

  // ---------- 4. rock displacement (world-space noise -> seamless) ----------
  const nv = md.pos.length / 3;
  let normals = computeNormals(md);
  const p = new THREE.Vector3();
  for (let i = 0; i < nv && !opts.preview; i++) {
    const wgt = md.rock[i];
    if (wgt <= 0) continue;
    md.P(i, p);
    let d = fbm3(p.x * 0.11, p.y * 0.13, p.z * 0.11, 4) * 2.2 + fbm3(p.x * 0.45, p.y * 0.5, p.z * 0.45, 2) * 0.35;
    d *= P.rockNoise;
    d = THREE.MathUtils.clamp(d, -P.rockNoise * 1.3, 0.25); // + = inward (normals face inward)
    p.x += normals[i * 3] * d * wgt; p.y += normals[i * 3 + 1] * d * wgt; p.z += normals[i * 3 + 2] * d * wgt;
    md.set(i, p);
    // rock colour variation
    const cv = 0.75 + 0.5 * (fbm3(p.x * 0.3 + 11, p.y * 0.3, p.z * 0.3, 3) + 0.5);
    const strata = 0.85 + 0.3 * Math.sin(p.y * 2.1 + fbm3(p.x * 0.1, 0, p.z * 0.1) * 6);
    md.col[i * 3] *= cv * strata; md.col[i * 3 + 1] *= cv * strata * 0.97; md.col[i * 3 + 2] *= cv * strata * 0.92;
  }
  if (!opts.preview) normals = computeNormals(md);

  // ---------- 5. triangulate (shorter diagonal) ----------
  const nq = md.quads.length / 4;
  const index = new Uint32Array(nq * 6);
  const A = new THREE.Vector3(), B = new THREE.Vector3(), Cq = new THREE.Vector3(), D = new THREE.Vector3();
  for (let q = 0; q < nq; q++) {
    const i0 = md.quads[q * 4], i1 = md.quads[q * 4 + 1], i2 = md.quads[q * 4 + 2], i3 = md.quads[q * 4 + 3];
    md.P(i0, A); md.P(i1, B); md.P(i2, Cq); md.P(i3, D);
    const o = q * 6;
    if (A.distanceToSquared(Cq) <= B.distanceToSquared(D)) {
      index.set([i0, i1, i2, i0, i2, i3], o);
    } else {
      index.set([i0, i1, i3, i1, i2, i3], o);
    }
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(md.pos, 3));
  geo.setAttribute('normal', new THREE.Float32BufferAttribute(normals, 3));
  geo.setAttribute('color', new THREE.Float32BufferAttribute(md.col, 3));
  geo.setAttribute('uv', new THREE.Float32BufferAttribute(md.uv, 2));
  geo.setAttribute('metal', new THREE.Float32BufferAttribute(md.metal, 1));
  geo.setIndex(new THREE.BufferAttribute(index, 1));
  geo.computeBoundingBox();
  geo.computeBoundingSphere();

  const conflicts = opts.preview ? [] : findConflicts(net, centerline, cut, P, halfW);
  conflicts.forEach((c) => warnings.push(`tunnels ${c.a} and ${c.b} intersect near (${c.p.x.toFixed(0)}, ${c.p.y.toFixed(0)}, ${c.p.z.toFixed(0)})`));

  return {
    conflicts,
    geometry: geo,
    quads: Uint32Array.from(md.quads),
    patchInfo: md.patchInfo,
    lanes, supports, lamps, centerline, hubs: hubInfo, splines, cut, warnings,
    profile: prof,
    stats: { verts: nv, quads: nq, tris: nq * 2, ms: performance.now() - t0 },
  };
}

function plateColor(p) {
  const c = COLORS[MAT.PLATE];
  // diamond tread plate tiles, slight variation per 2m tile
  const tx = Math.floor(p.x / 2), tz = Math.floor(p.z / 2);
  const v = 0.85 + 0.3 * (((tx * 73856093) ^ (tz * 19349663)) & 255) / 255;
  return [c[0] * v, c[1] * v, c[2] * v, METAL[MAT.PLATE]];
}

function computeNormals(md) {
  const nv = md.pos.length / 3;
  const n = new Float32Array(nv * 3);
  const a = new THREE.Vector3(), b = new THREE.Vector3(), c = new THREE.Vector3(), d = new THREE.Vector3();
  const e1 = new THREE.Vector3(), e2 = new THREE.Vector3();
  const q = md.quads;
  for (let i = 0; i < q.length; i += 4) {
    md.P(q[i], a); md.P(q[i + 1], b); md.P(q[i + 2], c); md.P(q[i + 3], d);
    e1.subVectors(c, a); e2.subVectors(d, b);
    const nn = e1.cross(e2); // area-weighted quad normal
    for (let j = 0; j < 4; j++) {
      const vi = q[i + j];
      n[vi * 3] += nn.x; n[vi * 3 + 1] += nn.y; n[vi * 3 + 2] += nn.z;
    }
  }
  for (let i = 0; i < nv; i++) {
    const l = Math.hypot(n[i * 3], n[i * 3 + 1], n[i * 3 + 2]) || 1;
    n[i * 3] /= l; n[i * 3 + 1] /= l; n[i * 3 + 2] /= l;
  }
  return n;
}

// Tunnels that pass through each other (not at a junction) -> reported so the
// editor can highlight them and the generator can reject the layout.
export function findConflicts(net, centerline, cut, P, halfW) {
  const cell = 6;
  const grid = new Map();
  const key = (x, z) => Math.floor(x / cell) + ',' + Math.floor(z / cell);
  const pts = centerline.filter((_, i) => i % 3 === 0);
  for (const c of pts) {
    const k = key(c.p.x, c.p.z);
    if (!grid.has(k)) grid.set(k, []);
    grid.get(k).push(c);
  }
  const minH = 2 * halfW + 0.6, minV = P.roofHeight + 1.8;
  const found = new Map();
  for (const c of pts) {
    const cx = Math.floor(c.p.x / cell), cz = Math.floor(c.p.z / cell);
    for (let dx = -2; dx <= 2; dx++) for (let dz = -2; dz <= 2; dz++) {
      const list = grid.get((cx + dx) + ',' + (cz + dz));
      if (!list) continue;
      for (const o of list) {
        if (o.edge <= c.edge) continue;
        const ea = net.edges[c.edge], eb = net.edges[o.edge];
        const hd = Math.hypot(c.p.x - o.p.x, c.p.z - o.p.z);
        if (hd > minH || Math.abs(c.p.y - o.p.y) > minV) continue;
        // allow proximity close to a shared junction
        const shared = [ea.a, ea.b].filter((n) => n === eb.a || n === eb.b);
        let nearShared = false;
        for (const n of shared) {
          const np = net.nodes[n].p;
          const r = Math.hypot(c.p.x - np[0], c.p.z - np[2]);
          if (r < 26) nearShared = true;
        }
        if (nearShared) continue;
        const k2 = c.edge + '-' + o.edge;
        if (!found.has(k2)) found.set(k2, { a: c.edge, b: o.edge, p: c.p.clone() });
      }
    }
  }
  return [...found.values()];
}
