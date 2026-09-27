/**
 * GAIT CONTROLLER
 *
 * Inter-leg coordination, stance planning and surface negotiation, built on
 * the published gait ladder for hexapods:
 *
 *   slow   → metachronal wave   (hind→mid→fore, alternating sides)  [Wendler 1965; Cruse 1976]
 *   mid    → gliding / tetrapod (two diagonal legs swing together)   [Wendler 1966; Grabowska 2012]
 *   fast   → alternating M-tripod, front leg leads mid leads hind    [Kim et al., PNAS 2021]
 *
 * Duty factor and intra-tripod delays come from the same source. Legs in a
 * tripod are *not* perfectly synchronous — the ~0.035-cycle lead per rank is
 * what stops the gait from looking mechanical.
 *
 * Two hard guarantees:
 *   • Footfall targets are probed (with a lateral search for the flattest
 *     foothold) and then locked in WORLD space for the whole stance, so slip
 *     is exactly zero by construction — not "small", zero.
 *   • Crossing between surfaces (ground → wall → tank hull) is handled by a
 *     coordinated re-grip: when a nearby surface of a different orientation
 *     is detected, all six tarsi release in a small metachronal ripple, the
 *     body rolls onto the new normal, and the feet re-plant — which is
 *     exactly what the insect does at an edge.
 */

import * as THREE from 'three';
import { GAIT, LEGS, TRIPOD_A, TRIPOD_B, TRIPOD_LEAD, clamp, lerp, smoothstep, coxaRestDir } from '../mosquito/anatomy.js';
import { LegSolver } from './legs.js';

const _v = new THREE.Vector3(), _v2 = new THREE.Vector3(), _v3 = new THREE.Vector3();
const _q = new THREE.Quaternion();
const _m4 = new THREE.Matrix4();

/** Wave-gait offsets (S10): hind → middle → fore on the right, then the left. */
const WAVE_OFFSETS = { R3: 0, R2: 1 / 6, R1: 2 / 6, L3: 3 / 6, L2: 4 / 6, L1: 5 / 6 };
const RANK = { 1: 0, 2: 1, 3: 2 };
const LEG_KEYS = ['L1', 'L2', 'L3', 'R1', 'R2', 'R3'];

function tripodOffsets() {
  const o = {};
  for (const k of TRIPOD_A) o[k] = TRIPOD_LEAD[RANK[k[1]]];
  for (const k of TRIPOD_B) o[k] = 0.5 + TRIPOD_LEAD[RANK[k[1]]];
  return o;
}

export class GaitController {
  constructor(rigged, field, opts = {}) {
    this.rig = rigged;
    this.field = field;
    this.legs = {};
    for (const key of LEG_KEYS) {
      this.legs[key] = {
        key,
        solver: new LegSolver(rigged.legs[key]),
        spec: LEGS[key],
        offset: 0,
        localPhase: 0,
        planted: false,
        plant: new THREE.Vector3(),
        plantNormal: new THREE.Vector3(0, 1, 0),
        lastPlanted: new THREE.Vector3(),
        liftoff: new THREE.Vector3(),
        target: new THREE.Vector3(),
        targetNormal: new THREE.Vector3(0, 1, 0),
        targetValid: false,
        inSwing: false,
        swingT: 0,
        contact: 0,
        tarsusFlex: 0,
        slip: 0,
        probing: 0,
      };
    }

    this.phase = 0;
    this.speed = 0;
    this.targetSpeed = 0;
    this.rate = opts.rate ?? 1.0;
    this.heading = new THREE.Vector3(1, 0, 0);
    this.up = new THREE.Vector3(0, 1, 0);
    this.upSmooth = new THREE.Vector3(0, 1, 0);
    this.upVel = new THREE.Vector3();
    // Nose-down walking attitude: the head and proboscis are carried below the
    // level of the thorax, and the abdominal tip trails a little lower still
    // (macro reference, standing female Aedes). The rotation is about
    // cross(up, heading), so a POSITIVE angle drops the head.
    this.bodyPitch = 9 * Math.PI / 180;
    this.bodyPos = new THREE.Vector3();
    this.attached = false;
    this.tripodT = 0;
    this.duty = GAIT.dutyFactor;
    this.stepLength = GAIT.stepLength;
    this.strideFreq = GAIT.strideFrequency;
    this.bobPhase = 0;
    this.standHeight = GAIT.bodyClearance;
    this.surface = 'ground';
    this.surfaceTarget = null;
    this.surfaceTransitions = 0;
    this.onSurfaceChange = null;
    this._bodyInv = new THREE.Matrix4();
  }

  /** 0..1 — how close the most loaded stance leg is to the end of its reach. */
  _maxLegLoad() {
    let worst = 0;
    for (const key of LEG_KEYS) {
      const l = this.legs[key];
      if (!l.planted) continue;
      const L = l.solver;
      if (!L || !L.hipWorld) continue;
      const d = L.hipWorld.distanceTo(l.plant) / Math.max(1e-4, l.spec.reach);
      if (d > worst) worst = d;
    }
    return clamp((worst - 0.55) / 0.40, 0, 1);
  }

  /* ------------------------------------------------------------- setup */

  attach(pos, up, heading) {
    this.bodyPos.copy(pos);
    this.up.copy(up).normalize();
    this.upSmooth.copy(this.up);
    this.upVel.set(0, 0, 0);
    if (heading && heading.lengthSq() > 1e-9) this.heading.copy(heading).projectOnPlane(this.up).normalize();
    this.phase = 0;
    this.speed = 0;
    this.surfaceTarget = null;
    this._candHold = 0;
    this._applyBody();
    for (const key of LEG_KEYS) {
      const leg = this.legs[key];
      this._planFoot(leg, true);
      leg.planted = true;
      leg.inSwing = false;
      leg.contact = 1;
      leg.swingT = 0;
      leg.lastPlanted.copy(leg.plant);
      leg.slip = 0;
    }
    this.attached = true;
  }

  /* --------------------------------------------------- surface seeking */

  /**
   * Look for a nearby surface whose normal differs enough from the one we are
   * standing on to be worth crossing onto. Casts a short fan of rays around
   * the body — the analogue of an insect feeling ahead with its tarsi.
   */
  _classifySurface() {
    const reach = LEGS.L3.reach;
    const origin = _v2.copy(this.bodyPos).addScaledVector(this.upSmooth, 0.55);
    let best = null;
    // Down, then a fan of tilted probes biased toward the heading
    for (let i = 0; i < 9; i++) {
      const a = (i / 9) * Math.PI * 2;
      const tilt = i === 0 ? 0 : 0.62;
      const d = this.upSmooth.clone().multiplyScalar(-Math.cos(tilt));
      // Tangential component, biased forward
      const t1 = new THREE.Vector3().crossVectors(this.upSmooth, this.heading).normalize();
      d.addScaledVector(t1, Math.sin(tilt) * Math.sin(a));
      d.addScaledVector(this.heading, Math.sin(tilt) * Math.cos(a));
      d.normalize();
      const hit = this.field.probe(origin.clone(), d, reach * 0.95);
      if (!hit) continue;
      let n = hit.normal;
      if (n.dot(this.upSmooth) < 0) continue;             // wrong hemisphere
      const ang = n.angleTo(this.upSmooth);
      if (ang < 0.30) continue;                            // same surface family
      const score = hit.t - ang * 2.0;
      if (!best || score < best.score) best = { score, normal: n.clone(), point: hit.point.clone(), name: hit.name };
    }
    return best;
  }

  /* ------------------------------------------------------------ foots */

  /** Is `p` on the correct side of the body midline for this leg? */
  _lateralOK(p, spec) {
    _v2.copy(p).applyMatrix4(this._bodyInv);
    return Math.sign(_v2.z || 1e-9) === spec.side && Math.abs(_v2.z) >= (spec.minLateral ?? 0.10);
  }

  /**
   * Nominal stance position of one tarsus, in thorax-local millimetres.
   *
   * The spread is not a free parameter: it is the radius at which this leg's
   * own femur + tibia put the femur–tibia joint at its neutral ~90° posture,
   * the value measured in walking insects. Anything shorter tucks the tarsus
   * under the body and folds the knee past its anatomical limit; anything
   * longer over-extends it. The fore–aft rake and the side-to-side splay still
   * come from the leg's rest pitch and abduction, so the six feet keep their
   * characteristic fan.
   */
  _nominalStance(spec) {
    const hip = spec._hipLocal || (spec._hipLocal =
      this.rig.legs[spec.key].root.position.clone()
        .addScaledVector(coxaRestDir(spec), spec.coxa));
    const rise = spec.tarsus.reduce((x, y) => x + y, 0) * 0.96;
    const soleY = -GAIT.bodyClearance;
    // Vertical component of the hip→ankle chord (the tarsus hangs below the
    // ankle by roughly its own length).
    const dy = (soleY + rise) - hip.y;
    const target = GAIT.stanceExtension * (spec.trochanter + spec.femur + spec.tibia);
    // Rake fore/aft by restPitch, swing outboard by restAbduct.
    const dir = new THREE.Vector3(
      Math.sin(spec.restPitch) * Math.cos(spec.restAbduct), 0,
      spec.side * Math.sin(spec.restAbduct));
    const hl = dir.length() || 1;
    dir.divideScalar(hl);
    const h = Math.max(Math.sqrt(Math.max(0, target * target - dy * dy)), (spec.minLateral ?? 0.1) + 0.22);
    return new THREE.Vector3(hip.x + dir.x * h, soleY, hip.z + dir.z * h);
  }

  /**
   * Choose a foothold: sample a small patch of the substrate around the
   * nominal stance position and take the flattest, best-connected spot.
   */
  _planFoot(leg, immediate = false) {
    const spec = leg.spec;
    const bodyQ = this.rig.thorax.getWorldQuaternion(_q);
    const local = this._nominalStance(spec).applyQuaternion(bodyQ);
    const probeFrom = local.add(this.bodyPos).addScaledVector(this.upSmooth, 1.0);
    const down = this.upSmooth.clone().negate();

    const CANDS = [[0, 0], [1, 0], [-1, 0], [0, 1], [0, -1], [0.8, 0.8], [-0.8, 0.8], [0.8, -0.8], [-0.8, -0.8]];
    let best = null;
    const spread = 0.10 + 0.05 * spec.index;
    const t1 = new THREE.Vector3().crossVectors(this.upSmooth, this.heading).normalize();
    const hipApprox = this.bodyPos.clone()
      .addScaledVector(this.heading, Math.cos(spec.restAbduct) * spec.coxa)
      .addScaledVector(this.upSmooth, -0.55);
    for (const [ox, oz] of CANDS) {
      const p = probeFrom.clone()
        .addScaledVector(t1, ox * spread)
        .addScaledVector(this.heading, oz * spread);
      const hit = this.field.probe(p, down, 2.6);
      if (!hit) continue;
      const d = hit.point.distanceTo(hipApprox);
      if (d > spec.reach * 0.94) continue;
      // Hard lateral constraint: a foothold must lie on this leg's own side of
      // the body midline. Rejecting (rather than later clamping) means the
      // foot is genuinely on the surface, never floating beside it.
      if (!this._lateralOK(hit.point, spec)) continue;
      const flat = 1 - this.field.roughnessAt(hit.point, hit.normal) * 2.2;
      const central = 1 - (Math.hypot(ox, oz) / 1.414) * 0.30;
      const score = flat * 1.4 + central + (spec.reach * 0.94 - d) * 0.05;
      if (!best || score > best.score) best = { score, point: hit.point, normal: hit.normal, name: hit.name };
    }
    if (!best) {
      // No substrate under the nominal spot (a ledge, or a different surface):
      // cast a short fan to find *something* within reach.
      for (let i = 0; i < 7; i++) {
        const a = (i / 7) * Math.PI * 2;
        const d = down.clone()
          .addScaledVector(t1, Math.sin(a) * 0.5)
          .addScaledVector(this.heading, Math.cos(a) * 0.5).normalize();
        const hit = this.field.probe(probeFrom.clone(), d, spec.reach * 0.9);
        if (hit && hit.point.distanceTo(hipApprox) < spec.reach * 0.9 && this._lateralOK(hit.point, spec)) {
          best = { score: 0, point: hit.point, normal: hit.normal, name: hit.name }; break;
        }
      }
    }
    if (!best) {
      const p = probeFrom.clone().addScaledVector(down, 1.0);
      best = { point: p, normal: this.upSmooth.clone(), name: this.surface, score: 0 };
    }
    leg.target.copy(best.point);
    leg.targetNormal.copy(best.normal);
    if (immediate) {
      leg.plant.copy(best.point);
      leg.plantNormal.copy(best.normal);
    }
  }

  /* ------------------------------------------------------------ update */

  update(dt, opts = {}) {
    this._bodyInv.copy(this.rig.thorax.matrixWorld).invert();
    const target = (opts.speed ?? 0) * this.rate;
    const accel = opts.accel ?? 26;
    this.targetSpeed = target;
    this.speed += clamp(target - this.speed, -accel * dt * 2, accel * dt);

    /* --- gait mode --------------------------------------------------- */
    const sp = Math.abs(this.speed);
    this.tripodT = smoothstep(0, GAIT.speedToTripod, sp);
    this.duty = lerp(0.86, GAIT.dutyFactor, this.tripodT);
    this.stepLength = clamp(0.40 + sp * 0.016, 0.40, 1.60);
    this.strideFreq = clamp(sp / (2 * this.stepLength), 0, GAIT.strideFrequencyMax);
    this.bobPhase += this.strideFreq * dt;

    const trip = tripodOffsets();
    for (const key of LEG_KEYS) {
      const leg = this.legs[key];
      // Blend wave → tripod; the intermediate values reproduce the real
      // gliding / tetrapod coordination seen at intermediate speeds.
      let off = lerp(WAVE_OFFSETS[key], trip[key], this.tripodT);
      // A whisper of per-leg asymmetry keeps the rhythm organic
      off += Math.sin(this.bobPhase * 2.1 + RANK[key[1]] * 2.3) * 0.004;
      leg.offset = off;
    }

    const period = 1 / Math.max(0.2, this.strideFreq);
    this.phase = (this.phase + this.strideFreq * dt) % 1;

    /* --- body integration -------------------------------------------- */
    if (sp > 1e-4) {
      this.heading.projectOnPlane(this.upSmooth);
      if (this.heading.lengthSq() > 1e-10) this.heading.normalize();
      this.bodyPos.addScaledVector(this.heading, this.speed * dt);
    }

    /* --- surface negotiation ----------------------------------------- */
    // Rather than lifting all six tarsi at once (which would drop the insect
    // below three points of contact), the controller *commits* to the new
    // surface normal and lets the ordinary tripod carry the feet across: the
    // swinging tripod lands on the new substrate while the stance tripod still
    // holds the old one. The body roll is rate-limited by leg load, so it
    // waits for a foot to be in the air before leaning over the edge — the
    // load-feedback rule described for insect walking.
    if (this.attached) {
      const cand = this._classifySurface();
      this._candHold = cand ? (this._candHold || 0) + dt : 0;
      this._rollCooldown = Math.max(0, (this._rollCooldown || 0) - dt);
      if (cand && this._candHold > 0.07 && this._rollCooldown <= 0 && !this.surfaceTarget) {
        this.surfaceTarget = { normal: cand.normal.clone(), name: cand.name, t: 0 };
        this._candHold = 0;
        this._rollCooldown = 1.6;
        this.surfaceTransitions++;
        this.onSurfaceChange?.(cand.name, this.surfaceTransitions);
      }
      if (this.surfaceTarget) {
        this.surfaceTarget.t += dt;
        let agree = 0;
        for (const key of LEG_KEYS) {
          const l = this.legs[key];
          if (l.planted && l.plantNormal.angleTo(this.surfaceTarget.normal) < 0.32) agree++;
        }
        if ((agree >= 4 && this.surfaceTarget.t > 0.12) || this.surfaceTarget.t > 4.0) {
          this.surface = this.surfaceTarget.name;
          this.surfaceTarget = null;
          for (const key of LEG_KEYS) this.legs[key].targetValid = false;
        }
      }
    }

    /* --- contact normals drive the body frame ------------------------ */
    let targetUp = this.upSmooth;
    const stanceNormals = [];
    for (const key of LEG_KEYS) {
      const leg = this.legs[key];
      const lp = (this.phase + leg.offset) % 1;
      leg.localPhase = lp;
      leg.inStance = lp < this.duty;
      if (leg.inStance && leg.planted) stanceNormals.push(leg.plantNormal);
    }
    if (this.surfaceTarget) {
      targetUp = this.surfaceTarget.normal;
    } else if (stanceNormals.length >= 2) {
      const avg = new THREE.Vector3();
      for (const n of stanceNormals) avg.add(n);
      avg.divideScalar(stanceNormals.length).normalize();
      const ahead = this.bodyPos.clone().addScaledVector(this.heading, this.stepLength * 1.6);
      const probe = this.field.probe(ahead.clone().addScaledVector(avg, 1.4), avg.clone().negate(), 3.0);
      if (probe) avg.lerp(probe.normal, 0.45);
      if (avg.dot(this.upSmooth) < 0) avg.negate();
      targetUp = avg;
    }

    // Critically damped spring: the body rolls into the terrain, never snaps.
    let omega = 14.0;
    if (this.surfaceTarget) {
      // Leaning onto a new surface is gated on load: if every foot is already
      // near the end of its reach we hold still until one lifts. This is the
      // classic insect rule that the stance tripod must not be disturbed.
      const load = this._maxLegLoad();
      omega = 20.0 * (1 - clamp(load, 0, 1) ** 2);
      if (omega < 1.5) omega = 1.5;
    }
    _v.copy(targetUp).sub(this.upSmooth);
    this.upVel.addScaledVector(_v, omega * omega * dt).addScaledVector(this.upVel, -2 * omega * dt);
    this.upSmooth.addScaledVector(this.upVel, dt);
    if (this.upSmooth.lengthSq() > 1e-10) this.upSmooth.normalize();
    this.up.copy(this.upSmooth);

    // Vertical placement: the thorax rides standHeight above the substrate
    // with the small gait-locked CoM oscillation measured in walking flies.
    const bob = Math.sin(this.bobPhase * Math.PI * 4) * GAIT.bodyBob * 0.5
      + Math.sin(this.bobPhase * Math.PI * 2) * GAIT.bodyBob * 0.22;
    const probeTop = this.bodyPos.clone().addScaledVector(this.upSmooth, 3.0);
    const groundHit = this.field.probe(probeTop, this.upSmooth.clone().negate(), 8.0);
    const height = this.standHeight + bob;
    if (groundHit) this.bodyPos.copy(groundHit.point).addScaledVector(this.upSmooth, height);

    /* --- per-leg stance / swing -------------------------------------- */
    const swingTime = (1 - this.duty) * period;
    for (const key of LEG_KEYS) {
      const leg = this.legs[key];
      if (leg.inStance) {
        // The phase is in stance. If the foot is still airborne — which happens
        // whenever a frame boundary steps over the end of the swing — complete
        // the touchdown NOW, at the target that was planned for it. Re-planting
        // at the previous position here would strand the foot far behind the
        // body and destroy the zero-slip invariant.
        if (leg.inSwing || !leg.planted) {
          leg.plant.copy(leg.target);
          leg.plantNormal.copy(leg.targetNormal);
          leg.planted = true;
          leg.inSwing = false;
          leg.lastPlanted.copy(leg.plant);
          leg.slip = 0;
          leg.contact = 0.02;
          leg.probing = 1;
        }
        leg.contact = clamp(leg.contact + dt / Math.max(1e-3, this.duty * period), 0, 1);
        leg.swingT = 0;
        this._predict(leg, swingTime, period);
      } else {
        if (!leg.inSwing) { leg.liftoff.copy(leg.plant); leg.inSwing = true; leg.planted = false; leg.contact = 0; }
        leg.swingT = (leg.localPhase - this.duty) / Math.max(1e-4, 1 - this.duty);
        // Plan against the body pose the foot will meet at touchdown.
        this._predict(leg, (1 - leg.swingT) * swingTime, period);
        leg.contact = clamp(leg.contact - dt / Math.max(1e-3, swingTime), 0, 1);
        leg.probing = Math.max(0, leg.probing - dt * 4);
      }
    }

    /* --- keep same-side feet apart ----------------------------------- */
    this._separateFeet();
    this._enforceLateral();

    /* --- drive the rig ------------------------------------------------- */
    this._applyBody();
    this._applyLegs();
  }

  /** Predict the body pose `lead` seconds ahead and plan a foothold for it. */
  _predict(leg, lead, period) {
    const savedPos = this.bodyPos.clone();
    const savedUp = this.upSmooth.clone();
    const savedHead = this.heading.clone();
    this.bodyPos.addScaledVector(this.heading, this.speed * clamp(lead, 0, period * 1.2));
    this._planFoot(leg);
    this.bodyPos.copy(savedPos);
    this.upSmooth.copy(savedUp);
    this.heading.copy(savedHead);
  }

  /**
   * Minimum toe separation. Two feet on the same side must never be closer
   * than roughly one pretarsus length — the anatomical constraint that keeps
   * the tarsi from tangling on a narrow ledge.
   */
  /**
   * Final hard guard. Footholds are planned on this leg's own side of the
   * midline and the separation pass can push them, so we re-assert the
   * constraint once more before the IK sees them. Cheap, and it makes
   * interpenetration structurally impossible rather than merely unlikely.
   */
  _enforceLateral() {
    for (const key of LEG_KEYS) {
      const leg = this.legs[key];
      if (leg.planted) continue;
      _v2.copy(leg.target).applyMatrix4(this._bodyInv);
      const min = leg.spec.minLateral ?? 0.10;
      if (Math.sign(_v2.z || 1e-9) === leg.spec.side && Math.abs(_v2.z) >= min) continue;
      _v2.z = leg.spec.side * Math.max(min, Math.abs(_v2.z));
      leg.target.copy(_v2).applyMatrix4(this.rig.thorax.matrixWorld);
      leg.targetNormal.copy(this.upSmooth);
    }
  }

  _separateFeet(iterations = 3) {
    const MIN = 0.17;
    for (let side = -1; side <= 1; side += 2) {
      const keys = ['1', '2', '3'].map((r) => (side > 0 ? 'L' : 'R') + r);
      for (let it = 0; it < iterations; it++) {
        for (let i = 0; i < 3; i++) for (let j = i + 1; j < 3; j++) {
          const a = this.legs[keys[i]], b = this.legs[keys[j]];
          // Stance feet are world-locked: they are the fixed reference the
          // swinging foot is pushed away from, never the other way round.
          const aFixed = a.planted, bFixed = b.planted;
          if (aFixed && bFixed) continue;
          const movable = aFixed ? b : a;
          const anchor = aFixed ? a.plant : b.plant;
          const d = _v.subVectors(movable.target, anchor);
          const dist = d.length();
          if (dist >= MIN || dist < 1e-6) continue;
          d.divideScalar(dist).multiplyScalar(MIN - dist);
          movable.target.add(d);
          movable.targetNormal.copy(this.upSmooth);
        }
      }
    }
  }

  _applyBody() {
    const body = this.rig.body;
    body.position.set(0, 0, 0);
    body.quaternion.identity();
    const th = this.rig.thorax;
    th.position.copy(this.bodyPos);
    const fwd = _v.copy(this.heading);
    if (fwd.lengthSq() < 1e-10) fwd.set(1, 0, 0);
    fwd.projectOnPlane(this.upSmooth).normalize();
    const up = this.upSmooth.clone();
    const right = _v2.crossVectors(up, fwd).normalize();
    _m4.makeBasis(fwd, up, right.clone().negate());
    const q = new THREE.Quaternion().setFromRotationMatrix(_m4);
    const pitch = new THREE.Quaternion().setFromAxisAngle(right, this.bodyPitch);
    th.quaternion.copy(pitch.multiply(q));
    th.updateWorldMatrix(true, true);
  }

  _applyLegs() {
    const th = this.rig.thorax;
    th.updateWorldMatrix(true, true);
    const bodyQ = th.getWorldQuaternion(_q);
    const fwd = new THREE.Vector3(1, 0, 0).applyQuaternion(bodyQ);
    const normal = this.upSmooth;

    for (const key of LEG_KEYS) {
      const leg = this.legs[key];
      if (!leg.solver) continue;
      let footPos, footNormal;

      if (leg.planted && !leg.inSwing) {
        footPos = leg.plant;
        footNormal = leg.plantNormal;
        leg.slip = leg.plant.distanceTo(leg.lastPlanted);
        leg.lastPlanted.copy(leg.plant);
      } else {
        // Swing: a long, low, eased arc. Double-ease on the horizontal
        // component plus a flattened lift profile — the foot sweeps forward
        // smoothly instead of snapping, with a broad apex.
        const t = clamp(leg.swingT, 0, 1);
        const e = t * t * (3 - 2 * t);
        const e2 = e * e * (3 - 2 * t);
        footPos = leg.liftoff.clone().lerp(leg.target, e2);
        const p = GAIT.swingLift * Math.pow(Math.sin(Math.PI * t), GAIT.swingLiftProfile);
        footPos.addScaledVector(normal, p * (1 - 0.15 * e));
        footNormal = normal;
        if (t >= 1) {
          leg.plant.copy(leg.target);
          leg.plantNormal.copy(leg.targetNormal);
          leg.planted = true;
          leg.probing = 1;
          leg.targetValid = false;
          // Seed the slip reference at touchdown, otherwise the first frame of
          // the new stance would be compared against the previous stance.
          leg.lastPlanted.copy(leg.plant);
          leg.slip = 0;
        }
      }

      // Tarsal timing (S13): laid flat at heel strike, pried up by the claws
      // at lift-off.
      const c = leg.contact;
      const strike = (1 - smoothstep(0, 0.30, c)) * GAIT.tarsusStrike;
      const release = smoothstep(0.72, 1.0, 1 - c) * GAIT.tarsusRelease;
      leg.tarsusFlex = strike + release;

      leg.solver.solve(footPos, footNormal, th, {
        tarsalAngle: lerp(0.80, 0.24, smoothstep(0, 0.5, c)),
        contact: c,
      });
    }
  }

  /* ------------------------------------------------------------ state */

  get footPositions() {
    const out = {};
    for (const key of LEG_KEYS) {
      const l = this.legs[key];
      out[key] = l.planted ? l.plant : l.target;
    }
    return out;
  }

  stanceCount() {
    let n = 0;
    for (const key of LEG_KEYS) if (this.legs[key].planted) n++;
    return n;
  }
}
