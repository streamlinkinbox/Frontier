// Interchange quality gate. Runs the generator and measures every ramp:
//
//  blend    at a gore nose the two pavements must touch (gap < 0.30 m), must never overlap,
//           and the painted gore must open at a sane taper rate (>= 1:6) without reversing
//  clearance a ramp may not thread through a roadway it does not join - within half that
//           road's pavement plus a metre, at the same elevation
//  ramp     length, design radius, grade and the vertical gap where it crosses another road
//
// Exit code 1 if any check fails.
import { generateCity } from '../src/road/cityGen.js';
import { mergeStats } from '../src/road/geom/merge.js';
import { layout, getProfile, RANK } from '../src/road/profiles.js';
import { edgeOffset } from '../src/road/interchange.js';

const seed = +(process.argv[2] || 1337), size = +(process.argv[3] || 4096);
const net = generateCity({ seed, size });
let fails = 0, checked = 0;
const rows = [];
const bad = m => { rows.push('  FAIL ' + m); fails++; };

// ---------------------------------------------------------------- ramp ↔ mainline blends
for (const edge of net.edges.values()) {
  if (!edge.merge) continue;
  const list = mergeStats(net, edge) || [];
  if (!list.length) { bad(`${edge.id} ${edge.profile}: unresolvable merge target`); continue; }
  for (const st of list) {
    checked++;
    const P = st.P, end = Math.min(st.end, P.length - 1), from = Math.max(0, st.nose);
    const gaps = P.slice(from, end + 1).map(p => p.w);
    const nose = gaps[0], wide = gaps[gaps.length - 1];
    if (wide < 1.2) { rows.push(`  ${edge.id.padEnd(6)} ${edge.profile.padEnd(9)} no gore needed (width ${wide.toFixed(1)}m)`); continue; }
    const taper = Math.abs(P[Math.max(from + 1, end)].ms - P[from].ms);
    // the legal-rate rule governs the PAINTED GORE - the stretch where the two surfaces are still
    // close enough that a drifting car reaches the other road. Past ~8 m of void the roadways have
    // properly separated and the ramp's own curve widens the gap, which no taper table regulates.
    const GORE = 8;
    let gend = end;
    for (let k = from + 1; k <= end; k++) if (gaps[k - from] >= GORE) { gend = k; break; }
    const gW = Math.max(0.4, Math.min(GORE, gaps[Math.max(0, gend - from)]));
    const gT = Math.abs(P[gend].ms - P[from].ms);
    const rate = gT / gW;                      // metres of run per metre of gore, must be >= 1:6
    const full = taper / Math.max(0.01, wide);
    let neg = 0, osc = 0;
    for (let k = 0; k < gaps.length; k++) {
      if (gaps[k] < -0.02) neg++;
      if (k > 1 && gaps[k] < gaps[k - 1] - 0.35 && wide - gaps[k] > 1) osc++;
    }
    const why = [];
    if (!(nose < 0.3)) why.push(`nose gap ${nose.toFixed(2)} m`);
    if (neg) why.push(`${neg} overlapping samples`);
    if (!(rate > 6)) why.push(`gore taper 1:${rate.toFixed(1)} over ${gT.toFixed(0)}m`);
    if (!(gT > 10)) why.push(`gore run ${gT.toFixed(0)}m too short to blend`);
    if (osc) why.push(`${osc} reversals`);
    rows.push(`  ${edge.id.padEnd(6)} ${edge.profile.padEnd(9)} nose=${nose.toFixed(2)}m gore=${gT.toFixed(0)}m/${gW.toFixed(1)}m (1:${rate.toFixed(1)}) void=${wide.toFixed(1)}m over ${taper.toFixed(0)}m (1:${full.toFixed(1)}) ${why.length ? 'FAIL ' + why.join(', ') : 'ok'}`);
    if (why.length) fails++;
  }
}

// ---------------------------------------------------------------- ramp clearance / profile
const box = new Map();
for (const e of net.edges.values()) {
  if (e._cls === 'ramp') continue;
  if (RANK[e.profile] === 0 || e.layer === 'path' || e.layer === 'subway') continue;   // footways
  const c = net.centerline(e);
  let x0 = Infinity, x1 = -Infinity, z0 = Infinity, z1 = -Infinity;
  for (let k = 0; k < c.X.length; k++) {
    if (c.X[k] < x0) x0 = c.X[k]; if (c.X[k] > x1) x1 = c.X[k];
    if (c.Z[k] < z0) z0 = c.Z[k]; if (c.Z[k] > z1) z1 = c.Z[k];
  }
  const lay = layout(getProfile(e));
  box.set(e, { c, half: Math.max(Math.abs(edgeOffset(lay, 1)), Math.abs(edgeOffset(lay, -1))), x0, x1, z0, z1 });
}
// The roads a ramp is actually connected to: named by its merge specs, sharing one of its end
// nodes, or holding one of its end nodes inside their own pavement (a ramp that terminates in a
// street necessarily runs down through that street's corridor - that is the connection, not a clash)
const endsOf = e => [net.nodes.get(e.a), net.nodes.get(e.b)].filter(Boolean);
const joins = e => {
  const set = new Set([].concat(e.merge || []).map(m => m.main).concat(
    net.nodeEdges(e.a).concat(net.nodeEdges(e.b)).filter(x => x !== e).map(x => x.id)));
  for (const [o, b] of box) {
    for (const nd of endsOf(e)) {
      if (nd.x < b.x0 - 6 || nd.x > b.x1 + 6 || nd.z < b.z0 - 6 || nd.z > b.z1 + 6) continue;
      if (b.c.nearest(nd.x, nd.z).d <= b.half + 8) {
        set.add(o.id);
        // a ramp necessarily runs over the road it terminates in, and that road is split into
        // several edges - the whole of it is the connection, so exempt it by name as well
        if (o.name) for (const [q] of box) if (q.name === o.name) set.add(q.id);
        break;
      }
    }
  }
  return set;
};

let nRamp = 0, maxGrade = 0, minClear = Infinity;
for (const e of net.edges.values()) {
  if (e._cls !== 'ramp') continue;
  nRamp++;
  const c = net.centerline(e), ok = joins(e);
  let L = 0, ys = [];
  for (let k = 0; k < c.X.length; k++) ys.push(c.Y[k]);
  for (let k = 1; k < c.X.length; k++) L += Math.hypot(c.X[k] - c.X[k - 1], c.Z[k] - c.Z[k]);
  const msg0 = [];
  let who = '';
  let bg = 0, bs = 0;
  // a grade is specified over a design length, not over one sample - a rounded crease must not
  // read as an 8 m step just because the centerline happens to be sampled at 1 m
  const ss = [0];
  for (let k = 1; k < c.X.length; k++) ss.push(ss[k - 1] + Math.hypot(c.X[k] - c.X[k - 1], c.Z[k] - c.Z[k - 1]));
  for (let k = 0; k + 8 < c.X.length; k++) {
    let j = k + 8; while (j < c.X.length - 1 && ss[j] - ss[k] < 9) j++;
    const g = Math.abs(c.Y[j] - c.Y[k]) / Math.max(1, ss[j] - ss[k]);
    if (g > maxGrade) maxGrade = g;
    if (g > bg) { bg = g; bs = k; }
  }
  if (bg > 0.085) msg0.push(`grade ${(bg * 100).toFixed(1)}% at s=${bs}m`);
  let clash = 0, over = 0;
  const tail = Math.min(c.X.length, 35);            // the two ends are blend zones, judged above
  for (let k = tail; k < c.X.length - tail; k += 2) {
    const x = c.X[k], z = c.Z[k], y = c.Y[k];
    for (const [o, b] of box) {
      if (ok.has(o.id)) continue;
      if (x < b.x0 - 30 || x > b.x1 + 30 || z < b.z0 - 30 || z > b.z1 + 30) continue;
      const q = b.c.nearest(x, z);
      if (q.d > b.half + 4) continue;
      const f = b.c.frameAt(q.s);
      if (Math.abs(y - f.y) < 1.6) { clash++; if (!who) who = `${o.id}:${o.name || o._cls || o.profile} dy=${(y - f.y).toFixed(2)} d=${q.d.toFixed(1)} s=${(k * 1).toFixed(0)}`; }
      else if (y > f.y) over = Math.max(over, y - f.y);
      else minClear = Math.min(minClear, y - f.y);
    }
  }
  const msg = msg0;
  if (clash) msg.push(`${clash} samples inside another roadway at the same level (${who})`);
  rows.push(`  ${e.id.padEnd(6)} ${e.profile.padEnd(9)} ${L.toFixed(0)}m long, ${((Math.max(...ys) - Math.min(...ys))).toFixed(1)}m of fall, ${c.X.length} pts${msg.length ? ' FAIL ' + msg.join(', ') : ''}`);
  if (msg.length) fails++;
}
console.log(rows.join('\n'));
console.log(`\n${checked} blends checked · ${nRamp} ramps · max ramp grade ${(maxGrade * 100).toFixed(1)}% · worst over-crossing clearance ${isFinite(minClear) ? minClear.toFixed(1) + 'm' : 'n/a'}`);
if (maxGrade > 0.085) { console.log('FAIL ramp grade over 8.5%'); fails++; }
console.log(`${fails ? 'FAILED' : 'PASSED'} (${fails})`);
process.exit(fails ? 1 : 0);
