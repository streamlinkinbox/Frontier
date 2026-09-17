export const clamp = (v: number, a: number, b: number) => Math.min(b, Math.max(a, v));
export const lerp = (a: number, b: number, t: number) => a + (b - a) * t;

/**
 * Concave "sorimashi / terizashi" profile easing.
 * Steep at the ridge (t=0), flattening toward the eaves (t=1),
 * approximating the traditional nawadarumi (slack-rope) curve.
 * s in [0, 0.45] typical.
 */
export function easeSori(t: number, s: number): number {
  const tc = clamp(t, 0, 1);
  if (s <= 0.0001) return tc;
  const sc = clamp(s, 0, 0.9);
  return (tc - sc * tc * tc) / (1 - sc);
}

export function smoothstep(a: number, b: number, x: number): number {
  const t = clamp((x - a) / (b - a), 0, 1);
  return t * t * (3 - 2 * t);
}

/** Deterministic PRNG so a given seed always builds the identical roof. */
export function mulberry32(seed: number) {
  let a = seed >>> 0;
  return () => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
