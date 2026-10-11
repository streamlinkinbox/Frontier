// Node geometry tests: quad-only topology, sane trims, upward normals.
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
globalThis.RF = {};
for (const f of ['math', 'spline', 'mesh', 'profiles', 'network', 'roadmesh']) vm.runInThisContext(readFileSync(new URL(`../src/${f}.js`, import.meta.url), 'utf8'), { filename: f + '.js' });
const RF = globalThis.RF;
let failures = 0;
const check = (cond, msg) => { if (!cond) { failures++; console.log('  ✗', msg); } };

function project(nodes, roads) {
  const p = { settings: { handedness: 'right' }, nodes: {}, roads: {} };
  for (const [id, x, y, z, extra] of nodes) p.nodes[id] = { id, p: [x, y, z], ...(extra || {}) };
  for (const [id, a, b, profile, pts, extra] of roads) p.roads[id] = { id, name: id, a, b, profile, pts: pts || [], ...(extra || {}) };
  return p;
}
function buildAll(proj, layers = {}) {
  const net = RF.network.solve(proj);
  const out = {};
  for (const R of net.roads.values()) RF.roadmesh.buildRoad(R, net, layers, out);
  for (const rec of net.nodes.values()) if (rec.kind === 'junction' || rec.kind === 'transition') RF.roadmesh.buildJunction(rec, net, layers, out);
  const meshes = Object.values(out).map((b) => { b.weld(); return b.build(); });
  return { net, meshes, out };
}
function analyse(name, meshes, net) {
  let quads = 0, tris = 0, nan = 0, downAsphalt = 0, asphaltQuads = 0;
  for (const m of meshes) {
    quads += m.quadCount; tris += m.triCount;
    for (let i = 0; i < m.vertices.length; i++) if (!Number.isFinite(m.vertices[i])) nan++;
    if (m.material === 'asphalt') {
      for (let q = 0; q < m.quads.length; q += 4) {
        asphaltQuads++;
        // Newell normal of quad
        let ny = 0; for (let k = 0; k < 4; k++) { const a = m.quads[q + k] * 12, b = m.quads[q + ((k + 1) % 4)] * 12; ny += (m.vertices[a + 2] - m.vertices[b + 2]) * (m.vertices[a] + m.vertices[b]); }
        if (ny <= 0) downAsphalt++;
      }
    }
  }
  console.log(`${name}: ${quads} quads, ${tris} tris, asphalt quads ${asphaltQuads}, downward ${downAsphalt}, NaN ${nan}, diagnostics ${net.diagnostics.length}`);
  check(tris === 0, 'non-quad faces present');
  check(nan === 0, 'NaN in vertex data');
  check(downAsphalt === 0, 'downward-facing asphalt quads (winding)');
  for (const d of net.diagnostics) console.log('   diag:', d.level, d.message);
}

// 1. Crossroads 90°
{
  const p = project([['c', 0, 0, 0], ['n', 0, 0, -120], ['s', 0, 0, 120], ['e', 120, 0, 0], ['w', -120, 0, 0]], [['r1', 'n', 'c', 'avenue'], ['r2', 'c', 's', 'avenue'], ['r3', 'w', 'c', 'street'], ['r4', 'c', 'e', 'street']]);
  const { net, meshes } = buildAll(p);
  const rec = net.nodes.get('c');
  console.log(' trims', rec.arms.map((a) => a.roadId + ':' + a.trim.toFixed(2)).join(' '), 'type', rec.type);
  check(rec.arms.every((a) => a.trim > 5 && a.trim < 20), 'crossroad trims in expected range');
  analyse('crossroads', meshes, net);
}
// 2. Acute merge (ramp onto carriageway at 12°)
{
  const ang = (12 * Math.PI) / 180;
  const p = project([['m', 0, 0, 0], ['a', -300, 0, 0], ['b', 300, 0, 0], ['r', -300 * Math.cos(ang), 0, 300 * Math.sin(ang)]], [['hw1', 'a', 'm', 'carriageway'], ['hw2', 'm', 'b', 'carriageway3'], ['ramp', 'r', 'm', 'ramp']]);
  const { net, meshes } = buildAll(p);
  const rec = net.nodes.get('m');
  console.log(' trims', rec.arms.map((a) => a.roadId + ':' + a.trim.toFixed(2)).join(' '), 'type', rec.type, 'corners', rec.corners.map((c) => (c.straight ? 'straight' : `R${c.radius.toFixed(1)} θ${(c.theta * 180 / Math.PI).toFixed(0)}°`)).join(' | '));
  check(rec.type === 'Merge', 'merge detected');
  check(rec.arms.find((a) => a.roadId === 'ramp').trim > 25, 'ramp runout long enough for acute merge');
  analyse('acute merge', meshes, net);
}
// 3. Y junction & curved road with pothole/speed bump
{
  const p = project([['c', 0, 0, 0], ['a', -150, 0, 0], ['b', 130, 0, -75], ['d', 130, 0, 75]], [['r1', 'a', 'c', 'collector', [[-80, 0, 20]], { features: { potholes: [{ s: 40, t: 2, r: 0.6, depth: 0.07 }], speedBumps: [{ s: 90 }] } }], ['r2', 'c', 'b', 'street'], ['r3', 'c', 'd', 'street']]);
  const { net, meshes } = buildAll(p);
  const rec = net.nodes.get('c');
  console.log(' trims', rec.arms.map((a) => a.roadId + ':' + a.trim.toFixed(2)).join(' '), 'type', rec.type);
  analyse('Y junction', meshes, net);
}
// 4. degree-2 pass-through & transition
{
  const p = project([['a', -100, 0, 0], ['m', 0, 0, 0], ['b', 100, 0, 0], ['m2', 200, 0, 0], ['c', 300, 0, 0]], [['r1', 'a', 'm', 'street'], ['r2', 'm', 'b', 'street'], ['r3', 'b', 'm2', 'avenue'], ['r4', 'm2', 'c', 'avenue']]);
  const { net, meshes } = buildAll(p);
  console.log(' kinds', [...net.nodes.values()].map((n) => n.id + ':' + n.kind).join(' '));
  check(net.nodes.get('m').kind === 'pass', 'equal-width colinear node is pass-through');
  check(net.nodes.get('b').kind === 'transition', 'width change creates transition');
  analyse('pass-through', meshes, net);
}
// 5. 5-way with elevation
{
  const nodes = [['c', 0, 2, 0]]; const roads = [];
  for (let i = 0; i < 5; i++) { const a = (i / 5) * Math.PI * 2 + 0.3; nodes.push(['n' + i, Math.cos(a) * 140, i % 2 ? 0 : 3, Math.sin(a) * 140]); roads.push(['r' + i, 'c', 'n' + i, i % 2 ? 'street' : 'collector']); }
  const p = project(nodes, roads);
  const { net, meshes } = buildAll(p);
  analyse('5-way', meshes, net);
}
// 6. Shared vertex welding check for a junction patch: every interior edge shared by exactly 2 quads
{
  const p = project([['c', 0, 0, 0], ['n', 0, 0, -100], ['e', 100, 0, 0], ['w', -100, 0, 0]], [['r1', 'n', 'c', 'street'], ['r3', 'w', 'c', 'street'], ['r4', 'c', 'e', 'street']]);
  const net = RF.network.solve(p); const out = {};
  RF.roadmesh.buildJunction(net.nodes.get('c'), net, {}, out);
  const b = out.asphalt; b.weld();
  const edges = new Map();
  for (let i = 0; i < b.quads.length; i += 4) for (let k = 0; k < 4; k++) { const a = b.quads[i + k], c = b.quads[i + ((k + 1) % 4)]; const key = a < c ? a + '-' + c : c + '-' + a; edges.set(key, (edges.get(key) || 0) + 1); }
  const counts = [...edges.values()]; const bad = counts.filter((c) => c > 2).length, boundary = counts.filter((c) => c === 1).length;
  console.log(`T-junction patch: ${b.quads.length / 4} quads, ${b.vertexCount} verts, boundary edges ${boundary}, non-manifold ${bad}`);
  check(bad === 0, 'junction patch is manifold');
  const expectedBoundary = 3 * (RF.network.END_HALF_RES * 2) + 3 * (RF.network.CORNER_HALF_RES * 2);
  check(boundary === expectedBoundary, `boundary edges ${boundary} == ${expectedBoundary}`);
}
console.log(failures ? `\n${failures} FAILURES` : '\nall geometry checks passed');
process.exit(failures ? 1 : 0);
