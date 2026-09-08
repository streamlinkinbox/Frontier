import { gradientNoise } from "./noise";
import { featureVisibility } from "./materials";
import type { Settings, Vec3 } from "./types";
export type ReliefSample = [number, number, number, number];

/** Band-limited, domain-warped fBm/ridge relief plus broken sedimentary layers.
 * Height/gradient only: this is surface shading, not an edit to the saved SDF. */
export function layeredRock(p: Vec3, s: Settings, footprint = 0): ReliefSample {
  if (
    s.rockRelief === 0 &&
    (s.rockLayerRelief === 0 ||
      (s.material !== "sandstone" && s.material !== "limestone"))
  )
    return [0, 0, 0, 0];
  const warp = gradientNoise(
    p[0] * 0.115 + s.seed * 0.002,
    p[1] * 0.115 + 7.9,
    p[2] * 0.115 + 3.1,
  );
  const strength = 0.35 + 0.4 * s.rockLayerWarp,
    c = [0.7 * strength, 0.17 * strength, 0.45 * strength];
  const q = [
    p[0] * 0.8 + p[2] * 0.6 + warp[0] * c[0],
    p[1] + warp[0] * c[1],
    -p[0] * 0.6 + p[2] * 0.8 + warp[0] * c[2],
  ];
  let height = 0,
    gx = 0,
    gy = 0,
    gz = 0,
    weight = 0.5,
    frequency = 1,
    total = 0;
  for (let i = 0; i < s.rockOctaves; i++) {
    total += weight;
    const f = frequency / s.rockNoiseScale,
      w = weight * featureVisibility(footprint, s.rockNoiseScale / frequency);
    if (w > 0) {
      const n = gradientNoise(
        q[0] * f + i * 4.31,
        q[1] * f * 1.3 - i * 7.17,
        q[2] * f + i * 11.63,
      );
      const absolute = Math.sqrt(n[0] * n[0] + 0.015);
      const value =
        n[0] * (1 - s.rockRidges) + (0.55 - absolute) * 1.5 * s.rockRidges;
      const derivative =
        1 - s.rockRidges - (1.5 * s.rockRidges * n[0]) / absolute;
      height += value * w;
      gx += n[1] * f * derivative * w;
      gy += n[2] * f * 1.3 * derivative * w;
      gz += n[3] * f * derivative * w;
    }
    frequency *= 2.07;
    weight *= 0.5;
  }
  const amplitude = (s.rockRelief * 0.01) / Math.max(total, 0.5);
  height *= amplitude;
  gx *= amplitude;
  gy *= amplitude;
  gz *= amplitude;
  const chain = gx * c[0] + gy * c[1] + gz * c[2];
  let dx = 0.8 * gx - 0.6 * gz + warp[1] * 0.115 * chain,
    dy = gy + warp[2] * 0.115 * chain,
    dz = 0.6 * gx + 0.8 * gz + warp[3] * 0.115 * chain;
  if (
    (s.material === "sandstone" || s.material === "limestone") &&
    s.rockLayerRelief > 0
  ) {
    const k = (Math.PI * 2) / s.rockLayerSpacing,
      w = featureVisibility(footprint, s.rockLayerSpacing * 0.35);
    const band = gradientNoise(
      p[0] * 0.65 + 13.1,
      p[1] * 0.12 + s.seed * 0.001,
      p[2] * 0.65 + 19.3,
    );
    const level =
      p[1] + p[0] * 0.025 + p[2] * 0.017 + warp[0] * s.rockLayerWarp * 0.8;
    const gp = [
      (0.025 + warp[1] * 0.115 * s.rockLayerWarp * 0.8) * k +
        band[1] * 0.65 * s.rockLayerWarp * 0.7,
      (1 + warp[2] * 0.115 * s.rockLayerWarp * 0.8) * k +
        band[2] * 0.12 * s.rockLayerWarp * 0.7,
      (0.017 + warp[3] * 0.115 * s.rockLayerWarp * 0.8) * k +
        band[3] * 0.65 * s.rockLayerWarp * 0.7,
    ];
    const phase = level * k + band[0] * s.rockLayerWarp * 0.7,
      second = phase * 2.07 + band[0] * 0.4;
    const wave = Math.sin(phase) + 0.25 * Math.sin(second),
      t = Math.max(0, Math.min(1, (wave + 0.25) / 1.1));
    const a = s.rockLayerRelief * 0.01 * w,
      scale = ((6 * t * (1 - t)) / 1.1) * a;
    const d = Math.cos(phase) + 0.25 * 2.07 * Math.cos(second),
      extra = 0.25 * Math.cos(second) * 0.4;
    height += (t * t * (3 - 2 * t) - 0.5) * a;
    dx += (gp[0] * d + band[1] * 0.65 * extra) * scale;
    dy += (gp[1] * d + band[2] * 0.12 * extra) * scale;
    dz += (gp[2] * d + band[3] * 0.65 * extra) * scale;
  }
  return [height, dx, dy, dz];
}
