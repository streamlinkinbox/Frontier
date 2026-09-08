import { describe, it, expect } from "vitest";
import {
  DEFAULT_SETTINGS,
  FOAM_QUALITIES,
  FOAM_VIEWS,
  type FrameState,
  type Settings,
} from "../types";
import {
  FoamClock,
  FOAM_STEP,
  foamProfile,
  foamDecay,
  foamCoverage,
  projectFlow,
  makeFoamAtlas,
} from "../foam/model";
import {
  FoamSystem,
  type FoamDriver,
  type FoamFormat,
  type FoamTexture,
} from "../foam/system";
import { validateSettings, encodeProject, decodeProject } from "../project";
import { packUniforms, UNIFORM_FLOATS } from "../uniforms";
import type { FoamPass } from "../foam/shaders";

const frame = (patch: Partial<Settings> = {}, time = 0): FrameState => ({
  eye: [0, 30, 15],
  forward: [0, -0.8, -0.6],
  right: [1, 0, 0],
  up: [0, 0.6, -0.8],
  width: 320,
  height: 240,
  brush: null,
  tool: "orbit",
  compare: false,
  time,
  settings: { ...DEFAULT_SETTINGS, ...patch },
});
class Driver implements FoamDriver<string> {
  supported = true;
  reason = "";
  textures = new Map<string, Float32Array>();
  operations: { kind: string; inputs?: string[]; outputs?: string[] }[] = [];
  disposed = false;
  id = 0;
  create(width: number, height: number, format: FoamFormat, _label: string) {
    const handle = `texture-${this.id++}`;
    this.textures.set(handle, new Float32Array(4));
    return { handle, width, height, format };
  }
  clear(textures: FoamTexture<string>[]) {
    for (const t of textures) this.textures.get(t.handle)!.fill(0);
    this.operations.push({
      kind: "clear",
      outputs: textures.map((t) => t.handle),
    });
  }
  pass(
    kind: FoamPass,
    outputs: FoamTexture<string>[],
    input: Map<number, string>,
  ) {
    this.operations.push({
      kind,
      inputs: [...input.values()],
      outputs: outputs.map((t) => t.handle),
    });
  }
  copy(from: FoamTexture<string>, to: FoamTexture<string>) {
    this.textures.get(to.handle)!.set(this.textures.get(from.handle)!);
    this.operations.push({ kind: "copy", outputs: [to.handle] });
  }
  destroy(texture: FoamTexture<string>) {
    expect(this.textures.delete(texture.handle)).toBe(true);
  }
  async read(texture: FoamTexture<string>) {
    return this.textures.get(texture.handle)!.slice();
  }
  dispose() {
    this.disposed = true;
  }
}

describe("bounded foam simulation model", () => {
  it("advances identical fixed time at 30, 60 and 120 display fps", () => {
    for (const rate of [30, 60, 120]) {
      const c = new FoamClock();
      let steps = c.advance(0, true);
      for (let i = 1; i <= rate * 3; i++) steps += c.advance(i / rate, true);
      expect(steps).toBe(90);
      expect(c.steps).toBe(90);
    }
  });
  it("caps catch-up work and never catches up pause/compare time", () => {
    const c = new FoamClock();
    c.advance(0, true);
    expect(c.advance(100, true)).toBe(3);
    expect(c.droppedSeconds).toBeCloseTo(99.9);
    expect(c.advance(101, false)).toBe(0);
    expect(c.advance(200, false)).toBe(0);
    expect(c.advance(200 + FOAM_STEP, true)).toBe(1);
    expect(c.advance(0, true)).toBe(0);
    expect(c.advance(0, true)).toBe(0);
    c.reset();
    expect(c.steps).toBe(0);
    expect(c.last).toBe(null);
  });
  it("uses timestep-independent exponential half-life and bounded coverage", () => {
    expect(foamDecay(2, 6, 6)).toBeCloseTo(1);
    let q = 2;
    for (let i = 0; i < 180; i++) q = foamDecay(q, FOAM_STEP, 6);
    expect(q).toBeCloseTo(1);
    expect(foamCoverage(-1)).toBe(0);
    expect(foamCoverage(0)).toBe(0);
    expect(foamCoverage(1000)).toBe(1);
    expect(foamCoverage(0.5)).toBeGreaterThan(foamCoverage(0.1));
  });
  it("blocks into-bank flow without reversing tangential or outgoing flow", () => {
    expect(projectFlow([2, 1], [-1, 0], 0)).toEqual([0, 1]);
    expect(projectFlow([-2, 1], [-1, 0], 0)).toEqual([-2, 1]);
    expect(projectFlow([2, 1], [-1, 0], 5)).toEqual([2, 1]);
  });
  it("has bounded, independent GTX/headroom profiles and GPU pool sizes", () => {
    expect(foamProfile("standard").particles).toBe(0);
    expect(foamProfile("ultra").particles).toBe(32768);
    expect(foamProfile("cinematic").particles).toBe(98304);
    for (const q of FOAM_QUALITIES)
      for (const budget of ["compact", "balanced", "expanded"] as const) {
        const p = foamProfile(q, budget);
        expect(p.flow).toBeLessThanOrEqual(256);
        expect(p.map).toBeLessThanOrEqual(1024);
        expect(p.particles).toBeLessThanOrEqual(196608);
        if (p.particles)
          expect(p.particleWidth * p.particleHeight).toBe(p.particles);
        // Two flow substeps, capped at 3m/s and depth=1.5m.
        if (q !== "low")
          expect(
            ((FOAM_STEP / 2) * (3 + Math.sqrt(9.81 * 1.5))) / (96 / p.flow),
          ).toBeLessThan(0.5);
      }
    expect(foamProfile("ultra", "compact").particles).toBe(16384);
  });
  it("builds deterministic porous atlas data and signed normal channels", () => {
    const a = makeFoamAtlas(32),
      b = makeFoamAtlas(32);
    expect(a).toEqual(b);
    expect(a.length).toBe(32 * 32 * 4);
    const height = Array.from(a).filter((_, i) => i % 4 === 3);
    expect(Math.max(...height) - Math.min(...height)).toBeGreaterThan(150);
    const normal = Array.from(a).filter((_, i) => i % 4 === 1);
    expect(Math.min(...normal)).toBeLessThan(100);
    expect(Math.max(...normal)).toBeGreaterThan(155);
  });
});

describe("portable foam settings", () => {
  it("keeps old archives on Low while new projects start Standard", () => {
    expect(DEFAULT_SETTINGS.foamQuality).toBe("standard");
    const raw: Record<string, unknown> = { ...DEFAULT_SETTINGS };
    for (const k of Object.keys(raw)) if (k.startsWith("foam")) delete raw[k];
    const old = validateSettings(raw);
    expect(old.foamQuality).toBe("low");
    expect(old.foamView).toBe("surface");
    expect(old.foamBudget).toBe("balanced");
  });
  it("validates every tier/view and rejects corrupt controls", () => {
    for (const q of FOAM_QUALITIES)
      for (const view of FOAM_VIEWS)
        expect(
          validateSettings({
            ...DEFAULT_SETTINGS,
            foamQuality: q,
            foamView: view,
          }),
        ).toMatchObject({ foamQuality: q, foamView: view });
    for (const patch of [
      { foamQuality: "RTX-only" },
      { foamQuality: ["ultra"] },
      { foamView: "particles" },
      { foamBudget: ["compact"] },
      { foamBudget: "infinite" },
      { foamLifetime: 0 },
      { foamLifetime: NaN },
      { foamLifetime: 21 },
      { foamSpray: 1.1 },
      { foamBubbles: -1 },
      { foamDetailScale: 0 },
      { foamPaused: 1 },
    ])
      expect(() =>
        validateSettings({ ...DEFAULT_SETTINGS, ...patch }),
      ).toThrow();
  });
  it("round-trips compact settings but no transient particle/cache data", async () => {
    const settings = {
      ...DEFAULT_SETTINGS,
      foamQuality: "cinematic" as const,
      foamBudget: "compact" as const,
      foamView: "age" as const,
      foamLifetime: 12,
      foamPaused: true,
    };
    const data = new Float32Array(16 * 8 * 16 * 4).fill(0.5);
    const encoded = encodeProject({
      version: 1,
      settings,
      data,
      size: { x: 16, y: 8, z: 16 },
      steps: 0,
      savedAt: new Date(0).toISOString(),
    });
    const restored = decodeProject(await encoded.arrayBuffer());
    expect(restored.settings).toEqual(settings);
    expect(restored.data).toEqual(data);
    expect(encoded.size).toBeLessThan(data.length * 2 + 8192);
  });
  it("appends aligned uniforms and disables live foam for original comparison", () => {
    const f = frame({
      foamQuality: "cinematic",
      foamView: "velocity",
      foamDetailScale: 0.5,
      foamLifetime: 8,
    });
    const packed = packUniforms(f, { x: 96, y: 48, z: 96 });
    expect(packed.length).toBe(UNIFORM_FLOATS);
    expect(UNIFORM_FLOATS).toBe(172);
    expect(Array.from(packed.slice(164, 168))).toEqual([3, 3, 0.5, 8]);
    expect(
      packUniforms({ ...f, compare: true }, { x: 96, y: 48, z: 96 })[164],
    ).toBe(0);
  });
});

describe("shared GPU foam resource graph", () => {
  it("falls back explicitly without float targets instead of breaking terrain startup", () => {
    const d = new Driver();
    d.supported = false;
    d.reason = "unsupported";
    const f = new FoamSystem(d);
    const actual = f.effectiveFrame(frame());
    expect(actual.settings.foamQuality).toBe("low");
    expect(f.diagnostics()).toMatchObject({
      requested: "standard",
      quality: "low",
      fallback: "unsupported",
    });
    expect(d.textures.size).toBe(2);
    f.dispose();
    expect(d.textures.size).toBe(0);
  });
  it("captures do not advance history; pause and compare retain it", () => {
    const d = new Driver(),
      f = new FoamSystem(d);
    const a = f.effectiveFrame(frame());
    f.update(a, "volume", false);
    expect(f.ticks).toBe(0);
    expect(d.operations.map((x) => x.kind)).not.toContain("density");
    f.update(a, "volume");
    expect(f.ticks).toBe(1);
    const count = f.ticks;
    f.update(
      { ...a, time: 8, settings: { ...a.settings, foamPaused: true } },
      "volume",
    );
    f.update({ ...a, time: 9, compare: true }, "original");
    expect(f.ticks).toBe(count);
    f.update({ ...a, time: 9 + FOAM_STEP }, "volume");
    expect(f.ticks).toBe(count + 1);
    f.dispose();
  });
  it("runs particle updates and cleared reconstruction instead of double-advection", () => {
    const d = new Driver(),
      f = new FoamSystem(d);
    const a = f.effectiveFrame(frame({ foamQuality: "ultra" }));
    f.update(a, "volume");
    expect(d.operations.map((o) => o.kind)).toContain("particles");
    expect(d.operations.map((o) => o.kind)).toContain("splat");
    expect(d.operations.map((o) => o.kind)).not.toContain("density");
    for (const op of d.operations)
      expect(op.outputs ?? []).not.toContain("volume");
    expect(f.diagnostics().particleCapacity).toBe(32768);
    f.dispose();
  });
  it("refreshes geometry after edits/water-level changes and reset clears only foam", () => {
    const d = new Driver(),
      f = new FoamSystem(d);
    let a = f.effectiveFrame(frame());
    f.update(a, "volume");
    const bakes = f.geometryBakes;
    f.invalidate();
    f.update({ ...a, time: 0.3 }, "edited-volume");
    expect(f.geometryBakes).toBe(bakes + 1);
    a = { ...a, time: 0.31, settings: { ...a.settings, waterLevel: 2 } };
    f.update(a, "edited-volume");
    expect(f.geometryBakes).toBe(bakes + 2);
    f.reset();
    f.update({ ...a, settings: { ...a.settings, foamPaused: true } }, "volume");
    expect(f.ticks).toBe(0);
    for (const op of d.operations)
      expect(op.outputs ?? []).not.toContain("volume");
    f.dispose();
  });
  it("resizes only screen targets, retains foam, and disposes old tier resources", () => {
    const d = new Driver(),
      f = new FoamSystem(d);
    const a = f.effectiveFrame(frame({ foamQuality: "cinematic" }));
    f.update(a, "volume");
    const density = f.density.handle;
    f.prepareScreen(321, 241);
    expect(f.air.width).toBe(161);
    expect(f.air.height).toBe(121);
    const before = d.textures.size;
    f.prepareScreen(640, 480);
    expect(d.textures.size).toBe(before);
    expect(f.density.handle).toBe(density);
    f.composite(
      { handle: "output", width: 640, height: 480, format: "rgba8unorm" },
      "volume",
    );
    expect(d.operations.slice(-3).map((o) => o.kind)).toEqual([
      "air",
      "bubble",
      "compose",
    ]);
    f.configure("low");
    expect(d.textures.size).toBe(2);
    expect(d.textures.has(density)).toBe(false);
    f.dispose();
    expect(d.disposed).toBe(true);
  });
  it("does not rebuild resources for ordinary lighting/lifetime changes", () => {
    const d = new Driver(),
      f = new FoamSystem(d);
    f.effectiveFrame(frame());
    const version = f.version;
    f.effectiveFrame(
      frame({ foamLifetime: 12, foamSpray: 0.5, foamPaused: true }),
    );
    expect(f.version).toBe(version);
    f.effectiveFrame(frame({ foamBudget: "expanded" }));
    expect(f.version).toBeGreaterThan(version);
    expect(f.profile.map).toBe(512);
    f.dispose();
  });
});
