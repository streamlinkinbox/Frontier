import { Quaternion, Vector3 } from 'three';
import { clamp, damp, lerp, sign } from '../core/mathx';
import { TYRE, VEHICLE } from './tuning';
import type { Track } from './track';

export interface VehicleInput {
  throttle: number;
  brake: number;
  steer: number;
  handbrake: boolean;
  boost: boolean;
}

export interface WheelState {
  /** Local mount position (x = right, y = up, z = forward). */
  mount: Vector3;
  isFront: boolean;
  isDriven: boolean;
  steer: number;
  /** Angular velocity about the wheel axle (rad/s). */
  spin: number;
  compression: number;
  prevCompression: number;
  load: number;
  contact: Vector3;
  centre: Vector3;
  worldFwd: Vector3;
  worldRight: Vector3;
  slipRatio: number;
  slipAngle: number;
  /** Anti-roll bar reaction applied by the mate wheel on this axle. */
  arbForce: number;
  /** Raw spring + damper force before the anti-roll bar is added. */
  springForce: number;
  /** Relaxed (filtered) slip used by the tyre model — keeps it stable at any dt. */
  kappaF: number;
  alphaF: number;
  fx: number;
  fy: number;
  surfaceMu: number;
  grounded: boolean;
}

export interface LatchTarget {
  name: string;
  world: Vector3;
  local: Vector3;
  radius: number;
  /** 'panel' targets can be cut by the bumper blade, 'wheel' targets grab tyres. */
  kind: 'panel' | 'wheel';
}

const TMP_A = new Vector3();
const TMP_B = new Vector3();
const TMP_C = new Vector3();
const TMP_D = new Vector3();

const ENGINE_CURVE: [number, number][] = [
  [0, 150],
  [800, 180],
  [1500, 300],
  [2400, 410],
  [3400, 480],
  [4200, 470],
  [4800, 430],
  [5600, 360],
  [6200, 300],
  [6800, 250],
  [7200, 205],
  [7600, 120],
];

function engineTorqueAt(rpm: number): number {
  if (rpm <= ENGINE_CURVE[0][0]) return ENGINE_CURVE[0][1];
  for (let i = 1; i < ENGINE_CURVE.length; i++) {
    const [r1, t1] = ENGINE_CURVE[i];
    if (rpm <= r1) {
      const [r0, t0] = ENGINE_CURVE[i - 1];
      return lerp(t0, t1, (rpm - r0) / (r1 - r0));
    }
  }
  return ENGINE_CURVE[ENGINE_CURVE.length - 1][1];
}

/**
 * Normalised tyre curve. `n` is the combined slip normalised so that the peak
 * of the curve sits at n = 1. It rises as a parabola to the peak and falls
 * away gently past it, which is what gives the car its characteristic
 * "grip then let go" transition instead of an instant cliff.
 */
export function tyreShape(n: number): number {
  if (n <= 1) return 2 * n - n * n;
  const over = n - 1;
  return 1 - (0.24 * over) / (1 + over);
}

/**
 * Combined-slip tyre force. `kappa` is the longitudinal slip ratio and `alpha`
 * the slip angle; the magnitude comes from the normalised curve and the
 * direction from the normalised slip vector (the friction ellipse).
 */
export function tyreForce(kappa: number, alpha: number, D: number): { fx: number; fy: number } {
  const nx = kappa / TYRE.peakKappa;
  const ny = Math.tan(alpha) / TYRE.peakAlpha;
  const n = Math.hypot(nx, ny);
  if (n < 1e-5) return { fx: 0, fy: 0 };
  const f = D * tyreShape(n);
  return { fx: (f * nx) / n, fy: (f * ny) / n };
}

export class Vehicle {
  readonly pos = new Vector3();
  readonly vel = new Vector3();
  readonly quat = new Quaternion();
  readonly angVel = new Vector3();

  wheels: WheelState[] = [];

  // --- drivetrain state
  gear = 1;
  rpm: number = VEHICLE.idleRpm;
  shiftTimer = 0;
  boostTimer = 0;
  boostCooldown = 0;
  reverseMode = false;
  private reverseHold = 0;

  // --- external modifiers (set by the vine system each step)
  gripScale = 1;
  downforceScale = 1;
  extraDrag = 0;
  steeringScale = 1;

  // --- telemetry
  speed = 0;
  lateralG = 0;
  longG = 0;
  slipAngleBody = 0;
  wheelSlipRatio = 0;
  airborne = false;
  distance = 0;
  /** Smoothed steering input, used by the camera for look-ahead. */
  steerBias = 0;
  /** Debug: the force and torque accumulated in the last step. */
  readonly lastForce = new Vector3();
  readonly lastTorque = new Vector3();

  private force = new Vector3();
  private torque = new Vector3();
  private up = new Vector3();
  private fwd = new Vector3();
  private right = new Vector3();
  private prevVel = new Vector3();
  private input: VehicleInput = { throttle: 0, brake: 0, steer: 0, handbrake: false, boost: false };

  constructor() {
    const halfF = VEHICLE.trackF * 0.5;
    const halfR = VEHICLE.trackR * 0.5;
    const wbF = VEHICLE.wheelbase * 0.5;
    const wbR = VEHICLE.wheelbase * 0.5;
    const mounts: [number, number, number, boolean, boolean][] = [
      [-halfF, 0, wbF, true, true],
      [halfF, 0, wbF, true, true],
      [-halfR, 0, -wbR, false, true],
      [halfR, 0, -wbR, false, true],
    ];
    for (const [x, y, z, isFront, isDriven] of mounts) {
      this.wheels.push({
        mount: new Vector3(x, y, z),
        isFront,
        isDriven,
        steer: 0,
        spin: 0,
        compression: 0,
        prevCompression: 0,
        load: 0,
        contact: new Vector3(),
        centre: new Vector3(),
        worldFwd: new Vector3(),
        worldRight: new Vector3(),
        slipRatio: 0,
        slipAngle: 0,
        arbForce: 0,
        springForce: 0,
        kappaF: 0,
        alphaF: 0,
        fx: 0,
        fy: 0,
        surfaceMu: 1,
        grounded: false,
      });
    }
  }

  reset(pos: Vector3, heading: number) {
    this.pos.copy(pos);
    this.vel.set(0, 0, 0);
    this.angVel.set(0, 0, 0);
    this.quat.setFromAxisAngle(new Vector3(0, 1, 0), heading);
    this.gear = 1;
    this.rpm = VEHICLE.idleRpm;
    this.shiftTimer = 0;
    this.boostTimer = 0;
    this.boostCooldown = 0;
    this.reverseMode = false;
    this.speed = 0;
    this.distance = 0;
    for (const w of this.wheels) {
      w.spin = 0;
      w.compression = 0;
      w.prevCompression = 0;
      w.load = 0;
    }
  }

  get forward(): Vector3 {
    return this.fwd;
  }
  get upVec(): Vector3 {
    return this.up;
  }
  get rightVec(): Vector3 {
    return this.right;
  }

  /** Accumulate a world-space force applied at a world-space point. */
  applyForceAtPoint(force: Vector3, worldPoint: Vector3): void {
    this.force.add(force);
    TMP_A.copy(worldPoint).sub(this.pos);
    TMP_B.copy(TMP_A).cross(force);
    this.torque.add(TMP_B);
  }

  /** Fixed-step simulation. dt is always 1/240 in the game loop. */
  step(dt: number, input: VehicleInput, track: Track): void {
    this.input = input;
    this.up.set(0, 1, 0).applyQuaternion(this.quat);
    this.fwd.set(0, 0, 1).applyQuaternion(this.quat);
    this.right.set(1, 0, 0).applyQuaternion(this.quat);

    this.force.set(0, 0, 0);
    this.torque.set(0, 0, 0);
    this.prevVel.copy(this.vel);

    this.updateBoost(dt, input);
    this.updateDrivetrain(dt, input);

    // ---------------------------------------------------------------- steering
    const speed = this.vel.length();
    const steerLimit = VEHICLE.maxSteer * clamp(1 - speed / 62, 0.22, 1);
    const steerTarget = input.steer * steerLimit * this.steeringScale;
    this.steerBias = damp(this.steerBias, input.steer, 6, dt);
    const steerRate = steerLimit / 0.21; // full lock in ~210 ms
    for (const w of this.wheels) {
      if (!w.isFront) continue;
      const target = w.mount.x < 0 ? steerTarget * (1 - VEHICLE.ackermann * 0.5) : steerTarget * (1 + VEHICLE.ackermann * 0.5);
      w.steer = damp(w.steer, target, 9, dt);
      if (Math.abs(w.steer - target) < 1e-4) w.steer = target;
      void steerRate;
    }

    // ------------------------------------------------------------- suspension
    // Pass 1 — geometry and spring force for every corner.
    //
    // `restLength` is the fully extended strut: with the wheel just touching
    // the ground the spring carries nothing. Past that the wheel is in the
    // air, which is what keeps a landing from loading a tyre that is not
    // touching anything.
    const restLength = 0.227;
    const maxLength = restLength;
    let groundedCount = 0;

    // A car on its roof is not resting on its wheels: the struts carry no load
    // until the body is back on an upright axis, otherwise the suspension
    // would happily push the wreck straight down through the terrain.
    const upright = this.up.y > 0.2;

    for (const w of this.wheels) {
      TMP_A.copy(w.mount).applyQuaternion(this.quat).add(this.pos); // mount world
      const groundY = track.heightAt(TMP_A.x, TMP_A.z);
      const vertical = TMP_A.y - groundY;
      const along = vertical / Math.max(0.35, this.up.y);
      const rawSusLen = along - VEHICLE.wheelRadius;

      if (rawSusLen > maxLength || !upright) {
        w.grounded = false;
        w.load = 0;
        w.springForce = 0;
        w.compression = 0;
        w.prevCompression = 0;
        w.arbForce = 0;
        // fully extended: the wheel hangs below the mount
        w.centre.copy(TMP_A).addScaledVector(this.up, -maxLength);
        w.contact.copy(TMP_A).addScaledVector(this.up, -Math.min(along, maxLength + VEHICLE.wheelRadius));
        w.fx = 0;
        w.fy = 0;
        continue;
      }

      w.grounded = true;
      w.arbForce = 0;
      groundedCount++;
      // the tyre can deflect a little, but the wheel never passes through the
      // ground: past the bump stop the strut length bottoms out
      const susLen = Math.max(-0.05, rawSusLen);
      const rawCompression = restLength - susLen;
      const compression = clamp(rawCompression, 0, VEHICLE.travel);
      // Compression velocity (positive while the strut is being squeezed).
      // The damper resists that motion, so it *adds* load on the way in and
      // unloads on the way out. Getting this sign wrong turns the suspension
      // into a pitch feedback loop.
      const rate = (compression - w.prevCompression) / dt;
      w.prevCompression = compression;
      w.compression = compression;

      const k = w.isFront ? VEHICLE.springF : VEHICLE.springR;
      const bump = w.isFront ? VEHICLE.damperBumpF : VEHICLE.damperBumpR;
      const rebound = w.isFront ? VEHICLE.damperReboundF : VEHICLE.damperReboundR;
      const c = rate > 0 ? bump : rebound;

      let fz = k * compression + c * rate;
      if (rawCompression > VEHICLE.travel - 0.03) {
        // The bump stop is progressive, not linear: it stays soft for the
        // first few millimetres and only goes properly rigid at the end of
        // the travel, which is what a real bump rubber does.
        const over = rawCompression - (VEHICLE.travel - 0.03);
        fz += 7.5e6 * over * over;
      }
      w.springForce = clamp(fz, 0, 26000);

      // the contact patch is on the ground, the wheel centre is one radius up
      const contact = TMP_B.copy(TMP_A).addScaledVector(this.up, -Math.min(along, rawSusLen + VEHICLE.wheelRadius));
      w.contact.copy(contact);
      w.centre.copy(TMP_A).addScaledVector(this.up, -susLen);
    }

    // Pass 2 — anti-roll bars couple the two wheels of an axle.
    for (const w of this.wheels) {
      const mate = this.wheels.find((o) => o.isFront === w.isFront && o !== w)!;
      if (!w.grounded || !mate.grounded) continue;
      const arb = w.isFront ? VEHICLE.arbF : VEHICLE.arbR;
      const f = clamp(arb * (w.compression - mate.compression) * 0.5, -6000, 6000);
      w.arbForce = f;
      mate.arbForce = -f;
    }
    for (const w of this.wheels) {
      w.springForce = clamp(w.springForce + w.arbForce, 0, 28000);
      w.load = w.springForce;
    }

    // Pass 3 — tyre forces and their application to the body.
    for (const w of this.wheels) {
      if (!w.grounded) continue;
      const fz = w.springForce;

      // contact point velocity: v + omega x r   (r = contact - com)
      TMP_C.copy(w.contact).sub(this.pos);
      const vPoint = new Vector3().copy(this.vel).add(new Vector3().crossVectors(this.angVel, TMP_C));

      // wheel frame
      const cs = Math.cos(w.steer);
      const sn = Math.sin(w.steer);
      w.worldFwd.copy(this.fwd).multiplyScalar(cs).addScaledVector(this.right, sn).normalize();
      w.worldRight.copy(this.right).multiplyScalar(cs).addScaledVector(this.fwd, -sn).normalize();

      const vLong = vPoint.dot(w.worldFwd);
      const vLat = vPoint.dot(w.worldRight);

      const props = track.surfaceProps(w.centre.x, w.centre.z);
      w.surfaceMu = props.mu;
      const mu = props.mu * this.gripScale;
      const loadSens = 1 - TYRE.loadSensitivity * (fz / TYRE.refLoad - 1);
      const D = mu * fz * clamp(loadSens, 0.55, 1.35);

      // ---- Tyre model (impulse formulation).
      //
      // The contact patch slips relative to the road with a velocity of
      // (spin*r - vLong, -vLat). The friction force opposes that slip, its
      // magnitude limited by a normalised Pacejka curve evaluated at the
      // combined slip. Applying it as an impulse (rather than as a force on
      // an explicitly integrated wheel) is what keeps the model stable at
      // 240 Hz and lets the tyre genuinely saturate into a slide.
      const vRef = Math.max(Math.abs(vLong), 1.8);
      const sx = w.spin * VEHICLE.wheelRadius - vLong;
      const sy = -vLat;
      const slipSpeed = Math.hypot(sx, sy);

      const kappaRaw = clamp(sx / vRef, -6, 6);
      const alphaRaw = Math.atan2(sy, Math.max(Math.abs(vLong), 1.2));
      w.kappaF = damp(w.kappaF, kappaRaw, 40, dt);
      w.alphaF = damp(w.alphaF, alphaRaw, 40, dt);
      w.slipRatio = w.kappaF;
      w.slipAngle = w.alphaF;

      const n = Math.hypot(kappaRaw / TYRE.peakKappa, Math.tan(alphaRaw) / TYRE.peakAlpha);
      const fLimit = D * tyreShape(n);

      let fx = 0;
      let fy = 0;
      if (slipSpeed > 1e-5) {
        fx = (fLimit * sx) / slipSpeed;
        fy = (fLimit * sy) / slipSpeed;

        // Never overshoot the slip: an impulse larger than the one that would
        // bring the contact to rest makes the tyre oscillate.
        const mEffLong = 1 / (1 / VEHICLE.wheelInertia + (VEHICLE.wheelRadius * VEHICLE.wheelRadius) / VEHICLE.mass);
        const jx = fx * dt;
        const jxNull = sx * mEffLong;
        if (Math.abs(jx) > Math.abs(jxNull)) fx = Math.sign(jx) * (Math.abs(jxNull) / dt);
        const jy = fy * dt;
        const jyNull = sy * VEHICLE.mass;
        if (Math.abs(jy) > Math.abs(jyNull)) fy = Math.sign(jy) * (Math.abs(jyNull) / dt);
      }

      // rolling resistance
      fx -= sign(vLong) * props.roll * fz;

      // ---- wheel rotational dynamics
      const driveTorque = this.driveTorqueForWheel(w);
      let brakeTorque = 0;
      if (input.brake > 0.01 && !this.reverseMode) {
        brakeTorque += (w.isFront ? VEHICLE.brakeTorqueF : VEHICLE.brakeTorqueR) * input.brake;
      }
      if (input.handbrake && !w.isFront) {
        brakeTorque += VEHICLE.handbrakeTorqueR;
      }
      const maxStop = (Math.abs(w.spin) * VEHICLE.wheelInertia) / dt;
      if (brakeTorque > maxStop) brakeTorque = maxStop;

      const r = VEHICLE.wheelRadius;
      const prevSpin = w.spin;
      let torque = driveTorque - sign(w.spin) * brakeTorque - fx * r;
      // a stopped wheel stays stopped while the brakes hold it: without this
      // the brake torque would drag it backwards through zero
      if (Math.abs(w.spin) < 0.4 && brakeTorque > 0 && driveTorque < brakeTorque) torque = 0;
      w.spin = clamp(w.spin + (torque / VEHICLE.wheelInertia) * dt, -320, 320);
      if (prevSpin * w.spin < 0 && Math.abs(w.spin) < 1.2) w.spin = 0;

      w.fx = fx;
      w.fy = fy;

      // apply to the body: the spring acts along the contact normal at the
      // contact patch, so load transfer emerges instead of being faked
      TMP_C.copy(w.worldFwd).multiplyScalar(fx).addScaledVector(w.worldRight, fy);
      this.applyForceAtPoint(TMP_D.copy(this.up).multiplyScalar(fz), w.contact);
      this.applyForceAtPoint(TMP_C, w.contact);
    }

    this.airborne = groundedCount === 0;

    // -------------------------------------------------------------- aerodynamics
    const v = this.vel;
    const speedNow = v.length();
    if (speedNow > 0.1) {
      const q = 0.5 * 1.225 * speedNow * speedNow;
      this.force.addScaledVector(v, -(q * VEHICLE.dragArea) / speedNow);
      const df = q * VEHICLE.downforceFront * this.downforceScale;
      const dr = q * VEHICLE.downforceRear * this.downforceScale;
      TMP_A.copy(this.pos).addScaledVector(this.fwd, 1.25);
      this.applyForceAtPoint(TMP_B.copy(this.up).multiplyScalar(-df), TMP_A);
      TMP_A.copy(this.pos).addScaledVector(this.fwd, -1.35);
      this.applyForceAtPoint(TMP_B.copy(this.up).multiplyScalar(-dr), TMP_A);
    }
    if (this.extraDrag > 0) {
      this.force.addScaledVector(v, -this.extraDrag);
    }

    // ------------------------------------------------------------------ gravity
    this.force.y -= VEHICLE.mass * 9.81;
    this.lastForce.copy(this.force);
    this.lastTorque.copy(this.torque);

    // ------------------------------------------------------------- integration
    const accel = TMP_A.copy(this.force).multiplyScalar(1 / VEHICLE.mass);
    this.vel.addScaledVector(accel, dt);

    // angular: body-frame Euler equations with a diagonal inertia tensor
    const I = VEHICLE.inertia;
    TMP_B.copy(this.torque).applyQuaternion(this.quat.clone().invert());
    const wBody = TMP_C.copy(this.angVel).applyQuaternion(this.quat.clone().invert());
    const Iw = { x: wBody.x * I.pitch, y: wBody.y * I.roll, z: wBody.z * I.yaw };
    // ω × (Iω)
    const gyro = {
      x: wBody.y * Iw.z - wBody.z * Iw.y,
      y: wBody.z * Iw.x - wBody.x * Iw.z,
      z: wBody.x * Iw.y - wBody.y * Iw.x,
    };
    const alphaBody = {
      x: (TMP_B.x - gyro.x) / I.pitch,
      y: (TMP_B.y - gyro.y) / I.roll,
      z: (TMP_B.z - gyro.z) / I.yaw,
    };
    wBody.x += alphaBody.x * dt;
    wBody.y += alphaBody.y * dt;
    wBody.z += alphaBody.z * dt;
    // mild angular damping keeps the sim numerically calm without feeling scripted
    const dampFactor = Math.exp(-(this.airborne ? 0.15 : 0.55) * dt);
    wBody.multiplyScalar(dampFactor);
    this.angVel.set(wBody.x, wBody.y, wBody.z).applyQuaternion(this.quat);

    this.pos.addScaledVector(this.vel, dt);

    const omega = this.angVel;
    const dq = new Quaternion(omega.x * dt * 0.5, omega.y * dt * 0.5, omega.z * dt * 0.5, 0);
    dq.multiply(this.quat);
    this.quat.set(
      this.quat.x + dq.x,
      this.quat.y + dq.y,
      this.quat.z + dq.z,
      this.quat.w + dq.w,
    ).normalize();

    // -------------------------------------------------------------- telemetry
    this.speed = this.vel.length();
    const dv = TMP_A.copy(this.vel).sub(this.prevVel).multiplyScalar(1 / dt);
    this.longG = dv.dot(this.fwd) / 9.81;
    this.lateralG = dv.dot(this.right) / 9.81;
    this.slipAngleBody = Math.atan2(-this.vel.dot(this.right), Math.max(Math.abs(this.vel.dot(this.fwd)), 1));
    this.wheelSlipRatio = Math.abs(this.wheels[2].slipRatio);
  }

  private updateBoost(dt: number, input: VehicleInput): void {
    if (input.boost && this.boostCooldown <= 0 && this.boostTimer <= 0) {
      this.boostTimer = VEHICLE.boostDuration;
      this.boostCooldown = VEHICLE.boostDuration + VEHICLE.boostCooldown;
    }
    this.boostTimer = Math.max(0, this.boostTimer - dt);
    this.boostCooldown = Math.max(0, this.boostCooldown - dt);
  }

  private updateDrivetrain(dt: number, input: VehicleInput): void {
    this.shiftTimer = Math.max(0, this.shiftTimer - dt);

    // reverse handling: hold brake at a standstill
    const stopped = this.speed < 1.2;
    if (stopped && input.brake > 0.4 && input.throttle < 0.1) {
      this.reverseHold += dt;
      if (this.reverseHold > 0.35) this.reverseMode = true;
    } else if (input.throttle > 0.1 || !stopped) {
      this.reverseHold = 0;
      if (input.throttle > 0.1) this.reverseMode = false;
    }

    const driven = this.wheels.filter((w) => w.isDriven);
    const avgSpin = driven.reduce((a, w) => a + w.spin, 0) / driven.length;
    const ratio = this.gearRatios();
    const wheelRpm = (Math.abs(avgSpin) * ratio * VEHICLE.finalDrive * 60) / (2 * Math.PI);
    const target = clamp(wheelRpm, VEHICLE.idleRpm, VEHICLE.redline);
    this.rpm = damp(this.rpm, target, 14, dt);

    // auto gearbox
    if (this.shiftTimer <= 0) {
      if (!this.reverseMode) {
        if (this.rpm > VEHICLE.shiftUpRpm && this.gear < VEHICLE.gearRatios.length) {
          this.gear++;
          this.shiftTimer = VEHICLE.shiftTime;
        } else if (this.rpm < VEHICLE.shiftDownRpm && this.gear > 1) {
          this.gear--;
          this.shiftTimer = VEHICLE.shiftTime * 0.7;
        }
      }
    }
  }

  private gearRatios(): number {
    return this.reverseMode ? VEHICLE.reverseRatio : VEHICLE.gearRatios[this.gear - 1];
  }

  private driveTorqueForWheel(w: WheelState): number {
    if (this.shiftTimer > 0) return 0;
    const throttle = this.reverseMode ? this.input.brake : this.input.throttle;
    if (throttle <= 0.01) return 0;
    const ratio = this.gearRatios();
    const torque = engineTorqueAt(this.rpm);
    const boost = this.boostTimer > 0 ? VEHICLE.boostTorque : 1;
    // the axle split applies to the drivetrain total, shared by that axle's two wheels
    const split = w.isFront ? VEHICLE.driveSplitFront : 1 - VEHICLE.driveSplitFront;
    return torque * ratio * VEHICLE.finalDrive * 0.92 * boost * split * 0.5 * throttle;
  }

  /** World-space latch targets the vine system can grab. */
  latchTargets(out: LatchTarget[]): LatchTarget[] {
    out.length = 0;
    const panels: [string, number, number, number, number][] = [
      ['roof', 0, 0.62, -0.15, 0.34],
      ['hood', 0, 0.3, 1.3, 0.32],
      ['bumperF', 0, -0.08, 2.05, 0.24],
      ['bumperR', 0, -0.05, -2.05, 0.22],
      ['rockerL', -0.92, -0.2, 0.05, 0.2],
      ['rockerR', 0.92, -0.2, 0.05, 0.2],
    ];
    for (const [name, x, y, z, r] of panels) {
      const local = new Vector3(x, y, z);
      const world = local.clone().applyQuaternion(this.quat).add(this.pos);
      out.push({ name, world, local, radius: r, kind: 'panel' });
    }
    for (const w of this.wheels) {
      out.push({
        name: 'wheel',
        world: w.centre.clone(),
        local: w.centre.clone().sub(this.pos).applyQuaternion(this.quat.clone().invert()),
        radius: VEHICLE.wheelRadius + 0.06,
        kind: 'wheel',
      });
    }
    return out;
  }
}
