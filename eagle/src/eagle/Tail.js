import * as THREE from 'three';
import { COLOR } from './anatomy.js';
import { RigidFeathers } from './Feathers.js';

const lerp = (a, b, t) => a + (b - a) * t;
const _m = new THREE.Matrix4(), _x = new THREE.Vector3(), _y = new THREE.Vector3(), _z = new THREE.Vector3(), _p = new THREE.Vector3(), _s = new THREE.Vector3();

// Tail: 12 rectrices on the pygostyle (tail bone, whose local -z points backward).
// Bald eagle: white, moderately long, slightly wedge-shaped (central rectrices longest).
// Dorsal stacking: the central pair lies on top, each outer pair beneath the previous one.
export class Tail {
  constructor(rig, sys) {
    this.rig = rig; this.sys = sys;
    this.rigid = new RigidFeathers(sys);
    this.spread = 0.2;          // 0 closed .. 1 fully fanned
    this.bend = 0;              // aerodynamic load (tips up, +)
    this.rects = [];
    const white = new THREE.Color(COLOR.white);
    for (let i = 0; i < 12; i++) {
      const side = i < 6 ? 1 : -1;
      const k = i % 6;              // 0 central .. 5 outermost
      const len = 0.325 - 0.0065 * k * k * 0.9;
      const c = white.clone().multiplyScalar(1.55 + (Math.random() - 0.5) * 0.06);
      const id = sys.add({ type: 'rectrix', variant: i, color: c, bend: -0.02, camber: 0.04, flutter: 0.012, seed: Math.random(), lift: 0, ruffle: 0, ao: 0.75, mirror: side < 0 });
      this.rects.push({ id, side, k, len });
    }
    // upper tail coverts (white, long) and undertail coverts (white, fluffy)
    this.coverts = [];
    for (let row = 0; row < 3; row++) for (let i = 0; i < 10; i++) {
      const side = i < 5 ? 1 : -1, k = i % 5;
      const c = white.clone().multiplyScalar(1.5 + (Math.random() - 0.5) * 0.06);
      const dorsal = row < 2;
      const id = sys.add({ type: dorsal ? 'covert' : 'fluff', variant: i + row * 3, color: c, bend: dorsal ? -0.04 : 0.05, camber: 0.05, flutter: 0.02, seed: Math.random(), lift: 0.05, ruffle: 0.6, ao: 0.7, mirror: side < 0 });
      this.coverts.push({ id, side, k, row, dorsal });
    }
    this.update();
  }

  update() {
    const tb = this.rig.tail;
    const fan = lerp(0.075, 0.5, this.spread);   // half-angle of the outermost rectrix
    for (const r of this.rects) {
      const a = r.side * fan * (r.k + 0.5) / 5.5;
      _z.set(Math.sin(a), 0, -Math.cos(a));
      _y.set(0, 1, 0);
      _x.crossVectors(_y, _z).normalize();
      _m.makeBasis(_x, _y, _z);
      // anchor along the pygostyle margin, outer feathers slightly lower & further out
      _p.set(r.side * (0.006 + r.k * 0.0055), 0.004 - r.k * 0.0016, 0.012 - r.k * 0.002);
      _m.setPosition(_p);
      _m.scale(_s.set(0.074, r.len, r.len));
      _m.premultiply(tb.matrixWorld);
      this.sys.setMatrix(r.id, _m);
      this.sys.setBend(r.id, -0.02 + this.bend * (0.6 + 0.4 * r.k / 5));
    }
    for (const c of this.coverts) {
      const a = c.side * lerp(0.08, 0.4, this.spread) * (c.k + 0.5) / 4.5;
      _z.set(Math.sin(a), c.dorsal ? -0.05 : 0.08, -Math.cos(a)).normalize();
      _y.set(0, c.dorsal ? 1 : -1, 0);
      _x.crossVectors(_y, _z).normalize();
      _y.crossVectors(_z, _x).normalize();
      _m.makeBasis(_x, _y, _z);
      const len = c.dorsal ? (c.row === 0 ? 0.2 : 0.13) : 0.14;
      _p.set(c.side * (0.004 + c.k * 0.008), c.dorsal ? 0.014 + (1 - c.row) * 0.006 : -0.016, c.dorsal ? 0.045 - c.row * 0.02 : 0.03);
      _m.setPosition(_p);
      _m.scale(_s.set(0.06, len, len));
      _m.premultiply(tb.matrixWorld);
      this.sys.setMatrix(c.id, _m);
    }
  }
}
