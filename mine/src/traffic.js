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

  rebuild(mine, count, seed = 3) {
    for (const t of this.trains) t.cars.forEach((c) => this.group.remove(c.mesh));
    this.trains = [];
    this.mine = mine;
    this.rnd = mulberry32(seed);
    this.outgoing = new Map();
    mine.lanes.forEach((l) => {
      if (!this.outgoing.has(l.from)) this.outgoing.set(l.from, []);
      this.outgoing.get(l.from).push(l);
    });
    const nTrains = Math.max(0, Math.round(count / 2.5));
    const lanes = mine.lanes.slice().sort(() => this.rnd() - 0.5);
    for (let i = 0; i < nTrains && i < lanes.length; i++) {
      const lane = lanes[i];
      const wagons = 1 + Math.floor(this.rnd() * 3);
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

  totalLen(train) { return train.segments.reduce((s, p) => s + p.length, 0); }

  pointAt(train, d, out) {
    for (const seg of train.segments) {
      if (d <= seg.length) return samplePath(seg, d, out);
      d -= seg.length;
    }
    const last = train.segments[train.segments.length - 1];
    return samplePath(last, last.length + d, out);
  }

  extend(train) {
    const last = train.segments[train.segments.length - 1];
    if (!last.lane) return; // transitions are always followed by a lane (added together)
    const node = last.lane.to;
    const p0 = last.pts[last.pts.length - 1], pm = last.pts[last.pts.length - 2];
    const tin = p0.clone().sub(pm).setY(0).normalize();
    const all = this.outgoing.get(node) || [];
    // no hairpins: carts only take turns up to ~115 degrees
    let options = all.filter((l) => l.edge !== last.lane.edge && tin.dot(l.pts[1].clone().sub(l.pts[0]).setY(0).normalize()) > -0.42);
    if (!options.length) options = all.filter((l) => l.edge !== last.lane.edge);
    if (!options.length) options = all;
    const next = options[Math.floor(this.rnd() * options.length)];
    const p3 = next.pts[0], p4 = next.pts[1];
    const t0 = p0.clone().sub(pm).normalize(), t3 = p4.clone().sub(p3).normalize();
    const dist = p0.distanceTo(p3);
    const c1 = p0.clone().addScaledVector(t0, dist * 0.42), c2 = p3.clone().addScaledVector(t3, -dist * 0.42);
    const pts = [];
    const n = Math.max(6, Math.ceil(dist / 0.8));
    for (let i = 0; i <= n; i++) {
      const t = i / n, it = 1 - t;
      pts.push(new THREE.Vector3()
        .addScaledVector(p0, it * it * it).addScaledVector(c1, 3 * it * it * t)
        .addScaledVector(c2, 3 * it * t * t).addScaledVector(p3, t * t * t));
    }
    train.segments.push(makePath(pts, null));
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
        this.pointAt(tr, tr.head + 7, tmp);
        for (const other of this.trains) {
          if (other === tr) continue;
          for (const c of other.cars) {
            if (c.pos.distanceToSquared(tmp) < 3.0 * 3.0 || (c.pos.distanceToSquared(head.pos) < 6.5 * 6.5 && tmp2.subVectors(c.pos, head.pos).dot(head.dir) > 1.0)) {
              blocked = true; break;
            }
          }
          if (blocked) break;
        }
      } else tr.ghost -= dt;
      if (blocked) { tr.wait += dt; if (tr.wait > 2.5) { tr.ghost = 2.5; tr.wait = 0; } }
      else tr.wait = 0;
      const target = blocked ? 0 : tr.cruise;
      tr.speed += THREE.MathUtils.clamp(target - tr.speed, -12 * dt, 3 * dt);
      tr.head += tr.speed * dt;
      while (tr.head + 8 > this.totalLen(tr)) this.extend(tr);
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
        if (yf !== null) f.y = yf; if (yb !== null) b.y = yb;
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
