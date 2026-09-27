/**
 * ANURA — main application.
 *
 * Scene assembly, the behaviour brain, the camera director, the inspection
 * HUD and the glTF export path.
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { GLTFExporter } from 'three/examples/jsm/exporters/GLTFExporter.js';

import { buildWorld, WORLD } from './world/world.js';
import { buildMaterials } from './mosquito/materials.js';
import { buildMosquito } from './mosquito/build.js';
import { GaitController } from './anim/gait.js';
import { FlightController } from './anim/flight.js';
import { FeedingController } from './anim/feeding.js';
import { BODY, LEGS, WING, FLIGHT, GAIT, FEED, clamp, lerp, smoothstep } from './mosquito/anatomy.js';
import { ClipBaker } from './export/bake.js';
import { makeRadialAlpha } from './render/textures.js';

const $ = (s) => document.querySelector(s);
const boot = (msg, pct) => { $('#bootMsg').textContent = msg; $('#bootBar').style.width = pct + '%'; };

/* ==================================================================== */
/*  Renderer                                                             */
/* ==================================================================== */

const container = $('#app');
const renderer = new THREE.WebGLRenderer({
  antialias: true, powerPreference: 'high-performance', stencil: false,
});
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.05;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
container.appendChild(renderer.domElement);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(30, innerWidth / innerHeight, 0.4, 30000);
camera.position.set(16, 10, 20);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.07;
controls.minDistance = 2.2;
controls.maxDistance = 6000;
controls.maxPolarAngle = Math.PI * 0.52;

let composer = null, bloomPass = null, renderPass = null;
function buildComposer() {
  composer = new EffectComposer(renderer);
  renderPass = new RenderPass(scene, camera);
  composer.addPass(renderPass);
  bloomPass = new UnrealBloomPass(new THREE.Vector2(innerWidth, innerHeight), 0.32, 0.62, 0.86);
  composer.addPass(bloomPass);
  composer.addPass(new OutputPass());
  composer.setSize(innerWidth, innerHeight);
  composer.setPixelRatio(Math.min(devicePixelRatio, 2));
}

addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
  composer?.setSize(innerWidth, innerHeight);
});

/* ==================================================================== */
/*  Build                                                                */
/* ==================================================================== */

boot('Synthesising terrain…', 8);
const world = buildWorld(renderer, scene, 1);
const envMap = world.envMap;

boot('Generating textures…', 30);
const materials = buildMaterials(envMap, 1);

boot('Modelling anatomy…', 52);
const mosq = buildMosquito(materials);
mosq.materials = materials;
scene.add(mosq.root);

boot('Rigging legs…', 70);
const gait = new GaitController(mosq, world.field, { rate: 3.0 });
const flight = new FlightController(mosq);

// Ghost wings for the shutter-streak
const ghostMat = new THREE.MeshBasicMaterial({
  color: 0xbfc9c4, transparent: true, opacity: 0.055,
  side: THREE.DoubleSide, depthWrite: false, blending: THREE.AdditiveBlending,
});
flight.buildGhosts(mosq.thorax, ghostMat);

const feeding = new FeedingController(mosq, scene, {
  timeScale: 1.0,
  onStageChange: (s) => { $('#feedNow').textContent = s; syncFeedButtons(s); },
  // The blood meal is a finite event: when it completes, hand the route back
  // so the autopilot can take off again instead of standing on the tank forever.
  onFinished: () => {
    if (brain.mode === 'feed') brain.mode = 'auto';
    if (brain.tasks[brain.task]?.kind === 'feed') advanceAfterArrival();
  },
});

boot('Preparing optics…', 86);
buildComposer();

/* -------------------------------------------------- contact shadow ---- */
// A tight second shadow camera that follows the insect. A 2 k map over a 3 m
// frustum cannot resolve a 5 mm body, so we light it separately.
const contactLight = new THREE.DirectionalLight(0xffd9b0, 1.5);
contactLight.castShadow = true;
contactLight.shadow.mapSize.set(1024, 1024);
contactLight.shadow.camera.left = -9; contactLight.shadow.camera.right = 9;
contactLight.shadow.camera.top = 9; contactLight.shadow.camera.bottom = -9;
contactLight.shadow.camera.near = 1; contactLight.shadow.camera.far = 60;
contactLight.shadow.bias = -0.0004;
contactLight.shadow.normalBias = 0.06;
scene.add(contactLight);
scene.add(contactLight.target);

/* --------------------------------------------------------- trail ------- */
const TRAIL_MAX = 260;
const trailPos = new Float32Array(TRAIL_MAX * 3);
const trailGeo = new THREE.BufferGeometry();
trailGeo.setAttribute('position', new THREE.BufferAttribute(trailPos, 3));
trailGeo.setDrawRange(0, 0);
const trail = new THREE.Line(trailGeo, new THREE.LineBasicMaterial({
  color: 0xffb35c, transparent: true, opacity: 0.35, depthWrite: false,
}));
trail.frustumCulled = false;
scene.add(trail);
let trailCount = 0;

/* ================================================================ */
/*  BEHAVIOUR BRAIN                                                   */
/* ================================================================ */

const brain = {
  mode: 'walk',          // walk | fly | feed | auto
  speed: 14,             // mm/s commanded
  task: 0,
  timer: 0,
  flying: false,
  pos: new THREE.Vector3(620, 0, 620),
  vel: new THREE.Vector3(),
  heading: new THREE.Vector3(1, 0, 0),
  target: new THREE.Vector3(),
  targetNormal: new THREE.Vector3(0, 1, 0),
  hasTarget: false,
  navTarget: new THREE.Vector3(),
  navSurface: 'ground',
  descent: 0,
  wobbleT: 0,
  climbing: false,
  manualPhase: null,
  feedingTarget: null,
  tasks: [
    { kind: 'land', at: [640, 0, 900], surf: 'ground', then: 'walkTank' },
    { kind: 'walkTo', at: [WORLD.tank.x + WORLD.tank.r + 55, 0, WORLD.tank.z + 60], then: 'climbVent' },
    { kind: 'climbTo', vent: true, then: 'feed' },
    { kind: 'feed', vent: true, then: 'flyWall' },
    { kind: 'takeoff', then: 'flyWall' },
    { kind: 'land', at: [WORLD.wall.min[0] + 120, 0, WORLD.wall.min[2] - 80], surf: 'ground', then: 'wall' },
    { kind: 'walkTo', at: [WORLD.wall.min[0] + 60, 0, WORLD.wall.min[2] - 26], then: 'climbWall' },
    { kind: 'climbTo', wall: true, then: 'flyGlass' },
    { kind: 'takeoff', then: 'flyGlass' },
    { kind: 'land', at: [WORLD.glass.max[0] + 90, 0, (WORLD.glass.min[2] + WORLD.glass.max[2]) / 2], surf: 'ground', then: 'glass' },
    { kind: 'walkTo', at: [WORLD.glass.max[0] + 50, 0, (WORLD.glass.min[2] + WORLD.glass.max[2]) / 2], then: 'climbGlass' },
    { kind: 'climbTo', glass: true, then: 'loop' },
  ],
};

const _tmp = new THREE.Vector3(), _tmp2 = new THREE.Vector3(), _q = new THREE.Quaternion();
const _m = new THREE.Matrix4();

function surfacePoint(x, z, out) {
  const p = new THREE.Vector3(x, 0, z);
  p.y = world.heightField.sample(x, z);
  return p;
}

function probeAt(v, up, dist = 3.0) {
  return world.field.probe(v.clone().addScaledVector(up, dist), up.clone().negate(), dist + 1.5);
}

const VENT_TOP = new THREE.Vector3();
{
  const outward = new THREE.Vector3(world.VENT.x - WORLD.tank.x, 0, world.VENT.z - WORLD.tank.z).normalize();
  VENT_TOP.set(world.VENT.x, world.VENT.y, world.VENT.z).addScaledVector(outward, world.ventR + 6);
}

function resolveTaskNav(task) {
  switch (task.kind) {
    case 'land': {
      const p = surfacePoint(task.at[0], task.at[1]);
      brain.navTarget.copy(p);
      brain.navSurface = 'ground';
      break;
    }
    case 'walkTo': {
      const p = surfacePoint(task.at[0], task.at[1]);
      brain.navTarget.copy(p);
      brain.navSurface = 'ground';
      break;
    }
    case 'climbTo': {
      if (task.vent) brain.navTarget.copy(VENT_TOP);
      else if (task.wall) brain.navTarget.set(WORLD.wall.min[0] + 30, 760, WORLD.wall.min[2] - 26);
      else brain.navTarget.set(WORLD.glass.max[0] - 30, 700, (WORLD.glass.min[2] + WORLD.glass.max[2]) / 2);
      brain.navSurface = task.vent ? 'tank' : task.wall ? 'wall' : 'glass';
      break;
    }
    case 'feed': {
      brain.navTarget.copy(VENT_TOP);
      brain.navSurface = 'tank';
      break;
    }
  }
  brain.hasTarget = true;
}

/**
 * Make task `i` current and resolve where it wants the insect to go.
 * Every task transition goes through here, so a route can never half-advance
 * and leave the brain pointing at the previous task's target.
 */
function gotoTask(i) {
  const n = brain.tasks.length;
  brain.task = ((i % n) + n) % n;
  const t = brain.tasks[brain.task];
  brain.timer = 0;
  brain.hasTarget = true;
  resolveTaskNav(t);
  $('#taskNow').textContent = t.kind;
  return t;
}

/**
 * Called when the insect has physically reached the current task's target.
 * A `feed` task starts the blood meal; the meal reports back through
 * `feeding.onFinished`, so a route that ends on a tank does not stall there
 * forever. A `takeoff` task leaves the ground at once.
 */
function advanceAfterArrival() {
  const t = gotoTask(brain.task + 1);
  if (t.kind === 'feed') { brain.mode = 'feed'; feeding.start(); }
  else if (t.kind === 'takeoff') takeoff();
}

function advanceTask() { advanceAfterArrival(); }

function beginTask() {
  const t = gotoTask(brain.task);
  if (t.kind === 'takeoff' && gait.attached) takeoff();
}

/* --------------------------------------------------------- locomotion -- */

function takeoff() {
  if (!gait.attached) return;
  const up = gait.upSmooth.clone();
  const fwd = gait.heading.clone();
  gait.attached = false;
  brain.flying = true;
  brain.pos.copy(gait.bodyPos).addScaledVector(up, 1.2);
  brain.vel.copy(fwd).multiplyScalar(160).addScaledVector(up, 130);
  brain.descend = 0;
}

function tryLand() {
  const t = brain.tasks[brain.task];
  const goal = brain.navTarget;
  const up = brain.targetNormal.lengthSq() > 0.1 ? brain.targetNormal : new THREE.Vector3(0, 1, 0);
  const d = brain.pos.distanceTo(goal);
  if (d < 1.1 && brain.vel.length() < 90) {
    const heading = _tmp.copy(brain.vel).setY(0);
    if (heading.lengthSq() < 1e-4) heading.set(1, 0, 0);
    heading.normalize();
    // Set the surface normal from an actual probe at the touchdown point
    const probe = world.field.probe(goal.clone().addScaledVector(up, 2.0), up.clone().negate(), 3.0);
    const n = probe ? probe.normal : up;
    const p = probe ? probe.point : goal;
    const headingOnSurface = heading.clone().projectOnPlane(n).normalize();
    gait.attach(p.clone().addScaledVector(n, GAIT.bodyClearance + 0.25), n, headingOnSurface);
    brain.flying = false;
    brain.pos.copy(gait.bodyPos);
    brain.vel.set(0, 0, 0);
    if (t.kind === 'land') advanceAfterArrival();
    return true;
  }
  return false;
}

function updateFly(dt) {
  const t = brain.tasks[brain.task];
  brain.timer += dt;

  // Where is the surface we are aiming at?
  if (t.kind === 'land' || t.kind === 'walkTo' || t.kind === 'climbTo' || t.kind === 'feed') {
    const probe = world.field.probe(brain.navTarget.clone().addScaledVector(new THREE.Vector3(0, 1, 0), 1.2), new THREE.Vector3(0, -1, 0), 4.0);
    if (probe) { brain.targetNormal.copy(probe.normal); }
  }

  const goal = brain.navTarget.clone();
  if (t.kind === 'land' || (t.kind === 'climbTo' && brain.descend > 0)) {
    goal.addScaledVector(brain.targetNormal, 1.4);
  }

  const to = _tmp.subVectors(goal, brain.pos);
  const dist = to.length();
  const dir = to.normalize();
  const cruise = t.kind === 'land' ? 190 : 240;
  const speed = clamp(dist * 2.4, 0, cruise);

  // Steering with a lazy figure-8 (S9: mosquito swarming flight is a figure-8)
  brain.wobbleT += dt * 2.6;
  const wob = new THREE.Vector3(
    Math.sin(brain.wobbleT) * 0.10,
    Math.sin(brain.wobbleT * 2) * 0.055,
    Math.cos(brain.wobbleT) * 0.10);
  const desired = dir.clone().add(wob).normalize().multiplyScalar(speed);

  // Arrival damping near a landing target
  if (t.kind === 'land' && dist < 6) {
    desired.addScaledVector(brain.targetNormal, -Math.max(0, (dist - 1.0)) * 55);
  }

  brain.vel.lerp(desired, Math.min(1, dt * (t.kind === 'land' ? 4.2 : 2.4)));
  brain.pos.addScaledVector(brain.vel, dt);

  // Body frame: forward = velocity, plus the hover nose-up attitude
  const v = brain.vel.clone();
  const spd = v.length();
  if (spd > 1e-3) {
    brain.heading.lerp(v.clone().normalize(), Math.min(1, dt * 5)).normalize();
  }
  const up = _tmp2.set(0, 1, 0).addScaledVector(
    _tmp.set(-brain.vel.z, 0, brain.vel.x).normalize(), -clamp(brain.vel.x * 0.0004 + brain.vel.z * 0.0009, -0.5, 0.5)
  ).normalize();
  const fwd = brain.heading.clone();
  const right = new THREE.Vector3().crossVectors(up, fwd).normalize();
  _m.makeBasis(fwd, up, right.clone().negate());
  _q.setFromRotationMatrix(_m);
  const pitch = new THREE.Quaternion().setFromAxisAngle(right, lerp(FLIGHT.bodyPitchHover, -0.08, clamp(spd / 300, 0, 1)));
  const target = pitch.multiply(_q);

  // Fly → set the thorax
  const th = mosq.thorax;
  th.position.lerp(brain.pos, Math.min(1, dt * 8));
  th.quaternion.slerp(target, Math.min(1, dt * 6));
  th.updateWorldMatrix(true, true);

  flight.update(dt, {
    onGround: false,
    speedNorm: clamp(spd / FLIGHT.maxSpeed, 0, 1),
    climbing: clamp(brain.vel.y / 200, -0.4, 0.6),
  });

  // Stow the legs against the body in flight
  for (const key in mosq.legs) {
    const l = gait.legs[key];
    l.solver.solve(
      _tmp.set(
        Math.cos(l.spec.restPitch) * 0.9 - 0.55,
        -0.30 - l.spec.index * 0.05,
        l.spec.side * (0.30 + l.spec.index * 0.10),
      ).applyQuaternion(th.quaternion).add(th.position),
      _tmp2.set(0, 1, 0).applyQuaternion(th.quaternion),
      th, { tarsalAngle: 0.85, contact: 0 });
    l.planted = false;
  }

  tryLand();
}

function updateWalk(dt) {
  const t = brain.tasks[brain.task];
  let speed = brain.speed;
  let heading = gait.heading.clone();

  if (brain.hasTarget && brain.mode !== 'free') {
    const to = _tmp.subVectors(brain.navTarget, gait.bodyPos);
    const planar = to.clone().projectOnPlane(gait.upSmooth);
    const dist = planar.length();
    if (dist > 1e-3) {
      const want = planar.normalize();
      // Steer smoothly; the insect cannot turn on a dime
      const rate = clamp(dt * (1.2 + brain.speed * 0.05), 0, 1);
      heading.lerp(want, rate).normalize();
    }
    // Arrive
    const arrive = smoothstep(0.2, 1.6, dist);
    speed *= arrive;
    const full = to.length();
    // Arrived: hand the route on. `feed` and `takeoff` are handled inside.
    if (full < 1.1) advanceAfterArrival();
  }

  gait.update(dt, { speed, accel: 30 });

  // Contact-shadow light rides the insect
  contactLight.position.copy(gait.bodyPos).addScaledVector(gait.upSmooth, 26)
    .addScaledVector(gait.heading, -8);
  contactLight.target.position.copy(gait.bodyPos);
  contactLight.target.updateMatrixWorld();

  brain.pos.copy(gait.bodyPos);
  brain.heading.copy(gait.heading);

  flight.update(dt, {
    onGround: true,
    speedNorm: clamp(Math.abs(gait.speed) / 60, 0, 1),
    climbing: 0,
  });
}

/* ==================================================================== */
/*  Camera director                                                     */
/* ==================================================================== */

const cam = {
  mode: 'follow',
  pos: new THREE.Vector3(),
  look: new THREE.Vector3(),
  init: false,
  shake: 0,
};

const CAM_PRESETS = {
  follow: { dist: 15, height: 6.5, lead: 2.2, fov: 30 },
  macro: { dist: 4.0, height: 1.5, lead: 1.1, fov: 24 },
  wide: { dist: 190, height: 90, lead: 10, fov: 34 },
};

function updateCamera(dt) {
  const th = mosq.thorax;
  const focus = _tmp.copy(th.position);
  if (cam.mode === 'free') {
    controls.target.lerp(focus, Math.min(1, dt * 3));
    controls.update();
    return;
  }
  const P = CAM_PRESETS[cam.mode] || CAM_PRESETS.follow;
  // Frame slightly ahead of travel
  const dir = brain.flying ? brain.heading : gait.heading;
  const target = _tmp2.copy(focus).addScaledVector(dir, P.lead);

  // Orbit slowly so the silhouette reads in 3D
  cam.timer = (cam.timer || 0) + dt * 0.10;
  const ang = Math.sin(cam.timer) * 0.55 + 0.5;
  const off = new THREE.Vector3(
    Math.cos(ang) * P.dist,
    P.height + Math.sin(cam.timer * 1.7) * 0.6,
    Math.sin(ang) * P.dist);
  const desired = target.clone().add(off);

  if (!cam.init) { cam.pos.copy(desired); cam.look.copy(target); cam.init = true; }
  const k = 1 - Math.pow(0.0016, dt);
  cam.pos.lerp(desired, k);
  cam.look.lerp(target, Math.min(1, dt * 4.5));
  camera.position.copy(cam.pos);
  camera.lookAt(cam.look);
  const fov = P.fov;
  if (Math.abs(camera.fov - fov) > 0.01) { camera.fov = fov; camera.updateProjectionMatrix(); }
}

/* ==================================================================== */
/*  HUD                                                                 */
/* ==================================================================== */

const hud = $('#hud');
const gaitCanvas = $('#gaitCanvas');
const gctx = gaitCanvas.getContext('2d');
let hudTick = 0;

function drawGaitDiagram() {
  const W = gaitCanvas.width, H = gaitCanvas.height;
  gctx.clearRect(0, 0, W, H);
  gctx.fillStyle = 'rgba(255,255,255,0.03)';
  gctx.fillRect(0, 0, W, H);
  const keys = ['R3', 'L3', 'R2', 'L2', 'R1', 'L1'];
  const rowH = H / keys.length;
  const duty = gait.duty;
  const off = {};
  for (const k of keys) off[k] = gait.legs[k].offset;
  for (let i = 0; i < keys.length; i++) {
    const k = keys[i];
    const y = i * rowH + rowH * 0.5;
    gctx.fillStyle = '#8d97a0';
    gctx.font = '9px ui-monospace, monospace';
    gctx.fillText(k, 2, y + 3);
    for (let c = 0; c < 2; c++) {
      for (let px = 0; px < W - 26; px++) {
        const ph = ((px / (W - 26)) + c - off[k] + 10) % 1;
        const stance = ph < duty;
        gctx.fillStyle = stance ? '#63d6c8' : 'rgba(255,179,92,0.55)';
        gctx.fillRect(26 + px, y - rowH * 0.28, 1, rowH * 0.56);
      }
    }
  }
  // Playhead
  gctx.fillStyle = 'rgba(255,255,255,0.75)';
  gctx.fillRect(26, 0, 1, H);
}

function updateHUD() {
  const mode = brain.flying ? 'FLIGHT' : (feeding.stage !== 'IDLE' ? 'FEEDING' : 'WALKING');
  const slip = Math.max(...Object.values(gait.legs).map((l) => l.slip));
  const stance = gait.stanceCount();
  const contact = feeding.attached ? 'ATTACHED' : (gait.attached ? `${stance}/6 DOWN` : 'AIRBORNE');
  const surf = brain.navSurface || '—';

  hud.innerHTML = `
    <div><span class="k">state</span><span class="v hi">${mode}</span> · <span class="v">${contact}</span></div>
    <div><span class="k">task</span><span class="v">${$('#taskNow')?.textContent || '—'}</span> · ${surf}</div>
    <div><span class="k">body up</span><span class="v">${gait.upSmooth.y.toFixed(3)}</span>
         <span class="dim">tilt ${(Math.acos(clamp(gait.upSmooth.y, -1, 1)) * 57.3).toFixed(0)}°</span></div>
    <hr>
    <div><span class="k">gait</span><span class="v">${gait.tripodT > 0.72 ? 'alternating tripod' : gait.tripodT > 0.25 ? 'gliding / tetrapod' : 'metachronal wave'}</span></div>
    <div><span class="k">duty factor</span><span class="v g">${gait.duty.toFixed(3)}</span>
         <span class="dim">stride ${gait.strideFreq.toFixed(1)} Hz</span></div>
    <div><span class="k">step length</span><span class="v">${gait.stepLength.toFixed(2)} mm</span>
         <span class="dim">speed ${gait.speed.toFixed(1)} mm/s</span></div>
    <div><span class="k">foot slip</span><span class="v ${slip < 1e-4 ? 'g' : 'warnc'}">${(slip * 1000).toFixed(3)} µm</span></div>
    <div><span class="k">femur–tibia</span><span class="v">${(gait.legs.L1.solver.flexion * 57.3).toFixed(0)}° / ${(gait.legs.R3.solver.flexion * 57.3).toFixed(0)}°</span></div>
    <hr>
    <div><span class="k">wingbeat</span><span class="v hi">${flight.frequency.toFixed(0)} Hz</span>
         <span class="dim">stroke ±${(flight.amplitude * 57.3).toFixed(0)}°</span></div>
    <div><span class="k">adv. ratio</span><span class="v">${flight.advanceRatio.toFixed(2)}</span></div>
    <div><span class="k">feeding</span><span class="v" id="feedNow">${feeding.stage}</span>
         <span class="dim">sheath ${(feeding.retraction * 100).toFixed(0)}%</span></div>
    <div><span class="k">engorgement</span><span class="v">${(feeding.engorge * 100).toFixed(0)}%</span>
         <span class="dim">penetration ${(feeding.insertion * 1000).toFixed(0)} µm</span></div>
    <hr>
    <div><span class="k">tris</span><span class="v">${renderer.info.render.triangles.toLocaleString()}</span>
         <span class="dim">${renderer.info.render.calls} draws · ${(1000 / fpsNow).toFixed(1)} ms</span></div>
  `;
}

/* ==================================================================== */
/*  UI wiring                                                           */
/* ==================================================================== */

$('#taskNow') || hud.insertAdjacentHTML('beforeend', '<span id="taskNow" hidden></span>');
document.body.insertAdjacentHTML('beforeend', '<span id="taskNow" hidden></span>');
document.body.insertAdjacentHTML('beforeend', '<span id="feedNow" hidden></span>');

const feedBtns = {};
{
  const wrap = $('#feedStages');
  for (const s of ['APPROACH', 'SHEATH', 'PROBE', 'SALIVATE', 'ENGORGE', 'WITHDRAW']) {
    const b = document.createElement('button');
    b.textContent = s.slice(0, 4);
    b.title = s;
    b.onclick = () => { feeding._set(s); if (!gait.attached) landOnVent(); };
    wrap.appendChild(b);
    feedBtns[s] = b;
  }
}
function syncFeedButtons(s) {
  for (const k in feedBtns) feedBtns[k].classList.toggle('on', k === s);
}

function landOnVent() {
  // put the insect on the tank at the vent
  const outward = new THREE.Vector3(world.VENT.x - WORLD.tank.x, 0, world.VENT.z - WORLD.tank.z).normalize();
  const p = VENT_TOP.clone().addScaledVector(outward, 1.2);
  p.y = world.heightField.sample(p.x, p.z);
  const probe = world.field.probe(p.clone().addScaledVector(outward, 1.5), outward.clone().negate(), 3);
  const n = probe ? probe.normal : outward.clone();
  const at = probe ? probe.point : p;
  const h = new THREE.Vector3(0, 1, 0).cross(n).normalize();
  gait.attach(at.clone().addScaledVector(n, GAIT.bodyClearance + 0.3), n, h);
  brain.flying = false;
  brain.mode = 'feed';
  brain.navSurface = 'tank';
  // No walking target: the vent is where we already are, and the nav point
  // floats off the hull, so leaving a target set would have the insect
  // wander the tank forever. The meal drives what happens next and hands the
  // route back through `feeding.onFinished`.
  brain.hasTarget = false;
  brain.navTarget.copy(VENT_TOP);
  feeding.start();
}

function setMode(m) {
  if (m === 'walk' || m === 'climb' || m === 'wall' || m === 'glass') {
    if (brain.flying) return;
    brain.mode = m;
    brain.hasTarget = false;
    if (m === 'walk') { brain.navTarget.copy(gait.bodyPos).addScaledVector(gait.heading, 400); brain.navSurface = 'ground'; }
    if (m === 'climb') { brain.navTarget.copy(VENT_TOP); brain.navSurface = 'tank'; brain.hasTarget = true; }
    if (m === 'wall') { brain.navTarget.set(WORLD.wall.min[0] + 30, 900, WORLD.wall.min[2] - 26); brain.navSurface = 'wall'; brain.hasTarget = true; }
    if (m === 'glass') { brain.navTarget.set(WORLD.glass.max[0] - 30, 760, (WORLD.glass.min[2] + WORLD.glass.max[2]) / 2); brain.navSurface = 'glass'; brain.hasTarget = true; }
  } else if (m === 'fly') {
    takeoff();
    brain.mode = 'fly';
  } else if (m === 'feed') {
    landOnVent();
  }
}

function bind(id, fn) { const el = $(id); if (el) el.onclick = fn; return el; }
function toggle(id, initial, fn) {
  const el = $(id);
  let on = initial;
  const apply = () => { el.classList.toggle('on', on); fn(on); };
  el.onclick = () => { on = !on; apply(); };
  apply();
  return { set: (v) => { on = v; apply(); }, get: () => on };
}

bind('#bWalk', () => setMode('walk'));
bind('#bFly', () => setMode('fly'));
bind('#bFeed', () => setMode('feed'));
bind('#bClimb', () => setMode('climb'));
bind('#bWall', () => setMode('wall'));
bind('#bGlass', () => setMode('glass'));

for (const [id, k] of [['#cFollow', 'follow'], ['#cMacro', 'macro'], ['#cWide', 'wide'], ['#cFree', 'free']]) {
  bind(id, () => {
    cam.mode = k; cam.init = false;
    for (const [i, kk] of [['#cFollow', 'follow'], ['#cMacro', 'macro'], ['#cWide', 'wide'], ['#cFree', 'free']]) $(i).classList.toggle('on', kk === k);
  });
}

$('#sSpeed').oninput = (e) => { brain.speed = +e.target.value * 0.42; $('#vSpeed').textContent = (brain.speed).toFixed(0) + ' mm/s'; };
$('#sRate').oninput = (e) => { gait.rate = +e.target.value; $('#vRate').textContent = gait.rate.toFixed(1) + '×'; };
$('#sTime').oninput = (e) => { timeScale = Math.pow(10, -4 + (+e.target.value / 100) * 4); $('#vTime').textContent = timeScale.toFixed(3) + '×'; };
$('#sShutter').oninput = (e) => { flight.shutter = Math.pow(10, -4 + (+e.target.value / 100) * 4.7); $('#vShutter').textContent = '1/' + Math.round(1 / flight.shutter) + ' s'; };
$('#sGhosts').oninput = (e) => { flight.setGhosts(+e.target.value); $('#vGhosts').textContent = e.target.value; ghostMat.opacity = 0.16 / Math.max(1, +e.target.value); };
$('#sPhase').oninput = (e) => { brain.manualPhase = +e.target.value / 100; $('#vPhase').textContent = brain.manualPhase.toFixed(2); };
$('#sEng').oninput = (e) => { feeding.engorge = +e.target.value / 100; materials.abdomenUniforms.uEngorge.value = feeding.engorge; materials.abdomenUniforms.uBlood.value = feeding.engorge; $('#vEng').textContent = e.target.value + '%'; };

toggle('#rIri', true, (on) => {
  for (const k of ['cuticle', 'scutum', 'legFore', 'legMid', 'legHind', 'abdomen', 'eye', 'labium', 'pretarsus', 'vein', 'paleScale']) {
    const m = materials[k]; if (!m) continue;
    if (m._iri === undefined) m._iri = m.iridescence;
    m.iridescence = on ? m._iri : 0;
    m.needsUpdate = true;
  }
});
toggle('#rSetae', true, (on) => { materials.seta.visible = on; materials.seta.needsUpdate = true; });
toggle('#rBloom', true, (on) => { if (bloomPass) bloomPass.enabled = on; });
toggle('#rXray', false, (on) => { materials.cuticle.transparent = on; materials.cuticle.opacity = on ? 0.28 : 1; materials.cuticle.depthWrite = !on; materials.cuticle.needsUpdate = true; });
toggle('#rXray2', false, (on) => { materials.cuticle.transparent = on; materials.cuticle.opacity = on ? 0.22 : 1; materials.cuticle.depthWrite = !on; materials.cuticle.needsUpdate = true; materials.scutum.transparent = on; materials.scutum.opacity = on ? 0.3 : 1; materials.scutum.depthWrite = !on; materials.scutum.needsUpdate = true; });
toggle('#rClear', false, (on) => { materials.wing.opacity = on ? 0.30 : 0.92; materials.wing.needsUpdate = true; });
toggle('#rViz', false, (on) => { legViz.visible = on; });

/* ------------------------------------------------------------ keys --- */
addEventListener('keydown', (e) => {
  const k = e.key.toLowerCase();
  if (k === '1') $('#cFollow').click();
  if (k === '2') $('#cMacro').click();
  if (k === '3') $('#cWide').click();
  if (k === '4') $('#cFree').click();
  if (k === ' ') { e.preventDefault(); if (!gait.attached) landOnVent(); else feeding.start(); }
  if (k === 'w') $('#bWalk').click();
  if (k === 'f') $('#bFly').click();
  if (k === 'h') document.querySelectorAll('.panel').forEach((p) => p.style.display = p.style.display === 'none' ? '' : 'none');
  if (k === 'g') $('#rViz').click();
});

/* ------------------------------------------------------------ rig viz */
const legViz = new THREE.Group();
legViz.visible = false;
{
  const mat = new THREE.LineBasicMaterial({ color: 0x63d6c8, depthTest: false, transparent: true, opacity: 0.9 });
  for (const key in mosq.legs) {
    const L = mosq.legs[key];
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(5 * 3), 3));
    const line = new THREE.Line(g, mat);
    line.renderOrder = 99;
    legViz.add(line);
  }
  const dotGeo = new THREE.SphereGeometry(0.035, 8, 6);
  const dotMat = new THREE.MeshBasicMaterial({ color: 0xffb35c, depthTest: false });
  for (const key in mosq.legs) {
    const d = new THREE.Mesh(dotGeo, dotMat);
    d.renderOrder = 99;
    legViz.add(d);
  }
}
scene.add(legViz);

function updateLegViz() {
  if (!legViz.visible) return;
  const legKeys = Object.keys(mosq.legs);
  legKeys.forEach((key, i) => {
    const L = mosq.legs[key];
    const p = [L.root, L.femur, L.tibia, L.tarsus[4], L.pretarsus];
    const arr = legViz.children[i].geometry.attributes.position.array;
    p.forEach((n, k) => { const w = n.getWorldPosition(_tmp); arr[k * 3] = w.x; arr[k * 3 + 1] = w.y; arr[k * 3 + 2] = w.z; });
    legViz.children[i].geometry.attributes.position.needsUpdate = true;
    const d = legViz.children[legKeys.length + i];
    d.position.copy(gait.legs[key].planted ? gait.legs[key].plant : gait.legs[key].target);
  });
}

/* ==================================================================== */
/*  GLB export                                                          */
/* ==================================================================== */

$('#bGLB').onclick = async () => {
  const btn = $('#bGLB');
  btn.textContent = 'Baking clips…'; btn.disabled = true;
  try {
    const clips = new ClipBaker({ mosq, gait, flight, feeding }).bakeAll();
    const exporter = new GLTFExporter();
    const result = await exporter.parseAsync(mosq.root, { binary: true, animations: clips, onlyVisible: false });
    const blob = new Blob([result], { type: 'model/gltf-binary' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'anura_mosquito_female.glb';
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 4000);
    btn.textContent = `Saved · ${clips.length} clips`;
  } catch (err) {
    console.error(err);
    btn.textContent = 'Export failed — see console';
  }
  setTimeout(() => { btn.textContent = 'Download GLB (rigged + clips)'; btn.disabled = false; }, 2600);
};

/* ==================================================================== */
/*  Main loop                                                           */
/* ==================================================================== */

let timeScale = 1;
let last = performance.now() / 1000;
let fpsNow = 16.7;
let elapsed = 0;
let started = false;

function frame() {
  requestAnimationFrame(frame);
  const now = performance.now() / 1000;
  let dt = Math.min(0.05, now - last);
  last = now;
  fpsNow = fpsNow * 0.9 + (dt * 1000) * 0.1;
  const sdt = dt * timeScale;
  elapsed += sdt;

  // A `takeoff` task is satisfied the moment the insect is airborne; the next
  // task in the route is the destination it is flying to.
  if (brain.flying && brain.tasks[brain.task]?.kind === 'takeoff' && brain.timer > 0.35) {
    gotoTask(brain.task + 1);
  }

  if (brain.flying) updateFly(sdt);
  else if (gait.attached) updateWalk(sdt);
  else updateFly(sdt);

  // Feeding runs on the clock too
  if (feeding.stage !== 'IDLE') {
    feeding.update(sdt, elapsed);
    materials.abdomenUniforms.uEngorge.value = feeding.engorge;
    materials.abdomenUniforms.uBlood.value = feeding.blood;
    materials.abdomenUniforms.uTime.value = elapsed;
    $('#sEng').value = feeding.engorge * 100;
    $('#vEng').textContent = (feeding.engorge * 100).toFixed(0) + '%';
  } else if (feeding.engorge > 0) {
    feeding.engorge = Math.max(0, feeding.engorge - dt * 0.35);
    feeding.blood = Math.max(0, feeding.blood - dt * 0.5);
    materials.abdomenUniforms.uEngorge.value = feeding.engorge;
    materials.abdomenUniforms.uBlood.value = feeding.blood;
  }

  // Advance ratio for the HUD
  const spd = brain.flying ? brain.vel.length() : Math.abs(gait.speed);
  flight.advanceRatio = spd / (2 * Math.PI * WING.length * flight.frequency);

  updateCamera(dt);
  updateLegViz();

  // Trail
  if (brain.flying) {
    trailCount = Math.min(TRAIL_MAX, trailCount + 1);
    const p = mosq.thorax.position;
    for (let i = trailCount - 1; i > 0; i--) {
      trailPos[i * 3] = trailPos[(i - 1) * 3];
      trailPos[i * 3 + 1] = trailPos[(i - 1) * 3 + 1];
      trailPos[i * 3 + 2] = trailPos[(i - 1) * 3 + 2];
    }
    trailPos[0] = p.x; trailPos[1] = p.y; trailPos[2] = p.z;
    trailGeo.setDrawRange(0, trailCount);
    trailGeo.attributes.position.needsUpdate = true;
  } else if (trailCount > 0) {
    trailCount = Math.max(0, trailCount - 2);
    trailGeo.setDrawRange(0, trailCount);
  }

  if (bloomPass && bloomPass.enabled) composer.render();
  else renderer.render(scene, camera);

  hudTick += dt;
  if (hudTick > 0.1) { hudTick = 0; updateHUD(); drawGaitDiagram(); }
}

/* ------------------------------------------------------------- start -- */
boot('Warming shaders…', 94);
renderer.compile(scene, camera);
boot('Ready', 100);
setTimeout(() => { $('#boot').style.opacity = '0'; setTimeout(() => $('#boot').remove(), 600); }, 260);

// Place the insect on the ground, facing the tank, and start the autopilot.
{
  const p = surfacePoint(640, 900);
  const n = new THREE.Vector3(0, 1, 0);
  const heading = new THREE.Vector3(WORLD.tank.x - 640, 0, WORLD.tank.z - 900).normalize();
  mosq.body.position.set(0, 0, 0); mosq.body.quaternion.identity();
  gait.attach(p.clone().addScaledVector(n, GAIT.bodyClearance + 0.3), n, heading);
  brain.pos.copy(gait.bodyPos);
  brain.mode = 'auto';
  resolveTaskNav(brain.tasks[0]);
  $('#taskNow').textContent = brain.tasks[0].kind;
  brain.timer = 0;
  // After a beat, start walking the autopilot route.
  setTimeout(() => { if (brain.mode === 'auto') { brain.hasTarget = true; brain.speed = 16; } }, 900);
}

frame();

// expose for inspection
window.ANURA = { scene, renderer, camera, mosq, gait, flight, feeding, brain, world, materials, cam, THREE };
