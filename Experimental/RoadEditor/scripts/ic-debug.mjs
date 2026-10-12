import { generateCity } from '../src/road/cityGen.js';
import { layout, getProfile } from '../src/road/profiles.js';
import { edgeOffset } from '../src/road/interchange.js';

const net = generateCity({ seed: +(process.argv[2] || 1337), size: +(process.argv[3] || 4096) });
const latOf = (f, x, z) => (x - f.x) * f.nx + (z - f.z) * f.nz;

for (const e of net.edges.values()) {
  if (e._cls !== 'ramp') continue;
  const c = net.centerline(e);
  const lay = layout(getProfile(e));
  const ctrl = [net.nodes.get(e.a), ...e.pts, net.nodes.get(e.b)];
  let dmax = 0, dwhere = 0;
  for (let k = 0; k < c.X.length; k++) {
    let best = Infinity;
    for (const p of ctrl) best = Math.min(best, Math.hypot(p.x - c.X[k], p.z - c.Z[k]));
    if (best > dmax) { dmax = best; dwhere = k; }
  }
  console.log(`${e.id} ${e.profile.padEnd(9)} L=${c.length.toFixed(0)} pts=${c.X.length} ctrl=${ctrl.length} splineDev=${dmax.toFixed(2)}@${(c.S[dwhere] || 0).toFixed(0)}`);
  for (const M of [].concat(e.merge || [])) {
    const a = net.edges.get(M.main);
    if (!a) { console.log('     missing anchor', M.main); continue; }
    const ac = net.centerline(a), al = layout(getProfile(a));
    const fromA = M.end !== 'b';
    const s0 = fromA ? 0 : c.length;
    const probe = c.frameAt(s0 + (fromA ? 8 : -8));
    const pq = ac.nearest(probe.x, probe.z), pf = ac.frameAt(pq.s);
    const anchorSide = Math.sign(latOf(pf, probe.x, probe.z)) || 1;        // where the anchor sits, in the anchor's frame
    const rampSide = Math.sign(latOf(probe, pf.x, pf.z)) || 1;              // where the anchor sits, in the ramp's frame
    const oRamp = rampSide > 0 ? lay.right : lay.left;
    const oMain = edgeOffset(al, anchorSide);
    const row = [];
    for (const adv of [0, 30, 60, 90, 120, 150]) {
      const ss = s0 + (fromA ? adv : -adv);
      if (ss < 0 || ss > c.length) break;
      const g = c.frameAt(ss);
      const bx = g.x + g.nx * oRamp, bz = g.z + g.nz * oRamp;
      const q = ac.nearest(bx, bz), af = ac.frameAt(q.s);
      const latB = latOf(af, bx, bz);
      row.push(`${adv}:${(latB - oMain).toFixed(2)}/${(Math.abs(latB) - Math.abs(oMain)).toFixed(2)}`);
    }
    console.log(`     ${M.main} end=${M.end} anchorSide=${anchorSide} rampSide=${rampSide} oRamp=${oRamp.toFixed(2)} oMain=${oMain.toFixed(2)} gap ${row.join(' ')}`);
  }
}
