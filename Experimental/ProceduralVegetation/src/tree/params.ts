/**
 * Tree parameters.
 *
 * The botanical part follows the parametric model of Weber & Penn,
 * "Creation and Rendering of Realistic Trees" (SIGGRAPH 1995) – the same model
 * behind Blender's Sapling / tree-gen add-ons and the ancestor of most DCC tree
 * generators. Per-level arrays are indexed by recursion level: 0 = trunk,
 * 1 = main limbs, 2 = branches, 3 = twigs (levels beyond 3 reuse index 3).
 *
 * Angles are in degrees, lengths are fractions of the parent length unless
 * stated otherwise. Y is up.
 */

import { EnvironmentParams, DEFAULT_SCATTER, scatterObstacles } from '../env/environment';
import { GrassParams, DEFAULT_GRASS, GRASS_PRESETS, GRASS_GROUPS } from '../plant/grassParams';

export type Level4<T> = [T, T, T, T];

export enum Shape {
  Conical = 0,
  Spherical = 1,
  Hemispherical = 2,
  Cylindrical = 3,
  TaperedCylindrical = 4,
  Flame = 5,
  InverseConical = 6,
  TendFlame = 7,
}

export const SHAPE_NAMES: Record<Shape, string> = {
  [Shape.Conical]: 'Conical',
  [Shape.Spherical]: 'Spherical',
  [Shape.Hemispherical]: 'Hemispherical',
  [Shape.Cylindrical]: 'Cylindrical',
  [Shape.TaperedCylindrical]: 'Tapered cylindrical',
  [Shape.Flame]: 'Flame',
  [Shape.InverseConical]: 'Inverse conical',
  [Shape.TendFlame]: 'Tend flame',
};

export interface BotanyParams {
  /** Crown envelope used for level-1 length distribution. */
  shape: Shape;
  /** Overall tree height in metres (± scaleV). */
  scale: number;
  scaleV: number;
  /** Recursion depth: 1..4 */
  levels: number;
  /** Trunk radius as a fraction of trunk length. */
  ratio: number;
  /** Exponent relating child radius to child/parent length ratio. */
  ratioPower: number;
  /** Exponential root flare of the trunk base (0 = none). */
  flare: number;
  /** Number of clones created at the base segment of the trunk (0 = none). */
  baseSplits: number;
  /** Fraction of each level's stem length without branches (bare base). */
  baseSize: Level4<number>;

  /** Relative length of stems at each level (level 0: fraction of `scale`). */
  length: Level4<number>;
  lengthV: Level4<number>;
  /** 0..1 linear taper to a point; 1..2 spherical end; 2..3 periodic. */
  taper: Level4<number>;
  /** Number of segments (curve resolution) per stem. */
  curveRes: Level4<number>;
  /** Total curvature along the stem (degrees). */
  curve: Level4<number>;
  /** If non-zero, the second half curves by this instead (S-shape). */
  curveBack: Level4<number>;
  curveV: Level4<number>;
  /** Random side-to-side bend per segment. */
  bendV: Level4<number>;
  /** Clones per segment (fractional = probabilistic). */
  segSplits: Level4<number>;
  splitAngle: Level4<number>;
  splitAngleV: Level4<number>;
  /** Angle between child and parent axis at emergence. Negative V = varies along stem. */
  downAngle: Level4<number>;
  downAngleV: Level4<number>;
  /** Phyllotactic rotation between successive children around the parent. Negative = alternate sides. */
  rotate: Level4<number>;
  rotateV: Level4<number>;
  /** Maximum number of children per stem at each level (index n = children created by level n-1). */
  branches: Level4<number>;
  /** 0 = alternate, 1 = opposite, >1 = whorled with (value+1) per whorl. */
  branchDist: Level4<number>;
  /** Per-level radius multiplier (artistic override). */
  radiusMod: Level4<number>;
  /** Vertical tropism (+ = grows up, - = droops). Applied to levels >= 2 fully, to lower levels horizontally. */
  attractionUp: number;
  /** Flattening of the fine growth towards the horizontal (0 = none, 1 = strong): flat-topped canopies (acacia). */
  flatten: number;
  /** Periodic swelling of the trunk (bamboo culm nodes): relative amplitude (0 = none). */
  nodeSwell: number;
  /** Spacing of the swellings as a fraction of the trunk length. */
  nodeSpacing: number;
  /** Leaves per twig (proxy quads). 0 disables. */
  leaves: number;
  leafScale: number;
  leafScaleX: number;
  /**
   * Fraction of each terminal stem, measured back from its tip, that carries
   * leaves. 1 spreads them along the whole stem (broadleaf); small values
   * gather them into a tip tuft (yucca rosettes, foxtail pine brushes).
   */
  leafTuft: number;
}

export interface MeshParams {
  /** Radial segments of the trunk ring (even, 6..48). Children derive from it. */
  trunkRadialSegments: number;
  /** Minimum radial segments for any stem (even, >= 4). Forces taller windows for thin children. */
  minRadialSegments: number;
  /** Ring stations per curve segment at level 0..3. */
  ringsPerSegment: Level4<number>;
  /** Hole width relative to child diameter (branch collar). */
  collarScale: number;
  /** Distance of the child's first full ring from the parent surface, in child radii. */
  collarLength: number;
  /** 0 = parents keep their radius past a junction (Weber-Penn), 1 = cross-section area is conserved (pipe model / da Vinci rule). */
  forkRadiusConservation: number;
  /** Intermediate rings inserted between a junction window and the child's first ring. */
  collarRings: number;
  /** 0 = straight chamfer between window and child ring, 1 = tangent-continuous fillet. */
  collarFillet: number;
  /** Mitre of the child's first ring towards the parent surface: 0 = perpendicular to the child, 1 = parallel to the parent surface. */
  collarMitre: number;
  /** Minimum stem radius in metres (avoids zero-radius tips). */
  minRadius: number;
  /** Vertices around a primary root where it leaves the trunk (even, >= 6). */
  rootRadialSegments: number;
  /** Root buttress lobes at the trunk base (used when the root system is disabled; otherwise one lobe per root). */
  rootLobes: number;
  rootLobeAmplitude: number;
  rootLobeHeight: number;
  /** Cap stem tips with quads (kite cap) instead of leaving them open. */
  capTips: boolean;
  /** Skip stems thinner than this in metres (LOD/cleanup). */
  cullRadius: number;
}

/**
 * Root system. Roots are grown by an environment-aware turtle (see
 * `roots.ts`): they leave the trunk base, follow the ground and climb onto or
 * skirt the objects placed around the tree, then dive underground at the tip.
 * Lengths are fractions of the tree scale; radii are fractions of the trunk's
 * nominal radius.
 */
export interface RootParams {
  enabled: boolean;
  /** Primary roots leaving the trunk base. */
  count: number;
  /** Primary root length as a fraction of the tree scale (metres = scale x length). */
  length: number;
  lengthV: number;
  /** Base radius of a primary root as a fraction of the trunk's nominal radius. */
  radius: number;
  radiusV: number;
  /** Taper exponent along the root (1 = linear to a point, <1 stays thick for longer). */
  taper: number;
  /** Height of the root axis above the ground where it leaves the trunk, in root radii. */
  emergeHeight: number;
  /** Initial pitch below the horizontal at the trunk, degrees. */
  descent: number;
  /** How much of a surface root shows: 0 = flush with the ground, 0.5 = half buried, 1 = resting on top. */
  exposure: number;
  /** Lateral roots per primary root. */
  laterals: number;
  /** Lateral base radius relative to the parent's local radius. */
  lateralRadius: number;
  /** Lateral length relative to the parent's remaining length. */
  lateralLength: number;
  /** Probability that a primary root forks into two. */
  forks: number;
  /** Horizontal meander (0 = straight, 1 = strongly winding). */
  wander: number;
  /** Objects up to this many root diameters tall are climbed over; taller ones are skirted (climbed part-way). */
  climb: number;
  /** Pull of the roots towards nearby objects (0 = indifferent, 1 = seeks them out). */
  attraction: number;
  /** How deeply roots press into the objects they follow (0 = rest on top, 1 = sunk in). */
  grip: number;
  /** Fraction of the length after which the tip dives underground. */
  dive: number;
}

export const DEFAULT_ROOTS: RootParams = {
  enabled: true,
  count: 5,
  length: 0.2,
  lengthV: 0.06,
  radius: 0.42,
  radiusV: 0.12,
  taper: 0.85,
  emergeHeight: 0.6,
  descent: 24,
  exposure: 0.4,
  laterals: 2,
  lateralRadius: 0.5,
  lateralLength: 0.5,
  forks: 0.5,
  wander: 0.5,
  climb: 3,
  attraction: 0.6,
  grip: 0.5,
  dive: 0.75,
};

/** What kind of plant a parameter set describes. Trees go through the Weber-Penn skeleton, grasses through the grass mesher. */
export type PlantKind = 'tree' | 'grass';

export interface TreeParams {
  name: string;
  seed: number;
  /** Defaults to 'tree' when absent (older saved parameter sets). */
  kind?: PlantKind;
  botany: BotanyParams;
  mesh: MeshParams;
  roots: RootParams;
  environment: EnvironmentParams;
  /** Grass description (kind === 'grass'). */
  grass?: GrassParams;
}

export const isGrass = (p: { kind?: PlantKind }): boolean => p.kind === 'grass';

/** Desert presets keep the welded tree pipeline but use xeric palettes and plant-specific framing in the viewer. */
export const isDesert = (p: { name?: string }): boolean => /Cactus|Agave|Aloe|Ocotillo|Sotol|Joshua Tree/i.test(p.name ?? '');

export const DEFAULT_MESH: MeshParams = {
  trunkRadialSegments: 24,
  minRadialSegments: 6,
  ringsPerSegment: [3, 2, 2, 1],
  collarScale: 1.3,
  collarLength: 1.2,
  forkRadiusConservation: 0.6,
  collarRings: 1,
  collarFillet: 0.7,
  collarMitre: 0.5,
  minRadius: 0.004,
  rootRadialSegments: 12,
  rootLobes: 5,
  rootLobeAmplitude: 0.35,
  rootLobeHeight: 0.08,
  capTips: true,
  cullRadius: 0.0,
};

const L = <T>(a: T, b: T, c: T, d: T): Level4<T> => [a, b, c, d];

export const DEFAULT_BOTANY: BotanyParams = {
  shape: Shape.TendFlame,
  scale: 13,
  scaleV: 3,
  levels: 3,
  ratio: 0.015,
  ratioPower: 1.2,
  flare: 0.6,
  baseSplits: 0,
  baseSize: L(0.3, 0.02, 0.02, 0.02),
  length: L(1, 0.3, 0.6, 0),
  lengthV: L(0, 0, 0, 0),
  taper: L(1, 1, 1, 1),
  curveRes: L(5, 5, 3, 1),
  curve: L(0, -40, -40, 0),
  curveBack: L(0, 0, 0, 0),
  curveV: L(20, 50, 75, 0),
  bendV: L(0, 50, 0, 0),
  segSplits: L(0, 0, 0, 0),
  splitAngle: L(40, 0, 0, 0),
  splitAngleV: L(5, 0, 0, 0),
  downAngle: L(0, 60, 45, 45),
  downAngleV: L(0, -50, 10, 10),
  rotate: L(0, 140, 140, 77),
  rotateV: L(0, 0, 0, 0),
  branches: L(1, 50, 30, 10),
  branchDist: L(0, 0, 0, 0),
  radiusMod: L(1, 1, 1, 1),
  attractionUp: 0.5,
  flatten: 0,
  nodeSwell: 0,
  nodeSpacing: 0.05,
  leaves: 25,
  leafScale: 0.17,
  leafScaleX: 1,
  leafTuft: 1,
};

export function cloneParams(p: TreeParams): TreeParams {
  return JSON.parse(JSON.stringify(p)) as TreeParams;
}

/** Nominal trunk radius at the base (metres), flare included. */
export function trunkBaseRadius(b: BotanyParams): number {
  const nominal = b.scale * b.length[0] * b.ratio * b.radiusMod[0];
  return nominal * (b.flare * 0.99 + 1);
}

/** Primary root length in metres. */
export function rootReach(p: { botany: BotanyParams; roots: RootParams }): number {
  return p.botany.scale * p.roots.length;
}

/** Scatter parameters proportioned to a tree: objects land within root reach. */
export function scatterFor(p: { botany: BotanyParams; roots: RootParams }, seed = DEFAULT_SCATTER.seed, count = DEFAULT_SCATTER.count): EnvironmentParams['scatter'] {
  const reach = Math.max(0.5, rootReach(p));
  return {
    ...DEFAULT_SCATTER,
    seed,
    count,
    minDistance: 0.08 * reach,
    maxDistance: 0.42 * reach,
    minSize: 0.16 * reach,
    maxSize: 0.32 * reach,
  };
}

export function defaultEnvironment(p: { botany: BotanyParams; roots: RootParams }): EnvironmentParams {
  const scatter = scatterFor(p);
  return { enabled: true, scatter, obstacles: scatterObstacles(scatter, trunkBaseRadius(p.botany)) };
}

function preset(name: string, botany: Partial<BotanyParams>, mesh: Partial<MeshParams> = {}, roots: Partial<RootParams> = {}): TreeParams {
  const b = { ...DEFAULT_BOTANY, ...botany };
  const r = { ...DEFAULT_ROOTS, ...roots };
  return {
    name,
    seed: 1,
    botany: b,
    mesh: { ...DEFAULT_MESH, ...mesh },
    roots: r,
    environment: defaultEnvironment({ botany: b, roots: r }),
  };
}

function grassPreset(name: string, grass: Partial<GrassParams>): TreeParams {
  const roots = { ...DEFAULT_ROOTS, enabled: false };
  return {
    name,
    seed: 1,
    kind: 'grass',
    botany: { ...DEFAULT_BOTANY },
    mesh: { ...DEFAULT_MESH },
    roots,
    environment: { enabled: false, scatter: { ...DEFAULT_SCATTER, count: 0 }, obstacles: [] },
    grass: { ...DEFAULT_GRASS, ...grass },
  };
}

/**
 * Tree presets. Botanical values are based on the species tables published in
 * Weber & Penn (1995) and tuned for the welded mesher.
 */
export const TREE_PRESETS: TreeParams[] = [
  preset('English Oak', {
    shape: Shape.Hemispherical,
    scale: 16,
    scaleV: 2,
    levels: 4,
    ratio: 0.022,
    ratioPower: 1.35,
    flare: 1.1,
    baseSplits: 0,
    baseSize: L(0.22, 0.05, 0.02, 0.02),
    length: L(1, 0.55, 0.45, 0.3),
    lengthV: L(0, 0.08, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(10, 10, 6, 3),
    curve: L(0, 25, 10, 0),
    curveBack: L(0, -35, 0, 0),
    curveV: L(60, 140, 120, 80),
    bendV: L(20, 90, 60, 0),
    segSplits: L(0.35, 0.45, 0.25, 0),
    splitAngle: L(38, 40, 40, 0),
    splitAngleV: L(8, 10, 10, 0),
    downAngle: L(0, 48, 45, 45),
    downAngleV: L(0, -30, 20, 20),
    rotate: L(0, 120, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 14, 22, 14),
    branchDist: L(0, 0, 0, 0),
    attractionUp: 0.35,
    leaves: 12,
    leafScale: 0.14,
    leafScaleX: 0.8,
  }, {}, { count: 6, radius: 0.45, length: 0.22, laterals: 2, forks: 0.6 }),

  preset('Quaking Aspen', {
    shape: Shape.TendFlame,
    scale: 13,
    scaleV: 3,
    levels: 3,
    ratio: 0.015,
    ratioPower: 1.2,
    flare: 0.6,
    baseSize: L(0.3, 0.02, 0.02, 0.02),
    length: L(1, 0.3, 0.6, 0),
    lengthV: L(0, 0, 0.1, 0),
    curveRes: L(6, 5, 4, 1),
    curve: L(0, -40, -60, 0),
    curveV: L(20, 100, 100, 0),
    bendV: L(0, 50, 0, 0),
    segSplits: L(0, 0, 0, 0),
    splitAngle: L(40, 0, 0, 0),
    splitAngleV: L(5, 0, 0, 0),
    downAngle: L(0, 60, 60, 45),
    downAngleV: L(0, -50, 20, 30),
    rotate: L(0, 140, 140, 77),
    branches: L(1, 45, 26, 1),
    attractionUp: 0.5,
    leaves: 30,
    leafScale: 0.17,
  }, {}, { count: 4, radius: 0.38, length: 0.16, laterals: 1, forks: 0.3 }),

  preset('Black Tupelo', {
    shape: Shape.TaperedCylindrical,
    scale: 23,
    scaleV: 5,
    levels: 4,
    ratio: 0.015,
    ratioPower: 1.3,
    flare: 1,
    baseSize: L(0.2, 0.02, 0.02, 0.02),
    length: L(1, 0.3, 0.6, 0.2),
    lengthV: L(0, 0.05, 0.1, 0),
    curveRes: L(10, 10, 8, 2),
    curve: L(0, 0, -10, 0),
    curveV: L(40, 90, 150, 0),
    bendV: L(0, 100, 0, 0),
    downAngle: L(0, 60, 40, 45),
    downAngleV: L(0, -40, 10, 10),
    rotate: L(0, 140, 140, 140),
    rotateV: L(0, 60, 50, 0),
    branches: L(1, 55, 20, 12),
    attractionUp: 0.5,
    leaves: 14,
    leafScale: 0.2,
  }, {}, { count: 5, radius: 0.4, length: 0.16 }),

  preset('California Black Oak', {
    shape: Shape.Hemispherical,
    scale: 10,
    scaleV: 2,
    levels: 3,
    ratio: 0.018,
    ratioPower: 1.25,
    flare: 1.2,
    baseSize: L(0.05, 0.02, 0.02, 0.02),
    length: L(1, 0.8, 0.3, 0.4),
    lengthV: L(0, 0.1, 0.05, 0),
    taper: L(0.95, 1, 1, 1),
    curveRes: L(8, 10, 4, 1),
    curve: L(0, 40, 0, 0),
    curveBack: L(0, -70, 0, 0),
    curveV: L(90, 150, 30, 0),
    bendV: L(0, 100, 0, 0),
    segSplits: L(0.4, 0.2, 0.1, 0),
    splitAngle: L(10, 10, 10, 0),
    splitAngleV: L(0, 10, 10, 0),
    downAngle: L(0, 30, 45, 45),
    downAngleV: L(0, -30, 10, 10),
    rotate: L(0, 80, 140, 140),
    rotateV: L(0, 20, 20, 20),
    branches: L(1, 24, 60, 0),
    attractionUp: 0.8,
    leaves: 20,
    leafScale: 0.2,
    leafScaleX: 0.66,
  }, {}, { count: 6, radius: 0.46, length: 0.26, forks: 0.6 }),

  preset('Southern Live Oak', {
    // Quercus virginiana: a short, massive trunk dividing low into a few huge
    // limbs that sweep out almost horizontally, sag, and rise again at the
    // tips. The crown is far wider than the tree is tall.
    shape: Shape.Hemispherical,
    scale: 11,
    scaleV: 2,
    levels: 4,
    ratio: 0.036,
    ratioPower: 1.1,
    flare: 1.0,
    baseSplits: 3,
    baseSize: L(0.22, 0.05, 0.02, 0.02),
    length: L(1, 0.9, 0.45, 0.3),
    lengthV: L(0, 0.1, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(8, 12, 6, 3),
    curve: L(0, 20, 15, 0),
    curveBack: L(0, -35, 0, 0),
    curveV: L(60, 120, 120, 80),
    bendV: L(20, 80, 60, 0),
    segSplits: L(0.3, 0.35, 0.2, 0),
    splitAngle: L(70, 45, 40, 0),
    splitAngleV: L(10, 10, 10, 0),
    downAngle: L(0, 72, 48, 45),
    downAngleV: L(0, -18, 20, 20),
    rotate: L(0, 120, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 10, 22, 14),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.2,
    leaves: 14,
    leafScale: 0.08,
    leafScaleX: 0.5,
  }, { trunkRadialSegments: 28 }, { count: 7, radius: 0.45, length: 0.24, laterals: 2, forks: 0.6 }),

  preset('White Oak', {
    // Quercus alba, open grown: a short stout trunk, massive gnarled limbs
    // held nearly horizontal, and a broad irregular crown wider than tall.
    shape: Shape.Hemispherical,
    scale: 17,
    scaleV: 3,
    levels: 4,
    ratio: 0.028,
    ratioPower: 1.15,
    flare: 1.0,
    baseSplits: 2,
    baseSize: L(0.2, 0.05, 0.02, 0.02),
    length: L(1, 0.65, 0.45, 0.3),
    lengthV: L(0, 0.08, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(9, 9, 6, 3),
    curve: L(0, 15, 5, 0),
    curveBack: L(0, -25, 0, 0),
    curveV: L(50, 160, 140, 90),
    bendV: L(15, 100, 70, 0),
    segSplits: L(0.4, 0.4, 0.2, 0),
    splitAngle: L(48, 42, 40, 0),
    splitAngleV: L(8, 10, 10, 0),
    downAngle: L(0, 62, 50, 45),
    downAngleV: L(0, -25, 20, 20),
    rotate: L(0, 125, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 13, 24, 14),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.3,
    leaves: 12,
    leafScale: 0.16,
    leafScaleX: 0.6,
  }, { trunkRadialSegments: 28 }, { count: 6, radius: 0.46, length: 0.24, laterals: 2, forks: 0.6 }),

  preset('Northern Red Oak', {
    // Quercus rubra: a tall straight trunk running well up into a high,
    // rounded, symmetrical crown of stout ascending limbs.
    shape: Shape.Spherical,
    scale: 26,
    scaleV: 3,
    levels: 4,
    ratio: 0.02,
    ratioPower: 1.3,
    flare: 0.8,
    baseSplits: 0,
    baseSize: L(0.35, 0.04, 0.02, 0.02),
    length: L(1, 0.55, 0.45, 0.3),
    lengthV: L(0, 0.06, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(10, 8, 6, 3),
    curve: L(0, -15, 5, 0),
    curveBack: L(0, 0, 0, 0),
    curveV: L(40, 80, 100, 80),
    bendV: L(10, 60, 40, 0),
    segSplits: L(0.25, 0.3, 0.2, 0),
    splitAngle: L(30, 35, 40, 0),
    splitAngleV: L(6, 10, 10, 0),
    downAngle: L(0, 42, 45, 45),
    downAngleV: L(0, -20, 15, 15),
    rotate: L(0, 130, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 13, 24, 14),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.5,
    leaves: 12,
    leafScale: 0.18,
    leafScaleX: 0.7,
  }, {}, { count: 6, radius: 0.42, length: 0.2, laterals: 2, forks: 0.5 }),

  preset('Valley Oak', {
    // Quercus lobata: a massive trunk and broad crown of long arching limbs
    // whose outer branchlets hang down in a weeping fringe.
    shape: Shape.Hemispherical,
    scale: 22,
    scaleV: 3,
    levels: 4,
    ratio: 0.03,
    ratioPower: 1.2,
    flare: 1.2,
    baseSplits: 0,
    baseSize: L(0.2, 0.05, 0.02, 0.02),
    length: L(1, 0.6, 0.45, 0.35),
    lengthV: L(0, 0.08, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(10, 10, 8, 4),
    curve: L(0, 20, 30, 20),
    curveBack: L(0, -20, 0, 0),
    curveV: L(50, 120, 100, 60),
    bendV: L(15, 80, 40, 0),
    segSplits: L(0.5, 0.35, 0.15, 0),
    splitAngle: L(45, 40, 35, 0),
    splitAngleV: L(8, 10, 10, 0),
    downAngle: L(0, 50, 50, 40),
    downAngleV: L(0, -25, 15, 15),
    rotate: L(0, 130, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 12, 22, 16),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: -1.0,
    leaves: 14,
    leafScale: 0.12,
    leafScaleX: 0.7,
  }, { trunkRadialSegments: 28 }, { count: 7, radius: 0.48, length: 0.24, laterals: 2, forks: 0.6 }),

  preset('Pin Oak', {
    // Quercus palustris: excurrent, a straight central leader to the very
    // top with slender tiered limbs: drooping at the bottom, horizontal in
    // the middle, ascending near the top. Pyramidal outline.
    shape: Shape.Conical,
    scale: 20,
    scaleV: 3,
    levels: 4,
    ratio: 0.019,
    ratioPower: 1.4,
    flare: 0.4,
    baseSplits: 0,
    baseSize: L(0.15, 0.02, 0.02, 0.02),
    length: L(1, 0.34, 0.5, 0.3),
    lengthV: L(0, 0.04, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(12, 8, 5, 2),
    curve: L(0, 10, 5, 0),
    curveBack: L(0, 0, 0, 0),
    curveV: L(15, 40, 80, 60),
    bendV: L(0, 20, 20, 0),
    segSplits: L(0, 0, 0.1, 0),
    splitAngle: L(0, 0, 30, 0),
    splitAngleV: L(0, 0, 8, 0),
    downAngle: L(0, 80, 60, 45),
    downAngleV: L(0, -45, 15, 15),
    rotate: L(0, 140, 140, 140),
    rotateV: L(0, 20, 20, 20),
    branches: L(1, 40, 22, 8),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.4,
    leaves: 16,
    leafScale: 0.12,
    leafScaleX: 0.7,
  }, {}, { count: 5, radius: 0.36, length: 0.16, laterals: 2, forks: 0.5 }),

  preset('Cork Oak', {
    // Quercus suber: a short thick trunk splitting low into a few heavy,
    // contorted limbs under a broad, open, rounded crown.
    shape: Shape.Hemispherical,
    scale: 11,
    scaleV: 2,
    levels: 4,
    ratio: 0.04,
    ratioPower: 1.1,
    flare: 0.9,
    baseSplits: 2,
    baseSize: L(0.22, 0.08, 0.02, 0.02),
    length: L(1, 0.6, 0.4, 0.3),
    lengthV: L(0, 0.08, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(8, 8, 6, 3),
    curve: L(10, 20, 10, 0),
    curveBack: L(-15, -30, 0, 0),
    curveV: L(150, 220, 160, 100),
    bendV: L(40, 120, 80, 0),
    segSplits: L(0.4, 0.35, 0.2, 0),
    splitAngle: L(50, 45, 40, 0),
    splitAngleV: L(10, 12, 10, 0),
    downAngle: L(0, 55, 50, 45),
    downAngleV: L(0, -25, 20, 20),
    rotate: L(0, 120, 140, 140),
    rotateV: L(0, 40, 40, 20),
    branches: L(1, 9, 16, 12),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.4,
    leaves: 14,
    leafScale: 0.07,
    leafScaleX: 0.5,
  }, { trunkRadialSegments: 28 }, { count: 6, radius: 0.44, length: 0.22, laterals: 2, forks: 0.6 }),

  preset('Silver Birch', {
    shape: Shape.Cylindrical,
    scale: 18,
    scaleV: 4,
    levels: 3,
    ratio: 0.013,
    ratioPower: 1.5,
    flare: 0.5,
    baseSize: L(0.3, 0.1, 0.02, 0.02),
    length: L(1, 0.3, 0.4, 0),
    lengthV: L(0, 0.05, 0.2, 0),
    curveRes: L(10, 8, 6, 0),
    curve: L(0, 0, -10, 0),
    curveV: L(50, 150, 200, 0),
    bendV: L(0, 100, 0, 0),
    segSplits: L(0, 0.3, 0, 0),
    splitAngle: L(15, 10, 0, 0),
    downAngle: L(0, 50, 40, 45),
    downAngleV: L(0, -20, 10, 10),
    rotate: L(0, 140, 140, 140),
    rotateV: L(0, 60, 50, 0),
    branches: L(1, 28, 40, 0),
    attractionUp: -2,
    leaves: 40,
    leafScale: 0.15,
  }, {}, { count: 4, radius: 0.36, length: 0.14, laterals: 1, forks: 0.3 }),

  preset('Weeping Willow', {
    shape: Shape.TaperedCylindrical,
    scale: 15,
    scaleV: 3,
    levels: 4,
    ratio: 0.03,
    ratioPower: 1.2,
    flare: 0.75,
    baseSplits: 2,
    baseSize: L(0.05, 0.3, 0.05, 0.05),
    length: L(1, 0.35, 2.0, 0.1),
    lengthV: L(0, 0.1, 0, 0),
    curveRes: L(8, 12, 12, 2),
    curve: L(0, 40, 0, 0),
    curveBack: L(25, 0, 0, 0),
    curveV: L(90, 200, 0, 0),
    bendV: L(0, 160, 0, 0),
    segSplits: L(0.2, 0.2, 0.1, 0),
    splitAngle: L(40, 30, 45, 0),
    splitAngleV: L(5, 10, 20, 0),
    downAngle: L(0, 40, 30, 20),
    downAngleV: L(0, 10, 10, 10),
    rotate: L(0, -120, -120, 140),
    rotateV: L(0, 30, 30, 0),
    branches: L(1, 14, 18, 40),
    radiusMod: L(1, 1, 0.1, 1),
    attractionUp: -3,
    leaves: 15,
    leafScale: 0.13,
    leafScaleX: 0.2,
  }, {}, { count: 6, radius: 0.42, length: 0.22, laterals: 2 }),

  preset('Scots Pine', {
    shape: Shape.Conical,
    scale: 22,
    scaleV: 4,
    levels: 3,
    ratio: 0.014,
    ratioPower: 1.4,
    flare: 0.4,
    baseSize: L(0.45, 0.05, 0.02, 0.02),
    length: L(1, 0.28, 0.35, 0),
    lengthV: L(0, 0.06, 0.1, 0),
    curveRes: L(10, 6, 4, 1),
    curve: L(0, -25, 0, 0),
    curveBack: L(0, 0, 0, 0),
    curveV: L(30, 60, 90, 0),
    bendV: L(0, 40, 0, 0),
    segSplits: L(0, 0, 0, 0),
    downAngle: L(0, 70, 55, 45),
    downAngleV: L(0, -30, 20, 10),
    rotate: L(0, 140, 140, 140),
    rotateV: L(0, 25, 30, 0),
    branches: L(1, 42, 24, 0),
    branchDist: L(0, 3, 0, 0),
    attractionUp: 1.5,
    leaves: 60,
    leafScale: 0.1,
    leafScaleX: 0.25,
  }, {}, { count: 4, radius: 0.4, length: 0.13, laterals: 1, forks: 0.25 }),

  preset('Coast Redwood', {
    // Sequoia sempervirens: a straight, massively buttressed column with short
    // horizontal limbs high up; the lower trunk is bare.
    shape: Shape.TaperedCylindrical,
    scale: 70,
    scaleV: 15,
    levels: 3,
    ratio: 0.017,
    ratioPower: 1.5,
    flare: 1.6,
    baseSplits: 0,
    baseSize: L(0.4, 0.05, 0.02, 0.02),
    length: L(1, 0.12, 0.45, 0),
    lengthV: L(0, 0.04, 0.1, 0),
    taper: L(1, 1, 1, 1),
    curveRes: L(12, 5, 4, 1),
    curve: L(0, -12, 0, 0),
    curveBack: L(0, 0, 0, 0),
    curveV: L(8, 40, 80, 0),
    bendV: L(0, 30, 20, 0),
    segSplits: L(0, 0, 0, 0),
    splitAngle: L(0, 0, 0, 0),
    splitAngleV: L(0, 0, 0, 0),
    downAngle: L(0, 80, 50, 45),
    downAngleV: L(0, -25, 15, 10),
    rotate: L(0, 140, 140, 140),
    rotateV: L(0, 30, 30, 0),
    branches: L(1, 90, 26, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 0.85, 1, 1),
    attractionUp: 0.8,
    leaves: 40,
    leafScale: 0.14,
    leafScaleX: 0.35,
  }, { rootLobeAmplitude: 0.5, rootLobeHeight: 0.06 }, { count: 7, radius: 0.36, length: 0.09, laterals: 1, forks: 0.4, emergeHeight: 0.7 }),

  preset('Norway Spruce', {
    // Picea abies: narrow spire, whorled limbs that droop and sweep up at the
    // tip, pendulous secondaries.
    shape: Shape.Conical,
    scale: 32,
    scaleV: 6,
    levels: 3,
    ratio: 0.012,
    ratioPower: 1.6,
    flare: 0.5,
    baseSplits: 0,
    baseSize: L(0.12, 0.02, 0.02, 0.02),
    length: L(1, 0.24, 0.3, 0),
    lengthV: L(0, 0.04, 0.08, 0),
    taper: L(1, 1, 1, 1),
    curveRes: L(10, 6, 4, 1),
    curve: L(0, 22, -25, 0),
    curveBack: L(0, -55, 0, 0),
    curveV: L(6, 30, 60, 0),
    bendV: L(0, 15, 0, 0),
    segSplits: L(0, 0, 0, 0),
    splitAngle: L(0, 0, 0, 0),
    splitAngleV: L(0, 0, 0, 0),
    downAngle: L(0, 78, 65, 45),
    downAngleV: L(0, -20, 20, 10),
    rotate: L(0, 0, 140, 140),
    rotateV: L(0, 20, 20, 0),
    branches: L(1, 170, 26, 0),
    branchDist: L(0, 5, 0, 0),
    radiusMod: L(1, 0.9, 1, 1),
    attractionUp: -1.6,
    leaves: 60,
    leafScale: 0.09,
    leafScaleX: 0.3,
  }, {}, { count: 5, radius: 0.36, length: 0.14, laterals: 1, forks: 0.3 }),

  preset('Bamboo', {
    // A single culm: straight, hollow-looking cane with swollen nodes, no
    // taper to speak of, short whorled branchlets in the upper half, tip nods.
    shape: Shape.Cylindrical,
    scale: 11,
    scaleV: 3,
    levels: 3,
    ratio: 0.0075,
    ratioPower: 1.0,
    flare: 0.05,
    baseSplits: 0,
    baseSize: L(0.45, 0.1, 0.02, 0.02),
    length: L(1, 0.14, 0.4, 0),
    lengthV: L(0, 0.05, 0.1, 0),
    taper: L(0.6, 1, 1, 1),
    curveRes: L(14, 4, 3, 1),
    curve: L(0, 15, 10, 0),
    curveBack: L(45, 0, 0, 0),
    curveV: L(10, 30, 40, 0),
    bendV: L(0, 10, 0, 0),
    segSplits: L(0, 0, 0, 0),
    splitAngle: L(0, 0, 0, 0),
    splitAngleV: L(0, 0, 0, 0),
    downAngle: L(0, 42, 40, 40),
    downAngleV: L(0, 10, 10, 10),
    rotate: L(0, 0, 140, 140),
    rotateV: L(0, 25, 20, 0),
    branches: L(1, 36, 8, 0),
    branchDist: L(0, 2, 0, 0),
    radiusMod: L(1, 0.7, 1, 1),
    attractionUp: 0.6,
    nodeSwell: 0.12,
    nodeSpacing: 0.045,
    leaves: 30,
    leafScale: 0.12,
    leafScaleX: 0.15,
  }, { rootLobeAmplitude: 0, rootLobeHeight: 0.02, ringsPerSegment: [2, 2, 2, 1] }, { enabled: false, count: 0, radius: 0.3, length: 0.05, laterals: 0, forks: 0 }),

  preset('Umbrella Thorn', {
    // Vachellia (Acacia) tortilis of the savanna: a short trunk splitting low
    // into a few leaning stems, limbs rising then levelling off, and the
    // finer growth spread into a wide, flat canopy.
    shape: Shape.InverseConical,
    scale: 8,
    scaleV: 1.5,
    levels: 4,
    ratio: 0.032,
    ratioPower: 1.2,
    flare: 0.7,
    baseSplits: 2,
    baseSize: L(0.3, 0.1, 0.05, 0.02),
    length: L(1, 0.85, 0.5, 0.3),
    lengthV: L(0, 0.1, 0.1, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(6, 8, 6, 3),
    curve: L(0, 5, 15, 5),
    curveBack: L(0, 45, 0, 0),
    curveV: L(40, 80, 100, 80),
    bendV: L(0, 50, 60, 20),
    segSplits: L(0.6, 0.55, 0.3, 0),
    splitAngle: L(60, 42, 40, 0),
    splitAngleV: L(10, 10, 10, 0),
    downAngle: L(0, 62, 65, 55),
    downAngleV: L(0, -20, 15, 15),
    rotate: L(0, 140, 140, 140),
    rotateV: L(0, 40, 40, 0),
    branches: L(1, 20, 16, 12),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: -0.3,
    flatten: 0.9,
    leaves: 20,
    leafScale: 0.08,
    leafScaleX: 0.5,
  }, {}, { count: 5, radius: 0.42, length: 0.24, laterals: 2, forks: 0.5 }),

  preset('Baobab', {
    // Adansonia: a hugely swollen, bottle-shaped trunk with a compact tangle
    // of thick, quickly tapering branches on top ("the upside-down tree").
    shape: Shape.Hemispherical,
    scale: 16,
    scaleV: 3,
    levels: 4,
    ratio: 0.15,
    ratioPower: 1.0,
    flare: 0.25,
    baseSplits: 0,
    baseSize: L(0.6, 0.15, 0.05, 0.02),
    length: L(1, 0.34, 0.5, 0.4),
    lengthV: L(0, 0.08, 0.1, 0.05),
    taper: L(0.72, 1, 1, 1),
    curveRes: L(8, 6, 5, 3),
    curve: L(0, 15, 20, 10),
    curveBack: L(0, 0, 0, 0),
    curveV: L(15, 120, 140, 100),
    bendV: L(0, 40, 50, 20),
    segSplits: L(0, 0.5, 0.4, 0),
    splitAngle: L(0, 45, 45, 0),
    splitAngleV: L(0, 10, 10, 0),
    downAngle: L(0, 42, 50, 50),
    downAngleV: L(0, -20, 15, 15),
    rotate: L(0, 140, 140, 140),
    rotateV: L(0, 40, 40, 0),
    branches: L(1, 16, 10, 6),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 0.32, 1.1, 1.3),
    attractionUp: 0.9,
    leaves: 10,
    leafScale: 0.14,
    leafScaleX: 0.6,
  }, { trunkRadialSegments: 32, rootLobeAmplitude: 0.2, rootLobeHeight: 0.05 }, { count: 6, radius: 0.13, length: 0.3, laterals: 1, forks: 0.4, emergeHeight: 0.5 }),

  // ---------------------------------------------------------------- forest
  preset('European Beech', {
    // Fagus sylvatica, open grown: a smooth columnar trunk under a huge,
    // dense, dome-shaped crown; the limbs sweep out and gently upward and the
    // fine twigs hang in layered, slightly drooping sprays.
    shape: Shape.Hemispherical,
    scale: 24,
    scaleV: 3,
    levels: 4,
    ratio: 0.02,
    ratioPower: 1.25,
    flare: 0.9,
    baseSplits: 0,
    baseSize: L(0.2, 0.04, 0.02, 0.02),
    length: L(1, 0.55, 0.5, 0.35),
    lengthV: L(0, 0.06, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(10, 8, 6, 3),
    curve: L(0, 0, 10, 10),
    curveBack: L(0, -20, 0, 0),
    curveV: L(30, 60, 100, 80),
    bendV: L(10, 40, 40, 0),
    segSplits: L(0.3, 0.3, 0.2, 0),
    splitAngle: L(35, 35, 35, 0),
    splitAngleV: L(6, 8, 8, 0),
    downAngle: L(0, 48, 50, 45),
    downAngleV: L(0, -22, 15, 15),
    rotate: L(0, 130, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 16, 22, 16),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: -0.4,
    flatten: 0.2,
    leaves: 22,
    leafScale: 0.11,
    leafScaleX: 0.7,
  }, {}, { count: 6, radius: 0.42, length: 0.22, laterals: 2, forks: 0.5 }),

  preset('Sugar Maple', {
    // Acer saccharum: a straight trunk into a dense, symmetrical, oval to
    // rounded crown of upright-then-spreading limbs.
    shape: Shape.Spherical,
    scale: 24,
    scaleV: 3,
    levels: 4,
    ratio: 0.019,
    ratioPower: 1.3,
    flare: 0.7,
    baseSplits: 0,
    baseSize: L(0.2, 0.04, 0.02, 0.02),
    length: L(1, 0.5, 0.5, 0.35),
    lengthV: L(0, 0.06, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(10, 8, 6, 3),
    curve: L(0, -20, 0, 0),
    curveBack: L(0, 0, 0, 0),
    curveV: L(30, 60, 100, 80),
    bendV: L(10, 40, 40, 0),
    segSplits: L(0.35, 0.3, 0.2, 0),
    splitAngle: L(30, 32, 35, 0),
    splitAngleV: L(6, 8, 8, 0),
    downAngle: L(0, 40, 45, 45),
    downAngleV: L(0, -20, 15, 15),
    rotate: L(0, 137, 140, 140),
    rotateV: L(0, 25, 30, 20),
    branches: L(1, 16, 22, 16),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.6,
    leaves: 20,
    leafScale: 0.15,
    leafScaleX: 0.9,
  }, {}, { count: 6, radius: 0.42, length: 0.2, laterals: 2, forks: 0.5 }),

  preset('Douglas Fir', {
    // Pseudotsuga menziesii: a very tall, straight, clear bole; a narrow
    // conical crown of short, level whorled limbs whose branchlets droop.
    shape: Shape.Conical,
    scale: 55,
    scaleV: 8,
    levels: 3,
    ratio: 0.012,
    ratioPower: 1.5,
    flare: 0.9,
    baseSplits: 0,
    baseSize: L(0.4, 0.02, 0.02, 0.02),
    length: L(1, 0.2, 0.32, 0),
    lengthV: L(0, 0.03, 0.06, 0),
    taper: L(1, 1, 1, 1),
    curveRes: L(12, 6, 4, 1),
    curve: L(0, 8, -20, 0),
    curveBack: L(0, -15, 0, 0),
    curveV: L(6, 30, 50, 0),
    bendV: L(0, 15, 0, 0),
    segSplits: L(0, 0, 0, 0),
    splitAngle: L(0, 0, 0, 0),
    splitAngleV: L(0, 0, 0, 0),
    downAngle: L(0, 80, 60, 45),
    downAngleV: L(0, -18, 20, 10),
    rotate: L(0, 140, 140, 77),
    rotateV: L(0, 25, 40, 0),
    branches: L(1, 130, 24, 0),
    branchDist: L(0, 4, 0, 0),
    radiusMod: L(1, 0.85, 1, 1),
    attractionUp: -1.2,
    leaves: 30,
    leafScale: 0.05,
    leafScaleX: 0.15,
  }, {}, { count: 6, radius: 0.4, length: 0.12, laterals: 1, forks: 0.4, emergeHeight: 0.6 }),

  // ---------------------------------------------------------------- jungle
  preset('Kapok', {
    // Ceiba pentandra: an enormous emergent with a tall straight cylindrical
    // bole on plank buttresses, bare of branches for most of its height, then
    // a few huge horizontal limbs in a flat, spreading, layered crown.
    shape: Shape.InverseConical,
    scale: 50,
    scaleV: 6,
    levels: 4,
    ratio: 0.022,
    ratioPower: 1.1,
    flare: 2.4,
    baseSplits: 0,
    baseSize: L(0.62, 0.05, 0.03, 0.02),
    length: L(1, 0.55, 0.5, 0.35),
    lengthV: L(0, 0.08, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(12, 10, 6, 3),
    curve: L(0, 10, 10, 5),
    curveBack: L(0, 0, 0, 0),
    curveV: L(15, 60, 80, 60),
    bendV: L(5, 40, 40, 0),
    segSplits: L(0, 0.35, 0.25, 0),
    splitAngle: L(0, 40, 40, 0),
    splitAngleV: L(0, 10, 10, 0),
    downAngle: L(0, 85, 70, 55),
    downAngleV: L(0, -10, 15, 15),
    rotate: L(0, 120, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 8, 18, 14),
    branchDist: L(0, 3, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: -0.2,
    flatten: 0.8,
    leaves: 16,
    leafScale: 0.12,
    leafScaleX: 0.5,
  }, { trunkRadialSegments: 40, rootLobeAmplitude: 0.9, rootLobeHeight: 0.12 }, { count: 7, radius: 0.5, length: 0.2, laterals: 1, forks: 0.4, emergeHeight: 1.6, descent: 30 }),

  preset('Rain-Forest Fig', {
    // Ficus (banyan group) of the closed canopy: a heavily buttressed trunk
    // dividing into a dense, wide, many-layered crown of twisting limbs.
    shape: Shape.Hemispherical,
    scale: 28,
    scaleV: 4,
    levels: 4,
    ratio: 0.03,
    ratioPower: 1.15,
    flare: 1.8,
    baseSplits: 2,
    baseSize: L(0.3, 0.06, 0.03, 0.02),
    length: L(1, 0.7, 0.5, 0.35),
    lengthV: L(0, 0.1, 0.1, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(8, 10, 6, 3),
    curve: L(0, 20, 15, 5),
    curveBack: L(0, -30, 0, 0),
    curveV: L(60, 160, 140, 90),
    bendV: L(20, 100, 60, 0),
    segSplits: L(0.3, 0.45, 0.25, 0),
    splitAngle: L(45, 45, 40, 0),
    splitAngleV: L(10, 10, 10, 0),
    downAngle: L(0, 60, 55, 50),
    downAngleV: L(0, -20, 20, 20),
    rotate: L(0, 120, 140, 140),
    rotateV: L(0, 40, 40, 20),
    branches: L(1, 12, 20, 14),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: -0.2,
    flatten: 0.4,
    leaves: 18,
    leafScale: 0.18,
    leafScaleX: 0.6,
  }, { trunkRadialSegments: 32, rootLobeAmplitude: 0.6, rootLobeHeight: 0.1 }, { count: 8, radius: 0.5, length: 0.3, laterals: 2, forks: 0.6, emergeHeight: 1.0, climb: 5 }),

  // ---------------------------------------------------------------- savanna
  preset('Marula', {
    // Sclerocarya birrea: a single stout grey trunk, bare for a good height,
    // under a wide, rounded, rather open crown of thick spreading limbs.
    shape: Shape.Hemispherical,
    scale: 12,
    scaleV: 2,
    levels: 4,
    ratio: 0.034,
    ratioPower: 1.15,
    flare: 0.6,
    baseSplits: 0,
    baseSize: L(0.4, 0.06, 0.03, 0.02),
    length: L(1, 0.6, 0.45, 0.3),
    lengthV: L(0, 0.08, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(8, 8, 6, 3),
    curve: L(0, 15, 10, 5),
    curveBack: L(0, -20, 0, 0),
    curveV: L(30, 120, 120, 80),
    bendV: L(10, 60, 50, 0),
    segSplits: L(0.2, 0.45, 0.25, 0),
    splitAngle: L(40, 45, 40, 0),
    splitAngleV: L(8, 10, 10, 0),
    downAngle: L(0, 45, 50, 45),
    downAngleV: L(0, -20, 20, 20),
    rotate: L(0, 130, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 8, 16, 12),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.3,
    flatten: 0.3,
    leaves: 14,
    leafScale: 0.1,
    leafScaleX: 0.5,
  }, {}, { count: 5, radius: 0.4, length: 0.2, laterals: 2, forks: 0.5 }),

  preset('Fever Tree', {
    // Vachellia xanthophloea: tall, slender, lime-green trunk; sparse open
    // crown of long, thin, spreading limbs with a flattish top.
    shape: Shape.InverseConical,
    scale: 16,
    scaleV: 3,
    levels: 4,
    ratio: 0.018,
    ratioPower: 1.2,
    flare: 0.5,
    baseSplits: 0,
    baseSize: L(0.45, 0.05, 0.03, 0.02),
    length: L(1, 0.5, 0.5, 0.3),
    lengthV: L(0, 0.1, 0.1, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(8, 8, 6, 3),
    curve: L(0, 10, 15, 5),
    curveBack: L(0, 20, 0, 0),
    curveV: L(30, 80, 100, 80),
    bendV: L(10, 50, 60, 20),
    segSplits: L(0.5, 0.45, 0.3, 0),
    splitAngle: L(40, 40, 40, 0),
    splitAngleV: L(10, 10, 10, 0),
    downAngle: L(0, 55, 60, 55),
    downAngleV: L(0, -20, 15, 15),
    rotate: L(0, 140, 140, 140),
    rotateV: L(0, 40, 40, 0),
    branches: L(1, 10, 14, 12),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.2,
    flatten: 0.7,
    leaves: 16,
    leafScale: 0.07,
    leafScaleX: 0.4,
  }, {}, { count: 5, radius: 0.38, length: 0.18, laterals: 2, forks: 0.5 }),

  // ----------------------------------------------------------------- desert
  preset('Joshua Tree', {
    // Yucca brevifolia: a stout shaggy trunk that forks again and again into
    // thick, stubby, upturned arms, each fork a near-equal Y, ending in a
    // dense head of club-shaped tips.
    shape: Shape.Spherical,
    scale: 5.5,
    scaleV: 1.5,
    levels: 3,
    ratio: 0.07,
    ratioPower: 0.8,
    flare: 0.5,
    baseSplits: 2,
    baseSize: L(0.3, 0.15, 0.1, 0.02),
    length: L(1, 0.55, 0.5, 0),
    lengthV: L(0, 0.15, 0.15, 0),
    taper: L(0.55, 0.45, 0.35, 1),
    curveRes: L(6, 5, 4, 1),
    curve: L(35, 25, 25, 0),
    curveBack: L(-45, -35, -35, 0),
    curveV: L(110, 90, 90, 0),
    bendV: L(70, 40, 40, 0),
    segSplits: L(0.7, 0.9, 0.9, 0),
    splitAngle: L(80, 60, 60, 0),
    splitAngleV: L(20, 15, 15, 0),
    downAngle: L(0, 55, 55, 50),
    downAngleV: L(0, 10, 10, 40),
    rotate: L(0, 140, 140, 137),
    rotateV: L(0, 40, 40, 20),
    branches: L(1, 4, 3, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 0.95, 0.95, 1),
    attractionUp: 1.0,
    // Foliage: a dense rosette of stiff, bayonet-shaped leaves (15–35 cm) at
    // the tip of every arm; the arms themselves stay bare.
    leaves: 110,
    leafScale: 0.32,
    leafScaleX: 0.12,
    leafTuft: 0.2,
  }, { trunkRadialSegments: 20, rootLobeAmplitude: 0.15, rootLobeHeight: 0.05 }, { count: 4, radius: 0.3, length: 0.12, laterals: 1, forks: 0.3 }),

  preset('Desert Ironwood', {
    // Olneya tesota: a short, gnarled, often multi-stemmed trunk under a low,
    // dense, rounded crown of crooked, intricately zig-zag branches.
    shape: Shape.Hemispherical,
    scale: 7,
    scaleV: 1.5,
    levels: 4,
    ratio: 0.035,
    ratioPower: 1.1,
    flare: 0.6,
    baseSplits: 2,
    baseSize: L(0.15, 0.06, 0.03, 0.02),
    length: L(1, 0.7, 0.45, 0.3),
    lengthV: L(0, 0.1, 0.1, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(6, 8, 6, 3),
    curve: L(10, 20, 10, 0),
    curveBack: L(-15, -30, 0, 0),
    curveV: L(160, 240, 200, 140),
    bendV: L(40, 140, 100, 0),
    segSplits: L(0.4, 0.4, 0.25, 0),
    splitAngle: L(55, 50, 45, 0),
    splitAngleV: L(12, 12, 10, 0),
    downAngle: L(0, 55, 55, 50),
    downAngleV: L(0, -20, 20, 20),
    rotate: L(0, 120, 140, 140),
    rotateV: L(0, 40, 40, 20),
    branches: L(1, 8, 14, 12),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.3,
    flatten: 0.2,
    leaves: 14,
    leafScale: 0.05,
    leafScaleX: 0.4,
  }, {}, { count: 5, radius: 0.36, length: 0.2, laterals: 2, forks: 0.5 }),

  preset('Saguaro Cactus', {
    // Carnegiea gigantea: a single ribbed column with a handful of high,
    // right-angled arms. The branch spacing and short curved segments keep the
    // silhouette unmistakable even before the spine/normal detail pass.
    shape: Shape.TaperedCylindrical,
    scale: 8.5,
    scaleV: 1.2,
    levels: 3,
    ratio: 0.065,
    ratioPower: 0.9,
    flare: 0.25,
    baseSplits: 0,
    baseSize: L(0.34, 0.18, 0.08, 0.02),
    length: L(1, 0.46, 0.34, 0),
    lengthV: L(0, 0.06, 0.05, 0),
    taper: L(0.58, 0.48, 0.35, 1),
    curveRes: L(10, 8, 5, 1),
    curve: L(3, 26, 20, 0),
    curveBack: L(0, -18, -12, 0),
    curveV: L(12, 25, 30, 0),
    bendV: L(6, 15, 18, 0),
    segSplits: L(0, 0.1, 0.05, 0),
    splitAngle: L(0, 90, 70, 0),
    splitAngleV: L(0, 7, 8, 0),
    downAngle: L(0, 82, 72, 45),
    downAngleV: L(0, -8, -12, 10),
    rotate: L(0, 137, 137, 137),
    rotateV: L(0, 10, 10, 0),
    branches: L(1, 3, 2, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 0.92, 0.8, 1),
    attractionUp: 1.25,
    leaves: 0,
  }, { trunkRadialSegments: 24, collarRings: 2, collarFillet: 0.9, rootLobeAmplitude: 0.2, rootLobeHeight: 0.06 }, { count: 5, radius: 0.32, length: 0.16, laterals: 1, forks: 0.2 }),

  preset('Golden Barrel Cactus', {
    // Echinocactus grusonii: a solitary globe with a flattened crown. A very
    // short, high-radius trunk produces the characteristic barrel profile.
    shape: Shape.Spherical,
    scale: 1.45,
    scaleV: 0.12,
    levels: 1,
    ratio: 0.24,
    ratioPower: 1,
    flare: 0.18,
    baseSplits: 0,
    baseSize: L(0.04, 0.02, 0.02, 0.02),
    length: L(1, 0, 0, 0),
    lengthV: L(0.02, 0, 0, 0),
    taper: L(1.9, 1, 1, 1),
    curveRes: L(12, 1, 1, 1),
    curve: L(0, 0, 0, 0),
    curveBack: L(0, 0, 0, 0),
    curveV: L(0, 0, 0, 0),
    bendV: L(0, 0, 0, 0),
    segSplits: L(0, 0, 0, 0),
    splitAngle: L(0, 0, 0, 0),
    splitAngleV: L(0, 0, 0, 0),
    downAngle: L(0, 0, 0, 0),
    downAngleV: L(0, 0, 0, 0),
    rotate: L(0, 0, 0, 0),
    rotateV: L(0, 0, 0, 0),
    branches: L(0, 0, 0, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0,
    leaves: 0,
  }, { trunkRadialSegments: 32, ringsPerSegment: [5, 1, 1, 1], collarRings: 0, rootLobeAmplitude: 0.1, rootLobeHeight: 0.04 }, { count: 4, radius: 0.3, length: 0.12, laterals: 1, forks: 0.2 }),

  preset('Organ Pipe Cactus', {
    // Stenocereus thurberi: a clump of tall, parallel, mostly unbranched
    // columns that split from a common basal crown.
    shape: Shape.Cylindrical,
    scale: 6.2,
    scaleV: 0.9,
    levels: 2,
    ratio: 0.052,
    ratioPower: 0.95,
    flare: 0.2,
    baseSplits: 5,
    baseSize: L(0.16, 0.04, 0.02, 0.02),
    length: L(1, 0.5, 0, 0),
    lengthV: L(0.05, 0.06, 0, 0),
    taper: L(0.45, 0.35, 1, 1),
    curveRes: L(8, 6, 1, 1),
    curve: L(4, 12, 0, 0),
    curveBack: L(0, -10, 0, 0),
    curveV: L(15, 24, 0, 0),
    bendV: L(5, 12, 0, 0),
    segSplits: L(0, 0.08, 0, 0),
    splitAngle: L(0, 24, 0, 0),
    splitAngleV: L(0, 8, 0, 0),
    downAngle: L(0, 84, 45, 45),
    downAngleV: L(0, -5, 0, 0),
    rotate: L(0, 137, 137, 137),
    rotateV: L(0, 12, 0, 0),
    branches: L(1, 2, 0, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 0.88, 1, 1),
    attractionUp: 1.1,
    leaves: 0,
  }, { trunkRadialSegments: 20, collarRings: 2, rootLobeAmplitude: 0.12, rootLobeHeight: 0.05 }, { count: 6, radius: 0.28, length: 0.14, laterals: 1, forks: 0.25 }),

  preset('Agave Americana', {
    // Agave americana: a low stemless rosette. Terminal leaf cards are kept
    // close to the crown with a strong taper and tuft, giving a layered sword
    // leaf silhouette rather than a generic tree canopy.
    shape: Shape.Hemispherical,
    scale: 1.25,
    scaleV: 0.12,
    levels: 2,
    ratio: 0.04,
    ratioPower: 1,
    flare: 0.1,
    baseSplits: 0,
    baseSize: L(0.08, 0.02, 0.02, 0.02),
    length: L(0.22, 0.28, 0, 0),
    lengthV: L(0.05, 0.08, 0, 0),
    taper: L(0.7, 0.55, 1, 1),
    curveRes: L(5, 4, 1, 1),
    curve: L(10, 25, 0, 0),
    curveBack: L(0, -12, 0, 0),
    curveV: L(10, 18, 0, 0),
    bendV: L(6, 10, 0, 0),
    segSplits: L(0, 0.2, 0, 0),
    splitAngle: L(0, 24, 0, 0),
    splitAngleV: L(0, 8, 0, 0),
    downAngle: L(0, 80, 60, 45),
    downAngleV: L(0, -5, 0, 0),
    rotate: L(0, 137.5, 137.5, 137.5),
    rotateV: L(0, 18, 0, 0),
    branches: L(1, 9, 0, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 0.7, 1, 1),
    attractionUp: 0.4,
    leaves: 72,
    leafScale: 0.72,
    leafScaleX: 0.13,
    leafTuft: 0.92,
  }, { trunkRadialSegments: 16, minRadialSegments: 6, collarRings: 1, rootLobeAmplitude: 0.12, rootLobeHeight: 0.03 }, { enabled: false, count: 0, radius: 0.3, length: 0.05, laterals: 0, forks: 0 }),

  preset('Aloe Vera', {
    // Aloe barbadensis: a compact succulent rosette with fewer, broader,
    // shorter leaves and a soft outward droop.
    shape: Shape.Hemispherical,
    scale: 0.62,
    scaleV: 0.08,
    levels: 2,
    ratio: 0.045,
    ratioPower: 1,
    flare: 0.08,
    baseSplits: 0,
    baseSize: L(0.1, 0.02, 0.02, 0.02),
    length: L(0.18, 0.2, 0, 0),
    lengthV: L(0.04, 0.06, 0, 0),
    taper: L(0.6, 0.45, 1, 1),
    curveRes: L(5, 3, 1, 1),
    curve: L(8, 30, 0, 0),
    curveBack: L(0, -18, 0, 0),
    curveV: L(12, 20, 0, 0),
    bendV: L(8, 12, 0, 0),
    segSplits: L(0, 0.1, 0, 0),
    splitAngle: L(0, 20, 0, 0),
    splitAngleV: L(0, 8, 0, 0),
    downAngle: L(0, 78, 55, 45),
    downAngleV: L(0, 0, 0, 0),
    rotate: L(0, 137.5, 137.5, 137.5),
    rotateV: L(0, 20, 0, 0),
    branches: L(1, 8, 0, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 0.72, 1, 1),
    attractionUp: 0.15,
    leaves: 44,
    leafScale: 0.34,
    leafScaleX: 0.34,
    leafTuft: 0.95,
  }, { trunkRadialSegments: 14, minRadialSegments: 6, collarRings: 1, rootLobeAmplitude: 0.08, rootLobeHeight: 0.02 }, { enabled: false, count: 0, radius: 0.3, length: 0.05, laterals: 0, forks: 0 }),

  preset('Ocotillo', {
    // Fouquieria splendens: many tall, whip-like stems from one basal crown;
    // the sparse leaf cards are restricted to the outer growth for a dry-season
    // silhouette that reads differently from a normal branched tree.
    shape: Shape.TaperedCylindrical,
    scale: 4.2,
    scaleV: 0.7,
    levels: 3,
    ratio: 0.014,
    ratioPower: 1.05,
    flare: 0.18,
    baseSplits: 7,
    baseSize: L(0.04, 0.02, 0.02, 0.02),
    length: L(1, 0.52, 0.2, 0),
    lengthV: L(0.06, 0.08, 0.05, 0),
    taper: L(0.45, 0.65, 0.5, 1),
    curveRes: L(7, 5, 3, 1),
    curve: L(16, 28, 12, 0),
    curveBack: L(-24, -30, -12, 0),
    curveV: L(40, 50, 45, 0),
    bendV: L(14, 24, 22, 0),
    segSplits: L(0.1, 0.16, 0.05, 0),
    splitAngle: L(20, 36, 35, 0),
    splitAngleV: L(8, 10, 8, 0),
    downAngle: L(0, 70, 58, 45),
    downAngleV: L(0, -10, 8, 0),
    rotate: L(0, 137, 137, 137),
    rotateV: L(0, 30, 20, 0),
    branches: L(1, 10, 9, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 0.65, 0.5, 1),
    attractionUp: 0.9,
    leaves: 50,
    leafScale: 0.11,
    leafScaleX: 0.22,
    leafTuft: 0.36,
  }, { trunkRadialSegments: 16, minRadialSegments: 6, collarRings: 1, rootLobeAmplitude: 0.16, rootLobeHeight: 0.04 }, { enabled: false, count: 0, radius: 0.3, length: 0.05, laterals: 0, forks: 0 }),

  preset('Dasylirion Sotol', {
    // Dasylirion wheeleri: a dense blue-green fountain of narrow leaves above
    // a short woody caudex, with no woody branch hierarchy.
    shape: Shape.Hemispherical,
    scale: 1.05,
    scaleV: 0.1,
    levels: 2,
    ratio: 0.05,
    ratioPower: 1,
    flare: 0.15,
    baseSplits: 0,
    baseSize: L(0.08, 0.02, 0.02, 0.02),
    length: L(0.2, 0.25, 0, 0),
    lengthV: L(0.05, 0.08, 0, 0),
    taper: L(0.72, 0.5, 1, 1),
    curveRes: L(5, 4, 1, 1),
    curve: L(18, 38, 0, 0),
    curveBack: L(0, -20, 0, 0),
    curveV: L(10, 20, 0, 0),
    bendV: L(8, 16, 0, 0),
    segSplits: L(0, 0.12, 0, 0),
    splitAngle: L(0, 22, 0, 0),
    splitAngleV: L(0, 8, 0, 0),
    downAngle: L(0, 75, 54, 45),
    downAngleV: L(0, -4, 0, 0),
    rotate: L(0, 137.5, 137.5, 137.5),
    rotateV: L(0, 22, 0, 0),
    branches: L(1, 10, 0, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 0.65, 1, 1),
    attractionUp: 0.55,
    leaves: 72,
    leafScale: 0.54,
    leafScaleX: 0.08,
    leafTuft: 0.96,
  }, { trunkRadialSegments: 16, minRadialSegments: 6, collarRings: 1, rootLobeAmplitude: 0.1, rootLobeHeight: 0.03 }, { enabled: false, count: 0, radius: 0.3, length: 0.05, laterals: 0, forks: 0 }),

  // ------------------------------------------------------------------ rocky
  preset('Bristlecone Pine', {
    // Pinus longaeva at the treeline: a squat, massively thick, twisted trunk,
    // sparse contorted limbs, a wind-flagged, half-dead silhouette.
    shape: Shape.TendFlame,
    scale: 7,
    scaleV: 2,
    levels: 3,
    ratio: 0.075,
    ratioPower: 1.0,
    flare: 1.0,
    baseSplits: -2,
    baseSize: L(0.15, 0.1, 0.02, 0.02),
    length: L(1, 0.55, 0.4, 0),
    lengthV: L(0, 0.25, 0.15, 0),
    taper: L(1, 1, 1, 1),
    curveRes: L(8, 8, 5, 1),
    curve: L(20, 30, 10, 0),
    curveBack: L(-40, -50, 0, 0),
    curveV: L(200, 260, 180, 0),
    bendV: L(80, 160, 90, 0),
    segSplits: L(0.5, 0.45, 0.2, 0),
    splitAngle: L(55, 55, 45, 0),
    splitAngleV: L(15, 15, 10, 0),
    downAngle: L(0, 55, 50, 40),
    downAngleV: L(0, -15, 20, 35),
    rotate: L(0, 110, 140, 137),
    rotateV: L(0, 50, 50, 30),
    branches: L(1, 7, 16, 0),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.7,
    // Foliage: short needles (2.5–4 cm) packed into bottle-brush tufts on the
    // outer half of every branchlet.
    leaves: 220,
    leafScale: 0.07,
    leafScaleX: 0.35,
    leafTuft: 0.4,
  }, { trunkRadialSegments: 28, rootLobeAmplitude: 0.5, rootLobeHeight: 0.1 }, { count: 6, radius: 0.5, length: 0.3, laterals: 2, forks: 0.6, exposure: 0.7, climb: 6 }),

  preset('Rowan', {
    // Sorbus aucuparia on rock: a slender, light-crowned mountain tree with a
    // short trunk and a few ascending stems, open and airy.
    shape: Shape.Spherical,
    scale: 9,
    scaleV: 2,
    levels: 4,
    ratio: 0.02,
    ratioPower: 1.3,
    flare: 0.5,
    baseSplits: -2,
    baseSize: L(0.2, 0.1, 0.03, 0.02),
    length: L(1, 0.55, 0.45, 0.3),
    lengthV: L(0, 0.08, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(8, 8, 6, 3),
    curve: L(0, -10, 0, 0),
    curveBack: L(0, 0, 0, 0),
    curveV: L(50, 80, 100, 80),
    bendV: L(20, 50, 40, 0),
    segSplits: L(0.3, 0.3, 0.2, 0),
    splitAngle: L(35, 35, 35, 0),
    splitAngleV: L(8, 8, 8, 0),
    downAngle: L(0, 42, 45, 45),
    downAngleV: L(0, -20, 15, 15),
    rotate: L(0, 137, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 10, 16, 12),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.8,
    leaves: 18,
    leafScale: 0.14,
    leafScaleX: 0.5,
  }, {}, { count: 5, radius: 0.4, length: 0.28, laterals: 2, forks: 0.6, exposure: 0.6, climb: 6 }),

  preset('Japanese Maple', {
    // Acer palmatum, garden form: a short trunk dividing low into several
    // sinuous, gently ascending stems that spread into a broad, layered,
    // dome-shaped crown with fine tiered branching held nearly level.
    shape: Shape.Hemispherical,
    scale: 6,
    scaleV: 1,
    levels: 4,
    ratio: 0.03,
    ratioPower: 1.15,
    flare: 0.7,
    baseSplits: 2,
    baseSize: L(0.2, 0.06, 0.03, 0.02),
    length: L(1, 0.6, 0.45, 0.35),
    lengthV: L(0, 0.08, 0.08, 0.05),
    taper: L(1, 1, 1, 1),
    curveRes: L(8, 8, 6, 3),
    curve: L(0, 15, 10, 0),
    curveBack: L(0, -25, 0, 0),
    curveV: L(60, 90, 120, 100),
    bendV: L(20, 60, 60, 0),
    segSplits: L(0.2, 0.4, 0.2, 0),
    splitAngle: L(55, 45, 40, 0),
    splitAngleV: L(8, 10, 10, 0),
    downAngle: L(0, 52, 60, 50),
    downAngleV: L(0, -15, 20, 20),
    rotate: L(0, 130, 140, 140),
    rotateV: L(0, 30, 30, 20),
    branches: L(1, 8, 14, 12),
    branchDist: L(0, 0, 0, 0),
    radiusMod: L(1, 1, 1, 1),
    attractionUp: 0.3,
    flatten: 0.35,
    leaves: 24,
    leafScale: 0.09,
    leafScaleX: 0.9,
  }, {}, { count: 5, radius: 0.42, length: 0.26, laterals: 2, forks: 0.5 }),
];

/** Every species: trees first, then the grasses. */
export const PRESETS: TreeParams[] = [...TREE_PRESETS, ...GRASS_PRESETS.map((g) => grassPreset(g.name, g.grass))];

/** Grouping for the species list. Presets missing here are shown under "Other". */
export const PRESET_GROUPS: { label: string; names: string[] }[] = [
  { label: 'Oaks', names: ['English Oak', 'White Oak', 'Northern Red Oak', 'Southern Live Oak', 'Valley Oak', 'Pin Oak', 'Cork Oak', 'California Black Oak'] },
  { label: 'Forest · broadleaf', names: ['European Beech', 'Sugar Maple', 'Quaking Aspen', 'Black Tupelo', 'Silver Birch', 'Weeping Willow', 'Japanese Maple'] },
  { label: 'Forest · conifers', names: ['Scots Pine', 'Douglas Fir', 'Coast Redwood', 'Norway Spruce'] },
  { label: 'Jungle', names: ['Kapok', 'Rain-Forest Fig', 'Bamboo'] },
  { label: 'Savanna', names: ['Umbrella Thorn', 'Baobab', 'Marula', 'Fever Tree'] },
  { label: 'Desert', names: ['Joshua Tree', 'Desert Ironwood', 'Saguaro Cactus', 'Golden Barrel Cactus', 'Organ Pipe Cactus', 'Agave Americana', 'Aloe Vera', 'Ocotillo', 'Dasylirion Sotol'] },
  { label: 'Rocky terrain', names: ['Bristlecone Pine', 'Rowan'] },
  ...GRASS_GROUPS,
];

export function getPreset(name: string): TreeParams {
  const p = PRESETS.find((x) => x.name === name) ?? PRESETS[0];
  return cloneParams(p);
}
