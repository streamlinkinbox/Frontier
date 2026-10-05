import test from 'node:test';
import assert from 'node:assert/strict';
import {
  addRoadPath,
  createEdge,
  edgeLength,
  getDegrees,
  mergePlanarCrossings,
  splitEdgeAt,
} from './road-network.js';

function blankNetwork() {
  return {
    schema: 'roadworks.network',
    version: 1,
    name: 'Topology test',
    units: 'metres',
    nodes: {},
    edges: [],
    settings: { snap: true, grid: true, defaultProfile: { laneCount: 2, laneWidth: 3.5 } },
  };
}

function addNode(network, id, x, z) {
  network.nodes[id] = { id, x, z, name: id };
}

test('at-grade crossing splits both roads into one shared degree-four graph vertex', () => {
  const network = blankNetwork();
  addNode(network, 'west', -10, 0);
  addNode(network, 'east', 10, 0);
  addNode(network, 'south', 0, -10);
  addNode(network, 'north', 0, 10);
  network.edges.push(createEdge('west', 'east', network.nodes, { id: 'east-west' }));
  network.edges.push(createEdge('south', 'north', network.nodes, { id: 'south-north' }));

  const before = network.edges.reduce((sum, edge) => sum + edgeLength(edge, network.nodes, 96), 0);
  const result = mergePlanarCrossings(network);
  const generated = Object.values(network.nodes).filter((node) => node.generated);
  const degrees = getDegrees(network);
  const after = network.edges.reduce((sum, edge) => sum + edgeLength(edge, network.nodes, 96), 0);

  assert.equal(result.nodesAdded, 1);
  assert.equal(result.crossings, 1);
  assert.equal(network.edges.length, 4);
  assert.equal(generated.length, 1);
  assert.equal(degrees[generated[0].id], 4);
  assert.ok(Math.abs(after - before) < 0.02, `split changed road length by ${Math.abs(after - before)} m`);
  assert.equal(mergePlanarCrossings(network).crossings, 0, 'rebuild should be idempotent after graph welding');
});

test('already-welded shared endpoints remain a single node', () => {
  const network = blankNetwork();
  addNode(network, 'shared', 0, 0);
  addNode(network, 'west', -8, 0);
  addNode(network, 'east', 8, 0);
  addNode(network, 'north', 0, 8);
  network.edges.push(createEdge('west', 'shared', network.nodes, { id: 'west-link' }));
  network.edges.push(createEdge('shared', 'east', network.nodes, { id: 'east-link' }));
  network.edges.push(createEdge('shared', 'north', network.nodes, { id: 'north-link' }));

  const result = mergePlanarCrossings(network);
  assert.equal(result.nodesAdded, 0);
  assert.equal(Object.keys(network.nodes).length, 4);
  assert.equal(network.edges.length, 3);
  assert.equal(getDegrees(network).shared, 3);
});

test('crossing cubic splines are split on the original curves, not chord approximations', () => {
  const network = blankNetwork();
  addNode(network, 'west', -10, 0);
  addNode(network, 'east', 10, 0);
  addNode(network, 'south', 0, -10);
  addNode(network, 'north', 0, 10);
  network.edges.push(createEdge('west', 'east', network.nodes, {
    id: 'curved-east-west', c1: { x: -5, z: 6 }, c2: { x: 5, z: 6 },
  }));
  network.edges.push(createEdge('south', 'north', network.nodes, { id: 'straight-south-north' }));
  const before = network.edges.reduce((sum, edge) => sum + edgeLength(edge, network.nodes, 128), 0);

  mergePlanarCrossings(network);
  const generated = Object.values(network.nodes).filter((node) => node.generated);
  const after = network.edges.reduce((sum, edge) => sum + edgeLength(edge, network.nodes, 128), 0);

  assert.equal(generated.length, 1);
  assert.equal(getDegrees(network)[generated[0].id], 4);
  assert.ok(Math.abs(after - before) < 0.04, `Bezier split changed arc length by ${Math.abs(after - before)} m`);
});

test('bridge crossings stay grade-separated', () => {
  const network = blankNetwork();
  addNode(network, 'west', -10, 0);
  addNode(network, 'east', 10, 0);
  addNode(network, 'south', 0, -10);
  addNode(network, 'north', 0, 10);
  network.edges.push(createEdge('west', 'east', network.nodes, { id: 'ground-road' }));
  network.edges.push(createEdge('south', 'north', network.nodes, {
    id: 'flyover',
    bridge: { enabled: true, height: 7, rampFraction: 0.2 },
  }));

  const result = mergePlanarCrossings(network);
  assert.equal(result.nodesAdded, 0);
  assert.equal(network.edges.length, 2);
  assert.equal(Object.keys(network.nodes).length, 4);
});

test('clicking an existing corridor interior creates a shared snap node', () => {
  const network = blankNetwork();
  addNode(network, 'west', -10, 0);
  addNode(network, 'east', 10, 0);
  network.edges.push(createEdge('west', 'east', network.nodes, { id: 'existing' }));

  addRoadPath(network, [{ x: 0, z: 0 }, { x: 0, z: 8 }]);
  const shared = Object.values(network.nodes).find((node) => Math.abs(node.x) < 0.02 && Math.abs(node.z) < 0.02);
  assert.ok(shared, 'expected a snapped graph node at the road hit');
  assert.equal(getDegrees(network)[shared.id], 3);
});

test('manual edge split preserves the original endpoints and produces two connected pieces', () => {
  const network = blankNetwork();
  addNode(network, 'west', -8, 0);
  addNode(network, 'east', 8, 0);
  const edge = createEdge('west', 'east', network.nodes, {
    id: 'split-me', c1: { x: -4, z: 2 }, c2: { x: 4, z: 2 },
  });
  network.edges.push(edge);
  const nodeId = splitEdgeAt(network, edge.id, 0.4);
  assert.ok(nodeId);
  assert.equal(network.edges.length, 2);
  assert.equal(network.edges[0].from, 'west');
  assert.equal(network.edges[0].to, nodeId);
  assert.equal(network.edges[1].from, nodeId);
  assert.equal(network.edges[1].to, 'east');
});
