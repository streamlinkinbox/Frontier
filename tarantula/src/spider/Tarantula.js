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
const _vS = new THREE.Vector3(), _vB = new THREE.Vector3(), _vC = new THREE.Vector3();
const _vP = new THREE.Vector3(), _tP = new THREE.Vector3(), _nP = new THREE.Vector3(), _mP = new THREE.Matrix4(), _qP = new THREE.Quaternion();

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
      retarget: 0, err: 0, femurLift: 0, tarBias: 0, restP: new THREE.Vector3(), altPose: limb.makePose(),
    }));
    this.actions = new ActionController(this);
    this._initialised = false;
    // Terrain clearance volumes. Front: chelicerae (lowest point) + sternum; rear: spheres filling the
    // abdomen. Resolved with signed distances to the rock so it works on floors, walls and ceilings.
    this.root.updateMatrixWorld(true);
    const lowest = (obj) => {
      let best = null;
      const inv = new THREE.Matrix4().copy(obj.matrixWorld).invert(), rel = new THREE.Matrix4();
      obj.traverse((o) => {
        if (!o.isMesh || o.isInstancedMesh || !o.name.endsWith(':base')) return;
        rel.multiplyMatrices(inv, o.matrixWorld);
        const pa = o.geometry.attributes.position, v = new THREE.Vector3();
        for (let i = 0; i < pa.count; i += 3) { v.fromBufferAttribute(pa, i).applyMatrix4(rel); if (!best || v.y < best.y) best = v.clone(); }
      });
      return best || new THREE.Vector3();
    };
    const abdBox = new THREE.Box3();
    {
      const inv = new THREE.Matrix4().copy(this.abdAnchor.matrixWorld).invert(), rel = new THREE.Matrix4(), v = new THREE.Vector3();
      this.abdAnchor.traverse((o) => {
        if (!o.isMesh || o.isInstancedMesh || o.name !== 'abdomen:base') return;
        rel.multiplyMatrices(inv, o.matrixWorld);
        const pa = o.geometry.attributes.position;
        for (let i = 0; i < pa.count; i += 5) abdBox.expandByPoint(v.fromBufferAttribute(pa, i).applyMatrix4(rel));
      });
    }
    const ac = abdBox.getCenter(new THREE.Vector3()), ah = abdBox.getSize(new THREE.Vector3()).multiplyScalar(0.5);
    const ar = Math.min(ah.x, ah.y) * 0.92;
    this._clearPts = [
      ...this.chelicerae.map((c) => ({ obj: c, local: lowest(c).add(new THREE.Vector3(0, 0.06, 0)), r: 0.07, part: 'front' })),
      { obj: this.body, local: new THREE.Vector3(0, -0.42, 0.05), r: 0.12, part: 'front' },
      ...[-0.5, 0, 0.5].map((k) => ({ obj: this.abdAnchor, local: ac.clone().add(new THREE.Vector3(0, 0, k * (ah.z - ar))), r: ar * (k === 0 ? 1 : 0.95), part: 'abd' })),
    ];
    this.clearPush = new THREE.Vector3();   // world-space push-out of the prosoma
    this.abdLift = 0;                       // dorsal rotation of the abdomen at the pedicel (rad)
  }

  setShellCount(n) { setShellCount(this.root, n); }

  // --------------------------------------------------------------------------------------- setup
  placeAt(point, normal, forward) {
    this.pos.copy(point);
    this.up.copy(normal).normalize();
    if (this.clearPush) { this.clearPush.set(0, 0, 0); this.abdLift = 0; for (const f of this.feet) { f.femurLift = 0; f.tarBias = 0; } }
    this.fwd.copy(forward).addScaledVector(this.up, -forward.dot(this.up)).normalize();
    this._updateRootTransform(0, true);
    for (const f of this.feet) {
      this._restWorld(f, f.rest);
      const hit = this._planFoothold(f, f.rest.clone());
      f.pos.copy(hit.p); f.normal.copy(hit.n); f.swinging = false; f.restT = 0;
    }
    this._initialised = true;
  }

  // rest foot position in the (unposed) locomotion frame
  _restLocal(f, out) {
    const limb = f.limb;
    const d = limb.planeDir(limb.yaw0, _v4);
    const reach = limb.isPalp ? 2.55 : limb.totalLen * limb.reachFrac + limb.L[0] * 0.7;
    return out.copy(limb.S).addScaledVector(d, reach).setY(-BODY.bodyHeight + (limb.isPalp ? 0.05 : 0));
  }
  _restWorld(f, out) {
    this._restLocal(f, out);
    return out.applyMatrix4(this.root.matrixWorld);
  }

  // Reach-aware foothold: if the surface point is beyond what the leg can take (downhill, off an
  // edge, far forward at speed) pull the candidate toward the body and search again.
  _foothold(f, candidate) {
    let hit = this._footholdRaw(f, candidate);
    const sock = _vS.copy(f.limb.S).applyMatrix4(this.root.matrixWorld);
    const max = f.limb.maxReach;
    if (hit.p.distanceTo(sock) <= max) return hit;
    const base = _vB.copy(sock).addScaledVector(this.up, -BODY.bodyHeight);
    const c0 = _vC.copy(candidate);
    for (const k of [0.82, 0.66, 0.5]) {
      const c = new THREE.Vector3().copy(base).lerp(c0, k);
      const h = this._footholdRaw(f, c);
      if (h.p.distanceTo(sock) <= max) return h;
      hit = h;
    }
    return hit;
  }

  // Foothold planner: try the full stride first, then progressively shorter steps; for each candidate
  // solve the leg and test the podomeres against the rock. Take the longest step whose pose is clear
  // and reachable (legs never cramp next to the hip, never reach through a ledge or boulder edge).
  _planFoothold(f, candidate) {
    if (f.isPalp) return this._foothold(f, candidate); // palps only tap the ground: simple foothold + collision steering
    const limb = f.limb, sock = _vP.copy(limb.S).applyMatrix4(this.root.matrixWorld);
    const inv = _mP.copy(this.body.matrixWorld).invert();
    this.body.getWorldQuaternion(_qP).invert();
    let best = null, bestCost = 1e9; this._planPen = 0;
    for (const k of [1, 0.86, 0.72, 0.58]) {
      const c = new THREE.Vector3().copy(this.pos).lerp(candidate, k);
      const h = this._footholdRaw(f, c);
      if (!h.ok) continue;
      const reach = h.p.distanceTo(sock) / limb.maxReach;
      if (reach > 1.02) continue;
      _tP.copy(h.p).applyMatrix4(inv); _nP.copy(h.n).applyQuaternion(_qP);
      limb.solve(_tP, _nP, f.altPose, 0, 0, 0);
      limb.apply(f.altPose);
      const pen = this._legPenetration(limb);
      const tipErr = limb.tip.distanceTo(_tP);
      const drop = Math.max(0, -_v4.copy(h.p).sub(this.pos).dot(this.up) - 0.8);
      const cost = pen * 10 + tipErr * 4 + (1 - k) * 1.5 + drop * 0.6 + Math.max(0, 0.42 - reach) * 6;
      if (cost < bestCost) { bestCost = cost; best = h; this._planPen = pen; }
      if (pen < 0.01 && tipErr < 0.06 && reach > 0.42) break; // longest clean step wins
    }
    return best || this._foothold(f, candidate);
  }

  // Find a foothold near `candidate` (world). Handles concave (wall ahead) and convex (edge) cases.
  _footholdRaw(f, candidate) {
    const w = this.world, up = this.up;
    const pad = f.isPalp ? 0.16 : f.limb.R[f.limb.n - 1] * 0.8;
    // Primary: the rock surface nearest to the ideal foot position. Works uniformly for open floor
    // (surface below), rising slopes / walls (candidate inside rock -> its surface), and drops (edge
    // face). Accept it only if the body can "see" it (not behind a thin fin of rock).
    {
      const c = w.closest(candidate, 2.6);
      if (c) {
        const eye = _v1.copy(this.pos).addScaledVector(up, 0.9);
        const tgt = _v3.copy(c.point).addScaledVector(c.normal, 0.08);
        const dir = _v2.copy(tgt).sub(eye); const len = dir.length();
        const block = len > 1e-4 ? w.raycast(eye, dir.divideScalar(len), len - 0.02) : null;
        if (!block) return { p: c.point.clone().addScaledVector(c.normal, pad), n: c.normal.clone(), ok: true };
      }
    }
    const center = _v1.copy(this.pos).addScaledVector(up, 0.9);
    // 1) wall ahead (concave transition): only a genuinely steep surface between body and candidate
    //    counts; gentle bumps in the floor must not capture the foot next to the body.
    const toC = _v2.copy(candidate).addScaledVector(up, 0.35).sub(center);
    const dist = toC.length();
    let hit = w.raycast(center, toC.divideScalar(dist), dist);
    if (hit && (hit.normal.dot(up) > 0.55 || hit.distance < dist * 0.45)) hit = null;
    if (!hit) {
      // 2) straight down onto the candidate - but only onto ground near the body's own plane.
      //    Past a ledge / boulder / mound rim this would plant the foot on the floor far below and
      //    drive the leg through the edge; pull the step back onto the current surface instead.
      const o = _v3.copy(candidate).addScaledVector(up, 2.2);
      hit = w.raycast(o, _v2.copy(up).negate(), 2.2 + 2.4);
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
    this.root.position.copy(this.pos).addScaledVector(this.up, BODY.bodyHeight + this.heightAdj).add(this.clearPush);
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

    // rest targets & error. When standing, the error is measured against the rest point projected
    // onto the terrain (the same place a settling step would land), otherwise on slopes the error
    // never clears and the animal shuffles in place forever.
    const moving = speedAbs > 0.15 || Math.abs(this.turnRate) > 0.08;
    let maxErr = 0, anySwing = false;
    for (const f of this.feet) {
      this._restWorld(f, f.rest);
      if (f.swinging) { anySwing = true; continue; }
      if (!moving) {
        if ((f.restT = (f.restT || 0) - dt) <= 0) { f.restP.copy(this._planFoothold(f, f.rest.clone()).p); f.restT = 0.4; }
        f.err = f.pos.distanceTo(f.restP);
        // a foot that is already comfortable (clear of rock, sensible reach) stays put at rest
        if (!f.isPalp && f.err < 1.6 && (f.disc || 0) < 0.05) {
          const r = f.pos.distanceTo(_vS.copy(f.limb.S).applyMatrix4(this.root.matrixWorld)) / f.limb.maxReach;
          if (r > 0.4 && r < 1.0) f.err = Math.min(f.err, 0.3);
        }
      }
      else f.err = f.pos.distanceTo(f.rest);
      if (!f.isPalp && !act.legLock(f)) maxErr = Math.max(maxErr, f.err);
    }
    // settle (reposition feet) only for a clearly bad stance, at a slow, deliberate cadence
    const settling = !moving && dPhase < dt * 0.25 && (anySwing || maxErr > 0.9);
    if (settling) dPhase = Math.max(dPhase, dt * 0.8);
    this.phase += dPhase;

    for (const f of this.feet) {
      const locked = act.legLock(f);
      const lp = ((this.phase - f.offset) % 1 + 1) % 1;
      const inSwing = lp < swingFrac;
      if (locked) { if (f.swinging) this._land(f); continue; }
      // emergency step: a planted foot that has become overstretched (fast turns, trailing leg IV,
      // terrain) steps now instead of waiting for its phase
      if (!f.swinging && !f.isPalp && !locked) {
        const sock = _vS.copy(f.limb.S).applyMatrix4(this.root.matrixWorld);
        const r = f.pos.distanceTo(sock) / f.limb.maxReach;
        f.jamCd = Math.max(0, (f.jamCd || 0) - dt);
        const jammed = f.jamCd <= 0 && ((f.disc || 0) > 0.22 || r < 0.3);
        const busy = this.feet.filter(o => o.emergency).length >= 2 ||
          this.feet.some(o => o !== f && o.swinging && !o.isPalp && o.limb.side === f.limb.side && Math.abs(o.limb.legIndex - f.limb.legIndex) === 1);
        if (r > 1.04 || (jammed && !busy)) {
          f.disc = 0; if (r <= 1.04) f.jamCd = 1.5;
          f.swinging = true; f.from.copy(f.pos); f.fromN.copy(f.normal); f.retarget = 0;
          f.lp0 = lp < swingFrac ? lp : 0; f.emergency = true;
          this._computeTarget(f, swingFrac, 0);
          if (r <= 1.04 && !((this._planPen < (f.curPen || 0) * 0.5 || (r < 0.3 && this._planPen < 0.05)) && f.to.distanceTo(f.pos) > 0.4)) {
            // re-planting would not help: stay, and do not keep retrying
            f.swinging = false; f.emergency = false; f.to.copy(f.pos); f.jamCd = 3;
            continue;
          }
          f.lift = clamp(0.35 + 0.22 * f.from.distanceTo(f.to), 0.35, 1.1);
          f.eT = 0;
        }
      }
      if (inSwing && !f.swinging && dPhase > 0 && lp < swingFrac * 0.4) {
        if (moving || f.err > 0.6) {
          f.swinging = true; f.from.copy(f.pos); f.fromN.copy(f.normal); f.retarget = 0; f.lp0 = lp;
          this._computeTarget(f, swingFrac, lp);
          f.lift = clamp(0.35 + 0.22 * f.from.distanceTo(f.to), 0.35, 1.1) * (f.isPalp ? 0.7 : 1);
        }
      }
      if (f.swinging) {
        if (f.emergency) {
          f.eT += dt; f.s = clamp(f.eT / 0.2, 0, 1);
          if (f.s >= 1) { f.emergency = false; this._land(f); continue; }
        } else {
          if (!inSwing) { this._land(f); continue; }
          f.s = clamp((lp - f.lp0) / Math.max(swingFrac - f.lp0, 1e-3), 0, 1);
        }
        // retarget during early swing (turning / uneven terrain)
        if (!f.emergency && f.s < 0.7 && (f.retarget++ % 3) === 0) this._computeTarget(f, swingFrac, lp);
        const e = 0.5 - 0.5 * Math.cos(Math.PI * f.s);
        const n = _v1.copy(f.fromN).lerp(f.toN, e).normalize();
        f.pos.copy(f.from).lerp(f.to, e).addScaledVector(n, f.lift * Math.pow(Math.sin(Math.PI * f.s), 0.9));
        f.normal.copy(n);
        const minH = (f.isPalp ? 0.16 : f.limb.R[f.limb.n - 1] * 0.8) + 0.25 * Math.sin(Math.PI * f.s);
        const g = this.world.raycast(_v3.copy(f.pos).addScaledVector(n, 1.6), _v4.copy(n).negate(), 1.6 + minH);
        if (g) { const above = 1.6 - g.distance; if (above < minH) f.pos.addScaledVector(n, minH - above); }
      }
    }
  }

  _land(f) {
    f.swinging = false; f.s = 0; f.emergency = false;
    f.pos.copy(f.to); f.normal.copy(f.toN);
  }

  _computeTarget(f, swingFrac, lp) {
    const { stride, duty } = this.gaitInfo;
    const dir = this.speed >= 0 ? 1 : -1;
    const cand = _v2.copy(f.rest);
    // time until this foot reaches mid-stance, using the same clock that drives the gait phase
    // (phase advances with translation AND turning), so the foot is centred in its stance
    const speedAbs = Math.abs(this.speed);
    const cycleT = stride / Math.max(speedAbs + Math.abs(this.turnRate) * 2.6, 0.5);
    const tAhead = cycleT * (duty * 0.5 + (1 - clamp(lp / swingFrac, 0, 1)) * swingFrac);
    cand.addScaledVector(this.fwd, dir * speedAbs * tAhead * (f.isPalp ? 0.25 : 1));
    // turning: rotate candidate about the body centre by the heading change until mid-stance
    const turnAhead = clamp(this.turnRate * tAhead, -0.6, 0.6);
    if (Math.abs(turnAhead) > 1e-4) {
      _q.setFromAxisAngle(this.up, turnAhead);
      cand.sub(this.pos).applyQuaternion(_q).add(this.pos);
    }
    const hit = this._planFoothold(f, cand.clone());
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

  // Keep chelicerae, sternum and abdomen out of the rock. Integrating controllers: push out at once on
  // contact, release slowly and only as far as the free clearance allows -> no in/out flicker.
  _clearance(dt) {
    const up = this.up, w = this.world, p = _v1;
    const push = 1;
    let frontFree = 1e9, abdFree = 1e9, abdPen = 0;
    const add = _v4.set(0, 0, 0);
    for (const c of this._clearPts) {
      p.copy(c.local); c.obj.localToWorld(p);
      const h = w.closest(p, c.r + 1.0);
      if (!h) continue;
      const free = h.distance - c.r - 0.03;
      if (c.part === 'front') {
        if (free < 0) add.addScaledVector(h.normal, -free);
        frontFree = Math.min(frontFree, free);
      } else {
        // surfaces below / behind the abdomen are cleared by pivoting it up at the pedicel
        if (free < 0 && h.normal.dot(up) > -0.3) abdPen = Math.max(abdPen, -free);
        // if pivoting cannot clear it (floor-wall corners, overhangs), push the whole body out
        if (free < 0 && (this.abdLift > 0.95 || h.normal.dot(up) <= -0.3)) add.addScaledVector(h.normal, -free * 0.6);
        abdFree = Math.min(abdFree, free);
      }
    }
    // prosoma
    if (add.lengthSq() > 0) this.clearPush.addScaledVector(add, push);
    else if (this.clearPush.lengthSq() > 0) {
      const l = this.clearPush.length();
      const nl = Math.max(0, l - Math.min(dt * 0.6, Math.max(frontFree, 0)));
      this.clearPush.multiplyScalar(nl / l);
    }
    if (this.clearPush.length() > 1.6) this.clearPush.setLength(1.6);
    // abdomen (lever ~2.2 cm from the pedicel)
    if (abdPen > 0) this.abdLift = Math.min(1.1, this.abdLift + (abdPen / 2.2) * push);
    else this.abdLift = Math.max(0, this.abdLift - Math.min(dt * 0.45, Math.max(abdFree, 0) / 2.2));
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
      limb.solve(tLocal, nLocal, f.pose, tarsusLift, f.femurLift, f.tarBias);
      const ov = act.override(f, dt);
      if (!(ov && ov.w > 0.5)) this._legCollision(f, tLocal, nLocal, tarsusLift, dt);
      if (ov && ov.w > 0) Limb.lerpPose(f.pose, ov.pose, ov.w, f.pose);
      limb.apply(f.pose);
    }
  }

  // Podomeres must not pass through rock. The leg chain is redundant (femur elevation and tarsus
  // flexion are free within joint limits), so when the current pose collides we evaluate a few
  // alternative configurations and steer smoothly toward the best one (penetration, reach, naturalness).
  _legPenetration(limb) {
    const mw = this.body.matrixWorld, J = limb.joints, R = limb.R;
    let pen = 0;
    const test = (p, r) => {
      p.applyMatrix4(mw);
      const h = this.world.closest(p, r + 0.6);
      if (h && h.distance < r) pen += r - h.distance;
    };
    test(_v1.copy(J[3]), R[3] * 1.05 + 0.02);
    test(_v1.copy(J[3]).lerp(J[4], 0.5), R[3] * 1.05 + 0.02);
    test(_v1.copy(J[4]).lerp(J[5], 0.5), R[4] * 1.05 + 0.02);
    test(_v1.copy(J[5]), R[4] * 1.05 + 0.02);
    test(_v1.copy(J[5]).lerp(J[6], 0.5), R[5] * 1.0 + 0.02);
    return pen;
  }

  _legCollision(f, tLocal, nLocal, tarsusLift, dt) {
    const limb = f.limb;
    if (this.noLegLift) { f.femurLift = 0; f.tarBias = 0; return; }
    limb.apply(f.pose);
    const tipErr = (pose) => limb.tip.distanceTo(tLocal);
    const pen0 = this._legPenetration(limb);
    let bestL = f.femurLift, bestT = f.tarBias;
    if (pen0 > 0.01) {
      let bestCost = pen0 * 12 + tipErr() * 4;
      const cands = [[0.2, 0], [0.4, 0], [-0.2, 0], [0, 0.35], [0, -0.3], [0.25, 0.35], [-0.2, 0.35], [0.4, 0.5]];
      for (const [dl, dtb] of cands) {
        const L = clamp(f.femurLift + dl, -0.5, 0.9), Tb = clamp(f.tarBias + dtb, -0.5, 0.8);
        limb.solve(tLocal, nLocal, f.altPose, tarsusLift, L, Tb);
        limb.apply(f.altPose);
        const cost = this._legPenetration(limb) * 12 + tipErr() * 4 + (Math.abs(L) + Math.abs(Tb)) * 0.15;
        if (cost < bestCost) { bestCost = cost; bestL = L; bestT = Tb; }
      }
      // move quickly toward the better configuration
      const k = 1 - Math.exp(-dt * 25);
      f.femurLift = lerp(f.femurLift, bestL, k); f.tarBias = lerp(f.tarBias, bestT, k);
    } else if (Math.abs(f.femurLift) + Math.abs(f.tarBias) > 1e-3) {
      // relax back toward the natural pose, slowly, and only if the relaxed pose is also clear
      const k = 1 - Math.exp(-dt * 1.5);
      const L = lerp(f.femurLift, 0, k), Tb = lerp(f.tarBias, 0, k);
      limb.solve(tLocal, nLocal, f.altPose, tarsusLift, L, Tb);
      limb.apply(f.altPose);
      if (this._legPenetration(limb) < 0.005) { f.femurLift = L; f.tarBias = Tb; }
    }
    limb.solve(tLocal, nLocal, f.pose, tarsusLift, f.femurLift, f.tarBias);
    // discomfort: a planted leg that stays jammed against rock gets re-planted (see _gait)
    if (!f.swinging) {
      let pen = pen0;
      if (pen0 > 0.01) { limb.apply(f.pose); pen = this._legPenetration(limb); } // re-test only when it mattered
      f.curPen = pen;
      f.disc = pen > 0.1 ? (f.disc || 0) + dt : Math.max(0, (f.disc || 0) - dt * 2);
    } else f.disc = 0;
  }

  _secondary(dt, act) {
    this._clearance(dt);
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
    this.abdPivot.rotation.set(-0.06 + a.pitch + this.abdLift, a.yaw, 0, 'YXZ');
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
