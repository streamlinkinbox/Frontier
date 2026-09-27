// Flight controller + hunting state machine for the eagle.
// The bird is a point-mass steered by desired-velocity with bounded
// acceleration; orientation (bank/pitch) and wing-beat power are derived from
// the motion so that the animation is always consistent with what the body
// is doing (flapping only when power is needed, gliding when descending,
// braking flare when decelerating, etc.).
import * as THREE from '../vendor/three.module.js';
import { terrainHeight, roadY, roadZ, roadDir, cliffFaceZ, perchPoint } from './terrain.js';
import { clamp, lerp, smoothstep } from './noise.js';

const G = 9.81;
const DEG = Math.PI / 180;
const _v = new THREE.Vector3(), _v2 = new THREE.Vector3(), _v3 = new THREE.Vector3(), _up = new THREE.Vector3(0, 1, 0);
const _q = new THREE.Quaternion(), _e = new THREE.Euler();

export const STATES = ['PERCH', 'ALERT', 'LAUNCH', 'STOOP', 'APPROACH', 'FLARE', 'GRAB', 'LIFT', 'SLAM_RUN', 'PULL_UP', 'RETURN', 'LAND'];

export class EagleBrain {
  constructor(eagle, events = {}) {
    this.eagle = eagle;
    this.events = events; // { onScreech, onDownstroke, onGrab, onRelease, onLand, onLaunch }
    this.perch = perchPoint();
    this.pos = new THREE.Vector3();
    this.vel = new THREE.Vector3();
    this.acc = new THREE.Vector3();
    this.yaw = Math.PI; this.pitch = 0; this.roll = 0;
    this.state = 'PERCH'; this.t = 0; this.restUntil = 4;
    this.target = null;
    this.flapAmp = 0; this.flapPhase = 0.3; this.flapFreq = 1.0;
    this.lookTarget = new THREE.Vector3();
    this.nextLook = 0; this.nextBlink = 2; this.blink = 0; this.nextShrug = 8; this.shrug = 0;
    this.smooth = { fold: 1, spread: 0.3, brake: 0, tuck: 0, tailFan: 0.2, tailPitch: 0, crouch: 0, dihedral: 0, toeCurl: 0.5, toeSpread: 0.4, legLoad: 0 };
    this.legMode = { perch: 1, tucked: 0, strike: 0, carry: 0 };
    this.swing = { x: 0, vx: 0, z: 0, vz: 0 }; // pendulum of the carried load
    this.wallPoint = new THREE.Vector3(0, 30, 30); this.runIn = new THREE.Vector3(); this.escapeDir = 1; this.slammed = false; this.grabBlend = 0;
    this.attacks = 0;
    this.placeOnPerch();
  }

  perchRoot(out = new THREE.Vector3()) { return out.set(this.perch.x, this.perch.y + 3.45, this.perch.z - 0.6); }
  placeOnPerch() { this.perchRoot(this.pos); this.vel.set(0, 0, 0); this.yaw = Math.PI; this.pitch = 0; this.roll = 0; }

  setState(s) { this.state = s; this.t = 0; }

  /* ------------------------------------------------------------------ */
  pickTarget(cars) {
    let best = null, bestScore = -1;
    for (const c of cars) {
      if (!c.attackable || (c.mode !== 'player' && c.mode !== 'ai')) continue;
      const dx = c.phys.pos.x - this.perch.x;
      const spd = c.phys.vel.length();
      if (Math.abs(dx) > 260 || spd < 5) continue;
      // prefer cars that are approaching the perch and the player a little
      const approaching = Math.sign(c.phys.vel.x) !== Math.sign(dx) || Math.abs(dx) < 40;
      let score = (approaching ? 2 : 0.6) * (1 - Math.abs(dx) / 300) * (c.isPlayer ? 1.6 : 1);
      if (score > bestScore) { bestScore = score; best = c; }
    }
    return best;
  }
  targetValid() {
    const c = this.target;
    return c && c.attackable && (c.mode === 'player' || c.mode === 'ai' || c.mode === 'grabbed');
  }
  roofPoint(car, out = new THREE.Vector3()) { return out.set(0, 0.7, 0).applyQuaternion(car.phys.quat).add(car.phys.pos); }

  /** Steer toward a desired velocity with bounded acceleration. */
  steer(desiredVel, maxAcc, gain, dt) {
    _v.copy(desiredVel).sub(this.vel).multiplyScalar(gain);
    const l = _v.length(); if (l > maxAcc) _v.multiplyScalar(maxAcc / l);
    this.acc.copy(_v);
    this.vel.addScaledVector(_v, dt);
    this.pos.addScaledVector(this.vel, dt);
  }

  /* ------------------------------------------------------------------ */
  update(dt, cars, fx) {
    const S = this.smooth, E = this.eagle;
    this.t += dt;
    const st = this.state;
    let wantLegs = 'tucked', flapWant = 0, freq = 1.0, maxAcc = 12, gain = 1.4;
    let desired = _v2.set(0, 0, 0), airborne = true;
    let toeCurl = 1, toeSpread = 0.2, brake = 0, tuck = 0, spread = 0.75, tailFan = 0.3, crouch = 0, dihedral = 4 * DEG;
    const car = this.target;

    switch (st) {
      case 'PERCH': {
        airborne = false; wantLegs = 'perch'; toeCurl = 0.55; toeSpread = 0.7; spread = 0.2; tailFan = 0.15;
        this.perchRoot(this.pos); this.vel.set(0, 0, 0); this.pitch = 0; this.roll = 0; this.yaw = lerp(this.yaw, Math.PI, clamp(dt * 3, 0, 1));
        // idle: look around, blink, shrug
        if (this.t > this.nextLook) {
          this.nextLook = this.t + 1.5 + Math.random() * 3;
          const x = this.perch.x + (Math.random() - 0.5) * 320;
          this.lookTarget.set(x, roadY(x) + 1 + Math.random() * 6, roadZ(x) - Math.random() * 60);
        }
        if (this.t > this.nextShrug) { this.nextShrug = this.t + 9 + Math.random() * 12; this.shrugT = 0; }
        if (this.shrugT !== undefined) { this.shrugT += dt; this.shrug = Math.sin(clamp(this.shrugT / 1.4, 0, 1) * Math.PI) * (1 - smoothstep(1.2, 1.4, this.shrugT)); if (this.shrugT > 1.4) this.shrugT = undefined; }
        if (this.t > this.restUntil) {
          const tgt = this.pickTarget(cars);
          if (tgt) { this.target = tgt; this.setState('ALERT'); this.lookTarget.copy(tgt.phys.pos); }
        }
        break;
      }
      case 'ALERT': {
        airborne = false; wantLegs = 'perch'; toeCurl = 0.6; toeSpread = 0.7; spread = 0.2;
        this.perchRoot(this.pos); this.vel.set(0, 0, 0);
        if (!this.targetValid()) { this.setState('PERCH'); break; }
        this.lookTarget.copy(car.phys.pos);
        crouch = smoothstep(0.2, 1.0, this.t);
        // turn the body toward the target
        const dx = car.phys.pos.x - this.pos.x, dz = car.phys.pos.z - this.pos.z;
        const desiredYaw = Math.atan2(dx, dz);
        this.yaw += shortAngle(this.yaw, desiredYaw) * clamp(dt * 2.5, 0, 1);
        this.pitch = -crouch * 0.25;
        if (this.t > 1.25) { this.setState('LAUNCH'); this.events.onScreech?.(this.pos); this.events.onLaunch?.(); }
        break;
      }
      case 'LAUNCH': {
        wantLegs = this.t < 0.35 ? 'perch' : 'tucked'; crouch = Math.max(0, 1 - this.t * 4);
        spread = 1; flapWant = 1; freq = 1.25; tailFan = 0.8;
        if (this.t < 0.3) { // leg push off the ledge
          const fwd = _v3.set(Math.sin(this.yaw), 0.15, Math.cos(this.yaw));
          this.vel.copy(fwd).multiplyScalar(this.t / 0.3 * 9);
          this.pos.addScaledVector(this.vel, dt);
          this.pitch = -0.3 * (this.t / 0.3);
        } else {
          desired.set(Math.sin(this.yaw), -0.35, Math.cos(this.yaw)).normalize().multiplyScalar(22);
          maxAcc = 10; gain = 1.2;
          this.steer(desired, maxAcc, gain, dt);
        }
        if (!this.targetValid()) { this.setState('RETURN'); break; }
        if (this.t > 1.4) this.setState('STOOP');
        break;
      }
      case 'STOOP': {
        if (!this.targetValid()) { this.setState('RETURN'); break; }
        // intercept point behind & above the car, lead by flight time
        const cp = car.phys.pos, cv = car.phys.vel;
        const cspd = cv.length();
        const cdir = cspd > 0.5 ? _v3.copy(cv).divideScalar(cspd) : roadDir(cp.x, _v3);
        let tLead = 0;
        const aim = new THREE.Vector3();
        for (let i = 0; i < 4; i++) {
          aim.copy(cp).addScaledVector(cv, tLead).addScaledVector(cdir, -16).add(_v.set(0, 9, 0));
          tLead = aim.distanceTo(this.pos) / Math.max(25, this.vel.length() + 15);
        }
        const dist = aim.distanceTo(this.pos);
        const spdT = Math.min(clamp(26 + this.t * 16, 0, 66), clamp(cspd + 8 + (dist - 24) * 0.55, cspd + 8, 66));
        desired.copy(aim).sub(this.pos).normalize().multiplyScalar(spdT);
        maxAcc = 26; gain = 1.6;
        this.steer(desired, maxAcc, gain, dt);
        const speed = this.vel.length();
        tuck = smoothstep(24, 48, speed) * 0.85; spread = 1 - tuck; tailFan = 0.25;
        flapWant = (this.acc.dot(_v.copy(this.vel).normalize()) > 2 && speed < 30) ? 1 : 0; freq = 1.1;
        dihedral = -6 * DEG;
        this.lookTarget.copy(cp);
        if (dist < 22 || this.t > 9) this.setState('APPROACH');
        break;
      }
      case 'APPROACH': {
        if (!this.targetValid()) { this.setState('RETURN'); break; }
        const roof = this.roofPoint(car, new THREE.Vector3());
        const cv = car.phys.vel, cspd = cv.length();
        const cdir = cspd > 0.5 ? _v3.copy(cv).divideScalar(cspd) : roadDir(car.phys.pos.x, _v3);
        const aim = new THREE.Vector3().copy(roof).addScaledVector(cdir, -7).add(_v.set(0, 4.2, 0));
        const toAim = _v.copy(aim).sub(this.pos);
        const dist = toAim.length();
        const closing = clamp(dist * 1.6, 4, 16);
        desired.copy(cv).add(toAim.normalize().multiplyScalar(closing));
        maxAcc = 24; gain = 2.2;
        this.steer(desired, maxAcc, gain, dt);
        spread = 1; tailFan = 0.6; tuck = 0;
        flapWant = this.vel.length() < cspd + 3 ? 1 : 0; freq = 1.15;
        this.lookTarget.copy(roof);
        const horiz = Math.hypot(roof.x - this.pos.x, roof.z - this.pos.z);
        if (horiz < 10 && this.pos.y - roof.y < 7 || this.t > 8) { this.setState('FLARE'); this.events.onScreech?.(this.pos); }
        break;
      }
      case 'FLARE': {
        if (!this.targetValid()) { this.setState('RETURN'); break; }
        wantLegs = 'strike'; toeCurl = 0.05; toeSpread = 1; brake = smoothstep(0, 0.35, this.t); spread = 1; tailFan = 1; dihedral = 10 * DEG;
        const roof = this.roofPoint(car, new THREE.Vector3()).addScaledVector(car.phys.vel, dt);
        // desired root position = roof - (footAnchor - root)
        const footOff = _v.copy(E.footAnchor).sub(E.root.position);
        const want = new THREE.Vector3().copy(roof).sub(footOff);
        const err = _v3.copy(want).sub(this.pos);
        const corr = err.multiplyScalar(1.8); if (corr.length() > 11) corr.setLength(11);
        desired.copy(car.phys.vel).add(corr);
        maxAcc = 38; gain = 3;
        this.steer(desired, maxAcc, gain, dt);
        flapWant = 1; freq = 1.3;
        this.lookTarget.copy(roof);
        const d = E.footAnchor.distanceTo(roof);
        if (d < 2.6 && this.t > 0.4) { this.setState('GRAB'); this.events.onGrab?.(car, roof); this.grabCar(car); }
        else if (this.t > 1.8) { this.target = null; this.setState('RETURN'); }
        break;
      }
      case 'GRAB': {
        if (!this.targetValid()) { this.setState('RETURN'); break; }
        wantLegs = 'carry'; toeCurl = smoothstep(0, 0.3, this.t); toeSpread = 1 - toeCurl * 0.6; brake = 1 - smoothstep(0.2, 0.6, this.t); tailFan = 1;
        desired.copy(car.phys.vel).setY(0).add(_v.set(0, 3 + this.t * 6, 0));
        maxAcc = 12; gain = 2;
        this.steer(desired, maxAcc, gain, dt);
        flapWant = 1; freq = 0.95;
        this.lookTarget.copy(car.phys.pos).add(_v.set(0, 0, 0));
        if (this.t > 0.7) { this.planSlam(car); this.setState('LIFT'); }
        break;
      }
      case 'LIFT': {
        if (!this.targetValid()) { this.setState('RETURN'); break; }
        wantLegs = 'carry'; toeCurl = 1; toeSpread = 0.5; tailFan = 0.7;
        const to = _v.copy(this.runIn).sub(this.pos);
        const dist = to.length();
        // climb first (limited climb rate), circle toward the run-in point
        const climb = clamp((this.runIn.y - this.pos.y) * 0.4, -3, 6.5);
        to.y = 0; to.normalize();
        desired.copy(to).multiplyScalar(17).setY(climb);
        maxAcc = 7; gain = 1.1;
        this.steer(desired, maxAcc, gain, dt);
        flapWant = 1; freq = 0.85;
        this.lookTarget.copy(this.wallPoint);
        if (dist < 18 || this.t > 26) this.setState('SLAM_RUN');
        break;
      }
      case 'SLAM_RUN': {
        if (!this.targetValid()) { this.setState('RETURN'); break; }
        wantLegs = 'carry'; toeCurl = 1; toeSpread = 0.5; tailFan = 0.4;
        const to = _v.copy(this.wallPoint).sub(this.pos);
        const dist = to.length();
        const spdT = clamp(20 + this.t * 5, 20, 36);
        desired.copy(to).normalize().multiplyScalar(spdT);
        maxAcc = 12; gain = 1.6;
        this.steer(desired, maxAcc, gain, dt);
        flapWant = this.t < 3 ? 1 : 0.0; freq = 0.9; tuck = smoothstep(26, 36, this.vel.length()) * 0.4;
        this.lookTarget.copy(this.wallPoint);
        if (dist < 30) { this.releaseCar(car); this.setState('PULL_UP'); this.events.onScreech?.(this.pos); }
        break;
      }
      case 'PULL_UP': {
        wantLegs = 'tucked'; toeCurl = this.t < 0.4 ? 0.2 : 1; toeSpread = 0.6; brake = 1 - smoothstep(0.6, 1.4, this.t); spread = 1; tailFan = 1;
        // hard pull-up along the wall, banking away from it
        const away = _v.set(0, 0, -1);
        const along = _v3.set(this.escapeDir, 0, 0);
        desired.copy(_up).multiplyScalar(14).addScaledVector(along, 18).addScaledVector(away, 10);
        maxAcc = 34; gain = 2.5;
        this.steer(desired, maxAcc, gain, dt);
        flapWant = this.t > 0.5 ? 1 : 0; freq = 1.05;
        this.lookTarget.copy(this.wallPoint);
        if (this.t > 2.2) { this.target = null; this.setState('RETURN'); }
        break;
      }
      case 'RETURN': {
        wantLegs = 'tucked'; toeCurl = 1; toeSpread = 0.3;
        const rootP = this.perchRoot(new THREE.Vector3());
        const approach = new THREE.Vector3(this.perch.x, this.perch.y + 22, this.perch.z - 90);
        const dApp = this.pos.distanceTo(approach);
        const dPerch = this.pos.distanceTo(rootP);
        if (!this.finalApproach && (dApp < 22 || (dPerch < 60 && this.pos.z < this.perch.z - 20 && this.pos.y > this.perch.y))) this.finalApproach = true;
        if (!this.finalApproach) {
          const to = _v.copy(approach).sub(this.pos);
          const climb = clamp((approach.y - this.pos.y) * 0.35, -6, 6);
          to.y = 0; to.normalize();
          desired.copy(to).multiplyScalar(24).setY(climb);
          maxAcc = 9; gain = 1.2; tailFan = 0.35;
          this.steer(desired, maxAcc, gain, dt);
          flapWant = (climb > 0.5 || this.vel.length() < 14) ? 1 : 0; freq = 0.95;
          this.lookTarget.copy(rootP);
        } else {
          // final: glide in, flare, legs down
          const to = _v.copy(rootP).sub(this.pos);
          const d = to.length();
          const spdT = clamp(4 + d * 0.28, 4, 20);
          desired.copy(to).normalize().multiplyScalar(spdT);
          maxAcc = 16; gain = 2.4;
          this.steer(desired, maxAcc, gain, dt);
          const near = 1 - smoothstep(6, 26, d);
          brake = near; tailFan = 0.5 + near * 0.5; spread = 1; dihedral = 8 * DEG;
          if (near > 0.3) { wantLegs = 'perch'; toeCurl = 0.2; toeSpread = 0.8; }
          flapWant = d < 30 ? 1 : (this.vel.length() < 12 ? 1 : 0); freq = 1.2;
          this.lookTarget.copy(rootP).add(_v3.set(0, -2, -10));
          if (d < 1.4 || this.t > 40) { this.setState('LAND'); this.finalApproach = false; this.events.onLand?.(); }
        }
        break;
      }
      case 'LAND': {
        airborne = false; wantLegs = 'perch'; toeCurl = 0.55; toeSpread = 0.7;
        this.perchRoot(_v); this.pos.lerp(_v, clamp(dt * 6, 0, 1)); this.vel.multiplyScalar(0.8);
        // settle: two braking flaps, then fold; hop-turn to face the road
        const k = clamp(this.t / 1.6, 0, 1);
        flapWant = this.t < 0.9 ? 1 : 0; freq = 1.4; brake = 1 - k; spread = 1 - k; tailFan = 1 - k * 0.8;
        this.yaw += shortAngle(this.yaw, Math.PI) * clamp(dt * 2.2, 0, 1);
        this.pitch = lerp(this.pitch, 0, clamp(dt * 4, 0, 1)); this.roll = lerp(this.roll, 0, clamp(dt * 4, 0, 1));
        this.lookTarget.set(this.perch.x + 40, this.perch.y - 40, this.perch.z - 60);
        if (this.t > 2.4) { this.setState('PERCH'); this.restUntil = this.t + 5 + Math.random() * 6; this.nextLook = 0; this.attacks++; this.slammed = false; }
        break;
      }
    }

    // ------------------------------------------------------------------
    // keep the bird out of the ground (except when perched)
    if (airborne) {
      const gh = terrainHeight(this.pos.x, this.pos.z) + 2.5;
      if (this.pos.y < gh) { this.pos.y = gh; if (this.vel.y < 0) this.vel.y *= -0.2; }
    }

    // ---- orientation from motion
    const speed = this.vel.length();
    if (airborne && speed > 1.5) {
      const desiredYaw = Math.atan2(this.vel.x, this.vel.z);
      this.yaw += shortAngle(this.yaw, desiredYaw) * clamp(dt * 6, 0, 1);
      const climbAngle = Math.asin(clamp(this.vel.y / speed, -1, 1));
      // angle of attack rises as speed falls & when braking
      const aoa = clamp(0.22 - speed * 0.004, 0.02, 0.2) + brake * 0.55 + (st === 'FLARE' ? 0.15 : 0);
      const targetPitch = -(climbAngle + aoa) * (st === 'STOOP' ? 0.85 : 1); // negative rotation.x = nose up
      this.pitch = lerp(this.pitch, targetPitch, clamp(dt * 4, 0, 1));
      // bank from lateral acceleration
      const right = _v.set(Math.cos(this.yaw), 0, -Math.sin(this.yaw));
      const aLat = this.acc.dot(right);
      const targetRoll = clamp(Math.atan2(aLat, G) * 1.15, -70 * DEG, 70 * DEG);
      this.roll = lerp(this.roll, targetRoll, clamp(dt * 3.5, 0, 1));
    } else if (airborne) {
      this.roll = lerp(this.roll, 0, clamp(dt * 3, 0, 1));
    }

    // ---- wing power: flap only when thrust is needed
    const fwd = _v.set(Math.sin(this.yaw), 0, Math.cos(this.yaw));
    const accFwd = this.acc.dot(fwd);
    const climbPower = this.vel.y * G;
    const dragPower = 0.02 * speed * speed;
    let needFlap = flapWant;
    if (airborne && flapWant === 0 && (accFwd > 1.5 || climbPower > 12 || (speed < 13 && st !== 'STOOP'))) needFlap = 1;
    if (st === 'STOOP' && speed > 30) needFlap = 0;
    this.flapAmp = lerp(this.flapAmp, needFlap, clamp(dt * (needFlap ? 5 : 2.5), 0, 1));
    // frequency: slower with load, faster when braking / launching
    const load = (st === 'GRAB' || st === 'LIFT' || st === 'SLAM_RUN') ? 1 : 0;
    this.flapFreq = lerp(this.flapFreq, freq * (1 - load * 0.18), clamp(dt * 3, 0, 1));
    if (this.flapAmp > 0.03 || (this.flapPhase % (Math.PI * 2)) > 0.2) {
      const prev = this.flapPhase;
      this.flapPhase += Math.PI * 2 * this.flapFreq * dt * clamp(this.flapAmp + 0.25, 0.25, 1);
      // downstroke start event (top of the stroke)
      if (Math.floor(prev / (Math.PI * 2)) !== Math.floor(this.flapPhase / (Math.PI * 2)) && this.flapAmp > 0.3) this.events.onDownstroke?.(this.pos, this.flapAmp, load);
    }

    // ---- smooth pose parameters
    const k = clamp(dt * 5, 0, 1), kf = clamp(dt * 3.2, 0, 1);
    const foldWant = airborne ? 0 : (st === 'LAND' ? smoothstep(0.9, 2.2, this.t) : (st === 'ALERT' ? 0.9 - 0.2 * crouch : 1));
    S.fold = lerp(S.fold, foldWant, st === 'LAUNCH' ? clamp(dt * 6, 0, 1) : kf);
    S.spread = lerp(S.spread, spread, k); S.brake = lerp(S.brake, brake, clamp(dt * 7, 0, 1)); S.tuck = lerp(S.tuck, tuck, kf);
    S.tailFan = lerp(S.tailFan, tailFan, k); S.crouch = lerp(S.crouch, crouch, k); S.dihedral = lerp(S.dihedral, dihedral, kf);
    S.toeCurl = lerp(S.toeCurl, toeCurl, clamp(dt * 6, 0, 1)); S.toeSpread = lerp(S.toeSpread, toeSpread, clamp(dt * 6, 0, 1));
    S.legLoad = lerp(S.legLoad, load, k);
    for (const m of Object.keys(this.legMode)) this.legMode[m] = lerp(this.legMode[m], m === wantLegs ? 1 : 0, clamp(dt * 4, 0, 1));
    // tail as elevator/rudder: pitch with body pitch changes, roll with bank
    S.tailPitch = lerp(S.tailPitch, brake * -0.35 + (st === 'STOOP' ? 0.08 : 0) + clamp(-this.acc.y * 0.02, -0.25, 0.25), k);
    const tailRoll = clamp(-this.roll * 0.35, -0.5, 0.5);

    // blink
    if (this.t > this.nextBlink) { this.nextBlink = this.t + 2.5 + Math.random() * 4; this.blinkT = 0; }
    if (this.blinkT !== undefined) { this.blinkT += dt; this.blink = Math.sin(clamp(this.blinkT / 0.22, 0, 1) * Math.PI); if (this.blinkT > 0.22) { this.blinkT = undefined; this.blink = 0; } }

    // ---- apply to the model
    const P = E.params;
    P.flapPhase = this.flapPhase; P.flapAmp = this.flapAmp;
    P.fold = S.fold; P.spread = S.spread; P.brake = S.brake; P.tuck = S.tuck; P.dihedral = S.dihedral;
    P.tailFan = S.tailFan; P.tailPitch = S.tailPitch; P.tailRoll = tailRoll;
    P.legMode = this.legMode; P.toeCurl = S.toeCurl; P.toeSpread = S.toeSpread; P.legLoad = S.legLoad;
    P.headTarget = this.lookTarget; P.headLevelRoll = -this.roll * 0.8; P.crouch = S.crouch; P.blink = this.blink; P.shrug = this.shrug * (1 - S.crouch);
    E.root.position.copy(this.pos);
    E.root.position.y += (E.torsoOffset || 0);
    _e.set(this.pitch, this.yaw, this.roll, 'YXZ');
    E.root.quaternion.setFromEuler(_e);
    E.pose(dt);

    // ---- carried car follows the feet with a pendulum swing
    if (car && car.mode === 'grabbed') this.carryCar(car, dt);
  }

  /** Forget the current target (e.g. the player respawned mid-abduction). */
  dropTarget() {
    const c = this.target;
    if (!c) return;
    if (c.mode === 'grabbed') { c.mode = c.isPlayer ? 'player' : 'ai'; c.phys.kinematic = !c.isPlayer; }
    this.target = null;
    if (this.state !== 'PERCH' && this.state !== 'LAND') this.setState('RETURN');
  }

  /* ------------------------------------------------------------------ */
  grabCar(car) {
    car.mode = 'grabbed';
    car.phys.kinematic = true;
    car.phys.input.throttle = 0; car.phys.input.brake = 0;
    this.swing.x = 0; this.swing.vx = 0; this.swing.z = 0; this.swing.vz = 0; this.grabBlend = 0;
    this.carYaw = Math.atan2(_v.set(0, 0, 1).applyQuaternion(car.phys.quat).x, _v.z);
  }
  carryCar(car, dt) {
    const E = this.eagle, sw = this.swing;
    // pendulum (legs + car) driven by the eagle's horizontal acceleration, in the eagle's yaw frame
    const cy = Math.cos(this.yaw), sy = Math.sin(this.yaw);
    const aF = this.acc.x * sy + this.acc.z * cy;   // forward accel
    const aR = this.acc.x * cy - this.acc.z * sy;   // rightward accel
    const L = 4.2, damp = 1.1;
    sw.vx += (-(G / L) * Math.sin(sw.x) - (aF / L) * Math.cos(sw.x) - damp * sw.vx) * dt; sw.x += sw.vx * dt;
    sw.vz += (-(G / L) * Math.sin(sw.z) - (aR / L) * Math.cos(sw.z) - damp * sw.vz) * dt; sw.z += sw.vz * dt;
    sw.x = clamp(sw.x, -0.5, 0.5); sw.z = clamp(sw.z, -0.5, 0.5);
    // car orientation: eagle yaw + swing tilts; car "forward" follows the eagle heading gradually
    this.carYaw += shortAngle(this.carYaw, this.yaw) * clamp(dt * 1.2, 0, 1);
    _e.set(sw.x * 0.9, this.carYaw, -sw.z * 0.9, 'YXZ');
    car.phys.quat.setFromEuler(_e);
    // roof point hangs from the foot anchor, displaced by the swing
    const disp = _v.set(Math.sin(sw.z) * cy + Math.sin(sw.x) * sy, -(1 - Math.cos(sw.x)) - (1 - Math.cos(sw.z)), -Math.sin(sw.z) * sy + Math.sin(sw.x) * cy).multiplyScalar(1.6);
    const roofOff = _v3.set(0, 0.7, 0).applyQuaternion(car.phys.quat);
    const target = _v2.copy(E.footAnchor).add(disp).sub(roofOff);
    // blend from the car's free motion into the grip over the first third of a second
    this.grabBlend = Math.min(1, this.grabBlend + dt * 3);
    if (this.grabBlend < 1) { const free = car.phys.pos.clone().addScaledVector(car.phys.vel, dt); target.lerp(free, 1 - this.grabBlend); }
    // velocity for FX / release continuity
    car.phys.vel.copy(target).sub(car.phys.pos).divideScalar(Math.max(dt, 1e-3));
    if (car.phys.vel.length() > 80) car.phys.vel.setLength(80);
    car.phys.pos.copy(target);
    car.sync();
  }
  planSlam(car) {
    // pick a point on the cliff face ahead of the current heading, ~18 m above the road
    const dir = Math.sign(car.phys.vel.x || 1);
    const xw = clamp(this.pos.x + dir * 120, -600, 600);
    const zw = cliffFaceZ(xw);
    this.wallPoint.set(xw, roadY(xw) + 17, zw + 1.5);
    // run-in point: out over the valley, higher, so the run is a shallow dive at the wall
    this.runIn.set(xw - dir * 30, this.wallPoint.y + 26, zw - 125);
    this.escapeDir = dir;
  }
  releaseCar(car) {
    car.mode = 'thrown';
    car.phys.kinematic = false;
    const dir = _v.copy(this.wallPoint).sub(car.phys.pos).normalize();
    car.phys.vel.copy(this.vel).multiplyScalar(1.05).addScaledVector(dir, 6);
    car.phys.angVel.set((Math.random() - 0.5) * 2, (Math.random() - 0.5) * 2, (Math.random() - 0.5) * 2);
    car.attackable = false;
    this.slammed = true;
    this.events.onRelease?.(car);
  }
}

function shortAngle(a, b) { let d = (b - a) % (Math.PI * 2); if (d > Math.PI) d -= Math.PI * 2; if (d < -Math.PI) d += Math.PI * 2; return d; }
