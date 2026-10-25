// Frontier Fluid — buoyancy for rigid bodies.
//
// Archimedes done the game way: each body carries a set of probe points.
// Every step, each probe samples the simulated surface; submerged probes
// push up with (density * g * displaced volume) applied at the probe
// position (so torque — rocking on waves, righting moments — falls out
// naturally), plus hydrodynamic drag against the *simulated current*.
// Crates float down the river. Vehicles get slowed, floated and shoved
// sideways by the flow. All from FluidSim.sampleWater().

import type { FluidSim } from './FluidSim';
import { mat4FromQuat, type Mat4, type Quat, type Vec3, vec3 } from '../engine/math';

export interface BuoyancyBodyOptions {
  /** Half extents of the (box-approximated) body. */
  halfExtents: Vec3;
  mass?: number;
  /** Local-space probe points. Default: 4 bottom corners + centre. */
  probes?: Vec3[];
  /** Fraction of each probe's "column" that counts as displaced (hull shape). */
  volumeScale?: number;
  linearDrag?: number;   // in water, per second
  angularDrag?: number;
  airDrag?: number;
}

function quatMul(a: Quat, b: Quat): Quat {
  const [ax, ay, az, aw] = a, [bx, by, bz, bw] = b;
  return [
    aw * bx + ax * bw + ay * bz - az * by,
    aw * by - ax * bz + ay * bw + az * bx,
    aw * bz + ax * by - ay * bx + az * bw,
    aw * bw - ax * bx - ay * by - az * bz,
  ];
}
function quatRotate(q: Quat, v: Vec3): Vec3 {
  const [x, y, z, w] = q;
  const tx = 2 * (y * v[2] - z * v[1]);
  const ty = 2 * (z * v[0] - x * v[2]);
  const tz = 2 * (x * v[1] - y * v[0]);
  return [
    v[0] + w * tx + (y * tz - z * ty),
    v[1] + w * ty + (z * tx - x * tz),
    v[2] + w * tz + (x * ty - y * tx),
  ];
}

export class BuoyantBody {
  position: Vec3;
  orientation: Quat = [0, 0, 0, 1];
  velocity: Vec3 = [0, 0, 0];
  angularVelocity: Vec3 = [0, 0, 0];
  readonly halfExtents: Vec3;
  readonly mass: number;
  /** 0..1 — how submerged the body was last step (for audio/gameplay). */
  submersion = 0;
  private probes: Vec3[];
  private volumeScale: number;
  private linearDrag: number;
  private angularDrag: number;
  private airDrag: number;
  private invInertia: Vec3;

  constructor(position: Vec3, opts: BuoyancyBodyOptions) {
    this.position = [...position] as Vec3;
    this.halfExtents = opts.halfExtents;
    const [hx, hy, hz] = opts.halfExtents;
    this.mass = opts.mass ?? 40;
    this.volumeScale = opts.volumeScale ?? 0.85;
    this.linearDrag = opts.linearDrag ?? 1.6;
    this.angularDrag = opts.angularDrag ?? 2.2;
    this.airDrag = opts.airDrag ?? 0.04;
    this.probes = opts.probes ?? [
      [-hx * 0.8, -hy, -hz * 0.8], [hx * 0.8, -hy, -hz * 0.8],
      [-hx * 0.8, -hy, hz * 0.8], [hx * 0.8, -hy, hz * 0.8],
      [0, -hy, 0],
    ];
    const m = this.mass;
    const ix = (m / 12) * ((2 * hy) ** 2 + (2 * hz) ** 2);
    const iy = (m / 12) * ((2 * hx) ** 2 + (2 * hz) ** 2);
    const iz = (m / 12) * ((2 * hx) ** 2 + (2 * hy) ** 2);
    this.invInertia = [1 / ix, 1 / iy, 1 / iz];
  }

  get volume(): number {
    const [hx, hy, hz] = this.halfExtents;
    return 8 * hx * hy * hz * this.volumeScale;
  }

  /** Full probe-update: gravity + buoyancy + drag + ground rest. */
  update(dt: number, sim: FluidSim): void {
    const g = sim.settings.gravity;
    const rho = sim.settings.density;
    let force: Vec3 = [0, -this.mass * g, 0];
    let torque: Vec3 = [0, 0, 0];
    let submerged = 0;

    const probeVol = this.volume / this.probes.length;
    const bodyH = this.halfExtents[1] * 2;

    for (const lp of this.probes) {
      const r = quatRotate(this.orientation, lp);
      const wp = vec3.add(this.position, r);
      const sample = sim.sampleWater(wp[0], wp[2]);
      if (sample.height === -Infinity) continue;
      const depthBelow = sample.height - wp[1];
      if (depthBelow <= 0) continue;
      const frac = Math.min(depthBelow / bodyH, 1);
      submerged += frac / this.probes.length;

      // buoyancy (up) applied at the probe => torque rocks the body on waves
      const fUp = rho * g * probeVol * frac;
      const fB: Vec3 = [0, fUp, 0];

      // hydrodynamic drag toward the simulated current at this point
      const pv = vec3.add(this.velocity, vec3.cross(this.angularVelocity, r));
      const waterV: Vec3 = [sample.velocity[0], 0, sample.velocity[1]];
      const rel = vec3.sub(waterV, pv);
      const fD = vec3.scale(rel, this.linearDrag * this.mass * frac / this.probes.length);

      const f = vec3.add(fB, fD);
      force = vec3.add(force, f);
      torque = vec3.add(torque, vec3.cross(r, f));
    }
    this.submersion = submerged;

    // integrate
    const invM = 1 / this.mass;
    this.velocity = vec3.add(this.velocity, vec3.scale(force, invM * dt));
    const dragK = Math.exp(-this.airDrag * dt) * (submerged > 0 ? Math.exp(-this.linearDrag * 0.25 * dt) : 1);
    this.velocity = vec3.scale(this.velocity, dragK);

    // angular: torque in world space; inertia approx in world axes (fine for props)
    this.angularVelocity = vec3.add(this.angularVelocity, [
      torque[0] * this.invInertia[0] * dt,
      torque[1] * this.invInertia[1] * dt,
      torque[2] * this.invInertia[2] * dt,
    ]);
    const aDrag = Math.exp(-(0.15 + (submerged > 0 ? this.angularDrag : 0)) * dt);
    this.angularVelocity = vec3.scale(this.angularVelocity, aDrag);
    // clamp spin
    const wlen = vec3.length(this.angularVelocity);
    if (wlen > 6) this.angularVelocity = vec3.scale(this.angularVelocity, 6 / wlen);

    this.position = vec3.add(this.position, vec3.scale(this.velocity, dt));

    // orientation from angular velocity
    const wq: Quat = [this.angularVelocity[0], this.angularVelocity[1], this.angularVelocity[2], 0];
    const dq = quatMul(wq, this.orientation);
    let q: Quat = [
      this.orientation[0] + 0.5 * dq[0] * dt,
      this.orientation[1] + 0.5 * dq[1] * dt,
      this.orientation[2] + 0.5 * dq[2] * dt,
      this.orientation[3] + 0.5 * dq[3] * dt,
    ];
    const ql = Math.hypot(q[0], q[1], q[2], q[3]) || 1;
    q = [q[0] / ql, q[1] / ql, q[2] / ql, q[3] / ql];
    this.orientation = q;

    // rest on the bed when out of (or under shallow) water
    const bedY = sim.sampleBed(this.position[0], this.position[2]);
    const minY = bedY + this.halfExtents[1] * 0.92;
    if (this.position[1] < minY) {
      this.position[1] = minY;
      if (this.velocity[1] < 0) this.velocity[1] = 0;
      this.velocity[0] *= Math.exp(-6 * dt);
      this.velocity[2] *= Math.exp(-6 * dt);
      this.angularVelocity = vec3.scale(this.angularVelocity, Math.exp(-8 * dt));
      // settle upright slowly when beached
      const target: Quat = [0, this.orientation[1], 0, this.orientation[3]];
      const tl = Math.hypot(target[1], target[3]) || 1;
      const mixAmt = Math.min(1, dt * 2.5);
      const nq: Quat = [
        this.orientation[0] * (1 - mixAmt),
        this.orientation[1] + (target[1] / tl - this.orientation[1]) * mixAmt,
        this.orientation[2] * (1 - mixAmt),
        this.orientation[3] + (target[3] / tl - this.orientation[3]) * mixAmt,
      ];
      const nl = Math.hypot(nq[0], nq[1], nq[2], nq[3]) || 1;
      this.orientation = [nq[0] / nl, nq[1] / nl, nq[2] / nl, nq[3] / nl];
    }
  }

  matrix(scale: Vec3 = [1, 1, 1]): Mat4 {
    return mat4FromQuat(this.position, this.orientation, scale);
  }

  /** Interactor for FluidSim so the body displaces water back. */
  toInteractor(strength = 1): { position: Vec3; velocity: Vec3; radius: number } {
    return {
      position: this.position,
      velocity: this.velocity,
      radius: Math.max(this.halfExtents[0], this.halfExtents[2]),
    };
  }
}
