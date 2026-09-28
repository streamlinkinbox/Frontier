import * as THREE from 'three';
import { FLIGHT } from './anatomy.js';

const clamp = (x, a, b) => Math.min(Math.max(x, a), b);
const lerp = (a, b, t) => a + (b - a) * t;
const sstep = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
const TAU = Math.PI * 2;
function hash(n) { const x = Math.sin(n * 127.1 + 311.7) * 43758.5453; return x - Math.floor(x); }
function noise1(t) { const i = Math.floor(t), f = t - i, u = f * f * (3 - 2 * f); return lerp(hash(i), hash(i + 1), u) * 2 - 1; }
const _v = new THREE.Vector3(), _v2 = new THREE.Vector3(), _q = new THREE.Quaternion(), _q2 = new THREE.Quaternion(), _e = new THREE.Euler(), _m = new THREE.Matrix4();
const UP = new THREE.Vector3(0, 1, 0);

// ------------------------------------------------------------------------------------------------
// Pose: every animatable degree of freedom, expressed in the bird's own ("root") frame, so that
// poses from different behaviours can be blended numerically.
// ------------------------------------------------------------------------------------------------
function wingPose(o = {}) { return { elev: 0, sweep: 0, twist: 0, elbow: 0.35, wrist: 0.3, handTwist: 0, digit: 0, elbowTwist: 0, ...o }; }
function wingParams(o = {}) { return { primBend: 0, secBend: 0, spread: 0, supinate: 0, tipCurl: 0, covertLift: 0, ...o }; }
export function makePose() {
  return {
    pelvis: new THREE.Vector3(0, 0.3, 0), pitch: -0.75, roll: 0, yaw: 0,
    head: new THREE.Vector3(0, 0.66, 0.12),  // head position (root frame)
    look: new THREE.Vector3(0, -0.12, 0),    // head orientation: yaw, pitch (+ down), roll (root frame)
    jaw: 0, bill: 0,
    wings: [wingPose(), wingPose()], wp: [wingParams(), wingParams()],
    tail: new THREE.Vector3(0, 0, 0), tailSpread: 0.15, tailBend: 0,
    feet: [new THREE.Vector3(0.075, 0.012, 0.02), new THREE.Vector3(-0.075, 0.012, 0.02)],
    toeCurl: [0, 0], toeSpread: [1, 1], femur: -0.95,
    ruffle: 0, flutter: 0, lids: 0, nict: 0,
  };
}
function copyPose(dst, src) {
  for (const k in src) {
    const v = src[k];
    if (v && v.isVector3) dst[k].copy(v);
    else if (Array.isArray(v)) v.forEach((x, i) => { if (x && x.isVector3) dst[k][i].copy(x); else if (typeof x === 'object') Object.assign(dst[k][i], x); else dst[k][i] = x; });
    else dst[k] = v;
  }
  return dst;
}
function blendPose(dst, b, t) {   // dst = lerp(dst, b, t)
  for (const k in b) {
    const v = b[k];
    if (v && v.isVector3) dst[k].lerp(v, t);
    else if (Array.isArray(v)) v.forEach((x, i) => {
      if (x && x.isVector3) dst[k][i].lerp(x, t);
      else if (typeof x === 'object') for (const kk in x) dst[k][i][kk] = lerp(dst[k][i][kk], x[kk], t);
      else dst[k][i] = lerp(dst[k][i], x, t);
    });
    else dst[k] = lerp(dst[k], v, t);
  }
  return dst;
}

// Folded wing (perched): humerus back & down along the flank, ulna forward to the carpal joint at the
// front-top of the folded wing, hand back along the back; wing plane rolled so the dorsal surface faces out.
// (solved numerically against landmark targets in the body frame: elbow by the hip, carpal joint in front
// by the shoulder, hand along the upper flank, dorsal surfaces facing out)
export const FOLDED = { elev: -0.09, sweep: -1.546, twist: 0.78, elbow: 2.95, wrist: 2.95, handTwist: 0.48, digit: 0.08, elbowTwist: 0.0 };
// Soaring wing: flat "plank", leading edge nearly straight
export const GLIDE = { elev: 0.07, sweep: 0.2, twist: 0.04, elbow: 0.34, wrist: 0.12, handTwist: -0.02, digit: -0.06, elbowTwist: 0 };

export class Animator {
  constructor(eagle, world) {
    this.eagle = eagle; this.world = world;
    this.rig = eagle.rig;
    this.t = 0;
    this.base = 'idle';            // idle | walk | flap | glide
    this.weights = { idle: 1, walk: 0, flap: 0, glide: 0 };
    this.pose = makePose();
    this.tmp = makePose();
    this.actions = { screech: null, headturn: null };
    // ground state
    this.pos = new THREE.Vector3(0, 0, 0); this.heading = 0;
    this.walkCentre = new THREE.Vector3(-1.2, 0, 0.6);
    this.walkPhase = 0;
    this.feet = [0, 1].map((i) => ({ world: new THREE.Vector3(), planted: true, swing: 0, from: new THREE.Vector3(), to: new THREE.Vector3(), lift: 0 }));
    this.feetInit = false;
    // idle state
    this.gaze = { yaw: 0, pitch: -0.1, roll: 0, tYaw: 0, tPitch: -0.1, tRoll: 0, next: 1.2, sacc: 0, from: [0, 0, 0] };
    this.blink = { t: 3, p: 0 };
    this.rouse = { next: 14, t: -1 };
    this.shift = { next: 5, t: -1, side: 1 };
    this.tailFlick = { next: 3, t: -1 };
    // flight state
    this.airborne = false;
    this.flight = { centre: new THREE.Vector3(10, 0, 5), radius: 42, angle: 0, alt: 24, phase: 0, speed: FLIGHT.flapSpeed, bank: 0, gust: new THREE.Vector3(), flapAmp: 1, stroke: 0 };
    this.flapToGlide = 0;
    this.headStab = new THREE.Vector3();
    this.bodyVel = new THREE.Vector3();
    this.timeScale = 1;
  }

  setMode(m) {
    const air = m === 'flap' || m === 'glide';
    if (air !== this.airborne) {
      // change of medium: place the bird in the new context (camera cut)
      this.airborne = air;
      for (const k in this.weights) this.weights[k] = 0;
      this.weights[m] = 1;
      if (!air) { this.pos.set(0, 0, 0); this.heading = 0; this.feetInit = false; }
      this.onCut && this.onCut(air);
    }
    if (m === 'glide' && this.base === 'flap') this.flapToGlide = 1;   // finish the flap on a downstroke
    this.base = m;
  }
  trigger(a) {
    if (a === 'screech') this.actions.screech = { t: 0, dur: 3.3 };
    if (a === 'headturn') this.actions.headturn = { t: 0, dur: 2.6, side: Math.random() < 0.5 ? 1 : -1 };
  }

  // =================================================================================================
  update(dtReal) {
    const dt = Math.min(dtReal, 1 / 20) * this.timeScale;
    this.t += dt;
    // cross-fade base behaviours
    for (const k in this.weights) {
      const target = k === this.base ? 1 : 0;
      const rate = (k === 'glide' || k === 'flap') ? 1.6 : 2.4;
      this.weights[k] += clamp(target - this.weights[k], -rate * dt, rate * dt);
    }
    // flap -> glide: do not start fading the flap until the wings finish a downstroke
    if (this.flapToGlide) {
      const ph = this.flight.phase % 1;
      if (ph > 0.48 && ph < 0.62) this.flapToGlide = 0;
      else { this.weights.flap = Math.max(this.weights.flap, 0.999); this.weights.glide = 1 - this.weights.flap; }
    }
    const P = this.pose;
    let first = true;
    const order = this.airborne ? ['flap', 'glide'] : ['idle', 'walk'];
    let acc = 0;
    for (const k of order) {
      const w = this.weights[k];
      if (w < 1e-3) continue;
      const pose = this[k](dt, this.tmp);
      if (first) { copyPose(P, pose); first = false; acc = w; }
      else { acc += w; blendPose(P, pose, w / acc); }
    }
    if (first) copyPose(P, this.airborne ? this.flap(dt, this.tmp) : this.idle(dt, this.tmp));
    // root motion
    this._root(dt);
    // overlays
    this._overlays(dt, P);
    this._apply(P, dt);
  }

  // ------------------------------------------------------------------ root / locomotion bookkeeping
  _root(dt) {
    const r = this.rig.root, H = this.world.heightAt;
    if (!this.airborne) {
      const ww = this.weights.walk;
      if (ww > 0.01 || this.base === 'walk') {
        // walk along a circle around walkCentre (counter-clockwise)
        const speed = 0.36 * ww;
        const rel = _v.copy(this.pos).sub(this.walkCentre); rel.y = 0;
        const R = 1.9;
        // steer towards the circle tangent
        const ang = Math.atan2(rel.z, rel.x);
        const d = rel.length();
        let hd = ang + Math.PI / 2 + clamp((d - R) * 1.4, -0.9, 0.9);   // tangent + radial correction
        const tgtHeading = Math.atan2(Math.cos(hd), Math.sin(hd));       // heading measured from +z towards +x
        let dh = tgtHeading - this.heading; dh = Math.atan2(Math.sin(dh), Math.cos(dh));
        this.heading += clamp(dh, -0.9 * dt, 0.9 * dt) * ww;
        this.pos.x += Math.sin(this.heading) * speed * dt;
        this.pos.z += Math.cos(this.heading) * speed * dt;
      }
      this.pos.y = H(this.pos.x, this.pos.z);
      r.position.copy(this.pos);
      r.quaternion.setFromAxisAngle(UP, this.heading);
    } else {
      const F = this.flight;
      const glideW = this.weights.glide;
      F.speed = lerp(FLIGHT.flapSpeed, FLIGHT.glideSpeed, glideW);
      // circle (thermal) with a bank angle from the turn rate: tan(bank) = v^2 / (g R)
      F.angle += (F.speed / F.radius) * dt;
      const x = F.centre.x + Math.cos(F.angle) * F.radius, z = F.centre.z + Math.sin(F.angle) * F.radius;
      const ground = Math.max(this.world.heightAt(x, z), -0.35);
      // flapping climbs slowly, gliding sinks slowly (bounded)
      F.alt += (glideW > 0.5 ? -0.25 : 0.45) * dt;
      F.alt = clamp(F.alt, 16, 34);
      const y = ground + F.alt;
      r.position.set(x, y, z);
      const heading = Math.atan2(-Math.sin(F.angle), Math.cos(F.angle));   // tangent direction (counter-clockwise)
      this.heading = heading;
      const bankTarget = Math.atan(F.speed * F.speed / (9.81 * F.radius)) * lerp(0.55, 1, glideW);
      F.bank = lerp(F.bank, bankTarget, 1 - Math.exp(-dt * 1.5));
      // heading faces along the velocity; positive angle = counter-clockwise -> turning left -> left wing down
      _q.setFromAxisAngle(UP, heading);
      _q2.setFromAxisAngle(new THREE.Vector3(0, 0, 1), F.bank);   // the circle turns right -> right wing down
      r.quaternion.copy(_q).multiply(_q2);
    }
  }

  // ------------------------------------------------------------------ IDLE (standing)
  idle(dt, out) {
    const t = this.t;
    const p = copyPose(out, makePoseCache());
    // standing posture: upright ("square-shouldered") body pitched ~45deg, legs flexed
    const breathe = Math.sin(t * TAU * 0.21);
    p.pelvis.set(0, 0.285 + breathe * 0.0015, -0.01);
    p.pitch = -0.78 - breathe * 0.01;
    p.femur = -0.95;
    // weight shift: occasionally lean onto one foot
    const S = this.shift;
    if (t > S.next && S.t < 0) { S.t = 0; S.side = Math.random() < 0.5 ? 1 : -1; S.dur = 2.5 + Math.random() * 3; }
    let lean = 0;
    if (S.t >= 0) { S.t += dt; lean = S.side * sstep(0, 0.6, S.t) * (1 - sstep(S.dur - 0.6, S.dur, S.t)); if (S.t > S.dur) { S.t = -1; S.next = t + 5 + Math.random() * 8; } }
    p.pelvis.x += lean * 0.018; p.roll = lean * 0.05;
    // wings folded, tiny breathing motion of the folded wings
    for (let i = 0; i < 2; i++) {
      Object.assign(p.wings[i], FOLDED);
      p.wings[i].elev += breathe * 0.006;
      p.wp[i] = wingParams();
    }
    // head: gaze saccades (fast shift then fixation)
    this._gaze(dt);
    const G = this.gaze;
    p.look.set(G.yaw, G.pitch, G.roll);
    p.head.set(0.0, 0.625 + breathe * 0.002, 0.125);
    // tail: hanging, occasional flick
    p.tail.set(0.05, 0, 0); p.tailSpread = 0.1;
    const TF = this.tailFlick;
    if (t > TF.next && TF.t < 0) TF.t = 0;
    if (TF.t >= 0) { TF.t += dt; const k = Math.sin(clamp(TF.t / 0.35, 0, 1) * Math.PI); p.tail.x -= 0.25 * k; p.tail.y += 0.12 * Math.sin(TF.t * 40) * k; p.tailSpread += 0.12 * k; if (TF.t > 0.35) { TF.t = -1; TF.next = t + 4 + Math.random() * 7; } }
    // feet square under the body
    p.feet[0].set(0.078, 0, 0.03); p.feet[1].set(-0.078, 0, 0.03);
    p.toeCurl = [0, 0]; p.toeSpread = [1, 1];
    // rouse: raise all feathers, shake, settle (maintenance behaviour)
    const R = this.rouse;
    if (t > R.next && R.t < 0 && !this.actions.screech) R.t = 0;
    if (R.t >= 0) {
      R.t += dt;
      const rt = R.t;
      const raise = sstep(0, 0.35, rt) * (1 - sstep(1.2, 1.8, rt));
      p.ruffle = raise;
      const shake = sstep(0.35, 0.45, rt) * (1 - sstep(0.95, 1.15, rt));
      p.roll += Math.sin(rt * TAU * 7.5) * 0.11 * shake;
      p.pelvis.x += Math.sin(rt * TAU * 7.5 + 0.6) * 0.008 * shake;
      p.tail.y += Math.sin(rt * TAU * 7.5 + 2) * 0.2 * shake;
      for (let i = 0; i < 2; i++) { p.wings[i].elev += 0.08 * raise; p.wings[i].sweep += 0.05 * shake * Math.sin(rt * TAU * 7.5 + i * 3); }
      if (rt > 2.0) { R.t = -1; R.next = t + 16 + Math.random() * 14; }
    }
    // blinks: the nictitating membrane sweeps across (fast, ~0.15s), occasionally after a saccade
    const B = this.blink;
    B.t -= dt;
    if (B.t < 0 && B.p <= 0) { B.p = 0.0001; }
    if (B.p > 0) { B.p += dt / 0.2; if (B.p >= 1) { B.p = 0; B.t = 2 + Math.random() * 5; } }
    p.nict = B.p > 0 ? Math.sin(B.p * Math.PI) : 0;
    return p;
  }

  _gaze(dt) {
    const G = this.gaze;
    G.next -= dt;
    if (G.next <= 0 && G.sacc <= 0) {
      // new fixation target: mostly moderate yaw, sometimes looking up (sky) or down (ground)
      G.from = [G.yaw, G.pitch, G.roll];
      const big = Math.random() < 0.2;
      G.tYaw = clamp(G.yaw + (Math.random() - 0.5) * (big ? 2.2 : 1.1), -1.3, 1.3);
      if (Math.abs(G.tYaw) < 0.12) G.tYaw += 0.2 * Math.sign(Math.random() - 0.5);
      G.tPitch = -0.1 + (Math.random() - 0.4) * 0.35;
      G.tRoll = Math.random() < 0.25 ? (Math.random() - 0.5) * 0.6 : 0;   // occasional head tilt
      // duration from amplitude: ~700 deg/s peak -> 60-200 ms
      const amp = Math.hypot(G.tYaw - G.yaw, G.tPitch - G.pitch);
      G.sdur = 0.06 + amp * 0.1;
      G.sacc = 1e-4;
      G.next = 0.9 + Math.random() * 3.2;
      if (Math.random() < 0.35) this.blink.t = G.sdur + 0.02;   // blink tied to a saccade
    }
    if (G.sacc > 0) {
      G.sacc += dt / G.sdur;
      const k = sstep(0, 1, Math.min(1, G.sacc));
      G.yaw = lerp(G.from[0], G.tYaw, k); G.pitch = lerp(G.from[1], G.tPitch, k); G.roll = lerp(G.from[2], G.tRoll, k);
      if (G.sacc >= 1) G.sacc = 0;
    }
  }

  // ------------------------------------------------------------------ WALK
  walk(dt, out) {
    const p = this.idle(0, out);   // start from the standing pose
    if (this.weights.idle < 0.02) this._gaze(dt);
    const ww = this.weights.walk;
    const f = 0.72;                // strides per second (1.44 steps / s)
    this.walkPhase += dt * f * clamp(ww * 1.5, 0, 1);
    const ph = this.walkPhase % 1;
    const w = TAU * ph;
    // body: more horizontal while walking, lowered, rolling onto the stance leg ("rocking gait")
    p.pitch = -0.58;
    p.pelvis.y = 0.268 - 0.012 * Math.cos(2 * w - 0.6);                 // dips after each touchdown
    p.pelvis.z = 0.0;
    p.roll = 0.085 * Math.sin(w - 0.25);                               // roll toward the stance foot
    p.pelvis.x = 0.024 * Math.sin(w - 0.25);                           // lateral sway over the stance foot
    p.yaw = 0.06 * Math.sin(w + 1.2);                                  // pelvis yaw with the swing leg
    // head: stabilised, slight forward thrust each step, looking ahead/down
    p.head.set(-p.pelvis.x * 0.6, 0.62, 0.2 + 0.008 * Math.sin(2 * w));
    p.look.set(this.gaze.yaw * 0.4, 0.05 + this.gaze.pitch * 0.3, -p.roll * 0.8);
    // wings shuffle for balance: the wing on the side of the lifted foot rises & opens slightly
    for (let i = 0; i < 2; i++) {
      const sgn = i === 0 ? 1 : -1;
      const lift = Math.max(0, -Math.sin(w - 0.25) * sgn);
      p.wings[i].elev += 0.1 * lift + 0.02;
      p.wings[i].elbow -= 0.12 * lift; p.wings[i].wrist -= 0.08 * lift;
      p.wings[i].sweep += 0.04 * lift;
    }
    p.tail.set(0.12, -0.12 * Math.sin(w - 0.25), 0.05 * Math.sin(w)); p.tailSpread = 0.14;
    // feet: gait targets in root frame are produced by the foot planner in _apply
    return p;
  }

  // ------------------------------------------------------------------ FLAP (flapping flight)
  flap(dt, out) {
    const p = copyPose(out, makePoseCache());
    const F = this.flight;
    F.phase += dt * FLIGHT.flapHz;
    const ph = F.phase % 1;
    const D = FLIGHT.downstrokeFrac;
    const down = ph < D;
    const u = down ? ph / D : (ph - D) / (1 - D);           // progress within the half-stroke
    // shoulder elevation: top of the stroke at ph=0, bottom at ph=D
    const elev = down ? 0.1 + 0.72 * Math.cos(Math.PI * u) : 0.1 - 0.72 * Math.cos(Math.PI * u);
    const sweep = down ? 0.05 + 0.22 * Math.sin(Math.PI * u) : 0.05 - 0.2 * Math.sin(Math.PI * u);
    const twist = down ? 0.28 * Math.sin(Math.PI * u) : -0.24 * Math.sin(Math.PI * u);
    // upstroke: elbow & wrist flex together (coupled by the radius/ulna mechanism), peak mid-upstroke
    const flex = down ? 0.08 * (1 - sstep(0, 0.35, u)) : Math.sin(Math.PI * Math.pow(u, 0.8));
    const elbow = 0.4 + 0.95 * flex, wrist = 0.34 + 1.05 * flex;
    const handTwist = down ? 0.08 * Math.sin(Math.PI * u) : -0.36 * Math.sin(Math.PI * u);
    for (let i = 0; i < 2; i++) {
      Object.assign(p.wings[i], { elev, sweep, twist, elbow, wrist, handTwist, digit: down ? -0.03 : 0.12 * flex, elbowTwist: 0 });
      Object.assign(p.wp[i], {
        primBend: down ? 0.16 * Math.sin(Math.PI * Math.pow(u, 0.9)) : -0.05 * Math.sin(Math.PI * u),
        secBend: down ? 0.05 * Math.sin(Math.PI * u) : -0.02,
        spread: down ? 0.35 : 0.35 - 0.6 * flex,
        supinate: down ? 0 : 0.95 * Math.sin(Math.PI * u),
        tipCurl: down ? 0.4 * Math.sin(Math.PI * u) : 0,
        covertLift: down ? 0 : 0.25 * Math.sin(Math.PI * u),
      });
    }
    F.stroke = ph;
    // body: horizontal, the downstroke accelerates it up & forward (bob ~3 cm, surge ~1.5 cm, pitch +-2deg)
    const w = TAU * ph;
    p.pitch = -0.04 + 0.035 * Math.sin(w + 0.6);
    p.pelvis.set(0, 0.018 * Math.sin(w - 0.9 + Math.PI), 0.012 * Math.sin(w - 0.3));
    p.roll = 0; p.yaw = 0;
    // head: stabilised in space - the neck absorbs the body bob
    p.head.set(0, 0.075, 0.43);
    p.head.y -= p.pelvis.y * 0.95; p.head.z -= p.pelvis.z * 0.9;
    p.look.set(0, 0.05, 0);
    // tail: pitches with the stroke, moderately spread
    p.tail.set(-0.04 + 0.06 * Math.sin(w + 2.2), 0, 0); p.tailSpread = 0.35; p.tailBend = 0.02 + 0.02 * Math.sin(w);
    // legs tucked back under the tail coverts, toes clenched
    p.femur = -0.35;
    p.feet[0].set(0.05, -0.1, -0.2); p.feet[1].set(-0.05, -0.1, -0.2);
    p.toeCurl = [1, 1]; p.toeSpread = [0.6, 0.6];
    p.flutter = 0.6;
    return p;
  }

  // ------------------------------------------------------------------ GLIDE (soaring)
  glide(dt, out) {
    const p = copyPose(out, makePoseCache());
    const t = this.t, F = this.flight;
    if (this.base === 'glide' && !this.flapToGlide) F.phase += dt * FLIGHT.flapHz * (1 - this.weights.glide);
    // turbulence: band-limited gusts (vertical & roll) with fast corrective reactions
    const gz = noise1(t * 1.3) * 0.6 + noise1(t * 3.7 + 20) * 0.4;
    const gr = noise1(t * 1.1 + 50) * 0.6 + noise1(t * 3.1 + 70) * 0.4;
    const gp = noise1(t * 0.9 + 90);
    for (let i = 0; i < 2; i++) {
      const sg = i === 0 ? 1 : -1;
      // the wing flexes a little under gust load; asymmetric twist corrects roll
      Object.assign(p.wings[i], GLIDE);
      p.wings[i].elev += 0.025 * gz + 0.02 * gr * sg;
      p.wings[i].twist += -0.03 * gr * sg + 0.02 * gp;
      p.wings[i].elbow += 0.05 * Math.max(0, -gz);           // brief tuck in a down-gust
      p.wings[i].wrist += 0.06 * Math.max(0, -gz);
      Object.assign(p.wp[i], { primBend: 0.1 + 0.05 * gz, secBend: 0.035 + 0.015 * gz, spread: 0.55, supinate: 0.12, tipCurl: 1, covertLift: 0.06 + 0.06 * Math.max(0, gz) });
    }
    p.pitch = -0.02 + 0.02 * gp; p.roll = 0.035 * gr; p.yaw = 0;
    p.pelvis.set(0, 0.006 * gz, 0);
    // tail: spread & twisted for roll/yaw control
    p.tail.set(-0.02 + 0.03 * gp, 0.03 * gr, -0.12 * gr); p.tailSpread = 0.4 + 0.1 * Math.abs(gr); p.tailBend = 0.03;
    // head held level against bank & gusts
    p.head.set(0, 0.075, 0.43); p.head.y -= p.pelvis.y;
    p.look.set(0.08 * Math.sin(t * 0.3), 0.18, 0);
    p.femur = -0.35;
    p.feet[0].set(0.05, -0.1, -0.2); p.feet[1].set(-0.05, -0.1, -0.2);
    p.toeCurl = [1, 1]; p.toeSpread = [0.6, 0.6];
    p.flutter = 1.0;
    return p;
  }

  // ------------------------------------------------------------------ overlays (screech, head turn)
  _overlays(dt, p) {
    const A = this.actions;
    if (A.screech) {
      const S = A.screech; S.t += dt;
      const t = S.t;
      const air = this.airborne;
      // posture envelope: throw the head back (bill skyward), extend the neck, lift the wings a little
      const env = sstep(0.0, 0.45, t) * (1 - sstep(S.dur - 0.5, S.dur, t));
      if (!air) {
        p.head.lerp(_v.set(0, 0.73, 0.02), env);
        p.look.y = lerp(p.look.y, -1.45, env);    // pitch up ~80-85deg: bill to the sky
        p.look.x = lerp(p.look.x, 0, env);
        p.look.z = lerp(p.look.z, 0, env);
        p.pitch = lerp(p.pitch, -0.9, env);
        for (let i = 0; i < 2; i++) { p.wings[i].elev += 0.1 * env; p.wings[i].sweep += 0.05 * env; p.wings[i].elbow -= 0.1 * env; }
        p.tail.x += 0.1 * env;
      } else {
        p.look.y = lerp(p.look.y, -0.5, env);
      }
      // notes: 4 slower "kwit" notes, then 7 rapid "kee" notes, a final lower note
      const notes = [0.55, 0.8, 1.03, 1.25, 1.52, 1.64, 1.76, 1.88, 2.0, 2.12, 2.26, 2.5];
      let jaw = 0, pulse = 0;
      for (let i = 0; i < notes.length; i++) {
        const d = t - notes[i];
        const len = i < 4 ? 0.2 : i < 11 ? 0.1 : 0.26;
        if (d > -0.03 && d < len + 0.08) {
          const a = sstep(-0.03, 0.03, d) * (1 - sstep(len - 0.02, len + 0.08, d));
          jaw = Math.max(jaw, a * (i < 4 ? 0.42 : i < 11 ? 0.34 : 0.45));
          pulse = Math.max(pulse, a);
        }
      }
      // keep the bill slightly open between the rapid notes
      jaw = Math.max(jaw, 0.08 * sstep(0.5, 0.6, t) * (1 - sstep(2.6, 2.8, t)));
      p.jaw = Math.max(p.jaw, jaw);
      p.bill = Math.max(p.bill, jaw * 0.12);              // cranial kinesis: the upper bill lifts a little
      // throat/body pulse with each note (air sac pressure)
      p.head.y += 0.004 * pulse; p.pelvis.y += 0.002 * pulse;
      p.pitch -= 0.015 * pulse;
      p.ruffle = Math.max(p.ruffle, 0.25 * env + 0.15 * pulse);
      if (t > S.dur) A.screech = null;
    }
    if (A.headturn) {
      const H = A.headturn; H.t += dt;
      const t = H.t;
      // look over the shoulder: ~0.22 s saccade (head leads, neck twists), fixate with a tilt, quick return
      const out = sstep(0.0, 0.22, t), back = sstep(1.85, 2.08, t);
      const k = out * (1 - back);
      const yaw = H.side * 2.55 * k;                        // ~146deg
      p.look.x = lerp(p.look.x, yaw, Math.min(1, k * 1.2));
      p.look.y = lerp(p.look.y, -0.05, k);
      // tilt to bring the deep fovea onto the target partway through the fixation
      const tilt = sstep(0.6, 0.8, t) * (1 - sstep(1.4, 1.6, t));
      p.look.z = lerp(p.look.z, H.side * 0.35 * tilt, k);
      if (!this.airborne) {
        // the head pulls in slightly and moves toward the turning side while twisting
        p.head.lerp(_v.set(H.side * 0.03, 0.66, 0.05), k * 0.8);
        p.pitch = lerp(p.pitch, p.pitch - 0.05, k);
      }
      // a small follow-up saccade during the hold (refixation)
      if (t > 1.1 && t < 1.2) p.look.x += H.side * 0.08 * Math.sin((t - 1.1) / 0.1 * Math.PI);
      if (t > H.dur) A.headturn = null;
    }
  }

  // ------------------------------------------------------------------ apply the pose to the rig
  _apply(P, dt) {
    const rig = this.rig, E = this.eagle;
    const root = rig.root;
    root.updateMatrixWorld(true);
    // pelvis (root frame)
    rig.pelvis.position.copy(P.pelvis);
    _e.set(P.pitch, P.yaw, P.roll, 'YXZ');
    rig.pelvis.quaternion.setFromEuler(_e);
    rig.thorax.quaternion.setFromAxisAngle(_v.set(1, 0, 0), -0.04);   // synsacrum-notarium: nearly rigid
    // tail
    _e.set(P.tail.x, P.tail.y, P.tail.z, 'XYZ');
    rig.tail.quaternion.setFromEuler(_e);
    E.tail.spread = P.tailSpread; E.tail.bend = P.tailBend;
    // wings
    for (let i = 0; i < 2; i++) { rig.setWing(i, P.wings[i]); Object.assign(E.wings.params[i], P.wp[i]); }
    rig.wings.forEach((w) => { w.alula.quaternion.setFromAxisAngle(_v.set(0, 1, 0), 0); });
    rig.pelvis.updateMatrixWorld(true);
    // neck & head: head target (root frame) -> thorax frame; orientation from look (root frame)
    const headW = _v.copy(P.head);
    root.localToWorld(headW);
    const tgt = rig.thorax.worldToLocal(headW.clone());
    // look quaternion in world: heading (root yaw only, keeps the head level during bank) * yaw * pitch * roll
    const rootYawOnly = new THREE.Quaternion().setFromAxisAngle(UP, this.heading);
    _e.set(P.look.y, P.look.x, P.look.z, 'YXZ');
    const headQ = rootYawOnly.multiply(_q.setFromEuler(_e));
    if (!this.airborne) { const rq = root.getWorldQuaternion(new THREE.Quaternion()); headQ.copy(rq.multiply(_q.setFromEuler(_e))); }
    this.neckState = rig.solveNeck(tgt, headQ, P.look.x * 0.9, P.look.z, this.neckState || { ext: 0.3, lean: 0 });
    // jaw & bill kinesis
    rig.jaw.quaternion.setFromAxisAngle(_v.set(1, 0, 0), P.jaw);
    rig.upperBill.quaternion.setFromAxisAngle(_v.set(1, 0, 0), -P.bill);
    // legs
    this._legs(P, dt);
    // eyes
    for (const e of E.body.eyes) E.body.setLids(e, P.lids, P.nict);
    // feather dynamics
    E.feathers.uniforms.uRuffle.value = P.ruffle;
    E.feathers.uniforms.uFlutter.value = P.flutter;
    E.update(dt, this.t);
  }

  _legs(P, dt) {
    const rig = this.rig, root = rig.root;
    const ground = !this.airborne;
    const H = this.world.heightAt;
    for (let i = 0; i < 2; i++) {
      const L = rig.legs[i];
      let targetRoot;
      let curl = P.toeCurl[i], spread = P.toeSpread[i];
      let footQ = null;
      if (ground) {
        const F = this.feet[i];
        // desired foot position (world) from the pose
        const desired = root.localToWorld(P.feet[i].clone());
        if (!this.feetInit) { F.world.copy(desired); F.world.y = H(F.world.x, F.world.z); F.planted = true; }
        const walking = this.weights.walk > 0.05;
        if (walking) {
          // gait timing: left swings in [0, 0.38), right in [0.5, 0.88)
          const ph = (this.walkPhase + (i === 0 ? 0 : 0.5)) % 1;
          const swingDur = 0.38;
          if (ph < swingDur) {
            if (F.planted) {
              F.planted = false; F.from.copy(F.world);
              // landing target: where the body will be at mid-stance of the next stance, plus hip offset
              const stepAhead = 0.36 * (1 / 0.72) * (0.5 - 0.0) * 0.72;
              const hd = this.heading;
              const lat = (i === 0 ? 1 : -1) * 0.075;
              F.to.set(this.pos.x + Math.sin(hd) * (stepAhead * 0.62 + 0.03) + Math.cos(hd) * lat, 0, this.pos.z + Math.cos(hd) * (stepAhead * 0.62 + 0.03) - Math.sin(hd) * lat);
              F.to.y = H(F.to.x, F.to.z);
            }
            const s = ph / swingDur, k = sstep(0, 1, s);
            F.world.lerpVectors(F.from, F.to, k);
            F.world.y += Math.sin(Math.PI * Math.pow(s, 0.8)) * 0.055;
            // perching reflex: flexing the ankle during swing pulls the toes closed
            curl = Math.max(curl, 0.75 * Math.sin(Math.PI * Math.min(1, s * 1.15)));
            spread = lerp(spread, 0.55, Math.sin(Math.PI * s));
          } else F.planted = true;
        } else {
          // standing: step to the square stance if a foot is far away from it (one foot at a time)
          const other = this.feet[1 - i];
          const dist = Math.hypot(desired.x - F.world.x, desired.z - F.world.z);
          if (F.planted && other.planted && dist > 0.05) { F.planted = false; F.swing = 0; F.from.copy(F.world); F.to.copy(desired); F.to.y = H(desired.x, desired.z); }
          if (!F.planted) {
            F.swing += dt / 0.4;
            const s = Math.min(1, F.swing), k = sstep(0, 1, s);
            F.world.lerpVectors(F.from, F.to, k); F.world.y += Math.sin(Math.PI * s) * 0.04;
            curl = Math.max(curl, 0.6 * Math.sin(Math.PI * s));
            if (s >= 1) F.planted = true;
          }
        }
        // ball of the foot sits on the toe pads (~8 mm above the ground)
        const w = F.world.clone(); w.y += 0.009;
        targetRoot = w;
        footQ = root.getWorldQuaternion(new THREE.Quaternion());
        footQ.multiply(_q.setFromAxisAngle(UP, (i === 0 ? 1 : -1) * 0.12));
      } else {
        targetRoot = root.localToWorld(P.feet[i].clone());
      }
      // into pelvis space
      rig.pelvis.updateMatrixWorld(true);
      const tp = rig.pelvis.worldToLocal(targetRoot.clone());
      // femur held at a fixed angle in the world sagittal plane (compensate the body pitch)
      rig.solveLeg(i, tp, P.femur - P.pitch, footQ, 0.06);
      rig.setToes(i, curl, spread);
    }
    this.feetInit = true;
  }
}

let _cache = null;
function makePoseCache() { return _cache || (_cache = makePose()); }
