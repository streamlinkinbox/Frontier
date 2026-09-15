import * as THREE from 'three';

// Analytic two-bone IK for the hindlimb (hip -> knee -> ankle) plus
// foot orientation (metatarsus pitch/yaw) and toe (MTP) articulation.
// All targets are in root-local space (root is static identity).

const _H = new THREE.Vector3();
const _dir = new THREE.Vector3();
const _dirN = new THREE.Vector3();
const _axis = new THREE.Vector3();
const _pole = new THREE.Vector3();
const _fA = new THREE.Vector3();
const _fB = new THREE.Vector3();
const _kA = new THREE.Vector3();
const _kB = new THREE.Vector3();
const _femur = new THREE.Vector3();
const _shin = new THREE.Vector3();
const _negY = new THREE.Vector3(0, -1, 0);
const _qF = new THREE.Quaternion();
const _qS = new THREE.Quaternion();
const _qFoot = new THREE.Quaternion();
const _qToe = new THREE.Quaternion();
const _qInv = new THREE.Quaternion();
const _e = new THREE.Euler();
const _K = new THREE.Vector3();

const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);

/**
 * P = {
 *   hip, knee, ank, mtp, L1, L2,
 *   pelvisPos: Vector3-like {x,y,z}, pelvisQuat: THREE.Quaternion,
 *   hipLocal: THREE.Vector3, target: {x,y,z},
 *   yaw, alpha, beta, side (+1 left / -1 right)
 * }
 * Returns { knee: Vector3 (reused), ext }.
 */
export function solveLeg(P) {
  const { hip, knee, ank, mtp, L1, L2 } = P;

  // hip joint center in root space
  _H.set(P.hipLocal.x, P.hipLocal.y, P.hipLocal.z)
    .applyQuaternion(P.pelvisQuat)
    .add(P.pelvisPos);

  _dir.set(P.target.x - _H.x, P.target.y - _H.y, P.target.z - _H.z);
  let D = _dir.length();
  D = clamp(D, Math.abs(L1 - L2) + 0.05, L1 + L2 - 0.02);
  _dirN.copy(_dir).normalize();

  // knee pole: forward + slightly outward (theropod knee faces forward)
  _pole.set(P.side * 0.15, 0.1, 1).normalize();
  _axis.crossVectors(_dirN, _pole);
  if (_axis.lengthSq() < 1e-6) _axis.set(1, 0, 0);
  _axis.normalize();

  // angle at hip between H->T line and femur
  const cosA = clamp((L1 * L1 + D * D - L2 * L2) / (2 * L1 * D), -1, 1);
  const a1 = Math.acos(cosA);

  // two mirrored femur directions — pick the one leaning toward the pole
  _fA.copy(_dirN).applyAxisAngle(_axis, a1);
  _fB.copy(_dirN).applyAxisAngle(_axis, -a1);
  _kA.copy(_H).addScaledVector(_fA, L1);
  _kB.copy(_H).addScaledVector(_fB, L1);
  const sA = (_kA.x - _H.x) * _pole.x + (_kA.y - _H.y) * _pole.y + (_kA.z - _H.z) * _pole.z;
  const sB = (_kB.x - _H.x) * _pole.x + (_kB.y - _H.y) * _pole.y + (_kB.z - _H.z) * _pole.z;
  if (sA >= sB) { _femur.copy(_fA); _K.copy(_kA); }
  else { _femur.copy(_fB); _K.copy(_kB); }

  // hip + knee local rotations
  _qF.setFromUnitVectors(_negY, _femur);
  hip.quaternion.copy(P.pelvisQuat).invert().multiply(_qF);

  _shin.set(P.target.x - _K.x, P.target.y - _K.y, P.target.z - _K.z).normalize();
  _qS.setFromUnitVectors(_negY, _shin);
  _qInv.copy(_qF).invert();
  knee.quaternion.copy(_qInv).multiply(_qS);

  // foot (metatarsus) world orientation: yaw then pitch
  _e.set(P.alpha, P.yaw, 0, 'YXZ');
  _qFoot.setFromEuler(_e);
  _qInv.copy(_qS).invert();
  ank.quaternion.copy(_qInv).multiply(_qFoot);

  // toes world orientation
  _e.set(P.beta, P.yaw, 0, 'YXZ');
  _qToe.setFromEuler(_e);
  _qInv.copy(_qFoot).invert();
  mtp.quaternion.copy(_qInv).multiply(_qToe);

  return { knee: _K, ext: D / (L1 + L2) };
}
