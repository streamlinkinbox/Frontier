import { WebGPUBackend } from "./WebGPUBackend";
import { WebGLBackend } from "./WebGLBackend";
import { clamp, normalize, add, scale, sub } from "./math";
import { makeBrushStamp, brushPlaneHit, projectToBrushPlane } from "./brush";
import { EditorCamera, FLY_KEYS, type NavigationMode } from "./EditorCamera";
import { sampleField } from "./field";
import { nextVisibleFrame, withTimeout } from "./startup";
import { chooseGPUDisplay } from "./display";
import { diagnostics, canvasSnapshot, summarizeVolume } from "../diagnostics";
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
  type BrushStamp,
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
  private pickTask: Promise<void> | null = null;
  private hoverNormal: Vec3 = [0, 1, 0];
  private stroke: BrushStamp | null = null;
  private strokeToken = 0;
  private strokeSequence = 0;
  private dabSequence = 0;
  private lastDab: Vec3 | null = null;
  private lastDabPointer: [number, number] = [0, 0];
  private lastSculpt = 0;
  private pointer: [number, number] = [0, 0];
  private mode: "orbit" | "pan" | "sculpt" | "look" | null = null;
  private down = false;
  private pointerInside = false;
  private last: [number, number] = [0, 0];
  readonly camera = new EditorCamera();
  private resolution = 0.9;
  private renderWidth = 1;
  private renderHeight = 1;
  private frameWaitStarted = 0;
  private graphicsFailed = false;
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
  private startupAbort = new AbortController();
  private restoreDisplayRequested = false;
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
    diagnostics.log("Engine", "Viewport created", {
      preset: settings.preset,
      seed: settings.seed,
      clientSize: [container.clientWidth, container.clientHeight],
    });
  }
  private startupStatus(message: string) {
    diagnostics.log("Startup", message);
    this.callbacks.status?.(message);
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
      const gpu = new WebGPUBackend(
        this.canvas,
        chooseGPUDisplay(location.search, navigator.userAgent),
      );
      this.backend = gpu;
      gpu.onLost = (m) => this.graphicsLost(m);
      this.startupStatus("Initializing the WebGPU terrain volume…");
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
        diagnostics.log(
          "Startup",
          "WebGPU failed; using compatibility renderer",
          error,
          "warn",
        );
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
      this.startupStatus("Starting the 3D compatibility renderer…");
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
    this.restoreDisplayRequested = false;
    this.resize();
    this.then = this.fpsStart = performance.now();
    this.frameCount = 0;
    this.emitStats();
    diagnostics.log(
      "Engine",
      "Ready (GPU readback checked; browser composition not verified)",
      { backend: this.backend.name },
    );
    this.callbacks.ready(this.backend.name);
    this.raf = requestAnimationFrame(this.tick);
    if (fallbackReason)
      this.callbacks.notice(
        "WebGPU could not display the scene. The 3D compatibility renderer is now active.",
      );
  }
  private graphicsLost(message: string) {
    if (!this.isCurrent || !this.initialized || this.graphicsFailed) return;
    diagnostics.log(
      "Engine",
      "Graphics interrupted",
      { message, backend: this.backend.getDiagnostics() },
      "error",
    );
    this.graphicsFailed = true;
    this.running = false;
    this.pendingSteps = 0;
    this.fps = 0;
    this.camera.clearKeys();
    this.emitStats();
    this.callbacks.error(`Graphics device: ${message}`);
  }
  private async verifyFirstFrame(backend: Backend) {
    this.startupStatus("Checking the first terrain frame…");
    for (let attempt = 0; attempt < 2; attempt++) {
      if (!this.isCurrent) return;
      if (document.hidden)
        this.startupStatus(
          "Waiting for the viewport to become visible before displaying terrain…",
        );
      await nextVisibleFrame(this.container, this.startupAbort.signal);
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
      if (document.hidden) {
        attempt--;
        continue;
      }
      // An initially hidden preview gets sized when it becomes visible. It
      // cannot be usefully checked for spatial variation at one pixel wide.
      diagnostics.log("Startup", "First-frame verification result", {
        backend: backend.name,
        attempt: attempt + 1,
        sampledBitmapSize: [width, height],
        visiblePixels: visible,
      });
      if (width < 32 || height < 32) {
        diagnostics.log(
          "Startup",
          "Tiny or hidden viewport; full-sized first-frame check skipped",
          { width, height },
          "warn",
        );
        return;
      }
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
    const dpr = Math.min(devicePixelRatio, 1.5);
    const width = Math.max(1, Math.round(this.container.clientWidth * dpr));
    const height = Math.max(1, Math.round(this.container.clientHeight * dpr));
    // Only a real display-size change may reset the visible bitmap. Adaptive
    // quality changes resize a backend-owned render target, not this canvas.
    if (this.canvas.width !== width) this.canvas.width = width;
    if (this.canvas.height !== height) this.canvas.height = height;
    const scale = this.quality === "native" ? 1 : this.resolution;
    this.renderWidth = Math.max(1, Math.round(width * scale));
    this.renderHeight = Math.max(1, Math.round(height * scale));
    diagnostics.log("Canvas", "Display/render sizing applied", {
      display: [width, height],
      render: [this.renderWidth, this.renderHeight],
      quality: this.quality,
      dpr,
    });
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
      width: this.renderWidth,
      height: this.renderHeight,
      time: this.time,
      settings: this.settings,
      brush:
        this.compare || this.mode === "look" || this.camera.moving
          ? null
          : this.brush,
      tool: this.tool,
      stamp:
        this.stroke ??
        (this.brush
          ? makeBrushStamp(
              this.brush,
              this.hoverNormal,
              this.camera.basis(this.aspect).right,
              this.settings.seed,
            )
          : undefined),
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
      if (this.initialized && !this.busy && !this.graphicsFailed)
        this.camera.update(dt, this.aspect);
    }
    if (
      document.hidden ||
      !this.initialized ||
      this.busy ||
      this.graphicsFailed
    ) {
      this.frameWaitStarted = 0;
      return;
    }
    if (!this.backend.ready()) {
      if (!this.frameWaitStarted) this.frameWaitStarted = now;
      if (now - this.frameWaitStarted > 15_000)
        this.graphicsLost(
          "The renderer stopped responding for 15 seconds. Try compatibility rendering or restart the renderer.",
        );
      return;
    }
    this.frameWaitStarted = 0;
    try {
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
      if (this.restoreDisplayRequested) {
        this.backend.refreshDisplaySurface?.();
        this.restoreDisplayRequested = false;
      }
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
      if (
        this.down &&
        this.mode === "sculpt" &&
        this.brush &&
        this.stroke &&
        !this.compare &&
        now - this.lastSculpt >= 32
      ) {
        const distance = this.lastDab
          ? Math.hypot(...sub(this.brush, this.lastDab))
          : Infinity;
        const spacing =
          this.tool === "boulder" ? this.settings.radius * 0.95 : 0;
        const pointerMoved =
          Math.hypot(
            (this.pointer[0] - this.lastDabPointer[0]) *
              this.container.clientWidth *
              0.5,
            (this.pointer[1] - this.lastDabPointer[1]) *
              this.container.clientHeight *
              0.5,
          ) > 2;
        if (
          distance >= spacing &&
          (this.tool !== "boulder" || !this.lastDab || pointerMoved)
        ) {
          if (
            this.lastDab &&
            (this.tool === "crack" || this.tool === "crevice") &&
            pointerMoved
          ) {
            const movement = sub(this.brush, this.lastDab);
            if (Math.hypot(...movement) > 0.1)
              this.stroke.tangent = makeBrushStamp(
                this.stroke.origin,
                this.hoverNormal,
                movement,
                this.stroke.seed,
              ).tangent;
          }
          const stamp =
            this.tool === "flatten"
              ? this.stroke
              : makeBrushStamp(
                  this.stroke.origin,
                  this.hoverNormal,
                  this.stroke.tangent,
                  this.stroke.seed +
                    (this.tool === "boulder" ? this.dabSequence * 101 : 0),
                );
          const center =
            this.tool === "flatten"
              ? projectToBrushPlane(this.brush, stamp)
              : this.brush;
          const previous = this.lastDab ?? center;
          // Fill small pointer gaps so a low frame rate does not break a drawn crack.
          const pieces =
            this.tool === "boulder"
              ? 1
              : Math.min(
                  8,
                  Math.max(
                    1,
                    Math.ceil(
                      Math.hypot(...sub(center, previous)) /
                        (this.settings.radius * 0.3),
                    ),
                  ),
                );
          for (let part = 1; part <= pieces; part++) {
            const dab: Vec3 = [
              previous[0] + ((center[0] - previous[0]) * part) / pieces,
              previous[1] + ((center[1] - previous[1]) * part) / pieces,
              previous[2] + ((center[2] - previous[2]) * part) / pieces,
            ];
            this.backend.sculpt(dab, this.tool, this.settings, {
              ...stamp,
              previous,
            });
          }
          this.lastDab = [...center];
          this.lastDabPointer = [...this.pointer];
          this.lastSculpt = now;
          this.dabSequence++;
          this.callbacks.changed();
        }
      }
      this.backend.render(this.frame);
    } catch (error) {
      this.graphicsLost(error instanceof Error ? error.message : String(error));
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
      displayMode: this.backend.displayMode,
      size: this.backend.size,
      running: this.running || this.pendingSteps > 0,
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
  private updateBrush(coordinates = this.pointer): Promise<void> {
    if (this.pickTask) return this.pickTask;
    if (
      this.down &&
      this.mode === "sculpt" &&
      this.tool === "flatten" &&
      this.stroke
    ) {
      const basis = this.camera.basis(this.aspect);
      const ray = normalize(
        add(
          basis.forward,
          add(
            scale(basis.right, coordinates[0] * this.aspect * 0.41421356),
            scale(basis.up, coordinates[1] * 0.41421356),
          ),
        ),
      );
      this.brush = brushPlaneHit(basis.eye, ray, this.stroke);
      return Promise.resolve();
    }
    this.picking = true;
    this.pickTask = (async () => {
      try {
        const hit = await this.backend.pick(
          this.frame,
          coordinates[0],
          coordinates[1],
        );
        if (!this.stopped) {
          this.brush = hit?.position ?? null;
          if (hit) this.hoverNormal = hit.normal;
        }
      } catch (error) {
        if (!this.stopped) console.warn("Picking interrupted", error);
      } finally {
        this.picking = false;
        this.pickTask = null;
      }
    })();
    return this.pickTask;
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
      const token = ++this.strokeToken;
      const startPointer: [number, number] = [...this.pointer];
      void this.snapshot()
        .then(async () => {
          if (!this.down || this.stopped || token !== this.strokeToken) return;
          if (this.pickTask) await this.pickTask;
          await this.updateBrush(startPointer);
          if (this.down && token === this.strokeToken && this.brush) {
            const normal: Vec3 =
              this.tool === "flatten" &&
              this.settings.flattenPlane === "horizontal"
                ? [0, 1, 0]
                : this.hoverNormal;
            this.stroke = makeBrushStamp(
              this.brush,
              normal,
              this.camera.basis(this.aspect).right,
              this.settings.seed + ++this.strokeSequence * 73,
            );
            this.lastDab = null;
            this.lastSculpt = 0;
            this.dabSequence = 0;
            this.mode = "sculpt";
          }
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
      this.stroke = null;
      this.lastDab = null;
      this.strokeToken++;
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
    const clear = () => {
      this.camera.clearKeys();
      up();
    };
    const visibility = () => {
      clear();
      this.frameWaitStarted = 0;
      if (!document.hidden) {
        this.restoreDisplayRequested = true;
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
  getDiagnostics(): Record<string, unknown> {
    return {
      initialized: this.initialized,
      currentOwner: this.isCurrent,
      stopped: this.stopped,
      graphicsFailed: this.graphicsFailed,
      busy: this.busy,
      running: this.running,
      fps: +this.fps.toFixed(1),
      steps: this.steps,
      pendingSteps: this.pendingSteps,
      waitingForFrameMs: this.frameWaitStarted
        ? Math.round(performance.now() - this.frameWaitStarted)
        : 0,
      quality: this.quality,
      resolutionScale: this.resolution,
      renderSize: [this.renderWidth, this.renderHeight],
      resizeRequested: this.resizeRequested,
      camera: { mode: this.camera.mode, ...this.camera.basis(this.aspect) },
      settings: {
        preset: this.settings.preset,
        seed: this.settings.seed,
        terrainVisible: this.settings.terrainVisible,
        view: this.settings.view,
        exposure: this.settings.exposure,
        sunAngle: this.settings.sunAngle,
        detail: this.settings.detail,
        water: this.settings.water,
        waterLevel: this.settings.waterLevel,
        compare: this.compare,
      },
      backend: this.backend?.getDiagnostics(),
      canvas: canvasSnapshot(this.canvas, this.container),
    };
  }
  /** Explicit, bounded inspection. It never sculpts, steps, resets or saves. */
  async checkViewport() {
    diagnostics.log(
      "Check",
      "User requested viewport diagnostics",
      this.getDiagnostics(),
    );
    if (
      !this.initialized ||
      !this.isCurrent ||
      this.graphicsFailed ||
      this.busy ||
      this.picking ||
      this.down
    ) {
      diagnostics.log(
        "Check",
        "GPU checks skipped: renderer is unavailable or busy. Startup/errors are still in this report.",
        undefined,
        "warn",
      );
      return;
    }
    const backend = this.backend;
    this.busy = true;
    this.camera.clearKeys();
    let stage = "waiting for GPU queue";
    try {
      diagnostics.log("Check", stage);
      await withTimeout(
        backend.sync(),
        6000,
        "GPU queue did not complete within 6 seconds.",
      );
      if (!this.isCurrent) return;
      stage = "checking presentation texture";
      diagnostics.log("Check", stage);
      await nextVisibleFrame(this.container, this.startupAbort.signal);
      if (!this.isCurrent) return;
      await withTimeout(
        backend.verifyFrame(this.makeFrame()),
        8000,
        "Presentation pixel readback timed out after 8 seconds.",
      );
      if (!this.isCurrent) return;
      // Never copy a live WebGPU canvas or route probes through PNG/Canvas2D:
      // a browser-side image transfer can block JS on an unhealthy compositor.
      diagnostics.log(
        "Check",
        "Browser compositor pixels are not readable safely by this app",
        {
          note: "Both probes copy GPU textures into owned buffers. They do not prove on-screen display; report the visible result separately.",
        },
      );
      stage = "checking owned off-screen image";
      diagnostics.log("Check", stage);
      const frame = this.makeFrame();
      await withTimeout(
        backend.verifyFrame(
          {
            ...frame,
            width: 192,
            height: Math.max(1, Math.round((192 * frame.height) / frame.width)),
            brush: null,
            tool: "orbit",
          },
          "offscreen",
        ),
        8000,
        "Owned render target check timed out after 8 seconds.",
      );
      if (!this.isCurrent) return;
      stage = "reading SDF volume statistics";
      diagnostics.log("Check", stage);
      const data = await withTimeout(
        backend.readVolume(),
        8000,
        "SDF readback timed out after 8 seconds.",
      );
      if (!this.isCurrent) return;
      const summary = summarizeVolume(data);
      diagnostics.log(
        "Check",
        "SDF volume statistics (no voxel data included)",
        summary,
        summary.nonFiniteSDF || !summary.solidVoxels ? "warn" : "info",
      );
      diagnostics.log(
        "Check",
        "Viewport checks complete. Copy these logs and describe whether the main viewport is still blank.",
      );
    } catch (error) {
      diagnostics.log("Check", `Failed while ${stage}`, error, "error");
    } finally {
      this.busy = false;
    }
  }
  setQuality(quality: "adaptive" | "native") {
    this.quality = quality;
    this.resize();
    this.emitStats();
  }

  setTool(tool: Tool) {
    this.tool = tool;
    this.brush = null;
    this.down = false;
    this.mode = null;
    this.stroke = null;
    this.lastDab = null;
    this.strokeToken++;
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
      !this.graphicsFailed &&
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
    if (this.busy || !this.initialized || this.graphicsFailed) return;
    if (this.running || this.pendingSteps > 0) {
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
  async weatherBatch(count = 80) {
    if (
      this.busy ||
      !this.initialized ||
      this.graphicsFailed ||
      this.steps >= 8000
    )
      return;
    this.pause();
    await this.snapshot();
    this.pendingSteps = Math.min(count, 8000 - this.steps);
    this.emitStats();
  }
  async singleStep() {
    if (this.busy || this.graphicsFailed || this.steps >= 8000) return;
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
    if (this.busy || !this.initialized || this.graphicsFailed) return;
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
      await withTimeout(
        this.backend.sync(),
        15_000,
        "The GPU did not finish the capture.",
      );
      // The backend copies its owned render texture before awaiting readback.
      // No visible canvas resize, and no recycled WebGPU swapchain capture.
      return await withTimeout(
        this.backend.capture({
          ...this.makeFrame(),
          width: this.canvas.width,
          height: this.canvas.height,
          brush: null,
          tool: "orbit",
        }),
        15_000,
        "Viewport capture timed out. Try compatibility rendering.",
      );
    } finally {
      this.busy = wasBusy;
    }
  }
  dispose() {
    diagnostics.log("Engine", "Viewport disposed");
    this.stopped = true;
    this.startupAbort.abort();
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
