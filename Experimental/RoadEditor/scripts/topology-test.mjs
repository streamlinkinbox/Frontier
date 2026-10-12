// Headless regression: at-grade crossings become shared graph nodes, grade separation does not.
import { RoadNetwork } from '../src/road/network.js';
import { NetworkBuilder } from '../src/road/builder.js';
import { Editor } from '../src/editor/editor.js';

async function editorFor(net) {
  const editor = new Editor();
  editor.net = net;
  editor.builder = new NetworkBuilder(net);
  await editor.builder.buildAll();
  editor.world = { applyUpdate() {}, setSelection() {} };
  return editor;
}

function crossing({ elevated = false } = {}) {
  const net = new RoadNetwork();
  const a = net.addNode({ x: -60, z: 0, y: 0 });
  const b = net.addNode({ x: 60, z: 0, y: 0 });
  const c = net.addNode({ x: 0, z: -60, y: elevated ? 5 : 0 });
  const d = net.addNode({ x: 0, z: 60, y: elevated ? 5 : 0 });
  net.addEdge({ a: a.id, b: b.id, profile: 'local', name: 'East West' });
  net.addEdge({ a: c.id, b: d.id, profile: 'local', name: 'North South' });
  return net;
}

const atGrade = await editorFor(crossing());
const fixed = atGrade.repairTopology();
const junction = [...atGrade.net.nodes.values()].find(n => Math.hypot(n.x, n.z) < 0.1);
if (fixed !== 1 || atGrade.net.nodes.size !== 5 || atGrade.net.edges.size !== 4 || !junction || atGrade.net.nodeEdges(junction.id).length !== 4) {
  throw new Error(`at-grade topology failed: fixed=${fixed}, nodes=${atGrade.net.nodes.size}, edges=${atGrade.net.edges.size}`);
}

const gradeSeparated = await editorFor(crossing({ elevated: true }));
const skipped = gradeSeparated.repairTopology();
if (skipped !== 0 || gradeSeparated.net.nodes.size !== 4 || gradeSeparated.net.edges.size !== 2) {
  throw new Error(`grade-separated topology failed: fixed=${skipped}, nodes=${gradeSeparated.net.nodes.size}, edges=${gradeSeparated.net.edges.size}`);
}

console.log('topology repair: at-grade weld passed; bridge/underpass separation passed');
