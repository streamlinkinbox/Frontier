// CPU-side rigid bodies + buoyancy probes driven by FluidSim point queries.
//
// The GPU answers point queries 1-3 frames late (async readback, never stalls the GPU). Probes
// query at positions predicted forward by that latency, which keeps buoyancy stable.
import { v3, quat } from './math.js';

export class RigidBody {
  constructor({ mass = 100, inertia = [10, 10, 10], position = [0, 0, 0], rotation = [0, 0, 0, 1] } = {}) {
    this.mass = mass;
    this.invMass = mass > 0 ? 1 / mass : 0;
    this.inertia = inertia;           // body-space principal moments
    this.position = position.slice();
    this.rotation = rotation.slice();
    this.linVel = [0, 0, 0];
    this.angVel = [0, 0, 0];          // world space
    this.force = [0, 0, 0];
    this.torque = [0, 0, 0];
    this.sleeping = false;
  }
  toWorld(p) { return v3.add(this.position, quat.rotate(this.rotation, p)); }
  dirToWorld(d) { return quat.rotate(this.rotation, d); }
  dirToLocal(d) { return quat.rotate(quat.conj(this.rotation), d); }
  pointVelocity(p) { return v3.add(this.linVel, v3.cross(this.angVel, v3.sub(p, this.position))); }
  addForce(f) { this.force[0] += f[0]; this.force[1] += f[1]; this.force[2] += f[2]; }
  addForceAtPoint(f, p) {
    this.addForce(f);
    const t = v3.cross(v3.sub(p, this.position), f);
    this.torque[0] += t[0]; this.torque[1] += t[1]; this.torque[2] += t[2];
  }
  addTorque(t) { this.torque[0] += t[0]; this.torque[1] += t[1]; this.torque[2] += t[2]; }
  /** apply an instantaneous impulse at a world point */
  applyImpulse(j, p) {
    this.linVel = v3.madd(this.linVel, j, this.invMass);
    const dw = this._invInertiaWorld(v3.cross(v3.sub(p, this.position), j));
    this.angVel = v3.add(this.angVel, dw);
  }
  _invInertiaWorld(t) {
    const tl = this.dirToLocal(t);
    const wl = [tl[0] / this.inertia[0], tl[1] / this.inertia[1], tl[2] / this.inertia[2]];
    return this.dirToWorld(wl);
  }
  integrate(dt, gravity = 9.81) {
    if (this.invMass === 0) return;
    this.linVel[0] += this.force[0] * this.invMass * dt;
    this.linVel[1] += (this.force[1] * this.invMass - gravity) * dt;
    this.linVel[2] += this.force[2] * this.invMass * dt;
    // gyroscopic term neglected (small bodies, short steps); semi-implicit Euler
    const dw = this._invInertiaWorld(this.torque);
    this.angVel = v3.madd(this.angVel, dw, dt);
    this.position = v3.madd(this.position, this.linVel, dt);
    this.rotation = quat.integrate(this.rotation, this.angVel, dt);
    this.force = [0, 0, 0];
    this.torque = [0, 0, 0];
  }
}

/**
 * Volume samples distributed over a box in body space. Each sample owns volume/N and responds to
 * the pressure head, density (mud!) and flow velocity reported by the solver.
 */
export class BuoyancyProbe {
  /**
   * @param {RigidBody} body
   * @param {object} o
   * @param {number[]} o.halfExtents     box enclosing the displaced volume (body space)
   * @param {number[]} [o.center]        box centre offset (body space)
   * @param {number[]} [o.grid]          samples per axis
   * @param {number} [o.volume]          displaced volume when fully submerged (default: box volume)
   * @param {number} [o.drag]            quadratic drag coefficient
   * @param {number} [o.linearDamping]   extra linear drag (N per m/s per m^3 submerged)
   */
  constructor(body, o) {
    this.body = body;
    this.half = o.halfExtents;
    this.center = o.center ?? [0, 0, 0];
    this.grid = o.grid ?? [3, 2, 3];
    this.volume = o.volume ?? 8 * this.half[0] * this.half[1] * this.half[2];
    this.drag = o.drag ?? 0.9;
    this.linearDamping = o.linearDamping ?? 250;
    this.angularDamping = o.angularDamping ?? 0.5;
    this.mudFactor = o.mudFactor ?? 1;
    const [gx, gy, gz] = this.grid;
    this.local = [];
    for (let k = 0; k < gz; k++) for (let j = 0; j < gy; j++) for (let i = 0; i < gx; i++) {
      this.local.push([
        this.center[0] + this.half[0] * (((i + 0.5) / gx) * 2 - 1),
        this.center[1] + this.half[1] * (((j + 0.5) / gy) * 2 - 1),
        this.center[2] + this.half[2] * (((k + 0.5) / gz) * 2 - 1),
      ]);
    }
    this.sampleHalfHeight = this.half[1] / gy;
    this.submerged = 0;      // fraction 0..1 from last apply()
    this.inMud = 0;
    this.lastWorld = this.local.map(() => [0, 0, 0]);
  }
  get count() { return this.local.length; }

  /** world xz of samples, predicted `lead` seconds ahead (query latency compensation) */
  writeQueryPoints(out, offset, lead = 0.033) {
    const b = this.body;
    const pp = v3.madd(b.position, b.linVel, lead);
    for (let s = 0; s < this.local.length; s++) {
      const w = v3.add(pp, quat.rotate(b.rotation, this.local[s]));
      out[(offset + s) * 2] = w[0];
      out[(offset + s) * 2 + 1] = w[2];
    }
  }

  /**
   * Apply buoyancy + drag from query results (8 floats per sample, see FluidSim.setQueryPoints).
   * @param {Float32Array} data
   * @param {number} offset  first sample index in data
   */
  apply(data, offset, { gravity = 9.81, mudCv = 0.39 } = {}) {
    const b = this.body;
    const vSample = this.volume / this.local.length;
    const area = Math.pow(vSample, 2 / 3);
    const hh = this.sampleHalfHeight;
    let sub = 0, mud = 0;
    for (let s = 0; s < this.local.length; s++) {
      const o = (offset + s) * 8;
      const head = data[o], depth = data[o + 1];
      if (!(depth > 0.002) || head < -1e3) continue;
      const p = b.toWorld(this.local[s]);
      // vertical extent of this sample in world space ~ hh (conservative for tilted bodies)
      const f = Math.min(1, Math.max(0, (head - (p[1] - hh)) / (2 * hh)));
      if (f <= 0) continue;
      const c = data[o + 4];
      const rho = 1000 + 1650 * mudCv * c;
      const Fb = rho * gravity * vSample * f;
      const flow = [data[o + 2], data[o + 7] * 0.5, data[o + 3]];
      const vr = v3.sub(b.pointVelocity(p), flow);
      const sp = v3.len(vr);
      const kq = 0.5 * rho * this.drag * area * f * sp;
      const kl = this.linearDamping * vSample * f * (1 + 20 * c * this.mudFactor);
      const F = [-(kq + kl) * vr[0], Fb - (kq + kl) * vr[1], -(kq + kl) * vr[2]];
      b.addForceAtPoint(F, p);
      sub += f; mud += c * f;
    }
    this.submerged = sub / this.local.length;
    this.inMud = sub > 0 ? mud / sub : 0;
    if (this.submerged > 0) {
      const ad = -this.angularDamping * this.submerged * b.mass;
      b.addTorque([b.angVel[0] * ad, b.angVel[1] * ad, b.angVel[2] * ad]);
    }
  }
}

/**
 * Collects probes into one query batch per frame and routes the (delayed) results back.
 * The layout tag ensures results are only applied when they match the submitted point layout.
 */
export class FluidQueryBatch {
  constructor(sim) {
    this.sim = sim;
    this.probes = [];
    this._buf = new Float32Array(sim.maxQueries * 2);
    this._layout = 0;
    this._offsets = new Map();
  }
  add(probe) { this.probes.push(probe); this._layout++; }
  /** replace the probe list; the layout version only changes when the list actually changes */
  setProbes(list) {
    if (list.length === this.probes.length && list.every((p, i) => p === this.probes[i])) return;
    this.probes = list.slice();
    this._layout++;
  }
  remove(probe) { this.probes = this.probes.filter((p) => p !== probe); this._layout++; }
  /** extra raw points (e.g. wheel contacts) can be registered as pseudo-probes {count, writeQueryPoints} */
  submit(lead = 0.033) {
    let off = 0;
    const offsets = new Map();
    for (const p of this.probes) {
      if (off + p.count > this.sim.maxQueries) break;
      p.writeQueryPoints(this._buf, off, lead);
      offsets.set(p, off);
      off += p.count;
    }
    this._offsets.set(this._layout, offsets);
    if (this._offsets.size > 8) this._offsets.delete(this._offsets.keys().next().value);
    this.sim.setQueryPoints(this._buf, off, this._layout);
  }
  /** returns { data, offset } for a probe, or null if no matching results are available yet */
  resultsFor(probe) {
    const r = this.sim.getQueryResults();
    if (!r || r.count === 0) return null;
    const offsets = this._offsets.get(r.tag);
    if (!offsets || !offsets.has(probe)) return null;
    return { data: r.data, offset: offsets.get(probe) };
  }
}
