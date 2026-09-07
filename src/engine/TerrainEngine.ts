import { WebGPUBackend } from "./WebGPUBackend";
import { WebGLBackend } from "./WebGLBackend";
import { clamp } from "./math";
import { EditorCamera, FLY_KEYS, type NavigationMode } from "./EditorCamera";
import { sampleField } from "./field";
import { withTimeout } from "./startup";
import {
  BOUNDS_MIN,
  WORLD_SIZE,
  type Backend,
  type Settings,
  type FrameState,
  type Tool,
  type Vec3,
  type EngineStats,
  type VolumeSize,
} from "./types";

export interface EngineCallbacks {
  stats: (stats: EngineStats) => void;
  changed: () => void;
  history: (undo: number, redo: number) => void;
  notice: (message: string) => void;
  error: (message: string) => void;
  ready: (backend: string) => void;
  status?: (message: string) => void;
}
// Only the current engine may mutate a viewport, including after async startup.
const canvasOwners = new WeakMap<HTMLElement, TerrainEngine>();

export class TerrainEngine {
  backend!: Backend;
  canvas: HTMLCanvasElement;
  settings: Settings;
  tool: Tool = "orbit";
  running = false;
  compare = false;
  steps = 0;
  busy = false;
  private stopped = false;
  private raf = 0;
  private time = 0;
  private then = performance.now();
  private fpsStart = performance.now();
  private frameCount = 0;
  private fps = 0;
  private pendingSteps = 0;
  private frame!: FrameState;
  private brush: Vec3 | null = null;
  private picking = false;
  private pointer: [number, number] = [0, 0];
  private mode: "orbit" | "pan" | "sculpt" | "look" | null = null;
  private down = false;
  private pointerInside = false;
  private last: [number, number] = [0, 0];
  readonly camera = new EditorCamera();
  private resolution = 0.9;
  private resizeRequested = true;
  private quality: "adaptive" | "native" = "adaptive";
  private lastResolutionChange = 0;
  private lowFpsWindows = 0;
  private highFpsWindows = 0;
  private undoStack: { data: Float32Array; steps: number }[] = [];
  private redoStack: { data: Float32Array; steps: number }[] = [];
  private resizeObserver: ResizeObserver;
  private cleanups: (() => void)[] = [];
  private lastSimulation = 0;
  private lastPick = 0;
  private initialized = false;
  private previewSnapshotPending = false;
  constructor(
    private container: HTMLElement,
    settings: Settings,
    private callbacks: EngineCallbacks,
  ) {
    this.settings = { ...settings };
    canvasOwners.get(container)?.dispose();
    canvasOwners.set(container, this);
    this.canvas = this.makeCanvas();
    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(container);
    this.events();
  }
  private get isCurrent() {
    return !this.stopped && canvasOwners.get(this.container) === this;
  }
  private makeCanvas() {
    if (!this.isCurrent)
      throw new DOMException("Renderer startup was superseded", "AbortError");
    const c = document.createElement("canvas");
    c.setAttribute(
      "aria-label",
      "Interactive 3D terrain. Right-drag to look, WASD to fly, Q/E down/up, Shift to boost. Left-drag orbits or sculpts. F frames the terrain.",
    );
    c.setAttribute("role", "img");
    c.tabIndex = 0;
    c.dataset.frontierCanvas = "active";
    // This div is dedicated to the engine. Never stack a replacement canvas
    // below a disposed WebGPU/WebGL canvas, including across hot reloads.
    this.container.replaceChildren(c);
    return c;
  }
  async initialize() {
    const forceGL =
      new URLSearchParams(location.search).get("renderer") === "webgl";
    let fallbackReason = "";
    if (!forceGL) {
      const gpu = new WebGPUBackend(this.canvas);
      this.backend = gpu;
      gpu.onLost = (m) => this.graphicsLost(m);
      this.callbacks.status?.("Initializing the WebGPU terrain volume…");
      try {
        await withTimeout(
          gpu.initialize(this.settings),
          20_000,
          "WebGPU startup timed out. Trying compatibility rendering.",
        );
        if (!this.isCurrent) {
          gpu.dispose();
          return;
        }
        await this.verifyFirstFrame(gpu);
      } catch (error) {
        gpu.dispose();
        // A disposed initializer must never append a fallback canvas later.
        if (!this.isCurrent) return;
        fallbackReason = error instanceof Error ? error.message : String(error);
        console.warn(
          "WebGPU startup failed; switching to the 3D compatibility renderer.",
          error,
        );
      }
    }
    if (forceGL || fallbackReason) {
      if (!this.isCurrent) return;
      this.canvas.remove();
      this.canvas = this.makeCanvas();
      const gl = new WebGLBackend(this.canvas);
      this.backend = gl;
      gl.onLost = (m) => this.graphicsLost(m);
      this.resolution = 0.7;
      this.callbacks.status?.("Starting the 3D compatibility renderer…");
      await gl.initialize(this.settings);
      if (!this.isCurrent) {
        gl.dispose();
        return;
      }
      await this.verifyFirstFrame(gl);
    }
    if (!this.isCurrent) {
      this.backend.dispose();
      return;
    }
    this.initialized = true;
    this.resize();
    this.then = this.fpsStart = performance.now();
    this.frameCount = 0;
    this.emitStats();
    this.callbacks.ready(this.backend.name);
    this.tick(performance.now());
    if (fallbackReason)
      this.callbacks.notice(
        "WebGPU could not display the scene. The 3D compatibility renderer is now active.",
      );
  }
  private graphicsLost(message: string) {
    if (!this.isCurrent || !this.initialized) return;
    this.pause();
    this.callbacks.error(`Graphics device: ${message}`);
  }
  private async verifyFirstFrame(backend: Backend) {
    this.callbacks.status?.("Checking the first terrain frame…");
    for (let attempt = 0; attempt < 2; attempt++) {
      if (!this.isCurrent) return;
      this.resize();
      this.applyResize();
      const width = this.canvas.width,
        height = this.canvas.height;
      this.frame = this.makeFrame();
      const visible = await withTimeout(
        backend.verifyFrame(this.frame),
        12_000,
        `${backend.name} did not finish the first frame.`,
      );
      if (!this.isCurrent) return;
      // An initially hidden preview gets sized when it becomes visible. It
      // cannot be usefully checked for spatial variation at one pixel wide.
      if (width < 32 || height < 32) return;
      if (
        this.canvas.width === width &&
        this.canvas.height === height &&
        visible
      )
        return;
    }
    throw new Error(
      `${backend.name} produced a blank terrain canvas. Try the compatibility renderer or enable browser graphics acceleration.`,
    );
  }
  private resize() {
    this.resizeRequested = true;
  }
  private applyResize() {
    if (!this.isCurrent || !this.resizeRequested) return;
    const dpr =
      Math.min(devicePixelRatio, 1.5) *
      (this.quality === "native" ? 1 : this.resolution);
    const width = Math.max(1, Math.round(this.container.clientWidth * dpr));
    const height = Math.max(1, Math.round(this.container.clientHeight * dpr));
    if (this.canvas.width !== width) this.canvas.width = width;
    if (this.canvas.height !== height) this.canvas.height = height;
    this.resizeRequested = false;
  }
  private get aspect() {
    return Math.max(
      0.01,
      this.container.clientWidth / Math.max(1, this.container.clientHeight),
    );
  }
  private makeFrame(): FrameState {
    return {
      ...this.camera.basis(this.aspect),
      width: this.canvas.width,
      height: this.canvas.height,
      time: this.time,
      settings: this.settings,
      brush:
        this.compare || this.mode === "look" || this.camera.moving
          ? null
          : this.brush,
      tool: this.tool,
      compare: this.compare,
    };
  }
  private tick = (now: number) => {
    if (!this.isCurrent) return;
    // Recover if a UI remount or old canvas unexpectedly detached the live
    // surface. Ownership above prevents an obsolete engine from doing this.
    if (
      this.canvas.parentElement !== this.container ||
      this.container.childElementCount !== 1
    )
      this.container.replaceChildren(this.canvas);
    this.raf = requestAnimationFrame(this.tick);
    const dt = Math.min((now - this.then) / 1000, 0.1);
    this.then = now;
    if (!document.hidden) {
      this.time += dt;
      if (this.initialized && !this.busy) this.camera.update(dt, this.aspect);
    }
    if (
      document.hidden ||
      !this.initialized ||
      this.busy ||
      !this.backend.ready()
    )
      return;
    if (
      (this.running || this.pendingSteps > 0) &&
      !this.compare &&
      now - this.lastSimulation > (this.backend.name === "WebGPU" ? 35 : 180)
    ) {
      const count = Math.min(
        8000 - this.steps,
        this.backend.name === "WebGPU"
          ? this.running
            ? this.settings.speed
            : Math.min(this.settings.speed, this.pendingSteps)
          : 1,
      );
      this.backend.step(this.settings, count);
      this.steps += count;
      this.pendingSteps = Math.max(0, this.pendingSteps - count);
      this.lastSimulation = now;
      this.callbacks.changed();
      if (this.steps >= 8000) {
        this.running = false;
        this.pendingSteps = 0;
        this.callbacks.notice(
          "8,000 iterations reached. Pause and inspect the result, or reset the simulation.",
        );
      }
    }
    this.applyResize();
    this.frame = this.makeFrame();
    if (
      this.tool !== "orbit" &&
      this.pointerInside &&
      !this.picking &&
      this.mode !== "look" &&
      !this.camera.moving &&
      now - this.lastPick > 55 &&
      !this.compare
    ) {
      this.lastPick = now;
      void this.updateBrush();
    }
    if (this.down && this.mode === "sculpt" && this.brush && !this.compare) {
      this.backend.sculpt(this.brush, this.tool, this.settings);
      this.callbacks.changed();
    }
    try {
      this.backend.render(this.frame);
    } catch (error) {
      this.running = false;
      this.busy = true;
      this.callbacks.error(
        error instanceof Error ? error.message : String(error),
      );
      return;
    }
    this.frameCount++;
    if (now - this.fpsStart > 850) {
      this.fps = (this.frameCount * 1000) / (now - this.fpsStart);
      this.frameCount = 0;
      this.fpsStart = now;
      this.lowFpsWindows = this.fps < 22 ? this.lowFpsWindows + 1 : 0;
      this.highFpsWindows = this.fps > 55 ? this.highFpsWindows + 1 : 0;
      if (
        this.quality === "adaptive" &&
        now - this.lastResolutionChange > 4000
      ) {
        if (this.lowFpsWindows >= 2 && this.resolution > 0.48) {
          this.resolution = Math.max(0.48, this.resolution - 0.08);
          this.resize();
          this.lastResolutionChange = now;
          this.lowFpsWindows = 0;
        } else if (this.highFpsWindows >= 4 && this.resolution < 1) {
          this.resolution = Math.min(1, this.resolution + 0.04);
          this.resize();
          this.lastResolutionChange = now;
          this.highFpsWindows = 0;
        }
      }
      this.emitStats();
    }
  };
  private emitStats() {
    this.callbacks.stats({
      fps: this.fps,
      steps: this.steps,
      backend: this.backend.name,
      size: this.backend.size,
      running: this.running,
      navigationMode: this.camera.mode,
      cameraSpeed: this.camera.speed,
      cameraPosition: this.camera.basis(this.aspect).eye,
      quality: this.quality,
      scaleMeters: this.camera.mode === "fly" ? 1 : 10,
      scalePixels:
        ((this.camera.mode === "fly" ? 1 : 10) * this.container.clientHeight) /
        (this.camera.focusDistance(this.aspect) * 0.82842712),
    });
  }
  private async updateBrush() {
    this.picking = true;
    try {
      const b = await this.backend.pick(
        this.frame,
        this.pointer[0],
        this.pointer[1],
      );
      if (!this.stopped) this.brush = b;
    } catch (e) {
      if (!this.stopped) console.warn("Picking interrupted", e);
    } finally {
      this.picking = false;
    }
  }
  private events() {
    const on = (
      type: string,
      fn: EventListener,
      options?: AddEventListenerOptions,
    ) => {
      this.container.addEventListener(type, fn, options);
      this.cleanups.push(() =>
        this.container.removeEventListener(type, fn, options),
      );
    };
    const coordinates = (e: PointerEvent) => {
      const r = this.container.getBoundingClientRect();
      this.pointer = [
        ((e.clientX - r.left) / r.width) * 2 - 1,
        1 - ((e.clientY - r.top) / r.height) * 2,
      ];
    };
    on("pointerdown", ((e: PointerEvent) => {
      if (e.target !== this.canvas || !this.initialized || this.busy) return;
      e.preventDefault();
      this.canvas.focus({ preventScroll: true });
      this.container.setPointerCapture(e.pointerId);
      this.down = true;
      this.last = [e.clientX, e.clientY];
      coordinates(e);
      if (e.button === 2) {
        this.mode = "look";
        this.brush = null;
        this.camera.enterFly(this.aspect);
        this.emitStats();
        return;
      }
      if (e.button === 1 || e.shiftKey) {
        this.mode = "pan";
        return;
      }
      if (e.altKey || this.tool === "orbit") {
        this.mode = "orbit";
        this.camera.enterOrbit(this.aspect);
        this.emitStats();
        return;
      }
      if (this.compare) return;
      this.running = false;
      this.pendingSteps = 0;
      this.mode = null;
      void this.snapshot()
        .then(async () => {
          if (!this.down || this.stopped) return;
          if (!this.picking) await this.updateBrush();
          if (this.down) this.mode = "sculpt";
        })
        .catch((e) => this.callbacks.error(String(e)));
    }) as EventListener);
    on("pointermove", ((e: PointerEvent) => {
      coordinates(e);
      this.pointerInside = e.target === this.canvas || this.down;
      if (!this.down) return;
      const dx = e.clientX - this.last[0],
        dy = e.clientY - this.last[1];
      this.last = [e.clientX, e.clientY];
      if (this.mode === "orbit") this.camera.orbit(dx, dy, this.aspect);
      if (this.mode === "look") this.camera.look(dx, dy, this.aspect);
      if (this.mode === "pan") this.camera.pan(dx, dy, this.aspect);
    }) as EventListener);
    const up = () => {
      if (this.mode === "look") this.camera.clearKeys();
      this.down = false;
      this.mode = null;
    };
    on("pointerup", up);
    on("pointercancel", up);
    on("lostpointercapture", up);
    on("pointerleave", () => {
      this.pointerInside = false;
      this.brush = null;
    });
    on("contextmenu", (e) => e.preventDefault());
    on(
      "wheel",
      ((e: WheelEvent) => {
        if (e.target !== this.canvas) return;
        e.preventDefault();
        this.camera.wheel(e.deltaY, this.mode === "look", this.aspect);
        this.emitStats();
      }) as EventListener,
      { passive: false },
    );
    on("keydown", ((event: KeyboardEvent) => {
      if (
        !this.initialized ||
        this.busy ||
        this.mode === "sculpt" ||
        event.ctrlKey ||
        event.metaKey ||
        event.altKey
      )
        return;
      if (
        FLY_KEYS.has(event.code) ||
        event.code === "ShiftLeft" ||
        event.code === "ShiftRight"
      ) {
        event.preventDefault();
        this.camera.key(event.code, true, this.aspect);
        this.brush = null;
        this.emitStats();
      }
    }) as EventListener);
    const release = (event: KeyboardEvent) =>
      this.camera.key(event.code, false, this.aspect);
    const clear = () => this.camera.clearKeys();
    const visibility = () => {
      clear();
      if (!document.hidden) {
        this.then = this.fpsStart = performance.now();
        this.frameCount = 0;
        this.lowFpsWindows = this.highFpsWindows = 0;
        this.resize();
      }
    };
    on("focusout", clear);
    window.addEventListener("keyup", release);
    window.addEventListener("blur", clear);
    document.addEventListener("visibilitychange", visibility);
    this.cleanups.push(() => {
      window.removeEventListener("keyup", release);
      window.removeEventListener("blur", clear);
      document.removeEventListener("visibilitychange", visibility);
    });
  }
  setNavigation(mode: NavigationMode) {
    if (mode === "fly") this.camera.enterFly(this.aspect);
    else this.camera.enterOrbit(this.aspect);
    this.canvas.focus({ preventScroll: true });
    this.emitStats();
  }
  setCameraSpeed(speed: number) {
    this.camera.speed = clamp(speed, 0.25, 80);
    this.emitStats();
  }
  setQuality(quality: "adaptive" | "native") {
    this.quality = quality;
    this.resize();
    this.emitStats();
  }

  setTool(tool: Tool) {
    this.tool = tool;
    this.brush = null;
    this.canvas.style.cursor = tool === "orbit" ? "grab" : "crosshair";
    if (tool !== "orbit") {
      this.running = false;
      this.pendingSteps = 0;
      this.emitStats();
    }
  }
  update(settings: Settings, preview = false) {
    this.settings = { ...settings };
    if (
      preview &&
      settings.autoPreview &&
      !this.running &&
      this.initialized &&
      this.steps < 8000
    ) {
      if (this.pendingSteps > 0) {
        this.pendingSteps = 12;
      } else if (!this.busy && !this.previewSnapshotPending) {
        this.previewSnapshotPending = true;
        void this.snapshot()
          .then(() => {
            if (this.settings.autoPreview && !this.stopped)
              this.pendingSteps = 12;
          })
          .catch((e) => this.callbacks.error(String(e)))
          .finally(() => (this.previewSnapshotPending = false));
      }
    }
    if (!settings.autoPreview && !this.running) this.pendingSteps = 0;
  }
  async toggleSimulation() {
    if (this.busy || !this.initialized) return;
    if (this.running) {
      this.running = false;
      this.pendingSteps = 0;
      this.emitStats();
      return;
    }
    if (this.steps >= 8000) {
      this.callbacks.notice(
        "Reset the terrain before starting another simulation.",
      );
      return;
    }
    await this.snapshot();
    this.running = true;
    this.emitStats();
  }
  async singleStep() {
    if (this.busy || this.steps >= 8000) return;
    this.running = false;
    await this.snapshot();
    this.pendingSteps = 1;
  }
  pause() {
    this.running = false;
    this.pendingSteps = 0;
    if (this.initialized) this.emitStats();
  }
  async snapshot() {
    if (this.busy || !this.initialized) return;
    this.busy = true;
    try {
      this.undoStack.push({
        data: await this.backend.readVolume(),
        steps: this.steps,
      });
      if (this.undoStack.length > 4) this.undoStack.shift();
      this.redoStack = [];
      this.callbacks.history(this.undoStack.length, 0);
    } finally {
      this.busy = false;
    }
  }
  async undo(redo = false) {
    if (this.busy) return;
    this.pause();
    const source = redo ? this.redoStack : this.undoStack,
      target = redo ? this.undoStack : this.redoStack;
    if (!source.length) return;
    this.busy = true;
    try {
      const now = await this.backend.readVolume(),
        previous = source.pop()!;
      target.push({ data: now, steps: this.steps });
      this.backend.writeVolume(previous.data);
      this.steps = previous.steps;
      this.callbacks.history(this.undoStack.length, this.redoStack.length);
      this.callbacks.changed();
      this.emitStats();
    } finally {
      this.busy = false;
    }
  }
  async regenerate(settings: Settings) {
    this.pause();
    this.busy = true;
    try {
      this.settings = { ...settings };
      await this.backend.regenerate(settings);
      this.steps = 0;
      this.undoStack = [];
      this.redoStack = [];
      this.callbacks.history(0, 0);
      this.callbacks.changed();
      this.resetCamera();
      this.emitStats();
    } finally {
      this.busy = false;
    }
  }
  async reset() {
    this.pause();
    await this.snapshot();
    this.backend.reset();
    this.steps = 0;
    this.callbacks.changed();
    this.emitStats();
  }
  resetCamera(top = false) {
    this.camera.frame(top);
    if (this.initialized) this.emitStats();
  }
  async load(
    data: Float32Array,
    size: VolumeSize,
    settings: Settings,
    steps: number,
  ) {
    await this.regenerate(settings);
    this.busy = true;
    try {
      const dest = this.backend.size;
      if (size.x === dest.x && size.y === dest.y && size.z === dest.z)
        this.backend.writeVolume(data);
      else {
        const resampled = new Float32Array(dest.x * dest.y * dest.z * 4);
        for (let z = 0; z < dest.z; z++)
          for (let y = 0; y < dest.y; y++)
            for (let x = 0; x < dest.x; x++) {
              const p: Vec3 = [
                BOUNDS_MIN[0] + ((x + 0.5) / dest.x) * WORLD_SIZE[0],
                BOUNDS_MIN[1] + ((y + 0.5) / dest.y) * WORLD_SIZE[1],
                BOUNDS_MIN[2] + ((z + 0.5) / dest.z) * WORLD_SIZE[2],
              ];
              for (let k = 0; k < 4; k++)
                resampled[((z * dest.y + y) * dest.x + x) * 4 + k] =
                  sampleField(data, size, p, k);
            }
        this.backend.writeVolume(resampled);
        this.callbacks.notice(
          "Project resampled to this device’s 3D volume resolution.",
        );
      }
      this.steps = steps;
      this.emitStats();
    } finally {
      this.busy = false;
    }
  }
  async capture(): Promise<Blob> {
    const wasBusy = this.busy;
    this.busy = true;
    try {
      // Export at viewport resolution, independent of adaptive live rendering,
      // and omit the editor's brush ring. Keep animation submissions paused
      // until the canvas has been read back.
      await this.backend.sync();
      const dpr = Math.min(devicePixelRatio, 1.5);
      this.canvas.width = Math.max(
        1,
        Math.round(this.container.clientWidth * dpr),
      );
      this.canvas.height = Math.max(
        1,
        Math.round(this.container.clientHeight * dpr),
      );
      this.backend.render({ ...this.makeFrame(), brush: null, tool: "orbit" });
      await this.backend.sync();
      return await new Promise<Blob>((resolve, reject) =>
        this.canvas.toBlob(
          (b) =>
            b
              ? resolve(b)
              : reject(new Error("Could not capture the viewport.")),
          "image/png",
        ),
      );
    } finally {
      // Leave the completed capture visible. The next live draw, not this
      // cleanup, will restore adaptive sizing without a blank interval.
      this.resize();
      this.busy = wasBusy;
    }
  }
  dispose() {
    this.stopped = true;
    this.camera.clearKeys();
    cancelAnimationFrame(this.raf);
    this.resizeObserver.disconnect();
    this.cleanups.forEach((fn) => fn());
    this.initialized = false;
    this.backend?.dispose();
    this.canvas.remove();
    if (canvasOwners.get(this.container) === this)
      canvasOwners.delete(this.container);
  }
}
