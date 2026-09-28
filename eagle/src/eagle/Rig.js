import * as THREE from 'three';
import { SKEL } from './anatomy.js';

// Skeleton of the eagle as a THREE.Bone hierarchy (bind pose = flight pose: trunk horizontal, wings fully
// extended laterally, neck forward, legs hanging).
//
// Conventions (all in the bird's own frame): +z forward, +y dorsal, +x = bird's LEFT.
//   spine bones (pelvis, thorax, neck*, head): child along local +z; tail: child along local -z
//   wing bones: child along local +x = outward. The RIGHT shoulder frame is turned 180deg about y, so for
//     both wings local +x is outward and +y is dorsal; "forward" is local +z on the left and -z on the
//     right. Use side factor s (+1 left, -1 right) for rotations about local x and y.
//   leg bones: child along local -y (down); flexion is rotation about local x.

const _q = new THREE.Quaternion(), _q2 = new THREE.Quaternion(), _e = new THREE.Euler();
const _v = new THREE.Vector3(), _v2 = new THREE.Vector3(), _v3 = new THREE.Vector3(), _m = new THREE.Matrix4();
const X = new THREE.Vector3(1, 0, 0), Y = new THREE.Vector3(0, 1, 0), Z = new THREE.Vector3(0, 0, 1);

function bone(name, parent, x = 0, y = 0, z = 0) {
  const b = new THREE.Bone(); b.name = name; b.position.set(x, y, z);
  if (parent) parent.add(b);
  b.userData.rest = b.position.clone();
  return b;
}

export class Rig {
  constructor() {
    const S = SKEL;
    this.root = new THREE.Object3D(); this.root.name = 'eagleRoot';
    // pelvis origin = acetabulum level, roughly the centre of mass of the standing bird
    this.pelvis = bone('pelvis', null, 0, 0, 0);
    this.root.add(this.pelvis);
    this.thorax = bone('thorax', this.pelvis, 0, 0.018, 0.1);
    this.tail = bone('tail', this.pelvis, 0, 0.012, S.tailToPelvis); // pygostyle
    // neck
    this.neck = [];
    let p = bone('neck0', this.thorax, 0, 0.032, 0.115);
    this.neck.push(p);
    for (let i = 1; i < S.neckSegs.length; i++) {
      p = bone('neck' + i, p, 0, 0, S.neckSegs[i - 1]);
      this.neck.push(p);
    }
    this.head = bone('head', p, 0, 0, S.neckSegs[S.neckSegs.length - 1]);
    this.jaw = bone('jaw', this.head, 0, -0.012, 0.03);        // quadrate-articular hinge
    this.upperBill = bone('upperBill', this.head, 0, 0.012, 0.062); // craniofacial hinge (kinesis)

    // wings
    this.wings = [1, -1].map((s) => {
      const sh = bone(s > 0 ? 'shoulderL' : 'shoulderR', this.thorax, S.shoulder[0] * s, S.shoulder[1], S.shoulder[2] - 0.035);
      if (s < 0) sh.userData.base = new THREE.Quaternion().setFromAxisAngle(Y, Math.PI);
      else sh.userData.base = new THREE.Quaternion();
      sh.quaternion.copy(sh.userData.base);
      const hum = bone(s > 0 ? 'humerusL' : 'humerusR', sh, 0, 0, 0);
      const uln = bone(s > 0 ? 'ulnaL' : 'ulnaR', hum, S.humerus, 0, 0);
      const hand = bone(s > 0 ? 'handL' : 'handR', uln, S.ulna, 0, 0);
      const dig = bone(s > 0 ? 'digitL' : 'digitR', hand, S.hand, 0, 0);
      const tip = bone(s > 0 ? 'wingTipL' : 'wingTipR', dig, S.digit, 0, 0);
      const alula = bone(s > 0 ? 'alulaL' : 'alulaR', hand, 0.012, 0.004, 0.012 * s);
      return { s, shoulder: sh, humerus: hum, ulna: uln, hand, digit: dig, tip, alula };
    });

    // legs
    this.legs = [1, -1].map((s) => {
      const hip = bone(s > 0 ? 'hipL' : 'hipR', this.pelvis, S.hip[0] * s, S.hip[1], S.hip[2]);
      const femur = bone(s > 0 ? 'femurL' : 'femurR', hip, 0, 0, 0);
      const tibia = bone(s > 0 ? 'tibiaL' : 'tibiaR', femur, 0, -S.femur, 0);
      const tarsus = bone(s > 0 ? 'tarsusL' : 'tarsusR', tibia, 0, -S.tibiotarsus, 0);
      const foot = bone(s > 0 ? 'footL' : 'footR', tarsus, 0, -S.tarsometatarsus, 0);
      const toes = S.toes.map((t, ti) => {
        const base = bone(`toe${t.name}${s > 0 ? 'L' : 'R'}`, foot, 0, 0, 0);
        const yaw = s > 0 ? t.dir : (ti === 0 ? 2 * Math.PI - t.dir : -t.dir);
        base.userData.yaw = yaw;
        base.quaternion.setFromAxisAngle(Y, yaw);
        const ph = [];
        let par = base;
        for (let k = 0; k < t.phal.length; k++) {
          const b = bone(`toe${t.name}${k}${s > 0 ? 'L' : 'R'}`, par, 0, 0, k === 0 ? 0 : t.phal[k - 1]);
          ph.push(b); par = b;
        }
        const claw = bone(`claw${t.name}${s > 0 ? 'L' : 'R'}`, par, 0, 0, t.phal[t.phal.length - 1]);
        return { def: t, base, ph, claw };
      });
      return { s, hip, femur, tibia, tarsus, foot, toes };
    });

    this.bones = [];
    this.pelvis.traverse((o) => { if (o.isBone) this.bones.push(o); });
    this.root.updateMatrixWorld(true);
    // bind snapshot
    this.bindWorld = new Map();
    for (const b of this.bones) this.bindWorld.set(b, b.matrixWorld.clone());
    this.core = [this.pelvis, this.thorax, ...this.neck, this.head, this.tail];
  }

  // ---------------- wings ----------------
  // elev: dihedral (+ up), sweep: + forward, twist: pronation (+ leading edge down),
  // elbow/wrist: flexion (0 = straight, + = folded; ulna swings forward, hand swings back),
  // handTwist: extra pronation of the hand, digit: flexion of the major digit (+ back)
  setWing(i, { elev = 0, sweep = 0, twist = 0, elbow = 0.25, wrist = 0.1, handTwist = 0, digit = 0, elbowTwist = 0 }) {
    const w = this.wings[i], s = w.s;
    // shoulder: sweep about dorsal axis, then elevation about the forward axis, then twist about the humerus
    _q.setFromAxisAngle(Y, -sweep * s);
    _q2.setFromAxisAngle(Z, elev); _q.multiply(_q2);
    _q2.setFromAxisAngle(X, twist * s); _q.multiply(_q2);
    w.humerus.quaternion.copy(_q);
    // elbow: the ulna swings forward (toward the leading edge) with flexion
    _q.setFromAxisAngle(Y, -elbow * s);
    _q2.setFromAxisAngle(X, elbowTwist * s); _q.multiply(_q2);
    w.ulna.quaternion.copy(_q);
    // wrist: the hand swings back (toward the trailing edge)
    _q.setFromAxisAngle(Y, wrist * s);
    _q2.setFromAxisAngle(X, handTwist * s); _q.multiply(_q2);
    w.hand.quaternion.copy(_q);
    w.digit.quaternion.setFromAxisAngle(Y, digit * s);
  }

  // ---------------- neck ----------------
  // The avian neck is an S-curve: lower neck angled up/back, mid neck forward, upper neck (atlas region)
  // flexed down. Posture parameters: ext (0 retracted S .. 1 fully extended), lean (whole neck pitch,
  // + forward/down), plus yaw & roll distributed along the chain (most of the twist in the upper neck).
  _neckFK(ext, lean, out) {
    const n = this.neck.length;
    // pitch per joint (rotation about +x; + = nose down)
    for (let i = 0; i < n; i++) {
      const u = i / (n - 1);
      // retracted S: base bent up strongly, middle bent down, top bent back up
      const sCurve = -0.62 * Math.exp(-((u - 0.05) ** 2) / 0.03) + 0.55 * Math.exp(-((u - 0.55) ** 2) / 0.05) - 0.18 * Math.exp(-((u - 0.95) ** 2) / 0.02);
      out[i] = (1 - ext) * sCurve + lean / n;
    }
    return out;
  }

  // Place the neck so that the head origin reaches `targetLocal` (thorax space, y/z plane; x handled by yaw)
  // and orient the head to world quaternion `headWorldQ`. Returns the solved {ext, lean}.
  solveNeck(targetLocal, headWorldQ, yaw = 0, roll = 0, guess = { ext: 0.3, lean: 0 }) {
    const n = this.neck.length;
    const pitches = this._pitch || (this._pitch = new Float32Array(n));
    const fk = (ext, lean) => {
      this._neckFK(ext, lean, pitches);
      // chain in thorax space, sagittal plane
      let py = this.neck[0].userData.rest.y, pz = this.neck[0].userData.rest.z, a = 0;
      for (let i = 0; i < n; i++) {
        a += pitches[i];
        const L = SKEL.neckSegs[i];
        py -= Math.sin(a) * L; pz += Math.cos(a) * L;
      }
      return [py, pz];
    };
    let ext = guess.ext, lean = guess.lean;
    const ty = targetLocal.y, tz = targetLocal.z;
    for (let it = 0; it < 6; it++) {
      const [y0, z0] = fk(ext, lean);
      const ey = ty - y0, ez = tz - z0;
      if (ey * ey + ez * ez < 1e-8) break;
      const h = 1e-3;
      const [y1, z1] = fk(ext + h, lean), [y2, z2] = fk(ext, lean + h);
      const a11 = (y1 - y0) / h, a21 = (z1 - z0) / h, a12 = (y2 - y0) / h, a22 = (z2 - z0) / h;
      const det = a11 * a22 - a12 * a21;
      if (Math.abs(det) < 1e-9) break;
      ext += (a22 * ey - a12 * ez) / det;
      lean += (-a21 * ey + a11 * ez) / det;
      ext = Math.min(Math.max(ext, -0.35), 1.05);
      lean = Math.min(Math.max(lean, -2.6), 2.2);
    }
    this._neckFK(ext, lean, pitches);
    // distribute yaw (twist) and roll: 25% lower neck, 75% upper neck
    for (let i = 0; i < n; i++) {
      const u = i / (n - 1);
      const wy = (0.25 / n) * 2 * (1 - u) + (0.75 / n) * 2 * u;
      _e.set(pitches[i], yaw * wy, roll * wy * 0.5, 'YXZ');
      this.neck[i].quaternion.setFromEuler(_e);
    }
    // head: absolute world orientation
    this.neck[n - 1].updateWorldMatrix(true, false);
    this.neck[n - 1].getWorldQuaternion(_q).invert();
    this.head.quaternion.copy(_q.multiply(headWorldQ));
    return { ext, lean };
  }

  // ---------------- legs ----------------
  // Two-bone IK (tibiotarsus + tarsometatarsus) with the femur held at `femurPitch` (birds walk mostly
  // with knee and intertarsal joints; the femur stays near horizontal inside the body). Foot target in
  // pelvis space is the metatarsophalangeal joint ("ball" of the foot).
  solveLeg(i, targetPelvis, femurPitch = -1.0, footPitchWorld = null, abduct = 0) {
    const L = this.legs[i];
    const l1 = SKEL.tibiotarsus, l2 = SKEL.tarsometatarsus;
    // femur: pitch forward (-x rotation brings -y toward +z), slight abduction
    _e.set(femurPitch, 0, abduct * L.s, 'XYZ');
    L.femur.quaternion.setFromEuler(_e);
    L.femur.updateWorldMatrix(true, false);
    // knee position in pelvis space
    const knee = _v.set(0, -SKEL.femur, 0).applyQuaternion(L.femur.quaternion).add(L.hip.position);
    const d = _v2.copy(targetPelvis).sub(knee);
    let dist = d.length();
    dist = Math.min(Math.max(dist, Math.abs(l1 - l2) + 1e-3), l1 + l2 - 1e-4);
    // work in the plane containing knee->target and the lateral axis
    const dir = d.clone().normalize();
    // bend: intertarsal joint points BACKWARD (-z); the plane normal is the lateral axis
    const side = new THREE.Vector3(1, 0, 0);
    const fwdInPlane = new THREE.Vector3().crossVectors(side, dir).normalize(); // perpendicular to dir, in sagittal plane
    const cosA = (l1 * l1 + dist * dist - l2 * l2) / (2 * l1 * dist);
    const A = Math.acos(Math.min(Math.max(cosA, -1), 1));
    // ankle point: rotate dir away toward the back (the intertarsal joint sits behind the knee->foot line)
    const ankle = knee.clone().addScaledVector(dir, Math.cos(A) * l1).addScaledVector(fwdInPlane, Math.sin(A) * l1);
    // set tibia: local -y should point from knee to ankle (expressed in femur space)
    this._aimDown(L.tibia, L.femur, knee, ankle);
    this._aimDown(L.tarsus, L.tibia, ankle, targetPelvis);
    // foot (toe base frame) keeps a world pitch (flat on the ground) if given
    if (footPitchWorld) {
      L.tarsus.updateWorldMatrix(true, false);
      L.tarsus.getWorldQuaternion(_q).invert();
      L.foot.quaternion.copy(_q.multiply(footPitchWorld));
    } else L.foot.quaternion.identity();
    return { knee, ankle };
  }

  // orient `b` (child of parent `par`) so that its local -y axis points from pelvis-space a to c
  _aimDown(b, par, a, c) {
    // direction in pelvis space -> in parent's space
    const dirP = _v3.copy(c).sub(a).normalize();
    // parent orientation relative to pelvis
    const qPar = new THREE.Quaternion();
    let o = par; const chain = [];
    while (o && o !== this.pelvis) { chain.push(o); o = o.parent; }
    for (let k = chain.length - 1; k >= 0; k--) qPar.multiply(chain[k].quaternion);
    const dirLocal = dirP.clone().applyQuaternion(qPar.clone().invert());
    _q.setFromUnitVectors(new THREE.Vector3(0, -1, 0), dirLocal);
    // keep the bone's lateral axis as close to +x as possible (remove twist)
    b.quaternion.copy(_q);
  }

  // toes: curl 0 = flat/spread on the ground, 1 = clenched (grip / tucked in flight); spread multiplies yaw
  setToes(i, curl = 0, spread = 1) {
    const L = this.legs[i];
    for (let ti = 0; ti < L.toes.length; ti++) {
      const t = L.toes[ti], n = t.ph.length, yw = t.base.userData.yaw;
      t.base.quaternion.setFromAxisAngle(Y, ti === 0 ? yw : yw * spread);
      for (let k = 0; k < n; k++) {
        // flat: tiny down-bend so the pads meet the ground; curled: progressive flexion
        const a = 0.05 + curl * (k === 0 ? 0.35 : 0.55 + 0.1 * k);
        t.ph[k].quaternion.setFromAxisAngle(X, a);
      }
      t.claw.quaternion.setFromAxisAngle(X, 0.25 + curl * 0.4);
    }
  }
}
