// Flick (R) audit: hind legs (leg IV) vs the abdomen - including its setae pile - and the rock.
//  hairPen*  : a leg-IV sample point inside the hairy envelope of the abdomen (3D)
//  top*      : a leg-IV sample point lies over the abdomen when seen from straight above (abdomen frame)
//  *Active   : the same, counted only during the flicking itself (not the blend in/out of the stance)
//  tipNear   : active-stroke frames where the tarsus tip comes within 0.3 cm of the hair envelope
// Envelope = per-z-slice ellipse measured from the real geometry by probes/abdsil.js.
(() => {
  const g = __game, T = g.THREE, s = g.spider, w = g.world;
  g.setMode('manual'); const h = w.raycast(new T.Vector3(6, 80, -2), new T.Vector3(0, -1, 0), 120);
  s.placeAt(h.point, h.normal, new T.Vector3(1, 0, 0.2).normalize()); g.step(3);
  const Z = [0.5, 0, -0.5, -1, -1.5, -2, -2.5, -3, -3.5, -4];
  const X = [0.02, 0.67, 1.3, 1.61, 1.84, 1.74, 1.62, 1.02, 0.91, 0.07];
  const YMAX = [0.02, 0.45, 1.19, 1.63, 1.81, 1.73, 1.47, 1.22, 0.67, -0.34];
  const YMIN = [-0.02, -0.45, -0.82, -1.04, -1.13, -1.01, -0.97, -0.85, -0.91, -0.34];
  const sil = (z) => {
    if (z >= Z[0] || z <= Z[Z.length - 1]) return null;
    let i = 0; while (Z[i + 1] > z) i++;
    const k = (Z[i] - z) / (Z[i] - Z[i + 1]), L = (a) => a[i] + (a[i + 1] - a[i]) * k;
    return { X: L(X), yc: (L(YMAX) + L(YMIN)) / 2, Y: (L(YMAX) - L(YMIN)) / 2 };
  };
  s.actions.trigger('flick');
  const res = { frames: 0, hairPenFrames: 0, hairWorst: 0, topFrames: 0, topWorst: 0, hairPenActive: 0, topActive: 0, topWorstActive: 0, activeFrames: 0, rockPenFrames: 0, rockWorst: 0, tipNear: 0, tipFrames: 0, minReach: 9 };
  const inv = new T.Matrix4(), v = new T.Vector3(), p = new T.Vector3(), mw = s.body.matrixWorld;
  for (let i = 0; i < 150; i++) {
    g.step(1 / 60); res.frames++;
    inv.copy(s.abdPivot.matrixWorld).invert();
    let hair = 0, top = 0, rock = 0;
    for (const f of s.feet) {
      if (f.isPalp || f.limb.legIndex !== 3) continue;
      const J = f.limb.joints, R = f.limb.R;
      for (let j = 2; j < f.limb.n; j++) for (const k of [0, 0.25, 0.5, 0.75]) {
        v.copy(J[j]).lerp(J[j + 1] || f.limb.tip, k).applyMatrix4(mw);
        const rr = w.closest(v, 2); if (rr) rock = Math.min(rock, rr.distance - R[j]);
        p.copy(v).applyMatrix4(inv);
        const e = sil(p.z); if (!e) continue;
        const r = R[j] || 0.1;
        // 3D: normalised radius in the slice ellipse, converted back to an approximate cm depth
        const q = Math.hypot(p.x / (e.X + r), (p.y - e.yc) / (e.Y + r));
        if (q < 1) hair = Math.min(hair, (q - 1) * Math.min(e.X, e.Y));
        // top view: inside the silhouette half-width and not hidden underneath the abdomen
        const dx = Math.abs(p.x) - (e.X + r);
        if (dx < 0 && p.y > e.yc) top = Math.min(top, dx);
      }
      const act = s.actions.current && s.actions.current.t > 0.5 && s.actions.current.t < 1.85;
      if (act) {
        res.tipFrames++;
        p.copy(f.limb.tip).applyMatrix4(mw).applyMatrix4(inv);
        const e = sil(p.z);
        if (e && Math.hypot(p.x / (e.X + 0.3), (p.y - e.yc) / (e.Y + 0.3)) < 1) res.tipNear++;
        res.minReach = Math.min(res.minReach, f.limb.tip.distanceTo(f.limb.S) / f.limb.maxReach);
      }
    }
    const inStroke = s.actions.current && s.actions.current.t > 0.5 && s.actions.current.t < 1.85;
    if (inStroke) {
      res.activeFrames++;
      if (hair < -0.02) res.hairPenActive++;
      if (top < -0.02) res.topActive++;
      res.topWorstActive = Math.min(res.topWorstActive, top);
    }
    if (hair < -0.02) res.hairPenFrames++; res.hairWorst = Math.min(res.hairWorst, hair);
    if (top < -0.02) res.topFrames++; res.topWorst = Math.min(res.topWorst, top);
    if (rock < -0.05) res.rockPenFrames++; res.rockWorst = Math.min(res.rockWorst, rock);
  }
  for (const k in res) res[k] = +(+res[k]).toFixed(2);
  return res;
})()
