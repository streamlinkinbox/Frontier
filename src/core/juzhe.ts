/**
 * juzhe.ts — 舉折 jǔzhé, the Yingzao Fashi roof-curve solver.
 *
 * This is the single source of truth for roof shape. Everything else in the
 * generator derives from it, which is why nothing can float or mismatch.
 *
 * Sources (see docs/roof-research.md):
 *   - Yingzao Fashi (李誡 Li Jie, 1103), juan 5 大木作制度 舉折之制
 *   - Architectura Sinica, "jǔzhé 舉折" (k000223)
 *   - Shen et al. 2020, "Parameterizing the Curvilinear Roofs of Traditional
 *     Chinese Architecture"
 *   - Computing Chinese Architecture, Springer 2025, ch. 24
 *
 * THE ALGORITHM (performed in this order):
 *
 *   舉屋 jǔwū — "raise the roof":  ridge height  H0 = riseRatio * B
 *        where B = distance between the CENTRE LINES OF THE FRONT AND REAR
 *        EAVE PURLINS (not the eave tips!). riseRatio = 1/3 (palace class),
 *        1/4 (hall class), 1/5 (small).
 *
 *   折屋 zhéwū — "bend the roof":  divide the half span B/2 into m equal
 *        horizontal steps (one per rafter bay, m <= 7). Drop the purlin
 *        directly below the ridge by H0/10 off the straight ridge→eave working
 *        line. Then re-draw the working line from that purlin to the eave and
 *        drop the next purlin by half the previous drop. Repeat.
 *
 *        Closed form: H_n = ((m-n)/(m-n+1))·H_(n-1) − (1/2)^(n-1)·(1/10)·H0
 *
 * The eave purlin (n = m) is the datum and sits at height 0 by definition — the
 * closed form overshoots it very slightly, so we clamp the final node. That
 * residual is absorbed by the last rafter bay, which is exactly what happens on
 * a real building, because the eave purlin is fixed by the columns and cannot
 * move.
 *
 * The purlins form a CONCAVE POLYLINE, and the rafters are straight timbers
 * spanning between consecutive purlins. It is deliberately not a spline: the
 * curve is a series of straight rafters, which is why the profile is polygonal.
 */

export interface JuzheSpec {
  /** B/2 — plan distance from the eave purlin centre line to the ridge. */
  halfSpan: number;
  /** H0 / B — 1/3 palace, 1/4 hall, 1/5 small. */
  riseRatio: number;
  /** m — number of rafter bays (horizontal steps) from eave purlin to ridge. Max 7 per YZS. */
  steps: number;
  /** Total eave projection from the eave purlin centre to the eave tip (flying rafter head). */
  eaveProjection: number;
  /**
   * Split of the eave projection between the eave rafter (檐椽) and the flying
   * rafter (飛椽). YZS: "an additional flying rafter should extend 6 cun
   * outward for every 1 chi of eave projection" ⇒ flying = 0.6 × eave rafter.
   */
  flyOverEave: number;
  /**
   * Slope of the flying rafter as a multiple of the average ridge→eave slope.
   * 1.0 is the physical baseline (flying rafter parallel to the mean slope);
   * real buildings push it a little steeper, which is what produces the
   * pronounced flying eave. The kink between eave rafter and flying rafter is
   * the origin of the concave eave line (MDPI Buildings 15(14):2582 §3.1).
   */
  flyPitch: number;
}

export interface PurlinNode {
  /** Index from the eave: 0 = eave purlin (檐檩), steps = ridge (脊槫). */
  index: number;
  /** Plan distance out from the eave purlin centre line. Negative = outboard. */
  q: number;
  /** Height above the eave purlin top. */
  h: number;
  /** Normalised position 0 (eave) … 1 (ridge) — the key for face parametrisation. */
  t: number;
}

export interface ProfileNode {
  q: number;
  h: number;
  /** true when the node is a structural purlin (as opposed to an eave node). */
  purlin: boolean;
}

export interface JuzheResult {
  /** Ridge rise above the eave purlin. */
  H0: number;
  /** m — the number of rafter bays actually used (1..7). */
  steps: number;
  /** Span used, i.e. 2*halfSpan. */
  B: number;
  /** Horizontal length of one rafter bay. */
  stepRun: number;
  /** Purlins ordered from the eave (index 0) up to the ridge (index steps). */
  purlins: PurlinNode[];
  /** Purlin just above the eave — its slope defines the eave rafter's direction. */
  slopeEave: number;
  /** Mean ridge→eave slope, H0 / halfSpan. */
  slopeMean: number;
  /**
   * Slope of the flying-rafter segment, as a positive rise-over-run measured
   * OUTBOARD. The profile array is ordered by ascending q, so within the array
   * this segment has slope −slopeFly: the flying eave rises as you go out.
   */
  slopeFly: number;
  /** Height of the eave tip (flying rafter head) relative to the eave purlin top. */
  tipRise: number;
  /** Plan distance from the eave purlin out to the 小連檐 (eave rafter head). */
  eaveRafterRun: number;
  /** Plan distance from the eave purlin out to the eave tip (flying rafter head). */
  eaveTipRun: number;
  /** Full profile polyline, ascending q, from the eave tip inboard to the ridge. */
  profile: ProfileNode[];
  /** Total pitch eave→ridge as rise:run, e.g. 2.0 means 1:2. */
  pitch: number;
}

/** Drop amount for the n-th purlin below the ridge (n starting at 1). */
function dropFor(n: number, H0: number): number {
  return Math.pow(0.5, n - 1) * (H0 / 10);
}

export function solveJuzhe(spec: JuzheSpec): JuzheResult {
  const { halfSpan, riseRatio, eaveProjection, flyOverEave, flyPitch } = spec;
  const steps = Math.max(1, Math.min(7, Math.round(spec.steps)));

  if (!(halfSpan > 0)) throw new Error('juzhe: halfSpan must be > 0');

  const B = halfSpan * 2;
  const H0 = riseRatio * B;
  const stepRun = halfSpan / steps;

  // ---- 舉屋 jǔwū is done (H0). Now 折屋 zhéwū, iteratively, exactly as written.
  // heightsFromRidge[k]: k = 0 is the ridge, k = steps is the eave purlin.
  const heightsFromRidge = new Array<number>(steps + 1);
  heightsFromRidge[0] = H0;
  for (let k = 1; k <= steps; k++) {
    // Working line from the previously placed purlin (k-1) straight down to the
    // eave purlin at height 0. It spans (steps - k + 1) horizontal steps.
    const parent = heightsFromRidge[k - 1];
    const onWorkingLine = parent * ((steps - k) / (steps - k + 1));
    const h = onWorkingLine - dropFor(k, H0);
    // The eave purlin is the datum: it is held by the columns at 0.
    heightsFromRidge[k] = k === steps ? 0 : h;
  }

  // ---- purlins indexed from the eave upward
  const purlins: PurlinNode[] = [];
  for (let i = 0; i <= steps; i++) {
    const k = steps - i; // distance from the ridge, in steps
    purlins.push({
      index: i,
      q: i * stepRun,
      h: heightsFromRidge[k],
      t: i / steps,
    });
  }

  const slopeEave = purlins[1].h / stepRun; // lowest rafter bay
  const slopeMean = H0 / halfSpan;

  // ---- eave construction: eave rafter (round) + flying rafter (square) nailed
  // to its back. fly = flyOverEave × eave rafter, using the YZS 6-cun-per-chi rule.
  const eaveRafterRun = eaveProjection / (1 + flyOverEave);
  const flyRun = eaveProjection - eaveRafterRun;

  // The eave rafter simply continues the lowest bay's slope outboard.
  const eaveRafterHeadH = -eaveRafterRun * slopeEave;
  const slopeFly = slopeMean * flyPitch;
  const tipRise = eaveRafterHeadH + flyRun * slopeFly;

  // ---- full profile polyline (ascending q)
  const profile: ProfileNode[] = [
    { q: -eaveProjection, h: tipRise, purlin: false }, // eave tip (瓦當/滴水 row)
    { q: -eaveRafterRun, h: eaveRafterHeadH, purlin: false }, // 小連檐 kink
    ...purlins.map((p) => ({ q: p.q, h: p.h, purlin: true })),
  ];

  // pitch as rise:run (Fashi practice lands between 1:3 and 1:1.5)
  const pitch = H0 / halfSpan;

  return {
    H0,
    steps,
    B,
    stepRun,
    purlins,
    slopeEave,
    slopeMean,
    slopeFly,
    tipRise,
    eaveRafterRun,
    eaveTipRun: eaveProjection,
    profile,
    pitch,
  };
}

/**
 * Evaluate the juzhe profile at plan distance q out from the eave purlin centre.
 * Exactly linear between nodes — the roof is a polyline of straight rafters.
 */
export function evalProfile(res: JuzheResult, q: number): number {
  const p = res.profile;
  if (q <= p[0].q) {
    // Outboard of the tip: extend the flying rafter's own line so nothing ever
    // dangles below the surface. The profile is ordered by ascending q, so this
    // segment runs as −slopeFly (the flying eave RISES going outward).
    return p[0].h - (q - p[0].q) * res.slopeFly;
  }
  const last = p[p.length - 1];
  if (q >= last.q) return last.h + (q - last.q) * res.slopeMean;

  for (let i = 0; i < p.length - 1; i++) {
    const a = p[i];
    const b = p[i + 1];
    if (q >= a.q && q <= b.q) {
      const s = b.q === a.q ? 0 : (q - a.q) / (b.q - a.q);
      return a.h + s * (b.h - a.h);
    }
  }
  return last.h;
}

/** Slope of the profile at q (used to orient tiles and rafters). */
export function profileSlope(res: JuzheResult, q: number): number {
  const p = res.profile;
  if (q <= p[0].q) return -res.slopeFly;
  const last = p[p.length - 1];
  if (q >= last.q) return res.slopeMean;
  for (let i = 0; i < p.length - 1; i++) {
    const a = p[i];
    const b = p[i + 1];
    if (q >= a.q && q <= b.q) return b.q === a.q ? 0 : (b.h - a.h) / (b.q - a.q);
  }
  return res.slopeMean;
}

/**
 * Solve for the plan distance from the eave purlin at which the profile reaches
 * a given height. Used by 歇山/入母屋 to place the break line exactly on the
 * skirt, and by the purlin/frame builder.
 */
export function qAtHeight(res: JuzheResult, h: number): number {
  const p = res.profile;
  if (h <= p[0].h) return p[0].q;
  const last = p[p.length - 1];
  if (h >= last.h) return last.q;
  for (let i = 0; i < p.length - 1; i++) {
    const a = p[i];
    const b = p[i + 1];
    const lo = Math.min(a.h, b.h);
    const hi = Math.max(a.h, b.h);
    if (h >= lo && h <= hi) {
      const s = hi === lo ? 0 : (h - a.h) / (b.h - a.h);
      return a.q + s * (b.q - a.q);
    }
  }
  return last.q;
}

/**
 * Self-test: the Fashi's own numbers. With B = 10 and riseRatio = 1/3 the
 * published worked example (H0 = 200 cun, first drop 20 cun, second 10 cun)
 * must reproduce.
 */
export function selfTest(): string[] {
  const out: string[] = [];
  const res = solveJuzhe({
    halfSpan: 5,
    riseRatio: 1 / 3,
    steps: 7,
    eaveProjection: 1,
    flyOverEave: 0.6,
    flyPitch: 1.2,
  });
  // H0 = (1/3)*10 = 3.333…  => scale so H0 = 200 cun
  const k = 200 / res.H0;
  const drops = res.purlins
    .map((_, i) => i)
    .slice(0, res.purlins.length - 1)
    .map((i) => res.purlins[i]);
  out.push(`H0 = ${res.H0.toFixed(4)} (in cun: ${(res.H0 * k).toFixed(1)})`);
  for (let n = 1; n <= 2; n++) {
    const Hn = res.purlins[res.purlins.length - 1 - n].h;
    const prevOnLine = res.purlins[res.purlins.length - n].h * ((res.steps - n) / (res.steps - n + 1));
    out.push(`drop #${n} = ${((prevOnLine - Hn) * k).toFixed(2)} cun (expect ${(dropFor(n, 200)).toFixed(2)})`);
  }
  out.push(`pitch 1:${(1 / res.pitch).toFixed(2)} (expect between 1:1.5 and 1:3)`);
  out.push(`eave tip rise ${res.tipRise.toFixed(3)} m (should be near/below 0, not floating)`);
  void drops;
  return out;
}
