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
    this.idle = { palpT: [3.5, 6.5], legT: 9, palpPhase: [-1, -1], legPhase: -1, legSide: 1 };
    this.emitHairs = null; // callback(worldPos, worldVel)
    this._tmp = new THREE.Vector3();
    this._tmpN = new THREE.Vector3();
    this._tmp2 = new THREE.Vector3(); this._tmpN2 = new THREE.Vector3(); this._tmpQ2 = new THREE.Quaternion();
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
      this.strikeW = { a, l, t, act: c };
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
      if (id.palpPhase[i] >= 0) { id.palpPhase[i] += dt / 2.6; if (id.palpPhase[i] > 1) id.palpPhase[i] = -1; }
      else if (still) { id.palpT[i] -= dt; if (id.palpT[i] < 0) { id.palpPhase[i] = 0; id.palpT[i] = 4 + Math.random() * 7; } }
    }
    // any locomotion / action winds idle gestures down quickly (never drag a raised leg along)
    const busyNow = Math.abs(this.s.speed) > 0.3 || Math.abs(this.s.turnRate) > 0.15 || !!c || tw > 0.05;
    if (busyNow) {
      for (let i = 0; i < 2; i++) if (id.palpPhase[i] >= 0) { id.palpPhase[i] = Math.max(id.palpPhase[i], 0.85) + dt / 0.3; if (id.palpPhase[i] > 1) id.palpPhase[i] = -1; }
      if (id.legPhase >= 0) { id.legPhase = Math.max(id.legPhase, 0.75) + dt / 0.35; if (id.legPhase > 1) id.legPhase = -1; }
    }
    if (id.legPhase >= 0) { id.legPhase += dt / 3.2; if (id.legPhase > 1) id.legPhase = -1; }
    else if (still) { id.legT -= dt; if (id.legT < 0) { id.legPhase = 0; id.legT = 8 + Math.random() * 10; id.legSide = Math.random() < 0.5 ? 1 : -1; } }
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

  _scratchB(f) {
    const m = this.scratchB || (this.scratchB = new Map());
    let p = m.get(f); if (!p) { p = f.limb.makePose(); m.set(f, p); }
    return p;
  }
  _blendB(f, p0, p1, t) {
    const m = this.scratchC || (this.scratchC = new Map());
    let o = m.get(f); if (!o) { o = f.limb.makePose(); m.set(f, o); }
    o.yaw = lerp(p0.yaw, p1.yaw, t);
    for (let i = 0; i < o.a.length; i++) o.a[i] = lerp(p0.a[i], p1.a[i], t);
    return o;
  }

  _scratch(f) {
    let p = this.scratch.get(f);
    if (!p) { p = f.limb.makePose(); this.scratch.set(f, p); }
    return p;
  }

  _solveWorld(f, pWorld, nWorld, lift = 0) {
    const body = this.s.body;
    const p = this._scratch(f);
    const lp = this._tmpW || (this._tmpW = new THREE.Vector3());
    const ln = this._tmpWN || (this._tmpWN = new THREE.Vector3());
    const q = this._tmpQ || (this._tmpQ = new THREE.Quaternion());
    lp.copy(pWorld); body.worldToLocal(lp); this._groundGuard(f, lp);
    body.getWorldQuaternion(q).invert();
    ln.copy(nWorld).applyQuaternion(q);
    f.limb.solve(lp, ln, p, lift);
    return p;
  }

  _solveBody(f, x, y, z, lift = 0, nx = 0, ny = 1, nz = 0) {
    const p = this._scratch(f);
    this._tmp.set(x * f.limb.side, y, z);
    this._groundGuard(f, this._tmp);
    this._tmpN.set(nx * f.limb.side, ny, nz).normalize();
    f.limb.solve(this._tmp, this._tmpN, p, lift);
    return p;
  }

  // Authored (body-space) targets must never push a tarsus/palp tip into rock on uneven ground.
  _groundGuard(f, local) {
    const s = this.s, body = s.body;
    const pad = f.isPalp ? 0.12 : f.limb.R[f.limb.n - 1] * 0.8;
    const w = this._gw || (this._gw = new THREE.Vector3());
    const o = this._go || (this._go = new THREE.Vector3());
    const d = this._gd || (this._gd = new THREE.Vector3());
    w.copy(local); body.localToWorld(w);
    // nearest rock surface (signed): robust under overhangs and when the target is inside rock
    const h = s.world.closest(w, pad + 1.5);
    if (!h || h.distance >= pad) return;
    w.copy(h.point).addScaledVector(h.normal, pad);
    local.copy(w); body.worldToLocal(local);
  }

  _override(f, dt) {
    const limb = f.limb, li = limb.legIndex, side = limb.side;
    const tw = ss(0, 1, this.threatW);
    const c = this.current;
    let w = 0, pose = null;
    const tremble = Math.sin(this.time * 23 + (f.isPalp ? -1.3 : li) * 1.7 + side) * 0.02; // palps have no legIndex

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
      const { a, l, t, act: sc } = sw;
      if (f.isPalp) {
        // pedipalps reach forward/down to take hold of the prey
        const target = [0.6, lerp(0.25, -0.55, l), lerp(2.3, 3.2, l) - 0.4 * ss(0.4, 0.9, t)];
        const sp = this._solveBody(f, target[0], target[1], target[2], 0.25 * (1 - l));
        const sw2 = env(t, 0.0, 0.16, 1.1, 1.5);
        if (pose && w > 0) { const tmp = f.limb.makePose(); tmp.yaw = pose.yaw; tmp.a.set(pose.a); pose = tmp; }
        pose = pose && w > 0 ? { yaw: lerp(pose.yaw, sp.yaw, sw2), a: pose.a.map((v, i) => lerp(v, sp.a[i], sw2)) } : sp;
        w = Math.max(w, sw2);
      } else if (li <= 1) {
        // legs I-II: rise and spread during the anticipation, then come down wide on the substrate
        // around the prey as the body lunges (a real foothold, so they stay planted and the recovery
        // is seamless: the base IK takes over from exactly the same place)
        const S = this.s;
        if (!sc.slam) sc.slam = new Map();
        let sl = sc.slam.get(f);
        if (!sl) {
          const cand = S._restWorld(f, new THREE.Vector3()).addScaledVector(S.fwd, 1.55 + (li === 0 ? 0.35 : 0.1));
          const h = S._planFoothold(f, cand); // collision-checked, reachable foothold
          sl = { p: h.p.clone(), n: h.n.clone(), planted: false };
          sc.slam.set(f, sl);
        }
        const raised = this._scratchB(f);
        const src = li === 0 ? THREAT[1] : THREAT[2];
        raised.yaw = src.yaw; raised.a.set(src.a);
        const d = ss(0.2, 0.33, t);                       // descent onto the prey
        const tgt = this._tmp2.copy(sl.p).addScaledVector(S.up, 1.8 * (1 - d));
        const down = this._solveWorld(f, tgt, sl.n, 0.35 * (1 - d));
        pose = this._blendB(f, raised, down, d);
        w = ss(0.0, 0.12, t) * (1 - ss(0.45, 0.75, t));
        if (!sl.planted && t >= 0.33) { f.pos.copy(sl.p); f.normal.copy(sl.n); f.swinging = false; sl.planted = true; }
      }
    }

    // --- urticating hair flick with leg IV: each hind leg sweeps its metatarsus/tarsus along the dorsal
    // abdomen and off the posterior end (the legs alternate), following the abdomen's real surface
    const fw = this.flickW;
    if (fw && li === 3) {
      const { w: ew, t } = fw;
      const S = this.s;
      const ph = (t - 0.35) * 4.2 + (side > 0 ? 0 : 0.5);
      const active = ss(0.3, 0.5, t) * (1 - ss(1.85, 2.05, t));
      const u = (0.5 - 0.5 * Math.cos(Math.PI * 2 * ph)) * active;   // 0 = mid-back, 1 = past the tip
      const backStroke = Math.sin(Math.PI * 2 * ph) > 0;
      // abdomen as an ellipsoid in the pedicel (abdPivot) frame: measured from the mesh incl. pile
      const cy = 0.34, cz = -1.98, ra = 1.45, rb = 1.2, rc = 1.75;
      const x = 0.5 * side, z = lerp(-1.9, -4.35, u);
      const q = 1 - (x / ra) ** 2 - ((z - cz) / rc) ** 2;
      const sy = cy + rb * Math.sqrt(Math.max(q, 0));
      const y = sy + 0.22 + (backStroke ? 0 : 0.25) * active + 0.45 * ss(0.85, 1, u);
      const pl = this._tmp2.set(x, y, z);
      const nl = this._tmpN2.set(x / (ra * ra), (sy - cy) / (rb * rb), (z - cz) / (rc * rc)).normalize();
      S.abdPivot.localToWorld(pl);
      nl.applyQuaternion(S.abdPivot.getWorldQuaternion(this._tmpQ2));
      pose = this._solveWorld(f, pl, nl, 0.1);
      w = ew;
      // emit urticating setae on the backward stroke as the tarsus leaves the abdomen
      if (active > 0.5 && backStroke && u > 0.55 && this.emitHairs) this.emitHairs(limb.tip, side, dt);
    }

    // --- idle palp tapping / foreleg probing
    if (!c && tw < 0.05) {
      if (f.isPalp) {
        const i = side > 0 ? 0 : 1;
        const ph = this.idle.palpPhase[i];
        if (ph >= 0) {
          // raise, reach slightly forward, one gentle touch of the substrate, settle back
          const up = ss(0.0, 0.35, ph) * (1 - ss(0.6, 1.0, ph));
          const touch = ss(0.4, 0.52, ph) * (1 - ss(0.58, 0.7, ph));
          const s = this.s;
          const p = this._tmp.copy(f.pos).addScaledVector(f.normal, 0.55 * up * (1 - touch) - 0.06 * touch)
            .addScaledVector(s.fwd, 0.35 * up);
          pose = this._solveWorld(f, p, f.normal, 0.3 * up);
          w = ss(0, 0.15, ph) * (1 - ss(0.85, 1, ph));
        }
      } else if (li === 0 && side === this.idle.legSide && this.idle.legPhase >= 0) {
        const ph = this.idle.legPhase;
        const up = Math.sin(Math.PI * ph);
        const wave = Math.sin(ph * Math.PI * 2) * 0.22;
        const sp = this._solveBody(f, 1.45 + wave, -0.75 + 1.6 * up, 3.4 + 0.5 * up, 0.5 * up);
        pose = sp; w = ss(0, 0.25, ph) * (1 - ss(0.75, 1, ph));
      }
    }
    if (!pose) return null;
    return { w: clamp(w, 0, 1), pose };
  }
}
