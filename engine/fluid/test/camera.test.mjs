// CPU-only checks of the Unreal-style editor camera and the picking helpers (no GPU, no DOM).
import { EditorCamera, CAMERA_SPEEDS } from '../demo/EditorCamera.js';
import { screenRay, projectToNdc, rayOBB, rayHeightfield, pickScene, objectBounds } from '../demo/picking.js';
import { buildScene } from '../demo/scene.js';
import { Vehicle } from '../demo/vehicle.js';
import { Props } from '../demo/props.js';
import { v3 } from '../src/math.js';

let fail = 0;
const check = (name, ok, detail) => { if (!ok) fail++; console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${detail ? '  — ' + detail : ''}`); };
const DEG = Math.PI / 180;
const near = (a, b, eps) => Math.abs(a - b) <= eps;
const f3 = (v) => `[${v.map((x) => x.toFixed(2)).join(', ')}]`;
const run = (cam, secs, fly, dt = 1 / 60) => { for (let t = 0; t < secs - 1e-9; t += dt) cam.update(dt, fly); };
const fresh = (o = {}) => new EditorCamera({ position: [0, 10, 0], target: [0, 10, 10], ...o });

{ // basis vectors and setLookAt
  const cam = new EditorCamera({ position: [1, 2, 3], target: [4, 6, 3] });
  const f = cam.forward();
  check('setLookAt points the camera at the target', v3.dot(f, v3.norm([3, 4, 0])) > 0.9999, f3(f));
  const r = cam.right(), u = cam.up();
  check('right = forward × up, horizontal; up ⟂ both', near(r[1], 0, 1e-9) && near(v3.dot(r, f), 0, 1e-9) && near(v3.dot(u, f), 0, 1e-9) && u[1] > 0);
  const c2 = fresh();
  check('looking down +Z, right is -X (right-handed, Y up)', near(c2.right()[0], -1, 1e-9));
}

{ // RMB drag: look
  const cam = fresh();
  const r0 = cam.right(), p0 = cam.position.slice();
  cam.look(100, 0);
  check('RMB drag right turns right', v3.dot(cam.forward(), r0) > 0.25, `forward ${f3(cam.forward())}`);
  check('RMB look does not move the camera', v3.len(v3.sub(cam.position, p0)) < 1e-9);
  cam.look(0, -100);
  check('RMB drag up looks up', cam.pitch > 0.3, `pitch ${(cam.pitch / DEG).toFixed(1)}°`);
  cam.look(0, -100000);
  check('pitch is clamped below 90°', cam.pitch < 89.01 * DEG && cam.pitch > 88 * DEG);
  cam.invertY = true; const p1 = cam.pitch; cam.look(0, -50);
  check('invert Y flips the vertical look', cam.pitch < p1);
}

{ // LMB drag: forward on the ground plane + yaw
  const cam = fresh();
  cam.look(0, 120); // look down a bit – LMB drag must still move horizontally
  const f0 = cam.forwardFlat(), p0 = cam.position.slice();
  cam.moveTurn(0, -100);
  const d = v3.sub(cam.position, p0);
  const want = 100 * cam.dragFactor * cam.speed;
  check('LMB drag up moves forward along the ground plane', near(v3.dot(d, f0), want, 1e-6) && near(d[1], 0, 1e-9), `moved ${f3(d)} (expected ${want.toFixed(2)} m)`);
  const r0 = cam.right();
  cam.moveTurn(80, 0);
  check('LMB drag right turns right', v3.dot(cam.forwardFlat(), r0) > 0.2);
}

{ // MMB / LMB+RMB: pan
  const cam = fresh();
  const p0 = cam.position.slice(), f0 = cam.forward(), r0 = cam.right();
  cam.pan(50, -30);
  const d = v3.sub(cam.position, p0), k = cam.dragFactor * cam.speed;
  check('pan follows the mouse: right and up', near(v3.dot(d, r0), 50 * k, 1e-6) && near(d[1], 30 * k, 1e-6), f3(d));
  check('pan does not rotate', v3.dot(cam.forward(), f0) > 0.999999);
}

{ // fly with WASD / QE / RF, boost, smoothing
  const cam = fresh({ speedSetting: 5 });
  cam.look(0, 150); // pitched down: W flies along the view direction
  const f = cam.forward(), p0 = cam.position.slice();
  run(cam, 1, { forward: 1 });
  const d = v3.sub(cam.position, p0);
  check('RMB+W flies along the view direction', v3.dot(v3.norm(d), f) > 0.9999, `${v3.len(d).toFixed(2)} m in 1 s`);
  check('fly speed ≈ setting (8 m/s at 5), smoothed start', v3.len(d) > 0.9 * cam.speed && v3.len(d) <= cam.speed + 1e-6, `${v3.len(d).toFixed(2)} m vs ${cam.speed} m/s`);
  const p1 = cam.position.slice();
  run(cam, 0.5, null);
  const glide = v3.len(v3.sub(cam.position, p1));
  check('stops shortly after the key is released', glide < 1.0 && v3.len(cam.velocity) === 0, `glide ${glide.toFixed(2)} m`);

  const c2 = fresh(); c2.look(40, 120);
  const q0 = c2.position.slice();
  run(c2, 1, { up: 1 });
  const dq = v3.sub(c2.position, q0);
  check('E moves up in world space', dq[1] > 7 && Math.hypot(dq[0], dq[2]) < 1e-6, f3(dq));
  run(c2, 1, null); // let the previous motion settle
  const q1 = c2.position.slice(), u = c2.up();
  run(c2, 1, { localUp: 1 });
  check('R moves up in camera space', v3.dot(v3.norm(v3.sub(c2.position, q1)), u) > 0.9999);

  const c3 = fresh(); const s0 = c3.position.slice();
  run(c3, 1, { forward: 1, right: 1 });
  const diag = v3.len(v3.sub(c3.position, s0));
  check('diagonal flight is not faster', diag <= c3.speed + 1e-6, `${diag.toFixed(2)} m`);
  const c4 = fresh(); const b0 = c4.position.slice();
  run(c4, 1, { forward: 1, boost: true });
  check('Shift boost ×3', v3.len(v3.sub(c4.position, b0)) > 2.7 * c4.speed);
}

{ // camera speed + wheel
  const cam = fresh();
  check('8 speed settings, default 5 = 8 m/s', CAMERA_SPEEDS.length === 8 && cam.speed === 8);
  cam.changeSpeed(+2);
  check('wheel up with RMB raises the speed', cam.speedSetting === 7 && cam.speed === 32);
  cam.changeSpeed(+10); const hi = cam.speedSetting; cam.changeSpeed(-20);
  check('speed is clamped to 1..8', hi === 8 && cam.speedSetting === 1);
  const c2 = fresh(); const p0 = c2.position.slice();
  c2.wheel(2);
  run(c2, 1, null);
  const d = v3.sub(c2.position, p0);
  check('wheel moves forward by increments (smoothed)', near(v3.dot(d, c2.forward()), 2 * c2.wheelStep * c2.speed, 1e-3), `${v3.len(d).toFixed(2)} m`);
  c2.wheel(-1); run(c2, 1, null);
  check('wheel down moves back', v3.dot(v3.sub(c2.position, p0), c2.forward()) < 2 * c2.wheelStep * c2.speed - 0.5);
}

{ // Z / C FOV zoom, snaps back on RMB release
  const cam = fresh();
  run(cam, 0.5, { zoom: -1 });
  const zoomed = cam.fov;
  check('C zooms in (lower FOV)', zoomed < cam.baseFov - 15 * DEG, `${(zoomed / DEG).toFixed(1)}°`);
  run(cam, 0.5, null);
  check('FOV is held while RMB stays down', cam.fov === zoomed);
  cam.endFlight();
  check('FOV snaps back when RMB is released', cam.fov === cam.baseFov);
  run(cam, 10, { zoom: 1 });
  check('Z zooms out, clamped', cam.fov === cam.maxFov);
}

{ // Alt+LMB orbit
  const cam = fresh();
  cam.look(0, 200);
  const pivot = cam.target(15);
  cam.setPivot(pivot);
  cam.orbit(150, -60);
  const d = v3.len(v3.sub(cam.position, pivot));
  const aim = v3.dot(v3.norm(v3.sub(pivot, cam.position)), cam.forward());
  check('orbit keeps the distance to the pivot', near(d, 15, 1e-6), `${d.toFixed(4)} m`);
  check('orbit keeps looking at the pivot', aim > 0.999999);
  // an off-centre pivot keeps its place on screen
  const c2 = fresh(); c2.look(30, 100);
  const off = v3.add(c2.target(20), v3.scale(c2.right(), 4));
  c2.setPivot(off);
  const ndc0 = projectToNdc(c2.position, c2.target(1), c2.fov, 1.5, off);
  c2.orbit(-120, 40);
  const ndc1 = projectToNdc(c2.position, c2.target(1), c2.fov, 1.5, off);
  check('orbit around an off-centre pivot keeps it fixed on screen', near(ndc0[0], ndc1[0], 1e-6) && near(ndc0[1], ndc1[1], 1e-6), `${f3(ndc0)} → ${f3(ndc1)}`);
  const c3 = fresh(); c3.setPivot(c3.target(10)); c3.orbit(0, 100000);
  check('orbit pitch is clamped', Math.abs(c3.pitch) <= 89 * DEG + 1e-9);
}

{ // Alt+RMB dolly, Alt+MMB track
  const cam = fresh(); cam.look(0, 100);
  const pivot = cam.target(20); cam.setPivot(pivot);
  const dir0 = v3.norm(v3.sub(pivot, cam.position));
  cam.dollyToPivot(100, 0);
  const d1 = v3.len(v3.sub(pivot, cam.position));
  check('Alt+RMB drag right dollies in', d1 < 20 * 0.7, `${d1.toFixed(2)} m`);
  check('dolly moves along the line to the pivot', v3.dot(v3.norm(v3.sub(pivot, cam.position)), dir0) > 0.999999);
  cam.dollyToPivot(0, 100000);
  check('dolly drag down backs away', v3.len(v3.sub(pivot, cam.position)) > 20);
  cam.dollyToPivot(1e6, 0);
  check('dolly never passes the pivot', v3.len(v3.sub(pivot, cam.position)) >= cam.minPivotDistance - 1e-9);

  const c2 = fresh(); const pv = c2.target(10); c2.setPivot(pv);
  const H = 600, ndc0 = projectToNdc(c2.position, c2.target(1), c2.fov, 1.5, pv);
  c2.track(60, 0, H);
  const ndc1 = projectToNdc(c2.position, c2.target(1), c2.fov, 1.5, pv);
  // the camera moves right, so the old pivot point slides left by exactly 60 px
  check('Alt+MMB track: camera follows the mouse, 1 px per px at the pivot', near((ndc0[0] - ndc1[0]) * (H * 1.5) / 2, 60, 1e-6), `Δ ${((ndc0[0] - ndc1[0]) * H * 0.75).toFixed(3)} px`);
  check('track carries the pivot along', near(v3.len(v3.sub(c2.pivot, c2.position)), 10, 1e-9));
}

{ // F focus
  const cam = fresh({ position: [30, 25, -40] });
  cam.look(0, -300); // looking up at the sky: focus must still look down on the target
  const C = [5, 1, 7], R = 2.6;
  cam.focus(C, R, { aspect: 16 / 9, duration: 0.35 });
  run(cam, 0.2, null);
  check('focus animates', cam.focusing);
  run(cam, 0.3, null);
  const ndc = projectToNdc(cam.position, cam.target(1), cam.fov, 16 / 9, C);
  const dist = v3.len(v3.sub(C, cam.position));
  check('focus centres the target', !cam.focusing && Math.hypot(ndc[0], ndc[1]) < 1e-6, f3(ndc));
  check('focus distance fits the bounding sphere', dist > R / Math.sin(cam.fov / 2) && dist < 2.2 * R / Math.sin(cam.fov / 2), `${dist.toFixed(2)} m`);
  check('focus looks down 12–70°', cam.pitch <= -12 * DEG + 1e-9 && cam.pitch >= -70 * DEG - 1e-9, `${(cam.pitch / DEG).toFixed(1)}°`);
  check('focus sets the orbit pivot', v3.len(v3.sub(cam.pivot, C)) < 1e-9);
  // a moving target is tracked during the transition
  let mv = [0, 0, 0];
  const c2 = fresh();
  c2.focus(() => mv, 1, { duration: 0.3 });
  for (let i = 0; i < 60 && c2.focusing; i++) { mv = [i * 0.2, 0, 0]; c2.update(1 / 60, null); }
  const n2 = projectToNdc(c2.position, c2.target(1), c2.fov, 16 / 9, mv);
  check('focus tracks a target that moves during the transition', !c2.focusing && Math.hypot(n2[0], n2[1]) < 1e-6, `target moved ${mv[0].toFixed(1)} m`);
  const c3 = fresh(); c3.focus(C, 1); c3.look(5, 0);
  check('any navigation cancels the focus transition', !c3.focusing);
}

{ // collision floor and bounds
  const floor = (x, z) => 2 + 0.1 * x;
  const cam = new EditorCamera({ position: [0, 5, 0], target: [0, 5, 10], floor, bounds: { min: [-50, -10, -50], max: [50, 100, 50] } });
  run(cam, 3, { up: -1 });
  check('flying down stops at the floor', near(cam.position[1], floor(cam.position[0], cam.position[2]), 1e-9), `y ${cam.position[1].toFixed(2)}`);
  run(cam, 20, { right: -1, boost: true });
  check('camera stays inside the bounds', Math.abs(cam.position[0]) <= 50 + 1e-9 && cam.position[1] >= floor(cam.position[0], 0) - 1e-9, f3(cam.position));
  cam.setPivot([0, 2, 30]);
  for (let i = 0; i < 20; i++) cam.orbit(0, 40);
  check('orbit cannot go below the floor', cam.position[1] >= floor(cam.position[0], cam.position[2]) - 1e-9);
}

{ // ray helpers
  const r = screenRay([0, 0, 0], [0, 0, 5], 60 * DEG, 2, 0, 0);
  check('screen-centre ray = view direction', v3.dot(r.dir, [0, 0, 1]) > 0.999999);
  const corner = screenRay([0, 0, 0], [0, 0, 5], 60 * DEG, 2, 1, 1);
  const back = projectToNdc([0, 0, 0], [0, 0, 5], 60 * DEG, 2, v3.scale(corner.dir, 7));
  check('screenRay and projectToNdc are inverse', near(back[0], 1, 1e-9) && near(back[1], 1, 1e-9));
  check('ray vs box', near(rayOBB([0, 0, -5], [0, 0, 1], [0, 0, 0], (v) => v, [1, 1, 1]), 4, 1e-9) && rayOBB([3, 0, -5], [0, 0, 1], [0, 0, 0], (v) => v, [1, 1, 1]) === null);
  const t = rayHeightfield([0, 10, 0], v3.norm([1, -1, 0]), (x) => 0.5 * Math.sin(x));
  const hp = v3.madd([0, 10, 0], v3.norm([1, -1, 0]), t);
  check('ray vs heightfield', t !== null && near(hp[1], 0.5 * Math.sin(hp[0]), 1e-4), `hit at ${f3(hp)}`);
  check('ray into the sky misses', rayHeightfield([0, 10, 0], v3.norm([1, 0.2, 0]), () => 0) === null);
}

{ // picking in the demo world (CPU scene, car and props)
  const scene = buildScene({ res: 128 });
  const veh = new Vehicle(scene);
  const s = scene.scenarios.beach;
  veh.reset(s.position, s.heading);
  const props = new Props(scene);
  const crate = props.spawn('crate', v3.add(s.position, [8, 3, 0]), { rotation: [0, 0, 0, 1] });
  const world = { vehicle: veh, props, surfaceAt: (x, z) => Math.max(scene.heightAt(x, z), scene.seaLevel) };
  const vc = objectBounds(null, veh).center;
  const hitCar = pickScene(world, v3.add(vc, [0, 20, 0]), [0, -1, 0]);
  check('click on the car selects the car', hitCar?.object?.kind === 'vehicle', `dist ${hitCar?.dist.toFixed(2)}`);
  const hitCrate = pickScene(world, v3.add(crate.body.position, [0, 0, -15]), [0, 0, 1]);
  check('click on a crate selects it', hitCrate?.object?.kind === 'prop' && hitCrate.object.item === crate && near(hitCrate.dist, 15 - 0.45, 1e-6));
  const gx = s.position[0] - 30, gz = s.position[2] + 25;
  const hitGround = pickScene(world, [gx, 60, gz], [0, -1, 0]);
  check('click on empty terrain hits the ground', hitGround && hitGround.object === null && near(hitGround.point[1], world.surfaceAt(gx, gz), 1e-3), `y ${hitGround?.point[1].toFixed(3)} vs ${world.surfaceAt(gx, gz).toFixed(3)}`);
  const sea = pickScene(world, [0, 30, -78], v3.norm([0, -1, -0.2]));
  check('ray into the bay hits the sea surface', sea && near(sea.point[1], scene.seaLevel, 1e-3), `y ${sea?.point[1].toFixed(3)}`);
  check('ray into the sky hits nothing', pickScene(world, [0, 40, 0], v3.norm([0.3, 1, 0.2])) === null);
  const occl = pickScene(world, v3.add(vc, [0, -5, 0]), [0, 1, 0]);
  check('objects behind the terrain are not picked', occl === null || occl.object === null || occl.dist === 0);
}

console.log(fail ? `\n${fail} camera check(s) FAILED` : '\nall camera checks passed');
process.exit(fail ? 1 : 0);
