import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildNetwork } from '../src/network.js';
import { junctionFootprint } from '../src/junction.js';

const road = (id, ctrl, extra = {}) => ({ id, ctrl, profile: 'avenue', seed: 7, ...extra });
const edgesOf = (net, nodeId) => net.nodes[nodeId].edges.map((eid) => net.edges.find((e) => e.id === eid));
const byId = (net) => Object.fromEntries(net.edges.map((e) => [e.id, e]));

test('plus (+) crossing merges into one 4-way junction', () => {
  const net = buildNetwork([
    road('h', [[-100, 0], [100, 0]]),
    road('v', [[0, -100], [0, 100]]),
  ]);
  assert.equal(net.stats.junctions, 1);
  const j = net.nodes.find((n) => n.kind === 'junction');
  assert.equal(j.edges.length, 4);
  assert.ok(Math.abs(j.x) < 0.01 && Math.abs(j.z) < 0.01);
  assert.equal(net.edges.length, 4);
});

test('T junction: road end landing on another road', () => {
  const net = buildNetwork([
    road('main', [[-100, 0], [100, 0]]),
    road('branch', [[0, 60], [0, 1]]),
  ]);
  assert.equal(net.stats.junctions, 1);
  const j = net.nodes.find((n) => n.kind === 'junction');
  assert.equal(j.edges.length, 3);
});

test('Y junction: three road ends meeting at one point', () => {
  const net = buildNetwork([
    road('a', [[-80, 80], [0, 0]]),
    road('b', [[80, 80], [0, 0]]),
    road('c', [[0, -80], [0, 0]]),
  ]);
  assert.equal(net.stats.junctions, 1);
  assert.equal(net.nodes.find((n) => n.kind === 'junction').edges.length, 3);
});

test('no duplicate nodes: road crossing near its own end snaps once', () => {
  const net = buildNetwork([
    road('h', [[-100, 0], [100, 0]]),
    road('v', [[0, -100], [0, 0.5]]), // ends 0.5 m from h
  ]);
  assert.equal(net.stats.junctions, 1);
  assert.equal(net.stats.ends, 3); // h two ends + v free end
});

test('bridge span suppresses merge: grade-separated crossing', () => {
  const net = buildNetwork([
    road('h', [[-100, 0], [100, 0]], { spans: [{ s: 100, len: 30, type: 'arch' }] }),
    road('v', [[0, -100], [0, 100]]),
  ]);
  assert.equal(net.stats.junctions, 0, 'no junction at the bridged crossing');
  const deck = net.edges.filter((e) => e.bridge);
  assert.equal(deck.length, 1);
  const maxY = Math.max(...deck[0].pts.map((p) => p[2]));
  assert.ok(Math.abs(maxY - 9) < 0.5, 'arch clearance reached: ' + maxY);
  // Ground endpoints of the deck sit at y=0 so the ramps meet the road.
  assert.ok(Math.abs(deck[0].pts[0][2]) < 1e-6);
});

test('junction footprint is a clean polygon with positive area', () => {
  const net = buildNetwork([
    road('h', [[-100, 0], [100, 0]]),
    road('v', [[0, -100], [0, 100]]),
  ]);
  const fp = junctionFootprint(net.nodes.find((n) => n.kind === 'junction'), byId(net));
  assert.ok(fp.area > 30 * 30 * 0.5, 'area ' + fp.area);
  assert.equal(fp.indices.length % 3, 0);
  assert.ok(fp.indices.length >= 3);
  assert.ok(fp.indices.every((i) => i >= 0 && i < fp.vertices.length));
  const _ = edgesOf; void _;
});

test('deterministic: same input gives identical topology', () => {
  const input = [road('h', [[-100, 0], [100, 0]]), road('v', [[0, -100], [0, 100]])];
  assert.deepEqual(buildNetwork(input).stats, buildNetwork(input).stats);
});
