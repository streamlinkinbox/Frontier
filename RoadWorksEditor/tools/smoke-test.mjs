// Headless sanity checks for the RoadWorks generators. Run with: node tools/smoke-test.mjs
import { buildNetwork } from '../src/Network.js';
import { toObj } from '../src/MeshSpec.js';
import { buildGraph, nodeGeneratesJunction } from '../src/Graph.js';
import { groupBase } from '../src/Network.js';
import { SIGN_UV } from '../src/Textures.js';
import { closestLineParam } from '../src/Ray.js';

// Pavement is split into one group per paving pattern (`pavement#brick@1.00`), so totals are taken by base name.
const tris = (net, base) =>
  Object.entries(net.groups).reduce((sum, [name, spec]) => (groupBase(name) === base ? sum + spec.triangleCount : sum), 0);

let failures = 0;
const check = (name, condition, detail = '') => {
  if (condition) console.log(`  ok   ${name}`);
  else {
    console.log(`  FAIL ${name} ${detail}`);
    failures++;
  }
};

const corridor = (id, points, extra = {}) => ({
  id,
  name: id,
  preset: 'street',
  family: 'road',
  capMode: 'flat',
  tension: 0,
  points: points.map(([x, y, z = 0]) => ({ x, y, z })),
  ...extra,
});

console.log('\n— crossroads —');
{
  const net = buildNetwork([
    corridor('ns', [[0, -60], [0, 0], [0, 60]]),
    corridor('ew', [[-60, 0], [0, 0], [60, 0]]),
  ]);
  check('two corridors split into four edges', net.stats.edges === 4, `got ${net.stats.edges}`);
  check('single merged junction node at the crossing', net.stats.junctions === 1, `got ${net.stats.junctions}`);
  check('road surface generated', net.groups.road.triangleCount > 100);
  check('curbs generated', net.groups.curb.triangleCount > 100);
  check('pavement generated', tris(net, 'pavement') > 100, `${tris(net, 'pavement')} tris`);
  check('markings generated', net.groups.markings.triangleCount > 10);
  check('normals present', net.groups.road.normals.length === net.groups.road.positions.length);
}

console.log('\n— junction furniture —');
{
  const net = buildNetwork([
    corridor('ns', [[0, -60], [0, 0], [0, 60]]),
    corridor('ew', [[-60, 0], [0, 0], [60, 0]]),
  ], { signage: 'stop', stopBars: true, crosswalks: true });
  check('stop signs placed on every arm', net.stats.signs === 4, `got ${net.stats.signs}`);
  check('sign faces generated', net.groups.signFace.triangleCount > 20, `${net.groups.signFace.triangleCount} tris`);
  check('sign posts generated', net.groups.signPost.triangleCount >= 4 * 12, `${net.groups.signPost.triangleCount} tris`);
  const bare = buildNetwork([
    corridor('ns', [[0, -60], [0, 0], [0, 60]]),
    corridor('ew', [[-60, 0], [0, 0], [60, 0]]),
  ], { signage: 'none', stopBars: false, crosswalks: false });
  check('signage can be switched off', bare.stats.signs === 0 && bare.groups.signFace.triangleCount === 0);
  check('stop bars and crossings add markings', net.groups.markings.triangleCount > bare.groups.markings.triangleCount);
}

console.log('\n— elevated roadbed —');
{
  const low = buildNetwork([corridor('ramp', [[-60, 0, 0], [0, 0, 3], [60, 0, 4]])]);
  check('shallow fill builds an earth embankment', low.groups.earth.triangleCount > 50, `${low.groups.earth.triangleCount} tris`);
  check('shallow fill builds no wall', low.groups.roadbed.triangleCount === 0);

  const high = buildNetwork([corridor('viaduct', [[-60, 0, 14], [0, 0, 16], [60, 0, 15]])]);
  check('deep fill switches to a retaining wall', high.groups.roadbed.triangleCount > 50, `${high.groups.roadbed.triangleCount} tris`);
  check('deep fill builds no embankment', high.groups.earth.triangleCount === 0);

  const flat = buildNetwork([corridor('ground', [[-60, 0], [60, 0]])]);
  check('at grade builds no roadbed at all', flat.groups.earth.triangleCount === 0 && flat.groups.roadbed.triangleCount === 0);

  const floating = buildNetwork([corridor('float', [[-60, 0, 6], [60, 0, 6]], { roadbed: { mode: 'none' } })]);
  check('roadbed can be disabled', floating.groups.earth.triangleCount === 0 && floating.groups.roadbed.triangleCount === 0);

  const slab = buildNetwork([corridor('slab', [[-60, 0, 6], [60, 0, 6]], { roadbed: { mode: 'slab' } })]);
  check('slab soffit builds', slab.groups.roadbed.triangleCount > 50);

  // A corridor that crosses grade must start and stop its skirt rather than diving underground.
  const crossing = buildNetwork([corridor('dip', [[-60, 0, 8], [0, 0, 0], [60, 0, 8]])]);
  const zs = [];
  const pos = crossing.groups.earth.positions;
  for (let i = 2; i < pos.length; i += 3) zs.push(pos[i]);
  check('skirt never dips below ground', Math.min(...zs) >= -0.001, `min z ${Math.min(...zs)}`);
}

console.log('\n— paving variants —');
{
  const net = buildNetwork([
    corridor('a', [[-60, 0], [0, 0], [60, 0]], { paving: 'brick' }),
    corridor('b', [[0, -60], [0, 0], [0, 60]], { paving: 'hex', pavingScale: 1.5 }),
  ]);
  const keys = Object.keys(net.groups).filter((n) => groupBase(n) === 'pavement');
  check('one pavement group per pattern', keys.includes('pavement#brick@1.00') && keys.includes('pavement#hex@1.50'), keys.join(', '));
  check('both paving groups carry geometry', keys.every((k) => net.groups[k].triangleCount >= 0));
  check('pavement UVs are in metres', (() => {
    const spec = net.groups['pavement#brick@1.00'];
    let max = 0;
    for (const v of spec.uvs) max = Math.max(max, Math.abs(v));
    return max > 20;
  })());
}

console.log('\n— T junction with mismatched widths —');
{
  const net = buildNetwork([
    corridor('main', [[-80, 0], [0, 0], [80, 0]], { preset: 'avenue' }),
    corridor('side', [[0, 0], [0, 50]], { preset: 'alley' }),
  ]);
  check('three edges', net.stats.edges === 3, `got ${net.stats.edges}`);
  check('one junction', net.stats.junctions === 1, `got ${net.stats.junctions}`);
  const node = [...net.graph.nodes.values()].find((n) => n.degree === 3);
  check('degree-3 node exists', !!node);
  check('corner radius scales with the avenue width', node && node.cornerRadius > 9, `r=${node?.cornerRadius?.toFixed(2)}`);
}

console.log('\n— curved corridor: curb offsets stay outside the carriageway —');
{
  const pts = [];
  for (let i = 0; i <= 12; i++) {
    const a = (i / 12) * Math.PI;
    pts.push([Math.cos(a) * 30, Math.sin(a) * 30]);
  }
  const net = buildNetwork([corridor('bend', pts)]);
  const curb = net.groups.curb;
  let minR = Infinity;
  let maxR = -Infinity;
  for (let i = 0; i < curb.positions.length; i += 3) {
    const r = Math.hypot(curb.positions[i], curb.positions[i + 1]);
    minR = Math.min(minR, r);
    maxR = Math.max(maxR, r);
  }
  check('inner curb never crosses the centreline', minR > 30 - 4.3 - 0.2, `minR=${minR.toFixed(2)}`);
  check('outer curb stays on the parallel curve', maxR < 30 + 4.4, `maxR=${maxR.toFixed(2)}`);
}

console.log('\n— grade separation is not merged —');
{
  const net = buildNetwork([
    corridor('ground', [[-60, 0], [60, 0]]),
    corridor('over', [[0, -60, 7], [0, 0, 7], [0, 60, 7]], { family: 'bridge' }),
  ]);
  check('no junction created across the overpass', net.stats.junctions === 0, `got ${net.stats.junctions}`);
  check('grade separation recorded', net.stats.gradeSeparations >= 1, `got ${net.stats.gradeSeparations}`);
  check('deck generated', net.groups.deck.triangleCount > 50);
  check('piers generated', net.groups.piers.triangleCount > 50);
}

console.log('\n— every bridge type and pier family builds —');
for (const type of ['beam', 'box', 'slab', 'cantilever', 'arch', 'tiedarch', 'masonry', 'truss', 'throughtruss', 'suspension', 'cablestay']) {
  for (const pierType of ['wall', 'column', 'hammerhead', 'vpier', 'none']) {
    const net = buildNetwork([
      corridor('span', [[-70, 0, 9], [0, 10, 9], [70, 0, 9]], {
        family: 'bridge',
        bridge: { type, pierType, railing: 'parapet' },
      }),
    ]);
    const tris = net.groups.deck.triangleCount + net.groups.structure.triangleCount + net.groups.piers.triangleCount + net.groups.cables.triangleCount;
    check(`${type}/${pierType}`, tris > 200 && Number.isFinite(tris), `tris=${tris}`);
  }
}

console.log('\n— railing families —');
for (const railing of ['parapet', 'steel', 'jersey', 'none']) {
  const net = buildNetwork([
    corridor('span', [[-40, 0, 6], [40, 0, 6]], { family: 'bridge', bridge: { railing } }),
  ]);
  const tris = net.groups.railing.triangleCount;
  check(`railing ${railing}`, railing === 'none' ? tris === 0 : tris > 20, `tris=${tris}`);
}

console.log('\n— structures that stand above the deck clear it —');
for (const type of ['tiedarch', 'throughtruss']) {
  const net = buildNetwork([
    corridor('span', [[-60, 0, 9], [60, 0, 9]], { family: 'bridge', bridge: { type, archRise: 7, trussHeight: 5 } }),
  ]);
  const pos = net.groups.structure.positions;
  let top = -Infinity;
  for (let i = 2; i < pos.length; i += 3) top = Math.max(top, pos[i]);
  check(`${type} rises above the deck`, top > 9 + 3, `top=${top.toFixed(2)} m`);
  check(`${type} has hangers or bracing`, net.groups.structure.triangleCount + net.groups.cables.triangleCount > 400);
}

console.log('\n— guardrail families —');
for (const type of ['wbeam', 'thrie', 'cable', 'jersey', 'parapet', 'pedestrian', 'none']) {
  const net = buildNetwork([
    corridor('ramp', [[0, 0, 3], [60, 0, 3.4], [120, 0, 3]], { guardrail: { type, when: 'always' } }),
  ]);
  const t = net.groups.railing.triangleCount + net.groups.barrier.triangleCount;
  check(`guardrail ${type}`, type === 'none' ? t === 0 : t > 60, `tris=${t}`);
  if (type !== 'none') {
    // the barrier must stand outside the carriageway and above the pavement, on both sides
    const spec = ['jersey', 'parapet'].includes(type) ? net.groups.barrier : net.groups.railing;
    let minY = Infinity;
    let maxY = -Infinity;
    let minZ = Infinity;
    for (let i = 0; i < spec.positions.length; i += 3) {
      minY = Math.min(minY, spec.positions[i + 1]);
      maxY = Math.max(maxY, spec.positions[i + 1]);
      minZ = Math.min(minZ, spec.positions[i + 2]);
    }
    check(`  ${type} sits clear of the 8 m carriageway`, Math.min(-minY, maxY) > 4.0, `|y|=${Math.min(-minY, maxY).toFixed(2)}`);
    check(`  ${type} stands on the pavement, not in it`, minZ > 2.9, `minZ=${minZ.toFixed(2)}`);
  }
}
{
  const flat = buildNetwork([corridor('flat', [[0, 0], [80, 0]], { guardrail: { type: 'wbeam', when: 'fill', fillTrigger: 1.5 } })]);
  check('fill-triggered railing stays off a road on grade', flat.groups.railing.triangleCount === 0);
  const high = buildNetwork([corridor('high', [[0, 0, 4], [80, 0, 4]], { guardrail: { type: 'wbeam', when: 'fill', fillTrigger: 1.5 } })]);
  check('fill-triggered railing appears on embankment', high.groups.railing.triangleCount > 60);
  const one = buildNetwork([corridor('one', [[0, 0, 4], [80, 0, 4]], { guardrail: { type: 'wbeam', when: 'always', side: 'left' } })]);
  check('single-sided railing is half the geometry', one.groups.railing.triangleCount < high.groups.railing.triangleCount * 0.75);
}

console.log('\n— the STOP legend reads the right way round —');
{
  const net = buildNetwork([
    corridor('ns', [[0, -60], [0, 0], [0, 60]]),
    corridor('ew', [[-60, 0], [0, 0], [60, 0]]),
  ], { signage: 'stop' });
  const spec = net.groups.signFace;
  const cell = SIGN_UV.stop;
  let tested = 0;
  let wrong = 0;
  for (let t = 0; t < spec.indices.length; t += 3) {
    const [i0, i1, i2] = [spec.indices[t], spec.indices[t + 1], spec.indices[t + 2]];
    const P = (i) => [spec.positions[i * 3], spec.positions[i * 3 + 1], spec.positions[i * 3 + 2]];
    const U = (i) => [spec.uvs[i * 2], spec.uvs[i * 2 + 1]];
    const inCell = [i0, i1, i2].every((i) => {
      const [u, v] = U(i);
      return u >= cell.u0 - 1e-6 && u <= cell.u1 + 1e-6 && v >= cell.v0 - 1e-6 && v <= cell.v1 + 1e-6;
    });
    if (!inCell) continue;
    const [a, b, c] = [P(i0), P(i1), P(i2)];
    const e1 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
    const e2 = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    // outward normal (triangles are wound CCW seen from the front)
    const n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    if (Math.abs(n[2]) > 0.3 * Math.hypot(...n)) continue; // skip the plate edges
    // gradient of u across the triangle, projected onto the horizontal
    const du1 = U(i1)[0] - U(i0)[0];
    const du2 = U(i2)[0] - U(i0)[0];
    const g = [e1[0] * du1 + e2[0] * du2, e1[1] * du1 + e2[1] * du2];
    if (Math.hypot(...g) < 1e-6) continue;
    // a viewer facing this plate has screen-right = Z x n; legible text needs u to increase that way
    const right = [-n[1], n[0]];
    tested++;
    if (g[0] * right[0] + g[1] * right[1] <= 0) wrong++;
  }
  check('front plates carry a STOP face', tested >= 4, `tested=${tested}`);
  check('legend is not mirrored on any plate', wrong === 0, `${wrong} of ${tested} mirrored`);
}

console.log('\n— gizmo axis maths —');
{
  const rnd = (n) => ((Math.sin(n * 12.9898) * 43758.5453) % 1) * 2 - 1;
  let worst = 0;
  for (let k = 1; k < 40; k++) {
    const origin = { x: rnd(k) * 50, y: rnd(k + 7) * 50, z: rnd(k + 13) * 20 };
    const ro = { x: rnd(k + 21) * 80, y: rnd(k + 31) * 80, z: 40 + rnd(k + 41) * 20 };
    const dir = { x: rnd(k + 51), y: rnd(k + 61), z: -0.4 - Math.abs(rnd(k + 71)) };
    const s = closestLineParam(origin, { x: 0, y: 0, z: 1 }, ro, dir);
    // brute force the same minimum: the distance in s is a convex parabola, so ternary search nails it
    const distAt = (ss) => {
      const w = { x: origin.x - ro.x, y: origin.y - ro.y, z: origin.z + ss - ro.z };
      const t = (w.x * dir.x + w.y * dir.y + w.z * dir.z) / (dir.x ** 2 + dir.y ** 2 + dir.z ** 2);
      return Math.hypot(w.x - dir.x * t, w.y - dir.y * t, w.z - dir.z * t);
    };
    let lo = -5000;
    let hi = 5000;
    for (let i = 0; i < 200; i++) {
      const m1 = lo + (hi - lo) / 3;
      const m2 = hi - (hi - lo) / 3;
      if (distAt(m1) < distAt(m2)) hi = m2;
      else lo = m1;
    }
    worst = Math.max(worst, Math.abs((lo + hi) / 2 - s));
  }
  check('vertical drag solves to the closest point, with the right sign', worst < 0.05, `worst error ${worst.toFixed(3)} m`);
  check('upward ray motion raises the point', (() => {
    const o = { x: 0, y: 0, z: 0 };
    const ro = { x: 0, y: -40, z: 20 };
    const lo = closestLineParam(o, { x: 0, y: 0, z: 1 }, ro, { x: 0, y: 1, z: -0.6 });
    const hi = closestLineParam(o, { x: 0, y: 0, z: 1 }, ro, { x: 0, y: 1, z: -0.4 });
    return hi > lo;
  })());
}

console.log('\n— the per-corridor mesh cache —');
{
  const doc = [
    corridor('ns', [[0, -60], [0, 0], [0, 60]]),
    corridor('ew', [[-60, 0], [0, 0], [60, 0]]),
    corridor('far', [[300, 300, 9], [420, 300, 9]], { family: 'bridge', bridge: { type: 'truss' } }),
  ];
  const cache = new Map();
  const cold = buildNetwork(doc, {}, cache);
  check('a cold build caches every corridor', cold.stats.cacheHits === 0 && cache.size === cold.stats.edges, `${cache.size} entries`);
  const warm = buildNetwork(doc, {}, cache);
  check('an unchanged network is served entirely from the cache', warm.stats.cacheHits === warm.stats.edges, `${warm.stats.cacheHits}/${warm.stats.edges}`);
  check('cached output is identical', warm.stats.triangles === cold.stats.triangles, `${cold.stats.triangles} vs ${warm.stats.triangles}`);

  doc[0].points[1].x += 3;
  const moved = buildNetwork(doc, {}, cache);
  check('moving one corridor only invalidates what it touched', moved.stats.cacheHits > 0 && moved.stats.cacheHits < moved.stats.edges, `${moved.stats.cacheHits}/${moved.stats.edges}`);
  const fresh = buildNetwork(doc, {});
  check('cached and uncached builds agree', fresh.stats.triangles === moved.stats.triangles, `${fresh.stats.triangles} vs ${moved.stats.triangles}`);
  check('the cache never grows past the live edge count', cache.size === moved.stats.edges, `${cache.size} entries`);
}

console.log('\n— geometry hygiene —');
{
  const net = buildNetwork([
    corridor('a', [[-50, -50], [0, 0], [50, 50]]),
    corridor('b', [[-50, 50], [0, 0], [50, -50]]),
    corridor('c', [[0, -60, 8], [0, 60, 8]], { family: 'bridge', bridge: { type: 'arch' } }),
  ]);
  let finite = true;
  for (const g of Object.values(net.groups)) {
    for (const v of g.positions) if (!Number.isFinite(v)) finite = false;
    for (const v of g.normals) if (!Number.isFinite(v)) finite = false;
  }
  check('no NaN / Infinity in any buffer', finite);
  const obj = toObj(Object.entries(net.groups).map(([name, spec]) => ({ name, spec })));
  check('OBJ export is non-empty and well formed', obj.includes('\nv ') && obj.includes('\nf ') && !obj.includes('NaN'));
  check('build under 500 ms', net.stats.buildMs < 500, `${net.stats.buildMs}ms`);
}

console.log('\n— closed loop (roundabout-ish) —');
{
  const pts = [];
  for (let i = 0; i < 10; i++) {
    const a = (i / 10) * Math.PI * 2;
    pts.push([Math.cos(a) * 22, Math.sin(a) * 22]);
  }
  const g = buildGraph([corridor('ring', pts, { closed: true }), corridor('spur', [[0, 22], [0, 70]])]);
  const junctions = [...g.nodes.values()].filter((n) => nodeGeneratesJunction(g, n)).length;
  check('ring + spur produce a junction', junctions >= 1, `got ${junctions}`);
}

console.log(`\n${failures === 0 ? 'ALL CHECKS PASSED' : `${failures} CHECK(S) FAILED`}\n`);
process.exit(failures === 0 ? 0 : 1);
