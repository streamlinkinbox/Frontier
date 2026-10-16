/**
 * Application entry: WebGPU bootstrap, fixed-step frame loop, UI, telemetry and benchmark.
 *
 * WebGPU is mandatory. If it is unavailable the page says so and stops; there is no CPU
 * fallback for the solver, by design.
 */
import { PHYS, PRESETS, SOLVERS, TELEM_BYTES } from './tyre/config.js';
import { TyreSim } from './sim/tyreSim.js';
import { TyreRenderer } from './render/renderer.js';
import { ReadbackRing } from './gpu/readback.js';

const $ = (id) => document.getElementById(id);
const fmt = (x, d = 0) => (Number.isFinite(x) ? x.toLocaleString('en-US', { maximumFractionDigits: d, minimumFractionDigits: d }) : '–');

function fatal(html) {
  const el = $('fatal');
  el.innerHTML = html;
  el.style.display = 'block';
  throw new Error('fatal');
}

/** Keeps the adapter alive for the page lifetime (dropping it can lose the device in some builds). */
let keepAdapter = null;

async function bootstrap() {
  if (!navigator.gpu) {
    fatal(`<h1>WebGPU is required</h1>
      <p>This demo runs the tyre solver entirely on the GPU and has no CPU fallback.</p>
      <p>Use a browser with WebGPU enabled (Chrome / Edge 113+, or Firefox / Safari with WebGPU turned on).</p>`);
  }
  const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
  if (!adapter) fatal('<h1>No WebGPU adapter</h1><p>The browser exposes WebGPU but no GPU adapter was returned.</p>');
  keepAdapter = adapter;
  const hasTs = adapter.features.has('timestamp-query');
  const device = await adapter.requestDevice({ requiredFeatures: hasTs ? ['timestamp-query'] : [] });
  device.lost.then((info) => fatal(`<h1>GPU device lost</h1><p>${info.message}</p>`));
  device.addEventListener('uncapturederror', (e) => console.error('[gpu]', e.error.message));
  return { device, hasTs, adapter };
}

async function loadText(path) {
  const r = await fetch(path, { cache: 'no-cache' });
  if (!r.ok) throw new Error(`failed to load ${path}: ${r.status}`);
  return r.text();
}

async function main() {
  const { device, hasTs } = await bootstrap();
  const canvas = $('view');
  const params = new URLSearchParams(location.search);
  // Headless test mode: render into an offscreen texture instead of presenting to the canvas.
  // Some headless WebGPU builds lose the device on canvas presentation; compute and render still run.
  const offscreen = params.get('offscreen') === '1';
  const ctx = canvas.getContext('webgpu');
  const format = navigator.gpu.getPreferredCanvasFormat();
  if (!offscreen) ctx.configure({ device, format, alphaMode: 'opaque' });
  let offTex = null;
  const targetView = () => {
    if (!offscreen) return ctx.getCurrentTexture().createView();
    if (!offTex || offTex.width !== canvas.width || offTex.height !== canvas.height) {
      offTex?.destroy();
      offTex = device.createTexture({
        size: [canvas.width, canvas.height],
        format,
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
      });
    }
    return offTex.createView();
  };
  // backing store follows CSS size times device pixel ratio (capped for the 4K case)
  const fitCanvas = () => {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.max(1, Math.floor(canvas.clientWidth * dpr));
    canvas.height = Math.max(1, Math.floor(canvas.clientHeight * dpr));
  };
  fitCanvas();
  window.addEventListener('resize', fitCanvas);
  $('tsBadge').textContent = hasTs ? 'GPU timestamps: on' : 'GPU timestamps: unavailable';
  $('tsBadge').className = hasTs ? 'badge' : 'badge warn';

  const [simCode, renderCode] = await Promise.all([
    loadText('src/shaders/tyre_sim.wgsl'),
    loadText('src/shaders/render.wgsl'),
  ]);
  const simModule = device.createShaderModule({ code: simCode, label: 'tyre_sim.wgsl' });
  const renderModule = device.createShaderModule({ code: renderCode, label: 'render.wgsl' });
  for (const [name, mod] of [['tyre_sim.wgsl', simModule], ['render.wgsl', renderModule]]) {
    const info = await mod.getCompilationInfo();
    const errors = info.messages.filter((m) => m.type === 'error');
    if (errors.length) {
      fatal(`<h1>Shader error in ${name}</h1><pre>${errors.map((e) => `${e.lineNum}:${e.linePos} ${e.message}`).join('\n')}</pre>`);
    }
  }

  // ------------------------------------------------------------------ shared state
  let sim = null;
  let renderer = null;
  let busy = false;
  let acc = 0;
  let frameNo = 0;
  let lastT = performance.now();
  const TS_BYTES = hasTs ? 16 : 0;
  const ring = new ReadbackRing(device, TELEM_BYTES + TS_BYTES, 3);
  let latest = null; // last decoded telemetry
  let rtfWindow = { t: performance.now(), sim: 0 };
  let rtf = 0;
  let gpuMs = NaN;
  let lastSubsteps = 0;
  let lastJsMs = 0;
  let loadStamp = -1;
  let rbLat = NaN;

  // ------------------------------------------------------------------ UI state
  const startSolver = SOLVERS[params.get('solver')] ? params.get('solver') : 'xpbd';
  const ui = {
    solver: startSolver,
    preset: PRESETS[params.get('preset')] ? params.get('preset') : 'high',
    iterations: SOLVERS[startSolver].defaultIterations,
    speedKmh: 36,
    slipDeg: 0,
    payloadKg: 400,
    mu: 0.9,
    bar: 2.3,
    stiffExp: 0,
    showStrain: true,
    paused: false,
    timeScale: 1,
    yaw: 0.0,
    pitch: 0.1,
    dist: 1.7,
  };

  // reflect the start-up choice in the controls before the sliders read their values
  $('solver').value = ui.solver;
  $('preset').value = ui.preset;
  $('iters').value = String(ui.iterations);

  const bind = (id, key, fn) => {
    const el = $(id);
    const apply = () => {
      ui[key] = parseFloat(el.value);
      fn?.(ui[key]);
    };
    el.addEventListener('input', apply);
    apply();
  };
  bind('speed', 'speedKmh', (v) => ($('speedOut').textContent = `${fmt(v)} km/h`));
  bind('slip', 'slipDeg', (v) => ($('slipOut').textContent = `${v.toFixed(1)}°`));
  bind('payload', 'payloadKg', (v) => ($('payloadOut').textContent = `${fmt(v)} kg`));
  bind('mu', 'mu', (v) => ($('muOut').textContent = v.toFixed(2)));
  bind('bar', 'bar', (v) => ($('barOut').textContent = `${v.toFixed(2)} bar`));
  bind('stiff', 'stiffExp', (v) => {
    ui.stiffScale = Math.pow(10, v);
    $('stiffOut').textContent = `${ui.stiffScale.toFixed(2)}×`;
    sim?.setStiffness(ui.stiffScale);
  });
  bind('iters', 'iterations', (v) => ($('itersOut').textContent = String(v)));
  $('strain').addEventListener('change', (e) => (ui.showStrain = e.target.checked));
  ui.showStrain = $('strain').checked;

  $('solver').addEventListener('change', (e) => {
    ui.solver = e.target.value;
    ui.iterations = SOLVERS[ui.solver].defaultIterations;
    $('iters').value = String(ui.iterations);
    $('itersOut').textContent = String(ui.iterations);
  });
  $('preset').addEventListener('change', (e) => switchPreset(e.target.value));
  $('pause').addEventListener('click', () => {
    ui.paused = !ui.paused;
    $('pause').textContent = ui.paused ? 'Resume' : 'Pause';
  });
  $('reset').addEventListener('click', () => {
    if (!sim || busy) return;
    sim.reset(ui.speedKmh, ui.bar);
    acc = 0;
    resetStats();
  });
  $('bench').addEventListener('click', () => runBenchmark());

  // orbit / zoom
  let drag = null;
  canvas.addEventListener('pointerdown', (e) => {
    drag = { x: e.clientX, y: e.clientY, yaw: ui.yaw, pitch: ui.pitch };
    canvas.setPointerCapture(e.pointerId);
  });
  canvas.addEventListener('pointermove', (e) => {
    if (!drag) return;
    ui.yaw = drag.yaw - (e.clientX - drag.x) * 0.005;
    ui.pitch = Math.max(-0.2, Math.min(1.2, drag.pitch + (e.clientY - drag.y) * 0.004));
  });
  canvas.addEventListener('pointerup', () => (drag = null));
  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    ui.dist = Math.max(0.6, Math.min(5, ui.dist * Math.exp(e.deltaY * 0.001)));
  }, { passive: false });

  // ------------------------------------------------------------------ simulation objects
  function resetStats() {
    latest = null;
    rtfWindow = { t: performance.now(), sim: 0 };
    gpuMs = NaN;
  }

  async function buildSim(presetKey) {
    if (sim) {
      for (const k of Object.keys(sim.buf)) sim.buf[k]?.destroy?.();
      sim.querySet?.destroy?.();
    }
    const p = PRESETS[presetKey];
    sim = new TyreSim(device, simModule, p, { timestamps: hasTs });
    await sim.init();
    sim.setStiffness(ui.stiffScale ?? 1);
    renderer = new TyreRenderer(device, format, renderModule, canvas);
    await renderer.init(sim);
    $('presetOut').textContent = `${p.NP}×${p.NC}`;
    resetStats();
  }

  async function switchPreset(key) {
    if (busy) return;
    busy = true;
    ui.preset = key;
    try {
      await buildSim(key);
    } finally {
      busy = false;
    }
  }

  await buildSim(ui.preset);

  // ------------------------------------------------------------------ frame loop
  function handleReadback(result) {
    const f = new Float32Array(result.data, 0, TELEM_BYTES / 4);
    const t = TyreSim.decodeTelemetry(f);
    latest = t;
    loadStamp = result.stamp;
    if (hasTs) {
      const ts = new BigUint64Array(result.data, TELEM_BYTES, 2);
      const ns = Number(ts[1] - ts[0]);
      gpuMs = ns / 1e6;
    }
    rbLat = result.latencyMs;
    updateHud(t);
  }
  function updateHud(t) {
    const N = sim.topo.N;
    const target = t.targetLoad;
    $('fz').textContent = `${fmt(t.Fz)} N`;
    $('fzT').textContent = `${fmt(target)} N`;
    const err = (t.Fz - target) / target;
    $('fzErr').textContent = `${(err * 100).toFixed(1)} %`;
    $('fzErr').className = Math.abs(err) < 0.05 ? 'v good' : 'v';
    $('fx').textContent = `${fmt(t.Fx)} N`;
    $('fy').textContent = `${fmt(t.FzLat)} N`;
    $('contacts').textContent = `${fmt(t.contacts, 0)} / ${fmt(N)}`;
    const patchCm2 = (t.contacts * sim.restArea) / N * 1e4;
    $('patch').textContent = `${fmt(patchCm2, 0)} cm²`;
    $('vol').textContent = `${fmt(t.V * 1000, 1)} L`;
    $('volErr').textContent = `${(t.volErr * 100).toFixed(2)} %`;
    $('pres').textContent = `${fmt(t.pressure / 1e5, 2)} bar`;
    $('strain').textContent = `${(t.strainMean * 100).toFixed(2)} %`;
    $('hub').textContent = `${fmt(t.hub.y * 1000, 0)} mm`;
    $('hubV').textContent = `${(t.hubVel.x * 3.6).toFixed(1)} km/h`;
    $('omega').textContent = `${t.hubVel.omega.toFixed(2)}`;
    const wheelV = -t.hubVel.omega * PHYS.effectiveRadius;
    const slip = t.hubVel.x > 0.5 ? (wheelV - t.hubVel.x) / Math.abs(t.hubVel.x) : 0;
    $('slipR').textContent = `${(slip * 100).toFixed(1)} %`;
  }

  function frame(now) {
    requestAnimationFrame(frame);
    if (!sim || busy) return;
    const dtReal = Math.min(0.1, (now - lastT) / 1000);
    lastT = now;
    if (!ui.paused) acc += dtReal * ui.timeScale;
    let nSub = Math.min(PHYS.maxSubstepsPerFrame, Math.floor(acc / PHYS.dtSub + 1e-9));
    acc -= nSub * PHYS.dtSub;
    if (acc > PHYS.dtSub * PHYS.maxSubstepsPerFrame) acc = 0;

    const t0 = performance.now();
    const encoder = device.createCommandEncoder({ label: 'frame' });
    let stamp = -1;
    if (nSub > 0) {
      sim.writeParams({
        nSub,
        iterations: ui.iterations,
        solver: ui.solver,
        speedKmh: ui.speedKmh,
        slipDeg: ui.slipDeg,
        payloadKg: ui.payloadKg,
        bar: ui.bar,
        mu: ui.mu,
      });
      sim.recordFrame(encoder, { nSub, iterations: ui.iterations, solver: ui.solver });
      lastSubsteps = nSub;
      stamp = frameNo;
      const slot = ring.acquire(stamp);
      if (slot >= 0) {
        // stage the telemetry block (and the GPU timestamps) into this slot
        const stg = ring.slots[slot].buffer;
        encoder.copyBufferToBuffer(sim.buf.TELEM, 0, stg, 0, TELEM_BYTES);
        if (hasTs) encoder.copyBufferToBuffer(sim.tsResolve, 0, stg, TELEM_BYTES, 16);
      }
      rtfWindow.sim += nSub * PHYS.dtSub;
    }
    sim.recordNormals(encoder);
    renderer.record(encoder, targetView(), {
      target: [0, 0.3, 0],
      yaw: ui.yaw,
      pitch: ui.pitch,
      dist: ui.dist,
      cameraX: sim.simTime * ui.speedKmh / 3.6,
      showStrain: ui.showStrain,
      strainScale: 6.0,
    });
    device.queue.submit([encoder.finish()]);
    lastJsMs = performance.now() - t0;
    ring.kick();
    frameNo++;

    for (const r of ring.poll()) handleReadback(r);

    // realtime factor over the last second of wall time
    if (now - rtfWindow.t > 1000) {
      rtf = rtfWindow.sim / ((now - rtfWindow.t) / 1000);
      rtfWindow = { t: now, sim: 0 };
      $('rtf').textContent = `${rtf.toFixed(2)}×`;
      $('gpuMs').textContent = Number.isFinite(gpuMs) ? `${gpuMs.toFixed(2)} ms` : (hasTs ? 'waiting…' : 'n/a');
      $('subs').textContent = `${lastSubsteps} × ${(PHYS.dtSub * 1000).toFixed(2)} ms`;
      $('rbLat').textContent = Number.isFinite(rbLat) ? `${rbLat.toFixed(1)} ms` : '–';
      $('rbAge').textContent = loadStamp >= 0 ? `${frameNo - loadStamp} frames` : '–';
    }
  }
  requestAnimationFrame(frame);

  // ------------------------------------------------------------------ benchmark
  async function runBenchmark() {
    if (busy || !sim) return;
    busy = true;
    const out = $('benchOut');
    const table = $('benchTable');
    table.innerHTML = '';
    const savedSolver = ui.solver;
    const savedIters = ui.iterations;
    const rows = [];
    try {
      for (const solver of Object.keys(SOLVERS)) {
        out.textContent = `Running ${SOLVERS[solver].label} …`;
        const iterations = SOLVERS[solver].defaultIterations;
        sim.reset(ui.speedKmh, ui.bar);
        const nSub = 8;
        const runFrames = (count) => {
          for (let f = 0; f < count; f++) {
            const enc = device.createCommandEncoder();
            sim.writeParams({ nSub, iterations, speedKmh: ui.speedKmh, slipDeg: ui.slipDeg, payloadKg: ui.payloadKg, bar: ui.bar, mu: ui.mu });
            sim.recordFrame(enc, { nSub, iterations, solver });
            device.queue.submit([enc.finish()]);
          }
        };
        runFrames(10); // warm-up (pipelines, caches)
        await device.queue.onSubmittedWorkDone();
        const tA = performance.now();
        runFrames(90);
        await device.queue.onSubmittedWorkDone();
        const msPerFrame = (performance.now() - tA) / 90;

        // settle for 2 s of simulated time (start-up ramps finish in ~1 s), then average the
        // load over a 1 s window sampled every frame. A single instant lands on a road bounce.
        runFrames(Math.round(2.0 / (nSub * PHYS.dtSub)));
        const readStg = device.createBuffer({ size: TELEM_BYTES, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        const sampleFrame = async () => {
          runFrames(1);
          const enc = device.createCommandEncoder();
          enc.copyBufferToBuffer(sim.buf.TELEM, 0, readStg, 0, TELEM_BYTES);
          device.queue.submit([enc.finish()]);
          await readStg.mapAsync(GPUMapMode.READ);
          const t = TyreSim.decodeTelemetry(new Float32Array(readStg.getMappedRange().slice(0)));
          readStg.unmap();
          return t;
        };
        const win = Math.round(1.0 / (nSub * PHYS.dtSub));
        const acc = { Fz: 0, volErr: 0, hub: 0, n: 0, min: Infinity, max: -Infinity, target: 0, pressure: 0, contacts: 0 };
        let last = null;
        for (let k = 0; k < win; k++) {
          last = await sampleFrame();
          acc.Fz += last.Fz;
          acc.volErr += last.volErr;
          acc.hub += last.hub.y;
          acc.pressure += last.pressure;
          acc.contacts += last.contacts;
          acc.target = last.targetLoad;
          acc.min = Math.min(acc.min, last.Fz);
          acc.max = Math.max(acc.max, last.Fz);
          acc.n++;
        }
        readStg.destroy();
        const meanFz = acc.Fz / acc.n;
        const err = (meanFz - acc.target) / acc.target;
        rows.push({
          label: SOLVERS[solver].label,
          iterations,
          msPerFrame,
          loadErr: err,
          loadMinN: acc.min,
          loadMaxN: acc.max,
          loadMeanN: meanFz,
          targetN: acc.target,
          volErr: acc.volErr / acc.n,
          pressureBar: acc.pressure / acc.n / 1e5,
          hubMm: (acc.hub / acc.n) * 1000,
          contacts: acc.contacts / acc.n,
        });
      }
    } finally {
      // restore the user's solver and iteration count on the shared state
      ui.solver = savedSolver;
      ui.iterations = savedIters;
      sim.reset(ui.speedKmh, ui.bar);
      resetStats();
      busy = false;
    }
    const rowsHtml = rows.map((r) => `
      <tr><td class="k" colspan="2"><b>${r.label}</b> (${r.iterations} iter)</td></tr>
      <tr><td class="k">Wall time per frame (8 substeps)</td><td class="v">${fmt(r.msPerFrame, 1)} ms</td></tr>
      <tr><td class="k">Mean load error, 1 s window after 2 s settle</td><td class="v">${(r.loadErr * 100).toFixed(1)} %</td></tr>
      <tr><td class="k">Load ripple (min–max Fz)</td><td class="v">${fmt(r.loadMinN, 0)} – ${fmt(r.loadMaxN, 0)} N (target ${fmt(r.targetN, 0)} N)</td></tr>
      <tr><td class="k">Volume error</td><td class="v">${(r.volErr * 100).toFixed(2)} %</td></tr>
      <tr><td class="k">Gauge pressure</td><td class="v">${r.pressureBar.toFixed(2)} bar</td></tr>
      <tr><td class="k">Hub height</td><td class="v">${r.hubMm.toFixed(0)} mm</td></tr>`).join('');
    table.innerHTML = rowsHtml || '<tr><td class="k">No results</td><td></td></tr>';
    out.textContent = `Done at ${sim ? PRESETS[ui.preset].NP + '×' + PRESETS[ui.preset].NC : ''}. Times are wall clock on this device (includes queue sync).`;
  }

  // expose a small handle for automated checks
  window.__tyre = {
    get sim() { return sim; },
    get ui() { return ui; },
    get latest() { return latest; },
    get frameNo() { return frameNo; },
    get busy() { return busy; },
    device,
    ring,
    runBenchmark,
    switchPreset,
    /** Advance n frames of nSub substeps without rendering (tests and benchmarks). */
    async stepFrames(n, nSub = 8, extra = {}) {
      const solver = ui.solver;
      const iterations = ui.iterations;
      for (let f = 0; f < n; f++) {
        const enc = device.createCommandEncoder();
        sim.writeParams({ nSub, iterations, speedKmh: ui.speedKmh, slipDeg: ui.slipDeg, payloadKg: ui.payloadKg, bar: ui.bar, mu: ui.mu, ...extra });
        sim.recordFrame(enc, { nSub, iterations, solver });
        device.queue.submit([enc.finish()]);
      }
      await device.queue.onSubmittedWorkDone();
    },
    async readTelemetry() {
      const f = await sim.readBuffer('TELEM');
      return TyreSim.decodeTelemetry(f);
    },
    async readBuffer(key) {
      return sim.readBuffer(key);
    },
    setSolver(s) {
      $('solver').value = s;
      $('solver').dispatchEvent(new Event('change'));
    },
    setIterations(n) {
      $('iters').value = String(n);
      $('iters').dispatchEvent(new Event('input'));
    },
  };
}

main().catch((err) => {
  console.error(err);
  if (err?.message !== 'fatal') {
    const el = $('fatal');
    el.innerHTML = `<h1>Initialisation failed</h1><pre>${String(err?.stack || err)}</pre>`;
    el.style.display = 'block';
  }
});
