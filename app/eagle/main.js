import * as THREE from '../vendor/three.module.js';
import { buildTerrain, terrainHeight, roadY, roadZ, roadDir, WORLD, PERCH_X } from './terrain.js';
import { Car } from './car.js';
import { Eagle } from './eagle.js';
import { EagleBrain } from './eagleBrain.js';
import { FX } from './fx.js';
import { makeSky, bakeEnvironment } from './sky.js';
import { Audio } from './audio.js';
import { makeRock } from './textures.js';
import { clamp, lerp, smoothstep } from './noise.js';

const $ = (id) => document.getElementById(id);
const loading = $('loading');
const setLoad = (t) => { if (loading) loading.querySelector('p').textContent = t; };

async function tick() { await new Promise(r => setTimeout(r, 0)); }

async function boot() {
  setLoad('Creating renderer…');
  const canvas = $('c');
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
  renderer.setPixelRatio(Math.min(devicePixelRatio, 1.5));
  renderer.setSize(innerWidth, innerHeight);
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  renderer.outputColorSpace = THREE.SRGBColorSpace;

  const scene = new THREE.Scene();
  const sunDir = new THREE.Vector3(0.42, 0.56, -0.72).normalize();
  const fogColor = new THREE.Color(0.74, 0.8, 0.9);
  scene.fog = new THREE.FogExp2(fogColor, 0.00105);
  const sky = makeSky(sunDir); scene.add(sky.mesh);
  setLoad('Baking environment lighting…'); await tick();
  scene.environment = bakeEnvironment(renderer, sunDir);

  // lights
  const sun = new THREE.DirectionalLight(0xfff0d8, 3.2);
  sun.castShadow = true;
  sun.shadow.mapSize.set(4096, 4096);
  sun.shadow.camera.near = 20; sun.shadow.camera.far = 700;
  sun.shadow.camera.left = -110; sun.shadow.camera.right = 110; sun.shadow.camera.top = 110; sun.shadow.camera.bottom = -110;
  sun.shadow.bias = -0.00035; sun.shadow.normalBias = 0.5; sun.shadow.radius = 2;
  sun.shadow.camera.updateProjectionMatrix();
  scene.add(sun, sun.target);
  const hemi = new THREE.HemisphereLight(0x9fc0ff, 0x6b5a45, 0.55);
  scene.add(hemi);

  setLoad('Carving the canyon…'); await tick();
  const terrain = buildTerrain();
  scene.add(terrain.group);

  setLoad('Assembling the vehicles…'); await tick();
  const cars = [];
  const player = new Car(scene, { color: 0x1f5eff, isPlayer: true });
  player.spawnPlayer(-340, 1);
  cars.push(player);
  const N_AI = 6;
  for (let i = 0; i < N_AI; i++) {
    const c = new Car(scene, {});
    const dir = i % 2 ? -1 : 1;
    c.spawnAI(-600 + (i / N_AI) * 1200 + (Math.random() - 0.5) * 80, dir);
    cars.push(c);
  }

  setLoad('Growing feathers (this takes a moment)…'); await tick();
  const eagle = new Eagle(scene);
  const audio = new Audio();
  const fx = new FX(scene, makeRock({ seed: 11, size: 256 }));
  fx.setLighting(sunDir, fogColor, scene.fog.density);

  // ---- events from the brain
  const camera = new THREE.PerspectiveCamera(58, innerWidth / innerHeight, 0.3, 5000);
  const dist01 = (p) => clamp(camera.position.distanceTo(p) / 320, 0, 1);
  const brain = new EagleBrain(eagle, {
    onScreech: (p) => audio.screech(dist01(p)),
    onDownstroke: (p, amp, load) => {
      audio.wingbeat(dist01(p), load);
      const gh = terrainHeight(p.x, p.z);
      if (p.y - gh < 14) fx.downdraft(new THREE.Vector3(p.x, gh + 0.3, p.z), (1 - (p.y - gh) / 14) * amp);
    },
    onGrab: (car, roof) => { audio.impact(0.5, dist01(roof)); fx.crash(roof, new THREE.Vector3(0, 1, 0), 0.25, car.phys.vel); cam.shake = 0.6; },
    onRelease: () => {},
    onLand: () => { fx.downdraft(brain.perch, 1); },
  });
  const n0 = new THREE.Vector3(0, 1, 0);
  for (const c of cars) c.onCrash = (car, v, p, n) => {
    const s = clamp((v - 5) / 20, 0.2, 1.5);
    fx.crash(p, n || n0, s, car.phys.vel);
    audio.impact(s, dist01(p));
    cam.shake = Math.max(cam.shake, s * (car.isPlayer ? 1.2 : 0.5) * (1 - dist01(p)));
  };

  // ---- input
  const keys = {};
  addEventListener('keydown', (e) => { keys[e.code] = true; if (!audio.enabled) audio.init(); audio.resume(); if (e.code === 'KeyC') cam.cycle(); if (e.code === 'KeyR') respawnPlayer(); if (e.code === 'KeyH') $('help').classList.toggle('hidden'); if (e.code === 'Space') e.preventDefault(); });
  addEventListener('keyup', (e) => { keys[e.code] = false; });
  addEventListener('pointerdown', () => { if (!audio.enabled) audio.init(); audio.resume(); });
  const input = { throttle: 0, brake: 0, steer: 0, handbrake: 0 };
  let steerRaw = 0;
  function readInput(dt) {
    const up = keys.KeyW || keys.ArrowUp, dn = keys.KeyS || keys.ArrowDown, l = keys.KeyA || keys.ArrowLeft, r = keys.KeyD || keys.ArrowRight;
    input.throttle = up ? 1 : dn ? -1 : 0;
    input.brake = 0;
    input.handbrake = keys.Space ? 1 : 0;
    const target = (r ? 1 : 0) - (l ? 1 : 0);
    const rate = target !== 0 ? 3.2 : 6;
    steerRaw += clamp(target - steerRaw, -rate * dt, rate * dt);
    input.steer = steerRaw;
  }

  // ---- respawn logic
  function respawnPlayer() {
    if (brain.target === player) brain.dropTarget();
    const x = clamp(PERCH_X - 340, WORLD.xMin + 60, WORLD.xMax - 60);
    player.spawnPlayer(x, 1);
    cam.snap = true;
  }
  function recycleAI(c) {
    const dir = Math.random() < 0.5 ? 1 : -1;
    c.spawnAI(dir > 0 ? WORLD.xMin + 40 : WORLD.xMax - 40, dir);
  }

  // ---- camera director
  const cam = {
    mode: 'AUTO', modes: ['AUTO', 'CHASE', 'EAGLE', 'ORBIT'], shot: null, shotT: 0, snap: true, shake: 0,
    pos: new THREE.Vector3(), look: new THREE.Vector3(), vel: new THREE.Vector3(), orbit: { yaw: 0.6, pitch: 0.25, dist: 22 },
    cycle() { this.mode = this.modes[(this.modes.indexOf(this.mode) + 1) % this.modes.length]; this.snap = true; this.shot = null; },
  };
  let drag = null;
  canvas.addEventListener('pointerdown', (e) => { drag = { x: e.clientX, y: e.clientY }; });
  addEventListener('pointerup', () => { drag = null; });
  addEventListener('pointermove', (e) => { if (!drag) return; cam.orbit.yaw -= (e.clientX - drag.x) * 0.006; cam.orbit.pitch = clamp(cam.orbit.pitch + (e.clientY - drag.y) * 0.004, -0.2, 1.2); drag = { x: e.clientX, y: e.clientY }; if (cam.mode !== 'ORBIT') { cam.mode = 'ORBIT'; cam.snap = true; } });
  addEventListener('wheel', (e) => { cam.orbit.dist = clamp(cam.orbit.dist * (1 + e.deltaY * 0.001), 6, 120); });

  const tmpA = new THREE.Vector3(), tmpB = new THREE.Vector3(), tmpC = new THREE.Vector3();
  function chooseShot() {
    const st = brain.state, tgt = brain.target;
    if (cam.mode === 'CHASE') return 'chase';
    if (cam.mode === 'EAGLE') return 'eagleFollow';
    if (cam.mode === 'ORBIT') return 'orbit';
    // AUTO director
    if (player.mode === 'wreck' || player.mode === 'thrown') return 'wreck';
    if (st === 'ALERT' || st === 'LAUNCH') return 'perch';
    if (st === 'STOOP') return 'stoop';
    if (st === 'APPROACH' || st === 'FLARE' || st === 'GRAB') return 'strike';
    if (st === 'LIFT') return brain.t < 7 ? 'groundUp' : 'eagleFollow';
    if (st === 'SLAM_RUN' || st === 'PULL_UP') return 'wall';
    if (st === 'RETURN' && brain.t < 3.5 && brain.slammed) return 'wall';
    return 'chase';
  }
  const camState = { p: new THREE.Vector3(), l: new THREE.Vector3(), fov: 58 };
  function updateCamera(dt) {
    const shot = chooseShot();
    if (shot !== cam.shot) { cam.shot = shot; cam.shotT = 0; cam.snap = true; if (shot === 'groundUp') { cam.anchor = brain.pos.clone(); cam.anchor.y = terrainHeight(cam.anchor.x, cam.anchor.z) + 2; cam.anchor.x += 12; } }
    cam.shotT += dt;
    const P = camState.p, L = camState.l;
    let fov = 58, stiff = 6;
    const eaglePos = brain.pos, tgt = brain.target;
    const carP = player.phys.pos;
    const pv = player.phys.vel;
    switch (shot) {
      case 'chase': {
        const fwd = tmpA.set(0, 0, 1).applyQuaternion(player.phys.quat);
        const spd = pv.length();
        if (spd > 4) fwd.lerp(tmpB.copy(pv).normalize(), clamp((spd - 4) / 20, 0, 0.7)).normalize();
        fwd.y = 0; fwd.normalize();
        P.copy(carP).addScaledVector(fwd, -(8.5 + spd * 0.06)).add(tmpB.set(0, 3.0 + spd * 0.01, 0));
        const gh = terrainHeight(P.x, P.z) + 1.2; if (P.y < gh) P.y = gh;
        L.copy(carP).addScaledVector(fwd, 6).add(tmpB.set(0, 0.9, 0));
        // during the player's own abduction, pull back and look up at the bird
        if (player.mode === 'grabbed') { P.copy(carP).addScaledVector(fwd, -16).add(tmpB.set(6, -1, 0)); L.copy(carP).lerp(eaglePos, 0.4); }
        fov = 58 + clamp(spd / 70, 0, 1) * 16; stiff = 8;
        break;
      }
      case 'eagleFollow': {
        const v = brain.vel.length() > 2 ? tmpA.copy(brain.vel).normalize() : tmpA.set(Math.sin(brain.yaw), 0, Math.cos(brain.yaw));
        P.copy(eaglePos).addScaledVector(v, -26).add(tmpB.set(0, 7, 0)).addScaledVector(tmpC.set(v.z, 0, -v.x), 9);
        L.copy(eaglePos); if (tgt) L.lerp(tgt.phys.pos, 0.35);
        fov = 55; stiff = 3.5;
        break;
      }
      case 'orbit': {
        const o = cam.orbit;
        P.set(Math.sin(o.yaw) * Math.cos(o.pitch), Math.sin(o.pitch), Math.cos(o.yaw) * Math.cos(o.pitch)).multiplyScalar(o.dist).add(carP);
        const gh = terrainHeight(P.x, P.z) + 0.8; if (P.y < gh) P.y = gh;
        L.copy(carP).add(tmpB.set(0, 1, 0)); stiff = 12;
        break;
      }
      case 'perch': {
        // low angle from below the ledge looking up at the bird, target car visible in the back
        const pr = brain.perch;
        P.set(pr.x + 14, pr.y + 1.5, pr.z - 20);
        L.copy(eaglePos).add(tmpB.set(0, 1.5, 0));
        fov = 42; stiff = 2;
        break;
      }
      case 'stoop': {
        const v = tmpA.copy(brain.vel).normalize();
        P.copy(eaglePos).addScaledVector(v, -22).add(tmpB.set(0, 6, 0)).addScaledVector(tmpC.set(v.z, 0, -v.x), 12);
        L.copy(eaglePos).lerp(tgt ? tgt.phys.pos : eaglePos, 0.55);
        fov = 50; stiff = 4;
        break;
      }
      case 'strike': {
        if (!tgt) break;
        const cv = tgt.phys.vel, cs = cv.length();
        const fwd = cs > 1 ? tmpA.copy(cv).normalize() : roadDir(tgt.phys.pos.x, tmpA);
        // low tracking shot from the valley side of the car
        P.copy(tgt.phys.pos).addScaledVector(fwd, -9).add(tmpB.set(0, 3.2, -13));
        const gh = terrainHeight(P.x, P.z) + 1.0; if (P.y < gh) P.y = gh;
        L.copy(tgt.phys.pos).lerp(eaglePos, 0.45).add(tmpB.set(0, 1, 0));
        fov = 60; stiff = 10;
        break;
      }
      case 'groundUp': {
        P.copy(cam.anchor);
        L.copy(tgt ? tgt.phys.pos : eaglePos).lerp(eaglePos, 0.5);
        fov = clamp(36 + P.distanceTo(L) * -0.1, 26, 50); stiff = 4;
        break;
      }
      case 'wall': {
        const w = brain.wallPoint;
        P.set(w.x - brain.escapeDir * 38, w.y - 4, w.z - 46);
        const c = tgt || player;
        L.copy(c.phys.pos).lerp(w, 0.2);
        fov = 44; stiff = 5;
        break;
      }
      case 'wreck': {
        const o = cam.orbit; o.yaw += dt * 0.15;
        P.set(Math.sin(o.yaw), 0.35, Math.cos(o.yaw)).multiplyScalar(18).add(carP);
        const gh = terrainHeight(P.x, P.z) + 1.0; if (P.y < gh) P.y = gh;
        L.copy(carP); fov = 50; stiff = 3;
        break;
      }
    }
    if (cam.snap) { cam.pos.copy(P); cam.look.copy(L); camState.fov = fov; cam.snap = false; }
    const k = clamp(dt * stiff, 0, 1);
    cam.pos.lerp(P, k); cam.look.lerp(L, clamp(dt * stiff * 1.6, 0, 1));
    camState.fov = lerp(camState.fov, fov, clamp(dt * 3, 0, 1));
    camera.position.copy(cam.pos);
    if (cam.shake > 0.001) {
      const s = cam.shake;
      camera.position.add(tmpB.set((Math.random() - 0.5) * s, (Math.random() - 0.5) * s, (Math.random() - 0.5) * s));
      cam.shake *= Math.max(0, 1 - dt * 3);
    }
    camera.lookAt(cam.look);
    camera.fov = camState.fov; camera.updateProjectionMatrix();
  }

  // ---- HUD
  const hud = { speed: $('speed'), state: $('state'), target: $('target'), cam: $('cammode'), fps: $('fps'), msg: $('msg') };
  let fpsAcc = 0, fpsN = 0, fpsShown = 0, msgT = 0;
  function message(t, dur = 3) { hud.msg.textContent = t; hud.msg.classList.add('on'); msgT = dur; }

  addEventListener('resize', () => { renderer.setSize(innerWidth, innerHeight); camera.aspect = innerWidth / innerHeight; camera.updateProjectionMatrix(); fx.setPixelScale(innerHeight); });
  fx.setPixelScale(innerHeight);

  loading.classList.add('hidden');
  message('The eagle is watching the road. Drive past the cliff… if you dare.', 5);

  // ---- main loop
  let last = performance.now();
  let lastState = brain.state;
  const focus = new THREE.Vector3();
  function frame(now) {
    requestAnimationFrame(frame);
    let dt = Math.min(0.05, (now - last) / 1000); last = now;
    if (dt <= 0) return;
    readInput(dt);

    // player & traffic
    for (const c of cars) {
      if (c.mode === 'ai') {
        c.update(dt);
        if (c.ai.x > WORLD.xMax - 30 || c.ai.x < WORLD.xMin + 30) recycleAI(c);
      } else if (c.isPlayer && c.mode === 'player') {
        c.update(dt, input);
        // keep the player inside the world
        if (c.phys.pos.x < WORLD.xMin + 20 || c.phys.pos.x > WORLD.xMax - 20) { message('End of the canyon road — turning you around.'); c.spawnPlayer(clamp(c.phys.pos.x, WORLD.xMin + 40, WORLD.xMax - 40), c.phys.pos.x > 0 ? -1 : 1); }
        // tyre dust
        const p = c.phys;
        if (p.grounded && p.slipEnergy > 0.02) {
          for (const w of p.wheels) if (w.contact && (Math.abs(w.slipA) > 0.12 || Math.abs(w.slipR) > 0.2) && Math.random() < 0.6) fx.tyreDust(w.contactPoint, p.vel, clamp(p.slipEnergy * 8, 0, 1));
        }
        p.slipEnergy *= Math.max(0, 1 - dt * 6);
      } else if (c.mode === 'grabbed') {
        c.stepDebris(dt);
      } else { // thrown / wreck
        c.update(dt);
        if (c.damage > 0.5) fx.smoke(c.phys.pos, dt, clamp(c.damage, 0.3, 1.2));
        if (c.mode === 'wreck' && c.wreckTime > 9) {
          if (c.isPlayer) { respawnPlayer(); message('Respawned. Press R anytime to respawn, C to change camera.'); } else recycleAI(c);
        } else if (c.mode === 'thrown' && c.wreckTime > 6) { c.mode = 'wreck'; c.wreckTime = 0; c.attackable = false; }
      }
    }
    // eagle
    brain.update(dt, cars, fx);
    if (brain.state !== lastState) {
      lastState = brain.state;
      if (brain.state === 'ALERT') message(brain.target?.isPlayer ? 'The eagle has spotted YOU.' : 'The eagle has spotted a car…', 3);
      if (brain.state === 'GRAB') message(brain.target?.isPlayer ? 'Grabbed! Hold on…' : 'Talons locked. Lifting…', 3);
      if (brain.state === 'PULL_UP') message('Released into the cliff face!', 3);
    }

    fx.update(dt);
    updateCamera(dt);

    // audio
    const rpm = clamp(Math.abs(player.phys.vel.dot(tmpA.set(0, 0, 1).applyQuaternion(player.phys.quat))) / 65, 0, 1);
    audio.engine(player.mode === 'player' ? 0.15 + rpm * 0.85 : 0, player.mode === 'player' ? Math.max(0, input.throttle) : 0, dt);
    const camSpeed = Math.max(player.phys.vel.length() * (cam.shot === 'chase' ? 1 : 0.3), brain.vel.length() * (cam.shot === 'eagleFollow' || cam.shot === 'stoop' ? 1 : 0.2));
    audio.wind(clamp(camSpeed / 70, 0, 1));

    // shadows follow the action
    focus.copy(cam.look).lerp(brain.pos, 0.35);
    sun.position.copy(focus).addScaledVector(sunDir, 320);
    sun.target.position.copy(focus);
    sun.target.updateMatrixWorld();
    sky.mesh.position.copy(camera.position);

    // HUD
    const kmh = Math.round(player.phys.vel.length() * 3.6);
    hud.speed.textContent = kmh;
    hud.state.textContent = brain.state;
    hud.target.textContent = brain.target ? (brain.target.isPlayer ? 'YOU' : 'traffic #' + brain.target.id) : '—';
    hud.cam.textContent = cam.mode + (cam.mode === 'AUTO' ? ' · ' + (cam.shot || '') : '');
    fpsAcc += dt; fpsN++; if (fpsAcc > 0.5) { fpsShown = Math.round(fpsN / fpsAcc); fpsAcc = 0; fpsN = 0; hud.fps.textContent = fpsShown; }
    if (msgT > 0) { msgT -= dt; if (msgT <= 0) hud.msg.classList.remove('on'); }

    renderer.render(scene, camera);
  }
  requestAnimationFrame(frame);
}

boot().catch((e) => { console.error(e); setLoad('Failed to start: ' + e.message); });
