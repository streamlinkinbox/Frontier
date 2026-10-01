const LOD_LAYOUTS = Object.freeze([
  Object.freeze({ nx: 80, ny: 64, nz: 80, cellSize: 0.4 }),
  Object.freeze({ nx: 48, ny: 40, nz: 48, cellSize: 0.75 }),
  Object.freeze({ nx: 40, ny: 32, nz: 40, cellSize: 1.2 }),
]);
const DEFAULT_LEVEL = 1;
const MAX_CELLS = Math.max(...LOD_LAYOUTS.map(({ nx, ny, nz }) => nx * ny * nz));
export const MAX_EMITTERS = 20;
export const VRAM_BUDGET_BYTES = 250 * 1024 * 1024;
export const VRAM_BUDGET_RESERVE_BYTES = 8 * 1024 * 1024;
const MAX_SOURCES = MAX_EMITTERS;
const MAX_STEPS_PER_FRAME = 4;
const SOURCE_WINDOW_RELEASE_DELAY = 6.0;
export const FIXED_STEP = 1 / 60;

const WORKGROUP = [4, 4, 4];
const PARAM_FLOATS = 24 + MAX_SOURCES * 12;
const PARAM_BYTES = PARAM_FLOATS * Float32Array.BYTES_PER_ELEMENT;

const COMMON = /* wgsl */ `
struct Source {
  positionAge: vec4<f32>,
  shape: vec4<f32>,
  detail: vec4<f32>,
};
struct SimParams {
  dims: vec4<i32>,
  originH: vec4<f32>,
  clock: vec4<f32>,
  control: vec4<f32>,
  wind: vec4<f32>,
  feed: vec4<f32>,
  sources: array<Source, ${MAX_SOURCES}>,
};
@group(0) @binding(0) var<uniform> params: SimParams;

fn inDomain(c: vec3<i32>) -> bool {
  return all(c >= vec3<i32>(0)) && all(c < params.dims.xyz);
}
fn cellIndex(c: vec3<i32>) -> u32 {
  return u32(c.x + params.dims.x * (c.y + params.dims.y * c.z));
}
fn worldAt(c: vec3<i32>) -> vec3<f32> {
  return params.originH.xyz + (vec3<f32>(c) + vec3<f32>(0.5)) * params.originH.w;
}
fn hash3(p: vec3<f32>) -> f32 {
  return fract(sin(dot(p, vec3<f32>(127.1, 311.7, 74.7))) * 43758.5453);
}
fn noise3(p: vec3<f32>) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (vec3<f32>(3.0) - 2.0 * f);
  let a = mix(hash3(i), hash3(i + vec3<f32>(1.0, 0.0, 0.0)), u.x);
  let b = mix(hash3(i + vec3<f32>(0.0, 1.0, 0.0)), hash3(i + vec3<f32>(1.0, 1.0, 0.0)), u.x);
  let c = mix(hash3(i + vec3<f32>(0.0, 0.0, 1.0)), hash3(i + vec3<f32>(1.0, 0.0, 1.0)), u.x);
  let d = mix(hash3(i + vec3<f32>(0.0, 1.0, 1.0)), hash3(i + vec3<f32>(1.0, 1.0, 1.0)), u.x);
  return mix(mix(a, b, u.y), mix(c, d, u.y), u.z);
}
`;

function samplerFunctions(name, bufferName) {
  const load = `load${name}`;
  return /* wgsl */ `
fn ${load}(c: vec3<i32>) -> vec4<f32> {
  if (!inDomain(c)) { return vec4<f32>(0.0); }
  return ${bufferName}[cellIndex(c)];
}
fn sample${name}(g: vec3<f32>) -> vec4<f32> {
  let b = floor(g);
  let f = g - b;
  let i = vec3<i32>(b);
  let c000 = ${load}(i);
  let c100 = ${load}(i + vec3<i32>(1, 0, 0));
  let c010 = ${load}(i + vec3<i32>(0, 1, 0));
  let c110 = ${load}(i + vec3<i32>(1, 1, 0));
  let c001 = ${load}(i + vec3<i32>(0, 0, 1));
  let c101 = ${load}(i + vec3<i32>(1, 0, 1));
  let c011 = ${load}(i + vec3<i32>(0, 1, 1));
  let c111 = ${load}(i + vec3<i32>(1, 1, 1));
  return mix(
    mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
    mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y),
    f.z
  );
}
`;
}

const ADVECTION = COMMON + /* wgsl */ `
@group(0) @binding(1) var<storage, read> velocityIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> gasIn: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> velocityTmp: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> gasTmp: array<vec4<f32>>;
${samplerFunctions('VelocityIn', 'velocityIn')}
${samplerFunctions('GasIn', 'gasIn')}
@compute @workgroup_size(${WORKGROUP.join(', ')})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!inDomain(c)) { return; }
  let id = cellIndex(c);
  let h = params.originH.w;
  let dt = params.clock.x;
  let cell = velocityIn[id].xyz;
  let departure = vec3<f32>(c) - cell * (dt / h);
  velocityTmp[id] = sampleVelocityIn(departure);
  gasTmp[id] = sampleGasIn(departure);
}
`;

const CORRECTION = COMMON + /* wgsl */ `
@group(0) @binding(1) var<storage, read> velocityIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> gasIn: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read> velocityTmp: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> gasTmp: array<vec4<f32>>;
@group(0) @binding(5) var<storage, read_write> velocityOut: array<vec4<f32>>;
@group(0) @binding(6) var<storage, read_write> gasOut: array<vec4<f32>>;
${samplerFunctions('VelocityTmp', 'velocityTmp')}
${samplerFunctions('GasTmp', 'gasTmp')}
@compute @workgroup_size(${WORKGROUP.join(', ')})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!inDomain(c)) { return; }
  let id = cellIndex(c);
  let h = params.originH.w;
  let dt = params.clock.x;
  let sourceVelocity = velocityIn[id];
  let departure = vec3<f32>(c) - sourceVelocity.xyz * (dt / h);
  let arrival = vec3<f32>(c) + sourceVelocity.xyz * (dt / h);
  let backVelocity = sampleVelocityTmp(arrival);
  let backGas = sampleGasTmp(arrival);
  var correctedVelocity = velocityTmp[id] + 0.5 * (sourceVelocity - backVelocity);
  var correctedGas = gasTmp[id] + 0.5 * (gasIn[id] - backGas);

  let base = vec3<i32>(floor(departure));
  var velocityMin = vec4<f32>(1e8);
  var velocityMax = vec4<f32>(-1e8);
  var gasMin = vec4<f32>(1e8);
  var gasMax = vec4<f32>(-1e8);
  for (var k = 0u; k < 8u; k++) {
    let offset = vec3<i32>(i32(k & 1u), i32((k >> 1u) & 1u), i32((k >> 2u) & 1u));
    let sampleCell = base + offset;
    let velocity = vec4<f32>(0.0);
    var safeVelocity = velocity;
    var safeGas = vec4<f32>(0.0);
    if (inDomain(sampleCell)) {
      safeVelocity = velocityIn[cellIndex(sampleCell)];
      safeGas = gasIn[cellIndex(sampleCell)];
    }
    velocityMin = min(velocityMin, safeVelocity);
    velocityMax = max(velocityMax, safeVelocity);
    gasMin = min(gasMin, safeGas);
    gasMax = max(gasMax, safeGas);
  }

  let lastCell = vec3<f32>(params.dims.xyz - vec3<i32>(1));
  let departureInside = all(departure >= vec3<f32>(0.0)) && all(departure <= lastCell);
  if (!departureInside) {
    correctedVelocity = velocityTmp[id];
    correctedGas = gasTmp[id];
  }
  correctedVelocity = clamp(correctedVelocity, velocityMin, velocityMax);
  correctedGas = clamp(correctedGas, gasMin, gasMax);
  correctedVelocity.w = 0.0;
  correctedGas = max(correctedGas, vec4<f32>(0.0));
  gasOut[id].w = clamp(correctedGas.w, 0.0, 1.0);
  gasOut[id].xyz = correctedGas.xyz;
  velocityOut[id] = correctedVelocity;
}
`;

const CURL = COMMON + /* wgsl */ `
@group(0) @binding(1) var<storage, read> velocityIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> curlOut: array<vec4<f32>>;
fn velocityAt(c: vec3<i32>) -> vec3<f32> {
  if (!inDomain(c)) { return vec3<f32>(0.0); }
  return velocityIn[cellIndex(c)].xyz;
}
@compute @workgroup_size(${WORKGROUP.join(', ')})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!inDomain(c)) { return; }
  let x = vec3<i32>(1, 0, 0);
  let y = vec3<i32>(0, 1, 0);
  let z = vec3<i32>(0, 0, 1);
  let left = velocityAt(c - x); let right = velocityAt(c + x);
  let down = velocityAt(c - y); let up = velocityAt(c + y);
  let back = velocityAt(c - z); let front = velocityAt(c + z);
  let scale = 0.5 / params.originH.w;
  let curl = vec3<f32>(
    (up.z - down.z) - (front.y - back.y),
    (front.x - back.x) - (right.z - left.z),
    (right.y - left.y) - (up.x - down.x)
  ) * scale;
  curlOut[cellIndex(c)] = vec4<f32>(curl, length(curl));
}
`;

const FORCES = COMMON + /* wgsl */ `
@group(0) @binding(1) var<storage, read> velocityIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> gasIn: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read> curlIn: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> velocityOut: array<vec4<f32>>;
@group(0) @binding(5) var<storage, read_write> gasOut: array<vec4<f32>>;
fn curlMagnitudeAt(c: vec3<i32>) -> f32 {
  if (!inDomain(c)) { return 0.0; }
  return curlIn[cellIndex(c)].w;
}
@compute @workgroup_size(${WORKGROUP.join(', ')})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!inDomain(c)) { return; }
  let id = cellIndex(c);
  let h = params.originH.w;
  let dt = params.clock.x;
  let wp = worldAt(c);
  var velocity = velocityIn[id].xyz;
  var gas = gasIn[id];

  let twoX = vec3<i32>(2, 0, 0);
  let twoY = vec3<i32>(0, 2, 0);
  let twoZ = vec3<i32>(0, 0, 2);
  let curlGradient = vec3<f32>(
    curlMagnitudeAt(c + twoX) - curlMagnitudeAt(c - twoX),
    curlMagnitudeAt(c + twoY) - curlMagnitudeAt(c - twoY),
    curlMagnitudeAt(c + twoZ) - curlMagnitudeAt(c - twoZ)
  ) * (0.25 / h);
  let gradientLength = length(curlGradient);
  if (gradientLength > 1e-5) {
    velocity += params.clock.w * h * cross(curlGradient / gradientLength, curlIn[id].xyz) * dt;
  }

  var plumeRetention = 0.0;
  for (var i = 0; i < ${MAX_SOURCES}; i++) {
    if (i >= params.dims.w) { break; }
    let source = params.sources[i];
    let age = source.positionAge.w;
    let radius = max(source.shape.x, 0.2);
    let intensity = max(source.shape.y, 0.0);
    if (age > source.shape.z) { continue; }
    let offset = wp - source.positionAge.xyz;
    let seed = source.detail.x;
    let mode = source.shape.w;
    let heightAboveSource = max(offset.y, 0.0);
    let reachScale = max(params.wind.w, 1.0);
    var allowedRadius = radius * 2.1 + heightAboveSource * 0.12 * reachScale;
    if (mode < 0.5) {
      allowedRadius = min(7.2 * reachScale, radius * 2.1 + (age * 0.55 + heightAboveSource * 0.10) * reachScale);
    } else {
      allowedRadius = min(4.3 * reachScale, allowedRadius);
    }
    let support = 1.0 - smoothstep(0.0, 1.1 * reachScale, length(offset.xz) - allowedRadius);
    plumeRetention = max(plumeRetention, support);

    if (mode < 0.5) {
      // A short, expanding pressure shell and one finite hot-gas charge.
      let distance = length(offset);
      let core = exp(-dot(offset, offset) / (radius * radius * 1.8));
      let shellRadius = radius * (0.72 + age * 2.0);
      let shellWidth = radius * 0.46 + 0.16;
      let shell = exp(-pow((distance - shellRadius) / shellWidth, 2.0));
      let pulse = exp(-age * 2.5);
      let roughness = 0.72 + 0.28 * noise3(wp * 1.3 + vec3<f32>(seed, seed * 0.31, seed * 0.73));
      if (age < 0.17) {
        let ignition = core * (1.0 - smoothstep(0.10, 0.17, age));
        gas.z += intensity * ignition * dt * 14.0 * params.feed.x;
        gas.y = max(gas.y, 5.0 * ignition * params.feed.x);
        gas.x += intensity * ignition * dt * 0.65 * params.feed.x;
        gas.w = max(gas.w, 0.9 * ignition * params.feed.x);
      }
      let radial = offset / max(distance, 0.08);
      velocity += radial * source.detail.y * shell * pulse * roughness * dt * 2.0;
      velocity.y += source.detail.z * core * pulse * dt;
    } else {
      // A narrow ignition kernel feeds the upward-moving fire; it never fills the whole volume.
      let plumeRadius = radius + max(offset.y, 0.0) * 0.08;
      let radial = exp(-dot(offset.xz, offset.xz) / (plumeRadius * plumeRadius));
      let vertical = exp(-max(offset.y, 0.0) / max(source.detail.z * 0.35, 1.0));
      let base = exp(-max(-offset.y, 0.0) * 5.0);
      let flicker = 0.82 + 0.18 * sin(params.clock.y * 13.0 + seed + offset.y * 1.9 + noise3(wp * 0.8 + vec3<f32>(seed)));
      let ignition = radial * vertical * base * flicker;
      gas.z += intensity * ignition * dt * 3.1 * params.feed.x;
      gas.y = max(gas.y, 2.8 * ignition * params.feed.x);
      gas.w = max(gas.w, 0.76 * ignition * params.feed.x);
      velocity.y += 6.5 * ignition * dt;
      let swirl = vec3<f32>(-offset.z, 0.0, offset.x) / max(length(offset.xz), 0.2);
      velocity += swirl * source.detail.w * ignition * dt;
    }
  }

  // Fuel burns only while hot. Temperature, soot, and buoyancy have separate decay rates.
  let hot = smoothstep(0.10, 0.62, gas.y);
  let burn = min(gas.z, gas.z * params.control.x * hot * dt);
  gas.z = max(gas.z - burn, 0.0);
  gas.y += burn * 5.2;
  gas.x += burn * params.control.y;
  gas.w = mix(gas.w, 0.86, clamp(burn * 4.0, 0.0, 1.0));
  gas.y *= exp(-1.55 * dt);
  gas.x *= exp(-params.control.z * dt);
  gas.z *= exp(-0.32 * dt);

  // Keep each live event's wake local. The smooth horizontal leash prevents a long-running
  // emitter from painting the entire fixed volume; once emitters stop, ordinary dissipation wins.
  if (params.dims.w > 0) {
    let escaped = 1.0 - clamp(plumeRetention, 0.0, 1.0);
    gas.x *= exp(-dt * escaped * 2.8);
    gas.y *= exp(-dt * escaped * 3.8);
    gas.z *= exp(-dt * escaped * 4.2);
  }

  velocity.y += (gas.y * params.clock.z - gas.x * 0.22) * dt;
  velocity += params.wind.xyz * dt * 0.42;
  velocity += (params.wind.xyz - velocity) * min(dt * 0.08, 0.01);
  let driftNoise = vec3<f32>(
    sin(wp.y * 0.9 + params.clock.y * 0.8 + wp.z * 0.17),
    sin(wp.x * 0.21 - params.clock.y * 0.65 + wp.z * 0.11),
    cos(wp.x * 0.14 + params.clock.y * 0.72 + wp.y * 0.37)
  );
  velocity += driftNoise * params.control.w * dt * (0.22 + gas.y * 0.05);

  // Open boundaries are softly absorbed; smoke can leave the box instead of accumulating on its walls.
  let sideCells = min(min(c.x, params.dims.x - 1 - c.x), min(c.z, params.dims.z - 1 - c.z));
  let spongeWidth = clamp(5.0 / h, 2.0, 12.0);
  let sideLoss = 1.0 - smoothstep(0.0, spongeWidth, f32(sideCells));
  let topCells = params.dims.y - 1 - c.y;
  let topLoss = 1.0 - smoothstep(0.0, spongeWidth, f32(topCells));
  let sponge = max(sideLoss, topLoss);
  gas.x *= exp(-dt * sponge * 1.6);
  gas.z *= exp(-dt * sponge * 2.2);
  if (c.y == 0) { velocity.y = max(velocity.y, 0.0); }

  gas.x = clamp(gas.x, 0.0, 1.8);
  gas.y = clamp(gas.y, 0.0, 6.0);
  gas.z = clamp(gas.z, 0.0, 4.0);
  gas.w = clamp(gas.w, 0.0, 1.0);
  velocityOut[id] = vec4<f32>(velocity, 0.0);
  gasOut[id] = gas;
}
`;

const DIVERGENCE = COMMON + /* wgsl */ `
@group(0) @binding(1) var<storage, read> velocityIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> divergenceOut: array<f32>;
fn velocityComponent(c: vec3<i32>) -> vec3<f32> {
  if (c.y < 0) { return vec3<f32>(0.0); }
  if (!inDomain(c)) { return vec3<f32>(0.0); }
  return velocityIn[cellIndex(c)].xyz;
}
@compute @workgroup_size(${WORKGROUP.join(', ')})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!inDomain(c)) { return; }
  let left = velocityComponent(c - vec3<i32>(1, 0, 0)).x;
  let right = velocityComponent(c + vec3<i32>(1, 0, 0)).x;
  let down = velocityComponent(c - vec3<i32>(0, 1, 0)).y;
  let up = velocityComponent(c + vec3<i32>(0, 1, 0)).y;
  let back = velocityComponent(c - vec3<i32>(0, 0, 1)).z;
  let front = velocityComponent(c + vec3<i32>(0, 0, 1)).z;
  divergenceOut[cellIndex(c)] = 0.5 / params.originH.w * ((right - left) + (up - down) + (front - back));
}
`;

const CLEAR_PRESSURE = COMMON + /* wgsl */ `
@group(0) @binding(1) var<storage, read_write> pressureOut: array<f32>;
@compute @workgroup_size(${WORKGROUP.join(', ')})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!inDomain(c)) { return; }
  pressureOut[cellIndex(c)] = 0.0;
}
`;

function jacobiShader(inputName, outputName) {
  return COMMON + /* wgsl */ `
@group(0) @binding(1) var<storage, read> pressureIn: array<f32>;
@group(0) @binding(2) var<storage, read_write> pressureOut: array<f32>;
@group(0) @binding(3) var<storage, read> divergenceIn: array<f32>;
fn pressureAt(c: vec3<i32>, centre: f32) -> f32 {
  if (c.y < 0) { return centre; }
  if (!inDomain(c)) { return 0.0; }
  return pressureIn[cellIndex(c)];
}
@compute @workgroup_size(${WORKGROUP.join(', ')})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!inDomain(c)) { return; }
  let id = cellIndex(c);
  let centre = pressureIn[id];
  let neighbors = pressureAt(c - vec3<i32>(1, 0, 0), centre)
    + pressureAt(c + vec3<i32>(1, 0, 0), centre)
    + pressureAt(c - vec3<i32>(0, 1, 0), centre)
    + pressureAt(c + vec3<i32>(0, 1, 0), centre)
    + pressureAt(c - vec3<i32>(0, 0, 1), centre)
    + pressureAt(c + vec3<i32>(0, 0, 1), centre);
  let h = params.originH.w;
  pressureOut[id] = (neighbors - h * h * divergenceIn[id]) / 6.0;
}
`;
}

const PROJECT = COMMON + /* wgsl */ `
@group(0) @binding(1) var<storage, read> pressureIn: array<f32>;
@group(0) @binding(2) var<storage, read> velocityIn: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> velocityOut: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read> gasIn: array<vec4<f32>>;
@group(0) @binding(5) var<storage, read_write> gasOut: array<vec4<f32>>;
fn pressureAt(c: vec3<i32>, centre: f32) -> f32 {
  if (c.y < 0) { return centre; }
  if (!inDomain(c)) { return 0.0; }
  return pressureIn[cellIndex(c)];
}
@compute @workgroup_size(${WORKGROUP.join(', ')})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!inDomain(c)) { return; }
  let id = cellIndex(c);
  let centre = pressureIn[id];
  let gradient = vec3<f32>(
    pressureAt(c + vec3<i32>(1, 0, 0), centre) - pressureAt(c - vec3<i32>(1, 0, 0), centre),
    pressureAt(c + vec3<i32>(0, 1, 0), centre) - pressureAt(c - vec3<i32>(0, 1, 0), centre),
    pressureAt(c + vec3<i32>(0, 0, 1), centre) - pressureAt(c - vec3<i32>(0, 0, 1), centre)
  ) * (0.5 / params.originH.w);
  var velocity = velocityIn[id].xyz - gradient;
  if (c.y == 0) { velocity.y = max(velocity.y, 0.0); }
  velocityOut[id] = vec4<f32>(velocity, 0.0);
  gasOut[id] = gasIn[id];
}
`;

const RESAMPLE = /* wgsl */ `
struct RemapParams {
  oldDims: vec4<i32>,
  newDims: vec4<i32>,
  oldOriginH: vec4<f32>,
  newOriginH: vec4<f32>,
};
@group(0) @binding(0) var<uniform> remap: RemapParams;
@group(0) @binding(1) var<storage, read> velocityIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read> gasIn: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> velocityOut: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> gasOut: array<vec4<f32>>;
fn insideOld(c: vec3<i32>) -> bool {
  return all(c >= vec3<i32>(0)) && all(c < remap.oldDims.xyz);
}
fn insideNew(c: vec3<i32>) -> bool {
  return all(c >= vec3<i32>(0)) && all(c < remap.newDims.xyz);
}
fn oldIndex(c: vec3<i32>) -> u32 {
  return u32(c.x + remap.oldDims.x * (c.y + remap.oldDims.y * c.z));
}
fn newIndex(c: vec3<i32>) -> u32 {
  return u32(c.x + remap.newDims.x * (c.y + remap.newDims.y * c.z));
}
fn loadVelocity(c: vec3<i32>) -> vec4<f32> {
  if (!insideOld(c)) { return vec4<f32>(0.0); }
  return velocityIn[oldIndex(c)];
}
fn loadGas(c: vec3<i32>) -> vec4<f32> {
  if (!insideOld(c)) { return vec4<f32>(0.0); }
  return gasIn[oldIndex(c)];
}
fn sampleVelocity(g: vec3<f32>) -> vec4<f32> {
  let b = floor(g); let f = g - b; let i = vec3<i32>(b);
  return mix(
    mix(mix(loadVelocity(i), loadVelocity(i + vec3<i32>(1, 0, 0)), f.x), mix(loadVelocity(i + vec3<i32>(0, 1, 0)), loadVelocity(i + vec3<i32>(1, 1, 0)), f.x), f.y),
    mix(mix(loadVelocity(i + vec3<i32>(0, 0, 1)), loadVelocity(i + vec3<i32>(1, 0, 1)), f.x), mix(loadVelocity(i + vec3<i32>(0, 1, 1)), loadVelocity(i + vec3<i32>(1, 1, 1)), f.x), f.y),
    f.z
  );
}
fn sampleGas(g: vec3<f32>) -> vec4<f32> {
  let b = floor(g); let f = g - b; let i = vec3<i32>(b);
  return mix(
    mix(mix(loadGas(i), loadGas(i + vec3<i32>(1, 0, 0)), f.x), mix(loadGas(i + vec3<i32>(0, 1, 0)), loadGas(i + vec3<i32>(1, 1, 0)), f.x), f.y),
    mix(mix(loadGas(i + vec3<i32>(0, 0, 1)), loadGas(i + vec3<i32>(1, 0, 1)), f.x), mix(loadGas(i + vec3<i32>(0, 1, 1)), loadGas(i + vec3<i32>(1, 1, 1)), f.x), f.y),
    f.z
  );
}
@compute @workgroup_size(${WORKGROUP.join(', ')})
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let c = vec3<i32>(gid);
  if (!insideNew(c)) { return; }
  let world = remap.newOriginH.xyz + (vec3<f32>(c) + vec3<f32>(0.5)) * remap.newOriginH.w;
  let oldGridPoint = (world - remap.oldOriginH.xyz) / remap.oldOriginH.w - vec3<f32>(0.5);
  let id = newIndex(c);
  velocityOut[id] = sampleVelocity(oldGridPoint);
  gasOut[id] = sampleGas(oldGridPoint);
}
`;

const SHADERS = {
  resample: RESAMPLE,
  advection: ADVECTION,
  correction: CORRECTION,
  curl: CURL,
  forces: FORCES,
  divergence: DIVERGENCE,
  clearPressure: CLEAR_PRESSURE,
  jacobi: jacobiShader('pressureIn', 'pressureOut'),
  project: PROJECT,
};

function makePipelineBindings(device, pipeline, uniformBuffer, buffers) {
  return device.createBindGroup({
    layout: pipeline.getBindGroupLayout(0),
    entries: [
      { binding: 0, resource: { buffer: uniformBuffer } },
      ...buffers.map((buffer, index) => ({ binding: index + 1, resource: { buffer } })),
    ],
  });
}

export class FluidSolver {
  constructor(device, validateShader) {
    const plannedGridBytes = MAX_CELLS * (9 * 16 + 3 * 4);
    const plannedAuxBytes = PARAM_BYTES * MAX_STEPS_PER_FRAME + 64;
    if (plannedGridBytes + plannedAuxBytes > VRAM_BUDGET_BYTES) {
      throw new Error(`Solver buffers need ${Math.ceil((plannedGridBytes + plannedAuxBytes) / (1024 * 1024))} MiB, above the 250 MiB app budget.`);
    }
    this.device = device;
    this.grid = describeGrid(DEFAULT_LEVEL);
    this.level = DEFAULT_LEVEL;
    this.center = [0, 0];
    this.reachScale = [1.0, 1.35, 1.75][DEFAULT_LEVEL];
    this.maxCellCount = MAX_CELLS;
    this.time = 0;
    this.sources = [];
    this.sourceWindowSignature = null;
    this.sourceWindowChangeTime = 0;
    this.nextSourceId = 1;
    this.remapBuffer = device.createBuffer({ size: 64, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, label: 'fluid-remap-params' });
    this.uniformBuffers = Array.from({ length: MAX_STEPS_PER_FRAME }, (_, index) => device.createBuffer({
      size: PARAM_BYTES,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      label: `fluid-params-${index}`,
    }));

    const makeBuffer = (size, label) => device.createBuffer({
      size,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC,
      label,
    });
    const vec4Bytes = this.maxCellCount * 16;
    const scalarBytes = this.maxCellCount * 4;
    this.buffers = {
      velocityA: makeBuffer(vec4Bytes, 'velocity-current'),
      velocityB: makeBuffer(vec4Bytes, 'velocity-corrected'),
      velocityTmp: makeBuffer(vec4Bytes, 'velocity-advected'),
      velocityC: makeBuffer(vec4Bytes, 'velocity-forces'),
      gasA: makeBuffer(vec4Bytes, 'gas-current'),
      gasB: makeBuffer(vec4Bytes, 'gas-corrected'),
      gasTmp: makeBuffer(vec4Bytes, 'gas-advected'),
      gasC: makeBuffer(vec4Bytes, 'gas-forces'),
      curl: makeBuffer(vec4Bytes, 'velocity-curl'),
      pressureA: makeBuffer(scalarBytes, 'pressure-a'),
      pressureB: makeBuffer(scalarBytes, 'pressure-b'),
      divergence: makeBuffer(scalarBytes, 'velocity-divergence'),
    };
    this.gasBuffer = this.buffers.gasA;
    this.gridBufferBytes = vec4Bytes * 9 + scalarBytes * 3;
    this.allocatedBytes = this.gridBufferBytes + PARAM_BYTES * MAX_STEPS_PER_FRAME + this.remapBuffer.size;
    this.pipelines = {};
    this.bindings = {};
    this.ready = this.initialize(validateShader);
  }

  async initialize(validateShader) {
    for (const [name, code] of Object.entries(SHADERS)) {
      const module = this.device.createShaderModule({ code, label: `fluid-${name}` });
      await validateShader(module, `fluid-${name}`);
      this.pipelines[name] = this.device.createComputePipeline({
        layout: 'auto',
        compute: { module, entryPoint: 'main' },
        label: `fluid-${name}`,
      });
    }

    const B = this.buffers;
    const bindSets = {
      advection: [B.velocityA, B.gasA, B.velocityTmp, B.gasTmp],
      correction: [B.velocityA, B.gasA, B.velocityTmp, B.gasTmp, B.velocityB, B.gasB],
      curl: [B.velocityB, B.curl],
      forces: [B.velocityB, B.gasB, B.curl, B.velocityC, B.gasC],
      divergence: [B.velocityC, B.divergence],
      clearPressure: [B.pressureA],
      jacobiForward: [B.pressureA, B.pressureB, B.divergence],
      jacobiBackward: [B.pressureB, B.pressureA, B.divergence],
      project: [B.pressureA, B.velocityC, B.velocityA, B.gasC, B.gasA],
    };
    const bindPipeline = (pipelineName, buffers) => this.uniformBuffers.map((uniformBuffer) =>
      makePipelineBindings(this.device, this.pipelines[pipelineName], uniformBuffer, buffers));

    this.bindings.advection = bindPipeline('advection', bindSets.advection);
    this.bindings.correction = bindPipeline('correction', bindSets.correction);
    this.bindings.curl = bindPipeline('curl', bindSets.curl);
    this.bindings.forces = bindPipeline('forces', bindSets.forces);
    this.bindings.divergence = bindPipeline('divergence', bindSets.divergence);
    this.bindings.clearPressure = bindPipeline('clearPressure', bindSets.clearPressure);
    this.bindings.jacobiForward = bindPipeline('jacobi', bindSets.jacobiForward);
    this.bindings.jacobiBackward = bindPipeline('jacobi', bindSets.jacobiBackward);
    this.bindings.project = bindPipeline('project', bindSets.project);
    this.bindings.resample = this.device.createBindGroup({
      layout: this.pipelines.resample.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: this.remapBuffer } },
        { binding: 1, resource: { buffer: B.velocityA } },
        { binding: 2, resource: { buffer: B.gasA } },
        { binding: 3, resource: { buffer: B.velocityB } },
        { binding: 4, resource: { buffer: B.gasB } },
      ],
    });

    this.device.queue.submit([this.clearEncoder().finish()]);
  }

  regrid(encoder, level, wind = 0) {
    const nextLevel = Math.max(0, Math.min(LOD_LAYOUTS.length - 1, Math.trunc(level)));
    const layout = LOD_LAYOUTS[nextLevel];
    let centerX = this.center[0];
    let centerZ = this.center[1];
    let cellSize = layout.cellSize;
    let sourceBounds = null;
    let sourceBoundsFitCurrent = false;
    const nextReachScale = [1.0, 1.35, 1.75][nextLevel];
    const windMargin = Math.min(5.0, Math.abs(wind) * 0.8);
    let margin = 5.0 + windMargin;

    if (this.sources.length) {
      sourceBounds = this.sources.reduce((bounds, source) => {
        const plumeEnvelope = source.kind === 'burst' ? 8.3 : 5.4;
        const sourceReach = Math.max(source.radius, plumeEnvelope * nextReachScale);
        return {
          minX: Math.min(bounds.minX, source.x - sourceReach),
          maxX: Math.max(bounds.maxX, source.x + sourceReach),
          minZ: Math.min(bounds.minZ, source.z - sourceReach),
          maxZ: Math.max(bounds.maxZ, source.z + sourceReach),
        };
      }, { minX: Infinity, maxX: -Infinity, minZ: Infinity, maxZ: -Infinity });
      const signature = [sourceBounds.minX, sourceBounds.maxX, sourceBounds.minZ, sourceBounds.maxZ]
        .map((value) => value.toFixed(2))
        .join(':');
      if (signature !== this.sourceWindowSignature) {
        this.sourceWindowSignature = signature;
        this.sourceWindowChangeTime = this.time;
      }
      const currentMargin = Math.max(5.0, this.grid.cellSize * 2.0) + windMargin;
      const fitTolerance = 0.01;
      sourceBoundsFitCurrent = sourceBounds.minX - currentMargin >= this.grid.origin[0] - fitTolerance
        && sourceBounds.maxX + currentMargin <= this.grid.origin[0] + this.grid.width + fitTolerance
        && sourceBounds.minZ - currentMargin >= this.grid.origin[2] - fitTolerance
        && sourceBounds.maxZ + currentMargin <= this.grid.origin[2] + this.grid.depth + fitTolerance;
      if (nextLevel !== this.level && sourceBoundsFitCurrent) {
        centerX = this.center[0];
        centerZ = this.center[1];
      } else {
        centerX = (sourceBounds.minX + sourceBounds.maxX) * 0.5;
        centerZ = (sourceBounds.minZ + sourceBounds.maxZ) * 0.5;
      }
      const sourceWidth = sourceBounds.maxX - sourceBounds.minX;
      const sourceDepth = sourceBounds.maxZ - sourceBounds.minZ;
      cellSize = Math.max(
        layout.cellSize,
        (sourceWidth + margin * 2) / layout.nx,
        (sourceDepth + margin * 2) / layout.nz,
      );
      for (let iteration = 0; iteration < 8; iteration++) {
        margin = Math.max(5.0, cellSize * 2.0) + windMargin;
        const requiredCellSize = Math.max(
          layout.cellSize,
          (sourceWidth + margin * 2) / layout.nx,
          (sourceDepth + margin * 2) / layout.nz,
        );
        if (requiredCellSize <= cellSize + 1e-7) break;
        cellSize = requiredCellSize;
      }
      margin = Math.max(5.0, cellSize * 2.0) + windMargin;
      if (nextLevel !== this.level) {
        cellSize = Math.max(
          cellSize,
          this.grid.width / layout.nx,
          this.grid.height / layout.ny,
          this.grid.depth / layout.nz,
        );
      }
    } else {
      this.sourceWindowSignature = null;
      this.sourceWindowChangeTime = this.time;
      // Keep the last simulation window around for residual smoke; camera motion alone does not move it.
      if (nextLevel === this.level) return false;
      cellSize = Math.max(
        layout.cellSize,
        this.grid.width / layout.nx,
        this.grid.height / layout.ny,
        this.grid.depth / layout.nz,
      );
    }

    const nextGrid = describeGrid(nextLevel, centerX, centerZ, cellSize);
    const moved = Math.hypot(centerX - this.center[0], centerZ - this.center[1]);
    const cellSizeChanged = Math.abs(nextGrid.cellSize - this.grid.cellSize) > 0.005;
    const desiredWindowChanged = cellSizeChanged || moved > 0.5;
    const sourceWindowMature = this.time - this.sourceWindowChangeTime >= SOURCE_WINDOW_RELEASE_DELAY;
    // Let old smoke decay before shrinking or shifting a still-fitting same-tier window.
    if (nextLevel === this.level && sourceBoundsFitCurrent && (!desiredWindowChanged || !sourceWindowMature)) return false;
    const values = new Float32Array(16);
    const ints = new Int32Array(values.buffer);
    ints.set([this.grid.nx, this.grid.ny, this.grid.nz, 0], 0);
    ints.set([nextGrid.nx, nextGrid.ny, nextGrid.nz, 0], 4);
    values.set([this.grid.origin[0], this.grid.origin[1], this.grid.origin[2], this.grid.cellSize], 8);
    values.set([nextGrid.origin[0], nextGrid.origin[1], nextGrid.origin[2], nextGrid.cellSize], 12);
    this.device.queue.writeBuffer(this.remapBuffer, 0, values);

    const pass = encoder.beginComputePass({ label: 'world-space-lod-remap' });
    pass.setPipeline(this.pipelines.resample);
    pass.setBindGroup(0, this.bindings.resample);
    pass.dispatchWorkgroups(
      Math.ceil(nextGrid.nx / WORKGROUP[0]),
      Math.ceil(nextGrid.ny / WORKGROUP[1]),
      Math.ceil(nextGrid.nz / WORKGROUP[2]),
    );
    pass.end();
    const activeBytes = nextGrid.cells * 16;
    encoder.copyBufferToBuffer(this.buffers.velocityB, 0, this.buffers.velocityA, 0, activeBytes);
    encoder.copyBufferToBuffer(this.buffers.gasB, 0, this.buffers.gasA, 0, activeBytes);

    this.grid = nextGrid;
    this.center = [centerX, centerZ];
    this.level = nextLevel;
    this.reachScale = [1.0, 1.35, 1.75][nextLevel];
    return true;
  }

  clearEncoder() {
    const encoder = this.device.createCommandEncoder({ label: 'clear-fluid-state' });
    for (const buffer of Object.values(this.buffers)) encoder.clearBuffer(buffer);
    return encoder;
  }

  clear({ level = this.level, centerX = this.center[0], centerZ = this.center[1] } = {}) {
    this.sources.length = 0;
    this.sourceWindowSignature = null;
    this.sourceWindowChangeTime = 0;
    this.time = 0;
    this.level = Math.max(0, Math.min(LOD_LAYOUTS.length - 1, Math.trunc(level)));
    this.center = [centerX, centerZ];
    this.reachScale = [1.0, 1.35, 1.75][this.level];
    this.grid = describeGrid(this.level, centerX, centerZ);
    this.device.queue.submit([this.clearEncoder().finish()]);
  }

  addSource(kind, x, z, options = {}) {
    if (kind !== 'burst' && kind !== 'plume') throw new Error(`Unknown source kind: ${kind}`);
    if (this.sources.length >= MAX_SOURCES) {
      const expendable = this.sources.findIndex((source) => source.kind === 'burst');
      this.sources.splice(expendable >= 0 ? expendable : 0, 1);
    }
    const isBurst = kind === 'burst';
    const source = {
      id: this.nextSourceId++,
      kind,
      x,
      y: options.y ?? 0.28,
      z,
      age: options.age ?? 0,
      radius: options.radius ?? (isBurst ? 1.45 : 0.85),
      intensity: options.intensity ?? (isBurst ? 1.5 : 0.82),
      life: options.life ?? (isBurst ? 5.5 : 30.0),
      seed: Math.random() * 1000,
      impulse: isBurst ? 18.0 : 0.0,
      height: options.height ?? (isBurst ? 2.8 : 7.0),
      twist: options.twist ?? (isBurst ? 0.0 : 1.2),
    };
    this.sources.push(source);
    return source;
  }

  removeSource(id) {
    this.sources = this.sources.filter((source) => source.id !== id);
  }

  writeParameters(slot, stepTime, stepOffset, settings) {
    const floats = new Float32Array(PARAM_FLOATS);
    const ints = new Int32Array(floats.buffer);
    ints[0] = this.grid.nx;
    ints[1] = this.grid.ny;
    ints[2] = this.grid.nz;
    const sources = this.sources.slice(0, MAX_SOURCES);
    ints[3] = sources.length;

    floats.set([this.grid.origin[0], this.grid.origin[1], this.grid.origin[2], this.grid.cellSize], 4);
    floats.set([FIXED_STEP, stepTime, settings.buoyancy, settings.vorticity], 8);
    floats.set([settings.burnRate, settings.sootYield, settings.smokeFade, settings.turbulence], 12);
    floats.set([settings.wind, 0, settings.wind * 0.12, this.reachScale], 16);
    floats.set([settings.fuelFeed, 0, 0, 0], 20);

    for (let i = 0; i < sources.length; i++) {
      const source = sources[i];
      const offset = 24 + i * 12;
      const mode = source.kind === 'burst' ? 0 : 1;
      floats.set([source.x, source.y, source.z, source.age + stepOffset], offset);
      floats.set([source.radius, source.intensity, source.life, mode], offset + 4);
      floats.set([source.seed, source.impulse, source.height, source.twist], offset + 8);
    }
    this.device.queue.writeBuffer(this.uniformBuffers[slot], 0, floats);
  }

  encodeStep(encoder, slot) {
    const pass = encoder.beginComputePass({ label: 'stable-fluids-step' });
    const run = (pipelineName, bindingName = pipelineName) => {
      pass.setPipeline(this.pipelines[pipelineName]);
      pass.setBindGroup(0, this.bindings[bindingName][slot]);
      pass.dispatchWorkgroups(
        Math.ceil(this.grid.nx / WORKGROUP[0]),
        Math.ceil(this.grid.ny / WORKGROUP[1]),
        Math.ceil(this.grid.nz / WORKGROUP[2]),
      );
    };

    run('clearPressure');
    run('advection');
    run('correction');
    run('curl');
    run('forces');
    run('divergence');
    for (let iteration = 0; iteration < 10; iteration++) {
      run('jacobi', iteration % 2 === 0 ? 'jacobiForward' : 'jacobiBackward');
    }
    run('project');
    pass.end();
  }

  encode(encoder, stepCount, settings) {
    if (!stepCount) {
      this.writeParameters(0, this.time, 0, settings);
      return;
    }
    for (let step = 0; step < stepCount; step++) {
      const slot = step % MAX_STEPS_PER_FRAME;
      const stepOffset = step * FIXED_STEP;
      this.writeParameters(slot, this.time + stepOffset, stepOffset, settings);
      this.encodeStep(encoder, slot);
    }
    const elapsed = stepCount * FIXED_STEP;
    this.time += elapsed;
    for (const source of this.sources) source.age += elapsed;
    this.sources = this.sources.filter((source) => source.age <= source.life);
  }
}

export function describeGrid(level = DEFAULT_LEVEL, centerX = 0, centerZ = 0, cellSizeOverride = null) {
  const layout = LOD_LAYOUTS[Math.max(0, Math.min(LOD_LAYOUTS.length - 1, Math.trunc(level)))];
  const { nx, ny, nz } = layout;
  const cellSize = cellSizeOverride ?? layout.cellSize;
  const origin = [centerX - nx * cellSize * 0.5, 0, centerZ - nz * cellSize * 0.5];
  return {
    ...layout,
    cellSize,
    origin,
    cells: nx * ny * nz,
    width: nx * cellSize,
    height: ny * cellSize,
    depth: nz * cellSize,
    maxSources: MAX_SOURCES,
  };
}
