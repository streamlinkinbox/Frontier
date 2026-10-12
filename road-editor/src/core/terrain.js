// Analytic terrain so the whole editor (and the city generator) can query height anywhere
// without a heightmap asset. Gentle countryside hills plus a river valley at WATER_Y.
export const WATER_Y = 0;

const clamp01 = (v) => Math.max(0, Math.min(1, v));
const smooth = (t) => t * t * (3 - 2 * t);

/** Centre line of the river as a function of x. */
export function riverZ(x) {
  return 260 * Math.sin(x / 610) + 40 * Math.sin(x / 180);
}

export function terrainY(x, z) {
  let base =
    6 +
    5 * Math.sin(x / 420 + 0.4) * Math.cos(z / 510) +
    3 * Math.sin((x + z) / 190) +
    2 * Math.cos(z / 150 - x / 260);
  const d = Math.abs(z - riverZ(x));
  const f = smooth(clamp01(1 - d / 70));
  return base * (1 - f) + -4 * f;
}

/** Terrain + water-aware "bank" height used when a bridge needs to clear the water. */
export const isWater = (x, z) => terrainY(x, z) < WATER_Y + 0.05;
