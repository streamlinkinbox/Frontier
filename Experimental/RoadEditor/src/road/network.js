// Road network data model: nodes (junction points), edges (road segments with a cross-section
// profile and curved centreline), plus non-graph layers (rail, subway, river, parks).
import { Centerline } from './centerline.js';
import { getProfile, layout } from './profiles.js';

export class RoadNetwork {
  constructor() {
    this.nodes = new Map();
    this.edges = new Map();
    this.adj = new Map();       // nodeId -> Set(edgeId)
    this.rails = new Map();     // id -> {id, kind:'rail', pts:[{x,z}], tracks, name}
    this.subways = new Map();   // id -> {id, pts:[{x,z}], depth, stations:[{s,name}], name, color}
    this.river = null;          // {pts:[[x,z]], width}
    this.parks = [];            // polygons [[x,z]...]
    this.districts = [];        // {poly, type, name}
    this.meta = { name: 'Untitled city', size: 4096, seed: 0, markingStyle: 'us' };
    this.seq = 1;
    this.cl = new Map();        // centreline cache
  }

  id(prefix) { return prefix + (this.seq++).toString(36); }

  addNode(n) {
    if (n.id && this.onTouch) this.onTouch('node', n.id);
    const node = { id: n.id || this.id('n'), x: n.x, z: n.z, y: n.y || 0, kind: n.kind || 'junction', control: n.control || 'auto', radius: n.radius ?? null, end: n.end || null, rbRadius: n.rbRadius ?? null };
    if (!n.id && this.onTouch) this.onTouch('node', node.id);
    this.nodes.set(node.id, node);
    if (!this.adj.has(node.id)) this.adj.set(node.id, new Set());
    return node;
  }
  addEdge(e) {
    const edge = { id: e.id || this.id('e'), a: e.a, b: e.b, profile: e.profile || 'local', custom: e.custom || null, pts: e.pts || [], name: e.name || '', features: e.features || [], flags: e.flags || {} };
    if (this.onTouch) this.onTouch('edge', edge.id);
    this.edges.set(edge.id, edge);
    this.adj.get(edge.a).add(edge.id);
    this.adj.get(edge.b).add(edge.id);
    return edge;
  }
  removeEdge(id) {
    const e = this.edges.get(id); if (!e) return;
    if (this.onTouch) this.onTouch('edge', id);
    this.adj.get(e.a)?.delete(id); this.adj.get(e.b)?.delete(id);
    this.edges.delete(id); this.cl.delete(id);
  }
  removeNode(id) {
    for (const eid of [...(this.adj.get(id) || [])]) this.removeEdge(eid);
    if (this.onTouch && this.nodes.has(id)) this.onTouch('node', id);
    this.nodes.delete(id); this.adj.delete(id);
  }
  touchNode(id) { if (this.onTouch) this.onTouch('node', id); }
  touchEdge(id) { if (this.onTouch) this.onTouch('edge', id); this.cl.delete(id); }
  nodeEdges(id) { return [...(this.adj.get(id) || [])].map(e => this.edges.get(e)).filter(Boolean); }
  other(edge, nodeId) { return edge.a === nodeId ? edge.b : edge.a; }

  // Raw outgoing direction from node along the edge's control polygon.
  rawDir(edge, nodeId) {
    const pts = this.controlPoints(edge);
    const p0 = nodeId === edge.a ? pts[0] : pts[pts.length - 1];
    const p1 = nodeId === edge.a ? pts[1] : pts[pts.length - 2];
    const l = Math.hypot(p1.x - p0.x, p1.z - p0.z) || 1;
    return [(p1.x - p0.x) / l, (p1.z - p0.z) / l];
  }
  controlPoints(edge) {
    const A = this.nodes.get(edge.a), B = this.nodes.get(edge.b);
    return [{ x: A.x, z: A.z, y: A.y }, ...edge.pts, { x: B.x, z: B.z, y: B.y }];
  }
  profileKey(edge) { return edge.custom ? JSON.stringify(edge.custom.lanes) : edge.profile; }

  // Degree-2 node whose two edges share a profile and run nearly straight: geometry flows through.
  isContinuation(nodeId) {
    const es = this.nodeEdges(nodeId);
    const node = this.nodes.get(nodeId);
    if (es.length !== 2 || node.kind === 'roundabout') return false;
    if (this.profileKey(es[0]) !== this.profileKey(es[1])) return false;
    const [ax, az] = this.rawDir(es[0], nodeId), [bx, bz] = this.rawDir(es[1], nodeId);
    if (ax * bx + az * bz > -0.94) return false;
    // lane directions must chain (a one-way can't flip direction at the node)
    const flipA = es[0].a === nodeId, flipB = es[1].a === nodeId;
    if (layout(getProfile(es[0])).oneWay && flipA === flipB) return false;
    return true;
  }
  continuationAxis(edge, nodeId) {
    const es = this.nodeEdges(nodeId), o = es[0].id === edge.id ? es[1] : es[0];
    const [ax, az] = this.rawDir(edge, nodeId), [bx, bz] = this.rawDir(o, nodeId);
    const l = Math.hypot(ax - bx, az - bz) || 1;
    return [(ax - bx) / l, (az - bz) / l];
  }

  centerline(edge) {
    let c = this.cl.get(edge.id);
    if (c) return c;
    const opts = {};
    if (this.isContinuation(edge.a)) opts.t0 = this.continuationAxis(edge, edge.a);
    if (this.isContinuation(edge.b)) { const [x, z] = this.continuationAxis(edge, edge.b); opts.t1 = [-x, -z]; }
    c = new Centerline(this.controlPoints(edge), opts);
    this.cl.set(edge.id, c);
    return c;
  }
  invalidate(edgeIds) { for (const id of edgeIds) this.cl.delete(id); }

  stats() {
    let km = 0, lanesKm = 0;
    for (const e of this.edges.values()) {
      const c = this.centerline(e); km += c.length / 1000;
      lanesKm += c.length / 1000 * getProfile(e).lanes.filter(l => l.type === 'drive').length;
    }
    let railKm = 0; for (const r of this.rails.values()) railKm += polyLen(r.pts) / 1000;
    let subKm = 0; for (const r of this.subways.values()) subKm += polyLen(r.pts) / 1000;
    return { nodes: this.nodes.size, edges: this.edges.size, km, lanesKm, railKm, subKm };
  }

  toJSON() {
    return {
      format: 'frontier-roads', version: 1, meta: this.meta, seq: this.seq,
      nodes: [...this.nodes.values()], edges: [...this.edges.values()],
      rails: [...this.rails.values()], subways: [...this.subways.values()],
      river: this.river, parks: this.parks, districts: this.districts, trees: this.trees || [],
      districtRaster: this.districtRaster ? { ...this.districtRaster, data: b64(this.districtRaster.data) } : null,
    };
  }
  static fromJSON(j) {
    const n = new RoadNetwork();
    n.meta = { ...n.meta, ...j.meta }; n.seq = j.seq || 1;
    for (const x of j.nodes) n.addNode(x);
    for (const e of j.edges) if (n.nodes.has(e.a) && n.nodes.has(e.b)) n.addEdge(e);
    for (const r of j.rails || []) n.rails.set(r.id, r);
    for (const r of j.subways || []) n.subways.set(r.id, r);
    n.river = j.river || null; n.parks = j.parks || []; n.districts = j.districts || [];
    n.trees = j.trees || [];
    if (j.districtRaster) n.districtRaster = { ...j.districtRaster, data: unb64(j.districtRaster.data) };
    return n;
  }
}

function b64(u8) { let s = ''; for (let i = 0; i < u8.length; i += 8192) s += String.fromCharCode.apply(null, u8.subarray(i, i + 8192)); return typeof btoa === 'function' ? btoa(s) : Buffer.from(s, 'binary').toString('base64'); }
function unb64(str) { const s = typeof atob === 'function' ? atob(str) : Buffer.from(str, 'base64').toString('binary'); const u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i); return u; }
function polyLen(pts) { let l = 0; for (let i = 1; i < pts.length; i++) l += Math.hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z); return l; }

// Uniform-grid spatial index over edge centreline samples and nodes for picking/snapping.
export class SpatialIndex {
  constructor(net, cell = 40) { this.net = net; this.cell = cell; this.map = new Map(); }
  key(ix, iz) { return ix * 73856093 ^ iz * 19349663; }
  insertEdge(edge) {
    const c = this.net.centerline(edge), seen = new Set();
    for (let i = 0; i < c.X.length; i += 2) {
      const ix = Math.floor(c.X[i] / this.cell), iz = Math.floor(c.Z[i] / this.cell), k = this.key(ix, iz);
      if (seen.has(k)) continue; seen.add(k);
      let b = this.map.get(k); if (!b) this.map.set(k, b = new Set()); b.add(edge.id);
    }
  }
  removeEdge(id) { for (const b of this.map.values()) b.delete(id); }
  rebuild() { this.map.clear(); for (const e of this.net.edges.values()) this.insertEdge(e); }
  query(x, z, r) {
    const out = new Set(), c = this.cell;
    for (let ix = Math.floor((x - r) / c); ix <= Math.floor((x + r) / c); ix++)
      for (let iz = Math.floor((z - r) / c); iz <= Math.floor((z + r) / c); iz++) {
        const b = this.map.get(this.key(ix, iz)); if (b) for (const id of b) out.add(id);
      }
    return [...out].map(id => this.net.edges.get(id)).filter(Boolean);
  }
  // Nearest edge to a world point; returns {edge, s, d, lateral}
  nearestEdge(x, z, r = 30, y = null) {
    let best = null;
    for (const e of this.query(x, z, r + 30)) {
      const c = this.net.centerline(e), q = c.nearest(x, z);
      const half = layout(getProfile(e)).total / 2;
      let d = q.d - half;
      if (y !== null) { const f = c.frameAt(q.s); d += Math.abs(f.y - y) * 2; }
      if (q.d < r + half && (!best || d < best.d)) best = { edge: e, s: q.s, d, dist: q.d };
    }
    return best;
  }
  nearestNode(x, z, r = 12) {
    let best = null;
    for (const n of this.net.nodes.values()) {
      const d = Math.hypot(n.x - x, n.z - z);
      if (d < r && (!best || d < best.d)) best = { node: n, d };
    }
    return best;
  }
}
