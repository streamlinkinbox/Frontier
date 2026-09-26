// Train traffic: junction interlocking + spacing must keep trains from
// overlapping, and nothing may gridlock (deadlock breaker = "ghost" events).
import * as THREE from 'three';
import { createMaze, DEFAULT_PARAMS } from '../src/network.js';
import { buildMine } from '../src/mineBuilder.js';
import { World } from '../src/world.js';
import { Traffic } from '../src/traffic.js';
let failed = false;
for (const seed of [7, 2]) {
  const mine = buildMine(createMaze(seed), DEFAULT_PARAMS);
  const world = new World(); world.setGeometry(mine.collisionGeometry);
  const tr = new Traffic(new THREE.Scene(), world); tr.setMine(mine);
  for (const [count, speed, wagons, maxGhost] of [[10, 1, 3, 2], [25, 1.5, 4, 12]]) {
    tr.spawn({ count, speed, wagons, seed: 12 });
    let overlaps = 0, ghosts = 0, moving = 0, samples = 0, t0 = performance.now();
    for (let f = 0; f < 60 * 45; f++) {
      const g0 = tr.trains.map((t) => t.ghost > 0);
      tr.update(1 / 60);
      tr.trains.forEach((t, i) => { if (!g0[i] && t.ghost > 0) ghosts++; });
      if (f % 30) continue;
      samples++;
      const cars = []; tr.forEachCar((c, t) => cars.push([c, t]));
      for (let i = 0; i < cars.length; i++) for (let j = i + 1; j < cars.length; j++)
        if (cars[i][1] !== cars[j][1] && cars[i][0].pos.distanceTo(cars[j][0].pos) < 1.6) overlaps++;
      moving += tr.trains.filter((t) => t.speed > 1).length / tr.trains.length;
    }
    const ok = overlaps === 0 && ghosts <= maxGhost;
    if (!ok) failed = true;
    console.log(`seed ${seed}: ${ok ? 'OK ' : 'FAIL'} ${count} trains ×${speed} speed: overlaps=${overlaps} deadlock-breaks=${ghosts} moving=${(100 * moving / samples).toFixed(0)}% ${((performance.now() - t0) / 2700).toFixed(2)} ms/frame`);
  }
}
process.exit(failed ? 1 : 0);
