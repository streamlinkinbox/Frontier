/**
 * Environment-aware root system.
 *
 * Roots are not Weber-Penn stems: their shape is dictated by what they grow
 * against, not by a crown envelope. Each root is grown by a turtle that, at
 * every step, senses the environment (ground + the objects the user placed)
 * and steers:
 *
 *  - it follows the *terrain height field* – the union of the ground and the
 *    tops of the objects – keeping its axis at a controlled depth (partly
 *    buried on soil, resting on / pressed into an object it climbs);
 *  - the objects' signed distance fields are hard constraints: the axis is
 *    pushed out of any object it penetrates (minus the "grip" allowance), so
 *    a root that meets a rock flank slides up it, over the top and down the
 *    far side, hugging the surface throughout;
 *  - objects too tall to climb are skirted: the heading is rotated onto the
 *    object's contour, and the push-out makes the root wrap around its base;
 *  - a tunable attraction pulls roots towards nearby objects so they seek
 *    them out and grip them;
 *  - roots avoid each other and the trunk, meander (low-frequency noise),
 *    sprout laterals and may fork once; the tip dives underground.
 *
 * Roots are attached to the trunk as ordinary side children, so the welded
 * mesher gives them a window in the trunk ring grid and a collar – the same
 * closed-loop join every branch gets. Because the mesher may shift a window
 * to resolve conflicts, every root carries a `regrow` closure that rebuilds
 * it from a new exit point (rigidly moving an environment-fitted root would
 * break its contact with the ground and the objects).
 */

import { V3, TAU, DEG2RAD, add, addScaled, clamp, cross, dot, length, lerp1, normalize, projectOnPlane, rotateAxis, rotateTowards, scale, sub, v3 } from '../core/math';
import { Random } from '../core/random';
import { Environment } from '../env/environment';
import { valueNoise3 } from '../env/noise';
import { MeshParams, RootParams, TreeParams } from './params';
import { MAX_SIDE_RATIO, Skeleton, SkelNode, Stem, sampleStem, stemRadiusAt, trunkLobeFactor } from './skeleton';

/** Stem level used for roots in per-vertex attributes (0..3 are shoot levels). */
export const ROOT_LEVEL = 4;

/** Lobes at the trunk base: driven by the root count when roots are enabled. */
export function effectiveRootLobes(p: TreeParams): number {
  if (p.roots.enabled && p.roots.count > 0) return Math.max(3, Math.round(p.roots.count));
  return Math.round(p.mesh.rootLobes);
}

interface LateralPlan {
  /** Logical fraction along the parent. */
  z: number;
  side: number;
  seed: number;
  lengthFrac: number;
  radiusFrac: number;
  yawDeg: number;
  elevDeg: number;
}

interface RootPlan {
  seed: number;
  radius: number;
  length: number;
  forkZ: number | null;
  forkYawDeg: number;
  laterals: LateralPlan[];
  /** Horizontal direction a primary heads for once it has left the collar (an object it targets). */
  goal?: V3;
  /** The root starts on an object (a lateral of a climbing parent): grow in climbing contact from the first step. */
  onObject?: boolean;
}

interface RegNode {
  p: V3;
  r: number;
  primary: number;
}

interface GrowState {
  p: V3;
  d: V3;
  right: V3;
  /** Actual horizontal heading. */
  h: V3;
  /**
   * Goal heading: where the root *wants* to go. Contact with objects and
   * other roots deflects `h` transiently; a spring pulls it back towards `g`,
   * so a root that has been pushed around a rock resumes its course instead
   * of orbiting the rock. Meander and attraction act on `g` (persistent).
   */
  g: V3;
  slope: number;
  s: number;
}

const UPV: Readonly<V3> = { x: 0, y: 1, z: 0 };

export class RootBuilder {
  private readonly R: RootParams;
  private readonly M: MeshParams;
  private readonly trunk: Stem;
  private readonly lobes: number;
  private lobePhase: number;
  private readonly registry: RegNode[] = [];
  private nextId: number;
  private primaries: Stem[] = [];

  constructor(private readonly skel: Skeleton, private readonly env: Environment) {
    this.R = skel.params.roots;
    this.M = skel.params.mesh;
    this.trunk = skel.roots[0];
    this.lobes = effectiveRootLobes(skel.params);
    this.lobePhase = skel.lobePhase;
    this.nextId = skel.stems.length ? Math.max(...skel.stems.map((s) => s.id)) + 1 : 0;
  }

  build(): Stem[] {
    const R = this.R;
    if (!R.enabled || R.count <= 0) return [];
    const rng = new Random(((this.skel.params.seed * 2654435761) >>> 0) ^ 0x726f6f74);
    const trunk = this.trunk;
    const count = Math.max(1, Math.round(R.count));
    const rNom = trunk.logicalRadius;

    // --- site analysis -------------------------------------------------------
    // Objects within reach are targets: the buttress-lobe pattern (and with it
    // the root exits, which sit on the lobe peaks) is rotated so that lobes
    // face the objects, and each root whose sector contains an object heads
    // for it and is long enough to reach across it.
    const frame = sampleStem(trunk, 1e-3);
    const frameUp = cross(frame.dir, frame.right);
    const thetaOf = (w: V3): number => Math.atan2(dot(w, frameUp), dot(w, frame.right));
    const reachMax = this.skel.treeScale * (R.length + R.lengthV);
    const targets: { theta: number; dist: number; far: number; dir: V3; weight: number }[] = [];
    for (let i = 0; i < this.env.count; i++) {
      const c = this.env.center(i);
      const dist = Math.hypot(c.x, c.z);
      const bound = this.env.bound(i);
      if (dist < 1e-6 || dist - bound > reachMax * 1.1) continue;
      const dir = v3(c.x / dist, 0, c.z / dist);
      const weight = (1 - clamp((dist - bound) / (reachMax * 1.1), 0, 1)) * Math.min(1, bound / (2 * rNom));
      targets.push({ theta: thetaOf(dir), dist, far: dist + bound, dir, weight });
    }
    if (targets.length > 0 && R.attraction > 0) {
      // Phase maximising Σ w·cos(lobes·θ + phase): lobe peaks face the objects.
      let sx = 0;
      let sy = 0;
      for (const t of targets) {
        sx += t.weight * Math.cos(this.lobes * t.theta);
        sy += t.weight * Math.sin(this.lobes * t.theta);
      }
      if (sx * sx + sy * sy > 1e-8) {
        const aligned = -Math.atan2(sy, sx);
        // Blend with the seed phase by the attraction setting (0 = leave the pattern alone).
        let d = aligned - this.lobePhase;
        while (d > Math.PI) d -= TAU;
        while (d < -Math.PI) d += TAU;
        this.lobePhase += d * clamp(R.attraction, 0, 1);
        this.skel.lobePhase = this.lobePhase;
      }
    }
    const sector = Math.PI / this.lobes;
    const claimed = new Set<number>();

    const plans: { plan: RootPlan; theta: number }[] = [];
    for (let k = 0; k < count; k++) {
      // Roots leave the trunk at the buttress lobes; lobe peaks sit at cos(lobes*theta + phase) = 1.
      const jitter = rng.uniform() * 0.12 * (TAU / this.lobes);
      const theta = (TAU * (k % this.lobes) - this.lobePhase) / this.lobes + jitter + (k >= this.lobes ? TAU / (2 * this.lobes) : 0);
      const radius = Math.max(this.M.minRadius * 3, rNom * R.radius * (1 + rng.uniform() * R.radiusV));
      let len = Math.max(radius * 6, this.skel.treeScale * (R.length + rng.uniform() * R.lengthV));

      // The object in this root's sector (nearest first, each object claimed once).
      let goal: V3 | undefined;
      if (R.attraction > 0) {
        let best = -1;
        let bestD = Infinity;
        targets.forEach((t, i) => {
          if (claimed.has(i)) return;
          let d = t.theta - theta;
          while (d > Math.PI) d -= TAU;
          while (d < -Math.PI) d += TAU;
          if (Math.abs(d) <= sector * 0.95 && t.dist < bestD) {
            best = i;
            bestD = t.dist;
          }
        });
        if (best >= 0) {
          const t = targets[best];
          claimed.add(best);
          goal = t.dir;
          // Long enough to cross the object and dive behind it.
          len = Math.max(len, Math.min(t.far + 0.35 * len, 1.6 * len));
        }
      }

      const plan: RootPlan = {
        seed: rng.int(0x7fffffff),
        radius,
        length: len,
        forkZ: rng.next() < R.forks ? rng.range(0.3, 0.55) : null,
        forkYawDeg: rng.range(18, 34),
        laterals: [],
        goal,
      };
      const nLat = Math.max(0, Math.round(R.laterals * (0.75 + rng.next() * 0.5)));
      for (let i = 0; i < nLat; i++) {
        const z = 0.16 + ((i + rng.range(0.25, 0.75)) / nLat) * (R.dive - 0.2);
        plan.laterals.push({
          z,
          side: i % 2 === 0 ? 1 : -1,
          seed: rng.int(0x7fffffff),
          lengthFrac: R.lateralLength * rng.range(0.7, 1.3),
          radiusFrac: R.lateralRadius * rng.range(0.8, 1.2),
          yawDeg: rng.range(45, 80),
          elevDeg: rng.range(15, 35),
        });
      }
      plans.push({ plan, theta });
    }

    // --- junction layout -------------------------------------------------------
    // Every primary gets a window in the trunk's ring grid at ground level: the
    // exits are quantised to the column grid and the windows narrowed (and
    // made taller – a buttress leaves the trunk as an upright fin anyway)
    // where the ring is crowded, so that all of them sit side by side with a
    // free column between neighbours. Only a ring with too few columns for
    // the count sends the remaining roots one tier down, below the ground.
    const N = Math.max(3, Math.round(this.M.trunkRadialSegments));
    const colStep = TAU / N;
    type Slot = { k: number; c: number; w: number; u: number; tier: number };
    const slots: Slot[] = plans.map(({ plan, theta }, k) => {
      let c = (theta / colStep) % N;
      if (c < 0) c += N;
      return { k, c, w: this.windowEstimate(plan.radius).w, u: 0, tier: 0 };
    });
    const perTier = Math.max(1, Math.floor(N / 2)); // every window needs its columns plus a free one
    const byRadius = slots.slice().sort((a, b) => plans[b.k].plan.radius - plans[a.k].plan.radius);
    const tiers: Slot[][] = [];
    for (let i = 0; i < byRadius.length; i += perTier) tiers.push(byRadius.slice(i, i + perTier));
    tiers.forEach((group, t) => {
      for (const sl of group) sl.tier = t;
      this.layoutWindows(group, N);
    });
    for (const sl of slots) plans[sl.k].theta = (sl.u + sl.w / 2) * colStep;
    if (this.lobes > 0 && slots.length > 0) {
      // Re-fit the lobe pattern to the quantised exits (peaks on the roots).
      let sx = 0;
      let sy = 0;
      for (const sl of slots) {
        sx += Math.cos(this.lobes * plans[sl.k].theta);
        sy += Math.sin(this.lobes * plans[sl.k].theta);
      }
      if (sx * sx + sy * sy > 1e-8) {
        this.lobePhase = -Math.atan2(sy, sx);
        this.skel.lobePhase = this.lobePhase;
      }
    }

    // The trunk continues below the ground (a stub of its base cross-section)
    // so that every window fits into the ring grid: the lower half of a
    // ground-level window lies below the ground, and lower tiers entirely.
    let depth = 0;
    let hhMax = 0;
    for (const sl of slots) hhMax = Math.max(hhMax, this.windowEstimate(plans[sl.k].plan.radius, sl.w).hh);
    const tierGap = 3.0 * hhMax;
    for (const sl of slots) {
      const est = this.windowEstimate(plans[sl.k].plan.radius, sl.w);
      depth = Math.max(depth, 1.15 * est.hh + est.alongAbs + 0.02 - this.exitHeight(plans[sl.k].plan.radius) + sl.tier * tierGap);
    }
    depth = Math.min(depth, Math.max(0.3 * trunk.length, 8 * hhMax));
    if (depth > 0) this.extendTrunkBelowGround(depth);
    this.skel.groundDepth = depth;

    for (const sl of slots) {
      const { plan, theta } = plans[sl.k];
      const root = this.makePrimary(plan, theta, depth - sl.tier * tierGap, sl.w);
      if (root) this.primaries.push(root);
    }
    return this.primaries;
  }

  /**
   * Lay the windows of one tier out on the trunk's ring of `N` columns: in
   * angular order, each as close to its wanted centre as possible, with a
   * free column between neighbours. Windows are narrowed (widest first) until
   * the tier fits; a forward sweep resolves the overlaps, a backward sweep the
   * wrap-around, and a common shift centres the displacement. Callers keep
   * each tier to ⌊N/2⌋ windows, which always fits.
   */
  private layoutWindows(group: { c: number; w: number; u: number }[], N: number): void {
    group.sort((a, b) => a.c - b.c);
    let need = group.reduce((acc, sl) => acc + sl.w + 1, 0);
    while (need > N) {
      let widest = group[0];
      for (const sl of group) if (sl.w > widest.w) widest = sl;
      if (widest.w <= 1) break;
      widest.w -= 1;
      need -= 1;
    }
    const K = group.length;
    const u = group.map((sl) => Math.round(sl.c - sl.w / 2));
    for (let i = 1; i < K; i++) u[i] = Math.max(u[i], u[i - 1] + group[i - 1].w + 1);
    let bound = u[0] + N;
    for (let i = K - 1; i >= 1; i--) {
      u[i] = Math.min(u[i], bound - group[i].w - 1);
      bound = u[i];
    }
    let disp = 0;
    for (let i = 0; i < K; i++) disp += u[i] - (group[i].c - group[i].w / 2);
    const shift = Math.round(disp / K);
    for (let i = 0; i < K; i++) {
      let v = (u[i] - shift) % N;
      if (v < 0) v += N;
      group[i].u = v;
    }
  }

  /**
   * Continue the trunk straight down below the ground by `depth`: a stub of
   * the base cross-section (full flare and lobes), so the flare itself stays
   * at ground level while the collars that do not fit at ground level get a
   * place below it. Arc lengths and attachments shift accordingly.
   */
  private extendTrunkBelowGround(depth: number): void {
    const trunk = this.trunk;
    const n0 = trunk.nodes[0];
    const dir = normalize(n0.dir);
    for (const n of trunk.nodes) n.s += depth;
    for (const c of trunk.children) c.attachS += depth;
    const base = addScaled(n0.pos, dir, -depth);
    const k = Math.max(1, Math.ceil(depth / Math.max(0.05, 0.4 * trunk.logicalRadius)));
    const stub: SkelNode[] = [];
    for (let i = 0; i < k; i++) {
      const t = (i / k) * depth;
      stub.push({ pos: addScaled(base, dir, t), dir: { ...dir }, right: { ...n0.right }, s: t });
    }
    trunk.nodes.unshift(...stub);
    trunk.length += depth;
    trunk.logicalStart = -depth / trunk.logicalLength;
  }

  /** How far the trunk currently continues below the ground. */
  private trunkDepth(): number {
    return Math.max(0, -this.trunk.logicalStart * this.trunk.logicalLength);
  }

  /** Height of the root axis above the ground where it leaves the trunk. */
  private exitHeight(rc: number): number {
    return Math.max(0, this.R.emergeHeight) * rc;
  }

  /**
   * Estimate of the junction window the mesher will open for a primary root
   * of radius `rc` at ring angle `theta` (mirrors `Mesher.planHoles`).
   */
  private windowEstimate(rc: number, wGiven?: number): { hh: number; alongAbs: number; w: number } {
    const M = this.M;
    const trunk = this.trunk;
    const descent = clamp(this.R.descent, 0, 70) * DEG2RAD;
    const N = Math.max(3, Math.round(M.trunkRadialSegments));
    // Same radius the mesher divides into columns (no lobes: it uses the plain radius).
    const probeS = this.trunkDepth() + this.exitHeight(rc);
    const rp = stemRadiusAt(trunk, probeS, M, this.skel.params.botany);
    const rho = rc * M.collarScale;
    const chord = (TAU * rp) / N;
    const sinT = Math.cos(descent);
    const extentHalf = rc * Math.max(0.6, 1 / Math.max(0.35, sinT)) * 0.5 + rc * 0.5;
    const minRing = Math.max(4, Math.round(Math.max(M.minRadialSegments, M.rootRadialSegments) / 2) * 2);
    const wMax = Math.max(1, Math.floor(N / 2) - 1);
    const w = wGiven ?? clamp(Math.round((2 * rho) / chord), 1, wMax);
    const W = w * chord;
    const hRows = Math.max(1, Math.ceil(minRing / 2 - w));
    const hh = Math.max(rho, extentHalf * M.collarScale, W * 0.5, ((hRows * W) / (2 * w)) * 0.9);
    const alongAbs = Math.min(M.collarLength * rc, 0.5 * this.skel.treeScale) * Math.sin(descent);
    return { hh, alongAbs, w };
  }

  // ---------------------------------------------------------------------------
  // Primary roots
  // ---------------------------------------------------------------------------

  private makePrimary(plan: RootPlan, theta: number, depth: number, windowCols: number): Stem | null {
    const R = this.R;
    const M = this.M;
    const trunk = this.trunk;
    const rc = plan.radius;
    const descent = clamp(R.descent, 0, 70) * DEG2RAD;
    const minRing = Math.max(4, Math.round(Math.max(M.minRadialSegments, M.rootRadialSegments) / 2) * 2);
    const attachS = clamp(depth + this.exitHeight(rc), 1e-3, Math.max(1e-3, trunk.length * 0.6));
    const est = this.windowEstimate(rc, windowCols);
    if (attachS + est.hh > trunk.length) return null;

    const stem = this.newStem(ROOT_LEVEL, trunk, 'side');
    stem.attachS = attachS;
    stem.windowCols = windowCols;
    stem.logicalLength = plan.length;
    stem.logicalStart = 0;
    stem.logicalRadius = rc;
    stem.taper = Math.max(0.05, R.taper);
    stem.radiusFactor = 1;
    stem.radiusDrop = 1;
    stem.minRing = minRing;
    stem.ringSpacing = this.ringSpacing(rc);
    stem.limbRoot = stem;
    stem.primary = stem.id;

    const smp = sampleStem(trunk, attachS);
    const up = cross(smp.dir, smp.right);
    const radial = normalize(add(scale(smp.right, Math.cos(theta)), scale(up, Math.sin(theta))));
    const rSurf = stemRadiusAt(trunk, attachS, M, this.skel.params.botany) * trunkLobeFactor(M, trunk, attachS, theta, this.lobePhase, this.lobes);
    const origin = addScaled(smp.pos, radial, rSurf);
    // Pitch the exit direction `descent` below the horizontal (rotation about the
    // horizontal axis perpendicular to the radial: positive rotates upwards).
    const emergeDir = normalize(rotateAxis(radial, normalize(cross(radial, UPV)), -descent));
    stem.attachDir = emergeDir;

    const grow = (o: V3, d: V3): void => {
      this.clearSubtree(stem);
      this.growRoot(stem, plan, o, d, 0);
    };
    stem.regrow = grow;
    grow(origin, emergeDir);
    trunk.children.push(stem);
    return stem;
  }

  private ringSpacing(r0: number): number {
    return clamp(0.6 * r0, 0.02, 0.22);
  }

  private clearSubtree(stem: Stem): void {
    // Forget previously grown geometry (laterals, forks, registry entries).
    stem.children = [];
    stem.forks = [];
    stem.nodes = [];
    stem.length = 0;
    for (let i = this.registry.length - 1; i >= 0; i--) if (this.registry[i].primary === stem.primary) this.registry.splice(i, 1);
  }

  private newStem(level: number, parent: Stem | null, attach: Stem['attach']): Stem {
    const stem: Stem = {
      id: this.nextId++,
      level,
      role: 'root',
      parent,
      attach,
      attachS: 0,
      attachDir: { x: 0, y: 1, z: 0 },
      nodes: [],
      length: 0,
      logicalLength: 0,
      logicalStart: 0,
      logicalRadius: 0,
      radiusFactor: 1,
      taper: 1,
      lengthChildMax: 0,
      offset: 0,
      children: [],
      forks: [],
      leaves: [],
      phase: 0,
      limbRoot: null as unknown as Stem,
      distFromLimb: 0,
      limbReach: 1,
      dropped: false,
      radiusDrop: 1,
    };
    stem.limbRoot = stem;
    return stem;
  }

  // ---------------------------------------------------------------------------
  // Growth
  // ---------------------------------------------------------------------------

  /**
   * Grow `stem` from `origin` along `dir0`. `sLogical0` is the logical arc
   * length at the origin (0 for primaries and laterals, the fork position for
   * fork children). Handles the fork and the laterals of the plan.
   */
  private growRoot(stem: Stem, plan: RootPlan, origin: V3, dir0: V3, sLogical0: number): void {
    const R = this.R;
    const env = this.env;
    const M = this.M;
    const L = plan.length;
    const r0 = plan.radius;
    const rng = new Random(plan.seed ^ (Math.round(sLogical0 * 1000) * 7919));
    const noiseSeed = plan.seed & 0xffff;
    const physLen = Math.max(1e-3, L - sLogical0);
    // Uniform steps (no tiny remainder at the tip).
    const nSteps = Math.max(2, Math.ceil(physLen / clamp(0.2 * r0, 0.012, 0.12)));
    const stepLen = physLen / nSteps;
    const climbHeight = Math.max(0, R.climb) * 2 * r0;
    const primaryId = stem.primary ?? stem.id;
    const isLateral = stem.attach === 'side' && stem.parent !== this.trunk;
    let skirtSide = 0;
    // Contact mode: free / climbing over an object / skirting round one.
    let climbing = !!plan.onObject;
    let skirting = false;
    // Emergence: the collar (or the fork crotch) needs the first few radii to
    // leave along the initial direction, so steering ramps up smoothly.
    const rStart = r0 * stem.radiusFactor;
    const emergeLen = stem.attach === 'fork' ? 5.5 * rStart : Math.max(3 * rStart, 2.5 * M.collarLength * rStart);
    // Curvature reference: thin tips may bend tighter than the base, but not without limit.
    const rBend = (r: number): number => Math.max(r, 0.5 * r0, 0.03);
    const gripOffsetOf = (r: number): number => r * (1 - clamp(R.grip, 0, 1));
    const restOffsetOf = (r: number): number => r * (2 * clamp(R.exposure, 0, 1) - 1);

    // Fork / lateral triggers (logical positions).
    const forkS = plan.forkZ !== null && sLogical0 < plan.forkZ * L && stem.attach !== 'fork' ? plan.forkZ * L : null;
    const laterals = plan.laterals.filter((l) => l.z * L > sLogical0 + 2.5 * r0 && (forkS === null || l.z * L < forkS - 2 * r0 || l.z * L > forkS + 3 * r0));
    let latIdx = 0;

    const st: GrowState = { p: { ...origin }, d: normalize(dir0), right: v3(), h: v3(), g: v3(), slope: 0, s: 0 };
    st.h = horizontal(st.d);
    st.g = { ...st.h };
    if (plan.goal && stem.parent === this.trunk) {
      // Head for the target object, but leave the collar no more than ~40° off the radial.
      const yaw = clamp(signedYaw(st.h, plan.goal), -40 * DEG2RAD, 40 * DEG2RAD);
      st.g = normalize(rotateAxis(st.h, UPV, yaw));
    }
    st.slope = st.d.y / Math.max(1e-6, Math.hypot(st.d.x, st.d.z));
    st.right = normalize(cross(st.d, UPV));
    if (length(st.right) < 1e-6) st.right = v3(1, 0, 0);
    stem.nodes.push({ pos: { ...st.p }, dir: { ...st.d }, right: { ...st.right }, s: 0 });

    const radiusAt = (s: number): number => {
      const z = clamp((sLogical0 + s) / L, 0, 1);
      return Math.max(M.minRadius, r0 * Math.pow(Math.max(0, 1 - z), stem.taper) * stem.radiusFactor);
    };

    for (let step = 0; step < nSteps; step++) {
      const s = st.s;
      const r = radiusAt(s);
      const sTot = sLogical0 + s;
      const t = sTot / L;
      const emerge = emergeLen > 0 ? 0.12 + 0.88 * smooth(s / emergeLen) : 1;
      const gripOffset = gripOffsetOf(r);

      // --- goal heading (persistent) -----------------------------------------
      const nz = valueNoise3(sTot * 0.9 + 3.1, noiseSeed * 0.013, 0.5, noiseSeed);
      let gTurn = R.wander * 1.1 * nz * stepLen;
      if (R.attraction > 0) gTurn += this.attraction(st, r, r0, climbHeight) * R.attraction * 2.0 * stepLen;
      if (gTurn !== 0) st.g = normalize(rotateAxis(st.g, UPV, gTurn));

      // --- actual heading (transient deflections + spring back to the goal) ----
      let turn = 2.5 * signedYaw(st.h, st.g) * stepLen;
      turn += this.avoidRoots(st, r, primaryId, stepLen);
      const maxTurn = emerge * (stepLen / (1.8 * rBend(r)));
      turn = clamp(turn, -maxTurn, maxTurn);
      if (turn !== 0) st.h = normalize(rotateAxis(st.h, UPV, turn));

      // --- terrain following ---------------------------------------------------
      // The height field (ground ∪ climbable object tops) gives the level the
      // axis wants to sit at. On soil it is sampled a little ahead (smooth
      // approach to rises); on or against an object it is sampled close by,
      // because there the distance field below governs the vertical motion and
      // a long look-ahead would lift the root off a convex surface.
      const la2 = Math.max(2.5 * rBend(r), 3 * stepLen);
      const eps = Math.max(0.03, 0.5 * r);
      const here = env.nearest(st.p, gripOffset + la2, eps);
      const touching = !!here && here.d < gripOffset + 0.8 * r;
      const sHere = env.surfaceHeight(st.p.x, st.p.z, climbHeight);
      const la = sHere.body >= 0 || touching ? Math.max(0.03, 0.8 * r) : Math.max(0.08, 2.5 * r);
      const aH = addScaled(st.p, st.h, la);
      const aG = addScaled(st.p, st.g, la);
      let sH = env.surfaceHeight(aH.x, aH.z, climbHeight);
      const sG = env.surfaceHeight(aG.x, aG.z, climbHeight);
      const ground = env.groundHeight(st.p.x, st.p.z);
      // Grazing the edge of an object (only one flank of the root would be on
      // it) is not a climb: the root hugs the object's base instead. Objects
      // are only climbed when the root meets them squarely.
      // The decision is taken on approach and kept while the root is on the
      // object (on a steep flank the axis sits horizontally outside the
      // object's footprint, so it cannot be re-evaluated there).
      if (climbing && !touching && sHere.body < 0 && sH.body < 0) {
        // Off the object's top. Still above the ground with a surface within
        // reach below (the far flank fell away faster than the axis could
        // bend), the root lands back on it; otherwise the climb is over.
        const canLand = !!here && here.n.y > 0.15 && st.p.y - ground > 0.5 * r;
        if (!canLand) climbing = false;
      }
      if (skirting && !here) skirting = false;
      // Meeting an object at ground level (not yet on it, not yet touching):
      // decide between climbing and going round. Contact steering below only
      // lifts the root once it is committed to a climb.
      const approaching = !climbing && !skirting && sHere.body < 0 && !touching && st.p.y - ground < 0.5 * r;
      if (approaching && sH.body >= 0) {
        // A climb is committed to only where the object is at least a root
        // diameter wide on both sides of the heading and continues ahead;
        // brushing an edge or a small spur is not a climb. The width test is
        // taken where the object is highest along the approach (its flank
        // is steep, so the footprint edge itself is always narrow).
        const side = normalize(cross(st.h, UPV));
        const probeD = la + 2.5 * r;
        const q = addScaled(st.p, st.h, probeD);
        const l = addScaled(q, side, 1.0 * r);
        const rgt = addScaled(q, side, -1.0 * r);
        const onL = env.surfaceHeight(l.x, l.z, climbHeight).body >= 0;
        const onR = env.surfaceHeight(rgt.x, rgt.z, climbHeight).body >= 0;
        const onFar = env.surfaceHeight(q.x, q.z, climbHeight).body >= 0;
        if (onL && onR && onFar) climbing = true;
        else skirting = true;
      }
      const grazing = skirting;
      if (grazing) sH = { h: ground, body: -1 };
      const surf = sG.h > sH.h && !grazing ? sG : sH;
      const onObject = surf.body >= 0 ? clamp((surf.h - ground) / Math.max(1e-6, 0.5 * r), 0, 1) : 0;
      const offset = lerp1(restOffsetOf(r), gripOffset, onObject);
      const diveT = t > R.dive ? smooth((t - R.dive) / Math.max(1e-6, 1 - R.dive)) : 0;
      const diveDepth = diveT * (1.6 * r0 + 0.04);
      let targetY = surf.h + offset - diveDepth;
      // While climbing the near flank, a ground-level target seen past the
      // edge of the object must not pull the root off the flank: hold the
      // level and let the contact terms below carry it over the top. On the
      // far flank (the surface faces along the heading) the ground is the
      // right target: the root descends, glued to the surface.
      if (climbing && touching && surf.body < 0 && st.p.y > ground + 0.5 * r && here) {
        const facingAway = dot(horizontal(here.n, v3()), st.h) > 0.2 && here.n.y < 0.9;
        if (!facingAway) targetY = st.p.y;
      }
      // Steep pitches are allowed on and around objects (climbing a flank,
      // descending the far side); through soil the root stays shallow.
      const airborne = st.p.y - ground > 0.5 * r && !touching && onObject < 0.5;
      const steep = onObject > 0.5 || touching;
      const maxDown = steep ? Math.tan(72 * DEG2RAD) : airborne ? Math.tan(50 * DEG2RAD) : Math.tan(Math.max(18, R.descent) * DEG2RAD) * 1.15;
      const maxUp = steep ? Math.tan(72 * DEG2RAD) : Math.tan(32 * DEG2RAD);
      const slopeDesired = clamp((targetY - st.p.y) / Math.max(1.4 * la, 1.5 * r), -maxDown, maxUp);
      st.slope = lerp1(st.slope, slopeDesired, 0.3);

      // --- step direction ------------------------------------------------------
      let dInt = normalize(add(st.h, scale(UPV, st.slope)));

      // Contact with objects (distance field). Sliding contact: the direction
      // is blended towards the surface tangent as the surface comes near, so
      // the turn is spread over the approach and the polyline stays smooth; a
      // root in contact is glued to the surface at its grip depth.
      const aheadS = env.nearest(addScaled(st.p, dInt, la2), gripOffset + la2, eps);
      const contact = aheadS && (!here || aheadS.d < here.d) ? aheadS : here;
      if (contact) {
        const nh = horizontal(contact.n, v3());
        const hereD = here ? here.d : gripOffset + la2;
        const low = env.top(contact.obstacle) - ground <= climbHeight;
        if (low && touching && !climbing && !skirting) climbing = true;
        if (low && !climbing && !skirting) {
          // A low object not reached yet: approach it freely (the decision to
          // climb or skirt is taken above, from the height field).
        } else if (climbing) {
          // Over the top: deflection in the vertical plane of the heading (up
          // the near flank, down the far one). Under an overhang on the far
          // side the root lets go and drops to the ground instead.
          const overhangBehind = contact.n.y < -0.15 && dot(st.h, nh) > 0;
          const above = st.p.y - ground > 0.5 * r;
          // Hovering over the object's upper surface (after topping a flank,
          // or over a hollow of the top): land on it. Without this the
          // sliding term would carry the root parallel to the surface forever.
          const landing = !touching && above && contact.n.y > 0.3;
          const glue = (touching && !overhangBehind && (surf.body >= 0 || contact.n.y < 0.3 || above)) || landing;
          // Above the ground (on a flank, or walked off a shoulder): press
          // harder, so the root follows the convex surface down instead of
          // leaving it along a chord.
          const press = above ? 0.6 : 0.2;
          if (!overhangBehind) dInt = steerAlong(dInt, contact.n, contact.d, hereD, gripOffset, la2, stepLen, st.right, glue, true, press);
          skirtSide = 0;
        } else {
          // Too tall to cross: slide along the surface and deflect the heading
          // sideways around the object, anticipating the wall ahead. The side
          // is chosen once per contact so the root does not dither. The root
          // lets go where the surface recedes from its goal, which is what
          // keeps roots from orbiting boulders.
          const receding = dot(st.g, nh) > 0.3 && contact.n.y < 0.2;
          const glue = touching && !receding && contact.n.y < 0.5;
          // Not (yet) climbing: only the sliding-contact term, and no sideways
          // deflection for an object that may still be climbed on arrival.
          dInt = steerAlong(dInt, contact.n, contact.d, hereD, gripOffset, la2, stepLen, st.right, glue, false);
          if (contact.n.y < 0.45 && length(nh) > 1e-6) {
            const nhn = normalize(nh);
            const facing = -dot(st.h, nhn); // 1 = heading straight into the wall
            if (facing > 0.2) {
              if (skirtSide === 0) skirtSide = cross(st.h, nhn).y >= 0 ? 1 : -1;
              const w = 1 - clamp((contact.d - gripOffset) / la2, 0, 1);
              const yaw = skirtSide * facing * w * (stepLen / (1.6 * rBend(r)));
              st.h = normalize(rotateAxis(st.h, UPV, clamp(yaw, -maxTurn, maxTurn)));
              dInt = normalize(add(st.h, scale(UPV, st.slope)));
              dInt = steerAlong(dInt, contact.n, contact.d, hereD, gripOffset, la2, stepLen, st.right, glue, false);
            }
          } else if (!here) skirtSide = 0;
        }
      } else skirtSide = 0;
      // The trunk is an obstacle too (outside the collar zone).
      if (s > emergeLen) {
        const tp = addScaled(st.p, dInt, la2);
        const dHere = this.trunkClearance(st.p, r);
        const dAhead = this.trunkClearance(tp, r);
        if (Math.min(dHere, dAhead) < la2) {
          const q = dAhead < dHere ? tp : st.p;
          const dist = Math.hypot(q.x, q.z) || 1e-6;
          const n = v3(q.x / dist, 0, q.z / dist);
          dInt = steerAlong(dInt, n, Math.min(dHere, dAhead), dHere, 0, la2, stepLen, st.right, false, false);
        }
      }

      // Rate limit: the axis bends no tighter than a bend radius (tighter for
      // thin tips, never below a floor), damped through the emergence zone.
      const maxAng = emerge * (stepLen / (1.8 * rBend(r)));
      let dNew = rotateTowards(st.d, dInt, maxAng);
      let pNew = addScaled(st.p, dNew, stepLen);
      // Safety net for residual penetration (concavities, surface detail):
      // the correction is itself rate-limited so it never kinks the axis –
      // roots are allowed to sink into what they grip, but not to zigzag.
      if (s > 1.5 * rStart) {
        let q = env.pushOut(pNew, gripOffset * 0.7, eps);
        if (s > emergeLen) q = this.clearTrunk(q, r);
        const dq = sub(q, st.p);
        if (length(dq) > 1e-7 && dot(normalize(dq), dNew) < 0.99999) {
          dNew = rotateTowards(dNew, normalize(dq), maxAng * 0.6);
          pNew = addScaled(st.p, dNew, stepLen);
        }
      }

      const moved = sub(pNew, st.p);
      const ml = length(moved);
      if (ml < 1e-7) break;
      st.d = scale(moved, 1 / ml);
      // `h` is the course, `d` the actual direction: contact terms deflect `d`
      // (sliding along a wall, pressing onto a flank) without turning the
      // course, so the root resumes it once the obstacle is passed. Only the
      // steering terms above ever rotate `h`.
      st.p = pNew;
      st.s += ml;
      let right = projectOnPlane(st.right, st.d);
      if (length(right) < 1e-6) right = cross(st.d, UPV);
      st.right = normalize(right);
      stem.nodes.push({ pos: { ...st.p }, dir: { ...st.d }, right: { ...st.right }, s: st.s });
      stem.length = st.s;
      if ((stem.nodes.length & 1) === 0) this.registry.push({ p: st.p, r, primary: primaryId });

      // --- laterals ----------------------------------------------------------
      while (latIdx < laterals.length && sLogical0 + st.s >= laterals[latIdx].z * L) {
        const lp = laterals[latIdx++];
        if (!isLateral) this.makeLateral(stem, lp, st.s, r0);
      }

      // --- fork --------------------------------------------------------------
      if (forkS !== null && sLogical0 + st.s >= forkS) {
        // Not at the edge of an object (steeply descending or in the air):
        // the children could not bend down onto the surface in time. Wait
        // for level ground or a flat top, but not forever.
        const edge = (climbing || airborne) && (st.slope < -0.45 || !touching);
        if (!edge || sLogical0 + st.s >= forkS + 0.2 * L) {
          this.makeFork(stem, plan, st, sLogical0 + st.s, climbing);
          return;
        }
      }
    }
    if (stem.nodes.length < 2) {
      const p2 = addScaled(st.p, st.d, stepLen);
      stem.nodes.push({ pos: p2, dir: { ...st.d }, right: { ...st.right }, s: stepLen });
      stem.length = stepLen;
    }
    void rng;
  }

  private makeFork(stem: Stem, plan: RootPlan, st: GrowState, sLogical: number, onObject: boolean): void {
    const M = this.M;
    const factor = Math.pow(0.5, 0.5 * M.forkRadiusConservation);
    const yaw = plan.forkYawDeg * DEG2RAD;
    for (let i = 0; i < 2; i++) {
      const child = this.newStem(ROOT_LEVEL, stem, 'fork');
      child.logicalLength = plan.length;
      child.logicalStart = sLogical / plan.length;
      child.logicalRadius = plan.radius;
      child.radiusFactor = stem.radiusFactor * factor;
      child.taper = stem.taper;
      child.radiusDrop = 1;
      child.minRing = Math.max(this.M.minRadialSegments, 8);
      child.ringSpacing = stem.ringSpacing;
      child.limbRoot = stem.limbRoot;
      child.primary = stem.primary;
      const sign = i === 0 ? 1 : -1;
      const h = normalize(rotateAxis(st.h, UPV, sign * yaw * (i === 0 ? 1 : 0.8)));
      const d = normalize(add(h, scale(UPV, st.slope * 0.6)));
      child.attachDir = d;
      // Children of a fork on an object continue in climbing contact.
      const subPlan: RootPlan = { ...plan, seed: (plan.seed * 31 + i * 977) >>> 0, laterals: plan.laterals.filter((l, k) => k % 2 === i), onObject };
      stem.forks.push(child);
      this.growRoot(child, subPlan, { ...st.p }, d, sLogical);
    }
  }

  private makeLateral(parent: Stem, lp: LateralPlan, sParent: number, rParent0: number): void {
    const M = this.M;
    const B = this.skel.params.botany;
    const smp = sampleStem(parent, sParent);
    const rp = stemRadiusAt(parent, sParent, M, B);
    const rc = Math.min(rp * MAX_SIDE_RATIO, Math.max(M.minRadius * 2, rp * lp.radiusFrac));
    if (rc < M.minRadius * 2.5 || rc < 0.012) return;
    const remaining = Math.max(0, parent.logicalLength * (1 - parent.logicalStart) - sParent);
    const len = Math.max(rc * 8, remaining * lp.lengthFrac);
    if (len < rc * 8) return;

    const stem = this.newStem(ROOT_LEVEL, parent, 'side');
    stem.attachS = sParent;
    stem.logicalLength = len;
    stem.logicalRadius = rc;
    stem.taper = Math.max(0.05, this.R.taper);
    stem.radiusDrop = clamp(Math.sqrt(Math.max(0, 1 - M.forkRadiusConservation * (rc / rp) ** 2)), 0.6, 1);
    stem.minRing = Math.max(M.minRadialSegments, 8);
    stem.ringSpacing = this.ringSpacing(rc);
    stem.limbRoot = parent.limbRoot;
    stem.primary = parent.primary;
    stem.offset = sParent;

    // Exit from the upper flank of the parent so the collar is visible above the soil.
    const up = normalize(projectOnPlane(UPV, smp.dir));
    const side = normalize(cross(smp.dir, up));
    const elev = lp.elevDeg * DEG2RAD;
    const radial = normalize(add(scale(side, lp.side * Math.cos(elev)), scale(up, Math.sin(elev))));
    const origin = addScaled(smp.pos, radial, rp);
    const heading = normalize(rotateAxis(horizontal(smp.dir), UPV, -lp.side * lp.yawDeg * DEG2RAD));
    const desired = normalize(add(heading, scale(UPV, -Math.tan(20 * DEG2RAD))));
    const emergeDir = rotateTowards(radial, desired, 38 * DEG2RAD);
    stem.attachDir = emergeDir;
    // A lateral whose exit is blocked by an object or another root is not
    // grown; neither is one that would start in the air (parent climbing an
    // object) unless it can hold on to that object.
    const probe = addScaled(origin, emergeDir, 2.5 * rc);
    if (!this.env.isFree(probe, 0.6 * rc) || !this.env.isFree(origin, 0.2 * rc)) return;
    if (this.nearRegistry(probe, rc * 2.2, parent.primary ?? -1)) return;
    const airborne = probe.y - this.env.groundHeight(probe.x, probe.z) > 1.5 * rc;
    let onObject = false;
    if (airborne) {
      const hold = this.env.nearest(probe, 2.0 * rc, Math.max(0.03, 0.5 * rc));
      if (!hold) return;
      // Only a surface it can lie on (not a wall it would have to drop off).
      if (hold.n.y < 0.2) return;
      onObject = true;
    }

    const plan: RootPlan = { seed: lp.seed, radius: rc, length: len, forkZ: null, forkYawDeg: 0, laterals: [], onObject };
    const grow = (o: V3, d: V3): void => {
      this.clearSubtree(stem);
      this.growRoot(stem, plan, o, d, 0);
    };
    stem.regrow = grow;
    grow(origin, emergeDir);
    parent.children.push(stem);
    void rParent0;
  }

  // ---------------------------------------------------------------------------
  // Steering terms (all return a signed yaw rate in radians per metre)
  // ---------------------------------------------------------------------------

  private attraction(st: GrowState, r: number, r0: number, climbHeight: number): number {
    const env = this.env;
    let best = 0;
    let bestW = 0;
    for (let i = 0; i < env.count; i++) {
      const c = env.center(i);
      const to = v3(c.x - st.p.x, 0, c.z - st.p.z);
      const dist = length(to);
      const reach = env.bound(i) + 2.5 * r0 + 0.8;
      if (dist > reach || dist < 1e-6) continue;
      const dir = scale(to, 1 / dist);
      const ahead = dot(dir, st.g);
      if (ahead < 0.1) continue; // behind or beside: do not turn back
      // Already on it: nothing to do.
      if (env.distance(i, st.p) < r * 1.5) continue;
      const ang = Math.atan2(cross(st.g, dir).y, ahead);
      const w = (1 - dist / reach) * ahead * (env.top(i) - env.groundHeight(c.x, c.z) <= climbHeight ? 1 : 0.7);
      if (w > bestW) {
        bestW = w;
        best = ang;
      }
    }
    return clamp(best, -1, 1) * bestW;
  }

  private avoidRoots(st: GrowState, r: number, primaryId: number, stepLen: number): number {
    let turn = 0;
    const p = st.p;
    for (const n of this.registry) {
      if (n.primary === primaryId) continue;
      const dx = n.p.x - p.x;
      const dz = n.p.z - p.z;
      const dy = n.p.y - p.y;
      const minD = (r + n.r) * 1.8;
      if (Math.abs(dx) > minD || Math.abs(dz) > minD || Math.abs(dy) > minD) continue;
      const d = Math.hypot(dx, dz);
      if (d > minD || d < 1e-6) continue;
      const to = v3(dx / d, 0, dz / d);
      const ahead = dot(to, st.h);
      if (ahead < -0.2) continue;
      const side = cross(st.h, to).y;
      turn -= Math.sign(side || 1) * (1 - d / minD) * 4.0 * stepLen;
    }
    return turn;
  }

  private nearRegistry(p: V3, dist: number, ignorePrimary: number): boolean {
    for (const n of this.registry) {
      if (n.primary === ignorePrimary) continue;
      const dx = n.p.x - p.x;
      const dy = n.p.y - p.y;
      const dz = n.p.z - p.z;
      const d = dist + n.r;
      if (dx * dx + dy * dy + dz * dz < d * d) return true;
    }
    return false;
  }

  private trunkSurfaceRadius(p: V3): number {
    const trunk = this.trunk;
    const s = clamp(p.y - trunk.nodes[0].pos.y, 0, trunk.length);
    const smp = sampleStem(trunk, s);
    const rel = sub(p, smp.pos);
    const up = cross(smp.dir, smp.right);
    const theta = Math.atan2(dot(rel, up), dot(rel, smp.right));
    return stemRadiusAt(trunk, s, this.M, this.skel.params.botany) * trunkLobeFactor(this.M, trunk, s, theta, this.lobePhase, this.lobes);
  }

  /** Signed clearance between a root axis point and the trunk surface (plus one root radius). */
  private trunkClearance(p: V3, r: number): number {
    if (p.y > this.trunk.length * 0.5) return Infinity;
    return Math.hypot(p.x, p.z) - (this.trunkSurfaceRadius(p) + r * 1.05);
  }

  private clearTrunk(p: V3, r: number): V3 {
    const dist = Math.hypot(p.x, p.z);
    if (dist < 1e-6) return p;
    if (p.y > this.trunk.length * 0.5) return p;
    const surface = this.trunkSurfaceRadius(p) + r * 1.05;
    if (dist >= surface) return p;
    const k = surface / dist;
    return v3(p.x * k, p.y, p.z * k);
  }
}

/**
 * Blend a direction towards the tangent of a surface with normal `n` that is
 * `d` away (the current point is `dHere` away). `la` is the anticipation
 * distance: the blend weight goes from 0 at `depth + la` to 1 at the surface.
 * With `climb` the tangent is taken in the vertical plane of the heading (the
 * root goes up and over); otherwise it is the free projection onto the tangent
 * plane (the root slides along, mostly sideways). With `glue` the result also
 * carries a small normal component that settles the axis at `depth` from the
 * surface (pressing in when too far out, backing off when too deep); without
 * it the root only avoids entering the surface and may leave it freely.
 */
function steerAlong(dir: V3, n: V3, d: number, dHere: number, depth: number, la: number, stepLen: number, right: V3, glue: boolean, climb: boolean, press = 0.2): V3 {
  const into = dot(dir, n);
  // A root glued onto a surface below it (descending a convex flank) follows
  // the surface; otherwise a direction leaving the surface steeply is let go.
  const adhering = glue && climb && n.y > 0.15;
  if (into > 0.6 && !adhering) return dir;
  const w = 1 - clamp((d - depth) / la, 0, 1);
  if (w <= 0) return dir;
  let tangent: V3;
  if (climb) {
    if (into >= 0 && adhering) {
      tangent = addScaled(dir, n, -into);
      if (length(tangent) < 1e-4) tangent = cross(n, right);
    } else if (into < 0) {
      // Entering the surface: deflect within the vertical plane of the heading
      // (up the flank; straight up along an overhang, which lies in the soil).
      const h = horizontal(dir, v3());
      const nh = dot(n, h);
      tangent = add(scale(h, n.y), scale(UPV, -nh)); // ⟂ n within the (h, up) plane
      // A tangent pointing back means an overhang: climb straight up along it
      // when ascending; when descending, let go of the surface.
      if (dot(tangent, h) < 0 || length(tangent) < 1e-4) tangent = dir.y >= 0 ? { ...UPV } : { ...dir };
    } else tangent = { ...dir }; // parallel or leaving: only the glue term acts
  } else {
    tangent = addScaled(dir, n, -into);
    if (length(tangent) < 1e-4) tangent = cross(n, right);
  }
  tangent = normalize(tangent);
  // Approach no steeper than ~12°; settle towards the grip depth over a few steps.
  let vn = clamp((depth - dHere) / (4 * stepLen), -press, 0.35);
  if (!glue && vn < 0) vn = 0;
  const target = add(scale(tangent, Math.sqrt(1 - vn * vn)), scale(n, vn));
  return normalize(add(scale(dir, 1 - w), scale(target, w)));
}

/** Signed yaw (about +Y) from horizontal direction `a` to `b`, radians. */
function signedYaw(a: V3, b: V3): number {
  return Math.atan2(a.z * b.x - a.x * b.z, a.x * b.x + a.z * b.z);
}

function horizontal(d: V3, fallback?: V3): V3 {
  const h = v3(d.x, 0, d.z);
  const l = length(h);
  if (l < 1e-6) return fallback ? { ...fallback } : v3(1, 0, 0);
  return scale(h, 1 / l);
}

function smooth(t: number): number {
  t = clamp(t, 0, 1);
  return t * t * (3 - 2 * t);
}

/** Grow the root system into the skeleton (attaches primaries to the trunk). */
export function buildRoots(skel: Skeleton, env: Environment): Stem[] {
  const builder = new RootBuilder(skel, env);
  const primaries = builder.build();
  skel.rootSystem = primaries;
  return primaries;
}

export const _rootInternals = { horizontal, smooth };
