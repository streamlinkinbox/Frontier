import { cameraUniformData } from './math.js?v=source-window-20261001';
import { MAX_EMITTERS, VRAM_BUDGET_BYTES, VRAM_BUDGET_RESERVE_BYTES } from './fluid-solver.js?v=source-window-20261001';

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
fn safeInverse(value: f32) -> f32 {
  if (abs(value) < 1e-5) { return 1e5; }
  return 1.0 / value;
}
fn hash2(p: vec2<f32>) -> f32 {
  return fract(sin(dot(p, vec2<f32>(12.9898, 78.233))) * 43758.5453);
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

fn boxEdgeAt(point: vec3<f32>, boxMin: vec3<f32>, boxMax: vec3<f32>, thickness: f32) -> f32 {
  let x = min(abs(point.x - boxMin.x), abs(boxMax.x - point.x));
  let y = min(abs(point.y - boxMin.y), abs(boxMax.y - point.y));
  let z = min(abs(point.z - boxMin.z), abs(boxMax.z - point.z));
  let distanceToEdge = min(min(x + y, x + z), y + z);
  return 1.0 - smoothstep(thickness, thickness * 2.8, distanceToEdge);
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
  var volumePixel = fragment.xy;
  var volumeRay = ray;
  if (camera.lod.w > 1.0) {
    let blockSize = camera.lod.w;
    volumePixel = floor(fragment.xy / blockSize) * blockSize + vec2<f32>(blockSize * 0.5);
    let volumeUv = volumePixel / dimensions;
    let volumeNdc = vec2<f32>(volumeUv.x * 2.0 - 1.0, 1.0 - volumeUv.y * 2.0);
    volumeRay = normalize(
      camera.forward.xyz
      + camera.right.xyz * volumeNdc.x * aspect * tangent
      + camera.up.xyz * volumeNdc.y * tangent
    );
  }
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
    let stepLength = max(camera.lod.x, segmentLength / f32(sampleBudget));
    let jitter = hash2(volumePixel + vec2<f32>(camera.viewport.z * 11.7, camera.viewport.z * 3.1)) * stepLength;
    var distance = volumeNear + jitter;
    var sampleIndex = 0u;
    let sunDirection = normalize(vec3<f32>(-0.48, 0.79, 0.38));
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
      let billow = 0.89 + 0.11 * sin(position.x * 1.65 + position.y * 2.2 + camera.viewport.z * 0.72)
        * cos(position.z * 1.8 - position.y * 1.35 + camera.viewport.z * 0.54);
      let smokeDensity = clamp(gas.x * edgeFade * billow, 0.0, 1.8);
      let temperature = max(gas.y * edgeFade, 0.0);

      if (smokeDensity > 0.001 || temperature > 0.12) {
        let opticalDepth = smokeDensity * 0.72;
        let opacity = 1.0 - exp(-opticalDepth * stepLength);
        let soot = clamp(gas.w, 0.0, 1.0);
        let altitude = clamp(position.y / max(volumeMax.y, 1.0), 0.0, 1.0);
        let lightSample = gasAtWorld(position + sunDirection * 1.35);
        let sunVisibility = exp(-max(lightSample.x, 0.0) * 0.82);
        let ambient = mix(vec3<f32>(0.10, 0.12, 0.15), vec3<f32>(0.26, 0.31, 0.37), altitude);
        let direct = vec3<f32>(0.78, 0.57, 0.35) * sunVisibility * 0.42;
        let smokeTint = mix(vec3<f32>(0.43, 0.47, 0.50), vec3<f32>(0.19, 0.145, 0.12), soot * 0.82);
        let powder = 1.0 - exp(-opticalDepth * stepLength * 2.0);
        let lighting = ambient + direct * mix(1.0, powder * 1.55, 0.28);
        scattered += transmittance * opacity * smokeTint * lighting * 2.15;
        transmittance *= 1.0 - opacity;

        let flutter = 0.84 + 0.16 * sin(camera.viewport.z * 10.5 + position.y * 2.3 + position.x * 1.4 + position.z * 0.9);
        let flameMask = smoothstep(0.42, 1.05, temperature) * flutter;
        let heat = clamp((temperature - 0.32) / 3.6, 0.0, 1.0);
        let orange = mix(vec3<f32>(2.25, 0.12, 0.008), vec3<f32>(2.55, 0.78, 0.11), smoothstep(0.18, 0.72, heat));
        let hotCore = mix(orange, vec3<f32>(1.8, 1.38, 0.74), smoothstep(0.72, 1.0, heat));
        let emission = pow(max(temperature - 0.28, 0.0), 1.28) * flameMask * 0.43;
        fireLight += transmittance * hotCore * emission * stepLength;
      }
      distance += stepLength;
      sampleIndex += 1u;
    }
  }

  let vignette = 1.0 - 0.16 * smoothstep(0.42, 0.92, length(ndc * vec2<f32>(0.78, 0.9)));
  var color = (scene * transmittance + scattered + fireLight) * vignette;
  color = vec3<f32>(1.0) - exp(-max(color, vec3<f32>(0.0)) * 1.28);
  color = pow(color, vec3<f32>(1.0 / 2.2));

  // A visible world-space cage makes the active simulation window legible.
  // Live grids use amber; the high-resolution baked plume uses mint. The
  // outline is evaluated at the entry and exit faces so it stays thick and
  // crisp without adding another render pass or any simulation work.
  var boxEdge = 0.0;
  if (volumeFar > volumeNear) {
    let boxThickness = max(0.22, params.originH.w * 0.42);
    boxEdge = max(boxEdge, boxEdgeAt(eye + volumeRay * volumeNear, volumeMin, volumeMax, boxThickness));
    boxEdge = max(boxEdge, boxEdgeAt(eye + volumeRay * volumeFar, volumeMin, volumeMax, boxThickness));
    let inset = min(0.65, (volumeFar - volumeNear) * 0.035);
    boxEdge = max(boxEdge, boxEdgeAt(eye + volumeRay * (volumeNear + inset), volumeMin, volumeMax, boxThickness));
    boxEdge = max(boxEdge, boxEdgeAt(eye + volumeRay * (volumeFar - inset), volumeMin, volumeMax, boxThickness));
  }
  let boxColor = select(vec3<f32>(1.0, 0.42, 0.10), vec3<f32>(0.18, 0.95, 0.70), params.dims.y > 100);
  color = mix(color, boxColor, boxEdge * 0.84) + boxColor * boxEdge * 0.22;
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
      size: 96,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
      label: 'volume-camera',
    });
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

  updateCamera(camera, width, height, time, lod) {
    const renderWidth = this.renderWidth || width;
    const renderHeight = this.renderHeight || height;
    this.device.queue.writeBuffer(this.cameraBuffer, 0, cameraUniformData(camera, renderWidth, renderHeight, time, lod));
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
  }
}
