import { describe, expect, it } from 'vitest';
import { BARK_PROFILES, barkColour, barkHeight, makeClusterSpecs } from '../src/bark-lab/procedural';

describe('isolated bark cluster laboratory', () => {
  it('keeps every bark height and colour sample in a bake-safe range', () => {
    for (const profile of BARK_PROFILES) {
      for (let i = 0; i <= 16; i++) {
        const u = i / 16;
        const h = barkHeight(profile, u, 0.37, 17);
        const colour = barkColour(profile, h, u, 0.37, 17);
        expect(h).toBeGreaterThanOrEqual(0);
        expect(h).toBeLessThanOrEqual(1);
        expect(colour.every((channel) => channel >= 0 && channel <= 1)).toBe(true);
      }
    }
  });

  it('places deterministic, separate cluster pieces for every species profile', () => {
    for (const profile of BARK_PROFILES) {
      const first = makeClusterSpecs(profile, 17, 2.4);
      const second = makeClusterSpecs(profile, 17, 2.4);
      expect(first).toEqual(second);
      expect(first.length).toBeGreaterThan(0);
      expect(first.every((cluster) => cluster.width > 0 && cluster.height > 0 && cluster.thickness > 0)).toBe(true);
    }
  });

  it('changes the cluster layout when the seed changes', () => {
    const first = makeClusterSpecs(BARK_PROFILES[0], 17, 2.4);
    const second = makeClusterSpecs(BARK_PROFILES[0], 18, 2.4);
    expect(first).not.toEqual(second);
  });
});
