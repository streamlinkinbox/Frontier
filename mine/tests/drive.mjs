// Headless driving test: autopilot follows a random route through the maze
// (tunnels + junctions) at speed and checks the car stays on the road.
import * as THREE from 'three';
import { createMaze, DEFAULT_PARAMS } from '../src/network.js';
import { buildMine } from '../src/mineBuilder.js';
import { World } from '../src/world.js';
import { CarPhysics } from '../src/car.js';
import { Traffic } from '../src/traffic.js';

const seed = Number(process.argv[2] || 7);
const targetSpeed = Number(process.argv[3] || 30);
const net = createMaze(seed);
const P = { ...DEFAULT_PARAMS };
const mine = buildMine(net, P);
const world = new World();
world.setGeometry(mine.geometry);
const traffic = new Traffic(new THREE.Scene(), world);
traffic.rebuild(mine, 3, 11);
const route = traffic.trains[0];
route.cars.length = 1; // use as a route generator only
const car = new CarPhysics(world);
const start = traffic.pointAt(route, route.head, new THREE.Vector3());
const ahead = traffic.pointAt(route, route.head + 2, new THREE.Vector3());
car.reset(start.clone().add(new THREE.Vector3(0, 0.9, 0)), ahead.clone().sub(start));
let impacts = 0; car.impactFn = (v) => { impacts++; };
let maxSpeed = 0, minClear = 99, flips = 0, airborne = 0, dist = 0;
const dt = 1 / 60;
let routeD = route.head;
let stuck = 0, reverseT = 0;
for (let f = 0; f < 60 * 60; f++) {
  // advance route parameter to the projection of the car
  const p = new THREE.Vector3();
  let bestD = routeD, bestDist = 1e9;
  for (let k = 0; k < 60; k++) {
    traffic.pointAt(route, routeD + k * 0.5, p);
    const dd = p.distanceTo(car.pos);
    if (dd < bestDist) { bestDist = dd; bestD = routeD + k * 0.5; }
  }
  routeD = bestD;
  while (routeD + 40 > traffic.totalLen(route)) traffic.extend(route);
  const look = traffic.pointAt(route, routeD + 4 + car.speed * 0.28, new THREE.Vector3());
  const fwd = car.forward();
  const toT = look.clone().sub(car.pos).setY(0).normalize();
  const f2 = fwd.clone().setY(0).normalize();
  const cross = f2.x * toT.z - f2.z * toT.x; // + means target to the right? (y-up, +Z fwd)
  const angle = Math.atan2(cross, f2.dot(toT));
  // car's left is +X local; steering + turns left (towards +X of the car)
  const steer = THREE.MathUtils.clamp(-angle * 2.2, -1, 1);
  // slow down for sharp junction turns ahead
  let tgt = targetSpeed;
  for (let la = 5; la < 45; la += 5) {
    const a0 = traffic.pointAt(route, routeD + la, new THREE.Vector3()), a1 = traffic.pointAt(route, routeD + la + 6, new THREE.Vector3());
    const dir = a1.sub(a0).setY(0).normalize();
    const turn = Math.acos(THREE.MathUtils.clamp(dir.dot(f2), -1, 1));
    if (turn > 0.4) tgt = Math.min(tgt, 6 + la * 0.4 + (1.8 - Math.min(turn, 1.8)) * 12);
  }
  const input = { throttle: car.speed < tgt ? 1 : 0, brake: car.speed > tgt + 2 ? 1 : 0, steer, handbrake: false, boost: false };
  if (car.speed < 1) stuck += dt; else if (reverseT <= 0) stuck = 0;
  if (stuck > 1.5) { reverseT = 1.2; stuck = 0; }
  if (reverseT > 0) { reverseT -= dt; input.throttle = 0; input.brake = 1; input.steer = -steer; }
  const before = car.pos.clone();
  car.step(dt, input);
  dist += car.pos.distanceTo(before);
  maxSpeed = Math.max(maxSpeed, car.speed);
  if (car.up().y < 0.3) flips++;
  if (car.grounded === 0) airborne++;
  const lat = traffic.pointAt(route, routeD, new THREE.Vector3()).distanceTo(car.pos);
  minClear = Math.min(minClear, 6 - lat);
  if (f % (process.env.EVERY ? +process.env.EVERY : 600) === 0) console.log(`t=${(f / 60).toFixed(0)}s pos=${car.pos.toArray().map((v) => v.toFixed(1))} v=${(car.speed * 3.6).toFixed(0)}km/h grounded=${car.grounded} up.y=${car.up().y.toFixed(2)} offRoute=${lat.toFixed(2)}`);
  if (lat > 12) { console.log('LOST ROUTE at', f / 60, 's'); break; }
}
console.log(`distance=${dist.toFixed(0)}m maxSpeed=${(maxSpeed * 3.6).toFixed(0)}km/h flippedFrames=${flips} airborneFrames=${airborne} wallImpacts=${impacts}`);
