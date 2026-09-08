import { FoamSystem } from "./foam/system";
import { GPUFoamDriver } from "./foam/drivers";
import {
  computeShader,
  presentShader,
  renderShader,
  cinematicShader,
} from "./shaders";
import heightShader from "./shaders/terrain-height.wgsl?raw";
import { GPUSatmapTextures } from "./satmaps/textures";
import {
  buildTerrainMaps,
  type TerrainMaps,
  type HeightSurface,
} from "./satmaps/terrainMaps";
import { encodeFramePNG } from "./presentation";
import { BitmapPresenter, type GPUDisplayMode } from "./display";
import { packUniforms, UNIFORM_BYTES } from "./uniforms";
import { diagnostics, summarizePixels } from "../diagnostics";
import { floatToHalf, halfToFloat } from "./math";
import {
  GPU_SIZE,
  type Backend,
  type FrameState,
  type Settings,
  type Tool,
  type Vec3,
  type VolumeSize,
  type BrushStamp,
  type BrushHit,
} from "./types";

export class WebGPUBackend implements Backend {
  readonly name = "WebGPU" as const;
  readonly size: VolumeSize = { ...GPU_SIZE };
  private device!: GPUDevice;
  private adapter!: GPUAdapter;
  private context!: GPUCanvasContext;
  private contextConfigured = false;
  private bitmap?: BitmapPresenter;
  private displayReadback?: GPUBuffer;
  private displayReadbackBytes = 0;
  private displayTask: Promise<void> = Promise.resolve();
  private lastBitmapProbe: unknown = null;
  private uniform!: GPUBuffer;
  private sampler!: GPUSampler;
  private fields!: GPUTexture[];
  private original!: GPUTexture;
  private satTextures!: GPUSatmapTextures;
  private heightPipeline!: GPUComputePipeline;
  private heightTexture!: GPUTexture;
  private terrainMaps!: TerrainMaps;
  private originalTerrainMaps!: TerrainMaps;
  private terrainRevision = 0;
  private bakedTerrainRevision = -1;
  private terrainMapTask: Promise<void> | null = null;
  private terrainMapsAt = 0;
  private flux!: GPUTexture;
  private current = 0;
  private pipelines: Record<string, GPUComputePipeline> = {};
  private renderPipeline!: GPURenderPipeline;
  private cinematicPipeline!: GPURenderPipeline;
  private foam!: FoamSystem<GPUTexture>;
  private foamVersion = -1;
  private presentPipeline!: GPURenderPipeline;
  private presentGroup!: GPUBindGroup;
  private renderTarget?: GPUTexture;
  private format!: GPUTextureFormat;
  private groups = new Map<string, GPUBindGroup>();
  private pickResult!: GPUBuffer;
  private pickRead!: GPUBuffer;
  private lastFrame!: FrameState;
  private destroyed = false;
  private pendingFrames = 0;
  private relaxationCycle = 0;
  private brushCycle = 0;
  private initialized = false;
  private submittedFrames = 0;
  private completedFrames = 0;
  private lastSubmission = 0;
  private lastCompletion = 0;
  private lastProbe: unknown = null;
  private lastOffscreenProbe: unknown = null;
  private deviceLoss: unknown = null;
  onLost?: (message: string) => void;
  constructor(
    private canvas: HTMLCanvasElement,
    readonly displayMode: GPUDisplayMode = "native",
  ) {}

  private assertActive() {
    if (this.destroyed)
      throw new DOMException("WebGPU startup was cancelled", "AbortError");
  }
  async initialize(settings: Settings): Promise<void> {
    this.assertActive();
    if (!navigator.gpu) throw new Error("This browser does not expose WebGPU.");
    diagnostics.log("WebGPU", "Requesting adapter", {
      powerPreference: "browser default (no ignored Windows hint)",
      displayMode: this.displayMode,
    });
    const adapter = await navigator.gpu.requestAdapter();
    this.assertActive();
    if (!adapter) throw new Error("No WebGPU adapter was available.");
    this.adapter = adapter;
    const info = adapter.info;
    diagnostics.log("WebGPU", "Adapter selected", {
      vendor: info?.vendor,
      architecture: info?.architecture,
      device: info?.device,
      description: info?.description,
      isFallbackAdapter: info?.isFallbackAdapter,
      features: [...adapter.features],
    });
    diagnostics.log("WebGPU", "Requesting device");
    const device = await this.adapter.requestDevice();
    if (this.destroyed) {
      device.destroy();
      this.assertActive();
    }
    this.device = device;
    diagnostics.log("WebGPU", "Device created", {
      features: [...device.features],
      limits: {
        maxTextureDimension2D: device.limits.maxTextureDimension2D,
        maxTextureDimension3D: device.limits.maxTextureDimension3D,
        maxBufferSize: device.limits.maxBufferSize,
        maxStorageBufferBindingSize: device.limits.maxStorageBufferBindingSize,
        maxComputeInvocationsPerWorkgroup:
          device.limits.maxComputeInvocationsPerWorkgroup,
        maxComputeWorkgroupsPerDimension:
          device.limits.maxComputeWorkgroupsPerDimension,
      },
    });
    this.device.lost.then((info) => {
      this.deviceLoss = {
        reason: info.reason,
        message: info.message,
        expectedDisposal: this.destroyed,
      };
      diagnostics.log(
        "WebGPU",
        "Device lost",
        this.deviceLoss,
        this.destroyed ? "info" : "error",
      );
      if (!this.destroyed) {
        this.destroyed = true;
        this.onLost?.(
          info.message || "Graphics device disconnected. Reload to reconnect.",
        );
      }
    });
    this.device.addEventListener("uncapturederror", (e) => {
      diagnostics.log(
        "WebGPU",
        "Uncaptured GPU error",
        { type: e.error.constructor.name, message: e.error.message },
        "error",
      );
      console.error("WebGPU:", e.error.message);
      if (this.initialized) this.onLost?.(e.error.message);
    });
    this.uniform = this.device.createBuffer({
      size: UNIFORM_BYTES,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.sampler = this.device.createSampler({
      magFilter: "linear",
      minFilter: "linear",
      mipmapFilter: "linear",
      addressModeU: "clamp-to-edge",
      addressModeV: "clamp-to-edge",
      addressModeW: "clamp-to-edge",
    });
    const make = (label: string) =>
      this.device.createTexture({
        label,
        size: [this.size.x, this.size.y, this.size.z],
        dimension: "3d",
        format: "rgba16float",
        usage:
          GPUTextureUsage.TEXTURE_BINDING |
          GPUTextureUsage.STORAGE_BINDING |
          GPUTextureUsage.COPY_SRC |
          GPUTextureUsage.COPY_DST,
      });
    this.fields = [
      make("SDF + water + sediment + erosion A"),
      make("SDF + water + sediment + erosion B"),
    ];
    this.original = make("Unmodified procedural volume");
    this.satTextures = new GPUSatmapTextures(device, this.size.x, this.size.z);
    this.satTextures.update(settings);
    this.heightTexture = device.createTexture({
      label: "Drainage upper envelope",
      size: [this.size.x, this.size.z],
      format: "rgba32float",
      usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC,
    });
    this.flux = make("Conservative face flux");
    this.pickResult = this.device.createBuffer({
      size: 32,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
    });
    this.pickRead = this.device.createBuffer({
      size: 32,
      usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
    });
    const compute = this.device.createShaderModule({
      label: "Volumetric terrain simulation",
      code: computeShader,
    });
    const render = this.device.createShaderModule({
      label: "SDF raymarch renderer",
      code: renderShader,
    });
    const heightModule = device.createShaderModule({
      label: "SatMap upper-envelope extraction",
      code: heightShader,
    });
    for (const module of [compute, render, heightModule]) {
      const info = await module.getCompilationInfo();
      this.assertActive();
      diagnostics.log(
        "WGSL",
        `Shader compilation: ${module.label}`,
        {
          messages: info.messages.map((m) => ({
            type: m.type,
            line: m.lineNum,
            column: m.linePos,
            message: m.message,
          })),
        },
        info.messages.some((m) => m.type === "error") ? "error" : "info",
      );
      const errors = info.messages.filter((m) => m.type === "error");
      if (errors.length)
        throw new Error(
          errors
            .map((e) => `Shader ${e.lineNum}:${e.linePos}: ${e.message}`)
            .join("\n"),
        );
    }
    await Promise.all(
      [
        "initialize",
        "flux",
        "evolve",
        "talusFlux",
        "talusSettle",
        "sculpt",
        "redistance",
        "pickSurface",
      ].map(async (entryPoint) => {
        this.pipelines[entryPoint] =
          await this.device.createComputePipelineAsync({
            label: entryPoint,
            layout: "auto",
            compute: { module: compute, entryPoint },
          });
      }),
    );
    this.heightPipeline = await device.createComputePipelineAsync({
      label: "Terrain drainage readback",
      layout: "auto",
      compute: { module: heightModule, entryPoint: "extractHeight" },
    });
    this.assertActive();
    diagnostics.log(
      "WebGPU",
      "Compute pipelines ready",
      Object.keys(this.pipelines),
    );
    const format = (this.format =
      this.displayMode === "safe"
        ? "rgba8unorm"
        : navigator.gpu.getPreferredCanvasFormat());
    this.renderPipeline = await this.device.createRenderPipelineAsync({
      layout: "auto",
      vertex: { module: render, entryPoint: "vertexMain" },
      fragment: {
        module: render,
        entryPoint: "fragmentMain",
        targets: [{ format }],
      },
      primitive: { topology: "triangle-list" },
    });
    this.assertActive();
    const foamDriver = new GPUFoamDriver(device, this.uniform, format);
    await foamDriver.initialize();
    this.assertActive();
    this.foam = new FoamSystem(foamDriver);
    this.foam.configure(settings.foamQuality);
    const cinematicModule = device.createShaderModule({
      label: "Cinematic HDR and hit data",
      code: cinematicShader,
    });
    this.cinematicPipeline = await device.createRenderPipelineAsync({
      label: "Cinematic terrain/water",
      layout: "auto",
      vertex: { module: cinematicModule, entryPoint: "vertexMain" },
      fragment: {
        module: cinematicModule,
        entryPoint: "cinematicMain",
        targets: [
          { format: "rgba16float" },
          { format: "rgba16float" },
          { format: "rgba16float" },
        ],
      },
      primitive: { topology: "triangle-list" },
    });
    this.assertActive();
    if (this.displayMode === "native") {
      const presenter = this.device.createShaderModule({ code: presentShader });
      this.presentPipeline = await this.device.createRenderPipelineAsync({
        label: "Native canvas presentation",
        layout: "auto",
        vertex: { module: presenter, entryPoint: "vertexMain" },
        fragment: {
          module: presenter,
          entryPoint: "fragmentMain",
          targets: [{ format }],
        },
        primitive: { topology: "triangle-list" },
      });
      this.assertActive();
      this.context = this.canvas.getContext("webgpu")!;
      if (!this.context) throw new Error("WebGPU canvas context unavailable.");
      // configure() is deliberately deferred until the first VISIBLE draw.
    } else {
      this.bitmap = new BitmapPresenter(this.canvas);
    }
    diagnostics.log("WebGPU", "Render/display pipelines ready", {
      format,
      displayMode: this.displayMode,
      nativeCanvas: this.displayMode === "native",
      note: this.bitmap
        ? "GPU terrain + GPU erosion, copied into a software-backed 2D bitmap for display"
        : "Native canvas will configure on its first visible draw",
    });
    this.lastFrame = {
      eye: [67, 59, 81],
      forward: [0, 0, -1],
      right: [1, 0, 0],
      up: [0, 1, 0],
      width: 1,
      height: 1,
      time: 0,
      settings,
      brush: null,
      tool: "orbit",
      compare: false,
    };
    this.device.queue.writeBuffer(
      this.uniform,
      0,
      packUniforms(this.lastFrame, this.size),
    );
    this.run("initialize");
    const enc = this.device.createCommandEncoder();
    enc.copyTextureToTexture(
      { texture: this.fields[this.current] },
      { texture: this.original },
      [this.size.x, this.size.y, this.size.z],
    );
    this.device.queue.submit([enc.finish()]);
    await this.device.queue.onSubmittedWorkDone();
    if (this.destroyed)
      throw new Error("The GPU canvas could not be initialized.");
    await this.refreshTerrainMaps();
    this.assertActive();
    this.originalTerrainMaps = this.terrainMaps;
    this.satTextures.writeTerrain(this.originalTerrainMaps, true);
    this.initialized = true;
    diagnostics.log("WebGPU", "Initial 3D volume submitted and completed", {
      size: this.size,
      preset: settings.preset,
      seed: settings.seed,
    });
  }
  getDiagnostics(): Record<string, unknown> {
    return {
      backend: this.name,
      displayMode: this.displayMode,
      nativeCanvasConfigured: this.contextConfigured,
      bitmapFrames: this.bitmap?.frames ?? 0,
      lastBitmapProbe: this.lastBitmapProbe,
      initialized: this.initialized,
      destroyed: this.destroyed,
      format: this.format,
      deviceLoss: this.deviceLoss,
      submittedFrames: this.submittedFrames,
      completedFrames: this.completedFrames,
      pendingFrames: this.pendingFrames,
      msSinceLastSubmission: this.lastSubmission
        ? Math.round(performance.now() - this.lastSubmission)
        : null,
      msSinceLastCompletion: this.lastCompletion
        ? Math.round(performance.now() - this.lastCompletion)
        : null,
      volumeSize: this.size,
      activeField: this.current,
      renderTargetSize: this.renderTarget
        ? [this.renderTarget.width, this.renderTarget.height]
        : null,
      pipelines: Object.keys(this.pipelines),
      foam: this.foam?.diagnostics(),
      satelliteMaps: {
        size: [this.size.x, this.size.z],
        revision: this.terrainRevision,
        bakedRevision: this.bakedTerrainRevision,
        pending: !!this.terrainMapTask,
        range: this.terrainMaps?.range,
        paletteSamples: 256,
        detailSize: 64,
      },
      lastPixelProbe: this.lastProbe,
      lastOffscreenProbe: this.lastOffscreenProbe,
    };
  }
  async regenerate(settings: Settings) {
    this.foam?.reset();
    this.relaxationCycle = 0;
    this.brushCycle = 0;
    this.lastFrame = { ...this.lastFrame, settings };
    this.device.queue.writeBuffer(
      this.uniform,
      0,
      packUniforms(this.lastFrame, this.size),
    );
    this.run("initialize");
    const enc = this.device.createCommandEncoder();
    enc.copyTextureToTexture(
      { texture: this.fields[this.current] },
      { texture: this.original },
      [this.size.x, this.size.y, this.size.z],
    );
    this.device.queue.submit([enc.finish()]);
    await this.sync();
    this.terrainRevision++;
    this.foam?.invalidate();
    await this.refreshTerrainMaps();
    this.originalTerrainMaps = this.terrainMaps;
    this.satTextures.writeTerrain(this.originalTerrainMaps, true);
  }
  ready() {
    return this.pendingFrames === 0 && !this.destroyed;
  }
  async sync() {
    await this.device.queue.onSubmittedWorkDone();
    await this.displayTask;
  }
  refreshDisplaySurface() {
    if (this.destroyed || this.displayMode !== "native") return;
    if (this.contextConfigured) this.context.unconfigure();
    this.contextConfigured = false;
    diagnostics.log(
      "WebGPU",
      "Native display surface reset after visibility change",
    );
  }
  private configureNativeCanvas() {
    if (this.contextConfigured) return;
    this.context.configure({
      device: this.device,
      format: this.format,
      alphaMode: "opaque",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
    });
    this.contextConfigured = true;
    diagnostics.log("WebGPU", "Native canvas configured for visible drawing", {
      visibility: document.visibilityState,
      format: this.format,
      bitmapSize: [this.canvas.width, this.canvas.height],
    });
  }
  private prepareBitmapReadback(width: number, height: number) {
    const rowBytes = Math.ceil((width * 4) / 256) * 256;
    const size = rowBytes * height;
    if (!this.displayReadback || this.displayReadbackBytes !== size) {
      this.displayReadback?.destroy();
      this.displayReadback = this.device.createBuffer({
        label: "Safe display: owned frame pixels",
        size,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
      });
      this.displayReadbackBytes = size;
    }
    return { buffer: this.displayReadback, rowBytes };
  }
  private async presentBitmap(
    buffer: GPUBuffer,
    rowBytes: number,
    width: number,
    height: number,
  ) {
    try {
      await buffer.mapAsync(GPUMapMode.READ);
      this.assertActive();
      this.bitmap!.present(
        new Uint8Array(buffer.getMappedRange()),
        width,
        height,
        rowBytes,
      );
      if (this.bitmap!.frames === 1)
        diagnostics.log("WebGPU", "First safe-display bitmap presented", {
          width,
          height,
        });
    } finally {
      if (buffer.mapState === "mapped") buffer.unmap();
    }
  }
  private group(kind: string, index = this.current): GPUBindGroup {
    const key = kind + index;
    const cached = this.groups.get(key);
    if (cached) return cached;
    const entries: GPUBindGroupEntry[] = [
      { binding: 0, resource: { buffer: this.uniform } },
    ];
    const tex = index === 2 ? this.original : this.fields[index];
    if (kind !== "initialize")
      entries.push({ binding: 1, resource: tex.createView() });
    if (["render", "cinematic", "pickSurface"].includes(kind))
      entries.push({ binding: 2, resource: this.sampler });
    if (
      ["initialize", "evolve", "sculpt", "redistance", "talusSettle"].includes(
        kind,
      )
    )
      entries.push({
        binding: 3,
        resource:
          this.fields[kind === "initialize" ? index : 1 - index].createView(),
      });
    if (kind === "flux" || kind === "talusFlux")
      entries.push({ binding: 4, resource: this.flux.createView() });
    if (kind === "evolve" || kind === "talusSettle")
      entries.push({ binding: 5, resource: this.flux.createView() });
    if (kind === "pickSurface")
      entries.push({ binding: 6, resource: { buffer: this.pickResult } });
    if (kind === "render" || kind === "cinematic")
      entries.push(
        { binding: 7, resource: this.satTextures.palette.createView() },
        { binding: 8, resource: this.satTextures.detail.createView() },
        {
          binding: 9,
          resource: this.satTextures.terrain[index === 2 ? 1 : 0].createView(),
        },
      );
    if (kind === "render" || kind === "cinematic") {
      const maps = this.foam.bindings();
      for (const binding of [10, 11, 12, 13, 14])
        entries.push({ binding, resource: maps.get(binding)!.createView() });
    }
    const pipeline =
      kind === "cinematic"
        ? this.cinematicPipeline
        : kind === "render"
          ? this.renderPipeline
          : this.pipelines[kind];
    const group = this.device.createBindGroup({
      layout: pipeline.getBindGroupLayout(0),
      entries,
    });
    this.groups.set(key, group);
    return group;
  }
  private run(kind: string, encoder?: GPUCommandEncoder) {
    const enc = encoder || this.device.createCommandEncoder();
    const pass = enc.beginComputePass({ label: kind });
    pass.setPipeline(this.pipelines[kind]);
    pass.setBindGroup(0, this.group(kind));
    if (kind === "pickSurface") pass.dispatchWorkgroups(1);
    else
      pass.dispatchWorkgroups(
        Math.ceil(this.size.x / 4),
        Math.ceil(this.size.y / 4),
        Math.ceil(this.size.z / 4),
      );
    pass.end();
    if (!encoder) this.device.queue.submit([enc.finish()]);
    if (
      kind === "evolve" ||
      kind === "sculpt" ||
      kind === "redistance" ||
      kind === "talusSettle"
    )
      this.current = 1 - this.current;
  }
  render(frame: FrameState) {
    this.drawFrame(frame);
  }
  private getRenderTarget(width: number, height: number): GPUTexture {
    if (
      this.renderTarget?.width === width &&
      this.renderTarget.height === height
    )
      return this.renderTarget;
    this.renderTarget?.destroy();
    this.renderTarget = this.device.createTexture({
      label: "Owned, scaled terrain frame",
      size: [width, height],
      format: this.format,
      usage:
        GPUTextureUsage.RENDER_ATTACHMENT |
        GPUTextureUsage.TEXTURE_BINDING |
        GPUTextureUsage.COPY_SRC,
    });
    if (this.displayMode === "native")
      this.presentGroup = this.device.createBindGroup({
        layout: this.presentPipeline.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: this.renderTarget.createView() },
          { binding: 1, resource: this.sampler },
        ],
      });
    return this.renderTarget;
  }
  private drawFrame(
    frame: FrameState,
    probe?: { buffer: GPUBuffer; rowBytes: number; rows?: number },
    present = true,
  ) {
    this.assertActive();
    this.lastFrame = frame;
    this.satTextures.update(frame.settings);
    if (
      !this.terrainMapTask &&
      this.terrainRevision !== this.bakedTerrainRevision &&
      performance.now() - this.terrainMapsAt > 500
    ) {
      void this.refreshTerrainMaps().catch((error) => {
        if (!this.destroyed)
          diagnostics.log(
            "SatMaps",
            "Drainage refresh failed; retaining last map",
            error,
            "warn",
          );
      });
    }
    frame = this.foam.effectiveFrame(frame);
    if (this.foam.version !== this.foamVersion) {
      this.groups.clear();
      this.foamVersion = this.foam.version;
    }
    const packed = packUniforms(
      frame,
      this.size,
      (frame.compare ? this.originalTerrainMaps : this.terrainMaps).range,
    );
    packed[171] = this.foam.time;
    this.device.queue.writeBuffer(this.uniform, 0, packed);
    const field = frame.compare ? this.original : this.fields[this.current];
    this.foam.update(frame, field, present);
    packed[171] = this.foam.time;
    this.device.queue.writeBuffer(this.uniform, 0, packed);
    const target = this.getRenderTarget(frame.width, frame.height);
    const cinematic = this.foam.isCinematic(frame);
    if (cinematic) this.foam.prepareScreen(frame.width, frame.height);
    let enc = this.device.createCommandEncoder();
    const targets = cinematic
      ? [
          this.foam.scene.handle,
          this.foam.hits.handle,
          this.foam.transmitted.handle,
        ]
      : [target];
    const pass = enc.beginRenderPass({
      colorAttachments: targets.map((t) => ({
        view: t.createView(),
        loadOp: "clear" as const,
        storeOp: "store" as const,
        clearValue: { r: 0, g: 0, b: 0, a: 0 },
      })),
    });
    pass.setPipeline(cinematic ? this.cinematicPipeline : this.renderPipeline);
    pass.setBindGroup(
      0,
      this.group(
        cinematic ? "cinematic" : "render",
        frame.compare ? 2 : this.current,
      ),
    );
    pass.draw(3);
    pass.end();
    if (cinematic) {
      this.device.queue.submit([enc.finish()]);
      this.foam.composite(
        {
          handle: target,
          width: frame.width,
          height: frame.height,
          format: "rgba8unorm",
        },
        field,
      );
      enc = this.device.createCommandEncoder();
    }
    let readTarget = target;
    const bitmapCopy =
      present && this.bitmap
        ? this.prepareBitmapReadback(frame.width, frame.height)
        : null;
    if (present && !this.bitmap) {
      this.configureNativeCanvas();
      // Scaling an owned texture is cheap and never resizes/clears the visible
      // canvas just because the expensive raymarch resolution has changed.
      readTarget = this.context.getCurrentTexture();
      const output = enc.beginRenderPass({
        colorAttachments: [
          {
            view: readTarget.createView(),
            loadOp: "clear",
            storeOp: "store",
            clearValue: { r: 0, g: 0, b: 0, a: 1 },
          },
        ],
      });
      output.setPipeline(this.presentPipeline);
      output.setBindGroup(0, this.presentGroup);
      output.draw(3);
      output.end();
    }
    if (bitmapCopy) {
      enc.copyTextureToBuffer(
        { texture: target },
        { buffer: bitmapCopy.buffer, bytesPerRow: bitmapCopy.rowBytes },
        [frame.width, frame.height, 1],
      );
    }
    if (probe) {
      // Read before presentation recycles the canvas texture. Verification
      // checks the actual presentation target; PNG capture uses our own target.
      if (probe.rows) {
        for (let row = 0; row < probe.rows; row++) {
          const y = Math.min(
            readTarget.height - 1,
            Math.floor(((row + 0.5) * readTarget.height) / probe.rows),
          );
          enc.copyTextureToBuffer(
            { texture: readTarget, origin: [0, y, 0] },
            {
              buffer: probe.buffer,
              offset: row * probe.rowBytes,
              bytesPerRow: probe.rowBytes,
            },
            [readTarget.width, 1, 1],
          );
        }
      } else {
        enc.copyTextureToBuffer(
          { texture: readTarget },
          { buffer: probe.buffer, bytesPerRow: probe.rowBytes },
          [readTarget.width, readTarget.height, 1],
        );
      }
    }
    this.device.queue.submit([enc.finish()]);
    this.pendingFrames++;
    this.submittedFrames++;
    this.lastSubmission = performance.now();
    if (this.submittedFrames === 1)
      diagnostics.log("WebGPU", "First terrain draw submitted", {
        renderSize: [frame.width, frame.height],
        displaySize: [this.canvas.width, this.canvas.height],
      });
    const completion = bitmapCopy
      ? this.presentBitmap(
          bitmapCopy.buffer,
          bitmapCopy.rowBytes,
          frame.width,
          frame.height,
        )
      : this.device.queue.onSubmittedWorkDone();
    if (bitmapCopy) this.displayTask = completion;
    void completion.then(
      () => {
        this.pendingFrames--;
        this.completedFrames++;
        this.lastCompletion = performance.now();
        if (this.completedFrames === 1)
          diagnostics.log("WebGPU", "First terrain draw completed");
      },
      (error) => {
        this.pendingFrames--;
        diagnostics.log("WebGPU", "Frame submission rejected", error, "error");
        if (!this.destroyed && this.initialized)
          this.onLost?.(`GPU submission failed: ${String(error)}`);
      },
    );
  }
  async verifyFrame(
    frame: FrameState,
    target: "presentation" | "offscreen" = "presentation",
  ): Promise<boolean> {
    this.assertActive();
    const width =
      target === "presentation" && !this.bitmap
        ? this.canvas.width
        : frame.width;
    const rows = 32,
      columns = 32,
      rowBytes = Math.ceil((width * 4) / 256) * 256;
    const buffer = this.device.createBuffer({
      label: "First-frame pixel verification",
      size: rowBytes * rows,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });
    try {
      this.drawFrame(
        frame,
        { buffer, rowBytes, rows },
        target === "presentation",
      );
      await buffer.mapAsync(GPUMapMode.READ);
      this.assertActive();
      const source = new Uint8Array(buffer.getMappedRange());
      const pixels = new Uint8Array(rows * columns * 4);
      for (let y = 0; y < rows; y++)
        for (let x = 0; x < columns; x++) {
          const sx = Math.min(
            width - 1,
            Math.floor(((x + 0.5) * width) / columns),
          );
          const start = y * rowBytes + sx * 4;
          pixels.set(source.subarray(start, start + 4), (y * columns + x) * 4);
        }
      const summary = summarizePixels(pixels, this.format === "bgra8unorm");
      const probe = {
        at: new Date().toISOString(),
        source:
          target === "presentation"
            ? this.bitmap
              ? "GPU source for safe bitmap display (not a screen capture)"
              : "GPU presentation texture (not a screen capture)"
            : "Owned GPU render target (not a screen capture)",
        ...summary,
      };
      if (target === "presentation") this.lastProbe = probe;
      else this.lastOffscreenProbe = probe;
      diagnostics.log(
        "WebGPU",
        target === "presentation"
          ? "Presentation texture pixel check"
          : "Owned render target pixel check",
        probe,
        summary.nonUniformOpaque ? "info" : "warn",
      );
      if (target === "presentation" && this.bitmap) {
        await this.displayTask;
        this.assertActive();
        const bitmap = summarizePixels(this.bitmap.sample());
        this.lastBitmapProbe = {
          at: new Date().toISOString(),
          source: "Software-backed display bitmap (not the GPU swapchain)",
          ...bitmap,
        };
        diagnostics.log(
          "WebGPU",
          "Safe display bitmap pixel check",
          this.lastBitmapProbe,
          bitmap.nonUniformOpaque ? "info" : "warn",
        );
        return summary.nonUniformOpaque && bitmap.nonUniformOpaque;
      }
      return summary.nonUniformOpaque;
    } finally {
      if (buffer.mapState === "mapped") buffer.unmap();
      buffer.destroy();
    }
  }
  async capture(frame: FrameState): Promise<Blob> {
    await this.refreshTerrainMaps();
    this.assertActive();
    const rowBytes = Math.ceil((frame.width * 4) / 256) * 256;
    const buffer = this.device.createBuffer({
      label: "Owned PNG readback",
      size: rowBytes * frame.height,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });
    try {
      this.drawFrame(frame, { buffer, rowBytes }, false);
      await buffer.mapAsync(GPUMapMode.READ);
      this.assertActive();
      const source = new Uint8Array(buffer.getMappedRange());
      const pixels = new Uint8Array(frame.width * frame.height * 4);
      for (let y = 0; y < frame.height; y++)
        pixels.set(
          source.subarray(y * rowBytes, y * rowBytes + frame.width * 4),
          y * frame.width * 4,
        );
      return await encodeFramePNG(pixels, frame.width, frame.height, {
        bgra: this.format === "bgra8unorm",
      });
    } finally {
      if (buffer.mapState === "mapped") buffer.unmap();
      buffer.destroy();
    }
  }
  step(settings: Settings, count: number) {
    this.device.queue.writeBuffer(
      this.uniform,
      0,
      packUniforms({ ...this.lastFrame, settings }, this.size),
    );
    const enc = this.device.createCommandEncoder();
    for (let i = 0; i < count; i++) {
      this.run("flux", enc);
      this.run("evolve", enc);
      if (settings.thermal > 0) {
        this.run("talusFlux", enc);
        this.run("talusSettle", enc);
      }
      if (
        ++this.relaxationCycle % 2 === 0 &&
        (settings.thermal > 0 ||
          settings.erosion > 0 ||
          settings.windErosion > 0)
      )
        this.run("redistance", enc);
    }
    this.device.queue.submit([enc.finish()]);
    this.terrainRevision++;
    this.foam?.invalidate();
  }
  sculpt(center: Vec3, tool: Tool, settings: Settings, stamp?: BrushStamp) {
    const a = packUniforms(
      { ...this.lastFrame, settings, tool, brush: center, stamp },
      this.size,
    );
    a.set([...center, settings.strength], 44);
    this.device.queue.writeBuffer(this.uniform, 0, a);
    const enc = this.device.createCommandEncoder();
    this.run("sculpt", enc);
    if (++this.brushCycle % 4 === 0) this.run("redistance", enc);
    this.device.queue.submit([enc.finish()]);
    this.terrainRevision++;
    this.foam?.invalidate();
  }
  async pick(
    frame: FrameState,
    x: number,
    y: number,
  ): Promise<BrushHit | null> {
    if (this.destroyed) return null;
    const a = packUniforms(frame, this.size);
    a.set([x, y, 0, 0], 52);
    this.device.queue.writeBuffer(this.uniform, 0, a);
    const enc = this.device.createCommandEncoder();
    this.run("pickSurface", enc);
    enc.copyBufferToBuffer(this.pickResult, 0, this.pickRead, 0, 32);
    this.device.queue.submit([enc.finish()]);
    await this.pickRead.mapAsync(GPUMapMode.READ);
    const v = new Float32Array(this.pickRead.getMappedRange()).slice();
    this.pickRead.unmap();
    return v[3] > 0.5
      ? { position: [v[0], v[1], v[2]], normal: [v[4], v[5], v[6]] }
      : null;
  }
  /** Coalesced asynchronous 2D readback. Capture/export can await this, while
   * normal rendering keeps the previous map until a new one is complete. */
  async refreshTerrainMaps(): Promise<void> {
    while (this.terrainMapTask) await this.terrainMapTask;
    this.assertActive();
    if (this.bakedTerrainRevision === this.terrainRevision) return;
    const revision = this.terrainRevision;
    this.terrainMapsAt = performance.now();
    const task = this.readHeightSurface().then((surface) => {
      if (this.destroyed || revision < this.bakedTerrainRevision) return;
      this.terrainMaps = buildTerrainMaps(surface);
      this.satTextures.writeTerrain(this.terrainMaps);
      this.bakedTerrainRevision = revision;
    });
    this.terrainMapTask = task;
    try {
      await task;
    } finally {
      if (this.terrainMapTask === task) this.terrainMapTask = null;
    }
  }
  private async readHeightSurface(): Promise<HeightSurface> {
    const device = this.device,
      width = this.size.x,
      height = this.size.z;
    const rowBytes = Math.ceil((width * 16) / 256) * 256;
    const buffer = device.createBuffer({
      label: "Small drainage readback",
      size: rowBytes * height,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });
    try {
      const enc = device.createCommandEncoder();
      const pass = enc.beginComputePass();
      pass.setPipeline(this.heightPipeline);
      pass.setBindGroup(
        0,
        device.createBindGroup({
          layout: this.heightPipeline.getBindGroupLayout(0),
          entries: [
            { binding: 0, resource: this.fields[this.current].createView() },
            { binding: 1, resource: this.heightTexture.createView() },
          ],
        }),
      );
      pass.dispatchWorkgroups(Math.ceil(width / 8), Math.ceil(height / 8));
      pass.end();
      enc.copyTextureToBuffer(
        { texture: this.heightTexture },
        { buffer, bytesPerRow: rowBytes },
        [width, height],
      );
      device.queue.submit([enc.finish()]);
      await buffer.mapAsync(GPUMapMode.READ);
      const pixels = new Float32Array(buffer.getMappedRange());
      const heights = new Float32Array(width * height),
        valid = new Uint8Array(width * height);
      for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
          heights[y * width + x] = pixels[(y * rowBytes) / 4 + x * 4];
          valid[y * width + x] =
            pixels[(y * rowBytes) / 4 + x * 4 + 1] > 0.5 ? 1 : 0;
        }
      return { width, height, heights, valid };
    } finally {
      if (buffer.mapState === "mapped") buffer.unmap();
      buffer.destroy();
    }
  }
  async readVolume(): Promise<Float32Array> {
    const { x, y, z } = this.size;
    const row = Math.ceil((x * 8) / 256) * 256;
    const buffer = this.device.createBuffer({
      size: row * y * z,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });
    const enc = this.device.createCommandEncoder();
    enc.copyTextureToBuffer(
      { texture: this.fields[this.current] },
      { buffer, bytesPerRow: row, rowsPerImage: y },
      [x, y, z],
    );
    this.device.queue.submit([enc.finish()]);
    await buffer.mapAsync(GPUMapMode.READ);
    const half = new Uint16Array(buffer.getMappedRange()),
      data = new Float32Array(x * y * z * 4);
    for (let iz = 0; iz < z; iz++)
      for (let iy = 0; iy < y; iy++)
        for (let ix = 0; ix < x * 4; ix++)
          data[(iz * y + iy) * x * 4 + ix] = halfToFloat(
            half[((iz * y + iy) * row) / 2 + ix],
          );
    buffer.unmap();
    buffer.destroy();
    return data;
  }
  writeVolume(data: Float32Array) {
    if (data.length !== this.size.x * this.size.y * this.size.z * 4)
      throw new Error("Volume dimensions do not match.");
    const half = new Uint16Array(data.length);
    for (let i = 0; i < data.length; i++) half[i] = floatToHalf(data[i]);
    this.terrainRevision++;
    this.foam?.invalidate();
    this.device.queue.writeTexture(
      { texture: this.fields[this.current] },
      half,
      { bytesPerRow: this.size.x * 8, rowsPerImage: this.size.y },
      [this.size.x, this.size.y, this.size.z],
    );
  }
  resetFoam() {
    this.foam?.reset();
  }
  reset() {
    this.foam?.reset();
    this.relaxationCycle = 0;
    this.brushCycle = 0;
    const enc = this.device.createCommandEncoder();
    enc.copyTextureToTexture(
      { texture: this.original },
      { texture: this.fields[this.current] },
      [this.size.x, this.size.y, this.size.z],
    );
    this.device.queue.submit([enc.finish()]);
    this.terrainRevision++;
    this.foam?.invalidate();
    this.bakedTerrainRevision = this.terrainRevision;
    this.terrainMaps = this.originalTerrainMaps;
    this.satTextures.writeTerrain(this.terrainMaps);
  }
  dispose() {
    diagnostics.log("WebGPU", "Backend disposed");
    this.destroyed = true;
    this.context?.unconfigure();
    this.displayReadback?.destroy();
    this.renderTarget?.destroy();
    this.fields?.forEach((t) => t.destroy());
    this.original?.destroy();
    this.satTextures?.dispose();
    this.foam?.dispose();
    this.heightTexture?.destroy();
    this.flux?.destroy();
    this.uniform?.destroy();
    this.pickResult?.destroy();
    this.pickRead?.destroy();
    this.device?.destroy();
  }
}
