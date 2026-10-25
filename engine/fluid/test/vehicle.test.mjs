// CPU-only checks of the demo vehicle (no GPU): stable driving, steering sign, braking, mud drag.
import { buildScene } from '../demo/scene.js';
import { Vehicle } from '../demo/vehicle.js';
import { v3 } from '../src/math.js';

let fail = 0;
const check = (name, ok, detail) => { if (!ok) fail++; console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  — ' + detail : ''}`); };

const scene = buildScene({ res: 256 });
const dt = 1 / 60;
const run = (veh, secs, input, fluid = null) => { for (let t = 0; t < secs; t += dt) veh.update(dt, input, fluid); };

{ // settles on its suspension and accelerates along the track
  const car = new Vehicle(scene);
  const s = scene.scenarios.puddles;
  car.reset(s.position, s.heading);
  run(car, 1.5, { throttle: 0, brake: 0, steer: 0, handbrake: 0 });
  const up0 = car.body.dirToWorld([0, 1, 0])[1];
  const ground = scene.heightAt(car.body.position[0], car.body.position[2]);
  check('car settles upright on its wheels', up0 > 0.95 && car.wheelsOnGround >= 3, `up.y ${up0.toFixed(3)}, wheels ${car.wheelsOnGround}, ride height ${(car.body.position[1] - ground).toFixed(2)} m`);
  run(car, 4, { throttle: 1, brake: 0, steer: 0, handbrake: 0 });
  const v = car.speed;
  check('accelerates (0-4 s)', v > 14, `${(v * 3.6).toFixed(0)} km/h after 4 s`);
  check('stays upright under power', car.body.dirToWorld([0, 1, 0])[1] > 0.85);
  run(car, 3, { throttle: 0, brake: 1, steer: 0, handbrake: 0 });
  check('brakes to a stop', Math.abs(car.speed) < 3, `${(car.speed * 3.6).toFixed(1)} km/h after braking`);
}

{ // steering: +steer turns towards the car's local +X (left), check heading change sign
  const car = new Vehicle(scene);
  const s = scene.scenarios.beach;
  car.reset(s.position, s.heading);
  run(car, 1, { throttle: 0, brake: 0, steer: 0, handbrake: 0 });
  const f0 = car.body.dirToWorld([0, 0, 1]);
  run(car, 1.5, { throttle: 0.6, brake: 0, steer: 1, handbrake: 0 });
  const f1 = car.body.dirToWorld([0, 0, 1]);
  const turn = v3.cross(f0, f1)[1]; // >0 = rotated counter-clockwise seen from above
  const left = car.body.dirToWorld([1, 0, 0]);
  const movedLeft = v3.dot(v3.sub(f1, f0), left) > 0;
  check('steer +1 turns left (towards local +X)', movedLeft, `yaw change ${turn.toFixed(3)}`);
}

{ // mud: same throttle, much slower when the wheels report mud
  const mk = (mud) => {
    const car = new Vehicle(scene);
    const s = scene.scenarios.puddles;
    car.reset(s.position, s.heading);
    run(car, 1, { throttle: 0, brake: 0, steer: 0, handbrake: 0 });
    for (const w of car.wheels) { w.depth = mud ? 0.2 : 0; w.mud = mud ? 1 : 0; w.flow = [0, 0]; }
    run(car, 3, { throttle: 1, brake: 0, steer: 0, handbrake: 0 });
    return car.speed;
  };
  const dry = mk(false), mud = mk(true);
  check('deep mud slows the car (drag + rolling resistance + grip)', mud < dry * 0.75, `dry ${(dry * 3.6).toFixed(0)} km/h vs mud ${(mud * 3.6).toFixed(0)} km/h`);
}

{ // buoyancy: a car pushed into deep still water floats instead of sinking through
  const car = new Vehicle(scene);
  car.reset([0, 5, -70], 0); // over the sea
  car.body.position = [0, -1.2, -70];
  const n = car.probe.count;
  const data = new Float32Array(n * 8);
  for (let s = 0; s < n; s++) { data[s * 8] = 0.0; data[s * 8 + 1] = 6; } // sea level 0, 6 m deep
  for (let t = 0; t < 6; t += dt) car.update(dt, { throttle: 0, brake: 0, steer: 0, handbrake: 0 }, { data, offset: 0 });
  check('car floats in deep water (buoyancy)', car.body.position[1] > -1.0 && car.body.position[1] < 0.6 && car.inWater > 0.2, `centre y ${car.body.position[1].toFixed(2)} m, submerged ${(car.inWater * 100).toFixed(0)}%`);
}
console.log(fail ? `\n${fail} check(s) failed` : '\nall vehicle checks passed');
process.exit(fail ? 1 : 0);
