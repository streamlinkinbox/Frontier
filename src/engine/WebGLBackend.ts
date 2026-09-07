import { glFragment, glVertex } from "./shaders";
import { generateField, pickField, sculptField } from "./field";
import { computeFlux, evolveField, redistanceField } from "./simulation";
import { packUniforms } from "./uniforms";
import { hasVisibleFrame } from "./startup";
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
  private data!: Float32Array;
  private original!: Float32Array;
  private scratch!: Float32Array;
  private flux!: Float32Array;
  private relaxationCycle = 0;
  private brushCycle = 0;
  private destroyed = false;
  private fence: WebGLSync | null = null;
  onLost?: (message: string) => void;
  private contextLost = (event: Event) => {
    event.preventDefault();
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
    const compile = (type: number, source: string) => {
      const shader = gl.createShader(type)!;
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
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
    this.flux = new Float32Array((this.data.length / 4) * 3);
    this.texture = this.createTexture(this.data);
    this.originalTexture = this.createTexture(this.original);
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
  render(frame: FrameState) {
    const gl = this.gl;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.useProgram(this.program);
    gl.bindBuffer(gl.UNIFORM_BUFFER, this.uniform);
    gl.bufferSubData(gl.UNIFORM_BUFFER, 0, packUniforms(frame, this.size));
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(
      gl.TEXTURE_3D,
      frame.compare ? this.originalTexture : this.texture,
    );
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    if (this.fence) gl.deleteSync(this.fence);
    this.fence = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0);
    gl.flush();
  }
  async verifyFrame(frame: FrameState): Promise<boolean> {
    this.render(frame);
    const gl = this.gl;
    if (gl.isContextLost())
      throw new Error("WebGL graphics context was lost during startup.");
    const row = new Uint8Array(frame.width * 4);
    const pixels = new Uint8Array(32 * 32 * 4);
    for (let y = 0; y < 32; y++) {
      const sy = Math.min(
        frame.height - 1,
        Math.floor(((y + 0.5) * frame.height) / 32),
      );
      gl.readPixels(0, sy, frame.width, 1, gl.RGBA, gl.UNSIGNED_BYTE, row);
      for (let x = 0; x < 32; x++) {
        const sx = Math.min(
          frame.width - 1,
          Math.floor(((x + 0.5) * frame.width) / 32),
        );
        pixels.set(row.subarray(sx * 4, sx * 4 + 4), (y * 32 + x) * 4);
      }
    }
    return hasVisibleFrame(pixels);
  }
  step(s: Settings, count: number) {
    for (let i = 0; i < count; i++) {
      computeFlux(this.data, this.size, this.flux);
      evolveField(this.data, this.size, s, this.flux, this.scratch);
      const old = this.data;
      this.data = this.scratch;
      this.scratch = old;
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
    this.destroyed = true;
    this.canvas.removeEventListener("webglcontextlost", this.contextLost);
    const gl = this.gl;
    if (!gl) return;
    if (this.fence) gl.deleteSync(this.fence);
    this.fence = null;
    gl.deleteTexture(this.texture);
    gl.deleteTexture(this.originalTexture);
    gl.deleteBuffer(this.uniform);
    gl.deleteProgram(this.program);
    gl.getExtension("WEBGL_lose_context")?.loseContext();
  }
}
