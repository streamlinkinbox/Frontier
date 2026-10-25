// Demo truck: arcade-but-water-aware. On land it drives; in water it gets
// drag, buoyancy and current push from the actual simulation; in mud it
// bogs. Its wheels + hull are FluidSim interactors, so bow waves, rooster
// tails, wheel churn and wakes are all produced by the solver.
import type { FluidSim } from '../fluid/FluidSim';
import type { FluidInteractor } from '../fluid/types';
import { clamp, lerp, mat4FromTRS, mat4Multiply, vec3, type Mat4, type Vec3 } from '../engine/math';

export interface VehicleInput {
  throttle: number; // -1..1
  steer: number;    // -1..1
  handbrake: boolean;
}

const WHEEL_R = 0.42;
const CLEARANCE = 0.58;
const WHEEL_OFFSETS: Array<[number, number, boolean]> = [
  [0.98, 1.42, true], [-0.98, 1.42, true], [0.98, -1.42, false], [-0.98, -1.42, false],
];

export class Vehicle {
  pos: Vec3 = [0, 2, 0];
  yaw = 0;
  vel: Vec3 = [0, 0, 0];
  vy = 0;
  steerVis = 0;
  wheelSpin = 0;
  speedFwd = 0;
  submersion = 0;   // 0..1 hull submersion (HUD/audio)
  floating = false;
  slipping = false;
  private pitch = 0;
  private roll = 0;
  private prevVy = 0;

  respawn(x: number, z: number, yaw: number, sim: FluidSim): void {
    this.pos = [x, sim.sampleBed(x, z) + CLEARANCE + 0.2, z];
    this.yaw = yaw;
    this.vel = [0, 0, 0];
    this.vy = 0;
    this.pitch = 0;
    this.roll = 0;
  }

  forward(): Vec3 { return [Math.sin(this.yaw), 0, Math.cos(this.yaw)]; }
  right(): Vec3 { const f = this.forward(); return vec3.normalize(vec3.cross([0, 1, 0], f)); }

  private wheelWorld(ox: number, oz: number): Vec3 {
    const f = this.forward(), r = this.right();
    return [this.pos[0] + r[0] * ox + f[0] * oz, this.pos[1], this.pos[2] + r[2] * ox + f[2] * oz];
  }

  update(dt: number, input: VehicleInput, sim: FluidSim): void {
    dt = Math.min(dt, 0.05);
    const f = this.forward();
    const r = this.right();

    // --- ground & water sampling
    const wheelGround = WHEEL_OFFSETS.map(([ox, oz]) => {
      const w = this.wheelWorld(ox, oz);
      return sim.sampleBed(w[0], w[2]);
    });
    const groundY = (wheelGround[0] + wheelGround[1] + wheelGround[2] + wheelGround[3]) / 4 + CLEARANCE;
    const center = sim.sampleWater(this.pos[0], this.pos[2]);
    const hullBottom = this.pos[1] - 0.55;
    const hullTop = this.pos[1] + 0.55;
    let sub = 0;
    if (center.height > hullBottom) sub = clamp((center.height - hullBottom) / (hullTop - hullBottom), 0, 1);
    this.submersion = sub;

    // buoyancy equilibrium: truck floats with a 0.45 m draft (scaled by fluid density)
    const densityRatio = clamp(sim.settings.density / 1000, 0.6, 2.2);
    const draft = 0.5 / densityRatio;
    const floatY = center.height === -Infinity ? -Infinity : center.height - draft + 0.55;
    this.floating = floatY > groundY + 0.02;
    const supportY = Math.max(groundY, floatY);

    // --- vertical spring (suspension / buoyancy bob)
    this.prevVy = this.vy;
    const stiff = this.floating ? 10 : 26;
    const damp = this.floating ? 4.5 : 9;
    this.vy += ((supportY - this.pos[1]) * stiff - this.vy * damp) * dt;
    this.pos[1] += this.vy * dt;
    if (this.pos[1] < groundY - 0.05) { this.pos[1] = groundY - 0.05; if (this.vy < 0) this.vy = 0; }

    // --- pitch / roll: terrain when grounded, wave slope when floating
    let pitchT = 0, rollT = 0;
    if (this.floating && center.height !== -Infinity) {
      const n = center.normal;
      // normal leans away from the upslope: wave rising ahead => n·f < 0 => nose up
      pitchT = Math.atan2(n[0] * f[0] + n[2] * f[2], n[1]);
      rollT = Math.atan2(-(n[0] * r[0] + n[2] * r[2]), n[1]);
    } else {
      const hf = (wheelGround[0] + wheelGround[1]) / 2;
      const hr = (wheelGround[2] + wheelGround[3]) / 2;
      const hRight = (wheelGround[0] + wheelGround[2]) / 2;
      const hLeft = (wheelGround[1] + wheelGround[3]) / 2;
      pitchT = -Math.atan2(hf - hr, 2.84);      // mat4FromTRS pitch pitches nose down for +value
      rollT = Math.atan2(hRight - hLeft, 1.96);
    }
    const tiltK = Math.min(1, dt * (this.floating ? 3.5 : 8));
    this.pitch = lerp(this.pitch, clamp(pitchT, -0.5, 0.5), tiltK);
    this.roll = lerp(this.roll, clamp(rollT, -0.5, 0.5), tiltK);

    // --- longitudinal / lateral dynamics
    let vF = vec3.dot(this.vel, f);
    let vR = vec3.dot(this.vel, r);
    const viscosity = sim.settings.viscosity;
    const bogged = sub > 0.25 && viscosity > 6;   // thick fluid (mud)
    const engine = this.floating ? 3.2 : bogged ? 7.5 : 13.5;
    vF += input.throttle * engine * dt;

    // drag: rolling + aero + hydrodynamic (quadratic-ish in water/mud)
    const waterDrag = sub * (viscosity > 6 ? 1.5 : 0.55);
    const quadDrag = sub * (viscosity > 6 ? 0.20 : 0.045) * Math.abs(vF);
    vF *= Math.exp(-(0.22 + waterDrag + quadDrag) * dt);
    vR *= Math.exp(-(0.22 + sub * 0.8) * dt);
    const grip = this.floating ? 0.7 : input.handbrake ? 1.1 : 8.5;
    vR *= Math.exp(-grip * dt);
    if (input.handbrake) vF *= Math.exp(-2.2 * dt);

    // river/surf current pushes the hull when deep
    if (sub > 0.05 && center.height !== -Infinity) {
      const push = Math.min(1, dt * 2.2 * sub);
      vF = lerp(vF, center.velocity[0] * f[0] + center.velocity[1] * f[2], push * 0.35);
      vR = lerp(vR, center.velocity[0] * r[0] + center.velocity[1] * r[2], push * 0.55);
    }

    // steering (paddling wheels still bite a little when floating)
    const steerAuthority = this.floating ? 0.4 : 1.0;
    this.yaw += input.steer * 1.75 * clamp(vF / 5.5, -1, 1) * steerAuthority * dt
      + (this.floating ? input.steer * input.throttle * 0.35 * dt : 0);
    this.steerVis = lerp(this.steerVis, input.steer * 0.5, Math.min(1, dt * 10));

    const f2 = this.forward();
    const r2 = this.right();
    this.vel = [
      f2[0] * vF + r2[0] * vR,
      0,
      f2[2] * vF + r2[2] * vR,
    ];
    this.speedFwd = vF;
    this.pos[0] += this.vel[0] * dt;
    this.pos[2] += this.vel[2] * dt;

    // stay inside the simulated world
    const half = sim.settings.worldSize / 2 - 2.5;
    this.pos[0] = clamp(this.pos[0], sim.settings.center[0] - half, sim.settings.center[0] + half);
    this.pos[2] = clamp(this.pos[2], sim.settings.center[1] - half, sim.settings.center[1] + half);

    // wheel spin (+ slip spin when churning in water/mud)
    this.slipping = Math.abs(input.throttle) > 0.2 && Math.abs(vF) < 2.2 && sub > 0.12;
    this.wheelSpin += (vF / WHEEL_R) * dt + (this.slipping || (input.throttle !== 0 && sub > 0.3) ? input.throttle * 16 * dt : 0);
  }

  /** Wheels + hull as fluid interactors. */
  interactors(input: VehicleInput): FluidInteractor[] {
    const f = this.forward();
    const out: FluidInteractor[] = [];
    const churn = this.slipping || (Math.abs(input.throttle) > 0.15 && this.submersion > 0.25);
    for (const [ox, oz] of WHEEL_OFFSETS) {
      const w = this.wheelWorld(ox, oz);
      const kick = churn ? -input.throttle * 3.2 : 0;
      out.push({
        kind: 0,
        position: [w[0], this.pos[1] - 0.16, w[2]],
        velocity: [
          this.vel[0] + f[0] * kick,
          this.vy + (churn ? 1.1 : 0),
          this.vel[2] + f[2] * kick,
        ],
        radius: 0.48,
        halfHeight: WHEEL_R,
        strength: churn ? 1.7 : 1.0,
      });
    }
    out.push({
      kind: 1,
      position: [this.pos[0], this.pos[1] - 0.1, this.pos[2]],
      velocity: [this.vel[0], Math.min(this.vy, this.prevVy), this.vel[2]],
      radius: 1.2,
      halfHeight: 0.55,
      strength: 0.85,
    });
    return out;
  }

  /** World matrices for [chassis, cabin, wheelFL, wheelFR, wheelRL, wheelRR]. */
  partMatrices(): Mat4[] {
    const bodyRoll = this.roll - this.steerVis * clamp(this.speedFwd * 0.012, -0.14, 0.14);
    const body = mat4FromTRS([this.pos[0], this.pos[1] + 0.05, this.pos[2]], this.yaw, [1, 1, 1], this.pitch, bodyRoll);
    const parts: Mat4[] = [
      mat4Multiply(body, mat4FromTRS([0, 0.18, 0], 0, [1.92, 0.62, 3.5])),
      mat4Multiply(body, mat4FromTRS([0, 0.74, 0.28], 0, [1.7, 0.6, 1.55])),
    ];
    WHEEL_OFFSETS.forEach(([ox, oz, front], i) => {
      const steer = front ? this.steerVis : 0;
      const local = mat4FromTRS([ox, -0.16, oz], steer, [0.36, 0.86, 0.86], -this.wheelSpin);
      parts.push(mat4Multiply(body, local));
    });
    return parts;
  }
}
