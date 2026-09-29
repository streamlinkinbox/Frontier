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
    crates.push({ x: Math.cos(a) * r, z: Math.sin(a) * r, yaw: rnd() * Math.PI, hx: s, hy: s, hz: s, color: [0.55 + rnd() * 0.15, 0.38, 0.2, 0] });
  }
  // a row of barriers to slide into
  for (let i = 0; i < 6; i++) crates.push({ x: -12 + i * 4.5, z: 22, yaw: 0, hx: 1.6, hy: 0.6, hz: 0.5, color: [0.9, 0.9, 0.9, 0] });

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
  const autoInput = new URLSearchParams(location.search).has('demo');

  const lightDir = (() => { const v = [0.45, 0.8, 0.35]; const l = Math.hypot(...v); return v.map((x) => x / l); })();
  let last = performance.now(), acc = 0, frame = 0, fpsT = 0, fpsN = 0, fps = 0, camHeading = 0;
  const camPos = [0, 4, -10];
  $('tier').textContent = `GTX tier · smoke ${SMOKE_DIMS.join('×')} @ ${SMOKE_H} m · Jacobi ${smoke.jacobiIters} · sand MLS-MPM ${SAND_MAX / 1024}k (${SAND_GRID.join('×')} grid)`;
  const vram = ((smoke.bytes + sand.bytes) / 1048576).toFixed(0);

  function frameFn(now) {
    const dtReal = Math.min(0.05, (now - last) / 1000); last = now;
    resize();
    fpsT += dtReal; fpsN++;
    if (fpsT > 0.5) { fps = fpsN / fpsT; fpsT = 0; fpsN = 0; }

    const input = autoInput ? { throttle: true, left: Math.sin(now / 1400) > -0.2, right: false, handbrake: (now % 3000) < 900, brake: false } : keys;
    acc += dtReal;
    const PH = 1 / 120;
    while (acc >= PH) { car.step(PH, input, crates); acc -= PH; }

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
    const view = mat4.lookAt(camPos, [car.x + fx * 1.5, 1.0, car.z + fz * 1.5], [0, 1, 0]);
    const proj = mat4.perspective(1.0, canvas.width / canvas.height, 0.1, 600);
    const vp = mat4.mul(proj, view);
    scene.writeCamera(vp, camPos, lightDir, canvas.width, canvas.height, frame, car);
    scene.buildInstances(car);

    // simulation inputs
    const simDt = 1 / 60;
    const emitters = car.wheels.map((wh) => {
      const tread = -wh.spinVel * car.wheelR;
      return {
        pos: [wh.pos[0], 0.3, wh.pos[2]], strength: wh.smoke * 1.4, radius: 0.5,
        vel: [wh.vel[0] * 0.25 + tread * wh.fwd[0] * 0.3, 1.2, wh.vel[2] * 0.25 + tread * wh.fwd[2] * 0.3],
      };
    });
    const near = crates.map((c) => [c, (c.x - car.x) ** 2 + (c.z - car.z) ** 2]).sort((a, b) => a[1] - b[1]).slice(0, MAX_CRATES).map((a) => a[0]);
    smoke.update(simDt, car, emitters, near);
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
