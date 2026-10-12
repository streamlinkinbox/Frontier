import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildNetwork } from '../src/core/network.js';
import { makeRoad, createModel } from '../src/core/model.js';
import { segmentIntersection, catmullRom } from '../src/core/geom.js';

const line = (pts, extra = {}) => makeRoad({ points: pts.map(([x, z]) => ({ x, z })), preset: 'residential', ...extra });

// Every pair of edges in the same level may only touch at a shared node.
function assertCleanTopology(net) {
  const { edges, nodes } = net;
  for (let i = 0; i < edges.length; i++) for (let j = i + 1; j < edges.length; j++) {
    const A = edges[i], B = edges[j];
    if (A.key !== B.key || A.road.kind === 'rail' || B.road.kind === 'rail') continue;
    for (let a = 0; a < A.pts.length - 1; a++) for (let b = 0; b < B.pts.length - 1; b++) {
      const hit = segmentIntersection(A.pts[a], A.pts[a + 1], B.pts[b], B.pts[b + 1]);
      if (!hit) continue;
      const shared = [A.a, A.b].filter((n) => n === B.a || n === B.b);
      const onNode = shared.some((n) => Math.hypot(nodes[n].x - hit.x, nodes[n].z - hit.z) < 0.5);
      assert.ok(onNode, `edges ${A.id} and ${B.id} cross at (${hit.x.toFixed(2)},${hit.z.toFixed(2)}) away from a node`);
    }
  }
}

test('plus crossing becomes one 4-way junction with 4 arms', () => {
  const m = createModel();
  m.roads.push(line([[-50, 0], [50, 0]]), line([[0, -50], [0, 50]]));
  const net = buildNetwork(m);
  assert.equal(net.stats.junctions, 1);
  assert.equal(net.edges.length, 4);
  const j = net.nodes.find((n) => n.degree >= 3);
  assert.equal(j.degree, 4);
  assert.ok(Math.hypot(j.x, j.z) < 0.01);
  assertCleanTopology(net);
});

test('T contact: endpoint landing on a road body is merged, not left floating', () => {
  const m = createModel();
  m.roads.push(line([[-50, 0], [50, 0]]), line([[0, 1.5], [0, 60]]));
  const net = buildNetwork(m);
  const j = net.nodes.find((n) => n.degree === 3);
  assert.ok(j, 'expected a degree-3 T junction');
  assert.ok(Math.abs(j.x) < 0.5 && Math.abs(j.z) < 0.5);
  assertCleanTopology(net);
});

test('Y junction: three arms meet at one node', () => {
  const m = createModel();
  m.roads.push(line([[-50, 0], [0, 0]]), line([[0, 0], [50, 30]]), line([[0, 0], [50, -30]]));
  const net = buildNetwork(m);
  const deg3 = net.nodes.filter((n) => n.degree === 3);
  assert.equal(deg3.length, 1);
  assertCleanTopology(net);
});

test('bridge over a ground road is grade-separated (no merge)', () => {
  const m = createModel();
  m.roads.push(line([[-50, 0], [50, 0]]));
  m.roads.push(line([[0, -50], [0, 50]], { layer: { mode: 'bridge', bridge: 'arch', clearance: 9 } }));
  const net = buildNetwork(m);
  assert.equal(net.stats.junctions, 0);
  assert.equal(net.crossings.length, 1);
  assert.equal(net.crossings[0].kind, 'grade');
  assertCleanTopology(net);
});

test('subway under a road is grade-separated', () => {
  const m = createModel();
  m.roads.push(line([[-50, 0], [50, 0]]));
  m.roads.push(line([[0, -50], [0, 50]], { layer: { mode: 'subway', depth: 8 } }));
  const net = buildNetwork(m);
  assert.equal(net.stats.junctions, 0);
});

test('railway crossing a road produces a level-crossing record and no merge', () => {
  const m = createModel();
  m.roads.push(line([[-50, 0], [50, 0]]));
  m.roads.push(makeRoad({ points: [{ x: 0, z: -50 }, { x: 0, z: 50 }], preset: 'railway' }));
  const net = buildNetwork(m);
  assert.equal(net.stats.junctions, 0);
  assert.equal(net.crossings[0].kind, 'level-crossing');
});

test('overlapping roads are reported as a warning', () => {
  const m = createModel();
  m.roads.push(line([[0, 0], [100, 0]]), line([[0, 0.2], [100, 0.2]]));
  const net = buildNetwork(m);
  assert.ok(net.warnings.some((w) => w.includes('Overlapping')));
});

test('centripetal Catmull-Rom passes through its control points', () => {
  const ctrl = [{ x: 0, z: 0 }, { x: 10, z: 5 }, { x: 25, z: -5 }, { x: 40, z: 0 }];
  const pts = catmullRom(ctrl, 1);
  for (const c of ctrl) assert.ok(pts.some((p) => Math.hypot(p.x - c.x, p.z - c.z) < 0.05));
});

test('random grid of crossing roads keeps a clean planar topology', () => {
  let seed = 7;
  const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
  const m = createModel();
  for (let i = 0; i < 6; i++) {
    m.roads.push(line([[-200, -150 + i * 60 + rnd() * 10], [-60, -150 + i * 60 + rnd() * 20], [200, -140 + i * 60 + rnd() * 10]]));
    m.roads.push(line([[-150 + i * 60 + rnd() * 10, -200], [-140 + i * 60, 200]]));
  }
  const net = buildNetwork(m);
  assert.ok(net.stats.junctions > 10);
  assertCleanTopology(net);
});
