/**
 * Welded quad mesher.
 *
 * Produces ONE closed manifold surface for the whole branch system:
 *
 *  - Each stem is a tube of rings bridged by quads.
 *  - A side branch does not intersect its parent. A rectangular window of
 *    cells (w columns x h rows) is removed from the parent's ring grid; the
 *    resulting closed edge loop (2(w+h) vertices) is bridged with quads to the
 *    child's first ring, which therefore has exactly 2(w+h) vertices. This is
 *    the same "select a face, extrude a branch" topology a modeller would
 *    build by hand. Extra rings are inserted into the parent around each
 *    attachment so the window is approximately square.
 *  - A fork (Weber-Penn clone) is meshed as a true Y: the parent's end ring is
 *    divided into one arc per child, a crotch bridge is built from the split
 *    vertices to a hub above the fork point, and every child's base ring is
 *    "its arc + the bridge". No child is placed on top of another.
 *
 * Every face is a quad except the ladder caps of stem tips, which may
 * contain one triangle for odd ring counts. The mesh is closed, so for a tree
 * Euler's formula V - E + F = 2 holds, which the validator checks.
 */

import {
  V3,
  TAU,
  add,
  addScaled,
  cross,
  dot,
  lerp,
  length,
  lengthSq,
  normalize,
  projectOnPlane,
  sub,
  scale as vscale,
  clamp,
  mod,
  anyPerpendicular,
  rotateTowards,
  rotateAxis as rotateAxisV,
} from '../core/math';
import { LeafMesh, QuadMesh, VertexWind } from './mesh';
import { Skeleton, Stem, sampleStem, stemRadiusAt, rotateSubtree, translateSubtree, trunkLobeFactor } from './skeleton';
import { TreeParams } from './params';
import { effectiveRootLobes } from './roots';

interface Ring {
  idx: number[];
  s: number;
  center: V3;
  dir: V3;
  radius: number;
}

interface HolePlan {
  child: Stem;
  s: number;
  sLow: number;
  sHigh: number;
  j0: number;
  w: number;
  hh: number;
  /** Desired number of rows (ring intervals) the window spans. */
  rows: number;
  row0: number;
  row1: number;
  /** Axial offset from the child's attachment point to the window centre. */
  along: number;
}

type BaseInfo =
  | { kind: 'root' }
  | { kind: 'side'; loop: number[]; radial: V3; parentDir: V3 }
  | { kind: 'fork'; dRing: number[]; t1: number };

export interface MesherStats {
  stems: number;
  droppedStems: number;
  dropReasons: Record<string, number>;
  junctions: number;
  forks: number;
  maxDepth: number;
  /** Root stems welded into the mesh (primaries, laterals and fork children). */
  rootStems: number;
  /** Roots that could not be given a window on the trunk. */
  droppedRoots: number;
}

export interface MeshBuildResult {
  mesh: QuadMesh;
  leaves: LeafMesh;
  stats: MesherStats;
}

export class Mesher {
  readonly mesh = new QuadMesh();
  readonly leafMesh = new LeafMesh();
  private readonly P: TreeParams['mesh'];
  private readonly B: TreeParams['botany'];
  private readonly treeHeight: number;
  private stats: MesherStats = { stems: 0, droppedStems: 0, dropReasons: {}, junctions: 0, forks: 0, maxDepth: 0, rootStems: 0, droppedRoots: 0 };
  private lobePhase: number;
  private lobes: number;

  constructor(private readonly skel: Skeleton) {
    this.P = skel.params.mesh;
    this.B = skel.params.botany;
    this.treeHeight = Math.max(1e-3, skel.height);
    this.lobePhase = skel.lobePhase;
    this.lobes = effectiveRootLobes(skel.params);
  }

  build(): MeshBuildResult {
    for (const root of this.skel.roots) this.meshStem(root, { kind: 'root' }, 0);
    this.buildLeaves();
    return { mesh: this.mesh, leaves: this.leafMesh, stats: this.stats };
  }

  // ---------------------------------------------------------------------------
  // Per-vertex attributes
  // ---------------------------------------------------------------------------

  private windAt(stem: Stem, s: number, y: number): VertexWind {
    // Roots are anchored: no sway, no bending, no flutter.
    if (stem.role === 'root') return { height: 0, limb: 0, phase: 0, detail: 0 };
    const L = Math.max(1e-6, stem.length);
    const limb = stem.level === 0 ? 0 : clamp((stem.distFromLimb + s) / stem.limbReach, 0, 1);
    const detail = stem.level >= 2 ? clamp(0.3 * (stem.level - 1) + (0.5 * s) / L, 0, 1) : 0;
    return { height: clamp(y / this.treeHeight, 0, 1), limb, phase: stem.limbRoot.phase, detail };
  }

  private pivotOf(stem: Stem): V3 {
    if (stem.level === 0 || stem.role === 'root') return { x: 0, y: 0, z: 0 };
    return stem.limbRoot.nodes[0].pos;
  }

  /** Surface radius of a stem at arc length `s` and ring angle `theta` (buttress lobes included). */
  private surfaceRadius(stem: Stem, s: number, theta: number): number {
    return stemRadiusAt(stem, s, this.P, this.B) * trunkLobeFactor(this.P, stem, s, theta, this.lobePhase, this.lobes);
  }

  private addVertex(p: V3, stem: Stem, s: number, junction = 0): number {
    return this.mesh.addVertex(p.x, p.y, p.z, this.windAt(stem, s, p.y), this.pivotOf(stem), stem.level, junction);
  }

  // ---------------------------------------------------------------------------
  // Stem
  // ---------------------------------------------------------------------------

  private meshStem(stem: Stem, base: BaseInfo, depth: number): void {
    const mesh = this.mesh;
    const P = this.P;
    this.stats.stems++;
    if (stem.role === 'root') this.stats.rootStems++;
    if (depth > this.stats.maxDepth) this.stats.maxDepth = depth;

    const N = base.kind === 'root' ? Math.max(3, Math.round(P.trunkRadialSegments)) : base.kind === 'side' ? base.loop.length : base.dRing.length;
    const L = stem.length;
    const r0 = stemRadiusAt(stem, 0, P, this.B);

    // First ring station.
    let sStart = 0;
    if (base.kind === 'side') sStart = collarStart(stem, P, this.B);
    else if (base.kind === 'fork') sStart = Math.min(base.t1, 0.7 * L);

    // Mitre: tilt the first ring of a side stem towards the parent surface so the
    // collar quads are evenly sized above and below the branch.
    let tilt: V3 | undefined;
    if (base.kind === 'side') {
      const a0 = stem.nodes[0].dir;
      const theta = Math.acos(clamp(dot(a0, base.radial), -1, 1));
      const dScale = clamp(sStart / Math.max(1e-6, P.collarLength * r0), 0, 1);
      const alpha = theta * clamp(P.collarMitre, 0, 0.9) * dScale;
      if (alpha > 1e-3) tilt = rotateTowards(a0, base.radial, alpha);
    }

    // Phase of the ring vertices around the stem axis so that the first ring
    // lines up with the loop it is bridged to.
    let phase = 0;
    let loop: number[] | null = null;
    if (base.kind === 'side') loop = base.loop.slice();
    if (base.kind === 'fork') loop = base.dRing.slice();
    if (loop) {
      const smp = sampleStem(stem, sStart);
      phase = this.fitPhase(loop, smp.pos, smp.dir, smp.right);
    }

    // Plan the windows of the side children.
    const holes = this.planHoles(stem, N, phase, sStart, L);

    // Ring stations.
    const stations = this.buildStations(stem, holes, sStart, L, base.kind);
    const K = stations.length;

    // Map hole rows onto the merged stations and resolve residual conflicts.
    const occupied = new Uint8Array(Math.max(1, K - 1) * N);
    const accepted: HolePlan[] = [];
    for (const h of holes) {
      let row0 = nearestIndex(stations, h.sLow);
      let row1 = nearestIndex(stations, h.sHigh);
      if (row1 - row0 < h.rows) row1 = row0 + h.rows;
      if (row1 > K - 1) {
        row1 = K - 1;
        row0 = Math.max(0, row1 - h.rows);
      }
      if (row1 <= row0 || row0 < 0) {
        this.drop(h.child, 'no room on parent');
        continue;
      }
      let free = true;
      for (let i = row0; i < row1 && free; i++) for (let j = 0; j < h.w; j++) if (occupied[i * N + mod(h.j0 + j, N)]) free = false;
      if (!free) {
        this.drop(h.child, 'window conflict');
        continue;
      }
      for (let i = row0; i < row1; i++) for (let j = 0; j < h.w; j++) occupied[i * N + mod(h.j0 + j, N)] = 1;
      h.row0 = row0;
      h.row1 = row1;
      accepted.push(h);
    }

    // Build rings. Vertices strictly inside a window (all four surrounding
    // cells removed) are never referenced, so they are not created.
    const cellOcc = (i: number, j: number): boolean => i >= 0 && i < K - 1 && occupied[i * N + mod(j, N)] === 1;
    const interior = (i: number, j: number): boolean => cellOcc(i - 1, j - 1) && cellOcc(i - 1, j) && cellOcc(i, j - 1) && cellOcc(i, j);
    const rings: Ring[] = [];
    for (let i = 0; i < K; i++) {
      rings.push(this.buildRing(stem, stations[i], N, phase, base.kind === 'root', i === 0 ? tilt : undefined, (j) => interior(i, j)));
    }

    // Base geometry.
    if (base.kind === 'root') {
      this.cap(rings[0].idx, true);
    } else if (base.kind === 'side' && loop) {
      this.buildCollar(stem, loop, rings[0], base.radial, r0);
      this.stats.junctions++;
    } else if (base.kind === 'fork' && loop) {
      const first = rings[0];
      const mid: number[] = [];
      const M = loop.length;
      const fork = stem.nodes[0].pos;
      const chordMid = lerp(fork, first.center, 0.5);
      for (let k = 0; k < M; k++) {
        const a = this.pos(loop[k]);
        const b = this.pos(first.idx[k]);
        let p = lerp(a, b, 0.5);
        let out = projectOnPlane(sub(p, chordMid), first.dir);
        if (lengthSq(out) > 1e-12) {
          out = normalize(out);
          p = addScaled(p, out, first.radius * 0.1);
        }
        mid.push(this.addVertex(p, stem, sStart * 0.5, 1));
      }
      this.bridgeLoops(loop, mid, 1);
      this.bridgeLoops(mid, first.idx, 1);
    }

    // Tube quads.
    for (let i = 0; i < K - 1; i++) {
      const a = rings[i].idx;
      const b = rings[i + 1].idx;
      const va = stations[i] / Math.max(1e-6, L);
      const vb = stations[i + 1] / Math.max(1e-6, L);
      for (let j = 0; j < N; j++) {
        if (occupied[i * N + j]) continue;
        const j1 = (j + 1) % N;
        const u0 = j / N;
        const u1 = (j + 1) / N;
        mesh.addQuad(a[j], a[j1], b[j1], b[j], [u0, va, u1, va, u1, vb, u0, vb]);
      }
    }

    // End: fork or tip.
    if (stem.forks.length > 0 && K >= 1) {
      this.meshFork(stem, rings[K - 1], depth);
    } else if (K >= 1) {
      this.cap(rings[K - 1].idx, false);
    }

    // Side children.
    for (const h of accepted) {
      const loopIdx = this.holeLoop(rings, h, N);
      const ps = sampleStem(stem, h.child.attachS);
      let radial = projectOnPlane(sub(h.child.nodes[0].pos, ps.pos), ps.dir);
      radial = lengthSq(radial) < 1e-12 ? anyPerpendicular(ps.dir) : normalize(radial);
      this.meshStem(h.child, { kind: 'side', loop: loopIdx, radial, parentDir: ps.dir }, depth + 1);
    }
  }

  private pos(i: number): V3 {
    const p = this.mesh.positions;
    return { x: p[i * 3], y: p[i * 3 + 1], z: p[i * 3 + 2] };
  }

  private drop(stem: Stem, reason: string): void {
    stem.dropped = true;
    stem.dropReason = reason;
    this.stats.droppedStems++;
    if (stem.role === 'root') this.stats.droppedRoots++;
    this.stats.dropReasons[reason] = (this.stats.dropReasons[reason] ?? 0) + 1;
  }

  /**
   * Branch collar: bridge the (rectangular) window loop to the child's first
   * ring through `collarRings` intermediate loops. Each intermediate vertex
   * lies on a quadratic curve from the window vertex (on the parent surface)
   * to its ring partner whose control point is where the child's surface line
   * meets the parent's tangent plane – a fillet, tangent to both surfaces.
   * `collarFillet` blends between a straight chamfer (0) and that fillet (1).
   */
  private buildCollar(stem: Stem, loop: number[], first: Ring, radial: V3, r0: number): void {
    const P = this.P;
    const n = Math.max(0, Math.round(P.collarRings));
    const M = loop.length;
    const a0 = stem.nodes[0].dir;
    const cosT = Math.max(0.2, dot(a0, radial));
    const fillet = clamp(P.collarFillet, 0, 1);
    let prev = loop;
    for (let q = 0; q < n; q++) {
      const k = (q + 1) / (n + 1);
      const mid: number[] = [];
      for (let i = 0; i < M; i++) {
        const a = this.pos(loop[i]);
        const b = this.pos(first.idx[i]);
        const chord = lerp(a, b, k);
        // Control point: slide back from b along the child axis to the tangent plane at a.
        let mu = dot(sub(b, a), radial) / cosT;
        mu = clamp(mu, 0, 4 * r0);
        const c = addScaled(b, a0, -mu);
        const w0 = (1 - k) * (1 - k);
        const w1 = 2 * k * (1 - k);
        const w2 = k * k;
        const bez = {
          x: a.x * w0 + c.x * w1 + b.x * w2,
          y: a.y * w0 + c.y * w1 + b.y * w2,
          z: a.z * w0 + c.z * w1 + b.z * w2,
        };
        const p = lerp(chord, bez, fillet);
        mid.push(this.addVertex(p, stem, first.s * k, 1));
      }
      this.bridgeLoops(prev, mid, 1);
      prev = mid;
    }
    this.bridgeLoops(prev, first.idx, 1);
  }

  // ---------------------------------------------------------------------------
  // Rings
  // ---------------------------------------------------------------------------

  /**
   * Ring of N vertices around the stem at arc length `s`. With `tiltNormal`
   * the ring is the intersection of the stem cylinder with the plane through
   * the station whose normal is `tiltNormal` (a mitre): each vertex keeps its
   * angular coordinate and slides along the axis onto that plane.
   */
  private buildRing(stem: Stem, s: number, N: number, phase: number, isTrunk: boolean, tiltNormal?: V3, skip?: (j: number) => boolean): Ring {
    const smp = sampleStem(stem, s);
    const r = stemRadiusAt(stem, s, this.P, this.B);
    const up = cross(smp.dir, smp.right);
    const idx: number[] = new Array(N);
    const P = this.P;
    const lobed = isTrunk && this.lobes > 0 && P.rootLobeAmplitude > 0 && s + stem.logicalStart * stem.logicalLength < P.rootLobeHeight * stem.logicalLength;
    const an = tiltNormal ? dot(smp.dir, tiltNormal) : 1;
    for (let j = 0; j < N; j++) {
      if (skip && skip(j)) {
        idx[j] = -1;
        continue;
      }
      const th = phase + (TAU * j) / N;
      let rr = r;
      if (lobed) rr *= trunkLobeFactor(P, stem, s, th, this.lobePhase, this.lobes);
      const c = Math.cos(th);
      const sn = Math.sin(th);
      const off = {
        x: rr * (c * smp.right.x + sn * up.x),
        y: rr * (c * smp.right.y + sn * up.y),
        z: rr * (c * smp.right.z + sn * up.z),
      };
      if (tiltNormal && an > 0.3) {
        const lambda = -dot(off, tiltNormal) / an;
        off.x += smp.dir.x * lambda;
        off.y += smp.dir.y * lambda;
        off.z += smp.dir.z * lambda;
      }
      idx[j] = this.addVertex({ x: smp.pos.x + off.x, y: smp.pos.y + off.y, z: smp.pos.z + off.z }, stem, s);
    }
    return { idx, s, center: smp.pos, dir: smp.dir, radius: r };
  }

  /**
   * Fit a regular angular spacing to a closed loop as seen along `dir`:
   * returns the starting angle that best aligns ring vertex k with loop
   * vertex k. The angles are measured from the projected centroid, so the
   * result does not depend on where the stem axis happens to pierce the loop.
   *
   * The loop's orientation is not inferred here: hole loops and fork D-rings
   * are both assembled so that they run counter-clockwise as seen from the
   * child's tip, which is the winding of the child's rings. (A signed-area
   * test on the projection was used before; it lies when the loop is folded
   * in projection, e.g. a root leaving a steep buttress almost tangentially,
   * and reversing the loop then winds every bridge quad against the parent.)
   */
  private fitPhase(loop: number[], center: V3, dir: V3, right: V3): number {
    const up = cross(dir, right);
    const M = loop.length;
    const xs: number[] = new Array(M);
    const ys: number[] = new Array(M);
    let cx = 0;
    let cy = 0;
    for (let k = 0; k < M; k++) {
      const v = sub(this.pos(loop[k]), center);
      xs[k] = dot(v, right);
      ys[k] = dot(v, up);
      cx += xs[k];
      cy += ys[k];
    }
    cx /= M;
    cy /= M;
    let sx = 0;
    let sy = 0;
    for (let k = 0; k < M; k++) {
      const a = Math.atan2(ys[k] - cy, xs[k] - cx) - (TAU * k) / M;
      sx += Math.cos(a);
      sy += Math.sin(a);
    }
    return Math.atan2(sy, sx);
  }

  /** Quads between two loops of equal length: (a[k], a[k+1], b[k+1], b[k]). */
  private bridgeLoops(a: number[], b: number[], junction: number): void {
    const M = a.length;
    for (let k = 0; k < M; k++) {
      const k1 = (k + 1) % M;
      const u0 = k / M;
      const u1 = (k + 1) / M;
      this.mesh.addQuad(a[k], a[k1], b[k1], b[k], [u0, -0.05, u1, -0.05, u1, 0, u0, 0]);
      if (junction) {
        this.mesh.junction[a[k]] = 1;
        this.mesh.junction[b[k]] = 1;
      }
    }
  }

  /** Ladder cap. `flip` reverses the winding (bottom caps). */
  private cap(ring: number[], flip: boolean): void {
    if (!this.P.capTips && !flip) return;
    const M = ring.length;
    const r = flip ? [...ring].reverse() : ring;
    for (let i = 0; 2 * i <= M - 3; i++) {
      const a = r[i];
      const b = r[i + 1];
      const c = r[M - 2 - i];
      const d = r[M - 1 - i];
      this.mesh.addQuad(a, b, c, d, [0, 0, 1, 0, 1, 1, 0, 1]);
    }
  }

  // ---------------------------------------------------------------------------
  // Side-branch windows
  // ---------------------------------------------------------------------------

  private planHoles(stem: Stem, N: number, phase: number, sStart: number, L: number): HolePlan[] {
    const P = this.P;
    const B = this.B;
    const cands: HolePlan[] = [];
    const children = stem.children.filter((c) => !c.dropped);
    // Largest children first so they claim their space.
    children.sort((a, b) => stemRadiusAt(b, 0, P, B) - stemRadiusAt(a, 0, P, B));

    const colStep = TAU / N;
    const wMax = Math.max(1, Math.floor(N / 2) - 1);

    for (const child of children) {
      const minRing = Math.max(4, Math.round((child.minRing ?? P.minRadialSegments) / 2) * 2);
      const sAttach = child.attachS;
      const smp = sampleStem(stem, sAttach);
      const rp = stemRadiusAt(stem, sAttach, P, B);
      const rc = stemRadiusAt(child, 0, P, B);
      const rho = rc * P.collarScale;
      const chord = (TAU * rp) / N;
      // The window is centred on the axial footprint of the child's first ring,
      // not on the point where the child axis pierces the surface: an inclined
      // branch reaches further along the parent on its upper side.
      const a0 = child.nodes[0].dir;
      const d0 = collarStart(child, P, B);
      const along = d0 * dot(a0, smp.dir);
      const sinT = Math.sqrt(Math.max(0, 1 - dot(a0, smp.dir) ** 2));
      const extentHalf = rc * Math.max(0.6, 1 / Math.max(0.35, sinT)) * 0.5 + rc * 0.5;
      // Primary roots come with their window width laid out by the root
      // builder (all of them fit round the trunk at ground level, narrow and
      // tall where the ring is crowded – a buttress leaves the trunk as an
      // upright fin anyway).
      let w = child.windowCols ? clamp(Math.round(child.windowCols), 1, wMax) : clamp(Math.round((2 * rho) / chord), 1, wMax);
      // The child ring has 2(w + h) vertices; h rows follow from the window height.
      // Guarantee the minimum ring size by widening (up to wMax) then heightening.
      const hMin = Math.max(1, Math.ceil(minRing / 2 - w));
      if (hMin > 2 && w < wMax && !child.windowCols) w = Math.min(wMax, Math.max(w, Math.ceil(minRing / 2) - 2));
      const W = w * chord;
      const hRows = Math.max(1, Math.ceil(minRing / 2 - w));
      const hh = Math.max(rho, extentHalf * P.collarScale, W * 0.5, ((hRows * W) / (2 * w)) * 0.9);
      const s = sAttach + along;
      const up = cross(smp.dir, smp.right);
      let radial = projectOnPlane(sub(child.nodes[0].pos, smp.pos), smp.dir);
      if (lengthSq(radial) < 1e-12) radial = projectOnPlane(child.attachDir, smp.dir);
      if (lengthSq(radial) < 1e-12) radial = smp.right;
      radial = normalize(radial);
      const phi = Math.atan2(dot(radial, up), dot(radial, smp.right)) - phase;
      const jc = phi / colStep; // fractional column of the child centre
      const j0 = Math.round(jc - w / 2);
      cands.push({ child, s, sLow: s - hh, sHigh: s + hh, j0, w, hh, rows: hRows, row0: 0, row1: 0, along });
    }

    const accepted: HolePlan[] = [];
    const r0 = stemRadiusAt(stem, 0, P, B);
    const sMin = sStart + (stem.attach === 'side' ? 0.45 * r0 : 1e-4);
    const maxTurn = Math.min(Math.PI / 4, Math.PI * 0.75);

    for (const c of cands) {
      const eps = c.hh * 0.6;
      const conflicts = (sLow: number, sHigh: number, j0: number): boolean => {
        for (const a of accepted) {
          const sOverlap = sLow < a.sHigh + eps && a.sLow < sHigh + eps;
          if (!sOverlap) continue;
          if (circularOverlap(j0 - 1, c.w + 2, a.j0, a.w, N)) return true;
        }
        return false;
      };
      const fits = (sLow: number, sHigh: number): boolean => sLow >= sMin + c.hh * 0.15 && sHigh <= L + 1e-6 && sLow + c.hh - c.along > 1e-4;

      let placed: { ds: number; dj: number } | null = null;
      const options: { ds: number; dj: number; cost: number }[] = [];
      const maxDj = Math.max(1, Math.min(c.w + 1, Math.floor(maxTurn / colStep)));
      // Roots would rather move sideways around the base than along the trunk,
      // and down (below ground, out of sight) rather than up.
      const isRoot = c.child.role === 'root';
      for (let dj = -maxDj; dj <= maxDj; dj++) {
        for (let ks = -6; ks <= 6; ks++) {
          const ds = ks * c.hh;
          const cost = isRoot ? (ks < 0 ? -ks * 2.0 : ks * 6.0) + Math.abs(dj) * 1.0 : Math.abs(ks) * 1.0 + Math.abs(dj) * 1.5;
          options.push({ ds, dj, cost });
        }
      }
      options.sort((a, b) => a.cost - b.cost);
      // A primary root stays at ground level if at all possible: sideways
      // moves and a narrower window come before any move along the trunk.
      const groundLevel = !!c.child.windowCols;
      const attempts: { narrow: boolean; anyDs: boolean }[] = groundLevel
        ? [
            { narrow: false, anyDs: false },
            { narrow: true, anyDs: false },
            { narrow: false, anyDs: true },
          ]
        : [
            { narrow: false, anyDs: true },
            { narrow: true, anyDs: true },
          ];
      for (const a of attempts) {
        if (placed) break;
        if (a.narrow) {
          // A tighter collar: one column narrower, one row taller keeps the ring size.
          if (c.w <= 1) continue;
          c.w -= 1;
          c.rows += 1;
        }
        for (const o of options) {
          if (!a.anyDs && o.ds !== 0) continue;
          const sLow = c.sLow + o.ds;
          const sHigh = c.sHigh + o.ds;
          if (!fits(sLow, sHigh)) continue;
          if (conflicts(sLow, sHigh, c.j0 + o.dj)) continue;
          placed = o;
          break;
        }
      }
      if (!placed) {
        this.drop(c.child, 'window conflict');
        continue;
      }

      // Move the child so that it exits exactly through the centre of its window.
      const oldSmp = sampleStem(stem, c.child.attachS);
      const newS = c.s + placed.ds;
      const newAttach = newS - c.along;
      const newJ0 = c.j0 + placed.dj;
      const newCenterCol = newJ0 + c.w / 2;
      const oldUp = cross(oldSmp.dir, oldSmp.right);
      const oldRadial = normalize(projectOnPlane(sub(c.child.nodes[0].pos, oldSmp.pos), oldSmp.dir));
      const oldAz = Math.atan2(dot(oldRadial, oldUp), dot(oldRadial, oldSmp.right));
      const newAz = phase + newCenterCol * colStep;
      let dAz = newAz - oldAz;
      while (dAz > Math.PI) dAz -= TAU;
      while (dAz < -Math.PI) dAz += TAU;

      const newSmp = sampleStem(stem, newAttach);
      const newUp = cross(newSmp.dir, newSmp.right);
      const rpNew = this.surfaceRadius(stem, newAttach, newAz);
      const radialNew = add(vscale(newSmp.right, Math.cos(newAz)), vscale(newUp, Math.sin(newAz)));
      const target = addScaled(newSmp.pos, radialNew, rpNew);

      if (c.child.regrow) {
        // Environment-fitted stem (root): regrow it from the new exit so it
        // still follows the ground and the objects around the tree.
        const delta = sub(target, c.child.nodes[0].pos);
        if (Math.abs(dAz) > 1e-6 || lengthSq(delta) > 1e-14) {
          const newDir = normalize(rotateAxisV(c.child.attachDir, oldSmp.dir, dAz));
          c.child.attachDir = newDir;
          c.child.attachS = newAttach;
          c.child.regrow(target, newDir);
        }
      } else {
        if (Math.abs(dAz) > 1e-6) rotateSubtree(c.child, oldSmp.pos, oldSmp.dir, dAz);
        const delta = sub(target, c.child.nodes[0].pos);
        if (lengthSq(delta) > 1e-14) translateSubtree(c.child, delta);
      }

      c.child.attachS = newAttach;
      c.s = newS;
      c.sLow += placed.ds;
      c.sHigh += placed.ds;
      c.j0 = mod(newJ0, N);
      accepted.push(c);
    }
    return accepted;
  }

  private buildStations(stem: Stem, holes: HolePlan[], sStart: number, L: number, kind: BaseInfo['kind']): number[] {
    const P = this.P;
    const B = this.B;
    const curveRes = Math.max(1, Math.round(B.curveRes[Math.min(stem.level, 3)]));
    const rps = Math.max(1, P.ringsPerSegment[Math.min(stem.level, 3)]);
    const spacing = stem.ringSpacing ?? stem.logicalLength / curveRes / rps;

    if (L - sStart < 1e-6) return [sStart];

    type St = { s: number; pri: number };
    const mand: St[] = [
      { s: sStart, pri: 3 },
      { s: L, pri: 3 },
    ];
    let minHole = Infinity;
    for (const h of holes) {
      mand.push({ s: h.sLow, pri: 2 }, { s: h.sHigh, pri: 2 });
      for (let r = 1; r < h.rows; r++) mand.push({ s: h.sLow + ((h.sHigh - h.sLow) * r) / h.rows, pri: 2 });
      const cell = (h.hh * 2) / h.rows;
      if (cell < minHole) minHole = cell;
    }
    if (kind === 'root') {
      // Denser rings through the root flare.
      const zs = [0.012, 0.028, 0.05, 0.08, 0.12, 0.17];
      for (const z of zs) {
        const s = (z - stem.logicalStart) * stem.logicalLength;
        if (s > sStart && s < L) mand.push({ s, pri: 1 });
      }
      // Culm nodes: rings on the ridge and on either shoulder of every swelling.
      if (B.nodeSwell > 0) {
        const sp = Math.max(0.01, B.nodeSpacing);
        const w = 0.16 * sp;
        for (let z = sp; z < 1; z += sp) {
          for (const dz of [-w, -w * 0.5, 0, w * 0.5, w]) {
            const s = (z + dz - stem.logicalStart) * stem.logicalLength;
            if (s > sStart && s < L) mand.push({ s, pri: 1 });
          }
        }
      }
    } else if (kind === 'side' && stem.role !== 'root') {
      // Follow the emergence bend: one ring per skeleton node across the first segment.
      const segLen = stem.logicalLength / curveRes;
      for (const nd of stem.nodes) {
        if (nd.s <= sStart + 1e-6) continue;
        if (nd.s > segLen + 1e-6 || nd.s >= L) break;
        mand.push({ s: nd.s, pri: 1 });
      }
    }
    const eps = Math.min(spacing * 0.3, isFinite(minHole) ? minHole * 0.3 : Infinity);

    mand.sort((a, b) => a.s - b.s || b.pri - a.pri);
    const kept: St[] = [];
    for (const st of mand) {
      const last = kept[kept.length - 1];
      if (last && st.s - last.s < eps) {
        // Merge: the higher priority survives (end ring always survives).
        if (st.pri > last.pri) kept[kept.length - 1] = st;
        continue;
      }
      kept.push(st);
    }
    // The end station must be exact.
    kept[kept.length - 1] = { s: L, pri: 3 };
    if (kept.length >= 2 && kept[kept.length - 1].s - kept[kept.length - 2].s < eps * 0.5) kept.splice(kept.length - 2, 1);

    // Uniform stations in the gaps.
    const out: number[] = [];
    for (let i = 0; i < kept.length; i++) {
      out.push(kept[i].s);
      if (i === kept.length - 1) break;
      const gap = kept[i + 1].s - kept[i].s;
      const n = Math.floor(gap / spacing);
      if (n >= 1) {
        const step = gap / (n + 1);
        for (let k = 1; k <= n; k++) out.push(kept[i].s + k * step);
      }
    }
    return out;
  }

  /** Closed loop of vertex indices around a window, counter-clockwise seen from outside. */
  private holeLoop(rings: Ring[], h: HolePlan, N: number): number[] {
    const loop: number[] = [];
    const { row0, row1, j0, w } = h;
    for (let j = 0; j <= w; j++) loop.push(rings[row0].idx[mod(j0 + j, N)]);
    for (let i = row0 + 1; i <= row1; i++) loop.push(rings[i].idx[mod(j0 + w, N)]);
    for (let j = w - 1; j >= 0; j--) loop.push(rings[row1].idx[mod(j0 + j, N)]);
    for (let i = row1 - 1; i >= row0 + 1; i--) loop.push(rings[i].idx[mod(j0, N)]);
    for (const v of loop) this.mesh.junction[v] = 1;
    return loop;
  }

  // ---------------------------------------------------------------------------
  // Forks
  // ---------------------------------------------------------------------------

  private meshFork(stem: Stem, E: Ring, depth: number): void {
    const P = this.P;
    const B = this.B;
    const N = E.idx.length;
    const kids = stem.forks.filter((k) => !k.dropped);
    if (kids.length === 0) {
      this.cap(E.idx, false);
      return;
    }
    this.stats.forks++;
    const F = E.center;
    const dirP = E.dir;
    const right = E.idx.length ? normalize(projectOnPlane(sub(this.pos(E.idx[0]), F), dirP)) : anyPerpendicular(dirP);
    const up = cross(dirP, right);
    // Ring vertex j sits at azimuth j * TAU / N in the (right, up) frame, by construction.
    const colStep = TAU / N;

    // Mean direction and per-child geometry.
    let meanDir = { x: 0, y: 0, z: 0 };
    for (const k of kids) meanDir = add(meanDir, k.nodes[0].dir);
    meanDir = lengthSq(meanDir) < 1e-10 ? dirP : normalize(meanDir);

    type KidInfo = { stem: Stem; col: number; r: number; theta: number; t1: number };
    const infos: KidInfo[] = [];
    for (const k of kids) {
      const d = k.nodes[0].dir;
      let proj = projectOnPlane(d, dirP);
      const r = stemRadiusAt(k, 0, P, B);
      const theta = Math.max(0.05, Math.acos(clamp(dot(d, meanDir), -1, 1)));
      const sep = Math.sin(Math.min(Math.PI / 2, 2 * theta));
      const t1 = clamp((2 * r) / Math.max(0.05, sep), 1.5 * r, 6 * r);
      infos.push({ stem: k, col: 0, r, theta, t1 });
      if (lengthSq(proj) < 1e-10) proj = { x: 0, y: 0, z: 0 };
      (infos[infos.length - 1] as KidInfo & { proj: V3 }).proj = proj;
    }
    // Azimuths; a child exactly on the parent axis takes the opposite of the others.
    for (const info of infos) {
      const proj = (info as KidInfo & { proj: V3 }).proj;
      if (lengthSq(proj) > 1e-10) {
        const az = Math.atan2(dot(proj, up), dot(proj, right));
        info.col = mod(az / colStep, N);
      } else {
        info.col = NaN;
      }
    }
    for (const info of infos) {
      if (!Number.isNaN(info.col)) continue;
      let sx = 0;
      let sy = 0;
      for (const o of infos) {
        if (Number.isNaN(o.col)) continue;
        sx += Math.cos(o.col * colStep);
        sy += Math.sin(o.col * colStep);
      }
      const az = Math.atan2(-sy, -sx);
      info.col = mod(az / colStep, N);
    }
    infos.sort((a, b) => a.col - b.col);
    const k = infos.length;

    // Split vertices between consecutive children (radius-weighted bisectors).
    const splits: number[] = new Array(k);
    for (let i = 0; i < k; i++) {
      const a = infos[i];
      const b = infos[(i + 1) % k];
      let gap = b.col - a.col;
      if (i === k - 1) gap += N;
      if (k === 1) gap = N;
      const t = a.r / (a.r + b.r);
      splits[i] = mod(Math.round(a.col + gap * t), N);
    }
    // Ensure every arc has at least one edge.
    if (k > 1) {
      for (let iter = 0; iter < N; iter++) {
        let ok = true;
        for (let i = 0; i < k; i++) {
          const a = splits[(i + k - 1) % k];
          const b = splits[i];
          if (mod(b - a, N) === 0) {
            splits[i] = mod(b + 1, N);
            ok = false;
          }
        }
        if (ok) break;
      }
    }
    // Arc i (child i) runs from splits[i-1] to splits[i].
    const arcLen: number[] = new Array(k);
    let aMin = N;
    for (let i = 0; i < k; i++) {
      const a = splits[(i + k - 1) % k];
      const b = splits[i];
      arcLen[i] = k === 1 ? N : mod(b - a, N);
      if (arcLen[i] < aMin) aMin = arcLen[i];
    }

    if (k === 1) {
      // Single continuation: no crotch, the child simply continues the tube.
      const child = infos[0].stem;
      this.meshStem(child, { kind: 'fork', dRing: E.idx.slice(), t1: Math.min(infos[0].t1, child.length * 0.5) }, depth + 1);
      return;
    }

    // Hub and half-bridges.
    let hc = 0;
    for (const info of infos) hc += info.t1 * Math.cos(info.theta);
    hc /= k;
    hc = clamp(hc, 0.5 * E.radius, 4 * E.radius);
    const H = addScaled(F, meanDir, hc);
    const m = Math.max(0, Math.floor((aMin + 1) / 2) - 1);
    const hub = this.addVertex(H, stem, stem.length, 1);
    const half: number[][] = new Array(k); // half[i]: interior vertices from splits[i] towards H
    for (let i = 0; i < k; i++) {
      const S = this.pos(E.idx[splits[i]]);
      const arr: number[] = [];
      // Slight outward bulge so the crotch reads as wood, not as a paper fold.
      const outward = normalize(projectOnPlane(sub(S, F), meanDir));
      for (let q = 0; q < m; q++) {
        const u = (q + 1) / (m + 1);
        let p = lerp(S, H, u);
        p = addScaled(p, outward, Math.sin(Math.PI * u) * E.radius * 0.12);
        arr.push(this.addVertex(p, stem, stem.length, 1));
      }
      half[i] = arr;
    }
    for (const v of E.idx) this.mesh.junction[v] = 1;

    // D-ring per child and recursion.
    for (let i = 0; i < k; i++) {
      const sA = splits[(i + k - 1) % k];
      const ring: number[] = [];
      for (let j = 0; j <= arcLen[i]; j++) ring.push(E.idx[mod(sA + j, N)]);
      for (const v of half[i]) ring.push(v);
      ring.push(hub);
      const back = half[(i + k - 1) % k];
      for (let q = back.length - 1; q >= 0; q--) ring.push(back[q]);
      const child = infos[i].stem;
      const t1 = Math.min(infos[i].t1, child.length * 0.7);
      this.meshStem(child, { kind: 'fork', dRing: ring, t1 }, depth + 1);
    }
  }

  // ---------------------------------------------------------------------------
  // Leaves (proxy quads)
  // ---------------------------------------------------------------------------

  private buildLeaves(): void {
    const B = this.B;
    const lm = this.leafMesh;
    const isDropped = (s: Stem): boolean => {
      let c: Stem | null = s;
      while (c) {
        if (c.dropped) return true;
        c = c.parent;
      }
      return false;
    };
    for (const leaf of this.skel.leaves) {
      if (isDropped(leaf.stem)) continue;
      const len = leaf.scale;
      const hw = leaf.scale * B.leafScaleX * 0.5;
      const s = leaf.t * leaf.stem.length;
      const wind = this.windAt(leaf.stem, s, leaf.pos.y);
      wind.detail = 1;
      const pivot = this.pivotOf(leaf.stem);
      // Slight cupping along the midrib.
      const tip = addScaled(leaf.pos, leaf.dir, len);
      const sag = addScaled(tip, leaf.normal, -len * 0.12);
      const corners = [
        addScaled(leaf.pos, leaf.right, -hw * 0.35),
        addScaled(leaf.pos, leaf.right, hw * 0.35),
        addScaled(lerp(leaf.pos, sag, 0.55), leaf.right, hw),
        sag,
        addScaled(lerp(leaf.pos, sag, 0.55), leaf.right, -hw),
      ];
      // 5-point leaf → 3 triangles (kite), stays cheap.
      const base = lm.positions.length / 3;
      for (const c of corners) {
        lm.positions.push(c.x, c.y, c.z);
        lm.normals.push(leaf.normal.x, leaf.normal.y, leaf.normal.z);
        lm.wind.push(wind.height, wind.limb, wind.phase, wind.detail);
        lm.pivots.push(pivot.x, pivot.y, pivot.z);
      }
      lm.uvs.push(0.4, 0, 0.6, 0, 1, 0.55, 0.5, 1, 0, 0.55);
      lm.indices.push(base, base + 1, base + 2, base, base + 2, base + 3, base, base + 3, base + 4);
    }
  }
}

/** Arc length of a side stem's first full ring (measured from the parent surface). */
function collarStart(stem: Stem, P: TreeParams['mesh'], B: TreeParams['botany']): number {
  const r0 = stemRadiusAt(stem, 0, P, B);
  return Math.min(P.collarLength * r0, 0.5 * stem.length);
}

function nearestIndex(sorted: number[], v: number): number {
  let lo = 0;
  let hi = sorted.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (sorted[mid] <= v) lo = mid;
    else hi = mid;
  }
  return Math.abs(sorted[lo] - v) <= Math.abs(sorted[hi] - v) ? lo : hi;
}

/** Do circular integer intervals [a, a+wa) and [b, b+wb) on a ring of N overlap? */
function circularOverlap(a: number, wa: number, b: number, wb: number, N: number): boolean {
  if (wa >= N || wb >= N) return true;
  a = mod(a, N);
  b = mod(b, N);
  const d = mod(b - a, N);
  if (d < wa) return true;
  const d2 = mod(a - b, N);
  return d2 < wb;
}

export function buildMesh(skel: Skeleton): MeshBuildResult {
  return new Mesher(skel).build();
}

// Re-export helpers used by tests.
export const _internal = { circularOverlap, nearestIndex, length };
