import {
  buildBvh,
  buildControlVehicle,
  buildWorldDistanceBvh,
  buildWorldGeometry,
  scaleWorldEmitterGeometry,
  createVehicleSdfMetadata,
  CITY_BLOCK_SIZE,
  GLOBAL_FIELD_RESOLUTION,
  MAX_SURFACE_CARDS,
  SURFACE_CARD_FLOATS,
  SURFACE_CARD_RESOLUTION,
  SURFACE_CARD_TEXELS,
  VEHICLE_SDF_DIMS,
  VEHICLE_VERTEX_STRIDE,
  WORLD_LENGTH,
  WORLD_WIDTH,
} from './scene.mjs';
import { buildEmissiveSourceBuffer } from './emissive.mjs';
import {
  BAKE_WGSL,
  COMPOSE_WGSL,
  DENOISE_WGSL,
  FIELD_MAP_WGSL,
  GI_WGSL,
  GBUFFER_WGSL,
  PRESENT_WGSL,
  TEMPORAL_WGSL,
  SHADE_WGSL,
  SURFACE_CACHE_WGSL,
  UPDATE_GDF_WGSL,
} from './shaders.mjs';

const GRID_RESOLUTION = GLOBAL_FIELD_RESOLUTION;
const MAX_STATIC_SHAPES = 55;
const SURFACE_CACHE_UPDATE_LIMIT = 64;
const SURFACE_CACHE_DYNAMIC_RADIUS = 24;
const CASCADE_EXTENTS = [12, 36, 108];
const FIELD_DEPTH = GRID_RESOLUTION * 3;
const FRAME_FLOATS = 152;
const FRAME_BYTES = FRAME_FLOATS * Float32Array.BYTES_PER_ELEMENT;
const RENDER_MODES = {
  gi: 0,
  field: 1,
  direct: 2,
  indirect: 3,
  normals: 4,
  albedo: 5,
  ao: 6,
  influence: 7,
};
const QUALITY = {
  fast: { scale: 0.38 },
  balanced: { scale: 0.56 },
  high: { scale: 0.70 },
};
const START_POSITION = { x: -3.2, z: -6.0, yaw: -Math.PI * 0.5 };
const WORLD_BOUNDARY_MARGIN = 3.0;
const MAX_FORWARD_SPEED = 32;
const MAX_REVERSE_SPEED = 8;
const TAU = Math.PI * 2;
const GPU_TIMING_PASSES = [
  { key: 'gdf', label: 'GDF update' },
  { key: 'surfaceCache', label: 'Surface cache' },
  { key: 'gbuffer', label: 'Primary G-buffer' },
  { key: 'cardId', label: 'Card-ID pass' },
  { key: 'direct', label: 'Direct lighting' },
  { key: 'gi', label: 'Diffuse GI' },
  { key: 'compose', label: 'Compose' },
  { key: 'temporal', label: 'Temporal' },
  { key: 'denoise', label: 'Denoise' },
  { key: 'present', label: 'Present' },
  { key: 'fieldMap', label: 'Field slice' },
];
const GPU_TIMING_PASS_INDEX = Object.fromEntries(GPU_TIMING_PASSES.map((pass, index) => [pass.key, index]));
const GPU_TIMING_QUERY_COUNT = GPU_TIMING_PASSES.length * 2;
const GPU_TIMING_BUFFER_SIZE = GPU_TIMING_QUERY_COUNT * BigUint64Array.BYTES_PER_ELEMENT;
const GPU_TIMING_RING_SIZE = 3;
const GPU_TIMING_LOG_LIMIT = 120;

const $ = selector => document.querySelector(selector);
const ui = {
  canvas: $('#viewCanvas'),
  clipmapOverlay: $('#clipmapOverlay'),
  fieldCanvas: $('#fieldCanvas'),
  status: $('#gpuStatus'),
  adapter: $('#adapterName'),
  loading: $('#loadingText'),
  error: $('#errorPanel'),
  errorTitle: $('#errorTitle'),
  errorDetails: $('#errorDetails'),
  renderMode: $('#renderMode'),
  clipmapBounds: $('#clipmapBounds'),
  fieldCardKicker: $('#fieldCardKicker'),
  fieldCardTitle: $('#fieldCardTitle'),
  fieldSliceLegend: $('#fieldSliceLegend'),
  quality: $('#quality'),
  temporalCache: $('#temporalCache'),
  autoDrive: $('#autoDrive'),
  driveSpeed: $('#driveSpeed'),
  driveSpeedValue: $('#driveSpeedValue'),
  giStrength: $('#giStrength'),
  giStrengthValue: $('#giStrengthValue'),
  giRays: $('#giRays'),
  giRaysValue: $('#giRaysValue'),
  skyStrength: $('#skyStrength'),
  skyStrengthValue: $('#skyStrengthValue'),
  aoStrength: $('#aoStrength'),
  aoStrengthValue: $('#aoStrengthValue'),
  bounceCount: $('#bounceCount'),
  sunIntensity: $('#sunIntensity'),
  sunIntensityValue: $('#sunIntensityValue'),
  sunAngularRadius: $('#sunAngularRadius'),
  sunAngularRadiusValue: $('#sunAngularRadiusValue'),
  pointLightIntensity: $('#pointLightIntensity'),
  pointLightIntensityValue: $('#pointLightIntensityValue'),
  pointLightRadius: $('#pointLightRadius'),
  pointLightRadiusValue: $('#pointLightRadiusValue'),
  emissiveIntensity: $('#emissiveIntensity'),
  emissiveIntensityValue: $('#emissiveIntensityValue'),
  emitterSize: $('#emitterSize'),
  emitterSizeValue: $('#emitterSizeValue'),
  emissiveSamples: $('#emissiveSamples'),
  emissiveSamplesValue: $('#emissiveSamplesValue'),
  pauseButton: $('#pauseButton'),
  resetButton: $('#resetButton'),
  fieldToggle: $('#fieldToggle'),
  fieldCard: $('#fieldCard'),
  fps: $('#fpsValue'),
  frameMs: $('#frameValue'),
  speed: $('#hudSpeed'),
  position: $('#hudPosition'),
  fieldCells: $('#fieldCells'),
  fieldVoxel: $('#fieldVoxel'),
  driveMode: $('#driveMode'),
  fieldLegend: $('#fieldLegend'),
  gpuTimingPanel: $('#gpuTimingPanel'),
  gpuTimeValue: $('#gpuTimeValue'),
  gpuTimingStatus: $('#gpuTimingStatus'),
  gpuTimingRows: [...document.querySelectorAll('[data-gpu-pass]')],
  copyGpuTimingLog: $('#copyGpuTimingLog'),
  gpuTimingLogStatus: $('#gpuTimingLogStatus'),
  gpuTimingLog: $('#gpuTimingLog'),
};

const state = {
  adapter: null,
  device: null,
  canvasContext: null,
  overlayContext: null,
  fieldContext: null,
  format: null,
  gpuTimingSupported: false,
  gpuTimingQuerySet: null,
  gpuTimingSlots: [],
  gpuTimingSlotCursor: 0,
  gpuTimingCurrentSlot: null,
  gpuTimingPassUsed: new Uint8Array(GPU_TIMING_PASSES.length),
  gpuTimingLatest: null,
  gpuTimingLatestFrame: 0,
  gpuTimingHistory: [],
  pipelines: {},
  bindGroups: {},
  textures: {},
  views: {},
  buffers: {},
  vehicle: null,
  world: null,
  worldStreamWorker: null,
  streamRequestId: 0,
  pendingStreamCell: null,
  emitterRebuildTimer: null,
  worldGdfRebuildPending: false,
  worldTriangleCount: 0,
  worldBvhNodeCount: 0,
  gdfCenters: null,
  gdfAllDirty: true,
  gdfDirtyMask: 0,
  gdfPendingMask: 0,
  gdfReadyMask: 0,
  gdfForceMask: 0,
  gdfNextCascade: 0,
  bvh: null,
  emissiveSourceCount: 0,
  surfaceCacheGeneration: 1,
  surfaceCacheCardCount: 0,
  surfaceCacheValid: null,
  surfaceCacheCursor: 0,
  surfaceCacheUpdateCount: 0,
  surfaceCacheUpdateList: new Uint32Array(SURFACE_CACHE_UPDATE_LIMIT),
  shapeCount: 0,
  shapeCapacity: 0,
  frameData: new Float32Array(FRAME_FLOATS),
  car: { ...START_POSITION, speed: 0 },
  previousCar: { ...START_POSITION },
  autoDrive: false,
  autoYaw: START_POSITION.yaw,
  manualSpeed: 0,
  steeringInput: 0,
  wheelRotation: 0,
  wheelSteering: 0,
  focus: [START_POSITION.x, 1.25, START_POSITION.z],
  camera: null,
  cameraTarget: null,
  viewProjection: null,
  clipmapCenters: null,
  clipmapTargets: null,
  showClipmapBounds: true,
  orbitYaw: 0.12,
  orbitPitch: 0.28,
  orbitDistance: 22,
  drag: null,
  keys: new Set(),
  mode: 'gi',
  quality: 'balanced',
  giStrength: 1.15,
  giRays: 4,
  skyStrength: 0.32,
  aoStrength: 0.45,
  bounces: 2,
  sunIntensity: 1.0,
  sunAngularRadius: 0.9,
  pointLightIntensity: 0,
  pointLightRadius: 0.8,
  pointLightPosition: [0, 6.5, 0],
  temporalEnabled: true,
  historyValid: false,
  historyReadIndex: 0,
  previousViewProjection: null,
  lastMode: 'gi',
  emissiveIntensity: 2.5,
  emitterSize: 1.35,
  emissiveSamples: 8,
  paused: false,
  showField: true,
  fieldSlice: 0.92,
  frameIndex: 0,
  lastFrameTime: 0,
  elapsed: 0,
  fpsStart: 0,
  fpsFrames: 0,
  fps: 0,
  lastHudUpdate: 0,
  targetWidth: 0,
  targetHeight: 0,
  renderWidth: 0,
  renderHeight: 0,
  renderScale: QUALITY.balanced.scale,
  started: false,
};

function showError(title, details) {
  ui.error.hidden = false;
  ui.errorTitle.textContent = title;
  ui.errorDetails.textContent = details;
  ui.loading.textContent = 'WebGPU initialization failed';
  ui.status.textContent = 'UNAVAILABLE';
  ui.status.dataset.state = 'error';
  ui.gpuTimeValue.textContent = '—';
  ui.gpuTimingStatus.textContent = 'unavailable';
  ui.gpuTimingLogStatus.dataset.state = 'unavailable';
  ui.gpuTimingLogStatus.textContent = 'WebGPU unavailable';
  ui.gpuTimingPanel.title = 'GPU timing is unavailable because WebGPU could not be initialized.';
}

function setLoading(message) {
  ui.loading.textContent = message;
}

function alignTo(value, multiple = 4) {
  return Math.ceil(value / multiple) * multiple;
}

function makeBuffer(device, data, usage, label) {
  const source = data instanceof ArrayBuffer
    ? new Uint8Array(data)
    : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  const size = Math.max(4, alignTo(source.byteLength, 4));
  const buffer = device.createBuffer({ label, size, usage: usage | GPUBufferUsage.COPY_DST });
  if (source.byteLength) device.queue.writeBuffer(buffer, 0, source);
  return buffer;
}

function makeUniformBuffer(device, size, label) {
  return device.createBuffer({
    label,
    size: alignTo(size, 16),
    usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
  });
}

function initializeGpuTiming(device) {
  if (!device.features?.has('timestamp-query')) {
    ui.gpuTimingStatus.textContent = 'unsupported';
    ui.gpuTimingLogStatus.dataset.state = 'unavailable';
    ui.gpuTimingLogStatus.textContent = 'timestamp-query unavailable';
    ui.gpuTimingPanel.title = 'This adapter does not expose the optional timestamp-query feature.';
    return;
  }
  try {
    state.gpuTimingQuerySet = device.createQuerySet({
      label: 'Per-pass GPU timestamp queries',
      type: 'timestamp',
      count: GPU_TIMING_QUERY_COUNT,
    });
    state.gpuTimingSlots = Array.from({ length: GPU_TIMING_RING_SIZE }, (_, index) => ({
      busy: false,
      resolveBuffer: device.createBuffer({
        label: `GPU timing resolve buffer ${index}`,
        size: GPU_TIMING_BUFFER_SIZE,
        usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC,
      }),
      readBuffer: device.createBuffer({
        label: `GPU timing asynchronous readback ${index}`,
        size: GPU_TIMING_BUFFER_SIZE,
        usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
      }),
    }));
    state.gpuTimingSupported = true;
    ui.gpuTimingStatus.textContent = 'waiting';
    ui.gpuTimingPanel.title = 'Asynchronous GPU timestamps; pass sum excludes queue idle and CPU time.';
  } catch (error) {
    console.warn('GPU timestamp queries could not be initialized:', error);
    state.gpuTimingSupported = false;
    state.gpuTimingQuerySet = null;
    state.gpuTimingSlots = [];
    ui.gpuTimingStatus.textContent = 'unavailable';
    ui.gpuTimingLogStatus.dataset.state = 'unavailable';
    ui.gpuTimingLogStatus.textContent = 'GPU timing unavailable';
    ui.gpuTimingPanel.title = 'Timestamp query setup failed on this adapter.';
  }
}

function prepareGpuTimingFrame() {
  state.gpuTimingPassUsed.fill(0);
  state.gpuTimingCurrentSlot = null;
  if (!state.gpuTimingSupported) return;
  for (let offset = 0; offset < state.gpuTimingSlots.length; offset++) {
    const index = (state.gpuTimingSlotCursor + offset) % state.gpuTimingSlots.length;
    const slot = state.gpuTimingSlots[index];
    if (slot.busy) continue;
    state.gpuTimingCurrentSlot = slot;
    state.gpuTimingSlotCursor = (index + 1) % state.gpuTimingSlots.length;
    return;
  }
}

function gpuTimestampWrites(passKey) {
  if (!state.gpuTimingCurrentSlot) return undefined;
  const passIndex = GPU_TIMING_PASS_INDEX[passKey];
  if (passIndex === undefined) return undefined;
  state.gpuTimingPassUsed[passIndex] = 1;
  return {
    querySet: state.gpuTimingQuerySet,
    beginningOfPassWriteIndex: passIndex * 2,
    endOfPassWriteIndex: passIndex * 2 + 1,
  };
}

function encodeGpuTimingReadback(encoder) {
  const slot = state.gpuTimingCurrentSlot;
  state.gpuTimingCurrentSlot = null;
  if (!slot) return null;
  const usedPasses = GPU_TIMING_PASSES.filter((pass, index) => state.gpuTimingPassUsed[index]);
  if (!usedPasses.length) return null;
  for (const pass of usedPasses) {
    const firstQuery = GPU_TIMING_PASS_INDEX[pass.key] * 2;
    const byteOffset = firstQuery * BigUint64Array.BYTES_PER_ELEMENT;
    encoder.resolveQuerySet(state.gpuTimingQuerySet, firstQuery, 2, slot.resolveBuffer, 0);
    encoder.copyBufferToBuffer(slot.resolveBuffer, 0, slot.readBuffer, byteOffset, 2 * BigUint64Array.BYTES_PER_ELEMENT);
  }
  return {
    slot,
    usedPasses,
    frame: state.frameIndex,
    snapshot: {
      mode: state.mode,
      quality: state.quality,
      giRays: state.giRays,
      bounces: state.bounces,
      renderWidth: state.renderWidth,
      renderHeight: state.renderHeight,
      targetWidth: state.targetWidth,
      targetHeight: state.targetHeight,
      rafFps: state.fps || null,
      rafCadenceMs: state.fps ? 1000 / state.fps : null,
    },
  };
}

function readGpuTimingResults(submission) {
  const { slot, usedPasses, frame, snapshot } = submission;
  slot.busy = true;
  slot.readBuffer.mapAsync(GPUMapMode.READ).then(() => {
    const timestamps = new BigUint64Array(slot.readBuffer.getMappedRange());
    const timings = {};
    for (const pass of usedPasses) {
      const queryIndex = GPU_TIMING_PASS_INDEX[pass.key] * 2;
      const start = timestamps[queryIndex];
      const end = timestamps[queryIndex + 1];
      if (end >= start) timings[pass.key] = Number(end - start) / 1_000_000;
    }
    slot.readBuffer.unmap();
    const gpuPassSumMs = Object.values(timings).reduce((sum, milliseconds) => sum + milliseconds, 0);
    state.gpuTimingHistory.push({ frame, ...snapshot, timings, gpuPassSumMs });
    if (state.gpuTimingHistory.length > GPU_TIMING_LOG_LIMIT) {
      state.gpuTimingHistory.sort((a, b) => a.frame - b.frame);
      state.gpuTimingHistory.splice(0, state.gpuTimingHistory.length - GPU_TIMING_LOG_LIMIT);
    }
    if (ui.gpuTimingLogStatus.dataset.state === 'collecting') {
      ui.gpuTimingLogStatus.dataset.state = 'ready';
      ui.gpuTimingLogStatus.textContent = 'GPU timing log ready';
    }
    if (frame >= state.gpuTimingLatestFrame) {
      state.gpuTimingLatest = timings;
      state.gpuTimingLatestFrame = frame;
    }
  }).catch(error => {
    console.warn('GPU timestamp readback failed; disabling per-pass timing:', error);
    state.gpuTimingSupported = false;
    ui.gpuTimingStatus.textContent = 'unavailable';
    ui.gpuTimingLogStatus.dataset.state = 'unavailable';
    ui.gpuTimingLogStatus.textContent = 'GPU timing readback failed';
    ui.gpuTimingPanel.title = 'GPU timestamp readback failed; see the console for details.';
    if (slot.readBuffer.mapState === 'mapped') slot.readBuffer.unmap();
  }).finally(() => {
    slot.busy = false;
  });
}

function updateGpuTimingHud() {
  if (!state.gpuTimingSupported) {
    ui.gpuTimeValue.textContent = '—';
    for (const row of ui.gpuTimingRows) row.textContent = '—';
    return;
  }
  if (!state.gpuTimingLatest) return;
  let total = 0;
  for (const row of ui.gpuTimingRows) {
    const milliseconds = state.gpuTimingLatest[row.dataset.gpuPass];
    row.textContent = Number.isFinite(milliseconds) ? milliseconds.toFixed(2) : '—';
    if (Number.isFinite(milliseconds)) total += milliseconds;
  }
  ui.gpuTimeValue.textContent = total.toFixed(2);
  ui.gpuTimingStatus.textContent = 'ms';
  ui.gpuTimingPanel.title = `Asynchronous GPU pass sum from frame ${state.gpuTimingLatestFrame}; queue idle and CPU time are excluded.`;
}

function formatGpuTimingLog() {
  const samples = state.gpuTimingHistory
    .slice()
    .sort((a, b) => a.frame - b.frame)
    .slice(-GPU_TIMING_LOG_LIMIT);
  const passColumns = GPU_TIMING_PASSES.map(({ key }) => `${key.replace(/[A-Z]/g, letter => `_${letter.toLowerCase()}`)}_ms`);
  const columns = [
    'frame', 'mode', 'quality', 'render_width', 'render_height', 'target_width', 'target_height',
    'gi_rays', 'bounces', 'raf_fps', 'raf_cadence_ms', 'gpu_pass_sum_ms', ...passColumns,
  ];
  const csvCell = value => `"${String(value ?? 'NA').replace(/"/g, '""')}"`;
  const fixed = (value, digits) => Number.isFinite(value) ? value.toFixed(digits) : 'NA';
  const rows = samples.map(sample => [
    sample.frame,
    sample.mode,
    sample.quality,
    sample.renderWidth,
    sample.renderHeight,
    sample.targetWidth,
    sample.targetHeight,
    sample.giRays,
    sample.bounces,
    fixed(sample.rafFps, 1),
    fixed(sample.rafCadenceMs, 2),
    fixed(sample.gpuPassSumMs, 3),
    ...GPU_TIMING_PASSES.map(pass => fixed(sample.timings[pass.key], 3)),
  ].map(csvCell).join(','));
  const adapter = (ui.adapter.textContent || 'unknown').replace(/[\r\n]+/g, ' ');
  return [
    '# Frontier GPU timing log',
    `# generated_utc=${new Date().toISOString()}`,
    `# adapter=${JSON.stringify(adapter)}`,
    '# GPU times are pass durations in ms; pass sum excludes CPU time and queue idle, and is not end-to-end frame time.',
    '# RAF fields are browser cadence, not GPU time. NA means the pass was skipped; frame gaps can occur during async readback.',
    `# samples=${samples.length}; maximum_recent_samples=${GPU_TIMING_LOG_LIMIT}`,
    ...(samples.length ? [] : ['# No GPU timing samples captured yet.']),
    columns.join(','),
    ...rows,
  ].join('\n');
}

async function copyGpuTimingLog() {
  if (!state.gpuTimingHistory.length) {
    ui.gpuTimingLogStatus.dataset.state = state.gpuTimingSupported ? 'collecting' : 'unavailable';
    ui.gpuTimingLogStatus.textContent = state.gpuTimingSupported
      ? 'Waiting for the first GPU sample.'
      : 'GPU timestamp-query is unavailable.';
    return;
  }
  const log = formatGpuTimingLog();
  const sampleCount = Math.min(state.gpuTimingHistory.length, GPU_TIMING_LOG_LIMIT);
  ui.gpuTimingLog.value = log;
  try {
    if (!navigator.clipboard?.writeText) throw new Error('Clipboard API unavailable.');
    await navigator.clipboard.writeText(log);
    ui.gpuTimingLog.hidden = true;
    ui.gpuTimingLogStatus.dataset.state = 'copied';
    ui.gpuTimingLogStatus.textContent = `Copied ${sampleCount} samples. Paste into chat.`;
  } catch (error) {
    console.warn('Clipboard write failed; exposing the GPU timing log for manual copy:', error);
    ui.gpuTimingLog.hidden = false;
    ui.gpuTimingLog.focus();
    ui.gpuTimingLog.select();
    let copied = false;
    try {
      copied = document.execCommand('copy');
    } catch {
      // Manual selection remains available if clipboard access is denied.
    }
    if (copied) {
      ui.gpuTimingLog.hidden = true;
      ui.gpuTimingLogStatus.dataset.state = 'copied';
      ui.gpuTimingLogStatus.textContent = `Copied ${sampleCount} samples. Paste into chat.`;
    } else {
      ui.gpuTimingLogStatus.dataset.state = 'manual';
      ui.gpuTimingLogStatus.textContent = 'Log selected below; copy it and paste into chat.';
    }
  }
}

async function shaderModule(device, label, code) {
  const module = device.createShaderModule({ label, code });
  const compilation = await module.getCompilationInfo();
  const errors = compilation.messages.filter(message => message.type === 'error');
  if (errors.length) {
    const report = errors.map(error => `${label}:${error.lineNum}:${error.linePos} ${error.message}`).join('\n');
    throw new Error(report);
  }
  return module;
}

async function buildPipelines(device, format) {
  const [bakeModule, updateModule, gbufferModule, shadeModule, giModule, surfaceCacheModule, composeModule, temporalModule, denoiseModule, presentModule, fieldModule] = await Promise.all([
    shaderModule(device, 'ControlVehicle SDF bake', BAKE_WGSL),
    shaderModule(device, 'Global distance-field update', UPDATE_GDF_WGSL),
    shaderModule(device, 'World-space G-buffer', GBUFFER_WGSL),
    shaderModule(device, 'Finite-emitter and area-light direct lighting', SHADE_WGSL),
    shaderModule(device, 'Multi-bounce diffuse transport', GI_WGSL),
    shaderModule(device, 'World-space mesh-card surface cache', SURFACE_CACHE_WGSL),
    shaderModule(device, 'Lighting composition and view modes', COMPOSE_WGSL),
    shaderModule(device, 'Reprojected temporal surface cache', TEMPORAL_WGSL),
    shaderModule(device, 'Edge-aware GI denoiser', DENOISE_WGSL),
    shaderModule(device, 'Final image presentation', PRESENT_WGSL),
    shaderModule(device, 'Global distance-field slice', FIELD_MAP_WGSL),
  ]);
  // Keep the heavy direct-light and multi-bounce workloads in separate pipelines.
  // This avoids a single monolithic shader frame exceeding compiler stack limits,
  // while retaining both exact-area next-event sampling and full diffuse bounces.
  // Pipeline compilation is serialized to reduce peak compiler memory on software
  // and lower-end WebGPU adapters.
  const bake = await device.createComputePipelineAsync({
    label: 'Bake the ControlVehicle mesh distance field',
    layout: 'auto',
    compute: { module: bakeModule, entryPoint: 'main' },
  });
  const update = await device.createComputePipelineAsync({
    label: 'Rebuild the three global clipmaps',
    layout: 'auto',
    compute: { module: updateModule, entryPoint: 'main' },
  });
  const surfaceCache = await device.createComputePipelineAsync({
    label: 'Incrementally shade visible world-space mesh cards',
    layout: 'auto',
    compute: { module: surfaceCacheModule, entryPoint: 'main' },
  });
  const gbuffer = await device.createRenderPipelineAsync({
    label: 'Rasterize primary visibility and G-buffer',
    layout: 'auto',
    vertex: {
      module: gbufferModule,
      entryPoint: 'vertexMain',
      buffers: [{
        arrayStride: VEHICLE_VERTEX_STRIDE * 4,
        stepMode: 'vertex',
        attributes: [
          { shaderLocation: 0, offset: 0, format: 'float32x4' },
          { shaderLocation: 1, offset: 16, format: 'float32x3' },
          { shaderLocation: 2, offset: 32, format: 'float32x4' },
          { shaderLocation: 3, offset: 48, format: 'float32x4' },
        ],
      }],
    },
    fragment: {
      module: gbufferModule,
      entryPoint: 'fragmentMain',
      targets: [
        { format: 'rgba16float' },
        { format: 'rgba16float' },
        { format: 'rgba16float' },
        { format: 'rgba16float' },
      ],
    },
    primitive: { topology: 'triangle-list', cullMode: 'none' },
    depthStencil: { format: 'depth24plus', depthWriteEnabled: true, depthCompare: 'less' },
  });
  const cardId = await device.createRenderPipelineAsync({
    label: 'Rasterize compact surface-card IDs against primary depth',
    layout: 'auto',
    vertex: {
      module: gbufferModule,
      entryPoint: 'vertexMain',
      buffers: [{
        arrayStride: VEHICLE_VERTEX_STRIDE * 4,
        stepMode: 'vertex',
        attributes: [
          { shaderLocation: 0, offset: 0, format: 'float32x4' },
          { shaderLocation: 1, offset: 16, format: 'float32x3' },
          { shaderLocation: 2, offset: 32, format: 'float32x4' },
          { shaderLocation: 3, offset: 48, format: 'float32x4' },
        ],
      }],
    },
    fragment: {
      module: gbufferModule,
      entryPoint: 'cardIdFragmentMain',
      targets: [{ format: 'r16uint' }],
    },
    primitive: { topology: 'triangle-list', cullMode: 'none' },
    depthStencil: { format: 'depth24plus', depthWriteEnabled: false, depthCompare: 'equal' },
  });
  const shade = await device.createComputePipelineAsync({
    label: 'Area-light, emissive and sun direct lighting',
    layout: 'auto',
    compute: { module: shadeModule, entryPoint: 'main' },
  });
  const gi = await device.createComputePipelineAsync({
    label: 'Multi-bounce SDF diffuse path transport',
    layout: 'auto',
    compute: { module: giModule, entryPoint: 'main' },
  });
  const compose = await device.createComputePipelineAsync({
    label: 'Compose direct, indirect and view-mode lighting',
    layout: 'auto',
    compute: { module: composeModule, entryPoint: 'main' },
  });
  const denoise = await device.createComputePipelineAsync({
    label: 'Edge-aware, current-frame GI denoising',
    layout: 'auto',
    compute: { module: denoiseModule, entryPoint: 'main' },
  });
  const temporal = await device.createComputePipelineAsync({
    label: 'Reproject and accumulate stable-surface lighting',
    layout: 'auto',
    compute: { module: temporalModule, entryPoint: 'main' },
  });
  const present = await device.createRenderPipelineAsync({
    label: 'Upscale and present the lit image',
    layout: 'auto',
    vertex: { module: presentModule, entryPoint: 'vertexMain' },
    fragment: { module: presentModule, entryPoint: 'fragmentMain', targets: [{ format }] },
    primitive: { topology: 'triangle-list' },
  });
  const fieldMap = await device.createRenderPipelineAsync({
    label: 'Live top-down GDF slice',
    layout: 'auto',
    vertex: { module: fieldModule, entryPoint: 'vertexMain' },
    fragment: { module: fieldModule, entryPoint: 'fragmentMain', targets: [{ format }] },
    primitive: { topology: 'triangle-list' },
  });
  return { bake, update, gbuffer, cardId, shade, gi, surfaceCache, compose, temporal, denoise, present, fieldMap };
}

function createTexture(device, label, width, height, depth, format, usage) {
  return device.createTexture({
    label,
    size: [width, height, depth],
    dimension: depth > 1 ? '3d' : '2d',
    format,
    usage,
  });
}

function encodeShapeBuffer(shapes) {
  const data = new Float32Array(Math.max(MAX_STATIC_SHAPES, shapes.length) * 16);
  for (let index = 0; index < shapes.length; index++) {
    const shape = shapes[index];
    const yaw = shape.yaw || 0;
    const cosine = Math.abs(Math.cos(yaw));
    const sine = Math.abs(Math.sin(yaw));
    const boundsHalfX = cosine * shape.half[0] + sine * shape.half[2];
    const boundsHalfZ = sine * shape.half[0] + cosine * shape.half[2];
    const base = index * 16;
    data.set([
      shape.center[0], shape.center[1], shape.center[2], yaw,
      shape.half[0], shape.half[1], shape.half[2], 0,
      shape.color[0], shape.color[1], shape.color[2], shape.metallic,
      shape.roughness, shape.emission, boundsHalfX, boundsHalfZ,
    ], base);
  }
  return data;
}

function encodeWorldGdfParams(worldDistanceBvh) {
  return new Uint32Array([worldDistanceBvh.triangleCount, worldDistanceBvh.nodeCount, 0, 0]);
}

function updateEmitterGeometry() {
  if (!state.world || !state.buffers.worldVertices || !state.buffers.shapes) return;
  const vertices = scaleWorldEmitterGeometry(state.world, state.emitterSize);
  state.device.queue.writeBuffer(state.buffers.worldVertices, 0, vertices);
  state.device.queue.writeBuffer(state.buffers.shapes, 0, encodeShapeBuffer(state.world.shapes));
  // The alias table retains base-size triangles; sampled points and their area
  // PDF use this same scale. The full-mesh field is rebuilt off-thread as well.
  state.worldGdfRebuildPending = true;
  state.gdfAllDirty = true;
  state.gdfReadyMask = 0;
  state.gdfPendingMask = 0;
  state.gdfForceMask = 0;
  queueWorldGeometryRebuild();
}

function createBakeParams(vehicle, bvh) {
  const buffer = new ArrayBuffer(64);
  const floats = new Float32Array(buffer);
  const uints = new Uint32Array(buffer);
  floats.set([...vehicle.boundsMin, 0, ...vehicle.boundsMax, 0], 0);
  uints.set([...VEHICLE_SDF_DIMS, 0], 8);
  uints.set([vehicle.triangleCount, bvh.nodes.length, vehicle.groupCount, 0], 12);
  return buffer;
}

async function loadAssets(device) {
  setLoading('Loading the Project Drive ControlVehicle GLB…');
  const response = await fetch('./assets/ControlVehicle.glb');
  if (!response.ok) throw new Error(`ControlVehicle.glb returned HTTP ${response.status}.`);
  const vehicle = buildControlVehicle(await response.arrayBuffer());
  const bvh = buildBvh(vehicle.triangles);
  const world = buildWorldGeometry(state.car.x, state.car.z);
  const emissiveSources = buildEmissiveSourceBuffer(world, vehicle);
  const worldVertices = scaleWorldEmitterGeometry(world, state.emitterSize);
  const worldDistanceBvh = buildWorldDistanceBvh(worldVertices);

  state.vehicle = vehicle;
  state.bvh = bvh;
  state.world = world;
  state.streamCell = world.streamCell;
  state.buffers.vehicleVertices = makeBuffer(device, vehicle.vertexData, GPUBufferUsage.VERTEX, 'ControlVehicle vertex stream');
  state.buffers.worldVertices = makeBuffer(device, worldVertices, GPUBufferUsage.VERTEX, 'Streamed city streets, buildings and traffic');
  state.buffers.worldTriangles = makeBuffer(
    device, worldDistanceBvh.triangleBuffer, GPUBufferUsage.STORAGE, 'All streamed static-world mesh triangles',
  );
  state.buffers.worldNodes = makeBuffer(
    device, worldDistanceBvh.nodeBuffer, GPUBufferUsage.STORAGE, 'Static-world triangle BVH nodes',
  );
  state.buffers.worldGdfParams = makeBuffer(
    device, encodeWorldGdfParams(worldDistanceBvh), GPUBufferUsage.UNIFORM, 'Static-world triangle and BVH counts',
  );
  state.buffers.triangles = makeBuffer(device, bvh.triangleBuffer, GPUBufferUsage.STORAGE, 'ControlVehicle triangles with materials');
  state.buffers.nodes = makeBuffer(device, bvh.nodeBuffer, GPUBufferUsage.STORAGE, 'Stackless triangle BVH');
  state.buffers.shapes = makeBuffer(device, encodeShapeBuffer(world.shapes), GPUBufferUsage.STORAGE, 'Analytic static distance-field shapes');
  state.buffers.emissiveSources = makeBuffer(device, emissiveSources.buffer, GPUBufferUsage.STORAGE, 'Power-weighted emissive triangle alias table');
  state.emissiveSourceCount = emissiveSources.count;
  if (world.surfaceCardCount > MAX_SURFACE_CARDS) throw new Error('The streamed city exceeds the fixed surface-card capacity.');
  state.buffers.surfaceCards = device.createBuffer({
    label: 'World-space mesh-card surface parameters',
    size: MAX_SURFACE_CARDS * SURFACE_CARD_FLOATS * Float32Array.BYTES_PER_ELEMENT,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  state.buffers.surfaceRadiance = device.createBuffer({
    label: 'Persistent mesh-card direct and indirect radiance',
    size: MAX_SURFACE_CARDS * SURFACE_CARD_TEXELS * 8 * Float32Array.BYTES_PER_ELEMENT,
    usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
  });
  state.buffers.surfaceCardUpdateList = makeBuffer(
    device, state.surfaceCacheUpdateList, GPUBufferUsage.STORAGE, 'Visible surface cards queued for cache shading',
  );
  device.queue.writeBuffer(state.buffers.surfaceCards, 0, world.surfaceCards);
  state.surfaceCacheCardCount = world.surfaceCardCount;
  state.surfaceCacheValid = new Uint8Array(world.surfaceCardCount);
  state.buffers.assetParams = makeBuffer(device, createVehicleSdfMetadata(vehicle), GPUBufferUsage.UNIFORM, 'ControlVehicle SDF bounds');
  state.buffers.bakeParams = makeBuffer(device, createBakeParams(vehicle, bvh), GPUBufferUsage.UNIFORM, 'SDF bake parameters');
  state.vehicleVertexCount = vehicle.vertexData.length / VEHICLE_VERTEX_STRIDE;
  state.worldVertexCount = world.vertices.length / VEHICLE_VERTEX_STRIDE;
  state.worldTriangleCount = worldDistanceBvh.triangleCount;
  state.worldBvhNodeCount = worldDistanceBvh.nodeCount;
  state.shapeCount = world.shapes.length;
  state.shapeCapacity = MAX_STATIC_SHAPES;
  state.gdfAllDirty = true;
  return vehicle;
}

function installStreamedWorld(world, worldDistanceBvh = null) {
  const emissive = buildEmissiveSourceBuffer(world, state.vehicle);
  if (world.shapes.length > state.shapeCapacity || emissive.buffer.byteLength !== state.buffers.emissiveSources.size) {
    throw new Error('Streamed city chunk exceeded the fixed SDF or area-light buffer budget.');
  }
  if (world.surfaceCardCount > MAX_SURFACE_CARDS) {
    throw new Error('Streamed city exceeded the fixed mesh-card surface-cache capacity.');
  }

  const worldVertices = scaleWorldEmitterGeometry(world, state.emitterSize);
  worldDistanceBvh ||= buildWorldDistanceBvh(worldVertices);
  const obsoleteWorldBuffers = [
    state.buffers.worldVertices,
    state.buffers.worldTriangles,
    state.buffers.worldNodes,
  ].filter(Boolean);

  state.world = world;
  state.streamCell = world.streamCell;
  state.surfaceCacheCardCount = world.surfaceCardCount;
  state.surfaceCacheValid = new Uint8Array(world.surfaceCardCount);
  state.surfaceCacheCursor = 0;
  state.device.queue.writeBuffer(state.buffers.surfaceCards, 0, world.surfaceCards);
  invalidateSurfaceCache();
  state.worldVertexCount = worldVertices.length / VEHICLE_VERTEX_STRIDE;
  state.worldTriangleCount = worldDistanceBvh.triangleCount;
  state.worldBvhNodeCount = worldDistanceBvh.nodeCount;
  state.shapeCount = world.shapes.length;
  state.emissiveSourceCount = emissive.count;
  state.buffers.worldVertices = makeBuffer(
    state.device, worldVertices, GPUBufferUsage.VERTEX, 'Streamed city streets, buildings and traffic',
  );
  state.buffers.worldTriangles = makeBuffer(
    state.device, worldDistanceBvh.triangleBuffer, GPUBufferUsage.STORAGE, 'All streamed static-world mesh triangles',
  );
  state.buffers.worldNodes = makeBuffer(
    state.device, worldDistanceBvh.nodeBuffer, GPUBufferUsage.STORAGE, 'Static-world triangle BVH nodes',
  );
  state.device.queue.writeBuffer(state.buffers.worldGdfParams, 0, encodeWorldGdfParams(worldDistanceBvh));
  state.device.queue.writeBuffer(state.buffers.shapes, 0, encodeShapeBuffer(world.shapes));
  state.device.queue.writeBuffer(state.buffers.emissiveSources, 0, emissive.buffer);
  if (state.pipelines.update) createGlobalFieldUpdateBindGroup(state.device);

  if (obsoleteWorldBuffers.length) {
    state.device.queue.onSubmittedWorkDone().then(
      () => obsoleteWorldBuffers.forEach(buffer => buffer.destroy()),
      () => obsoleteWorldBuffers.forEach(buffer => buffer.destroy()),
    );
  }
  ui.loading.textContent = `${state.vehicle.triangleCount.toLocaleString()} ControlVehicle triangles · ${state.worldTriangleCount.toLocaleString()} streamed static mesh triangles · ${state.surfaceCacheCardCount.toLocaleString()} surface cards`;
  ui.fieldLegend.textContent = `${GRID_RESOLUTION}³ × 3 full-mesh clipmaps · ${state.worldTriangleCount.toLocaleString()} static tris · vehicle ${VEHICLE_SDF_DIMS.join(' × ')}`;
  state.worldGdfRebuildPending = false;
  state.gdfReadyMask = 0;
  state.gdfPendingMask = 0;
  state.gdfForceMask = 0;
  state.gdfAllDirty = true;
}

function createWorldStreamWorker() {
  if (typeof Worker === 'undefined') return;
  try {
    const worker = new Worker(new URL('./world-stream-worker.mjs', import.meta.url), { type: 'module' });
    state.worldStreamWorker = worker;
    worker.addEventListener('message', event => {
      const { requestId, world, worldDistanceBvh, error } = event.data;
      if (requestId !== state.streamRequestId) return;
      state.pendingStreamCell = null;
      if (error) {
        console.error(`City stream worker failed: ${error}`);
        worker.terminate();
        state.worldStreamWorker = null;
        return;
      }
      const cellX = Math.floor(state.car.x / CITY_BLOCK_SIZE);
      const cellZ = Math.floor(state.car.z / CITY_BLOCK_SIZE);
      if (world.streamCell.x !== cellX || world.streamCell.z !== cellZ) return;
      const desiredEmitterSize = Math.max(0.2, Math.min(state.emitterSize, 2.5));
      if (Math.abs((world.emitterSize || 1) - desiredEmitterSize) > 1e-4) {
        queueWorldGeometryRebuild();
        return;
      }
      try {
        installStreamedWorld(world, worldDistanceBvh);
      } catch (streamError) {
        console.error('Unable to install streamed city geometry:', streamError);
      }
    });
    worker.addEventListener('error', event => {
      console.error('City stream worker crashed:', event.message || event.error);
      worker.terminate();
      state.worldStreamWorker = null;
      state.pendingStreamCell = null;
    });
  } catch (error) {
    console.warn('Background city streaming is unavailable; using the synchronous fallback.', error);
    state.worldStreamWorker = null;
  }
}

function requestWorldGeometry(centerX, centerZ) {
  const cellX = Math.floor(centerX / CITY_BLOCK_SIZE);
  const cellZ = Math.floor(centerZ / CITY_BLOCK_SIZE);
  if (state.worldStreamWorker) {
    const requestId = ++state.streamRequestId;
    state.pendingStreamCell = { x: cellX, z: cellZ };
    try {
      state.worldStreamWorker.postMessage({ requestId, centerX, centerZ, emitterSize: state.emitterSize });
      return true;
    } catch (error) {
      console.error('Unable to request background city streaming:', error);
      state.worldStreamWorker.terminate();
      state.worldStreamWorker = null;
    }
  }

  state.pendingStreamCell = null;
  const world = buildWorldGeometry(centerX, centerZ);
  const vertices = scaleWorldEmitterGeometry(world, state.emitterSize);
  installStreamedWorld(world, buildWorldDistanceBvh(vertices));
  return true;
}

function queueWorldGeometryRebuild() {
  if (!state.device || !state.vehicle || !state.world) return;
  if (state.emitterRebuildTimer !== null) clearTimeout(state.emitterRebuildTimer);
  state.emitterRebuildTimer = setTimeout(() => {
    state.emitterRebuildTimer = null;
    requestWorldGeometry(state.car.x, state.car.z);
  }, 180);
}

function refreshStreamedWorld() {
  if (!state.device || !state.vehicle || !state.world) return false;
  const cellX = Math.floor(state.car.x / CITY_BLOCK_SIZE);
  const cellZ = Math.floor(state.car.z / CITY_BLOCK_SIZE);
  if (state.streamCell?.x === cellX && state.streamCell?.z === cellZ) {
    if (state.pendingStreamCell &&
        (state.pendingStreamCell.x !== cellX || state.pendingStreamCell.z !== cellZ)) {
      state.streamRequestId++;
      state.pendingStreamCell = null;
    }
    return false;
  }
  if (state.pendingStreamCell?.x === cellX && state.pendingStreamCell?.z === cellZ) return false;
  return requestWorldGeometry(state.car.x, state.car.z);
}
function createVolumeResources(device) {
  const [sx, sy, sz] = VEHICLE_SDF_DIMS;
  state.textures.vehicleDistance = createTexture(
    device, 'ControlVehicle signed distance volume', sx, sy, sz, 'r32float',
    GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
  );
  state.textures.vehicleMaterial = createTexture(
    device, 'ControlVehicle albedo and emissive volume', sx, sy, sz, 'rgba8unorm',
    GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
  );
  state.textures.vehicleSurface = createTexture(
    device, 'ControlVehicle roughness and metalness volume', sx, sy, sz, 'rgba8unorm',
    GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
  );
  const clipmapUsage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
  state.textures.globalDistance = createTexture(
    device, 'Three vehicle-following full static-mesh distance clipmaps',
    GRID_RESOLUTION, GRID_RESOLUTION, FIELD_DEPTH, 'r32float', clipmapUsage,
  );
  state.textures.globalSurfaceId = createTexture(
    device, 'Nearest static-mesh surface and card IDs per clipmap voxel',
    GRID_RESOLUTION, GRID_RESOLUTION, FIELD_DEPTH, 'r32uint', clipmapUsage,
  );
  state.textures.globalAlbedoRoughness = createTexture(
    device, 'Nearest static-mesh albedo and roughness per clipmap voxel',
    GRID_RESOLUTION, GRID_RESOLUTION, FIELD_DEPTH, 'rgba16float', clipmapUsage,
  );
  state.textures.globalEmissionMetallic = createTexture(
    device, 'Nearest static-mesh emission and metallic per clipmap voxel',
    GRID_RESOLUTION, GRID_RESOLUTION, FIELD_DEPTH, 'rgba16float', clipmapUsage,
  );
  state.views.vehicleDistance = state.textures.vehicleDistance.createView({ dimension: '3d' });
  state.views.vehicleMaterial = state.textures.vehicleMaterial.createView({ dimension: '3d' });
  state.views.vehicleSurface = state.textures.vehicleSurface.createView({ dimension: '3d' });
  state.views.globalDistance = state.textures.globalDistance.createView({ dimension: '3d' });
  state.views.globalSurfaceId = state.textures.globalSurfaceId.createView({ dimension: '3d' });
  state.views.globalAlbedoRoughness = state.textures.globalAlbedoRoughness.createView({ dimension: '3d' });
  state.views.globalEmissionMetallic = state.textures.globalEmissionMetallic.createView({ dimension: '3d' });
}

function createGlobalFieldUpdateBindGroup(device) {
  state.bindGroups.update = device.createBindGroup({
    label: 'Full streamed-mesh GDF update bindings',
    layout: state.pipelines.update.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: state.buffers.frame } },
      { binding: 1, resource: state.views.globalDistance },
      { binding: 11, resource: { buffer: state.buffers.worldTriangles } },
      { binding: 12, resource: { buffer: state.buffers.worldNodes } },
      { binding: 13, resource: { buffer: state.buffers.worldGdfParams } },
      { binding: 14, resource: state.views.globalSurfaceId },
      { binding: 15, resource: state.views.globalAlbedoRoughness },
      { binding: 16, resource: state.views.globalEmissionMetallic },
    ],
  });
}

function createPersistentBindGroups(device) {
  const { bake, gbuffer, cardId, present, fieldMap, surfaceCache } = state.pipelines;
  state.bindGroups.bake = device.createBindGroup({
    label: 'Triangle-to-SDF bake bindings',
    layout: bake.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: state.buffers.bakeParams } },
      { binding: 1, resource: { buffer: state.buffers.triangles } },
      { binding: 2, resource: { buffer: state.buffers.nodes } },
      { binding: 3, resource: state.views.vehicleDistance },
      { binding: 4, resource: state.views.vehicleMaterial },
      { binding: 5, resource: state.views.vehicleSurface },
    ],
  });
  createGlobalFieldUpdateBindGroup(device);
  state.bindGroups.surfaceCache = device.createBindGroup({
    label: 'View-independent mesh-card radiance cache bindings',
    layout: surfaceCache.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: state.buffers.frame } },
      { binding: 5, resource: state.views.globalDistance },
      { binding: 6, resource: state.views.vehicleDistance },
      { binding: 7, resource: state.views.vehicleMaterial },
      { binding: 8, resource: state.views.vehicleSurface },
      { binding: 9, resource: { buffer: state.buffers.assetParams } },
      { binding: 10, resource: { buffer: state.buffers.shapes } },
      { binding: 11, resource: { buffer: state.buffers.emissiveSources } },
      { binding: 12, resource: { buffer: state.buffers.surfaceCards } },
      { binding: 13, resource: { buffer: state.buffers.surfaceRadiance } },
      { binding: 14, resource: { buffer: state.buffers.surfaceCardUpdateList } },
      { binding: 18, resource: state.views.globalSurfaceId },
      { binding: 19, resource: state.views.globalAlbedoRoughness },
      { binding: 20, resource: state.views.globalEmissionMetallic },
    ],
  });
  state.bindGroups.gbuffer = device.createBindGroup({
    label: 'Primary visibility frame data',
    layout: gbuffer.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: state.buffers.frame } }],
  });
  state.bindGroups.cardId = device.createBindGroup({
    label: 'Surface-card visibility frame data',
    layout: cardId.getBindGroupLayout(0),
    entries: [{ binding: 0, resource: { buffer: state.buffers.frame } }],
  });
  state.bindGroups.fieldMap = device.createBindGroup({
    label: 'GDF inspection slice bindings',
    layout: fieldMap.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: state.buffers.frame } },
      { binding: 1, resource: state.views.globalDistance },
      { binding: 2, resource: state.views.vehicleDistance },
      { binding: 3, resource: { buffer: state.buffers.assetParams } },
      { binding: 4, resource: { buffer: state.buffers.shapes } },
    ],
  });
  state.bindGroups.presentSampler = device.createSampler({
    label: 'Bilinear upscale sampler',
    minFilter: 'linear',
    magFilter: 'linear',
    mipmapFilter: 'nearest',
  });
}

function recreateRenderTargets() {
  const { device } = state;
  if (!device || !state.canvasContext || !state.pipelines.gbuffer) return;
  const pixelRatio = Math.min(window.devicePixelRatio || 1, 1.5);
  const bounds = ui.canvas.getBoundingClientRect();
  resizeClipmapOverlay();
  const outputWidth = Math.max(320, Math.round(bounds.width * pixelRatio));
  const outputHeight = Math.max(240, Math.round(bounds.height * pixelRatio));
  const scale = QUALITY[state.quality].scale;
  const width = Math.max(320, Math.round(outputWidth * scale));
  const height = Math.max(240, Math.round(outputHeight * scale));
  const unchanged = outputWidth === state.targetWidth && outputHeight === state.targetHeight &&
    width === state.renderWidth && height === state.renderHeight;
  if (unchanged) return;

  if (state.canvasContext) {
    ui.canvas.width = outputWidth;
    ui.canvas.height = outputHeight;
  }
  state.targetWidth = outputWidth;
  state.targetHeight = outputHeight;
  state.renderWidth = width;
  state.renderHeight = height;
  state.renderScale = scale;

  for (const texture of Object.values(state.textures.gbuffer || {})) texture.destroy();
  if (state.textures.depth) state.textures.depth.destroy();
  if (state.textures.direct) state.textures.direct.destroy();
  if (state.textures.indirect) state.textures.indirect.destroy();
  if (state.textures.output) state.textures.output.destroy();
  if (state.textures.denoised) state.textures.denoised.destroy();
  for (const texture of state.textures.historyColor || []) texture.destroy();
  if (state.textures.historyPosition) state.textures.historyPosition.destroy();
  if (state.textures.historyNormal) state.textures.historyNormal.destroy();

  const usage = GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC;
  state.textures.gbuffer = {
    position: createTexture(device, 'G-buffer world position', width, height, 1, 'rgba16float', usage),
    normal: createTexture(device, 'G-buffer normal and roughness', width, height, 1, 'rgba16float', usage),
    albedo: createTexture(device, 'G-buffer base colour and metalness', width, height, 1, 'rgba16float', usage),
    emission: createTexture(device, 'G-buffer emissive radiance', width, height, 1, 'rgba16float', usage),
    cardId: createTexture(device, 'G-buffer world-space mesh-card ID', width, height, 1, 'r16uint', usage),
  };
  state.textures.depth = createTexture(device, 'Primary visibility depth', width, height, 1, 'depth24plus', GPUTextureUsage.RENDER_ATTACHMENT);
  const lightUsage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
  state.textures.direct = createTexture(device, 'Alias-sampled direct radiance', width, height, 1, 'rgba16float', lightUsage);
  state.textures.indirect = createTexture(device, 'Multi-bounce diffuse radiance', width, height, 1, 'rgba16float', lightUsage);
  state.textures.output = createTexture(
    device, 'Linear HDR composed SDF GI resolve', width, height, 1, 'rgba16float',
    GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
  );
  state.textures.denoised = createTexture(
    device, 'Edge-aware denoised HDR GI resolve', width, height, 1, 'rgba16float',
    GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING,
  );
  const historyUsage = GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING;
  state.textures.historyColor = [0, 1].map(index => createTexture(
    device, `Temporal surface-cache radiance ${index}`, width, height, 1, 'rgba16float', historyUsage,
  ));
  const historyCopyUsage = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST;
  state.textures.historyPosition = createTexture(device, 'Previous-frame surface positions', width, height, 1, 'rgba16float', historyCopyUsage);
  state.textures.historyNormal = createTexture(device, 'Previous-frame surface normals', width, height, 1, 'rgba16float', historyCopyUsage);
  state.views.gbuffer = Object.fromEntries(Object.entries(state.textures.gbuffer).map(([key, texture]) => [key, texture.createView()]));
  state.views.direct = state.textures.direct.createView();
  state.views.indirect = state.textures.indirect.createView();
  state.views.output = state.textures.output.createView();
  state.views.denoised = state.textures.denoised.createView();
  state.views.historyColor = state.textures.historyColor.map(texture => texture.createView());
  state.views.historyPosition = state.textures.historyPosition.createView();
  state.views.historyNormal = state.textures.historyNormal.createView();

  state.bindGroups.temporal = [0, 1].map(readIndex => {
    const writeIndex = 1 - readIndex;
    return device.createBindGroup({
      label: `Temporal surface-cache history ${readIndex} to ${writeIndex}`,
      layout: state.pipelines.temporal.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: state.buffers.frame } },
        { binding: 1, resource: state.views.output },
        { binding: 2, resource: state.views.gbuffer.position },
        { binding: 3, resource: state.views.gbuffer.normal },
        { binding: 4, resource: state.views.historyColor[readIndex] },
        { binding: 5, resource: state.views.historyPosition },
        { binding: 6, resource: state.views.historyNormal },
        { binding: 7, resource: state.views.historyColor[writeIndex] },
      ],
    });
  });

  state.bindGroups.shade = device.createBindGroup({
    label: 'G-buffer, SDF visibility, area-light and emissive bindings',
    layout: state.pipelines.shade.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: state.buffers.frame } },
      { binding: 1, resource: state.views.gbuffer.position },
      { binding: 2, resource: state.views.gbuffer.normal },
      { binding: 3, resource: state.views.gbuffer.albedo },
      { binding: 4, resource: state.views.gbuffer.emission },
      { binding: 5, resource: state.views.globalDistance },
      { binding: 6, resource: state.views.vehicleDistance },
      { binding: 9, resource: { buffer: state.buffers.assetParams } },
      { binding: 10, resource: { buffer: state.buffers.shapes } },
      { binding: 11, resource: { buffer: state.buffers.emissiveSources } },
      { binding: 12, resource: state.views.direct },
      { binding: 13, resource: state.views.historyPosition },
      { binding: 14, resource: state.views.historyNormal },
      { binding: 15, resource: state.views.gbuffer.cardId },
      { binding: 16, resource: { buffer: state.buffers.surfaceCards } },
      { binding: 17, resource: { buffer: state.buffers.surfaceRadiance } },
    ],
  });
  state.bindGroups.gi = device.createBindGroup({
    label: 'G-buffer, moving-vehicle SDF and emissive path bindings',
    layout: state.pipelines.gi.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: state.buffers.frame } },
      { binding: 1, resource: state.views.gbuffer.position },
      { binding: 2, resource: state.views.gbuffer.normal },
      { binding: 3, resource: state.views.gbuffer.albedo },
      { binding: 5, resource: state.views.globalDistance },
      { binding: 6, resource: state.views.vehicleDistance },
      { binding: 7, resource: state.views.vehicleMaterial },
      { binding: 8, resource: state.views.vehicleSurface },
      { binding: 9, resource: { buffer: state.buffers.assetParams } },
      { binding: 10, resource: { buffer: state.buffers.shapes } },
      { binding: 11, resource: { buffer: state.buffers.emissiveSources } },
      { binding: 12, resource: state.views.indirect },
      { binding: 13, resource: state.views.historyPosition },
      { binding: 14, resource: state.views.historyNormal },
      { binding: 15, resource: state.views.gbuffer.cardId },
      { binding: 16, resource: { buffer: state.buffers.surfaceCards } },
      { binding: 17, resource: { buffer: state.buffers.surfaceRadiance } },
      { binding: 18, resource: state.views.globalSurfaceId },
      { binding: 19, resource: state.views.globalAlbedoRoughness },
      { binding: 20, resource: state.views.globalEmissionMetallic },
    ],
  });
  state.bindGroups.compose = device.createBindGroup({
    label: 'Direct, indirect, cache and final-output composition bindings',
    layout: state.pipelines.compose.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: state.buffers.frame } },
      { binding: 1, resource: state.views.gbuffer.position },
      { binding: 2, resource: state.views.gbuffer.normal },
      { binding: 3, resource: state.views.gbuffer.albedo },
      { binding: 5, resource: state.views.globalDistance },
      { binding: 6, resource: state.views.vehicleDistance },
      { binding: 9, resource: { buffer: state.buffers.assetParams } },
      { binding: 10, resource: { buffer: state.buffers.shapes } },
      { binding: 11, resource: state.views.direct },
      { binding: 12, resource: state.views.indirect },
      { binding: 13, resource: state.views.output },
    ],
  });
  state.bindGroups.denoise = state.views.historyColor.map((source, index) => device.createBindGroup({
    label: `Surface-cache history ${index} spatial denoising bindings`,
    layout: state.pipelines.denoise.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: source },
      { binding: 1, resource: state.views.gbuffer.position },
      { binding: 2, resource: state.views.gbuffer.normal },
      { binding: 3, resource: state.views.gbuffer.albedo },
      { binding: 4, resource: state.views.denoised },
    ],
  }));
  state.historyReadIndex = 0;
  state.historyValid = false;
  state.previousViewProjection = null;
  state.bindGroups.present = device.createBindGroup({
    label: 'Denoised image to canvas',
    layout: state.pipelines.present.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: state.views.denoised },
      { binding: 1, resource: state.bindGroups.presentSampler },
    ],
  });
}

function perspectiveWebGpu(fov, aspect, near, far) {
  const f = 1 / Math.tan(fov * 0.5);
  const out = new Float32Array(16);
  out[0] = f / aspect;
  out[5] = f;
  out[10] = far / (near - far);
  out[11] = -1;
  out[14] = (far * near) / (near - far);
  return out;
}

function lookAt(eye, target) {
  let zx = eye[0] - target[0];
  let zy = eye[1] - target[1];
  let zz = eye[2] - target[2];
  let length = Math.hypot(zx, zy, zz) || 1;
  zx /= length; zy /= length; zz /= length;
  let xx = zz;
  let xy = 0;
  let xz = -zx;
  length = Math.hypot(xx, xy, xz) || 1;
  xx /= length; xy /= length; xz /= length;
  const yx = zy * xz - zz * xy;
  const yy = zz * xx - zx * xz;
  const yz = zx * xy - zy * xx;
  const out = new Float32Array(16);
  out[0] = xx; out[1] = yx; out[2] = zx; out[3] = 0;
  out[4] = xy; out[5] = yy; out[6] = zy; out[7] = 0;
  out[8] = xz; out[9] = yz; out[10] = zz; out[11] = 0;
  out[12] = -(xx * eye[0] + xy * eye[1] + xz * eye[2]);
  out[13] = -(yx * eye[0] + yy * eye[1] + yz * eye[2]);
  out[14] = -(zx * eye[0] + zy * eye[1] + zz * eye[2]);
  out[15] = 1;
  return out;
}

function multiply4(a, b) {
  const out = new Float32Array(16);
  for (let column = 0; column < 4; column++) {
    for (let row = 0; row < 4; row++) {
      out[column * 4 + row] = a[row] * b[column * 4] + a[4 + row] * b[column * 4 + 1] +
        a[8 + row] * b[column * 4 + 2] + a[12 + row] * b[column * 4 + 3];
    }
  }
  return out;
}

function snap(value, voxelSize) {
  return Math.round(value / voxelSize) * voxelSize;
}

const CLIPMAP_COLORS = ['#72f2b5', '#65b8ff', '#ffb36b'];
const CUBE_EDGES = [
  [0, 1], [2, 3], [4, 5], [6, 7],
  [0, 2], [1, 3], [4, 6], [5, 7],
  [0, 4], [1, 5], [2, 6], [3, 7],
];
const VIEW_CLIP_PLANES = [
  [1, 0, 0, 1], [-1, 0, 0, 1],
  [0, 1, 0, 1], [0, -1, 0, 1],
  [0, 0, 1, 0], [0, 0, -1, 1],
];

function resizeClipmapOverlay() {
  if (!state.overlayContext) return;
  const bounds = ui.canvas.getBoundingClientRect();
  const ratio = Math.min(window.devicePixelRatio || 1, 1.5);
  const width = Math.max(1, Math.round(bounds.width * ratio));
  const height = Math.max(1, Math.round(bounds.height * ratio));
  if (ui.clipmapOverlay.width !== width || ui.clipmapOverlay.height !== height) {
    ui.clipmapOverlay.width = width;
    ui.clipmapOverlay.height = height;
  }
  state.overlayContext.setTransform(ratio, 0, 0, ratio, 0, 0);
  state.overlayWidth = bounds.width;
  state.overlayHeight = bounds.height;
}

function setFieldCardVisible(visible) {
  state.showField = visible;
  ui.fieldCard.classList.toggle('hidden', !visible);
  ui.fieldToggle.classList.toggle('active', visible);
  ui.fieldToggle.setAttribute('aria-pressed', String(visible));
  ui.fieldToggle.setAttribute('aria-label', visible ? 'Hide debug inset' : 'Show debug inset');
  ui.fieldToggle.title = visible ? 'Hide debug inset' : 'Show debug inset';
}

function transformToClip(point) {
  const matrix = state.viewProjection;
  const [x, y, z] = point;
  return [
    matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12],
    matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13],
    matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14],
    matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15],
  ];
}

function clipLineToView(first, second) {
  let start = 0;
  let end = 1;
  for (const plane of VIEW_CLIP_PLANES) {
    const firstDistance = first[0] * plane[0] + first[1] * plane[1] + first[2] * plane[2] + first[3] * plane[3];
    const secondDistance = second[0] * plane[0] + second[1] * plane[1] + second[2] * plane[2] + second[3] * plane[3];
    if (firstDistance < 0 && secondDistance < 0) return null;
    if ((firstDistance < 0) !== (secondDistance < 0)) {
      const crossing = firstDistance / (firstDistance - secondDistance);
      if (firstDistance < 0) start = Math.max(start, crossing);
      else end = Math.min(end, crossing);
      if (start > end) return null;
    }
  }
  const mix = (a, b, t) => a + (b - a) * t;
  return [
    first.map((value, axis) => mix(value, second[axis], start)),
    first.map((value, axis) => mix(value, second[axis], end)),
  ];
}

function projectClipPoint(point) {
  const inverseW = 1 / point[3];
  return [
    (point[0] * inverseW * 0.5 + 0.5) * state.overlayWidth,
    (0.5 - point[1] * inverseW * 0.5) * state.overlayHeight,
  ];
}

function drawClipmapOverlay() {
  const context = state.overlayContext;
  if (!context) return;
  context.clearRect(0, 0, state.overlayWidth, state.overlayHeight);
  if (!state.showClipmapBounds || !state.viewProjection || !state.clipmapCenters) return;

  context.save();
  context.lineCap = 'round';
  context.lineJoin = 'round';
  for (let level = 2; level >= 0; level--) {
    const center = state.clipmapCenters[level];
    const half = CASCADE_EXTENTS[level] * 0.5;
    const corners = Array.from({ length: 8 }, (_, index) => transformToClip([
      center[0] + ((index & 1) ? half : -half),
      center[1] + ((index & 2) ? half : -half),
      center[2] + ((index & 4) ? half : -half),
    ]));
    context.strokeStyle = CLIPMAP_COLORS[level];
    context.globalAlpha = [0.66, 0.42, 0.27][level];
    context.lineWidth = [1.5, 1.1, 0.85][level];
    context.setLineDash(level === 0 ? [4, 3] : level === 1 ? [6, 5] : [2, 6]);
    context.shadowColor = CLIPMAP_COLORS[level];
    context.shadowBlur = level === 0 ? 2 : 1;
    for (const [a, b] of CUBE_EDGES) {
      const segment = clipLineToView(corners[a], corners[b]);
      if (!segment) continue;
      const start = projectClipPoint(segment[0]);
      const end = projectClipPoint(segment[1]);
      context.beginPath();
      context.moveTo(start[0], start[1]);
      context.lineTo(end[0], end[1]);
      context.stroke();
    }
    const label = projectClipPoint(corners[0]);
    const labels = ['FINE · 12 m', 'MID · 36 m', 'FAR · 108 m'];
    context.setLineDash([]);
    context.globalAlpha = 0.96;
    context.shadowBlur = 3;
    context.font = '700 10px ui-monospace, monospace';
    context.fillStyle = CLIPMAP_COLORS[level];
    context.fillText(labels[level], Math.max(6, Math.min(state.overlayWidth - 92, label[0] + 5)),
      Math.max(12, Math.min(state.overlayHeight - 6, label[1] - 5)));
  }
  context.restore();
}

// City geometry is pass-through by design; only the finite world perimeter constrains driving.
function moveVehicleWithinWorld(deltaX, deltaZ) {
  const minX = -WORLD_WIDTH * 0.5 + WORLD_BOUNDARY_MARGIN;
  const maxX = WORLD_WIDTH * 0.5 - WORLD_BOUNDARY_MARGIN;
  const minZ = -WORLD_LENGTH * 0.5 + WORLD_BOUNDARY_MARGIN;
  const maxZ = WORLD_LENGTH * 0.5 - WORLD_BOUNDARY_MARGIN;
  const wantedX = state.car.x + deltaX;
  const wantedZ = state.car.z + deltaZ;
  const nextX = Math.max(minX, Math.min(maxX, wantedX));
  const nextZ = Math.max(minZ, Math.min(maxZ, wantedZ));
  state.car.x = nextX;
  state.car.z = nextZ;
  return Math.abs(nextX - wantedX) > 1e-5 || Math.abs(nextZ - wantedZ) > 1e-5;
}

function updateVehicle(dt) {
  const startX = state.car.x;
  const startZ = state.car.z;
  const drivingKeys = ['w', 'a', 's', 'd', 'arrowup', 'arrowleft', 'arrowdown', 'arrowright', ' '];
  const manualInput = drivingKeys.some(key => state.keys.has(key));
  if (manualInput && state.autoDrive) {
    state.autoDrive = false;
    ui.autoDrive.checked = false;
  }

  let steeringAngle = 0;
  if (state.autoDrive) {
    const cruiseSpeed = Number(ui.driveSpeed.value);
    const acceleration = Math.max(-9 * dt, Math.min(9 * dt, cruiseSpeed - state.manualSpeed));
    state.manualSpeed += acceleration;
    state.car.yaw = state.autoYaw;
  } else {
    const throttle = (state.keys.has('w') || state.keys.has('arrowup') ? 1 : 0) -
      (state.keys.has('s') || state.keys.has('arrowdown') ? 1 : 0);
    const steering = (state.keys.has('a') || state.keys.has('arrowleft') ? 1 : 0) -
      (state.keys.has('d') || state.keys.has('arrowright') ? 1 : 0);
    const brake = state.keys.has(' ');
    state.manualSpeed += throttle * 9.5 * dt;
    state.manualSpeed *= Math.exp(-(brake ? 5.5 : throttle === 0 ? 0.32 : 0.10) * dt);
    state.manualSpeed = Math.max(-MAX_REVERSE_SPEED, Math.min(MAX_FORWARD_SPEED, state.manualSpeed));
    state.steeringInput += (steering - state.steeringInput) * (1 - Math.exp(-dt * 6));
    const maxSteeringAngle = 0.43 / (1 + Math.abs(state.manualSpeed) * 0.045);
    steeringAngle = state.steeringInput * maxSteeringAngle;
    const wheelBase = Math.max(state.vehicle.wheelBase, 1);
    const yawRate = state.manualSpeed / wheelBase * Math.tan(steeringAngle);
    state.car.yaw += yawRate * dt;
  }

  const headingX = Math.cos(state.car.yaw);
  const headingZ = -Math.sin(state.car.yaw);
  const travelSpeed = state.manualSpeed;
  const hitWorldBoundary = moveVehicleWithinWorld(headingX * travelSpeed * dt, headingZ * travelSpeed * dt);
  if (hitWorldBoundary) {
    state.manualSpeed = 0;
    if (state.autoDrive) {
      state.autoDrive = false;
      ui.autoDrive.checked = false;
    }
  }
  state.car.speed = Math.abs(state.manualSpeed);
  updateWheelPose(startX, startZ, travelSpeed, steeringAngle);
}

function updateWheelPose(startX, startZ, signedSpeed, steeringAngle) {
  const distance = Math.hypot(state.car.x - startX, state.car.z - startZ);
  const wheelRadius = Math.max(state.vehicle.wheelParts[0].radius, 0.1);
  if (distance > 1e-7 && Math.abs(signedSpeed) > 1e-4) {
    state.wheelRotation = (state.wheelRotation + Math.sign(signedSpeed) * distance / wheelRadius) % TAU;
  }
  state.wheelSteering = steeringAngle;
}

function updateCameraAndField(dt) {
  const follow = 1 - Math.exp(-Math.max(dt, 0.001) * 6.5);
  state.focus[0] += (state.car.x - state.focus[0]) * follow;
  state.focus[1] += (1.1 - state.focus[1]) * follow;
  state.focus[2] += (state.car.z - state.focus[2]) * follow;

  const target = [state.car.x, 1.1, state.car.z];
  const orbit = Math.PI - state.car.yaw + state.orbitYaw;
  const horizontal = Math.cos(state.orbitPitch) * state.orbitDistance;
  const eye = [
    target[0] + Math.cos(orbit) * horizontal,
    target[1] + Math.sin(state.orbitPitch) * state.orbitDistance,
    target[2] + Math.sin(orbit) * horizontal,
  ];
  const view = lookAt(eye, target);
  const projection = perspectiveWebGpu(52 * Math.PI / 180, state.targetWidth / Math.max(state.targetHeight, 1), 0.1, 260);
  const viewProjection = multiply4(projection, view);
  state.camera = eye;
  state.cameraTarget = target;
  state.viewProjection = viewProjection;

  const centers = CASCADE_EXTENTS.map(extent => {
    const cell = extent / GRID_RESOLUTION;
    return [snap(state.focus[0], cell), snap(state.focus[1], cell), snap(state.focus[2], cell)];
  });
  state.clipmapTargets = centers;
  if (state.worldGdfRebuildPending) {
    // Never repopulate the clipmaps from the old BVH while emitter geometry is
    // changing; the worker's new triangles arrive together with its mesh stream.
    state.gdfPendingMask = 0;
    state.gdfForceMask = 0;
    state.gdfDirtyMask = 0;
    state.gdfAllDirty = true;
  } else {
    for (let level = 0; level < centers.length; level++) {
      const bit = 1 << level;
      const previous = state.gdfCenters?.[level];
      const shift = previous
        ? Math.max(...centers[level].map((value, axis) => Math.abs(value - previous[axis])))
        : Infinity;
      const recenterThreshold = CASCADE_EXTENTS[level] * 0.12;
      if (state.gdfAllDirty || !previous || shift >= recenterThreshold) {
        state.gdfPendingMask |= bit;
        if (state.gdfAllDirty || !previous) state.gdfForceMask |= bit;
      }
    }
    state.gdfAllDirty = false;
    state.gdfDirtyMask = 0;
    for (let offset = 0; offset < centers.length; offset++) {
      const level = (state.gdfNextCascade + offset) % centers.length;
      const bit = 1 << level;
      if ((state.gdfPendingMask & bit) !== 0) {
        state.gdfDirtyMask = bit;
        state.gdfNextCascade = (level + 1) % centers.length;
        break;
      }
    }
  }
  const sampleCenters = centers.map((target, level) => {
    const updatedThisFrame = (state.gdfDirtyMask & (1 << level)) !== 0;
    return updatedThisFrame || !state.gdfCenters?.[level] ? target : state.gdfCenters[level];
  });
  state.clipmapCenters = sampleCenters;
  const data = state.frameData;
  data.set(viewProjection, 0);
  data.set([eye[0], eye[1], eye[2], state.elapsed], 16);
  data.set([state.car.x, 0, state.car.z, state.car.yaw], 20);
  data.set([...sampleCenters[0], 0], 24);
  data.set([...sampleCenters[1], 0], 28);
  data.set([...sampleCenters[2], 0], 32);
  data.set([...CASCADE_EXTENTS, GRID_RESOLUTION], 36);
  data.set([...state.vehicle.boundsMin, 0], 40);
  data.set([...state.vehicle.boundsMax, 0], 44);
  data.set([state.renderWidth, state.renderHeight, state.targetWidth, state.targetHeight], 48);
  data.set([RENDER_MODES[state.mode], state.giRays, state.bounces, state.frameIndex], 52);
  const sun = normalize3([-0.38, 0.86, 0.34]);
  data.set([sun[0], sun[1], sun[2], state.sunIntensity], 56);
  data.set([0.30, 0.43, 0.56, state.skyStrength], 60);
  data.set([state.giStrength, state.surfaceCacheGeneration, state.aoStrength, state.shapeCount], 64);
  data.set([state.fieldSlice, ui.fieldCanvas.width, ui.fieldCanvas.height, 1.05], 68);
  data.set([state.emissiveIntensity, state.emitterSize, state.sunAngularRadius, state.emissiveSamples], 72);
  data.set([state.emissiveSourceCount, state.historyValid ? 1 : 0, state.temporalEnabled ? 1 : 0, state.surfaceCacheUpdateCount], 76);
  data.set([...state.pointLightPosition, state.pointLightIntensity], 80);
  data.set([1.0, 0.72, 0.43, state.pointLightRadius], 84);
  data.set(state.previousViewProjection || viewProjection, 88);
  state.previousViewProjection = new Float32Array(viewProjection);
  const previousCar = state.previousCar || state.car;
  data.set([previousCar.x, 0, previousCar.z, previousCar.yaw], 104);
  const [frontLeft, frontRight, rearLeft, rearRight] = state.vehicle.wheelParts;
  data.set([...frontLeft.center, state.wheelRotation], 108);
  data.set([...frontRight.center, state.wheelRotation], 112);
  data.set([...rearLeft.center, state.wheelRotation], 116);
  data.set([...rearRight.center, state.wheelRotation], 120);
  data.set([state.wheelSteering, state.wheelSteering, state.gdfDirtyMask, state.gdfReadyMask | state.gdfDirtyMask], 124);
  data.set([...centers[0], (state.gdfForceMask & 1) !== 0 ? 1 : 0], 128);
  data.set([...centers[1], (state.gdfForceMask & 2) !== 0 ? 1 : 0], 132);
  data.set([...centers[2], (state.gdfForceMask & 4) !== 0 ? 1 : 0], 136);
  for (let level = 0; level < centers.length; level++) {
    data.set([...(state.gdfCenters?.[level] || centers[level]), 0], 140 + level * 4);
  }
  state.previousCar = { x: state.car.x, z: state.car.z, yaw: state.car.yaw };
}

function scheduleSurfaceCardUpdates() {
  state.surfaceCacheUpdateCount = 0;
  if (state.worldGdfRebuildPending || state.gdfReadyMask !== 0b111 ||
      !state.surfaceCacheCardCount || !state.surfaceCacheValid ||
      !['gi', 'direct', 'indirect'].includes(state.mode)) return;
  const cards = state.world.surfaceCards;
  const cardCount = state.surfaceCacheCardCount;
  const valid = state.surfaceCacheValid;
  const start = state.surfaceCacheCursor % cardCount;
  let scanned = 0;
  while (scanned < cardCount && state.surfaceCacheUpdateCount < SURFACE_CACHE_UPDATE_LIMIT) {
    const cardIndex = (start + scanned) % cardCount;
    scanned++;
    if (valid[cardIndex]) continue;
    const base = cardIndex * SURFACE_CARD_FLOATS;
    const originX = cards[base];
    const originY = cards[base + 1];
    const originZ = cards[base + 2];
    const ux = cards[base + 4];
    const uy = cards[base + 5];
    const uz = cards[base + 6];
    const vx = cards[base + 8];
    const vy = cards[base + 9];
    const vz = cards[base + 10];
    const center = [originX + (ux + vx) * 0.5, originY + (uy + vy) * 0.5, originZ + (uz + vz) * 0.5];
    const cameraDx = state.camera[0] - center[0];
    const cameraDy = state.camera[1] - center[1];
    const cameraDz = state.camera[2] - center[2];
    const distance = Math.hypot(cameraDx, cameraDy, cameraDz);
    const radius = 0.5 * Math.hypot(Math.hypot(ux, uy, uz), Math.hypot(vx, vy, vz));
    const vehicleDistance = Math.hypot(center[0] - state.car.x, center[2] - state.car.z);
    if (distance > 260 || vehicleDistance < SURFACE_CACHE_DYNAMIC_RADIUS + radius) continue;
    const clip = transformToClip(center);
    if (clip[3] <= 0.05) continue;
    const margin = Math.min(radius / Math.max(distance, 1), 1.5);
    if (Math.abs(clip[0] / clip[3]) > 1 + margin || Math.abs(clip[1] / clip[3]) > 1 + margin) continue;
    state.surfaceCacheUpdateList[state.surfaceCacheUpdateCount++] = cardIndex;
    valid[cardIndex] = 1;
  }
  state.surfaceCacheCursor = (start + Math.max(scanned, 1)) % cardCount;
  if (state.surfaceCacheUpdateCount > 0) {
    state.device.queue.writeBuffer(
      state.buffers.surfaceCardUpdateList,
      0,
      state.surfaceCacheUpdateList.subarray(0, state.surfaceCacheUpdateCount),
    );
  }
}

function normalize3(v) {
  const length = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / length, v[1] / length, v[2] / length];
}

function updateHud(now) {
  state.fpsFrames++;
  if (now - state.fpsStart >= 900) {
    state.fps = state.fpsFrames * 1000 / Math.max(now - state.fpsStart, 1);
    state.fpsFrames = 0;
    state.fpsStart = now;
  }
  if (now - state.lastHudUpdate < 180) return;
  state.lastHudUpdate = now;
  ui.fps.textContent = state.fps ? state.fps.toFixed(0) : '—';
  ui.frameMs.textContent = state.fps ? `${(1000 / state.fps).toFixed(1)} ms` : '—';
  ui.speed.textContent = `${state.car.speed.toFixed(1)} m/s`;
  ui.position.textContent = `${state.car.x.toFixed(1)} / ${state.car.z.toFixed(1)}`;
  ui.fieldCells.textContent = `${GRID_RESOLUTION}³ × 3`;
  ui.fieldVoxel.textContent = `${(CASCADE_EXTENTS[0] / GRID_RESOLUTION).toFixed(2)} m`;
  ui.driveMode.textContent = state.autoDrive ? 'AUTO CRUISE' : 'MANUAL DRIVE';
  ui.driveMode.dataset.mode = state.autoDrive ? 'auto' : 'manual';
  updateGpuTimingHud();
}

function renderFrame(now) {
  if (!state.started) return;
  prepareGpuTimingFrame();
  const dt = state.lastFrameTime ? Math.min((now - state.lastFrameTime) / 1000, 0.05) : 1 / 60;
  state.lastFrameTime = now;
  if (!state.paused) {
    state.elapsed += dt;
    updateVehicle(dt);
    refreshStreamedWorld();
  }
  recreateRenderTargets();
  updateCameraAndField(dt);
  scheduleSurfaceCardUpdates();
  state.frameData[79] = state.surfaceCacheUpdateCount;
  state.device.queue.writeBuffer(state.buffers.frame, 0, state.frameData);
  drawClipmapOverlay();
  updateHud(now);

  const { device } = state;
  const encoder = device.createCommandEncoder({ label: `SDF GI frame ${state.frameIndex}` });

  if (state.gdfDirtyMask !== 0) {
    const updatePass = encoder.beginComputePass({
      label: 'Update dirty snapped global distance-field cascades',
      timestampWrites: gpuTimestampWrites('gdf'),
    });
    updatePass.setPipeline(state.pipelines.update);
    updatePass.setBindGroup(0, state.bindGroups.update);
    updatePass.dispatchWorkgroups(
      Math.ceil(GRID_RESOLUTION / 4),
      Math.ceil(GRID_RESOLUTION / 4),
      Math.ceil(GRID_RESOLUTION / 4),
    );
    updatePass.end();
  }

  if (state.surfaceCacheUpdateCount > 0) {
    const cachePass = encoder.beginComputePass({
      label: 'Update visible world-space mesh-card radiance',
      timestampWrites: gpuTimestampWrites('surfaceCache'),
    });
    cachePass.setPipeline(state.pipelines.surfaceCache);
    cachePass.setBindGroup(0, state.bindGroups.surfaceCache);
    cachePass.dispatchWorkgroups(
      Math.ceil(state.surfaceCacheUpdateCount * SURFACE_CARD_TEXELS / 64),
      1,
      1,
    );
    cachePass.end();
  }

  const gbufferPass = encoder.beginRenderPass({
    label: 'Rasterize scene and project-accurate ControlVehicle mesh',
    timestampWrites: gpuTimestampWrites('gbuffer'),
    colorAttachments: [
      state.views.gbuffer.position,
      state.views.gbuffer.normal,
      state.views.gbuffer.albedo,
      state.views.gbuffer.emission,
    ].map(view => ({ view, clearValue: { r: 0, g: 0, b: 0, a: 0 }, loadOp: 'clear', storeOp: 'store' })),
    depthStencilAttachment: {
      view: state.textures.depth.createView(),
      depthClearValue: 1,
      depthLoadOp: 'clear',
      depthStoreOp: 'store',
    },
  });
  gbufferPass.setPipeline(state.pipelines.gbuffer);
  gbufferPass.setBindGroup(0, state.bindGroups.gbuffer);
  gbufferPass.setVertexBuffer(0, state.buffers.worldVertices);
  gbufferPass.draw(state.worldVertexCount);
  gbufferPass.setVertexBuffer(0, state.buffers.vehicleVertices);
  gbufferPass.draw(state.vehicleVertexCount);
  gbufferPass.end();

  const cardIdPass = encoder.beginRenderPass({
    label: 'Rasterize compact world-space surface-card IDs',
    timestampWrites: gpuTimestampWrites('cardId'),
    colorAttachments: [{
      view: state.views.gbuffer.cardId,
      clearValue: { r: 0, g: 0, b: 0, a: 0 },
      loadOp: 'clear',
      storeOp: 'store',
    }],
    depthStencilAttachment: {
      view: state.textures.depth.createView(),
      depthLoadOp: 'load',
      depthStoreOp: 'store',
    },
  });
  cardIdPass.setPipeline(state.pipelines.cardId);
  cardIdPass.setBindGroup(0, state.bindGroups.cardId);
  cardIdPass.setVertexBuffer(0, state.buffers.worldVertices);
  cardIdPass.draw(state.worldVertexCount);
  cardIdPass.end();

  const renderGroupsX = Math.ceil(state.renderWidth / 8);
  const renderGroupsY = Math.ceil(state.renderHeight / 8);
  if (state.mode === 'gi' || state.mode === 'direct') {
    const directPass = encoder.beginComputePass({
      label: 'Alias-sampled finite-emitter and sun direct lighting',
      timestampWrites: gpuTimestampWrites('direct'),
    });
    directPass.setPipeline(state.pipelines.shade);
    directPass.setBindGroup(0, state.bindGroups.shade);
    directPass.dispatchWorkgroups(renderGroupsX, renderGroupsY, 1);
    directPass.end();
  }
  if (state.mode === 'gi' || state.mode === 'indirect') {
    const giPass = encoder.beginComputePass({
      label: 'Multi-bounce distance-field diffuse transport',
      timestampWrites: gpuTimestampWrites('gi'),
    });
    giPass.setPipeline(state.pipelines.gi);
    giPass.setBindGroup(0, state.bindGroups.gi);
    giPass.dispatchWorkgroups(renderGroupsX, renderGroupsY, 1);
    giPass.end();
  }

  const composePass = encoder.beginComputePass({
    label: 'Compose lighting and render-mode output',
    timestampWrites: gpuTimestampWrites('compose'),
  });
  composePass.setPipeline(state.pipelines.compose);
  composePass.setBindGroup(0, state.bindGroups.compose);
  composePass.dispatchWorkgroups(renderGroupsX, renderGroupsY, 1);
  composePass.end();

  const historyReadIndex = state.historyReadIndex;
  const historyWriteIndex = 1 - historyReadIndex;
  const temporalPass = encoder.beginComputePass({
    label: 'Reproject lighting into the temporal surface cache',
    timestampWrites: gpuTimestampWrites('temporal'),
  });
  temporalPass.setPipeline(state.pipelines.temporal);
  temporalPass.setBindGroup(0, state.bindGroups.temporal[historyReadIndex]);
  temporalPass.dispatchWorkgroups(renderGroupsX, renderGroupsY, 1);
  temporalPass.end();

  encoder.copyTextureToTexture(
    { texture: state.textures.gbuffer.position },
    { texture: state.textures.historyPosition },
    [state.renderWidth, state.renderHeight, 1],
  );
  encoder.copyTextureToTexture(
    { texture: state.textures.gbuffer.normal },
    { texture: state.textures.historyNormal },
    [state.renderWidth, state.renderHeight, 1],
  );

  const denoisePass = encoder.beginComputePass({
    label: 'Denoise temporally accumulated lighting without crossing scene edges',
    timestampWrites: gpuTimestampWrites('denoise'),
  });
  denoisePass.setPipeline(state.pipelines.denoise);
  denoisePass.setBindGroup(0, state.bindGroups.denoise[historyWriteIndex]);
  denoisePass.dispatchWorkgroups(Math.ceil(state.renderWidth / 8), Math.ceil(state.renderHeight / 8), 1);
  denoisePass.end();

  const presentPass = encoder.beginRenderPass({
    label: 'Display the edge-aware SDF GI resolve',
    timestampWrites: gpuTimestampWrites('present'),
    colorAttachments: [{
      view: state.canvasContext.getCurrentTexture().createView(),
      clearValue: { r: 0.01, g: 0.02, b: 0.03, a: 1 },
      loadOp: 'clear',
      storeOp: 'store',
    }],
  });
  presentPass.setPipeline(state.pipelines.present);
  presentPass.setBindGroup(0, state.bindGroups.present);
  presentPass.draw(3);
  presentPass.end();

  if (state.showField) {
    const fieldPass = encoder.beginRenderPass({
      label: 'Visualize the live fine-cascade signed-distance slice',
      timestampWrites: gpuTimestampWrites('fieldMap'),
      colorAttachments: [{
        view: state.fieldContext.getCurrentTexture().createView(),
        clearValue: { r: 0.01, g: 0.02, b: 0.03, a: 1 },
        loadOp: 'clear',
        storeOp: 'store',
      }],
    });
    fieldPass.setPipeline(state.pipelines.fieldMap);
    fieldPass.setBindGroup(0, state.bindGroups.fieldMap);
    fieldPass.draw(3);
    fieldPass.end();
  }

  const gpuTimingSubmission = encodeGpuTimingReadback(encoder);
  device.queue.submit([encoder.finish()]);
  if (gpuTimingSubmission) readGpuTimingResults(gpuTimingSubmission);
  if (state.gdfDirtyMask !== 0) {
    const level = Math.log2(state.gdfDirtyMask);
    state.gdfCenters ||= Array(CASCADE_EXTENTS.length).fill(null);
    state.gdfCenters[level] = state.clipmapTargets[level].slice();
    state.gdfPendingMask &= ~state.gdfDirtyMask;
    state.gdfReadyMask |= state.gdfDirtyMask;
    state.gdfForceMask &= ~state.gdfDirtyMask;
  }
  state.historyReadIndex = historyWriteIndex;
  state.historyValid = state.temporalEnabled && ['gi', 'direct', 'indirect'].includes(state.mode);
  state.frameIndex++;
  requestAnimationFrame(renderFrame);
}

function invalidateSurfaceHistory() {
  state.historyValid = false;
}

function invalidateSurfaceCache() {
  state.surfaceCacheGeneration = state.surfaceCacheGeneration >= 16_777_214
    ? 1
    : state.surfaceCacheGeneration + 1;
  if (state.surfaceCacheValid) state.surfaceCacheValid.fill(0);
  state.surfaceCacheCursor = 0;
  invalidateSurfaceHistory();
}

function bindInput() {
  ui.copyGpuTimingLog.addEventListener('click', copyGpuTimingLog);
  ui.renderMode.addEventListener('change', () => {
    state.mode = ui.renderMode.value;
    if (state.mode !== state.lastMode) invalidateSurfaceHistory();
    state.lastMode = state.mode;
    if (state.mode === 'influence') {
      state.showClipmapBounds = true;
      ui.clipmapBounds.checked = true;
      setFieldCardVisible(true);
    }
  });
  ui.clipmapBounds.addEventListener('change', () => {
    state.showClipmapBounds = ui.clipmapBounds.checked;
    if (state.showClipmapBounds) setFieldCardVisible(true);
  });
  ui.temporalCache.addEventListener('change', () => {
    state.temporalEnabled = ui.temporalCache.checked;
    invalidateSurfaceHistory();
  });
  ui.quality.addEventListener('change', () => {
    state.quality = ui.quality.value;
    recreateRenderTargets();
  });
  ui.autoDrive.addEventListener('change', () => {
    state.autoDrive = ui.autoDrive.checked;
    if (state.autoDrive) state.autoYaw = state.car.yaw;
  });
  ui.driveSpeed.addEventListener('input', () => { ui.driveSpeedValue.textContent = `${Number(ui.driveSpeed.value).toFixed(1)} m/s`; });
  ui.giStrength.addEventListener('input', () => {
    state.giStrength = Number(ui.giStrength.value);
    ui.giStrengthValue.textContent = `${state.giStrength.toFixed(2)}×`;
    invalidateSurfaceCache();
  });
  ui.giRays.addEventListener('input', () => {
    state.giRays = Number(ui.giRays.value);
    ui.giRaysValue.textContent = `${state.giRays} rays / pixel`;
    invalidateSurfaceCache();
  });
  ui.skyStrength.addEventListener('input', () => {
    state.skyStrength = Number(ui.skyStrength.value);
    ui.skyStrengthValue.textContent = `${state.skyStrength.toFixed(2)}×`;
    invalidateSurfaceCache();
  });
  ui.aoStrength.addEventListener('input', () => {
    state.aoStrength = Number(ui.aoStrength.value);
    ui.aoStrengthValue.textContent = `${state.aoStrength.toFixed(2)}×`;
    invalidateSurfaceHistory();
  });
  ui.bounceCount.addEventListener('change', () => {
    state.bounces = Number(ui.bounceCount.value);
    invalidateSurfaceCache();
  });
  ui.sunIntensity.addEventListener('input', () => {
    state.sunIntensity = Number(ui.sunIntensity.value);
    ui.sunIntensityValue.textContent = state.sunIntensity.toFixed(1);
    invalidateSurfaceCache();
  });
  ui.sunAngularRadius.addEventListener('input', () => {
    state.sunAngularRadius = Number(ui.sunAngularRadius.value);
    ui.sunAngularRadiusValue.textContent = `${state.sunAngularRadius.toFixed(2)}°`;
    invalidateSurfaceCache();
  });
  ui.pointLightIntensity.addEventListener('input', () => {
    state.pointLightIntensity = Number(ui.pointLightIntensity.value);
    ui.pointLightIntensityValue.textContent = state.pointLightIntensity.toFixed(0);
    invalidateSurfaceCache();
  });
  ui.pointLightRadius.addEventListener('input', () => {
    state.pointLightRadius = Number(ui.pointLightRadius.value);
    ui.pointLightRadiusValue.textContent = `${state.pointLightRadius.toFixed(2)} m`;
    invalidateSurfaceCache();
  });
  ui.emissiveIntensity.addEventListener('input', () => {
    state.emissiveIntensity = Number(ui.emissiveIntensity.value);
    ui.emissiveIntensityValue.textContent = `${state.emissiveIntensity.toFixed(2)}×`;
    invalidateSurfaceCache();
  });
  ui.emitterSize.addEventListener('input', () => {
    state.emitterSize = Number(ui.emitterSize.value);
    ui.emitterSizeValue.textContent = `${state.emitterSize.toFixed(2)}×`;
    updateEmitterGeometry();
    invalidateSurfaceCache();
  });
  ui.emissiveSamples.addEventListener('input', () => {
    state.emissiveSamples = Number(ui.emissiveSamples.value);
    ui.emissiveSamplesValue.textContent = `${state.emissiveSamples} samples / pixel`;
    invalidateSurfaceCache();
  });
  ui.pauseButton.addEventListener('click', () => {
    state.paused = !state.paused;
    ui.pauseButton.textContent = state.paused ? 'Resume drive' : 'Pause drive';
    ui.pauseButton.classList.toggle('active', state.paused);
  });
  ui.resetButton.addEventListener('click', resetView);
  ui.fieldToggle.addEventListener('click', () => {
    setFieldCardVisible(!state.showField);
  });
  ui.canvas.addEventListener('pointerdown', event => {
    if (event.button !== 0) return;
    state.drag = { x: event.clientX, y: event.clientY };
    ui.canvas.setPointerCapture(event.pointerId);
  });
  ui.canvas.addEventListener('pointermove', event => {
    if (!state.drag) return;
    const dx = event.clientX - state.drag.x;
    const dy = event.clientY - state.drag.y;
    state.drag = { x: event.clientX, y: event.clientY };
    state.orbitYaw -= dx * 0.006;
    state.orbitPitch = Math.max(0.06, Math.min(1.12, state.orbitPitch - dy * 0.004));
  });
  ui.canvas.addEventListener('pointerup', () => { state.drag = null; });
  ui.canvas.addEventListener('pointercancel', () => { state.drag = null; });
  ui.canvas.addEventListener('wheel', event => {
    event.preventDefault();
    state.orbitDistance = Math.max(7, Math.min(35, state.orbitDistance + event.deltaY * 0.012));
  }, { passive: false });

  window.addEventListener('keydown', event => {
    const target = event.target;
    if (target instanceof HTMLInputElement || target instanceof HTMLSelectElement || target instanceof HTMLButtonElement) return;
    const key = event.key.toLowerCase();
    if (['w', 'a', 's', 'd', 'arrowup', 'arrowdown', 'arrowleft', 'arrowright', ' '].includes(key)) {
      state.keys.add(key);
      event.preventDefault();
    }
    if (key === 'r') resetView();
    if (key === 'escape') state.keys.clear();
  });
  window.addEventListener('keyup', event => state.keys.delete(event.key.toLowerCase()));
  window.addEventListener('blur', () => state.keys.clear());
  window.addEventListener('resize', recreateRenderTargets);
}

function resetView() {
  state.autoDrive = false;
  state.autoYaw = START_POSITION.yaw;
  ui.autoDrive.checked = false;
  state.car = { ...START_POSITION, speed: 0 };
  state.previousCar = { ...START_POSITION };
  state.manualSpeed = 0;
  state.steeringInput = 0;
  state.wheelRotation = 0;
  state.wheelSteering = 0;
  state.focus = [START_POSITION.x, 1.1, START_POSITION.z];
  state.orbitYaw = 0.12;
  state.orbitPitch = 0.28;
  state.orbitDistance = 18;
  if (state.world) refreshStreamedWorld();
  invalidateSurfaceHistory();
}

async function initialize() {
  if (!navigator.gpu) {
    showError('This browser does not expose WebGPU', 'Open the exhibit in a current WebGPU-capable browser over HTTPS or localhost. This page intentionally has no WebGL or native C++ SDF fallback.');
    return;
  }
  try {
    setLoading('Requesting a WebGPU adapter…');
    state.adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!state.adapter) throw new Error('No WebGPU adapter was returned by the browser.');
    const timingFeatures = state.adapter.features?.has('timestamp-query') ? ['timestamp-query'] : [];
    try {
      state.device = await state.adapter.requestDevice({ requiredFeatures: timingFeatures });
    } catch (error) {
      if (!timingFeatures.length) throw error;
      console.warn('Adapter exposed timestamp-query but declined it; retrying without GPU timing:', error);
      state.device = await state.adapter.requestDevice();
    }
    initializeGpuTiming(state.device);
    state.device.addEventListener('uncapturederror', event => {
      console.error(`WebGPU validation error: ${event.error.message}`);
    });
    state.device.lost.then(info => {
      if (info.reason !== 'destroyed') showError('The WebGPU device was lost', info.message || 'Reload the exhibit to request a new device.');
    });
    const adapterInfo = state.adapter.info || {};
    ui.adapter.textContent = adapterInfo.description || adapterInfo.device || 'WebGPU adapter';
    state.canvasContext = ui.canvas.getContext('webgpu');
    state.fieldContext = ui.fieldCanvas.getContext('webgpu');
    state.overlayContext = ui.clipmapOverlay.getContext('2d');
    if (!state.canvasContext || !state.fieldContext || !state.overlayContext) {
      throw new Error('Unable to create the WebGPU scene and distance-field debug canvases.');
    }
    state.format = navigator.gpu.getPreferredCanvasFormat();
    state.canvasContext.configure({ device: state.device, format: state.format, alphaMode: 'opaque' });
    state.fieldContext.configure({ device: state.device, format: state.format, alphaMode: 'opaque' });

    state.buffers.frame = makeUniformBuffer(state.device, FRAME_BYTES, 'Frame and clipmap uniforms');
    createVolumeResources(state.device);
    await loadAssets(state.device);
    createWorldStreamWorker();
    setLoading('Compiling standalone WGSL distance-field pipelines…');
    state.pipelines = await buildPipelines(state.device, state.format);
    createPersistentBindGroups(state.device);
    recreateRenderTargets();
    await bakeVehicleField();
    state.started = true;
    state.fpsStart = performance.now();
    ui.status.textContent = 'WEBGPU · LIVE';
    ui.status.dataset.state = 'ready';
    ui.fieldLegend.textContent = `${GRID_RESOLUTION}³ × 3 full-mesh clipmaps · ${state.worldTriangleCount.toLocaleString()} static tris · vehicle ${VEHICLE_SDF_DIMS.join(' × ')}`;
    ui.loading.textContent = `${state.vehicle.triangleCount.toLocaleString()} ControlVehicle triangles · ${state.worldTriangleCount.toLocaleString()} streamed static mesh triangles in the GDF · ${state.surfaceCacheCardCount.toLocaleString()} surface cards`;
    requestAnimationFrame(renderFrame);
  } catch (error) {
    console.error(error);
    showError('SDF GI failed to initialize', error instanceof Error ? error.message : String(error));
  }
}

async function bakeVehicleField() {
  setLoading(`Baking the real vehicle mesh into a ${VEHICLE_SDF_DIMS.join(' × ')} signed-distance volume…`);
  const encoder = state.device.createCommandEncoder({ label: 'ControlVehicle exact-mesh SDF bake' });
  const pass = encoder.beginComputePass({ label: 'BVH nearest-triangle distance and per-part parity sign' });
  pass.setPipeline(state.pipelines.bake);
  pass.setBindGroup(0, state.bindGroups.bake);
  pass.dispatchWorkgroups(
    Math.ceil(VEHICLE_SDF_DIMS[0] / 4),
    Math.ceil(VEHICLE_SDF_DIMS[1] / 4),
    Math.ceil(VEHICLE_SDF_DIMS[2] / 4),
  );
  pass.end();
  state.device.queue.submit([encoder.finish()]);
  await state.device.queue.onSubmittedWorkDone();
}

bindInput();
initialize();
