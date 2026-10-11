/* RoadForge — network solver: splines, arms, junction corner fillets, trims and junction boundaries.
 *
 * Every junction is solved from the actual curb-line polylines of its approaches: the two edge lines that face each
 * other are intersected, a tangent-continuous fillet of the requested radius is fitted beyond that intersection, and
 * each approach is trimmed back to the furthest tangent point it owns. Acute merges therefore receive long runouts
 * instead of overlapping geometry, and the junction surface becomes one merged all-quad patch.
 */
(function (RF) {
  'use strict';
  const V = RF.V, S = RF.spline;
  const DENSE_STEP = 0.25;
  const STRAIGHT_GAP = (176 * Math.PI) / 180; // gaps wider than this get no fillet (through movement)
  const CORNER_HALF_RES = 6, END_HALF_RES = 4;

  function nodeDegree(project, nodeId) { let d = 0; for (const r of Object.values(project.roads)) { if (r.a === nodeId) d++; if (r.b === nodeId) d++; } return d; }

  function roadsAtNode(project, nodeId) { return Object.values(project.roads).filter((r) => r.a === nodeId || r.b === nodeId); }

  /** Control points of a road including its end nodes. */
  function controlPoints(project, road) {
    const A = project.nodes[road.a], B = project.nodes[road.b];
    return [V.clone(A.p), ...(road.pts || []).map(V.clone), V.clone(B.p)];
  }

  /** Tangent for a degree-2 node so both roads join smoothly. Returns unit direction pointing INTO `road` from the node. */
  function continuityTangent(project, road, nodeId) {
    const others = roadsAtNode(project, nodeId).filter((r) => r.id !== road.id);
    if (others.length !== 1) return null;
    const o = others[0];
    const cpThis = controlPoints(project, road);
    const cpOther = controlPoints(project, o);
    const nextThis = road.a === nodeId ? cpThis[1] : cpThis[cpThis.length - 2];
    const nextOther = o.a === nodeId ? cpOther[1] : cpOther[cpOther.length - 2];
    const d = V.normXZ(V.sub(nextThis, nextOther));
    // only enforce continuity when the roads are roughly opposite (not a sharp kink)
    const node = project.nodes[nodeId].p;
    const a = V.normXZ(V.sub(nextThis, node)), b = V.normXZ(V.sub(nextOther, node));
    if (V.dot(a, b) > -0.5) return null;
    return d;
  }

  function solve(project, opts = {}) {
    const handed = project.settings?.handedness || 'right';
    const diagnostics = [];
    const roads = new Map(), nodes = new Map();
    // --- 1. roads: dense samples & edges
    for (const road of Object.values(project.roads)) {
      if (!project.nodes[road.a] || !project.nodes[road.b]) continue;
      const prof = RF.profiles.effective(road), lay = RF.profiles.layout(prof, handed);
      const cps = controlPoints(project, road);
      const tanA = continuityTangent(project, road, road.a);
      const tanBraw = continuityTangent(project, road, road.b);
      const tanB = tanBraw ? V.mul(tanBraw, -1) : null; // samplePolyline expects outgoing tangent at the end
      let dense = S.samplePolyline(cps, tanA, tanB, DENSE_STEP);
      // ensure straight 2-point lines get subdivided for elevation interpolation
      if (dense.length === 2) { const L = V.dist(dense[0], dense[1]); const n = Math.max(2, Math.ceil(L / 2)); const a = dense[0], b = dense[1]; dense = []; for (let i = 0; i <= n; i++) dense.push(V.lerp(a, b, i / n)); }
      const cum = S.cumulativeXZ(dense);
      const length = cum[cum.length - 1];
      // monotone (Fritsch–Carlson) vertical alignment through the control-point heights: no overshoot below/above the
      // heights the user actually set, so a 7 m hump never produces a spurious cutting next to it.
      if (cps.length > 2) {
        const sk = []; { let acc = 0; sk.push(0); for (let i = 1; i < cps.length; i++) { acc += V.distXZ(cps[i - 1], cps[i]); sk.push(acc); } }
        // map control stations onto the actual arc length of the spline
        const scale = length / Math.max(sk[sk.length - 1], 1e-6); for (let i = 0; i < sk.length; i++) sk[i] *= scale;
        const ys = cps.map((c) => c[1]);
        const n = ys.length, d = new Array(n - 1), m = new Array(n).fill(0);
        for (let i = 0; i < n - 1; i++) d[i] = (ys[i + 1] - ys[i]) / Math.max(sk[i + 1] - sk[i], 1e-6);
        m[0] = d[0]; m[n - 1] = d[n - 2];
        for (let i = 1; i < n - 1; i++) m[i] = d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2;
        for (let i = 0; i < n - 1; i++) { if (Math.abs(d[i]) < 1e-9) { m[i] = 0; m[i + 1] = 0; continue; } const a = m[i] / d[i], b = m[i + 1] / d[i]; const h = Math.hypot(a, b); if (h > 3) { m[i] = (3 * a) / h * d[i]; m[i + 1] = (3 * b) / h * d[i]; } }
        for (let k = 0; k < dense.length; k++) {
          const x = cum[k]; let i = 0; while (i < n - 2 && x > sk[i + 1]) i++;
          const h = Math.max(sk[i + 1] - sk[i], 1e-6), t = RF.clamp((x - sk[i]) / h, 0, 1), t2 = t * t, t3 = t2 * t;
          dense[k][1] = (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h * m[i + 1];
        }
      }
      const edgeR = S.offsetPolyline(dense, lay.hwR), edgeL = S.offsetPolyline(dense, -lay.hwL);
      roads.set(road.id, { id: road.id, road, prof, lay, cps, dense, cum, length, edgeR, edgeL, trimStart: 0, trimEnd: 0, arms: {} });
    }
    // --- 2. arms per node
    const armsByNode = new Map();
    for (const R of roads.values()) {
      for (const isStart of [true, false]) {
        const nodeId = isStart ? R.road.a : R.road.b;
        const n = R.dense.length;
        const maxPts = Math.min(n, Math.ceil(180 / DENSE_STEP));
        const idx = (i) => (isStart ? i : n - 1 - i);
        const center = [], toward = [], away = [], st = [];
        // Extend every edge straight back behind the node so oblique approaches of wide roads still find their crossing.
        const ext = Math.ceil((Math.max(R.lay.hwL, R.lay.hwR) * 4 + 6) / DENSE_STEP);
        const k0 = idx(0), k1 = idx(1);
        const back = V.normXZ(V.sub(R.dense[k0], R.dense[k1]));
        for (let i = ext; i >= 1; i--) {
          const d = i * DENSE_STEP;
          center.push(V.madd(R.dense[k0], back, d)); st.push(-d);
          const tw = isStart ? R.edgeR[k0] : R.edgeL[k0], aw = isStart ? R.edgeL[k0] : R.edgeR[k0];
          toward.push(V.madd(tw, back, d)); away.push(V.madd(aw, back, d));
        }
        const i0 = center.length;
        for (let i = 0; i < maxPts; i++) {
          const k = idx(i);
          center.push(R.dense[k]);
          st.push(isStart ? R.cum[k] : R.length - R.cum[k]);
          if (isStart) { toward.push(R.edgeR[k]); away.push(R.edgeL[k]); } else { toward.push(R.edgeL[k]); away.push(R.edgeR[k]); }
        }
        const dirOut = S.dirAtIndex(center, i0);
        const arm = { road: R, roadId: R.id, isStart, nodeId, center, toward, away, st, i0, dirOut, angle: V.heading(dirOut), hwToward: isStart ? R.lay.hwR : R.lay.hwL, hwAway: isStart ? R.lay.hwL : R.lay.hwR, trim: 0, corners: {} };
        R.arms[isStart ? 'a' : 'b'] = arm;
        if (!armsByNode.has(nodeId)) armsByNode.set(nodeId, []);
        armsByNode.get(nodeId).push(arm);
      }
    }
    // --- 3. corners per node
    for (const node of Object.values(project.nodes)) {
      const arms = (armsByNode.get(node.id) || []).sort((a, b) => a.angle - b.angle);
      const rec = { id: node.id, node, arms, corners: [], kind: arms.length === 0 ? 'orphan' : arms.length === 1 ? 'end' : 'junction', passThrough: false, diagnostics: [] };
      nodes.set(node.id, rec);
      if (arms.length < 2) continue;
      for (let i = 0; i < arms.length; i++) {
        const A = arms[i], B = arms[(i + 1) % arms.length];
        const gap = arms.length === 1 ? Math.PI * 2 : RF.angGap(A.angle, B.angle);
        const corner = { A, B, gap, straight: true, sTA: 0, sTB: 0, fTA: 0, fTB: 0, radius: 0 };
        rec.corners.push(corner);
        A.corners.toward = corner; B.corners.away = corner;
        if (gap > STRAIGHT_GAP || A === B) continue;
        const hit = S.polyIntersect(A.toward, B.away);
        if (!hit) continue;
        const fPA = hit.i + hit.t, fPB = hit.j + hit.u;
        const u1 = S.dirAtIndex(A.toward, fPA), u2 = S.dirAtIndex(B.away, fPB);
        const cosT = RF.clamp(V.dot(u1, u2), -1, 1);
        const theta = Math.acos(cosT); // angle between the edges beyond the intersection
        if (theta < 0.02) continue;
        let radius = node.radius ?? Math.min(A.road.prof.cornerRadius, B.road.prof.cornerRadius);
        // shallow divergence (< ~35°) is a gore, not a turning corner: a nose radius keeps the runout short and the gore a thin painted triangle
        if (theta < 0.6) radius = Math.min(radius, A.road.lay.hasCurb || B.road.lay.hasCurb ? 2.0 : 1.0);
        const minLen = Math.min(A.road.length, B.road.length);
        let t = radius / Math.tan(theta / 2);
        // available runout: do not consume more than 42% of the shorter road
        const sPA = S.interpArr(A.st, fPA), sPB = S.interpArr(B.st, fPB);
        const avail = Math.max(0.5, 0.42 * minLen - Math.max(sPA, sPB, 0));
        if (t > avail) {
          t = avail; radius = t * Math.tan(theta / 2);
          rec.diagnostics.push({ level: 'warn', message: `Corner radius reduced to ${radius.toFixed(1)} m between ${A.road.road.name || A.roadId} and ${B.road.road.name || B.roadId} (short approach).` });
        }
        const fTA = S.walk(A.toward, fPA, t), fTB = S.walk(B.away, fPB, t);
        const T1 = S.atIndex(A.toward, fTA), T2 = S.atIndex(B.away, fTB);
        const d1 = S.dirAtIndex(A.toward, fTA), d2 = S.dirAtIndex(B.away, fTB);
        const sweep = Math.PI - theta;
        const chord = V.distXZ(T1, T2);
        let h = (4 / 3) * Math.tan(sweep / 4) * radius;
        if (!isFinite(h) || h <= 0) h = chord / 3;
        h = Math.min(h, chord * 0.6);
        Object.assign(corner, { straight: false, P: hit.p, fPA, fPB, fTA, fTB, sTA: S.interpArr(A.st, fTA), sTB: S.interpArr(B.st, fTB), T1, T2, d1, d2, h, radius, theta, sweep });
      }
      // --- trims
      for (const a of arms) a.trim = Math.max(0, a.corners.toward?.sTA || 0, a.corners.away?.sTB || 0);
      if (arms.length === 2) {
        const [A, B] = arms;
        const widthDelta = Math.abs(A.hwToward - B.hwAway) + Math.abs(A.hwAway - B.hwToward);
        const bothStraight = rec.corners.every((c) => c.straight);
        if (bothStraight && widthDelta < 0.02 && Math.abs(RF.angGap(A.angle, B.angle) - Math.PI) < 0.05) { rec.passThrough = true; rec.kind = 'pass'; }
        else if (bothStraight) { const taper = Math.max(2.0, widthDelta * 4); A.trim = Math.max(A.trim, taper); B.trim = Math.max(B.trim, taper); rec.kind = 'transition'; }
      }
    }
    // --- 4. clamp trims against road length
    for (const R of roads.values()) {
      const a = R.arms.a, b = R.arms.b;
      let ta = a.trim, tb = b.trim;
      const minBody = 1.0;
      if (ta + tb > R.length - minBody) {
        const scale = Math.max(0, (R.length - minBody) / Math.max(ta + tb, 1e-6));
        ta *= scale; tb *= scale;
        diagnostics.push({ level: 'error', owner: R.id, message: `${R.road.name || R.id} is too short for its junction geometry (${R.length.toFixed(1)} m).` });
      }
      a.trim = ta; b.trim = tb; R.trimStart = ta; R.trimEnd = tb;
    }
    // --- 5. stationize roads between trims (features demand local refinement)
    for (const R of roads.values()) {
      const zones = [];
      const feats = R.road.features || {};
      for (const sb of feats.speedBumps || []) zones.push({ s0: sb.s - 2.5, s1: sb.s + 2.5, step: 0.25 });
      for (const ph of feats.potholes || []) zones.push({ s0: ph.s - ph.r * 1.6, s1: ph.s + ph.r * 1.6, step: Math.max(0.12, ph.r / 4) });
      for (const c of feats.construction || []) { zones.push({ s0: c.s0 - 0.5, s1: c.s0 + 0.5, step: 0.25 }); zones.push({ s0: c.s1 - 0.5, s1: c.s1 + 0.5, step: 0.25 }); }
      for (const rc of feats.railCrossings || []) zones.push({ s0: rc.s - 6, s1: rc.s + 6, step: 0.3 });
      const required = [R.trimStart, R.length - R.trimEnd];
      for (const sb of feats.speedBumps || []) required.push(sb.s - 1.8, sb.s + 1.8);
      for (const c of feats.construction || []) required.push(c.s0, c.s1);
      const st = S.stationize(R.dense, { minStep: opts.minStep ?? 0.5, maxStep: opts.maxStep ?? (R.lay.kind === 'highway' ? 4 : 2.5), tol: opts.tol ?? 0.008, zones, required: required.filter((x) => x > 0 && x < R.length) });
      // keep only stations within trims (inclusive)
      const s0 = R.trimStart, s1 = R.length - R.trimEnd;
      const samples = st.samples.filter((q) => q.s >= s0 - 1e-6 && q.s <= s1 + 1e-6);
      // guarantee exact endpoints
      const endSample = (s) => { const e = S.evalAt(R.dense, R.cum, s); const d = V.normXZ(e.d); return { p: e.p, d, n: V.perp(d), s, k: 0, slope: e.d[1] }; };
      if (!samples.length || Math.abs(samples[0].s - s0) > 1e-4) samples.unshift(endSample(s0));
      if (Math.abs(samples[samples.length - 1].s - s1) > 1e-4) samples.push(endSample(s1));
      // smooth end tangents with the dense curve
      samples[0].d = V.normXZ(S.evalAt(R.dense, R.cum, s0 + 0.01).d); samples[0].n = V.perp(samples[0].d);
      samples[samples.length - 1].d = V.normXZ(S.evalAt(R.dense, R.cum, s1 - 0.01).d); samples[samples.length - 1].n = V.perp(samples[samples.length - 1].d);
      R.samples = samples;
      R.bodyLength = s1 - s0;
      // end frames for junction stitching
      R.arms.a.endFrame = samples[0]; R.arms.b.endFrame = samples[samples.length - 1];
    }
    // --- 6. junction boundaries
    for (const rec of nodes.values()) {
      if (rec.kind !== 'junction' && rec.kind !== 'transition') continue;
      const arms = rec.arms;
      for (const arm of arms) {
        const f = arm.endFrame;
        // outward-facing frame at the arm end
        const sgn = arm.isStart ? 1 : -1;
        arm.endCenter = f.p;
        arm.endToward = V.madd(f.p, f.n, sgn * arm.hwToward * 1); // toward = +n_out ; n_out = sgn * n
        arm.endAway = V.madd(f.p, f.n, -sgn * arm.hwAway);
        arm.endDirOut = V.mul(f.d, -sgn); // pointing away from the junction
        arm.nOut = V.mul(f.n, sgn);
        arm.fTrim = S.walk(arm.center, arm.i0, arm.trim); // fractional index at the trim station
      }
      for (const c of rec.corners) {
        const A = c.A, B = c.B;
        const pts = [A.endToward];
        if (c.straight) {
          // along A.toward back to the node, then along B.away out to its end
          for (let i = Math.floor(A.fTrim); i >= A.i0; i--) pts.push(A.toward[i]);
          for (let i = B.i0; i <= Math.floor(B.fTrim); i++) pts.push(B.away[i]);
        } else {
          for (let i = Math.floor(A.fTrim); i > c.fTA; i--) pts.push(A.toward[i]);
          // fillet as cubic Bezier (tangent-continuous with both edges)
          const P0 = c.T1, P3 = c.T2, P1 = V.madd(c.T1, c.d1, -c.h), P2 = V.madd(c.T2, c.d2, -c.h);
          const segs = Math.max(6, Math.ceil((c.sweep * c.radius) / 0.4));
          for (let k = 0; k <= segs; k++) {
            const t = k / segs, mt = 1 - t;
            pts.push([
              mt * mt * mt * P0[0] + 3 * mt * mt * t * P1[0] + 3 * mt * t * t * P2[0] + t * t * t * P3[0],
              mt * P0[1] + t * P3[1],
              mt * mt * mt * P0[2] + 3 * mt * mt * t * P1[2] + 3 * mt * t * t * P2[2] + t * t * t * P3[2],
            ]);
          }
          for (let i = Math.ceil(c.fTB); i <= Math.floor(B.fTrim); i++) pts.push(B.away[i]);
        }
        pts.push(B.endAway);
        // de-duplicate consecutive near-identical points
        const clean = [pts[0]];
        for (let i = 1; i < pts.length; i++) if (V.distXZ(pts[i], clean[clean.length - 1]) > 1e-4) clean.push(pts[i]);
        if (clean.length === 1) clean.push(V.clone(pts[pts.length - 1]));
        c.poly = clean;
        c.cum = S.cumulativeXZ(clean);
        c.length = c.cum[c.cum.length - 1];
        c.side = S.resampleEqual(clean, CORNER_HALF_RES * 2);
      }
      for (const arm of arms) {
        const side = S.resampleEqual([arm.endAway, arm.endToward], END_HALF_RES * 2);
        const sgn = arm.isStart ? 1 : -1, R = arm.road, f = arm.endFrame;
        for (let k = 0; k < side.length; k++) { const u = k / (side.length - 1); const tArm = -arm.hwAway + (arm.hwAway + arm.hwToward) * u; side[k][1] = f.p[1] + surfaceOffset(R, f.s, tArm * sgn, opts.layers); }
        arm.endSide = side;
      }
      // boundary polygon (plan) for validation
      const boundary = [];
      for (let i = 0; i < arms.length; i++) { boundary.push(...arms[i].endSide.slice(0, -1)); boundary.push(...rec.corners[i].side.slice(0, -1)); }
      rec.boundary = boundary;
      rec.area = Math.abs(S.polygonArea(boundary));
      // area centroid of the boundary (plan) keeps patches well shaped in long merge wedges
      let cx = 0, cz = 0, cy = 0, aa = 0;
      for (let i = 0; i < boundary.length; i++) { const p = boundary[i], q = boundary[(i + 1) % boundary.length]; const w = p[0] * q[2] - q[0] * p[2]; aa += w; cx += (p[0] + q[0]) * w; cz += (p[2] + q[2]) * w; cy += p[1]; }
      cy /= boundary.length || 1;
      if (Math.abs(aa) > 1e-6) { cx /= 3 * aa; cz /= 3 * aa; } else { cx = rec.node.p[0]; cz = rec.node.p[2]; }
      if (!S.pointInPoly([cx, 0, cz], boundary)) { cx = rec.node.p[0]; cz = rec.node.p[2]; }
      rec.center = [cx, cy, cz];
      // junction category
      const minGap = Math.min(...rec.corners.map((c) => c.gap));
      rec.type = arms.length === 2 ? (rec.kind === 'transition' ? 'Transition' : 'Bend') : minGap < (40 * Math.PI) / 180 ? 'Merge' : arms.length === 3 ? 'T-junction' : arms.length === 4 ? 'Crossroads' : `${arms.length}-way`;
      rec.isHighway = arms.every((a) => a.road.lay.kind === 'highway');
      rec.signals = rec.node.control === 'signals' || (rec.node.control !== 'none' && rec.node.control !== 'priority' && arms.length >= 4 && !rec.isHighway && arms.some((a) => a.road.lay.driveLanes.length >= 3));
    }
    const net = { roads, nodes, diagnostics, handed, cornerHalfRes: CORNER_HALF_RES, endHalfRes: END_HALF_RES };
    for (const rec of nodes.values()) for (const d of rec.diagnostics) diagnostics.push({ ...d, owner: rec.id });
    return net;
  }

  /** Surface height offset function for a road (crown/crossfall + features) at station s and lateral t. */
  function surfaceOffset(R, s, t, layers) {
    const lay = R.lay; let y = 0;
    const cross = (layers?.crossfall ?? 0.02) * (lay.kind === 'highway' || lay.medianW > 0 ? 0.5 : 1);
    const hwSide = t >= 0 ? lay.hwR : lay.hwL;
    y += cross * Math.max(0, hwSide - Math.abs(t)); // edges sit at datum, crown rises towards the centreline
    const f = R.road.features || {};
    if (f.speedBumps) for (const sb of f.speedBumps) {
      const w = sb.w || 3.6, hgt = sb.h || 0.075;
      const dx = Math.abs(s - sb.s);
      if (dx < w / 2 && Math.abs(t) < lay.hwR + 0.01 && Math.abs(t) < lay.hwL + 0.01) { const x = dx / (w / 2); y += hgt * (1 - x * x) * (1 - 0.0 * x); }
    }
    if (f.potholes) for (const ph of f.potholes) {
      const d = Math.hypot((s - ph.s) / (ph.rs || ph.r), (t - ph.t) / (ph.rt || ph.r));
      if (d < 1) { const depth = ph.depth || 0.06; const prof = Math.pow(1 - d * d, 0.6); const jag = 1 + 0.25 * Math.sin(s * 7.3 + t * 5.1) * d; y -= depth * prof * jag; }
    }
    if (f.construction) for (const c of f.construction) if (s > c.s0 && s < c.s1) { const edge = Math.min(s - c.s0, c.s1 - s); y -= 0.05 * RF.smoothstep(0, 0.3, edge); }
    return y;
  }
  /** Which feature mask applies at (s,t): returns aux vector [damage, wet, lateral, flag]. */
  function surfaceAux(R, s, t) {
    const f = R.road.features || {};
    let damage = 0, flag = 0;
    if (f.potholes) for (const ph of f.potholes) { const d = Math.hypot((s - ph.s) / (ph.rs || ph.r), (t - ph.t) / (ph.rt || ph.r)); if (d < 1.35) damage = Math.max(damage, RF.clamp(1.3 - d, 0, 1)); }
    if (f.construction) for (const c of f.construction) if (s > c.s0 && s < c.s1) flag = 2;
    if (f.speedBumps) for (const sb of f.speedBumps) { const w = sb.w || 3.6; if (Math.abs(s - sb.s) < w / 2) flag = 3; }
    return [damage, 0, t, flag];
  }

  RF.network = { solve, nodeDegree, roadsAtNode, controlPoints, surfaceOffset, surfaceAux, CORNER_HALF_RES, END_HALF_RES };
})(globalThis.RF || (globalThis.RF = {}));
