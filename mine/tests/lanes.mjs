// Cart-lane checks: every junction move the traffic system allows must keep the
// cart body clear of the cave, and every lane must have at least one exit.
import * as THREE from 'three';
import { createMaze, DEFAULT_PARAMS } from '../src/network.js';
import { buildMine } from '../src/mineBuilder.js';
import { World } from '../src/world.js';
import { Traffic } from '../src/traffic.js';
let failed = false;
for (const seed of [7, 1, 2, 3, 42]) {
  const net = createMaze(seed);
  const mine = buildMine(net, DEFAULT_PARAMS);
  const world = new World(); world.setGeometry(mine.geometry);
  const tr = new Traffic(new THREE.Scene(), world); tr.rebuild(mine, 0, 1);
  let allowed = 0, rejected = 0, deadEnds = 0, minClear = 9;
  const probe = new THREE.Vector3();
  for (const lin of mine.lanes) {
    const ok = tr.allowed.get(lin);
    const all = (tr.outgoing.get(lin.to) || []).filter((l) => l.edge !== lin.edge);
    rejected += all.length - ok.length; allowed += ok.length;
    if (!ok.length) deadEnds++;
    for (const lout of ok) for (const p of tr.transitionCache.get(lin).get(lout)) {
      const h = world.sphereContact(probe.set(p.x, p.y + 1.3, p.z), 1.0);
      if (h) minClear = Math.min(minClear, 1.0 - h.depth);
    }
    // lanes themselves
    for (const p of lin.pts) {
      const h = world.sphereContact(probe.set(p.x, p.y + 1.3, p.z), 1.0);
      if (h) minClear = Math.min(minClear, 1.0 - h.depth);
    }
  }
  const ok = deadEnds === 0 && minClear > 0.75;
  if (!ok) failed = true;
  console.log(`seed ${seed}: ${ok ? 'OK ' : 'FAIL'} allowed turns=${allowed} rejected=${rejected} lanes-without-exit=${deadEnds} min body clearance=${minClear.toFixed(2)} m`);
}
process.exit(failed ? 1 : 0);
