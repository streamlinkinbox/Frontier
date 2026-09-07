import { glFragment, glVertex } from "./shaders";
import { generateField, pickField, sculptField } from "./field";
import {
  computeFlux,
  evolveField,
  redistanceField,
  computeTalusFlux,
  settleTalus,
} from "./simulation";
import { packUniforms } from "./uniforms";
import { diagnostics, summarizePixels } from "../diagnostics";
import { encodeFramePNG } from "./presentation";
import {
  CPU_SIZE,
  type Backend,
  type Settings,
  type FrameState,
  type Vec3,
  type Tool,
  type VolumeSize,
} from "./types";

export class WebGLBackend implements Backend {
  readonly name = "WebGL2" as const;
  readonly size: VolumeSize = { ...CPU_SIZE };
  private gl!: WebGL2RenderingContext;
  private program!: WebGLProgram;
  private uniform!: WebGLBuffer;
  private texture!: WebGLTexture;
  private originalTexture!: WebGLTexture;
  private framebuffer: WebGLFramebuffer | null = null;
  private colorTarget: WebGLTexture | null = null;
  private targetWidth = 0;
  private targetHeight = 0;
  private data!: Float32Array;
  private original!: Float32Array;
  private scratch!: Float32Array;
  private flux!: Float32Array;
  private relaxationCycle = 0;
  private brushCycle = 0;
  private destroyed = false;
  private fence: WebGLSync | null = null;
  private drawCount = 0;
  private lastProbe: unknown = null;
  private lastOffscreenProbe: unknown = null;
  private rendererInfo: unknown = null;
  onLost?: (message: string) => void;
  private contextLost = (event: Event) => {
    event.preventDefault();
    diagnostics.log(
      "WebGL2",
      "Context lost",
      { expectedDisposal: this.destroyed },
      this.destroyed ? "info" : "error",
    );
    if (!this.destroyed)
      this.onLost?.(
        "The WebGL graphics context was lost. Restart the renderer to reconnect.",
      );
  };
  constructor(private canvas: HTMLCanvasElement) {}
  async initialize(settings: Settings): Promise<void> {
    if (this.destroyed)
      throw new DOMException("WebGL startup was cancelled", "AbortError");
    this.canvas.addEventListener("webglcontextlost", this.contextLost);
    diagnostics.log("WebGL2", "Requesting compatibility graphics context");
    const gl = this.canvas.getContext("webgl2", {
      alpha: false,
      antialias: false,
      preserveDrawingBuffer: true,
      powerPreference: "high-performance",
    });
    if (!gl)
      throw new Error(
        "Neither WebGPU nor WebGL2 is available. Enable graphics acceleration in your browser.",
      );
    this.gl = gl;
    const debug = gl.getExtension("WEBGL_debug_renderer_info");
    this.rendererInfo = {
      vendor: gl.getParameter(gl.VENDOR),
      renderer: gl.getParameter(gl.RENDERER),
      unmaskedVendor: debug
        ? gl.getParameter(debug.UNMASKED_VENDOR_WEBGL)
        : "not exposed",
      unmaskedRenderer: debug
        ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL)
        : "not exposed",
      version: gl.getParameter(gl.VERSION),
      shadingLanguage: gl.getParameter(gl.SHADING_LANGUAGE_VERSION),
      maxTextureSize: gl.getParameter(gl.MAX_TEXTURE_SIZE),
      max3DTextureSize: gl.getParameter(gl.MAX_3D_TEXTURE_SIZE),
      attributes: gl.getContextAttributes(),
    };
    diagnostics.log("WebGL2", "Context created", this.rendererInfo);
    const compile = (type: number, source: string) => {
      const shader = gl.createShader(type)!;
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      const compiled = gl.getShaderParameter(shader, gl.COMPILE_STATUS);
      diagnostics.log(
        "GLSL",
        type === gl.VERTEX_SHADER
          ? "Vertex shader compiled"
          : "Fragment shader compiled",
        {
          compiled,
          info: gl.getShaderInfoLog(shader),
        },
        compiled ? "info" : "error",
      );
      if (!compiled) {
        const error = gl.getShaderInfoLog(shader);
        gl.deleteShader(shader);
        throw new Error(error || "Shader compilation failed");
      }
      return shader;
    };
    const vs = compile(gl.VERTEX_SHADER, glVertex),
      fs = compile(gl.FRAGMENT_SHADER, glFragment);
    this.program = gl.createProgram()!;
    gl.attachShader(this.program, vs);
    gl.attachShader(this.program, fs);
    gl.linkProgram(this.program);
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    diagnostics.log("WebGL2", "Program link result", {
      linked: gl.getProgramParameter(this.program, gl.LINK_STATUS),
      info: gl.getProgramInfoLog(this.program),
    });
    if (!gl.getProgramParameter(this.program, gl.LINK_STATUS))
      throw new Error(
        gl.getProgramInfoLog(this.program) || "Shader link failed",
      );
    gl.useProgram(this.program);
    gl.uniform1i(gl.getUniformLocation(this.program, "field"), 0);
    this.uniform = gl.createBuffer()!;
    gl.bindBuffer(gl.UNIFORM_BUFFER, this.uniform);
    gl.bufferData(gl.UNIFORM_BUFFER, 224, gl.DYNAMIC_DRAW);
    gl.bindBufferBase(gl.UNIFORM_BUFFER, 0, this.uniform);
    gl.uniformBlockBinding(
      this.program,
      gl.getUniformBlockIndex(this.program, "Params"),
      0,
    );
    this.data = generateField(this.size, settings);
    this.original = this.data.slice();
    this.scratch = new Float32Array(this.data.length);
    this.flux = new Float32Array(this.data.length);
    this.texture = this.createTexture(this.data);
    this.originalTexture = this.createTexture(this.original);
    diagnostics.log("WebGL2", "Initial 3D volume uploaded", {
      size: this.size,
      seed: settings.seed,
      preset: settings.preset,
    });
  }
  getDiagnostics(): Record<string, unknown> {
    return {
      backend: this.name,
      destroyed: this.destroyed,
      contextLost: this.gl?.isContextLost(),
      rendererInfo: this.rendererInfo,
      drawCount: this.drawCount,
      frameFencePending: !!this.fence,
      renderTargetSize: [this.targetWidth, this.targetHeight],
      volumeSize: this.size,
      lastPixelProbe: this.lastProbe,
      lastOffscreenProbe: this.lastOffscreenProbe,
    };
  }
  async regenerate(settings: Settings) {
    this.relaxationCycle = 0;
    this.brushCycle = 0;
    this.data = generateField(this.size, settings);
    this.original = this.data.slice();
    this.upload();
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_3D, this.originalTexture);
    gl.texSubImage3D(
      gl.TEXTURE_3D,
      0,
      0,
      0,
      0,
      this.size.x,
      this.size.y,
      this.size.z,
      gl.RGBA,
      gl.FLOAT,
      this.original,
    );
  }
  ready() {
    if (this.destroyed || this.gl.isContextLost()) return false;
    if (!this.fence) return true;
    const state = this.gl.clientWaitSync(this.fence, 0, 0);
    if (state === this.gl.TIMEOUT_EXPIRED) return false;
    if (state === this.gl.WAIT_FAILED) {
      this.onLost?.(
        "WebGL could not complete the terrain frame. Restart the renderer.",
      );
      return false;
    }
    this.gl.deleteSync(this.fence);
    this.fence = null;
    return true;
  }
  async sync() {
    if (this.destroyed || this.gl.isContextLost())
      throw new Error("WebGL context is unavailable.");
    this.gl.finish();
  }
  private createTexture(data: Float32Array): WebGLTexture {
    const gl = this.gl,
      t = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_3D, t);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_3D, gl.TEXTURE_WRAP_R, gl.CLAMP_TO_EDGE);
    gl.texImage3D(
      gl.TEXTURE_3D,
      0,
      gl.RGBA16F,
      this.size.x,
      this.size.y,
      this.size.z,
      0,
      gl.RGBA,
      gl.FLOAT,
      data,
    );
    return t;
  }
  private upload() {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_3D, this.texture);
    gl.texSubImage3D(
      gl.TEXTURE_3D,
      0,
      0,
      0,
      0,
      this.size.x,
      this.size.y,
      this.size.z,
      gl.RGBA,
      gl.FLOAT,
      this.data,
    );
  }
  private bindRenderTarget(width: number, height: number) {
    const gl = this.gl;
    if (this.targetWidth !== width || this.targetHeight !== height) {
      gl.deleteTexture(this.colorTarget);
      gl.deleteFramebuffer(this.framebuffer);
      this.colorTarget = gl.createTexture();
      this.framebuffer = gl.createFramebuffer();
      gl.bindTexture(gl.TEXTURE_2D, this.colorTarget);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA8,
        width,
        height,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        null,
      );
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.framebuffer);
      gl.framebufferTexture2D(
        gl.FRAMEBUFFER,
        gl.COLOR_ATTACHMENT0,
        gl.TEXTURE_2D,
        this.colorTarget,
        0,
      );
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE)
        throw new Error(
          "Could not allocate the terrain render target. Try a smaller browser window.",
        );
      this.targetWidth = width;
      this.targetHeight = height;
    } else gl.bindFramebuffer(gl.FRAMEBUFFER, this.framebuffer);
  }
  render(frame: FrameState) {
    this.drawFrame(frame);
  }
  private drawFrame(frame: FrameState, present = true) {
    const gl = this.gl;
    if (this.destroyed || gl.isContextLost())
      throw new Error("WebGL context is unavailable.");
    this.bindRenderTarget(frame.width, frame.height);
    gl.viewport(0, 0, frame.width, frame.height);
    gl.useProgram(this.program);
    gl.bindBuffer(gl.UNIFORM_BUFFER, this.uniform);
    gl.bufferSubData(gl.UNIFORM_BUFFER, 0, packUniforms(frame, this.size));
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(
      gl.TEXTURE_3D,
      frame.compare ? this.originalTexture : this.texture,
    );
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    if (present) {
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.framebuffer);
      gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
      gl.blitFramebuffer(
        0,
        0,
        frame.width,
        frame.height,
        0,
        0,
        this.canvas.width,
        this.canvas.height,
        gl.COLOR_BUFFER_BIT,
        gl.LINEAR,
      );
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    if (this.fence) gl.deleteSync(this.fence);
    this.fence = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0);
    gl.flush();
    this.drawCount++;
    if (this.drawCount === 1)
      diagnostics.log("WebGL2", "First terrain draw submitted", {
        renderSize: [frame.width, frame.height],
        displaySize: [this.canvas.width, this.canvas.height],
      });
  }
  async verifyFrame(
    frame: FrameState,
    target: "presentation" | "offscreen" = "presentation",
  ): Promise<boolean> {
    this.drawFrame(frame, target === "presentation");
    const gl = this.gl;
    const width = target === "presentation" ? this.canvas.width : frame.width;
    const height =
      target === "presentation" ? this.canvas.height : frame.height;
    gl.bindFramebuffer(
      gl.READ_FRAMEBUFFER,
      target === "presentation" ? null : this.framebuffer,
    );
    if (gl.isContextLost())
      throw new Error("WebGL graphics context was lost during startup.");
    const row = new Uint8Array(width * 4);
    const pixels = new Uint8Array(32 * 32 * 4);
    for (let y = 0; y < 32; y++) {
      const sy = Math.min(height - 1, Math.floor(((y + 0.5) * height) / 32));
      gl.readPixels(0, sy, width, 1, gl.RGBA, gl.UNSIGNED_BYTE, row);
      for (let x = 0; x < 32; x++) {
        const sx = Math.min(width - 1, Math.floor(((x + 0.5) * width) / 32));
        pixels.set(row.subarray(sx * 4, sx * 4 + 4), (y * 32 + x) * 4);
      }
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const summary = summarizePixels(pixels);
    const probe = {
      at: new Date().toISOString(),
      source:
        target === "presentation"
          ? "WebGL default framebuffer (not a screen capture)"
          : "Owned WebGL framebuffer (not a screen capture)",
      glError: gl.getError(),
      ...summary,
    };
    if (target === "presentation") this.lastProbe = probe;
    else this.lastOffscreenProbe = probe;
    diagnostics.log(
      "WebGL2",
      target === "presentation"
        ? "Presentation framebuffer pixel check"
        : "Owned render target pixel check",
      probe,
      summary.nonUniformOpaque ? "info" : "warn",
    );
    return summary.nonUniformOpaque;
  }
  async capture(frame: FrameState): Promise<Blob> {
    this.drawFrame(frame, false);
    const gl = this.gl;
    const pixels = new Uint8Array(frame.width * frame.height * 4);
    try {
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.framebuffer);
      gl.readPixels(
        0,
        0,
        frame.width,
        frame.height,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        pixels,
      );
      if (gl.isContextLost())
        throw new Error("WebGL context was lost during capture.");
      return await encodeFramePNG(pixels, frame.width, frame.height, {
        flipY: true,
      });
    } finally {
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    }
  }
  step(s: Settings, count: number) {
    for (let i = 0; i < count; i++) {
      computeFlux(this.data, this.size, this.flux);
      evolveField(this.data, this.size, s, this.flux, this.scratch);
      const old = this.data;
      this.data = this.scratch;
      this.scratch = old;
      if (s.thermal > 0) {
        computeTalusFlux(this.data, this.size, s, this.flux);
        settleTalus(this.data, this.size, this.flux, this.scratch);
        [this.data, this.scratch] = [this.scratch, this.data];
      }
      if (++this.relaxationCycle % 2 === 0 && (s.thermal > 0 || s.erosion > 0))
        this.redistance();
    }
    this.upload();
  }
  sculpt(center: Vec3, tool: Tool, s: Settings) {
    sculptField(this.data, this.size, center, tool, s);
    if (++this.brushCycle % 4 === 0) this.redistance();
    this.upload();
  }
  private redistance() {
    redistanceField(this.data, this.size, this.scratch);
    [this.data, this.scratch] = [this.scratch, this.data];
  }
  async pick(frame: FrameState, x: number, y: number) {
    return pickField(
      frame.compare ? this.original : this.data,
      this.size,
      frame,
      x,
      y,
    );
  }
  async readVolume() {
    return this.data.slice();
  }
  writeVolume(data: Float32Array) {
    if (data.length !== this.data.length)
      throw new Error("Volume dimensions do not match.");
    this.data.set(data);
    this.upload();
  }
  reset() {
    this.relaxationCycle = 0;
    this.brushCycle = 0;
    this.data.set(this.original);
    this.upload();
  }
  dispose() {
    diagnostics.log("WebGL2", "Backend disposed");
    this.destroyed = true;
    this.canvas.removeEventListener("webglcontextlost", this.contextLost);
    const gl = this.gl;
    if (!gl) return;
    if (this.fence) gl.deleteSync(this.fence);
    this.fence = null;
    gl.deleteTexture(this.colorTarget);
    gl.deleteFramebuffer(this.framebuffer);
    gl.deleteTexture(this.texture);
    gl.deleteTexture(this.originalTexture);
    gl.deleteBuffer(this.uniform);
    gl.deleteProgram(this.program);
    gl.getExtension("WEBGL_lose_context")?.loseContext();
  }
}
