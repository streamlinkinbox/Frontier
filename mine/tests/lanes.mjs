import * as THREE from 'three';
import { createMaze, DEFAULT_PARAMS } from '../src/network.js';
import { buildMine } from '../src/mineBuilder.js';
import { World } from '../src/world.js';
import { Traffic } from '../src/traffic.js';
// Checks every junction transition the carts can take has body clearance.
for (const seed of [7, 1, 2, 3, 42]) {
const net = createMaze(seed);
const mine = buildMine(net, DEFAULT_PARAMS);
const world = new World(); world.setGeometry(mine.geometry);
const tr = new Traffic(new THREE.Scene(), world); tr.rebuild(mine, 0, 1);
let bad = 0, total = 0;
for (const lin of mine.lanes) {
  for (const lout of tr.outgoing.get(lin.to) || []) {
    if (lout.edge === lin.edge) continue;
    { const a = lin.pts.at(-1).clone().sub(lin.pts.at(-2)).setY(0).normalize(); const b = lout.pts[1].clone().sub(lout.pts[0]).setY(0).normalize(); if (a.dot(b) <= -0.42) continue; }
    const train = { segments: [{ pts: lin.pts, cum: null, length: 0, lane: lin }] };
    // reuse extend by faking
    const fake = { segments: [] };
    const mk = (pts, lane) => { const cum=[0]; for (let i=1;i<pts.length;i++) cum.push(cum[i-1]+pts[i].distanceTo(pts[i-1])); return {pts,cum,length:cum.at(-1),lane}; };
    fake.segments.push(mk(lin.pts, lin));
    const save = tr.outgoing.get(lin.to); tr.outgoing.set(lin.to, [lout]);
    tr.extend(fake); tr.outgoing.set(lin.to, save);
    const tseg = fake.segments[1];
    let minD = 9;
    for (const p of tseg.pts) {
      const c = p.clone().add(new THREE.Vector3(0, 1.3, 0));
      const h = world.sphereContact(c, 1.0);
      if (h) minD = Math.min(minD, 1.0 - h.depth);
    }
    total++;
    if (minD < 0.25) { bad++; console.log(`hub ${lin.to}: e${lin.edge}->e${lout.edge} clearance ${minD.toFixed(2)}`); }
  }
}
console.log(`seed ${seed}: ${bad}/${total} tight cart transitions`);
}
