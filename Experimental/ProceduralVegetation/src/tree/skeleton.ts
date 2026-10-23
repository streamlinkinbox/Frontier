/**
 * Botanical skeleton generator.
 *
 * Implements the parametric model of Weber & Penn, "Creation and Rendering of
 * Realistic Trees" (SIGGRAPH 1995): recursive stems with curvature, splits
 * (clones), phyllotactic child placement, crown-shape length envelopes,
 * taper / flare and tropism. The output is a hierarchy of *physical* stems:
 * polylines with rotation-minimising frames, explicit side attachments and
 * explicit forks, which is exactly what the welded mesher needs.
 *
 * Differences from a straight port of the paper, all made for meshing quality:
 *  - a stem that splits ends there; every continuation (including the
 *    original) becomes a fork child, so a fork is an explicit k-way node;
 *  - fork siblings are guaranteed a minimum angular separation;
 *  - side children start on the parent surface and leave it within a maximum
 *    angle from the surface normal, bending into their growth direction over
 *    the first segment (a real branch collar does the same);
 *  - child radius is clamped to a fraction of the parent's local radius so the
 *    junction window always fits into the parent ring.
 */

import {
  V3,
  UP,
  DEG2RAD,
  add,
  addScaled,
  clone,
  cross,
  dot,
  length,
  normalize,
  rotateAxis,
  rotateTowards,
  projectOnPlane,
  anyPerpendicular,
  declinationDeg,
  clamp,
  lengthSq,
  angleBetween,
  sub,
  scale as vscale,
} from '../core/math';
import { Random } from '../core/random';
import { BotanyParams, MeshParams, Shape, TreeParams } from './params';

export interface SkelNode {
  pos: V3;
  dir: V3;
  right: V3;
  /** Arc length from the physical stem base. */
  s: number;
}

export interface Leaf {
  pos: V3;
  dir: V3;
  right: V3;
  normal: V3;
  scale: number;
  stem: Stem;
  /** 0..1 along the carrying twig. */
  t: number;
}

export type AttachKind = 'root' | 'side' | 'fork';
/** Shoots are the Weber-Penn branch system; roots are grown by the environment-aware root builder. */
export type StemRole = 'shoot' | 'root';

export interface Stem {
  id: number;
  level: number;
  role: StemRole;
  parent: Stem | null;
  attach: AttachKind;
  /** Side attachment: arc length along the parent's polyline. */
  attachS: number;
  /** Side attachment: intended growth direction (before the collar bend). */
  attachDir: V3;
  nodes: SkelNode[];
  /** Physical arc length. */
  length: number;
  /** Logical (Weber-Penn) stem this physical stem is part of. */
  logicalLength: number;
  logicalStart: number;
  logicalRadius: number;
  radiusFactor: number;
  taper: number;
  /** Max relative length of children (length[level+1] ± V), sampled once per logical stem. */
  lengthChildMax: number;
  /** Logical offset of this stem's base along its parent (side children). */
  offset: number;
  children: Stem[];
  forks: Stem[];
  leaves: Leaf[];
  phase: number;
  limbRoot: Stem;
  distFromLimb: number;
  limbReach: number;
  /** Set by the mesher if the stem could not be welded (no room on the parent). */
  dropped: boolean;
  dropReason?: string;
  /**
   * Pipe-model factor applied to the parent's radius beyond this child's
   * attachment (sqrt(1 - c * (rc/rp)^2)). 1 = no reduction.
   */
  radiusDrop: number;
  /** Explicit ring spacing in metres (roots); shoots derive it from curveRes. */
  ringSpacing?: number;
  /** Minimum number of vertices in this stem's first ring (roots); shoots use the mesh default. */
  minRing?: number;
  /** Width of this stem's junction window in parent-ring columns, when laid out by the root builder (primary roots). */
  windowCols?: number;
  /** Id of the primary root this stem belongs to (roots). */
  primary?: number;
  /**
   * Environment-aware stems cannot simply be moved when the mesher relocates
   * their exit window: they must be regrown from the new exit so they still
   * follow the ground and the objects around them. Called with the new exit
   * point on the parent surface and the (azimuth-rotated) emergence direction.
   */
  regrow?: (origin: V3, emergeDir: V3) => void;
}

export interface Skeleton {
  roots: Stem[];
  /** Shoot stems (trunk, limbs, branches, twigs). */
  stems: Stem[];
  /** Primary roots attached to the trunk base; their subtrees hold laterals and forks. */
  rootSystem: Stem[];
  leaves: Leaf[];
  height: number;
  treeScale: number;
  params: TreeParams;
  stemsPerLevel: number[];
  /** How far the trunk continues below the ground (metres), set by the root builder. */
  groundDepth: number;
  /** Rotation of the buttress-lobe pattern (radians); the root builder aligns it with the site. */
  lobePhase: number;
}

/** Every stem of the tree: shoots first, then the whole root system (depth first). */
export function allStems(skel: Skeleton): Stem[] {
  const out = skel.stems.slice();
  const stack = skel.rootSystem.slice().reverse();
  while (stack.length) {
    const s = stack.pop()!;
    out.push(s);
    for (let i = s.forks.length - 1; i >= 0; i--) stack.push(s.forks[i]);
    for (let i = s.children.length - 1; i >= 0; i--) stack.push(s.children[i]);
  }
  return out;
}

interface Turtle {
  pos: V3;
  dir: V3;
  right: V3;
}

const copyTurtle = (t: Turtle): Turtle => ({ pos: clone(t.pos), dir: clone(t.dir), right: clone(t.right) });

function pitchDown(t: Turtle, deg: number): void {
  t.dir = normalize(rotateAxis(t.dir, t.right, -deg * DEG2RAD));
}
function turnLeft(t: Turtle, deg: number): void {
  const axis = normalize(cross(t.dir, t.right));
  t.dir = normalize(rotateAxis(t.dir, axis, -deg * DEG2RAD));
  t.right = normalize(rotateAxis(t.right, axis, -deg * DEG2RAD));
}
function rollRight(t: Turtle, deg: number): void {
  t.right = normalize(rotateAxis(t.right, t.dir, deg * DEG2RAD));
}
function rotateAroundUp(t: Turtle, deg: number): void {
  t.dir = normalize(rotateAxis(t.dir, UP, deg * DEG2RAD));
  t.right = normalize(rotateAxis(t.right, UP, deg * DEG2RAD));
}
/** Rotate the whole frame so that `dir` becomes `newDir` (minimal rotation). */
function setDirection(t: Turtle, newDir: V3): void {
  const axis = cross(t.dir, newDir);
  const s = length(axis);
  const c = clamp(dot(t.dir, newDir), -1, 1);
  if (s < 1e-9) {
    t.dir = clone(newDir);
    t.right = normalize(projectOnPlane(t.right, t.dir));
    return;
  }
  const ang = Math.atan2(s, c);
  const k = vscale(axis, 1 / s);
  t.dir = normalize(rotateAxis(t.dir, k, ang));
  t.right = normalize(projectOnPlane(rotateAxis(t.right, k, ang), t.dir));
}
function applyTropism(t: Turtle, tropism: V3, fraction: number): void {
  const hxt = cross(t.dir, tropism);
  const mag = length(hxt);
  if (mag < 1e-9) return;
  const alpha = 10 * mag * fraction * DEG2RAD;
  const axis = vscale(hxt, 1 / mag);
  t.dir = normalize(rotateAxis(t.dir, axis, alpha));
  t.right = normalize(rotateAxis(t.right, axis, alpha));
}

/** Pull the heading towards the horizontal plane (flat-topped canopies). */
function flattenTowardsHorizontal(t: Turtle, amount: number): void {
  const h = { x: t.dir.x, y: 0, z: t.dir.z };
  const hl = length(h);
  if (hl < 1e-6) return;
  const target = vscale(h, 1 / hl);
  const ang = angleBetween(t.dir, target);
  if (ang < 1e-5) return;
  const step = Math.min(ang, 12 * amount * DEG2RAD * Math.sin(ang));
  const nd = rotateTowards(t.dir, target, step);
  setDirection(t, nd);
}

/** Skeleton sub-steps per Weber-Penn segment (independent of mesh resolution). */
const SUBSTEPS = [6, 4, 3, 2];
/** Maximum angle between a side child and the parent surface normal at emergence. */
const MAX_EMERGE_DEG = 40;
/** Minimum angle between fork siblings. */
const MIN_FORK_DEG = 14;
/** Fraction of the parent's local radius a side child may reach. */
export const MAX_SIDE_RATIO = 0.68;

export function shapeRatio(shape: Shape, ratio: number): number {
  switch (shape) {
    case Shape.Spherical:
      return 0.2 + 0.8 * Math.sin(Math.PI * ratio);
    case Shape.Hemispherical:
      return 0.2 + 0.8 * Math.sin(0.5 * Math.PI * ratio);
    case Shape.Cylindrical:
      return 1.0;
    case Shape.TaperedCylindrical:
      return 0.5 + 0.5 * ratio;
    case Shape.Flame:
      return ratio <= 0.7 ? ratio / 0.7 : (1.0 - ratio) / 0.3;
    case Shape.InverseConical:
      return 1.0 - 0.8 * ratio;
    case Shape.TendFlame:
      return ratio <= 0.7 ? 0.5 + (0.5 * ratio) / 0.7 : 0.5 + (0.5 * (1.0 - ratio)) / 0.3;
    case Shape.Conical:
    default:
      return 0.2 + 0.8 * ratio;
  }
}

/** Weber-Penn radius profile (radius_at_offset) without the fork factor. */
export function taperRadius(radius0: number, nTaper: number, z: number, logicalLength: number): number {
  const unitTaper = nTaper < 1 ? nTaper : nTaper < 2 ? 2 - nTaper : 0;
  const taper = radius0 * (1 - unitTaper * z);
  if (nTaper < 1) return taper;
  const z2 = (1 - z) * logicalLength;
  const depth = nTaper < 2 || z2 < taper ? 1 : nTaper - 2;
  const z3 = nTaper < 2 ? z2 : Math.abs(z2 - 2 * taper * Math.floor(z2 / (2 * taper) + 0.5));
  if (nTaper < 2 && z3 >= taper) return taper;
  const inner = Math.max(0, taper * taper - (z3 - taper) * (z3 - taper));
  return (1 - depth) * taper + depth * Math.sqrt(inner);
}

/**
 * Culm nodes (bamboo): a short, smooth ridge every `nodeSpacing` of the trunk
 * length, `nodeSwell` high relative to the radius. Zero at the very base so
 * the trunk ring the roots are welded into stays round.
 */
export function nodeSwellFactor(botany: BotanyParams, z: number): number {
  const spacing = Math.max(0.01, botany.nodeSpacing);
  const u = (z / spacing) % 1; // 0..1 within an internode
  const w = 0.16; // ridge half-width as a fraction of the internode
  const d = Math.min(u, 1 - u) / w;
  if (d >= 1) return 1;
  const bump = 0.5 + 0.5 * Math.cos(Math.PI * d);
  return 1 + botany.nodeSwell * bump * Math.min(1, z / spacing);
}

export function flareFactor(flare: number, z: number): number {
  const y = Math.max(0, 1 - 8 * z);
  return flare * ((Math.pow(100, y) - 1) / 100) + 1;
}

/**
 * Pipe-model reduction of a stem's radius at arc length `s`: every side child
 * attached before `s` takes its share of the cross-section away (da Vinci's
 * rule, weighted by `forkRadiusConservation`). The reduction fades in across
 * the child's collar so the shoulder lands inside the junction window.
 */
export function pipeFactor(stem: Stem, s: number, mesh: MeshParams): number {
  let f = 1;
  for (const c of stem.children) {
    if (c.dropped || c.radiusDrop >= 1) continue;
    const hh = Math.max(1e-6, c.logicalRadius * mesh.collarScale);
    const t = clamp((s - (c.attachS - hh)) / (2 * hh), 0, 1);
    if (t <= 0) continue;
    const k = t * t * (3 - 2 * t);
    f *= 1 + (c.radiusDrop - 1) * k;
  }
  return f;
}

/** Radius of a physical stem at arc length `s`. */
export function stemRadiusAt(stem: Stem, s: number, mesh: MeshParams, botany: BotanyParams): number {
  const z = clamp(stem.logicalStart + s / stem.logicalLength, 0, 1);
  let r: number;
  if (stem.role === 'root') {
    // Roots: power-law taper to the tip, `taper` is the exponent (<1 keeps them thick for longer).
    r = stem.logicalRadius * Math.pow(Math.max(0, 1 - z), Math.max(0.05, stem.taper));
  } else {
    r = taperRadius(stem.logicalRadius, stem.taper, z, stem.logicalLength);
    if (stem.level === 0) {
      r *= flareFactor(botany.flare, z);
      if (botany.nodeSwell > 0) r *= nodeSwellFactor(botany, z);
    }
  }
  r *= stem.radiusFactor;
  if (stem.children.length) r *= pipeFactor(stem, s, mesh);
  return Math.max(mesh.minRadius, r);
}

/** Phase of the trunk's buttress lobes for a given seed (shared by the mesher and the root builder). */
export function lobePhaseFor(seed: number): number {
  return (seed % 360) * (Math.PI / 180);
}

/**
 * Buttress-lobe multiplier of the trunk radius at arc length `s` and ring
 * angle `theta` (measured in the trunk's (right, up) frame). 1 for anything
 * that is not the base of the trunk.
 */
export function trunkLobeFactor(mesh: MeshParams, stem: Stem, s: number, theta: number, lobePhase: number, lobes: number): number {
  if (stem.level !== 0 || stem.role === 'root' || stem.parent || lobes <= 0 || mesh.rootLobeAmplitude <= 0) return 1;
  // Measured from the ground: the trunk may carry a below-ground stub
  // (negative logicalStart) that keeps the full lobes.
  const sAbove = s + stem.logicalStart * stem.logicalLength;
  const lobeFall = clamp(1 - sAbove / Math.max(1e-6, mesh.rootLobeHeight * stem.logicalLength), 0, 1);
  if (lobeFall <= 0) return 1;
  const f = lobeFall * lobeFall;
  return 1 + mesh.rootLobeAmplitude * f * (0.5 + 0.5 * Math.cos(lobes * theta + lobePhase));
}

/** Sample position and frame of a polyline at arc length `s` (clamped). */
export function sampleStem(stem: Stem, s: number): { pos: V3; dir: V3; right: V3 } {
  const nodes = stem.nodes;
  if (s <= 0 || nodes.length === 1) {
    const n = nodes[0];
    return { pos: clone(n.pos), dir: clone(n.dir), right: clone(n.right) };
  }
  const last = nodes[nodes.length - 1];
  if (s >= last.s) return { pos: clone(last.pos), dir: clone(last.dir), right: clone(last.right) };
  // binary search
  let lo = 0;
  let hi = nodes.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (nodes[mid].s <= s) lo = mid;
    else hi = mid;
  }
  const a = nodes[lo];
  const b = nodes[hi];
  const t = (s - a.s) / Math.max(1e-9, b.s - a.s);
  const pos = {
    x: a.pos.x + (b.pos.x - a.pos.x) * t,
    y: a.pos.y + (b.pos.y - a.pos.y) * t,
    z: a.pos.z + (b.pos.z - a.pos.z) * t,
  };
  const dir = normalize({
    x: a.dir.x + (b.dir.x - a.dir.x) * t,
    y: a.dir.y + (b.dir.y - a.dir.y) * t,
    z: a.dir.z + (b.dir.z - a.dir.z) * t,
  });
  let right = projectOnPlane(
    { x: a.right.x + (b.right.x - a.right.x) * t, y: a.right.y + (b.right.y - a.right.y) * t, z: a.right.z + (b.right.z - a.right.z) * t },
    dir,
  );
  if (lengthSq(right) < 1e-10) right = anyPerpendicular(dir);
  else right = normalize(right);
  return { pos, dir, right };
}

export class SkeletonBuilder {
  private readonly p: BotanyParams;
  private readonly mesh: MeshParams;
  private rng: Random;
  private stems: Stem[] = [];
  private leaves: Leaf[] = [];
  private treeScale = 0;
  private readonly leafRng: Random;
  private baseLength = 0;
  private splitError = [0, 0, 0, 0, 0];
  private nextId = 0;
  private readonly tropism: V3;

  constructor(private readonly params: TreeParams) {
    this.p = params.botany;
    this.mesh = params.mesh;
    this.rng = new Random(params.seed);
    // Leaves draw from their own stream so leaf density / size never perturbs
    // the branch skeleton of a given seed.
    this.leafRng = new Random((params.seed ^ 0x1eaf5eed) >>> 0);
    this.tropism = { x: 0, y: this.p.attractionUp, z: 0 };
  }

  build(): Skeleton {
    const p = this.p;
    this.treeScale = p.scale + this.rng.uniform() * p.scaleV;

    const turtle: Turtle = { pos: { x: 0, y: 0, z: 0 }, dir: clone(UP), right: { x: 1, y: 0, z: 0 } };
    rollRight(turtle, this.rng.range(0, 360));

    const trunk = this.newStem(0, null, 'root');
    trunk.lengthChildMax = this.levelParam(p.length, 1) + this.rng.uniform() * this.levelParam(p.lengthV, 1);
    trunk.logicalLength = Math.max(0.1, this.treeScale * (p.length[0] + this.rng.uniform() * p.lengthV[0]));
    trunk.logicalRadius = trunk.logicalLength * p.ratio * p.radiusMod[0];
    trunk.taper = p.taper[0];
    this.baseLength = trunk.logicalLength * p.baseSize[0];

    this.makeStem(turtle, trunk, 0, 0, 1, 1);

    // Post pass: limb reach (for wind amplitude) and height.
    let height = 0;
    for (const s of this.stems) {
      for (const n of s.nodes) if (n.pos.y > height) height = n.pos.y;
      const reach = s.distFromLimb + s.length;
      if (reach > s.limbRoot.limbReach) s.limbRoot.limbReach = reach;
    }
    for (const s of this.stems) s.limbReach = Math.max(1e-6, s.limbRoot.limbReach);

    const stemsPerLevel = [0, 0, 0, 0, 0];
    for (const s of this.stems) stemsPerLevel[Math.min(4, s.level)]++;

    return {
      roots: [trunk],
      stems: this.stems,
      rootSystem: [],
      groundDepth: 0,
      lobePhase: lobePhaseFor(this.params.seed),
      leaves: this.leaves,
      height,
      treeScale: this.treeScale,
      params: this.params,
      stemsPerLevel,
    };
  }

  private levelParam(arr: readonly number[], level: number): number {
    return arr[Math.min(level, arr.length - 1)];
  }

  private newStem(level: number, parent: Stem | null, attach: AttachKind): Stem {
    const stem: Stem = {
      id: this.nextId++,
      level,
      role: 'shoot',
      parent,
      attach,
      attachS: 0,
      attachDir: clone(UP),
      nodes: [],
      length: 0,
      logicalLength: 0,
      logicalStart: 0,
      logicalRadius: 0,
      radiusFactor: 1,
      taper: this.levelParam(this.p.taper, level),
      lengthChildMax: 0,
      offset: 0,
      children: [],
      forks: [],
      leaves: [],
      phase: this.rng.next(),
      limbRoot: null as unknown as Stem,
      distFromLimb: 0,
      limbReach: 0,
      dropped: false,
      radiusDrop: 1,
    };
    stem.limbRoot = stem;
    this.stems.push(stem);
    return stem;
  }

  // ---------------------------------------------------------------------------
  // Stem construction
  // ---------------------------------------------------------------------------

  private makeStem(
    turtle: Turtle,
    stem: Stem,
    start: number,
    splitCorrAngle: number,
    branchesFactor: number,
    cloneProb: number,
    emergeTarget?: V3,
  ): void {
    const p = this.p;
    const level = stem.level;
    const lp = Math.min(level + 1, 3);
    const curveRes = Math.max(1, Math.round(this.levelParam(p.curveRes, level)));
    const segSplits = this.levelParam(p.segSplits, level);
    const segLength = stem.logicalLength / curveRes;
    const baseSegIndex = Math.ceil(p.baseSize[0] * Math.max(1, Math.round(p.curveRes[0])));
    const substeps = SUBSTEPS[Math.min(level, 3)];
    const isLastLevel = level === p.levels - 1;

    // Children / leaves budget for this physical stem.
    let leafCount = 0;
    let branchCount = 0;
    const tuft = clamp(p.leafTuft, 0.05, 1);
    if (isLastLevel && p.leaves > 0) {
      // Tufted foliage keeps the same number of leaves but packs them into the
      // last `tuft` of the stem, so the per-segment budget is scaled up.
      leafCount = (this.calcLeafCount(stem) * (1 - start / curveRes)) / tuft;
    } else if (!isLastLevel) {
      branchCount = this.calcBranchCount(stem) * (1 - start / curveRes) * branchesFactor;
    }
    let perSegBranches = branchCount / curveRes;
    const perSegLeaves = leafCount / curveRes;
    let branchError = 0;
    let leafError = 0;

    const prevRot = { v: this.levelParam(p.rotate, lp) >= 0 ? this.rng.range(0, 360) : 1 };

    // Node 0.
    stem.nodes.push({ pos: clone(turtle.pos), dir: clone(turtle.dir), right: clone(turtle.right), s: 0 });
    let s = 0;
    let pendingPitch = 0;
    let pendingTurn = 0;
    let emergeAngle = 0;
    if (emergeTarget) emergeAngle = angleBetween(turtle.dir, emergeTarget);

    for (let seg = start + 1; seg <= curveRes; seg++) {
      const remainingSegs = curveRes + 1 - seg;

      // --- advance one segment in sub-steps -------------------------------
      const stepLen = segLength / substeps;
      for (let k = 0; k < substeps; k++) {
        if (pendingPitch !== 0) pitchDown(turtle, pendingPitch / substeps);
        if (pendingTurn !== 0) turnLeft(turtle, pendingTurn / substeps);
        if (emergeTarget && emergeAngle > 1e-4 && seg === start + 1) {
          // Bend from the emergence direction into the growth direction over the first segment.
          const nd = rotateTowards(turtle.dir, emergeTarget, emergeAngle / substeps);
          setDirection(turtle, nd);
        }
        if (level > 1) {
          applyTropism(turtle, this.tropism, 1 / substeps);
          if (p.flatten > 0) flattenTowardsHorizontal(turtle, p.flatten / substeps);
        }
        turtle.pos = addScaled(turtle.pos, turtle.dir, stepLen);
        s += stepLen;
        stem.nodes.push({ pos: clone(turtle.pos), dir: clone(turtle.dir), right: clone(turtle.right), s });
      }
      stem.length = s;

      // --- splits for this segment ---------------------------------------
      let numSplits = 0;
      const isBaseSplit = p.baseSplits !== 0 && level === 0 && seg === baseSegIndex;
      if (seg < curveRes) {
        if (isBaseSplit) {
          numSplits = p.baseSplits < 0 ? Math.floor(this.rng.next() * (Math.abs(p.baseSplits) + 0.5)) : Math.floor(p.baseSplits);
        } else if (segSplits > 0 && (level > 0 || seg > baseSegIndex)) {
          if (this.rng.next() <= cloneProb) {
            numSplits = Math.floor(segSplits + this.splitError[level]);
            this.splitError[level] -= numSplits - segSplits;
            if (numSplits > 0) {
              cloneProb /= numSplits + 1;
              branchesFactor /= numSplits + 1;
              branchesFactor = Math.max(0.8, branchesFactor);
              branchCount *= branchesFactor;
              perSegBranches = branchCount / curveRes;
            }
          }
        }
      }

      // --- children on this segment ---------------------------------------
      if (branchCount > 0 && !isLastLevel) {
        const onSeg = Math.floor(perSegBranches + branchError);
        branchError -= onSeg - perSegBranches;
        if (onSeg > 0) this.makeBranches(stem, seg, curveRes, segLength, start, onSeg, prevRot);
      } else if (leafCount > 0 && seg > (1 - tuft) * curveRes - 1) {
        const onSeg = Math.floor(perSegLeaves + leafError);
        leafError -= onSeg - perSegLeaves;
        if (onSeg > 0) this.makeLeaves(stem, seg, curveRes, segLength, start, onSeg, prevRot);
      }

      // --- fork -----------------------------------------------------------
      if (numSplits > 0) {
        stem.length = s;
        this.makeFork(turtle, stem, seg, curveRes, remainingSegs, numSplits, isBaseSplit, branchesFactor, cloneProb);
        return;
      }

      // --- curvature for the next segment ----------------------------------
      if (seg < curveRes) {
        pendingTurn = this.rng.uniform() * this.levelParam(p.bendV, level) / curveRes;
        pendingPitch = this.calcCurveAngle(level, seg, curveRes) - splitCorrAngle;
      }
    }
    stem.length = s;

    // Tufted species (yucca rosettes, foxtail-pine brushes) carry foliage on
    // every bare tip, not only on the deepest level: an arm that produced no
    // children ends in a tuft as well.
    if (!isLastLevel && tuft < 1 && p.leaves > 0 && stem.children.length === 0) {
      const perSeg = this.calcLeafCount(stem) / tuft / curveRes;
      let err = 0;
      for (let seg = Math.max(start + 1, 1); seg <= curveRes; seg++) {
        if (seg <= (1 - tuft) * curveRes - 1) continue;
        const onSeg = Math.floor(perSeg + err);
        err -= onSeg - perSeg;
        if (onSeg > 0) this.makeLeaves(stem, seg, curveRes, segLength, start, onSeg, prevRot);
      }
    }
  }

  private makeFork(
    turtle: Turtle,
    stem: Stem,
    seg: number,
    curveRes: number,
    remainingSegs: number,
    numSplits: number,
    isBaseSplit: boolean,
    branchesFactor: number,
    cloneProb: number,
  ): void {
    const p = this.p;
    const level = stem.level;
    const declination = declinationDeg(turtle.dir);
    let splAngle = Math.abs(this.levelParam(p.splitAngle, level)) + this.rng.uniform() * this.levelParam(p.splitAngleV, level) - declination;
    splAngle = Math.max(0, splAngle);
    const splitCorr = splAngle / remainingSegs;
    const r = this.rng.next();
    const sprAngle = -(20 + 0.75 * (30 + Math.abs(declination - 90) * r * r));

    const turtles: Turtle[] = [];
    // Clones.
    for (let i = 0; i < numSplits; i++) {
      const t = copyTurtle(turtle);
      pitchDown(t, splAngle / 2);
      let eff: number;
      if (isBaseSplit) eff = (i + 1) * (360 / (numSplits + 1)) + this.rng.uniform() * this.levelParam(p.splitAngleV, level);
      else eff = i === 0 ? sprAngle / 2 : -sprAngle / 2;
      rotateAroundUp(t, eff);
      turtles.push(t);
    }
    // Original continuation.
    const orig = copyTurtle(turtle);
    pitchDown(orig, splAngle / 2);
    if (!isBaseSplit && numSplits === 1) rotateAroundUp(orig, -sprAngle / 2);
    turtles.unshift(orig);

    this.enforceForkSeparation(turtles);

    const k = turtles.length;
    const factor = Math.pow(1 / k, 0.5 * this.mesh.forkRadiusConservation);
    for (const t of turtles) {
      const child = this.newStem(level, stem, 'fork');
      child.logicalLength = stem.logicalLength;
      child.logicalStart = seg / curveRes;
      child.logicalRadius = stem.logicalRadius;
      child.radiusFactor = stem.radiusFactor * factor;
      child.taper = stem.taper;
      child.lengthChildMax = stem.lengthChildMax;
      child.offset = stem.offset;
      child.limbRoot = stem.limbRoot;
      child.distFromLimb = stem.distFromLimb + stem.length;
      child.attachDir = clone(t.dir);
      stem.forks.push(child);
      this.makeStem(t, child, seg, splitCorr, branchesFactor, cloneProb);
    }
  }

  /** Spread fork siblings apart until every pair is at least MIN_FORK_DEG apart. */
  private enforceForkSeparation(turtles: Turtle[]): void {
    const minAng = MIN_FORK_DEG * DEG2RAD;
    for (let iter = 0; iter < 8; iter++) {
      let moved = false;
      for (let i = 0; i < turtles.length; i++) {
        for (let j = i + 1; j < turtles.length; j++) {
          const a = turtles[i];
          const b = turtles[j];
          const ang = angleBetween(a.dir, b.dir);
          if (ang >= minAng) continue;
          let axis = cross(a.dir, b.dir);
          if (lengthSq(axis) < 1e-10) axis = anyPerpendicular(a.dir);
          axis = normalize(axis);
          const delta = (minAng - ang) / 2 + 1e-3;
          setDirection(a, normalize(rotateAxis(a.dir, axis, -delta)));
          setDirection(b, normalize(rotateAxis(b.dir, axis, delta)));
          moved = true;
        }
      }
      if (!moved) break;
    }
  }

  // ---------------------------------------------------------------------------
  // Children
  // ---------------------------------------------------------------------------

  private makeBranches(
    stem: Stem,
    seg: number,
    curveRes: number,
    segLength: number,
    start: number,
    count: number,
    prevRot: { v: number },
  ): void {
    const p = this.p;
    const lp = Math.min(stem.level + 1, 3);
    const branchDist = this.levelParam(p.branchDist, lp);
    const baseLength = stem.logicalLength * this.levelParam(p.baseSize, stem.level);
    const startOffset = start * segLength;

    if (branchDist > 1) {
      // Whorled.
      const perWhorl = Math.round(branchDist + 1);
      const numWhorls = Math.max(1, Math.floor(count / perWhorl));
      for (let w = 0; w < numWhorls; w++) {
        const offset = clamp((w + 0.5) / numWhorls, 0, 1);
        const stemOffset = ((seg - 1 + offset) / curveRes) * stem.logicalLength;
        if (stemOffset > baseLength) {
          for (let b = 0; b < perWhorl; b++) {
            const rAngle = prevRot.v + (360 * b) / perWhorl + this.rng.uniform() * this.levelParam(p.rotateV, lp);
            this.setUpBranch(stem, stemOffset - startOffset, stemOffset, rAngle);
          }
        }
        prevRot.v += this.levelParam(p.rotate, lp);
      }
      return;
    }

    for (let b = 0; b < count; b++) {
      let offset: number;
      if (b % 2 === 0) offset = clamp(b / count, 0, 1);
      else offset = clamp((b - branchDist) / count, 0, 1);
      // Keep children off the exact segment boundaries.
      offset = 0.08 + offset * 0.84;
      const stemOffset = ((seg - 1 + offset) / curveRes) * stem.logicalLength;
      if (stemOffset <= baseLength) continue;
      const rAngle = this.calcRotateAngle(lp, prevRot);
      this.setUpBranch(stem, stemOffset - startOffset, stemOffset, rAngle);
    }
  }

  private setUpBranch(stem: Stem, sLocal: number, stemOffset: number, rAngle: number): void {
    const p = this.p;
    const level = stem.level + 1;
    if (sLocal <= 0 || sLocal >= stem.length) return;

    const sample = sampleStem(stem, sLocal);
    const childRight = normalize(rotateAxis(sample.right, sample.dir, rAngle * DEG2RAD));
    const dAngle = this.calcDownAngle(stem, stemOffset);
    const childDir = normalize(rotateAxis(sample.dir, childRight, -dAngle * DEG2RAD));

    const child = this.newStem(level, stem, 'side');
    child.offset = stemOffset;
    child.lengthChildMax = this.levelParam(p.length, level + 1) + this.rng.uniform() * this.levelParam(p.lengthV, level + 1);
    child.logicalLength = this.calcStemLength(child, stem);
    if (child.logicalLength < this.mesh.minRadius * 4) {
      this.stems.pop();
      this.nextId--;
      return;
    }
    const radiusLimit = stemRadiusAt(stem, sLocal, this.mesh, p);
    child.logicalRadius = this.calcStemRadius(child, stem, radiusLimit);
    if (this.mesh.cullRadius > 0 && child.logicalRadius < this.mesh.cullRadius) {
      this.stems.pop();
      this.nextId--;
      return;
    }
    const share = (child.logicalRadius / Math.max(1e-6, radiusLimit)) ** 2;
    child.radiusDrop = clamp(Math.sqrt(Math.max(0, 1 - this.mesh.forkRadiusConservation * share)), 0.55, 1);
    child.attachS = sLocal;
    child.attachDir = childDir;
    child.limbRoot = level <= 1 ? child : stem.limbRoot;
    child.distFromLimb = level <= 1 ? 0 : stem.distFromLimb + sLocal;

    // Emergence: the child leaves the parent surface at the hole centre.
    let radial = projectOnPlane(childDir, sample.dir);
    if (lengthSq(radial) < 1e-8) radial = childRight;
    radial = normalize(radial);
    const origin = addScaled(sample.pos, radial, radiusLimit);
    const emergeDir = rotateTowards(radial, childDir, MAX_EMERGE_DEG * DEG2RAD);
    let right = projectOnPlane(childRight, emergeDir);
    if (lengthSq(right) < 1e-8) right = anyPerpendicular(emergeDir);
    const turtle: Turtle = { pos: origin, dir: emergeDir, right: normalize(right) };

    stem.children.push(child);
    this.makeStem(turtle, child, 0, 0, 1, 1, childDir);
  }

  private makeLeaves(
    stem: Stem,
    seg: number,
    curveRes: number,
    segLength: number,
    start: number,
    count: number,
    prevRot: { v: number },
  ): void {
    const p = this.p;
    const lp = Math.min(stem.level + 1, 3);
    const startOffset = start * segLength;
    const leafScale = p.leafScale * (this.treeScale / Math.max(1e-6, p.scale));
    const tuftStart = (1 - clamp(p.leafTuft, 0.05, 1)) * stem.logicalLength;
    for (let b = 0; b < count; b++) {
      const offset = clamp((b + 0.5) / count, 0, 1);
      const stemOffset = ((seg - 1 + offset) / curveRes) * stem.logicalLength;
      const sLocal = stemOffset - startOffset;
      if (sLocal <= 0 || sLocal > stem.length || stemOffset < tuftStart) continue;
      const rAngle = this.calcRotateAngle(lp, prevRot, this.leafRng);
      const sample = sampleStem(stem, sLocal);
      const right = normalize(rotateAxis(sample.right, sample.dir, rAngle * DEG2RAD));
      const dAngle = this.calcDownAngle(stem, stemOffset);
      const dir = normalize(rotateAxis(sample.dir, right, -dAngle * DEG2RAD));
      let radial = projectOnPlane(dir, sample.dir);
      if (lengthSq(radial) < 1e-8) radial = right;
      radial = normalize(radial);
      const radius = stemRadiusAt(stem, sLocal, this.mesh, p);
      const pos = addScaled(sample.pos, radial, radius * 0.8);
      // Leaf plane: keep the blade roughly facing up while following the stalk direction.
      let leafRight = cross(UP, dir);
      if (lengthSq(leafRight) < 1e-6) leafRight = right;
      leafRight = normalize(leafRight);
      const normal = normalize(cross(dir, leafRight));
      const leaf: Leaf = {
        pos,
        dir,
        right: leafRight,
        normal,
        scale: leafScale * this.leafRng.range(0.85, 1.15),
        stem,
        t: clamp(sLocal / Math.max(1e-6, stem.length), 0, 1),
      };
      stem.leaves.push(leaf);
      this.leaves.push(leaf);
    }
  }

  // ---------------------------------------------------------------------------
  // Weber-Penn helper formulas
  // ---------------------------------------------------------------------------

  private calcStemLength(stem: Stem, parent: Stem): number {
    const p = this.p;
    if (stem.level === 1) {
      const denom = Math.max(1e-6, parent.logicalLength - this.baseLength);
      const ratio = clamp((parent.logicalLength - stem.offset) / denom, 0, 1);
      return Math.max(0, parent.logicalLength * parent.lengthChildMax * shapeRatio(p.shape, ratio));
    }
    return Math.max(0, parent.lengthChildMax * (parent.logicalLength - 0.7 * stem.offset));
  }

  private calcStemRadius(stem: Stem, parent: Stem, radiusLimit: number): number {
    const p = this.p;
    let r = this.levelParam(p.radiusMod, stem.level) * parent.logicalRadius * Math.pow(stem.logicalLength / Math.max(1e-6, parent.logicalLength), p.ratioPower);
    r = Math.max(this.mesh.minRadius, r);
    r = Math.min(r, radiusLimit * MAX_SIDE_RATIO);
    return r;
  }

  private calcCurveAngle(level: number, seg: number, curveRes: number): number {
    const p = this.p;
    const curve = this.levelParam(p.curve, level);
    const curveV = this.levelParam(p.curveV, level);
    const curveBack = this.levelParam(p.curveBack, level);
    let angle: number;
    if (curveBack === 0) angle = curve / curveRes;
    else angle = seg < curveRes / 2 ? curve / (curveRes / 2) : curveBack / (curveRes / 2);
    angle += (this.rng.uniform() * curveV) / curveRes;
    return angle;
  }

  private calcDownAngle(stem: Stem, stemOffset: number): number {
    const p = this.p;
    const lp = Math.min(stem.level + 1, 3);
    const dv = this.levelParam(p.downAngleV, lp);
    const d = this.levelParam(p.downAngle, lp);
    if (dv >= 0) return d + this.rng.uniform() * dv;
    const base = this.levelParam(p.baseSize, stem.level);
    const denom = Math.max(1e-6, stem.logicalLength * (1 - base));
    const ratio = clamp((stem.logicalLength - stemOffset) / denom, 0, 1);
    let angle = d + dv * (1 - 2 * shapeRatio(Shape.Conical, ratio));
    angle += this.rng.uniform() * Math.abs(angle * 0.1);
    return angle;
  }

  private calcRotateAngle(lp: number, prevRot: { v: number }, rng: Random = this.rng): number {
    const p = this.p;
    const rot = this.levelParam(p.rotate, lp);
    const rotV = this.levelParam(p.rotateV, lp);
    if (rot >= 0) {
      const r = (prevRot.v + rot + rng.uniform() * rotV) % 360;
      prevRot.v = r;
      return r;
    }
    const r = prevRot.v * (180 + rot + rng.uniform() * rotV);
    prevRot.v = -prevRot.v;
    return r;
  }

  private calcLeafCount(stem: Stem): number {
    const p = this.p;
    const parent = stem.parent;
    const leaves = (p.leaves * this.treeScale) / Math.max(1e-6, p.scale);
    if (!parent) return leaves;
    // Fork children continue the logical stem: use the logical parent for the ratio.
    const logical = stem.attach === 'fork' ? this.logicalParent(stem) : parent;
    if (!logical) return leaves;
    return leaves * (stem.logicalLength / Math.max(1e-6, logical.lengthChildMax * logical.logicalLength));
  }

  private logicalParent(stem: Stem): Stem | null {
    let s: Stem | null = stem;
    while (s && s.attach === 'fork') s = s.parent;
    return s ? s.parent : null;
  }

  private calcBranchCount(stem: Stem): number {
    const p = this.p;
    const lp = Math.min(stem.level + 1, 3);
    const n = this.levelParam(p.branches, lp);
    let result: number;
    if (stem.level === 0) {
      result = n * (this.rng.next() * 0.2 + 0.9);
    } else {
      const parent = this.logicalParent(stem);
      if (!parent) result = n;
      else if (stem.level === 1) {
        result = n * (0.2 + (0.8 * (stem.logicalLength / Math.max(1e-6, parent.logicalLength))) / Math.max(1e-6, parent.lengthChildMax));
      } else {
        result = n * (1.0 - (0.5 * stem.offset) / Math.max(1e-6, parent.logicalLength));
      }
    }
    return result / Math.max(0.05, 1 - this.levelParam(p.baseSize, stem.level));
  }
}

export function buildSkeleton(params: TreeParams): Skeleton {
  return new SkeletonBuilder(params).build();
}

/** Rigidly transform a stem and its whole subtree. */
export function transformSubtree(stem: Stem, fn: (pos: V3) => V3, rot: (dir: V3) => V3): void {
  const stack: Stem[] = [stem];
  while (stack.length) {
    const s = stack.pop()!;
    for (const n of s.nodes) {
      n.pos = fn(n.pos);
      n.dir = rot(n.dir);
      n.right = rot(n.right);
    }
    s.attachDir = rot(s.attachDir);
    for (const leaf of s.leaves) {
      leaf.pos = fn(leaf.pos);
      leaf.dir = rot(leaf.dir);
      leaf.right = rot(leaf.right);
      leaf.normal = rot(leaf.normal);
    }
    for (const c of s.children) stack.push(c);
    for (const f of s.forks) stack.push(f);
  }
}

export function translateSubtree(stem: Stem, delta: V3): void {
  transformSubtree(stem, (p) => add(p, delta), (d) => d);
}

/** Rotate a subtree around an axis through `origin`. */
export function rotateSubtree(stem: Stem, origin: V3, axis: V3, angle: number): void {
  transformSubtree(
    stem,
    (p) => add(origin, rotateAxis(sub(p, origin), axis, angle)),
    (d) => normalize(rotateAxis(d, axis, angle)),
  );
}
