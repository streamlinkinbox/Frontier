import { AudioEngine } from './audio/audio';
import { SceneView } from './render/scene';
import { WorldRenderer } from './render/world3d';
import { clamp, damp } from './core/mathx';
import { World } from './sim/world';
import type { VehicleInput } from './sim/vehicle';
import { Hud } from './ui/hud';
import { SPECIES } from './sim/tuning';

const app = document.getElementById('app') as HTMLDivElement;
const overlay = document.getElementById('overlay') as HTMLDivElement;

const view = new SceneView(app);
const world = new World(20261122);
const worldRenderer = new WorldRenderer(view.scene, world.track);
const hud = new Hud();
const audio = new AudioEngine();

const keys = new Set<string>();
let running = false;
let steerInput = 0;
let cutterSweep = 0;

const input: VehicleInput = {
  throttle: 0,
  brake: 0,
  steer: 0,
  handbrake: false,
  boost: false,
};

window.addEventListener('keydown', (e) => {
  const k = e.key.toLowerCase();
  if (['arrowup', 'arrowdown', 'arrowleft', 'arrowright', ' '].includes(k)) e.preventDefault();
  if (keys.has(k)) return;
  keys.add(k);

  if (k === 'e') {
    if (world.vines.triggerCutter()) {
      audio.cutter();
      cutterSweep = 0.5;
      view.shake(0.03, 22);
    } else {
      hud.showToast('CUTTER CYCLING', 0.8);
    }
  }
  if (k === 'r') {
    world.recoverVehicle();
    hud.showToast('RECOVERED', 1);
  }
  if (k === 'm') {
    audio.setMuted(!audio.isMuted);
    hud.showToast(audio.isMuted ? 'MUTED' : 'SOUND ON', 0.9);
  }
});

window.addEventListener('keyup', (e) => keys.delete(e.key.toLowerCase()));
window.addEventListener('blur', () => keys.clear());

function beginRun() {
  if (running) return;
  running = true;
  overlay.classList.add('hidden');
  audio.start();
}

overlay.addEventListener('click', beginRun);
window.addEventListener('keydown', () => beginRun(), { once: true });

window.addEventListener('resize', () => view.resize(app));

let last = performance.now();
let elapsed = 0;

function frame(now: number) {
  requestAnimationFrame(frame);
  const rawDt = Math.min(0.05, (now - last) / 1000);
  last = now;
  elapsed += rawDt;

  if (!running) {
    view.render();
    return;
  }

  // ------------------------------------------------------------- input
  const throttleKey = keys.has('w') || keys.has('arrowup');
  const brakeKey = keys.has('s') || keys.has('arrowdown');
  const left = keys.has('a') || keys.has('arrowleft');
  const right = keys.has('d') || keys.has('arrowright');

  input.throttle = damp(input.throttle, throttleKey ? 1 : 0, 9, rawDt);
  input.brake = damp(input.brake, brakeKey ? 1 : 0, 12, rawDt);
  // The chassis turns toward its own +x when the steer input is positive, and
  // the chase camera sits behind the car, so +x is screen-left: pressing D
  // (right) has to push the input negative to turn the right way on screen.
  const steerTarget = (left ? 1 : 0) - (right ? 1 : 0);
  steerInput = damp(steerInput, steerTarget, 7, rawDt);
  if (Math.abs(steerInput) < 0.002) steerInput = 0;
  input.steer = steerInput;
  input.handbrake = keys.has(' ');
  input.boost = keys.has('shift');

  // ------------------------------------------------------------- simulate
  world.update(rawDt, input);

  // ------------------------------------------------------------- events
  for (const ev of world.drainEvents()) {
    switch (ev.type) {
      case 'latch': {
        const spec = ev.species ? SPECIES[ev.species as keyof typeof SPECIES] : null;
        audio.latch();
        worldRenderer.burstLeaves(ev.pos.clone(), 0.8);
        worldRenderer.burstSap(ev.pos.clone(), 0.8);
        const heavy = ev.species === 'anchor';
        view.shake(heavy ? 0.22 : 0.07, heavy ? 7 : 16);
        view.stop(heavy ? 0.12 : 0.06);
        if (spec) hud.showToast(spec.label.toUpperCase(), 1.4);
        break;
      }
      case 'break': {
        const spec = ev.species ? SPECIES[ev.species as keyof typeof SPECIES] : null;
        if (spec?.breakMode === 'shear') audio.woodyCrack();
        else if (spec?.breakMode === 'release') audio.wetRelease();
        else audio.snap();
        worldRenderer.burstLeaves(ev.pos.clone(), 1);
        view.shake(0.05, 20);
        break;
      }
      case 'snap': {
        audio.woodyCrack();
        view.shake(0.16, 8);
        break;
      }
      case 'impact': {
        audio.impact(ev.strength);
        view.shake(0.1 + ev.strength * 0.25, 12);
        worldRenderer.dust(ev.pos.clone().setY(ev.pos.y - 0.2), ev.strength);
        break;
      }
      case 'recover': {
        // the car was pulled back onto the trail
        hud.showToast('BACK ON TRAIL', 1.2);
        audio.impact(0.45);
        view.shake(0.14, 10);
        break;
      }
      default:
        break;
    }
  }

  const vines = world.vines.vines;

  // tension creak
  let creak = 0;
  for (const vine of vines) {
    if (vine.latch) creak = Math.max(creak, clamp(vine.tensionSmooth / vine.spec.maxTension, 0, 1));
  }
  if (creak > 0.25 && Math.random() < creak * 0.06) audio.creak(creak);

  // ------------------------------------------------------------- presentation
  const canopy = clamp(world.director.pressure * 0.75 + 0.2, 0, 1);
  view.updateCamera(world.vehicle, rawDt, canopy);
  worldRenderer.update(world, rawDt, elapsed);

  if (cutterSweep > 0) cutterSweep = Math.max(0, cutterSweep - rawDt);

  const v = world.vehicle;
  audio.update(
    v.rpm,
    input.throttle * 0.8 + input.brake * 0.4,
    v.speed,
    v.wheels[2]?.grounded ? 1 : 0.1,
    world.director.pressure,
    creak * 0.7,
  );

  hud.update(world, rawDt);
  view.render();
}

requestAnimationFrame(frame);
