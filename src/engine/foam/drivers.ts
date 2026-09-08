import { halfToFloat } from "../math";
import {
  glFoamShaders,
  gpuFoamShader,
  passBindings,
  textureNames,
  type FoamPass,
} from "./shaders";
import type { FoamDriver, FoamFormat, FoamTexture } from "./system";

const units: Record<number, number> = {
  1: 0,
  10: 4,
  11: 5,
  12: 6,
  13: 7,
  14: 8,
  15: 9,
  17: 10,
  18: 11,
  19: 12,
  20: 13,
  21: 14,
  22: 15,
};
function atlasLevels(pixels: Uint8Array, size: number) {
  const levels = [{ pixels, size }];
  while (size > 1) {
    const next = size / 2,
      out = new Uint8Array(next * next * 4);
    for (let y = 0; y < next; y++)
      for (let x = 0; x < next; x++)
        for (let c = 0; c < 4; c++) {
          let sum = 0;
          for (let dy = 0; dy < 2; dy++)
            for (let dx = 0; dx < 2; dx++)
              sum += pixels[((y * 2 + dy) * size + x * 2 + dx) * 4 + c];
          out[(y * next + x) * 4 + c] = Math.round(sum / 4);
        }
    pixels = out;
    size = next;
    levels.push({ pixels, size });
  }
  return levels;
}

export class GLFoamDriver implements FoamDriver<WebGLTexture> {
  supported: boolean;
  reason = "";
  private programs = new Map<FoamPass, WebGLProgram>();
  private fbo = new Map<WebGLTexture, WebGLFramebuffer>();
  private attached = new Map<WebGLFramebuffer, number>();
  private readFbo: WebGLFramebuffer;
  private params: WebGLBuffer;
  constructor(
    readonly gl: WebGL2RenderingContext,
    private uniform: WebGLBuffer,
  ) {
    this.supported = !!gl.getExtension("EXT_color_buffer_float");
    this.readFbo = gl.createFramebuffer()!;
    this.params = gl.createBuffer()!;
    gl.bindBuffer(gl.UNIFORM_BUFFER, this.params);
    gl.bufferData(gl.UNIFORM_BUFFER, 48, gl.DYNAMIC_DRAW);
    if (!this.supported)
      this.reason =
        "Floating-point render targets unavailable; using Low foam.";
    if (this.supported) {
      // Probe actual renderability of both state formats, not just the extension.
      for (const format of ["rgba16float", "rgba32float"] as const) {
        const texture = this.create(2, 2, format, "Foam format probe");
        this.target([texture]);
        if (
          gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE
        )
          this.supported = false;
        this.destroy(texture);
      }
      if (!this.supported)
        this.reason =
          "Floating-point framebuffer probe failed; using Low foam.";
    }
  }
  initialize() {
    if (!this.supported) return;
    try {
      for (const kind of Object.keys(passBindings) as FoamPass[]) {
        const source = glFoamShaders(kind),
          gl = this.gl;
        const compile = (type: number, text: string) => {
          const shader = gl.createShader(type)!;
          gl.shaderSource(shader, text);
          gl.compileShader(shader);
          if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            const error = gl.getShaderInfoLog(shader);
            gl.deleteShader(shader);
            throw new Error(`${kind}: ${error}`);
          }
          return shader;
        };
        const vs = compile(gl.VERTEX_SHADER, source.vertex),
          fs = compile(gl.FRAGMENT_SHADER, source.fragment),
          program = gl.createProgram()!;
        gl.attachShader(program, vs);
        gl.attachShader(program, fs);
        gl.linkProgram(program);
        gl.deleteShader(vs);
        gl.deleteShader(fs);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
          const error = gl.getProgramInfoLog(program);
          gl.deleteProgram(program);
          throw new Error(`${kind}: ${error}`);
        }
        gl.useProgram(program);
        for (const [binding, name] of Object.entries(textureNames))
          gl.uniform1i(
            gl.getUniformLocation(program, name),
            units[Number(binding)],
          );
        for (const [i, name] of ["Params", "FoamParamsBlock"].entries()) {
          const index = gl.getUniformBlockIndex(program, name);
          if (index !== gl.INVALID_INDEX)
            gl.uniformBlockBinding(program, index, i);
        }
        this.programs.set(kind, program);
      }
    } catch (error) {
      this.supported = false;
      this.reason = `Foam shaders unavailable; using Low. ${String(error)}`;
      for (const p of this.programs.values()) this.gl.deleteProgram(p);
      this.programs.clear();
      console.warn(this.reason);
    }
  }
  create(
    width: number,
    height: number,
    format: FoamFormat,
    _label: string,
    pixels?: Uint8Array,
    mipmaps = false,
  ): FoamTexture<WebGLTexture> {
    const gl = this.gl,
      handle = gl.createTexture()!;
    gl.activeTexture(gl.TEXTURE15);
    gl.bindTexture(gl.TEXTURE_2D, handle);
    const internal =
      format === "rgba32float"
        ? gl.RGBA32F
        : format === "rgba16float"
          ? gl.RGBA16F
          : gl.RGBA8;
    const filter = format === "rgba32float" ? gl.NEAREST : gl.LINEAR;
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      internal,
      width,
      height,
      0,
      gl.RGBA,
      format === "rgba8unorm" ? gl.UNSIGNED_BYTE : gl.FLOAT,
      pixels ?? null,
    );
    if (pixels && width > 1) {
      for (const [level, mip] of atlasLevels(pixels, width).entries())
        gl.texImage2D(
          gl.TEXTURE_2D,
          level,
          internal,
          mip.size,
          mip.size,
          0,
          gl.RGBA,
          gl.UNSIGNED_BYTE,
          mip.pixels,
        );
      gl.texParameteri(
        gl.TEXTURE_2D,
        gl.TEXTURE_MIN_FILTER,
        gl.LINEAR_MIPMAP_LINEAR,
      );
    }
    const mipLevels =
      mipmaps || (pixels && width > 1) ? Math.log2(width) + 1 : 1;
    if (mipmaps) {
      for (let level = 1; level < mipLevels; level++)
        gl.texImage2D(
          gl.TEXTURE_2D,
          level,
          internal,
          width >> level,
          height >> level,
          0,
          gl.RGBA,
          gl.FLOAT,
          null,
        );
      gl.texParameteri(
        gl.TEXTURE_2D,
        gl.TEXTURE_MIN_FILTER,
        gl.LINEAR_MIPMAP_LINEAR,
      );
    }
    return { handle, width, height, format, mipLevels };
  }
  target(outputs: FoamTexture<WebGLTexture>[]) {
    const gl = this.gl;
    let fbo = this.fbo.get(outputs[0].handle);
    if (!fbo) {
      fbo = gl.createFramebuffer()!;
      this.fbo.set(outputs[0].handle, fbo);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    const count = Math.max(outputs.length, this.attached.get(fbo) ?? 0);
    for (let i = 0; i < count; i++)
      gl.framebufferTexture2D(
        gl.FRAMEBUFFER,
        gl.COLOR_ATTACHMENT0 + i,
        gl.TEXTURE_2D,
        outputs[i]?.handle ?? null,
        0,
      );
    this.attached.set(fbo, outputs.length);
    gl.drawBuffers(outputs.map((_, i) => gl.COLOR_ATTACHMENT0 + i));
    gl.viewport(0, 0, outputs[0].width, outputs[0].height);
  }
  clear(textures: FoamTexture<WebGLTexture>[]) {
    this.target(textures);
    this.gl.disable(this.gl.SCISSOR_TEST);
    for (let i = 0; i < textures.length; i++)
      this.gl.clearBufferfv(this.gl.COLOR, i, new Float32Array(4));
  }
  pass(
    kind: FoamPass,
    outputs: FoamTexture<WebGLTexture>[],
    inputs: Map<number, WebGLTexture>,
    params: Float32Array,
    instances = 0,
  ) {
    const gl = this.gl;
    this.target(outputs);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    gl.disable(gl.SCISSOR_TEST);
    gl.bindVertexArray(null);
    gl.useProgram(this.programs.get(kind)!);
    gl.bindBufferBase(gl.UNIFORM_BUFFER, 0, this.uniform);
    gl.bindBufferBase(gl.UNIFORM_BUFFER, 1, this.params);
    gl.bindBuffer(gl.UNIFORM_BUFFER, this.params);
    gl.bufferSubData(gl.UNIFORM_BUFFER, 0, params);
    for (const binding of passBindings[kind]) {
      if (!textureNames[binding]) continue;
      const texture = inputs.get(binding);
      if (!texture)
        throw new Error(`Missing foam texture ${binding} for ${kind}`);
      gl.activeTexture(gl.TEXTURE0 + units[binding]);
      gl.bindTexture(binding === 1 ? gl.TEXTURE_3D : gl.TEXTURE_2D, texture);
    }
    if (["splat", "air", "bubble"].includes(kind)) {
      gl.clearBufferfv(gl.COLOR, 0, new Float32Array(4));
      gl.enable(gl.BLEND);
      gl.blendEquation(gl.FUNC_ADD);
      gl.blendFunc(gl.ONE, gl.ONE);
      gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, instances);
      gl.disable(gl.BLEND);
    } else {
      gl.disable(gl.BLEND);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
  }
  copy(from: FoamTexture<WebGLTexture>, to: FoamTexture<WebGLTexture>) {
    const gl = this.gl;
    this.target([to]);
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.readFbo);
    gl.framebufferTexture2D(
      gl.READ_FRAMEBUFFER,
      gl.COLOR_ATTACHMENT0,
      gl.TEXTURE_2D,
      from.handle,
      0,
    );
    gl.readBuffer(gl.COLOR_ATTACHMENT0);
    gl.blitFramebuffer(
      0,
      0,
      from.width,
      from.height,
      0,
      0,
      to.width,
      to.height,
      gl.COLOR_BUFFER_BIT,
      gl.NEAREST,
    );
  }
  bindRenderTextures(input: Map<number, WebGLTexture>) {
    for (const key of [10, 11, 12, 13, 14]) {
      this.gl.activeTexture(this.gl.TEXTURE0 + units[key]);
      this.gl.bindTexture(this.gl.TEXTURE_2D, input.get(key)!);
    }
  }
  async read(texture: FoamTexture<WebGLTexture>) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.readFbo);
    gl.framebufferTexture2D(
      gl.READ_FRAMEBUFFER,
      gl.COLOR_ATTACHMENT0,
      gl.TEXTURE_2D,
      texture.handle,
      0,
    );
    gl.readBuffer(gl.COLOR_ATTACHMENT0);
    const data = new Float32Array(texture.width * texture.height * 4);
    if (texture.format === "rgba8unorm") {
      const bytes = new Uint8Array(data.length);
      gl.readPixels(
        0,
        0,
        texture.width,
        texture.height,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        bytes,
      );
      for (let i = 0; i < data.length; i++) data[i] = bytes[i] / 255;
    } else
      gl.readPixels(
        0,
        0,
        texture.width,
        texture.height,
        gl.RGBA,
        gl.FLOAT,
        data,
      );
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    return data;
  }
  mipmaps(texture: FoamTexture<WebGLTexture>) {
    if (!texture.mipLevels || texture.mipLevels < 2) return;
    this.gl.activeTexture(this.gl.TEXTURE15);
    this.gl.bindTexture(this.gl.TEXTURE_2D, texture.handle);
    this.gl.generateMipmap(this.gl.TEXTURE_2D);
  }
  forgetTarget(handle: WebGLTexture) {
    const fb = this.fbo.get(handle);
    if (fb) {
      this.gl.deleteFramebuffer(fb);
      this.fbo.delete(handle);
      this.attached.delete(fb);
    }
  }
  destroy(texture: FoamTexture<WebGLTexture>) {
    this.forgetTarget(texture.handle);
    this.gl.deleteTexture(texture.handle);
  }
  dispose() {
    for (const p of this.programs.values()) this.gl.deleteProgram(p);
    for (const fb of this.fbo.values()) this.gl.deleteFramebuffer(fb);
    this.gl.deleteFramebuffer(this.readFbo);
    this.gl.deleteBuffer(this.params);
    this.programs.clear();
    this.fbo.clear();
  }
}

export class GPUFoamDriver implements FoamDriver<GPUTexture> {
  supported = true;
  reason = "";
  private layouts = new Map<FoamPass, GPUBindGroupLayout>();
  private pipelines = new Map<FoamPass, GPURenderPipeline>();
  private params: GPUBuffer;
  private sampler: GPUSampler;
  private mipPipeline!: GPURenderPipeline;
  constructor(
    readonly device: GPUDevice,
    private uniform: GPUBuffer,
    private format: GPUTextureFormat,
  ) {
    this.params = device.createBuffer({
      label: "Foam step constants",
      size: 48,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.sampler = device.createSampler({
      magFilter: "linear",
      minFilter: "linear",
      mipmapFilter: "linear",
    });
  }
  async initialize() {
    const device = this.device,
      module = device.createShaderModule({
        label: "Shared foam GPU kernels",
        code: gpuFoamShader,
      });
    const info = await module.getCompilationInfo();
    const errors = info.messages.filter((m) => m.type === "error");
    if (errors.length)
      throw new Error(
        errors.map((m) => `Foam WGSL ${m.lineNum}: ${m.message}`).join("\n"),
      );
    const mipModule = device.createShaderModule({
      label: "Foam mip reduction",
      code: `
      @group(0) @binding(0) var image:texture_2d<f32>;
      @group(0) @binding(1) var imageSampler:sampler;
      struct V { @builtin(position) pos:vec4f, @location(0) uv:vec2f, };
      @vertex fn vertex(@builtin(vertex_index) i:u32)->V {let x=f32((i<<1u)&2u);let y=f32(i&2u);return V(vec4f(x*2.-1.,y*2.-1.,0.,1.),vec2f(x,1.-y));}
      @fragment fn fragment(v:V)->@location(0) vec4f {return textureSampleLevel(image,imageSampler,v.uv,0.);}
    `,
    });
    this.mipPipeline = await device.createRenderPipelineAsync({
      layout: "auto",
      vertex: { module: mipModule, entryPoint: "vertex" },
      fragment: {
        module: mipModule,
        entryPoint: "fragment",
        targets: [{ format: "rgba16float" }],
      },
      primitive: { topology: "triangle-list" },
    });
    await Promise.all(
      (Object.keys(passBindings) as FoamPass[]).map(async (kind) => {
        const visibility = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
        const layout = device.createBindGroupLayout({
          entries: passBindings[kind].map((binding) => {
            if (binding === 0 || binding === 16)
              return {
                binding,
                visibility,
                buffer: { type: "uniform" as const },
              };
            if (binding === 2)
              return {
                binding,
                visibility,
                sampler: { type: "filtering" as const },
              };
            return {
              binding,
              visibility,
              texture: {
                sampleType:
                  binding === 15 || binding === 17
                    ? ("unfilterable-float" as const)
                    : ("float" as const),
                viewDimension:
                  binding === 1 ? ("3d" as const) : ("2d" as const),
              },
            };
          }),
        });
        const blend = ["splat", "air", "bubble"].includes(kind);
        const formats: GPUTextureFormat[] =
          kind === "geometry"
            ? ["rgba16float", "rgba16float"]
            : kind === "particles"
              ? ["rgba32float", "rgba32float"]
              : [kind === "compose" ? this.format : "rgba16float"];
        const pipeline = await device.createRenderPipelineAsync({
          label: `Foam ${kind}`,
          layout: device.createPipelineLayout({ bindGroupLayouts: [layout] }),
          vertex: {
            module,
            entryPoint:
              kind === "splat"
                ? "splatVertex"
                : blend
                  ? "opticalVertex"
                  : "quadVertex",
          },
          fragment: {
            module,
            entryPoint: `${kind}Main`,
            targets: formats.map((format) => ({
              format,
              ...(blend
                ? {
                    blend: {
                      color: {
                        srcFactor: "one" as const,
                        dstFactor: "one" as const,
                      },
                      alpha: {
                        srcFactor: "one" as const,
                        dstFactor: "one" as const,
                      },
                    },
                  }
                : {}),
            })),
          },
          primitive: { topology: "triangle-list" },
        });
        this.layouts.set(kind, layout);
        this.pipelines.set(kind, pipeline);
      }),
    );
  }
  create(
    width: number,
    height: number,
    format: FoamFormat,
    label: string,
    pixels?: Uint8Array,
    mipmaps = false,
  ): FoamTexture<GPUTexture> {
    const mipLevels =
      mipmaps || (pixels && width > 1) ? Math.log2(width) + 1 : 1;
    const handle = this.device.createTexture({
      label,
      size: [width, height],
      format,
      mipLevelCount: mipLevels,
      usage:
        GPUTextureUsage.TEXTURE_BINDING |
        GPUTextureUsage.RENDER_ATTACHMENT |
        GPUTextureUsage.COPY_SRC |
        GPUTextureUsage.COPY_DST,
    });
    if (pixels) {
      for (const [level, mip] of atlasLevels(pixels, width).entries())
        this.device.queue.writeTexture(
          { texture: handle, mipLevel: level },
          mip.pixels,
          { bytesPerRow: mip.size * 4 },
          [mip.size, mip.size],
        );
    }
    return { handle, width, height, format, mipLevels };
  }
  clear(textures: FoamTexture<GPUTexture>[]) {
    const enc = this.device.createCommandEncoder();
    const pass = enc.beginRenderPass({
      colorAttachments: textures.map((t) => ({
        view: t.handle.createView({ baseMipLevel: 0, mipLevelCount: 1 }),
        loadOp: "clear" as const,
        storeOp: "store" as const,
        clearValue: { r: 0, g: 0, b: 0, a: 0 },
      })),
    });
    pass.end();
    this.device.queue.submit([enc.finish()]);
  }
  pass(
    kind: FoamPass,
    outputs: FoamTexture<GPUTexture>[],
    input: Map<number, GPUTexture>,
    params: Float32Array,
    instances = 0,
  ) {
    this.device.queue.writeBuffer(
      this.params,
      0,
      params as Float32Array<ArrayBuffer>,
    );
    const group = this.device.createBindGroup({
      layout: this.layouts.get(kind)!,
      entries: passBindings[kind].map((binding) => {
        if (binding === 0)
          return { binding, resource: { buffer: this.uniform } };
        if (binding === 16)
          return { binding, resource: { buffer: this.params } };
        if (binding === 2) return { binding, resource: this.sampler };
        const texture = input.get(binding);
        if (!texture)
          throw new Error(`Missing foam texture ${binding} for ${kind}`);
        return { binding, resource: texture.createView() };
      }),
    });
    const enc = this.device.createCommandEncoder({ label: `Foam ${kind}` });
    const pass = enc.beginRenderPass({
      colorAttachments: outputs.map((t) => ({
        view: t.handle.createView({ baseMipLevel: 0, mipLevelCount: 1 }),
        loadOp: "clear" as const,
        storeOp: "store" as const,
        clearValue: { r: 0, g: 0, b: 0, a: 0 },
      })),
    });
    pass.setPipeline(this.pipelines.get(kind)!);
    pass.setBindGroup(0, group);
    if (["splat", "air", "bubble"].includes(kind)) pass.draw(6, instances);
    else pass.draw(3);
    pass.end();
    this.device.queue.submit([enc.finish()]);
  }
  copy(from: FoamTexture<GPUTexture>, to: FoamTexture<GPUTexture>) {
    const enc = this.device.createCommandEncoder();
    enc.copyTextureToTexture({ texture: from.handle }, { texture: to.handle }, [
      from.width,
      from.height,
    ]);
    this.device.queue.submit([enc.finish()]);
  }
  async read(texture: FoamTexture<GPUTexture>) {
    const bpp =
      texture.format === "rgba32float"
        ? 16
        : texture.format === "rgba16float"
          ? 8
          : 4;
    const row = Math.ceil((texture.width * bpp) / 256) * 256;
    const buffer = this.device.createBuffer({
      size: row * texture.height,
      usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
    });
    try {
      const enc = this.device.createCommandEncoder();
      enc.copyTextureToBuffer(
        { texture: texture.handle },
        { buffer, bytesPerRow: row },
        [texture.width, texture.height],
      );
      this.device.queue.submit([enc.finish()]);
      await buffer.mapAsync(GPUMapMode.READ);
      const data = new Float32Array(texture.width * texture.height * 4),
        bytes = new DataView(buffer.getMappedRange());
      for (let y = 0; y < texture.height; y++)
        for (let x = 0; x < texture.width * 4; x++) {
          data[y * texture.width * 4 + x] =
            bpp === 16
              ? bytes.getFloat32(y * row + x * 4, true)
              : bpp === 8
                ? halfToFloat(bytes.getUint16(y * row + x * 2, true))
                : bytes.getUint8(y * row + x) / 255;
        }
      return data;
    } finally {
      if (buffer.mapState === "mapped") buffer.unmap();
      buffer.destroy();
    }
  }
  mipmaps(texture: FoamTexture<GPUTexture>) {
    if (!texture.mipLevels || texture.mipLevels < 2) return;
    const enc = this.device.createCommandEncoder({
      label: "Filter foam density",
    });
    for (let level = 1; level < texture.mipLevels; level++) {
      const group = this.device.createBindGroup({
        layout: this.mipPipeline.getBindGroupLayout(0),
        entries: [
          {
            binding: 0,
            resource: texture.handle.createView({
              baseMipLevel: level - 1,
              mipLevelCount: 1,
            }),
          },
          { binding: 1, resource: this.sampler },
        ],
      });
      const pass = enc.beginRenderPass({
        colorAttachments: [
          {
            view: texture.handle.createView({
              baseMipLevel: level,
              mipLevelCount: 1,
            }),
            loadOp: "clear",
            storeOp: "store",
          },
        ],
      });
      pass.setPipeline(this.mipPipeline);
      pass.setBindGroup(0, group);
      pass.draw(3);
      pass.end();
    }
    this.device.queue.submit([enc.finish()]);
  }
  destroy(texture: FoamTexture<GPUTexture>) {
    texture.handle.destroy();
  }
  dispose() {
    this.params.destroy();
    this.layouts.clear();
    this.pipelines.clear();
  }
}
