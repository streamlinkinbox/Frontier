// Floating props (crates, barrels) – rigid bodies with buoyancy probes, terrain contact and
// collisions with the car. They displace water (ripples, waves when dropped) like the car does.
import { RigidBody, BuoyancyProbe } from '../src/Buoyancy.js';
import { v3, quat, mat4 } from '../src/math.js';

const TYPES = {
  crate: { half: [0.45, 0.45, 0.45], density: 430, color: [0.32, 0.17, 0.065], params: [0.75, 0, 1, 0], mesh: 'box', grid: [3, 3, 3] },
  barrel: { half: [0.45, 0.29, 0.29], density: 260, color: [0.55, 0.09, 0.02], params: [0.45, 0.25, 3, 0], mesh: 'cylinder', grid: [3, 2, 2], volumeScale: 0.785 },
  ball: { half: [0.6, 0.6, 0.6], density: 2400, color: [0.08, 0.08, 0.09], params: [0.3, 0.6, 0, 0], mesh: 'sphere', grid: [2, 2, 2], volumeScale: 0.52 },
};

export class Props {
  constructor(scene) {
    this.scene = scene;
    this.items = [];
  }

  spawn(type, position, { rotation = null, velocity = [0, 0, 0], angVel = null } = {}) {
    const T = TYPES[type];
    const [hx, hy, hz] = T.half;
    const vol = 8 * hx * hy * hz * (T.volumeScale ?? 1);
    const mass = vol * T.density;
    const body = new RigidBody({
      mass, position,
      rotation: rotation ?? quat.normalize([Math.random() - 0.5, Math.random() - 0.5, Math.random() - 0.5, 2]),
      inertia: [mass / 3 * (hy * hy + hz * hz), mass / 3 * (hx * hx + hz * hz), mass / 3 * (hx * hx + hy * hy)],
    });
    body.linVel = velocity.slice();
    if (angVel) body.angVel = angVel.slice();
    const probe = new BuoyancyProbe(body, { halfExtents: T.half, grid: T.grid, volume: vol, drag: 1.0, linearDamping: 120, angularDamping: 0.25 });
    const item = { type, T, body, probe, radius: Math.hypot(hx, hy, hz), corners: corners(T.half), age: 0 };
    this.items.push(item);
    return item;
  }

  remove(item) { this.items = this.items.filter((i) => i !== item); }

  update(dt, vehicle, batch) {
    const sub = 2, h = dt / sub;
    for (const it of this.items) {
      it.age += dt;
      const r = batch.resultsFor(it.probe);
      for (let s = 0; s < sub; s++) {
        if (r) it.probe.apply(r.data, r.offset);
        this._ground(it);
        if (vehicle) this._collideVehicle(it, vehicle);
        it.body.integrate(h);
      }
    }
    // prop–prop
    for (let a = 0; a < this.items.length; a++) for (let b = a + 1; b < this.items.length; b++) this._collidePair(this.items[a], this.items[b]);
    // out of bounds -> remove
    const { origin, size } = this.scene;
    this.items = this.items.filter((it) => {
      const p = it.body.position;
      return p[1] > -25 && p[0] > origin[0] - 5 && p[2] > origin[1] - 5 && p[0] < origin[0] + size + 5 && p[2] < origin[1] + size + 5;
    });
  }

  _ground(it) {
    const b = it.body;
    for (const c of it.corners) {
      const p = b.toWorld(c);
      const gh = this.scene.heightAt(p[0], p[2]);
      const pen = gh - p[1];
      if (pen > 0) {
        const v = b.pointVelocity(p);
        const k = b.mass * 900, damp = b.mass * 40;
        const Fn = Math.max(0, k * pen - damp * v[1]);
        const vt = [v[0], 0, v[2]];
        const vtl = Math.hypot(vt[0], vt[2]);
        const Ft = vtl > 1e-3 ? v3.scale(vt, -Math.min(0.55 * Fn, b.mass * 30 * vtl) / vtl) : [0, 0, 0];
        b.addForceAtPoint([Ft[0], Fn, Ft[2]], p);
      }
    }
  }

  _collideVehicle(it, veh) {
    const vb = veh.body, b = it.body;
    // prop sphere vs. car OBB
    const half = [1.0, 0.75, 2.35];
    const lp = vb.dirToLocal(v3.sub(b.position, v3.add(vb.position, vb.dirToWorld([0, 0.1, 0]))));
    const cl = [Math.max(-half[0], Math.min(half[0], lp[0])), Math.max(-half[1], Math.min(half[1], lp[1])), Math.max(-half[2], Math.min(half[2], lp[2]))];
    const dl = v3.sub(lp, cl);
    const dist = v3.len(dl);
    const r = it.radius * 0.85;
    if (dist < r) {
      const nL = dist > 1e-4 ? v3.scale(dl, 1 / dist) : [0, 1, 0];
      const n = vb.dirToWorld(nL);
      const pen = r - dist;
      const contact = v3.add(vb.position, vb.dirToWorld(v3.add(cl, [0, 0.1, 0])));
      const vrel = v3.sub(b.pointVelocity(b.position), vb.pointVelocity(contact));
      const vn = v3.dot(vrel, n);
      b.position = v3.madd(b.position, n, pen * 0.8);
      if (vn < 0) {
        const j = -(1 + 0.3) * vn / (b.invMass + vb.invMass);
        b.applyImpulse(v3.scale(n, j), b.position);
        vb.applyImpulse(v3.scale(n, -j), contact);
        // spin the prop a bit
        b.angVel = v3.add(b.angVel, v3.scale(v3.cross(n, vrel), 0.3));
      }
    }
  }

  _collidePair(a, c) {
    const d = v3.sub(c.body.position, a.body.position);
    const dist = v3.len(d), r = (a.radius + c.radius) * 0.8;
    if (dist < r && dist > 1e-4) {
      const n = v3.scale(d, 1 / dist);
      const pen = r - dist;
      const tm = a.body.invMass + c.body.invMass;
      a.body.position = v3.madd(a.body.position, n, -pen * a.body.invMass / tm);
      c.body.position = v3.madd(c.body.position, n, pen * c.body.invMass / tm);
      const vn = v3.dot(v3.sub(c.body.linVel, a.body.linVel), n);
      if (vn < 0) {
        const j = -(1.2) * vn / tm;
        a.body.linVel = v3.madd(a.body.linVel, n, -j * a.body.invMass);
        c.body.linVel = v3.madd(c.body.linVel, n, j * c.body.invMass);
      }
    }
  }

  primitives() {
    return this.items.map((it) => {
      const b = it.body, T = it.T;
      const base = { position: b.position, rotation: b.rotation, velocity: b.linVel, angularVelocity: b.angVel };
      if (T.mesh === 'cylinder') return { ...base, type: 'cylinder', radius: T.half[1], halfLength: T.half[0] };
      if (T.mesh === 'sphere') return { ...base, type: 'sphere', radius: T.half[0] };
      return { ...base, type: 'box', halfExtents: T.half };
    });
  }

  instances() {
    return this.items.map((it) => ({
      mesh: it.T.mesh,
      matrix: mat4.fromRotationTranslation(it.body.rotation, it.body.position, it.T.mesh === 'cylinder' ? [it.T.half[0] * 2, it.T.half[1] * 2, it.T.half[2] * 2] : it.T.half.map((x) => x * 2)),
      color: it.T.color,
      params: it.T.params,
    }));
  }
}

function corners(h) {
  const out = [];
  for (const x of [-1, 1]) for (const y of [-1, 1]) for (const z of [-1, 1]) out.push([x * h[0], y * h[1], z * h[2]]);
  return out;
}
