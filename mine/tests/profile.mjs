// Drivability of the vertical profile:
//  - tunnel crest radius >= 60 m (car stays planted up to ~90 km/h before downforce)
//  - grade <= 20 %
//  - the overpass keeps >= 1.5 m of rock between the two tunnels
import { createMaze, DEFAULT_PARAMS } from '../src/network.js';
import { buildMine } from '../src/mineBuilder.js';
let failed = false;
for (const seed of [7, 1, 2, 3, 42]) {
  const net = createMaze(seed); const m = buildMine(net, DEFAULT_PARAMS);
  const byEdge = {};
  m.centerline.forEach((c) => (byEdge[c.edge] ||= []).push(c));
  let minR = 1e9, maxG = 0;
  for (const e in byEdge) {
    const cl = byEdge[e];
    // neighbours ~3 m away along the tunnel (edge loops are denser around potholes)
    let ia = 0, ic = 0;
    for (let i = 0; i < cl.length; i++) {
      while (ia < i && cl[i].s - cl[ia + 1].s >= 3) ia++;
      while (ic < cl.length - 1 && cl[ic].s - cl[i].s < 3) ic++;
      if (cl[i].s - cl[ia].s < 2.9 || cl[ic].s - cl[i].s < 2.9) continue;
      const a = cl[ia], b = cl[i], c = cl[ic];
      const s1 = (b.p.y - a.p.y) / Math.hypot(b.p.x - a.p.x, b.p.z - a.p.z);
      const s2 = (c.p.y - b.p.y) / Math.hypot(c.p.x - b.p.x, c.p.z - b.p.z);
      const k = (s2 - s1) / ((c.s - a.s) / 2);
      maxG = Math.max(maxG, Math.abs(s1));
      if (k < 0) minR = Math.min(minR, -1 / k);
    }
  }
  const up = net.edges.findIndex((e) => e.a === 2 && e.b === 7), dn = net.edges.findIndex((e) => e.a === 1 && e.b === 8);
  let best = null;
  for (const u of byEdge[up]) for (const d of byEdge[dn]) {
    const h = Math.hypot(u.p.x - d.p.x, u.p.z - d.p.z);
    if (!best || h < best.h) best = { h, gap: u.p.y - d.p.y };
  }
  const rock = best.gap - DEFAULT_PARAMS.roofHeight - DEFAULT_PARAMS.rockNoise;
  const ok = minR >= 60 && maxG <= 0.2 && rock >= 1.5;
  if (!ok) failed = true;
  console.log(`seed ${seed}: ${ok ? 'OK ' : 'FAIL'} min crest radius ${minR.toFixed(0)} m (liftoff ≈ ${(Math.sqrt(9.81 * minR) * 3.6).toFixed(0)} km/h) · max grade ${(maxG * 100).toFixed(0)}% · overpass rock ${rock.toFixed(1)} m`);
}
process.exit(failed ? 1 : 0);
