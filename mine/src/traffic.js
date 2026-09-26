// Mine-train traffic. Trains (loco + 1-3 ore wagons) run on the cart lanes
// (right-hand running, the grooves in the road), take random turns at the
// junctions through smooth Bezier transitions, and keep distance from each other.
import * as THREE from 'three';
import { mulberry32 } from './noise.js';

const _v = new THREE.Vector3();
const DOWN = new THREE.Vector3(0, -1, 0);

function makePath(pts, lane = null) {
  const cum = [0];
  for (let i = 1; i < pts.length; i++) cum.push(cum[i - 1] + pts[i].distanceTo(pts[i - 1]));
  return { pts, cum, length: cum[cum.length - 1], lane };
}
function samplePath(path, d, out) {
  const { pts, cum } = path;
  if (d <= 0) return out.copy(pts[0]);
  if (d >= path.length) return out.copy(pts[pts.length - 1]);
  let lo = 0, hi = cum.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= d) lo = m; else hi = m; }
  const t = (d - cum[lo]) / (cum[hi] - cum[lo] || 1);
  return out.copy(pts[lo]).lerp(pts[hi], t);
}

export class Traffic {
  constructor(scene, world) {
    this.scene = scene;
    this.world = world;
    this.group = new THREE.Group();
    scene.add(this.group);
    this.trains = [];
    this.materials = makeMaterials();
  }

  // opts: number of trains, or { count, speed, wagons, seed }
  rebuild(mine, opts = {}, seed = 3) {
    this.setMine(mine);
    if (typeof opts === 'number') opts = { count: opts, seed };
    this.spawn(opts);
  }

  // new mine geometry: lane graph + clearance-checked junction routes
  setMine(mine) {
    this.mine = mine;
    this.outgoing = new Map();
    mine.lanes.forEach((l) => {
      if (!this.outgoing.has(l.from)) this.outgoing.set(l.from, []);
      this.outgoing.get(l.from).push(l);
    });
    this.computeAllowed();
  }

  setSpeed(mul) { this.speedMul = mul; }

  // (re)populate the trains; cheap, no mine rebuild needed
  spawn({ count = 10, speed = 1, wagons: maxWagons = 3, seed = 3 } = {}) {
    for (const t of this.trains) t.cars.forEach((c) => this.group.remove(c.mesh));
    this.trains = [];
    this.locks = new Map(); // hub node -> train holding the junction
    this.queues = new Map(); // hub node -> trains waiting (FIFO)
    this.speedMul = speed;
    this.rnd = mulberry32(seed);
    const mine = this.mine;
    const nTrains = Math.max(0, Math.round(count));
    const lanes = mine.lanes.slice().sort(() => this.rnd() - 0.5);
    for (let i = 0; i < nTrains && i < lanes.length; i++) {
      const lane = lanes[i];
      const wagons = 1 + Math.floor(this.rnd() * Math.max(1, maxWagons));
      const train = {
        segments: [makePath(lane.pts, lane)],
        head: 0,
        speed: 0,
        cruise: 8 + this.rnd() * 5,
        cars: [],
        wait: 0,
        ghost: 0,
      };
      const len = train.segments[0].length;
      train.head = Math.min(len * 0.5, 6 + wagons * 3.2 + this.rnd() * Math.max(0, len - 20));
      const kinds = ['loco'];
      for (let w = 0; w < wagons; w++) kinds.push('wagon');
      let off = 0;
      kinds.forEach((kind, idx) => {
        const mesh = kind === 'loco' ? makeLoco(this.materials) : makeWagon(this.materials, this.rnd);
        this.group.add(mesh);
        const half = kind === 'loco' ? 1.35 : 1.25;
        if (idx > 0) off += half;
        train.cars.push({ kind, mesh, offset: off, half: new THREE.Vector3(0.78, 0.8, half), pos: new THREE.Vector3(), dir: new THREE.Vector3(0, 0, 1), vel: new THREE.Vector3() });
        off += half + 0.5;
      });
      this.trains.push(train);
    }
    this.update(0);
  }

  interlock(tr) {
    if (!this.locks) this.locks = new Map();
    if (!this.queues) this.queues = new Map();
    const held = tr.held || (tr.held = new Set());
    const last = tr.cars[tr.cars.length - 1];
    const tailD = tr.head - last.offset - last.half.z - 0.5;
    const still = new Set();
    let acc = 0, next = null, nextDist = 0;
    for (const seg of tr.segments) {
      if (seg.hub !== undefined) {
        const s0 = acc, s1 = acc + seg.length;
        if (s1 > tailD) still.add(seg.hub); // not yet cleared by the last wagon (incl. reserved ahead)
        if (next === null && s0 > tr.head - 0.01) { next = seg.hub; nextDist = s0 - tr.head; }
      }
      acc += seg.length;
    }
    for (const h of held) if (!still.has(h)) { held.delete(h); if (this.locks.get(h) === tr) this.locks.delete(h); }
    const stop = (tr.speed * tr.speed) / 20; // braking distance at 10 m/s^2 (14 available)
    const inRange = next !== null && nextDist <= stop + 9 && !held.has(next);
    // FIFO queue per junction (no starvation of trains waiting at the signal)
    if (tr.queued !== undefined && (!inRange || tr.queued !== next)) this.dequeue(tr);
    if (!inRange || tr.ghost > 0) return false;
    if (tr.queued === undefined) { tr.queued = next; (this.queues.get(next) || this.queues.set(next, []).get(next)).push(tr); }
    const q = this.queues.get(next);
    const holder = this.locks.get(next);
    const free = !holder || !this.trains.includes(holder);
    tr.ready = this.exitClear(tr, nextDist); // don't block the box
    // an earlier queued train that could go right now has priority
    const earlierReady = q.slice(0, q.indexOf(tr)).some((o) => o.ready && this.trains.includes(o));
    if (free && tr.ready && !earlierReady) {
      this.locks.set(next, tr); held.add(next); this.dequeue(tr);
      return false;
    }
    return nextDist < stop + 4.5; // wait at the "signal", clear of the junction
  }

  overlapsAny(tr) {
    for (const other of this.trains) {
      if (other === tr || other.cars[0].pos.distanceToSquared(tr.cars[0].pos) > 60 * 60) continue;
      for (const c of other.cars) for (const m of tr.cars) if (c.pos.distanceToSquared(m.pos) < 2.6 * 2.6) return true;
    }
    return false;
  }

  dequeue(tr) {
    const q = this.queues.get(tr.queued);
    if (q) { const k = q.indexOf(tr); if (k >= 0) q.splice(k, 1); }
    tr.queued = undefined; tr.ready = false;
  }

  // is there room for the whole train beyond the junction route starting at `d0`?
  exitClear(tr, d0) {
    let acc = 0, transLen = 12;
    for (const seg of tr.segments) { if (seg.hub !== undefined && acc >= tr.head + d0 - 0.02) { transLen = seg.length; break; } acc += seg.length; }
    const last = tr.cars[tr.cars.length - 1];
    const need = d0 + transLen + last.offset + last.half.z + 5;
    while (this.totalLen(tr) < tr.head + need + 1) this.extend(tr);
    const p = new THREE.Vector3();
    for (let d = d0; d <= need; d += 1.5) {
      this.pointAt(tr, tr.head + d, p);
      for (const other of this.trains) {
        if (other === tr || other.cars[0].pos.distanceToSquared(p) > (other.cars[other.cars.length - 1].offset + 6) ** 2) continue;
        for (const c of other.cars) if (c.pos.distanceToSquared(p) < 2.0 * 2.0) return false;
      }
    }
    return true;
  }

  totalLen(train) { return train.segments.reduce((s, p) => s + p.length, 0); }

  pointAt(train, d, out) {
    for (const seg of train.segments) {
      if (d <= seg.length) return samplePath(seg, d, out);
      d -= seg.length;
    }
    const last = train.segments[train.segments.length - 1];
    return samplePath(last, last.length + d, out);
  }

  // Bezier transition from the end of lane `lin` to the start of lane `lout`
  transitionPts(lin, lout, handle = 0.42) {
    const p0 = lin.pts[lin.pts.length - 1], pm = lin.pts[lin.pts.length - 2];
    const p3 = lout.pts[0], p4 = lout.pts[1];
    const t0 = p0.clone().sub(pm).normalize(), t3 = p4.clone().sub(p3).normalize();
    const dist = p0.distanceTo(p3);
    const c1 = p0.clone().addScaledVector(t0, dist * handle), c2 = p3.clone().addScaledVector(t3, -dist * handle);
    const pts = [];
    const n = Math.max(8, Math.ceil(dist * (1 + handle) / 0.8));
    for (let i = 0; i <= n; i++) {
      const t = i / n, it = 1 - t;
      pts.push(new THREE.Vector3()
        .addScaledVector(p0, it * it * it).addScaledVector(c1, 3 * it * it * t)
        .addScaledVector(c2, 3 * it * t * t).addScaledVector(p3, t * t * t));
    }
    return pts;
  }

  // which junction moves are allowed: no U-turns, no hairpins (> ~115 deg), and
  // the swept cart body must clear the actual cave mesh
  computeAllowed() {
    this.allowed = new Map();
    this.transitionCache = new Map();
    const probe = new THREE.Vector3();
    for (const lin of this.mine.lanes) {
      const p0 = lin.pts[lin.pts.length - 1], pm = lin.pts[lin.pts.length - 2];
      const tin = p0.clone().sub(pm).setY(0).normalize();
      const ok = [];
      for (const lout of this.outgoing.get(lin.to) || []) {
        if (lout.edge === lin.edge) continue;
        const tout = lout.pts[1].clone().sub(lout.pts[0]).setY(0).normalize();
        if (tin.dot(tout) <= -0.9) continue; // no U-turns
        // try a tight arc first, then wider swings through the junction (hairpins)
        let pts = null;
        for (const handle of [0.42, 0.7, 1.0, 1.35]) {
          const cand = this.transitionPts(lin, lout, handle);
          let clear = true;
          for (const p of cand) {
            const hit = this.world.sphereContact(probe.set(p.x, p.y + 1.3, p.z), 1.0);
            if (hit && hit.depth > 0.2) { clear = false; break; } // wall closer than ~0.8 m (cart half-width)
          }
          if (clear) { pts = cand; break; }
        }
        if (!pts) continue;
        ok.push(lout);
        this.transitionCache.set(lin, (this.transitionCache.get(lin) || new Map()).set(lout, pts));
      }
      this.allowed.set(lin, ok);
    }
  }

  extend(train) {
    const last = train.segments[train.segments.length - 1];
    if (!last.lane) return; // transitions are always followed by a lane (added together)
    const node = last.lane.to;
    let options = this.allowed ? this.allowed.get(last.lane) : null;
    if (!options || !options.length) options = (this.outgoing.get(node) || []).filter((l) => l.edge !== last.lane.edge);
    if (!options.length) options = this.outgoing.get(node) || [];
    const next = options[Math.floor(this.rnd() * options.length)];
    const cached = this.transitionCache && this.transitionCache.get(last.lane);
    const pts = (cached && cached.get(next)) || this.transitionPts(last.lane, next);
    const tseg = makePath(pts, null);
    tseg.hub = node; // junction route: needs the hub interlock
    train.segments.push(tseg);
    train.segments.push(makePath(next.pts, next));
  }

  update(dt, carPos = null) {
    const tmp = new THREE.Vector3(), tmp2 = new THREE.Vector3();
    // leader positions for spacing
    for (const tr of this.trains) {
      const head = tr.cars[0];
      // look ahead for other trains
      let blocked = false;
      if (tr.ghost <= 0) {
        const look = 7 + (tr.speed * tr.speed) / (2 * 14); // braking distance at 14 m/s^2
        this.pointAt(tr, tr.head + look, tmp);
        // sample the path ahead once (only needed beyond the default 7 m look-ahead)
        const ahead = [];
        for (let d = 9; d <= look; d += 2) ahead.push(this.pointAt(tr, tr.head + d, new THREE.Vector3()));
        const reach2 = (look + 4) * (look + 4);
        for (const other of this.trains) {
          if (other === tr) continue;
          const oLen = other.cars[other.cars.length - 1].offset + 3;
          if (other.cars[0].pos.distanceToSquared(head.pos) > (look + oLen + 6) ** 2) continue; // far away
          for (const c of other.cars) {
            // close range: in front AND on our track (not the parallel track 3.5 m to the side)
            tmp2.subVectors(c.pos, head.pos);
            const fwd = tmp2.dot(head.dir);
            const lat2 = tmp2.lengthSq() - fwd * fwd;
            if (c.pos.distanceToSquared(tmp) < 2.2 * 2.2 || (fwd > 1.0 && fwd < 6.5 && lat2 < 1.9 * 1.9)) {
              blocked = true; break;
            }
            if (ahead.length && c.pos.distanceToSquared(head.pos) < reach2) {
              for (const a of ahead) if (a.distanceToSquared(c.pos) < 2.2 * 2.2) { blocked = true; break; }
              if (blocked) break;
            }
          }
          if (blocked) break;
        }
      } else {
        tr.ghost -= dt;
        // stay "ghosted" until clear of every other train (no half-overlapping restarts)
        if (tr.ghost <= 0 && this.overlapsAny(tr)) tr.ghost = 0.25;
      }
      // junction interlocking: reserve the hub before entering a switch route,
      // release it once the last wagon has cleared it -> no crossing collisions
      const lockStop = this.interlock(tr);
      if (lockStop) blocked = true;
      // deadlock breaker: after a long wait, pass through ("ghost") for a moment
      if (blocked) { tr.wait += dt; if (tr.wait > 10) { tr.ghost = 2.5; tr.wait = 0; } }
      else tr.wait = 0;
      // slow down through junction transitions (tighter curves, crossing traffic)
      let d = tr.head + 4, onTransition = false;
      for (const seg of tr.segments) { if (d <= seg.length) { onTransition = !seg.lane; break; } d -= seg.length; }
      const mul = this.speedMul ?? 1;
      const cruise = tr.cruise * mul;
      const target = blocked ? 0 : (onTransition ? Math.min(cruise, 6.5 * Math.sqrt(Math.max(mul, 0.3))) : cruise);
      tr.speed += THREE.MathUtils.clamp(target - tr.speed, -14 * dt, 3 * Math.max(1, mul) * dt);
      tr.head += tr.speed * dt;
      while (tr.head + 12 + (tr.speed * tr.speed) / 28 > this.totalLen(tr)) this.extend(tr);
      // drop segments the last wagon has left
      const tail = tr.cars[tr.cars.length - 1].offset + 4;
      while (tr.segments.length > 2 && tr.head - tail > tr.segments[0].length) {
        tr.head -= tr.segments[0].length;
        tr.segments.shift();
      }
      for (const c of tr.cars) {
        const d = tr.head - c.offset;
        const f = this.pointAt(tr, d + c.half.z * 0.8, new THREE.Vector3());
        const b = this.pointAt(tr, d - c.half.z * 0.8, new THREE.Vector3());
        // settle on the actual floor
        const yf = this.groundY(f), yb = this.groundY(b);
        if (yf !== null) f.y = yf + 0.045; if (yb !== null) b.y = yb + 0.045; // wheels on the rail heads
        const prev = c.pos.clone();
        c.pos.copy(f).add(b).multiplyScalar(0.5);
        c.dir.subVectors(f, b).normalize();
        if (dt > 0) c.vel.subVectors(c.pos, prev).divideScalar(dt);
        c.mesh.position.copy(c.pos);
        c.mesh.lookAt(_v.copy(c.pos).add(c.dir));
        if (c.mesh.userData.wheels) c.mesh.userData.wheels.forEach((w) => { w.rotation.x += tr.speed * dt / 0.25; });
      }
    }
  }

  groundY(p) {
    const hit = this.world.raycast(_v.set(p.x, p.y + 1.5, p.z), DOWN, 4);
    return hit ? hit.point.y : null;
  }

  // iterate all cart OBBs (for collision + minimap)
  forEachCar(fn) { for (const t of this.trains) for (const c of t.cars) fn(c, t); }

  // nearest cart lights (for the dynamic light pool)
  lightSources() {
    const out = [];
    for (const t of this.trains) {
      const c = t.cars[0];
      out.push({ p: c.pos.clone().addScaledVector(c.dir, 1.5).add(new THREE.Vector3(0, 1.3, 0)), color: 0xffd28a, cart: true });
    }
    return out;
  }
}

function makeMaterials() {
  return {
    iron: new THREE.MeshStandardMaterial({ color: 0x3a3430, roughness: 0.6, metalness: 0.7 }),
    rust: new THREE.MeshStandardMaterial({ color: 0x6b3a1f, roughness: 0.85, metalness: 0.35 }),
    yellow: new THREE.MeshStandardMaterial({ color: 0xd9a300, roughness: 0.5, metalness: 0.3 }),
    dark: new THREE.MeshStandardMaterial({ color: 0x151515, roughness: 0.8, metalness: 0.4 }),
    ore: new THREE.MeshStandardMaterial({ color: 0x3b3129, roughness: 0.95, flatShading: true }),
    gold: new THREE.MeshStandardMaterial({ color: 0xffc34d, roughness: 0.3, metalness: 1, emissive: 0x4a2c00, emissiveIntensity: 0.6 }),
    lamp: new THREE.MeshStandardMaterial({ color: 0xffffff, emissive: 0xffe2a8, emissiveIntensity: 8 }),
    beacon: new THREE.MeshStandardMaterial({ color: 0x331100, emissive: 0xff8a00, emissiveIntensity: 6 }),
    red: new THREE.MeshStandardMaterial({ color: 0x330000, emissive: 0xff1100, emissiveIntensity: 3 }),
  };
}

function addWheels(g, M, zs) {
  const wg = new THREE.CylinderGeometry(0.25, 0.25, 0.1, 12);
  wg.rotateZ(Math.PI / 2);
  const wheels = [];
  for (const z of zs) for (const x of [-0.5, 0.5]) {
    const w = new THREE.Mesh(wg, M.dark);
    w.position.set(x, 0.22, z);
    g.add(w); wheels.push(w);
  }
  g.userData.wheels = wheels;
}

function makeWagon(M, rnd) {
  const g = new THREE.Group();
  const box = (w, h, d, m, x, y, z) => { const b = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), m); b.position.set(x, y, z); b.castShadow = true; g.add(b); return b; };
  box(1.1, 0.14, 2.3, M.dark, 0, 0.42, 0);
  // tapered tub (trapezoid via scaled box geometry)
  const tub = new THREE.BoxGeometry(1.5, 0.95, 2.4);
  const pos = tub.attributes.position;
  for (let i = 0; i < pos.count; i++) if (pos.getY(i) < 0) { pos.setX(i, pos.getX(i) * 0.72); pos.setZ(i, pos.getZ(i) * 0.86); }
  tub.computeVertexNormals();
  const t = new THREE.Mesh(tub, M.rust); t.position.y = 0.98; t.castShadow = true; g.add(t);
  box(1.56, 0.08, 2.46, M.iron, 0, 1.48, 0); // rim
  for (const z of [-0.8, 0, 0.8]) box(1.52, 0.9, 0.06, M.iron, 0, 1.0, z).scale.x = 0.9;
  // ore heap
  const heap = new THREE.Mesh(new THREE.DodecahedronGeometry(0.72, 1), M.ore);
  heap.scale.set(0.95, 0.42, 1.45); heap.position.y = 1.5; g.add(heap);
  for (let i = 0; i < 5; i++) {
    const n = new THREE.Mesh(new THREE.DodecahedronGeometry(0.09 + rnd() * 0.06, 0), M.gold);
    n.position.set((rnd() - 0.5) * 0.9, 1.72 + rnd() * 0.05, (rnd() - 0.5) * 1.6); g.add(n);
  }
  box(0.1, 0.1, 0.35, M.dark, 0, 0.45, 1.3); box(0.1, 0.1, 0.35, M.dark, 0, 0.45, -1.3); // couplers
  addWheels(g, M, [-0.75, 0.75]);
  return g;
}

function makeLoco(M) {
  const g = new THREE.Group();
  const box = (w, h, d, m, x, y, z) => { const b = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), m); b.position.set(x, y, z); b.castShadow = true; g.add(b); return b; };
  box(1.3, 0.25, 2.6, M.dark, 0, 0.45, 0);
  box(1.4, 0.8, 1.5, M.yellow, 0, 0.98, 0.45);   // engine hood
  box(1.45, 1.25, 1.0, M.yellow, 0, 1.2, -0.75); // cab
  box(1.25, 0.08, 1.1, M.dark, 0, 1.86, -0.75);
  box(0.9, 0.45, 0.04, M.dark, 0, 1.45, -0.24);  // window
  box(0.3, 0.2, 0.06, M.lamp, 0, 1.08, 1.22);    // headlamp
  box(0.18, 0.12, 0.18, M.beacon, 0, 1.96, -0.75); // beacon
  box(0.2, 0.14, 0.04, M.red, 0.45, 0.8, -1.27);
  box(0.2, 0.14, 0.04, M.red, -0.45, 0.8, -1.27);
  // hazard stripes on the front
  for (let i = 0; i < 4; i++) box(0.18, 0.2, 0.04, i % 2 ? M.dark : M.yellow, -0.45 + i * 0.3, 0.45, 1.32);
  addWheels(g, M, [-0.8, 0.8]);
  return g;
}
