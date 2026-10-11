/* RoadForge — road body, kerbs, footways, medians, markings and junction patch meshes (all quads). */
(function (RF) {
  'use strict';
  const V = RF.V, S = RF.spline, MB = RF.MeshBuilder, prim = RF.prim;
  const PAINT_LIFT = 0.004, SURF = 0.0;

  /** Interpolated frame at station s along a solved road (within its sample range). */
  function frameAt(R, s) {
    const sm = R.samples; const n = sm.length;
    if (s <= sm[0].s) return { p: sm[0].p, d: sm[0].d, n: sm[0].n, s: sm[0].s };
    if (s >= sm[n - 1].s) return { p: sm[n - 1].p, d: sm[n - 1].d, n: sm[n - 1].n, s: sm[n - 1].s };
    let lo = 0, hi = n - 1; while (hi - lo > 1) { const m = (lo + hi) >> 1; if (sm[m].s <= s) lo = m; else hi = m; }
    const a = sm[lo], b = sm[hi], t = (s - a.s) / Math.max(b.s - a.s, 1e-9);
    const d = V.normXZ(V.lerp(a.d, b.d, t));
    return { p: V.lerp(a.p, b.p, t), d, n: V.perp(d), s };
  }
  /** World position on the road surface at (s, t) including crown/features. */
  function surfPoint(R, s, t, layers, lift = 0) {
    const f = frameAt(R, s);
    const y = RF.network.surfaceOffset(R, s, t, layers);
    return [f.p[0] + f.n[0] * t, f.p[1] + y + lift, f.p[2] + f.n[2] * t];
  }

  /* ------------------------------------------------------------------ */
  /* Marking standards                                                    */
  /* ------------------------------------------------------------------ */
  const MARKING_STD = {
    eu: { name: 'Europe (CEN)', lineW: 0.12, centre: 'white', centreDash: [3, 6], laneDash: [3, 6], edgeW: 0.15, stopW: 0.5, crosswalk: 'zebra', zebraW: 0.5, zebraGap: 0.5, crossLen: 4.0, doubleCentre: false },
    us: { name: 'United States (MUTCD)', lineW: 0.1, centre: 'yellow', centreDash: [3, 9], laneDash: [3, 9], edgeW: 0.1, stopW: 0.6, crosswalk: 'continental', zebraW: 0.6, zebraGap: 0.6, crossLen: 3.0, doubleCentre: true },
    uk: { name: 'United Kingdom (TSRGD)', lineW: 0.1, centre: 'white', centreDash: [2, 7], laneDash: [2, 7], edgeW: 0.15, stopW: 0.4, crosswalk: 'zebra', zebraW: 0.5, zebraGap: 0.5, crossLen: 3.8, doubleCentre: false },
    za: { name: 'South Africa (SARTSM)', lineW: 0.1, centre: 'white', centreDash: [3, 9], laneDash: [3, 9], edgeW: 0.1, stopW: 0.5, crosswalk: 'zebra', zebraW: 0.5, zebraGap: 0.5, crossLen: 3.5, doubleCentre: false, yellowEdge: true },
  };

  function paintMat(color) { return color === 'yellow' ? 'paint_yellow' : 'paint_white'; }

  /** Paint a longitudinal line at lateral t (centre of the line), width w, between stations s0..s1, optional dash [on, off]. */
  function paintLine(b, R, t, w, s0, s1, dash, layers, phase = 0) {
    s0 = Math.max(s0, R.samples[0].s); s1 = Math.min(s1, R.samples[R.samples.length - 1].s);
    if (s1 - s0 < 0.05) return;
    const pieces = [];
    if (!dash) pieces.push([s0, s1]);
    else { const period = dash[0] + dash[1]; let s = s0 - ((s0 + phase) % period); while (s < s1) { const a = Math.max(s, s0), e = Math.min(s + dash[0], s1); if (e - a > 0.05) pieces.push([a, e]); s += period; } }
    for (const [a, e] of pieces) {
      const steps = Math.max(1, Math.ceil((e - a) / 1.0));
      const rowL = [], rowR = [];
      for (let k = 0; k <= steps; k++) {
        const s = a + ((e - a) * k) / steps;
        rowL.push(b.vertex(surfPoint(R, s, t - w / 2, layers, PAINT_LIFT), [s, 0], [0, 0, 0, 0]));
        rowR.push(b.vertex(surfPoint(R, s, t + w / 2, layers, PAINT_LIFT), [s, 1], [0, 0, 0, 0]));
      }
      b.strip(rowL, rowR, true);
    }
  }
  /** Transverse bar: from lateral t0..t1 at station s, length len along the road. */
  function paintBar(b, R, s, len, t0, t1, layers, subdiv = 2) {
    const rows = [];
    const n = Math.max(1, Math.ceil(Math.abs(t1 - t0) / 1.5));
    for (let k = 0; k <= subdiv; k++) {
      const ss = s + (len * k) / subdiv; const row = [];
      for (let j = 0; j <= n; j++) { const t = t0 + ((t1 - t0) * j) / n; row.push(b.vertex(surfPoint(R, ss, t, layers, PAINT_LIFT), [ss, t], [0, 0, 0, 0])); }
      rows.push(row);
    }
    b.grid(rows, true);
  }
  /** Turn arrow (straight / left / right / straight-left / straight-right) as quads, at station s (tail) in lane centre t, pointing along +dir. */
  function paintArrow(b, R, s, t, dir, kind, layers) {
    const L = 5.0, shaftW = 0.25, headW = 0.9, headL = 1.6;
    const P = (ds, dt) => surfPoint(R, s + ds * dir, t + dt, layers, PAINT_LIFT);
    const quadP = (p0, p1, p2, p3) => { const i = [p0, p1, p2, p3].map((p) => b.vertex(p, [p[0], p[2]], [0, 0, 0, 0])); if (dir > 0) b.quad(i[0], i[1], i[2], i[3]); else b.quad(i[3], i[2], i[1], i[0]); };
    const shaft = (tOff, len0, len1) => { quadP(P(len0, tOff - shaftW / 2), P(len0, tOff + shaftW / 2), P(len1, tOff + shaftW / 2), P(len1, tOff - shaftW / 2)); };
    const head = (tOff, base, forward = true) => {
      // kite made of two quads (keeps all-quad topology)
      const tip = P(base + (forward ? headL : -headL), tOff), bl = P(base, tOff - headW / 2), br = P(base, tOff + headW / 2), mid = P(base + (forward ? headL * 0.45 : -headL * 0.45), tOff);
      const bl2 = P(base, tOff - shaftW / 2), br2 = P(base, tOff + shaftW / 2);
      quadP(bl, br, mid, bl);
      if (forward) { quadP(bl, mid, tip, V.lerp(bl, tip, 0.5)); quadP(mid, br, V.lerp(br, tip, 0.5), tip); } else { quadP(br, mid, tip, V.lerp(br, tip, 0.5)); quadP(mid, bl, V.lerp(bl, tip, 0.5), tip); }
      void bl2; void br2;
    };
    const straight = kind.includes('straight'), left = kind.includes('left'), right = kind.includes('right');
    shaft(0, 0, straight ? L - headL : L * 0.55);
    if (straight) head(0, L - headL);
    const side = (sgn) => {
      const tOff = sgn * 0.9; const base = L * 0.45;
      // side branch: short lateral shaft then a head pointing sideways
      quadP(P(base, 0), P(base, tOff), P(base + shaftW, tOff), P(base + shaftW, 0));
      const tip = P(base + shaftW / 2, tOff + sgn * 0.9), b0 = P(base - 0.5, tOff), b1 = P(base + shaftW + 0.5, tOff), mid = P(base + shaftW / 2, tOff + sgn * 0.35);
      quadP(b0, b1, mid, V.lerp(b0, b1, 0.5)); quadP(b0, mid, tip, V.lerp(b0, tip, 0.5)); quadP(mid, b1, V.lerp(b1, tip, 0.5), tip);
    };
    // left/right in travel direction: for dir=+1 right is +n ; for dir=-1 right is -n
    if (left) side(-dir); if (right) side(dir);
  }
  /** Hatched area (chevrons/diagonals) between two lateral bounds over a station range. */
  function paintHatch(b, R, s0, s1, t0, t1, layers, spacing = 1.5, w = 0.15) {
    const span = t1 - t0;
    for (let s = s0; s < s1 - 0.2; s += spacing) {
      const steps = 3; const rowA = [], rowB = [];
      for (let k = 0; k <= steps; k++) { const f = k / steps; const ss = s + f * span * 0.9, tt = t0 + f * span; if (ss > s1) break; rowA.push(b.vertex(surfPoint(R, ss, tt, layers, PAINT_LIFT), [ss, tt], [0, 0, 0, 0])); rowB.push(b.vertex(surfPoint(R, ss + w * 1.4, tt, layers, PAINT_LIFT), [ss, tt], [0, 0, 0, 0])); }
      if (rowA.length > 1) b.strip(rowA, rowB, false);
    }
  }

  /* ------------------------------------------------------------------ */
  /* Road body                                                            */
  /* ------------------------------------------------------------------ */
  function buildRoad(R, net, layers, out) {
    const lay = R.lay, prof = R.prof, sm = R.samples, feats = R.road.features || {};
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    const curbMat = layers.curbMaterial || 'curb';
    const pavingMat = 'paving';
    const asphaltMat = R.road.surface === 'concrete' ? 'concrete_road' : 'asphalt';
    // lateral breakpoints
    const bpSet = new Set([-lay.hwL, lay.hwR]);
    for (const l of lay.lanes) { bpSet.add(l.t0); bpSet.add(l.t1); if (l.kind === 'drive' && l.t1 - l.t0 > 3.0) bpSet.add((l.t0 + l.t1) / 2); }
    if (lay.hasCurb) { bpSet.add(lay.hwR - 0.3); bpSet.add(-lay.hwL + 0.3); }
    for (const ph of feats.potholes || []) { const r = ph.rt || ph.r; for (const k of [-1.3, -1, -0.66, -0.33, 0, 0.33, 0.66, 1, 1.3]) { const t = ph.t + r * k; if (t > -lay.hwL + 0.05 && t < lay.hwR - 0.05) bpSet.add(Math.round(t * 1000) / 1000); } }
    const bps = [...bpSet].filter((t) => t >= -lay.hwL - 1e-6 && t <= lay.hwR + 1e-6).sort((a, b) => a - b);
    const excl = lay.medianKind === 'raised' ? [-lay.medianW / 2, lay.medianW / 2] : null;
    // asphalt grid
    const asphalt = get(asphaltMat);
    const rows = [];
    for (let i = 0; i < sm.length; i++) {
      const f = sm[i]; const row = [];
      for (const t of bps) {
        const y = RF.network.surfaceOffset(R, f.s, t, layers);
        const aux = RF.network.surfaceAux(R, f.s, t);
        row.push(asphalt.vertex([f.p[0] + f.n[0] * t, f.p[1] + y, f.p[2] + f.n[2] * t], [f.s, t], [aux[0], 0, aux[2], aux[3]]));
      }
      rows.push(row);
    }
    for (let i = 0; i < rows.length - 1; i++) for (let j = 0; j < bps.length - 1; j++) {
      if (excl && bps[j] >= excl[0] - 1e-6 && bps[j + 1] <= excl[1] + 1e-6) continue;
      asphalt.quad(rows[i][j], rows[i][j + 1], rows[i + 1][j + 1], rows[i + 1][j]);
    }
    // construction zones: milled surface + gravel patch handled via aux flag=2 in shader; add trench edges (small vertical drop)
    // kerbs & footways
    const edgeAt = (f, t) => { const y = RF.network.surfaceOffset(R, f.s, t, layers); return [f.p[0] + f.n[0] * t, f.p[1] + y, f.p[2] + f.n[2] * t]; };
    const sideStrip = (side) => {
      const sgn = side === 'R' ? 1 : -1, hw = side === 'R' ? lay.hwR : lay.hwL, sw = side === 'R' ? lay.sidewalkR : lay.sidewalkL;
      if (lay.hasCurb) {
        const curb = get(curbMat), ch = lay.curbH, cw = lay.curbW;
        const r0 = [], r1 = [], r2 = [], r3 = [];
        for (let i = 0; i < sm.length; i++) {
          const f = sm[i]; const e = edgeAt(f, sgn * hw);
          const o = (d, h) => [e[0] + f.n[0] * sgn * d, e[1] + h, e[2] + f.n[2] * sgn * d];
          // dropped kerb at driveways? keep constant; flag construction
          r0.push(curb.vertex(o(0, 0), [f.s, 0], [0, 0, 0, 0]));
          r1.push(curb.vertex(o(0.012, ch - 0.015), [f.s, ch], [0, 0, 0, 0]));
          r2.push(curb.vertex(o(0.03, ch), [f.s, ch + 0.02], [0, 0, 0, 0]));
          r3.push(curb.vertex(o(cw, ch), [f.s, ch + cw], [0, 0, 0, 0]));
        }
        curb.grid(side === 'R' ? [r0, r1, r2, r3] : [r3, r2, r1, r0], side !== 'R');
        if (sw > 0) {
          const pav = get(pavingMat); const pa = [], pb = [];
          for (let i = 0; i < sm.length; i++) {
            const f = sm[i]; const e = edgeAt(f, sgn * hw);
            const o = (d, h) => [e[0] + f.n[0] * sgn * d, e[1] + h, e[2] + f.n[2] * sgn * d];
            const aux = [0, 0, sgn * (hw + cw), 0];
            pa.push(pav.vertex(o(cw, ch), [f.s, 0], aux));
            // footway falls 2% towards the kerb (outer edge is higher)
            pb.push(pav.vertex(o(cw + sw, ch + sw * 0.02), [f.s, sw], [0, 0, sgn * (hw + cw + sw), 0]));
          }
          pav.strip(pa, pb, side === 'R');
          if (lay.verge > 0) {
            const g = get('grass'); const ga = [], gb = [];
            for (let i = 0; i < sm.length; i++) { const f = sm[i]; const e = edgeAt(f, sgn * hw); const o = (d, h) => [e[0] + f.n[0] * sgn * d, e[1] + h, e[2] + f.n[2] * sgn * d]; ga.push(g.vertex(o(cw + sw, ch + sw * 0.02), [f.s, 0])); gb.push(g.vertex(o(cw + sw + lay.verge, ch + sw * 0.02 - lay.verge * 0.03), [f.s, lay.verge])); }
            g.strip(ga, gb, side === 'R');
          }
        }
        // tie the footway back to the ground plane (embankment / cutting slope); suppressed on decks, walls and tunnels
        tieIn(sgn, hw, cw + sw + lay.verge, ch + sw * 0.02 - lay.verge * 0.03);
      } else {
        // no kerb: soft verge sloping away, then embankment (fill) or cut slope to ground level 0
        const g = get('grass'); const ga = [], gb = [];
        const verge = Math.max(lay.verge, 1.0);
        for (let i = 0; i < sm.length; i++) {
          const f = sm[i]; const e = edgeAt(f, sgn * hw);
          const o = (d, h) => [e[0] + f.n[0] * sgn * d, e[1] + h, e[2] + f.n[2] * sgn * d];
          ga.push(g.vertex(o(0, -0.02), [f.s, 0]));
          gb.push(g.vertex(o(verge, -0.02 - verge * 0.06), [f.s, verge]));
        }
        g.strip(ga, gb, side === 'R');
        tieIn(sgn, hw, verge, -0.02 - verge * 0.06);
      }
    };
    /** Slope strip from the outermost road element down (or up) to the ground plane; split into runs so decks/walls stay clean. */
    function tieIn(sgn, hw, dOuter, hOuter) {
      const g = get('grass'); const GY = RF.structures ? RF.structures.GROUND_Y : -0.02;
      const open = (f) => { if (!R.structure) return true; const k = R.structure.at(f.s); return k === 'ground' || k === 'embankment' || k === 'cut'; };
      let ra = [], rb = [], rc = [];
      const flush = () => { if (ra.length > 1) g.grid([ra, rb, rc], sgn > 0); ra = []; rb = []; rc = []; };
      for (let i = 0; i < sm.length; i++) {
        const f = sm[i];
        if (!open(f)) { flush(); continue; }
        const e = edgeAt(f, sgn * hw);
        const o = (d, h) => [e[0] + f.n[0] * sgn * d, e[1] + h, e[2] + f.n[2] * sgn * d];
        const yO = e[1] + hOuter; const dy = GY - yO; // signed drop to ground
        const run = Math.max(0.6, Math.abs(dy) * (dy < 0 ? 2.0 : 1.5)); // 1:2 fill slopes, 1:1.5 cut slopes
        ra.push(g.vertex(o(dOuter, hOuter), [f.s, 0]));
        rb.push(g.vertex(o(dOuter + run * 0.5, hOuter + dy * 0.5), [f.s, run * 0.5]));
        rc.push(g.vertex(o(dOuter + run, hOuter + dy), [f.s, run]));
      }
      flush();
    }
    sideStrip('R'); sideStrip('L');
    // raised median
    if (lay.medianKind === 'raised') {
      const curb = get(curbMat), pav = get(layers.medianSurface === 'grass' ? 'grass' : pavingMat), half = lay.medianW / 2, ch = lay.curbH || 0.125;
      for (const sgn of [1, -1]) {
        const r0 = [], r1 = [], r2 = [];
        for (let i = 0; i < sm.length; i++) { const f = sm[i]; const e = edgeAt(f, sgn * half); const o = (d, h) => [e[0] - f.n[0] * sgn * d, e[1] + h, e[2] - f.n[2] * sgn * d]; r0.push(curb.vertex(o(0, 0), [f.s, 0])); r1.push(curb.vertex(o(0.012, ch - 0.015), [f.s, ch])); r2.push(curb.vertex(o(0.15, ch), [f.s, ch + 0.15])); }
        curb.grid(sgn > 0 ? [r2, r1, r0] : [r0, r1, r2], sgn > 0);
      }
      const pa = [], pb = [];
      for (let i = 0; i < sm.length; i++) { const f = sm[i]; const yc = f.p[1] + RF.network.surfaceOffset(R, f.s, half, layers) + ch; pa.push(pav.vertex([f.p[0] - f.n[0] * (half - 0.15), yc + 0.01, f.p[2] - f.n[2] * (half - 0.15)], [f.s, 0], [0, 0, 50, 0])); pb.push(pav.vertex([f.p[0] + f.n[0] * (half - 0.15), yc + 0.01, f.p[2] + f.n[2] * (half - 0.15)], [f.s, lay.medianW], [0, 0, 50, 0])); }
      pav.strip(pa, pb, true);
    } else if (lay.medianKind === 'barrier') {
      // Jersey / step barrier profile (F-shape, 0.81 m high, 0.6 m base) swept along the centreline
      const bar = get('barrier');
      const hb = Math.min(lay.medianW / 2, 0.3);
      const profile = [[-hb, 0], [-hb, 0.075], [-hb + 0.055, 0.075], [-hb + 0.055 + 0.2, 0.33], [-hb + 0.055 + 0.2 + 0.03, 0.33], [-0.1, 0.81], [0.1, 0.81], [hb - 0.055 - 0.2 - 0.03, 0.33], [hb - 0.055 - 0.2, 0.33], [hb - 0.055, 0.075], [hb, 0.075], [hb, 0]];
      const frames = sm.map((f) => ({ p: [f.p[0], f.p[1] + RF.network.surfaceOffset(R, f.s, 0, layers), f.p[2]], d: f.d, n: f.n }));
      prim.sweep(bar, frames, profile, { uvScale: 1 });
    }
    buildRoadMarkings(R, net, layers, out);
    return out;
  }

  /* ------------------------------------------------------------------ */
  /* Markings                                                             */
  /* ------------------------------------------------------------------ */
  function buildRoadMarkings(R, net, layers, out) {
    if (layers.markings === false) return;
    const std = MARKING_STD[layers.markingStandard || 'eu'] || MARKING_STD.eu;
    const lay = R.lay, s0 = R.samples[0].s, s1 = R.samples[R.samples.length - 1].s;
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    const white = get('paint_white'), yellow = get('paint_yellow');
    const lanes = lay.driveLanes;
    const noMarkings = lay.kind === 'urban' && lanes.length <= 1; // single-lane service lanes
    if (noMarkings) return;
    const hasCurb = lay.hasCurb;
    // approach geometry: stop lines + crosswalk reservations at junction ends
    const nodeA = net.nodes.get(R.road.a), nodeB = net.nodes.get(R.road.b);
    const reserve = (rec, arm) => {
      if (!rec || (rec.kind !== 'junction')) return { cross: 0, stop: 0 };
      const urban = lay.kind === 'urban' && layers.crosswalks !== false && rec.arms.length >= 3;
      const cross = urban ? std.crossLen + 0.6 : 0;
      const incoming = lanes.some((l) => (arm.isStart ? l.dir < 0 : l.dir > 0));
      const stop = incoming && !rec.isHighway && rec.arms.length >= 3 ? std.stopW + 0.4 : 0;
      return { cross, stop, urban, incoming, rec };
    };
    const rA = reserve(nodeA, R.arms.a), rB = reserve(nodeB, R.arms.b);
    const lineStart = s0 + rA.cross + rA.stop, lineEnd = s1 - rB.cross - rB.stop;
    // centre line(s)
    const fwd = lanes.filter((l) => l.dir > 0), bwd = lanes.filter((l) => l.dir < 0);
    if (fwd.length && bwd.length && lay.medianKind !== 'raised' && lay.medianKind !== 'barrier') {
      const mat = std.centre === 'yellow' ? yellow : white;
      const tC = (Math.max(...bwd.map((l) => l.t1)) + Math.min(...fwd.map((l) => l.t0))) / 2;
      const solid = layers.noOvertaking || lanes.length >= 4;
      if (std.doubleCentre && solid) { paintLine(mat, R, tC - std.lineW * 0.75, std.lineW, lineStart, lineEnd, null, layers); paintLine(mat, R, tC + std.lineW * 0.75, std.lineW, lineStart, lineEnd, null, layers); }
      else paintLine(mat, R, tC, std.lineW, lineStart, lineEnd, solid ? null : std.centreDash, layers);
      if (lay.medianKind === 'painted' && lay.medianW > 0.6) { paintLine(mat, R, -lay.medianW / 2, std.lineW, lineStart, lineEnd, null, layers); paintLine(mat, R, lay.medianW / 2, std.lineW, lineStart, lineEnd, null, layers); paintHatch(mat, R, lineStart + 1, lineEnd - 1, -lay.medianW / 2 + 0.2, lay.medianW / 2 - 0.2, layers, 2.0); }
    }
    // lane lines (between same-direction lanes)
    const sameDir = (arr) => { arr.sort((a, b) => a.t0 - b.t0); for (let i = 0; i < arr.length - 1; i++) { const t = arr[i].t1; paintLine(white, R, t, std.lineW, lineStart, lineEnd, std.laneDash, layers); } };
    sameDir([...fwd]); sameDir([...bwd]);
    // edge lines: on highways/rural always; urban only when no kerb or wide roads
    const edgeLines = lay.kind !== 'urban' || !hasCurb;
    const shoulderMarks = lay.lanes.filter((l) => l.kind === 'shoulder');
    if (edgeLines) {
      for (const l of lay.lanes) {
        if (l.kind === 'shoulder') {
          const inner = Math.abs(l.t0) < Math.abs(l.t1) ? l.t0 : l.t1; // edge of travelled way
          const isLeftOfTravel = (l.dir > 0 && l.t1 <= 0.01) || (l.dir < 0 && l.t0 >= -0.01);
          const mat = std.yellowEdge && isLeftOfTravel ? yellow : white;
          paintLine(mat, R, inner, std.edgeW, lineStart, lineEnd, null, layers);
        }
      }
      if (!shoulderMarks.length) { paintLine(white, R, lay.hwR - std.edgeW, std.edgeW, lineStart, lineEnd, null, layers); paintLine(white, R, -lay.hwL + std.edgeW, std.edgeW, lineStart, lineEnd, null, layers); }
    }
    // cycle lanes: solid white separator + green? keep white line
    for (const l of lay.lanes) if (l.kind === 'cycle') paintLine(white, R, l.dir > 0 ? l.t0 : l.t1, std.lineW, lineStart, lineEnd, null, layers);
    // parking bays: T / L markings every 6 m
    if (layers.parkingBays !== false) for (const l of lay.lanes) if (l.kind === 'parking') {
      const inner = l.dir > 0 ? l.t0 : l.t1; // lane-side edge
      const sgn = l.t0 >= 0 ? 1 : -1;
      const start = lineStart + 10, end = lineEnd - 10; // keep 10 m clear of junction mouths
      paintLine(white, R, inner + sgn * 0.05, 0.1, start, end, null, layers);
      for (let s = start; s + 6 <= end; s += 6) { paintBar(white, R, s, 0.1, inner, inner + sgn * (l.t1 - l.t0) * 0.9, layers, 1); }
      R.parkingBays = R.parkingBays || [];
      for (let s = start; s + 6 <= end; s += 6) R.parkingBays.push({ s: s + 3, t: (l.t0 + l.t1) / 2, dir: l.dir, side: sgn });
    }
    // approach treatments
    const approach = (res, arm, atStart) => {
      if (!res.rec) return;
      const sEdge = atStart ? s0 : s1, dirIn = atStart ? -1 : 1; // dirIn = +1 when traffic approaching the node travels along +s
      const sgnS = atStart ? 1 : -1; // moving into the road body from the junction end
      let cursor = sEdge;
      if (res.cross) {
        // crosswalk occupies [sEdge, sEdge + crossLen] on the road body side
        const a = cursor + sgnS * 0.3, len = std.crossLen;
        const tL = -lay.hwL + 0.3, tR = lay.hwR - 0.3;
        const w = std.zebraW, g = std.zebraGap;
        const n = Math.floor((tR - tL + g) / (w + g));
        const margin = (tR - tL - n * (w + g) + g) / 2;
        for (let k = 0; k < n; k++) { const t0 = tL + margin + k * (w + g); paintBar(white, R, Math.min(a, a + sgnS * len), len, t0, t0 + w, layers, 3); }
        cursor += sgnS * (res.cross);
      }
      if (res.stop) {
        const inc = lanes.filter((l) => (atStart ? l.dir < 0 : l.dir > 0));
        if (inc.length) {
          const tA = Math.min(...inc.map((l) => l.t0)), tB = Math.max(...inc.map((l) => l.t1));
          const sStop = cursor + sgnS * 0.2;
          paintBar(white, R, Math.min(sStop, sStop + sgnS * std.stopW), std.stopW, tA, tB, layers, 1);
          // turn arrows on incoming lanes
          if (layers.turnArrows !== false && res.rec.arms.length >= 3 && R.bodyLength > 30) {
            inc.sort((a, b) => a.t0 - b.t0);
            const dirSign = atStart ? -1 : 1; // arrows point toward the junction
            // permitted movements from this arm: classify the other arms by turn angle relative to the inbound heading
            // (+ = geometric right turn in this x-east / z-south frame)
            const inbound = arm.angle + Math.PI;
            const avail = { left: false, straight: false, right: false };
            for (const o of res.rec.arms) { if (o === arm) continue; const turn = RF.angDiff(inbound, o.angle); if (Math.abs(turn) < (38 * Math.PI) / 180) avail.straight = true; else if (turn > 0) avail.right = true; else avail.left = true; }
            if (!avail.left && !avail.straight && !avail.right) avail.straight = true;
            const n = inc.length;
            const compose = (L, S, Rr) => { const parts = []; if (S) parts.push('straight'); if (L) parts.push('left'); if (Rr) parts.push('right'); return parts.join('-') || 'straight'; };
            inc.forEach((l, i) => {
              const tc = (l.t0 + l.t1) / 2; const tail = cursor + sgnS * 9;
              const li = dirSign > 0 ? i : n - 1 - i; // lane index counted from the left in the direction of travel
              let kind;
              if (n === 1) kind = compose(avail.left, avail.straight, avail.right);
              else if (li === 0) kind = compose(avail.left, avail.straight && (n === 2 || !avail.right || !avail.left), false);
              else if (li === n - 1) kind = compose(false, avail.straight, avail.right);
              else kind = compose(false, true, false);
              if (li === 0 && !avail.left) kind = compose(false, avail.straight, n === 2 && !avail.straight ? avail.right : false);
              paintArrow(white, R, tail, tc, dirSign, kind, layers);
            });
          }
        }
        cursor += sgnS * res.stop;
      }
    };
    approach(rA, R.arms.a, true); approach(rB, R.arms.b, false);
    // speed bump chevrons
    for (const sb of (R.road.features || {}).speedBumps || []) {
      const w = sb.w || 3.6; const tL = -lay.hwL + 0.3, tR = lay.hwR - 0.3; const n = Math.floor((tR - tL) / 1.0);
      for (let k = 0; k < n; k += 2) { const t0 = tL + k * 1.0; paintBar(white, R, sb.s - w * 0.45, w * 0.9, t0, t0 + 0.5, layers, 6); }
    }
    // construction zone: temporary yellow edge lines
    for (const c of (R.road.features || {}).construction || []) { paintLine(yellow, R, -lay.hwL + 0.5, 0.12, c.s0, c.s1, [1, 1], layers); paintLine(yellow, R, lay.hwR - 0.5, 0.12, c.s0, c.s1, [1, 1], layers); }
  }

  /* ------------------------------------------------------------------ */
  /* Junction                                                             */
  /* ------------------------------------------------------------------ */
  function buildJunction(rec, net, layers, out) {
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    const arms = rec.arms, N = arms.length;
    const hE = net.endHalfRes, hC = net.cornerHalfRes;
    const asphaltMat = arms.some((a) => a.road.road.surface === 'concrete') ? 'concrete_road' : 'asphalt';
    const asphalt = get(asphaltMat);
    // Sides in boundary order: end_0, corner_0, end_1, corner_1, ...
    const sides = [];
    for (let i = 0; i < N; i++) { sides.push({ pts: arms[i].endSide, h: hE, kind: 'end', arm: arms[i] }); sides.push({ pts: rec.corners[i].side, h: hC, kind: 'corner', corner: rec.corners[i] }); }
    const M = sides.length;
    const C = rec.center;
    // Centre vertex height: average of boundary; also apply the crown so water drains (slight dome 1%)
    const spokeTo = (pt, segs) => { const arr = []; for (let k = 0; k <= segs; k++) arr.push(V.lerp(C, pt, k / segs)); return arr; };
    const mid = (side) => side.pts[side.h];
    const dome = (p, u, v) => p;
    const auxFn = (p) => [0, 0, 60, 0];
    // One Coons patch per boundary vertex V_k (between side k-1 and side k)
    for (let k = 0; k < M; k++) {
      const prev = sides[(k - 1 + M) % M], cur = sides[k];
      const Vk = cur.pts[0];
      const cPrevHalf = prev.pts.slice(prev.h); // M_{k-1} -> V_k   (h_prev segments)
      const cCurHalf = cur.pts.slice(0, cur.h + 1); // V_k -> M_k     (h_cur segments)
      // patch corners: c0 = bottom (C -> M_{k-1}) with h_cur segs, c1 = right (M_{k-1} -> V_k) h_prev segs, c2 = top (M_k -> V_k reversed?) ...
      // Define patch param: u along (C->M_{k-1}) direction, v along (C->M_k).
      const c0 = spokeTo(mid(prev), cur.h);           // u: 0..1 at v=0  (C -> M_{k-1}), h_cur segments
      const c3 = spokeTo(mid(cur), prev.h);           // v: 0..1 at u=0  (C -> M_k),   h_prev segments
      const c1 = cPrevHalf;                           // right: M_{k-1} -> V_k (v 0..1), h_prev segs
      const c2 = cCurHalf.slice().reverse();          // top: at v=1 from M_k (u=0) -> V_k (u=1), h_cur segs
      // sanity: c0 has cur.h+1 pts, c2 has cur.h+1, c1 has prev.h+1, c3 has prev.h+1
      prim.coons(asphalt, c0, c1, c2, c3, { uvFn: (p) => [p[0], p[2]], auxFn, posFn: dome, flip: false });
    }
    // Corner kerbs & footways
    const curbMat = layers.curbMaterial || 'curb';
    for (const c of rec.corners) {
      const A = c.A, B = c.B;
      const layA = A.road.lay, layB = B.road.lay;
      const hasCurb = layA.hasCurb || layB.hasCurb;
      const swA = A.isStart ? layA.sidewalkR : layA.sidewalkL, swB = B.isStart ? layB.sidewalkL : layB.sidewalkR;
      const chA = layA.curbH, chB = layB.curbH;
      const poly = S.resampleEqual(c.poly, Math.max(2, Math.ceil(c.length / 0.45)));
      const n = poly.length;
      // outward normals; snap ends to the arm frames so the strips meet the road strips exactly
      const normals = poly.map((p, i) => {
        const a = poly[Math.max(0, i - 1)], b = poly[Math.min(n - 1, i + 1)];
        const d = V.normXZ(V.sub(b, a)); let nn = V.perp(d);
        // outward = away from junction centre
        if (V.dot(nn, V.sub(p, C)) < 0) nn = V.mul(nn, -1);
        return nn;
      });
      normals[0] = A.nOut; normals[n - 1] = V.mul(B.nOut, -1);
      const mixT = (i) => i / (n - 1);
      const urban = layA.kind === 'urban' || layB.kind === 'urban';
      if (hasCurb) {
        const curb = get(curbMat), pav = get('paving');
        const r0 = [], r1 = [], r2 = [], r3 = [], pa = [], pb = [];
        const dropLen = urban && layers.crosswalks !== false && rec.arms.length >= 3 ? 2.2 : 0;
        for (let i = 0; i < n; i++) {
          const p = poly[i], nn = normals[i], t = mixT(i);
          let ch = RF.lerp(chA, chB, t);
          const sw = RF.lerp(swA, swB, t);
          const along = c.cum ? (c.length * t) : 0;
          // dropped kerb ramps at both ends (crosswalk landings) with tactile zone
          let ramp = 0;
          if (dropLen > 0) { const dEnd = Math.min(along, c.length - along); ramp = 1 - RF.smoothstep(0.9, dropLen, dEnd); ch = Math.max(0.006, ch * (1 - ramp)); }
          const o = (d, h) => [p[0] + nn[0] * d, p[1] + h, p[2] + nn[2] * d];
          r0.push(curb.vertex(o(0, 0), [along, 0])); r1.push(curb.vertex(o(0.012, ch - 0.015), [along, ch])); r2.push(curb.vertex(o(0.03, ch), [along, ch + 0.02])); r3.push(curb.vertex(o(0.15, ch), [along, ch + 0.15]));
          const tact = ramp > 0.5 ? 1 : 0;
          pa.push(pav.vertex(o(0.15, ch), [along, 0], [0, 0, 60, tact]));
          pb.push(pav.vertex(o(0.15 + sw, RF.lerp(chA, chB, t) + sw * 0.02), [along, sw], [0, 0, 60, tact]));
        }
        curb.grid([r0, r1, r2, r3], true);
        if (swA > 0 || swB > 0) pav.strip(pa, pb, true);
      } else {
        const g = get('grass'); const ga = [], gb = [], gc = [];
        const verge = Math.max(layA.verge, layB.verge, 1.0);
        for (let i = 0; i < n; i++) {
          const p = poly[i], nn = normals[i];
          const o = (d, h) => [p[0] + nn[0] * d, p[1] + h, p[2] + nn[2] * d];
          ga.push(g.vertex(o(0, -0.02), [i, 0])); gb.push(g.vertex(o(verge, -0.02 - verge * 0.06), [i, verge]));
          const yEdge = p[1] - 0.02 - verge * 0.06; const drop = Math.max(0, yEdge); const run = drop * 2;
          gc.push(g.vertex(o(verge + Math.max(run, 0.001), -0.02 - verge * 0.06 - drop), [i, verge + run]));
        }
        g.grid([ga, gb, gc], true);
      }
    }
    // Highway merges: painted gore nose + chevrons
    if (rec.isHighway && layers.markings !== false) {
      const white = get('paint_white');
      for (const c of rec.corners) if (!c.straight && c.gap < (60 * Math.PI) / 180) {
        // gore: triangle-ish area between A.toward and B.away from P to the nose; paint edge lines along the fillet polyline
        const poly = S.resampleEqual(c.poly, Math.max(4, Math.ceil(c.length / 0.5)));
        const rowA = [], rowB = [];
        for (let i = 0; i < poly.length; i++) {
          const p = poly[i]; const a = poly[Math.max(0, i - 1)], b = poly[Math.min(poly.length - 1, i + 1)];
          let nn = V.perp(V.normXZ(V.sub(b, a))); if (V.dot(nn, V.sub(p, C)) > 0) nn = V.mul(nn, -1); // inward
          rowA.push(white.vertex([p[0] + nn[0] * 0.1, p[1] + PAINT_LIFT, p[2] + nn[2] * 0.1], [i, 0]));
          rowB.push(white.vertex([p[0] + nn[0] * 0.3, p[1] + PAINT_LIFT, p[2] + nn[2] * 0.3], [i, 1]));
        }
        white.strip(rowA, rowB, false);
      }
    }
    buildJunctionMarkings(rec, net, layers, out);
    return out;
  }

  /** Lateral offset list of painted longitudinal lines for an arm (in the arm's outward frame: +t = toward side). */
  function armLineOffsets(arm, std) {
    const lay = arm.road.lay, sgn = arm.isStart ? 1 : -1; // road +n maps to arm toward when isStart
    const lines = [];
    const lanes = lay.driveLanes, fwd = lanes.filter((l) => l.dir > 0), bwd = lanes.filter((l) => l.dir < 0);
    if (fwd.length && bwd.length && lay.medianKind !== 'raised' && lay.medianKind !== 'barrier') {
      const tC = (Math.max(...bwd.map((l) => l.t1)) + Math.min(...fwd.map((l) => l.t0))) / 2;
      const solid = lanes.length >= 4;
      if (std.doubleCentre && solid) { lines.push({ t: tC - std.lineW * 0.75, w: std.lineW, color: std.centre, dash: null }); lines.push({ t: tC + std.lineW * 0.75, w: std.lineW, color: std.centre, dash: null }); }
      else lines.push({ t: tC, w: std.lineW, color: std.centre, dash: solid ? null : std.centreDash });
    }
    for (const grp of [fwd, bwd]) { const arr = [...grp].sort((a, b) => a.t0 - b.t0); for (let i = 0; i < arr.length - 1; i++) lines.push({ t: arr[i].t1, w: std.lineW, color: 'white', dash: std.laneDash }); }
    const edgeLines = lay.kind !== 'urban' || !lay.hasCurb;
    if (edgeLines) {
      const sh = lay.lanes.filter((l) => l.kind === 'shoulder');
      for (const l of sh) lines.push({ t: Math.abs(l.t0) < Math.abs(l.t1) ? l.t0 : l.t1, w: std.edgeW, color: 'white', dash: null, edge: true });
      if (!sh.length) { lines.push({ t: lay.hwR - std.edgeW, w: std.edgeW, color: 'white', dash: null, edge: true }); lines.push({ t: -lay.hwL + std.edgeW, w: std.edgeW, color: 'white', dash: null, edge: true }); }
    }
    // convert to arm frame (toward = +)
    return lines.map((l) => ({ ...l, ta: l.t * sgn, tRoad: l.t }));
  }
  /** Paint a line along a cubic Bezier between two arm end frames (lateral offsets tA on arm A, tB on arm B, both in arm frames). */
  function paintBezierLine(b, A, tA, B, tB, w, dash, lift = PAINT_LIFT) {
    const pA = V.madd(A.endCenter, A.nOut, tA), pB = V.madd(B.endCenter, B.nOut, tB);
    const dist = V.distXZ(pA, pB); const h = dist / 3;
    const P1 = V.madd(pA, A.endDirOut, -h), P2 = V.madd(pB, B.endDirOut, -h);
    const pt = (t) => { const mt = 1 - t; return [mt * mt * mt * pA[0] + 3 * mt * mt * t * P1[0] + 3 * mt * t * t * P2[0] + t * t * t * pB[0], mt * pA[1] + t * pB[1] + lift, mt * mt * mt * pA[2] + 3 * mt * mt * t * P1[2] + 3 * mt * t * t * P2[2] + t * t * t * pB[2]]; };
    const segs = Math.max(4, Math.ceil(dist / 0.8));
    const pts = []; for (let k = 0; k <= segs; k++) pts.push(pt(k / segs));
    const cum = S.cumulativeXZ(pts), L = cum[segs];
    const pieces = []; if (!dash) pieces.push([0, L]); else { const per = dash[0] + dash[1]; for (let s = 0; s < L; s += per) pieces.push([s, Math.min(s + dash[0], L)]); }
    for (const [s0, s1] of pieces) {
      if (s1 - s0 < 0.05) continue; const n = Math.max(1, Math.ceil((s1 - s0) / 0.8)); const rl = [], rr = [];
      for (let k = 0; k <= n; k++) { const e = S.evalAt(pts, cum, s0 + ((s1 - s0) * k) / n); const nn = V.perp(V.normXZ(e.d)); rl.push(b.vertex(V.madd(e.p, nn, -w / 2), [k, 0])); rr.push(b.vertex(V.madd(e.p, nn, w / 2), [k, 1])); }
      b.strip(rl, rr, true);
    }
  }
  function buildJunctionMarkings(rec, net, layers, out) {
    if (layers.markings === false) return;
    const std = MARKING_STD[layers.markingStandard || 'eu'] || MARKING_STD.eu;
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    const arms = rec.arms;
    const connect = (A, B, scaleWidth) => {
      const la = armLineOffsets(A, std), lb = armLineOffsets(B, std);
      // A.toward side faces B.away side: a line at arm-frame offset ta on A continues at offset -ta' on B
      const wA = (A.hwToward + A.hwAway), wB = (B.hwToward + B.hwAway);
      for (const l of la) {
        const target = -l.ta * (scaleWidth ? wB / wA : 1);
        let best = null; for (const m of lb) { const d = Math.abs(m.ta - target); if (!!m.edge === !!l.edge && (!best || d < best.d)) best = { m, d }; }
        if (!best || best.d > 1.2) continue;
        paintBezierLine(get(paintMat(l.color)), A, l.ta, B, best.m.ta, l.w, l.dash && best.m.dash ? l.dash : null);
      }
    };
    if (arms.length === 2) { connect(arms[0], arms[1], true); return; }
    if (rec.isHighway) {
      // mutual-best through pairs (gap closest to 180°)
      const bestOf = (a) => { let best = null; for (const b of arms) { if (b === a) continue; const d = Math.abs(RF.angGap(a.angle, b.angle) - Math.PI); if (!best || d < best.d) best = { b, d }; } return best; };
      const through = new Set();
      for (const a of arms) { const ba = bestOf(a); if (ba && ba.d < (30 * Math.PI) / 180 && bestOf(ba.b)?.b === a && !through.has(a)) { connect(a, ba.b, false); through.add(a); through.add(ba.b); } }
      // merging / diverging arms: dashed auxiliary-lane line from the inner edge line to the nearest lane line of the continuing arm
      for (const m of arms) {
        if (through.has(m)) continue;
        let target = null; for (const t of arms) { if (t === m || !through.has(t)) continue; const d = V.dot(t.dirOut, m.dirOut); if (!target || d < target.d) target = { t, d }; }
        if (!target) continue;
        const T = target.t;
        const lm = armLineOffsets(m, std).filter((l) => l.edge), lt = armLineOffsets(T, std);
        // the inner edge is the one closest (in world) to the target arm's end centre
        let inner = null; for (const l of lm) { const p = V.madd(m.endCenter, m.nOut, l.ta); const d = V.distXZ(p, T.endCenter); if (!inner || d < inner.d) inner = { l, d }; }
        if (!inner) continue;
        const pIn = V.madd(m.endCenter, m.nOut, inner.l.ta);
        let bestLine = null; for (const l of lt) { if (l.edge) continue; const p = V.madd(T.endCenter, T.nOut, l.ta); const d = V.distXZ(p, pIn); if (!bestLine || d < bestLine.d) bestLine = { l, d }; }
        if (!bestLine) { for (const l of lt) { const p = V.madd(T.endCenter, T.nOut, l.ta); const d = V.distXZ(p, pIn); if (!bestLine || d < bestLine.d) bestLine = { l, d }; } }
        if (bestLine) paintBezierLine(get('paint_white'), m, inner.l.ta, T, bestLine.l.ta, std.lineW * 1.5, [3, 3]);
      }
    }
  }

  /** Dead-end cap: short closing strip so the road body is watertight at a cul-de-sac. */
  function buildEndCap(rec, net, layers, out) {
    const arm = rec.arms[0]; if (!arm) return out;
    const R = arm.road, lay = R.lay, f = arm.endFrame || (arm.isStart ? R.samples[0] : R.samples[R.samples.length - 1]);
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    if (!lay.hasCurb) return out;
    const curb = get(layers.curbMaterial || 'curb');
    const sgn = arm.isStart ? -1 : 1; // outward along d
    const ch = lay.curbH, left = -lay.hwL - lay.curbW, right = lay.hwR + lay.curbW;
    const r0 = [], r1 = [];
    for (let k = 0; k <= 8; k++) { const t = left + ((right - left) * k) / 8; const y = RF.network.surfaceOffset(R, f.s, RF.clamp(t, -lay.hwL, lay.hwR), layers); const base = [f.p[0] + f.n[0] * t, f.p[1] + y, f.p[2] + f.n[2] * t]; r0.push(curb.vertex(base, [t, 0])); r1.push(curb.vertex([base[0] + f.d[0] * sgn * 0.15, base[1] + ch, base[2] + f.d[2] * sgn * 0.15], [t, ch])); }
    curb.strip(r0, r1, sgn > 0);
    return out;
  }

  RF.roadmesh = { frameAt, surfPoint, buildRoad, buildJunction, buildEndCap, MARKING_STD, paintLine, paintBar, PAINT_LIFT };
})(globalThis.RF || (globalThis.RF = {}));
