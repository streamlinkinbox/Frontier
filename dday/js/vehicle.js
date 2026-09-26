// Arcade vehicle physics on the heightfield with obstacle collisions
import * as THREE from '../vendor/three.module.min.js';
import { clamp, lerp } from './util.js';
import { ROAD_HW, FIELD_HALF, WALL_Z } from './layout.js';

const G = 22;
const SURF = {
  road: { accel: 15, vmax: 34, roll: 0.35, grip: 9.0 },
  sand: { accel: 10, vmax: 21, roll: 1.5, grip: 5.2 },
  wet: { accel: 8, vmax: 16, roll: 2.4, grip: 4.2 },
  grass: { accel: 12, vmax: 25, roll: 0.9, grip: 6.5 },
  mud: { accel: 7, vmax: 13, roll: 3.0, grip: 4.0 },
};

export class Vehicle {
  constructor(mesh, T, world) {
    this.mesh = mesh; this.T = T; this.W = world;
    this.pos = new THREE.Vector3(); this.vel = new THREE.Vector3();
    this.yaw = 0; this.pitch = 0; this.roll = 0; this.steer = 0;
    this.av = new THREE.Vector3(); // airborne angular velocity (pitch, yaw, roll)
    this.hp = 100; this.grounded = true; this.surface = 'sand'; this.inWire = false; this.inWater = 0;
    this.wheelRot = 0; this.susp = [0, 0, 0, 0];
    this._q = []; this.lastHit = 0;
    this.events = [];
  }
  reset(x, z, yaw) {
    this.pos.set(x, this.T.groundAt(x, z) + 0.05, z);
    this.vel.set(0, 0, 0); this.yaw = yaw; this.pitch = this.roll = 0; this.av.set(0, 0, 0); this.steer = 0;
    this.hp = 100; this.grounded = true;
  }
  get speed() { return Math.hypot(this.vel.x, this.vel.z); }
  forward() { return new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw)); }

  damage(v, cause) { if (v <= 0) return; this.hp = Math.max(0, this.hp - v); this.events.push({ type: 'damage', v, cause }); }
  launch(fromX, fromZ, power) {
    const dx = this.pos.x - fromX, dz = this.pos.z - fromZ, d = Math.hypot(dx, dz) || 1;
    this.vel.y = Math.max(this.vel.y, power);
    this.vel.x += dx / d * power * 0.35; this.vel.z += dz / d * power * 0.35;
    this.av.set((Math.random() - 0.5) * 5, (Math.random() - 0.5) * 3, (Math.random() - 0.5) * 6);
    this.grounded = false;
    this.pos.y += 0.2;
  }

  update(dt, input, waterLevel) {
    const T = this.T, p = this.pos;
    const ri = T.roadInfo(p.x, p.z);
    const ground = T.groundAt(p.x, p.z, ri);
    const depth = waterLevel - ground;
    this.inWater = depth;
    // surface
    const trench = T.lineInfo(p.x, p.z);
    let surf = ri.d < ROAD_HW + 0.6 ? 'road' : p.z < 330 ? (depth > -0.3 || p.z < 25 ? 'wet' : 'sand') : 'grass';
    if (trench.d < 2 && trench.type === 'trench' && ri.d > ROAD_HW + 4) surf = 'mud';
    this.surface = surf;
    const S = SURF[surf];
    // wire
    this.inWire = false;
    for (const w of this.W.wires.query(p.x, p.z, 3, this._q)) {
      const dx = w.bx - w.ax, dz = w.bz - w.az, L2 = dx * dx + dz * dz;
      const t = clamp(((p.x - w.ax) * dx + (p.z - w.az) * dz) / L2, 0, 1);
      if (Math.hypot(p.x - (w.ax + dx * t), p.z - (w.az + dz * t)) < w.hw + 0.8 && p.y < ground + 1.2) { this.inWire = true; break; }
    }

    const fwd = new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw));
    const right = new THREE.Vector3(-Math.cos(this.yaw), 0, Math.sin(this.yaw));
    let vF = this.vel.dot(fwd), vR = this.vel.dot(right);
    const dead = this.hp <= 0;
    const thr = dead ? 0 : input.throttle, brk = dead ? 0 : input.brake;

    if (this.grounded) {
      let accel = S.accel, vmax = S.vmax, roll = S.roll, grip = S.grip;
      if (this.inWire) { vmax = 6; roll += 6; }
      if (depth > 0.05) { roll += depth * 7; vmax *= Math.max(0.25, 1 - depth * 0.9); }
      if (input.handbrake) { grip *= 0.25; roll += 3.5; }
      // throttle / brake / reverse
      if (thr > 0) vF += accel * thr * dt * clamp(1 - vF / vmax, -0.5, 1);
      if (brk > 0) { if (vF > 0.8) vF -= 26 * brk * dt; else vF = Math.max(-9, vF - 8 * brk * dt); }
      // rolling resistance & aero
      const drag = (roll + 0.0016 * vF * vF) * dt;
      vF -= Math.sign(vF) * Math.min(Math.abs(vF), drag);
      if (vF > vmax * 1.05) vF = lerp(vF, vmax, 1 - Math.exp(-2 * dt));
      // lateral grip (allows slides)
      vR *= Math.exp(-grip * dt);
      // steering (bicycle model)
      const maxSteer = lerp(0.62, 0.16, clamp(Math.abs(vF) / 32, 0, 1));
      this.steer = lerp(this.steer, (dead ? 0 : input.steer) * maxSteer, 1 - Math.exp(-10 * dt));
      this.yaw += (vF / 2.8) * Math.tan(this.steer) * dt * (input.handbrake ? 1.35 : 1);
      const nf = new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw)), nr = new THREE.Vector3(-Math.cos(this.yaw), 0, Math.sin(this.yaw));
      const vy = this.vel.y;
      this.vel.copy(nf).multiplyScalar(vF).addScaledVector(nr, vR);
      // gravity along slope
      const n = T.normalAt(p.x, p.z);
      this.vel.x += n.x * G * 0.85 * dt; this.vel.z += n.z * G * 0.85 * dt;
      this.vel.y = vy;
    } else {
      this.vel.y -= G * dt;
      this.pitch += this.av.x * dt; this.yaw += this.av.y * dt; this.roll += this.av.z * dt;
      this.av.multiplyScalar(Math.exp(-0.6 * dt));
    }

    // integrate
    const prevGround = ground;
    p.addScaledVector(this.vel, dt);
    // boundaries
    if (Math.abs(p.x) > FIELD_HALF) { p.x = Math.sign(p.x) * FIELD_HALF; this.vel.x *= -0.3; }
    if (p.z > WALL_Z + 20) { p.z = WALL_Z + 20; this.vel.z = 0; }
    this.collide(dt);

    const g2 = T.groundAt(p.x, p.z);
    if (p.y <= g2) {
      if (!this.grounded) {
        const impact = -this.vel.y;
        if (impact > 9) this.damage((impact - 9) * 2.2, 'landing');
        this.events.push({ type: 'land', v: impact });
      }
      p.y = g2; this.vel.y = Math.max(0, (g2 - prevGround) / dt * 0.0); this.grounded = true;
    } else if (this.grounded && p.y - g2 < 0.35 + this.speed * 0.012) {
      // stick to ground over small drops; keep vertical speed for crests
      const vyTerrain = (g2 - prevGround) / dt;
      p.y = g2; this.vel.y = vyTerrain;
    } else {
      if (this.grounded) { this.av.set(this.pitch * 0, 0, 0); }
      this.grounded = false;
    }

    // orientation from wheel contact points
    if (this.grounded) {
      const c = Math.cos(this.yaw), s = Math.sin(this.yaw);
      const hp = (lx, lz) => T.groundAt(p.x + lx * c + lz * s, p.z - lx * s + lz * c);
      const fl = hp(0.8, 1.4), fr = hp(-0.8, 1.4), rl = hp(0.8, -1.38), rr = hp(-0.8, -1.38);
      const tp = Math.atan2((rl + rr) / 2 - (fl + fr) / 2, 2.78), tr = Math.atan2((fl + rl) / 2 - (fr + rr) / 2, 1.6);
      const k = 1 - Math.exp(-14 * dt);
      this.pitch = lerp(wrap(this.pitch), tp, k); this.roll = lerp(wrap(this.roll), tr, k);
      const avg = (fl + fr + rl + rr) / 4;
      p.y = Math.max(p.y, avg - 0.05);
      this.susp = [fl, fr, rl, rr].map(h => h - avg);
    }
    this.wheelRot += this.vel.dot(new THREE.Vector3(Math.sin(this.yaw), 0, Math.cos(this.yaw))) / 0.31 * dt;

    // wire damage
    if (this.inWire && this.speed > 1) this.damage(6 * dt, 'wire');
    // write to mesh
    this.mesh.position.copy(p);
    this.mesh.rotation.set(this.pitch, this.yaw, this.roll, 'YXZ');
    for (const w of this.mesh.userData.wheels) {
      w.spin.rotation.x = this.wheelRot;
      if (w.front) w.pivot.rotation.y = this.steer;
    }
  }

  collide(dt) {
    const p = this.pos, c = Math.cos(this.yaw), s = Math.sin(this.yaw);
    const circles = [1.45, 0, -1.45];
    for (const off of circles) {
      const cx = p.x + s * off, cz = p.z + c * off, r = 0.95;
      for (const o of this.W.colliders.query(cx, cz, r + 4, this._q)) {
        if (o.dead) continue;
        const groundH = this.T.heightAt(o.x, o.z);
        if (p.y > groundH + o.top) continue; // jumped over it
        let nx = 0, nz = 0, pen = 0;
        if (o.type === 'c') {
          const dx = cx - o.x, dz = cz - o.z, d = Math.hypot(dx, dz);
          if (d < o.r + r) { pen = o.r + r - d; nx = dx / (d || 1); nz = dz / (d || 1); }
        } else {
          // box local frame: local x = (c, -s), local z = (s, c)
          const dx = cx - o.x, dz = cz - o.z;
          const lx = dx * o.c - dz * o.s, lz = dx * o.s + dz * o.c;
          const qx = clamp(lx, -o.hx, o.hx), qz = clamp(lz, -o.hz, o.hz);
          let ex = lx - qx, ez = lz - qz, d = Math.hypot(ex, ez);
          if (d < r) {
            if (d < 1e-4) { // centre inside the box: push out along smallest axis
              const px = o.hx - Math.abs(lx), pz = o.hz - Math.abs(lz);
              if (px < pz) { ex = Math.sign(lx); ez = 0; pen = px + r; } else { ex = 0; ez = Math.sign(lz); pen = pz + r; }
              d = 1;
            } else pen = r - d;
            const lnx = ex / d, lnz = ez / d;
            nx = lnx * o.c + lnz * o.s; nz = -lnx * o.s + lnz * o.c;
          }
        }
        if (pen > 0) {
          p.x += nx * pen; p.z += nz * pen;
          const vn = this.vel.x * nx + this.vel.z * nz;
          if (vn < 0) {
            this.vel.x -= 1.35 * vn * nx; this.vel.z -= 1.35 * vn * nz;
            this.vel.x *= 0.8; this.vel.z *= 0.8;
            const imp = -vn;
            if (imp > 4 && performance.now() - this.lastHit > 180) {
              this.lastHit = performance.now();
              this.damage((imp - 4) * 1.6, o.kind);
              this.events.push({ type: 'crash', v: imp, x: p.x + s * off, z: p.z + c * off });
            }
            if (o.mine && o.mine.alive) this.events.push({ type: 'trigger', mine: o.mine });
            // yaw kick when hit off-centre
            this.yaw += clamp(off * (nx * c - nz * s) * 0.05 * Math.min(imp, 10) * 0.2, -0.2, 0.2);
          }
        }
      }
    }
  }
}
function wrap(a) { return Math.atan2(Math.sin(a), Math.cos(a)); }
