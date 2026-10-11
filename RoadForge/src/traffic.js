/* RoadForge — AI traffic: lane graph, Bézier junction connectors, IDM car-following, signal phases and priority rules.
 * Capped at 25 vehicles. Vehicles are rendered as instanced procedural cars (see props.js).
 */
(function (RF) {
  'use strict';
  const V = RF.V, S = RF.spline;
  const MAX_VEHICLES = 25;
  const IDM = { a: 1.6, b: 2.4, s0: 2.5, T: 1.4 };
  const KINDS = [['sedan', 4.7, 0.42], ['hatch', 4.1, 0.25], ['suv', 4.8, 0.2], ['van', 5.4, 0.08], ['truck', 8.5, 0.03], ['bus', 12, 0.02]];

  /* ---------------- lane graph ---------------- */
  function buildGraph(net, layers) {
    const lanes = [], segs = [];
    const byNodeIn = new Map(), byNodeOut = new Map();
    const push = (m, k, v) => { if (!m.has(k)) m.set(k, []); m.get(k).push(v); };
    for (const R of net.roads.values()) {
      const dl = R.lay.driveLanes; if (!dl.length || R.samples.length < 2) continue;
      const fwd = dl.filter((l) => l.dir > 0).sort((a, b) => a.t0 - b.t0); // innermost (left for RHT) first
      const bwd = dl.filter((l) => l.dir < 0).sort((a, b) => b.t0 - a.t0);
      for (const [group, dir] of [[fwd, 1], [bwd, -1]]) {
        group.forEach((l, idx) => {
          const t = (l.t0 + l.t1) / 2;
          const sm = dir > 0 ? R.samples : [...R.samples].reverse();
          const pts = sm.map((f) => RF.roadmesh.surfPoint(R, f.s, t, layers, 0.0));
          const cum = S.cumulativeXZ(pts);
          const lane = { id: `${R.id}:${dir}:${idx}`, kind: 'lane', road: R, dir, t, idx, n: group.length, pts, cum, length: cum[cum.length - 1], from: dir > 0 ? R.road.a : R.road.b, to: dir > 0 ? R.road.b : R.road.a, next: [], vmax: (R.lay.speed || 50) / 3.6, vehicles: [] };
          lanes.push(lane); segs.push(lane); push(byNodeIn, lane.to, lane); push(byNodeOut, lane.from, lane);
        });
      }
    }
    // connectors at nodes
    for (const nodeId of new Set([...byNodeIn.keys(), ...byNodeOut.keys()])) {
      const ins = byNodeIn.get(nodeId) || [], outs = byNodeOut.get(nodeId) || [];
      const rec = net.nodes.get(nodeId);
      for (const li of ins) {
        const pEnd = li.pts[li.pts.length - 1], dEnd = V.normXZ(V.sub(pEnd, li.pts[Math.max(0, li.pts.length - 3)]));
        const candidates = outs.filter((lo) => lo.road !== li.road || li.road.road.a === li.road.road.b).map((lo) => {
          const pStart = lo.pts[0], dStart = V.normXZ(V.sub(lo.pts[Math.min(2, lo.pts.length - 1)], pStart));
          const ang = Math.atan2(dEnd[0] * dStart[2] - dEnd[2] * dStart[0], V.dot(dEnd, dStart)); // signed turn angle
          const turn = Math.abs(ang) < 0.5 ? 'straight' : ang > 0 ? 'right' : 'left';
          return { lo, ang, turn, pStart, dStart };
        });
        const movesByTurn = (turn) => candidates.filter((c) => c.turn === turn);
        const allowed = [];
        const nIn = li.n, isLeftmost = li.idx === 0, isRightmost = li.idx === nIn - 1;
        for (const c of candidates) {
          if (c.turn === 'straight') { if (c.lo.idx === Math.min(li.idx, c.lo.n - 1) || (nIn === 1)) allowed.push(c); }
          else if (c.turn === 'left' && isLeftmost && c.lo.idx === 0) allowed.push(c);
          else if (c.turn === 'right' && isRightmost && c.lo.idx === c.lo.n - 1) allowed.push(c);
        }
        // guarantee every lane has an exit where any exists
        if (!allowed.length && candidates.length) { const s = movesByTurn('straight'); allowed.push(s.length ? s.reduce((a, b) => (Math.abs(a.lo.idx - li.idx) < Math.abs(b.lo.idx - li.idx) ? a : b)) : candidates[0]); }
        // dead end: U-turn onto the opposite lane of the same road
        if (!candidates.length) { const back = outs.find((lo) => lo.road === li.road && lo.dir !== li.dir && lo.idx === 0); if (back) allowed.push({ lo: back, ang: Math.PI, turn: 'uturn', pStart: back.pts[0], dStart: V.normXZ(V.sub(back.pts[1], back.pts[0])) }); }
        for (const c of allowed) {
          const dist = V.distXZ(pEnd, c.pStart);
          const k = c.turn === 'straight' ? 0.35 : c.turn === 'uturn' ? 0.9 : 0.42;
          const P0 = pEnd, P1 = V.madd(pEnd, dEnd, dist * k), P2 = V.madd(c.pStart, c.dStart, -dist * k), P3 = c.pStart;
          const n = Math.max(4, Math.ceil(dist / 1.0)); const pts = [];
          for (let i = 0; i <= n; i++) { const u = i / n, w = 1 - u; pts.push([w * w * w * P0[0] + 3 * w * w * u * P1[0] + 3 * w * u * u * P2[0] + u * u * u * P3[0], w * w * w * P0[1] + 3 * w * w * u * P1[1] + 3 * w * u * u * P2[1] + u * u * u * P3[1], w * w * w * P0[2] + 3 * w * w * u * P1[2] + 3 * w * u * u * P2[2] + u * u * u * P3[2]]); }
          const cum = S.cumulativeXZ(pts);
          const vmax = c.turn === 'straight' ? li.vmax : c.turn === 'uturn' ? 3 : Math.min(li.vmax, c.turn === 'right' ? 6 : 8);
          const conn = { id: li.id + '>' + c.lo.id, kind: 'conn', node: nodeId, rec, from: li, to: c.lo, turn: c.turn, pts, cum, length: cum[cum.length - 1], next: [c.lo], vmax, vehicles: [], arm: li.road.id };
          li.next.push(conn); segs.push(conn);
        }
      }
    }
    // signal controllers
    const signals = new Map();
    for (const rec of net.nodes.values()) {
      if (!rec.signals) continue;
      // pair opposite arms into phases
      const arms = rec.arms.map((a) => ({ id: a.roadId, dir: a.endDirOut }));
      const used = new Set(), phases = [];
      for (let i = 0; i < arms.length; i++) {
        if (used.has(i)) continue; used.add(i); const grp = [arms[i].id];
        let best = -1, bestDot = -0.7;
        for (let j = i + 1; j < arms.length; j++) { if (used.has(j)) continue; const d = V.dot(arms[i].dir, arms[j].dir); if (d < bestDot) { bestDot = d; best = j; } }
        if (best >= 0) { used.add(best); grp.push(arms[best].id); }
        phases.push(grp);
      }
      signals.set(rec.id, { phases, phase: RF.hashStr(rec.id) % phases.length, t: (RF.hashStr(rec.id + 'x') % 8), green: 14, amber: 3, allRed: 1.5, state: new Map() });
    }
    return { lanes, segs, signals };
  }

  function signalState(sig, armId) { return sig.state.get(armId) || 'red'; }
  function tickSignals(graph, dt) {
    for (const sig of graph.signals.values()) {
      sig.t += dt;
      const cyc = sig.green + sig.amber + sig.allRed;
      if (sig.t >= cyc) { sig.t -= cyc; sig.phase = (sig.phase + 1) % sig.phases.length; }
      const cur = sig.phases[sig.phase];
      for (const grp of sig.phases) for (const arm of grp) sig.state.set(arm, grp === cur ? (sig.t < sig.green ? 'green' : sig.t < sig.green + sig.amber ? 'amber' : 'red') : 'red');
    }
  }

  /* ---------------- vehicles ---------------- */
  function pickKind(rng) { let r = rng(); for (const [k, L, w] of KINDS) { r -= w; if (r <= 0) return { kind: k, length: L }; } return { kind: 'sedan', length: 4.7 }; }

  class Traffic {
    constructor(net, layers, opts = {}) {
      this.graph = buildGraph(net, layers);
      this.vehicles = [];
      this.rng = RF.rng(opts.seed || 7);
      this.target = Math.min(MAX_VEHICLES, opts.count ?? 18);
      this.time = 0;
    }
    setCount(n) { this.target = RF.clamp(n | 0, 0, MAX_VEHICLES); }
    spawn() {
      const lanes = this.graph.lanes.filter((l) => l.length > 30);
      if (!lanes.length) return false;
      for (let tries = 0; tries < 12; tries++) {
        const lane = lanes[Math.floor(this.rng() * lanes.length)];
        const s = 10 + this.rng() * (lane.length - 20);
        if (lane.vehicles.some((v) => Math.abs(v.s - s) < 14)) continue;
        const { kind, length } = pickKind(this.rng);
        const col = RF.props.CAR_COLORS[Math.floor(this.rng() * RF.props.CAR_COLORS.length)];
        const veh = { id: this.vehicles.length + ':' + (this.time | 0), kind, length, color: col, seg: lane, s, v: lane.vmax * 0.6, plan: null, wait: 0, pos: null, dir: null };
        this.vehicles.push(veh); lane.vehicles.push(veh);
        return true;
      }
      return false;
    }
    choosePlan(veh) {
      const nx = veh.seg.next; if (!nx.length) return null;
      // prefer straight; avoid U-turns when alternatives exist
      const weights = nx.map((c) => (c.turn === 'straight' ? 3 : c.turn === 'uturn' ? 0.2 : 1));
      let r = this.rng() * weights.reduce((a, b) => a + b, 0);
      for (let i = 0; i < nx.length; i++) { r -= weights[i]; if (r <= 0) return nx[i]; }
      return nx[nx.length - 1];
    }
    /** Nearest leader ahead along the vehicle's path (same segment, then planned segments). Returns {gap, v} or null. */
    leader(veh, maxDist = 90) {
      let seg = veh.seg, s = veh.s, dist = 0, best = null;
      for (let hop = 0; hop < 4 && dist < maxDist; hop++) {
        for (const o of seg.vehicles) { if (o === veh) continue; const gap = (o.s - o.length) - (hop === 0 ? s : 0) + dist; if (hop === 0 && o.s <= s) continue; if (gap >= -o.length && (!best || gap < best.gap)) best = { gap: Math.max(gap, 0.05), v: o.v }; }
        if (best) break;
        dist += seg.length - (hop === 0 ? s : 0);
        const nxt = hop === 0 ? (veh.plan || seg.next[0]) : seg.next[0];
        if (!nxt) break; seg = nxt;
      }
      return best;
    }
    /** Should a vehicle wait before entering the junction segment `conn`? */
    mustYield(veh, conn, distToEntry) {
      const g = this.graph;
      const rec = conn.rec;
      if (!rec) return false;
      const sig = g.signals.get(conn.node);
      if (sig) {
        const st = signalState(sig, conn.arm);
        if (st === 'red' || (st === 'amber' && distToEntry > veh.v * 1.0)) return distToEntry > -1;
        return false;
      }
      if (conn.turn === 'uturn') return false;
      // priority: give way to vehicles already inside the junction on conflicting movements, and to major-road traffic approaching
      const myW = conn.from.road.lay.hwL + conn.from.road.lay.hwR;
      for (const seg of conn.from.next.length ? g.segs : []) { void seg; break; }
      for (const other of this.vehicles) {
        if (other === veh) continue;
        const os = other.seg;
        if (os.kind === 'conn' && os.node === conn.node && os.arm !== conn.arm) {
          if (other.s < os.length - 2) return true; // someone is crossing
        }
        if (os.kind === 'lane' && os.to === conn.node && os !== conn.from) {
          const otherW = os.road.lay.hwL + os.road.lay.hwR;
          const major = otherW > myW + 0.5 || (Math.abs(otherW - myW) <= 0.5 && this.onRight(conn, os));
          const toEnd = os.length - other.s;
          if (major && toEnd < Math.max(18, other.v * 3.5) && other.v > 0.5) return true;
        }
      }
      return false;
    }
    onRight(conn, otherLane) {
      // is the other approach on the right of ours? (priority to the right)
      const dMe = V.normXZ(V.sub(conn.pts[1], conn.pts[0]));
      const pO = otherLane.pts[otherLane.pts.length - 1]; const rel = V.sub(pO, conn.pts[0]);
      return dMe[0] * rel[2] - dMe[2] * rel[0] > 0;
    }
    step(dt) {
      dt = Math.min(dt, 0.1); this.time += dt;
      tickSignals(this.graph, dt);
      while (this.vehicles.length < this.target) if (!this.spawn()) break;
      while (this.vehicles.length > this.target) { const v = this.vehicles.pop(); v.seg.vehicles = v.seg.vehicles.filter((x) => x !== v); }
      for (const veh of this.vehicles) {
        const seg = veh.seg;
        if (!veh.plan && seg.kind === 'lane') veh.plan = this.choosePlan(veh);
        // desired speed & gaps
        let v0 = Math.min(seg.vmax, veh.plan && seg.kind === 'lane' && (seg.length - veh.s) < 30 ? Math.max(veh.plan.vmax, 4) : 99);
        if (seg.kind === 'conn') v0 = seg.vmax;
        let sStar = 0, gapTerm = 0;
        const lead = this.leader(veh);
        if (lead) { const dv = veh.v - lead.v; const sS = IDM.s0 + Math.max(0, veh.v * IDM.T + (veh.v * dv) / (2 * Math.sqrt(IDM.a * IDM.b))); gapTerm = Math.pow(sS / Math.max(lead.gap, 0.1), 2); sStar = sS; }
        // junction entry / stop line
        if (seg.kind === 'lane' && veh.plan) {
          const dEntry = seg.length - veh.s - 1.0;
          if (dEntry < 45 && this.mustYield(veh, veh.plan, dEntry)) {
            const sS = IDM.s0 + Math.max(0, veh.v * IDM.T + (veh.v * veh.v) / (2 * Math.sqrt(IDM.a * IDM.b)));
            gapTerm = Math.max(gapTerm, Math.pow(sS / Math.max(dEntry, 0.1), 2));
            veh.wait += dt;
            if (veh.wait > 9 && !this.graph.signals.get(veh.plan.node)) gapTerm = lead ? gapTerm : 0; // anti-deadlock: creep after 9 s at priority junctions
          } else veh.wait = Math.max(0, veh.wait - dt);
        }
        void sStar;
        const acc = IDM.a * (1 - Math.pow(veh.v / Math.max(v0, 0.5), 4) - gapTerm);
        veh.v = Math.max(0, veh.v + acc * dt);
        if (veh.v < 0.02 && gapTerm > 1.5) veh.v = 0;
        veh.s += veh.v * dt;
        // segment transitions
        while (veh.s > seg.length && veh.seg) {
          const nxt = seg.kind === 'lane' ? veh.plan : seg.next[0];
          seg.vehicles = seg.vehicles.filter((x) => x !== veh);
          if (!nxt) { veh.s = 0; veh.seg = this.graph.lanes[Math.floor(this.rng() * this.graph.lanes.length)]; veh.seg.vehicles.push(veh); veh.plan = null; break; }
          veh.s -= seg.length; veh.seg = nxt; nxt.vehicles.push(veh); veh.plan = nxt.kind === 'lane' ? null : veh.plan;
          if (nxt.kind === 'lane') veh.plan = this.choosePlan(veh);
          break;
        }
        // pose
        const cur = veh.seg; const sMid = RF.clamp(veh.s - veh.length / 2, 0, cur.length);
        const e = S.evalAt(cur.pts, cur.cum, sMid);
        const ahead = S.evalAt(cur.pts, cur.cum, Math.min(cur.length, sMid + 1.5)).p, behind = S.evalAt(cur.pts, cur.cum, Math.max(0, sMid - 1.5)).p;
        veh.pos = e.p; veh.dir = V.norm(V.sub(ahead, behind));
      }
      // keep per-segment lists ordered
      for (const seg of this.graph.segs) if (seg.vehicles.length > 1) seg.vehicles.sort((a, b) => a.s - b.s);
    }
    /** Instances per part key for rendering. */
    instances(placed) {
      for (const veh of this.vehicles) {
        if (!veh.pos) continue;
        const parts = RF.props.get('vehicle', veh.kind);
        const m = RF.M4.frame([veh.pos[0], veh.pos[1], veh.pos[2]], veh.dir);
        for (const part of parts) {
          const key = 'veh:' + veh.kind + ':' + part.material;
          let rec = placed.get(key); if (!rec) { rec = { parts: [part], instances: [], owners: [] }; placed.set(key, rec); }
          const out = new Array(24); for (let i = 0; i < 16; i++) out[i] = m[i];
          const paint = part.material === 'carpaint';
          out[16] = veh.color[0]; out[17] = veh.color[1]; out[18] = veh.color[2]; out[19] = paint ? 1 : 0; out[20] = 1; out[21] = 0; out[22] = 0; out[23] = 0;
          rec.instances.push(out); rec.owners.push('traffic');
        }
      }
    }
  }

  RF.traffic = { Traffic, buildGraph, MAX_VEHICLES, signalState };
})(globalThis.RF || (globalThis.RF = {}));
