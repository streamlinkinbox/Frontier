/**
 * Deterministic PRNG (mulberry32) so a given (preset, seed) pair always yields
 * the same tree. Vital for a production pipeline: artists can iterate on a
 * specific seed and the exported asset is reproducible.
 */
export class Random {
  private s: number;

  constructor(seed: number) {
    this.s = (seed >>> 0) || 0x9e3779b9;
  }

  /** Uniform in [0, 1). */
  next(): number {
    let t = (this.s += 0x6d2b79f5);
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  }

  /** Uniform in [lo, hi). */
  range(lo: number, hi: number): number {
    return lo + (hi - lo) * this.next();
  }

  /** Uniform in [-1, 1). Matches Weber & Penn's "±V" notation: value ± variation. */
  uniform(): number {
    return this.next() * 2 - 1;
  }

  /** value ± variation (uniform). */
  vary(value: number, variation: number): number {
    return value + this.uniform() * variation;
  }

  /** Integer in [0, n). */
  int(n: number): number {
    return Math.floor(this.next() * n);
  }

  /** Fork a child stream so sub-systems do not perturb each other's sequences. */
  fork(): Random {
    return new Random(Math.floor(this.next() * 0xffffffff) ^ 0x51ed270b);
  }
}
