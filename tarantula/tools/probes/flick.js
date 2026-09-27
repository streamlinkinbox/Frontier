// Flick (R) audit: leg IV segments vs the abdomen ellipsoid and the rock, over the whole action.
(() => {
  const g = __game, T = g.THREE, s = g.spider, w = g.world;
  g.setMode('manual'); const h = w.raycast(new T.Vector3(6, 80, -2), new T.Vector3(0, -1, 0), 120);
  s.placeAt(h.point, h.normal, new T.Vector3(1, 0, 0.2).normalize()); g.step(3);
  const cy = 0.34, cz = -1.98, ra = 1.45, rb = 1.2, rc = 1.75, core = 0.8; // core = abdomen without pile
  s.actions.trigger('flick');
  const res = { frames: 0, abdPenFrames: 0, abdWorst: 0, rockPenFrames: 0, rockWorst: 0, tipOnAbd: 0, tipFrames: 0 };
  const inv = new T.Matrix4(), v = new T.Vector3(), mw = s.body.matrixWorld;
  for (let i = 0; i < 150; i++) {
    g.step(1 / 60); res.frames++;
    inv.copy(s.abdPivot.matrixWorld).invert();
    let worstA = 0, worstR = 0;
    for (const f of s.feet) { if (f.isPalp || f.limb.legIndex !== 3) continue; const J = f.limb.joints;
      for (let j = 3; j < f.limb.n; j++) for (const k of [0, 0.5]) {
        v.copy(J[j]).lerp(J[j + 1] || f.limb.tip, k).applyMatrix4(mw);
        const rr = w.closest(v, 2); if (rr) worstR = Math.min(worstR, rr.distance - f.limb.R[j]);
        const p = v.clone().applyMatrix4(inv);
        // normalised ellipsoid "radius" of the pile-free core; <1 means inside the abdomen body
        const e = Math.hypot(p.x / (ra * core), (p.y - cy) / (rb * core), (p.z - cz) / (rc * core));
        worstA = Math.min(worstA, e - 1);
      }
      const t = f.limb.tip.clone().applyMatrix4(mw).applyMatrix4(inv);
      const et = Math.hypot(t.x / ra, (t.y - cy) / rb, (t.z - cz) / rc);
      const act = s.actions.current && s.actions.current.t > 0.5 && s.actions.current.t < 1.85;
      if (act) { res.tipFrames++; if (et < 1.35) res.tipOnAbd++; }
    }
    if (worstA < -0.02) res.abdPenFrames++; res.abdWorst = Math.min(res.abdWorst, worstA);
    if (worstR < -0.05) res.rockPenFrames++; res.rockWorst = Math.min(res.rockWorst, worstR);
  }
  for (const k in res) res[k] = +(+res[k]).toFixed(2);
  return res;
})()
