import { cameraFrame, clamp, intersectGround, rayFromScreen } from './math.js?v=unreal-volume-gradient-20261001';
import { describeGrid, FIXED_STEP, FluidSolver, MAX_EMITTERS, VRAM_BUDGET_BYTES, VRAM_BUDGET_RESERVE_BYTES } from './fluid-solver.js?v=unreal-volume-gradient-20261001';
import { VolumeRenderer, PRESENTATION_BUFFER_COUNT, RENDER_TARGET_BUFFER_COUNT } from './volume-renderer.js?v=unreal-volume-gradient-20261001';
import { loadBakedPlume } from './baked-plume.js?v=baked-plume-cage-20261001';

const $ = (selector) => document.querySelector(selector);
const VRAM_SAFETY_MARGIN_BYTES = 1024 * 1024;
const WORLD_COORDINATE_LIMIT = 1000;
const canvas = $('#world');
const gpuError = $('#gpu-error');
const sourceList = $('#source-list');
let grid = describeGrid();

const DEFAULTS = Object.freeze({
  buoyancy: 6.0,
  vorticity: 2.2,
  burnRate: 3.3,
  fuelFeed: 1.0,
  sootYield: 0.55,
  smokeFade: 0.45,
  turbulence: 1.3,
  wind: 0.4,
  // Unreal-style volume transfer controls. These affect rendering only; the
  // solver continues to evolve the original density and temperature fields.
  densityGain: 1.35,
  densityCutoff: 0.018,
  densityCurve: 0.82,
  sootDensityGain: 0.24,
  temperatureGain: 1.0,
  fireColorLow: [1.0, 0.239, 0.075],
  fireColorHigh: [1.0, 0.941, 0.627],
  smokeColorLight: [0.43, 0.47, 0.50],
  smokeColorDense: [0.188, 0.145, 0.118],
});
const settings = { ...DEFAULTS };
const camera = {
  target: [0, 6.5, 0],
  distance: 38,
  targetDistance: 38,
  yaw: 0,
  pitch: 0.29,
  fov: Math.PI * 0.27,
};
const spawn = { x: 0, z: 0 };
let currentLodLevel = 1;
let placeMode = false;
let adapterLabel = 'GPU';
let device;
let context;
let solver;
let renderer;
let engineReady = false;
let stressTestQueued = false;
let renderScale = -1;
let renderAccumulator = 0;
let accumulator = FIXED_STEP;
let previousFrame = 0;
let simulationTime = 0;
let panelClock = 0;
let lastLodName = '';
let activeMode = 'live';
let bakedPlume = null;
let bakedLoadPromise = null;
let bakedLoadQueued = false;

function showError(message) {
  gpuError.hidden = false;
  $('#gpu-error-text').textContent = message;
  $('#engine-status').textContent = 'ENGINE UNAVAILABLE';
  $('.engine-badge').classList.add('status-error');
  console.error(message);
}

async function validateShader(module, label) {
  const info = await module.getCompilationInfo();
  const errors = info.messages.filter((message) => message.type === 'error');
  if (errors.length) {
    const detail = errors.map((message) => `${label}:${message.lineNum}:${message.linePos} ${message.message}`).join('\n');
    throw new Error(detail);
  }
}

function resolutionFor(scale) {
  const rect = canvas.getBoundingClientRect();
  const requestedDpr = Math.min(window.devicePixelRatio || 1, 1.25);
  const cssPixels = Math.max(1, rect.width * rect.height);
  const activeScale = Math.min(1, Math.max(0.35, scale));
  const bytesPerOutputPixel = 4 * (PRESENTATION_BUFFER_COUNT + RENDER_TARGET_BUFFER_COUNT * activeScale * activeScale);
  const simulationBytes = renderer?.simulationResourceBytes ?? solver?.allocatedBytes ?? 0;
  const availableBytes = Math.max(1, VRAM_BUDGET_BYTES - simulationBytes - VRAM_BUDGET_RESERVE_BYTES - VRAM_SAFETY_MARGIN_BYTES);
  const maxOutputPixels = availableBytes / bytesPerOutputPixel;
  const budgetScale = Math.min(1, Math.sqrt(maxOutputPixels / (cssPixels * requestedDpr * requestedDpr)));
  const dpr = requestedDpr * budgetScale;
  const width = Math.max(1, Math.round(rect.width * dpr));
  const height = Math.max(1, Math.round(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  renderer?.resize(width, height, scale);
}

function chooseLod() {
  const eye = cameraFrame(camera).eye;
  const distance = Math.hypot(
    eye[0] - camera.target[0],
    eye[1] - camera.target[1],
    eye[2] - camera.target[2],
  );
  // Hysteresis keeps the grid from remapping repeatedly as the camera sits on a threshold.
  if (currentLodLevel === 0 && distance > 32) currentLodLevel = 1;
  else if (currentLodLevel === 1 && distance < 26) currentLodLevel = 0;
  else if (currentLodLevel === 1 && distance > 62) currentLodLevel = 2;
  else if (currentLodLevel === 2 && distance < 50) currentLodLevel = 1;

  const tiers = [
    {
      name: 'NEAR', level: 0, cellSize: 0.4, step: 0.32, subCellSamples: 2,
      maxSamples: 128, shadowSamples: 4, shadowStep: 1.25, scale: 0.96, renderHz: 60,
    },
    {
      name: 'MID', level: 1, cellSize: 0.75, step: 0.58, subCellSamples: 2,
      maxSamples: 88, shadowSamples: 3, shadowStep: 1.20, scale: 0.82, renderHz: 60,
    },
    // Far keeps the cheaper grid and ray budget, but it still presents every
    // frame. The previous 30 Hz cap made moving outside the window feel like
    // a CPU stall even though the solver remained at 60 Hz. Shadow taps are
    // reduced independently from the primary sub-cell ray budget.
    {
      name: 'FAR', level: 2, cellSize: 1.2, step: 0.9, subCellSamples: 2,
      maxSamples: 64, shadowSamples: 2, shadowStep: 1.35, scale: 0.68, renderHz: 60,
    },
  ];
  const tier = tiers[currentLodLevel];
  // Keep far LOD smooth. A lower render target is enough for the budget; the
  // old square censor blocks were the source of the visible pixel mosaic.
  return { ...tier, distance, censorPixels: 0 };
}

function refreshGridInfo() {
  if (!solver) return;
  grid = solver.grid;
}

function boundsFromGrid(value) {
  return {
    min: [...value.origin],
    max: [
      value.origin[0] + value.width,
      value.origin[1] + value.height,
      value.origin[2] + value.depth,
    ],
  };
}

function activeBounds() {
  if (activeMode === 'baked' && bakedPlume) return bakedPlume.bounds;
  return boundsFromGrid(grid);
}

function rangeLabel(min, max) {
  return `${min.toFixed(1)}…${max.toFixed(1)}`;
}

function updateBoundsUI() {
  const bounds = activeBounds();
  const mode = activeMode === 'baked' ? 'BAKED · HQ READ ONLY' : 'LIVE · SOURCE WINDOW';
  $('#bounds-label').textContent = mode;
  $('#bounds-value').textContent = `X ${rangeLabel(bounds.min[0], bounds.max[0])} · Y ${rangeLabel(bounds.min[1], bounds.max[1])} · Z ${rangeLabel(bounds.min[2], bounds.max[2])} m`;
  $('#bounds-readout').classList.toggle('baked', activeMode === 'baked');
  renderer?.setBounds(
    bounds,
    activeMode === 'baked' ? [0.18, 0.95, 0.70] : [1.0, 0.42, 0.10],
    grid.cellSize,
  );
}

function lodDescription(lod) {
  const windowLabel = activeMode === 'baked'
    ? 'fixed baked bounds'
    : (solver?.sources.length ? 'source window' : 'simulation window');
  const field = `${Math.round(lod.distance)}m camera · ${Math.round(grid.width)}m ${windowLabel}`;
  return lod.renderHz < 60
    ? `${field} · ${lod.renderHz}Hz render · ${Math.round(lod.censorPixels)}px mosaic`
    : field;
}

function setLodUI(lod) {
  if (lod.name === lastLodName) return;
  lastLodName = lod.name;
  $('#lod-value').textContent = lod.name;
  $('#lod-distance').textContent = lodDescription(lod);
  $('#lod-value').dataset.lod = lod.name.toLowerCase();
  $$('.distance-button').forEach((button) => {
    button.classList.toggle('selected', button.dataset.view === lod.name.toLowerCase());
  });
}

function updateOutputs() {
  const formatters = {
    buoyancy: (value) => `${value.toFixed(1)} m/s²`,
    vorticity: (value) => value.toFixed(1),
    burnRate: (value) => `${value.toFixed(1)} s⁻¹`,
    fuelFeed: (value) => `${value.toFixed(2)}×`,
    sootYield: (value) => value.toFixed(2),
    smokeFade: (value) => `${value.toFixed(2)} s⁻¹`,
    turbulence: (value) => value.toFixed(1),
    wind: (value) => `${value.toFixed(1)} m/s`,
  };
  for (const input of $$('[data-param]')) {
    const name = input.dataset.param;
    const value = Number(input.value);
    settings[name] = value;
    const output = $(`[data-value="${name}"]`);
    if (output) output.textContent = formatters[name](value);
  }
}

function hexToRgb(hex) {
  const normalized = String(hex || '').replace('#', '').trim();
  const value = Number.parseInt(normalized.length === 3
    ? normalized.split('').map((channel) => `${channel}${channel}`).join('')
    : normalized, 16);
  if (!Number.isFinite(value)) return [1, 1, 1];
  return [
    ((value >> 16) & 255) / 255,
    ((value >> 8) & 255) / 255,
    (value & 255) / 255,
  ];
}

function rgbToHex(rgb) {
  return `#${rgb.slice(0, 3).map((channel) => Math.round(clamp(Number(channel) || 0, 0, 1) * 255).toString(16).padStart(2, '0')).join('')}`;
}

function updateColorOutputs() {
  for (const input of $$('[data-color-param]')) {
    settings[input.dataset.colorParam] = hexToRgb(input.value);
    const output = $(`#${input.id}-value`);
    if (output) output.textContent = input.value.toUpperCase();
  }
}

function setSpawn(x, z) {
  spawn.x = clamp(Number.isFinite(x) ? x : spawn.x, -WORLD_COORDINATE_LIMIT, WORLD_COORDINATE_LIMIT);
  spawn.z = clamp(Number.isFinite(z) ? z : spawn.z, -WORLD_COORDINATE_LIMIT, WORLD_COORDINATE_LIMIT);
  $('#spawn-coordinate').textContent = `${spawn.x.toFixed(1)}  /  ${spawn.z.toFixed(1)} m`;
  $('#spawn-dot').style.transform = `translate(${(spawn.x / 12) * 5}px, ${(spawn.z / 12) * 5}px)`;
}

function updateGridUI() {
  $('#grid-value').textContent = `${grid.nx} × ${grid.ny} × ${grid.nz}`;
  $('#cell-value').textContent = `${(grid.cells / 1000).toFixed(0)}k · ${Math.round(grid.width)}×${Math.round(grid.height)}×${Math.round(grid.depth)}m @ ${grid.cellSize.toFixed(2)}m`;
  updateBoundsUI();
}

function setModeUI(mode) {
  activeMode = mode;
  const baked = mode === 'baked';
  if (baked && placeMode) setPlaceMode(false);
  const strip = $('#mode-strip');
  strip.classList.toggle('baked', baked);
  $('#mode-label').textContent = baked ? 'BAKED FIELD · READ ONLY' : 'LIVE SOLVER · COMPUTE';
  $('#live-mode').disabled = !baked;
  $('#load-baked-plume').classList.toggle('is-active', baked);
  $('#telemetry-engine-label').textContent = baked ? 'BAKED VOLUME' : 'VOLUME SOLVER';
  $('.controls-panel').classList.toggle('baked-mode', baked);
  for (const selector of ['#airburst', '#ignite-plume', '#stress-test', '#place-toggle', '#extinguish', '#clear-field', '#restore-defaults']) {
    $(selector).disabled = baked;
  }
  for (const input of $$('[data-param]')) input.disabled = baked;
  $('#engine-status').textContent = baked ? 'BAKED · READ ONLY' : 'COMPUTE LIVE';
  updateGridUI();
  updateSourcePanel();
}

function setLiveMode(showToast = false) {
  if (!solver || !renderer) return;
  activeMode = 'live';
  renderer.bindField(solver);
  grid = solver.grid;
  setModeUI('live');
  if (renderScale > 0) resolutionFor(renderScale);
  if (showToast) showEventToast('Live solver resumed · 60 Hz compute is active', 2200);
}

async function loadBakedSimulation() {
  if (!engineReady) {
    bakedLoadQueued = true;
    showEventToast('Baked plume queued · waiting for GPU initialization', 2400);
    return;
  }
  if (bakedLoadPromise) return bakedLoadPromise;
  const button = $('#load-baked-plume');
  button.classList.add('is-loading');
  const originalLabel = button.querySelector('.event-copy strong').textContent;
  button.querySelector('.event-copy strong').textContent = 'Reading baked plume…';
  bakedLoadPromise = (async () => {
    try {
      if (!bakedPlume) bakedPlume = await loadBakedPlume(device);
      grid = bakedPlume.grid;
      if (window.__frontierAtmosphere) window.__frontierAtmosphere.grid = grid;
      simulationTime = 0;
      const frame = bakedPlume.getFrame(simulationTime);
      renderer.bindField(frame);
      camera.target[0] = 0;
      camera.target[1] = 15;
      camera.target[2] = 0;
      camera.targetDistance = 38;
      setModeUI('baked');
      if (renderScale > 0) resolutionFor(renderScale);
      showEventToast(`Baked plume ready · ${bakedPlume.frameCount} frames · ${bakedPlume.grid.cells.toLocaleString()} cells read`, 2800);
    } catch (error) {
      showEventToast(`Baked plume could not load · ${error.message || error}`, 3600);
      console.error(error);
    } finally {
      button.classList.remove('is-loading');
      button.querySelector('.event-copy strong').textContent = originalLabel;
      bakedLoadPromise = null;
    }
  })();
  return bakedLoadPromise;
}

function showEventToast(message, duration = 2200) {
  const toast = $('#event-toast');
  toast.textContent = message;
  toast.classList.add('visible');
  window.clearTimeout(toast._timer);
  toast._timer = window.setTimeout(() => toast.classList.remove('visible'), duration);
}

function addEvent(kind, options = {}) {
  if (activeMode === 'baked') {
    showEventToast('Baked field is read-only · choose Use live to add emitters', 2400);
    return;
  }
  if (!solver) return;
  const source = solver.addSource(kind, spawn.x, spawn.z, options);
  updateSourcePanel();
  const label = kind === 'burst' ? 'Airburst queued' : 'Flame source ignited';
  $('#event-toast').textContent = `${label} · ${source.x.toFixed(1)}, ${source.z.toFixed(1)} m`;
  $('#event-toast').classList.add('visible');
  window.setTimeout(() => $('#event-toast').classList.remove('visible'), 2200);
}

function startEmitterStressTest() {
  if (!solver || !engineReady) {
    stressTestQueued = true;
    $('#event-toast').textContent = 'Stress test queued · waiting for GPU initialization';
    $('#event-toast').classList.add('visible');
    return;
  }
  if (settings.fuelFeed < 0.8) {
    $('#fuel-feed').value = '0.8';
    updateOutputs();
  }
  const centerX = spawn.x;
  const centerZ = spawn.z;
  currentLodLevel = 0;
  solver.clear({ level: currentLodLevel, centerX, centerZ });
  refreshGridInfo();
  if (window.__frontierAtmosphere) window.__frontierAtmosphere.grid = grid;
  const goldenAngle = Math.PI * (3 - Math.sqrt(5));
  camera.target[0] = centerX;
  camera.target[2] = centerZ;
  camera.distance = 24;
  camera.targetDistance = 24;
  renderAccumulator = 0;

  for (let index = 0; index < MAX_EMITTERS; index++) {
    const radius = 2.2 + 9.2 * Math.sqrt((index + 0.5) / MAX_EMITTERS);
    const angle = index * goldenAngle;
    const x = centerX + Math.cos(angle) * radius;
    const z = centerZ + Math.sin(angle) * radius;
    solver.addSource('plume', x, z, {
      radius: 0.9 + (index % 4) * 0.1,
      intensity: 1.05,
      life: 90,
      height: 7.5,
      twist: 1.0 + (index % 3) * 0.2,
    });
  }

  updateSourcePanel();
  $('#event-toast').textContent = `${MAX_EMITTERS} plumes ignited · camera framed close · fuel feed ≥ 0.8`;
  $('#event-toast').classList.add('visible');
  window.setTimeout(() => $('#event-toast').classList.remove('visible'), 2600);
}

function updateSourcePanel() {
  if (!solver) return;
  sourceList.replaceChildren();
  if (activeMode === 'baked' && bakedPlume) {
    $('#source-count').textContent = `1 / 1`;
    $('#source-state').textContent = 'BAKED · READ ONLY';
    const row = document.createElement('div');
    row.className = 'source-row source-plume baked-source-row';
    const swatch = document.createElement('span');
    swatch.className = 'source-swatch';
    const details = document.createElement('span');
    details.className = 'source-copy';
    const title = document.createElement('strong');
    title.textContent = 'High-res baked plume';
    const sub = document.createElement('small');
    sub.textContent = `${bakedPlume.frameCount} cached frames · ${bakedPlume.grid.nx}×${bakedPlume.grid.ny}×${bakedPlume.grid.nz} cells`;
    details.append(title, sub);
    row.append(swatch, details);
    sourceList.append(row);
    return;
  }

  const sources = solver.sources;
  $('#source-count').textContent = `${sources.length} / ${MAX_EMITTERS}`;
  $('#source-state').textContent = sources.length ? 'ACTIVE FIELD' : 'NO LIVE EMITTERS';
  if (!sources.length) {
    const empty = document.createElement('p');
    empty.className = 'empty-source';
    empty.textContent = 'The gas field is clear. Add an airburst or ignite a plume.';
    sourceList.append(empty);
    return;
  }

  for (const source of sources) {
    const row = document.createElement('div');
    row.className = `source-row ${source.kind === 'burst' ? 'source-burst' : 'source-plume'}`;
    const swatch = document.createElement('span');
    swatch.className = 'source-swatch';
    const details = document.createElement('span');
    details.className = 'source-copy';
    const title = document.createElement('strong');
    title.textContent = source.kind === 'burst' ? 'Airburst' : 'Burning plume';
    const sub = document.createElement('small');
    sub.textContent = `${source.x.toFixed(1)}, ${source.z.toFixed(1)} m · ${source.age.toFixed(1)} s`;
    const remove = document.createElement('button');
    remove.type = 'button';
    remove.className = 'remove-source';
    remove.setAttribute('aria-label', `Remove ${title.textContent}`);
    remove.textContent = '×';
    remove.addEventListener('click', () => {
      solver.removeSource(source.id);
      updateSourcePanel();
    });
    details.append(title, sub);
    row.append(swatch, details, remove);
    sourceList.append(row);
  }
}

function setPlaceMode(enabled) {
  placeMode = enabled;
  const button = $('#place-toggle');
  button.classList.toggle('selected', enabled);
  button.setAttribute('aria-pressed', String(enabled));
  button.innerHTML = enabled
    ? '<span class="button-icon">＋</span><span>Click a point on the field</span>'
    : '<span class="button-icon">⌖</span><span>Place emitter on field</span>';
  canvas.classList.toggle('placing', enabled);
}

function placeFromPointer(event) {
  const rect = canvas.getBoundingClientRect();
  const ray = rayFromScreen(camera, event.clientX - rect.left, event.clientY - rect.top, rect.width, rect.height);
  const point = intersectGround(ray);
  if (!point) return;
  setSpawn(point[0], point[2]);
  camera.target[0] = spawn.x;
  camera.target[2] = spawn.z;
  setPlaceMode(false);
}

function registerCanvasControls() {
  let pointer = null;
  canvas.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) return;
    pointer = { id: event.pointerId, x: event.clientX, y: event.clientY, moved: false };
    canvas.setPointerCapture(event.pointerId);
    canvas.classList.add('dragging');
  });
  canvas.addEventListener('pointermove', (event) => {
    if (!pointer || pointer.id !== event.pointerId) return;
    const dx = event.clientX - pointer.x;
    const dy = event.clientY - pointer.y;
    if (Math.abs(event.clientX - pointer.x) + Math.abs(event.clientY - pointer.y) > 3) pointer.moved = true;
    if (pointer.moved && !placeMode) {
      camera.yaw -= dx * 0.0052;
      camera.pitch = clamp(camera.pitch + dy * 0.0041, 0.055, 0.92);
    }
    pointer.x = event.clientX;
    pointer.y = event.clientY;
  });
  const finishPointer = (event) => {
    if (!pointer || pointer.id !== event.pointerId) return;
    if (!pointer.moved && placeMode) placeFromPointer(event);
    pointer = null;
    canvas.classList.remove('dragging');
  };
  canvas.addEventListener('pointerup', finishPointer);
  canvas.addEventListener('pointercancel', finishPointer);
  canvas.addEventListener('wheel', (event) => {
    camera.targetDistance = clamp(camera.targetDistance + event.deltaY * 0.025, 18, 160);
    event.preventDefault();
  }, { passive: false });
  window.addEventListener('resize', () => { renderScale = -1; });
}

function registerUI() {
  for (const input of $$('[data-param]')) {
    input.addEventListener('input', updateOutputs);
  }
  for (const input of $$('[data-color-param]')) {
    input.addEventListener('input', updateColorOutputs);
  }
  updateOutputs();
  updateColorOutputs();

  $('#airburst').addEventListener('click', () => addEvent('burst'));
  $('#ignite-plume').addEventListener('click', () => addEvent('plume'));
  $('#stress-test').addEventListener('click', startEmitterStressTest);
  $('#load-baked-plume').addEventListener('click', loadBakedSimulation);
  $('#live-mode').addEventListener('click', () => setLiveMode(true));
  $('#place-toggle').addEventListener('click', () => setPlaceMode(!placeMode));
  $('#extinguish').addEventListener('click', () => {
    if (!solver || activeMode === 'baked') return;
    solver.sources.length = 0;
    updateSourcePanel();
    $('#event-toast').textContent = 'Emitters stopped · suspended smoke is left to drift';
    $('#event-toast').classList.add('visible');
    window.setTimeout(() => $('#event-toast').classList.remove('visible'), 2400);
  });
  $('#clear-field').addEventListener('click', () => {
    if (!solver || activeMode === 'baked') {
      showEventToast('Baked field is read-only · choose Use live to clear the solver', 2400);
      return;
    }
    solver.clear();
    refreshGridInfo();
    if (window.__frontierAtmosphere) window.__frontierAtmosphere.grid = grid;
    updateSourcePanel();
    $('#event-toast').textContent = 'Field cleared · local volumes reset';
    $('#event-toast').classList.add('visible');
    window.setTimeout(() => $('#event-toast').classList.remove('visible'), 2200);
  });
  $('#restore-defaults').addEventListener('click', () => {
    for (const input of $$('[data-param]')) input.value = DEFAULTS[input.dataset.param];
    for (const input of $$('[data-color-param]')) input.value = rgbToHex(DEFAULTS[input.dataset.colorParam]);
    updateOutputs();
    updateColorOutputs();
  });
  for (const button of $$('.distance-button')) {
    button.addEventListener('click', () => {
      const distance = Number(button.dataset.distance);
      camera.targetDistance = distance;
    });
  }
  const toggleHelp = () => $('#help-card').classList.toggle('help-collapsed');
  $('#help-toggle').addEventListener('click', toggleHelp);
  $('#help-toggle-inner').addEventListener('click', toggleHelp);
  $('#controls-toggle').addEventListener('click', (event) => {
    const open = document.body.classList.toggle('show-controls');
    event.currentTarget.setAttribute('aria-expanded', String(open));
  });
  registerCanvasControls();
  setModeUI('live');
}

function updateTelemetry(fps, hz, lod) {
  $('#fps-value').textContent = fps.toFixed(0);
  $('#sim-hz-value').textContent = activeMode === 'baked' ? 'READ' : `${hz.toFixed(0)} Hz`;
  $('#lod-value').textContent = lod.name;
  $('#lod-distance').textContent = lodDescription(lod);
  $('#lod-value').dataset.lod = lod.name.toLowerCase();
  updateGridUI();
  $('#adapter-value').textContent = adapterLabel || 'WebGPU';
  const memoryMiB = (renderer.estimatedVramBytes / (1024 * 1024)).toFixed(0);
  $('#vram-value').textContent = `≈ ${memoryMiB} / 250 MiB app estimate`;
  $('#source-count').textContent = activeMode === 'baked' ? '1 / 1' : `${solver.sources.length} / ${MAX_EMITTERS}`;
  $('#engine-status').textContent = activeMode === 'baked' ? 'BAKED · READ ONLY' : 'COMPUTE LIVE';
  $('.engine-badge').classList.remove('status-error');
  $$('.distance-button').forEach((button) => {
    const activeLevel = lod.name.toLowerCase();
    button.classList.toggle('selected', button.dataset.view === activeLevel);
  });
}

async function start() {
  registerUI();
  setSpawn(0, 0);
  if (!('gpu' in navigator)) {
    showError('This browser does not expose WebGPU. Open the simulation in a current Chromium-based browser with hardware acceleration enabled.');
    return;
  }

  try {
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('No WebGPU adapter is available. Check browser GPU settings and hardware acceleration.');
    try {
      const info = adapter.info || (await adapter.requestAdapterInfo?.());
      if (info) adapterLabel = [info.vendor, info.architecture, info.description].filter(Boolean).join(' · ') || 'WebGPU adapter';
    } catch {
      adapterLabel = 'WebGPU adapter';
    }
    device = await adapter.requestDevice({ label: 'frontier-atmosphere-engine' });
    device.addEventListener('uncapturederror', (event) => showError(`GPU validation error: ${event.error.message}`));
    device.lost.then((info) => {
      if (info.reason !== 'destroyed') showError(`GPU device lost: ${info.message || info.reason || 'unknown reason'}`);
    });

    context = canvas.getContext('webgpu');
    if (!context) throw new Error('The browser could not create a WebGPU canvas context.');
    const format = navigator.gpu.getPreferredCanvasFormat();
    context.configure({ device, format, alphaMode: 'opaque' });

    solver = new FluidSolver(device, validateShader);
    await solver.ready;
    renderer = new VolumeRenderer(device, format, solver, validateShader);
    await renderer.ready;

    // A small starter fire and a single, finite ignition pulse make the field legible on first load.
    solver.addSource('plume', -2.1, -0.6, { radius: 0.9, intensity: 0.72, life: 90, height: 7.5 });
    solver.addSource('burst', 2.2, 0.35, { age: 0.035, radius: 1.35, intensity: 1.35, life: 1.35 });
    grid = solver.grid;
    updateGridUI();
    updateSourcePanel();
    $('#gpu-error').hidden = true;
    $('#engine-status').textContent = 'COMPUTE LIVE';
    $('#adapter-value').textContent = adapterLabel;
    engineReady = true;
    if (bakedLoadQueued) {
      bakedLoadQueued = false;
      loadBakedSimulation();
    }
    if (stressTestQueued) {
      stressTestQueued = false;
      startEmitterStressTest();
    }

    let fpsTimer = 0;
    let frames = 0;
    let ticks = 0;
    const frame = (now) => {
      const realDt = previousFrame ? Math.min((now - previousFrame) / 1000, 0.12) : FIXED_STEP;
      previousFrame = now;
      camera.distance += (camera.targetDistance - camera.distance) * Math.min(1, realDt * 5.5);
      accumulator = Math.min(accumulator + realDt, 0.25);
      const stepCount = Math.min(Math.floor(accumulator / FIXED_STEP), 4);
      accumulator -= stepCount * FIXED_STEP;
      if (activeMode === 'live') ticks += stepCount;

      const lod = chooseLod();
      let shouldRender = true;
      if (lod.renderHz < 60) {
        renderAccumulator += realDt;
        const renderInterval = 1 / lod.renderHz;
        shouldRender = renderAccumulator >= renderInterval;
        if (shouldRender) renderAccumulator %= renderInterval;
      } else {
        renderAccumulator = 0;
      }
      if (Math.abs(renderScale - lod.scale) > 0.001) {
        renderScale = lod.scale;
        resolutionFor(renderScale);
      }

      const encoder = device.createCommandEncoder({ label: 'frontier-atmosphere-frame' });
      if (activeMode === 'live') {
        const regridded = solver.regrid(encoder, lod.level, settings.wind);
        if (regridded) {
          refreshGridInfo();
          if (window.__frontierAtmosphere) window.__frontierAtmosphere.grid = grid;
          updateGridUI();
        }
        setLodUI(lod);
        solver.encode(encoder, stepCount, settings);
        simulationTime = solver.time;
      } else if (bakedPlume) {
        simulationTime = (simulationTime + realDt) % bakedPlume.duration;
        const bakedFrame = bakedPlume.getFrame(simulationTime);
        if (renderer.activeField !== bakedFrame) renderer.bindField(bakedFrame);
        setLodUI(lod);
      }
      if (shouldRender) {
        renderer.updateCamera(camera, canvas.width, canvas.height, simulationTime, lod, settings);
        renderer.draw(encoder, context.getCurrentTexture().createView());
      }
      device.queue.submit([encoder.finish()]);

      if (shouldRender) frames++;
      fpsTimer += realDt;
      panelClock += realDt;
      if (panelClock > 0.28) {
        updateSourcePanel();
        panelClock = 0;
      }
      if (fpsTimer >= 0.55) {
        const fps = frames / fpsTimer;
        const hz = ticks / fpsTimer;
        updateTelemetry(fps, hz, lod);
        fpsTimer = 0;
        frames = 0;
        ticks = 0;
      }
      requestAnimationFrame(frame);
    };
    requestAnimationFrame(frame);
    window.__frontierAtmosphere = { device, solver, renderer, camera, grid };
  } catch (error) {
    showError(error?.stack || error?.message || String(error));
  }
}

function $$(selector) {
  return Array.from(document.querySelectorAll(selector));
}

start();
