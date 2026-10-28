// Demo raycast vehicle with two-way water / mud coupling.
//  car -> water : body + 4 wheels rasterised into the solver (displacement, bow wave, wake, ruts)
//                 + spray / mud-roost particle emitters that take real water from the grid
//  water -> car : buoyancy & hydrodynamic drag (pressure head + flow from GPU queries), wheel
//                 drag in water, aquaplaning, mud resistance and reduced grip
import { RigidBody, BuoyancyProbe } from '../src/Buoyancy.js';
import { v3, quat, mat4, clamp, smoothstep } from '../src/math.js';

const RIGHT = [1, 0, 0], UP = [0, 1, 0], FWD = [0, 0, 1];

export class Vehicle {
  constructor(scene, { color = [0.012, 0.105, 1.0] } = {}) {
    this.scene = scene;
    this.color = color;
    const mass = 1350;
    const [w, h, l] = [2.0, 1.25, 4.5];
    this.body = new RigidBody({ mass, inertia: [mass / 12 * (h * h + l * l), mass / 12 * (w * w + l * l) * 1.1, mass / 12 * (w * w + h * h)] });
    this.cfg = {
      radius: 0.34, halfWidth: 0.13, rest: 0.30, k: 42000, c: 4800, maxComp: 0.24,
      engineForce: 8000, power: 165000, brakeForce: 12000, maxSteer: 0.6,
      cDrag: 0.45, rollRes: 0.012, front: 0.4,  // AWD torque split to the front
    };
    const x = 0.82, y = -0.05, z = 1.38;
    this.wheels = [[-x, y, z, true], [x, y, z, true], [-x, y, -z, false], [x, y, -z, false]].map(([a, b, c, front]) => ({
      mount: [a, b, c], front, comp: 0, compPrev: 0, contact: false, spin: 0, spinRate: 0, slip: 0,
      depth: 0, mud: 0, onWater: 0, groundPoint: [0, 0, 0], load: 0, surface: 'road',
    }));
    this.probe = new BuoyancyProbe(this.body, { halfExtents: [0.92, 0.5, 2.2], center: [0, 0.02, 0], grid: [3, 2, 5], volume: 2.3, drag: 1.0, linearDamping: 320, angularDamping: 0.6 });
    // wheel contact query points (4) – a pseudo-probe for FluidQueryBatch
    this.wheelProbe = { count: 4, writeQueryPoints: (out, off, lead) => {
      for (let i = 0; i < 4; i++) {
        const wpos = v3.madd(this.body.toWorld(this.wheels[i].mount), this.body.linVel, lead);
        out[(off + i) * 2] = wpos[0]; out[(off + i) * 2 + 1] = wpos[2];
      }
    } };
    this.input = { throttle: 0, brake: 0, steer: 0, handbrake: 0 };
    this.steer = 0;
    this.speed = 0;
    this.inWater = 0;
    this.soak = 0;
    this.mudCoat = 0;
    this.reset(scene.spawn.position, scene.spawn.heading);
  }

  reset(position, heading) {
    const b = this.body;
    b.position = position.slice();
    b.position[1] = this.scene.heightAt(position[0], position[2]) + 0.75;
    b.rotation = quat.fromAxisAngle(UP, heading);
    b.linVel = [0, 0, 0]; b.angVel = [0, 0, 0];
    for (const wh of this.wheels) { wh.comp = wh.compPrev = 0; wh.spin = 0; }
  }

  groundAt(x, z) {
    const h = this.scene.heightAt(x, z);
    const e = 0.15;
    const gx = (this.scene.heightAt(x + e, z) - this.scene.heightAt(x - e, z)) / (2 * e);
    const gz = (this.scene.heightAt(x, z + e) - this.scene.heightAt(x, z - e)) / (2 * e);
    return { h, n: v3.norm([-gx, 1, -gz]) };
  }

  /** apply GPU query results (wheel contacts) */
  setWheelWater(data, offset) {
    for (let i = 0; i < 4; i++) {
      const o = (offset + i) * 8;
      const wh = this.wheels[i];
      wh.depth = data[o + 1] > 0 ? data[o + 1] : 0;
      wh.mud = data[o + 4];
      wh.flow = [data[o + 2], data[o + 3]];
    }
  }

  /**
   * @param {number} dt
   * @param {{throttle:number, brake:number, steer:number, handbrake:number}} input
   * @param {{data: Float32Array, offset: number}|null} fluid  latest query results for the hull probe
   */
  update(dt, input, fluid = null) {
    const sub = 4, h = dt / sub;
    const k = 1 - Math.exp(-dt * 6);
    this.input = input;
    const vf = v3.dot(this.body.linVel, this.body.dirToWorld(FWD));
    const steerTarget = input.steer * this.cfg.maxSteer / (1 + Math.abs(vf) / 22);
    this.steer += (steerTarget - this.steer) * k;
    for (let s = 0; s < sub; s++) {
      // buoyancy + hydrodynamic drag every physics sub-step (positions change between them)
      if (fluid) this.probe.apply(fluid.data, fluid.offset);
      this._step(h);
    }
    this.inWater = this.probe.submerged;
    this.frontDepth = fluid ? this._frontDepth(fluid.data, fluid.offset) : 0;
    this.speed = v3.dot(this.body.linVel, this.body.dirToWorld(FWD));
    // how soaked the body is (drains off after leaving the water) and how muddy it is
    const deep = Math.max(this.inWater * 1.6, ...this.wheels.map((w) => smoothstep(0.25, 0.6, w.depth)));
    this.soak = Math.max(this.soak * Math.exp(-dt / 2.5), Math.min(1, deep));
    const inMud = Math.max(...this.wheels.map((w) => (w.depth > 0.01 ? smoothstep(0.3, 0.8, w.mud) : 0)));
    const clean = Math.max(...this.wheels.map((w) => (w.depth > 0.12 && w.mud < 0.1 ? 1 : 0)));
    this.mudCoat = clamp(this.mudCoat + dt * (0.35 * inMud * smoothstep(0.5, 6, Math.abs(this.speed) + 2 * inMud) - 0.12 * clean - 0.002), 0, 1);
  }

  // water depth over the lowest front samples of the hull probe (drives the bow splash)
  _frontDepth(data, offset) {
    const [gx, gy, gz] = this.probe.grid;
    let d = 0;
    for (let i = 0; i < gx; i++) {
      const s = ((gz - 1) * gy + 0) * gx + i;
      const head = data[(offset + s) * 8];
      const p = this.body.toWorld(this.probe.local[s]);
      d = Math.max(d, head - (p[1] - this.probe.sampleHalfHeight));
    }
    return clamp(d, 0, 1.5);
  }

  _step(dt) {
    const b = this.body, cfg = this.cfg, inp = this.input;
    const up = b.dirToWorld(UP), fwdB = b.dirToWorld(FWD);
    const vLongBody = v3.dot(b.linVel, fwdB);
    // throttle / brake / reverse logic
    let drive = 0, brake = 0;
    if (inp.throttle > 0) drive = inp.throttle;
    if (inp.brake > 0) { if (vLongBody > 1.0) brake = inp.brake; else drive = -0.6 * inp.brake; }
    const speed = Math.abs(vLongBody);
    const engine = Math.min(cfg.engineForce, cfg.power / Math.max(speed, 1)) * drive;

    let wheelsOnGround = 0;
    for (let i = 0; i < 4; i++) {
      const wh = this.wheels[i];
      const mountW = b.toWorld(wh.mount);
      const g = this.groundAt(mountW[0], mountW[2]);
      // distance along -up from the mount to the ground plane
      const denom = Math.max(v3.dot(up, g.n), 0.2);
      const dist = (mountW[1] - g.h) * g.n[1] / denom;
      const len = dist - cfg.radius;
      wh.compPrev = wh.comp;
      wh.comp = clamp(cfg.rest - len, 0, cfg.rest + cfg.maxComp);
      wh.contact = len < cfg.rest;
      wh.groundPoint = v3.madd(mountW, up, -dist);
      if (!wh.contact) { wh.load = 0; wh.spinRate *= 0.99; continue; }
      wheelsOnGround++;
      const cv = (wh.comp - wh.compPrev) / dt;
      let Fz = Math.max(0, cfg.k * wh.comp + cfg.c * cv);
      if (wh.comp > cfg.rest + cfg.maxComp * 0.9) Fz += 120000 * (wh.comp - (cfg.rest + cfg.maxComp * 0.9));
      wh.load = Fz;
      const C = wh.groundPoint;
      // wheel frame on the ground plane
      const steer = wh.front ? this.steer : 0;
      const fw = b.dirToWorld([Math.sin(steer), 0, Math.cos(steer)]);
      const f = v3.norm(v3.madd(fw, g.n, -v3.dot(fw, g.n)));
      const s = v3.cross(g.n, f);
      const vc = b.pointVelocity(C);
      const vl = v3.dot(vc, f), vs = v3.dot(vc, s);
      // surface + water + mud
      const mat = this.scene.materialAt(C[0], C[2]);
      let mu = 1.0 * mat.road + 0.72 * (1 - mat.road);
      mu = mu * (1 - mat.sand * 0.25) * (1 - mat.river * 0.2);
      const mudLevel = Math.max(mat.mud * 0.7, smoothstep(0.02, 0.6, wh.mud) * smoothstep(0.005, 0.05, wh.depth));
      mu *= 1 - 0.55 * mudLevel;
      // aquaplaning (Horne: ~9*sqrt(psi) knots ~ 26 m/s at 32 psi) above ~3 mm film
      const vAp = 26 * (1 - 0.4 * smoothstep(0.01, 0.06, wh.depth));
      const aqua = smoothstep(0.003, 0.012, wh.depth) * smoothstep(0.65 * vAp, vAp, Math.abs(vl)) * (1 - mudLevel);
      mu *= 1 - 0.85 * aqua;
      wh.slipGrip = mu;
      // longitudinal force (AWD)
      const share = (wh.front ? cfg.front : 1 - cfg.front) / 2;
      let Fx = engine * share;
      if (brake > 0) Fx -= Math.sign(vl) * Math.min(cfg.brakeForce * brake * 0.25, Math.abs(vl) * 4000);
      if (inp.handbrake && !wh.front) Fx -= Math.sign(vl) * Math.min(4000, Math.abs(vl) * 3000);
      // rolling resistance + mud drag (Bingham mud resists the tyre bulldozing through it)
      const rr = cfg.rollRes + 0.22 * mudLevel + 0.05 * mat.sand;
      Fx -= Math.sign(vl) * Math.min(rr * Fz, Math.abs(vl) * 2000);
      // lateral (simplified Pacejka on slip angle)
      const alpha = Math.atan2(vs, Math.abs(vl) + 0.6);
      const latMu = mu * (inp.handbrake && !wh.front ? 0.45 : 1);
      let Fy = -latMu * Fz * Math.sin(1.55 * Math.atan(9 * alpha));
      // friction circle
      const Fmax = mu * Fz;
      const mag = Math.hypot(Fx, Fy);
      wh.slip = Math.max(0, Math.abs(Fx) / Math.max(Fmax, 1) - 0.6) + Math.abs(alpha) * 0.5;
      if (mag > Fmax) { Fx *= Fmax / mag; Fy *= Fmax / mag; }
      // water drag on the tyre (frontal area in the water column) – relative to the flow
      const dW = Math.min(wh.depth, 2 * cfg.radius);
      let Fw = [0, 0, 0];
      if (dW > 0.01) {
        const flow = wh.flow ?? [0, 0];
        const rel = [vc[0] - flow[0], 0, vc[2] - flow[1]];
        const rs = Math.hypot(rel[0], rel[2]);
        const area = 2 * cfg.halfWidth * dW + 0.1 * dW;   // tyre + wheel-arch spray resistance
        const rho = 1000 + 640 * wh.mud;
        Fw = v3.scale(rel, -0.5 * rho * 1.1 * area * rs);
      }
      const F = v3.add(v3.add(v3.add(v3.scale(g.n, Fz), v3.scale(f, Fx)), v3.scale(s, Fy)), Fw);
      b.addForceAtPoint(F, C);
      // visual wheel spin; spins up when traction is lost (mud / handbrake)
      const target = vl / cfg.radius + (Fmax > 0 ? clamp(Math.abs(engine * share) / Math.max(Fmax, 1) - 1, 0, 3) * 12 * Math.sign(engine) : 0);
      wh.spinRate += (target - wh.spinRate) * Math.min(1, dt * 20);
      wh.spin += wh.spinRate * dt;
    }
    for (const wh of this.wheels) if (!wh.contact) wh.spin += wh.spinRate * dt;

    // aerodynamic drag
    const sp = v3.len(b.linVel);
    b.addForce(v3.scale(b.linVel, -cfg.cDrag * sp));
    // chassis collisions with the ground (corners + underbody)
    for (const lp of CHASSIS_POINTS) {
      const p = b.toWorld(lp);
      const g = this.groundAt(p[0], p[2]);
      const pen = (g.h - p[1]) * g.n[1];
      if (pen > 0) {
        const v = b.pointVelocity(p);
        const vn = v3.dot(v, g.n);
        const Fn = Math.max(0, 160000 * pen - 12000 * vn);
        const vt = v3.madd(v, g.n, -vn);
        const Ft = v3.scale(vt, -Math.min(0.6 * Fn / Math.max(v3.len(vt), 0.1), 2500));
        b.addForceAtPoint(v3.add(v3.scale(g.n, Fn), Ft), p);
      }
    }
    // anti-flip assist when stuck upside down
    if (up[1] < 0.2 && wheelsOnGround === 0 && sp < 2) b.addTorque(v3.scale(v3.cross(up, UP), 9000));
    this.wheelsOnGround = wheelsOnGround;
    b.integrate(dt);
  }

  // ---- coupling outputs ----------------------------------------------------------------------
  primitives() {
    const b = this.body, out = [];
    const bodyC = b.toWorld([0, -0.1, 0.05]);
    out.push({ type: 'box', position: bodyC, rotation: b.rotation, halfExtents: [0.93, 0.3, 2.2], velocity: b.pointVelocity(bodyC), angularVelocity: b.angVel });
    const cab = b.toWorld([0, 0.42, -0.25]);
    out.push({ type: 'box', position: cab, rotation: b.rotation, halfExtents: [0.74, 0.25, 1.05], velocity: b.pointVelocity(cab), angularVelocity: b.angVel });
    for (const wh of this.wheels) {
      const c = this.wheelCenter(wh);
      const steer = wh.front ? this.steer : 0;
      const rot = quat.mul(b.rotation, quat.fromAxisAngle(UP, steer));
      const axle = quat.rotate(rot, RIGHT);
      out.push({ type: 'cylinder', position: c, rotation: rot, radius: this.cfg.radius, halfLength: this.cfg.halfWidth,
        velocity: b.pointVelocity(c), angularVelocity: v3.add(b.angVel, v3.scale(axle, wh.spinRate)) });
    }
    return out;
  }

  wheelCenter(wh) {
    const up = this.body.dirToWorld(UP);
    const len = this.cfg.rest - wh.comp;
    return v3.madd(this.body.toWorld(wh.mount), up, -Math.max(len, -0.1));
  }

  /** spray / mud particle emitters for this frame */
  emitters(sim) {
    const out = [];
    const b = this.body;
    const right = b.dirToWorld(RIGHT), fwd = b.dirToWorld(FWD);
    for (const wh of this.wheels) {
      if (!wh.contact && wh.depth < 0.05) continue;
      const c = this.wheelCenter(wh);
      const v = b.pointVelocity(c);
      const vh = Math.hypot(v[0], v[2]);
      const side = Math.sign(wh.mount[0]);
      const d = Math.min(wh.depth, 0.35);
      if (d > 0.012 && vh > 1.2) {
        // sheet spray thrown sideways and up from the tyre / bow of the wheel
        const base = [c[0] + right[0] * side * 0.2, c[1] - this.cfg.radius + d, c[2] + right[2] * side * 0.2];
        const vel = v3.add(v3.add(v3.scale(right, side * (0.4 * vh + 0.8)), v3.scale(fwd, 0.15 * Math.sign(this.speed) * vh)), [v[0] * 0.25, 0.32 * vh + 1.2, v[2] * 0.25]);
        out.push({ position: base, velocity: vel, spread: 0.2 * vh + 0.4, radius: [0.22, 0.22], rate: Math.min(9000, 2600 * d * vh), kind: 'spray', fromWater: true, mud: -1 });
        // rooster tail behind the tyre
        out.push({ position: v3.madd(base, fwd, -0.45 * Math.sign(this.speed)), velocity: v3.add(v3.scale(fwd, -0.35 * vh * Math.sign(this.speed)), [0, 0.45 * vh + 1, 0]), spread: 0.15 * vh + 0.3, radius: 0.12, rate: Math.min(6000, 1500 * d * vh), kind: 'spray', fromWater: true, mud: -1 });
        if (vh > 6) out.push({ position: base, velocity: [v[0] * 0.3, 0.25 * vh, v[2] * 0.3], spread: 0.25 * vh, radius: 0.3, rate: 70 * vh, kind: 'mist', fromWater: true, volume: 0 });
      }
      // mud roost: spinning tyre in mud throws clumps backwards
      const spin = Math.abs(wh.spinRate * this.cfg.radius) - vh;
      if (wh.mud > 0.3 && wh.depth > 0.01 && (spin > 1.0 || vh > 4)) {
        const sp = Math.max(spin, vh * 0.5);
        const back = v3.scale(fwd, -Math.sign(wh.spinRate || 1));
        out.push({ position: v3.madd([c[0], c[1] - this.cfg.radius * 0.7, c[2]], back, 0.35), velocity: v3.add(v3.scale(back, 0.7 * sp), [0, 0.55 * sp + 1, 0]), spread: 0.3 * sp + 0.4, radius: 0.15, rate: Math.min(3000, 220 * sp), kind: 'spray', fromWater: true, mud: -1, size: 0.035, volume: 2.5e-5 });
      }
    }
    // water draining off the body after it leaves deep water
    if (this.soak > 0.08 && this.inWater < 0.05) {
      for (const sx of [-1, 1]) {
        const p = b.toWorld([sx * 0.96, -0.32, 0]);
        out.push({ position: p, velocity: v3.add(v3.scale(b.linVel, 0.9), v3.scale(right, sx * 0.35)), spread: 0.25, radius: [0.3, 0.3], rate: 900 * this.soak, kind: 'spray', fromWater: false, size: 0.02, volume: 4e-6, mud: this.mudCoat * 0.5 });
      }
    }
    // bow splash when the body front plunges into water fast
    const front = b.toWorld([0, -0.35, 2.25]);
    const fs = v3.dot(b.linVel, fwd);
    const fd = this.frontDepth ?? 0;
    if (fd > 0.05 && fs > 3) {
      out.push({ position: front, velocity: v3.add(v3.scale(fwd, 0.55 * fs), [0, 0.35 * fs + 1.5, 0]), spread: 0.35 * fs, radius: [0.9, 0.3], rate: Math.min(12000, 900 * fd * fs), kind: 'spray', fromWater: true, mud: -1 });
    }
    return out;
  }

  /** render instances */
  instances() {
    const b = this.body, list = [];
    const M = (lp, half, rot = b.rotation) => mat4.fromRotationTranslation(rot, b.toWorld(lp), [half[0] * 2, half[1] * 2, half[2] * 2]);
    const coat = this.mudCoat;
    const paint = { color: [...this.color, coat], params: [0.28, 0.35, 0, 0] };
    list.push({ mesh: 'box', matrix: M([0, -0.1, 0.05], [0.95, 0.3, 2.25]), ...paint });
    list.push({ mesh: 'box', matrix: M([0, 0.2, -0.15], [0.9, 0.08, 1.6]), ...paint });
    list.push({ mesh: 'box', matrix: M([0, 0.43, -0.3], [0.76, 0.26, 1.1]), color: [0.015, 0.018, 0.022], params: [0.06, 0.0, 0, 0] });
    list.push({ mesh: 'box', matrix: M([0, 0.62, -2.05], [0.92, 0.035, 0.22]), color: [0.02, 0.02, 0.022], params: [0.4, 0.2, 0, 0] });
    list.push({ mesh: 'box', matrix: M([-0.75, 0.48, -2.0], [0.03, 0.14, 0.12]), color: [0.02, 0.02, 0.022], params: [0.4, 0.2, 0, 0] });
    list.push({ mesh: 'box', matrix: M([0.75, 0.48, -2.0], [0.03, 0.14, 0.12]), color: [0.02, 0.02, 0.022], params: [0.4, 0.2, 0, 0] });
    list.push({ mesh: 'box', matrix: M([-0.62, -0.02, 2.29], [0.2, 0.06, 0.02]), color: [1, 0.95, 0.85], params: [0.2, 0, 0, 3.0] });
    list.push({ mesh: 'box', matrix: M([0.62, -0.02, 2.29], [0.2, 0.06, 0.02]), color: [1, 0.95, 0.85], params: [0.2, 0, 0, 3.0] });
    list.push({ mesh: 'box', matrix: M([-0.65, 0.0, -2.31], [0.22, 0.05, 0.02]), color: [0.9, 0.02, 0.01], params: [0.2, 0, 0, 1.2] });
    list.push({ mesh: 'box', matrix: M([0.65, 0.0, -2.31], [0.22, 0.05, 0.02]), color: [0.9, 0.02, 0.01], params: [0.2, 0, 0, 1.2] });
    for (const wh of this.wheels) {
      const steer = wh.front ? this.steer : 0;
      const rot = quat.mul(quat.mul(b.rotation, quat.fromAxisAngle(UP, steer)), quat.fromAxisAngle(RIGHT, wh.spin));
      const c = this.wheelCenter(wh);
      list.push({ mesh: 'cylinder', matrix: mat4.fromRotationTranslation(rot, c, [this.cfg.halfWidth * 2, this.cfg.radius * 2, this.cfg.radius * 2]), color: [0.03, 0.03, 0.03, Math.min(1, coat * 1.5)], params: [0.8, 0, 2, 0] });
    }
    return list;
  }
}

const CHASSIS_POINTS = [];
for (const x of [-0.9, 0.9]) for (const z of [-2.2, 0, 2.2]) CHASSIS_POINTS.push([x, -0.38, z]);
for (const x of [-0.75, 0.75]) for (const z of [-1.2, 1.2]) CHASSIS_POINTS.push([x, 0.62, z]);
