import * as THREE from 'three';
import { Rig } from './Rig.js';
import { Body } from './Body.js';
import { createFeatherAtlas, FeatherSystem } from './Feathers.js';
import { Wings } from './Wing.js';
import { Legs } from './Legs.js';
import { Tail } from './Tail.js';
import { COLOR } from './anatomy.js';

const _m = new THREE.Matrix4(), _m2 = new THREE.Matrix4(), _m3 = new THREE.Matrix4();

// Feathers skinned to the core skeleton: linear blend of the two bone deformation matrices
export class SkinnedFeathers {
  constructor(sys) { this.sys = sys; this.list = []; }
  add(id, bind, bones, weights) { this.list.push({ id, bind, bones, weights }); }
  update(rig) {
    const def = new Map();
    const get = (b) => {
      let m = def.get(b);
      if (!m) { m = new THREE.Matrix4().multiplyMatrices(b.matrixWorld, rig.bindInv.get(b)); def.set(b, m); }
      return m;
    };
    const e = _m.elements, a = _m2.elements;
    for (const f of this.list) {
      const w0 = f.weights[0], w1 = f.weights[1];
      if (w1 < 1e-3) { _m.multiplyMatrices(get(f.bones[0]), f.bind); }
      else {
        const A = get(f.bones[0]).elements, B = get(f.bones[1]).elements;
        for (let k = 0; k < 16; k++) a[k] = A[k] * w0 + B[k] * w1;
        _m.multiplyMatrices(_m2, f.bind);
      }
      this.sys.setMatrix(f.id, _m);
    }
  }
}

export class Eagle {
  constructor({ quality = 'high' } = {}) {
    this.group = new THREE.Group();
    this.rig = new Rig();
    this.rig.bindInv = new Map();
    for (const [b, m] of this.rig.bindWorld) this.rig.bindInv.set(b, m.clone().invert());
    this.group.add(this.rig.root);
    this.body = new Body(this.rig);
    this.rig.root.add(this.body.group);

    this.atlas = createFeatherAtlas(quality === 'low' ? 1024 : 2048);
    this.feathers = new FeatherSystem(this.atlas, 9000);
    this.group.add(this.feathers.mesh);
    this.skinned = new SkinnedFeathers(this.feathers);
    this._bodyPlumage();
    this._scapulars();

    this.wings = new Wings(this.rig, this.feathers);
    this.legs = new Legs(this.rig, this.feathers, this.skinned, this.body);
    this.tail = new Tail(this.rig, this.feathers, this.skinned, this.body);

    this.update(0, 0);
  }

  _bodyPlumage() {
    const B = this.body, brown = new THREE.Color(COLOR.brown), white = new THREE.Color(COLOR.white), wS = new THREE.Color(COLOR.whiteShade);
    const anchors = B.sampleFeathers(11);
    // draw the smallest (deepest) rows first; order does not matter for alpha-tested cards
    const c = new THREE.Color();
    for (const a of anchors) {
      const y = a.n.clone(), z = a.flow.clone(), x = new THREE.Vector3().crossVectors(y, z).normalize();
      z.crossVectors(x, y).normalize();
      const pos = a.p.clone().addScaledVector(a.n, -0.0015);
      const bind = new THREE.Matrix4().makeBasis(x, y, z).setPosition(pos);
      bind.scale(new THREE.Vector3(a.wid, a.len, a.len));
      if (a.white) c.copy(white).lerp(wS, Math.random() * 0.5).multiplyScalar(1.6 + (Math.random() - 0.5) * 0.1);
      else c.copy(brown).offsetHSL((Math.random() - 0.5) * 0.02, (Math.random() - 0.5) * 0.08, (Math.random() - 0.5) * 0.04).multiplyScalar(1.5);
      const id = this.feathers.add({ type: a.type, variant: Math.floor(a.seed * 97), color: c, bend: -0.06 - 0.04 * a.seed, camber: 0.05, flutter: a.type === 'lance' ? 0.03 : 0.015, seed: a.seed, lift: a.lift, ruffle: a.ruffle, ao: a.white ? 0.72 : 0.55 });
      const w = B.weightsAt(a.p.z);
      this.skinned.add(id, bind, [w[0][0], w[1][0]], [w[0][1], w[1][1]]);
    }
  }

  // Scapulars: long feathers of the humeral tract on each side of the back; they overlie the base of the
  // folded wing and bridge the back to the wing coverts.
  _scapulars() {
    const brown = new THREE.Color(COLOR.brown), r = this.rig;
    for (const s of [1, -1]) for (let row = 0; row < 3; row++) for (let k = 0; k < 7; k++) {
      const u = k / 6;
      const x = s * (0.035 + row * 0.022), z = 0.18 - u * 0.2, y = 0.055 - row * 0.008 - 0.02 * u * u;
      const p = new THREE.Vector3(x, y + 0.004, z);
      const n = new THREE.Vector3(s * (0.25 + row * 0.25), 1, 0).normalize();
      const f = new THREE.Vector3(s * 0.12, -0.06, -1).normalize();
      const X = new THREE.Vector3().crossVectors(n, f).normalize(); const Z = new THREE.Vector3().crossVectors(X, n);
      const len = 0.13 + 0.03 * u - row * 0.015;
      const bind = new THREE.Matrix4().makeBasis(X, n, Z).setPosition(p).scale(new THREE.Vector3(0.068, len, len));
      const c = brown.clone().offsetHSL(0, (Math.random() - 0.5) * 0.06, (Math.random() - 0.5) * 0.03).multiplyScalar(1.5);
      const id = this.feathers.add({ type: 'covert', variant: k + row, color: c, bend: -0.05, camber: 0.05, flutter: 0.02, seed: Math.random(), lift: 0.08, ruffle: 0.6, ao: 0.6, mirror: s < 0 });
      this.skinned.add(id, bind, [r.thorax, r.pelvis], [1 - u * 0.5, u * 0.5]);
    }
  }

  update(dt, t) {
    this.rig.root.updateMatrixWorld(true);
    this.skinned.update(this.rig);
    this.wings.update(dt, t);
    this.legs.update(dt, t);
    this.tail.update(dt, t);
    this.feathers.uniforms.uTime.value = t;
    this.feathers.commit();
  }
}
