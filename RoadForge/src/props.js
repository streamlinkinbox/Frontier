/* RoadForge — street furniture & vehicle library (procedural, quad meshes) and placement rules.
 *
 * Every prop is a set of parts (material → mesh) built once and instanced. Placement returns per-part instance
 * lists (mat4 + colour + ext) so the whole network's furniture is a handful of draw calls.
 *
 * Dimensions follow common practice: 10 m / 8 m lighting columns with 1.5 m outreach, 4.5 m signal poles with
 * 4–7 m mast arms and 300 mm aspects, Ø114 mm steel bollards 1.0 m high, 0.73 m W-beam guardrail on 2 m posts,
 * 1.1 m pedestrian guardrail panels, 12 × 4 m billboard faces.
 */
(function (RF) {
  'use strict';
  const V = RF.V, M4 = RF.M4, MB = RF.MeshBuilder, prim = RF.prim, S = RF.spline;
  const cache = new Map();

  const circle = (r, n) => { const p = []; for (let i = 0; i < n; i++) { const a = (i / n) * Math.PI * 2; p.push([Math.cos(a) * r, Math.sin(a) * r]); } return p; };
  const arcFrames = (c, r, a0, a1, n, axis) => { const fr = []; for (let i = 0; i <= n; i++) { const a = a0 + (a1 - a0) * (i / n); const p = [c[0] + Math.cos(a) * r, c[1] + Math.sin(a) * r, c[2]]; const d = [-Math.sin(a), Math.cos(a), 0]; fr.push({ p, d, n: [0, 0, 1], up: V.cross([0, 0, 1], d) }); } void axis; return fr; };

  /* ---------------- part builders (local space: +z = forward / facing, +y up, origin at ground) ---------------- */
  const LIB = {
    streetlight(h = 10) {
      const pole = new MB('metal_dark'), lum = new MB('metal_dark'), lens = new MB('emissive_warm');
      // tapered column with base flange & door, 1.5 m outreach bending to horizontal
      prim.lathe(pole, [[0.17, 0], [0.17, 0.05], [0.09, 0.08], [0.09, 0.9], [0.085, 0.9], [0.06, h - 1.5]], 14, { capTop: false });
      // outreach: quarter arc from vertical to horizontal, radius 1.0, then straight 0.6 m
      const fr = []; const R = 1.0, n = 8;
      for (let i = 0; i <= n; i++) { const a = (i / n) * (Math.PI / 2); fr.push({ p: [0, h - 1.5 + Math.sin(a) * R, R - Math.cos(a) * R], d: [0, Math.cos(a), Math.sin(a)], n: [1, 0, 0], up: [0, Math.sin(a) * 1, -Math.cos(a)] }); }
      fr.push({ p: [0, h - 0.5, R + 0.6], d: [0, 0, 1], n: [1, 0, 0], up: [0, 1, 0] });
      prim.sweep(pole, fr, circle(0.045, 10), { closed: true });
      // luminaire (cobra-head style) with lens underneath
      const T = M4.trs([0, h - 0.5, R + 0.95], 0);
      prim.box(lum, [0.32, 0.14, 0.75], { center: [0, 0, 0], transform: T, bevel: 0.04 });
      prim.box(lens, [0.24, 0.02, 0.55], { center: [0, -0.075, 0.05], transform: T });
      return [pole, lum, lens];
    },
    signal(armLen = 5, heads = 2) {
      const pole = new MB('metal_dark'), head = new MB('metal_dark'), r = new MB('signal_red'), a = new MB('signal_amber'), g = new MB('signal_green');
      prim.lathe(pole, [[0.16, 0], [0.16, 0.04], [0.08, 0.06], [0.08, 4.6], [0.0, 4.6]], 14);
      // mast arm: tapered horizontal tube with a short gusset riser
      const fr = [{ p: [0, 4.4, 0], d: [1, 0, 0], n: [0, 0, 1], up: [0, 1, 0] }, { p: [armLen, 4.6, 0], d: [1, 0, 0], n: [0, 0, 1], up: [0, 1, 0] }];
      prim.sweep(pole, fr, circle(0.055, 10), { closed: true });
      prim.box(pole, [0.06, 0.4, 0.06], { center: [0.6, 4.75, 0] });
      const headAt = (x, y, scale = 1) => {
        const w = 0.34 * scale, hh = 1.05 * scale, dd = 0.3 * scale, T = M4.trs([x, y, 0], 0);
        prim.box(head, [w, hh, dd], { center: [0, 0, 0], transform: T, bevel: 0.02 });
        const lensR = 0.13 * scale;
        [[r, 0.33], [a, 0], [g, -0.33]].forEach(([b, oy]) => {
          prim.lathe(b, [[lensR * 0.3, 0], [lensR, 0.0], [lensR, 0.03]], 16, { transform: M4.mul(T, M4.trs([0, oy * scale, dd / 2], 0, 1, Math.PI / 2)), capTop: true });
          // visor hood
          const hood = [[-lensR - 0.02, 0], [-lensR - 0.02, 0.22], [lensR + 0.02, 0.22], [lensR + 0.02, 0]];
          const hf = []; for (let i = 0; i <= 6; i++) { const t = Math.PI * (i / 6); hf.push({ p: [Math.cos(t) * (lensR + 0.03), oy * scale + Math.sin(t) * (lensR + 0.03) + 0.0, dd / 2], d: [-Math.sin(t), Math.cos(t), 0], n: [0, 0, 1], up: [Math.cos(t), Math.sin(t), 0] }); }
          prim.sweep(head, hf.map((f) => ({ p: M4.transformPoint(T, f.p), d: f.d, n: f.n, up: f.up })), [[0, 0], [0.2, 0.0], [0.2, 0.02], [0, 0.02]], { flip: false });
          void hood;
        });
      };
      for (let i = 0; i < heads; i++) headAt(armLen - 0.6 - i * 3.5, 4.0);
      headAt(0.0, 3.2, 0.8); // pole-mounted secondary head
      return [pole, head, r, a, g];
    },
    bollard(kind) {
      const b = new MB(kind === 'concrete' ? 'concrete' : kind === 'wood' ? 'wood' : kind === 'cast' ? 'cast_iron' : kind === 'striped' ? 'plastic_white' : kind === 'sphere' ? 'granite' : 'metal_dark');
      if (kind === 'steel' || kind === 'striped') prim.lathe(b, [[0.057, 0], [0.057, 0.88], [0.05, 0.93], [0.03, 0.98], [0, 1.0]], 14);
      else if (kind === 'cast') prim.lathe(b, [[0.09, 0], [0.09, 0.08], [0.075, 0.1], [0.075, 0.6], [0.095, 0.66], [0.095, 0.72], [0.07, 0.78], [0.07, 0.86], [0.1, 0.9], [0.1, 0.94], [0.05, 1.0], [0, 1.02]], 16);
      else if (kind === 'concrete') prim.lathe(b, [[0.19, 0], [0.19, 0.42], [0.17, 0.5], [0.09, 0.56], [0, 0.57]], 18);
      else if (kind === 'wood') prim.box(b, [0.15, 1.0, 0.15], { center: [0, 0.5, 0], bevel: 0.02 });
      else if (kind === 'sphere') prim.lathe(b, [[0.001, 0], [0.2, 0.05], [0.3, 0.3], [0.2, 0.55], [0.001, 0.6]], 16);
      return [b];
    },
    railingPanel() {
      // 2.0 m pedestrian guardrail: 2 posts, top/bottom rails, 11 verticals (RHS 40×40 / tubes Ø25)
      const m = new MB('metal_galv');
      for (const x of [-0.98, 0.98]) prim.box(m, [0.06, 1.1, 0.06], { center: [x, 0.55, 0] });
      for (const y of [1.05, 0.2]) prim.box(m, [1.9, 0.06, 0.05], { center: [0, y, 0] });
      for (let i = 0; i < 11; i++) prim.box(m, [0.025, 0.85, 0.025], { center: [-0.8 + (1.6 * i) / 10, 0.62, 0] });
      return [m];
    },
    guardrailPost() {
      const m = new MB('metal_galv');
      prim.box(m, [0.1, 1.1, 0.15], { center: [0, 0.3, 0] }); // C-post, 0.5 m buried… shown from -0.25
      prim.box(m, [0.14, 0.3, 0.2], { center: [0.12, 0.6, 0] }); // blockout
      return [m];
    },
    billboard() {
      const pole = new MB('metal_dark'), face = new MB('billboard'), frame = new MB('metal_dark'), walk = new MB('metal_galv'), lamp = new MB('emissive_white');
      prim.lathe(pole, [[0.5, 0], [0.5, 0.1], [0.4, 0.15], [0.4, 9.5], [0.3, 11.0]], 18);
      const T = M4.trs([0, 11.5, 0], 0);
      prim.box(frame, [12.4, 4.4, 0.35], { transform: T, bevel: 0.03 });
      // face panel: 1 quad with uv 0..1
      const q = [[-6, -2, 0.18], [6, -2, 0.18], [6, 2, 0.18], [-6, 2, 0.18]].map((p, i) => face.vertex(M4.transformPoint(T, p), [[0, 0], [1, 0], [1, 1], [0, 1]][i]));
      face.quad(q[0], q[1], q[2], q[3]);
      prim.box(walk, [12.6, 0.06, 0.8], { center: [0, 9.1, 0.55] });
      prim.box(walk, [12.6, 0.04, 0.04], { center: [0, 10.1, 0.95] });
      for (let i = 0; i < 4; i++) { prim.box(frame, [0.04, 0.04, 1.0], { center: [-4.5 + 3 * i, 13.9, 0.6] }); prim.box(lamp, [0.5, 0.08, 0.2], { center: [-4.5 + 3 * i, 13.85, 1.05] }); }
      return [pole, face, frame, walk, lamp];
    },
    sign(tile, shape = 'disc', h = 2.2) {
      const pole = new MB('metal_galv'), face = new MB('sign_face'); face.tile = tile;
      prim.lathe(pole, [[0.038, 0], [0.038, h + 0.4]], 10);
      let pts;
      if (shape === 'octagon') pts = circle(0.4, 8).map((p) => [p[0] * 1.0, p[1]]);
      else if (shape === 'triangle') pts = [[-0.45, 0], [0.45, 0], [0, -0.78]];
      else if (shape === 'cross') pts = null;
      else pts = circle(0.3, 16);
      if (pts) {
        const c = face.vertex([0, h, 0.05], [0.5, 0.5]);
        const idx = pts.map((p) => face.vertex([p[0], h + p[1] - (shape === 'triangle' ? -0.26 : 0), 0.05], [0.5 + p[0] / (shape === 'triangle' ? 1 : 0.8), 0.5 + p[1] / (shape === 'triangle' ? 1 : 0.8)]));
        for (let i = 0; i < idx.length; i++) { const a = idx[i], b = idx[(i + 1) % idx.length]; const m = face.vertex(V.lerp([pts[i][0], h + pts[i][1], 0.05], [pts[(i + 1) % idx.length][0], h + pts[(i + 1) % idx.length][1], 0.05], 0.5), [0.5, 0.5]); face.quad(a, m, c, m === c ? c : face.vertex([pts[i][0] * 0.5, h + pts[i][1] * 0.5, 0.05], [0.5 + pts[i][0] * 0.6, 0.5 + pts[i][1] * 0.6])); }
      } else {
        // crossbuck: two 1.2 × 0.2 boards at ±45°
        for (const a of [Math.PI / 4, -Math.PI / 4]) prim.box(face, [1.2, 0.22, 0.03], { transform: M4.mul(M4.trs([0, h, 0.05], 0), M4.trs([0, 0, 0], 0, 1, 0, a)) });
      }
      return [pole, face];
    },
    cone() {
      const c = new MB('plastic_orange');
      prim.lathe(c, [[0.2, 0], [0.2, 0.03], [0.12, 0.05], [0.04, 0.72], [0, 0.73]], 12, { capTop: true });
      return [c];
    },
    barrierBoard() {
      const w = new MB('plastic_white'), r = new MB('plastic_red'), legs = new MB('metal_galv');
      prim.box(w, [2.0, 0.25, 0.03], { center: [0, 1.0, 0] }); prim.box(r, [2.0, 0.25, 0.03], { center: [0, 0.75, 0] });
      prim.box(w, [2.0, 0.25, 0.03], { center: [0, 0.5, 0] }); prim.box(r, [2.0, 0.25, 0.03], { center: [0, 0.25, 0] });
      for (const x of [-0.9, 0.9]) { prim.box(legs, [0.05, 1.2, 0.05], { center: [x, 0.6, 0] }); prim.box(legs, [0.05, 0.05, 0.6], { center: [x, 0.025, 0] }); }
      return [w, r, legs];
    },
    crossingBarrier(len = 5) {
      const base = new MB('metal_dark'), arm = new MB('plastic_white'), bands = new MB('plastic_red'), lamp = new MB('signal_red');
      prim.box(base, [0.5, 1.1, 0.5], { center: [0, 0.55, 0], bevel: 0.03 });
      prim.box(base, [0.3, 0.3, 0.3], { center: [0, 1.25, 0] });
      prim.box(arm, [len, 0.12, 0.05], { center: [len / 2 - 0.3, 1.25, 0.2] });
      for (let i = 0; i < Math.floor(len / 1.0); i++) if (i % 2 === 1) prim.box(bands, [0.5, 0.125, 0.052], { center: [i * 1.0 - 0.05, 1.25, 0.2] });
      // mast with twin flashing lights + crossbuck
      prim.lathe(base, [[0.06, 0], [0.06, 3.6]], 10, { transform: M4.translation(0, 0, -0.45) });
      for (const x of [-0.3, 0.3]) prim.lathe(lamp, [[0.03, 0], [0.11, 0], [0.11, 0.03]], 14, { transform: M4.mul(M4.trs([x, 2.6, -0.3], 0), M4.trs([0, 0, 0], 0, 1, Math.PI / 2)), capTop: true });
      prim.box(base, [0.9, 0.08, 0.06], { center: [0, 2.6, -0.42] });
      return [base, arm, bands, lamp];
    },
    /* vehicles: origin at ground under the rear axle centre?, +z forward; lengths real-world */
    vehicle(kind) {
      const body = new MB('carpaint'), glass = new MB('glass'), tyre = new MB('rubber'), rim = new MB('chrome'), trim = new MB('plastic_black'), head = new MB('headlight'), tail = new MB('taillight');
      const D = { sedan: { L: 4.7, W: 1.84, H: 1.45, wb: 2.8, cabin: [0.55, 0.42], hood: 0.95 }, hatch: { L: 4.1, W: 1.78, H: 1.5, wb: 2.6, cabin: [0.45, 0.3], hood: 0.85 }, suv: { L: 4.8, W: 1.95, H: 1.75, wb: 2.85, cabin: [0.5, 0.35], hood: 1.0 }, van: { L: 5.4, W: 2.05, H: 2.3, wb: 3.3, cabin: [0.9, 0.0], hood: 0.6 }, truck: { L: 8.5, W: 2.5, H: 3.4, wb: 4.8, cabin: [0.0, 0.0], hood: 0.0 }, bus: { L: 12.0, W: 2.55, H: 3.2, wb: 6.0, cabin: [0, 0], hood: 0 } }[kind] || null;
      const L = D.L, W = D.W, H = D.H, clearance = kind === 'suv' || kind === 'truck' ? 0.28 : 0.17;
      const z0 = -L / 2, z1 = L / 2;
      if (kind === 'truck') {
        // cab + box body
        prim.box(body, [W, 2.2, 2.2], { center: [0, clearance + 1.1 + 0.1, z1 - 1.1], bevel: 0.12 });
        prim.box(glass, [W - 0.2, 0.9, 0.06], { center: [0, clearance + 1.8, z1 + 0.0], bevel: 0.0 });
        const bx = new MB('plastic_white'); prim.box(bx, [W, H - clearance - 0.4, L - 2.6], { center: [0, clearance + 0.4 + (H - clearance - 0.4) / 2, z0 + (L - 2.6) / 2], bevel: 0.05 });
        prim.box(trim, [W - 0.3, 0.35, L - 2.4], { center: [0, clearance + 0.25, z0 + (L - 2.4) / 2 + 0.1] });
        wheels(tyre, rim, W, clearance, [z1 - 1.6, z0 + 1.6, z0 + 2.9], 0.5, 0.3);
        lights(head, tail, W, clearance + 1.0, z1 - 0.02, z0 + 0.02, 0.3);
        return [body, glass, tyre, rim, trim, head, tail, bx];
      }
      if (kind === 'bus') {
        prim.box(body, [W, H - clearance - 0.35, L], { center: [0, clearance + 0.35 + (H - clearance - 0.35) / 2, 0], bevel: 0.18 });
        prim.box(glass, [W + 0.02, 1.2, L - 1.0], { center: [0, clearance + 1.9, 0] });
        prim.box(glass, [W - 0.3, 1.5, 0.06], { center: [0, clearance + 1.8, z1 + 0.0] });
        prim.box(trim, [W - 0.2, 0.3, L - 0.6], { center: [0, clearance + 0.2, 0] });
        wheels(tyre, rim, W, clearance, [z1 - 2.6, z0 + 2.4], 0.5, 0.3);
        lights(head, tail, W, clearance + 0.6, z1 - 0.02, z0 + 0.02, 0.3);
        return [body, glass, tyre, rim, trim, head, tail];
      }
      // passenger car: lower body shell + cabin (greenhouse) with glass band
      const beltY = clearance + 0.55 + (kind === 'suv' ? 0.15 : 0);
      prim.box(body, [W, beltY - clearance, L], { center: [0, clearance + (beltY - clearance) / 2, 0], bevel: 0.12 });
      const cabL = L - D.hood - D.cabin[0] - D.cabin[1] - 0.3, cabZ = z0 + D.cabin[1] + 0.15 + cabL / 2 + (kind === 'van' ? 0.0 : 0.0);
      const roofH = H - beltY;
      // greenhouse as a tapered loft: 4 rings (belt → roof) approximated with two stacked bevelled boxes (glass band + roof)
      prim.box(glass, [W - 0.12, roofH * 0.62, cabL], { center: [0, beltY + roofH * 0.31, cabZ], bevel: 0.1 });
      prim.box(body, [W - 0.26, roofH * 0.42, cabL - 0.5], { center: [0, beltY + roofH * 0.79, cabZ], bevel: 0.1 });
      // pillars/trim
      prim.box(trim, [W - 0.1, 0.05, cabL], { center: [0, beltY + 0.02, cabZ] });
      prim.box(trim, [W - 0.4, 0.22, 0.12], { center: [0, clearance + 0.25, z1 - 0.05] }); // grille
      prim.box(trim, [W, 0.18, 0.15], { center: [0, clearance + 0.12, z1 - 0.06] }); // front bumper
      prim.box(trim, [W, 0.18, 0.15], { center: [0, clearance + 0.12, z0 + 0.06] });
      wheels(tyre, rim, W, clearance, [z1 - (L - D.wb) / 2, z0 + (L - D.wb) / 2], kind === 'suv' ? 0.37 : 0.32, 0.22);
      lights(head, tail, W, clearance + 0.5, z1 - 0.01, z0 + 0.01, 0.15);
      // mirrors
      for (const x of [-1, 1]) prim.box(trim, [0.2, 0.1, 0.12], { center: [x * (W / 2 + 0.08), beltY + 0.12, cabZ + cabL / 2 - 0.1] });
      return [body, glass, tyre, rim, trim, head, tail];
    },
    trainCar(kind = 'metro') {
      const body = new MB('carpaint'), glass = new MB('glass'), bogie = new MB('metal_dark'), wheel = new MB('rail'), roof = new MB('metal_dark'), door = new MB('plastic_black');
      const L = kind === 'metro' ? 18 : 24, W = kind === 'metro' ? 2.8 : 3.0, H = kind === 'metro' ? 3.5 : 4.0, floor = 1.1;
      prim.box(body, [W, H - floor - 0.3, L - 0.3], { center: [0, floor + (H - floor - 0.3) / 2, 0], bevel: 0.15 });
      prim.box(roof, [W - 0.4, 0.3, L - 1.0], { center: [0, H - 0.15, 0], bevel: 0.1 });
      // window band and doors
      prim.box(glass, [W + 0.01, 0.95, L - 2.0], { center: [0, floor + 1.55, 0] });
      const nDoors = kind === 'metro' ? 3 : 2;
      for (let i = 0; i < nDoors; i++) { const z = -L / 2 + (L / (nDoors + 1)) * (i + 1); for (const x of [-1, 1]) prim.box(door, [0.02, H - floor - 0.5, 1.3], { center: [x * (W / 2 + 0.005), floor + (H - floor - 0.5) / 2, z] }); }
      // end windows (cab)
      prim.box(glass, [W - 0.6, 1.1, 0.02], { center: [0, floor + 1.6, L / 2 - 0.14] });
      prim.box(glass, [W - 0.6, 1.1, 0.02], { center: [0, floor + 1.6, -L / 2 + 0.14] });
      // underframe + bogies
      prim.box(bogie, [W - 0.5, 0.5, L - 1.5], { center: [0, floor - 0.25, 0] });
      for (const z of [L / 2 - 3.0, -L / 2 + 3.0]) {
        prim.box(bogie, [2.2, 0.45, 2.6], { center: [0, 0.7, z], bevel: 0.05 });
        for (const dz of [-0.9, 0.9]) for (const x of [-0.75, 0.75]) prim.lathe(wheel, [[0.0, -0.07], [0.46, -0.07], [0.46, 0.07], [0.0, 0.07]], 18, { transform: M4.mul(M4.trs([x, 0.46, z + dz], 0), M4.trs([0, 0, 0], 0, 1, 0, Math.PI / 2)), capTop: true, capBottom: true });
      }
      return [body, glass, bogie, wheel, roof, door];
    },
  };
  function wheels(tyre, rim, W, clearance, zs, r, w) {
    for (const z of zs) for (const x of [-1, 1]) {
      const T = M4.mul(M4.trs([x * (W / 2 - w / 2 - 0.02), r, z], 0), M4.trs([0, 0, 0], 0, 1, 0, Math.PI / 2));
      prim.lathe(tyre, [[r * 0.62, -w / 2], [r * 0.9, -w / 2], [r, -w / 2 + 0.03], [r, w / 2 - 0.03], [r * 0.9, w / 2], [r * 0.62, w / 2]], 20, { transform: T });
      prim.lathe(rim, [[0.0, -w / 2 + 0.02], [r * 0.3, -w / 2 + 0.06], [r * 0.6, -w / 2 + 0.01], [r * 0.63, -w / 2 + 0.01], [r * 0.63, w / 2 - 0.01], [0.0, w / 2 - 0.01]], 20, { transform: T, capTop: true, capBottom: true });
    }
  }
  function lights(head, tail, W, y, zf, zr, h) {
    for (const x of [-1, 1]) { prim.box(head, [0.45, h, 0.04], { center: [x * (W / 2 - 0.35), y, zf] }); prim.box(tail, [0.4, h * 0.8, 0.04], { center: [x * (W / 2 - 0.3), y, zr] }); }
  }

  /** Build (or fetch) a prop as [{material, mesh, tile?}] */
  function get(name, ...args) {
    const key = name + ':' + args.join(',');
    if (cache.has(key)) return cache.get(key);
    const builders = LIB[name](...args);
    const parts = builders.map((b) => { const mesh = b.build(); return { material: b.material, tile: b.tile, mesh }; }).filter((p) => p.mesh.indices.length);
    cache.set(key, parts);
    return parts;
  }

  /* ---------------- placement ---------------- */
  /** instance from position / forward dir (xz) / optional tilt; colour optional. */
  function inst(p, fwd, col = null, ext = null) {
    const m = M4.frame(p, [fwd[0], 0, fwd[2]]);
    const out = new Array(24); for (let i = 0; i < 16; i++) out[i] = m[i];
    out[16] = col ? col[0] : 0; out[17] = col ? col[1] : 0; out[18] = col ? col[2] : 0; out[19] = col ? 1 : 0;
    out[20] = ext ? ext[0] : 1; out[21] = ext ? ext[1] : 0; out[22] = ext ? ext[2] : 0; out[23] = ext ? ext[3] : 0;
    return out;
  }
  const RIGHT = (d) => V.perp(d); // +n = right-hand side of travel for RHT

  /**
   * Place all furniture. ctx: { net, project, layers, placed: Map(key → {parts, instances:[], pick:[]}), lights:[], signals:[] }
   */
  function place(ctx) {
    const { net, project, layers } = ctx;
    const F = layers.furniture || {};
    const add = (key, parts, p, fwd, owner, col, ext) => {
      let rec = ctx.placed.get(key); if (!rec) { rec = { parts, instances: [], owners: [] }; ctx.placed.set(key, rec); }
      rec.instances.push(inst(p, fwd, col, ext)); rec.owners.push(owner);
      return rec.instances.length - 1;
    };
    const surf = (R, s, t, lift = 0) => RF.roadmesh.surfPoint(R, s, t, layers, lift);
    for (const R of net.roads.values()) {
      const lay = R.lay, props = R.road.props || {}, s0 = R.samples[0].s, s1 = R.samples[R.samples.length - 1].s, body = s1 - s0;
      if (body < 4) continue;
      const structAt = (s) => (R.structure ? R.structure.at(s) : 'ground');
      const sideOffset = (side) => (side > 0 ? lay.hwR : lay.hwL) + (lay.hasCurb ? lay.curbW + 0.55 : 1.0);
      // -- street lighting
      const lightsOn = props.streetlights ?? (F.streetlights !== false && (lay.hasCurb || lay.kind === 'highway'));
      if (lightsOn) {
        const h = lay.kind === 'highway' || lay.driveLanes.length >= 4 ? 12 : lay.driveLanes.length >= 3 ? 10 : 8;
        const spacing = (F.lightSpacing || (lay.kind === 'highway' ? 45 : 32)) * (h / 10) ;
        const parts = get('streetlight', h);
        const sides = lay.kind === 'highway' && lay.medianKind === 'barrier' ? [0] : props.lightsSide === 'left' ? [-1] : props.lightsSide === 'right' ? [1] : [1, -1];
        const n = Math.max(1, Math.round(body / spacing));
        for (let i = 0; i <= n; i++) {
          const s = s0 + 6 + ((body - 12) * i) / n;
          if (s < s0 + 4 || s > s1 - 4) continue;
          for (const side of sides) {
            if (sides.length === 2 && side === -1 && i % 2 === 0) continue; // staggered
            if (sides.length === 2 && side === 1 && i % 2 === 1) continue;
            const k = structAt(s); if (k === 'tunnel') continue;
            const f = RF.roadmesh.frameAt(R, s);
            let p, fwd;
            if (side === 0) { p = surf(R, s, 0, 0.6); fwd = V.perp(f.d); add('streetlight:' + h, parts, p, fwd, R.id); add('streetlight:' + h, parts, p, V.mul(fwd, -1), R.id); }
            else { const t = side * sideOffset(side); p = surf(R, s, t, lay.hasCurb ? lay.curbH + 0.01 : 0); fwd = V.mul(V.perp(f.d), -side); add('streetlight:' + h, parts, p, fwd, R.id); }
            ctx.lights.push({ pos: [p[0] + fwd[0] * 1.9, p[1] + h - 0.6, p[2] + fwd[2] * 1.9], radius: h * 2.6, color: [1, 0.85, 0.6], intensity: 1 });
          }
        }
      }
      // -- bollards along footways
      const bk = props.bollards || 'none';
      if (bk !== 'none' && lay.hasCurb) {
        const parts = get('bollard', bk); const pitch = bk === 'sphere' || bk === 'concrete' ? 3.0 : 2.2;
        for (const side of [1, -1]) {
          const sw = side > 0 ? lay.sidewalkR : lay.sidewalkL; if (sw < 1.2) continue;
          for (let s = s0 + 3; s < s1 - 3; s += pitch) { if (structAt(s) === 'tunnel') continue; const f = RF.roadmesh.frameAt(R, s); add('bollard:' + bk, parts, surf(R, s, side * (lay.hwR * (side > 0 ? 1 : 0) + lay.hwL * (side < 0 ? 1 : 0) + lay.curbW + 0.35), lay.curbH), V.perp(f.d), R.id); }
        }
      }
      // -- guardrail (W-beam) on embankments / rural & highway outer edges
      const gr = props.guardrail ?? (F.guardrails !== false && !lay.hasCurb ? 'auto' : 'none');
      if (gr !== 'none') {
        for (const side of [1, -1]) {
          const t = side * ((side > 0 ? lay.hwR : lay.hwL) + 0.45);
          let run = [];
          const flush = () => { if (run.length > 2) buildWBeam(ctx, R, run, side, layers); run = []; };
          for (const f of R.samples) {
            const k = structAt(f.s);
            const want = gr === 'both' || gr === 'auto' && (k === 'embankment' || (k === 'ground' && lay.kind === 'highway') || k === 'cut');
            if (want && k !== 'bridge' && k !== 'tunnel') run.push({ s: f.s, t }); else flush();
          }
          flush();
        }
      }
      // -- billboards
      const bbs = props.billboards || (F.billboards && lay.kind === 'highway' ? autoBillboards(R, s0, s1) : []);
      for (const bb of bbs) {
        const side = bb.side || 1; const s = RF.clamp(bb.s, s0 + 10, s1 - 10); const f = RF.roadmesh.frameAt(R, s);
        const t = side * ((side > 0 ? lay.outerR : lay.outerL) + 10);
        const p = [f.p[0] + f.n[0] * t, RF.structures ? RF.structures.GROUND_Y : 0, f.p[2] + f.n[2] * t];
        const fwd = V.norm(V.add(V.mul(f.d, bb.facing || -1), V.mul(f.n, -side * 0.45)));
        const tile = bb.ad != null ? bb.ad % 8 : (RF.hashStr(R.id) + bbs.indexOf(bb) * 3) % 8; // per-instance advert from the 4x4 atlas (tiles 0-7)
        add('billboard', get('billboard'), p, fwd, R.id, null, [1, tile, 1, 0]);
        ctx.pick.push({ type: 'billboard', owner: R.id, index: bbs.indexOf(bb), p, r: 7 });
      }
      // -- construction zones: cones + barrier boards + sign
      for (const c of (R.road.features || {}).construction || []) {
        const side = c.side || 1; const edgeT = side * ((side > 0 ? lay.hwR : lay.hwL) - 0.4);
        const innerT = edgeT - side * (c.width || 3.2);
        const cone = get('cone'), board = get('barrierBoard');
        for (let s = c.s0 - 15; s <= c.s1 + 2; s += 4) { if (s < s0 + 1 || s > s1 - 1) continue; const u = RF.clamp((s - (c.s0 - 15)) / 15, 0, 1); const t = edgeT + (innerT - edgeT) * u; const f = RF.roadmesh.frameAt(R, s); add('cone', cone, surf(R, s, t), f.d, R.id); }
        for (const s of [c.s0, c.s1]) if (s > s0 + 1 && s < s1 - 1) { const f = RF.roadmesh.frameAt(R, s); add('barrierBoard', board, surf(R, s, (edgeT + innerT) / 2), f.n, R.id); }
        const sS = c.s0 - 40; if (sS > s0 + 2) { const f = RF.roadmesh.frameAt(R, sS); add('sign:roadworks', get('sign', 10, 'triangle'), surf(R, sS, side * (sideOffset(side)), lay.hasCurb ? lay.curbH : 0), V.mul(f.d, -1), R.id); }
      }
      // -- parked cars in parking lanes
      if (F.parkedCars !== false && layers.traffic?.parked !== false) {
        for (const l of lay.lanes) {
          if (l.kind !== 'parking') continue;
          const tc = (l.t0 + l.t1) / 2, pitch = 6.0; const rng = RF.rng(RF.hashStr(R.id + tc));
          for (let s = s0 + 12; s < s1 - 12; s += pitch) {
            if (rng() < 0.45) continue;
            const f = RF.roadmesh.frameAt(R, s); const kind = ['sedan', 'hatch', 'suv', 'sedan', 'van'][Math.floor(rng() * 5)];
            const col = CAR_COLORS[Math.floor(rng() * CAR_COLORS.length)];
            const fwd = V.mul(f.d, l.dir >= 0 ? 1 : -1);
            const parts = get('vehicle', kind);
            for (const part of parts) add('veh:' + kind + ':' + part.material, [part], surf(R, s, tc), fwd, R.id, part.material === 'carpaint' ? col : null);
          }
        }
      }
    }
    // -- junction furniture: signals, signs, railings, corner bollards
    for (const rec of net.nodes.values()) {
      if (rec.kind !== 'junction') continue;
      const urban = rec.arms.some((a) => a.road.lay.hasCurb);
      if (rec.signals && F.signals !== false) placeSignals(ctx, rec, add);
      else if (rec.arms.length >= 3 && F.signs !== false) placeSigns(ctx, rec, add);
      if (urban && F.railings) placeCornerRailings(ctx, rec, add);
      if (urban && F.cornerBollards) placeCornerBollards(ctx, rec, add);
    }
  }
  const CAR_COLORS = [[0.9, 0.9, 0.92], [0.05, 0.05, 0.06], [0.5, 0.5, 0.52], [0.25, 0.25, 0.27], [0.6, 0.02, 0.02], [0.05, 0.12, 0.4], [0.75, 0.75, 0.78], [0.1, 0.3, 0.15], [0.55, 0.35, 0.1]];

  function autoBillboards(R, s0, s1) { const out = []; for (let s = s0 + 120, i = 0; s < s1 - 60; s += 380, i++) out.push({ s, side: i % 2 ? -1 : 1 }); return out; }

  /** W-beam guardrail: swept beam (per road, not instanced) + instanced posts every 2 m. */
  function buildWBeam(ctx, R, run, side, layers) {
    const b = ctx.out.metal_galv || (ctx.out.metal_galv = new MB('metal_galv'));
    const frames = run.map((q) => { const f = RF.roadmesh.frameAt(R, q.s); const p = RF.roadmesh.surfPoint(R, q.s, q.t, layers, 0); return { p: [p[0], p[1], p[2]], d: f.d, n: V.mul(f.n, side), up: [0, 1, 0] }; });
    // W profile (x outward from traffic face, y up) — 310 mm tall, 80 mm deep, top at 0.73 m
    const yTop = 0.73, yBot = 0.42, d = 0.08;
    const prof = [[0, yBot], [-d, yBot + 0.05], [-d, yBot + 0.1], [0, yBot + 0.14], [0, yBot + 0.17], [-d, yBot + 0.21], [-d, yBot + 0.26], [0, yBot + 0.31]].map((q) => [q[0] - 0.1, q[1]]);
    void yTop;
    prim.sweep(b, frames, prof, { flip: side < 0, uvScale: 1 });
    // end terminals: flare down to ground over 4 m is approximated by a sloped box
    const parts = get('guardrailPost');
    const key = 'guardrailPost';
    let rec = ctx.placed.get(key); if (!rec) { rec = { parts, instances: [], owners: [] }; ctx.placed.set(key, rec); }
    let acc = 2;
    for (let i = 0; i < frames.length; i++) {
      if (i > 0) acc += V.dist(frames[i - 1].p, frames[i].p);
      if (acc >= 2) { acc = 0; const f = frames[i]; rec.instances.push(inst([f.p[0] + f.n[0] * 0.15, f.p[1] - 0.25, f.p[2] + f.n[2] * 0.15], V.mul(f.n, -1))); rec.owners.push(R.id); }
    }
  }

  function placeSignals(ctx, rec, add) {
    for (const arm of rec.arms) {
      const R = arm.road, lay = R.lay; const inDir = V.mul(arm.endDirOut, -1); // direction of travel INTO the junction
      const right = RIGHT(inDir);
      const incoming = lay.driveLanes.filter((l) => (arm.isStart ? l.dir < 0 : l.dir > 0));
      if (!incoming.length) continue;
      // lateral extents of the incoming lanes measured along `right` from the road centreline
      const sgn = arm.isStart ? -1 : 1; // road +n → right of incoming travel when !isStart
      const ts = incoming.map((l) => [l.t0 * sgn, l.t1 * sgn]).flat();
      const tNear = Math.max(...ts), tFar = Math.min(...ts); // along `right`
      const curbT = (arm.isStart ? lay.hwL : lay.hwR) + (lay.hasCurb ? lay.curbW + 0.6 : 1.0);
      const armLen = RF.clamp(curbT - tFar - 0.5, 4, 9);
      const heads = Math.min(3, incoming.length);
      const f = arm.endFrame; const back = 1.2;
      const base = V.add(V.madd(f.p, arm.endDirOut, back), V.mul(right, curbT));
      const p = RF.roadmesh.surfPoint(R, f.s + (arm.isStart ? back : -back), curbT * (arm.isStart ? -1 : 1), ctx.layers, lay.hasCurb ? lay.curbH : 0);
      p[0] = base[0]; p[2] = base[2];
      const fwd = V.mul(right, -1); // mast arm extends to the left over the lanes (local +x = right of fwd → we want arm toward -right)
      const parts = get('signal', armLen, heads);
      const idx = add('signal:' + armLen + ':' + heads, parts, p, V.mul(V.perp(fwd), -1), rec.id, null, [1, 0, 0, 1]);
      ctx.signals.push({ node: rec.id, arm: arm.roadId, key: 'signal:' + armLen + ':' + heads, index: idx, heading: inDir });
      void tNear;
    }
  }
  function placeSigns(ctx, rec, add) {
    // priority: widest road(s) are major; minor arms get a give-way (triangle) or stop sign for ≥ 2 lane crossings
    const widths = rec.arms.map((a) => a.road.lay.hwL + a.road.lay.hwR); const maxW = Math.max(...widths);
    rec.arms.forEach((arm, i) => {
      if (widths[i] >= maxW - 0.5 && rec.arms.length > 2 && widths.filter((w) => w >= maxW - 0.5).length <= 2) return;
      const R = arm.road, lay = R.lay; const inDir = V.mul(arm.endDirOut, -1); const right = RIGHT(inDir);
      const curbT = (arm.isStart ? lay.hwL : lay.hwR) + (lay.hasCurb ? lay.curbW + 0.5 : 1.2);
      const f = arm.endFrame; const back = 2.5;
      const base = V.add(V.madd(f.p, arm.endDirOut, back), V.mul(right, curbT));
      const y = RF.roadmesh.surfPoint(R, f.s, 0, ctx.layers, lay.hasCurb ? lay.curbH : 0)[1];
      const stop = rec.node.control === 'stop' || maxW > 14;
      add(stop ? 'sign:stop' : 'sign:yield', stop ? get('sign', 8, 'octagon') : get('sign', 9, 'triangle'), [base[0], y, base[2]], V.mul(inDir, -1), rec.id);
    });
  }
  function placeCornerRailings(ctx, rec, add) {
    const parts = get('railingPanel');
    for (const c of rec.corners) {
      if (!c.poly || c.poly.length < 3) continue;
      const sw = Math.min(c.A.road.lay.sidewalkR || 2, c.B.road.lay.sidewalkR || 2); if (sw < 1.5) continue;
      // offset the kerb polyline 0.5 m onto the footway and walk 2 m panels, skipping 3 m at both ends (crossings)
      const off = outwardOffset(c.poly, 0.6, rec.node.p); const cum = S.cumulativeXZ(off); const L = cum[cum.length - 1];
      for (let s = 3 + 1; s + 1 < L - 3; s += 2.05) { const e = S.evalAt(off, cum, s); const d = V.normXZ(e.d); add('railing', parts, [e.p[0], e.p[1] + c.A.road.lay.curbH, e.p[2]], V.perp(d), rec.id); }
    }
  }
  function placeCornerBollards(ctx, rec, add) {
    const parts = get('bollard', 'steel');
    for (const c of rec.corners) {
      if (!c.poly || c.poly.length < 3) continue;
      const off = outwardOffset(c.poly, 0.5, rec.node.p); const cum = S.cumulativeXZ(off); const L = cum[cum.length - 1];
      for (let s = 1.5; s < L - 1; s += 1.5) { const e = S.evalAt(off, cum, s); add('bollard:steel', parts, [e.p[0], e.p[1] + c.A.road.lay.curbH, e.p[2]], V.normXZ(e.d), rec.id); }
    }
  }

  /** Offset a kerb polyline onto the footway side (away from the junction centre). */
  function outwardOffset(poly, d, centre) {
    const a = S.offsetPolyline(poly, d), b = S.offsetPolyline(poly, -d);
    const mid = (q) => q[Math.floor(q.length / 2)];
    return V.distXZ(mid(a), centre) > V.distXZ(mid(b), centre) ? a : b;
  }
  RF.props = { get, place, inst, LIB, CAR_COLORS };
})(globalThis.RF || (globalThis.RF = {}));
