/**
 * FEEDING CONTROLLER
 *
 * The blood meal, staged exactly as the literature describes it:
 *
 *   1. APPROACH   — the labella are lowered onto the host and the palps tap
 *                   the surface (they are the primary CO₂ / capillary-finder).
 *   2. SHEATH     — the labium, a flexible scaly lower lip, SLIDES BACK INTO
 *                   A LOOP and comes to rest on the skin, exposing the
 *                   fascicle. The labella stay down throughout.
 *   3. PROBE      — the six stylets are worked into the tissue. The maxillae
 *                   operate as variable-frequency microsaws at 10-15 Hz while
 *                   the whole fascicle oscillates at ~30 Hz; the maxillae and
 *                   mandibles anchor with their serrated teeth while the
 *                   labrum's food canal is steered toward a vessel.
 *   4. SALIVATE   — the hypopharynx pumps in anti-coagulant.
 *   5. ENGORGE    — the cibarial pump runs; the abdomen fills. The tergites
 *                   separate, the cuticle goes translucent and the gut shows
 *                   through dark red; the body pitches nose-up under the
 *                   weight and the hind legs splay for balance.
 *   6. WITHDRAW   — the labium sheath slides forward over the fascicle, which
 *                   is the last thing to leave the host.
 */

import * as THREE from 'three';
import { FEED, clamp, lerp, smoothstep } from '../mosquito/anatomy.js';

export const FEED_STAGES = ['IDLE', 'APPROACH', 'SHEATH', 'PROBE', 'SALIVATE', 'ENGORGE', 'WITHDRAW', 'SATIATED'];

const STAGE_TIME = {
  APPROACH: 0.9,
  SHEATH: 1.1,
  PROBE: 3.0,
  SALIVATE: 1.0,
  ENGORGE: 14.0,
  WITHDRAW: 1.0,
  SATIATED: 2.0,
};

/* Per-joint curl of the labium sheath, in degrees. The total (~340°) is what
 * turns the straight sheath into the folded-back loop described in S1/S3/S4. */
const LABIUM_CURL = [8, 28, 52, 72, 74, 58, 34, 14];
const CURL_SIGN = -1;

const _v = new THREE.Vector3(), _v2 = new THREE.Vector3(), _q = new THREE.Quaternion();

export class FeedingController {
  constructor(rigged, scene, opts = {}) {
    this.rig = rigged;
    this.prob = rigged.prob;
    this.scene = scene;
    this.stage = 'IDLE';
    this.t = 0;
    this.timeScale = opts.timeScale ?? 1;
    this.retraction = 0;
    this.insertion = 0;
    this.engorge = 0;
    this.blood = 0;
    this.headPitch = 0;
    this.salivaOut = 0;
    this.droplets = [];
    this._buildDroplets();
    this.onStageChange = opts.onStageChange || (() => { });
    this.onFinished = opts.onFinished || null;
  }

  _buildDroplets() {
    const geo = new THREE.SphereGeometry(0.016, 8, 6);
    const mat = this.rig.materials?.saliva || new THREE.MeshPhysicalMaterial({
      color: 0xdfe8e4, roughness: 0.02, transmission: 0.8, thickness: 0.02, transparent: true, opacity: 0.8,
    });
    for (let i = 0; i < 10; i++) {
      const m = new THREE.Mesh(geo, mat);
      m.visible = false;
      m.renderOrder = 4;
      this.scene.add(m);
      this.droplets.push({ mesh: m, life: 0, vel: new THREE.Vector3() });
    }
  }

  start() { this._set('APPROACH'); }
  stop() { this._set('IDLE'); }

  _set(stage, opts = {}) {
    if (this.stage === stage) return;
    const from = this.stage;
    this.stage = stage;
    this.t = 0;
    this.onStageChange(stage);
    // A blood meal is a finite event, and the caller has to hear about it
    // however it ends — SATIATED running out, or an explicit stop() — or a
    // route that ends on a tank waits forever for a callback that only fires
    // on the happy path.
    if (stage === 'IDLE' && from !== 'IDLE' && !opts.silent) this.onFinished?.();
  }

  get stageProgress() {
    const d = STAGE_TIME[this.stage];
    return d ? clamp(this.t / d, 0, 1) : 0;
  }

  /** Probability the mosquito is currently attached and feeding. */
  get attached() {
    return this.stage !== 'IDLE' && this.stage !== 'APPROACH';
  }

  /* ---------------------------------------------------------- per-frame */

  update(dt, t) {
    const scaled = dt * this.timeScale;
    if (STAGE_TIME[this.stage]) this.t += scaled;
    else this.t = 0;

    switch (this.stage) {
      case 'APPROACH': {
        const p = smoothstep(0, 0.8, this.stageProgress);
        this.retraction = lerp(0, 0.10, p);
        this.insertion = 0;
        this.headPitch = p * 0.85;
        this.salivaOut = 0;
        if (this.stageProgress >= 1) this._set('SHEATH');
        break;
      }
      case 'SHEATH': {
        // The sheath slides back into its loop with a slight initial resistance
        // (it is being drawn over the stylets) then runs free.
        const p = this.stageProgress;
        const e = p < 0.25 ? smoothstep(0, 0.25, p) * 0.28 : 0.28 + 0.72 * smoothstep(0.25, 1, p);
        this.retraction = lerp(0.10, 1.0, e);
        this.insertion = lerp(0, 0.25, smoothstep(0.55, 1, p));
        this.headPitch = lerp(0.85, 1.0, smoothstep(0, 0.5, p));
        this.salivaOut = 0;
        if (p >= 1) this._set('PROBE');
        break;
      }
      case 'PROBE': {
        this.retraction = 1;
        // The stylets hunt: the fascicle advances in short strokes as the
        // maxillae saw, with pauses while the palps re-sense the surface.
        const p = this.stageProgress;
        const saw = 0.5 + 0.5 * Math.sin(t * 2 * Math.PI * FEED.sawHz);
        const hunt = 0.5 + 0.5 * Math.sin(t * 2 * Math.PI * 0.55);
        this.insertion = lerp(0.25, FEED.penetration, smoothstep(0, 0.7, p) * (0.55 + 0.45 * saw));
        this.headPitch = 1 + 0.05 * Math.sin(t * 2 * Math.PI * 0.9);
        this.salivaOut = smoothstep(0.45, 1, p) * 0.8;
        if (p >= 1) this._set('SALIVATE');
        break;
      }
      case 'SALIVATE': {
        this.retraction = 1;
        this.insertion = FEED.penetration * (1 - 0.10 * this.stageProgress);
        this.salivaOut = 1 - 0.4 * this.stageProgress;
        this.headPitch = 1;
        if (this.stageProgress >= 1) this._set('ENGORGE');
        break;
      }
      case 'ENGORGE': {
        this.retraction = 1;
        this.insertion = FEED.penetration * (0.90 + 0.10 * Math.sin(t * 2 * Math.PI * 0.4));
        this.salivaOut = 0.22;
        this.headPitch = 1;
        // Engorgement saturates; it is not linear
        const p = this.stageProgress;
        this.engorge = Math.pow(p, 0.72);
        this.blood = smoothstep(0.02, 0.35, p);
        if (p >= 1) this._set('WITHDRAW');
        break;
      }
      case 'WITHDRAW': {
        this.retraction = 1 - smoothstep(0, 0.55, this.stageProgress);
        this.insertion = FEED.penetration * (1 - smoothstep(0.2, 0.8, this.stageProgress));
        this.salivaOut = 0;
        this.headPitch = 1 - 0.4 * smoothstep(0.5, 1, this.stageProgress);
        if (this.stageProgress >= 1) this._set('SATIATED');
        break;
      }
      case 'SATIATED': {
        this.retraction = 0;
        this.insertion = 0;
        this.engorge = 1 - 0.25 * smoothstep(0.3, 1, this.stageProgress);
        this.blood = Math.max(0, 1 - smoothstep(0.2, 0.9, this.stageProgress));
        this.headPitch = lerp(0.6, 0, smoothstep(0, 0.5, this.stageProgress));
        if (this.stageProgress >= 1) this._set('IDLE');
        break;
      }
      default: {
        this.retraction = 0; this.insertion = 0; this.engorge = 0;
        this.blood = 0; this.headPitch = 0; this.salivaOut = 0;
      }
    }

    this._applyProboscis(dt, t);
    this._applyDroplets(dt);
  }

  _applyProboscis(dt, t) {
    const p = this.prob;
    const r = this.retraction;

    // --- Labium sheath: fold into the loop ---------------------------
    for (let i = 0; i < p.labiumJoints.length; i++) {
      const j = p.labiumJoints[i];
      const curl = LABIUM_CURL[i] * Math.PI / 180 * CURL_SIGN * r;
      // A small lateral wobble keeps the loop from being a perfect 2-D curve
      const wob = Math.sin(i * 1.7 + t * 1.3) * 0.02 * r;
      j.rotation.set(0, 0, 0);
      j.rotateZ(curl);
      j.rotateY(wob);
      // The whole sheath also lifts slightly as it is drawn back
      j.rotateX(-0.10 * r * (i / p.labiumJoints.length));
    }

    // --- Labella stay on the skin, splayed slightly -------------------
    p.labella.rotation.set(0, 0, 0);
    p.labella.rotateZ(-0.55 * r);
    p.labella.rotateY(Math.sin(t * 2.1) * 0.05 * r);

    // --- Fascicle: insertion + the 30 Hz oscillation ------------------
    p.fascRoot.position.set(0, -this.insertion, 0);
    const vib = Math.sin(t * 2 * Math.PI * FEED.fascicleHz);
    for (const st of p.stylets) {
      const saw = Math.sin(t * 2 * Math.PI * FEED.sawHz + st.phase);
      if (st.id.startsWith('max')) {
        // Maxillae: variable-frequency microsaws, sawing about their long axis
        st.node.rotation.set(0, 0, 0);
        st.node.rotateY(saw * 0.22);
        st.node.rotateX(vib * 0.05);
      } else if (st.id.startsWith('mand')) {
        // Mandibles: pivot to saw with a smaller, faster motion
        st.node.rotation.set(0, 0, 0);
        st.node.rotateY(-saw * 0.14);
      } else if (st.id === 'labrum') {
        // The food canal is steered, searching for the vessel
        st.node.rotation.set(0, 0, 0);
        st.node.rotateZ(Math.sin(t * 2 * Math.PI * 0.7 + 1.1) * 0.10);
        st.node.rotateX(vib * 0.035);
      } else {
        st.node.rotation.set(0, 0, 0);
        st.node.rotateZ(Math.sin(t * 2 * Math.PI * 0.9) * 0.06);
      }
    }

    // --- Cibarial pump: the suction strokes ---------------------------
    this.pump = 0.5 + 0.5 * Math.sin(t * 2 * Math.PI * 4.2);
    const cib = this.rig.prob.root.getObjectByName('CibarialPump');
    if (cib) {
      const s = 1 + 0.10 * this.pump * this.salivaOut + 0.05 * this.pump * (this.engorge > 0 ? 1 : 0);
      cib.scale.set(s, 1 / s, s);
    }
  }

  _applyDroplets(dt) {
    // Saliva is delivered from the hypopharynx; droplets bead and fall.
    let want = 0;
    if (this.salivaOut > 0.05) {
      want = Math.min(this.droplets.length, Math.round(this.salivaOut * FEED.salivationDrops));
      this._emit = (this._emit || 0) + this.salivaOut * dt * 6;
    }
    for (let i = 0; i < this.droplets.length; i++) {
      const d = this.droplets[i];
      if (d.life <= 0) {
        if (i < want && this._emit > 1) {
          this._emit -= 1;
          const hypo = this.prob.stylets.find((s) => s.id === 'hypopharynx');
          const tip = hypo.node.localToWorld(_v.set(0, this.prob.segLen * 8 - this.insertion, 0));
          d.mesh.position.copy(tip);
          const scale = 0.4 + Math.random() * 0.8;
          d.mesh.scale.setScalar(scale);
          d.vel.set((Math.random() - 0.5) * 0.04, -0.05, (Math.random() - 0.5) * 0.04);
          d.life = 1.6;
          d.mesh.visible = true;
        } else continue;
      }
      d.life -= dt;
      d.vel.y -= 1.2 * dt;          // g at scene scale
      d.mesh.position.addScaledVector(d.vel, dt);
      // Stick to whatever it lands on: just fade it in place
      d.mesh.scale.setScalar(d.mesh.scale.x * (1 - dt * 0.2));
      if (d.life <= 0) d.mesh.visible = false;
    }
  }
}
