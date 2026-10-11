/* RoadForge — automatic city blocks: the planar faces of the road network, bounded by the footway outer edges. */
(function (RF) {
  'use strict';
  const V = RF.V, S = RF.spline;

  const MIN_AREA = 150, MAX_AREA = 450000, MAX_GRADE_Y = 0.6;

  function signedArea(pts) { let a = 0; for (let i = 0, n = pts.length; i < n; i++) { const p = pts[i], q = pts[(i + 1) % n]; a += p[0] * q[1] - q[0] * p[1]; } return a / 2; }

  /** Ramer–Douglas–Peucker simplification of a closed loop (keeps fillet corners, collapses straight runs). */
  function simplifyLoop(pts, tol) {
    if (pts.length < 8) return pts;
    // open the loop at the two farthest-apart vertices so the closing edge is handled too
    let i0 = 0, i1 = 0, best = -1;
    for (let i = 0; i < pts.length; i += Math.max(1, Math.floor(pts.length / 64))) for (let j = i + 1; j < pts.length; j += Math.max(1, Math.floor(pts.length / 64))) { const d = (pts[i][0] - pts[j][0]) ** 2 + (pts[i][1] - pts[j][1]) ** 2; if (d > best) { best = d; i0 = i; i1 = j; } }
    const a = [], b = [];
    for (let i = i0; i !== i1; i = (i + 1) % pts.length) a.push(pts[i]); a.push(pts[i1]);
    for (let i = i1; i !== i0; i = (i + 1) % pts.length) b.push(pts[i]); b.push(pts[i0]);
    const rdp = (arr) => {
      if (arr.length < 3) return arr;
      const p0 = arr[0], p1 = arr[arr.length - 1]; const dx = p1[0] - p0[0], dz = p1[1] - p0[1]; const L = Math.hypot(dx, dz) || 1;
      let far = -1, fi = -1;
      for (let i = 1; i < arr.length - 1; i++) { const d = Math.abs((arr[i][0] - p0[0]) * dz - (arr[i][1] - p0[1]) * dx) / L; if (d > far) { far = d; fi = i; } }
      if (far <= tol) return [p0, p1];
      const left = rdp(arr.slice(0, fi + 1)), right = rdp(arr.slice(fi));
      return left.slice(0, -1).concat(right);
    };
    const ra = rdp(a), rb = rdp(b);
    return ra.slice(0, -1).concat(rb.slice(0, -1));
  }

  function selfIntersects(pts) {
    const n = pts.length; if (n < 4) return false;
    const segInt = (a, b, c, d) => {
      const r = [b[0] - a[0], b[1] - a[1]], s = [d[0] - c[0], d[1] - c[1]];
      const den = r[0] * s[1] - r[1] * s[0]; if (Math.abs(den) < 1e-12) return false;
      const t = ((c[0] - a[0]) * s[1] - (c[1] - a[1]) * s[0]) / den, u = ((c[0] - a[0]) * r[1] - (c[1] - a[1]) * r[0]) / den;
      return t > 1e-6 && t < 1 - 1e-6 && u > 1e-6 && u < 1 - 1e-6;
    };
    for (let i = 0; i < n; i++) {
      for (let j = i + 2; j < n; j++) {
        if (i === 0 && j === n - 1) continue;
        if (segInt(pts[i], pts[(i + 1) % n], pts[j], pts[(j + 1) % n])) return true;
      }
    }
    return false;
  }

  /**
   * Trace the faces of the solved network and return block records (same shape as explicit project blocks).
   * Every face is walked with the block on the traveller's left: along each road's left footway edge, then around the
   * junction corner footway between the arriving arm and the next arm anticlockwise.
   */
  function autoBlocks(net, project, opts = {}) {
    const surfaceMode = opts.surface || 'mixed';
    const depth = opts.depth || 120;
    const overrides = project.blockOverrides || {};
    const visited = new Set();
    const faces = [];
    const halfKey = (roadId, dir) => roadId + (dir > 0 ? '>' : '<');
    const urbanOK = (R) => R.lay.kind === 'urban' && R.lay.hasCurb;
    const atGrade = (R) => (R.samples || []).every((f) => Math.abs(f.p[1]) <= MAX_GRADE_Y);
    /** Footway outer-edge points of a half-edge (travelling dir), optionally limited to `fromEnd` metres before the far node / `fromStart` after the start. */
    const edgePts = (R, dir, lim = {}) => {
      const lay = R.lay, off = dir > 0 ? -lay.outerL : lay.outerR;
      const sm = R.samples || []; const order = dir > 0 ? sm : [...sm].reverse();
      const s0 = order.length ? order[0].s : 0, s1 = order.length ? order[order.length - 1].s : 0;
      const along = (f) => Math.abs(f.s - s0), total = Math.abs(s1 - s0);
      const out = [];
      for (const f of order) {
        const a = along(f);
        if (lim.fromStart != null && a > lim.fromStart + 1e-6) break;
        if (lim.fromEnd != null && total - a > lim.fromEnd + 1e-6) continue;
        out.push([f.p[0] + f.n[0] * off, f.p[2] + f.n[2] * off]);
      }
      // exact cut points
      const cut = (sta) => { const e = S.evalAt(R.dense, R.cum, sta); const d = V.normXZ(e.d), n = V.perp(d); return [e.p[0] + n[0] * off, e.p[2] + n[2] * off]; };
      if (lim.fromStart != null && lim.fromStart < total) out.push(cut(dir > 0 ? s0 + lim.fromStart : s0 - lim.fromStart));
      if (lim.fromEnd != null && lim.fromEnd < total) out.unshift(cut(dir > 0 ? s1 - lim.fromEnd : s1 + lim.fromEnd));
      return out;
    };
    const cornerPts = (rec, corner) => {
      const X = corner.A, B = corner.B, layA = X.road.lay, layB = B.road.lay;
      const swA = X.isStart ? layA.sidewalkR : layA.sidewalkL, swB = B.isStart ? layB.sidewalkL : layB.sidewalkR;
      const poly = S.resampleEqual(corner.poly, Math.max(2, Math.ceil(corner.length / 0.6)));
      const n = poly.length, C = rec.node.p, out = [];
      for (let i = 0; i < n; i++) {
        const p = poly[i]; let nn;
        if (i === 0) nn = X.nOut; else if (i === n - 1) nn = V.mul(B.nOut, -1);
        else { const a = poly[i - 1], b = poly[i + 1]; nn = V.perp(V.normXZ(V.sub(b, a))); if (V.dot(nn, V.sub(p, C)) < 0) nn = V.mul(nn, -1); }
        const t = i / (n - 1), d = (layA.hasCurb || layB.hasCurb ? 0.15 : 0) + RF.lerp(swA, swB, t) + Math.max(layA.verge, layB.verge);
        out.push([p[0] + nn[0] * d, p[2] + nn[2] * d]);
      }
      return out;
    };
    // --- 1. trace every face as a sequence of (half-edge, node, corner) steps
    for (const R0 of net.roads.values()) {
      for (const dir0 of [1, -1]) {
        if (visited.has(halfKey(R0.id, dir0))) continue;
        const steps = []; let R = R0, dir = dir0, ok = true, guard = 0;
        while (guard++ < 4000) {
          const key = halfKey(R.id, dir);
          if (visited.has(key)) { if (!(R === R0 && dir === dir0 && steps.length)) ok = false; break; }
          visited.add(key);
          const nodeId = dir > 0 ? R.road.b : R.road.a;
          const rec = net.nodes.get(nodeId); if (!rec) { ok = false; break; }
          const X = R.arms[dir > 0 ? 'b' : 'a'];
          const idx = rec.arms.indexOf(X); if (idx < 0) { ok = false; break; }
          const next = rec.arms[(idx + 1) % rec.arms.length];
          const corner = (rec.corners || []).find((c) => c.A === X && c.poly) || null;
          steps.push({ R, dir, rec, X, next, corner });
          R = next.road; dir = next.isStart ? 1 : -1;
          if (R === R0 && dir === dir0) break;
        }
        if (!ok || !steps.length) continue;
        const pts = [];
        for (const st of steps) { pts.push(...edgePts(st.R, st.dir)); if (st.corner) pts.push(...cornerPts(st.rec, st.corner)); }
        const clean = dedupe(pts);
        if (clean.length < 3) continue;
        faces.push({ steps, pts: clean, area: signedArea(clean), allUrban: steps.every((st) => urbanOK(st.R) && atGrade(st.R)) });
      }
    }
    if (!faces.length) return [];
    // the unbounded face of each component is traced with the opposite orientation to the inner faces
    let outer = faces[0]; for (const f of faces) if (Math.abs(f.area) > Math.abs(outer.area)) outer = f;
    const outerSign = Math.sign(outer.area);
    const out = [];
    const emit = (key, poly, roads, nodes, maxCh, maxSw) => {
      if (poly.length < 3) return;
      const A = Math.abs(signedArea(poly));
      if (A < MIN_AREA || A > MAX_AREA || selfIntersects(poly)) return;
      const ov = overrides[key] || {};
      if (ov.hidden) return;
      const cx = poly.reduce((a, p) => a + p[0], 0) / poly.length, cz = poly.reduce((a, p) => a + p[1], 0) / poly.length;
      const hsh = Math.abs(RF.hashStr ? RF.hashStr(key) : Math.round(cx * 7 + cz * 13));
      const mixed = ['paving', 'grass', 'paving', 'gravel', 'grass', 'concrete'];
      const surface = ov.surface || (surfaceMode === 'mixed' ? mixed[hsh % mixed.length] : surfaceMode);
      out.push({ id: key, name: ov.name || `Block ${out.length + 1}`, poly, surface, sidewalk: 0, auto: true, edgeH: 0.02 + maxCh + maxSw * 0.02 + 0.03, area: A, centre: [cx, 0, cz], roads, nodes });
    };
    for (const f of faces) {
      const inner = Math.sign(f.area) !== outerSign && Math.abs(f.area) <= MAX_AREA;
      if (inner && f.allUrban) {
        const roads = [...new Set(f.steps.map((st) => st.R.id))], nodes = [...new Set(f.steps.map((st) => st.rec.id))];
        const maxCh = Math.max(...f.steps.map((st) => st.R.lay.curbH)), maxSw = Math.max(...f.steps.map((st) => st.dir > 0 ? st.R.lay.sidewalkL : st.R.lay.sidewalkR));
        emit('auto:' + nodes.slice().sort().join('+') + ':' + roads.length, simplifyLoop(f.pts, 0.02), roads, nodes, maxCh, maxSw);
        continue;
      }
      // --- 2. unbounded / oversized faces: one roadside "quadrant" block per junction corner, `depth` metres deep
      for (const st of f.steps) {
        if (!st.corner || st.corner.gap > 2.6) continue;
        const RA = st.R, RB = st.next.road, dirB = st.next.isStart ? 1 : -1;
        if (!urbanOK(RA) || !urbanOK(RB) || !atGrade(RA) || !atGrade(RB)) continue;
        const farEnd = (R, dir) => net.nodes.get(dir > 0 ? R.road.b : R.road.a)?.kind === 'end';
        const LA = Math.min(depth, farEnd(RA, -st.dir) ? RA.bodyLength : RA.bodyLength / 2);
        const LB = Math.min(depth, farEnd(RB, dirB) ? RB.bodyLength : RB.bodyLength / 2);
        if (LA < 12 || LB < 12) continue;
        const ea = edgePts(RA, st.dir, { fromEnd: LA }), cp = cornerPts(st.rec, st.corner), eb = edgePts(RB, dirB, { fromStart: LB });
        if (!ea.length || !eb.length) continue;
        const E1 = ea[0], E2 = eb[eb.length - 1], P2 = eb[0];
        const K = [E1[0] + (E2[0] - P2[0]), E1[1] + (E2[1] - P2[1])];
        const poly = simplifyLoop(dedupe([...ea, ...cp, ...eb, K]), 0.02);
        emit('auto:' + st.rec.id + ':' + RA.id + '>' + RB.id, poly, [RA.id, RB.id], [st.rec.id], Math.max(RA.lay.curbH, RB.lay.curbH), Math.max(st.dir > 0 ? RA.lay.sidewalkL : RA.lay.sidewalkR, dirB > 0 ? RB.lay.sidewalkL : RB.lay.sidewalkR));
      }
    }
    return out;
  }
  function dedupe(pts) {
    if (!pts.length) return [];
    const clean = [pts[0]];
    for (let i = 1; i < pts.length; i++) { const q = clean[clean.length - 1]; if (Math.hypot(pts[i][0] - q[0], pts[i][1] - q[1]) > 0.05) clean.push(pts[i]); }
    while (clean.length > 1 && Math.hypot(clean[0][0] - clean[clean.length - 1][0], clean[0][1] - clean[clean.length - 1][1]) < 0.05) clean.pop();
    return clean;
  }

  RF.blocks = { autoBlocks, signedArea, simplifyLoop, selfIntersects };
})(globalThis.RF || (globalThis.RF = {}));
