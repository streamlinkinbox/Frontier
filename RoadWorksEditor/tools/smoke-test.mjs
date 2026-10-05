// Headless sanity checks for the RoadWorks generators. Run with: node tools/smoke-test.mjs
import { buildNetwork } from '../src/Network.js';
import { toObj } from '../src/MeshSpec.js';
import { buildGraph, nodeGeneratesJunction } from '../src/Graph.js';

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
  check('pavement generated', net.groups.pavement.triangleCount > 100);
  check('markings generated', net.groups.markings.triangleCount > 10);
  check('normals present', net.groups.road.normals.length === net.groups.road.positions.length);
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
for (const type of ['beam', 'box', 'arch', 'truss', 'suspension', 'cablestay']) {
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
