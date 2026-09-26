// Places every battlefield object, builds instanced meshes and registers colliders / hazards.
import * as THREE from '../vendor/three.module.min.js';
import { rng, clamp, lerp, segDist, paint, merge, mat, triGeo, smooth, TAU } from './util.js';
import * as M from './models.js';
import { BUNKERS, TANKS, WALL_Z, WALL_TURRETS, GATE_HW, CHECKPOINTS, ROAD_HW, FIELD_HALF, DRAWS } from './layout.js';

// ---------------- spatial hash for colliders / hazards ----------------
export class Hash {
  constructor(cell = 12) { this.cell = cell; this.map = new Map(); this.stamp = 0; }
  key(i, j) { return (i + 1000) * 100000 + (j + 1000); }
  add(o, x, z, r) {
    o._s = 0;
    for (let i = Math.floor((x - r) / this.cell); i <= Math.floor((x + r) / this.cell); i++)
      for (let j = Math.floor((z - r) / this.cell); j <= Math.floor((z + r) / this.cell); j++) {
        const k = this.key(i, j); let a = this.map.get(k); if (!a) this.map.set(k, a = []); a.push(o);
      }
  }
  query(x, z, r, out = []) {
    out.length = 0; const st = ++this.stamp;
    for (let i = Math.floor((x - r) / this.cell); i <= Math.floor((x + r) / this.cell); i++)
      for (let j = Math.floor((z - r) / this.cell); j <= Math.floor((z + r) / this.cell); j++) {
        const a = this.map.get(this.key(i, j)); if (!a) continue;
        for (const o of a) if (o._s !== st) { o._s = st; out.push(o); }
      }
    return out;
  }
}

export function buildProps(scene, T) {
  const R = rng(6061944);
  const colliders = new Hash(12);
  const wires = new Hash(12);
  const mines = new Hash(10);
  const world = { colliders, wires, mines, turrets: [], crates: [], checkpoints: [], smokers: [], boats: [], mineList: [], flags: [] };

  const H = (x, z) => T.heightAt(x, z);
  const roadD = (x, z) => T.roadInfo(x, z).d;
  const occupied = new Hash(8);
  const occ = [];
  const free = (x, z, r) => { for (const o of occupied.query(x, z, r + 6, occ)) if (Math.hypot(o.x - x, o.z - z) < r + o.r) return false; return true; };
  const claim = (x, z, r) => occupied.add({ x, z, r }, x, z, r);

  // colliders
  const addCircle = (x, z, r, top, kind, extra = {}) => { const c = { type: 'c', x, z, r, bound: r, top, kind, ...extra }; colliders.add(c, x, z, r); claim(x, z, r); return c; };
  const addBox = (x, z, hx, hz, yaw, top, kind, extra = {}) => {
    const c = { type: 'b', x, z, hx, hz, c: Math.cos(yaw), s: Math.sin(yaw), bound: Math.hypot(hx, hz), top, kind, ...extra };
    colliders.add(c, x, z, c.bound); claim(x, z, Math.max(hx, hz)); return c;
  };

  // ---------------- instancing helper ----------------
  const batches = new Map();
  const inst = (name, geo, material = M.MAT.flat, shadow = true) => {
    if (!batches.has(name)) batches.set(name, { geo, material, shadow, list: [] });
    return batches.get(name);
  };
  const push = (name, m, color) => { const b = batches.get(name); b.list.push({ m, color }); return b.list.length - 1; };
  inst('hedgehog', M.hedgehogGeo(), M.MAT.metal);
  inst('teller', M.tellerGeo());
  inst('ap', M.apMineGeo(), M.MAT.flat, false);
  inst('stake', M.stakeGeo());
  inst('ramp', M.logRampGeo());
  inst('gate', M.belgianGateGeo(), M.MAT.metal);
  inst('tooth', M.toothGeo());
  inst('knife', M.knifeRestGeo());
  inst('block', M.concreteBlockGeo());
  inst('bag', M.sandbagGeo());
  inst('bush', M.bushGeo());
  inst('tree', M.treeGeo());
  inst('crate', M.crateGeo());

  const lineVerts = []; // barbed-wire line segments
  const tiltTo = (x, z, yaw, s = 1) => {
    const n = T.normalAt(x, z);
    const q = new THREE.Quaternion().setFromUnitVectors(new THREE.Vector3(0, 1, 0), n);
    const qy = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), yaw);
    q.multiply(qy);
    return new THREE.Matrix4().compose(new THREE.Vector3(x, H(x, z), z), q, new THREE.Vector3(s, s, s));
  };

  // ---------------- mines ----------------
  const addMine = (type, x, z, opts = {}) => {
    const y = opts.y ?? H(x, z);
    let idx = -1;
    if (type === 'teller') idx = push('teller', tiltTo(x, z, R() * TAU, 1.35));
    if (type === 'ap') idx = push('ap', tiltTo(x, z, R() * TAU));
    const m = { type, x, z, y, r: opts.r ?? (type === 'ap' ? 1.25 : 1.55), dmg: opts.dmg ?? (type === 'ap' ? 24 : 46), alive: true, batch: opts.batch ?? type, idx: opts.idx ?? idx, lift: opts.lift ?? (type === 'ap' ? 5 : 8.5) };
    mines.add(m, x, z, 2);
    world.mineList.push(m);
    claim(x, z, 1.2);
    return m;
  };

  // ---------------- barbed wire (triple concertina + pickets) ----------------
  const pickets = [];
  const wireRun = (ax, az, bx, bz) => {
    const L = Math.hypot(bx - ax, bz - az); if (L < 2) return;
    const dx = (bx - ax) / L, dz = (bz - az) / L, px = -dz, pz = dx;
    const coil = (off, lift, r, phase) => {
      const pitch = 0.42, n = Math.ceil(L / pitch * 7);
      let prev = null;
      for (let i = 0; i <= n; i++) {
        const t = i / n, s = t * L, a = (s / pitch) * TAU + phase;
        const cx = ax + dx * s + px * off, cz = az + dz * s + pz * off;
        const rr = r * (0.92 + 0.08 * Math.sin(s * 1.7 + phase));
        const x = cx + px * Math.cos(a) * rr + dx * Math.sin(a) * 0.08, z = cz + pz * Math.cos(a) * rr + dz * Math.sin(a) * 0.08;
        const y = H(cx, cz) + lift + r + Math.sin(a) * rr;
        const p = [x, y, z];
        if (prev) lineVerts.push(...prev, ...p);
        prev = p;
      }
    };
    coil(0.48, 0, 0.46, 0); coil(-0.48, 0, 0.46, 1.3); coil(0, 0.78, 0.44, 2.2);
    // pickets + straight strands with barbs
    const np = Math.max(2, Math.round(L / 3.2) + 1);
    let prevTop = null;
    for (let i = 0; i < np; i++) {
      const s = i / (np - 1) * L, x = ax + dx * s, z = az + dz * s, y = H(x, z);
      for (const o of [-0.95, 0.95]) pickets.push(mat(x + px * o, y + 0.7, z + pz * o, 0, R() * TAU, (R() - 0.5) * 0.08));
      const top = [x, y + 1.62, z];
      if (prevTop) {
        lineVerts.push(...prevTop, ...top);
        const seg = Math.hypot(top[0] - prevTop[0], top[2] - prevTop[2]);
        for (let b = 0.2; b < seg; b += 0.3) {
          const t = b / seg, bx2 = lerp(prevTop[0], top[0], t), by = lerp(prevTop[1], top[1], t), bz2 = lerp(prevTop[2], top[2], t);
          lineVerts.push(bx2 - px * 0.05, by - 0.05, bz2 - pz * 0.05, bx2 + px * 0.05, by + 0.05, bz2 + pz * 0.05);
        }
      }
      prevTop = top;
    }
    const w = { ax, az, bx, bz, hw: 1.25 };
    wires.add(w, (ax + bx) / 2, (az + bz) / 2, L / 2 + 2);
    for (let s = 0; s <= L; s += 3) claim(ax + dx * s, az + dz * s, 1.4);
  };
  // wire along a z-line with gaps at roads + random gaps
  const wireLine = (z0, x0, x1, gapChance, wobble = 3) => {
    let x = x0, run = [];
    const flush = () => { if (run.length >= 2) for (let i = 0; i < run.length - 1; i++) wireRun(run[i][0], run[i][1], run[i + 1][0], run[i + 1][1]); run = []; };
    while (x <= x1) {
      const z = z0 + Math.sin(x * 0.05) * wobble + (R() - 0.5) * 1.5;
      const blocked = roadD(x, z) < ROAD_HW + 3;
      if (blocked) { flush(); x += 4; continue; }
      if (run.length > 4 && R() < gapChance) { flush(); x += R.range(7, 14); continue; }
      run.push([x, z]); x += 6;
    }
    flush();
  };

  // ---------------- sandbags ----------------
  const bagColor = () => new THREE.Color().setHSL(0.1 + R() * 0.025, 0.3 + R() * 0.1, 0.36 + R() * 0.07);
  const bagRow = (pts, layers, closed = false) => {
    for (let l = 0; l < layers; l++) {
      for (let i = 0; i < pts.length - 1; i++) {
        const [ax, az] = pts[i], [bx, bz] = pts[i + 1];
        const L = Math.hypot(bx - ax, bz - az), yaw = Math.atan2(bx - ax, bz - az);
        const n = Math.max(1, Math.round(L / 0.6));
        for (let k = 0; k < n; k++) {
          const t = (k + (l % 2 ? 0.5 : 0.0) + 0.25) / n; if (t > 1) continue;
          const x = lerp(ax, bx, t), z = lerp(az, bz, t);
          push('bag', mat(x, H(x, z) - 0.05 + l * 0.23, z, (R() - 0.5) * 0.08, yaw - Math.PI / 2 + (R() - 0.5) * 0.12, (R() - 0.5) * 0.06), bagColor());
        }
      }
    }
  };
  const bagArc = (cx, cz, r, a0, a1, layers) => {
    const pts = []; const n = Math.max(3, Math.round(Math.abs(a1 - a0) * r / 1.6));
    for (let i = 0; i <= n; i++) { const a = lerp(a0, a1, i / n); pts.push([cx + Math.sin(a) * r, cz + Math.cos(a) * r]); }
    bagRow(pts, layers);
    return pts;
  };

  // =====================================================================
  // BEACH DEFENCES
  // =====================================================================
  const nearStart = (x, z) => Math.hypot(x, z - 6) < 14;
  // Belgian gates (outer line, low tide)
  for (let x = -160; x <= 160; x += R.range(18, 26)) {
    const z = 36 + R.range(-3, 3);
    if (nearStart(x, z) || Math.abs(x) < 8) continue;
    const yaw = Math.PI + R.range(-0.15, 0.15);
    push('gate', mat(x, H(x, z) - 0.05, z, 0, yaw, 0));
    addBox(x - Math.sin(yaw) * 1.3, z - Math.cos(yaw) * 1.3, 1.6, 1.4, yaw, 2.5, 'steel');
  }
  // Czech hedgehogs in staggered belts
  [[64, 9.5], [94, 10], [126, 9], [166, 12], [232, 14]].forEach(([zr, step], row) => {
    for (let x = -168 + (row % 2) * step / 2; x <= 168; x += step + R.range(-2, 2)) {
      const z = zr + R.range(-4, 4);
      if (R() < 0.1 || !free(x, z, 1.6) || nearStart(x, z)) continue;
      push('hedgehog', mat(x, H(x, z), z, 0, R() * TAU, 0, 1.05));
      addCircle(x, z, 1.05, 1.3, 'steel');
    }
  });
  // mined stakes (Rommel's asparagus)
  for (let i = 0; i < 70; i++) {
    const x = R.range(-165, 165), z = R.range(172, 212);
    if (!free(x, z, 1.8)) continue;
    const y = H(x, z);
    push('stake', mat(x, y - 0.1, z, 0, R.range(-0.2, 0.2), 0));
    const m = addMine('stake', x, z - 2.2, { y: y + 2.9, r: 0, dmg: 42, batch: 'stake', idx: batches.get('stake').list.length - 1 });
    addCircle(x, z - 1.2, 0.9, 3.2, 'stake', { mine: m });
  }
  // log ramps with mines
  for (let i = 0; i < 16; i++) {
    const x = R.range(-160, 160), z = R.range(214, 262);
    if (!free(x, z, 3.2)) continue;
    push('ramp', mat(x, H(x, z) - 0.05, z, 0, Math.PI + R.range(-0.2, 0.2), 0));
    const m = addMine('stake', x, z + 2.4, { y: H(x, z) + 2.5, r: 0, dmg: 42, batch: 'ramp', idx: batches.get('ramp').list.length - 1 });
    addBox(x, z, 1.0, 2.7, 0, 2.6, 'stake', { mine: m });
  }
  // Teller mines strewn over the upper beach
  for (let i = 0, n = 0; i < 400 && n < 85; i++) {
    const x = R.range(-165, 165), z = R.range(70, 300);
    if (!free(x, z, 3) || nearStart(x, z)) continue;
    addMine('teller', x, z); n++;
  }
  // beach wire at the shingle and along bluff crest
  wireLine(296, -172, 172, 0.12, 4);
  wireLine(386, -172, 172, 0.06, 3);
  // sealed draw: wire + blocks
  for (let k = 0; k < 4; k++) wireRun(-170 + k * 3, 350 + k * 6, -128 - k * 3, 352 + k * 6);

  // =====================================================================
  // INLAND LINES: wire belts, dragon's teeth, trenches, hedges
  // =====================================================================
  wireLine(598, -172, 172, 0.18, 4);
  wireLine(1060, -172, 172, 0.2, 5);
  wireLine(1238, -172, 172, 0.2, 4);
  wireLine(1454, -172, 172, 0.1, 3);
  wireLine(1566, -172, 172, 0.04, 2);
  // Mine Alley: flanking wire so you commit to the road
  const alley = T.roads.find(r => r.name === 'Mine Alley');
  for (let s = 30; s < alley.length - 30; s += 22) {
    for (const side of [1, -1]) {
      const a = T.roadAt(alley, s), b = T.roadAt(alley, s + 16);
      const o = side * (ROAD_HW + 5);
      wireRun(a.x + a.tz * o, a.z - a.tx * o, b.x + b.tz * o, b.z - b.tx * o);
    }
  }

  // dragon's teeth band
  for (let row = 0; row < 3; row++) {
    for (let x = -176 + row * 1.3; x <= 176; x += 2.6) {
      const z = 1500 + row * 2.4;
      if (roadD(x, z) < ROAD_HW + 2.2) continue;
      push('tooth', mat(x, H(x, z) - 0.15, z, 0, R.range(-0.1, 0.1), 0, R.range(0.92, 1.1)));
      addCircle(x, z, 0.85, 1.4, 'concrete');
    }
  }

  // trenches: sandbag parapet (sea side), colliders; open where roads cross
  T.trenches.forEach(pts => {
    let run = [];
    const flush = () => { if (run.length > 1) bagRow(run, 2); run = []; };
    for (let i = 0; i < pts.length - 1; i++) {
      const [ax, az] = pts[i], [bx, bz] = pts[i + 1];
      const mx = (ax + bx) / 2, mz = (az + bz) / 2;
      const L = Math.hypot(bx - ax, bz - az), yaw = Math.atan2(bx - ax, bz - az);
      if (roadD(mx, mz) < ROAD_HW + 9 || roadD(ax, az) < ROAD_HW + 7 || roadD(bx, bz) < ROAD_HW + 7) { flush(); continue; }
      // collider covering the trench + berms
      addBox(mx, mz, 3.6, L / 2 + 0.4, yaw, 1.6, 'trench');
      const px = -(bz - az) / L, pz = (bx - ax) / L; // left normal
      const side = pz < 0 ? 1 : -1; // pick the seaward (-z) side
      const o = 2.7 * side;
      if (!run.length) run.push([ax + px * o, az + pz * o]);
      run.push([bx + px * o, bz + pz * o]);
    }
    flush();
  });
  // hedgerows
  T.hedges.forEach(pts => {
    for (let i = 0; i < pts.length - 1; i++) {
      const [ax, az] = pts[i], [bx, bz] = pts[i + 1];
      const L = Math.hypot(bx - ax, bz - az), yaw = Math.atan2(bx - ax, bz - az);
      for (let s = 0; s < L; s += 2.1) {
        const x = lerp(ax, bx, s / L) + R.range(-0.5, 0.5), z = lerp(az, bz, s / L) + R.range(-0.5, 0.5);
        if (roadD(x, z) < ROAD_HW + 3) continue;
        push('bush', mat(x, H(x, z) - 0.3, z, 0, R() * TAU, 0, R.range(1.0, 1.6)), new THREE.Color().setHSL(0.24 + R() * 0.05, 0.35, 0.28 + R() * 0.06));
        if (R() < 0.12) push('tree', mat(x, H(x, z) - 0.2, z, 0, R() * TAU, 0, R.range(0.9, 1.3)));
      }
      addBox((ax + bx) / 2, (az + bz) / 2, 2.4, L / 2, yaw, 2.5, 'hedge');
    }
  });

  // =====================================================================
  // BUNKERS (MG turrets)
  // =====================================================================
  const nearestRoadPoint = (x, z) => {
    let best = null, bd = Infinity;
    for (const r of T.roads) for (let i = 0; i < r.samples.length; i += 3) { const s = r.samples[i], d = Math.hypot(s.x - x, s.z - z); if (d < bd) { bd = d; best = s; } }
    return { p: best, d: bd };
  };
  const addTurret = (obj, baseYaw, isWall = false) => {
    const u = obj.userData;
    obj.updateMatrixWorld(true);
    const pivot = new THREE.Vector3(); u.pitch.getWorldPosition(pivot);
    world.turrets.push({ obj, yawObj: u.yaw, pitchObj: u.pitch, flashes: u.flashes, lamp: u.lamp, pos: pivot, baseYaw, yaw: 0, pitch: 0, cool: R.range(0, 2), burst: 0, shotT: 0, alert: 0, scan: R() * TAU, isWall, range: isWall ? 230 : 175, flashT: 0, barrel: 0 });
  };
  BUNKERS.forEach(([x, z]) => {
    const nr = nearestRoadPoint(x, z);
    let tx = nr.p.x - x, tz = nr.p.z - z;
    if (z < 450 && z > 350) { tx = 0; tz = -1; }
    const yaw = Math.atan2(tx, tz);
    const b = M.buildBunker();
    b.position.set(x, H(x, z) - 1.0, z);
    b.rotation.y = yaw;
    scene.add(b);
    addCircle(x, z, 5.4, 5, 'concrete');
    bagArc(x, z, 7.6, yaw - 1.0, yaw + 1.0, 3);
    addTurret(b, yaw);
    // wire apron in front
    const a0 = yaw - 0.9, a1 = yaw + 0.9;
    for (let k = 0; k < 4; k++) {
      const aa = lerp(a0, a1, k / 4), ab = lerp(a0, a1, (k + 1) / 4);
      const ax = x + Math.sin(aa) * 13, az = z + Math.cos(aa) * 13, bx = x + Math.sin(ab) * 13, bz = z + Math.cos(ab) * 13;
      if (roadD(ax, az) > ROAD_HW + 2 && roadD(bx, bz) > ROAD_HW + 2) wireRun(ax, az, bx, bz);
    }
  });

  // =====================================================================
  // TANKS (static)
  // =====================================================================
  TANKS.forEach(([x, z, yaw, type, state]) => {
    const t = M.buildTank(type, state);
    const y = H(x, z);
    t.position.set(x, y - (state === 'dug' ? 0.95 : 0.05), z);
    t.rotation.set(state === 'wreck' ? R.range(-0.06, 0.06) : 0, yaw, state === 'wreck' ? R.range(-0.1, 0.1) : 0, 'YXZ');
    scene.add(t);
    const hx = type === 'tiger' ? 1.9 : 1.6, hz = type === 'tiger' ? 3.3 : 3.1;
    addBox(x, z, hx, hz, yaw, 2.6, 'tank');
    if (state === 'dug') {
      const berm = new THREE.Mesh(paint(new THREE.SphereGeometry(1, 9, 4, 0, TAU, 0, Math.PI / 2), '#6f5a3d'), M.MAT.flat);
      berm.scale.set(3.4, 1.35, 1.8); berm.position.set(x + Math.sin(yaw) * 3.4, y - 0.25, z + Math.cos(yaw) * 3.4); berm.rotation.y = yaw;
      berm.receiveShadow = true; berm.castShadow = true; scene.add(berm);
      bagArc(x, z, 4.6, yaw + 1.9, yaw + TAU - 1.9, 2);
    }
    if (state === 'wreck') world.smokers.push({ x, y: y + 2.2, z, t: R() });
  });

  // =====================================================================
  // ROAD CHALLENGES: slaloms, knife-rests, mine patterns, craters
  // =====================================================================
  const signAt = (x, z, yaw) => { const s = M.signMesh(); s.position.set(x, H(x, z), z); s.rotation.y = yaw; scene.add(s); };
  T.roads.forEach((road, ri) => {
    let s = 34 + R.range(0, 20);
    let k = ri;
    while (s < road.length - 34) {
      const p = T.roadAt(road, s);
      const nx = p.tz, nz = -p.tx, yaw = Math.atan2(p.tx, p.tz);
      const at = (ds, lat) => { const q = T.roadAt(road, s + ds); return [q.x + q.tz * lat, q.z - q.tx * lat, Math.atan2(q.tx, q.tz)]; };
      const type = ['slalom', 'mines', 'knife', 'mines2', 'slalom2'][k++ % 5];
      if (type === 'slalom' || type === 'slalom2') {
        const n = type === 'slalom' ? 3 : 4;
        for (let i = 0; i < n; i++) {
          const [x, z, yw] = at(i * 15, (i % 2 ? -1 : 1) * 1.75);
          push('block', mat(x, T.roadHeight(x, z), z, 0, yw + Math.PI / 2, 0));
          addBox(x, z, 1.25, 0.7, yw, 1.1, 'concrete');
          if (type === 'slalom2' && i % 2) addMine('teller', ...at(i * 15 + 7, (i % 2 ? 1 : -1) * 2.0).slice(0, 2));
        }
        s += n * 15;
      } else if (type === 'mines' || type === 'mines2') {
        const pat = type === 'mines' ? [[0, 0], [9, -2.1], [9, 2.1], [18, 0.6], [27, -1.6]] : [[0, -1.9], [0, 1.9], [11, 0], [22, -2.2], [22, 1.4], [32, 0.2]];
        for (const [ds, lat] of pat) { const [x, z] = at(ds, lat); addMine('teller', x, z); }
        const [sx, sz] = at(-6, ROAD_HW + 2.5); signAt(sx, sz, yaw + Math.PI);
        s += 34;
      } else {
        const side = R() < 0.5 ? 1 : -1;
        const [x, z, yw] = at(0, side * 1.6);
        push('knife', mat(x, T.roadHeight(x, z), z, 0, yw, 0));
        addBox(x, z, 2.2, 0.7, yw, 1.6, 'barricade');
        const axx = Math.cos(yw), axz = -Math.sin(yw), tgx = Math.sin(yw), tgz = Math.cos(yw), y0 = T.roadHeight(x, z) + 1.0;
        let prev = null;
        for (let i = 0; i <= 90; i++) {
          const t = -2 + i / 90 * 4, a = i * 0.55;
          const p = [x + axx * t + tgx * Math.cos(a) * 0.38, y0 + Math.sin(a) * 0.38, z + axz * t + tgz * Math.cos(a) * 0.38];
          if (prev) lineVerts.push(...prev, ...p);
          prev = p;
        }
        const [x2, z2, yw2] = at(18, -side * 1.8);
        push('block', mat(x2, T.roadHeight(x2, z2), z2, 0, yw2 + Math.PI / 2, 0));
        addBox(x2, z2, 1.25, 0.7, yw2, 1.1, 'concrete');
        s += 20;
      }
      s += R.range(58, 92);
    }
  });

  // repair crates along the route
  [[2, 0.5], [3, 0.35], [3, 0.8], [4, 0.5], [5, 0.12], [5, 0.33], [5, 0.55], [5, 0.78], [0, 0.5], [1, 0.5]].forEach(([ri, f]) => {
    const road = T.roads[ri], p = T.roadAt(road, road.length * f);
    const lat = (R() < 0.5 ? -1 : 1) * 2.2;
    const x = p.x + p.tz * lat, z = p.z - p.tx * lat;
    const g = new THREE.Mesh(batches.get('crate').geo, M.MAT.flat); g.castShadow = true;
    g.position.set(x, T.roadHeight(x, z), z); scene.add(g);
    const marker = new THREE.Mesh(new THREE.OctahedronGeometry(0.35, 0), new THREE.MeshBasicMaterial({ color: 0x6dff8a }));
    marker.position.set(x, g.position.y + 1.8, z); scene.add(marker);
    world.crates.push({ x, z, mesh: g, marker, alive: true, y: g.position.y });
  });

  // checkpoints
  CHECKPOINTS.forEach(([x, z, label], i) => {
    const ri = T.roadInfo(x, z);
    let fx = x + 9, fz = z - 10;
    if (ri.road) { const p = T.roadAt(ri.road, ri.road.samples[ri.i].s); fx = x + p.tz * (ROAD_HW + 3); fz = z - p.tx * (ROAD_HW + 3); }
    const f = M.flagMesh(); f.position.set(fx, H(fx, fz), fz); scene.add(f);
    world.checkpoints.push({ x, z, label, flag: f, reached: i === 0, i });
  });

  // =====================================================================
  // FIELD SCATTER: AP minefields, MG nests, trees, extra wire
  // =====================================================================
  // AP minefields (dense patches marked by signs), plus stray mines
  const fields = [];
  for (let i = 0; i < 18; i++) fields.push([R.range(-150, 150), R.range(440, 1540)]);
  fields.forEach(([fx, fz]) => {
    if (roadD(fx, fz) < 14) return;
    for (let k = 0; k < 16; k++) {
      const x = fx + R.range(-16, 16), z = fz + R.range(-12, 12);
      if (roadD(x, z) > ROAD_HW + 2.5 && free(x, z, 1)) addMine('ap', x, z);
    }
    signAt(fx + 18, fz, R() * TAU);
  });
  for (let i = 0, n = 0; i < 1500 && n < 110; i++) {
    const x = R.range(-165, 165), z = R.range(420, 1560);
    if (roadD(x, z) < ROAD_HW + 3 || !free(x, z, 2)) continue;
    addMine(R() < 0.35 ? 'teller' : 'ap', x, z); n++;
  }
  // teller minefield in front of the wall
  for (let i = 0, n = 0; i < 800 && n < 60; i++) {
    const x = R.range(-170, 170), z = R.range(1512, 1590);
    if (roadD(x, z) < ROAD_HW + 1.5 || !free(x, z, 2.5)) continue;
    addMine('teller', x, z); n++;
  }
  // MG nests / sandbag emplacements
  for (let i = 0, n = 0; i < 400 && n < 26; i++) {
    const x = R.range(-160, 160), z = R.range(420, 1540);
    if (roadD(x, z) < 12 || !free(x, z, 5)) continue;
    const a = R() * TAU;
    bagArc(x, z, 2.6, a, a + 4.2, 3);
    addCircle(x, z, 3.0, 0.8, 'sandbag');
    n++;
  }
  // roadside sandbag walls near checkpoints & bends
  T.roads.forEach(road => {
    for (let s = 50; s < road.length - 20; s += R.range(110, 160)) {
      const side = R() < 0.5 ? 1 : -1, a = T.roadAt(road, s), b = T.roadAt(road, s + 9), o = side * (ROAD_HW + 2.4);
      const pts = [[a.x + a.tz * o, a.z - a.tx * o], [b.x + b.tz * o, b.z - b.tx * o]];
      if (!free((pts[0][0] + pts[1][0]) / 2, (pts[0][1] + pts[1][1]) / 2, 2)) continue;
      bagRow(pts, 3);
      addBox((pts[0][0] + pts[1][0]) / 2, (pts[0][1] + pts[1][1]) / 2, 0.45, 4.6, Math.atan2(b.x - a.x, b.z - a.z), 0.7, 'sandbag');
    }
  });
  // trees on the flanks and bluffs
  for (let i = 0, n = 0; i < 3000 && n < 260; i++) {
    const side = R() < 0.5 ? -1 : 1, x = side * R.range(132, 205), z = R.range(380, 1640);
    if (!free(x, z, 3)) continue;
    push('tree', mat(x, H(x, z) - 0.2, z, 0, R() * TAU, 0, R.range(0.9, 1.5)));
    if (Math.abs(x) < FIELD_HALF + 2) addCircle(x, z, 0.6, 5, 'tree');
    n++;
  }
  // bushes scattered on the bluff face
  for (let i = 0; i < 160; i++) {
    const x = R.range(-170, 170), z = R.range(336, 380);
    if (roadD(x, z) < 8 || !free(x, z, 1.5)) continue;
    push('bush', mat(x, H(x, z) - 0.4, z, 0, R() * TAU, 0, R.range(0.6, 1.1)), new THREE.Color().setHSL(0.2 + R() * 0.06, 0.3, 0.33 + R() * 0.06));
  }

  // =====================================================================
  // THE ATLANTIC WALL
  // =====================================================================
  buildWall(scene, T, world, addBox, addTurret, bagArc);

  // =====================================================================
  // SEA: landing craft, ships
  // =====================================================================
  const lc = M.buildLCVP(true); lc.position.set(0, -0.5, -7); scene.add(lc);
  world.boats.push({ obj: lc, base: -0.5, phase: 0, main: true });
  for (let i = 0; i < 14; i++) {
    const b = M.buildLCVP(false);
    b.position.set(R.range(-200, 200), -0.5, R.range(-60, -260)); b.rotation.y = R.range(-0.3, 0.3);
    scene.add(b); world.boats.push({ obj: b, base: -0.5, phase: R() * TAU, drift: R.range(0.5, 1.2) });
  }
  for (let i = 0; i < 5; i++) {
    const s = M.buildShip(); s.position.set(-700 + i * 350 + R.range(-60, 60), -1.0, -900 - R() * 300); s.rotation.y = Math.PI / 2 + R.range(-0.2, 0.2);
    scene.add(s); world.boats.push({ obj: s, base: -1.0, phase: R() * TAU, ship: true });
  }

  // ---------------- finalize instanced meshes ----------------
  world.inst = {};
  for (const [name, b] of batches) {
    if (!b.list.length) continue;
    const im = new THREE.InstancedMesh(b.geo, b.material, b.list.length);
    b.list.forEach((e, i) => { im.setMatrixAt(i, e.m); if (e.color) im.setColorAt(i, e.color); });
    if (b.list[0].color) im.instanceColor.needsUpdate = true;
    im.castShadow = b.shadow; im.receiveShadow = true;
    im.computeBoundingSphere();
    scene.add(im);
    world.inst[name] = im;
  }
  // pickets
  const picketGeo = paint(new THREE.CylinderGeometry(0.035, 0.035, 1.5, 4), '#3b3632');
  const pm = new THREE.InstancedMesh(picketGeo, M.MAT.metal, pickets.length);
  pickets.forEach((m, i) => pm.setMatrixAt(i, m));
  pm.castShadow = true; scene.add(pm);
  // wire lines
  const lg = new THREE.BufferGeometry(); lg.setAttribute('position', new THREE.Float32BufferAttribute(lineVerts, 3));
  const lines = new THREE.LineSegments(lg, M.MAT.wire); lines.frustumCulled = false; scene.add(lines);

  world.hideMine = m => {
    const im = world.inst[m.batch]; if (!im || m.idx < 0) return;
    const z = new THREE.Matrix4().makeScale(0, 0, 0);
    if (m.batch === 'stake' || m.batch === 'ramp') {
      // knock the log over instead of hiding it
      const cur = new THREE.Matrix4(); im.getMatrixAt(m.idx, cur);
      const p = new THREE.Vector3(), q = new THREE.Quaternion(), s = new THREE.Vector3(); cur.decompose(p, q, s);
      q.multiply(new THREE.Quaternion().setFromEuler(new THREE.Euler(-1.1, 0, 0.3)));
      im.setMatrixAt(m.idx, new THREE.Matrix4().compose(p, q, s));
    } else im.setMatrixAt(m.idx, z);
    im.instanceMatrix.needsUpdate = true;
  };
  return world;
}

// =====================================================================
function buildWall(scene, T, world, addBox, addTurret, bagArc) {
  const Z0 = WALL_Z - 1.5, ZT = WALL_Z + 1.6, ZB = WALL_Z + 12;
  const tris = [];
  const cA = '#a29e92', cB = '#98948a', cDark = '#23211f', cTop = '#8e8a80';
  let REF = [0, 0, 0];
  const quad = (a, b, c, d, col) => { tris.push([a, b, c, col, REF], [a, c, d, col, REF]); };
  // a sloped-front block between x0..x1, from y0 to y1 (front bands for formwork lifts)
  const block = (x0, x1, y0, y1, zf0 = Z0, zft = ZT, zb = ZB, bands = true) => {
    const zAt = y => lerp(zf0, zft, (y - y0) / (y1 - y0));
    REF = [(x0 + x1) / 2, (y0 + y1) / 2, (zf0 + zb) / 2];
    const step = 2.2; let y = y0, k = 0;
    while (y < y1 - 0.01) {
      const yn = Math.min(y1, y + step);
      quad([x0, y, zAt(y)], [x1, y, zAt(y)], [x1, yn, zAt(yn)], [x0, yn, zAt(yn)], bands ? (k % 2 ? cA : cB) : cA);
      y = yn; k++;
    }
    quad([x0, y1, zft], [x1, y1, zft], [x1, y1, zb], [x0, y1, zb], cTop);
    quad([x0, y0, zb], [x1, y0, zb], [x1, y1, zb], [x0, y1, zb], cB);
    quad([x0, y0, zf0], [x0, y0, zb], [x0, y1, zb], [x0, y1, zft], cB);
    quad([x1, y0, zf0], [x1, y1, zft], [x1, y1, zb], [x1, y0, zb], cB);
  };
  const boxT = (cx, cy, cz, hx, hy, hz, col) => {
    const x0 = cx - hx, x1 = cx + hx, y0 = cy - hy, y1 = cy + hy, z0 = cz - hz, z1 = cz + hz;
    REF = [cx, cy, cz];
    quad([x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0], col);
    quad([x0, y0, z1], [x0, y1, z1], [x1, y1, z1], [x1, y0, z1], col);
    quad([x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1], col);
    quad([x0, y0, z0], [x0, y0, z1], [x1, y0, z1], [x1, y0, z0], col);
    quad([x0, y0, z0], [x0, y1, z0], [x0, y1, z1], [x0, y0, z1], col);
    quad([x1, y0, z0], [x1, y0, z1], [x1, y1, z1], [x1, y1, z0], col);
  };
  const PW = 24;
  const ground = x => Math.max(T.heightAt(x, WALL_Z - 2), T.heightAt(x, WALL_Z + 6));
  const tops = {};
  for (let x0 = -264; x0 < 264; x0 += PW) {
    const x1 = x0 + PW, xc = (x0 + x1) / 2;
    let g = -Infinity, gmin = Infinity;
    for (let x = x0; x <= x1; x += 4) { g = Math.max(g, ground(x)); gmin = Math.min(gmin, ground(x)); }
    const y0 = gmin - 6, top = Math.round(g + 17);
    tops[xc] = top;
    if (x0 < 0 && x1 > 0) {
      const gy = ground(0), gTop = gy + 11;
      block(x0, -GATE_HW, y0, top); block(GATE_HW, x1, y0, top);
      block(-GATE_HW, GATE_HW, gTop, top, lerp(Z0, ZT, (gTop - y0) / (top - y0)));
      // tunnel: floor, dark back wall, side liners
      boxT(0, gy - 0.1, WALL_Z + 6, GATE_HW, 0.12, 6.5, '#6c6860');
      boxT(0, gy + 5.5, ZB - 0.4, GATE_HW, 5.5, 0.3, cDark);
      boxT(0, gTop - 0.4, WALL_Z + 6, GATE_HW, 0.35, 6.4, '#4d4a45');
      addBox(-GATE_HW - (PW / 2 - GATE_HW) / 2 - 0.5, WALL_Z + 5, (PW / 2 - GATE_HW) / 2 + 0.5, 7, 0, 99, 'wall');
      addBox(GATE_HW + (PW / 2 - GATE_HW) / 2 + 0.5, WALL_Z + 5, (PW / 2 - GATE_HW) / 2 + 0.5, 7, 0, 99, 'wall');
      addBox(0, ZB + 1, GATE_HW, 1, 0, 99, 'wall');
      world.gate = { x: 0, z: WALL_Z, y: gy };
    } else {
      block(x0, x1, y0, top);
      addBox(xc, WALL_Z + 5, PW / 2, 7, 0, 99, 'wall');
    }
    // embrasures
    if (Math.abs(xc) > 20) for (const ex of [-6, 6]) {
      const ey = g + 8, ez = lerp(Z0, ZT, (ey - y0) / (top - y0)) - 0.12;
      boxT(xc + ex, ey, ez, 1.1, 0.28, 0.14, cDark);
    }
    // crenellated parapet
    for (let cx = x0 + 1.5; cx < x1; cx += 4) boxT(cx + 1, top + 0.7, ZT + 0.5, 1.0, 0.7, 0.5, cA);
    // buttress
    const bt = top + 1.5;
    boxT(x0, (y0 + bt) / 2, WALL_Z - 1.6, 1.6, (bt - y0) / 2, 2.4, cB);
    boxT(x0, bt + 0.25, WALL_Z - 1.6, 1.9, 0.25, 2.7, cTop);
    addBox(x0, WALL_Z - 1.6, 1.6, 2.4, 0, 99, 'wall');
  }
  // gate towers
  const gy = ground(0);
  for (const s of [-1, 1]) {
    const tx = s * (GATE_HW + 6), top = gy + 25;
    boxT(tx, (gy - 4 + top) / 2, WALL_Z - 2.5, 5.5, (top - gy + 4) / 2, 4.5, cB);
    boxT(tx, top + 0.4, WALL_Z - 2.5, 6.0, 0.4, 5.0, cTop);
    boxT(tx, gy + 12, WALL_Z - 7.05, 2.2, 0.35, 0.1, cDark);
    addBox(tx, WALL_Z - 2.5, 5.5, 4.5, 0, 99, 'wall');
  }
  const g2 = triGeo(tris, null); // every quad carries its primitive centre -> outward windings
  const wall = new THREE.Mesh(g2, M.MAT.flat); wall.castShadow = true; wall.receiveShadow = true;
  scene.add(wall);
  world.wallMesh = wall;

  // wall-top turrets
  WALL_TURRETS.forEach(x => {
    const xc = Math.floor((x + 264) / PW) * PW - 264 + PW / 2;
    const top = tops[xc] ?? gy + 17;
    const b = M.buildBunker();
    b.children[0].visible = false;
    b.position.set(x, top - 3.4, WALL_Z + 3.5);
    b.rotation.y = Math.PI;
    const base = new THREE.Mesh(paint(new THREE.BoxGeometry(4.5, 1.2, 4.5), '#8e8a80'), M.MAT.flat);
    base.position.set(x, top + 0.2, WALL_Z + 3.5); base.castShadow = true; scene.add(base);
    scene.add(b);
    addTurret(b, Math.PI, true);
  });
  for (const s of [-1, 1]) {
    const b = M.buildBunker(); b.children[0].visible = false;
    b.position.set(s * (GATE_HW + 6), gy + 25.8 - 3.4, WALL_Z - 2.5); b.rotation.y = Math.PI; scene.add(b);
    addTurret(b, Math.PI, true);
  }
  // finish beacon: light column + banner over the gate so it reads from far away
  const beam = new THREE.Mesh(new THREE.CylinderGeometry(3.5, 5.5, 160, 12, 1, true), new THREE.MeshBasicMaterial({ color: 0x4d80ff, transparent: true, opacity: 0.16, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide, fog: false }));
  beam.position.set(0, gy + 80, WALL_Z + 6); scene.add(beam);
  const bc = document.createElement('canvas'); bc.width = 512; bc.height = 96;
  const bx = bc.getContext('2d'); bx.fillStyle = '#1f5eff'; bx.fillRect(0, 0, 512, 96); bx.fillStyle = '#fff'; bx.font = 'bold 54px Arial'; bx.textAlign = 'center'; bx.fillText('BREACH  ▸  GATE', 256, 68);
  const btex = new THREE.CanvasTexture(bc); btex.colorSpace = THREE.SRGBColorSpace;
  const banner = new THREE.Mesh(new THREE.PlaneGeometry(16, 3), new THREE.MeshBasicMaterial({ map: btex, side: THREE.DoubleSide }));
  banner.position.set(0, gy + 12.6, WALL_Z - 0.6); banner.rotation.y = Math.PI; scene.add(banner);
  world.finishFlare = { x: 0, y: gy, z: WALL_Z + 1, beam };
}
