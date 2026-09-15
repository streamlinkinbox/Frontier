// Walk-in-place gait engine + idle behavior for the T. rex.
// Pure math (no three.js dependency): outputs FK poses, IK targets and events.

const TAU = Math.PI * 2;
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const lerp = (a, b, t) => a + (b - a) * t;
const sstep = (t) => t * t * (3 - 2 * t);
const smoother = (t) => t * t * t * (t * (t * 6 - 15) + 10);
// Keyframe track: K(x, [[t,v],...]) with smoothstep interpolation.
export function K(x, keys) {
  if (x <= keys[0][0]) return keys[0][1];
  for (let i = 1; i < keys.length; i++) {
    if (x <= keys[i][0]) {
      const p = keys[i - 1], c = keys[i];
      return lerp(p[1], c[1], sstep((x - p[0]) / Math.max(1e-6, c[0] - p[0])));
    }
  }
  return keys[keys.length - 1][1];
}

export const GAIT = {
  stance: 0.6,
  stepFront: 0.55,
  stepBack: 0.65,
  ankleBase: 0.7,
  hipX: 0.44,
  pelvisY: 3.02,
};

function blankPose() {
  const e = () => ({ x: 0, y: 0, z: 0 });
  return {
    pelvis: { pos: { x: 0, y: GAIT.pelvisY, z: 0 }, rot: e() },
    spine: e(), neck1: e(), neck2: e(), head: e(),
    jaw: 0, tailRoot: e(),
    shL: e(), shR: e(), elL: 0, elR: 0,
    legL: { ankle: { x: 0, y: 0, z: 0 }, alpha: 0, beta: 0, yaw: 0 },
    legR: { ankle: { x: 0, y: 0, z: 0 }, alpha: 0, beta: 0, yaw: 0 },
  };
}

function legTargets(q, out) {
  const { stance, stepFront, stepBack, ankleBase, hipX } = GAIT;
  if (q < stance) {
    const s = q / stance;
    out.ankle.z = lerp(stepFront, -stepBack, s);       // constant belt speed
    out.ankle.y = K(q, [[0, ankleBase], [0.3, ankleBase], [0.45, 0.72], [0.6, 0.92]]);
    out.ankle.x = hipX + 0.02;
    out.alpha = K(q, [[0, -0.2], [0.25, -0.2], [0.6, -0.7]]);
  } else {
    const s = (q - stance) / (1 - stance);
    const e = smoother(clamp(s, 0, 1));
    out.ankle.z = lerp(-stepBack, stepFront, e);
    out.ankle.y = lerp(0.92, ankleBase, e) + 0.4 * Math.sin(Math.PI * clamp(s, 0, 1));
    out.ankle.x = hipX + 0.02 + 0.05 * Math.sin(Math.PI * clamp(s, 0, 1));
    out.alpha = K(s, [[0, -0.7], [0.3, 0.25], [0.65, 0.25], [1, -0.2]]);
  }
  out.beta = K(q, [[0, 0.05], [0.45, 0.05], [0.6, 0.6], [0.72, 0.55], [0.82, 0.3], [1, 0.05]]);
  out.yaw = 0.07;
}

export function createGait() {
  const S = {
    mode: 'idle',
    speed: 1.2,
    phase: 0,
    blend: 0,
    prevQL: 0,
    prevQR: 0.5,
  };
  const walk = blankPose();
  const idle = blankPose();
  const out = {
    ...blankPose(),
    events: [],
    qL: 0, qR: 0.5, stanceL: true, stanceR: false,
    freq: 0, belt: 0, phase: 0, blend: 0,
  };

  function computeWalk(p, time) {
    const w2 = TAU * p;                    // stride angle
    const sway = Math.sin(w2 - 0.314);     // lateral weight signal
    // -- pelvis --
    walk.pelvis.pos.x = 0.09 * sway;
    walk.pelvis.pos.y = GAIT.pelvisY + 0.05 * Math.cos(2 * w2 + 2.14);
    walk.pelvis.pos.z = 0;
    walk.pelvis.rot.x = 0.06 + 0.015 * Math.cos(2 * w2 + 2.14);
    walk.pelvis.rot.y = -0.12 * sway;
    walk.pelvis.rot.z = 0.07 * sway;
    // -- spine counter-rotation --
    const breath = Math.sin(time * 1.3);
    walk.spine.x = 0.05 + 0.012 * Math.cos(2 * w2 + 2.14) + 0.015 * breath;
    walk.spine.y = 0.08 * sway;
    walk.spine.z = -0.045 * sway;
    // -- gaze stabilization across neck + head --
    const tYaw = walk.pelvis.rot.y + walk.spine.y;
    const tPitch = walk.pelvis.rot.x + walk.spine.x - 0.11;
    const tRoll = walk.pelvis.rot.z + walk.spine.z;
    const lookY = 0.4 * (0.2 * Math.sin(0.37 * time) + 0.12 * Math.sin(0.23 * time + 2));
    const lookP = 0.03 * Math.sin(0.31 * time + 1);
    walk.neck1.x = -0.1 - tPitch * 0.4; walk.neck1.y = -tYaw * 0.4; walk.neck1.z = -tRoll * 0.4;
    walk.neck2.x = -0.08 - tPitch * 0.35; walk.neck2.y = -tYaw * 0.35; walk.neck2.z = -tRoll * 0.35;
    walk.head.x = -0.05 - tPitch * 0.25 + lookP; walk.head.y = -tYaw * 0.25 + lookY; walk.head.z = -tRoll * 0.25;
    // -- jaw: near-closed with breath + stride micro-motion --
    walk.jaw = 0.04 + 0.008 * breath + 0.01 * Math.cos(2 * w2 + 1);
    // -- tail base: lagged counter-sway --
    walk.tailRoot.x = 0.02 * Math.cos(2 * w2 + 2.9);
    walk.tailRoot.y = 0.06 * Math.sin(w2 - 0.314 - 1.2);
    walk.tailRoot.z = 0;
    // -- tiny arm counter-swing --
    walk.shL.x = 0.12 * Math.cos(w2); walk.shR.x = -0.12 * Math.cos(w2);
    walk.shL.y = 0; walk.shR.y = 0; walk.shL.z = 0.05; walk.shR.z = -0.05;
    walk.elL = -0.32 + 0.04 * Math.sin(w2 + 2); walk.elR = -0.32 + 0.04 * Math.sin(w2 + 2 + Math.PI);
    // -- legs --
    legTargets(p % 1, walk.legL);
    walk.legL.ankle.x = Math.abs(walk.legL.ankle.x);
    legTargets((p + 0.5) % 1, walk.legR);
    walk.legR.ankle.x = -Math.abs(walk.legR.ankle.x);
  }

  function computeIdle(time) {
    const breath = Math.sin(time * 1.3);
    const breath2 = Math.sin(time * 1.3 + 0.6);
    const shift = Math.sin(0.25 * time);
    idle.pelvis.pos.x = 0.06 * shift;
    idle.pelvis.pos.y = GAIT.pelvisY + 0.015 * breath;
    idle.pelvis.pos.z = 0;
    idle.pelvis.rot.x = 0.06 + 0.008 * breath;
    idle.pelvis.rot.y = 0.02 * Math.sin(0.2 * time);
    idle.pelvis.rot.z = 0.03 * Math.sin(0.25 * time + 0.4);
    idle.spine.x = 0.05 + 0.02 * breath2;
    idle.spine.y = 0.03 * Math.sin(0.3 * time + 1);
    idle.spine.z = -0.015 * shift;
    const lookY = 0.25 * Math.sin(0.37 * time) + 0.15 * Math.sin(0.23 * time + 2);
    const lookP = -0.05 + 0.08 * Math.sin(0.31 * time + 1) + 0.01 * breath;
    const lookR = 0.03 * Math.sin(0.27 * time);
    idle.neck1.x = -0.1 + lookP * 0.3; idle.neck1.y = lookY * 0.3; idle.neck1.z = lookR * 0.3;
    idle.neck2.x = -0.08 + lookP * 0.3; idle.neck2.y = lookY * 0.3; idle.neck2.z = lookR * 0.3;
    idle.head.x = -0.05 + lookP * 0.4; idle.head.y = lookY * 0.4; idle.head.z = lookR * 0.4;
    const gape = Math.pow(Math.max(0, Math.sin(time * 0.55 + 1.7)), 30);
    idle.jaw = 0.04 + 0.012 * breath + 0.5 * gape;
    idle.tailRoot.x = 0.02 * Math.sin(0.5 * time);
    idle.tailRoot.y = 0.05 * Math.sin(0.4 * time + 1);
    idle.tailRoot.z = 0;
    idle.shL.x = 0.05 + 0.02 * breath; idle.shR.x = 0.05 + 0.02 * breath2;
    idle.shL.y = 0; idle.shR.y = 0; idle.shL.z = 0.05; idle.shR.z = -0.05;
    idle.elL = -0.32 + 0.02 * Math.sin(0.4 * time); idle.elR = -0.32 + 0.02 * Math.sin(0.4 * time + 1);
    for (const [leg, sx] of [['legL', 1], ['legR', -1]]) {
      idle[leg].ankle.x = sx * (GAIT.hipX + 0.02);
      idle[leg].ankle.y = GAIT.ankleBase;
      idle[leg].ankle.z = 0.12;
      idle[leg].alpha = -0.22; idle[leg].beta = 0.05; idle[leg].yaw = 0.07;
    }
  }

  function mix(a, b, t) { return a + (b - a) * t; }
  function mixE(o, A, B, w) {
    o.x = mix(A.x, B.x, w); o.y = mix(A.y, B.y, w); o.z = mix(A.z, B.z, w);
  }

  return {
    S,
    get mode() { return S.mode; },
    setMode(m) { if (m === 'idle' || m === 'walk') S.mode = m; },
    setSpeed(v) { S.speed = clamp(v, 0.4, 2.2); },

    update(dt, time) {
      out.events.length = 0;
      const target = S.mode === 'walk' ? 1 : 0;
      S.blend += (target - S.blend) * (1 - Math.exp(-dt * 3));
      if (Math.abs(target - S.blend) < 0.001) S.blend = target;
      const w = S.blend;

      const strideLen = 2 * (GAIT.stepFront + GAIT.stepBack);
      const freq = S.speed / strideLen;
      if (S.mode === 'walk') S.phase = (S.phase + dt * freq) % 1;
      const p = S.phase;

      computeWalk(p, time);
      computeIdle(time);

      // blend walk <-> idle
      mixE(out.pelvis.pos, idle.pelvis.pos, walk.pelvis.pos, w);
      mixE(out.pelvis.rot, idle.pelvis.rot, walk.pelvis.rot, w);
      mixE(out.spine, idle.spine, walk.spine, w);
      mixE(out.neck1, idle.neck1, walk.neck1, w);
      mixE(out.neck2, idle.neck2, walk.neck2, w);
      mixE(out.head, idle.head, walk.head, w);
      mixE(out.tailRoot, idle.tailRoot, walk.tailRoot, w);
      mixE(out.shL, idle.shL, walk.shL, w);
      mixE(out.shR, idle.shR, walk.shR, w);
      out.jaw = mix(idle.jaw, walk.jaw, w);
      // walk keeps a smaller autonomous gape so mouth hydraulics stay alive
      out.jaw += (1 - w * 0.5) * 0; // (idle gape already blended)
      out.elL = mix(idle.elL, walk.elL, w);
      out.elR = mix(idle.elR, walk.elR, w);
      for (const leg of ['legL', 'legR']) {
        mixE(out[leg].ankle, idle[leg].ankle, walk[leg].ankle, w);
        out[leg].alpha = mix(idle[leg].alpha, walk[leg].alpha, w);
        out[leg].beta = mix(idle[leg].beta, walk[leg].beta, w);
        out[leg].yaw = mix(idle[leg].yaw, walk[leg].yaw, w);
      }

      // footfall events
      const qL = p % 1, qR = (p + 0.5) % 1;
      if (S.mode === 'walk' && w > 0.5) {
        if (S.prevQL > qL) out.events.push({ type: 'strike', leg: 'L' });
        if (S.prevQL < GAIT.stance && qL >= GAIT.stance) out.events.push({ type: 'off', leg: 'L' });
        if (S.prevQR > qR) out.events.push({ type: 'strike', leg: 'R' });
        if (S.prevQR < GAIT.stance && qR >= GAIT.stance) out.events.push({ type: 'off', leg: 'R' });
      }
      S.prevQL = qL; S.prevQR = qR;

      out.qL = qL; out.qR = qR;
      out.stanceL = qL < GAIT.stance; out.stanceR = qR < GAIT.stance;
      out.freq = freq; out.phase = p; out.blend = w;
      out.belt = ((GAIT.stepFront + GAIT.stepBack) / GAIT.stance) * freq * w;

      return out;
    },
  };
}
