import * as THREE from 'three';
import { CFG, worldToTileX, worldToTileZ } from './config.js';
import { clamp, damp, formatTime } from './util.js';
import { generateMaze, pickRailLines } from './maze.js';
import { buildLevel } from './level.js';
import { Car } from './car.js';
import { CartSystem } from './carts.js';
import { AudioSys } from './audio.js';
import { HUD } from './hud.js';

// ===========================================================================
// boot
// ===========================================================================
const hud = new HUD();
let renderer, scene, camera, maze, level, car, carts, audio;
let lampPool = [], conePool = [], dust, dustPos;
let state = 'menu';            // menu | count | racing | wreckPause | finished
let raceT = 0, countT = 0, msgT = 0, invulnT = 0, health = CFG.HEALTH;
let nextCP = 0, respawn = null, camMode = 0, trauma = 0;
let best = null, hits = 0, scrapeT = 0;
const clock = new THREE.Clock();

const url = new URL(location.href);
const seed = parseInt(url.searchParams.get('seed') || '', 10) || ((Math.random() * 1e9) | 0);

const input = { steer: 0, throttle: 0, handbrake: false };
const keys = {};

init();

function init() {
  try {
    renderer = new THREE.WebGLRenderer({ canvas: document.getElementById('game'), antialias: true });
    renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
    renderer.setSize(innerWidth, innerHeight);
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.12;

    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x050302);
    scene.fog = new THREE.FogExp2(0x0a0705, 0.024);

    camera = new THREE.PerspectiveCamera(64, innerWidth / innerHeight, 0.1, 240);
    camera.position.set(0, 4, -8);

    scene.add(new THREE.HemisphereLight(0x33415c, 0x1d130a, 0.55));

    // --- world ---
    maze = generateMaze(seed);
    const railCellsList = pickRailLines(maze, CFG.RAIL_LINES, [[0, 0], [maze.W - 1, maze.H - 1]]);
    level = buildLevel(scene, maze, railCellsList);

    // --- actors ---
    car = new Car(scene);
    car.reset(level.startPose.x, level.startPose.z, level.startPose.yaw);
    respawn = { ...level.startPose };
    carts = new CartSystem(scene, level.railLines);
    audio = new AudioSys();

    // --- dynamic lantern light pool (+ fake volumetric cones) ---
    for (let i = 0; i < CFG.LAMP_POOL; i++) {
      const l = new THREE.PointLight(0xffb46b, 30, 25, 1.75);
      scene.add(l);
      lampPool.push(l);
      const cone = new THREE.Mesh(
        new THREE.ConeGeometry(2.35, 3.5, 18, 1, true),
        new THREE.MeshBasicMaterial({
          color: 0xffb46b, transparent: true, opacity: 0.075,
          blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide,
        })
      );
      cone.visible = false;
      scene.add(cone);
      conePool.push(cone);
    }

    // --- floating dust motes ---
    const N = 260;
    dustPos = new Float32Array(N * 3);
    for (let i = 0; i < N; i++) {
      dustPos[i * 3] = level.startPose.x + (Math.random() - 0.5) * 44;
      dustPos[i * 3 + 1] = Math.random() * 4.8;
      dustPos[i * 3 + 2] = level.startPose.z + (Math.random() - 0.5) * 44;
    }
    const dustGeo = new THREE.BufferGeometry();
    dustGeo.setAttribute('position', new THREE.BufferAttribute(dustPos, 3));
    dust = new THREE.Points(dustGeo, new THREE.PointsMaterial({
      color: 0xffd9a0, size: 0.055, transparent: true, opacity: 0.38,
      depthWrite: false, blending: THREE.AdditiveBlending,
    }));
    dust.frustumCulled = false;
    scene.add(dust);

    // --- HUD ---
    hud.buildMinimap(maze, level.railLines, level.finishPos);
    hud.setSeedLine(seed);
    hud.setCP(0, level.checkpoints.length);
    hud.setHearts(health);
    hud.setTime(0, null);
    loadBest();
    hud.onButtons({
      start: () => { audio.init(); audio.countBeep(false); hud.hideOverlay(); startCountdown(); },
      again: () => { hud.hideResult(); restartRun(); },
      newMap: () => { url.searchParams.set('seed', (Math.random() * 1e9) | 0); location.href = url.toString(); },
    });

    bindInput();
    addEventListener('resize', onResize);
    renderer.setAnimationLoop(tick);
  } catch (e) {
    hud.showError(e);
    console.error(e);
  }
}

// ===========================================================================
// input
// ===========================================================================
function bindInput() {
  addEventListener('keydown', (e) => {
    if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' '].includes(e.key)) e.preventDefault();
    keys[e.key.toLowerCase()] = true;
    if (e.key.toLowerCase() === 'r' && (state === 'racing')) restartRun();
    if (e.key.toLowerCase() === 'c') camMode = (camMode + 1) % 2;
    if (e.key.toLowerCase() === 'm' && audio) { audio.setMuted(!audio.muted); }
  });
  addEventListener('keyup', (e) => { keys[e.key.toLowerCase()] = false; });

  // touch buttons
  const tbind = (id, on, off) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.addEventListener('pointerdown', (e) => { e.preventDefault(); on(); });
    el.addEventListener('pointerup', off);
    el.addEventListener('pointercancel', off);
    el.addEventListener('pointerleave', off);
  };
  tbind('t-left', () => (input.tLeft = true), () => (input.tLeft = false));
  tbind('t-right', () => (input.tRight = true), () => (input.tRight = false));
  tbind('t-gas', () => (input.tGas = true), () => (input.tGas = false));
  tbind('t-brake', () => (input.tBrake = true), () => (input.tBrake = false));
}

function readInput() {
  input.steer = ((keys['a'] || keys['arrowleft'] || input.tLeft) ? -1 : 0)
              + ((keys['d'] || keys['arrowright'] || input.tRight) ? 1 : 0);
  input.throttle = ((keys['w'] || keys['arrowup'] || input.tGas) ? 1 : 0)
                 + ((keys['s'] || keys['arrowdown'] || input.tBrake) ? -1 : 0);
  input.handbrake = !!keys[' '];
}

// ===========================================================================
// race flow
// ===========================================================================
function startCountdown() {
  state = 'count';
  countT = 3.4;
  hud.message('3');
}

function restartRun() {
  car.reset(level.startPose.x, level.startPose.z, level.startPose.yaw);
  respawn = { ...level.startPose };
  raceT = 0; hits = 0; nextCP = 0; health = CFG.HEALTH; invulnT = 0;
  for (const cp of level.checkpoints) {
    cp.done = false;
    cp.ringMat.color.setHex(0x59e6ff);
    cp.ringMat.opacity = 0.6;
  }
  hud.setCP(0, level.checkpoints.length);
  hud.setHearts(health);
  hud.hideWreck();
  hud.hideResult();
  startCountdown();
}

function wreck() {
  audio.crash();
  health -= 1;
  hud.setHearts(Math.max(0, health));
  hud.hit();
  trauma = Math.min(1, trauma + 0.85);
  if (health <= 0) {
    state = 'wreckPause';
    hud.showWreck();
    setTimeout(() => {
      hud.hideWreck();
      health = CFG.HEALTH;
      hud.setHearts(health);
      car.reset(respawn.x, respawn.z, respawn.yaw);
      invulnT = 2.2;
      state = 'racing';
    }, 1600);
  } else {
    const away = car.forward.multiplyScalar(-1);
    car.vel.addScaledVector(away, 9);
    car.speed = -5;
    invulnT = 1.8;
  }
}

function passCheckpoint(cp) {
  cp.done = true;
  cp.ringMat.color.setHex(0xffd166);
  cp.ringMat.opacity = 0.18;
  audio.chime(cp.index);
  nextCP++;
  hud.setCP(nextCP, level.checkpoints.length);
  const nx = nextCP < level.checkpoints.length
    ? level.checkpoints[nextCP].pos
    : level.finishPos;
  respawn = { x: cp.pos.x, z: cp.pos.z, yaw: Math.atan2(nx.x - cp.pos.x, nx.z - cp.pos.z) };
  if (nextCP >= level.checkpoints.length) {
    hud.message('GOLD CHAMBER OPEN', '#ffd166');
    msgT = 2.2;
  }
}

function finishRun() {
  state = 'finished';
  audio.fanfare();
  let isBest = false;
  if (best == null || raceT < best) { best = raceT; isBest = true; saveBest(); }
  hud.setTime(raceT, best);
  hud.showResult(raceT, best, hits);
  hud.warn(false);
}

function loadBest() {
  try {
    const v = localStorage.getItem('gg_best_' + seed);
    best = v ? parseFloat(v) : null;
  } catch { best = null; }
  hud.setTime(raceT, best);
}
function saveBest() {
  try { localStorage.setItem('gg_best_' + seed, String(best)); } catch { /* private mode */ }
}

// ===========================================================================
// per-frame
// ===========================================================================
let lampT = 0, mmT = 0;

function tick() {
  const dt = Math.min(clock.getDelta(), 0.05);
  const t = clock.elapsedTime;
  readInput();

  // ---- state machine ----
  if (state === 'count') {
    const prev = Math.ceil(countT);
    countT -= dt;
    const cur = Math.ceil(countT);
    if (cur !== prev && cur > 0) { hud.message(String(cur)); audio.countBeep(false); }
    if (countT <= 0) {
      state = 'racing';
      raceT = 0;
      hud.message('GO!', '#59e6ff');
      msgT = 0.7;
      audio.countBeep(true);
    }
    car.update(dt, { steer: input.steer, throttle: 0, handbrake: true }, maze, carts.obstacles());
  } else if (state === 'racing') {
    raceT += dt;
    car.update(dt, input, maze, carts.obstacles());
  } else if (state === 'menu' || state === 'finished' || state === 'wreckPause') {
    car.update(dt, { steer: 0, throttle: 0, handbrake: true }, maze, carts.obstacles());
  }

  if (msgT > 0) { msgT -= dt; if (msgT <= 0) hud.message(null); }
  if (invulnT > 0) {
    invulnT -= dt;
    car.body.visible = Math.floor(t * 14) % 2 === 0;
    if (invulnT <= 0) car.body.visible = true;
  }

  // ---- carts ----
  const { nearest, hit } = carts.update(dt, car.pos, audio);
  if (state === 'racing') {
    const danger = nearest < 26;
    hud.warn(danger);
    if (danger) audio.ding();
    if (hit && invulnT <= 0) { hits++; wreck(); }
  } else {
    hud.warn(false);
  }

  // ---- wall scrape feedback ----
  scrapeT -= dt;
  if (car.wallBump > 2.5) {
    trauma = Math.min(1, trauma + car.wallBump * 0.012);
    if (scrapeT <= 0) { audio.scrape(); scrapeT = 0.14; }
  }

  // ---- checkpoints & finish ----
  if (state === 'racing') {
    const cps = level.checkpoints;
    if (nextCP < cps.length) {
      const cp = cps[nextCP];
      if (car.pos.distanceTo(cp.pos) < 4.6) passCheckpoint(cp);
    } else if (car.pos.distanceTo(level.finishPos) < 5.2) {
      finishRun();
    }
  }

  // ---- checkpoint ring animation ----
  for (let i = 0; i < level.checkpoints.length; i++) {
    const cp = level.checkpoints[i];
    const s = 1 + Math.sin(t * 3 + i) * 0.045;
    cp.ring.scale.setScalar(i === nextCP && !cp.done ? s : 1);
    cp.ring.rotation.z = t * 0.6;
    if (!cp.done) cp.ringMat.opacity = i === nextCP ? 0.55 + Math.sin(t * 4) * 0.2 : 0.22;
  }

  // ---- lantern light pool: nearest N lanterns become real lights ----
  lampT -= dt;
  if (lampT <= 0) {
    lampT = 0.22;
    const anchors = level.lampAnchors;
    const scored = [];
    for (let i = 0; i < anchors.length; i++) {
      const d2 = anchors[i].distanceToSquared(car.pos);
      if (d2 < 46 * 46) scored.push([d2, i]);
    }
    scored.sort((a, b) => a[0] - b[0]);
    for (let k = 0; k < lampPool.length; k++) {
      const l = lampPool[k], cone = conePool[k];
      if (k < scored.length) {
        const p = anchors[scored[k][1]];
        l.position.set(p.x, p.y, p.z);
        l.userData.base = 30;
        cone.visible = true;
        cone.position.set(p.x, p.y - 1.72, p.z);
      } else {
        l.userData.base = 0;
        cone.visible = false;
      }
    }
  }
  for (let k = 0; k < lampPool.length; k++) {
    const l = lampPool[k];
    if (!l.userData.base) { l.intensity = 0; continue; }
    l.intensity = l.userData.base * (0.9 + 0.1 * Math.sin(t * 11 + k * 1.7) + 0.04 * Math.sin(t * 43 + k * 3.1));
  }

  // ---- dust drift ----
  {
    const px = car.pos.x, pz = car.pos.z;
    for (let i = 0; i < dustPos.length; i += 3) {
      dustPos[i + 1] -= dt * 0.14;
      if (dustPos[i + 1] < 0.1) dustPos[i + 1] = 4.8;
      if (dustPos[i] - px > 23) dustPos[i] -= 46; else if (dustPos[i] - px < -23) dustPos[i] += 46;
      if (dustPos[i + 2] - pz > 23) dustPos[i + 2] -= 46; else if (dustPos[i + 2] - pz < -23) dustPos[i + 2] += 46;
    }
    dust.geometry.attributes.position.needsUpdate = true;
  }

  // ---- camera ----
  updateCamera(dt, t);

  // ---- HUD ----
  hud.setSpeed(Math.abs(car.speed) * 3.6);
  if (state === 'racing' || state === 'finished') hud.setTime(raceT, best);
  mmT -= dt;
  if (mmT <= 0) {
    mmT = 0.1;
    hud.drawMinimap(car.pos, car.yaw, carts.carts, level.checkpoints, nextCP, t);
  }

  // ---- engine audio ----
  audio.setEngine(
    clamp(Math.abs(car.speed) / CFG.CAR.MAX_SPEED, 0, 1),
    state === 'racing' ? input.throttle : 0,
    car.driftAmount || 0
  );

  renderer.render(scene, camera);
}

function updateCamera(dt, t) {
  const fwd = car.forward;
  const speedN = clamp(Math.abs(car.speed) / CFG.CAR.MAX_SPEED, 0, 1);

  if (camMode === 1) {
    // hood cam
    const hx = car.pos.x + fwd.x * 0.5, hz = car.pos.z + fwd.z * 0.5;
    camera.position.set(hx, 1.5, hz);
    camera.lookAt(hx + fwd.x * 30, 1.0, hz + fwd.z * 30);
  } else {
    const desired = new THREE.Vector3(
      car.pos.x - fwd.x * 7.4 - fwd.z * car.steer * 1.6,
      3.25,
      car.pos.z - fwd.z * 7.4 + fwd.x * car.steer * 1.6
    );
    // keep the camera inside the tunnel (not inside rock)
    if (!maze.isOpen(worldToTileX(desired.x), worldToTileZ(desired.z))) {
      for (let k = 0.8; k >= 0.25; k -= 0.15) {
        const cx = car.pos.x + (desired.x - car.pos.x) * k;
        const cz = car.pos.z + (desired.z - car.pos.z) * k;
        if (maze.isOpen(worldToTileX(cx), worldToTileZ(cz))) { desired.x = cx; desired.z = cz; break; }
      }
    }
    desired.y = Math.min(desired.y, CFG.WALL_H - 0.7);
    camera.position.lerp(desired, damp(5.5, dt));
    const look = new THREE.Vector3(
      car.pos.x + fwd.x * 5.2 + car.vel.x * 0.32,
      1.15,
      car.pos.z + fwd.z * 5.2 + car.vel.z * 0.32
    );
    camera.lookAt(look);
  }

  // camera shake (cart hits, scrapes)
  trauma = Math.max(0, trauma - dt * 1.7);
  if (trauma > 0) {
    const s = trauma * trauma;
    camera.position.x += (Math.sin(t * 91) * 0.35) * s;
    camera.position.y += (Math.sin(t * 83 + 4) * 0.3) * s;
    camera.rotation.z += Math.sin(t * 97 + 2) * 0.02 * s;
  }

  // speed FOV kick
  const targetFov = 62 + speedN * 15;
  if (Math.abs(camera.fov - targetFov) > 0.05) {
    camera.fov += (targetFov - camera.fov) * damp(4, dt);
    camera.updateProjectionMatrix();
  }
}

function onResize() {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
}
