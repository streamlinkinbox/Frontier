// Road network model: nodes (junction pivots) + roads (cubic splines between nodes) + computed arms.
import { Spline, sub, add, mul, norm, normXZ, rightXZ, angleXZ, len, dist, distXZ, lerp, smoothstep, clamp, madd, dotXZ } from "./math.js";
import { cloneProfile, crossSection, edgeKit, PROFILES } from "./profile.js";

let _uid = 1;
export const uid = (prefix = "id") => `${prefix}${(_uid++).toString(36)}${Date.now().toString(36).slice(-3)}`;

export class Network {
  constructor() {
    this.nodes = new Map();
    this.roads = new Map();
    this.rails = new Map(); // railway lines: {id, pts, ha, hb, elevated, tunnel}
    this.props = new Map(); // free-placed props: {id, kind, p, yaw, params}
  }
  addNode(p, opts = {}) {
    const id = opts.id || uid("n");
    const node = { id, p: [p[0], p[1] ?? 0, p[2]], radius: opts.radius ?? 9, control: opts.control ?? "auto", crosswalks: opts.crosswalks ?? "auto", name: opts.name || "", roundabout: opts.roundabout || 0, locked: false };
    this.nodes.set(id, node);
    return node;
  }
  addRoad(a, b, opts = {}) {
    const id = opts.id || uid("r");
    const profile = typeof opts.profile === "string" ? cloneProfile(opts.profile) : opts.profile ? { ...cloneProfile(opts.profile.id || "local"), ...opts.profile } : cloneProfile("local");
    const road = {
      id, a: typeof a === "string" ? a : a.id, b: typeof b === "string" ? b : b.id,
      pts: (opts.pts || []).map((p) => [p[0], p[1] ?? 0, p[2]]),
      ta: opts.ta ? [...opts.ta] : null, tb: opts.tb ? [...opts.tb] : null,
      profile, name: opts.name || "",
      bridge: opts.bridge ?? "auto", // auto | always | never  (auto → bridge wherever deck is above ground)
      bridgeType: opts.bridgeType || "girder", // girder | box | concrete
      tunnel: opts.tunnel ?? "auto", // auto → tunnel where below ground
      construction: opts.construction || null, // {from, to} stations with road works
      speedBumps: opts.speedBumps || [], // stations
      potholes: opts.potholes ?? 0, // density 0..1
      wear: opts.wear ?? 0.5,
      guardrail: opts.guardrail ?? "auto", // auto | none | wbeam | thrie | cable | concrete | box
      streetlights: opts.streetlights ?? "auto",
      billboards: opts.billboards ?? false,
      trees: false,
      oneWay: profile.lanesB === 0,
    };
    this.roads.set(id, road);
    return road;
  }
  addRail(pts, opts = {}) {
    const id = opts.id || uid("t");
    const rail = { id, pts: pts.map((p) => [p[0], p[1] ?? 0.03, p[2]]), tunnel: !!opts.tunnel, name: opts.name || "", trains: opts.trains ?? true };
    this.rails.set(id, rail);
    return rail;
  }
  removeRail(id) { this.rails.delete(id); }
  removeRoad(id) { this.roads.delete(id); this._pruneNodes(); }
  removeNode(id) { for (const [rid, r] of [...this.roads]) if (r.a === id || r.b === id) this.roads.delete(rid); this.nodes.delete(id); }
  _pruneNodes() { for (const [id] of [...this.nodes]) if (!this.roadsAt(id).length) this.nodes.delete(id); }
  roadsAt(nodeId) { const out = []; for (const r of this.roads.values()) if (r.a === nodeId || r.b === nodeId) out.push(r); return out; }
  otherEnd(road, nodeId) { return road.a === nodeId ? road.b : road.a; }
  /** Split a road at station s, inserting a new node. Returns the new node. */
  splitRoad(roadId, s) {
    const r = this.roads.get(roadId), sp = this.splineOf(r);
    const p = sp.pointAt(s), t = sp.tangentAt(s);
    const n = this.addNode(p, { radius: 8 });
    // Distribute interior points by station
    const left = [], right = [];
    for (const q of r.pts) { const cs = sp.closestStation(q).s; (cs < s ? left : right).push(q); }
    const tan = mul(t, Math.min(s, sp.length - s) * 0.8);
    const r1 = this.addRoad(r.a, n.id, { ...r, id: undefined, pts: left, ta: r.ta, tb: tan, profile: r.profile });
    const r2 = this.addRoad(n.id, r.b, { ...r, id: undefined, pts: right, ta: tan, tb: r.tb, profile: r.profile });
    this.roads.delete(roadId);
    return { node: n, roads: [r1, r2] };
  }
  splineOf(road) {
    const A = this.nodes.get(road.a), B = this.nodes.get(road.b);
    const pts = [A.p, ...road.pts, B.p];
    const L = dist(A.p, B.p);
    let ta = road.ta, tb = road.tb;
    const nextA = pts[1], prevB = pts[pts.length - 2];
    if (!ta || len(ta) < 1e-6) ta = mul(norm(sub(nextA, A.p)), Math.min(L, dist(nextA, A.p)));
    if (!tb || len(tb) < 1e-6) tb = mul(norm(sub(B.p, prevB)), Math.min(L, dist(B.p, prevB)));
    return new Spline(pts, ta, tb);
  }
  toJSON() {
    return {
      version: 1,
      nodes: [...this.nodes.values()],
      roads: [...this.roads.values()],
      rails: [...this.rails.values()],
      props: [...this.props.values()],
    };
  }
  static fromJSON(j) {
    const n = new Network();
    for (const nd of j.nodes || []) n.nodes.set(nd.id, { ...nd });
    for (const r of j.roads || []) n.roads.set(r.id, { ...r, profile: { ...cloneProfile(r.profile?.id || "local"), ...r.profile } });
    for (const r of j.rails || []) n.rails.set(r.id, { ...r });
    for (const p of j.props || []) n.props.set(p.id, { ...p });
    return n;
  }
  clone() { return Network.fromJSON(JSON.parse(JSON.stringify(this.toJSON()))); }
}

/**
 * Analyse the network: per-road splines/sections, per-node arms sorted by angle.
 * Returns a "plan" consumed by the geometry builders.
 */
export function analyse(net, settings = {}) {
  const roads = new Map();
  for (const r of net.roads.values()) {
    const spline = net.splineOf(r);
    const section = crossSection(r.profile);
    roads.set(r.id, { road: r, spline, length: spline.length, section, kitL: edgeKit(r.profile), kitR: edgeKit(r.profile), cutA: 0, cutB: 0, extraCols: 0 });
  }
  const nodes = new Map();
  for (const n of net.nodes.values()) {
    const arms = [];
    for (const r of net.roadsAt(n.id)) {
      const R = roads.get(r.id);
      const atStart = r.a === n.id;
      // a road looping from a node to itself contributes two arms
      const ends = atStart && r.b === n.id ? [true, false] : [atStart];
      for (const isStart of ends) {
        const d = isStart ? R.spline.tangentAt(0) : mul(R.spline.tangentAt(R.length), -1);
        const dxz = normXZ(d);
        arms.push({
          node: n, roadId: r.id, R, isStart, d: dxz, angle: angleXZ(dxz), r: rightXZ(dxz),
          // arm-local widths: the arm's RIGHT (looking outward from the node) is the road's right when isStart
          wRight: isStart ? R.section.widthR : R.section.widthL,
          wLeft: isStart ? R.section.widthL : R.section.widthR,
          kitRight: isStart ? R.kitR : R.kitL,
          kitLeft: isStart ? R.kitL : R.kitR,
          cut: 0,
        });
      }
    }
    arms.sort((a, b) => a.angle - b.angle);
    nodes.set(n.id, { node: n, arms, kind: arms.length <= 1 ? "end" : arms.length === 2 ? "joint" : "junction" });
  }
  return { net, roads, nodes, settings };
}

/**
 * Road frame with straightened approaches: within `blend` metres of a junction cut the plan position eases
 * onto the ideal straight approach line so junction geometry (built on straight lines) is watertight.
 */
export function makeFrameFn(plan, R) {
  const sp = R.spline, L = R.length;
  const A = plan.net.nodes.get(R.road.a), B = plan.net.nodes.get(R.road.b);
  const dA = normXZ(sp.tangentAt(0)), dB = normXZ(sp.tangentAt(L));
  const nodeA = plan.nodes.get(R.road.a), nodeB = plan.nodes.get(R.road.b);
  const straightA = nodeA && nodeA.kind === "junction", straightB = nodeB && nodeB.kind === "junction";
  const blendLen = (cut) => Math.max(8, Math.min(30, cut * 1.2 + 6));
  return (s) => {
    const f = sp.frameAt(s);
    let p = f.p, dxz = f.dxz;
    if (straightA && s < R.cutA + blendLen(R.cutA)) {
      const t = smoothstep(R.cutA, R.cutA + blendLen(R.cutA), s);
      const ps = madd(A.p, dA, s); ps[1] = f.p[1];
      p = [lerp(ps[0], p[0], t), p[1], lerp(ps[2], p[2], t)];
      dxz = normXZ([lerp(dA[0], dxz[0], t), 0, lerp(dA[2], dxz[2], t)]);
    }
    if (straightB && s > L - R.cutB - blendLen(R.cutB)) {
      const t = smoothstep(L - R.cutB, L - R.cutB - blendLen(R.cutB), s);
      const ps = madd(B.p, dB, -(L - s)); ps[1] = f.p[1];
      p = [lerp(ps[0], p[0], t), p[1], lerp(ps[2], p[2], t)];
      dxz = normXZ([lerp(dB[0], dxz[0], t), 0, lerp(dB[2], dxz[2], t)]);
    }
    const r = rightXZ(dxz);
    return { p, d: f.d, dxz, r, grade: f.grade, curvature: f.curvature, s };
  };
}

export { PROFILES };
