import { mat4 } from './math.js';
import { Car } from './car.js';
import { Smoke, SMOKE_DIMS, SMOKE_H, MAX_CRATES } from './smoke.js';
import { Sand, SAND_MAX, SAND_GRID } from './sand.js';
import { Scene } from './scene.js';

const $ = (id) => document.getElementById(id);
const errors = [];
function fatal(msg) {
  console.error(msg);
  errors.push(String(msg));
  $('err').style.display = 'block';
  $('err').textContent = errors.slice(-6).join('\n\n');
  window.__simErrors = errors;
}
window.__simErrors = errors;

async function init() {
  if (!navigator.gpu) { fatal('WebGPU is not available in this browser.\nUse Chrome/Edge 113+ (or enable WebGPU), on a secure context (https or localhost).'); return; }
  const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
  if (!adapter) { fatal('No WebGPU adapter found (GPU blocklisted or drivers too old).'); return; }
  const device = await adapter.requestDevice({ label: 'frontier-sim' });
  device.lost.then((info) => fatal(`GPU device lost: ${info.reason} ${info.message}`));
  device.addEventListener('uncapturederror', (e) => fatal('WebGPU error: ' + e.error.message));
  let adapterName = '';
  try { const i = adapter.info || (await adapter.requestAdapterInfo?.()); adapterName = i ? `${i.vendor} ${i.architecture || ''} ${i.description || ''}`.trim() : ''; } catch { /* optional */ }

  const checkModule = (module, label) => {
    module.getCompilationInfo().then((info) => {
      for (const m of info.messages) if (m.type === 'error') fatal(`[${label}] WGSL ${m.lineNum}:${m.linePos} ${m.message}`);
    });
    return module;
  };

  const canvas = $('c');
  const ctx = canvas.getContext('webgpu');
  const format = navigator.gpu.getPreferredCanvasFormat();
  ctx.configure({ device, format, alphaMode: 'opaque' });

  // world
  const crates = [];
  let seed = 7;
  const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647);
  for (let i = 0; i < 40; i++) {
    const a = rnd() * Math.PI * 2, r = 14 + rnd() * 110;
    const s = 0.6 + rnd() * 1.1;
    crates.push({ x: Math.cos(a) * r, z: Math.sin(a) * r, yaw: rnd() * Math.PI, hx: s, hy: s, hz: s, color: [0.55 + rnd() * 0.15, 0.38, 0.2, 0], gas: true });
  }
  // a row of barriers to slide into
  for (let i = 0; i < 6; i++) crates.push({ x: -12 + i * 4.5, z: 22, yaw: 0, hx: 1.6, hy: 0.6, hz: 0.5, color: [0.9, 0.9, 0.9, 0] });

  if (new URLSearchParams(location.search).has('boomtest')) { Object.assign(crates[0], { x: 1.0, z: 4.2, yaw: 0.3 }); }
  device.pushErrorScope('validation');
  const car = new Car();
  const scene = new Scene(device, format, checkModule, crates);
  const smoke = new Smoke(device, format, checkModule);
  const sand = new Sand(device, format, checkModule);
  const err = await device.popErrorScope();
  if (err) { fatal('Pipeline creation failed: ' + err.message); return; }

  let depthTex = null;
  function resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 1.5);
    const w = Math.max(1, Math.floor(canvas.clientWidth * dpr)), h = Math.max(1, Math.floor(canvas.clientHeight * dpr));
    if (depthTex && canvas.width === w && canvas.height === h) return;
    canvas.width = w; canvas.height = h;
    depthTex?.destroy();
    depthTex = device.createTexture({ size: [w, h], format: 'depth32float', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING });
    smoke.makeRenderBindGroup(scene.camUbo, depthTex.createView());
  }
  sand.makeRenderBindGroup(scene.camUbo);

  // input
  const keys = {};
  const map = { KeyW: 'throttle', ArrowUp: 'throttle', KeyS: 'brake', ArrowDown: 'brake', KeyA: 'left', ArrowLeft: 'left', KeyD: 'right', ArrowRight: 'right', Space: 'handbrake' };
  addEventListener('keydown', (e) => { if (map[e.code]) { keys[map[e.code]] = true; e.preventDefault(); } if (e.code === 'KeyR') car.reset(); if (e.code === 'KeyH') $('help').classList.toggle('hide'); });
  addEventListener('keyup', (e) => { if (map[e.code]) keys[map[e.code]] = false; });
  addEventListener('blur', () => { for (const k in keys) keys[k] = false; });
  // touch/mouse buttons
  document.querySelectorAll('[data-k]').forEach((b) => {
    const k = b.dataset.k;
    const on = (e) => { keys[k] = true; e.preventDefault(); }, off = () => { keys[k] = false; };
    b.addEventListener('pointerdown', on); b.addEventListener('pointerup', off); b.addEventListener('pointerleave', off);
  });
  // orbit camera
  let camYaw = 0, camPitch = 0.32, camDist = 11, dragging = false, lastX = 0, lastY = 0, userOrbit = 0;
  canvas.addEventListener('pointerdown', (e) => { dragging = true; lastX = e.clientX; lastY = e.clientY; });
  addEventListener('pointerup', () => { dragging = false; });
  addEventListener('pointermove', (e) => {
    if (!dragging) return;
    camYaw -= (e.clientX - lastX) * 0.006; camPitch = Math.min(1.3, Math.max(0.05, camPitch + (e.clientY - lastY) * 0.004));
    lastX = e.clientX; lastY = e.clientY; userOrbit = 2.5;
  });
  canvas.addEventListener('wheel', (e) => { camDist = Math.min(40, Math.max(5, camDist * (1 + Math.sign(e.deltaY) * 0.1))); e.preventDefault(); }, { passive: false });
  // smoke settings panel
  const DEFAULTS = { blast: 1.0, emission: 0.6, fade: 0.9, opacity: 1.3, shadow: 1.6, ambient: 1.0, brightness: 1.0, dust: 0.35, tint: 1.0, phase: 0.45, vorticity: 5, buoyancy: 1.6 };
  const RANGES = {
    blast: [0, 2, 0.05, 'Explosion size (0=off)'],
    emission: [0, 2, 0.05, 'Amount'], fade: [0.1, 3, 0.05, 'Fade speed'], opacity: [0.2, 4, 0.05, 'Opacity'],
    shadow: [0, 4, 0.05, 'Self-shadow'], ambient: [0, 2, 0.05, 'Ambient / sky'], brightness: [0.3, 2, 0.05, 'Brightness'],
    dust: [0, 1, 0.01, 'Sand-dust mix'], tint: [0.5, 1.2, 0.01, 'Grey level'], phase: [0, 0.85, 0.01, 'Sun glow (fwd scatter)'],
    vorticity: [0, 14, 0.1, 'Curl / vorticity'], buoyancy: [0, 5, 0.05, 'Rise (buoyancy)'],
  };
  let settings = { ...DEFAULTS };
  try { Object.assign(settings, JSON.parse(localStorage.getItem('smokeSettings') || '{}')); } catch { /* ignore */ }
  const sl = $('sliders');
  const build = () => {
    sl.innerHTML = '';
    for (const [k, [mn, mx, st, label]] of Object.entries(RANGES)) {
      const row = document.createElement('label');
      row.innerHTML = `<span>${label}</span><input type="range" min="${mn}" max="${mx}" step="${st}" value="${settings[k]}"><output>${(+settings[k]).toFixed(2)}</output>`;
      const inp = row.querySelector('input'), out = row.querySelector('output');
      inp.addEventListener('input', () => { settings[k] = +inp.value; out.textContent = (+inp.value).toFixed(2); localStorage.setItem('smokeSettings', JSON.stringify(settings)); });
      inp.addEventListener('keydown', (e) => e.stopPropagation());
      sl.appendChild(row);
    }
  };
  build();
  $('resetSmoke').onclick = () => { settings = { ...DEFAULTS }; localStorage.removeItem('smokeSettings'); build(); };
  $('toggleSmoke').onclick = () => $('smokePanel').classList.toggle('collapsed');
  const forceSmoke = new URLSearchParams(location.search).has('smoketest');
  const autoInput = new URLSearchParams(location.search).has('demo');

  const lightDir = (() => { const v = [0.45, 0.8, 0.35]; const l = Math.hypot(...v); return v.map((x) => x / l); })();
  let last = performance.now(), acc = 0, frame = 0, fpsT = 0, fpsN = 0, fps = 0, camHeading = 0;
  const camPos = [0, 4, -10];
  $('tier').textContent = `GTX tier · smoke ${SMOKE_DIMS.join('×')} @ ${SMOKE_H} m · Jacobi ${smoke.jacobiIters} · sand MLS-MPM ${SAND_MAX / 1024}k (${SAND_GRID.join('×')} grid)`;
  const vram = ((smoke.bytes + sand.bytes) / 1048576).toFixed(0);

  // ---- gas-crate explosions: proximity fuse -> fireball + shock + debris ----
  const blasts = [], debris = [];
  let shake = 0; const flash = [0, 0, 0, 0];
  smoke.blasts = blasts;
  function detonate(c) {
    const k = settings.blast;
    c.exploded = true; c.respawn = 15; c.fuse = undefined; c.drawColor = null;
    const size = Math.max(c.hx, c.hz);
    blasts.push({ x: c.x, y: c.hy, z: c.z, age: 0, radius: (1.8 + size * 1.3) * Math.sqrt(k), fuel: 1.8 * k, impulse: 9 * k });
    sand.burst(c.x, c.z, Math.floor(2500 * k), 7 + 4 * k);
    sand.blast = [c.x, 0.5, c.z, 8 * k];
    for (let i = 0; i < 14; i++) {
      const a = Math.random() * Math.PI * 2, sp = (4 + Math.random() * 9) * k;
      debris.push({ x: c.x, y: c.hy, z: c.z, vx: Math.cos(a) * sp, vy: (4 + Math.random() * 9) * k, vz: Math.sin(a) * sp,
        s: size * (0.12 + Math.random() * 0.22), yaw: 0, rx: 0, rz: 0, wr: [(Math.random() - 0.5) * 15, (Math.random() - 0.5) * 15], life: 7,
        color: Math.random() < 0.3 ? [0.12, 0.1, 0.08, 0] : c.color });
    }
    // shock on the car
    const dx = car.x - c.x, dz = car.z - c.z, dist = Math.hypot(dx, dz) || 1;
    const f = Math.max(0, 1 - dist / 12) * k;
    car.vx += (dx / dist) * 16 * f; car.vz += (dz / dist) * 16 * f; car.w += (Math.random() - 0.5) * 5 * f;
    shake = Math.min(1.5, shake + 0.4 + 1.2 * f);
    flash[0] = c.x; flash[1] = 1.8; flash[2] = c.z; flash[3] = 10 * k;
  }
  function updateExplosions(dt) {
    for (const c of crates) {
      if (!c.gas) continue;
      if (c.exploded) { c.respawn -= dt; if (c.respawn <= 0 && Math.hypot(car.x - c.x, car.z - c.z) > 15) c.exploded = false; continue; }
      const gap = Math.hypot(car.x - c.x, car.z - c.z) - Math.max(c.hx, c.hz) - 1.2;
      if (c.fuse === undefined && gap < 3.0 && settings.blast > 0) c.fuse = 0.45;
      if (c.fuse !== undefined) {
        c.fuse -= dt;
        c.drawColor = (Math.floor(c.fuse * 16) % 2) ? [1, 0.25, 0.1, 0] : c.color; // hissing gas warning blink
        if (c.fuse <= 0) detonate(c);
      }
    }
    for (const b of blasts) b.age += 1 / 60;
    while (blasts.length && blasts[0].age > 0.1) blasts.shift();
    flash[3] *= Math.exp(-dt * 7);
    for (let i = debris.length - 1; i >= 0; i--) {
      const b = debris[i];
      b.vy -= 9.81 * dt; b.x += b.vx * dt; b.y += b.vy * dt; b.z += b.vz * dt;
      b.rx += b.wr[0] * dt; b.rz += b.wr[1] * dt;
      if (b.y < b.s) { b.y = b.s; b.vy *= -0.3; b.vx *= 0.6; b.vz *= 0.6; b.wr[0] *= 0.6; b.wr[1] *= 0.6; }
      b.life -= dt;
      if (b.life <= 0) debris.splice(i, 1);
    }
  }

  function frameFn(now) {
    const dtReal = Math.min(0.05, (now - last) / 1000); last = now;
    resize();
    fpsT += dtReal; fpsN++;
    if (fpsT > 0.5) { fps = fpsN / fpsT; fpsT = 0; fpsN = 0; }

    const input = autoInput ? { throttle: true, left: Math.sin(now / 1400) > -0.2, right: false, handbrake: (now % 3000) < 900, brake: false } : keys;
    acc += dtReal;
    const PH = 1 / 120;
    const solids = crates.filter((c) => !c.exploded);
    while (acc >= PH) { car.step(PH, input, solids); acc -= PH; }
    updateExplosions(dtReal);

    // camera: chase + orbit
    const [fx, fz] = car.fwd();
    const vh = car.speed > 2 ? Math.atan2(car.vx, car.vz) : car.heading;
    const target = 0.7 * car.heading + 0.3 * vh;
    let dh = target - camHeading; dh = Math.atan2(Math.sin(dh), Math.cos(dh));
    camHeading += dh * Math.min(1, dtReal * 3);
    userOrbit = Math.max(0, userOrbit - dtReal);
    if (userOrbit <= 0) camYaw *= 1 - Math.min(1, dtReal * 1.5);
    const yaw = camHeading + camYaw + Math.PI;
    const want = [car.x + Math.sin(yaw) * Math.cos(camPitch) * camDist, 1 + Math.sin(camPitch) * camDist, car.z + Math.cos(yaw) * Math.cos(camPitch) * camDist];
    for (let i = 0; i < 3; i++) camPos[i] += (want[i] - camPos[i]) * Math.min(1, dtReal * 6);
    shake *= Math.exp(-dtReal * 5);
    const shaken = camPos.map((v) => v + (Math.random() - 0.5) * shake);
    const view = mat4.lookAt(shaken, [car.x + fx * 1.5, 1.0, car.z + fz * 1.5], [0, 1, 0]);
    const proj = mat4.perspective(1.0, canvas.width / canvas.height, 0.1, 600);
    const vp = mat4.mul(proj, view);
    scene.writeCamera(vp, shaken, lightDir, canvas.width, canvas.height, frame, car, flash);
    scene.buildInstances(car, debris);

    // simulation inputs
    const simDt = 1 / 60;
    if (forceSmoke) for (const wh of car.wheels) if (!wh.front) wh.smoke = 1;
    const emitters = car.wheels.map((wh) => {
      const tread = -wh.spinVel * car.wheelR;
      return {
        // emit just behind the contact patch (outside the solid tyre), opposite to the patch's sliding direction
        pos: (() => { const sx = wh.vel[0] + tread * wh.fwd[0], sz = wh.vel[2] + tread * wh.fwd[2], l = Math.hypot(sx, sz) || 1;
          return [wh.pos[0] + (sx / l) * 0.55, 0.2, wh.pos[2] + (sz / l) * 0.55]; })(),
        strength: wh.smoke * 1.6, radius: Math.max(0.45, SMOKE_H * 1.2),
        center: [wh.pos[0], car.wheelR, wh.pos[2]], wheelR: car.wheelR, spin: wh.spinVel,
        axis: [Math.cos(car.heading + (wh.front ? car.steer : 0)), 0, -Math.sin(car.heading + (wh.front ? car.steer : 0))],
        vel: [wh.vel[0] * 0.25 + tread * wh.fwd[0] * 0.3, 0.8, wh.vel[2] * 0.25 + tread * wh.fwd[2] * 0.3],
      };
    });
    const near = solids.map((c) => [c, (c.x - car.x) ** 2 + (c.z - car.z) ** 2]).sort((a, b) => a[1] - b[1]).slice(0, MAX_CRATES).map((a) => a[0]);
    smoke.update(simDt, car, emitters, near, settings, lightDir);
    sand.update(simDt, car);
    sand.spawn(car, dtReal);

    const enc = device.createCommandEncoder();
    sand.encode(enc);
    smoke.encode(enc);
    const view0 = ctx.getCurrentTexture().createView();
    const p1 = enc.beginRenderPass({
      colorAttachments: [{ view: view0, loadOp: 'clear', storeOp: 'store', clearValue: { r: 0.62, g: 0.72, b: 0.84, a: 1 } }],
      depthStencilAttachment: { view: depthTex.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' },
    });
    scene.draw(p1);
    sand.draw(p1);
    p1.end();
    const p2 = enc.beginRenderPass({ colorAttachments: [{ view: view0, loadOp: 'load', storeOp: 'store' }] });
    smoke.draw(p2);
    p2.end();
    device.queue.submit([enc.finish()]);

    frame++;
    if (frame % 10 === 0) {
      const alive = sand.aliveEstimate(dtReal * 10);
      $('stats').innerHTML =
        `<b>${fps.toFixed(0)}</b> fps · ${(1000 / Math.max(fps, 1)).toFixed(1)} ms<br>` +
        `speed <b>${(car.speed * 3.6).toFixed(0)}</b> km/h<br>` +
        `sand particles ~<b>${alive}</b> / ${SAND_MAX}<br>` +
        `sim VRAM ≈ <b>${vram} MB</b> / 1024<br>` +
        `<span class="dim">${adapterName}</span>`;
      $('drift').style.width = (car.drift * 100).toFixed(0) + '%';
      $('driftLbl').textContent = car.drift > 0.05 ? 'DRIFT 💨' : 'grip';
    } else if (frame % 10 === 1) {
      // aliveEstimate decrements in 10-frame batches
    }
    window.__simFrame = frame;
    requestAnimationFrame(frameFn);
  }
  requestAnimationFrame(frameFn);
}

init().catch((e) => fatal(e.stack || e));
