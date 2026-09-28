import * as THREE from 'three';
import { SKEL, COLOR } from './anatomy.js';
import { loftGeometry } from './Body.js';

const clamp = (x, a, b) => Math.min(Math.max(x, a), b);
const lerp = (a, b, t) => a + (b - a) * t;
const sstep = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
const _m = new THREE.Matrix4(), _q = new THREE.Quaternion(), _q2 = new THREE.Quaternion();
const _x = new THREE.Vector3(), _y = new THREE.Vector3(), _z = new THREE.Vector3(), _p = new THREE.Vector3(), _s = new THREE.Vector3();

// Wing plumage & membrane.
// Each wing feather lives in the frame of one wing bone (local +x outward along the bone, +y dorsal).
// Its rachis direction is given by an angle theta measured from the bone's outward axis towards the
// trailing edge, interpolated between an extended and a folded value by the flexion of the joint that
// drives the fan (elbow for secondaries/tertials & their coverts, wrist for primaries & their coverts).
//
// Measurements (bald eagle): primaries p1..p10 ~ 30-50 cm (p8 longest, p10 short), secondaries ~31-33 cm,
// tertials ~22-27 cm, 12 rectrices ~ 32 cm (see anatomy.js).
const PRIM_LEN = [0.3, 0.315, 0.335, 0.36, 0.395, 0.435, 0.47, 0.49, 0.48, 0.395];   // p1..p10
const PRIM_W = [0.078, 0.078, 0.077, 0.076, 0.075, 0.074, 0.07, 0.066, 0.062, 0.055];

export class Wings {
  constructor(rig, sys) {
    this.rig = rig; this.sys = sys;
    this.items = [];              // {id, w (wing index), bone, anchor, thE, thF, drive, y, len, wid, group, k, pitch, roll}
    this.params = [0, 1].map(() => ({ primBend: 0, secBend: 0, spread: 0, supinate: 0, slot: 0, covertLift: 0, flutter: 0, tipCurl: 0 }));
    for (let i = 0; i < 2; i++) this._build(i);
    this.cores = [0, 1].map((i) => this._core(i));
  }

  _add(wi, bone, o) {
    const w = this.rig.wings[wi];
    const brown = new THREE.Color(o.color ?? COLOR.brownDark);
    brown.offsetHSL((Math.random() - 0.5) * 0.015, (Math.random() - 0.5) * 0.05, (Math.random() - 0.5) * 0.025).multiplyScalar(1.5);
    const id = this.sys.add({ type: o.type, variant: o.variant ?? Math.floor(Math.random() * 7), color: brown, bend: o.bend ?? 0, camber: o.camber ?? 0.04,
      flutter: o.flutter ?? 0.01, seed: Math.random(), lift: 0, ruffle: o.ruffle ?? 0.2, ao: o.ao ?? 0.65, mirror: w.s < 0 });
    this.items.push({ id, wi, bone, ...o });
  }

  _build(wi) {
    const W = this.rig.wings[wi];
    const hum = SKEL.humerus, uln = SKEL.ulna, hand = SKEL.hand, dig = SKEL.digit;
    // ---- primaries: p1-p6 on the carpometacarpus, p7-p10 on the digit (phalanges) ----
    for (let k = 0; k < 10; k++) {
      const onDigit = k >= 6;
      const bone = onDigit ? W.digit : W.hand;
      const ax = onDigit ? 0.012 + (k - 6) * 0.018 : 0.004 + k * 0.019;
      // extended fan: p1 points back-out (~72deg from the hand axis), p10 nearly along the hand (~6deg)
      const thE = lerp(0.95, -0.14, Math.pow(k / 9, 0.9));
      const thF = lerp(0.14, 0.02, k / 9) - (onDigit ? 0.05 : 0);
      this._add(wi, bone, { group: 'prim', k, type: k >= 5 ? 'primaryOuter' : 'primaryInner', anchor: [ax, 0, -0.006], thE, thF, drive: 'wrist',
        y: -0.0022 * k, len: PRIM_LEN[k], wid: PRIM_W[k], bend: -0.03, camber: 0.05, ao: 0.6, flutter: 0.008 });
    }
    // ---- secondaries: 14 along the ulna, s1 at the wrist ... s14 at the elbow ----
    for (let k = 0; k < 14; k++) {
      const u = k / 13;
      const ax = lerp(uln - 0.012, 0.02, u);
      const len = 0.315 + 0.02 * Math.sin(u * Math.PI) - 0.03 * sstep(0.75, 1, u);
      this._add(wi, W.ulna, { group: 'sec', k, type: 'secondary', anchor: [ax, 0.002, -0.008], thE: lerp(1.72, 1.98, u), thF: lerp(2.98, 3.06, u), drive: 'elbow',
        y: 0.0008 * k + 0.002, len, wid: 0.082, bend: -0.035, camber: 0.05, ao: 0.6, flutter: 0.008 });
    }
    // ---- tertials: 3 at the proximal ulna / elbow ----
    for (let k = 0; k < 3; k++) {
      this._add(wi, W.ulna, { group: 'ter', k, type: 'secondary', anchor: [0.012 - k * 0.012, 0.004, -0.006], thE: 2.05 + k * 0.12, thF: 3.08 + k * 0.02, drive: 'elbow',
        y: 0.009 + 0.001 * k, len: 0.27 - k * 0.025, wid: 0.085, bend: -0.03, camber: 0.05, ao: 0.62, color: COLOR.brown, flutter: 0.012 });
    }
    // ---- dorsal coverts ----
    // greater secondary coverts (one per secondary)
    for (let k = 0; k < 15; k++) {
      const u = k / 14;
      this._add(wi, W.ulna, { group: 'cov', type: 'covert', anchor: [lerp(uln - 0.01, 0.01, u), 0.006, 0.004], thE: lerp(1.72, 1.98, u), thF: lerp(2.95, 3.05, u), drive: 'elbow',
        y: 0.012 + 0.0006 * k, len: 0.15, wid: 0.07, bend: -0.03, camber: 0.05, color: COLOR.brownCovert, ruffle: 0.5, ao: 0.6, flutter: 0.02 });
    }
    // median coverts
    for (let k = 0; k < 18; k++) {
      const u = k / 17;
      this._add(wi, W.ulna, { group: 'cov', type: 'covert', anchor: [lerp(uln, 0.0, u), 0.009, 0.02], thE: lerp(1.75, 2.0, u), thF: lerp(2.95, 3.05, u), drive: 'elbow',
        y: 0.016 + 0.0005 * k, len: 0.095, wid: 0.055, bend: -0.03, camber: 0.05, color: COLOR.brownCovert, ruffle: 0.6, ao: 0.55, flutter: 0.02 });
    }
    // lesser & marginal coverts: dense small rows over the forearm and the propatagium (leading edge)
    for (let row = 0; row < 4; row++) {
      const n = 22 + row * 2;
      for (let k = 0; k < n; k++) {
        const u = (k + (row % 2) * 0.5) / n;
        const zf = 0.03 + row * 0.014;
        // the propatagium bulges forward near the elbow
        const bulge = 0.022 * Math.exp(-((u - 0.85) ** 2) / 0.04);
        this._add(wi, W.ulna, { group: 'cov', type: 'contour', anchor: [lerp(uln + 0.01, -0.01, u), 0.01 - row * 0.001, zf + bulge], thE: lerp(1.7, 1.95, u), thF: lerp(2.9, 3.05, u), drive: 'elbow',
          y: 0.019 + 0.0025 * row, len: 0.07 - row * 0.008, wid: 0.045 - row * 0.004, bend: -0.05, camber: 0.05, color: COLOR.brownCovert, ruffle: 0.8, ao: 0.6, flutter: 0.025 });
      }
    }
    // humeral coverts (upper arm) & scapular-side coverts on the humerus
    for (let row = 0; row < 4; row++) {
      for (let k = 0; k < 12; k++) {
        const u = (k + (row % 2) * 0.5) / 12;
        this._add(wi, W.humerus, { group: 'hcov', type: 'contour', anchor: [lerp(0.02, hum + 0.01, u), 0.01, -0.03 + row * 0.024], thE: 1.62, thF: 0.3 + row * 0.05, drive: 'elbow',
          y: 0.016 + row * 0.0025, len: 0.11 - row * 0.015, wid: 0.06 - row * 0.006, bend: -0.05, camber: 0.05, color: COLOR.brown, ruffle: 0.7, ao: 0.55, flutter: 0.02 });
      }
    }
    // tertial-area / humeral flight feathers (fill between body and secondaries)
    for (let k = 0; k < 6; k++) {
      this._add(wi, W.humerus, { group: 'hum', type: 'secondary', anchor: [0.05 + k * 0.028, -0.002, -0.02], thE: 1.7, thF: 0.22, drive: 'elbow',
        y: 0.01 + k * 0.0008, len: 0.24 + k * 0.012, wid: 0.08, bend: -0.03, camber: 0.05, color: COLOR.brown, ao: 0.6, flutter: 0.01 });
    }
    // primary coverts (greater + median) on the hand
    for (let k = 0; k < 10; k++) {
      const onDigit = k >= 6, bone = onDigit ? W.digit : W.hand;
      const ax = onDigit ? 0.012 + (k - 6) * 0.018 : 0.004 + k * 0.019;
      const thE = lerp(0.95, -0.14, Math.pow(k / 9, 0.9)) + 0.12;
      this._add(wi, bone, { group: 'pcov', k, type: 'covert', anchor: [ax, 0.006, 0.006], thE, thF: lerp(0.3, 0.1, k / 9), drive: 'wrist',
        y: 0.006 + 0.0005 * (10 - k), len: 0.16 - 0.004 * k, wid: 0.06, bend: -0.03, camber: 0.05, ao: 0.6, color: COLOR.brownCovert, flutter: 0.015 });
      this._add(wi, bone, { group: 'pcov', k, type: 'covert', anchor: [ax, 0.009, 0.022], thE: thE + 0.2, thF: lerp(0.4, 0.15, k / 9), drive: 'wrist',
        y: 0.01 + 0.0005 * (10 - k), len: 0.085, wid: 0.05, bend: -0.04, camber: 0.05, ao: 0.6, color: COLOR.brownCovert, flutter: 0.02 });
    }
    // marginal coverts on the hand leading edge
    for (let k = 0; k < 12; k++) {
      const bone = k < 8 ? W.hand : W.digit;
      const ax = k < 8 ? k * 0.014 : (k - 8) * 0.018;
      this._add(wi, bone, { group: 'pcov', type: 'contour', anchor: [ax, 0.008, 0.03], thE: 0.7, thF: 0.3, drive: 'wrist',
        y: 0.013, len: 0.06, wid: 0.04, bend: -0.05, camber: 0.05, ao: 0.6, color: COLOR.brownCovert, ruffle: 0.6, flutter: 0.02 });
    }
    // alula (bastard wing): 4 stiff feathers on the alular digit, pointing forward-out along the leading edge
    for (let k = 0; k < 4; k++) {
      this._add(wi, W.alula, { group: 'alula', k, type: 'covert', anchor: [0.004 * k, 0.012 + 0.002 * k, 0.0], thE: -0.28 + k * 0.1, thF: -0.2, drive: 'none',
        y: 0.0, len: 0.13 - k * 0.018, wid: 0.04, bend: -0.03, camber: 0.04, ao: 0.6, flutter: 0.01 });
    }
    // ---- underwing coverts (ventral, normal -y): same layout mirrored below ----
    for (let row = 0; row < 3; row++) {
      for (let k = 0; k < 16; k++) {
        const u = (k + (row % 2) * 0.5) / 16;
        this._add(wi, W.ulna, { group: 'ucov', type: 'covert', anchor: [lerp(uln, 0.0, u), -0.006 - row * 0.002, 0.004 + row * 0.02], thE: lerp(1.72, 1.98, u), thF: lerp(2.95, 3.05, u), drive: 'elbow',
          y: -0.004 - row * 0.003, len: 0.15 - row * 0.035, wid: 0.065, bend: 0.03, camber: -0.04, ventral: true, color: COLOR.brownCovert, ao: 0.5, flutter: 0.02 });
      }
      for (let k = 0; k < 10; k++) {
        const u = (k + (row % 2) * 0.5) / 10;
        this._add(wi, W.humerus, { group: 'ucov', type: 'covert', anchor: [lerp(0.02, hum, u), -0.012 - row * 0.002, -0.02 + row * 0.022], thE: 1.62, thF: 0.3, drive: 'elbow',
          y: -0.006 - row * 0.003, len: 0.14 - row * 0.03, wid: 0.065, bend: 0.03, camber: -0.04, ventral: true, color: COLOR.brownCovert, ao: 0.5, flutter: 0.02 });
      }
      for (let k = 0; k < 10; k++) {
        const onDigit = k >= 6, bone = onDigit ? W.digit : W.hand;
        const ax = onDigit ? 0.012 + (k - 6) * 0.018 : 0.004 + k * 0.019;
        this._add(wi, bone, { group: 'ucov', type: 'covert', anchor: [ax, -0.006 - row * 0.002, 0.004 + row * 0.018], thE: lerp(0.95, -0.14, Math.pow(k / 9, 0.9)) + 0.1, thF: 0.2, drive: 'wrist',
          y: -0.004 - row * 0.003, len: 0.15 - row * 0.035, wid: 0.06, bend: 0.03, camber: -0.04, ventral: true, color: COLOR.brownCovert, ao: 0.5, flutter: 0.02 });
      }
    }
  }

  // Rigid bone coverings (muscle mass of the arm, forearm & hand) - rigid per bone so they never stretch
  // when the wing folds; the plumage covers them.
  _core(wi) {
    const W = this.rig.wings[wi], s = W.s;
    const mat = new THREE.MeshStandardMaterial({ color: COLOR.brown, roughness: 0.95 });
    const seg = (bone, len, r0, r1, fwd) => {
      const g = new THREE.CapsuleGeometry(1, 1, 6, 14);
      // capsule along y -> along x; taper by scaling vertices
      const pos = g.attributes.position;
      for (let i = 0; i < pos.count; i++) {
        let x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
        const t = THREE.MathUtils.clamp(y + 0.5, 0, 1);
        const r = lerp(r0, r1, t);
        pos.setXYZ(i, (y + 1.5) / 3 * len * 1.08 - len * 0.04, x * r * 0.7, z * r * s + fwd * s);
      }
      g.computeVertexNormals();
      if (s < 0) { const idx = g.index.array; for (let i = 0; i < idx.length; i += 3) { const t = idx[i + 1]; idx[i + 1] = idx[i + 2]; idx[i + 2] = t; } g.computeVertexNormals(); }
      const m = new THREE.Mesh(g, mat); m.castShadow = true; m.receiveShadow = true; bone.add(m);
      return m;
    };
    return [seg(W.humerus, SKEL.humerus, 0.03, 0.02, 0.0), seg(W.ulna, SKEL.ulna, 0.022, 0.014, 0.006), seg(W.hand, SKEL.hand + SKEL.digit, 0.013, 0.007, 0.0)];
  }

  // fold drivers from the actual joint angles
  _fold(wi) {
    const W = this.rig.wings[wi];
    const e = 2 * Math.acos(clamp(Math.abs(W.ulna.quaternion.w), 0, 1));
    const w = 2 * Math.acos(clamp(Math.abs(W.hand.quaternion.w), 0, 1));
    // extended pose ~ elbow 0.35 / wrist 0.3; folded ~ elbow 2.6 / wrist 2.7
    return { elbow: sstep(0.35, 2.8, e), wrist: sstep(0.14, 2.8, w) };
  }

  update() {
    const f = [this._fold(0), this._fold(1)];
    this.foldAmount = f;
    for (const it of this.items) {
      const W = this.rig.wings[it.wi], s = W.s, P = this.params[it.wi];
      const fd = it.drive === 'elbow' ? f[it.wi].elbow : it.drive === 'wrist' ? f[it.wi].wrist : 0;
      let th = lerp(it.thE, it.thF, fd);
      let roll = 0, pitch = 0, bend = it.bend;
      if (it.group === 'prim') {
        const kk = it.k / 9;
        // spread (fan out further) and slotting on the upstroke / glide: outer primaries separate
        th += P.spread * (0.5 - kk) * 0.25 * (1 - fd);
        // supination: primaries rotate about their shafts (nose-up) to open the slots on the upstroke
        roll = P.supinate * (0.15 + 0.6 * sstep(0.4, 1, kk)) * (1 - fd);
        // aerodynamic load bends the feather tips upward (downstroke / glide)
        bend = it.bend + P.primBend * (0.4 + 0.8 * kk) * (1 - fd);
        // emarginated tips curl upward in gliding flight (vertical separation of the fingers)
        pitch = P.tipCurl * sstep(0.5, 1, kk) * 0.18 * (1 - fd);
      } else if (it.group === 'sec' || it.group === 'ter') {
        bend = it.bend + P.secBend * (1 - fd);
      }
      // rachis direction in the bone frame: from outward (+x) towards the trailing edge
      const cs = Math.cos(th), sn = Math.sin(th);
      _z.set(cs, 0, -s * sn);
      _y.set(0, it.ventral ? -1 : 1, 0);
      if (pitch) { _y.addScaledVector(_z, -pitch).normalize(); _z.addScaledVector(_y, pitch).normalize(); }
      _x.crossVectors(_y, _z).normalize();
      _y.crossVectors(_z, _x).normalize();
      if (roll) { _q.setFromAxisAngle(_z, -roll * s); _x.applyQuaternion(_q); _y.applyQuaternion(_q); }
      _m.makeBasis(_x, _y, _z);
      _p.set(it.anchor[0], it.anchor[1] + it.y, it.anchor[2] * s);
      _m.setPosition(_p);
      _m.scale(_s.set(it.wid, it.len, it.len));
      _m.premultiply(it.bone.matrixWorld);
      this.sys.setMatrix(it.id, _m);
      if (bend !== it._bend) { this.sys.setBend(it.id, bend); it._bend = bend; }
      if (it.group === 'cov' || it.group === 'hcov' || it.group === 'pcov') this.sys.setLift(it.id, P.covertLift * (it.ruffle ?? 0.5));
    }
  }
}
