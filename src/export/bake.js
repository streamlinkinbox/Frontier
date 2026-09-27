/**
 * GLB animation baking.
 *
 * Samples the three live systems — walking, flight and the blood meal — into
 * real `AnimationClip`s, one position curve and one rotation curve per joint.
 * The same code path runs in the app and in `tools/verify.mjs`, so the export
 * is covered by the headless suite rather than only by clicking the button.
 *
 * Every node in the rig has a unique name (see `Rig.joint` in build.js) because
 * a glTF animation track is addressed by object name.
 */

import * as THREE from 'three';
import { GAIT } from '../mosquito/anatomy.js';

export class ClipBaker {
  constructor({ mosq, gait, flight, feeding }) {
    this.mosq = mosq;
    this.gait = gait;
    this.flight = flight;
    this.feeding = feeding;
    this._a = new THREE.Vector3();
    this._b = new THREE.Vector3();
    this._tracks = new Map();
    this._t = 0;
  }

  _legNodes(L) { return [L.root, L.hip, L.knee, L.tibia, ...L.tarsus, L.pretarsus]; }

  _wingNodes(side) { return [this.mosq.wings[side].stroke, this.mosq.wings[side].pitch]; }

  /** Append one node's local transform to its tracks. */
  _sample(node) {
    const p = node.position, q = node.quaternion;
    let t = this._tracks.get(node);
    if (!t) { t = { node, times: [], pos: [], quat: [] }; this._tracks.set(node, t); }
    t.times.push(this._t);
    t.pos.push(p.x, p.y, p.z);
    t.quat.push(q.x, q.y, q.z, q.w);
  }

  _reset() { this._tracks.clear(); this._t = 0; }

  _clip(name, duration) {
    const trs = [];
    for (const { node, times, pos, quat } of this._tracks.values()) {
      const n = times.length;
      // A track carries exactly one value per keyframe; skip a node that was
      // sampled on a different number of frames than the clock ticked.
      if (pos.length < n * 3 || quat.length < n * 4) continue;
      trs.push(new THREE.VectorKeyframeTrack(`${node.name}.position`, times.slice(), pos.slice(0, n * 3)));
      trs.push(new THREE.QuaternionKeyframeTrack(`${node.name}.quaternion`, times.slice(), quat.slice(0, n * 4)));
    }
    return new THREE.AnimationClip(name, duration, trs);
  }

  /** Two strides of alternating tripod on level ground. */
  bakeWalk() {
    const { mosq, gait } = this;
    const names = Object.keys(mosq.legs);
    const steps = 120;
    const dur = 2 / Math.max(1, GAIT.strideFrequency * 1.4);
    const dt = dur / steps;
    const save = {
      pos: mosq.thorax.position.clone(), quat: mosq.thorax.quaternion.clone(),
      phase: gait.phase, speed: gait.speed, body: gait.bodyPos.clone(),
    };
    this._reset();
    for (let i = 0; i <= steps; i++) {
      this._t = i * dt;
      gait.speed = 16;
      gait.update(dt, { speed: 16 });
      mosq.root.updateWorldMatrix(true, true);
      for (const key of names) for (const n of this._legNodes(mosq.legs[key])) this._sample(n);
      for (const side of ['L', 'R']) for (const n of this._wingNodes(side)) this._sample(n);
    }
    const clip = this._clip('walk_tripod', dur);
    mosq.thorax.position.copy(save.pos); mosq.thorax.quaternion.copy(save.quat);
    gait.phase = save.phase; gait.speed = save.speed; gait.bodyPos.copy(save.body);
    this._reset();
    return clip;
  }

  /** Free flight: wing stroke plus the legs folded into the flight tuck. */
  bakeFlight() {
    const { mosq, flight, gait } = this;
    const names = Object.keys(mosq.legs);
    const steps = 120, dur = 0.4, f = 24;
    const startQ = mosq.thorax.quaternion.clone();
    this._reset();
    for (let i = 0; i <= steps; i++) {
      this._t = (i / steps) * dur;
      flight.update(1 / f, { onGround: false, speedNorm: 0.5 });
      for (const side of ['L', 'R']) for (const n of this._wingNodes(side)) this._sample(n);
      const th = mosq.thorax;
      const up = this._b.set(0, 1, 0).applyQuaternion(th.quaternion);
      for (const key of names) {
        const L = mosq.legs[key];
        this._a.set(
          Math.cos(L.spec.restPitch) * 0.9 - 0.55, -0.30 - L.spec.index * 0.05,
          L.spec.side * (0.30 + L.spec.index * 0.10)).applyQuaternion(th.quaternion).add(th.position);
        gait.legs[key].solver.solve(this._a, up, th, { tarsalAngle: 0.85, contact: 0 });
        for (const n of this._legNodes(L)) this._sample(n);
      }
    }
    const clip = this._clip('flight', dur);
    mosq.thorax.quaternion.copy(startQ);
    this._reset();
    return clip;
  }

  /** Probe → salivate → engorge → withdraw, at the real stage durations. */
  bakeFeeding() {
    const { mosq, feeding } = this;
    const stages = ['APPROACH', 'SHEATH', 'PROBE', 'SALIVATE', 'ENGORGE', 'WITHDRAW'];
    const DUR = { APPROACH: 0.9, SHEATH: 1.1, PROBE: 3.0, SALIVATE: 1.0, ENGORGE: 8.0, WITHDRAW: 1.0 };
    const saveStage = feeding.stage, saveT = feeding.t;
    this._reset();
    for (const s of stages) {
      feeding._set(s);
      const d = DUR[s];
      for (let i = 0; i < 24; i++) {
        this._t += d / 24;
        feeding.t = (i / 24) * d;
        feeding.update(0, this._t);
        for (const node of mosq.prob.labiumJoints) this._sample(node);
        this._sample(mosq.prob.labella);
        for (const st of mosq.prob.stylets) this._sample(st.node);
        this._sample(mosq.prob.fascRoot);
      }
    }
    const clip = this._clip('blood_meal', this._t);
    feeding._set(saveStage, { silent: true }); feeding.t = saveT;
    this._reset();
    return clip;
  }

  /** All three clips, ready to hand to GLTFExporter. */
  bakeAll() {
    return [this.bakeWalk(), this.bakeFlight(), this.bakeFeeding()];
  }
}
