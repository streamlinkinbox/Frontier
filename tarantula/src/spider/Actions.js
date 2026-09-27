import * as THREE from 'three';

// Attack / display behaviours layered on top of locomotion.
//  threat : defensive rearing - forelegs + palps raised, chelicerae lifted, fangs unsheathed (hold)
//  strike : predatory lunge - anticipation, lunge, forelegs pin the prey, orthognath fangs stab
//           downward/backward, venom-pumping hold, recovery
//  flick  : New-World defence - hind legs brush urticating setae off the dorsal-posterior abdomen
// Plus idle sensory behaviour: pedipalp tapping and foreleg probing.

const clamp = (x, a, b) => Math.min(Math.max(x, a), b);
const lerp = (a, b, t) => a + (b - a) * t;
const ss = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
const env = (t, a, b, c, d) => ss(a, b, t) * (1 - ss(c, d, t)); // attack-sustain-release envelope

function P(yaw, a) { return { yaw, a: Float32Array.from(a) }; }
// Authored display poses (absolute in-plane joint angles, radians)
const THREAT = {
  1: P(0.38, [-0.15, 0.3, 1.08, 0.5, 0.36, -0.12, -0.5]),
  2: P(0.98, [-0.2, 0.2, 0.86, 0.3, 0.12, -0.38, -0.7]),
  palp: P(0.22, [-0.3, 0.15, 0.82, 0.3, 0.12, -0.35]),
};

export class ActionController {
  constructor(spider) {
    this.s = spider;
    this.current = null; // {type, t, dur}
    this.threatHold = false;
    this.threatW = 0;
    this.out = {
      locomotionLock: 0, turn: 0, shove: 0,
      body: { x: 0, y: 0, z: 0, pitch: 0, roll: 0, yaw: 0 },
      abdomenPitch: 0, abdomenYaw: 0, chelRaise: 0, fangOpen: 0,
      legLock: (f) => this._legLock(f),
      override: (f, dt) => this._override(f, dt),
    };
    this.scratch = new Map();
    this.idle = { palpT: [2.0, 3.3], legT: 5.5, palpPhase: [-1, -1], legPhase: -1, legSide: 1 };
    this.emitHairs = null; // callback(worldPos, worldVel)
    this._tmp = new THREE.Vector3();
    this._tmpN = new THREE.Vector3();
    this.onEvent = null;
  }

  trigger(type) {
    if (this.current && this.current.type !== 'threat') return false;
    const dur = { strike: 1.55, flick: 2.5 }[type];
    if (!dur) return false;
    this.current = { type, t: 0, dur };
    this.onEvent && this.onEvent(type);
    return true;
  }
  setThreat(on) { this.threatHold = on; }
  get busy() { return !!this.current || this.threatW > 0.05; }

  update(dt, ctrl) {
    const o = this.out;
    this.time = (this.time || 0) + dt;
    this.threatW = lerp(this.threatW, this.threatHold && !this.current ? 1 : 0, 1 - Math.exp(-dt * (this.threatHold ? 7 : 5)));
    const tw = ss(0, 1, this.threatW);
    const c = this.current;
    if (c) { c.t += dt; if (c.t >= c.dur) this.current = null; }
    const t = c ? c.t : 0;

    // ---- defaults blended from the threat display
    const tremble = (Math.sin(this.time * 31) * 0.5 + Math.sin(this.time * 47.3) * 0.5) * 0.012 * tw;
    o.body.x = 0; o.body.y = 0.55 * tw; o.body.z = -0.35 * tw; o.body.pitch = 0.44 * tw + tremble; o.body.roll = 0; o.body.yaw = 0;
    o.abdomenPitch = 0.42 * tw; o.abdomenYaw = 0;
    o.chelRaise = 0.38 * tw; o.fangOpen = 1.72 * ss(0.3, 1, tw);
    o.locomotionLock = 0.85 * tw; o.turn = 0; o.shove = 0;

    if (c && c.type === 'strike') {
      const a = ss(0.0, 0.2, t) * (1 - ss(0.2, 0.3, t));            // anticipation
      const l = ss(0.2, 0.3, t) * (1 - ss(0.95, 1.45, t));           // lunge + hold
      const stab = ss(0.27, 0.36, t) * (1 - ss(1.0, 1.4, t));
      const pump = Math.sin(clamp(t - 0.4, 0, 1) * 28) * 0.07 * env(t, 0.4, 0.5, 0.85, 0.95);
      o.body.y += 0.35 * a - 0.12 * l;
      o.body.z += -0.45 * a + 1.55 * l;
      o.body.pitch += 0.3 * a - 0.12 * l;
      o.abdomenPitch += 0.15 * a - 0.08 * l;
      o.chelRaise = Math.max(o.chelRaise, 0.45 * a + 0.15 * l - 0.2 * stab);
      const open = Math.max(o.fangOpen, 1.9 * ss(0.02, 0.2, t));
      const strikeFang = lerp(open, 0.55 + pump, stab);
      o.fangOpen = lerp(strikeFang, o.fangOpen, ss(1.15, 1.5, t));
      o.locomotionLock = 1;
      this.strikeW = { a, l, t };
    } else this.strikeW = null;

    if (c && c.type === 'flick') {
      const w = env(t, 0.0, 0.35, 2.05, 2.5);
      o.abdomenPitch += 0.28 * w;
      o.body.pitch -= 0.08 * w;
      o.body.y += 0.1 * w;
      o.locomotionLock = Math.max(o.locomotionLock, 0.9 * w);
      this.flickW = { w, t };
    } else this.flickW = null;

    // idle timers
    const still = Math.abs(this.s.speed) < 0.3 && !c && tw < 0.05;
    const id = this.idle;
    for (let i = 0; i < 2; i++) {
      if (id.palpPhase[i] >= 0) { id.palpPhase[i] += dt / 0.9; if (id.palpPhase[i] > 1) id.palpPhase[i] = -1; }
      else if (still) { id.palpT[i] -= dt; if (id.palpT[i] < 0) { id.palpPhase[i] = 0; id.palpT[i] = 1.5 + Math.random() * 4; } }
    }
    if (id.legPhase >= 0) { id.legPhase += dt / 1.6; if (id.legPhase > 1) id.legPhase = -1; }
    else if (still) { id.legT -= dt; if (id.legT < 0) { id.legPhase = 0; id.legT = 4 + Math.random() * 7; id.legSide = Math.random() < 0.5 ? 1 : -1; } }
    return o;
  }

  _legLock(f) {
    const c = this.current;
    if (f.isPalp) return this.threatW > 0.05 || (c && c.type === 'strike');
    const li = f.limb.legIndex;
    if (c && c.type === 'strike') return true;
    if (c && c.type === 'flick') return true;
    if (this.threatW > 0.05) return true;
    if (this.idle.legPhase >= 0 && li === 0 && f.limb.side === this.idle.legSide) return true;
    return false;
  }

  _scratch(f) {
    let p = this.scratch.get(f);
    if (!p) { p = f.limb.makePose(); this.scratch.set(f, p); }
    return p;
  }

  _solveBody(f, x, y, z, lift = 0, nx = 0, ny = 1, nz = 0) {
    const p = this._scratch(f);
    this._tmp.set(x * f.limb.side, y, z);
    this._tmpN.set(nx * f.limb.side, ny, nz).normalize();
    f.limb.solve(this._tmp, this._tmpN, p, lift);
    return p;
  }

  _override(f, dt) {
    const limb = f.limb, li = limb.legIndex, side = limb.side;
    const tw = ss(0, 1, this.threatW);
    const c = this.current;
    let w = 0, pose = null;
    const tremble = Math.sin(this.time * 23 + li * 1.7 + side) * 0.02;

    // --- threat display
    if (tw > 0.001) {
      const src = f.isPalp ? THREAT.palp : li === 0 ? THREAT[1] : li === 1 ? THREAT[2] : null;
      if (src) {
        pose = this._scratch(f);
        pose.yaw = src.yaw + tremble * 0.5; pose.a.set(src.a);
        for (let i = 2; i < pose.a.length; i++) pose.a[i] += tremble * (i - 1) * 0.4;
        w = tw;
      }
    }

    // --- strike
    const sw = this.strikeW;
    if (sw) {
      const { a, l, t } = sw;
      if (f.isPalp || li <= 1) {
        let target;
        if (f.isPalp) target = [0.55, lerp(0.2, -0.55, l), lerp(1.6, 2.5, l) - 0.4 * ss(0.4, 0.9, t)];
        else if (li === 0) target = [lerp(1.2, 1.25, l), lerp(1.4, -0.72, l), lerp(2.6, 4.1, l) - 0.9 * ss(0.4, 0.9, t)];
        else target = [lerp(2.2, 2.5, l), lerp(0.9, -0.72, l), lerp(1.6, 2.9, l) - 0.6 * ss(0.4, 0.9, t)];
        const sp = this._solveBody(f, target[0], target[1], target[2], 0.25 * (1 - l));
        const sw2 = env(t, 0.0, 0.16, 1.1, 1.5);
        if (pose && w > 0) { const tmp = f.limb.makePose(); tmp.yaw = pose.yaw; tmp.a.set(pose.a); pose = tmp; }
        pose = pose && w > 0 ? { yaw: lerp(pose.yaw, sp.yaw, sw2), a: pose.a.map((v, i) => lerp(v, sp.a[i], sw2)) } : sp;
        w = Math.max(w, sw2);
      }
    }

    // --- urticating hair flick with leg IV
    const fw = this.flickW;
    if (fw && li === 3) {
      const { w: ew, t } = fw;
      const osc = 0.5 + 0.5 * Math.sin((t - 0.35) * Math.PI * 2 * 6.0 + (side > 0 ? 0 : Math.PI));
      const active = ss(0.3, 0.45, t) * (1 - ss(1.85, 2.05, t));
      const z = lerp(-2.0, -3.7, osc), y = 1.35 + 0.2 * Math.sin(osc * Math.PI) - 0.25 * active;
      const sp = this._solveBody(f, 0.62, lerp(0.4, y, ss(0, 0.35, t)), lerp(-2.8, z, active), 0.1, 0, 1, 0.2);
      pose = sp; w = ew;
      // emit urticating setae on the backward stroke
      if (active > 0.5 && this.emitHairs) {
        const phaseV = Math.cos((t - 0.35) * Math.PI * 2 * 6.0 + (side > 0 ? 0 : Math.PI));
        if (phaseV > 0.2) this.emitHairs(limb.tip, side, dt);
      }
    }

    // --- idle palp tapping / foreleg probing
    if (!c && tw < 0.05) {
      if (f.isPalp) {
        const i = side > 0 ? 0 : 1;
        const ph = this.idle.palpPhase[i];
        if (ph >= 0) {
          const up = Math.sin(Math.PI * ph);
          const tap = Math.max(0, Math.sin(ph * Math.PI * 4)) * (ph > 0.35 ? 1 : 0);
          const sp = this._solveBody(f, 0.55, -0.72 + 0.55 * up - 0.12 * tap, 1.75 + 0.3 * up, 0.35 * up);
          pose = sp; w = ss(0, 0.2, ph) * (1 - ss(0.8, 1, ph));
        }
      } else if (li === 0 && side === this.idle.legSide && this.idle.legPhase >= 0) {
        const ph = this.idle.legPhase;
        const up = Math.sin(Math.PI * ph);
        const wave = Math.sin(ph * Math.PI * 3) * 0.35;
        const sp = this._solveBody(f, 1.45 + wave, -0.75 + 1.6 * up, 3.4 + 0.5 * up, 0.5 * up);
        pose = sp; w = ss(0, 0.25, ph) * (1 - ss(0.75, 1, ph));
      }
    }
    if (!pose) return null;
    return { w: clamp(w, 0, 1), pose };
  }
}
