// Headless smoke test (node test/smoke.mjs) — exercises everything that does
// not need a DOM/WebGL context: maze gen, solvability, rail-line picking,
// rail curve sanity, car physics in-maze, cart traffic simulation.
import * as THREE from 'three';
import { generateMaze, bfsPath, pickRailLines } from '../src/maze.js';
import { CFG, tileToWorldX, tileToWorldZ, worldToTileX, worldToTileZ } from '../src/config.js';
import { Car } from '../src/car.js';
import { CartSystem } from '../src/carts.js';

let failures = 0;
const ok = (cond, msg) => { if (!cond) { failures++; console.error('FAIL:', msg); } };

// ---------- 1) maze generation + solvability across many seeds ----------
for (let seed = 1; seed <= 300; seed++) {
  const maze = generateMaze(seed * 7919);
  const sol = bfsPath(maze, [0, 0], [maze.W - 1, maze.H - 1]);
  if (!sol.length) { failures++; console.error('FAIL: unsolvable maze, seed', seed * 7919); continue; }
  // every consecutive pair must have an open wall between them
  for (let i = 1; i < sol.length; i++) {
    const [px, pz] = sol[i - 1], [cx, cz] = sol[i];
    if (!maze.isOpen(px + cx + 1, pz + cz + 1)) {
      failures++; console.error('FAIL: path crosses wall, seed', seed * 7919);
      break;
    }
  }
}
console.log('maze solvability: 300 seeds ok');

// ---------- 2) rail lines ----------
{
  const maze = generateMaze(42);
  const lines = pickRailLines(maze, CFG.RAIL_LINES, [[0, 0], [maze.W - 1, maze.H - 1]]);
  ok(lines.length >= 1, 'at least one rail line');
  for (const cells of lines) {
    ok(cells.length >= 8, 'rail line long enough');
    for (let i = 1; i < cells.length; i++) {
      const [px, pz] = cells[i - 1], [cx, cz] = cells[i];
      ok(Math.abs(px - cx) + Math.abs(pz - cz) === 1, 'rail cells contiguous');
      ok(maze.isOpen(px + cx + 1, pz + cz + 1), 'rail passes through open wall');
    }
    ok(!(cells.some(([x, z]) => (x === 0 && z === 0) || (x === maze.W - 1 && z === maze.H - 1))),
      'rail avoids start/finish');
  }
  console.log('rail lines:', lines.map((l) => l.length + ' cells').join(', '));
}

// ---------- 3) rail curve + cart simulation ----------
{
  const sceneStub = { add() {} };
  const maze = generateMaze(1337);
  const railCellsList = pickRailLines(maze, 3, [[0, 0], [14, 14]]);
  const railLines = railCellsList.map((cells) => {
    const pts = cells.map(([cx, cz]) => new THREE.Vector3(tileToWorldX(2 * cx + 1), 0, tileToWorldZ(2 * cz + 1)));
    const curve = new THREE.CatmullRomCurve3(pts, false, 'centripetal', 0.5);
    return { curve, length: curve.getLength(), beacons: [], tiles: cells };
  });
  ok(railLines.every((l) => l.length > 40 && isFinite(l.length)), 'rail lengths sane');
  // curve must never poke through rock: sample densely, tile under point must be open
  for (const l of railLines) {
    for (let i = 0; i <= 400; i++) {
      const p = l.curve.getPointAt(i / 400);
      ok(maze.isOpen(worldToTileX(p.x), worldToTileZ(p.z)), 'rail curve stays inside tunnels');
    }
  }
  const carts = new CartSystem(sceneStub, railLines);
  const ppos = new THREE.Vector3(0, 0, 0);
  let maxD = 0;
  for (let i = 0; i < 60 * 120; i++) {   // simulate 2 minutes of traffic
    carts.update(1 / 60, ppos, null);
    for (const c of carts.carts) {
      ok(c.u >= -0.001 && c.u <= 1.001, 'cart stays on track');
      ok(isFinite(c.g.position.x) && isFinite(c.g.position.z), 'cart position finite');
      maxD = Math.max(maxD, Math.abs(c.g.position.y - 0.12));
    }
  }
  ok(carts.carts.some((c) => c.state === 'run' || c.wait < 100), 'carts cycle/run at some point');
  console.log('cart simulation: 2 sim-minutes ok,', carts.carts.length, 'carts on', railLines.length, 'lines');
}

// ---------- 4) car physics: drive forward blindly, must never NaN or tunnel ----------
{
  const sceneStub = { add() {} };
  const maze = generateMaze(7);
  performance.now = performance.now.bind(performance);
  const car = new Car(sceneStub);
  const sx = tileToWorldX(1), sz = tileToWorldZ(1);
  car.reset(sx, sz, Math.atan2(1, 0));
  let nan = false, escaped = false;
  const inputs = [
    { steer: 0, throttle: 1, handbrake: false },
    { steer: 0.6, throttle: 1, handbrake: false },
    { steer: -0.8, throttle: 1, handbrake: true },
    { steer: 0, throttle: -1, handbrake: false },
  ];
  for (let i = 0; i < 60 * 90; i++) {
    const inp = inputs[((i / 900) | 0) % inputs.length];
    car.update(1 / 60, inp, maze, []);
    if (!isFinite(car.pos.x) || !isFinite(car.pos.z) || !isFinite(car.speed)) { nan = true; break; }
    if (!maze.isOpen(worldToTileX(car.pos.x), worldToTileZ(car.pos.z))) { escaped = true; break; }
  }
  ok(!nan, 'car physics never NaN');
  ok(!escaped, 'car never tunnels through rock');
  console.log('car physics: 90 sim-seconds, final speed', car.speed.toFixed(2), 'm/s');
}

if (failures) { console.error(`\n${failures} FAILURE(S)`); process.exit(1); }
console.log('\nALL SMOKE TESTS PASSED');
