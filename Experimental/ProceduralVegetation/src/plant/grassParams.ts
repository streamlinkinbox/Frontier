/**
 * Grass parameters and species presets.
 *
 * A grass plant is described botanically: a crown (the tussock base) that
 * bears leaf blades and flowering culms; culms carry a leaf at each node and
 * end in an inflorescence. Lengths are metres, angles degrees.
 */

export type Inflorescence = 'none' | 'spike' | 'foxtail' | 'panicle' | 'plume';

export const INFLORESCENCE_NAMES: Record<Inflorescence, string> = {
  none: 'None (vegetative)',
  spike: 'Spike – wheat, barley',
  foxtail: 'Foxtail – dense bottlebrush',
  panicle: 'Open panicle – oat, meadow grass',
  plume: 'Plume – pampas, reed',
};

/** What the "branches" of the head are, per inflorescence type (UI labels). */
export const HEAD_BRANCH_NAMES: Record<Inflorescence, string> = {
  none: 'Branches',
  spike: 'Spikelets',
  foxtail: 'Bristles',
  panicle: 'Branchlets',
  plume: 'Branchlets',
};

export interface GrassParams {
  // ---- Crown & habit -------------------------------------------------------
  /** Radius of the crown (tussock base) at ground level. */
  crownRadius: number;
  /** Height of the crown dome above the soil (0 = flat pad). */
  crownHeight: number;
  /** How far the crown is sunk into the soil; blades then emerge straight from the ground. */
  crownSink: number;
  /** Dome profile exponent: 2 = rounded, higher = flat top with a rounded shoulder. */
  crownProfile: number;
  /** Radial distribution of the tillers: -1 a ring at the rim, 0 even, 1 crowded at the centre. */
  centreBias: number;

  // ---- Leaf blades from the crown -----------------------------------------
  blades: number;
  bladeLength: number;
  /** ± fraction. */
  bladeLengthV: number;
  bladeWidth: number;
  /** Blade thickness as a fraction of its width. */
  bladeThickness: number;
  /** 0 tapers evenly from the base, 1 keeps its width and ends in a fine point. */
  bladeTaper: number;
  /** Depth of the V-fold along the midrib as a fraction of the width. */
  keel: number;
  /** 0 flat blade, 1 rolled bristle-like blade (fescue, marram). */
  rolled: number;
  /** Twist along the blade, degrees (± twistV). */
  twist: number;
  twistV: number;
  /** Tilt from the vertical at the rim of the crown (the centre stays upright), degrees. */
  lean: number;
  leanV: number;
  /** Arch from base to tip, degrees. */
  droop: number;
  droopV: number;
  /** 1 bends evenly, higher keeps the base stiff and lets the tip hang. */
  droopPower: number;
  /** Lateral waviness, degrees. */
  wave: number;

  // ---- Culms (flowering stems) ---------------------------------------------
  culms: number;
  culmHeight: number;
  culmHeightV: number;
  culmRadius: number;
  culmLean: number;
  culmLeanV: number;
  /** Bend along the culm, degrees. */
  culmCurve: number;
  /** Leaf nodes per culm. */
  nodes: number;
  /** Relative swelling of the culm at the nodes. */
  nodeSwell: number;
  /** Culm leaf length relative to the basal blades. */
  culmLeafLength: number;
  /** Culm leaf width relative to the basal blades. */
  culmLeafWidth: number;
  /** Angle between a culm leaf and the culm, degrees. */
  culmLeafAngle: number;

  // ---- Inflorescence -------------------------------------------------------
  head: Inflorescence;
  headLength: number;
  /** Thickness of a spike or foxtail; panicles and plumes spread by their branchlet angle instead. */
  headWidth: number;
  /** Nod of the head, degrees. */
  headDroop: number;
  /** Spikelets (spike), bristles (foxtail) or branchlets (panicle, plume). */
  headBranches: number;
  /** Branchlet length relative to the head length (panicle, plume). */
  headBranchLength: number;
  /** Angle between a branchlet and the rachis, degrees. */
  headBranchAngle: number;
  /** Arch of a branchlet, degrees. */
  headBranchDroop: number;
  /** Fraction of the head length (from its base) over which branchlets attach. */
  headBranchSpread: number;
  /** Branchlet radius. */
  headBranchRadius: number;
  /** Secondary branchlets per branchlet (panicle, plume). */
  headSecondary: number;
  /** Spikelet radius: the body of a spike's spikelets, the bulge at the tip of a panicle branchlet (0 = none). */
  spikelet: number;
  /** Awn length (0 = none): from each spikelet of a spike, beyond the spikelet of a panicle branchlet. */
  awn: number;

  // ---- Mesh resolution -----------------------------------------------------
  /** Rings along a blade. */
  bladeSegments: number;
  /** Vertices around a blade: 4 flat strip, 6 keeled, 8 keeled and wide. */
  bladeSides: number;
  /** Vertices around a culm (6, 8, 10 or 12). */
  culmSides: number;
  /** Rings along a culm (below the head). */
  culmSegments: number;
  /** Rings along a branchlet or bristle. */
  branchletSegments: number;
  /** Intermediate loops between a window and the first ring of the tiller. */
  collarRings: number;
}

export const DEFAULT_GRASS: GrassParams = {
  crownRadius: 0.12,
  crownHeight: 0.03,
  crownSink: 0.01,
  crownProfile: 2.5,
  centreBias: 0,

  blades: 180,
  bladeLength: 0.32,
  bladeLengthV: 0.3,
  bladeWidth: 0.004,
  bladeThickness: 0.12,
  bladeTaper: 0.6,
  keel: 0.4,
  rolled: 0,
  twist: 60,
  twistV: 60,
  lean: 32,
  leanV: 12,
  droop: 70,
  droopV: 30,
  droopPower: 1.8,
  wave: 6,

  culms: 14,
  culmHeight: 0.6,
  culmHeightV: 0.15,
  culmRadius: 0.0015,
  culmLean: 8,
  culmLeanV: 6,
  culmCurve: 6,
  nodes: 2,
  nodeSwell: 0.15,
  culmLeafLength: 0.55,
  culmLeafWidth: 1,
  culmLeafAngle: 35,

  head: 'panicle',
  headLength: 0.12,
  headWidth: 0.07,
  headDroop: 10,
  headBranches: 24,
  headBranchLength: 0.4,
  headBranchAngle: 40,
  headBranchDroop: 15,
  headBranchSpread: 0.85,
  headBranchRadius: 0.0005,
  headSecondary: 0,
  spikelet: 0.002,
  awn: 0,

  bladeSegments: 8,
  bladeSides: 6,
  culmSides: 6,
  culmSegments: 14,
  branchletSegments: 3,
  collarRings: 1,
};

/** Approximate height of a grass plant (m), for the library metadata and framing. */
export function grassHeight(g: GrassParams): number {
  const bladeTop = Math.max(0, g.crownHeight - g.crownSink) + g.bladeLength * 0.85;
  const culmTop = g.culms > 0 ? g.culmHeight * (1 + g.culmHeightV * 0.5) + (g.head === 'none' ? 0 : g.headLength) : 0;
  return Math.max(0.03, bladeTop, culmTop);
}

/** Habit word used in the species metadata. */
export function grassHabit(g: GrassParams): string {
  if (g.crownSink >= g.crownHeight && g.crownRadius > 0.08 && g.culms < 8) return 'turf';
  if (g.crownRadius < 0.05) return 'single culms';
  if (g.crownRadius >= 0.25) return 'large tussock';
  return 'tussock';
}

/** Species presets. Values are typical field measurements of the species. */
export const GRASS_PRESETS: { name: string; grass: Partial<GrassParams> }[] = [
  // ---- turf & meadow --------------------------------------------------------
  {
    name: 'Lawn Turf',
    grass: {
      crownRadius: 0.14,
      crownHeight: 0.012,
      crownSink: 0.01,
      crownProfile: 3,
      blades: 330,
      bladeLength: 0.09,
      bladeLengthV: 0.3,
      bladeWidth: 0.0035,
      bladeThickness: 0.12,
      bladeTaper: 0.6,
      keel: 0.35,
      twist: 40,
      twistV: 60,
      lean: 28,
      leanV: 12,
      droop: 30,
      droopV: 20,
      droopPower: 1.6,
      wave: 4,
      culms: 0,
      head: 'none',
      bladeSegments: 6,
      bladeSides: 4,
    },
  },
  {
    name: 'Meadow Grass',
    grass: { headBranches: 28, headSecondary: 2 }, // otherwise the defaults: Poa pratensis
  },
  {
    name: 'Timothy',
    grass: {
      crownRadius: 0.1,
      crownHeight: 0.03,
      crownSink: 0.01,
      blades: 120,
      bladeLength: 0.3,
      bladeLengthV: 0.3,
      bladeWidth: 0.007,
      keel: 0.3,
      twist: 50,
      twistV: 50,
      lean: 30,
      droop: 60,
      droopV: 25,
      culms: 16,
      culmHeight: 0.9,
      culmHeightV: 0.12,
      culmRadius: 0.002,
      culmLean: 6,
      culmLeanV: 5,
      culmCurve: 5,
      nodes: 3,
      nodeSwell: 0.2,
      culmLeafLength: 0.6,
      culmLeafAngle: 30,
      head: 'foxtail',
      headLength: 0.09,
      headWidth: 0.009,
      headDroop: 4,
      headBranches: 140,
      headBranchAngle: 60,
      headBranchDroop: 10,
      headBranchSpread: 1,
      headBranchRadius: 0.0004,
      culmSides: 8,
      branchletSegments: 2,
    },
  },
  {
    name: 'Red Oat Grass',
    grass: {
      crownRadius: 0.12,
      crownHeight: 0.05,
      crownSink: 0,
      blades: 160,
      bladeLength: 0.35,
      bladeLengthV: 0.35,
      bladeWidth: 0.004,
      keel: 0.3,
      rolled: 0.3,
      twist: 90,
      twistV: 90,
      lean: 30,
      leanV: 12,
      droop: 55,
      droopV: 30,
      droopPower: 1.7,
      culms: 22,
      culmHeight: 0.9,
      culmHeightV: 0.2,
      culmRadius: 0.0018,
      culmLean: 10,
      culmLeanV: 6,
      culmCurve: 12,
      nodes: 3,
      nodeSwell: 0.18,
      culmLeafLength: 0.5,
      culmLeafAngle: 30,
      head: 'panicle',
      headLength: 0.28,
      headWidth: 0.12,
      headDroop: 25,
      headBranches: 12,
      headBranchLength: 0.35,
      headBranchAngle: 35,
      headBranchDroop: 70,
      headBranchSpread: 0.9,
      headBranchRadius: 0.0006,
      headSecondary: 2,
      spikelet: 0.003,
      awn: 0.05,
      culmSides: 6,
      branchletSegments: 4,
    },
  },

  // ---- tussocks -------------------------------------------------------------
  {
    name: 'Blue Fescue',
    grass: {
      crownRadius: 0.11,
      crownHeight: 0.025,
      crownSink: 0.01,
      crownProfile: 2,
      centreBias: -0.2,
      blades: 900,
      bladeLength: 0.22,
      bladeLengthV: 0.25,
      bladeWidth: 0.0022,
      bladeThickness: 0.5,
      bladeTaper: 0.5,
      keel: 0,
      rolled: 0.85,
      twist: 0,
      twistV: 0,
      lean: 50,
      leanV: 15,
      droop: 55,
      droopV: 30,
      droopPower: 1.5,
      wave: 5,
      culms: 18,
      culmHeight: 0.42,
      culmHeightV: 0.1,
      culmRadius: 0.001,
      culmLean: 12,
      culmLeanV: 8,
      culmCurve: 10,
      nodes: 1,
      nodeSwell: 0.1,
      culmLeafLength: 0.4,
      culmLeafAngle: 25,
      head: 'panicle',
      headLength: 0.08,
      headWidth: 0.02,
      headDroop: 8,
      headBranches: 10,
      headBranchLength: 0.3,
      headBranchAngle: 20,
      headBranchDroop: 10,
      headBranchSpread: 0.8,
      headBranchRadius: 0.0004,
      spikelet: 0.0018,
      awn: 0.004,
      bladeSegments: 7,
      bladeSides: 4,
      culmSides: 6,
    },
  },
  {
    name: 'Fountain Grass',
    grass: {
      crownRadius: 0.16,
      crownHeight: 0.07,
      crownSink: 0,
      blades: 300,
      bladeLength: 0.55,
      bladeLengthV: 0.25,
      bladeWidth: 0.004,
      keel: 0.35,
      rolled: 0.3,
      twist: 120,
      twistV: 90,
      lean: 40,
      leanV: 12,
      droop: 110,
      droopV: 30,
      droopPower: 1.6,
      wave: 6,
      culms: 30,
      culmHeight: 0.8,
      culmHeightV: 0.15,
      culmRadius: 0.0016,
      culmLean: 22,
      culmLeanV: 8,
      culmCurve: 35,
      nodes: 2,
      nodeSwell: 0.12,
      culmLeafLength: 0.5,
      culmLeafAngle: 30,
      head: 'foxtail',
      headLength: 0.22,
      headWidth: 0.04,
      headDroop: 45,
      headBranches: 220,
      headBranchAngle: 50,
      headBranchDroop: 25,
      headBranchSpread: 1,
      headBranchRadius: 0.0006,
      bladeSegments: 10,
      culmSides: 8,
      branchletSegments: 3,
    },
  },
  {
    name: 'Marram Grass',
    grass: {
      crownRadius: 0.14,
      crownHeight: 0.04,
      crownSink: 0.02,
      blades: 200,
      bladeLength: 0.7,
      bladeLengthV: 0.25,
      bladeWidth: 0.004,
      bladeThickness: 0.35,
      bladeTaper: 0.5,
      keel: 0.1,
      rolled: 0.8,
      twist: 0,
      twistV: 20,
      lean: 18,
      leanV: 10,
      droop: 25,
      droopV: 15,
      droopPower: 2,
      wave: 4,
      culms: 12,
      culmHeight: 1.0,
      culmHeightV: 0.15,
      culmRadius: 0.0025,
      culmLean: 5,
      culmLeanV: 4,
      culmCurve: 4,
      nodes: 2,
      nodeSwell: 0.12,
      culmLeafLength: 0.6,
      culmLeafAngle: 20,
      head: 'foxtail',
      headLength: 0.2,
      headWidth: 0.014,
      headDroop: 3,
      headBranches: 90,
      headBranchAngle: 30,
      headBranchDroop: 5,
      headBranchSpread: 1,
      headBranchRadius: 0.0005,
      bladeSegments: 9,
      bladeSides: 4,
      culmSides: 8,
      branchletSegments: 2,
    },
  },
  {
    name: 'Chinese Silver Grass',
    grass: {
      crownRadius: 0.3,
      crownHeight: 0.15,
      crownSink: 0,
      blades: 360,
      bladeLength: 1.0,
      bladeLengthV: 0.25,
      bladeWidth: 0.012,
      bladeThickness: 0.08,
      bladeTaper: 0.75,
      keel: 0.3,
      twist: 90,
      twistV: 90,
      lean: 25,
      leanV: 12,
      droop: 90,
      droopV: 35,
      droopPower: 2,
      wave: 5,
      culms: 40,
      culmHeight: 1.9,
      culmHeightV: 0.2,
      culmRadius: 0.004,
      culmLean: 8,
      culmLeanV: 5,
      culmCurve: 8,
      nodes: 4,
      nodeSwell: 0.12,
      culmLeafLength: 0.5,
      culmLeafAngle: 40,
      head: 'plume',
      headLength: 0.25,
      headWidth: 0.12,
      headDroop: 20,
      headBranches: 24,
      headBranchLength: 0.9,
      headBranchAngle: 22,
      headBranchDroop: 35,
      headBranchSpread: 0.25,
      headBranchRadius: 0.0008,
      headSecondary: 4,
      bladeSegments: 14,
      bladeSides: 6,
      culmSides: 8,
      culmSegments: 20,
      branchletSegments: 4,
    },
  },
  {
    name: 'Pampas Grass',
    grass: {
      crownRadius: 0.45,
      crownHeight: 0.25,
      crownSink: 0,
      crownProfile: 2.5,
      blades: 520,
      bladeLength: 1.5,
      bladeLengthV: 0.3,
      bladeWidth: 0.012,
      bladeThickness: 0.06,
      bladeTaper: 0.85,
      keel: 0.35,
      twist: 150,
      twistV: 90,
      lean: 35,
      leanV: 15,
      droop: 140,
      droopV: 30,
      droopPower: 1.7,
      wave: 6,
      culms: 24,
      culmHeight: 2.4,
      culmHeightV: 0.12,
      culmRadius: 0.007,
      culmLean: 6,
      culmLeanV: 4,
      culmCurve: 4,
      nodes: 3,
      nodeSwell: 0.1,
      culmLeafLength: 0.35,
      culmLeafAngle: 30,
      head: 'plume',
      headLength: 0.5,
      headWidth: 0.14,
      headDroop: 10,
      headBranches: 100,
      headBranchLength: 0.22,
      headBranchAngle: 25,
      headBranchDroop: 20,
      headBranchSpread: 1,
      headBranchRadius: 0.001,
      headSecondary: 2,
      bladeSegments: 16,
      bladeSides: 6,
      culmSides: 8,
      culmSegments: 24,
      branchletSegments: 3,
    },
  },

  // ---- cereals & reeds ------------------------------------------------------
  {
    name: 'Wheat',
    grass: {
      crownRadius: 0.035,
      crownHeight: 0.01,
      crownSink: 0.01,
      blades: 6,
      bladeLength: 0.25,
      bladeLengthV: 0.2,
      bladeWidth: 0.012,
      bladeThickness: 0.06,
      bladeTaper: 0.7,
      keel: 0.25,
      twist: 40,
      twistV: 40,
      lean: 30,
      leanV: 10,
      droop: 60,
      droopV: 20,
      culms: 5,
      culmHeight: 0.85,
      culmHeightV: 0.06,
      culmRadius: 0.002,
      culmLean: 4,
      culmLeanV: 3,
      culmCurve: 5,
      nodes: 4,
      nodeSwell: 0.25,
      culmLeafLength: 0.8,
      culmLeafWidth: 1.1,
      culmLeafAngle: 40,
      head: 'spike',
      headLength: 0.09,
      headWidth: 0.012,
      headDroop: 15,
      headBranches: 18,
      headBranchAngle: 22,
      headBranchDroop: 5,
      headBranchRadius: 0.0004,
      spikelet: 0.0028,
      awn: 0.03,
      bladeSegments: 8,
      culmSides: 8,
      culmSegments: 16,
      branchletSegments: 3,
    },
  },
  {
    name: 'Barley',
    grass: {
      crownRadius: 0.035,
      crownHeight: 0.01,
      crownSink: 0.01,
      blades: 6,
      bladeLength: 0.24,
      bladeLengthV: 0.2,
      bladeWidth: 0.014,
      bladeThickness: 0.06,
      bladeTaper: 0.7,
      keel: 0.25,
      twist: 40,
      twistV: 40,
      lean: 32,
      leanV: 10,
      droop: 65,
      droopV: 20,
      culms: 5,
      culmHeight: 0.8,
      culmHeightV: 0.06,
      culmRadius: 0.002,
      culmLean: 4,
      culmLeanV: 3,
      culmCurve: 5,
      nodes: 4,
      nodeSwell: 0.25,
      culmLeafLength: 0.75,
      culmLeafWidth: 1.1,
      culmLeafAngle: 42,
      head: 'spike',
      headLength: 0.08,
      headWidth: 0.011,
      headDroop: 70,
      headBranches: 22,
      headBranchAngle: 20,
      headBranchDroop: 4,
      headBranchRadius: 0.00035,
      spikelet: 0.0022,
      awn: 0.14,
      bladeSegments: 8,
      culmSides: 8,
      culmSegments: 16,
      branchletSegments: 4,
    },
  },
  {
    name: 'Oat',
    grass: {
      crownRadius: 0.035,
      crownHeight: 0.01,
      crownSink: 0.01,
      blades: 6,
      bladeLength: 0.28,
      bladeLengthV: 0.2,
      bladeWidth: 0.012,
      bladeThickness: 0.06,
      bladeTaper: 0.7,
      keel: 0.25,
      twist: 40,
      twistV: 40,
      lean: 30,
      leanV: 10,
      droop: 60,
      droopV: 20,
      culms: 5,
      culmHeight: 1.1,
      culmHeightV: 0.08,
      culmRadius: 0.0022,
      culmLean: 5,
      culmLeanV: 3,
      culmCurve: 8,
      nodes: 4,
      nodeSwell: 0.22,
      culmLeafLength: 0.8,
      culmLeafWidth: 1.1,
      culmLeafAngle: 40,
      head: 'panicle',
      headLength: 0.25,
      headWidth: 0.18,
      headDroop: 12,
      headBranches: 26,
      headBranchLength: 0.45,
      headBranchAngle: 45,
      headBranchDroop: 60,
      headBranchSpread: 0.85,
      headBranchRadius: 0.0005,
      headSecondary: 2,
      spikelet: 0.004,
      awn: 0.02,
      bladeSegments: 8,
      culmSides: 8,
      culmSegments: 16,
      branchletSegments: 4,
    },
  },
  {
    name: 'Common Reed',
    grass: {
      crownRadius: 0.06,
      crownHeight: 0.02,
      crownSink: 0.02,
      blades: 4,
      bladeLength: 0.45,
      bladeLengthV: 0.2,
      bladeWidth: 0.025,
      bladeThickness: 0.05,
      bladeTaper: 0.7,
      keel: 0.2,
      twist: 30,
      twistV: 40,
      lean: 30,
      leanV: 10,
      droop: 50,
      droopV: 20,
      droopPower: 1.6,
      culms: 3,
      culmHeight: 3.0,
      culmHeightV: 0.1,
      culmRadius: 0.007,
      culmLean: 3,
      culmLeanV: 2,
      culmCurve: 6,
      nodes: 9,
      nodeSwell: 0.12,
      culmLeafLength: 1.0,
      culmLeafWidth: 1,
      culmLeafAngle: 45,
      head: 'plume',
      headLength: 0.32,
      headWidth: 0.15,
      headDroop: 30,
      headBranches: 90,
      headBranchLength: 0.4,
      headBranchAngle: 30,
      headBranchDroop: 30,
      headBranchSpread: 0.9,
      headBranchRadius: 0.0007,
      headSecondary: 4,
      bladeSegments: 10,
      bladeSides: 8,
      culmSides: 8,
      culmSegments: 30,
      branchletSegments: 4,
    },
  },
  {
    name: 'Elephant Grass',
    grass: {
      crownRadius: 0.35,
      crownHeight: 0.12,
      crownSink: 0,
      blades: 60,
      bladeLength: 0.9,
      bladeLengthV: 0.25,
      bladeWidth: 0.03,
      bladeThickness: 0.05,
      bladeTaper: 0.7,
      keel: 0.25,
      twist: 60,
      twistV: 60,
      lean: 35,
      leanV: 12,
      droop: 80,
      droopV: 30,
      droopPower: 1.8,
      culms: 26,
      culmHeight: 3.0,
      culmHeightV: 0.15,
      culmRadius: 0.012,
      culmLean: 8,
      culmLeanV: 5,
      culmCurve: 10,
      nodes: 8,
      nodeSwell: 0.15,
      culmLeafLength: 0.9,
      culmLeafWidth: 1,
      culmLeafAngle: 50,
      head: 'foxtail',
      headLength: 0.22,
      headWidth: 0.035,
      headDroop: 10,
      headBranches: 200,
      headBranchAngle: 45,
      headBranchDroop: 15,
      headBranchSpread: 1,
      headBranchRadius: 0.0008,
      bladeSegments: 12,
      bladeSides: 8,
      culmSides: 8,
      culmSegments: 30,
      branchletSegments: 3,
    },
  },
];

export const GRASS_GROUPS: { label: string; names: string[] }[] = [
  { label: 'Grasses · turf & meadow', names: ['Lawn Turf', 'Meadow Grass', 'Timothy', 'Red Oat Grass'] },
  { label: 'Grasses · tussock', names: ['Blue Fescue', 'Fountain Grass', 'Marram Grass', 'Chinese Silver Grass', 'Pampas Grass'] },
  { label: 'Grasses · cereals & reeds', names: ['Wheat', 'Barley', 'Oat', 'Common Reed', 'Elephant Grass'] },
];
