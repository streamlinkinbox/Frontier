import { FOAM_QUALITIES, type FoamQuality, type Settings } from "../types";

export const FOAM_STEP = 1 / 30;
export const FOAM_PROFILES = {
  low: { flow: 1, map: 1, particleWidth: 1, particleHeight: 1, particles: 0 },
  standard: {
    flow: 128,
    map: 256,
    particleWidth: 1,
    particleHeight: 1,
    particles: 0,
  },
  ultra: {
    flow: 256,
    map: 512,
    particleWidth: 256,
    particleHeight: 128,
    particles: 32768,
  },
  cinematic: {
    flow: 256,
    map: 512,
    particleWidth: 384,
    particleHeight: 256,
    particles: 98304,
  },
} as const;
export type FoamProfile = {
  flow: number;
  map: number;
  particleWidth: number;
  particleHeight: number;
  particles: number;
};
export function foamProfile(
  quality: FoamQuality,
  budget: Settings["foamBudget"] = "balanced",
): FoamProfile {
  const base = FOAM_PROFILES[quality];
  if (quality === "low") return base;
  const factor = budget === "compact" ? 0.5 : budget === "expanded" ? 2 : 1;
  return {
    flow: Math.min(256, base.flow * factor),
    map: base.map * factor,
    particleWidth: base.particleWidth,
    particleHeight: base.particles ? base.particleHeight * factor : 1,
    particles: base.particles * factor,
  };
}
export const foamQualityIndex = (quality: FoamQuality) =>
  FOAM_QUALITIES.indexOf(quality);

/** Fixed effect clock, NOT an erosion-iteration counter. No catch-up storm on
 * resumed tabs, captures, comparison, or slow GPUs. Counts dropped effect time. */
export class FoamClock {
  last: number | null = null;
  accumulator = 0;
  steps = 0;
  droppedSeconds = 0;
  advance(time: number, enabled: boolean): number {
    const elapsed = this.last === null ? 0 : Math.max(0, time - this.last);
    this.last = time;
    if (!enabled) {
      this.accumulator = 0;
      return 0;
    }
    const dt = Math.min(elapsed, FOAM_STEP * 3);
    this.droppedSeconds += Math.max(0, elapsed - dt);
    this.accumulator += dt;
    const count = Math.min(
      3,
      Math.floor((this.accumulator + 1e-8) / FOAM_STEP),
    );
    this.accumulator -= count * FOAM_STEP;
    this.steps += count;
    return count;
  }
  reset() {
    this.last = null;
    this.accumulator = 0;
    this.steps = 0;
    this.droppedSeconds = 0;
  }
}
export const foamDecay = (q: number, dt: number, halfLife: number) =>
  q * Math.exp((-Math.LN2 * dt) / halfLife);
export const foamCoverage = (q: number) => 1 - Math.exp(-Math.max(q, 0) * 1.8);
export function projectFlow(
  velocity: readonly number[],
  normal: readonly number[],
  clearance: number,
): [number, number] {
  const into = Math.min(0, velocity[0] * normal[0] + velocity[1] * normal[1]);
  const proximity = Math.max(0, 1 - Math.max(clearance, 0) / 1.2);
  return [
    velocity[0] - into * normal[0] * proximity,
    velocity[1] - into * normal[1] * proximity,
  ];
}
/** RGBA texture: porous coverage, normal X/Z, bubble height. One-time authored
 * synthetic detail; it never determines foam birth/movement. Mips stop sparkle. */
export function makeFoamAtlas(size = 128): Uint8Array {
  let seed = 90127;
  const random = () => {
    seed ^= seed << 13;
    seed ^= seed >>> 17;
    seed ^= seed << 5;
    return (seed >>> 0) / 4294967296;
  };
  const bubbles = Array.from({ length: 120 }, () => [
    random(),
    random(),
    0.012 + random() ** 2 * 0.065,
  ]);
  const heights = new Float32Array(size * size);
  for (let y = 0; y < size; y++)
    for (let x = 0; x < size; x++) {
      let h = 0;
      for (const [bx, by, r] of bubbles) {
        const a = Math.abs((x + 0.5) / size - bx),
          b = Math.abs((y + 0.5) / size - by);
        const d = Math.hypot(Math.min(a, 1 - a), Math.min(b, 1 - b)) / r;
        h = Math.max(
          h,
          Math.sqrt(Math.max(0, 1 - d * d)) * 0.65 +
            Math.exp(-Math.pow((d - 0.88) * 8, 2)) * 0.3,
        );
      }
      heights[y * size + x] = Math.min(1, h);
    }
  const at = (x: number, y: number) =>
    heights[((y + size) % size) * size + ((x + size) % size)];
  const data = new Uint8Array(size * size * 4);
  for (let y = 0; y < size; y++)
    for (let x = 0; x < size; x++) {
      const i = (y * size + x) * 4,
        h = at(x, y);
      data[i] = Math.round((0.15 + h * 0.85) * 255);
      data[i + 1] = Math.round(
        (0.5 +
          Math.max(-0.5, Math.min(0.5, (at(x - 1, y) - at(x + 1, y)) * 0.65))) *
          255,
      );
      data[i + 2] = Math.round(
        (0.5 +
          Math.max(-0.5, Math.min(0.5, (at(x, y - 1) - at(x, y + 1)) * 0.65))) *
          255,
      );
      data[i + 3] = Math.round(h * 255);
    }
  return data;
}
