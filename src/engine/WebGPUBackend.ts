import { computeShader, renderShader } from "./shaders";
import { packUniforms } from "./uniforms";
import { hasVisibleFrame } from "./startup";
import { floatToHalf, halfToFloat } from "./math";
import {
  GPU_SIZE,
  type Backend,
  type FrameState,
  type Settings,
  type Tool,
  type Vec3,
  type VolumeSize,
} from "./types";

export class WebGPUBackend implements Backend {
  readonly name = "WebGPU" as const;
  readonly size: VolumeSize = { ...GPU_SIZE };
  private device!: GPUDevice;
  private adapter!: GPUAdapter;
  private context!: GPUCanvasContext;
  private uniform!: GPUBuffer;
  private sampler!: GPUSampler;
  private fields!: GPUTexture[];
  private original!: GPUTexture;
  private flux!: GPUTexture;
  private current = 0;
  private pipelines: Record<string, GPUComputePipeline> = {};
  private renderPipeline!: GPURenderPipeline;
  private groups = new Map<string, GPUBindGroup>();
  private pickResult!: GPUBuffer;
  private pickRead!: GPUBuffer;
  private lastFrame!: FrameState;
  private destroyed = false;
  private pendingFrames = 0;
  private relaxationCycle = 0;
  private brushCycle = 0;
  private initialized = false;
  onLost?: (message: string) => void;
  constructor(private canvas: HTMLCanvasElement) {}

  private assertActive() {
    if (this.destroyed)
      throw new DOMException("WebGPU startup was cancelled", "AbortError");
  }
  async initialize(settings: Settings): Promise<void> {
    this.assertActive();
    if (!navigator.gpu) throw new Error("This browser does not expose WebGPU.");
    const adapter = await navigator.gpu.requestAdapter({
      powerPreference: "high-performance",
    });
    this.assertActive();
    if (!adapter) throw new Error("No WebGPU adapter was available.");
    this.adapter = adapter;
    const device = await this.adapter.requestDevice();
    if (this.destroyed) {
      device.destroy();
      this.assertActive();
    }
    this.device = device;
    this.device.lost.then((info) => {
      if (!this.destroyed) {
        this.destroyed = true;
        this.onLost?.(
          info.message || "Graphics device disconnected. Reload to reconnect.",
        );
      }
    });
    this.device.addEventListener("uncapturederror", (e) => {
      console.error("WebGPU:", e.error.message);
      if (this.initialized) this.onLost?.(e.error.message);
    });
    this.uniform = this.device.createBuffer({
      size: 224,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.sampler = this.device.createSampler({
      magFilter: "linear",
      minFilter: "linear",
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
    this.flux = make("Conservative face flux");
    this.pickResult = this.device.createBuffer({
      size: 16,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
    });
    this.pickRead = this.device.createBuffer({
      size: 16,
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
    for (const module of [compute, render]) {
      const info = await module.getCompilationInfo();
      this.assertActive();
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
    this.assertActive();
    const format = navigator.gpu.getPreferredCanvasFormat();
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
    this.context = this.canvas.getContext("webgpu")!;
    if (!this.context) throw new Error("WebGPU canvas context unavailable.");
    this.context.configure({
      device: this.device,
      format,
      alphaMode: "opaque",
      usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC,
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
    const probe = this.device.createCommandEncoder();
    const pass = probe.beginRenderPass({
      colorAttachments: [
        {
          view: this.context.getCurrentTexture().createView(),
          clearValue: { r: 0.15, g: 0.18, b: 0.16, a: 1 },
          loadOp: "clear",
          storeOp: "store",
        },
      ],
    });
    pass.end();
    this.device.queue.submit([probe.finish()]);
    await this.device.queue.onSubmittedWorkDone();
    if (this.destroyed)
      throw new Error("The GPU canvas could not be initialized.");
    this.initialized = true;
  }
  async regenerate(settings: Settings) {
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
  }
  ready() {
    return this.pendingFrames === 0 && !this.destroyed;
  }
  async sync() {
    await this.device.queue.onSubmittedWorkDone();
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
    if (["render", "pickSurface"].includes(kind))
      entries.push({ binding: 2, resource: this.sampler });
    if (["initialize", "evolve", "sculpt", "redistance"].includes(kind))
      entries.push({
        binding: 3,
        resource:
          this.fields[kind === "initialize" ? index : 1 - index].createView(),
      });
    if (kind === "flux")
      entries.push({ binding: 4, resource: this.flux.createView() });
    if (kind === "evolve")
      entries.push({ binding: 5, resource: this.flux.createView() });
    if (kind === "pickSurface")
      entries.push({ binding: 6, resource: { buffer: this.pickResult } });
    const pipeline =
      kind === "render" ? this.renderPipeline : this.pipelines[kind];
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
    if (kind === "evolve" || kind === "sculpt" || kind === "redistance")
      this.current = 1 - this.current;
  }
  render(frame: FrameState) {
    this.drawFrame(frame);
  }
  private drawFrame(
    frame: FrameState,
    probe?: { buffer: GPUBuffer; rowBytes: number; rows: number },
  ) {
    if (this.destroyed) return;
    this.lastFrame = frame;
    this.device.queue.writeBuffer(
      this.uniform,
      0,
      packUniforms(frame, this.size),
    );
    const enc = this.device.createCommandEncoder();
    const target = this.context.getCurrentTexture();
    const pass = enc.beginRenderPass({
      colorAttachments: [
        {
          view: target.createView(),
          loadOp: "clear",
          storeOp: "store",
          clearValue: { r: 0.22, g: 0.26, b: 0.28, a: 1 },
        },
      ],
    });
    pass.setPipeline(this.renderPipeline);
    pass.setBindGroup(
      0,
      this.group("render", frame.compare ? 2 : this.current),
    );
    pass.draw(3);
    pass.end();
    if (probe) {
      // Read the actual render target in the same submission. Reading a WebGPU
      // canvas through a 2D context after an await can instead see a recycled
      // swapchain image, so it is not a reliable first-frame test.
      for (let row = 0; row < probe.rows; row++) {
        const y = Math.min(
          frame.height - 1,
          Math.floor(((row + 0.5) * frame.height) / probe.rows),
        );
        enc.copyTextureToBuffer(
          { texture: target, origin: [0, y, 0] },
          {
            buffer: probe.buffer,
            offset: row * probe.rowBytes,
            bytesPerRow: probe.rowBytes,
          },
          [frame.width, 1, 1],
        );
      }
    }
    this.device.queue.submit([enc.finish()]);
    this.pendingFrames++;
    void this.device.queue.onSubmittedWorkDone().then(
      () => this.pendingFrames--,
      () => {
        this.pendingFrames--;
      },
    );
  }
  async verifyFrame(frame: FrameState): Promise<boolean> {
    this.assertActive();
    const rows = 32,
      columns = 32,
      rowBytes = Math.ceil((frame.width * 4) / 256) * 256;
    const buffer = this.device.createBuffer({
      label: "First-frame pixel verification",
      size: rowBytes * rows,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
    });
    try {
      this.drawFrame(frame, { buffer, rowBytes, rows });
      await buffer.mapAsync(GPUMapMode.READ);
      this.assertActive();
      const source = new Uint8Array(buffer.getMappedRange());
      const pixels = new Uint8Array(rows * columns * 4);
      for (let y = 0; y < rows; y++)
        for (let x = 0; x < columns; x++) {
          const sx = Math.min(
            frame.width - 1,
            Math.floor(((x + 0.5) * frame.width) / columns),
          );
          const start = y * rowBytes + sx * 4;
          pixels.set(source.subarray(start, start + 4), (y * columns + x) * 4);
        }
      return hasVisibleFrame(pixels);
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
      if (
        ++this.relaxationCycle % 2 === 0 &&
        (settings.thermal > 0 || settings.erosion > 0)
      )
        this.run("redistance", enc);
    }
    this.device.queue.submit([enc.finish()]);
  }
  sculpt(center: Vec3, tool: Tool, settings: Settings) {
    const a = packUniforms({ ...this.lastFrame, settings, tool }, this.size);
    a.set([...center, settings.strength], 44);
    this.device.queue.writeBuffer(this.uniform, 0, a);
    const enc = this.device.createCommandEncoder();
    this.run("sculpt", enc);
    if (++this.brushCycle % 4 === 0) this.run("redistance", enc);
    this.device.queue.submit([enc.finish()]);
  }
  async pick(frame: FrameState, x: number, y: number): Promise<Vec3 | null> {
    if (this.destroyed) return null;
    const a = packUniforms(frame, this.size);
    a.set([x, y, 0, 0], 52);
    this.device.queue.writeBuffer(this.uniform, 0, a);
    const enc = this.device.createCommandEncoder();
    this.run("pickSurface", enc);
    enc.copyBufferToBuffer(this.pickResult, 0, this.pickRead, 0, 16);
    this.device.queue.submit([enc.finish()]);
    await this.pickRead.mapAsync(GPUMapMode.READ);
    const v = new Float32Array(this.pickRead.getMappedRange()).slice();
    this.pickRead.unmap();
    return v[3] > 0.5 ? [v[0], v[1], v[2]] : null;
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
    this.device.queue.writeTexture(
      { texture: this.fields[this.current] },
      half,
      { bytesPerRow: this.size.x * 8, rowsPerImage: this.size.y },
      [this.size.x, this.size.y, this.size.z],
    );
  }
  reset() {
    this.relaxationCycle = 0;
    this.brushCycle = 0;
    const enc = this.device.createCommandEncoder();
    enc.copyTextureToTexture(
      { texture: this.original },
      { texture: this.fields[this.current] },
      [this.size.x, this.size.y, this.size.z],
    );
    this.device.queue.submit([enc.finish()]);
  }
  dispose() {
    this.destroyed = true;
    this.context?.unconfigure();
    this.fields?.forEach((t) => t.destroy());
    this.original?.destroy();
    this.flux?.destroy();
    this.uniform?.destroy();
    this.pickResult?.destroy();
    this.pickRead?.destroy();
    this.device?.destroy();
  }
}
