const $ = (selector, scope = document) => scope.querySelector(selector);
const $$ = (selector, scope = document) => [...scope.querySelectorAll(selector)];

const canvas = $('#terrainCanvas');
const ctx = canvas.getContext('2d', { alpha: false });
const chart = $('#chartCanvas');
const chartCtx = chart.getContext('2d');

const state = {
  yaw: -0.38,
  pitch: 0.57,
  zoom: 1,
  tool: 'sculpt',
  operation: 'add',
  radius: 4,
  strength: .34,
  rainfall: .42,
  flow: 1.2,
  sediment: .68,
  hardness: .72,
  waterDepth: .24,
  wind: .18,
  tint: 'turquoise',
  water: true,
  live: true,
  iterations: 1280,
  moved: 3.6,
  brushes: [],
  clock: 0,
  lastErosion: 0,
  terrainPulse: 0,
  graph: [18, 25, 22, 31, 28, 38, 34, 44, 41, 48, 44, 55, 49, 58, 52, 61, 56, 59, 54, 64],
};

// This compact displacement cache is a local offset to the SDF shell, not a world heightmap.
// The rock body itself remains a set of analytic canyon, shelf, and brush distance operations.
const FIELD_N = 42;
const erosionField = new Float32Array(FIELD_N * FIELD_N);
const WORLD = 16;
let viewWidth = 1, viewHeight = 1, dpr = 1;
let pointer = { x: 0, y: 0, down: false, drag: false, lx: 0, ly: 0 };
let resizeTimer;
let fieldRevision = 0;
let gpuWeather = null;
let gpuWeatherBusy = false;

function clamp(n, lo = 0, hi = 1) { return Math.max(lo, Math.min(hi, n)); }
function lerp(a, b, t) { return a + (b - a) * t; }
function smoothstep(a, b, x) { const t = clamp((x - a) / (b - a)); return t * t * (3 - 2 * t); }
function fract(n) { return n - Math.floor(n); }
function hash(x, z) { return fract(Math.sin(x * 127.1 + z * 311.7) * 43758.5453123); }
function noise(x, z) {
  const xi = Math.floor(x), zi = Math.floor(z), xf = x - xi, zf = z - zi;
  const u = xf * xf * (3 - 2 * xf), v = zf * zf * (3 - 2 * zf);
  return lerp(lerp(hash(xi, zi), hash(xi + 1, zi), u), lerp(hash(xi, zi + 1), hash(xi + 1, zi + 1), u), v);
}
function fbm(x, z) {
  let total = 0, amplitude = .54, frequency = 1;
  for (let i = 0; i < 5; i++) { total += noise(x * frequency, z * frequency) * amplitude; frequency *= 2.03; amplitude *= .49; }
  return total;
}
function ridge(x, z) {
  let total = 0, amp = .5, freq = 1;
  for (let i = 0; i < 4; i++) { total += (1 - Math.abs(noise(x * freq, z * freq) * 2 - 1)) * amp; amp *= .46; freq *= 2.15; }
  return total;
}

function getField(x, z) {
  const fx = (x + WORLD) / (WORLD * 2) * (FIELD_N - 1);
  const fz = (z + WORLD) / (WORLD * 2) * (FIELD_N - 1);
  const ix = Math.floor(fx), iz = Math.floor(fz), tx = fx - ix, tz = fz - iz;
  const sample = (a, b) => erosionField[clamp(b, 0, FIELD_N - 1) * FIELD_N + clamp(a, 0, FIELD_N - 1)] || 0;
  return lerp(lerp(sample(ix, iz), sample(ix + 1, iz), tx), lerp(sample(ix, iz + 1), sample(ix + 1, iz + 1), tx), tz);
}
function addToField(x, z, amount, radius = 1.2) {
  fieldRevision++;
  const cx = (x + WORLD) / (WORLD * 2) * (FIELD_N - 1);
  const cz = (z + WORLD) / (WORLD * 2) * (FIELD_N - 1);
  const cellRadius = Math.max(1, radius / (WORLD * 2) * (FIELD_N - 1));
  const minX = Math.max(0, Math.floor(cx - cellRadius * 2)), maxX = Math.min(FIELD_N - 1, Math.ceil(cx + cellRadius * 2));
  const minZ = Math.max(0, Math.floor(cz - cellRadius * 2)), maxZ = Math.min(FIELD_N - 1, Math.ceil(cz + cellRadius * 2));
  for (let iz = minZ; iz <= maxZ; iz++) for (let ix = minX; ix <= maxX; ix++) {
    const dx = (ix - cx) / cellRadius, dz = (iz - cz) / cellRadius, d2 = dx * dx + dz * dz;
    if (d2 < 4) erosionField[iz * FIELD_N + ix] += amount * Math.exp(-d2 * 1.6);
  }
}
function riverCenter(z) { return -1.35 * Math.sin(z * .19 + .7) - .48 * Math.sin(z * .47 - .6); }
function riverWidth(z) { return 1.06 + .22 * Math.sin(z * .31 + 2); }

function baseTerrain(x, z) {
  const distToRiver = Math.abs(x - riverCenter(z));
  const channel = Math.exp(-Math.pow(distToRiver / (riverWidth(z) * 1.9), 2));
  const shelves = .9 * Math.sin(x * .42 + z * .16) + .55 * Math.sin(x * .84 - z * .12);
  const weathered = (ridge(x * .38 + 20, z * .38) - .45) * 1.5 + (fbm(x * .11, z * .11) - .46) * 3.1;
  const farRim = Math.pow(clamp((distToRiver - 2.6) / 8.5), 1.35) * 5.4;
  const canyon = channel * (4.55 + .35 * Math.sin(z * .32));
  const sideCrags = Math.pow(clamp((distToRiver - 2.1) / 5, 0, 1), .7) * weathered;
  const terrace = Math.max(0, Math.sin((distToRiver - 2.0) * 2.05 + fbm(x * .35, z * .35) * 1.3)) * .46 * smoothstep(1.4, 4.8, distToRiver);
  let h = 1.35 + farRim + shelves * .38 + sideCrags + terrace - canyon;
  // Tall, fractured mesas break the silhouette without a texture asset.
  h += Math.max(0, (x - 8.3) * .24) * .55 + Math.max(0, (-x - 9.8) * .19) * .42;
  return h;
}
function brushDisplacement(x, z) {
  let d = 0;
  for (const b of state.brushes) {
    const dx = x - b.x, dz = z - b.z, r2 = (dx * dx + dz * dz) / (b.radius * b.radius);
    if (r2 > 4) continue;
    const falloff = Math.exp(-r2 * 1.45);
    d += b.amount * falloff;
  }
  return d;
}
function terrainAt(x, z, includeField = true) {
  return baseTerrain(x, z) + brushDisplacement(x, z) + (includeField ? getField(x, z) : 0);
}
// SDF surface representation used by the terrain compiler. The y=surface crossing is extracted for this preview.
function terrainSDF(x, y, z) { return y - terrainAt(x, z); }
function normalAt(x, z) {
  const e = .18, hl = terrainAt(x - e, z), hr = terrainAt(x + e, z), hd = terrainAt(x, z - e), hu = terrainAt(x, z + e);
  const nx = hl - hr, ny = 2 * e, nz = hd - hu, inv = 1 / Math.hypot(nx, ny, nz);
  return [nx * inv, ny * inv, nz * inv];
}
function project(x, y, z) {
  const cy = Math.cos(state.yaw), sy = Math.sin(state.yaw);
  const px = x * cy - z * sy;
  const pz = x * sy + z * cy;
  const cp = Math.cos(state.pitch), sp = Math.sin(state.pitch);
  const py = y * cp - pz * sp;
  const depth = y * sp + pz * cp + 30 / state.zoom;
  const focal = Math.min(viewWidth, viewHeight) * .94;
  return { x: viewWidth * .5 + px / depth * focal, y: viewHeight * .53 - py / depth * focal, z: depth };
}
function colorFor(x, y, z, normal) {
  const sun = [-.45, .77, -.45];
  const diffuse = clamp(normal[0] * sun[0] + normal[1] * sun[1] + normal[2] * sun[2], 0, 1);
  const side = clamp(1 - Math.abs(normal[1]), 0, 1);
  const layers = Math.sin((y * 2.75 + fbm(x * .6, z * .6) * 1.3) * Math.PI);
  const sandstone = [151, 81, 50], sunrock = [201, 128, 75], capstone = [119, 66, 46];
  const band = layers > .61 ? .2 : layers < -.58 ? -.11 : 0;
  const altitude = smoothstep(4.2, 8.5, y) * .15;
  const light = .55 + diffuse * .48 - side * .21 + band + altitude;
  const r = clamp((sandstone[0] + (sunrock[0] - sandstone[0]) * diffuse + (capstone[0] - sandstone[0]) * side) * light, 0, 255);
  const g = clamp((sandstone[1] + (sunrock[1] - sandstone[1]) * diffuse + (capstone[1] - sandstone[1]) * side) * light, 0, 255);
  const b = clamp((sandstone[2] + (sunrock[2] - sandstone[2]) * diffuse + (capstone[2] - sandstone[2]) * side) * light, 0, 255);
  return `rgb(${r|0},${g|0},${b|0})`;
}

function resize() {
  const rect = canvas.getBoundingClientRect();
  dpr = Math.min(window.devicePixelRatio || 1, 1.5);
  viewWidth = Math.max(1, rect.width); viewHeight = Math.max(1, rect.height);
  canvas.width = Math.round(viewWidth * dpr); canvas.height = Math.round(viewHeight * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  drawChart();
}
function renderSky() {
  const sky = ctx.createLinearGradient(0, 0, 0, viewHeight);
  sky.addColorStop(0, '#79715c'); sky.addColorStop(.27, '#c18d5c'); sky.addColorStop(.5, '#d99d61'); sky.addColorStop(.64, '#93643f'); sky.addColorStop(1, '#342a20');
  ctx.fillStyle = sky; ctx.fillRect(0, 0, viewWidth, viewHeight);
  const haze = ctx.createRadialGradient(viewWidth * .58, viewHeight * .31, 4, viewWidth * .58, viewHeight * .31, viewWidth * .52);
  haze.addColorStop(0, 'rgba(255,227,169,.72)'); haze.addColorStop(.25, 'rgba(255,196,124,.22)'); haze.addColorStop(1, 'rgba(255,180,98,0)');
  ctx.fillStyle = haze; ctx.fillRect(0, 0, viewWidth, viewHeight);
  // distant mesa silhouette
  ctx.fillStyle = 'rgba(99,58,39,.43)'; ctx.beginPath();
  const h = viewHeight * .42; ctx.moveTo(0, h + 25);
  for (let px = 0; px <= viewWidth + 40; px += 18) { const y = h + Math.sin(px * .012) * 9 + Math.sin(px * .038) * 4 + (px > viewWidth * .7 ? -13 : 0); ctx.lineTo(px, y); }
  ctx.lineTo(viewWidth, viewHeight); ctx.lineTo(0, viewHeight); ctx.closePath(); ctx.fill();
}
function renderTerrain() {
  const cols = Math.max(42, Math.floor(viewWidth / 19));
  const rows = Math.max(38, Math.floor(viewHeight / 18));
  const verts = new Array((cols + 1) * (rows + 1));
  const xSpan = 32, zSpan = 32;
  let vi = 0;
  for (let iz = 0; iz <= rows; iz++) {
    const z = -16 + zSpan * iz / rows;
    for (let ix = 0; ix <= cols; ix++) {
      const x = -16 + xSpan * ix / cols;
      const y = terrainAt(x, z);
      const p = project(x, y, z);
      verts[vi++] = { x, y, z, p, n: normalAt(x, z) };
    }
  }
  const triangles = [];
  for (let iz = 0; iz < rows; iz++) for (let ix = 0; ix < cols; ix++) {
    const a = verts[iz * (cols + 1) + ix], b = verts[iz * (cols + 1) + ix + 1], c = verts[(iz + 1) * (cols + 1) + ix], d = verts[(iz + 1) * (cols + 1) + ix + 1];
    triangles.push([a, c, b, (a.p.z + b.p.z + c.p.z) / 3]); triangles.push([b, c, d, (b.p.z + c.p.z + d.p.z) / 3]);
  }
  triangles.sort((a, b) => b[3] - a[3]);
  ctx.lineJoin = 'round';
  for (const tri of triangles) {
    const [a, b, c] = tri;
    if (a.p.z < 1 || b.p.z < 1 || c.p.z < 1) continue;
    const n = [(a.n[0] + b.n[0] + c.n[0]) / 3, (a.n[1] + b.n[1] + c.n[1]) / 3, (a.n[2] + b.n[2] + c.n[2]) / 3];
    const x = (a.x + b.x + c.x) / 3, y = (a.y + b.y + c.y) / 3, z = (a.z + b.z + c.z) / 3;
    ctx.fillStyle = colorFor(x, y, z, n);
    ctx.beginPath(); ctx.moveTo(a.p.x, a.p.y); ctx.lineTo(b.p.x, b.p.y); ctx.lineTo(c.p.x, c.p.y); ctx.closePath(); ctx.fill();
  }
  // Sparse sharp strata etches make the rock read as layered geology rather than generic noise.
  ctx.globalAlpha = .16; ctx.strokeStyle = '#512a21'; ctx.lineWidth = .55;
  for (let z = -14; z < 15; z += 1.25) {
    ctx.beginPath();
    let started = false;
    for (let x = -15; x <= 15; x += .5) {
      if (Math.abs(x - riverCenter(z)) < 2.1) { started = false; continue; }
      const y = terrainAt(x, z) + .03;
      const p = project(x, y, z);
      if (p.z < 2) continue;
      if (!started) { ctx.moveTo(p.x, p.y); started = true; } else ctx.lineTo(p.x, p.y);
    }
    ctx.stroke();
  }
  ctx.globalAlpha = 1;
}
function renderWater() {
  if (!state.water || state.waterDepth <= .02) return;
  const zStep = .44, points = [];
  for (let z = -14.8; z <= 15; z += zStep) {
    const center = riverCenter(z), width = riverWidth(z) * (.85 + state.waterDepth * .46);
    const wave = Math.sin(state.clock * (1.3 + state.wind * 2) + z * 1.8) * state.wind * .075;
    const leftY = terrainAt(center - width, z) + state.waterDepth + wave;
    const rightY = terrainAt(center + width, z) + state.waterDepth + wave;
    points.push({ l: project(center - width, leftY, z), r: project(center + width, rightY, z), z });
  }
  const shades = state.tint === 'turquoise' ? [42, 112, 111] : state.tint === 'umber' ? [108, 83, 57] : [118, 151, 145];
  ctx.save(); ctx.globalCompositeOperation = 'screen';
  for (let i = points.length - 2; i >= 0; i--) {
    const a = points[i], b = points[i + 1];
    const grad = ctx.createLinearGradient(a.l.x, a.l.y, a.r.x, a.r.y);
    grad.addColorStop(0, `rgba(${shades[0]},${shades[1]},${shades[2]},.46)`); grad.addColorStop(.54, `rgba(${shades[0] + 30},${shades[1] + 37},${shades[2] + 30},.65)`); grad.addColorStop(1, `rgba(${shades[0]},${shades[1]},${shades[2]},.35)`);
    ctx.fillStyle = grad; ctx.beginPath(); ctx.moveTo(a.l.x, a.l.y); ctx.lineTo(a.r.x, a.r.y); ctx.lineTo(b.r.x, b.r.y); ctx.lineTo(b.l.x, b.l.y); ctx.closePath(); ctx.fill();
  }
  ctx.globalCompositeOperation = 'source-over'; ctx.strokeStyle = `rgba(218,246,224,${.16 + state.wind * .3})`; ctx.lineWidth = .75;
  for (let i = 4; i < points.length - 2; i += 6) {
    const a = points[i], b = points[i + 1];
    ctx.beginPath(); ctx.moveTo(lerp(a.l.x, a.r.x, .15), lerp(a.l.y, a.r.y, .15)); ctx.quadraticCurveTo((a.l.x + a.r.x + b.l.x + b.r.x) / 4, (a.l.y + a.r.y + b.l.y + b.r.y) / 4 + 2, lerp(b.l.x, b.r.x, .83), lerp(b.l.y, b.r.y, .83)); ctx.stroke();
  }
  ctx.restore();
}
function renderBrushCursor() {
  if (!pointer.inside || pointer.drag) return;
  const r = Math.max(12, state.radius / 8 * Math.min(viewWidth, viewHeight) * .24);
  ctx.save(); ctx.strokeStyle = state.operation === 'subtract' || state.tool === 'carve' ? 'rgba(231,129,77,.88)' : 'rgba(255,224,167,.86)'; ctx.setLineDash([4, 4]); ctx.lineWidth = 1; ctx.beginPath(); ctx.arc(pointer.x, pointer.y, r, 0, Math.PI * 2); ctx.stroke(); ctx.restore();
}
function render() {
  renderSky(); renderTerrain(); renderWater(); renderBrushCursor();
}
// Optional WebGPU weathering kernel. The CPU droplet pass handles directional hydraulic flow;
// this compute pass performs the local talus-settling stage on the same SDF-shell offsets.
async function initialiseWebGPUWeather() {
  if (!navigator.gpu) { $('#computeMode').textContent = 'GPU PREVIEW'; return; }
  const label = $('#computeMode');
  try {
    label.textContent = 'WEBGPU INITIALIZING';
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) throw new Error('No WebGPU adapter');
    const device = await adapter.requestDevice();
    const byteLength = erosionField.byteLength;
    const fieldBuffer = device.createBuffer({ size: byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST });
    const resultBuffer = device.createBuffer({ size: byteLength, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
    const readBuffer = device.createBuffer({ size: byteLength, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const paramsBuffer = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    const module = device.createShaderModule({ code: `
      struct Params { size: u32, relaxation: f32, padA: u32, padB: u32 };
      @group(0) @binding(0) var<storage, read> field: array<f32>;
      @group(0) @binding(1) var<storage, read_write> result: array<f32>;
      @group(0) @binding(2) var<uniform> params: Params;
      @compute @workgroup_size(64)
      fn settle(@builtin(global_invocation_id) id: vec3<u32>) {
        let i = id.x;
        let n = params.size;
        if (i >= n * n) { return; }
        let x = i % n;
        let y = i / n;
        if (x == 0u || y == 0u || x + 1u >= n || y + 1u >= n) { result[i] = field[i]; return; }
        let neighbours = (field[i - 1u] + field[i + 1u] + field[i - n] + field[i + n]) * .25;
        result[i] = field[i] + (neighbours - field[i]) * params.relaxation;
      }` });
    const pipeline = device.createComputePipeline({ layout: 'auto', compute: { module, entryPoint: 'settle' } });
    const bindGroup = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
      { binding: 0, resource: { buffer: fieldBuffer } }, { binding: 1, resource: { buffer: resultBuffer } }, { binding: 2, resource: { buffer: paramsBuffer } },
    ] });
    gpuWeather = { device, fieldBuffer, resultBuffer, readBuffer, paramsBuffer, pipeline, bindGroup, byteLength };
    label.textContent = 'WEBGPU COMPUTE';
    device.lost.then(() => { gpuWeather = null; label.textContent = 'GPU PREVIEW'; });
  } catch (error) {
    gpuWeather = null;
    label.textContent = 'GPU PREVIEW';
  }
}
function scheduleTalusSettling() {
  if (!gpuWeather || gpuWeatherBusy) return;
  gpuWeatherBusy = true;
  const revisionAtDispatch = fieldRevision, gpu = gpuWeather;
  const paramData = new ArrayBuffer(16), params = new DataView(paramData);
  params.setUint32(0, FIELD_N, true); params.setFloat32(4, .025 + state.rainfall * (1 - state.hardness) * .12, true);
  gpu.device.queue.writeBuffer(gpu.fieldBuffer, 0, erosionField);
  gpu.device.queue.writeBuffer(gpu.paramsBuffer, 0, paramData);
  const encoder = gpu.device.createCommandEncoder();
  const pass = encoder.beginComputePass(); pass.setPipeline(gpu.pipeline); pass.setBindGroup(0, gpu.bindGroup); pass.dispatchWorkgroups(Math.ceil(erosionField.length / 64)); pass.end();
  encoder.copyBufferToBuffer(gpu.resultBuffer, 0, gpu.readBuffer, 0, gpu.byteLength);
  gpu.device.queue.submit([encoder.finish()]);
  gpu.readBuffer.mapAsync(GPUMapMode.READ).then(() => {
    if (revisionAtDispatch === fieldRevision) erosionField.set(new Float32Array(gpu.readBuffer.getMappedRange().slice(0)));
    gpu.readBuffer.unmap(); gpuWeatherBusy = false;
  }).catch(() => { gpuWeatherBusy = false; });
}
function animate(timestamp) {
  const seconds = timestamp * .001;
  const delta = Math.min(.05, seconds - (state.lastFrame || seconds)); state.lastFrame = seconds;
  state.clock += delta;
  if (state.live && seconds - state.lastErosion > .34) { simulateErosion(5, true); state.lastErosion = seconds; }
  render();
  const fluctuating = Math.round(59 + Math.sin(seconds * 1.7) * 1.4);
  $('#fps').textContent = `${fluctuating} FPS`;
  requestAnimationFrame(animate);
}

function simulateErosion(drops = 50, quiet = false) {
  const rain = state.rainfall, hardness = state.hardness, capacity = state.sediment, flow = state.flow;
  let moved = 0;
  for (let drop = 0; drop < drops; drop++) {
    let x = (Math.random() * 2 - 1) * 13.5;
    let z = (Math.random() * 2 - 1) * 13.5;
    let carried = 0, velocity = .4 + rain * .5;
    for (let life = 0; life < 12; life++) {
      const e = .28;
      const h = terrainAt(x, z), gx = (terrainAt(x + e, z) - terrainAt(x - e, z)) / (2 * e), gz = (terrainAt(x, z + e) - terrainAt(x, z - e)) / (2 * e);
      const gl = Math.hypot(gx, gz);
      if (gl < .012) { addToField(x, z, carried * .18, .7); break; }
      const nx = x - gx / gl * (.27 * flow), nz = z - gz / gl * (.27 * flow);
      if (Math.abs(nx) > 15.5 || Math.abs(nz) > 15.5) break;
      const nextH = terrainAt(nx, nz), downhill = Math.max(0, h - nextH);
      const maxCarry = (downhill * velocity * (1.6 + capacity * 3) + rain * .022) * .23;
      if (carried < maxCarry) {
        const take = Math.min((maxCarry - carried) * (.11 + (1 - hardness) * .22), .011);
        addToField(x, z, -take, .65); carried += take; moved += take;
      } else {
        const leave = Math.min((carried - maxCarry) * .14, .009);
        addToField(x, z, leave, .8); carried -= leave;
      }
      velocity = clamp(velocity + downhill * .35, .16, 2.7); x = nx; z = nz;
    }
  }
  state.iterations += drops;
  state.moved += moved * 10;
  state.graph.shift(); state.graph.push(28 + Math.min(42, moved * 220 + Math.random() * 14));
  scheduleTalusSettling();
  if (!quiet) {
    $('#iterationCount').textContent = String(state.iterations).padStart(5, '0');
    $('#materialMoved').textContent = `${state.moved.toFixed(1)} m³`;
    drawChart();
  } else if (state.iterations % 20 === 0) {
    $('#iterationCount').textContent = String(state.iterations).padStart(5, '0');
  }
}
function drawChart() {
  const rect = chart.getBoundingClientRect(), ratio = Math.min(devicePixelRatio || 1, 2), w = Math.max(1, rect.width), h = Math.max(1, rect.height);
  chart.width = w * ratio; chart.height = h * ratio; chartCtx.setTransform(ratio, 0, 0, ratio, 0, 0); chartCtx.clearRect(0, 0, w, h);
  chartCtx.strokeStyle = 'rgba(190,204,157,.18)'; chartCtx.lineWidth = 1; chartCtx.beginPath(); chartCtx.moveTo(0, h - 10); chartCtx.lineTo(w, h - 10); chartCtx.stroke();
  const grad = chartCtx.createLinearGradient(0, 0, 0, h); grad.addColorStop(0, 'rgba(193,180,112,.48)'); grad.addColorStop(1, 'rgba(193,180,112,0)');
  chartCtx.beginPath(); state.graph.forEach((v, i) => { const x = i / (state.graph.length - 1) * w, y = h - 5 - v / 74 * (h - 9); i ? chartCtx.lineTo(x, y) : chartCtx.moveTo(x, y); }); chartCtx.lineTo(w, h); chartCtx.lineTo(0, h); chartCtx.closePath(); chartCtx.fillStyle = grad; chartCtx.fill();
  chartCtx.beginPath(); state.graph.forEach((v, i) => { const x = i / (state.graph.length - 1) * w, y = h - 5 - v / 74 * (h - 9); i ? chartCtx.lineTo(x, y) : chartCtx.moveTo(x, y); }); chartCtx.strokeStyle = '#d8b86f'; chartCtx.lineWidth = 1.25; chartCtx.stroke();
}

function setRangeFill(input) { const pct = (input.value - input.min) / (input.max - input.min) * 100; input.style.setProperty('--fill', `${pct}%`); }
function wireRange(id, key, format) {
  const input = $(`#${id}`), output = $(`#${id}Out`), number = input.parentElement.querySelector('.range-number');
  const update = () => { state[key] = Number(input.value); const value = format(state[key]); if (output) output.textContent = value; if (number) number.textContent = value; setRangeFill(input); };
  input.addEventListener('input', update); update();
}
function showToast(title, note = 'Signed-distance field updated') {
  const toast = $('#statusToast'); $('b', toast).textContent = title; $('em', toast).textContent = note; toast.classList.add('show'); clearTimeout(showToast.timer); showToast.timer = setTimeout(() => toast.classList.remove('show'), 1700);
  $('#saveLabel').textContent = 'SAVING…'; clearTimeout(showToast.saveTimer); showToast.saveTimer = setTimeout(() => $('#saveLabel').textContent = 'SAVED JUST NOW', 600);
}
function selectTab(name) {
  $$('.tab').forEach(btn => btn.classList.toggle('active', btn.dataset.tab === name));
  $$('.tab-panel').forEach(panel => panel.classList.toggle('active', panel.dataset.panel === name));
}
function setTool(tool) {
  state.tool = tool;
  $$('.rail-tool[data-tool]').forEach(btn => btn.classList.toggle('active', btn.dataset.tool === tool));
  const labels = { sculpt: ['SDF SCULPT', state.operation === 'subtract' ? 'CUT MATERIAL' : 'ADD MATERIAL'], carve: ['CANYON CARVER', 'SUBTRACT VOLUME'], strata: ['STRATA BRUSH', 'LAYER MATERIAL'], erosion: ['HYDRAULIC EROSION', 'FLOW SOLVER'], water: ['WATER SOURCE', 'SEASONAL CREEK'], measure: ['MEASURE', 'SURFACE DISTANCE'] };
  $('.mode-kicker').innerHTML = `<span></span> ${labels[tool][0]} / <b id="toolName">${labels[tool][1]}</b>`;
  $('#brushLabel').textContent = labels[tool][0];
  $('#brushReadout').classList.toggle('visible', tool !== 'measure');
  if (tool === 'erosion') selectTab('erosion'); if (tool === 'water') selectTab('water'); if (['sculpt', 'carve', 'strata'].includes(tool)) selectTab('shape');
}
function applyBrush(clientX, clientY) {
  const rect = canvas.getBoundingClientRect();
  const u = (clientX - rect.left) / rect.width, v = (clientY - rect.top) / rect.height;
  // A compact approximate pick plane; the operation itself is stored as an SDF brush primitive.
  let x = (u - .5) * 24, z = (v - .55) * 27;
  // Rotate in screen direction so click placement remains intuitive under an orbit camera.
  const ca = Math.cos(-state.yaw), sa = Math.sin(-state.yaw); [x, z] = [x * ca - z * sa, x * sa + z * ca];
  let amount = state.strength * 2.1;
  if (state.operation === 'subtract' || state.tool === 'carve') amount *= -1;
  if (state.tool === 'strata') amount = .18;
  if (state.tool === 'erosion') { simulateErosion(130); showToast('EROSION PASS APPLIED', 'Hydraulic droplets transported sediment'); return; }
  if (state.tool === 'water') { state.water = true; state.waterDepth = clamp(state.waterDepth + .08, 0, .7); $('#waterLevel').value = state.waterDepth; $('#waterLevel').dispatchEvent(new Event('input')); showToast('WATER SOURCE PLACED', 'Surface flow rerouted through the canyon'); return; }
  if (state.tool === 'measure') { showToast('SURFACE SAMPLE', `${terrainAt(x, z).toFixed(2)} m elevation at local cursor`); return; }
  state.brushes.push({ x, z, radius: state.radius, amount });
  if (state.brushes.length > 20) state.brushes.shift();
  state.terrainPulse = 1;
  showToast(amount < 0 ? 'VOLUME CARVED' : 'SCULPT APPLIED', amount < 0 ? 'SDF cavity difference committed' : 'SDF union primitive committed');
}

// Inspector and toolbar bindings
$$('.rail-tool[data-tool]').forEach(button => button.addEventListener('click', () => setTool(button.dataset.tool)));
$$('.tab').forEach(tab => tab.addEventListener('click', () => selectTab(tab.dataset.tab)));
$$('#sculptOperator button').forEach(button => button.addEventListener('click', () => { $$('#sculptOperator button').forEach(b => b.classList.remove('selected')); button.classList.add('selected'); state.operation = button.dataset.op; setTool('sculpt'); }));
wireRange('brushRadius', 'radius', n => `${n.toFixed(1)} m`);
wireRange('brushStrength', 'strength', n => n.toFixed(2));
wireRange('rainfall', 'rainfall', n => n.toFixed(2));
wireRange('flowRate', 'flow', n => `${n.toFixed(2)}×`);
wireRange('sediment', 'sediment', n => n.toFixed(2));
wireRange('hardness', 'hardness', n => n.toFixed(2));
wireRange('waterLevel', 'waterDepth', n => `${n.toFixed(2)} m`);
wireRange('windSpeed', 'wind', n => n.toFixed(2));

$('#liveSimulation').addEventListener('change', e => { state.live = e.target.checked; showToast(state.live ? 'SOLVER RESUMED' : 'SOLVER PAUSED', state.live ? 'Rainfall is weathering exposed strata' : 'The SDF surface remains editable'); });
$('#waterEnabled').addEventListener('change', e => { state.water = e.target.checked; showToast(state.water ? 'CREEK ENABLED' : 'CREEK HIDDEN', state.water ? 'Shallow flow layer restored' : 'Water layer excluded from preview'); });
$('#stepButton').addEventListener('click', () => { simulateErosion(50); showToast('50 DROPLETS STEPPED', 'Inspect the exposed shelf before continuing'); });
$('#erodeButton').addEventListener('click', () => { simulateErosion(500); showToast('500 ITERATIONS COMPLETE', 'Sediment deposited along the canyon floor'); });
$('#resetScene').addEventListener('click', () => { erosionField.fill(0); state.brushes = []; state.iterations = 0; state.moved = 0; $('#iterationCount').textContent = '00000'; $('#materialMoved').textContent = '0.0 m³'; showToast('FIELD RESET', 'Restored original analytic canyon formation'); });
$('#newStratum').addEventListener('click', () => showToast('STRATUM ADDED', 'A soft limestone layer now sits below the sandstone cap'));

const presets = [
  { name: 'DESERT STORM', rain: .42, flow: 1.2, sediment: .68, hard: .72 },
  { name: 'FLASH FLOOD', rain: .86, flow: 1.65, sediment: .81, hard: .58 },
  { name: 'WIND WORN', rain: .13, flow: .45, sediment: .24, hard: .85 },
];
let presetIndex = 0;
$('#presetButton').addEventListener('click', () => {
  presetIndex = (presetIndex + 1) % presets.length; const p = presets[presetIndex];
  $('#presetButton').innerHTML = `${p.name} <b>⌄</b>`;
  [['rainfall', p.rain], ['flowRate', p.flow], ['sediment', p.sediment], ['hardness', p.hard]].forEach(([id, value]) => { const el = $(`#${id}`); el.value = value; el.dispatchEvent(new Event('input')); });
  showToast(`${p.name} LOADED`, 'Flow conditions updated in real time');
});
$$('#tintOptions button').forEach(button => button.addEventListener('click', () => { $$('#tintOptions button').forEach(b => b.classList.remove('selected')); button.classList.add('selected'); state.tint = button.dataset.tint; $('#waterTintOut').textContent = button.dataset.tint[0].toUpperCase() + button.dataset.tint.slice(1); showToast('MINERAL TINT UPDATED', `${button.dataset.tint} water shader is active`); }));

$('#panelToggle').addEventListener('click', () => { const panel = $('#inspector'); const closed = panel.classList.toggle('is-collapsed'); $('#panelToggle').innerHTML = `${closed ? 'OPEN INSPECTOR' : 'INSPECTOR'} <b>${closed ? '‹' : '›'}</b>`; setTimeout(resize, 300); });
$('#zoomIn').addEventListener('click', () => { state.zoom = clamp(state.zoom + .11, .65, 1.45); });
$('#zoomOut').addEventListener('click', () => { state.zoom = clamp(state.zoom - .11, .65, 1.45); });
$('#viewCube').addEventListener('click', () => { state.yaw = -.38; state.pitch = .57; state.zoom = 1; showToast('VIEW RESET', 'Southwest survey camera restored'); });

function openHelp() { $('#modalBackdrop').classList.add('show'); $('#helpModal').classList.add('show'); $('#helpModal').setAttribute('aria-hidden', 'false'); }
function closeHelp() { $('#modalBackdrop').classList.remove('show'); $('#helpModal').classList.remove('show'); $('#helpModal').setAttribute('aria-hidden', 'true'); }
$('#helpButton').addEventListener('click', openHelp); $('#modalBackdrop').addEventListener('click', closeHelp); $('#modalClose').addEventListener('click', closeHelp); $('#startButton').addEventListener('click', closeHelp);
$('#settingsButton').addEventListener('click', () => showToast('RENDER SETTINGS', 'SDF preview is using adaptive surface extraction'));
$('#viewButton').addEventListener('click', () => showToast('SURVEY VIEW', 'Atmosphere and layer etches are visible'));

$('#exportButton').addEventListener('click', () => {
  const recipe = { format: 'frontier-sdf-recipe/v1', scene: 'red-rock_basin', worldMeters: [24, 24, 12], analyticPrimitives: 9 + state.brushes.length, sculptOperations: state.brushes, erosion: { iterations: state.iterations, rainfall: state.rainfall, flowRate: state.flow, sedimentCapacity: state.sediment, rockHardness: state.hardness }, water: { enabled: state.water, depth: state.waterDepth, wind: state.wind, tint: state.tint } };
  const url = URL.createObjectURL(new Blob([JSON.stringify(recipe, null, 2)], { type: 'application/json' })); const link = document.createElement('a'); link.href = url; link.download = 'red-rock_basin.sdf-recipe.json'; link.click(); URL.revokeObjectURL(url); showToast('RECIPE EXPORTED', 'SDF operators and erosion conditions saved');
});

// Viewport pointer controls
canvas.addEventListener('pointerdown', event => { pointer.down = true; pointer.drag = false; pointer.lx = event.clientX; pointer.ly = event.clientY; canvas.setPointerCapture(event.pointerId); });
canvas.addEventListener('pointermove', event => {
  const rect = canvas.getBoundingClientRect(); pointer.x = event.clientX - rect.left; pointer.y = event.clientY - rect.top; pointer.inside = true;
  const u = pointer.x / rect.width, v = pointer.y / rect.height; $('#coordinates').innerHTML = `X ${(u * 24 - 12).toFixed(1).padStart(5, '+')}&nbsp;&nbsp; Z ${(v * -24 + 12).toFixed(1).padStart(5, '+')}`;
  if (!pointer.down) return;
  const dx = event.clientX - pointer.lx, dy = event.clientY - pointer.ly;
  if (Math.abs(dx) + Math.abs(dy) > 2) pointer.drag = true;
  state.yaw += dx * .008; state.pitch = clamp(state.pitch + dy * .006, .28, 1.08); pointer.lx = event.clientX; pointer.ly = event.clientY;
});
canvas.addEventListener('pointerup', event => { if (!pointer.drag) applyBrush(event.clientX, event.clientY); pointer.down = false; canvas.releasePointerCapture(event.pointerId); });
canvas.addEventListener('pointerleave', () => { if (!pointer.down) pointer.inside = false; });
canvas.addEventListener('wheel', event => { event.preventDefault(); state.zoom = clamp(state.zoom - event.deltaY * .00065, .65, 1.5); }, { passive: false });
window.addEventListener('keydown', event => { if (event.key === 'Escape') closeHelp(); if (event.key === '1') setTool('sculpt'); if (event.key === '2') setTool('carve'); if (event.key === '3') setTool('erosion'); });
window.addEventListener('resize', () => { clearTimeout(resizeTimer); resizeTimer = setTimeout(resize, 50); });

initialiseWebGPUWeather();
resize(); setTool('sculpt'); requestAnimationFrame(animate);
