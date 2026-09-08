import { getFractureNetwork } from "./fractures";
import { getBuiltinSatmap } from "../engine/satmaps/catalog";
import { paletteRGBA } from "../engine/satmaps/pixels";
import {
  LAB_VIEWS,
  clamp,
  detailEnvelope,
  fieldBounds,
  slopeBound,
  type StoneSettings,
  type LabView,
} from "./model";
import { stoneFragment, stoneVertex } from "./shader";
import type { Point } from "./field";
export interface LabStatus {
  ready: boolean;
  width: number;
  height: number;
  error: string;
  footprint: number;
  draws: number;
}
const normalize = (v: Point): Point => {
  const n = Math.hypot(...v) || 1;
  return v.map((x) => x / n) as Point;
};
const cross = (a: Point, b: Point): Point => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
export class StoneRenderer {
  private gl: WebGL2RenderingContext;
  private program!: WebGLProgram;
  private palette!: WebGLTexture;
  private target!: WebGLTexture;
  private framebuffer!: WebGLFramebuffer;
  private locations = new Map<string, WebGLUniformLocation | null>();
  private observer: ResizeObserver;
  private abort = new AbortController();
  private frame = 0;
  private fence: WebGLSync | null = null;
  private submitted = 0;
  private dirty = true;
  private disposed = false;
  private lost = false;
  private paletteKey = "";
  private crackKey = "";
  private fractureSegments = 0;
  private width = 0;
  private height = 0;
  private yaw = 0.65;
  private pitch = 0.34;
  private distance = 1.1;
  private down: { x: number; y: number; id: number } | null = null;
  private smooth = false;
  private time = 0;
  private draws = 0;
  private footprint = 0;
  constructor(
    readonly canvas: HTMLCanvasElement,
    private settings: StoneSettings,
    private status: (s: LabStatus) => void,
  ) {
    const gl = canvas.getContext("webgl2", {
      alpha: false,
      antialias: false,
      preserveDrawingBuffer: true,
      powerPreference: "high-performance",
    });
    if (!gl)
      throw new Error(
        "WebGL2 is unavailable. Enable browser graphics acceleration to use the SDF material editor.",
      );
    this.gl = gl;
    this.initializeGPU();
    const signal = this.abort.signal;
    canvas.addEventListener(
      "webglcontextlost",
      (e) => {
        e.preventDefault();
        this.lost = true;
        this.report(
          "Graphics context lost. Reload the material editor to reconnect.",
        );
      },
      { signal },
    );
    canvas.addEventListener(
      "webglcontextrestored",
      () => {
        try {
          this.lost = false;
          this.initializeGPU();
          this.request();
        } catch (error) {
          this.report(String(error));
        }
      },
      { signal },
    );
    canvas.addEventListener(
      "pointerdown",
      (e) => {
        if (e.button > 2) return;
        canvas.focus();
        canvas.setPointerCapture(e.pointerId);
        this.down = { x: e.clientX, y: e.clientY, id: e.pointerId };
      },
      { signal },
    );
    canvas.addEventListener(
      "pointermove",
      (e) => {
        if (!this.down || e.pointerId !== this.down.id) return;
        this.yaw -= (e.clientX - this.down.x) * 0.006;
        this.pitch = clamp(
          this.pitch + (e.clientY - this.down.y) * 0.005,
          0.06,
          1.32,
        );
        this.down = { x: e.clientX, y: e.clientY, id: e.pointerId };
        this.request();
      },
      { signal },
    );
    const release = () => {
      this.down = null;
    };
    canvas.addEventListener("pointerup", release, { signal });
    canvas.addEventListener("pointercancel", release, { signal });
    canvas.addEventListener("lostpointercapture", release, { signal });
    canvas.addEventListener("contextmenu", (e) => e.preventDefault(), {
      signal,
    });
    canvas.addEventListener(
      "wheel",
      (e) => {
        e.preventDefault();
        this.distance = clamp(
          this.distance * Math.exp(e.deltaY * 0.0011),
          0.46,
          3,
        );
        this.request();
      },
      { signal, passive: false },
    );
    canvas.addEventListener(
      "keydown",
      (e) => {
        if (e.key.toLowerCase() === "f") {
          e.preventDefault();
          this.resetCamera();
        }
        if (e.key.toLowerCase() === "c") {
          this.smooth = true;
          this.request();
        }
      },
      { signal },
    );
    window.addEventListener(
      "keyup",
      (e) => {
        if (e.key.toLowerCase() === "c") {
          this.smooth = false;
          this.request();
        }
      },
      { signal },
    );
    window.addEventListener(
      "blur",
      () => {
        this.smooth = false;
        this.down = null;
        this.request();
      },
      { signal },
    );
    document.addEventListener(
      "visibilitychange",
      () => {
        this.time = 0;
        if (!document.hidden) this.request();
      },
      { signal },
    );
    this.observer = new ResizeObserver(() => this.request());
    this.observer.observe(canvas);
    this.request();
  }
  private initializeGPU() {
    const gl = this.gl;
    const compile = (type: number, code: string) => {
      const s = gl.createShader(type)!;
      gl.shaderSource(s, code);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        const error = gl.getShaderInfoLog(s);
        gl.deleteShader(s);
        throw new Error(error || "Stone shader compilation failed.");
      }
      return s;
    };
    const vs = compile(gl.VERTEX_SHADER, stoneVertex);
    let fs: WebGLShader;
    try {
      fs = compile(gl.FRAGMENT_SHADER, stoneFragment);
    } catch (error) {
      gl.deleteShader(vs);
      throw error;
    }
    this.program = gl.createProgram()!;
    gl.attachShader(this.program, vs);
    gl.attachShader(this.program, fs);
    gl.linkProgram(this.program);
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    if (!gl.getProgramParameter(this.program, gl.LINK_STATUS))
      throw new Error(
        gl.getProgramInfoLog(this.program) || "Stone shader linking failed.",
      );
    this.locations.clear();
    for (const name of [
      "uEye",
      "uRight",
      "uUp",
      "uForward",
      "uShape",
      "uMeso",
      "uFracture",
      "uMicro",
      "uMaterial",
      "uLight",
      "uDisplay",
      "uBounds",
      "uProbe",
      "uStructure",
      "uFieldBounds",
      "uCrackA[0]",
      "uCrackB[0]",
      "uCrackN[0]",
      "uCrackCount",
    ])
      this.locations.set(name, gl.getUniformLocation(this.program, name));
    this.palette = gl.createTexture()!;
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.palette);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      gl.SRGB8_ALPHA8,
      256,
      1,
      0,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      null,
    );
    gl.useProgram(this.program);
    gl.uniform1i(gl.getUniformLocation(this.program, "uPalette"), 0);
    this.target = gl.createTexture()!;
    this.framebuffer = gl.createFramebuffer()!;
    this.width = this.height = 0;
    this.paletteKey = "";
    this.crackKey = "";
    this.fence = null;
    this.dirty = true;
  }
  update(settings: StoneSettings) {
    this.settings = settings;
    this.request();
  }
  zoomBy(factor: number) {
    this.distance = clamp(this.distance * factor, 0.46, 3);
    this.request();
  }
  resetCamera() {
    this.yaw = 0.65;
    this.pitch = 0.34;
    this.distance = 1.1;
    this.request();
  }
  setCamera(yaw: number, pitch: number, distance: number) {
    this.yaw = yaw;
    this.pitch = clamp(pitch, 0.06, 1.32);
    this.distance = clamp(distance, 0.46, 3);
    this.request();
  }
  private request() {
    this.dirty = true;
    if (!this.frame && !this.disposed && !this.lost)
      this.frame = requestAnimationFrame(this.tick);
  }
  private report(error = "") {
    this.status({
      ready: !error,
      width: this.width,
      height: this.height,
      error,
      footprint: this.footprint,
      draws: this.draws,
    });
  }
  private tick = (now: number) => {
    this.frame = 0;
    if (this.disposed || this.lost || document.hidden) return;
    const gl = this.gl;
    if (this.fence) {
      const result = gl.clientWaitSync(this.fence, 0, 0);
      if (result === gl.TIMEOUT_EXPIRED) {
        if (now - this.submitted > 30000) {
          this.report("The SDF draw timed out. Reload and use Draft quality.");
          return;
        }
        this.frame = requestAnimationFrame(this.tick);
        return;
      }
      gl.deleteSync(this.fence);
      this.fence = null;
      if (result === gl.WAIT_FAILED) {
        this.report(
          "Graphics synchronization failed. Reload the material editor.",
        );
        return;
      }
      this.report();
    }
    if (this.settings.turntable) {
      if (this.time)
        this.yaw += Math.min((now - this.time) / 1000, 0.05) * 0.18;
      this.dirty = true;
    }
    this.time = now;
    if (this.dirty) {
      try {
        this.draw(this.settings, true);
        this.dirty = false;
        this.fence = gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0);
        gl.flush();
        this.submitted = now;
      } catch (error) {
        this.report(error instanceof Error ? error.message : String(error));
        return;
      }
    }
    if (this.fence || this.settings.turntable)
      this.frame = requestAnimationFrame(this.tick);
  };
  private dimensions(s: StoneSettings) {
    const w = Math.max(1, this.canvas.clientWidth),
      h = Math.max(1, this.canvas.clientHeight);
    const cap = { draft: 320, balanced: 600, closeup: 900 }[s.quality];
    const scale = Math.min(devicePixelRatio || 1, 2, cap / Math.sqrt(w * h));
    return [
      Math.max(1, Math.round(w * scale)),
      Math.max(1, Math.round(h * scale)),
    ];
  }
  private bindTarget(width: number, height: number) {
    const gl = this.gl;
    if (this.width !== width || this.height !== height) {
      gl.activeTexture(gl.TEXTURE1);
      gl.bindTexture(gl.TEXTURE_2D, this.target);
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
        this.target,
        0,
      );
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE)
        throw new Error("Could not allocate the material preview.");
      this.width = width;
      this.height = height;
    } else gl.bindFramebuffer(gl.FRAMEBUFFER, this.framebuffer);
    gl.viewport(0, 0, width, height);
  }
  private uniforms(
    s: StoneSettings,
    width: number,
    height: number,
    forceDetail?: boolean,
  ) {
    const gl = this.gl;
    const set = (name: string, v: number[]) =>
      gl.uniform4fv(this.locations.get(name)!, v);
    const eye: Point = [
      Math.sin(this.yaw) * Math.cos(this.pitch) * this.distance,
      Math.sin(this.pitch) * this.distance,
      Math.cos(this.yaw) * Math.cos(this.pitch) * this.distance,
    ];
    const forward = normalize(eye.map((v) => -v) as Point),
      right = normalize(cross(forward, [0, 1, 0])),
      up = cross(right, forward),
      fov = 0.36;
    this.footprint = (2 * Math.max(0.04, this.distance - 0.29) * fov) / height;
    const baseBound = 1 + s.form * 5.2 * (0.016 * 4 + 0.007 * 11),
      detail = (forceDetail ?? s.detail) && !this.smooth;
    const effective = { ...s, detail };
    set("uEye", [...eye, this.footprint]);
    set("uRight", [...right, fov]);
    set("uUp", [...up, 0]);
    set("uForward", [...forward, width / height]);
    set("uShape", [s.seed, s.form, s.facets, detail ? 1 : 0]);
    set("uMeso", [
      s.chips * 0.001,
      s.bedding * 0.001,
      s.spacing * 0.001,
      (s.tilt * Math.PI) / 180,
    ]);
    set("uFracture", [
      s.crackDepth * 0.001,
      s.crackWidth * 0.001,
      s.crackSpacing * 0.001,
      s.porosity,
    ]);
    set("uMicro", [
      s.poreSize * 0.001,
      s.grain * 0.001,
      s.grainSize * 0.001,
      0,
    ]);
    set("uStructure", [
      s.layerBreakup,
      s.crackBranching,
      s.crackChipping,
      s.poreIrregularity,
    ]);
    const bounds = fieldBounds(effective, this.footprint);
    set("uFieldBounds", [bounds.substrate, bounds.fracture, bounds.pore, 0]);
    const network = s.crackDepth > 0 ? getFractureNetwork(s) : null,
      key = network?.key ?? "off";
    if (key !== this.crackKey) {
      this.crackKey = key;
      this.fractureSegments = network?.segments.length ?? 0;
      gl.uniform1i(this.locations.get("uCrackCount")!, this.fractureSegments);
      if (network) {
        gl.uniform4fv(this.locations.get("uCrackA[0]")!, network.a);
        gl.uniform4fv(this.locations.get("uCrackB[0]")!, network.b);
        gl.uniform4fv(this.locations.get("uCrackN[0]")!, network.normals);
      }
    }
    set("uMaterial", [s.roughness, s.contrast, s.bias, s.saturation]);
    set("uLight", [
      (s.sunAzimuth * Math.PI) / 180,
      (s.sunElevation * Math.PI) / 180,
      s.exposure,
      { draft: 300, balanced: 420, closeup: 600 }[s.quality],
    ]);
    set("uDisplay", [
      width,
      height,
      LAB_VIEWS.indexOf(s.view),
      s.compare ? s.split : -1,
    ]);
    set("uBounds", [
      detailEnvelope(effective, this.footprint),
      baseBound,
      slopeBound(effective, this.footprint),
      clamp(this.footprint * 0.1, 0.00002, 0.00015),
    ]);
    set("uProbe", [0, 0, 0, 0]);
  }
  private draw(s: StoneSettings, present: boolean) {
    if (this.disposed || this.lost || this.gl.isContextLost())
      throw new Error("The graphics context is unavailable.");
    const gl = this.gl,
      [width, height] = this.dimensions(s);
    this.bindTarget(width, height);
    gl.useProgram(this.program);
    gl.disable(gl.BLEND);
    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.CULL_FACE);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.palette);
    const asset = getBuiltinSatmap(s.palette)!;
    if (asset.id !== this.paletteKey) {
      gl.texSubImage2D(
        gl.TEXTURE_2D,
        0,
        0,
        0,
        256,
        1,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        paletteRGBA(asset.palette),
      );
      this.paletteKey = asset.id;
    }
    this.uniforms(s, width, height);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    if (present) {
      if (this.canvas.width !== width) this.canvas.width = width;
      if (this.canvas.height !== height) this.canvas.height = height;
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.framebuffer);
      gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
      gl.blitFramebuffer(
        0,
        0,
        width,
        height,
        0,
        0,
        width,
        height,
        gl.COLOR_BUFFER_BIT,
        gl.NEAREST,
      );
    }
    this.draws++;
  }
  async capture(
    view: LabView = this.settings.view,
    detail = this.settings.detail,
  ): Promise<Blob> {
    this.draw({ ...this.settings, view, detail }, false);
    const gl = this.gl,
      pixels = new Uint8Array(this.width * this.height * 4);
    gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.framebuffer);
    gl.readPixels(
      0,
      0,
      this.width,
      this.height,
      gl.RGBA,
      gl.UNSIGNED_BYTE,
      pixels,
    );
    const flipped = new Uint8ClampedArray(pixels.length),
      row = this.width * 4;
    for (let y = 0; y < this.height; y++)
      flipped.set(
        pixels.subarray(y * row, (y + 1) * row),
        (this.height - 1 - y) * row,
      );
    const canvas = document.createElement("canvas");
    canvas.width = this.width;
    canvas.height = this.height;
    canvas
      .getContext("2d")!
      .putImageData(new ImageData(flipped, this.width, this.height), 0, 0);
    const blob = await new Promise<Blob | null>((resolve) =>
      canvas.toBlob(resolve, "image/png"),
    );
    this.request();
    if (!blob) throw new Error("Could not encode the preview PNG.");
    return blob;
  }
  /** Explicit development probe. Production never publishes this renderer globally. */
  probe(points: Point[], detail = true): number[] {
    const gl = this.gl,
      [width, height] = this.dimensions(this.settings);
    this.bindTarget(width, height);
    gl.useProgram(this.program);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.palette);
    this.uniforms(this.settings, width, height, detail);
    gl.viewport(0, 0, 1, 1);
    const bytes = new Uint8Array(4),
      values: number[] = [];
    for (const p of points) {
      gl.uniform4fv(this.locations.get("uProbe")!, [...p, 1]);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, bytes);
      values.push(
        ((bytes[0] * 65536 + bytes[1] * 256 + bytes[2]) / 16777215) * 2 - 1,
      );
    }
    this.request();
    return values;
  }
  getDiagnostics() {
    return {
      backend: "WebGL2",
      width: this.width,
      height: this.height,
      footprint: this.footprint,
      draws: this.draws,
      materialTextures: 1,
      fractureSegments: this.fractureSegments,
      structure:
        "irregular sheets / connected finite fractures / organic vesicles",
      texturePurpose: "256×1 color CLUT only",
      detailSource: "analytic 3D signed field",
      glError: this.gl.getError(),
      contextLost: this.gl.isContextLost(),
    };
  }
  dispose() {
    this.disposed = true;
    cancelAnimationFrame(this.frame);
    this.abort.abort();
    this.observer.disconnect();
    if (this.fence) this.gl.deleteSync(this.fence);
    this.gl.deleteTexture(this.palette);
    this.gl.deleteTexture(this.target);
    this.gl.deleteFramebuffer(this.framebuffer);
    this.gl.deleteProgram(this.program);
  }
}
