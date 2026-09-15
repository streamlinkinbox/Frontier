import * as THREE from 'three';

// Verlet tail simulation: the heavy tail follows the pelvis with lag,
// sway and ground collision. Targets come from an analytic gait wave;
// the sim adds overlap/secondary motion like a game physics asset.

const TAU = Math.PI * 2;

export class TailSim {
  constructor(lens) {
    this.lens = lens;
    this.n = lens.length + 1;
    this.pos = [];
    this.prev = [];
    this.tgt = [];
    for (let i = 0; i < this.n; i++) {
      this.pos.push(new THREE.Vector3());
      this.prev.push(new THREE.Vector3());
      this.tgt.push(new THREE.Vector3());
    }
    // target-follow strength, base -> tip
    this.follow = [1, 0.55, 0.3, 0.18, 0.12, 0.09, 0.07, 0.05, 0.04];
    this.droop = [-0.03, -0.05, -0.06, -0.06, -0.05, -0.03, -0.01, 0.01];
    this.initialized = false;
    this._d = new THREE.Vector3();
    this._e = new THREE.Euler();
    this._q = new THREE.Quaternion();
    this._v = new THREE.Vector3();
  }

  reset(basePos, baseQuat) {
    this._d.set(0, 0.1, -1).normalize().applyQuaternion(baseQuat);
    this.tgt[0].copy(basePos);
    for (let i = 0; i < this.lens.length; i++) {
      this.tgt[i + 1].copy(this.tgt[i]).addScaledVector(this._d, this.lens[i]);
    }
    for (let i = 0; i < this.n; i++) {
      this.pos[i].copy(this.tgt[i]);
      this.prev[i].copy(this.tgt[i]);
    }
    this.initialized = true;
  }

  update(dt, basePos, baseQuat, phase, time, prm) {
    if (!this.initialized) this.reset(basePos, baseQuat);
    const w = prm.walkBlend;

    // --- analytic targets: gait wave down the tail ---
    this._d.set(0, 0.1, -1).normalize().applyQuaternion(baseQuat);
    this.tgt[0].copy(basePos);
    for (let i = 0; i < this.lens.length; i++) {
      const amp = 0.03 + i * 0.012;
      const yaw = w * amp * Math.sin(TAU * phase - i * 0.55 - 0.31)
        + (1 - w * 0.5) * 0.025 * Math.sin(0.9 * time - i * 0.6);
      const pitch = this.droop[i] + w * 0.02 * Math.sin(2 * TAU * phase + 2.14 - i * 0.4);
      this._e.set(pitch, yaw, 0, 'YXZ');
      this._q.setFromEuler(this._e);
      this._d.applyQuaternion(this._q).normalize();
      this.tgt[i + 1].copy(this.tgt[i]).addScaledVector(this._d, this.lens[i]);
    }

    if (!prm.enabled) {
      // stiff FK comparison mode
      const k = 1 - Math.exp(-10 * dt);
      for (let i = 0; i < this.n; i++) {
        this.pos[i].lerp(this.tgt[i], k);
        this.prev[i].copy(this.pos[i]);
      }
      return this.pos;
    }

    const damp = prm.damp;
    const grav = prm.gravity;
    const stiff = prm.stiff;
    const sub = 2;
    const h = Math.min(dt, 1 / 30) / sub;

    for (let s = 0; s < sub; s++) {
      // integrate
      for (let i = 1; i < this.n; i++) {
        const p = this.pos[i], pp = this.prev[i];
        this._v.copy(p).sub(pp).multiplyScalar(damp);
        pp.copy(p);
        p.add(this._v);
        p.y -= grav * h * h;
      }
      this.pos[0].copy(basePos);
      // pull toward targets (strong at base, loose at tip)
      for (let i = 1; i < this.n; i++) {
        this.pos[i].lerp(this.tgt[i], Math.min(1, this.follow[i] * stiff));
      }
      // distance constraints
      for (let it = 0; it < 3; it++) {
        for (let i = 0; i < this.lens.length; i++) {
          const a = this.pos[i], b = this.pos[i + 1];
          this._v.copy(b).sub(a);
          const d = this._v.length() || 1e-6;
          const diff = (d - this.lens[i]) / d;
          if (i === 0) {
            b.addScaledVector(this._v, -diff);
          } else {
            a.addScaledVector(this._v, diff * 0.5);
            b.addScaledVector(this._v, -diff * 0.5);
          }
        }
        this.pos[0].copy(basePos);
      }
      // ground collision + friction
      for (let i = 1; i < this.n; i++) {
        const p = this.pos[i], pp = this.prev[i];
        if (p.y < 0.35) {
          p.y = 0.35;
          pp.x += (p.x - pp.x) * 0.6;
          pp.z += (p.z - pp.z) * 0.6;
          pp.y = p.y;
        }
      }
    }
    return this.pos;
  }
}
