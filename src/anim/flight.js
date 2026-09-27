/**
 * FLIGHT CONTROLLER
 *
 * Mosquito aerodynamics are the opposite of a hummingbird's. From Bomphrey et
 * al. (Nature 2017) and the mosquito aero reviews:
 *
 *   • Wingbeat 450–500 Hz for a female Aedes/Culex (measured; 800+ Hz in
 *     free flight), stroke amplitude only ~44° total, aspect ratio 4.2, Re≈120.
 *   • Lift comes mostly from the *pitch rotation* of the wing at stroke
 *     reversal (pronation/supination) plus wake-capture on the trailing-edge
 *     vortex — not from the translation, which is why such a shallow stroke
 *     still holds the insect up.
 *   • The reversal flip is fast but lagged: the pitch rotation occupies ~6 %
 *     of the cycle and happens while the wing is nearly stationary.
 *   • The wing is flexible; the tip lags the root in phase (this is a real,
 *     measurable effect on these high-aspect-ratio panels).
 *
 * A 500 Hz wingbeat cannot be shown at 60 fps. Rather than cheat by slowing
 * the animation down, the wing is sampled at the virtual shutter time — the
 * same strobed appearance a 10 000 fps camera records — and several
 * sub-exposures are composited so the blur streak is physically shaped.
 */

import * as THREE from 'three';
import { FLIGHT, clamp, lerp, smoothstep } from '../mosquito/anatomy.js';

const _v = new THREE.Vector3();

export class FlightController {
  constructor(rigged, opts = {}) {
    this.rig = rigged;
    this.wings = rigged.wings;
    this.phase = 0;
    this.frequency = FLIGHT.frequencyHover;
    this.targetFrequency = FLIGHT.frequencyHover;
    this.stroke = 0;              // 0..1 within the current cycle
    this.advanceRatio = 0;
    this.speed = 0;
    this.shutter = 1 / 240;        // s — the "camera" exposure
    this.ghosts = 3;               // sub-exposures composited per wing
    this.ghostNodes = { L: [], R: [] };
    this.bodyPitch = FLIGHT.bodyPitchHover;
    this.bodyRoll = 0;
    this.bodyYaw = 0;
    this.pitchAngle = 0;
    this.amplitude = FLIGHT.sweepAmplitude;
    this.wingLoad = 0;
    this.onGround = true;
    this.thrust = 0;
  }

  /** Build the sub-exposure ghost copies used to render the blur streak. */
  buildGhosts(scene, material) {
    for (const side of ['L', 'R']) {
      const src = this.wings[side];
      for (let i = 1; i < this.ghosts; i++) {
        const g = src.pitch.clone(true);
        g.name = `WingGhost_${side}_${i}`;
        g.traverse((o) => { if (o.isMesh) { o.material = material; o.castShadow = false; o.receiveShadow = false; o.renderOrder = 2; } });
        scene.add(g);
        this.ghostNodes[side].push(g);
      }
    }
  }

  setGhosts(n) {
    this.ghosts = Math.max(1, Math.min(6, n | 0));
    for (const side of ['L', 'R']) {
      for (let i = 1; i < this.ghostNodes[side].length; i++) {
        this.ghostNodes[side][i - 1].visible = i < this.ghosts;
      }
    }
  }

  /* ------------------------------------------------- analytic kinematics */

  /**
   * Wing state at an absolute time. Everything is a pure function of t so it
   * can be sampled at any shutter time without integration error.
   * @returns {{sweep:number, pitch:number, twist:number, dvdt:number}}
   */
  sampleAt(t) {
    const f = this.frequency;
    const ph = (2 * Math.PI * f * t + this.phase) % (2 * Math.PI);
    const s = Math.sin(ph);
    // Sharpen the stroke: quick through the middle of the half-stroke, slow
    // at reversal — the mosquito's signature stroke shape.
    const shape = Math.sign(s) * Math.pow(Math.abs(s), 1.35);
    const sweep = shape * this.amplitude;

    // Pitch (angle of attack). Flips at each stroke reversal over ~6 % of the
    // cycle, centred a little after the reversal — the measured lag.
    // k = 7.0 puts the -0.9 → +0.9 transition across ~6.5 % of the cycle
    // (2·asin(atanh(0.9)/k)/2π), matching the measured reversal duration.
    const flip = Math.tanh(7.0 * Math.sin(ph - 0.12));
    const pitch = flip * FLIGHT.angleOfAttack;

    // Spanwise velocity drives the dynamic twist (S6: the flexible panel
    // twists in proportion to its angular velocity).
    const dphi = 2 * Math.PI * f;
    const dvdt = Math.cos(ph) * Math.abs(Math.cos(ph)) ** 0.35 * 1.35 * dphi;
    const twist = -clamp(dvdt * FLIGHT.dvdtLag, -1, 1) * FLIGHT.dvdtGain;

    return { sweep, pitch, twist, dvdt, ph };
  }

  /**
   * Pose a wing node group for a given sample.
   * `node` is the pitch node (whose parent is the stroke node).
   */
  applyWing(side, t) {
    const s = this.sampleAt(t);
    const w = this.wings[side];
    // Stroke: rotation about the hinge axis. The hinge is raised and swept,
    // so the stroke plane sits above the body and is inclined (S6: 28°).
    w.stroke.rotation.set(0, 0, 0);
    w.stroke.rotation.z = s.sweep;
    w.stroke.rotation.x = FLIGHT.strokePlaneTilt * 0.55;
    // Pitch about the wing's own long axis
    w.pitch.rotation.set(0, 0, 0);
    w.pitch.rotateX(-s.pitch);
    // Wing bending: the tip lags the root in phase (S6)
    w.pitch.rotation.y = Math.sin((s.ph - FLIGHT.tipLagPhase)) * FLIGHT.tipLag * Math.sign(s.dvdt || 1);
    // Twist is applied per-vertex in the shader (spanwise), so we only carry
    // the root value here.
    if (w.membrane.material.userData) w.membrane.material.userData.twist = s.twist;
    if (w.membrane.material.userData) w.membrane.material.userData.needsUpdate = false;
    return s;
  }

  /* ------------------------------------------------------------ update */

  update(dt, ctx = {}) {
    const { onGround = false, targetSpeed = 0, speedNorm = 0, climbing = 0, turning = 0 } = ctx;

    // Wingbeat frequency: mosquitoes modulate 450→800 Hz with load and
    // manoeuvring. (S9: up to 50 Hz change within a single flight.)
    const load = 0.55 + 0.45 * speedNorm;
    const base = lerp(FLIGHT.frequencyHover, FLIGHT.frequencyCruise, speedNorm);
    this.targetFrequency = clamp(base * (0.92 + 0.16 * load) + climbing * 40, FLIGHT.frequencyMin, FLIGHT.frequencyMax);
    // Frequency changes themselves are gradual — the resonant thorax has inertia.
    this.frequency += (this.targetFrequency - this.frequency) * Math.min(1, dt * 2.2);

    // Ground effect: near the substrate the stroke amplitude increases
    const targetAmp = FLIGHT.sweepAmplitude * (onGround ? 1.22 : 1.0);
    this.amplitude += (targetAmp - this.amplitude) * Math.min(1, dt * 4);

    this.phase = (this.phase + 2 * Math.PI * this.frequency * dt) % (2 * Math.PI);
    this.stroke = (this.stroke + this.frequency * dt) % 1;

    // Body attitude
    const targetPitch = FLIGHT.bodyPitchHover - speedNorm * 6 * Math.PI / 180 + climbing * 0.18;
    this.bodyPitch += (targetPitch - this.bodyPitch) * Math.min(1, dt * 5);
    this.bodyRoll += ((-turning * 0.42) - this.bodyRoll) * Math.min(1, dt * 4);
    this.bodyYaw += turning * dt * 0.8;

    // Pose the real wings at the shutter instant, and the ghosts at
    // sub-exposure offsets so the streak has the correct shape and length.
    const tNow = performance.now() / 1000;
    for (const side of ['L', 'R']) {
      this.applyWing(side, tNow - this.shutter * 0.5);
      const g = this.ghostNodes[side];
      for (let i = 0; i < g.length; i++) {
        if (i + 1 >= this.ghosts) { g[i].visible = false; continue; }
        g[i].visible = true;
        const dt2 = -this.shutter * ((i + 1) / this.ghosts - 0.5);
        this._poseGhost(g[i], this.wings[side], tNow + dt2, side);
      }
    }
  }

  _poseGhost(node, src, t, side) {
    // Copy the wing node transforms at a different shutter sub-time
    const s = this.sampleAt(t);
    node.rotation.copy(src.pitch.rotation);
    const parent = src.stroke.parent;
    if (parent) {
      // Reproduce the stroke rotation in the ghost's own space
      const ws = new THREE.Vector3(), wp = new THREE.Vector3();
      src.stroke.getWorldPosition(ws);
      src.stroke.parent.getWorldPosition(wp);
      node.position.copy(src.pitch.position);
      node.rotation.set(0, 0, 0);
      node.rotateZ(s.sweep);
      node.rotateX(FLIGHT.strokePlaneTilt * 0.55);
      node.rotateY(side * 0.28);
      node.rotateX(-0.10);
      node.rotateX(-s.pitch);
      node.rotateY(Math.sin((s.ph - FLIGHT.tipLagPhase)) * FLIGHT.tipLag * Math.sign(s.dvdt || 1));
    }
  }

  /** Stall / knockdown at very high advance ratio (S5: high wing loading). */
  knockdown(advance) {
    return smoothstep(0.9, 1.8, advance) * 0.6;
  }
}
