import * as THREE from 'three';
import { buildTarantula, setShellCount } from './build.js';
import { BODY, gaitOffset } from './anatomy.js';
import { Limb } from './Limb.js';
import { ActionController } from './Actions.js';

const clamp = (x, a, b) => Math.min(Math.max(x, a), b);
const lerp = (a, b, t) => a + (b - a) * t;
const damp = (a, b, lambda, dt) => lerp(a, b, 1 - Math.exp(-lambda * dt));
const smoothstep = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };

const _v1 = new THREE.Vector3(), _v2 = new THREE.Vector3(), _v3 = new THREE.Vector3(), _v4 = new THREE.Vector3();
const _q = new THREE.Quaternion(), _q2 = new THREE.Quaternion(), _m = new THREE.Matrix4();
const _invQ = new THREE.Quaternion();

/**
 * Procedurally animated tarantula.
 *  - Locomotion: alternating-tetrapod gait (L1,R2,L3,R4 | R1,L2,R3,L4) with a ~10 % lead of the hind
 *    pair inside each tetrapod; speed is raised mostly through stride frequency (Biancardi et al.).
 *  - Duty factor falls from ~0.75 (slow walk) to ~0.55 (fast), feet are planted in world space (no slide).
 *  - Adhesion: the body frame follows the substrate normal (floor -> wall -> ceiling) using probe rays
 *    and the plane of the planted feet (scopulae give tarantulas grip on vertical surfaces).
 */
export class Tarantula {
  constructor(world, mats, opts = {}) {
    this.world = world;
    const parts = buildTarantula(mats, opts);
    Object.assign(this, parts);
    this.object = this.root;

    this.pos = new THREE.Vector3();     // ground point under the body
    this.up = new THREE.Vector3(0, 1, 0);
    this.fwd = new THREE.Vector3(0, 0, 1);
    this.speed = 0;                     // cm/s along fwd
    this.turnRate = 0;                  // rad/s
    this.phase = 0;
    this.time = 0;
    this.bodyLift = 0;
    this.heightAdj = 0;
    this.maxSpeed = 9;                  // cm/s (slow gait < 11 cm/s)
    this.runSpeed = 17;

    this.bodyPose = { pos: new THREE.Vector3(), rot: new THREE.Euler(0, 0, 0, 'YXZ') };
    this.abd = { pitch: 0, yaw: 0, pv: 0, yv: 0, breathe: 0 };
    this._lastFwdVel = new THREE.Vector3();

    const allLimbs = [...this.legs, ...this.palps];
    this.feet = allLimbs.map((limb, i) => ({
      limb,
      isPalp: limb.isPalp,
      offset: limb.isPalp ? (gaitOffset(0, limb.side) + 0.3) % 1 : gaitOffset(limb.legIndex, limb.side),
      pos: new THREE.Vector3(), normal: new THREE.Vector3(0, 1, 0),
      from: new THREE.Vector3(), fromN: new THREE.Vector3(), to: new THREE.Vector3(), toN: new THREE.Vector3(0, 1, 0),
      swinging: false, s: 0, lift: 0.5, pose: limb.makePose(), rest: new THREE.Vector3(), tapT: -1,
      retarget: 0,
    }));
    this.actions = new ActionController(this);
    this._initialised = false;
  }

  setShellCount(n) { setShellCount(this.root, n); }

  // --------------------------------------------------------------------------------------- setup
  placeAt(point, normal, forward) {
    this.pos.copy(point);
    this.up.copy(normal).normalize();
    this.fwd.copy(forward).addScaledVector(this.up, -forward.dot(this.up)).normalize();
    this._updateRootTransform(0, true);
    for (const f of this.feet) {
      this._restWorld(f, f.rest);
      const hit = this._foothold(f, f.rest);
      f.pos.copy(hit.p); f.normal.copy(hit.n); f.swinging = false;
    }
    this._initialised = true;
  }

  // rest foot position in the (unposed) locomotion frame
  _restLocal(f, out) {
    const limb = f.limb;
    const d = limb.planeDir(limb.yaw0, _v4);
    const reach = limb.isPalp ? 1.55 : limb.totalLen * limb.reachFrac + limb.L[0] * 0.7;
    return out.copy(limb.S).addScaledVector(d, reach).setY(-BODY.bodyHeight + (limb.isPalp ? 0.05 : 0));
  }
  _restWorld(f, out) {
    this._restLocal(f, out);
    return out.applyMatrix4(this.root.matrixWorld);
  }

  // Find a foothold near `candidate` (world). Handles concave (wall ahead) and convex (edge) cases.
  _foothold(f, candidate) {
    const w = this.world, up = this.up;
    const pad = f.isPalp ? 0.08 : f.limb.R[f.limb.n - 1] * 0.8;
    const center = _v1.copy(this.pos).addScaledVector(up, 0.9);
    // 1) from body toward the candidate: hits a wall first?
    const toC = _v2.copy(candidate).addScaledVector(up, 0.35).sub(center);
    const dist = toC.length();
    let hit = w.raycast(center, toC.divideScalar(dist), dist);
    if (!hit) {
      // 2) straight down onto the candidate
      const o = _v3.copy(candidate).addScaledVector(up, 2.2);
      hit = w.raycast(o, _v2.copy(up).negate(), 5.0);
    }
    if (!hit) {
      // 3) wrap around a convex edge: from below the candidate back toward the body
      const o = _v3.copy(candidate).addScaledVector(up, -1.8);
      const dir = _v2.copy(this.pos).addScaledVector(up, -1.8).sub(o);
      const l = dir.length();
      hit = w.raycast(o, dir.divideScalar(l), l + 1.5);
    }
    if (!hit) return { p: candidate.clone(), n: up.clone(), ok: false };
    return { p: hit.point.clone().addScaledVector(hit.normal, pad), n: hit.normal.clone(), ok: true };
  }

  // --------------------------------------------------------------------------------------- update
  /**
   * @param dt seconds
   * @param ctrl { dir: Vector3|null (world, desired heading), speed: 0..1, run: bool }
   */
  update(dt, ctrl) {
    dt = Math.min(dt, 1 / 20);
    this.time += dt;
    const act = this.actions.update(dt, ctrl);

    this._steer(dt, ctrl, act);
    this._adhere(dt);
    this._updateRootTransform(dt);
    this._gait(dt, act);
    this._pose(dt, act);
    this.root.updateMatrixWorld(true);
    this._solveLimbs(dt, act);
    this._secondary(dt, act);
  }

  _steer(dt, ctrl, act) {
    const up = this.up;
    let desiredSpeed = 0, turn = 0;
    const lock = act.locomotionLock; // 0..1
    if (ctrl && ctrl.dir && ctrl.speed > 0.01) {
      const d = _v1.copy(ctrl.dir).addScaledVector(up, -ctrl.dir.dot(up));
      if (d.lengthSq() > 1e-6) {
        d.normalize();
        const cross = _v2.crossVectors(this.fwd, d);
        const ang = Math.atan2(cross.dot(up), this.fwd.dot(d));
        const top = ctrl.run ? this.runSpeed : this.maxSpeed;
        const align = smoothstep(1.9, 0.5, Math.abs(ang));
        desiredSpeed = top * ctrl.speed * align * (1 - lock);
        const maxTurn = (ctrl.run ? 3.2 : 2.3) * (1 - lock * 0.7);
        turn = clamp(ang * 4.0, -maxTurn, maxTurn);
      }
    }
    if (act.turn) turn += act.turn;
    this.turnRate = damp(this.turnRate, turn, 10, dt);
    const accel = desiredSpeed > this.speed ? 7 : 9;
    this.speed = damp(this.speed, desiredSpeed, accel, dt);
    if (Math.abs(this.speed) < 0.02 && desiredSpeed === 0) this.speed = 0;
    // rotate heading about the surface normal
    _q.setFromAxisAngle(up, this.turnRate * dt);
    this.fwd.applyQuaternion(_q).normalize();
    this.pos.addScaledVector(this.fwd, this.speed * dt);
    if (act.shove) this.pos.addScaledVector(this.fwd, act.shove * dt);
  }

  _adhere(dt) {
    const w = this.world, up = this.up, fwd = this.fwd;
    const side = _v4.crossVectors(up, fwd).normalize();
    let groundN = null;
    // re-project the ground point along -up
    let hit = w.raycast(_v1.copy(this.pos).addScaledVector(up, 1.4), _v2.copy(up).negate(), 3.2);
    if (hit) { this.pos.copy(hit.point); groundN = hit.normal.clone(); }
    else {
      // convex edge: wrap around by casting back under the body
      hit = w.raycast(_v1.copy(this.pos).addScaledVector(up, -0.9), _v2.copy(fwd).negate(), 3.5);
      if (!hit) hit = w.raycast(_v1.copy(this.pos).addScaledVector(up, -0.9), _v2.copy(fwd), 3.5);
      if (hit) { this.pos.copy(hit.point); groundN = hit.normal.clone(); }
    }
    const nT = _v3.set(0, 0, 0);
    if (groundN) nT.addScaledVector(groundN, 2.0);
    const origin = _v1.copy(this.pos).addScaledVector(up, 1.0);
    const probe = (dx, dz, far, weight) => {
      const dir = _v2.copy(up).multiplyScalar(-1).addScaledVector(fwd, dz).addScaledVector(side, dx).normalize();
      const h = w.raycast(origin, dir, far);
      if (h) nT.addScaledVector(h.normal, weight / (1 + h.distance * 0.5));
    };
    probe(0, 1.1, 4.5, 1.2); probe(0, -1.1, 4.5, 1.0); probe(1.1, 0, 4.5, 1.0); probe(-1.1, 0, 4.5, 1.0);
    // wall ahead (concave transition): ramp its influence as it gets closer
    const wall = w.raycast(_v1.copy(this.pos).addScaledVector(up, 0.6), fwd, 3.2);
    if (wall && wall.normal.dot(up) < 0.8) {
      const k = 1 - wall.distance / 3.2;
      nT.addScaledVector(wall.normal, 5.0 * k * k);
    }
    // plane of planted feet
    const fn = this._feetNormal();
    if (fn) nT.addScaledVector(fn, 1.2);
    if (nT.lengthSq() > 1e-6) {
      nT.normalize();
      const k = 1 - Math.exp(-dt * (6 + Math.abs(this.speed) * 0.6));
      _q.setFromUnitVectors(up, _v1.copy(up).lerp(nT, k).normalize());
      up.applyQuaternion(_q).normalize();
      fwd.applyQuaternion(_q);
    }
    fwd.addScaledVector(up, -fwd.dot(up)).normalize();
  }

  _feetNormal() {
    const lf = _v1.set(0, 0, 0), rt = new THREE.Vector3(), fr = new THREE.Vector3(), bk = new THREE.Vector3();
    let nl = 0, nr = 0, nf = 0, nb = 0;
    for (const f of this.feet) {
      if (f.isPalp || f.swinging) continue;
      if (f.limb.side > 0) { lf.add(f.pos); nl++; } else { rt.add(f.pos); nr++; }
      if (f.limb.legIndex <= 1) { fr.add(f.pos); nf++; } else { bk.add(f.pos); nb++; }
    }
    if (!nl || !nr || !nf || !nb) return null;
    lf.divideScalar(nl); rt.divideScalar(nr); fr.divideScalar(nf); bk.divideScalar(nb);
    const a = fr.sub(bk), b = lf.sub(rt);
    const n = new THREE.Vector3().crossVectors(a, b);
    if (n.lengthSq() < 1e-8) return null;
    n.normalize();
    if (n.dot(this.up) < 0) n.negate();
    return n;
  }

  _updateRootTransform(dt, snap = false) {
    // body height from planted feet (keeps clearance on uneven ground)
    let h = 0, c = 0;
    if (this._initialised) {
      for (const f of this.feet) {
        if (f.isPalp || f.swinging) continue;
        h += _v1.subVectors(f.pos, this.pos).dot(this.up); c++;
      }
    }
    const target = c ? clamp(h / c, -0.8, 1.2) * 0.65 : 0;
    this.heightAdj = snap ? target : damp(this.heightAdj, target, 6, dt);
    this.root.position.copy(this.pos).addScaledVector(this.up, BODY.bodyHeight + this.heightAdj);
    const x = _v1.crossVectors(this.up, this.fwd).normalize();
    _m.makeBasis(x, this.up, this.fwd);
    this.root.quaternion.setFromRotationMatrix(_m);
    this.root.updateMatrixWorld(true);
  }

  _gait(dt, act) {
    const speedAbs = Math.abs(this.speed);
    const sn = clamp(speedAbs / this.runSpeed, 0, 1);
    const stride = lerp(2.3, 3.5, sn);
    const duty = lerp(0.75, 0.56, sn);
    const swingFrac = 1 - duty;
    this.gaitInfo = { stride, duty, sn };
    const travel = speedAbs * dt + Math.abs(this.turnRate) * dt * 2.6;
    let dPhase = travel / stride;

    // rest targets & error
    let maxErr = 0, anySwing = false;
    for (const f of this.feet) {
      this._restWorld(f, f.rest);
      if (f.swinging) anySwing = true;
      else if (!f.isPalp && !act.legLock(f)) maxErr = Math.max(maxErr, f.pos.distanceTo(f.rest));
    }
    const settling = dPhase < dt * 0.25 && (anySwing || maxErr > 0.75);
    if (settling) dPhase = Math.max(dPhase, dt * 1.7);
    this.phase += dPhase;
    const moving = speedAbs > 0.15 || Math.abs(this.turnRate) > 0.08;

    for (const f of this.feet) {
      const locked = act.legLock(f);
      const lp = ((this.phase - f.offset) % 1 + 1) % 1;
      const inSwing = lp < swingFrac;
      if (locked) { if (f.swinging) this._land(f); continue; }
      if (inSwing && !f.swinging && dPhase > 0 && lp < swingFrac * 0.4) {
        const err = f.pos.distanceTo(f.rest);
        if (moving || err > 0.35) {
          f.swinging = true; f.from.copy(f.pos); f.fromN.copy(f.normal); f.retarget = 0; f.lp0 = lp;
          this._computeTarget(f, swingFrac, lp);
          f.lift = clamp(0.35 + 0.22 * f.from.distanceTo(f.to), 0.35, 1.1) * (f.isPalp ? 0.7 : 1);
        }
      }
      if (f.swinging) {
        if (!inSwing) { this._land(f); continue; }
        f.s = clamp((lp - f.lp0) / Math.max(swingFrac - f.lp0, 1e-3), 0, 1);
        // retarget during early swing (turning / uneven terrain)
        if (f.s < 0.7 && (f.retarget++ % 3) === 0) this._computeTarget(f, swingFrac, lp);
        const e = 0.5 - 0.5 * Math.cos(Math.PI * f.s);
        const n = _v1.copy(f.fromN).lerp(f.toN, e).normalize();
        f.pos.copy(f.from).lerp(f.to, e).addScaledVector(n, f.lift * Math.pow(Math.sin(Math.PI * f.s), 0.9));
        f.normal.copy(n);
      }
    }
  }

  _land(f) {
    f.swinging = false; f.s = 0;
    f.pos.copy(f.to); f.normal.copy(f.toN);
  }

  _computeTarget(f, swingFrac, lp) {
    const { stride, duty } = this.gaitInfo;
    const dir = this.speed >= 0 ? 1 : -1;
    const mv = clamp(Math.abs(this.speed) / 0.8, 0, 1);
    const cand = _v2.copy(f.rest);
    // place the foot ahead so it is centred in its stance phase
    cand.addScaledVector(this.fwd, dir * mv * stride * (duty * 0.5 + (1 - clamp(lp / swingFrac, 0, 1)) * swingFrac));
    // turning: rotate candidate about the body centre
    const turnAhead = this.turnRate * (stride / Math.max(Math.abs(this.speed), 3)) * duty * 0.5;
    if (Math.abs(turnAhead) > 1e-4) {
      _q.setFromAxisAngle(this.up, turnAhead);
      cand.sub(this.pos).applyQuaternion(_q).add(this.pos);
    }
    const hit = this._foothold(f, cand.clone());
    f.to.copy(hit.p); f.toN.copy(hit.n);
  }

  _pose(dt, act) {
    // gait-induced body motion: small vertical bob and roll between the two tetrapods
    let bob = 0, roll = 0, pitch = 0;
    for (const f of this.feet) {
      if (!f.swinging || f.isPalp) continue;
      const k = Math.sin(Math.PI * f.s);
      bob -= 0.018 * k;
      roll += 0.012 * k * f.limb.side * (f.limb.legIndex === 1 || f.limb.legIndex === 2 ? 1 : 0.5);
      pitch += 0.006 * k * (f.limb.legIndex <= 1 ? 1 : -1);
    }
    const sn = this.gaitInfo ? this.gaitInfo.sn : 0;
    const breath = Math.sin(this.time * 1.25) * 0.012;
    const bp = this.bodyPose;
    bp.pos.set(0, damp(bp.pos.y, bob + breath * 0.3 + act.body.y, 18, dt), damp(bp.pos.z, act.body.z - sn * 0.1, 14, dt));
    bp.pos.x = damp(bp.pos.x, act.body.x, 14, dt);
    bp.rot.x = damp(bp.rot.x, -act.body.pitch + pitch - sn * 0.03, 14, dt);
    bp.rot.z = damp(bp.rot.z, roll + act.body.roll, 14, dt);
    bp.rot.y = damp(bp.rot.y, act.body.yaw, 14, dt);
    this.body.position.copy(bp.pos);
    this.body.rotation.copy(bp.rot);
  }

  _solveLimbs(dt, act) {
    const body = this.body;
    body.updateMatrixWorld(true);
    const inv = _m.copy(body.matrixWorld).invert();
    body.getWorldQuaternion(_invQ).invert();
    const tLocal = new THREE.Vector3(), nLocal = new THREE.Vector3();
    for (const f of this.feet) {
      const limb = f.limb;
      tLocal.copy(f.pos).applyMatrix4(inv);
      nLocal.copy(f.normal).applyQuaternion(_invQ);
      const tarsusLift = f.swinging ? 0.55 * Math.sin(Math.PI * f.s) : 0;
      limb.solve(tLocal, nLocal, f.pose, tarsusLift);
      const ov = act.override(f, dt);
      if (ov && ov.w > 0) Limb.lerpPose(f.pose, ov.pose, ov.w, f.pose);
      limb.apply(f.pose);
    }
  }

  _secondary(dt, act) {
    // abdomen: damped spring driven by body acceleration + gait sway; breathing (book lungs)
    const a = this.abd;
    const accel = (this.speed - (this._prevSpeed ?? 0)) / Math.max(dt, 1e-3);
    this._prevSpeed = this.speed;
    let sway = 0;
    for (const f of this.feet) if (f.swinging && !f.isPalp) sway += Math.sin(Math.PI * f.s) * f.limb.side * 0.02;
    const targetPitch = clamp(-accel * 0.004, -0.12, 0.12) + act.abdomenPitch;
    const targetYaw = -this.turnRate * 0.05 + sway + act.abdomenYaw;
    const k = 90, c = 12;
    a.pv += ((targetPitch - a.pitch) * k - a.pv * c) * dt; a.pitch += a.pv * dt;
    a.yv += ((targetYaw - a.yaw) * k - a.yv * c) * dt; a.yaw += a.yv * dt;
    this.abdPivot.rotation.set(-0.06 + a.pitch, a.yaw, 0, 'YXZ');
    const b = 1 + Math.sin(this.time * 1.25) * 0.012;
    this.abdAnchor.scale.set(1 + (b - 1) * 0.6, b, 1);

    // chelicerae & fangs
    for (let i = 0; i < 2; i++) {
      const ch = this.chelicerae[i];
      ch.rotation.x = ch.userData.restPitch - act.chelRaise;
      this.fangs[i].rotation.x = Math.PI / 2 - act.fangOpen;
    }
    // spinnerets: slow exploratory motion
    const t = this.time;
    this.spinnerets.forEach((chain, i) => {
      chain[1].rotation.x = -0.22 + 0.05 * Math.sin(t * 0.9 + i * 2.1);
      chain[1].rotation.y = 0.04 * Math.sin(t * 0.6 + i);
    });
  }

  getHeadWorld(out) { return out.set(0, 0.2, 1.3).applyMatrix4(this.body.matrixWorld); }
}
