import { cameraUniformData } from './math.js?v=unreal-volume-gradient-20261001';
import { MAX_EMITTERS, VRAM_BUDGET_BYTES, VRAM_BUDGET_RESERVE_BYTES } from './fluid-solver.js?v=unreal-volume-gradient-20261001';

export const PRESENTATION_BUFFER_COUNT = 3;
export const RENDER_TARGET_BUFFER_COUNT = 2;

const SHADER = /* wgsl */ `
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
  sources: array<Source, ${MAX_EMITTERS}>,
};
struct Camera {
  position: vec4<f32>,
  forward: vec4<f32>,
  right: vec4<f32>,
  up: vec4<f32>,
  viewport: vec4<f32>,
  lod: vec4<f32>,
  transfer: vec4<f32>,
  lighting: vec4<f32>,
  fireColorLow: vec4<f32>,
  fireColorHigh: vec4<f32>,
  smokeColorLight: vec4<f32>,
  smokeColorDense: vec4<f32>,
};
@group(0) @binding(0) var<uniform> params: SimParams;
@group(0) @binding(1) var<uniform> camera: Camera;
@group(0) @binding(2) var<storage, read> gasField: array<vec4<f32>>;

fn inDomain(c: vec3<i32>) -> bool {
  return all(c >= vec3<i32>(0)) && all(c < params.dims.xyz);
}
fn cellIndex(c: vec3<i32>) -> u32 {
  return u32(c.x + params.dims.x * (c.y + params.dims.y * c.z));
}
fn loadGas(c: vec3<i32>) -> vec4<f32> {
  if (!inDomain(c)) { return vec4<f32>(0.0); }
  return gasField[cellIndex(c)];
}
fn sampleGas(g: vec3<f32>) -> vec4<f32> {
  let b = floor(g);
  let f = g - b;
  let i = vec3<i32>(b);
  let c000 = loadGas(i);
  let c100 = loadGas(i + vec3<i32>(1, 0, 0));
  let c010 = loadGas(i + vec3<i32>(0, 1, 0));
  let c110 = loadGas(i + vec3<i32>(1, 1, 0));
  let c001 = loadGas(i + vec3<i32>(0, 0, 1));
  let c101 = loadGas(i + vec3<i32>(1, 0, 1));
  let c011 = loadGas(i + vec3<i32>(0, 1, 1));
  let c111 = loadGas(i + vec3<i32>(1, 1, 1));
  return mix(
    mix(mix(c000, c100, f.x), mix(c010, c110, f.x), f.y),
    mix(mix(c001, c101, f.x), mix(c011, c111, f.x), f.y),
    f.z
  );
}
fn gasAtWorld(wp: vec3<f32>) -> vec4<f32> {
  let gridPoint = (wp - params.originH.xyz) / params.originH.w - vec3<f32>(0.5);
  return sampleGas(gridPoint);
}
fn transferDensity(rawDensity: f32, soot: f32) -> f32 {
  // Density is treated as a transfer function, not remixed with world-space
  // noise. This mirrors Niagara's density gain, minimum, and curve controls.
  let gain = max(camera.transfer.x, 0.001);
  let cutoff = max(camera.transfer.y, 0.0);
  let curve = max(camera.transfer.z, 0.25);
  let sootBoost = 1.0 + clamp(soot, 0.0, 1.0) * max(camera.transfer.w, 0.0);
  let response = max(rawDensity - cutoff, 0.0) * gain * sootBoost;
  return pow(clamp(response, 0.0, 2.0), curve);
}

fn blackbodyColor(normalizedTemperature: f32) -> vec3<f32> {
  // Approximation of a Kelvin black-body locus. The simulation temperature is
  // normalized before this function; emission intensity remains separate.
  let kelvin = mix(900.0, 3200.0, pow(clamp(normalizedTemperature, 0.0, 1.0), 0.72));
  let t = max(kelvin / 100.0, 1.0);
  var red = 0.0;
  var green = 0.0;
  var blue = 0.0;
  if (t <= 66.0) {
    red = 1.0;
    green = clamp(0.3900815787 * log(t) - 0.6318414438, 0.0, 1.0);
  } else {
    red = clamp(1.2929361861 * pow(t - 60.0, -0.1332047592), 0.0, 1.0);
    green = clamp(1.1298908610 * pow(t - 60.0, -0.0755148492), 0.0, 1.0);
  }
  if (t >= 66.0) {
    blue = 1.0;
  } else if (t > 19.0) {
    blue = clamp(0.5432067891 * log(t - 10.0) - 1.1962540891, 0.0, 1.0);
  }
  return vec3<f32>(red, green, blue);
}

fn shadowVisibility(position: vec3<f32>, lightDirection: vec3<f32>, shadowStep: f32, shadowSamples: u32) -> f32 {
  // Shadow taps are a distinct ray. They use the transferred density field,
  // rather than reusing the primary sample or adding procedural breakup.
  let tapCount = min(max(shadowSamples, 1u), 8u);
  let stride = max(shadowStep, params.originH.w * 0.25);
  var opticalDepth = 0.0;
  for (var tap = 0u; tap < 8u; tap++) {
    if (tap >= tapCount) { break; }
    let offset = (f32(tap) + 0.5) * stride;
    let shadowGas = gasAtWorld(position + lightDirection * offset);
    let shadowDensity = transferDensity(shadowGas.x, shadowGas.w);
    let shadowHeat = clamp(max(shadowGas.y - camera.lighting.y, 0.0) * max(camera.lighting.x, 0.0) / 4.4, 0.0, 1.0) * 0.16;
    opticalDepth += (shadowDensity + shadowHeat) * stride * 0.68;
  }
  return exp(-opticalDepth);
}

fn safeInverse(value: f32) -> f32 {
  if (abs(value) < 1e-5) { return 1e5; }
  return 1.0 / value;
}
fn skyColor(direction: vec3<f32>) -> vec3<f32> {
  let horizon = smoothstep(-0.22, 0.42, direction.y);
  let zenith = vec3<f32>(0.13, 0.19, 0.25);
  let lowSky = vec3<f32>(0.042, 0.057, 0.074);
  return mix(lowSky, zenith, horizon);
}
fn groundColor(point: vec3<f32>) -> vec3<f32> {
  let fineGrid = abs(fract(point.xz * 0.5 + vec2<f32>(0.5)) - vec2<f32>(0.5)) * 2.0;
  let fineLine = 1.0 - smoothstep(0.018, 0.065, min(fineGrid.x, fineGrid.y));
  let majorGrid = abs(fract(point.xz * 0.1 + vec2<f32>(0.5)) - vec2<f32>(0.5)) * 10.0;
  let majorLine = 1.0 - smoothstep(0.025, 0.09, min(majorGrid.x, majorGrid.y));
  let radial = abs(length(point.xz) - 8.0);
  let stageRing = 1.0 - smoothstep(0.035, 0.10, radial);
  var color = vec3<f32>(0.035, 0.044, 0.051);
  color += vec3<f32>(0.012, 0.019, 0.024) * fineLine;
  color += vec3<f32>(0.022, 0.031, 0.037) * majorLine;
  color += vec3<f32>(0.07, 0.035, 0.015) * stageRing;
  color *= 0.86 + 0.14 * exp(-length(point.xz) * 0.018);
  return color;
}

struct VertexOut { @builtin(position) position: vec4<f32> };
@vertex fn vs(@builtin(vertex_index) vertexIndex: u32) -> VertexOut {
  var triangle = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>(3.0, -1.0),
    vec2<f32>(-1.0, 3.0)
  );
  var output: VertexOut;
  output.position = vec4<f32>(triangle[vertexIndex], 0.0, 1.0);
  return output;
}

@fragment fn fs(@builtin(position) fragment: vec4<f32>) -> @location(0) vec4<f32> {
  let dimensions = camera.viewport.xy;
  let uv = fragment.xy / dimensions;
  let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  let aspect = dimensions.x / max(dimensions.y, 1.0);
  let tangent = camera.viewport.w;
  let ray = normalize(
    camera.forward.xyz
    + camera.right.xyz * ndc.x * aspect * tangent
    + camera.up.xyz * ndc.y * tangent
  );
  // All distance tiers use the actual fragment ray. Far LOD lowers the
  // volume render target and ray budget, but does not quantize pixels into
  // censor blocks.
  let volumeRay = ray;
  let eye = camera.position.xyz;
  var scene = skyColor(ray);
  var groundT = 1e6;
  var floorPoint = vec3<f32>(0.0);
  if (ray.y < -1e-4) {
    groundT = -eye.y / ray.y;
    if (groundT > 0.0) {
      floorPoint = eye + ray * groundT;
      scene = groundColor(floorPoint);
      // Low, warm contact glows mark sources without adding a second simulated field.
      for (var sourceIndex = 0; sourceIndex < ${MAX_EMITTERS}; sourceIndex++) {
        if (sourceIndex >= params.dims.w) { break; }
        let source = params.sources[sourceIndex];
        let delta = floorPoint.xz - source.positionAge.xz;
        let distance = length(delta);
        let ring = 1.0 - smoothstep(0.025, 0.11, abs(distance - source.shape.x * 1.8));
        let glow = exp(-dot(delta, delta) / max(source.shape.x * source.shape.x * 7.0, 0.1));
        var tint = vec3<f32>(1.0, 0.20, 0.025);
        if (source.shape.w > 0.5) { tint = vec3<f32>(1.0, 0.46, 0.07); }
        scene += tint * (ring * 0.095 + glow * 0.035);
      }
    }
  }

  let volumeMin = params.originH.xyz;
  let volumeMax = volumeMin + vec3<f32>(params.dims.xyz) * params.originH.w;
  var volumeGroundT = 1e6;
  if (volumeRay.y < -1e-4) { volumeGroundT = -eye.y / volumeRay.y; }
  let inverseRay = vec3<f32>(safeInverse(volumeRay.x), safeInverse(volumeRay.y), safeInverse(volumeRay.z));
  let nearPlane = (volumeMin - eye) * inverseRay;
  let farPlane = (volumeMax - eye) * inverseRay;
  let axisNear = min(nearPlane, farPlane);
  let axisFar = max(nearPlane, farPlane);
  let volumeNear = max(max(max(axisNear.x, axisNear.y), axisNear.z), 0.0);
  let volumeFar = min(min(min(axisFar.x, axisFar.y), axisFar.z), volumeGroundT);

  var transmittance = 1.0;
  var scattered = vec3<f32>(0.0);
  var fireLight = vec3<f32>(0.0);
  if (volumeFar > volumeNear) {
    let segmentLength = volumeFar - volumeNear;
    let sampleBudget = max(u32(camera.lod.y), 1u);
    // Render Step Size Mult: keep the primary ray below one cell where the
    // tier budget allows it, rather than inventing detail between cells.
    let subCellSamples = max(u32(camera.lod.z), 1u);
    let subCellStep = params.originH.w / f32(subCellSamples);
    let stepLength = max(max(camera.lod.x, subCellStep), segmentLength / f32(sampleBudget));
    var distance = volumeNear + stepLength * 0.5;
    var sampleIndex = 0u;
    let sunDirection = normalize(vec3<f32>(-0.48, 0.79, 0.38));
    let shadowStep = max(camera.lighting.z, 0.25) * params.originH.w;
    let shadowSamples = max(u32(camera.lighting.w), 1u);
    loop {
      if (sampleIndex >= sampleBudget || distance >= volumeFar || transmittance < 0.012) { break; }
      let position = eye + volumeRay * distance;
      let local = position - volumeMin;
      let sideDistance = min(min(local.x, volumeMax.x - position.x), min(local.z, volumeMax.z - position.z));
      let edgeFadeWidth = max(5.0, params.originH.w * 2.0);
      let sideFade = smoothstep(0.0, edgeFadeWidth, sideDistance);
      let topFade = smoothstep(0.0, edgeFadeWidth, volumeMax.y - position.y);
      let groundFade = smoothstep(0.0, 0.75, local.y);
      let edgeFade = min(min(sideFade, topFade), groundFade);
      let gridPoint = local / params.originH.w - vec3<f32>(0.5);
      let gas = sampleGas(gridPoint);
      let soot = clamp(gas.w, 0.0, 1.0);
      let smokeDensity = clamp(transferDensity(gas.x, soot) * edgeFade, 0.0, 2.0);
      let temperature = max(gas.y * edgeFade, 0.0);
      let temperatureResponse = max(temperature - camera.lighting.y, 0.0) * max(camera.lighting.x, 0.0);

      if (smokeDensity > 0.001 || temperatureResponse > 0.02) {
        let opticalDepth = smokeDensity * 0.72;
        let opacity = 1.0 - exp(-opticalDepth * stepLength);
        let altitude = clamp(position.y / max(volumeMax.y, 1.0), 0.0, 1.0);
        let sunVisibility = shadowVisibility(position, sunDirection, shadowStep, shadowSamples);
        let ambientUnoccluded = mix(vec3<f32>(0.10, 0.12, 0.15), vec3<f32>(0.26, 0.31, 0.37), altitude);
        // Primary-ray transmittance handles view occlusion; this additional
        // term darkens the volume core so self-shadowing is visible in smoke.
        let viewOcclusion = 1.0 - exp(-opticalDepth * stepLength * 1.8);
        let ambient = mix(ambientUnoccluded, ambientUnoccluded * 0.34, viewOcclusion);
        let direct = vec3<f32>(0.78, 0.57, 0.35) * sunVisibility * 0.42;
        // Smoke uses a density/soot gradient: open volume stays light and
        // dense, sooty regions move toward the second user-selected stop.
        let smokeGrade = clamp(max(smokeDensity * 0.62, soot * 0.82), 0.0, 1.0);
        let smokeTint = mix(camera.smokeColorLight.rgb, camera.smokeColorDense.rgb, smokeGrade);
        let powder = 1.0 - exp(-opticalDepth * 1.6);
        let lighting = ambient + direct * mix(1.0, powder * 1.55, 0.28);
        scattered += transmittance * opacity * smokeTint * lighting * 2.15;
        transmittance *= 1.0 - opacity;

        // Temperature is mapped through a Kelvin black-body approximation;
        // there is no world-space breakup or hand-authored flame palette here.
        let heat = clamp(temperatureResponse / 4.4, 0.0, 1.0);
        let flameMask = smoothstep(0.02, 0.24, heat);
        let blackbody = blackbodyColor(heat);
        // The two fire stops shape the Kelvin response rather than replacing
        // it. Preserve black-body luminance so temperature still controls the
        // energy and the gradient controls only the visible hue.
        let fireGradient = mix(camera.fireColorLow.rgb, camera.fireColorHigh.rgb, heat);
        let blackbodyLuma = max(dot(blackbody, vec3<f32>(0.2126, 0.7152, 0.0722)), 0.02);
        let gradientLuma = max(dot(fireGradient, vec3<f32>(0.2126, 0.7152, 0.0722)), 0.04);
        let gradientColor = fireGradient * (blackbodyLuma / gradientLuma);
        let emissionColor = mix(blackbody, gradientColor, 0.72);
        let emission = pow(temperatureResponse, 1.18) * flameMask * 0.43;
        fireLight += transmittance * emissionColor * emission * stepLength;
      }
      distance += stepLength;
      sampleIndex += 1u;
    }
  }

  let vignette = 1.0 - 0.16 * smoothstep(0.42, 0.92, length(ndc * vec2<f32>(0.78, 0.9)));
  var color = (scene * transmittance + scattered + fireLight) * vignette;
  color = vec3<f32>(1.0) - exp(-max(color, vec3<f32>(0.0)) * 1.28);
  color = pow(color, vec3<f32>(1.0 / 2.2));

  return vec4<f32>(color, 1.0);
}
`;

const UPSCALE_SHADER = /* wgsl */ `
@group(0) @binding(0) var lowResolutionFrame: texture_2d<f32>;
@group(0) @binding(1) var linearClampSampler: sampler;

struct VertexOut {
  @builtin(position) position: vec4<f32>,
  @location(0) uv: vec2<f32>,
};
@vertex fn vs(@builtin(vertex_index) vertexIndex: u32) -> VertexOut {
  var triangle = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>(3.0, -1.0),
    vec2<f32>(-1.0, 3.0)
  );
  var output: VertexOut;
  let position = triangle[vertexIndex];
  output.position = vec4<f32>(position, 0.0, 1.0);
  output.uv = position * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5, 0.5);
  return output;
}
@fragment fn fs(input: VertexOut) -> @location(0) vec4<f32> {
  return textureSampleLevel(lowResolutionFrame, linearClampSampler, input.uv, 0.0);
}
`;

const CAGE_SHADER = /* wgsl */ `
struct Camera {
  position: vec4<f32>,
  forward: vec4<f32>,
  right: vec4<f32>,
  up: vec4<f32>,
  viewport: vec4<f32>,
  lod: vec4<f32>,
};
struct Cage {
  color: vec4<f32>,
};
@group(0) @binding(0) var<uniform> camera: Camera;
@group(0) @binding(1) var<uniform> cage: Cage;

struct VertexOut {
  @builtin(position) position: vec4<f32>,
};

@vertex fn vs(@location(0) worldPosition: vec3<f32>) -> VertexOut {
  let view = worldPosition - camera.position.xyz;
  let depth = max(dot(view, camera.forward.xyz), 0.001);
  let aspect = camera.viewport.x / max(camera.viewport.y, 1.0);
  let tangent = camera.viewport.w;
  let ndc = vec2<f32>(
    dot(view, camera.right.xyz) / (depth * tangent * aspect),
    dot(view, camera.up.xyz) / (depth * tangent)
  );
  var output: VertexOut;
  output.position = vec4<f32>(ndc, 0.0, 1.0);
  return output;
}

@fragment fn fs() -> @location(0) vec4<f32> {
  return cage.color;
}
`;

function add3(a, b) {
  return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}
function scale3(a, scalar) {
  return [a[0] * scalar, a[1] * scalar, a[2] * scalar];
}
function addFace(vertices, a, b, c, d) {
  vertices.push(...a, ...b, ...c, ...a, ...c, ...d);
}

function appendCageEdge(vertices, start, end, axis, halfThickness) {
  const u = axis === 0 ? [0, halfThickness, 0] : [halfThickness, 0, 0];
  const v = axis === 2 ? [0, halfThickness, 0] : [0, 0, halfThickness];
  const nu = scale3(u, -1);
  const nv = scale3(v, -1);
  const a0 = add3(add3(start, nu), nv);
  const a1 = add3(add3(start, u), nv);
  const a2 = add3(add3(start, u), v);
  const a3 = add3(add3(start, nu), v);
  const b0 = add3(add3(end, nu), nv);
  const b1 = add3(add3(end, u), nv);
  const b2 = add3(add3(end, u), v);
  const b3 = add3(add3(end, nu), v);
  addFace(vertices, a0, a1, a2, a3);
  addFace(vertices, b1, b0, b3, b2);
  addFace(vertices, a0, b0, b1, a1);
  addFace(vertices, a1, b1, b2, a2);
  addFace(vertices, a2, b2, b3, a3);
  addFace(vertices, a3, b3, b0, a0);
}

function buildCageGeometry(bounds, thickness) {
  const min = bounds.min;
  const max = bounds.max;
  const vertices = [];
  const halfThickness = thickness * 0.5;
  for (const y of [min[1], max[1]]) {
    for (const z of [min[2], max[2]]) {
      appendCageEdge(vertices, [min[0], y, z], [max[0], y, z], 0, halfThickness);
    }
  }
  for (const x of [min[0], max[0]]) {
    for (const z of [min[2], max[2]]) {
      appendCageEdge(vertices, [x, min[1], z], [x, max[1], z], 1, halfThickness);
    }
  }
  for (const x of [min[0], max[0]]) {
    for (const y of [min[1], max[1]]) {
      appendCageEdge(vertices, [x, y, min[2]], [x, y, max[2]], 2, halfThickness);
    }
  }
  return new Float32Array(vertices);
}

export class VolumeRenderer {
  constructor(device, format, solver, validateShader) {
    this.device = device;
    this.format = format;
    this.solver = solver;
    this.activeField = solver;
    this.simulationResourceBytes = solver.allocatedBytes;
    this.outputWidth = 0;
    this.outputHeight = 0;
    this.renderWidth = 0;
    this.renderHeight = 0;
    this.renderTexture = null;
    this.renderView = null;
    this.cameraBuffer = device.createBuffer({
      size: 192,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      label: 'volume-camera-transfer-and-gradient-controls',
    });
    this.cageColorBuffer = device.createBuffer({
      size: 16,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      label: 'simulation-bounds-cage-color',
    });
    this.cageVertexBuffer = null;
    this.cageVertexCount = 0;
    this.cageSignature = '';
    this.cagePending = null;
    this.cagePipeline = null;
    this.cageBindGroup = null;
    this.linearSampler = device.createSampler({
      addressModeU: 'clamp-to-edge',
      addressModeV: 'clamp-to-edge',
      magFilter: 'linear',
      minFilter: 'linear',
    });
    this.ready = this.initialize(validateShader);
  }

  async initialize(validateShader) {
    const module = this.device.createShaderModule({ code: SHADER, label: 'volumetric-fire-render' });
    await validateShader(module, 'volumetric-fire-render');
    this.pipeline = this.device.createRenderPipeline({
      layout: 'auto',
      label: 'volumetric-fire-render',
      vertex: { module, entryPoint: 'vs' },
      fragment: { module, entryPoint: 'fs', targets: [{ format: this.format }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
    });
    this.bindField(this.activeField);

    const upscaleModule = this.device.createShaderModule({ code: UPSCALE_SHADER, label: 'linear-volume-upscale' });
    await validateShader(upscaleModule, 'linear-volume-upscale');
    this.upscalePipeline = this.device.createRenderPipeline({
      layout: 'auto',
      label: 'linear-volume-upscale',
      vertex: { module: upscaleModule, entryPoint: 'vs' },
      fragment: { module: upscaleModule, entryPoint: 'fs', targets: [{ format: this.format }] },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
    });

    const cageModule = this.device.createShaderModule({ code: CAGE_SHADER, label: 'simulation-bounds-cage' });
    await validateShader(cageModule, 'simulation-bounds-cage');
    this.cagePipeline = this.device.createRenderPipeline({
      layout: 'auto',
      label: 'simulation-bounds-cage',
      vertex: {
        module: cageModule,
        entryPoint: 'vs',
        buffers: [{ arrayStride: 12, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x3' }] }],
      },
      fragment: {
        module: cageModule,
        entryPoint: 'fs',
        targets: [{
          format: this.format,
          blend: {
            color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha', operation: 'add' },
            alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha', operation: 'add' },
          },
        }],
      },
      primitive: { topology: 'triangle-list', cullMode: 'none' },
    });
    this.cageBindGroup = this.device.createBindGroup({
      layout: this.cagePipeline.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: this.cameraBuffer } },
        { binding: 1, resource: { buffer: this.cageColorBuffer } },
      ],
    });
    if (this.cagePending) this.setBounds(...this.cagePending);
  }

  bindField(field) {
    this.activeField = field;
    if (!this.pipeline) return;
    const uniformBuffer = field.uniformBuffer || field.uniformBuffers[0];
    const gasBuffer = field.gasBuffer;
    this.bindGroup = this.device.createBindGroup({
      layout: this.pipeline.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: uniformBuffer } },
        { binding: 1, resource: { buffer: this.cameraBuffer } },
        { binding: 2, resource: { buffer: gasBuffer } },
      ],
    });
    this.simulationResourceBytes = field === this.solver
      ? this.solver.allocatedBytes
      : this.solver.allocatedBytes + (field.resourceBytes || 0);
  }

  setBounds(bounds, color = [1.0, 0.42, 0.10], cellSize = 0.75) {
    const safeColor = color.slice(0, 3).map((value) => Number(value) || 0);
    const thickness = Math.max(0.38, Number(cellSize) * 0.70);
    const signature = [
      ...bounds.min,
      ...bounds.max,
      ...safeColor,
      thickness,
    ].map((value) => Number(value).toFixed(4)).join(':');
    this.cagePending = [bounds, safeColor, cellSize];
    if (!this.cagePipeline || signature === this.cageSignature) return;
    const positions = buildCageGeometry(bounds, thickness);
    const previousBuffer = this.cageVertexBuffer;
    if (previousBuffer) {
      this.device.queue.onSubmittedWorkDone()
        .then(() => previousBuffer.destroy())
        .catch(() => previousBuffer.destroy());
    }
    this.cageVertexBuffer = this.device.createBuffer({
      size: Math.max(4, positions.byteLength),
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
      label: 'simulation-bounds-cage-geometry',
    });
    this.device.queue.writeBuffer(this.cageVertexBuffer, 0, positions);
    this.device.queue.writeBuffer(this.cageColorBuffer, 0, new Float32Array([...safeColor, 0.94]));
    this.cageVertexCount = positions.length / 3;
    this.cageSignature = signature;
  }

  resize(width, height, scale = 1) {
    const outputWidth = Math.max(1, Math.trunc(width));
    const outputHeight = Math.max(1, Math.trunc(height));
    const outputPixels = outputWidth * outputHeight;
    const fixedBytes = this.simulationResourceBytes
      + VRAM_BUDGET_RESERVE_BYTES
      + outputPixels * 4 * PRESENTATION_BUFFER_COUNT;
    const remainingTargetBytes = Math.max(4, VRAM_BUDGET_BYTES - fixedBytes);
    const maxRenderPixels = remainingTargetBytes / (4 * RENDER_TARGET_BUFFER_COUNT);
    const budgetScale = Math.sqrt(maxRenderPixels / outputPixels);
    const renderScale = Math.max(0.35, Math.min(1, scale, budgetScale));
    const renderWidth = Math.max(1, Math.round(outputWidth * renderScale));
    const renderHeight = Math.max(1, Math.round(outputHeight * renderScale));
    const textureChanged = renderWidth !== this.renderWidth || renderHeight !== this.renderHeight;
    this.outputWidth = outputWidth;
    this.outputHeight = outputHeight;
    this.renderWidth = renderWidth;
    this.renderHeight = renderHeight;
    this.renderScale = renderScale;
    this.estimatedVramBytes = this.simulationResourceBytes + VRAM_BUDGET_RESERVE_BYTES
      + (outputPixels * PRESENTATION_BUFFER_COUNT + renderWidth * renderHeight * RENDER_TARGET_BUFFER_COUNT) * 4;

    if (textureChanged) {
      const previousTexture = this.renderTexture;
      if (previousTexture) {
        this.device.queue.onSubmittedWorkDone()
          .then(() => previousTexture.destroy())
          .catch(() => previousTexture.destroy());
      }
      this.renderTexture = this.device.createTexture({
        label: `volume-render-${renderWidth}x${renderHeight}`,
        size: [renderWidth, renderHeight, 1],
        format: this.format,
        usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING,
      });
      this.renderView = this.renderTexture.createView();
      if (this.upscalePipeline) {
        this.upscaleBindGroup = this.device.createBindGroup({
          layout: this.upscalePipeline.getBindGroupLayout(0),
          entries: [
            { binding: 0, resource: this.renderView },
            { binding: 1, resource: this.linearSampler },
          ],
        });
      }
    }

  }

  updateCamera(camera, width, height, time, lod, render = {}) {
    const renderWidth = this.renderWidth || width;
    const renderHeight = this.renderHeight || height;
    this.device.queue.writeBuffer(
      this.cameraBuffer,
      0,
      cameraUniformData(camera, renderWidth, renderHeight, time, lod, render),
    );
  }

  draw(encoder, view) {
    const volumePass = encoder.beginRenderPass({
      label: 'volumetric-fire-render',
      colorAttachments: [{
        view: this.renderView,
        loadOp: 'clear',
        storeOp: 'store',
        clearValue: { r: 0.035, g: 0.05, b: 0.065, a: 1 },
      }],
    });
    volumePass.setPipeline(this.pipeline);
    volumePass.setBindGroup(0, this.bindGroup);
    volumePass.draw(3);
    volumePass.end();

    const upscalePass = encoder.beginRenderPass({
      label: 'linear-volume-upscale',
      colorAttachments: [{ view, loadOp: 'clear', storeOp: 'store' }],
    });
    upscalePass.setPipeline(this.upscalePipeline);
    upscalePass.setBindGroup(0, this.upscaleBindGroup);
    upscalePass.draw(3);
    upscalePass.end();

    if (this.cageVertexBuffer && this.cageVertexCount && this.cageBindGroup) {
      const cagePass = encoder.beginRenderPass({
        label: 'simulation-bounds-cage',
        colorAttachments: [{ view, loadOp: 'load', storeOp: 'store' }],
      });
      cagePass.setPipeline(this.cagePipeline);
      cagePass.setBindGroup(0, this.cageBindGroup);
      cagePass.setVertexBuffer(0, this.cageVertexBuffer);
      cagePass.draw(this.cageVertexCount);
      cagePass.end();
    }
  }
}
