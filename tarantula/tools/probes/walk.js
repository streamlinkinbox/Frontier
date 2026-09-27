// Walking audit (the numbers in IMPROVING.md). 4 seeded AI runs x 60 s at 60 fps.
// Reports how often pedipalps / legs intersect rock, how often palps are cramped, idle stability is in idle.js.
(() => {
  const g = __game, T = g.THREE, s = g.spider, w = g.world;
  const tot = { frames: 0, palp01: 0, palp03: 0, palpWorst: 0, palpCramped: 0, femMax: -9, nan: 0, leg03: 0, leg1: 0, legWorst: 0 };
  for (const seed of [12345, 2026, 31337, 777]) {
    // deterministic run: the AI uses Math.random
    let _s = seed; Math.random = () => { _s |= 0; _s = (_s + 0x6D2B79F5) | 0; let t = Math.imul(_s ^ (_s >>> 15), 1 | _s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
    const sp = g.cave.findSpawn(); s.placeAt(sp.point, sp.normal, new T.Vector3(1, 0, 0)); g.setMode('ai');
    for (let i = 0; i < 3600; i++) {
      g.step(1 / 60); tot.frames++;
      const mw = s.body.matrixWorld; let pw = 0, lw = 0;
      for (const f of s.feet) {
        const L = f.limb;
        if (f.pose.a.some((x) => !isFinite(x))) tot.nan++;
        if (f.isPalp) {
          for (let j = 2; j <= L.n; j++) { const p = (j === L.n ? L.tip : L.joints[j]).clone().applyMatrix4(mw); const h = w.closest(p, 3); if (h) pw = Math.min(pw, h.distance - (j === L.n ? 0.03 : L.R[j])); }
          if (!f.swinging) { const r = f.pos.distanceTo(L.S.clone().applyMatrix4(s.root.matrixWorld)) / L.maxReach; if (r < 0.45) tot.palpCramped++; }
          tot.femMax = Math.max(tot.femMax, f.pose.a[2]);
        } else for (const j of [3, 4, 5]) { const p = L.joints[j].clone().applyMatrix4(mw); const h = w.closest(p, 3); if (h) lw = Math.min(lw, h.distance - L.R[j]); }
      }
      if (pw < -0.1) tot.palp01++; if (pw < -0.3) tot.palp03++; tot.palpWorst = Math.min(tot.palpWorst, pw);
      if (lw < -0.3) tot.leg03++; if (lw < -1) tot.leg1++; tot.legWorst = Math.min(tot.legWorst, lw);
    }
  }
  for (const k in tot) tot[k] = +(+tot[k]).toFixed(2);
  return tot;
})()
