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

// Abdomen outline in the abdPivot (pedicel) frame, measured from the real geometry with
// tools/probes/abdsil.js, per 0.25 cm z-slice: half-width and vertical extent of (a) the abdomen body
// and (b) the envelope of its long setae. Legs may brush through the setae but never enter the body.
const ABD_Z = [0, -0.25, -0.5, -0.75, -1, -1.25, -1.5, -1.75, -2, -2.25, -2.5, -2.75, -3, -3.25, -3.5, -3.75];
const ABD_BODY_X = [0, 0.56, 0.82, 1.03, 1.15, 1.21, 1.22, 1.2, 1.14, 1.04, 0.86, 0.67, 0.44, 0.2, 0, 0];
const ABD_BODY_YMAX = [0.01, 0.53, 0.78, 1, 1.11, 1.18, 1.19, 1.18, 1.12, 1.02, 0.84, 0.65, 0.42, 0.2, 0, 0];
const ABD_BODY_YMIN = [0.01, -0.37, -0.53, -0.66, -0.73, -0.76, -0.76, -0.75, -0.71, -0.64, -0.54, -0.43, -0.28, -0.12, 0, 0];
const ABD_X = [0.44, 0.91, 1.25, 1.61, 1.71, 1.8, 1.84, 1.94, 1.77, 1.7, 1.56, 1.43, 1.2, 0.99, 0.97, 0.92];
const ABD_YMAX = [-0.24, 0.9, 1.11, 1.4, 1.56, 1.87, 1.81, 1.75, 1.76, 1.65, 1.53, 1.27, 1.14, 0.83, 0.53, 0.43];
const ABD_YMIN = [-0.24, -0.64, -0.79, -1, -1.04, -1.13, -1.16, -1.04, -1.03, -0.94, -1.02, -0.91, -0.81, -1.15, -0.91, -0.68];
// Slice at z: `t` = 0 gives the body surface (+ short pile), 1 the tips of the long setae.
function abdSlice(z, out, t = 1) {
  const n = ABD_Z.length;
  let i = 0;
  if (z >= ABD_Z[0]) i = 0; else if (z <= ABD_Z[n - 1]) i = n - 2; else while (ABD_Z[i + 1] > z) i++;
  const k = clamp((ABD_Z[i] - z) / (ABD_Z[i] - ABD_Z[i + 1]), 0, 1), L = (a) => a[i] + (a[i + 1] - a[i]) * k;
  const PILE = 0.1;
  const bx = L(ABD_BODY_X) + PILE, bt = L(ABD_BODY_YMAX) + PILE, bb = L(ABD_BODY_YMIN) - PILE;
  const x = lerp(bx, L(ABD_X), t), top = lerp(bt, L(ABD_YMAX), t), bot = lerp(bb, L(ABD_YMIN), t);
  out.X = Math.max(x, 0.05); out.yc = 0.5 * (top + bot); out.Y = Math.max(0.5 * (top - bot), 0.05);
  return out;
}
const _sl = { X: 0, yc: 0, Y: 0 };

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

  _solveWorld(f, pWorld, nWorld, lift = 0, femurLift = 0) {
    const body = this.s.body;
    const p = this._scratch(f);
    const lp = this._tmpW || (this._tmpW = new THREE.Vector3());
    const ln = this._tmpWN || (this._tmpWN = new THREE.Vector3());
    const q = this._tmpQ || (this._tmpQ = new THREE.Quaternion());
    lp.copy(pWorld); body.worldToLocal(lp); this._groundGuard(f, lp);
    body.getWorldQuaternion(q).invert();
    ln.copy(nWorld).applyQuaternion(q);
    f.limb.solve(lp, ln, p, lift, femurLift);
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

    // --- urticating hair flick with leg IV (the legs alternate half a cycle apart). On the backward stroke
    // the tarsus brushes along the dorsolateral "shoulder" of the abdomen, just inside the tips of the
    // setae pile, and then kicks backward and outward off the abdomen, releasing hairs. On the forward
    // return it swings wide and high, clear of the abdomen. A tarantula leg flexes in one vertical plane,
    // so the brushing line stays on the flank: only the tarsus ever meets the hair, never the leg on top.
    const fw = this.flickW;
    if (fw && li === 3) {
      const { w: ew, t } = fw;
      const S = this.s;
      const ph = (t - 0.35) * 4.2 + (side > 0 ? 0 : 0.5);
      const active = ss(0.3, 0.5, t) * (1 - ss(1.85, 2.05, t));
      const u = (0.5 - 0.5 * Math.cos(Math.PI * 2 * ph)) * active;   // 0 = front of stroke, 1 = kicked off
      const back = ss(-0.3, 0.3, Math.sin(Math.PI * 2 * ph));          // 1 on the backward (brushing) stroke
      // coxal socket in the abdomen frame (moves with the abdomen's pitch)
      const sock = this._tmp2.copy(limb.S);
      S.body.localToWorld(sock); S.abdPivot.worldToLocal(sock);
      const xs = Math.abs(sock.x), zs = sock.z;
      // (a) brushing point at elevation PHI on the flank, in the middle of the setae layer
      const z = lerp(-1.45, -2.75, u);
      abdSlice(z, _sl, 0.5);                                           // halfway into the long setae
      const PHI = 0.5;
      const cx = _sl.X * Math.cos(PHI), cyy = _sl.yc + _sl.Y * Math.sin(PHI);
      // (b) clear point on the line from the coxa tangent to the abdomen outline (kick-off / return)
      const margin = (limb.R[4] || 0.12) + 0.2;
      let k = 0;
      for (let i = 0; i < ABD_Z.length; i++) {
        if (ABD_Z[i] > zs - 0.3) continue;
        k = Math.max(k, (ABD_X[i] + margin - xs) / (zs - ABD_Z[i]));
      }
      const zk = lerp(-1.45, -3.2, u);
      const tx = xs + k * (zs - zk) + 0.4, ty = cyy + 0.45;
      // contact while brushing backward, leave toward the clear line at the end of the stroke and on the return
      const leave = Math.max(1 - back, ss(0.72, 0.98, u));
      const x = side * lerp(cx, tx, leave);
      const y = lerp(cyy, ty, leave);
      const zz = lerp(z, zk, leave);
      const pl = this._tmp2.set(x, y, zz);
      // contact normal = envelope gradient (tarsus lies along the setae), blending to "outward" when clear
      const nl = this._tmpN2.set(side * Math.cos(PHI) / _sl.X, Math.sin(PHI) / _sl.Y, 0).normalize()
        .lerp(this._tmpW2 || (this._tmpW2 = new THREE.Vector3()).set(side * 0.9, 0.35, 0).normalize(), leave).normalize();
      S.abdPivot.localToWorld(pl);
      nl.applyQuaternion(S.abdPivot.getWorldQuaternion(this._tmpQ2));
      // knee raised during contact so femur/patella arc over the flank instead of into the pile
      pose = this._solveWorld(f, pl, nl, 0.05, 0.75 * active);
      w = ew;
      // urticating setae come off where the tarsus scrapes and as it kicks off the abdomen
      if (active > 0.5 && back > 0.5 && u > 0.3 && u < 0.95 && this.emitHairs) this.emitHairs(limb.tip, side, dt);
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
