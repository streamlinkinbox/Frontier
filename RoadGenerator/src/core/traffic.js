// AI traffic: lane graph from the plan, signal controller, car-following (IDM-style) with junction
// yielding, plus trains on rail lines. Pure simulation — the app turns vehicle poses into instance matrices.
import { add, sub, mul, madd, norm, lerp, clamp, mat4, makeRng, bezierPoint, cross, dist } from "./math.js";
import { roadPoint, trafficLanes, stationRange, approachSide } from "./query.js";
import { VEHICLE_KINDS, VEHICLE_LENGTH } from "./props.js";
import { railPoint } from "./rail.js";

export const MAX_VEHICLES = 25;
export function defaultTrafficSettings() { return { enabled: true, vehicles: 18, trains: true, speedScale: 1, seed: 11 }; }

/** Sample a lane: polyline of {p, d} every ~2 m (station range respects junction cuts). */
function sampleLane(R, lane, dir) {
  const { s0, s1 } = stationRange(R);
  const n = Math.max(2, Math.ceil((s1 - s0) / 2));
  const pts = [];
  for (let i = 0; i <= n; i++) {
    const s = dir > 0 ? lerp(s0, s1, i / n) : lerp(s1, s0, i / n);
    pts.push(roadPoint(R, s, lane.xc, 0.0));
  }
  return pts;
}
function polyLength(pts) { let L = 0; for (let i = 1; i < pts.length; i++) L += dist(pts[i], pts[i - 1]); return L; }
function polyAt(pts, cum, s) {
  if (s <= 0) return { p: pts[0], d: norm(sub(pts[1], pts[0])) };
  const L = cum[cum.length - 1]; if (s >= L) { const n = pts.length; return { p: pts[n - 1], d: norm(sub(pts[n - 1], pts[n - 2])) }; }
  let lo = 0, hi = cum.length - 1; while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
  const t = (s - cum[lo]) / Math.max(1e-6, cum[lo + 1] - cum[lo]);
  return { p: [lerp(pts[lo][0], pts[lo + 1][0], t), lerp(pts[lo][1], pts[lo + 1][1], t), lerp(pts[lo][2], pts[lo + 1][2], t)], d: norm(sub(pts[lo + 1], pts[lo])) };
}
function makeEdge(pts, meta) { const cum = [0]; for (let i = 1; i < pts.length; i++) cum.push(cum[i - 1] + dist(pts[i], pts[i - 1])); return { pts, cum, length: cum[cum.length - 1], next: [], vehicles: [], ...meta }; }

/** Build the lane graph. Returns { edges, byNode } */
export function buildLaneGraph(plan) {
  const edges = [];
  const laneEdges = new Map(); // roadId → [{edge, lane, dir}]
  for (const R of plan.roads.values()) {
    const arr = [];
    for (const lane of trafficLanes(R)) {
      const dir = lane.dir || 1;
      const pts = sampleLane(R, lane, dir);
      const from = dir > 0 ? R.road.a : R.road.b, to = dir > 0 ? R.road.b : R.road.a;
      const e = makeEdge(pts, { kind: "lane", road: R, lane, dir, from, to, speed: (R.road.profile.speed || 50) / 3.6, laneIndex: lane.index, laneCount: trafficLanes(R).filter((l) => l.dir === lane.dir).length });
      edges.push(e); arr.push(e);
    }
    laneEdges.set(R.road.id, arr);
  }
  // junction connectors
  for (const N of plan.nodes.values()) {
    const incoming = [], outgoing = [];
    for (const arm of N.arms) for (const e of laneEdges.get(arm.roadId) || []) { if (e.to === N.node.id) incoming.push({ e, arm }); if (e.from === N.node.id) outgoing.push({ e, arm }); }
    for (const inc of incoming) {
      const pIn = inc.e.pts[inc.e.pts.length - 1], dIn = norm(sub(pIn, inc.e.pts[inc.e.pts.length - 2]));
      let targets = outgoing.filter((o) => o.arm !== inc.arm);
      if (!targets.length) targets = outgoing; // dead end → U-turn
      const classify = (o) => {
        const pOut = o.e.pts[0], dOut = norm(sub(o.e.pts[1], pOut));
        const crossY = dIn[0] * dOut[2] - dIn[2] * dOut[0], dotD = dIn[0] * dOut[0] + dIn[2] * dOut[2];
        // right-hand traffic: a right turn is clockwise seen from above (+y). 2-arm joints are always "straight".
        return N.arms.length <= 2 ? "straight" : dotD > 0.85 ? "straight" : crossY > 0 ? "right" : "left";
      };
      // lane discipline: right turns from the rightmost lane, left turns from the leftmost, straight from any
      const rightmost = inc.e.laneIndex === inc.e.laneCount - 1, leftmost = inc.e.laneIndex === 0;
      let allowed = targets.filter((o) => {
        const turn = classify(o);
        if (inc.e.laneCount <= 1) return true;
        if (turn === "right") return rightmost;
        if (turn === "left") return leftmost;
        return Math.abs(inc.e.laneIndex - o.e.laneIndex) <= 1 || o.e.laneCount <= 1;
      });
      if (!allowed.length) allowed = targets; // never strand a lane
      for (const o of allowed) {
        const pOut = o.e.pts[0], dOut = norm(sub(o.e.pts[1], pOut));
        const L = dist(pIn, pOut);
        const turn = classify(o);
        const h = clamp(L * 0.42, 1.5, 30);
        const p1 = madd(pIn, dIn, h), p2 = madd(pOut, dOut, -h);
        const n = Math.max(3, Math.ceil(L / 1.5));
        const pts = []; for (let i = 0; i <= n; i++) pts.push(bezierPoint(pIn, p1, p2, pOut, i / n));
        const c = makeEdge(pts, { kind: "connector", node: N, from: N.node.id, to: N.node.id, armIn: inc.arm, armOut: o.arm, turn, speed: turn === "straight" ? inc.e.speed * 0.8 : Math.min(inc.e.speed, 7), uturn: o.arm === inc.arm });
        c.next.push(o.e);
        edges.push(c); inc.e.next.push(c);
      }
    }
  }
  return { edges, laneEdges };
}

/** Signal controller: groups arms into phases (opposite arms together). */
export class SignalController {
  constructor(signals, plan) {
    this.nodes = signals.map((sig) => {
      const N = plan.nodes.get(sig.nodeId);
      const arms = sig.arms.map((a) => ({ ...a, angle: N.arms[a.index].angle }));
      const phases = [];
      const used = new Set();
      arms.forEach((a, i) => {
        if (used.has(i)) return;
        const group = [i]; used.add(i);
        arms.forEach((b, j) => { if (!used.has(j)) { let d = Math.abs(b.angle - a.angle); d = Math.min(d, Math.PI * 2 - d); if (Math.abs(d - Math.PI) < 0.6) { group.push(j); used.add(j); } } });
        phases.push(group);
      });
      return { sig, N, arms, phases, phase: 0, t: 0, green: 11, amber: 3, allRed: 1.5, offset: 0 };
    });
    this.nodes.forEach((n, i) => { n.t = (i * 5.3) % (n.green + n.amber + n.allRed); });
  }
  update(dt) { for (const n of this.nodes) { n.t += dt; const cycle = n.green + n.amber + n.allRed; if (n.t >= cycle) { n.t -= cycle; n.phase = (n.phase + 1) % n.phases.length; } } }
  /** 'green' | 'amber' | 'red' for arm index `armIndex` at node */
  state(nodeId, armIndex) {
    const n = this.nodes.find((q) => q.sig.nodeId === nodeId); if (!n) return "green";
    const ai = n.arms.findIndex((a) => a.index === armIndex); if (ai < 0) return "green";
    if (!n.phases[n.phase].includes(ai)) return "red";
    return n.t < n.green ? "green" : n.t < n.green + n.amber ? "amber" : "red";
  }
  /** All lens poses grouped by colour: { red: [{p, facing}], amber: [], green: [] } */
  lenses() {
    const out = { red: [], amber: [], green: [] };
    for (const n of this.nodes) n.arms.forEach((a, ai) => {
      const st = !n.phases[n.phase].includes(ai) ? "red" : n.t < n.green ? "green" : n.t < n.green + n.amber ? "amber" : "red";
      for (const L of a.lenses) out[st].push({ p: L[st], facing: L.facing });
    });
    return out;
  }
}

export class Traffic {
  constructor(plan, furnishOut, rails, S = defaultTrafficSettings()) {
    this.S = { ...defaultTrafficSettings(), ...S };
    this.plan = plan;
    this.graph = buildLaneGraph(plan);
    this.signals = new SignalController(furnishOut?.signals || [], plan);
    this.rng = makeRng(this.S.seed);
    this.vehicles = [];
    this.trains = [];
    this.time = 0;
    const lanes = this.graph.edges.filter((e) => e.kind === "lane" && e.length > 12);
    const count = Math.min(MAX_VEHICLES, this.S.vehicles);
    for (let i = 0; i < count && lanes.length; i++) {
      const e = lanes[Math.floor(this.rng() * lanes.length)];
      const kind = this.rng() < 0.12 ? "bus" : this.rng() < 0.1 ? "truck" : this.rng() < 0.2 ? "van" : VEHICLE_KINDS[Math.floor(this.rng() * 3)];
      const v = { id: i, kind, length: VEHICLE_LENGTH[kind], edge: e, s: this.rng() * e.length, v: e.speed * 0.5, desired: e.speed * (0.85 + this.rng() * 0.25), accel: kind === "bus" || kind === "truck" ? 1.1 : 2.2, brake: 3.5, gap: 2.0 + this.rng(), tHead: 1.4, waiting: 0 };
      e.vehicles.push(v); this.vehicles.push(v);
    }
    if (this.S.trains && rails) for (const rl of rails) {
      if (rl.line.trains === false) continue;
      const cars = rl.tunnel ? 4 : 3;
      this.trains.push({ rail: rl, cars, s: this.rng() * rl.length, v: 0, dir: 1, speed: rl.tunnel ? 18 : 22, dwell: 0 });
    }
    this.occupancy = new Map(); // nodeId → Set(vehicle)
  }

  /** Leader on the same edge (closest ahead) or on the next edge chosen. */
  _leader(v) {
    let best = null, gap = Infinity;
    for (const o of v.edge.vehicles) if (o !== v && o.s > v.s && o.s - v.s < gap) { gap = o.s - v.s; best = o; }
    if (best) return { veh: best, gap: gap - best.length / 2 - v.length / 2 };
    // look one edge ahead
    const nxt = v.planned || v.edge.next[0];
    if (nxt) {
      for (const o of nxt.vehicles) { const g = v.edge.length - v.s + o.s; if (g < gap) { gap = g; best = o; } }
      if (best) return { veh: best, gap: gap - best.length / 2 - v.length / 2 };
      if (nxt.kind === "connector" && nxt.next[0]) for (const o of nxt.next[0].vehicles) { const g = v.edge.length - v.s + nxt.length + o.s; if (g < gap) { gap = g; best = o; } }
      if (best) return { veh: best, gap: gap - best.length / 2 - v.length / 2 };
    }
    return null;
  }
  _choose(v) {
    const n = v.edge.next; if (!n.length) return null;
    // prefer straight (60%), then random
    const straight = n.filter((e) => e.turn === "straight");
    if (straight.length && this.rng() < 0.6) return straight[Math.floor(this.rng() * straight.length)];
    return n[Math.floor(this.rng() * n.length)];
  }
  /** Move a stranded vehicle (one-way dead end) to the start of a quiet lane. */
  _respawn(v) {
    const lanes = this.graph.edges.filter((e) => e.kind === "lane" && e.length > 20 && !e.vehicles.some((o) => o.s < 15));
    if (!lanes.length) { v.s = v.edge.length; v.v = 0; return; }
    const e = lanes[Math.floor(this.rng() * lanes.length)];
    v.edge.vehicles.splice(v.edge.vehicles.indexOf(v), 1);
    v.edge = e; v.s = 0.5; v.v = Math.min(v.v, e.speed * 0.5); v.planned = null; v.waiting = 0; e.vehicles.push(v);
  }
  /** Can v enter the connector c now? Signals + simple junction occupancy. */
  _mayEnter(v, c) {
    const nodeId = c.node.node.id;
    if (this.signals.nodes.some((q) => q.sig.nodeId === nodeId)) {
      const st = this.signals.state(nodeId, c.node.arms.indexOf(c.armIn));
      if (st === "red") return false;
      if (st === "amber" && v.v < 6) return false;
      return true;
    }
    // unsignalised: at most one vehicle inside from a different arm; let straight-through on major road go
    const occ = this.occupancy.get(nodeId);
    if (!occ || !occ.size) return true;
    for (const o of occ) if (o.edge.armIn !== c.armIn && o.edge.armOut !== c.armOut) return false;
    return true;
  }
  update(dt) {
    dt = Math.min(dt, 0.1) * this.S.speedScale;
    this.time += dt;
    this.signals.update(dt);
    for (const v of this.vehicles) {
      const e = v.edge;
      if (!v.planned && e.kind === "lane") v.planned = this._choose(v);
      // desired speed, curvature slowdown on connectors
      let vDes = Math.min(v.desired, e.speed) * this.S.speedScale;
      // IDM
      const lead = this._leader(v);
      let sStar = 0, acc = v.accel * (1 - Math.pow(v.v / Math.max(0.1, vDes), 4));
      if (lead) {
        const dv = v.v - lead.veh.v;
        sStar = v.gap + Math.max(0, v.v * v.tHead + (v.v * dv) / (2 * Math.sqrt(v.accel * v.brake)));
        acc -= v.accel * Math.pow(sStar / Math.max(0.5, lead.gap), 2);
      }
      // stop line handling: approaching the end of a lane edge whose connector is blocked
      if (e.kind === "lane" && v.planned) {
        const remain = e.length - v.s - v.length / 2 - 0.6;
        if (!this._mayEnter(v, v.planned) && remain < 60) {
          // decelerate to stop at the line (virtual standing leader)
          const g = Math.max(0.3, remain);
          const sS = v.gap * 0.5 + v.v * v.tHead * 0.6 + (v.v * v.v) / (2 * Math.sqrt(v.accel * v.brake));
          acc = Math.min(acc, v.accel * (1 - Math.pow(v.v / Math.max(0.1, vDes), 4) - Math.pow(sS / g, 2)));
          if (remain < 0.6 && v.v < 0.3) { v.v = 0; acc = Math.min(acc, 0); v.waiting += dt; }
        }
      }
      v.v = Math.max(0, v.v + acc * dt);
      v.s += v.v * dt;
      // edge transitions
      while (v.s >= e.length && v.edge === e) {
        const nxt = e.kind === "lane" ? v.planned : e.next[0];
        if (!nxt) { this._respawn(v); break; }
        if (e.kind === "lane" && !this._mayEnter(v, nxt)) { v.s = e.length - 0.01; v.v = 0; break; }
        v.s -= e.length;
        e.vehicles.splice(e.vehicles.indexOf(v), 1);
        if (e.kind === "connector") { const occ = this.occupancy.get(e.node.node.id); occ?.delete(v); }
        v.edge = nxt; nxt.vehicles.push(v); v.planned = null; v.waiting = 0;
        if (nxt.kind === "connector") { let occ = this.occupancy.get(nxt.node.node.id); if (!occ) this.occupancy.set(nxt.node.node.id, (occ = new Set())); occ.add(v); }
        break;
      }
    }
    // trains: shuttle between the ends with dwell
    for (const t of this.trains) {
      if (t.dwell > 0) { t.dwell -= dt; continue; }
      const L = t.rail.length, stopDist = 60;
      const toEnd = t.dir > 0 ? L - t.s : t.s;
      const target = toEnd < stopDist ? Math.max(1.5, t.speed * Math.sqrt(toEnd / stopDist)) : t.speed;
      t.v += clamp(target - t.v, -1.2 * dt, 0.8 * dt);
      t.s += t.v * t.dir * dt;
      const margin = t.cars * 20 / 2 + 2;
      if (t.s >= L - margin) { t.s = L - margin; t.dir = -1; t.v = 0; t.dwell = 6; } else if (t.s <= margin) { t.s = margin; t.dir = 1; t.v = 0; t.dwell = 6; }
    }
  }
  /** Vehicle poses → { kind: Float32Array(16n) } instance matrices (prop frame: +Z forward). */
  poses() {
    const groups = {};
    for (const v of this.vehicles) {
      const { p, d } = polyAt(v.edge.pts, v.edge.cum, v.s);
      const f = norm([d[0], 0, d[2]]);
      const left = [f[2], 0, -f[0]];
      // pitch along the road slope
      const fwd = norm(d), up = norm(cross(fwd, left));
      const m = mat4.fromBasis(left, up[1] > 0 ? up : mul(up, -1), fwd, [p[0], p[1] + 0.02, p[2]]);
      (groups[v.kind] || (groups[v.kind] = [])).push(m);
    }
    const out = {};
    for (const k in groups) { const arr = new Float32Array(groups[k].length * 16); groups[k].forEach((m, i) => arr.set(m, i * 16)); out[k] = arr; }
    return out;
  }
  trainPoses() {
    const mats = [];
    for (const t of this.trains) {
      for (let c = 0; c < t.cars; c++) {
        const sc = t.s + (c - (t.cars - 1) / 2) * 20.6 * t.dir;
        const s0 = clamp(sc - 7, 0, t.rail.length), s1 = clamp(sc + 7, 0, t.rail.length);
        const a = railPoint(t.rail, s0, 0, 0), b = railPoint(t.rail, s1, 0, 0);
        const fwd = norm(sub(b, a)); const left = norm([fwd[2], 0, -fwd[0]]); const up = norm(cross(fwd, left));
        const mid = mul(add(a, b), 0.5);
        mats.push(mat4.fromBasis(left, up[1] > 0 ? up : mul(up, -1), fwd, [mid[0], mid[1] + 0.02, mid[2]]));
      }
    }
    const arr = new Float32Array(mats.length * 16); mats.forEach((m, i) => arr.set(m, i * 16));
    return arr;
  }
}
