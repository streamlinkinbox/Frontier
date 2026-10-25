// Browser bootstrap for the fluid demo: WebGPU device, canvas, input, UI and the frame loop.
import { App, QUALITY } from './App.js';
import { ViewportInput, FLY_CODES } from './input.js';

const $ = (id) => document.getElementById(id);
const params = new URLSearchParams(location.search);
const quality = QUALITY[params.get('q')] ? params.get('q') : 'medium';
const renderScale = Math.min(2, Math.max(0.25, +(params.get('scale') ?? 1)));
const DEG = Math.PI / 180;
// build stamp: the dev server (demo/serve.mjs) serves each build under /b/<id>/ – shown in the menu
// so it is obvious which version is running (stale caches show an old id or none at all)
const BUILD = location.pathname.match(/\/b\/([0-9a-z]+)\//)?.[1] ?? 'dev';
console.info(`Frontier Fluid demo · build ${BUILD} · Unreal camera · particle foam v2`);

// ---- persisted UI preferences (localStorage can be blocked in sandboxed iframes) ----------------
const PREFS_KEY = 'frontier.fluid.demo';
const prefs = (() => { try { return JSON.parse(localStorage.getItem(PREFS_KEY) ?? '{}') ?? {}; } catch { return {}; } })();
const savePrefs = () => { try { localStorage.setItem(PREFS_KEY, JSON.stringify(prefs)); } catch { /* not persisted */ } };

// ---- menu visibility ------------------------------------------------------------------------------
// 'open' | 'collapsed' (title bar only) | 'hidden' (slid away, a small Menu button remains).
// G = Unreal-style "game view": hides every overlay (menu, HUD, speedometer).
// URL: ?ui=hidden starts with the menu hidden, ?ui=game in game view.
let menuState = ['open', 'collapsed', 'hidden'].includes(prefs.menu) ? prefs.menu : 'open';
let lastVisible = prefs.lastVisible === 'collapsed' ? 'collapsed' : 'open';
let gameView = false;
const uiParam = params.get('ui');
if (uiParam === '0' || uiParam === 'hidden') menuState = 'hidden';
if (uiParam === 'game' || uiParam === 'none') gameView = true;

let settleTimer = 0;
function applyMenu() {
  const panel = $('panel');
  // animate this change, then pin the end state (also ends a slide the compositor never started)
  panel.classList.remove('settled');
  void panel.offsetWidth;
  clearTimeout(settleTimer);
  settleTimer = setTimeout(() => panel.classList.add('settled'), 320);
  document.body.classList.toggle('menu-hidden', menuState === 'hidden');
  document.body.classList.toggle('game-view', gameView);
  $('panel').classList.toggle('collapsed', menuState === 'collapsed');
  $('collapse').textContent = menuState === 'collapsed' ? '+' : '–';
  $('collapse').title = menuState === 'collapsed' ? 'Expand the menu' : 'Minimise to the title bar';
}
function setMenu(state) {
  menuState = state;
  if (state !== 'hidden') lastVisible = state;
  prefs.menu = state; prefs.lastVisible = lastVisible;
  savePrefs();
  applyMenu();
}
const toggleMenu = () => setMenu(menuState === 'hidden' ? lastVisible : 'hidden');
function setGameView(on) {
  gameView = on;
  applyMenu();
  if (on) toast('Game view: all UI hidden. Press G to bring it back.', 2200);
}
let toastTimer = 0;
function toast(msg, ms = 1500) {
  const el = $('toast');
  el.textContent = msg;
  el.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove('show'), ms);
}
const toggleSheet = (on = $('sheet').hidden) => { $('sheet').hidden = !on; };

applyMenu();
$('collapse').addEventListener('click', () => setMenu(menuState === 'collapsed' ? 'open' : 'collapsed'));
$('hideMenu').addEventListener('click', () => setMenu('hidden'));
$('menuBtn').addEventListener('click', () => setMenu(lastVisible));
$('bHide').addEventListener('click', () => setMenu('hidden'));
$('bGame').addEventListener('click', () => setGameView(true));
$('bHelp').addEventListener('click', () => toggleSheet(true));
$('sheetClose').addEventListener('click', () => toggleSheet(false));
$('sheet').addEventListener('click', (e) => { if (e.target === $('sheet')) toggleSheet(false); });

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
  $('sub').textContent = `Shallow-water sim · ${app.sim.nx}×${app.sim.ny} cells · ${app.sim.dx.toFixed(2)} m · ${(app.sim.size[0]).toFixed(0)} m domain · build ${BUILD}`;

  // ---- input -----------------------------------------------------------------------------
  const input = new ViewportInput(canvas, app, { onToast: (m) => toast(m), onChange: () => syncCameraUI() });
  window.viewportInput = input;
  const isTyping = () => {
    const a = document.activeElement;
    if (!a) return false;
    if (a.tagName === 'TEXTAREA' || a.tagName === 'SELECT' || a.isContentEditable) return true;
    return a.tagName === 'INPUT' && !['range', 'checkbox', 'radio', 'button'].includes(a.type);
  };
  const NO_DEFAULT = new Set(['Space', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'PageUp', 'PageDown', 'Slash', 'Quote', 'F8']);
  const SCENARIOS = { Digit1: 'beach', Digit2: 'ford', Digit3: 'mud', Digit4: 'rapids', Digit5: 'puddles' };
  const CAM_NAMES = { chase: 'Chase camera', low: 'Low camera', orbit: 'Orbit camera', free: 'Free camera (Unreal controls)' };

  window.addEventListener('keydown', (e) => {
    if (isTyping()) return;
    input.alt = e.altKey;
    if (e.ctrlKey || e.metaKey || e.code === 'F5' || e.code === 'F11' || e.code === 'F12') return; // browser shortcuts
    if (e.key === 'Alt') { e.preventDefault(); return; } // keep Alt (orbit modifier) from focusing the browser menu
    input.keyDown(e);
    if (NO_DEFAULT.has(e.code)) e.preventDefault();
    if (input.flying && FLY_CODES.has(e.code)) { e.preventDefault(); return; } // RMB held: the camera owns these keys
    if (e.repeat) return;
    if (SCENARIOS[e.code]) { app.teleport(SCENARIOS[e.code]); return; }
    switch (e.code) {
      case 'KeyR': app.vehicle.reset(app.vehicle.body.position, headingOf(app)); break;
      case 'KeyC': app.cycleCamera(); toast(CAM_NAMES[app.cam.mode]); syncCameraUI(); break;
      case 'F8': app.toggleFreeCamera(); toast(CAM_NAMES[app.cam.mode]); syncCameraUI(); break;
      case 'KeyF': app.focusSelection(); syncCameraUI(); break;
      case 'KeyX': app.dropCrates(); break;
      case 'KeyB': app.cannonball(); break;
      case 'KeyT': toggle('tRain'); break;
      case 'KeyP': app.paused = !app.paused; toast(app.paused ? 'Paused (P)' : 'Running'); break;
      case 'KeyV': setView((app.fluidR.settings.debugMode + 1) % 5); break;
      case 'KeyH': toggleMenu(); break;
      case 'KeyG': setGameView(!gameView); break;
      case 'Delete': case 'Backspace': if (app.deleteSelection()) syncCameraUI(); break; // Backspace: Mac laptops
      case 'Escape': app.select(null); toggleSheet(false); syncCameraUI(); break;
      default:
        if (e.key === '?') toggleSheet();
    }
  });
  window.addEventListener('keyup', (e) => {
    input.keyUp(e);
    if (e.key === 'Alt') e.preventDefault();
  });
  window.addEventListener('blur', () => input.reset());

  function readInput() {
    // while flying (RMB held) the movement keys belong to the camera
    const k = (...c) => !input.flying && c.some((x) => input.isDown(x));
    let throttle = k('KeyW', 'ArrowUp') ? 1 : 0;
    let brake = k('KeyS', 'ArrowDown') ? 1 : 0;
    // car local +X is its left side
    let steer = (k('KeyA', 'ArrowLeft') ? 1 : 0) - (k('KeyD', 'ArrowRight') ? 1 : 0);
    let handbrake = input.isDown('Space') ? 1 : 0;
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (const p of pads) {
      if (!p) continue;
      const ax = Math.abs(p.axes[0]) > 0.12 ? p.axes[0] : 0;
      steer = Math.max(-1, Math.min(1, steer - ax));
      throttle = Math.max(throttle, p.buttons[7]?.value ?? 0);
      brake = Math.max(brake, p.buttons[6]?.value ?? 0);
      if (p.buttons[0]?.pressed) handbrake = 1;
    }
    return { throttle, brake, steer, handbrake, camera: input.cameraInput() };
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
  for (const b of $('scenarios').children) b.addEventListener('click', () => app.teleport(b.dataset.s));
  for (const b of $('quality').children) {
    b.classList.toggle('on', b.dataset.q === quality);
    b.addEventListener('click', () => { params.set('q', b.dataset.q); location.search = params.toString(); });
  }
  $('aCrates').addEventListener('click', () => app.dropCrates());
  $('aBall').addEventListener('click', () => app.cannonball());
  $('aReset').addEventListener('click', () => app.resetWater());

  // camera
  for (const b of $('camModes').children) b.addEventListener('click', () => { app.setCameraMode(b.dataset.c); syncCameraUI(); });
  $('camSpeed').value = prefs.camSpeed ?? app.editorCam.speedSetting;
  bindRange('camSpeed', (v) => `${v} · ${app.editorCam.speed} m/s`, (v) => {
    app.editorCam.speedSetting = v;
    $('camSpeedO').textContent = `${v} · ${app.editorCam.speed} m/s`;
    prefs.camSpeed = v; savePrefs();
  });
  $('camFov').value = prefs.fov ?? 62;
  bindRange('camFov', (v) => `${v}°`, (v) => { app.setFov(v * DEG); prefs.fov = v; savePrefs(); });
  input.setPointerLock(!!prefs.mouseLock);
  $('tLock').addEventListener('click', () => {
    input.setPointerLock(!(input.pointerLock && !input.lockUnavailable));
    prefs.mouseLock = input.pointerLock; savePrefs();
    syncCameraUI();
  });
  $('tFocus').addEventListener('click', () => { app.focusSelection(); syncCameraUI(); });

  // give keyboard focus back to the viewport after using the panel
  $('panel').addEventListener('click', (e) => { if (e.target.closest('button')) canvas.focus({ preventScroll: true }); });
  for (const el of document.querySelectorAll('input[type=range]')) el.addEventListener('change', () => canvas.focus({ preventScroll: true }));

  function syncCameraUI() {
    const ec = app.editorCam, mode = app.cam.mode;
    for (const b of $('camModes').children) b.classList.toggle('on', b.dataset.c === mode);
    if (+$('camSpeed').value !== ec.speedSetting) $('camSpeed').value = ec.speedSetting;
    $('camSpeedO').textContent = `${ec.speedSetting} · ${ec.speed} m/s`;
    $('tLock').classList.toggle('on', input.pointerLock && !input.lockUnavailable);
    $('tFocus').textContent = app.selection ? `Focus ${app.selectionName().toLowerCase()} (F)` : 'Focus car (F)';
    let info;
    if (mode === 'free') {
      const fov = Math.abs(ec.fov - ec.baseFov) > 0.5 * DEG ? ` · FOV ${Math.round(ec.fov / DEG)}°` : '';
      info = input.flying
        ? `flying · speed ${ec.speedSetting} (${ec.speed} m/s)${fov} · wheel = speed`
        : `speed ${ec.speedSetting} (${ec.speed} m/s) · RMB + WASD fly · F focus · C / F8 back to the car`;
    } else {
      info = 'hold RMB to fly (Unreal controls) · F focus · C cycle';
    }
    const sel = app.selectionName();
    if (sel) info += ` · selected: ${sel}`;
    const name = mode === 'free' ? 'Free camera' : CAM_NAMES[mode];
    if ($('hudMode').textContent !== name) $('hudMode').textContent = name;
    if ($('hudInfo').textContent !== info) $('hudInfo').textContent = info;
  }
  syncCameraUI();

  new ResizeObserver(() => {
    const [nw, nh] = size();
    if (nw !== canvas.width || nh !== canvas.height) { canvas.width = nw; canvas.height = nh; app.resize(nw, nh); }
  }).observe(canvas);

  // ---- loop ----------------------------------------------------------------------------------
  let last = performance.now(), acc = 0, frames = 0, fps = 0, statT = 0, wasFlying = false;
  function loop(now) {
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;
    app.frame(dt, readInput(), ctx.getCurrentTexture().createView());
    frames++; acc += dt; statT += dt;
    if (acc > 0.5) { fps = frames / acc; frames = 0; acc = 0; }
    if (input.flying !== wasFlying) { wasFlying = input.flying; syncCameraUI(); }
    if (statT > 0.25) {
      statT = 0;
      syncCameraUI();
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
