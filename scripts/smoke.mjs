// Headless smoke test: builds the whole scene and simulates gameplay.
// Run: node scripts/smoke.mjs
import * as THREE from 'three';
import { buildTerrain, terrainHeight } from '../src/terrain.js';
import { buildOcean } from '../src/ocean.js';
import { buildProps } from '../src/props.js';
import { buildCar, buildTanks } from '../src/vehicles.js';
import { buildFortress } from '../src/fortress.js';
import { createGame } from '../src/game.js';
import { WORLD } from '../src/layout.js';

let failures = 0;
const ok = (name, cond, extra = '') => {
  if (cond) console.log(`  ✓ ${name}`);
  else { console.error(`  ✗ ${name} ${extra}`); failures++; }
};

console.log('— building world —');
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(72, 16 / 9, 0.1, 1200);

buildTerrain(scene);
const ocean = buildOcean(scene);
const { tankMines, apMines } = buildProps(scene);
const tanks = buildTanks(scene);
const car = buildCar();
scene.add(car);
const { sentries } = buildFortress(scene);

let meshes = 0, triangles = 0;
scene.traverse((o) => {
  if (o.isMesh) {
    meshes++;
    const g = o.geometry;
    if (g.index) triangles += g.index.count / 3;
    else if (g.attributes.position) triangles += g.attributes.position.count / 3;
  }
});
ok(`scene built (${meshes} meshes, ~${Math.round(triangles)} tris)`, meshes > 200);
ok('tank mines placed', tankMines.length > 50, `got ${tankMines.length}`);
ok('AP mines placed', apMines.length > 30, `got ${apMines.length}`);
ok('sentries placed', sentries.length >= 11, `got ${sentries.length}`);
ok('tanks placed', tanks.length === 5);

// terrain sanity
const h0 = terrainHeight(0, 100);
const h1 = terrainHeight(0, -18);
ok('terrain rises toward the wall', h0 > h1, `${h0} > ${h1}`);
ok('sea floor below start sea level', terrainHeight(0, -55) < WORLD.seaLevel);

console.log('— simulating game —');
const game = createGame({ scene, camera, ocean, sentries, tankMines, apMines, tanks, car });
game.start();

function tick(seconds, { keys = {}, mouseDX = 0 } = {}) {
  for (const [code, v] of Object.entries(keys)) {
    if (v) game.keydown(code); else game.keyup(code);
  }
  const steps = Math.round(seconds * 60);
  for (let i = 0; i < steps; i++) {
    ocean.update(i / 60, 1 / 60);
    game.update(1 / 60);
  }
}

// 1. walk forward for 5s
tick(0.1, { keys: { KeyW: true } });
const z0 = game.state.player.pos.z;
tick(5, { keys: { KeyW: true } });
const z1 = game.state.player.pos.z;
ok('player walks toward the wall', z1 > z0 + 10, `z ${z0.toFixed(1)} -> ${z1.toFixed(1)}`);

// 2. mouse look
game.mouse(200, 0);
ok('mouse look rotates', game.state.player.yaw !== Math.PI);

// 3. back to car, enter it, drive
game.state.player.pos.copy(car.position).add(new THREE.Vector3(1, 0, 0));
tick(0.05, { keys: { KeyE: true } });
ok('entered car', game.state.carState.occupied === true);

tick(6, { keys: { KeyW: true } });
ok('car drives', Math.abs(game.state.carState.speed) > 2, `speed ${game.state.carState.speed.toFixed(2)}`);

// 4. force a mine detonation under the car
const m = tankMines[0];
car.position.set(m.x, terrainHeight(m.x, m.z), m.z);
tick(0.1, {});
ok('mine exploded', m.exploded === true);
ok('car took damage', game.state.carState.hp < 100, `hp ${game.state.carState.hp.toFixed(1)}`);

// 5. sentry fire: put the car in front of a bunker
game.keyup('KeyW');
game.keyup('KeyE');
car.position.set(-26, terrainHeight(-26, 100), 100);
game.state.carState.hp = 100;
game.state.carState.occupied = true;
let sawDamage = false;
for (let i = 0; i < 60 * 30; i++) {
  ocean.update(i / 60, 1 / 60);
  game.update(1 / 60);
  if (game.state.carState.hp < 100) { sawDamage = true; break; }
  if (game.state.over) break;
}
ok('sentries shoot the vehicle', sawDamage, `car hp ${game.state.carState.hp.toFixed(1)}`);

// 6. tide rises
ocean.t = 0;
tick(120, {});
ok('tide rose', ocean.level > WORLD.seaLevel + 0.5, `level ${ocean.level.toFixed(2)}`);

// 7. win by reaching the gate
game.state.playing = true;
game.state.over = false;
game.state.carState.occupied = false;
game.state.player.pos.set(0, terrainHeight(0, 140), 140);
game.state.player.yaw = Math.PI;
tick(3.5, { keys: { KeyW: true } });
ok('reaching the gate wins the game', game.state.over === true);

console.log(failures === 0 ? '\nALL SMOKE TESTS PASSED' : `\n${failures} FAILURES`);
process.exit(failures === 0 ? 0 : 1);
