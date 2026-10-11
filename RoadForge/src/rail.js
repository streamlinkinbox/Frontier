/* RoadForge — railways: ballasted track, level crossings, rail bridges, subway tunnels and moving trains.
 * Standard gauge 1435 mm, UIC60 rail (172 mm high, 72 mm head), concrete sleepers 2.6 × 0.26 × 0.22 at 600 mm centres,
 * ballast shoulder 0.5 m beyond sleeper ends with 1:1.5 side slopes. Subway: Ø 6.0 m bored tunnel lining.
 */
(function (RF) {
  'use strict';
  const V = RF.V, S = RF.spline, MB = RF.MeshBuilder, prim = RF.prim;
  const GAUGE = 1.435, RAIL_H = 0.172, SLEEPER = { w: 2.6, h: 0.22, d: 0.26, pitch: 0.6 };
  const GY = () => (RF.structures ? RF.structures.GROUND_Y : -0.02);

  function solve(rail) {
    // rail.pts y = rail-head level (so a level crossing road at the same y is flush with the rails); internal datum = sleeper top
    const pts = rail.pts.map((q) => [q[0], q[1] - RAIL_H + 0.02, q[2]]); // head sits 2 cm proud of a level-crossing road surface
    if (pts.length < 2) return null;
    let dense = S.samplePolyline(pts, null, null, 0.5);
    if (dense.length === 2) { const n = Math.max(2, Math.ceil(V.dist(dense[0], dense[1]) / 2)); const a = dense[0], b = dense[1]; dense = []; for (let i = 0; i <= n; i++) dense.push(V.lerp(a, b, i / n)); }
    const cum = S.cumulativeXZ(dense); const length = cum[cum.length - 1];
    const st = S.stationize(dense, { minStep: 0.5, maxStep: 2.0, tol: 0.006 });
    const kindAt = (y) => (rail.kind === 'subway' || y < -3.0 ? 'tunnel' : y > 4.5 ? 'bridge' : 'ground');
    const samples = st.samples; samples.forEach((f) => { f.kind = kindAt(f.p[1]); });
    return { id: rail.id, rail, dense, cum, length, samples };
  }

  /** Level-crossing / grade separation detection against road network. */
  function crossings(T, net) {
    const out = [];
    for (const R of net.roads.values()) {
      const hits = S.polyIntersectAll(T.dense, T.cum, R.dense, R.cum);
      for (const h of hits) {
        const sRail = h.sA, sRoad = h.sB;
        if (sRoad < R.trimStart + 2 || sRoad > R.length - R.trimEnd - 2) continue;
        const pRail = S.evalAt(T.dense, T.cum, sRail), pRoad = S.evalAt(R.dense, R.cum, sRoad);
        const dy = pRail.p[1] - pRoad.p[1];
        out.push({ road: R, sRail, sRoad, dy, p: pRail.p, dRail: V.normXZ(pRail.d), dRoad: V.normXZ(pRoad.d), level: Math.abs(dy) < 0.6 });
      }
    }
    return out;
  }

  function build(T, net, layers, out, ctx) {
    const get = (m) => (out[m] || (out[m] = new MB(m)));
    const sm = T.samples; if (sm.length < 2) return;
    const rails = get('rail'), ballast = get('ballast'), con = get('concrete'), steel = get('steel_painted'), lining = get('tunnel_wall'), panel = get('concrete_road');
    const xs = crossings(T, net);
    const levelZones = xs.filter((x) => x.level).map((x) => ({ s0: x.sRail - (x.road.lay.outerL + x.road.lay.outerR) / 2 - 1.0, s1: x.sRail + (x.road.lay.outerL + x.road.lay.outerR) / 2 + 1.0, x }));
    const inLevel = (s) => levelZones.find((z) => s > z.s0 && s < z.s1);
    const topOfSleeper = (f) => f.p[1] + (f.kind === 'bridge' ? 0.0 : 0.0); // rail datum = sleeper top
    // --- rails (continuous)
    const railProf = [[-0.075, 0], [0.075, 0], [0.075, 0.012], [0.012, 0.03], [0.012, 0.13], [0.036, 0.142], [0.036, RAIL_H], [-0.036, RAIL_H], [-0.036, 0.142], [-0.012, 0.13], [-0.012, 0.03], [-0.075, 0.012]];
    for (const side of [-1, 1]) {
      const fr = sm.map((f) => ({ p: V.add(V.madd(f.p, f.n, side * GAUGE / 2), [0, 0, 0]), d: f.d, n: f.n, up: [0, 1, 0] }));
      prim.sweep(rails, fr, railProf, { closed: true });
    }
    // --- sleepers (instanced) & ballast / deck / tunnel per run
    const sleeperParts = sleeperPart();
    let rec = ctx.placed.get('sleeper'); if (!rec) { rec = { parts: sleeperParts, instances: [], owners: [] }; ctx.placed.set('sleeper', rec); }
    let acc = 0;
    for (let i = 1; i < sm.length; i++) {
      const ds = sm[i].s - sm[i - 1].s; let s = sm[i - 1].s + (SLEEPER.pitch - acc);
      while (s <= sm[i].s) { if (!inLevel(s)) { const e = S.evalAt(T.dense, T.cum, s); const d = V.normXZ(e.d); rec.instances.push(RF.props.inst([e.p[0], e.p[1] - SLEEPER.h, e.p[2]], d)); rec.owners.push(T.id); } s += SLEEPER.pitch; }
      acc = (acc + ds) % SLEEPER.pitch;
    }
    // runs by kind
    const runs = []; for (const f of sm) { const z = inLevel(f.s); const k = z ? 'level' : f.kind; if (runs.length && runs[runs.length - 1].kind === k) runs[runs.length - 1].fr.push(f); else runs.push({ kind: k, fr: [f] }); }
    // make runs overlap by one sample so there are no gaps
    for (let i = 1; i < runs.length; i++) runs[i].fr.unshift(runs[i - 1].fr[runs[i - 1].fr.length - 1]);
    for (const run of runs) {
      const fr = run.fr.map((f) => ({ p: f.p, d: f.d, n: f.n, up: [0, 1, 0] }));
      if (fr.length < 2) continue;
      if (run.kind === 'ground') {
        // ballast prism: top at sleeper mid-height, shoulders 0.5 m beyond sleeper ends, slopes 1:1.5 to ground (y per sample)
        // Where the formation sits below the surrounding ground the track runs in a shallow drained trench (ground hole + grass verge back up).
        const rows = [[], [], [], [], [], [], []]; const hole = [[], []];
        for (const f of fr) {
          const yTop = f.p[1] - SLEEPER.h * 0.5, yG = GY();
          const above = yTop - yG;
          const o = (t, y) => ballast.vertex([f.p[0] + f.n[0] * t, y, f.p[2] + f.n[2] * t], [t, f.p[0] + f.p[2]]);
          const half = SLEEPER.w / 2 + 0.5;
          const run2 = Math.max(0.3, Math.abs(above)) * 1.5;      // ballast shoulder slope 1:1.5
          const yToe = above > 0 ? yG : yTop - 0.3;               // embankment toe at ground, or ditch bottom
          const verge = above > 0 ? 0 : (yG - yToe) * 2 + 0.3;    // grass batter 1:2 back up to the ground plane
          rows[0].push(o(-half - run2 - verge, yG)); rows[1].push(o(-half - run2, yToe)); rows[2].push(o(-half, yTop)); rows[3].push(o(0, yTop + 0.02)); rows[4].push(o(half, yTop)); rows[5].push(o(half + run2, yToe)); rows[6].push(o(half + run2 + verge, yG));
          if (above <= 0) { const w = half + run2 + verge - 0.05; hole[0].push([f.p[0] + f.n[0] * w, f.p[2] + f.n[2] * w]); hole[1].push([f.p[0] - f.n[0] * w, f.p[2] - f.n[2] * w]); }
        }
        ballast.grid(rows, false);
        if (hole[0].length > 1 && ctx.holes) ctx.holes.push([...hole[0], ...hole[1].reverse()]);
      } else if (run.kind === 'level') {
        // crossing panels flush with rail head: centre panel between rails, outer panels to the road edge of the ballast shoulder
        const top = RAIL_H - 0.01;
        prim.sweep(panel, fr, [[-GAUGE / 2 + 0.08, top], [GAUGE / 2 - 0.08, top], [GAUGE / 2 - 0.08, 0], [-GAUGE / 2 + 0.08, 0]], { closed: true, flip: false });
        for (const side of [-1, 1]) {
          const a = side * (GAUGE / 2 + 0.08), b = side * (GAUGE / 2 + 1.0);
          prim.sweep(panel, fr, side > 0 ? [[a, top], [b, top], [b, -0.1], [a, -0.1]] : [[b, top], [a, top], [a, -0.1], [b, -0.1]], { closed: true, flip: false });
        }
        const z = inLevel(run.fr[Math.floor(run.fr.length / 2)].s);
        if (z) placeCrossingFurniture(ctx, z.x, layers);
      } else if (run.kind === 'bridge') {
        // half-through steel plate-girder bridge: deck slab under the ballast, two 1.6 m girders outside the track, piers every 25 m
        const half = SLEEPER.w / 2 + 0.6;
        const ball = [[-half, -SLEEPER.h * 0.5], [-half, -SLEEPER.h - 0.1]];
        void ball;
        prim.sweep(ballast, fr, [[-half + 0.05, -SLEEPER.h * 0.5 - 0.0], [0, -SLEEPER.h * 0.4], [half - 0.05, -SLEEPER.h * 0.5]], { flip: false });
        prim.sweep(con, fr, [[-half, -SLEEPER.h * 0.5], [-half, -SLEEPER.h - 0.45], [half, -SLEEPER.h - 0.45], [half, -SLEEPER.h * 0.5]], { closed: true, flip: true });
        for (const side of [-1, 1]) {
          const t = side * (half + 0.3);
          prim.sweep(steel, fr, [[t - 0.3, 1.1], [t + 0.3, 1.1], [t + 0.3, 1.05], [t + 0.025, 1.05], [t + 0.025, -SLEEPER.h - 0.5 - 1.6], [t + 0.3, -SLEEPER.h - 0.5 - 1.6], [t + 0.3, -SLEEPER.h - 0.5 - 1.65], [t - 0.3, -SLEEPER.h - 0.5 - 1.65], [t - 0.3, -SLEEPER.h - 0.5 - 1.6], [t - 0.025, -SLEEPER.h - 0.5 - 1.6], [t - 0.025, 1.05], [t - 0.3, 1.05]], { closed: true, flip: false });
        }
        const L = run.fr[run.fr.length - 1].s - run.fr[0].s; const n = Math.max(1, Math.round(L / 25));
        for (let i = 1; i < n; i++) {
          const s = run.fr[0].s + (L * i) / n; const e = S.evalAt(T.dense, T.cum, s); const d = V.normXZ(e.d); const nn = V.perp(d);
          const ySoffit = e.p[1] - SLEEPER.h - 0.5 - 1.65; if (ySoffit - GY() < 1.5) continue;
          const M = RF.M4.fromBasis(nn, [0, 1, 0], d, [e.p[0], 0, e.p[2]]);
          prim.box(con, [2 * half + 1.2, 1.2, 1.4], { center: [0, ySoffit - 0.6, 0], transform: M, bevel: 0.06 });
          prim.lathe(con, [[0.7, GY() - 0.5], [0.7, ySoffit - 1.2]], 18, { transform: RF.M4.translation(e.p[0], 0, e.p[2]) });
        }
      } else if (run.kind === 'tunnel') {
        // circular lining Ø6 m with a concrete invert / walkway; inward-facing
        const R0 = 3.0, cy = 1.9; const prof = []; const n = 20;
        for (let i = 0; i <= n; i++) { const a = -0.25 * Math.PI + (1.5 * Math.PI * i) / n; prof.push([Math.cos(a) * R0, cy + Math.sin(a) * R0]); }
        prim.sweep(lining, fr, prof, { flip: true });
        prim.sweep(con, fr, [[-2.1, -0.4], [-2.1, 0.0], [-1.6, 0.0], [-1.6, -0.3], [1.6, -0.3], [1.6, 0.0], [2.1, 0.0], [2.1, -0.4]], { flip: false });
        prim.sweep(con, fr, [[-R0 - 0.35, cy - 2.2], [-R0 - 0.35, cy + R0 + 0.35], [R0 + 0.35, cy + R0 + 0.35], [R0 + 0.35, cy - 2.2]], { flip: true });
        // lighting strip
        const lamp = get('emissive_white');
        prim.sweep(lamp, fr, [[-0.1, cy + R0 - 0.25], [0.1, cy + R0 - 0.25], [0.1, cy + R0 - 0.32], [-0.1, cy + R0 - 0.32]], { closed: true });
      }
    }
    // portals where tunnel meets ground
    for (let i = 1; i < runs.length; i++) {
      const a = runs[i - 1], b = runs[i];
      if ((a.kind === 'tunnel') !== (b.kind === 'tunnel')) {
        const f = b.fr[0]; const M = RF.M4.fromBasis(f.n, [0, 1, 0], f.d, [f.p[0], f.p[1], f.p[2]]);
        prim.box(con, [8.5, 7.5, 0.8], { center: [0, 2.5, 0], transform: M });
      }
    }
    // overhead catenary masts for electrified lines (ground/bridge only)
    if (T.rail.electrified) {
      let last = -1e9; const mast = get('metal_galv');
      for (const f of sm) { if (f.kind === 'tunnel' || f.s - last < 55) continue; last = f.s; const p = V.madd(f.p, f.n, SLEEPER.w / 2 + 1.0); prim.lathe(mast, [[0.1, 0], [0.1, 7.0]], 8, { transform: RF.M4.translation(p[0], p[1] - SLEEPER.h, p[2]) }); const fr2 = [{ p: [p[0], p[1] + 6.0, p[2]], d: V.mul(f.n, -1), n: f.d, up: [0, 1, 0] }, { p: V.madd([p[0], p[1] + 5.7, p[2]], f.n, -(SLEEPER.w / 2 + 1.2)), d: V.mul(f.n, -1), n: f.d, up: [0, 1, 0] }]; prim.sweep(mast, fr2, [[-0.04, -0.04], [0.04, -0.04], [0.04, 0.04], [-0.04, 0.04]], { closed: true }); }
      // contact wire
      prim.sweep(get('metal_dark'), sm.filter((f) => f.kind !== 'tunnel').map((f) => ({ p: [f.p[0], f.p[1] + 5.3, f.p[2]], d: f.d, n: f.n, up: [0, 1, 0] })), [[-0.01, -0.01], [0.01, -0.01], [0.01, 0.01], [-0.01, 0.01]], { closed: true });
    }
  }
  function sleeperPart() {
    const key = 'sleeper';
    const b = new MB('concrete'); prim.box(b, [SLEEPER.w, SLEEPER.h, SLEEPER.d], { center: [0, SLEEPER.h / 2, 0] }); // plain box: thousands of instances, bevels would cost ~150 quads each
    const mesh = b.build(); void key;
    return [{ material: 'concrete', mesh }];
  }
  function placeCrossingFurniture(ctx, x, layers) {
    const R = x.road, lay = R.lay;
    // barrier + crossbuck on the right-hand approach side of each direction, 4 m before the track
    for (const dir of [1, -1]) {
      const inDir = V.mul(x.dRoad, dir); const right = V.perp(inDir);
      const s = x.sRoad - dir * 5.0; if (s < R.samples[0].s || s > R.samples[R.samples.length - 1].s) continue;
      const hwRight = dir > 0 ? lay.hwR : lay.hwL; const t = (hwRight + (lay.hasCurb ? lay.curbW + 0.6 : 1.0)) * dir;
      const p = RF.roadmesh.surfPoint(R, s, t, layers, lay.hasCurb ? lay.curbH : 0);
      const lanesW = Math.max(3, hwRight - (lay.medianW || 0) / 2);
      const parts = RF.props.get('crossingBarrier', Math.min(7, lanesW));
      const key = 'crossingBarrier:' + Math.min(7, lanesW);
      let rec = ctx.placed.get(key); if (!rec) { rec = { parts, instances: [], owners: [] }; ctx.placed.set(key, rec); }
      // local +x = arm direction = toward the road centre (−right); prop faces +z = inDir reversed so the arm swings over the lanes
      rec.instances.push(RF.props.inst(p, V.mul(inDir, -1))); rec.owners.push(R.id);
      void right;
    }
  }

  /* ---------------- trains ---------------- */
  function makeTrains(T, count) {
    const trains = []; const kind = T.rail.kind === 'subway' ? 'metro' : 'metro';
    const carL = 18.5, cars = T.rail.cars || 4;
    for (let i = 0; i < count; i++) trains.push({ s: (T.length * (i + 0.3)) / count, dir: i % 2 ? -1 : 1, v: 0, vmax: T.rail.kind === 'subway' ? 20 : 28, cars, carL, kind, color: T.rail.kind === 'subway' ? [0.75, 0.75, 0.78] : [0.55, 0.05, 0.05] });
    return trains;
  }
  /** Advance trains and return instance arrays per part key. Trains decelerate to stop at line ends, pause, reverse. */
  function updateTrains(T, trains, dt, placedInst) {
    for (const tr of trains) {
      const L = tr.cars * tr.carL + 2;
      const toEnd = tr.dir > 0 ? T.length - 10 - tr.s : tr.s - 10 - L;
      if (tr.wait > 0) { tr.wait -= dt; tr.v = 0; if (tr.wait <= 0) tr.dir *= -1; }
      else { const vAllowed = Math.min(tr.vmax, Math.sqrt(Math.max(0, 2 * 0.8 * toEnd))); tr.v += RF.clamp(vAllowed - tr.v, -1.2 * dt, 0.7 * dt) * 1; if (toEnd <= 0.2) { tr.wait = 12; tr.v = 0; } }
      tr.s += tr.v * dt * tr.dir;
      for (let c = 0; c < tr.cars; c++) {
        const sc = tr.s - tr.dir * (c * tr.carL + tr.carL / 2) + (tr.dir < 0 ? -L + tr.carL : 0) * 0 ;
        const sA = RF.clamp(sc + 7.5, 0, T.length), sB = RF.clamp(sc - 7.5, 0, T.length);
        const a = S.evalAt(T.dense, T.cum, sA).p, b = S.evalAt(T.dense, T.cum, sB).p;
        const d = V.norm(V.sub(a, b)); const mid = V.lerp(a, b, 0.5);
        const parts = RF.props.get('trainCar', tr.kind);
        for (const part of parts) {
          const key = 'train:' + tr.kind + ':' + part.material;
          let rec = placedInst.get(key); if (!rec) { rec = { parts: [part], instances: [], owners: [] }; placedInst.set(key, rec); }
          const m = RF.M4.frame(mid, d);
          const out = new Array(24); for (let i = 0; i < 16; i++) out[i] = m[i];
          out[16] = tr.color[0]; out[17] = tr.color[1]; out[18] = tr.color[2]; out[19] = part.material === 'carpaint' ? 1 : 0; out[20] = 1; out[21] = 0; out[22] = 0; out[23] = 0;
          rec.instances.push(out); rec.owners.push(T.id);
        }
      }
    }
  }

  RF.rail = { solve, build, crossings, makeTrains, updateTrains, GAUGE };
})(globalThis.RF || (globalThis.RF = {}));
