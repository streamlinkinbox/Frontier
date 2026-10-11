/* RoadForge — vertical structures: bridges / viaducts, embankment tie-ins, retaining walls, cuttings, tunnels.
 *
 * Every road is classified per station into spans of {ground, embankment, bridge, cut, retained, tunnel}. The road
 * surface itself (asphalt, kerbs, footways) is always built by roadmesh; this module adds everything that carries or
 * encloses it, and reports the ground-plane holes (stencil) that cuttings and tunnel portals require.
 *
 * Real-world dimensions used: minimum headroom 5.3 m under overbridges, box-girder depth ≈ span/22 (1.2–1.8 m),
 * 0.45 m × 0.95 m concrete parapets, Ø1.5 m circular piers at ≤ 32 m centres, retaining walls 0.5 m thick with a
 * 1.1 m coping above ground, tunnel clear height 5.5 m with a 0.9 m roof slab.
 */
(function (RF) {
  'use strict';
  const V = RF.V, S = RF.spline, MB = RF.MeshBuilder, prim = RF.prim;
  const GROUND_Y = -0.02;
  const T = { ground: 0.6, bridgeAuto: 14.0, tunnelAuto: 9.0, minSpan: 6 };

  function kindForHeight(R, y) {
    const o = R.road.structure || 'auto';
    if (o !== 'auto') return o;
    if (y > T.bridgeAuto) return 'bridge';
    if (y > T.ground) return 'embankment';
    if (y >= -T.ground) return 'ground';
    if (y > -T.tunnelAuto) return R.lay.hasCurb ? 'retained' : 'cut';
    return 'tunnel';
  }

  /** Does the point lie within the open cutting footprint of another road? (used to turn ground-level crossings into decks) */
  function overCutting(net, R, p) {
    for (const O of net.roads.values()) {
      if (O === R || !O.structure) continue;
      if (!O.structure.spans.some((sp) => sp.kind === 'cut' || sp.kind === 'retained')) continue;
      const c = S.closestOnPolyline(O.dense, O.cum, p);
      if (!c || !c.p) continue;
      const half = Math.max(O.lay.outerL, O.lay.outerR) + 1.0;
      if (c.dist > half) continue;
      const k = O.structure.at(c.s);
      if ((k === 'cut' || k === 'retained') && c.p[1] < p[1] - 3.0) return true;
    }
    return false;
  }

  /** Is the point on an elevated road passing over another road / rail corridor (needs a deck instead of an embankment)?
   *  Allowance = corridor half width + 4 m verge + 1:1.5 slope run of the height difference (capped), so the embankment toe never buries the lower road. */
  function overCorridor(net, R, p, corridors) {
    for (const O of net.roads.values()) {
      if (O === R) continue;
      const c = S.closestOnPolyline(O.dense, O.cum, p);
      if (!c || !c.p) continue;
      const dy = p[1] - c.p[1]; if (dy < 3.0) continue;
      const half = Math.max(O.lay.outerL, O.lay.outerR);
      if (c.dist <= half + 4.0 + Math.min(14, dy * 1.5)) return true;
    }
    for (const C of corridors || []) {
      const c = S.closestOnPolyline(C.dense, C.cum, p);
      if (!c || !c.p) continue;
      const dy = p[1] - c.p[1]; if (dy < 3.0) continue;
      if (c.dist <= (C.half || 4.5) + 4.0 + Math.min(14, dy * 1.5)) return true;
    }
    return false;
  }
  /** Would a pier at this XZ point stand inside another road's paved corridor? */
  function pierBlocked(net, R, p, corridors) {
    for (const O of net.roads.values()) {
      if (O === R) continue;
      const c = S.closestOnPolyline(O.dense, O.cum, p);
      if (!c || !c.p || c.p[1] > p[1] - 3.0) continue;
      if (c.dist <= Math.max(O.lay.outerL, O.lay.outerR) + 1.2) return true;
    }
    for (const C of corridors || []) { const c = S.closestOnPolyline(C.dense, C.cum, p); if (c && c.p && c.dist <= (C.half || 4.5) + 1.2) return true; }
    return false;
  }

  function spansFrom(R, kinds) {
    const sm = R.samples; const spans = [];
    for (let i = 0; i < sm.length; i++) {
      const k = kinds[i];
      if (spans.length && spans[spans.length - 1].kind === k) { spans[spans.length - 1].s1 = sm[i].s; spans[spans.length - 1].i1 = i; }
      else spans.push({ kind: k, s0: sm[i].s, s1: sm[i].s, i0: i, i1: i });
    }
    // absorb very short spans into the neighbour with the "heavier" structure
    const rank = { ground: 0, embankment: 1, cut: 2, retained: 3, bridge: 4, tunnel: 5 };
    let changed = true;
    while (changed && spans.length > 1) {
      changed = false;
      for (let i = 0; i < spans.length; i++) {
        const sp = spans[i];
        if (sp.s1 - sp.s0 >= T.minSpan) continue;
        const prev = spans[i - 1], next = spans[i + 1];
        const into = prev && next ? (rank[prev.kind] >= rank[next.kind] ? prev : next) : prev || next;
        if (!into) break;
        into.s0 = Math.min(into.s0, sp.s0); into.s1 = Math.max(into.s1, sp.s1); into.i0 = Math.min(into.i0, sp.i0); into.i1 = Math.max(into.i1, sp.i1);
        spans.splice(i, 1); changed = true; break;
      }
    }
    // merge equal neighbours again
    for (let i = spans.length - 2; i >= 0; i--) if (spans[i].kind === spans[i + 1].kind) { spans[i].s1 = spans[i + 1].s1; spans[i].i1 = spans[i + 1].i1; spans.splice(i + 1, 1); }
    return spans;
  }

  function attach(R, spans) {
    const at = (s) => { for (const sp of spans) if (s <= sp.s1 + 1e-6) return sp.kind; return spans[spans.length - 1].kind; };
    R.structure = { spans, at, isBridgeAt: (s) => { const k = at(s); return k === 'bridge' || k === 'tunnel'; }, hasKind: (k) => spans.some((sp) => sp.kind === k) };
  }

  /** Classify every road into structure spans. Two passes so ground roads crossing cuttings become decks. */
  function classify(net, opts = {}) {
    const corridors = opts.corridors || [];
    net.corridors = corridors;
    for (const R of net.roads.values()) attach(R, spansFrom(R, R.samples.map((f) => kindForHeight(R, f.p[1]))));
    for (const R of net.roads.values()) {
      if ((R.road.structure || 'auto') !== 'auto') continue;
      const kinds = R.samples.map((f) => {
        const k = kindForHeight(R, f.p[1]);
        if (k === 'embankment' && overCorridor(net, R, f.p, corridors)) return 'bridge';
        if ((k === 'ground' || k === 'embankment') && overCutting(net, R, f.p)) return 'bridge';
        return k;
      });
      attach(R, spansFrom(R, kinds));
    }
  }

  /* ------------------------------------------------------------------ */
  /* Geometry                                                             */
  /* ------------------------------------------------------------------ */
  const outerOf = (R, side) => (side > 0 ? R.lay.hwR + (R.lay.hasCurb ? R.lay.curbW + R.lay.sidewalkR : 0) : R.lay.hwL + (R.lay.hasCurb ? R.lay.curbW + R.lay.sidewalkL : 0));
  const topLift = (R, side) => (R.lay.hasCurb ? R.lay.curbH + (side > 0 ? R.lay.sidewalkR : R.lay.sidewalkL) * 0.02 : 0);
  const framesIn = (R, sp) => R.samples.slice(sp.i0, sp.i1 + 1);

  /** Point on the road cross-section at lateral offset t (y includes crossfall/features). */
  function P(R, f, t, lift = 0, layers) { return [f.p[0] + f.n[0] * t, f.p[1] + RF.network.surfaceOffset(R, f.s, t, layers) + lift, f.p[2] + f.n[2] * t]; }

  function buildBridge(R, sp, net, layers, out) {
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    const con = get('concrete'), rail = get('metal_galv');
    const frames = framesIn(R, sp); if (frames.length < 2) return;
    const tR = outerOf(R, 1), tL = outerOf(R, -1), lift = topLift(R, 1);
    const span = sp.s1 - sp.s0;
    const nPiers = Math.max(1, Math.round(span / 32));
    const depth = RF.clamp((span / nPiers) / 22, 1.1, 1.9);
    const web = Math.min(1.8, (tR + tL) * 0.22);
    const parW = R.lay.hasCurb ? 0.45 : 0.5, parH = 0.95;
    const edgeExt = R.lay.hasCurb ? 0 : 0.5; // highways: deck extends beyond the shoulder to carry the parapet
    const eR = tR + edgeExt, eL = tL + edgeExt;
    // cross-section (t, dy) relative to the outer-top datum; traversed right → down → soffit → up left (outward normals with flip)
    const prof = [
      [eR - parW, 0], [eR - parW, parH - 0.12], [eR - parW + 0.1, parH], [eR, parH], [eR, 0], [eR, -0.55], [eR - web, -0.95], [eR - web, -depth],
      [-eL + web, -depth], [-eL + web, -0.95], [-eL, -0.55], [-eL, 0], [-eL, parH], [-eL + parW - 0.1, parH], [-eL + parW, parH - 0.12], [-eL + parW, 0],
    ];
    const fr = frames.map((f) => ({ p: [f.p[0], f.p[1] + lift, f.p[2]], d: f.d, n: f.n }));
    prim.sweep(con, fr, prof, { flip: true, uvScale: 0.5 });
    // steel railing on the parapet: posts every 2 m, top rail + mid rail (tube Ø 48 mm)
    for (const side of [1, -1]) {
      const tP = side > 0 ? eR - parW / 2 : -(eL - parW / 2);
      const railPts = frames.map((f) => P(R, f, tP, lift + parH, layers));
      for (const h of [0.55, 1.05]) prim.sweep(rail, frames.map((f, i) => ({ p: [railPts[i][0], railPts[i][1] + h, railPts[i][2]], d: f.d, n: f.n })), circleProfile(0.024, 6), { closed: true });
      let acc = 0;
      for (let i = 1; i < frames.length; i++) {
        acc += frames[i].s - frames[i - 1].s;
        if (acc >= 2 || i === 1) { acc = 0; const p = railPts[i]; prim.lathe(rail, [[0.03, 0], [0.03, 1.05]], 6, { transform: RF.M4.translation(p[0], p[1], p[2]) }); }
      }
    }
    // piers: circular columns with a hammer-head cap from ground to soffit
    const soffitW = eR + eL - 2 * web;
    for (let i = 1; i <= nPiers; i++) {
      let s = sp.s0 + (span * (i - 0.5)) / nPiers;
      if (span < 14 && i > 1) break;
      // never stand a pier in a lower carriageway: slide it along the deck (median / verge) or drop it
      let f = RF.roadmesh.frameAt(R, s), ok = !pierBlocked(net, R, f.p, net.corridors);
      for (let k = 1; !ok && k <= 10; k++) {
        for (const sg of [1, -1]) { const ss = s + sg * k * 1.5; if (ss < sp.s0 + 3 || ss > sp.s1 - 3) continue; const ff = RF.roadmesh.frameAt(R, ss); if (!pierBlocked(net, R, ff.p, net.corridors)) { s = ss; f = ff; ok = true; break; } }
      }
      if (!ok) continue;
      const ySoffit = f.p[1] + lift - depth, groundY = GROUND_Y - 0.5;
      if (ySoffit - groundY < 1.5) continue;
      const capH = 1.3, capW = soffitW + 0.6, capL = 1.6;
      const M = RF.M4.fromBasis(f.n, [0, 1, 0], f.d, [f.p[0], 0, f.p[2]]);
      prim.box(con, [capW, capH, capL], { center: [0, ySoffit - capH / 2, 0], transform: M, bevel: 0.08 });
      const cols = capW > 9 ? [-capW / 4, capW / 4] : [0];
      for (const c of cols) prim.lathe(con, [[0.75, groundY], [0.75, ySoffit - capH + 0.01]], 20, { transform: RF.M4.translation(f.p[0] + f.n[0] * c, 0, f.p[2] + f.n[2] * c) });
    }
  }

  function circleProfile(r, n) { const p = []; for (let i = 0; i < n; i++) { const a = (i / n) * Math.PI * 2; p.push([Math.cos(a) * r, Math.sin(a) * r]); } return p; }

  /** Retaining walls for retained cuttings (urban underpasses) + coping above ground. */
  function buildRetained(R, sp, net, layers, out, holes) {
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    const con = get('concrete');
    const frames = framesIn(R, sp); if (frames.length < 2) return;
    const poly = [];
    for (const side of [1, -1]) {
      const tO = outerOf(R, side), lift = topLift(R, side);
      const wallT = 0.5, coping = 1.1;
      const rows = [[], [], [], [], []];
      for (const f of frames) {
        const base = P(R, f, side * tO, lift, layers);
        const o = (d, y) => [base[0] + f.n[0] * side * d, y, base[2] + f.n[2] * side * d];
        const topY = Math.max(GROUND_Y + coping, base[1] + 0.3);
        rows[0].push(con.vertex(base, [f.s, 0]));
        rows[1].push(con.vertex(o(0, topY), [f.s, topY - base[1]]));
        rows[2].push(con.vertex(o(wallT, topY), [f.s, topY - base[1] + wallT]));
        rows[3].push(con.vertex(o(wallT, GROUND_Y - 0.02), [f.s, 0]));
        rows[4].push(con.vertex(o(wallT + 0.3, GROUND_Y - 0.02), [f.s, 0]));
      }
      con.grid(rows, side < 0);
      const edge = frames.map((f) => { const b = P(R, f, side * (tO + 0.55), 0, layers); return [b[0], b[2]]; });
      if (side > 0) poly.push(...edge); else poly.push(...edge.reverse());
    }
    holes.push(poly);
  }

  /** Open cutting: grass slopes to ground are built by roadmesh; here only the ground hole polygon. */
  function cutHole(R, sp, layers, holes) {
    const frames = framesIn(R, sp); if (frames.length < 2) return;
    const poly = [];
    for (const side of [1, -1]) {
      const edge = frames.map((f) => { const tO = outerOf(R, side) + Math.max(R.lay.verge, 1); const e = P(R, f, side * tO, 0, layers); const depth = Math.max(0, GROUND_Y - e[1] + 0.3); const run = depth * 1.5 + 0.4; const b = P(R, f, side * (tO + run), 0, layers); return [b[0], b[2]]; });
      if (side > 0) poly.push(...edge); else poly.push(...edge.reverse());
    }
    holes.push(poly);
  }

  /** Tunnel: walls, roof slab, lighting, portals at open ends. */
  function buildTunnel(R, sp, net, layers, out) {
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    const wall = get('tunnel_wall'), con = get('concrete'), lamp = get('emissive_white');
    const frames = framesIn(R, sp); if (frames.length < 2) return;
    const tR = outerOf(R, 1), tL = outerOf(R, -1), lift = topLift(R, 1);
    const clear = 5.5, slab = 0.9, wallT = 0.6;
    // inner lining (inward-facing normals): left wall up, ceiling, right wall down
    const inner = [[tR, 0], [tR, 2.2], [tR - 0.15, clear - 0.9], [tR - 0.9, clear], [-tL + 0.9, clear], [-tL + 0.15, clear - 0.9], [-tL, 2.2], [-tL, 0]];
    const fr = frames.map((f) => ({ p: [f.p[0], f.p[1] + lift, f.p[2]], d: f.d, n: f.n }));
    prim.sweep(wall, fr, inner, { flip: false, uvScale: 0.5 });
    // outer shell (visible in x-ray / from above when ground is cut)
    const outer = [[-tL - wallT, -0.3], [-tL - wallT, clear + slab], [tR + wallT, clear + slab], [tR + wallT, -0.3]];
    prim.sweep(con, fr, outer, { flip: true, uvScale: 0.5 });
    // continuous lighting strips
    for (const t of [tR - 1.2, -tL + 1.2]) prim.sweep(lamp, fr.map((f) => ({ p: V.add(V.add(f.p, V.mul(f.n, t)), [0, clear - 0.05, 0]), d: f.d, n: f.n })), [[-0.12, 0], [0.12, 0], [0.12, -0.08], [-0.12, -0.08]], { closed: true, flip: false });
    // portals
    const ends = [];
    if (sp.s0 > R.samples[0].s + 1e-3) ends.push({ f: frames[0], sgn: -1 });
    if (sp.s1 < R.samples[R.samples.length - 1].s - 1e-3) ends.push({ f: frames[frames.length - 1], sgn: 1 });
    for (const e of ends) {
      const f = e.f; const M = RF.M4.fromBasis(f.n, [0, 1, 0], f.d, [f.p[0], f.p[1] + lift, f.p[2]]);
      const W = tR + tL + 2 * wallT + 2.4, H = clear + slab + 1.2;
      // head wall with opening: four boxes around the opening
      const t0 = -tL - wallT - 1.2, t1 = tR + wallT + 1.2;
      prim.box(con, [W, 1.2 + slab, 1.0], { center: [(t0 + t1) / 2, clear + (1.2 + slab) / 2, e.sgn * 0.5], transform: M });
      prim.box(con, [wallT + 1.2, clear, 1.0], { center: [t1 - (wallT + 1.2) / 2, clear / 2, e.sgn * 0.5], transform: M });
      prim.box(con, [wallT + 1.2, clear, 1.0], { center: [t0 + (wallT + 1.2) / 2, clear / 2, e.sgn * 0.5], transform: M });
      void H;
    }
  }

  /** Build all structures for a road; returns ground-hole polygons ([x,z] loops). */
  function build(R, net, layers, out) {
    const holes = [];
    if (!R.structure) return holes;
    for (const sp of R.structure.spans) {
      if (sp.kind === 'bridge') buildBridge(R, sp, net, layers, out);
      else if (sp.kind === 'retained') buildRetained(R, sp, net, layers, out, holes);
      else if (sp.kind === 'cut') cutHole(R, sp, layers, holes);
      else if (sp.kind === 'tunnel') buildTunnel(R, sp, net, layers, out);
    }
    return holes;
  }

  RF.structures = { classify, build, outerOf, topLift, GROUND_Y, THRESHOLDS: T };
})(globalThis.RF || (globalThis.RF = {}));
