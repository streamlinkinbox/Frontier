/** Deterministic, seedable RNG (mulberry32) so every building is reproducible. */
export class Rng {
  private s: number;

  constructor(seed: number | string) {
    this.s = typeof seed === 'number' ? seed >>> 0 : Rng.hash(seed);
  }

  static hash(str: string): number {
    let h = 2166136261 >>> 0;
    for (let i = 0; i < str.length; i++) {
      h ^= str.charCodeAt(i);
      h = Math.imul(h, 16777619) >>> 0;
    }
    return h >>> 0;
  }

  /** [0, 1) */
  next(): number {
    this.s = (this.s + 0x6d2b79f5) >>> 0;
    let t = this.s;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  }

  range(min: number, max: number): number {
    return min + this.next() * (max - min);
  }

  int(min: number, max: number): number {
    return Math.floor(this.range(min, max + 1 - 1e-9));
  }

  pick<T>(items: readonly T[]): T {
    return items[Math.min(items.length - 1, Math.floor(this.next() * items.length))];
  }

  chance(p: number): boolean {
    return this.next() < p;
  }

  /** A jittered copy of the rng, so changing one seed cascades cleanly. */
  fork(salt: string): Rng {
    return new Rng((this.s ^ Rng.hash(salt)) >>> 0);
  }
}
