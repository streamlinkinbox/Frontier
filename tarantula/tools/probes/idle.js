// Idle audit: the spider is placed at 5 spots (floor, wall, slope) and left alone for 30 s.
// Any foot/palp step while standing still counts (target: 0 everywhere).
(() => {
  let _s = 12345; Math.random = () => { _s |= 0; _s = (_s + 0x6D2B79F5) | 0; let t = Math.imul(_s ^ (_s >>> 15), 1 | _s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  const g = __game, T = g.THREE, s = g.spider, w = g.world;
  g.setMode('manual');
  const out = [];
  for (const [x, z] of [[6, -2], [-20, 10], [25, 30], [-40, -30], [10, -40]]) {
    const c = w.raycast(new T.Vector3(x, 80, z), new T.Vector3(0, -1, 0), 120); if (!c) continue;
    s.placeAt(c.point, c.normal, new T.Vector3(1, 0, 0.2).normalize()); g.step(3);
    const was = s.feet.map((f) => f.swinging); let steps = 0;
    for (let i = 0; i < 30 * 60; i++) { g.step(1 / 60); s.feet.forEach((f, k) => { if (f.swinging && !was[k]) steps++; was[k] = f.swinging; }); }
    out.push({ at: [x, z], surfaceNormalY: +c.normal.y.toFixed(2), steps });
  }
  return out;
})()
