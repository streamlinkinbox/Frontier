// Flick (R) audit: hind legs (leg IV) vs the abdomen and the rock, sampled every frame of the action.
// The abdomen is described by two per-slice ellipses measured with probes/abdsil.js (same tables as
// ABD_* in src/spider/Actions.js): the BODY (+0.1 cm short pile) and the tips of the long SETAE.
//
//  bodyPen*      frames where a leg-IV podomere (femur..metatarsus) enters the body; *Worst = depth, cm
//  tarsusBody    deepest the tarsus gets into the body (cm) - it may brush the setae, not the body
//  topBody*      a podomere (femur..metatarsus) lies over the abdomen BODY when seen from straight above
//  contact       active-stroke leg-frames where the tarsus tip is in the setae layer (touching the hair)
//  *Active       counted only while flicking (not during the blend from / back to the stance)
(() => {
  const g = __game, T = g.THREE, s = g.spider, w = g.world;
  g.setMode('manual'); const h = w.raycast(new T.Vector3(6, 80, -2), new T.Vector3(0, -1, 0), 120);
  s.placeAt(h.point, h.normal, new T.Vector3(1, 0, 0.2).normalize()); g.step(3);
  const Z = [0, -0.25, -0.5, -0.75, -1, -1.25, -1.5, -1.75, -2, -2.25, -2.5, -2.75, -3, -3.25, -3.5, -3.75];
  const BX = [0, 0.56, 0.82, 1.03, 1.15, 1.21, 1.22, 1.2, 1.14, 1.04, 0.86, 0.67, 0.44, 0.2, 0, 0];
  const BT = [0.01, 0.53, 0.78, 1, 1.11, 1.18, 1.19, 1.18, 1.12, 1.02, 0.84, 0.65, 0.42, 0.2, 0, 0];
  const BB = [0.01, -0.37, -0.53, -0.66, -0.73, -0.76, -0.76, -0.75, -0.71, -0.64, -0.54, -0.43, -0.28, -0.12, 0, 0];
  const HX = [0.44, 0.91, 1.25, 1.61, 1.71, 1.8, 1.84, 1.94, 1.77, 1.7, 1.56, 1.43, 1.2, 0.99, 0.97, 0.92];
  const HT = [-0.24, 0.9, 1.11, 1.4, 1.56, 1.87, 1.81, 1.75, 1.76, 1.65, 1.53, 1.27, 1.14, 0.83, 0.53, 0.43];
  const HB = [-0.24, -0.64, -0.79, -1, -1.04, -1.13, -1.16, -1.04, -1.03, -0.94, -1.02, -0.91, -0.81, -1.15, -0.91, -0.68];
  const slice = (z, hair) => {
    if (z > Z[0] || z < Z[Z.length - 1]) return null;
    let i = 0; while (i < Z.length - 2 && Z[i + 1] > z) i++;
    const k = (Z[i] - z) / (Z[i] - Z[i + 1]), L = (a) => a[i] + (a[i + 1] - a[i]) * k;
    const X = hair ? L(HX) : L(BX) + 0.1, top = hair ? L(HT) : L(BT) + 0.1, bot = hair ? L(HB) : L(BB) - 0.1;
    if (X < 0.06) return null;
    return { X, yc: (top + bot) / 2, Y: Math.max((top - bot) / 2, 0.05) };
  };
  // signed distance-ish (cm) from an ellipse slice: negative inside
  const sd = (p, e, r = 0) => (Math.hypot(p.x / (e.X + r), (p.y - e.yc) / (e.Y + r)) - 1) * Math.min(e.X + r, e.Y + r);
  s.actions.trigger('flick');
  const res = { frames: 0, activeFrames: 0, bodyPenFrames: 0, bodyPenActive: 0, bodyWorst: 0, tarsusBody: 0,
    topBodyActive: 0, topBodyWorst: 0, contact: 0, tipFrames: 0, rockPenFrames: 0, rockWorst: 0, minReach: 9 };
  const inv = new T.Matrix4(), v = new T.Vector3(), p = new T.Vector3(), mw = s.body.matrixWorld;
  for (let i = 0; i < 150; i++) {
    g.step(1 / 60); res.frames++;
    const act = s.actions.current && s.actions.current.t > 0.5 && s.actions.current.t < 1.85;
    if (act) res.activeFrames++;
    inv.copy(s.abdPivot.matrixWorld).invert();
    let body = 0, top = 0, rock = 0;
    for (const f of s.feet) {
      if (f.isPalp || f.limb.legIndex !== 3) continue;
      const J = f.limb.joints, R = f.limb.R, n = f.limb.n;
      for (let j = 2; j < n; j++) for (const k of [0, 0.25, 0.5, 0.75]) {
        const isTar = j === n - 1, r = R[j] || 0.1;
        v.copy(J[j]).lerp(J[j + 1] || f.limb.tip, k).applyMatrix4(mw);
        const rr = w.closest(v, 2); if (rr) rock = Math.min(rock, rr.distance - r);
        p.copy(v).applyMatrix4(inv);
        const e = slice(p.z, false); if (!e) continue;
        const d = sd(p, e, r);
        if (isTar) { res.tarsusBody = Math.min(res.tarsusBody, d); continue; }
        body = Math.min(body, d);
        const dx = Math.abs(p.x) - (e.X + r);
        if (dx < 0 && p.y > e.yc) top = Math.min(top, dx);
      }
      if (act) {
        res.tipFrames++;
        p.copy(f.limb.tip).applyMatrix4(mw).applyMatrix4(inv);
        const eh = slice(p.z, true), eb = slice(p.z, false);
        if (eh && sd(p, eh) < 0 && (!eb || sd(p, eb) > 0)) res.contact++;
        res.minReach = Math.min(res.minReach, f.limb.tip.distanceTo(f.limb.S) / f.limb.maxReach);
      }
    }
    if (body < -0.02) { res.bodyPenFrames++; if (act) res.bodyPenActive++; }
    res.bodyWorst = Math.min(res.bodyWorst, body);
    if (act && top < -0.02) res.topBodyActive++;
    if (act) res.topBodyWorst = Math.min(res.topBodyWorst, top);
    if (rock < -0.05) res.rockPenFrames++; res.rockWorst = Math.min(res.rockWorst, rock);
  }
  for (const k in res) res[k] = +(+res[k]).toFixed(2);
  return res;
})()
