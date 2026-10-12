// Subway: cut-and-cover box tunnels with twin tracks, station boxes with platforms and
// street-level entrances placed on the nearest wide sidewalk.
import { Centerline } from '../centerline.js';
import { Out, ribbon, wall, yawFor } from './out.js';
import { getProfile, layout, LEVEL } from '../profiles.js';

export function buildSubway(net, line, index, ctx) {
  const out = new Out(line.id);
  const c = new Centerline(line.pts.map(p => ({ x: p.x, z: p.z, y: -(line.depth || 16) })));
  const stations = (line.stations || []).map(st => ({ ...st, s0: st.s - 70, s1: st.s + 70 }));
  const inSt = s => stations.find(st => s > st.s0 && s < st.s1);
  const extra = stations.flatMap(st => [st.s0 - 12, st.s0, st.s1, st.s1 + 12]);
  const st = c.stations(0, c.length, extra, 12, 0.03);
  const frames = st.map(s => c.frameAt(s));
  const halfW = s => { const S = inSt(s); if (S) return 11; const near = stations.find(x => s > x.s0 - 12 && s < x.s1 + 12); if (!near) return 4.8; const d = s < near.s0 ? near.s0 - s : s - near.s1; return 11 - (11 - 4.8) * (d / 12); };
  const H = 6.2;
  // floor, walls, ceiling (inner faces; material is double sided)
  ribbon(out, 'tunnel', frames, (k, f) => -halfW(f.s), (k, f) => halfW(f.s), 0);
  ribbon(out, 'tunnel', frames, (k, f) => halfW(f.s), (k, f) => -halfW(f.s), H);
  wall(out, 'tunnel', frames, f => -halfW(f.s), 0, H, 1);
  wall(out, 'tunnel', frames, f => halfW(f.s), 0, H, -1);
  // tracks
  for (const o of [-2.0, 2.0]) for (const g of [-0.7175, 0.7175]) ribbon(out, 'steel', frames, o + g - 0.04, o + g + 0.04, 0.18);
  for (const o of [-2.0, 2.0]) ribbon(out, 'concrete', frames, o - 1.3, o + 1.3, 0.12);
  // station platforms + entrances
  for (const S of stations) {
    const pf = c.stations(S.s0, S.s1, [], 10).map(s => c.frameAt(s));
    for (const side of [-1, 1]) {
      const a = side * 3.6, b = side * 10.8;
      ribbon(out, 'platform', pf, Math.min(a, b), Math.max(a, b), 1.1);
      wall(out, 'platformEdge', pf, a, 0, 1.1, -side);
      ribbon(out, 'markYellow', pf, side > 0 ? a + 0.1 : a - 0.6, side > 0 ? a + 0.6 : a - 0.1, 1.112);
    }
    for (let s = S.s0 + 8; s < S.s1 - 8; s += 12) {
      const f = c.frameAt(s);
      for (const o of [-7.5, 7.5]) out.prop('stationColumn', f.x + f.nx * o, f.y + 1.1, f.z + f.nz * o, 0, 1, H - 1.1, 1);
    }
    if (!index) continue;
    // entrances: up to 4 on sidewalks near the station ends
    let placed = 0;
    for (const ds of [-55, 55, -20, 20]) {
      if (placed >= 4) break;
      const f = c.frameAt(S.s + ds);
      const hit = index.nearestEdge(f.x, f.z, 70);
      if (!hit) continue;
      const lay = layout(getProfile(hit.edge));
      const ec = net.centerline(hit.edge);
      const side = ds < 0 ? -1 : 1;
      const zone = side < 0 ? lay.sideL : lay.sideR;
      if (zone < 3.2 || hit.s < 25 || hit.s > ec.length - 25) continue;
      const curb = side < 0 ? lay.curbL : lay.curbR;
      const o = curb + side * Math.min(zone - 1.3, 2.4);
      const g = ec.frameAt(hit.s);
      out.prop('subwayEntrance', g.x + g.nx * o, g.y + LEVEL.raised, g.z + g.nz * o, yawFor(g.tx, g.tz));
      placed++;
    }
  }
  out.color = line.color;
  return out;
}
