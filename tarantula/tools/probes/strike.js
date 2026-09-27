// Strike (E/Space) audit: how folded legs I-II get (tip distance / max reach) and rock contact.
(() => {
  const g = __game, T = g.THREE, s = g.spider, w = g.world;
  g.setMode('manual'); const h = w.raycast(new T.Vector3(6, 80, -2), new T.Vector3(0, -1, 0), 120);
  s.placeAt(h.point, h.normal, new T.Vector3(1, 0, 0.2).normalize()); g.step(3);
  s.actions.trigger('strike');
  const res = { minFold: 9, foldBelow45: 0, rockPenFrames: 0, rockWorst: 0, popMax: 0 };
  const mw = s.body.matrixWorld, v = new T.Vector3(); let prev = null;
  for (let i = 0; i < 140; i++) {
    g.step(1 / 60);
    let worstR = 0; const tips = [];
    for (const f of s.feet) { if (f.isPalp || f.limb.legIndex > 1) continue; const L = f.limb;
      const tip = L.tip.clone().applyMatrix4(mw); tips.push(tip);
      const r = tip.distanceTo(L.S.clone().applyMatrix4(mw)) / L.maxReach; res.minFold = Math.min(res.minFold, r);
      if (r < 0.45 && s.actions.current) res.foldBelow45++;
      for (let j = 2; j < L.n; j++) { v.copy(L.joints[j]).applyMatrix4(mw); const c = w.closest(v, 2); if (c) worstR = Math.min(worstR, c.distance - L.R[j]); }
    }
    if (prev) for (let k = 0; k < tips.length; k++) res.popMax = Math.max(res.popMax, tips[k].distanceTo(prev[k]));
    prev = tips;
    if (worstR < -0.05) res.rockPenFrames++; res.rockWorst = Math.min(res.rockWorst, worstR);
  }
  for (const k in res) res[k] = +(+res[k]).toFixed(2);
  return res;
})()
