import { clamp } from "./math";
import type { Settings, Vec3 } from "./types";
export const FLOW_ARC_SAMPLES = 32;
export function canyonCenter(z: number, seed: number): number {
  return (
    -9.5 * Math.sin(z * 0.058) -
    3.3 * Math.sin(z * 0.117 + (seed % 8192) * 0.01)
  );
}
export function canyonSlope(z: number, seed: number): number {
  return (
    -9.5 * 0.058 * Math.cos(z * 0.058) -
    3.3 * 0.117 * Math.cos(z * 0.117 + (seed % 8192) * 0.01)
  );
}
const arcCache = new Map<number, Float32Array>();
/** Arc distance, not a z/time wobble: a constant speed follows the bends. */
export function channelArcTable(seed: number): Float32Array {
  const key = seed % 8192,
    cached = arcCache.get(key);
  if (cached) return cached;
  const table = new Float32Array(FLOW_ARC_SAMPLES),
    step = 96 / (FLOW_ARC_SAMPLES - 1);
  const speed = (z: number) => Math.hypot(1, canyonSlope(z, seed));
  let total = 0;
  for (let i = 1; i < FLOW_ARC_SAMPLES; i++) {
    const start = -48 + (i - 1) * step,
      dt = step / 8;
    for (let j = 0; j < 8; j++) {
      const a = start + j * dt,
        b = a + dt;
      total += ((speed(a) + 4 * speed((a + b) / 2) + speed(b)) * dt) / 6;
    }
    table[i] = total;
  }
  if (arcCache.size > 16) arcCache.clear();
  arcCache.set(key, table);
  return table;
}
export function channelArc(z: number, seed: number): [number, number] {
  const table = channelArcTable(seed),
    step = 96 / (FLOW_ARC_SAMPLES - 1),
    index = clamp(Math.floor((z + 48) / step), 0, FLOW_ARC_SAMPLES - 2);
  const z0 = -48 + index * step,
    t = clamp((z - z0) / step, 0, 1),
    t2 = t * t,
    t3 = t2 * t;
  const a = table[index],
    b = table[index + 1],
    m0 = Math.hypot(1, canyonSlope(z0, seed)) * step,
    m1 = Math.hypot(1, canyonSlope(z0 + step, seed)) * step;
  return [
    (2 * t3 - 3 * t2 + 1) * a +
      (t3 - 2 * t2 + t) * m0 +
      (-2 * t3 + 3 * t2) * b +
      (t3 - t2) * m1,
    ((6 * t2 - 6 * t) * a +
      (3 * t2 - 4 * t + 1) * m0 +
      (-6 * t2 + 6 * t) * b +
      (3 * t2 - 2 * t) * m1) /
      step,
  ];
}
export function followsChannel(s: Settings): boolean {
  return s.waterFlowMode === "channel" && s.preset === "canyon";
}
export function riverCoordinates(p: Vec3, s: Settings) {
  if (followsChannel(s)) {
    const sign = s.waterReverse ? 1 : -1,
      [arc, derivative] = channelArc(p[2], s.seed);
    const slope = canyonSlope(p[2], s.seed),
      length = Math.hypot(1, slope);
    return {
      along: arc * sign,
      across: p[0] - canyonCenter(p[2], s.seed),
      du: [0, derivative * sign],
      dv: [1, -slope],
      direction: [(slope / length) * sign, sign / length],
    };
  }
  const angle = (s.waterDirection * Math.PI) / 180,
    sign = s.waterReverse ? -1 : 1,
    dx = Math.cos(angle) * sign,
    dz = Math.sin(angle) * sign;
  return {
    along: p[0] * dx + p[2] * dz,
    across: -p[0] * dz + p[2] * dx,
    du: [dx, dz],
    dv: [-dz, dx],
    direction: [dx, dz],
  };
}
/** Follow a point downstream on the seeded channel for tests/flow guides. */
export function channelPointAtArc(arc: number, seed: number): Vec3 {
  let lo = -48,
    hi = 48;
  for (let i = 0; i < 30; i++) {
    const mid = (lo + hi) / 2;
    if (channelArc(mid, seed)[0] < arc) lo = mid;
    else hi = mid;
  }
  const z = (lo + hi) / 2;
  return [canyonCenter(z, seed), 0, z];
}
