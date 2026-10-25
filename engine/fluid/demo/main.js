// Browser bootstrap for the fluid demo: WebGPU device, canvas, input, UI and the frame loop.
import { App, QUALITY } from './App.js';

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
const quality = QUALITY[params.get('q')] ? params.get('q') : 'medium';
const renderScale = Math.min(2, Math.max(0.25, +(params.get('scale') ?? 1)));

function fail(msg) {
  $('msg').innerHTML = msg;
  $('bar').style.display = 'none';
}

async function main() {
  if (!navigator.gpu) {
    fail('WebGPU is not available in this browser.<br>Use Chrome / Edge 113+ (or Safari 26 / Firefox 141+ with WebGPU enabled).');
    return;
  }
  const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
  if (!adapter) { fail('No suitable GPU adapter was found.'); return; }
  const device = await adapter.requestDevice();
  device.lost.then((info) => { if (info.reason !== 'destroyed') fail('The GPU device was lost: ' + info.message + '<br>Reload the page.'); $('overlay').style.opacity = 1; $('overlay').style.display = 'grid'; });
  device.addEventListener('uncapturederror', (e) => console.error('[WebGPU]', e.error.message));

  const canvas = $('gfx');
  const ctx = canvas.getContext('webgpu');
  const format = navigator.gpu.getPreferredCanvasFormat();
  ctx.configure({ device, format, alphaMode: 'opaque' });
  const size = () => {
    const dpr = Math.min(window.devicePixelRatio || 1, 2) * renderScale;
    return [Math.max(1, Math.floor(canvas.clientWidth * dpr)), Math.max(1, Math.floor(canvas.clientHeight * dpr))];
  };
  let [w, h] = size();
  canvas.width = w; canvas.height = h;

  $('msg').textContent = `Building the ${quality} world and compiling GPU kernels…`;
  await new Promise((r) => setTimeout(r, 30));
  const app = new App(device, { quality, width: w, height: h, outputFormat: format, srgbOutput: !format.endsWith('-srgb') });
  window.app = app; // handy for tweaking from the console
  $('msg').textContent = 'Letting the river settle…';
  await new Promise((r) => setTimeout(r, 30));
  app.warmup(+(params.get('warmup') ?? 4));
  await device.queue.onSubmittedWorkDone();
  $('overlay').style.opacity = 0;
  setTimeout(() => ($('overlay').style.display = 'none'), 500);
  $('sub').textContent = `Shallow-water sim · ${app.sim.nx}×${app.sim.ny} cells · ${app.sim.dx.toFixed(2)} m · ${(app.sim.size[0]).toFixed(0)} m domain`;

  // ---- input -----------------------------------------------------------------------------
  const keys = new Set();
  const isTyping = () => document.activeElement && ['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName);
  window.addEventListener('keydown', (e) => {
    if (isTyping()) return;
    keys.add(e.code);
    if (['Space', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(e.code)) e.preventDefault();
    if (e.repeat) return;
    switch (e.code) {
      case 'KeyR': app.vehicle.reset(app.vehicle.body.position, headingOf(app)); break;
      case 'KeyC': app.cam.mode = { chase: 'low', low: 'orbit', orbit: 'chase' }[app.cam.mode]; break;
      case 'KeyF': app.dropCrates(); break;
      case 'KeyG': app.cannonball(); break;
      case 'KeyT': toggle('tRain'); break;
      case 'KeyP': app.paused = !app.paused; break;
      case 'KeyV': setView((app.fluidR.settings.debugMode + 1) % 5); break;
      case 'KeyH': $('panel').classList.toggle('collapsed'); break;
      case 'Digit1': app.teleport('beach'); break;
      case 'Digit2': app.teleport('ford'); break;
      case 'Digit3': app.teleport('mud'); break;
      case 'Digit4': app.teleport('rapids'); break;
      case 'Digit5': app.teleport('puddles'); break;
    }
  });
  window.addEventListener('keyup', (e) => keys.delete(e.code));
  window.addEventListener('blur', () => keys.clear());
  let drag = null;
  canvas.addEventListener('pointerdown', (e) => { drag = { x: e.clientX, y: e.clientY }; app.cam.dragging = true; canvas.setPointerCapture(e.pointerId); canvas.focus(); });
  canvas.addEventListener('pointerup', () => { drag = null; app.cam.dragging = false; });
  canvas.addEventListener('pointermove', (e) => {
    if (!drag) return;
    const dx = (e.clientX - drag.x) * 0.006, dy = (e.clientY - drag.y) * 0.004;
    drag = { x: e.clientX, y: e.clientY };
    if (app.cam.mode === 'orbit') { app.cam.orbitYaw -= dx; app.cam.orbitPitch += dy; }
    else { app.cam.userYaw -= dx; app.cam.userPitch = Math.max(-0.2, Math.min(1.0, app.cam.userPitch + dy)); }
  });
  canvas.addEventListener('wheel', (e) => { app.cam.dist = Math.max(4, Math.min(40, app.cam.dist * Math.exp(e.deltaY * 0.001))); e.preventDefault(); }, { passive: false });

  function readInput() {
    const k = (...c) => c.some((x) => keys.has(x));
    let throttle = k('KeyW', 'ArrowUp') ? 1 : 0;
    let brake = k('KeyS', 'ArrowDown') ? 1 : 0;
    // car local +X is its left side
    let steer = (k('KeyA', 'ArrowLeft') ? 1 : 0) - (k('KeyD', 'ArrowRight') ? 1 : 0);
    let handbrake = k('Space') ? 1 : 0;
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (const p of pads) {
      if (!p) continue;
      const ax = Math.abs(p.axes[0]) > 0.12 ? p.axes[0] : 0;
      steer = Math.max(-1, Math.min(1, steer - ax));
      throttle = Math.max(throttle, p.buttons[7]?.value ?? 0);
      brake = Math.max(brake, p.buttons[6]?.value ?? 0);
      if (p.buttons[0]?.pressed) handbrake = 1;
    }
    return { throttle, brake, steer, handbrake };
  }

  // ---- UI ----------------------------------------------------------------------------------
  const bindRange = (id, fmt, fn) => {
    const el = $(id), out = $(id + 'O');
    const upd = () => { out.textContent = fmt(+el.value); fn(+el.value); };
    el.addEventListener('input', upd);
    upd();
  };
  bindRange('swell', (v) => `${v.toFixed(2)} m`, (v) => app.setSwell(v));
  bindRange('period', (v) => `${v.toFixed(1)} s`, (v) => app.setSwell(app.swell.height, v));
  bindRange('river', (v) => `${v.toFixed(1)} m³/s`, (v) => app.setRiverFlow(v));
  bindRange('wind', (v) => `${v.toFixed(1)} m/s`, (v) => app.setWind(v));
  const toggles = {
    tCaustics: (on) => { app.fluidR.settings.caustics = on; },
    tParticles: (on) => { app.sim.particles.params.enabled = on; app.fluidR.settings.particles = on; },
    tShadows: (on) => { app.renderer.shadows = on; },
    tRain: (on) => { app.rain = on; },
  };
  function toggle(id) { const el = $(id); el.classList.toggle('on'); toggles[id](el.classList.contains('on')); }
  for (const id of Object.keys(toggles)) $(id).addEventListener('click', () => toggle(id));
  function setView(v) {
    app.fluidR.settings.debugMode = v;
    for (const b of $('views').children) b.classList.toggle('on', +b.dataset.v === v);
  }
  for (const b of $('views').children) b.addEventListener('click', () => setView(+b.dataset.v));
  for (const b of $('scenarios').children) b.addEventListener('click', () => { app.teleport(b.dataset.s); canvas.focus(); });
  for (const b of $('quality').children) {
    b.classList.toggle('on', b.dataset.q === quality);
    b.addEventListener('click', () => { params.set('q', b.dataset.q); location.search = params.toString(); });
  }
  $('aCrates').addEventListener('click', () => app.dropCrates());
  $('aBall').addEventListener('click', () => app.cannonball());
  $('aReset').addEventListener('click', () => app.resetWater());
  $('collapse').addEventListener('click', () => $('panel').classList.toggle('collapsed'));

  new ResizeObserver(() => {
    const [nw, nh] = size();
    if (nw !== canvas.width || nh !== canvas.height) { canvas.width = nw; canvas.height = nh; app.resize(nw, nh); }
  }).observe(canvas);

  // ---- loop ----------------------------------------------------------------------------------
  let last = performance.now(), acc = 0, frames = 0, fps = 0, statT = 0;
  function loop(now) {
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;
    app.frame(dt, readInput(), ctx.getCurrentTexture().createView());
    frames++; acc += dt; statT += dt;
    if (acc > 0.5) { fps = frames / acc; frames = 0; acc = 0; }
    if (statT > 0.25) {
      statT = 0;
      const st = app.sim.stats;
      $('sFps').textContent = fps.toFixed(0);
      $('sParts').textContent = st.particles != null ? (st.particles / 1000).toFixed(1) + 'k' : '–';
      $('sGrid').textContent = `${app.sim.nx}² · ${app.sim.lastSubsteps}`;
      $('sVol').textContent = st.volume ? Math.round(st.volume).toLocaleString() + ' m³' : '–';
      $('spd').textContent = Math.abs(app.vehicle.speed * 3.6).toFixed(0);
      const v = app.vehicle, mat = app.scene.materialAt(v.body.position[0], v.body.position[2]);
      const wd = Math.max(...v.wheels.map((w) => w.depth || 0));
      const mud = Math.max(...v.wheels.map((w) => (w.depth > 0.01 ? w.mud : 0) || 0));
      $('surf').textContent = v.inWater > 0.25 ? 'Floating' : mud > 0.4 ? 'Mud' : wd > 0.02 ? `Water ${(wd * 100).toFixed(0)} cm` : wd > 0.002 ? 'Wet' : mat.road > 0.5 ? 'Dirt track' : mat.sand > 0.5 ? 'Sand' : mat.river > 0.5 ? 'Gravel' : 'Grass';
    }
    requestAnimationFrame(loop);
  }
  requestAnimationFrame(loop);
}

function headingOf(app) {
  const f = app.vehicle.body.dirToWorld([0, 0, 1]);
  return Math.atan2(f[0], f[2]);
}

main().catch((e) => { console.error(e); fail('Failed to start: ' + (e?.message ?? e)); });
