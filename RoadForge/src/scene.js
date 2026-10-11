/* RoadForge — scene orchestration: project → network → meshes → renderer items; picking, gizmo, live traffic, export.
 *
 * Draw items are keyed "<owner>|<material>" so that editing one road only re-uploads that road's buffers. Furniture and
 * vehicles are instanced per part. The ground is a single plane drawn only where the stencil has not been marked by the
 * cutting / portal "hole" meshes.
 */
(function (RF) {
  'use strict';
  const V = RF.V, S = RF.spline, MB = RF.MeshBuilder, M4 = RF.M4;
  const GROUND_SIZE = 12000; // square half-size; diagonal (17 km) stays inside the 24 km far plane

  /* ---------------- project helpers ---------------- */
  let idCounter = 1;
  const uid = (prefix) => `${prefix}${(idCounter++).toString(36)}${Date.now().toString(36).slice(-3)}`;

  function defaultLayers() {
    return {
      surface: { asphaltColor: '#161617', wear: 0.55, cracks: 0.25, patches: 0.3, wetness: 0.0, aggregateScale: 1, pavingPattern: 1, pavingScale: 1, pavingColor: '#9c9384', curbMaterial: 'concrete', medianSurface: 'paving', crossfall: 0.02, markingStd: 'eu', paintWear: 0.35, rust: 0.15, seed: 1, wearEnabled: true, cracksEnabled: true, patchesEnabled: true, wetEnabled: true },
      furniture: { streetlights: true, lightSpacing: 0, signals: true, signs: true, railings: true, cornerBollards: false, guardrails: true, billboards: true, parkedCars: true },
      traffic: { enabled: true, count: 18, parked: true, trains: true },
      environment: { timeOfDay: 14.5, azimuth: 215, fog: 0.00012, exposure: 1.0, cloud: 0.2 },
    };
  }

  function emptyProject(name = 'Untitled network') {
    return { name, version: 1, settings: { handedness: 'right', autoBlocks: { enabled: true, surface: 'mixed', depth: 120 } }, nodes: {}, roads: {}, rails: {}, blocks: {}, blockOverrides: {}, layers: defaultLayers() };
  }

  /** Split roads where their centrelines cross at (near) grade and insert shared nodes. Mutates & returns the project. */
  function autoIntersect(project, maxDy = 4.5) {
    let changed = true, guard = 0;
    while (changed && guard++ < 40) {
      changed = false;
      const roads = Object.values(project.roads);
      const geo = new Map();
      for (const r of roads) {
        if (!project.nodes[r.a] || !project.nodes[r.b]) continue;
        const cps = [project.nodes[r.a].p, ...(r.pts || []), project.nodes[r.b].p];
        let dense = S.samplePolyline(cps, null, null, 0.5);
        if (dense.length === 2) dense = [dense[0], V.lerp(dense[0], dense[1], 0.5), dense[1]];
        geo.set(r.id, { dense, cum: S.cumulativeXZ(dense) });
      }
      outer: for (let i = 0; i < roads.length; i++) for (let j = i + 1; j < roads.length; j++) {
        const A = roads[i], B = roads[j]; const ga = geo.get(A.id), gb = geo.get(B.id); if (!ga || !gb) continue;
        if (A.a === B.a || A.a === B.b || A.b === B.a || A.b === B.b) {
          // shared node: skip hits near that node only
        }
        const hits = S.polyIntersectAll(ga.dense, ga.cum, gb.dense, gb.cum);
        for (const h of hits) {
          const La = ga.cum[ga.cum.length - 1], Lb = gb.cum[gb.cum.length - 1];
          if (h.sA < 6 || h.sA > La - 6 || h.sB < 6 || h.sB > Lb - 6) continue;
          const ya = S.evalAt(ga.dense, ga.cum, h.sA).p[1], yb = S.evalAt(gb.dense, gb.cum, h.sB).p[1];
          if (Math.abs(ya - yb) > maxDy) continue;
          const node = { id: uid('n'), p: [h.p[0], (ya + yb) / 2, h.p[2]], control: 'auto', name: 'Junction' };
          project.nodes[node.id] = node;
          splitRoad(project, A, h.sA, node.id, ga);
          splitRoad(project, B, h.sB, node.id, gb);
          changed = true; break outer;
        }
      }
    }
    return project;
  }
  function splitRoad(project, road, s, nodeId, g) {
    const stationOf = (p) => S.closestOnPolyline(g.dense, g.cum, p).s;
    const pts = road.pts || [];
    const r1 = { ...road, id: uid('r'), name: road.name, b: nodeId, pts: pts.filter((p) => stationOf(p) < s - 3).map(V.clone), features: shiftFeatures(road.features, 0, s), props: { ...(road.props || {}), billboards: (road.props?.billboards || []).filter((b) => b.s < s) } };
    const r2 = { ...road, id: uid('r'), name: road.name, a: nodeId, pts: pts.filter((p) => stationOf(p) > s + 3).map(V.clone), features: shiftFeatures(road.features, s, Infinity), props: { ...(road.props || {}), billboards: (road.props?.billboards || []).filter((b) => b.s >= s).map((b) => ({ ...b, s: b.s - s })) } };
    delete project.roads[road.id];
    project.roads[r1.id] = r1; project.roads[r2.id] = r2;
  }
  function shiftFeatures(f, s0, s1) {
    if (!f) return undefined;
    const o = {};
    const inR = (s) => s >= s0 && s < s1;
    if (f.speedBumps) o.speedBumps = f.speedBumps.filter((x) => inR(x.s)).map((x) => ({ ...x, s: x.s - s0 }));
    if (f.potholes) o.potholes = f.potholes.filter((x) => inR(x.s)).map((x) => ({ ...x, s: x.s - s0 }));
    if (f.construction) o.construction = f.construction.filter((x) => inR((x.s0 + x.s1) / 2)).map((x) => ({ ...x, s0: Math.max(0, x.s0 - s0), s1: x.s1 - s0 }));
    if (f.railCrossings) o.railCrossings = f.railCrossings.filter((x) => inR(x.s)).map((x) => ({ ...x, s: x.s - s0 }));
    return o;
  }

  /* ---------------- polygon fill (city blocks) ---------------- */
  function earcut(poly) {
    // poly: [[x,z],...] CCW or CW; returns triangles as index triples
    const n = poly.length; if (n < 3) return [];
    const idx = []; for (let i = 0; i < n; i++) idx.push(i);
    const area = poly.reduce((a, p, i) => { const q = poly[(i + 1) % n]; return a + p[0] * q[1] - q[0] * p[1]; }, 0);
    if (area < 0) idx.reverse();
    const tris = []; let guard = 0;
    const cross = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    const inside = (p, a, b, c) => cross(a, b, p) >= -1e-9 && cross(b, c, p) >= -1e-9 && cross(c, a, p) >= -1e-9;
    while (idx.length > 3 && guard++ < 5000) {
      let cut = false;
      for (let i = 0; i < idx.length; i++) {
        const i0 = idx[(i + idx.length - 1) % idx.length], i1 = idx[i], i2 = idx[(i + 1) % idx.length];
        const a = poly[i0], b = poly[i1], c = poly[i2];
        if (cross(a, b, c) <= 1e-9) continue;
        let ok = true;
        for (const k of idx) { if (k === i0 || k === i1 || k === i2) continue; if (inside(poly[k], a, b, c)) { ok = false; break; } }
        if (!ok) continue;
        tris.push([i0, i1, i2]); idx.splice(i, 1); cut = true; break;
      }
      if (!cut) break;
    }
    if (idx.length === 3) tris.push([idx[0], idx[1], idx[2]]);
    return tris;
  }
  /** Inset polygon by d (positive = inward for CCW). */
  function insetPolygon(poly, d) {
    const n = poly.length; const out = [];
    const area = poly.reduce((a, p, i) => { const q = poly[(i + 1) % n]; return a + p[0] * q[1] - q[0] * p[1]; }, 0);
    const sgn = area > 0 ? 1 : -1;
    for (let i = 0; i < n; i++) {
      const p0 = poly[(i + n - 1) % n], p1 = poly[i], p2 = poly[(i + 1) % n];
      const d1 = norm2(sub2(p1, p0)), d2 = norm2(sub2(p2, p1));
      const n1 = [-d1[1] * sgn, d1[0] * sgn], n2 = [-d2[1] * sgn, d2[0] * sgn];
      const bis = norm2([n1[0] + n2[0], n1[1] + n2[1]]); const cosA = bis[0] * n1[0] + bis[1] * n1[1];
      const L = Math.min(d / Math.max(cosA, 0.3), d * 3);
      out.push([p1[0] + bis[0] * L, p1[1] + bis[1] * L]);
    }
    return out;
  }
  const sub2 = (a, b) => [a[0] - b[0], a[1] - b[1]]; const norm2 = (a) => { const l = Math.hypot(a[0], a[1]) || 1; return [a[0] / l, a[1] / l]; };

  function buildBlock(block, layers, out) {
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    const poly = block.poly; if (!poly || poly.length < 3) return;
    const y = block.y ?? (RF.structures ? RF.structures.GROUND_Y : 0);
    const curbH = block.edgeH ?? 0.125, cw = 0.15, sw = block.sidewalk ?? 3.0;
    const curb = get(layers.curbMaterial || 'curb'), pav = get('paving'), fill = get(block.surface === 'grass' ? 'grass' : block.surface === 'gravel' ? 'ballast' : block.surface === 'concrete' ? 'concrete' : 'paving_alt');
    const area = poly.reduce((a, p, i) => { const q = poly[(i + 1) % poly.length]; return a + p[0] * q[1] - q[0] * p[1]; }, 0);
    const ring = area > 0 ? poly : [...poly].reverse(); // CCW in (x,z)
    const r0 = ring, r1 = insetPolygon(ring, cw), r2 = insetPolygon(ring, cw + sw);
    const n = ring.length;
    // kerb: outer face + top ring
    const rowsCurb = [[], [], [], []];
    for (let i = 0; i <= n; i++) { const a = r0[i % n], b = r1[i % n]; rowsCurb[0].push(curb.vertex([a[0], y, a[1]], [i, 0])); rowsCurb[1].push(curb.vertex([a[0] + (b[0] - a[0]) * 0.08, y + curbH - 0.015, a[1] + (b[1] - a[1]) * 0.08], [i, curbH])); rowsCurb[2].push(curb.vertex([a[0] + (b[0] - a[0]) * 0.2, y + curbH, a[1] + (b[1] - a[1]) * 0.2], [i, curbH + 0.02])); rowsCurb[3].push(curb.vertex([b[0], y + curbH, b[1]], [i, curbH + cw])); }
    curb.grid(rowsCurb, false);
    const rowsPav = [[], []];
    if (sw > 0) for (let i = 0; i <= n; i++) { const a = r1[i % n], b = r2[i % n]; rowsPav[0].push(pav.vertex([a[0], y + curbH, a[1]], [a[0], a[1]], [0, 0, 60, 0])); rowsPav[1].push(pav.vertex([b[0], y + curbH + 0.02, b[1]], [b[0], b[1]], [0, 0, 60, 0])); }
    if (sw > 0) pav.grid(rowsPav, false);
    // interior fill: ear-clipped triangles (quads preferred elsewhere; interior polygons are arbitrary)
    const tris = earcut(r2);
    const vi = r2.map((p) => fill.vertex([p[0], y + curbH + 0.02 + (block.surface === 'grass' ? 0.03 : 0), p[1]], [p[0], p[1]]));
    const upFacing = (a, b, c) => ((r2[b][0] - r2[a][0]) * (r2[c][1] - r2[a][1]) - (r2[b][1] - r2[a][1]) * (r2[c][0] - r2[a][0])) > 0;
    for (const [a, b, c] of tris) { if (upFacing(a, b, c)) fill.tri(vi[a], vi[c], vi[b]); else fill.tri(vi[a], vi[b], vi[c]); }
    return out;
  }

  /* ---------------- Scene ---------------- */
  class Scene {
    constructor(renderer) {
      this.R = renderer;
      this.project = emptyProject();
      this.net = null; this.meshes = new Map(); // owner → {material → mesh}
      this.pickables = []; this.lights = []; this.placed = new Map(); this.signals = [];
      this.traffic = null; this.rails = []; this.trains = [];
      this.specs = RF.materials.specs(this.project.layers.surface);
      this.selection = new Set();
      this.xray = false; this.showTraffic = true;
      this.stats = { quads: 0, tris: 0, roads: 0, nodes: 0 };
      this.diagnostics = [];
      this.groundBuilt = false;
    }
    setProject(p) { this.project = p; if (!p.layers) p.layers = defaultLayers(); if (!p.rails) p.rails = {}; if (!p.blocks) p.blocks = {}; this.rebuild(); }

    spec(material, selected = false) {
      const base = this.specs[material] || this.specs.concrete;
      return selected ? { ...base, selected: true } : base;
    }

    /** Full rebuild of everything. */
    rebuild() {
      const R = this.R, P = this.project, L = P.layers;
      this.specs = RF.materials.specs(L.surface);
      const layers = { ...L.surface, curbMaterial: L.surface.curbMaterial === 'granite' ? 'granite' : 'curb', furniture: L.furniture, traffic: L.traffic };
      this.layers = layers;
      const net = RF.network.solve(P, { layers });
      const railT = new Map(); for (const rail of Object.values(P.rails || {})) { const T = RF.rail.solve(rail); if (T) railT.set(rail.id, T); }
      RF.structures.classify(net, { corridors: [...railT.values()].map((T) => ({ dense: T.dense, cum: T.cum, half: 4.5 })) });
      // level crossings: tell the roads where the panels are so markings stop short and stop lines are painted
      for (const T of railT.values()) for (const x of RF.rail.crossings(T, net)) {
        if (!x.level) continue;
        const sinA = Math.max(0.35, Math.abs(x.dRail[0] * x.dRoad[2] - x.dRail[2] * x.dRoad[0]));
        (x.road.railCrossings = x.road.railCrossings || []).push({ s: x.sRoad, half: (RF.rail.GAUGE / 2 + 1.0) / sinA + 0.5 });
      }
      this.net = net;
      // --- dispose old
      for (const owner of this.meshes.keys()) R.removeByPrefix(owner + '|');
      R.removeByPrefix('prop|'); R.removeByPrefix('veh|'); R.removeByPrefix('train|'); R.removeByPrefix('rail|'); R.removeByPrefix('block|'); R.removeByPrefix('holes|');
      this.meshes.clear(); this.pickables = []; this.lights = []; this.placed = new Map(); this.signals = []; this.holes = [];
      const outByOwner = new Map();
      const get = (owner) => { let o = outByOwner.get(owner); if (!o) { o = {}; outByOwner.set(owner, o); } return o; };
      // --- roads & structures
      for (const Rd of net.roads.values()) {
        const out = get('road:' + Rd.id);
        RF.roadmesh.buildRoad(Rd, net, layers, out);
        this.holes.push(...RF.structures.build(Rd, net, layers, out));
      }
      // --- junctions
      for (const rec of net.nodes.values()) {
        const out = get('node:' + rec.id);
        if (rec.kind === 'junction' || rec.kind === 'transition') RF.roadmesh.buildJunction(rec, net, layers, out);
        else if (rec.kind === 'end') RF.roadmesh.buildEndCap(rec, net, layers, out);
      }
      // --- rails
      this.rails = []; this.trains = [];
      const ctx = { net, project: P, layers, placed: this.placed, lights: this.lights, signals: this.signals, pick: this.pickables, holes: this.holes, out: null };
      for (const rail of Object.values(P.rails || {})) {
        const T = railT.get(rail.id); if (!T) continue;
        const out = get('rail:' + rail.id);
        ctx.out = out;
        RF.rail.build(T, net, layers, out, ctx);
        this.rails.push(T);
        if (L.traffic.trains !== false) this.trains.push({ T, trains: RF.rail.makeTrains(T, rail.trains ?? 1) });
      }
      // --- blocks: explicit polygons plus the automatic blocks traced from the network's faces
      this.autoBlocks = {};
      const ab = P.settings?.autoBlocks || {};
      if (ab.enabled ?? (Object.keys(P.blocks || {}).length === 0)) for (const b of RF.blocks.autoBlocks(net, P, { surface: ab.surface || 'mixed', depth: ab.depth || 120 })) this.autoBlocks[b.id] = b;
      for (const b of [...Object.values(P.blocks || {}), ...Object.values(this.autoBlocks)]) buildBlock(b, layers, get('block:' + b.id));
      // --- furniture (W-beams go into the owning road's out map)
      ctx.out = null;
      ctx.outFor = (owner) => get(owner);
      // props.buildWBeam writes to ctx.out → give it a shared furniture owner
      ctx.out = get('prop:beams');
      RF.props.place(ctx);
      // --- upload
      for (const [owner, out] of outByOwner) {
        const mats = {};
        for (const [mat, b] of Object.entries(out)) {
          if (!b.indexCount && !(b.indices && b.indices.length) && !b.faces) { /* fallthrough */ }
          b.weld(); const mesh = b.build(); if (!mesh.indices.length) continue;
          mats[mat] = mesh;
          const spec = this.spec(mat, this.isSelectedOwner(owner));
          R.setItem({ id: owner + '|' + mat, mesh, material: spec, pipeline: spec.pipeline || 'main', castShadow: spec.pipeline !== 'decal' });
        }
        this.meshes.set(owner, mats);
      }
      this.uploadPlaced();
      this.buildGround();
      // --- traffic
      this.traffic = L.traffic.enabled !== false ? new RF.traffic.Traffic(net, layers, { count: L.traffic.count ?? 18, seed: 11 }) : null;
      this.diagnostics = net.diagnostics;
      this.updateStats();
      this.buildPickables();
    }
    /** Explicit or automatic block record by id. */
    blockById(id) { return (this.project.blocks || {})[id] || (this.autoBlocks || {})[id] || null; }
    isSelectedOwner(owner) { for (const id of this.selection) if (owner === 'road:' + id || owner === 'node:' + id || owner === 'rail:' + id || owner === 'block:' + id) return true; return false; }

    uploadPlaced() {
      const R = this.R;
      for (const [key, rec] of this.placed) {
        if (key.startsWith('veh:') && !key.includes(':static')) { /* parked cars share the same key family as traffic; keep */ }
        const inst = new Float32Array(rec.instances.flat());
        rec.parts.forEach((part, pi) => {
          const spec = { ...this.spec(part.material) }; if (part.tile != null) spec.tile = part.tile;
          let data = inst;
          if (key.startsWith('signal:') && /^signal_(red|amber|green)$/.test(part.material)) { data = new Float32Array(inst); for (let i = 0; i < data.length; i += 24) { data[i + 20] = part.material === 'signal_red' ? 1 : 0; data[i + 23] = 1; } }
          R.setItem({ id: `prop|${key}|${pi}`, mesh: part.mesh, material: spec, instances: data, pipeline: spec.pipeline || (part.material === 'glass' ? 'glass' : 'main'), castShadow: part.material !== 'glass' });
        });
        rec.baseInstances = inst;
      }
    }

    buildGround() {
      const R = this.R;
      if (!this.groundBuilt) {
        const g = new MB('ground'); const Sz = GROUND_SIZE, n = 24; const rows = [];
        for (let i = 0; i <= n; i++) { const row = []; for (let j = 0; j <= n; j++) { const u = (j / n) * 2 - 1, v = (i / n) * 2 - 1; const w = (x) => Math.sign(x) * Math.pow(Math.abs(x), 3) * Sz; row.push(g.vertex([w(u), RF.structures.GROUND_Y, w(v)], [w(u), w(v)])); } rows.push(row); }
        g.grid(rows, false);
        const spec = this.spec('ground');
        R.setItem({ id: 'ground', mesh: g.build(), material: spec, pipeline: 'ground', castShadow: false });
        this.groundBuilt = true;
      }
      R.removeByPrefix('holes|');
      if (this.holes.length) {
        const h = new MB('hole');
        for (const poly of this.holes) {
          // polygons are strips (right edge then reversed left edge) → quads between the halves
          const n = poly.length / 2; if (n < 2 || n !== Math.floor(n)) { const tris = earcut(poly); const vi = poly.map((p) => h.vertex([p[0], RF.structures.GROUND_Y + 0.001, p[1]], [0, 0])); for (const t of tris) h.tri(vi[t[0]], vi[t[1]], vi[t[2]]); continue; }
          const right = poly.slice(0, n), left = poly.slice(n).reverse();
          const ra = right.map((p) => h.vertex([p[0], RF.structures.GROUND_Y + 0.001, p[1]], [0, 0])), la = left.map((p) => h.vertex([p[0], RF.structures.GROUND_Y + 0.001, p[1]], [0, 0]));
          h.strip(la, ra, true);
        }
        const mesh = h.build(); if (mesh.indices.length) R.setItem({ id: 'holes|0', mesh, material: this.spec('hole'), pipeline: 'hole', castShadow: false });
      }
    }

    updateStats() {
      const st = this.R.stats();
      this.stats = { quads: st.quads, tris: st.tris - st.quads * 2, draws: st.draws, roads: this.net ? this.net.roads.size : 0, nodes: this.net ? this.net.nodes.size : 0, verts: st.verts };
    }

    /* ---------------- selection & materials ---------------- */
    setSelection(ids) {
      const prev = new Set(this.selection); this.selection = new Set(ids);
      const touched = new Set([...prev, ...this.selection]);
      for (const id of touched) for (const prefix of ['road:', 'node:', 'rail:', 'block:']) {
        const owner = prefix + id; const mats = this.meshes.get(owner); if (!mats) continue;
        for (const mat of Object.keys(mats)) this.R.updateMaterial(owner + '|' + mat, this.spec(mat, this.selection.has(id)));
      }
    }
    /** Surface-layer tweak that needs no remesh: refresh material slots only. */
    refreshMaterials() {
      this.specs = RF.materials.specs(this.project.layers.surface);
      for (const [owner, mats] of this.meshes) for (const mat of Object.keys(mats)) this.R.updateMaterial(owner + '|' + mat, this.spec(mat, this.isSelectedOwner(owner)));
      for (const [key, rec] of this.placed) rec.parts.forEach((part, pi) => { const spec = { ...this.spec(part.material) }; if (part.tile != null) spec.tile = part.tile; this.R.updateMaterial(`prop|${key}|${pi}`, spec); });
      this.R.updateMaterial('ground', this.spec('ground'));
    }

    /* ---------------- picking ---------------- */
    buildPickables() {
      const net = this.net; this.pickMeshes = [];
      for (const [owner, mats] of this.meshes) {
        const ci = owner.indexOf(':'); const type = owner.slice(0, ci), id = owner.slice(ci + 1);
        for (const [mat, mesh] of Object.entries(mats)) {
          if (type === 'road' && !/asphalt|concrete_road|paving|grass|curb/.test(mat)) continue;
          if (type === 'node' && !/asphalt|concrete_road|paving/.test(mat)) continue;
          if (type === 'rail' && !/ballast|concrete|rail/.test(mat)) continue;
          if (type === 'block' && !/paving|grass|ballast|concrete/.test(mat)) continue;
          this.pickMeshes.push({ type, id, mesh });
        }
      }
      this.nodeHandles = [...net.nodes.values()].map((rec) => ({ id: rec.id, p: rec.node.p, r: rec.kind === 'junction' ? Math.max(2.5, Math.min(6, (rec.arms[0]?.hwToward || 3) * 0.6)) : 2.0 }));
    }
    pick(ray, opts = {}) {
      let best = null;
      const consider = (hit) => { if (hit && (!best || hit.t < best.t)) best = hit; };
      // node handles (spheres) first — they must stay easy to grab
      for (const h of this.nodeHandles) { const t = raySphere(ray, h.p, h.r * (opts.handleScale || 1)); if (t != null) consider({ t, type: 'node', id: h.id, point: V.madd(ray.o, ray.d, t) }); }
      // road control points
      if (opts.controlPoints) for (const cp of opts.controlPoints) { const t = raySphere(ray, cp.p, cp.r); if (t != null) consider({ t: t - 0.5, type: 'cp', id: cp.road, index: cp.index, point: V.madd(ray.o, ray.d, t) }); }
      if (best && best.type !== 'cp') { /* still allow nearer mesh hits below */ }
      for (const pm of this.pickMeshes) {
        const b = pm.mesh.bounds; if (b && !rayAABB(ray, b)) continue;
        const t = rayMesh(ray, pm.mesh); if (t != null) consider({ t, type: pm.type, id: pm.id, point: V.madd(ray.o, ray.d, t) });
      }
      for (const pk of this.pickables) { const t = raySphere(ray, pk.p, pk.r); if (t != null) consider({ t, type: pk.type, id: pk.owner, index: pk.index, point: V.madd(ray.o, ray.d, t) }); }
      if (!best) { const t = rayPlane(ray, RF.structures.GROUND_Y); if (t != null) best = { t, type: 'ground', point: V.madd(ray.o, ray.d, t) }; }
      return best;
    }
    groundPoint(ray, y = 0) { const t = rayPlane(ray, y); return t == null ? null : V.madd(ray.o, ray.d, t); }

    /* ---------------- gizmo (overlay) ---------------- */
    setGizmo(pos, opts = {}) {
      const R = this.R; R.removeByPrefix('gizmo|');
      if (!pos) return;
      const scale = opts.scale || 1;
      const L = 5.2 * scale, headL = 1.7 * scale, headR = 0.5 * scale, shaftR = 0.17 * scale;
      // axis, colour, name, lateral basis (u, v) so that fromBasis(u, d, v) maps local +Y onto the axis
      const axes = [
        [[1, 0, 0], [0.96, 0.32, 0.3], 'x', [0, 0, -1], [0, 1, 0]],
        [[0, 1, 0], [0.5, 0.95, 0.4], 'y', [1, 0, 0], [0, 0, 1]],
        [[0, 0, 1], [0.35, 0.6, 1.0], 'z', [1, 0, 0], [0, -1, 0]],
      ];
      for (const [d, col, name, u, v] of axes) {
        const b = new MB('gizmo');
        const T = M4.fromBasis(u, d, v, pos);
        // shaft (closed tube) + cone head, both built along local +Y then mapped onto the axis
        RF.prim.lathe(b, [[0.001, 0.9 * scale], [shaftR, 0.9 * scale], [shaftR, L]], 10, { transform: T });
        RF.prim.lathe(b, [[0.001, L], [headR, L], [0.001, L + headL]], 16, { transform: T });
        const mesh = b.build();
        const hl = opts.active === name;
        R.setItem({ id: 'gizmo|' + name, mesh, material: { type: 'flat', color: hl ? [1, 0.95, 0.4] : col, pipeline: 'overlay' }, pipeline: 'overlay', castShadow: false });
      }
      // centre disc (XZ plane drag) — flat disc with a thin ring
      const b = new MB('gizmo');
      RF.prim.lathe(b, [[1.3 * scale, 0.02 * scale], [0.001, 0.02 * scale]], 24, { transform: M4.translation(pos[0], pos[1], pos[2]) });
      RF.prim.lathe(b, [[1.3 * scale, 0], [1.3 * scale, 0.12 * scale], [1.55 * scale, 0.12 * scale], [1.55 * scale, 0]], 24, { transform: M4.translation(pos[0], pos[1], pos[2]) });
      R.setItem({ id: 'gizmo|c', mesh: b.build(), material: { type: 'flat', color: opts.active === 'c' ? [1, 0.95, 0.4] : [0.88, 0.88, 0.88], alpha: 0.6, pipeline: 'overlay' }, pipeline: 'overlay', castShadow: false });
      // height reference: a thin plumb line from the handle down to (or up to) ground level with a ground marker
      if (Math.abs(pos[1]) > 0.05) {
        const g = new MB('gizmo'); const y0 = Math.min(pos[1], 0), y1 = Math.max(pos[1], 0);
        RF.prim.lathe(g, [[0.05 * scale, y0], [0.05 * scale, y1]], 6, { transform: M4.translation(pos[0], 0, pos[2]) });
        RF.prim.lathe(g, [[1.0 * scale, 0], [1.0 * scale, 0.08 * scale], [1.15 * scale, 0.08 * scale], [1.15 * scale, 0]], 24, { transform: M4.translation(pos[0], 0, pos[2]) });
        R.setItem({ id: 'gizmo|plumb', mesh: g.build(), material: { type: 'flat', color: [0.5, 0.95, 0.4], alpha: 0.5, pipeline: 'overlay' }, pipeline: 'overlay', castShadow: false });
      }
    }
    /** Hit-test gizmo handles. Returns 'x'|'y'|'z'|'c'|null. */
    pickGizmo(ray, pos, scale = 1) {
      const axes = { x: [1, 0, 0], y: [0, 1, 0], z: [0, 0, 1] };
      let best = null;
      for (const [name, d] of Object.entries(axes)) {
        const res = rayCylinder(ray, pos, d, 7.2 * scale, 0.75 * scale); if (res != null && (!best || res < best.t)) best = { t: res, name };
      }
      const tc = rayPlane(ray, pos[1]); if (tc != null) { const p = V.madd(ray.o, ray.d, tc); if (V.distXZ(p, pos) < 1.6 * scale && (!best || tc < best.t)) best = { t: tc, name: 'c' }; }
      return best ? best.name : null;
    }
    /** Control-point handles for the selected road(s) (overlay spheres). */
    setControlPointHandles(roadIds, scale = 1) {
      const R = this.R; R.removeByPrefix('cph|'); const list = [];
      for (const id of roadIds) {
        const road = this.project.roads[id]; if (!road) continue;
        const b = new MB('gizmo'); let k = 0;
        for (const p of road.pts || []) { RF.prim.lathe(b, [[0.001, -0.6 * scale], [0.6 * scale, 0], [0.001, 0.6 * scale]], 10, { transform: M4.translation(p[0], p[1] + 0.3, p[2]) }); list.push({ road: id, index: k++, p: [p[0], p[1] + 0.3, p[2]], r: 0.9 * scale }); }
        const mesh = b.build(); if (mesh.indices.length) R.setItem({ id: 'cph|' + id, mesh, material: { type: 'flat', color: [1, 0.8, 0.3], pipeline: 'overlay' }, pipeline: 'overlay', castShadow: false });
        // centreline ribbon
        const Rd = this.net.roads.get(id);
        if (Rd) { const rb = new MB('gizmo'); const fr = Rd.samples.map((f) => ({ p: [f.p[0], f.p[1] + RF.network.surfaceOffset(Rd, f.s, 0, this.layers) + 0.05, f.p[2]], d: f.d, n: f.n, up: [0, 1, 0] })); if (fr.length > 1) { RF.prim.sweep(rb, fr, [[-0.08 * scale, 0], [0.08 * scale, 0]], {}); R.setItem({ id: 'cph|line|' + id, mesh: rb.build(), material: { type: 'flat', color: [1, 0.8, 0.3], alpha: 0.9, pipeline: 'overlay' }, pipeline: 'overlay', castShadow: false }); } }
      }
      return list;
    }
    clearHandles() { this.R.removeByPrefix('cph|'); this.R.removeByPrefix('gizmo|'); }

    /* ---------------- live update ---------------- */
    tick(dt, cam, env) {
      const R = this.R;
      if (this.traffic && this.showTraffic) {
        this.traffic.step(dt);
        const placed = new Map(); this.traffic.instances(placed);
        this.syncDynamic('veh|', placed);
        // signal lamps
        for (const [key, rec] of this.placed) {
          if (!key.startsWith('signal:')) continue;
          rec.parts.forEach((part, pi) => {
            const m = /^signal_(red|amber|green)$/.exec(part.material); if (!m) return;
            const data = new Float32Array(rec.baseInstances);
            for (let i = 0; i < data.length; i += 24) {
              const sig = this.signals.find((s) => s.key === key && s.index === i / 24);
              const ctrl = sig && this.traffic.graph.signals.get(sig.node);
              const st = ctrl ? RF.traffic.signalState(ctrl, sig.arm) : 'red';
              data[i + 20] = st === m[1] ? 1 : 0; data[i + 23] = 1;
            }
            R.updateInstances(`prop|${key}|${pi}`, data);
          });
        }
      }
      if (this.trains.length) {
        const placed = new Map();
        for (const tr of this.trains) RF.rail.updateTrains(tr.T, tr.trains, dt, placed);
        this.syncDynamic('train|', placed);
      }
      // night lights: nearest street lamps to the camera target
      if (env && env.night > 0.05 && this.lights.length) {
        const c = cam.target; const sorted = this.lights.map((l) => ({ l, d: V.distXZ(l.pos, c) })).sort((a, b) => a.d - b.d).slice(0, 48);
        R.setLights(sorted.map((x) => ({ pos: x.l.pos, radius: x.l.radius, color: x.l.color.map((v) => v * 2.2 * env.night) })));
      } else R.setLights([]);
    }
    syncDynamic(prefix, placed) {
      const R = this.R; const seen = new Set();
      for (const [key, rec] of placed) {
        rec.parts.forEach((part, pi) => {
          const id = `${prefix}${key}|${pi}`; seen.add(id);
          const data = new Float32Array(rec.instances.flat());
          if (R.items.has(id)) R.updateInstances(id, data);
          else { const spec = this.spec(part.material); R.setItem({ id, mesh: part.mesh, material: spec, instances: data, pipeline: part.material === 'glass' ? 'glass' : 'main', castShadow: part.material !== 'glass' }); }
        });
      }
      for (const id of [...R.items.keys()]) if (id.startsWith(prefix) && !seen.has(id)) R.removeItem(id);
    }

    /* ---------------- export ---------------- */
    exportOBJ() {
      const lines = ['# RoadForge export (quads preserved)', 'mtllib roadforge.mtl'];
      let base = 1;
      const emit = (name, mesh, mat) => {
        lines.push(`o ${name}`, `usemtl ${mat}`);
        const v = mesh.vertices; const n = mesh.vertexCount;
        for (let i = 0; i < n; i++) lines.push(`v ${v[i * 12].toFixed(4)} ${v[i * 12 + 1].toFixed(4)} ${v[i * 12 + 2].toFixed(4)}`);
        for (let i = 0; i < n; i++) lines.push(`vn ${v[i * 12 + 3].toFixed(3)} ${v[i * 12 + 4].toFixed(3)} ${v[i * 12 + 5].toFixed(3)}`);
        for (let i = 0; i < n; i++) lines.push(`vt ${v[i * 12 + 6].toFixed(4)} ${v[i * 12 + 7].toFixed(4)}`);
        const q = mesh.quads || [];
        for (let i = 0; i < q.length; i += 4) lines.push(`f ${[0, 1, 2, 3].map((k) => `${base + q[i + k]}/${base + q[i + k]}/${base + q[i + k]}`).join(' ')}`);
        const t = mesh.tris || [];
        for (let i = 0; i < t.length; i += 3) lines.push(`f ${[0, 1, 2].map((k) => `${base + t[i + k]}/${base + t[i + k]}/${base + t[i + k]}`).join(' ')}`);
        base += n;
      };
      for (const [owner, mats] of this.meshes) for (const [mat, mesh] of Object.entries(mats)) emit(owner.replace(':', '_') + '_' + mat, mesh, mat);
      // furniture: bake instances
      for (const [key, rec] of this.placed) rec.parts.forEach((part, pi) => {
        const m = part.mesh; const inst = rec.instances; const nv = m.vertexCount;
        const verts = new Float32Array(nv * 12 * inst.length); const quads = [], tris = [];
        inst.forEach((I, k) => { const M = new Float32Array(I.slice(0, 16)); for (let i = 0; i < nv; i++) { const p = M4.transformPoint(M, [m.vertices[i * 12], m.vertices[i * 12 + 1], m.vertices[i * 12 + 2]]); const nn = M4.transformDir(M, [m.vertices[i * 12 + 3], m.vertices[i * 12 + 4], m.vertices[i * 12 + 5]]); const o = (k * nv + i) * 12; verts[o] = p[0]; verts[o + 1] = p[1]; verts[o + 2] = p[2]; verts[o + 3] = nn[0]; verts[o + 4] = nn[1]; verts[o + 5] = nn[2]; verts[o + 6] = m.vertices[i * 12 + 6]; verts[o + 7] = m.vertices[i * 12 + 7]; } for (const qi of m.quads || []) quads.push(qi + k * nv); for (const ti of m.tris || []) tris.push(ti + k * nv); });
        emit(`prop_${key.replace(/[:|]/g, '_')}_${pi}`, { vertices: verts, vertexCount: nv * inst.length, quads, tris }, part.material);
      });
      return lines.join('\n');
    }
    exportMTL() {
      const out = ['# RoadForge materials'];
      for (const [name, s] of Object.entries(this.specs)) { const c = (s.color || [0.5, 0.5, 0.5]).map((v) => Math.pow(v, 1 / 2.2).toFixed(3)); out.push(`newmtl ${name}`, `Kd ${c.join(' ')}`, `Ns ${Math.round((1 - (s.roughness ?? 0.8)) * 200)}`, s.emissive ? `Ke ${c.join(' ')}` : ''); }
      return out.join('\n');
    }
  }

  /* ---------------- ray helpers ---------------- */
  function circleP(r, n) { const p = []; for (let i = 0; i < n; i++) { const a = (i / n) * Math.PI * 2; p.push([Math.cos(a) * r, Math.sin(a) * r]); } return p; }
  function raySphere(ray, c, r) { const oc = V.sub(ray.o, c); const b = V.dot(oc, ray.d); const cc = V.dot(oc, oc) - r * r; const h = b * b - cc; if (h < 0) return null; const t = -b - Math.sqrt(h); return t > 0 ? t : null; }
  function rayPlane(ray, y) { if (Math.abs(ray.d[1]) < 1e-9) return null; const t = (y - ray.o[1]) / ray.d[1]; return t > 0 ? t : null; }
  function rayAABB(ray, b) { let tmin = 0, tmax = 1e9; for (let i = 0; i < 3; i++) { const inv = 1 / (ray.d[i] || 1e-12); let t0 = (b[i] - ray.o[i]) * inv, t1 = (b[i + 3] - ray.o[i]) * inv; if (t0 > t1) [t0, t1] = [t1, t0]; tmin = Math.max(tmin, t0); tmax = Math.min(tmax, t1); if (tmax < tmin) return false; } return true; }
  function rayMesh(ray, mesh) {
    const v = mesh.vertices, idx = mesh.indices; let best = null;
    const o = ray.o, d = ray.d;
    for (let i = 0; i < idx.length; i += 3) {
      const a = idx[i] * 12, b = idx[i + 1] * 12, c = idx[i + 2] * 12;
      const e1x = v[b] - v[a], e1y = v[b + 1] - v[a + 1], e1z = v[b + 2] - v[a + 2], e2x = v[c] - v[a], e2y = v[c + 1] - v[a + 1], e2z = v[c + 2] - v[a + 2];
      const px = d[1] * e2z - d[2] * e2y, py = d[2] * e2x - d[0] * e2z, pz = d[0] * e2y - d[1] * e2x;
      const det = e1x * px + e1y * py + e1z * pz; if (Math.abs(det) < 1e-12) continue; const inv = 1 / det;
      const tx = o[0] - v[a], ty = o[1] - v[a + 1], tz = o[2] - v[a + 2];
      const u = (tx * px + ty * py + tz * pz) * inv; if (u < 0 || u > 1) continue;
      const qx = ty * e1z - tz * e1y, qy = tz * e1x - tx * e1z, qz = tx * e1y - ty * e1x;
      const w = (d[0] * qx + d[1] * qy + d[2] * qz) * inv; if (w < 0 || u + w > 1) continue;
      const t = (e2x * qx + e2y * qy + e2z * qz) * inv; if (t > 0 && (best == null || t < best)) best = t;
    }
    return best;
  }
  function rayCylinder(ray, base, axis, len, r) {
    // distance between ray and segment; returns ray t if within radius
    const u = ray.d, v = axis, w0 = V.sub(ray.o, base);
    const a = V.dot(u, u), b = V.dot(u, v), c = V.dot(v, v), d = V.dot(u, w0), e = V.dot(v, w0);
    const den = a * c - b * b; if (den < 1e-9) return null;
    const sc = (b * e - c * d) / den, tc = (a * e - b * d) / den;
    if (sc < 0 || tc < 0 || tc > len) return null;
    const p = V.madd(ray.o, ray.d, sc), q = V.madd(base, axis, tc);
    return V.dist(p, q) < r ? sc : null;
  }

  RF.scene = { Scene, emptyProject, defaultLayers, autoIntersect, uid, earcut, insetPolygon, buildBlock };
})(globalThis.RF || (globalThis.RF = {}));
