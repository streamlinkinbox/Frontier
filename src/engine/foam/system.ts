import { diagnostics } from "../../diagnostics";
import type { FrameState, FoamQuality, Settings } from "../types";
import {
  FOAM_PROFILES,
  FOAM_STEP,
  FoamClock,
  foamQualityIndex,
  foamProfile,
  makeFoamAtlas,
  type FoamProfile,
} from "./model";
import type { FoamPass } from "./shaders";

export type FoamFormat = "rgba8unorm" | "rgba16float" | "rgba32float";
export interface FoamTexture<T> {
  handle: T;
  width: number;
  height: number;
  format: FoamFormat;
  mipLevels?: number;
}
export interface FoamDriver<T> {
  supported: boolean;
  reason: string;
  create(
    width: number,
    height: number,
    format: FoamFormat,
    label: string,
    pixels?: Uint8Array,
    mipmaps?: boolean,
  ): FoamTexture<T>;
  clear(textures: FoamTexture<T>[]): void;
  pass(
    kind: FoamPass,
    outputs: FoamTexture<T>[],
    inputs: Map<number, T>,
    params: Float32Array,
    instances?: number,
  ): void;
  copy(from: FoamTexture<T>, to: FoamTexture<T>): void;
  destroy(texture: FoamTexture<T>): void;
  read(texture: FoamTexture<T>): Promise<Float32Array>;
  mipmaps?(texture: FoamTexture<T>): void;
  dispose(): void;
}
/** Shared resource graph and fixed-step scheduling, backed by GPU render-to-
 * texture passes on BOTH APIs. Particle state stays in floating-point textures. */
export class FoamSystem<T> {
  readonly clock = new FoamClock();
  quality: FoamQuality = "low";
  requested: FoamQuality = "low";
  budget: Settings["foamBudget"] = "balanced";
  profile: FoamProfile = FOAM_PROFILES.low;
  version = 0;
  time = 0;
  ticks = 0;
  geometryBakes = 0;
  private configured = false;
  private dirty = true;
  private resetRequested = false;
  private geometryTime = -Infinity;
  private geometryKey = "";
  private lastSeed = "";
  private warming = true;
  private resources: FoamTexture<T>[] = [];
  private screenResources: FoamTexture<T>[] = [];
  private focus: [number, number] = [0, 0];
  private input = new Map<number, T>();
  readonly atlas: FoamTexture<T>;
  readonly dummy: FoamTexture<T>;
  density!: FoamTexture<T>;
  motion!: FoamTexture<T>;
  geometry!: FoamTexture<T>;
  lighting!: FoamTexture<T>;
  private motionScratch!: FoamTexture<T>;
  private densityScratch!: FoamTexture<T>;
  positions!: FoamTexture<T>;
  velocities!: FoamTexture<T>;
  private positionScratch!: FoamTexture<T>;
  private velocityScratch!: FoamTexture<T>;
  scene!: FoamTexture<T>;
  hits!: FoamTexture<T>;
  transmitted!: FoamTexture<T>;
  air!: FoamTexture<T>;
  bubbles!: FoamTexture<T>;
  constructor(readonly driver: FoamDriver<T>) {
    this.dummy = driver.create(
      1,
      1,
      "rgba8unorm",
      "Empty foam",
      new Uint8Array(4),
    );
    this.atlas = driver.create(
      128,
      128,
      "rgba8unorm",
      "Filtered foam microstructure",
      makeFoamAtlas(),
    );
    this.configure("low");
  }
  configure(
    requested: FoamQuality,
    budget: Settings["foamBudget"] = "balanced",
  ) {
    this.requested = requested;
    const quality = this.driver.supported ? requested : "low";
    if (this.configured && quality === this.quality && budget === this.budget)
      return;
    for (const texture of this.resources) this.driver.destroy(texture);
    for (const texture of this.screenResources) this.driver.destroy(texture);
    this.resources = [];
    this.screenResources = [];
    this.quality = quality;
    this.budget = budget;
    this.profile = foamProfile(quality, budget);
    const create = (
      w: number,
      h: number,
      format: FoamFormat,
      label: string,
      mipmaps = false,
    ) => {
      const t = this.driver.create(w, h, format, label, undefined, mipmaps);
      this.resources.push(t);
      return t;
    };
    if (quality === "low") {
      this.density =
        this.motion =
        this.geometry =
        this.lighting =
        this.positions =
        this.velocities =
          this.dummy;
    } else {
      const { flow, map, particleWidth, particleHeight } = this.profile;
      this.geometry = create(
        flow,
        flow,
        "rgba16float",
        "Water-level bank and bed cache",
      );
      this.lighting = create(flow, flow, "rgba16float", "Foam lighting cache");
      this.motion = create(
        flow,
        flow,
        "rgba16float",
        "Water velocity, head, emission",
      );
      this.motionScratch = create(flow, flow, "rgba16float", "Flow next state");
      this.density = create(
        map,
        map,
        "rgba16float",
        "Foam density and age moment",
        true,
      );
      this.densityScratch = this.profile.particles
        ? this.dummy
        : create(map, map, "rgba16float", "Foam transport next state");
      if (this.profile.particles) {
        this.positions = create(
          particleWidth,
          particleHeight,
          "rgba32float",
          "Foam particle position and life",
        );
        this.velocities = create(
          particleWidth,
          particleHeight,
          "rgba32float",
          "Foam particle velocity and phase",
        );
        this.positionScratch = create(
          particleWidth,
          particleHeight,
          "rgba32float",
          "Particle position next state",
        );
        this.velocityScratch = create(
          particleWidth,
          particleHeight,
          "rgba32float",
          "Particle velocity next state",
        );
      } else {
        this.positions = this.velocities = this.dummy;
      }
      // New WebGPU textures are zero initialized; explicitly clear for parity.
      for (const t of this.resources) this.driver.clear([t]);
    }
    this.configured = true;
    this.version++;
    this.resetClock();
    diagnostics.log("Foam", "GPU foam tier configured", {
      requested,
      effective: quality,
      ...this.profile,
      fallback: this.driver.reason,
    });
  }
  effectiveFrame(frame: FrameState): FrameState {
    this.configure(frame.settings.foamQuality, frame.settings.foamBudget);
    return this.quality === frame.settings.foamQuality
      ? frame
      : {
          ...frame,
          settings: {
            ...frame.settings,
            foamQuality: this.quality,
            foamView: "surface",
          },
        };
  }
  isCinematic(frame: FrameState) {
    return (
      this.quality === "cinematic" &&
      !frame.compare &&
      frame.settings.water &&
      frame.settings.view === "lit" &&
      frame.settings.foamView === "surface" &&
      (frame.settings.textureMode !== "satmap" ||
        frame.settings.satmapPreview === "beauty")
    );
  }
  private resetClock() {
    this.clock.reset();
    this.time = 0;
    this.ticks = 0;
    this.dirty = true;
    this.warming = true;
    this.geometryTime = -Infinity;
  }
  invalidate() {
    this.dirty = true;
  }
  reset() {
    this.resetRequested = true;
  }
  private params(dt = FOAM_STEP): Float32Array {
    return new Float32Array([
      dt,
      this.time,
      foamQualityIndex(this.quality),
      this.warming ? 1 : 0,
      this.profile.flow,
      this.profile.map,
      this.profile.particleWidth,
      this.profile.particleHeight,
      ...this.focus,
      18,
      0,
    ]);
  }
  bindings(field?: T): Map<number, T> {
    const map = new Map<number, T>([
      [10, this.density.handle],
      [11, this.motion.handle],
      [12, this.geometry.handle],
      [13, this.lighting.handle],
      [14, this.atlas.handle],
      [15, this.positions.handle],
      [17, this.velocities.handle],
    ]);
    if (field) map.set(1, field);
    return map;
  }
  update(frame: FrameState, field: T, advance = true) {
    if (this.quality === "low") {
      this.clock.advance(frame.time, false);
      return;
    }
    const key = `${frame.settings.seed}:${frame.settings.preset}`;
    if (key !== this.lastSeed) {
      this.lastSeed = key;
      this.resetRequested = true;
    }
    if (this.resetRequested) {
      for (const t of this.resources) this.driver.clear([t]);
      this.driver.mipmaps?.(this.density);
      this.resetClock();
      this.resetRequested = false;
    }
    // Compare never runs current foam through the original SDF. Its original
    // water uses Low shading while the live history remains intact and frozen.
    if (frame.compare) {
      if (advance) this.clock.advance(frame.time, false);
      return;
    }
    const gkey = `${frame.settings.waterLevel}:${frame.settings.sunAngle}:${frame.settings.terrainVisible}`;
    if (gkey !== this.geometryKey) {
      this.geometryKey = gkey;
      this.dirty = true;
      this.geometryTime = -Infinity;
    }
    this.input = this.bindings(field);
    const range = Math.min(
      300,
      Math.max(
        0,
        (frame.eye[1] - frame.settings.waterLevel) /
          Math.max(0.15, -frame.forward[1]),
      ),
    );
    this.focus = [
      Math.round(
        Math.max(-36, Math.min(36, frame.eye[0] + frame.forward[0] * range)) /
          2,
      ) * 2,
      Math.round(
        Math.max(-36, Math.min(36, frame.eye[2] + frame.forward[2] * range)) /
          2,
      ) * 2,
    ];
    if (
      this.dirty &&
      (frame.time - this.geometryTime >= 0.25 || !advance || this.warming)
    ) {
      this.driver.pass(
        "geometry",
        [this.geometry, this.lighting],
        this.input,
        this.params(),
      );
      this.geometryTime = frame.time;
      this.dirty = false;
      this.geometryBakes++;
    }
    if (!advance) return; // Captures do not advance or prewarm effect history.
    const enabled = frame.settings.water && !frame.settings.foamPaused;
    const count = this.clock.advance(frame.time, enabled);
    if (!enabled) return;
    // One bounded initial step seeds the driving velocity; no fake prefilled foam.
    const steps = this.warming ? Math.max(1, count) : count;
    for (let i = 0; i < steps; i++) {
      // 60 Hz substeps keep wave CFL < .5 for the bounded 3 m/s, 1.5 m head model.
      for (let j = 0; j < 2; j++) {
        this.driver.pass(
          "flow",
          [this.motionScratch],
          this.input,
          this.params(FOAM_STEP / 2),
        );
        this.driver.copy(this.motionScratch, this.motion);
      }
      this.warming = false;
      if (!this.profile.particles) {
        this.driver.pass(
          "density",
          [this.densityScratch],
          this.input,
          this.params(),
        );
        this.driver.copy(this.densityScratch, this.density);
      } else {
        this.driver.pass(
          "particles",
          [this.positionScratch, this.velocityScratch],
          this.input,
          this.params(),
        );
        this.driver.copy(this.positionScratch, this.positions);
        this.driver.copy(this.velocityScratch, this.velocities);
      }
      this.time += FOAM_STEP;
      this.ticks++;
    }
    if (steps && this.profile.particles) {
      // The reconstruction target is CLEARED. Never advect a map containing
      // yesterday's splats as well as advecting the same particles.
      this.driver.pass(
        "splat",
        [this.density],
        this.input,
        this.params(),
        this.profile.particles,
      );
    }
    if (steps) this.driver.mipmaps?.(this.density);
  }
  prepareScreen(width: number, height: number) {
    if (
      this.screenResources.length &&
      this.scene.width === width &&
      this.scene.height === height
    )
      return;
    for (const t of this.screenResources) this.driver.destroy(t);
    this.screenResources = [];
    const make = (w: number, h: number, name: string) => {
      const t = this.driver.create(w, h, "rgba16float", name);
      this.screenResources.push(t);
      return t;
    };
    this.scene = make(width, height, "Linear HDR water and terrain");
    this.hits = make(width, height, "Opaque and water hit distances");
    this.transmitted = make(width, height, "Weighted water transmission");
    this.air = make(
      Math.ceil(width / 2),
      Math.ceil(height / 2),
      "Airborne spray optical layer",
    );
    this.bubbles = make(
      Math.ceil(width / 2),
      Math.ceil(height / 2),
      "Submerged bubble optical layer",
    );
  }
  composite(destination: FoamTexture<T>, field: T) {
    const input = this.bindings(field);
    input.set(18, this.scene.handle);
    input.set(19, this.hits.handle);
    input.set(20, this.transmitted.handle);
    input.set(21, this.air.handle);
    input.set(22, this.bubbles.handle);
    this.driver.pass(
      "air",
      [this.air],
      input,
      this.params(),
      this.profile.particles,
    );
    this.driver.pass(
      "bubble",
      [this.bubbles],
      input,
      this.params(),
      this.profile.particles,
    );
    this.driver.pass("compose", [destination], input, this.params());
  }
  diagnostics() {
    const bytes = [
      ...this.resources,
      ...this.screenResources,
      this.atlas,
    ].reduce(
      (sum, t) =>
        sum +
        Math.round(
          t.width *
            t.height *
            (t.format === "rgba32float"
              ? 16
              : t.format === "rgba16float"
                ? 8
                : 4) *
            (t.mipLevels && t.mipLevels > 1
              ? (1 - Math.pow(0.25, t.mipLevels)) / 0.75
              : 1),
        ),
      0,
    );
    return {
      requested: this.requested,
      quality: this.quality,
      budget: this.budget,
      supported: this.driver.supported,
      fallback: this.driver.reason,
      model:
        this.quality === "low"
          ? "stateless"
          : this.quality === "standard"
            ? "advected density"
            : this.quality === "ultra"
              ? "GPU surface particles"
              : "localized spray / foam / bubbles",
      flowResolution: this.profile.flow,
      mapResolution: this.profile.map,
      particleCapacity: this.profile.particles,
      ticks: this.ticks,
      simulationSeconds: this.time,
      droppedSeconds: this.clock.droppedSeconds,
      geometryBakes: this.geometryBakes,
      bytes,
      focus: this.focus,
      precision: "f32 particles / f16 fields",
      transientHistory: true,
    };
  }
  /** Explicit diagnostic readback only. Never called by the animation loop. */
  async inspect() {
    const [density, motion, geometry, positions, velocities] =
      await Promise.all([
        this.driver.read(this.density),
        this.driver.read(this.motion),
        this.driver.read(this.geometry),
        this.driver.read(this.positions),
        this.driver.read(this.velocities),
      ]);
    const counts = [0, 0, 0];
    for (let i = 3; i < velocities.length; i += 4) {
      const kind = Math.round(velocities[i]);
      if (kind >= 1 && kind <= 3 && positions[i] > 0) counts[kind - 1]++;
    }
    return { density, motion, geometry, positions, velocities, counts };
  }
  dispose() {
    for (const t of [
      ...this.resources,
      ...this.screenResources,
      this.atlas,
      this.dummy,
    ])
      this.driver.destroy(t);
    this.driver.dispose();
  }
}
