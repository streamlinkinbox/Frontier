/**
 * LEG SOLVER
 *
 * Analytic inverse kinematics for an insect leg: a two-link femur–tibia solved
 * in the plane defined by the hip, the target and a pole vector, followed by a
 * constant-curvature chain solve for the five tarsomeres.
 *
 * Two properties matter more than anything else here:
 *
 *   1. NO FOOT SLIP. During stance the target is a world-space point that does
 *      not change, so the solved foot position is exact to floating point.
 *   2. NO LEG CROSS-INS. The coxa orientation is built from the outward
 *      (abduction) reference, not from the raw target direction, so the knees
 *      stay outboard of the midline and neighbouring tarsi can never cross —
 *      even at full extension or on a strongly curved surface.
 *
 * Bone convention: each joint's local +Y points along the segment it drives.
 */

import * as THREE from 'three';
import { GAIT, clamp } from '../mosquito/anatomy.js';

const _v1 = new THREE.Vector3(), _v2 = new THREE.Vector3(), _v3 = new THREE.Vector3();
const _q1 = new THREE.Quaternion(), _q2 = new THREE.Quaternion();
const _m1 = new THREE.Matrix4();
const Y = new THREE.Vector3(0, 1, 0);

/** Set a node's quaternion from a desired world-space quaternion. */
function setWorldQuat(node, worldQuat) {
  const pq = _q1;
  node.parent.getWorldQuaternion(pq);
  node.quaternion.copy(pq.invert()).multiply(worldQuat);
  _q1.copy(pq);
}

/** World quaternion whose +Y aligns with `dir`. */
function alignY(dir, out = new THREE.Quaternion()) {
  return out.setFromUnitVectors(Y, _v1.copy(dir).normalize());
}

/**
 * Constant-curvature chain solve.
 * Places `n` links of length `L` so the chain starts at `start`, ends at
 * `end`, and bulges toward `bulge`. Used for the tarsus so the foot is always
 * exactly on the surface while the ankle is above it.
 */
export function solveChain(start, end, linkLens, n, bulge, out) {
  const chord = _v1.subVectors(end, start);
  const d = chord.length();
  // Real tarsomere lengths, shortest-last. The node offsets in the rig use
  // exactly these, so the analytic chain must too — otherwise the solved
  // joints and the rendered joints drift apart.
  const lens = new Float64Array(n);
  let S = 0;
  for (let i = 0; i < n; i++) { lens[i] = linkLens[i]; S += lens[i]; }
  if (out) out.length = 0;
  if (d < 1e-6) { for (let i = 0; i <= n; i++) out?.push(start.clone()); return out; }

  const dir = _v2.copy(chord).divideScalar(d);
  if (d >= S * 0.999) {
    let c = start.clone();
    out?.push(c.clone());
    for (let i = 0; i < n; i++) { c = c.clone().addScaledVector(dir, lens[i]); out?.push(c.clone()); }
    return out;
  }
  // Bisect the total turn θ for this arc length and chord.
  let lo = 1e-5, hi = (2 * Math.PI * (n - 1)) / n * 0.999;
  const chordOf = (th) => 2 * (S / th) * Math.sin(th / 2);
  for (let k = 0; k < 48; k++) {
    const mid = (lo + hi) * 0.5;
    if (chordOf(mid) > d) lo = mid; else hi = mid;
  }
  const theta = (lo + hi) * 0.5;
  // Arc plane: bulge direction projected perpendicular to the chord
  const b = _v3.copy(bulge).addScaledVector(dir, -bulge.dot(dir));
  if (b.lengthSq() < 1e-8) b.set(0, 1, 0).addScaledVector(dir, -dir.y);
  b.normalize();

  // Each link's direction is the chord direction rotated by (i + 0.5 - n/2)*θ/n
  // toward the bulge.
  const per = theta / n;
  let cursor = start.clone();
  if (out) out.push(cursor.clone());
  for (let i = 0; i < n; i++) {
    const ang = (i + 0.5 - n / 2) * per;
    const d2 = dir.clone().multiplyScalar(Math.cos(ang)).addScaledVector(b, Math.sin(ang));
    const next = cursor.clone().addScaledVector(d2, lens[i]);
    if (out) out.push(next.clone());
    cursor = next;
  }
  return out;
}

/** Analytic two-link IK. Returns the world direction of each link. */
export function solveTwoBone(hip, target, a, b, poleDir) {
  const to = _v1.subVectors(target, hip);
  const raw = to.length();
  const dMin = Math.abs(a - b) + 1e-3;
  const dMax = a + b - 1e-3;
  const dc = clamp(raw, dMin, dMax);
  const dir = to.clone().normalize();

  // Bend plane: the component of the pole direction perpendicular to the
  // hip→target chord. For an insect leg the pole is the body up, so the knee
  // apex always sits above the hip→foot line — the high arch of a mosquito
  // standing on its legs, and the same arch on a wall when the body's up is
  // the wall normal.
  const perp = poleDir.clone().addScaledVector(dir, -poleDir.dot(dir));
  if (perp.lengthSq() < 1e-8) {
    perp.set(0, 0, 1).addScaledVector(dir, -dir.z);
    if (perp.lengthSq() < 1e-8) perp.set(1, 0, 0).addScaledVector(dir, -dir.x);
  }
  perp.normalize();

  // Interior angle at the hip, so that |hip→knee| = a and |knee→target| = b.
  const cosA = clamp((a * a + dc * dc - b * b) / (2 * a * dc), -1, 1);
  const alpha = Math.acos(cosA);
  const femurDir = dir.clone().multiplyScalar(Math.cos(alpha))
    .addScaledVector(perp, Math.sin(alpha)).normalize();
  const knee = hip.clone().addScaledVector(femurDir, a);
  const tibiaDir = target.clone().sub(knee).normalize();
  // Knee bend away from straight (0 = fully extended, π = fully folded).
  const flex = Math.acos(clamp(femurDir.dot(tibiaDir), -1, 1));
  return { femurDir, tibiaDir, knee, reached: target, clamped: Math.abs(raw - dc) > 1e-4, flex, reach: raw };
}

/* ============================================================== */

export class LegSolver {
  /**
   * @param {object} leg  entry from buildMosquito().legs
   * @param {object} opts { abdomenRef }
   */
  constructor(leg, opts = {}) {
    this.leg = leg;
    this.spec = leg.spec;
    this.side = leg.spec.side;
    this.femurLen = leg.spec.femur + leg.spec.trochanter;
    this.tibiaLen = leg.spec.tibia;
    this.tarsusLen = leg.spec.tarsus.reduce((a, b) => a + b, 0);
    this.tarsusN = leg.spec.tarsus.length;

    // Rest orientation of the coxa: outward abduction + the pitch of this leg
    // pair. The abduction is what guarantees feet never cross the midline.
    this.abduct = leg.spec.restAbduct;
    this.pitch = leg.spec.restPitch;
    this._chain = [];
    this._footWorld = new THREE.Vector3();
    this._hipWorld = new THREE.Vector3();
    this.flexion = 0;
  }

  /**
   * Place the leg.
   * @param {THREE.Vector3} foot  world-space contact point (sole of the pretarsus)
   * @param {THREE.Vector3} normal surface normal at the contact
   * @param {THREE.Object3D} body  the body node (for forward / lateral references)
   * @param {object} style { tarsalAngle, reachScale, contact }
   */
  solve(foot, normal, body, style = {}) {
    const leg = this.leg;

    // --- body frame references ---------------------------------------
    const fwd = _v1.set(1, 0, 0).applyQuaternion(body.getWorldQuaternion(_q1)).normalize().clone();
    const up = _v2.set(0, 1, 0).applyQuaternion(body.getWorldQuaternion(_q2)).normalize().clone();
    const lat = new THREE.Vector3().crossVectors(up, fwd).normalize().multiplyScalar(this.side);

    // --- structural anti-crossing guard --------------------------------
    // A foot is never allowed onto the far side of the body midline, however
    // extreme the terrain or the body attitude. This is a hard constraint, not
    // a tuning: it is the reason the six tarsi can never intertwine.
    const bodyInv = _m1.copy(body.matrixWorld).invert();
    const local = _v3.copy(foot).applyMatrix4(bodyInv);
    const minZ = this.spec.minLateral ?? 0.10;
    if (Math.sign(local.z || 1e-9) !== this.side || Math.abs(local.z) < minZ) {
      local.z = this.side * Math.max(minZ, Math.abs(local.z));
      // Clone: the caller owns this vector and it is world-locked during stance.
      foot = new THREE.Vector3().copy(local).applyMatrix4(body.matrixWorld);
    }

    // --- coxa aim -----------------------------------------------------
    // Point the coxa outboard by the pair's abduction angle, then bias toward
    // the target so long reaches stay solvable without the knee flipping.
    const coxaAim = fwd.clone().multiplyScalar(Math.cos(this.abduct))
      .addScaledVector(lat, Math.sin(this.abduct))
      .addScaledVector(up, -Math.abs(Math.sin(this.pitch)) * 0.75 - 0.55)
      .addScaledVector(fwd, -Math.sin(this.pitch) * 0.55)
      .normalize();
    // Blend a little toward the target so the femur has a sane starting angle
    const baseW = leg.root.getWorldPosition(new THREE.Vector3());
    const toFoot = foot.clone().sub(baseW);
    const flat = toFoot.clone().addScaledVector(up, -toFoot.dot(up));
    if (flat.lengthSq() > 1e-6) {
      flat.normalize();
      coxaAim.lerp(flat, 0.28).normalize();
    }
    const coxaQuat = alignY(coxaAim);
    setWorldQuat(leg.root, coxaQuat);
    leg.root.updateWorldMatrix(true, true);

    // --- hip ----------------------------------------------------------
    // Read the hip back from the rig rather than reconstructing it, so the IK
    // and the rendered geometry can never disagree about where the leg starts.
    leg.hip.updateWorldMatrix(true, false);
    const hip = this._hipWorld.copy(leg.hip.getWorldPosition(new THREE.Vector3()));

    // --- tarsus target -------------------------------------------------
    // The tarsus runs from the ankle to the sole; it is held arched, with the
    // pretarsus flat on the substrate. `tarsalAngle` controls how steeply.
    const tarsalAngle = style.tarsalAngle ?? 0.42;   // rad, tilt of the chain off the normal
    const soleDir = up.clone().multiplyScalar(-1)
      .addScaledVector(fwd, -Math.sin(tarsalAngle) * 0.75)
      .addScaledVector(lat, Math.sin(tarsalAngle) * 0.25)
      .normalize();
    // Pretarsus drop: the sole sits slightly below the ankle chain end
    const solePos = foot.clone().addScaledVector(normal, 0.012);
    const ankleTarget = solePos.clone().addScaledVector(soleDir, -this.tarsusLen * 0.96);

    // --- femur / tibia -------------------------------------------------
    // Pole vector: knees ride high and outboard, as in a live insect.
    const pole = up.clone().multiplyScalar(0.75)
      .addScaledVector(lat, 0.85)
      .addScaledVector(fwd, -0.25).normalize();

    const sol = solveTwoBone(hip, ankleTarget, this.femurLen, this.tibiaLen, pole);
    this.flexion = sol.flex;

    // Joint limits (GAIT.maxJointAngle). If the raw solution over-extends we
    // shorten the effective femur sweep by lifting the ankle target — this
    // keeps the sole on the surface instead of letting the foot float.
    let ankle = sol.reached;
    let s = sol;
    if (s.flex > GAIT.maxJointAngle) {
      // Re-solve with a hip raised along the pole until the flexion fits.
      for (let k = 0; k < 6; k++) {
        const over = s.flex - GAIT.maxJointAngle;
        const lift = over * 0.55;
        ankle = ankleTarget.clone().addScaledVector(pole, lift);
        s = solveTwoBone(hip, ankle, this.femurLen, this.tibiaLen, pole);
        if (s.flex <= GAIT.maxJointAngle + 1e-3) break;
      }
    }

    // Trochanter and femur are one straight 1.88 mm link in the anatomy, so
    // both joints take the same direction; the tibia then turns at the knee.
    setWorldQuat(leg.femur, alignY(s.femurDir));
    setWorldQuat(leg.knee, alignY(s.femurDir));
    setWorldQuat(leg.tibia, alignY(s.tibiaDir));
    leg.femur.updateWorldMatrix(true, true);
    leg.knee.updateWorldMatrix(true, true);
    leg.tibia.updateWorldMatrix(true, true);

    // --- tarsus chain ---------------------------------------------------
    // The ANKLE is the end of the tibia, i.e. the origin of the first
    // tarsomere — not the origin of the tibia node, which is the knee.
    const ankleW = leg.tarsus[0].getWorldPosition(new THREE.Vector3());
    // Bulge the tarsus away from the ground near the ankle, then let the last
    // tarsomeres lie along the substrate.
    const bulge = up.clone().multiplyScalar(1.0).addScaledVector(fwd, 0.15).normalize();
    solveChain(ankleW, solePos, this.spec.tarsus, this.tarsusN, bulge, this._chain);
    const pts = this._chain;

    for (let i = 0; i < this.tarsusN; i++) {
      const node = leg.tarsus[i];
      const a = pts[i], b = pts[i + 1];
      const d = b.clone().sub(a);
      if (d.lengthSq() < 1e-10) continue;
      d.normalize();
      setWorldQuat(node, alignY(d));
      node.updateWorldMatrix(true, false);
    }
    const t5 = leg.tarsus[this.tarsusN - 1];
    t5.updateWorldMatrix(true, true);

    // --- pretarsus: sole flat on the substrate -------------------------
    // The pretarsus is the only part of the leg that must lie *flat* on the
    // surface, because that is where the pulvilli and the claw tips engage.
    // We orient it from a clean basis: +Y along the surface normal (away),
    // +Z along the direction of travel, so the claws point the way the leg
    // is about to be pulled.
    const pre = leg.pretarsus;
    // Travel axis: the direction the tarsus is actually pointing, flattened
    // onto the substrate. The claws then lie along the pull the leg is about
    // to apply, instead of snapping to an unrelated heading every frame.
    const axis = solePos.clone().sub(ankleW);
    axis.addScaledVector(normal, -axis.dot(normal));
    if (axis.lengthSq() < 1e-10) axis.copy(fwd).addScaledVector(normal, -fwd.dot(normal));
    if (axis.lengthSq() < 1e-10) axis.copy(lat);
    axis.normalize();
    // Orthogonalise the travel direction against the surface normal
    const zAxis = axis.clone().addScaledVector(normal, -axis.dot(normal));
    if (zAxis.lengthSq() < 1e-8) zAxis.copy(lat);
    zAxis.normalize();
    const yAxis = normal.clone();
    const xAxis = new THREE.Vector3().crossVectors(yAxis, zAxis).normalize();
    const basis = new THREE.Matrix4().makeBasis(xAxis, yAxis, zAxis);
    setWorldQuat(pre, new THREE.Quaternion().setFromRotationMatrix(basis));
    pre.updateWorldMatrix(true, true);

    // Record the achieved foot position for slip verification
    this._footWorld.copy(pre.getWorldPosition(new THREE.Vector3()))
      .addScaledVector(normal, -0.012);
    this.soleDirWorld = soleDir;
    this.hipWorld = hip.clone();
    this.contact = style.contact ?? 0;
    return this._footWorld;
  }
}
