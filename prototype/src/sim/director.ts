import { Rng } from '../core/rng';
import { fbm1 } from '../core/noise';
import { canopyPressure, speciesWeights } from './tuning';
import { clamp } from '../core/mathx';
import type { Track } from './track';
import type { VineSystem } from './vines';

/**
 * The Director stages every vine in the game.
 *
 * It owns the canopy pressure curve, the species mix and the spacing rules.
 * It never touches physics — it only issues spawn orders to the VineSystem.
 * Everything it does is aimed at one guarantee: the player always has enough
 * warning to answer what the canopy throws at them.
 */
export class Director {
  private nextSpawnS = 90;
  private rng: Rng;
  pressure = 0;

  constructor(seed: number) {
    this.rng = new Rng(seed ^ 0x5f3759df);
  }

  update(playerS: number, track: Track, vines: VineSystem, playerSpeed = 26): void {
    this.pressure = canopyPressure(playerS);
    // the horizon scales with the player's speed: a vine has to be able to
    // finish growing before the car arrives, whatever pace the run is at
    const horizon = playerS + clamp(playerSpeed * 2.8, 55, 115);

    while (this.nextSpawnS < horizon) {
      const s = this.nextSpawnS;
      const local = this.localDensity(s);
      const gap = this.gapFor(local);

      if (this.rng.float() < local) {
        this.spawnAt(s, track, vines);
      }
      this.nextSpawnS += gap;
    }

    // Retire what the player has already driven past: those vines are out of
    // sight, out of play, and would otherwise pile up for the whole run.
    for (const vine of vines.vines) {
      if (!vine.dead && vine.spawnS < playerS - 100) {
        vine.dead = true;
        vine.latch = null;
        vine.state = 'broken';
        vine.tension = 0;
      }
    }
  }

  /** Canopy cells are not uniform: some stretches are thicker than others. */
  private localDensity(s: number): number {
    const cellNoise = fbm1(s * 0.012, 3, 909) * 0.5 + 0.5; // 0..1
    const base = 0.62 + this.pressure * 0.38;
    return Math.max(0.12, Math.min(0.96, base * (0.62 + 0.62 * cellNoise)));
  }

  /**
   * Spacing rule. The gap shrinks with pressure but never below the distance
   * the player covers in the minimum telegraph window (2.0 s) — this is the
   * "guaranteed clear line" rule from the design docs.
   */
  private gapFor(local: number): number {
    const speed = 26; // m/s, typical run speed
    const minClear = speed * 2.0;
    const base = 78 - this.pressure * 44;
    return Math.max(minClear * 0.5, base * (0.6 + 0.8 * local) * this.rng.range(0.75, 1.35));
  }

  private spawnAt(s: number, track: Track, vines: VineSystem): void {
    const weights = speciesWeights(this.pressure);
    if (weights.length === 0) return;
    const species = this.rng.weighted(weights);

    const sample = track.sampleAt(s);
    // the canopy reaches over the racing line, not just the treeline
    const lateral = this.rng.range(-1, 1) * (sample.width * 0.35);

    // The canopy closes overhead as pressure rises: the roots hang lower.
    const baseHeight = 3.2 + this.rng.range(0, 1.6);
    const height = baseHeight + this.pressure * this.rng.range(0, 1.4);

    const root = sample.pos
      .clone()
      .addScaledVector(sample.side, lateral)
      .addScaledVector(sample.up, height);

    // A short germination delay keeps the forest from reacting instantly.
    const delay = this.rng.range(0.1, 0.9) * (1.35 - this.pressure * 0.7);
    const vine = vines.spawn(species, root, delay);
    vine.spawnS = s;
    vine.rootS = s;
    vine.rootLateral = lateral;
  }
}
