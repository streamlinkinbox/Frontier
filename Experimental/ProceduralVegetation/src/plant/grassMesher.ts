/**
 * Grass mesher.
 *
 * A grass plant is built with the same discipline as the trees: ONE closed
 * quad manifold. The crown (tussock base) is a grid dome; every blade, culm,
 * culm leaf, rachis branchlet, spikelet and awn is a closed tube that leaves
 * its parent through a rectangular window cut into the parent's ring grid and
 * is welded to it with collar loops. Nothing is intersected, instanced or
 * merged, so the topology validator reports a single genus-0 surface and the
 * wind deformation is continuous from the soil to the tip of every awn.
 *
 * Organ levels (for the Levels view and the GLB TEXCOORD_1 channel):
 *   0 crown · 1 culms (incl. the rachis) · 2 blades and culm leaves ·
 *   3 inflorescence parts (branchlets, bristles, spikelets, awns).
 */

import { QuadMesh, VertexWind } from '../tree/mesh';
import { Random } from '../core/random';
import { V3, UP, TAU, DEG2RAD, add, addScaled, sub, dot, cross, normalize, lengthSq, lerp, mod, clamp, rotateTowards, rotateAxis, scale } from '../core/math';
import { Line, Frame, growLine } from './line';
import { GrassParams, grassHeight } from './grassParams';

export interface GrassStats {
  /** Organs meshed (crown, blades, culms, leaves, head parts). */
  organs: number;
  /** Windows welded (every organ but the crown has one). */
  junctions: number;
  dropped: number;
  dropReasons: Record<string, number>;
  /** crown · culms · blades (incl. culm leaves) · inflorescence parts */
  perLevel: [number, number, number, number];
  culms: number;
  blades: number;
}

export interface GrassBuildResult {
  mesh: QuadMesh;
  stats: GrassStats;
  height: number;
  /** Depth of the crown's root ball below the ground (metres). */
  groundDepth: number;
}

/** Where a child leaves its parent: a point on the parent surface with its frame. */
interface Exit {
  pos: V3;
  /** Outward surface normal. */
  normal: V3;
  /** Parent axis direction (UP for the crown). */
  dir: V3;
  /** Physical size of the window (longest side), metres. */
  size: number;
  /** Vertices of the window loop = vertices around the child's rings. */
  N: number;
}

interface Attachment {
  /** Arc length of the exit along the parent. */
  s: number;
  /** Azimuth of the exit in the parent's ring frame, radians. */
  az: number;
  /** Window size in cells. */
  w: number;
  h: number;
  /** Half height of the window along the parent, metres. */
  hh: number;
  make: (exit: Exit) => Organ;
  // Filled in by the planner.
  j0: number;
  row0: number;
  row1: number;
}

interface Organ {
  level: number;
  line: Line;
  /** Arc length of the first ring (collar length). */
  sStart: number;
  /** Ring vertex j of N at arc length s, in the (right, up) frame of the centreline. */
  profile: (s: number, j: number, N: number) => { x: number; y: number };
  /** Local radius (window sizing, exits of children). */
  radius: (s: number) => number;
  /** Round cross-section: the ring phase is fitted to the window (blades keep their width axis). */
  round: boolean;
  /** Extra mandatory ring stations. */
  extra: number[];
  /** Regular ring spacing. */
  spacing: number;
  children: Attachment[];
  wind: (s: number, y: number) => VertexWind;
  pivot: V3;
  /** Nominal radius for the collar fillet clamp. */
  r0: number;
}

interface Ring {
  idx: number[];
  s: number;
  f: Frame;
}

interface CrownWindow {
  a0: number;
  b0: number;
  w: number;
  h: number;
  make: (exit: Exit) => Organ;
}

type HeadVals = { length: number; droop: number; secondaries: number; bodyR: number; awnL: number; angle: number; rb: number; phase: number };

const GOLDEN = 2.399963229728653; // 137.5°

/** (rows, columns) offsets tried when a window does not fit where the child wants it, cheapest first. */
const OFFSETS: [number, number][] = [
  [0, 0],
  [0, 1],
  [0, -1],
  [1, 0],
  [-1, 0],
  [1, 1],
  [1, -1],
  [-1, 1],
  [-1, -1],
  [2, 0],
  [-2, 0],
  [0, 2],
  [0, -2],
  [2, 1],
  [2, -1],
  [-2, 1],
  [-2, -1],
  [3, 0],
  [-3, 0],
  [3, 1],
  [-3, 1],
  [4, 0],
  [-4, 0],
];

export class GrassMesher {
  readonly mesh = new QuadMesh();
  private readonly g: GrassParams;
  private readonly rng: Random;
  private readonly bladeRng: Random;
  private readonly culmRng: Random;
  private readonly headRng: Random;
  private plantH = 1;
  /** Detail flutter weight: absolute in the shader, so small plants get less of it. */
  private flutter = 1;
  private groundDepth = 0;
  private stats: GrassStats = { organs: 0, junctions: 0, dropped: 0, dropReasons: {}, perLevel: [0, 0, 0, 0], culms: 0, blades: 0 };

  constructor(g: GrassParams, seed: number) {
    this.g = g;
    this.rng = new Random((seed ^ 0x6ea55c0d) >>> 0);
    this.bladeRng = this.rng.fork();
    this.culmRng = this.rng.fork();
    this.headRng = this.rng.fork();
  }

  build(): GrassBuildResult {
    this.plantH = Math.max(0.05, grassHeight(this.g));
    this.flutter = clamp(this.plantH / 1.5, 0.15, 1);
    this.meshCrown();
    // Normalise the sway weight by the real height of the plant.
    let maxY = 1e-3;
    const p = this.mesh.positions;
    for (let i = 1; i < p.length; i += 3) if (p[i] > maxY) maxY = p[i];
    const w = this.mesh.wind;
    for (let i = 0; i < w.length; i += 4) w[i] = clamp(p[(i / 4) * 3 + 1] / maxY, 0, 1);
    return { mesh: this.mesh, stats: this.stats, height: maxY, groundDepth: this.groundDepth };
  }

  // ---------------------------------------------------------------------------
  // Crown: a grid dome with the tillers laid out on a lattice of windows
  // ---------------------------------------------------------------------------

  private meshCrown(): void {
    const g = this.g;
    const mesh = this.mesh;
    const R = Math.max(0.01, g.crownRadius);
    const sink = Math.max(0, g.crownSink) + 0.004;
    const H = Math.max(0, g.crownHeight);
    const prof = Math.max(1.2, g.crownProfile);

    // Window shapes.
    const bladeN = evenSides(g.bladeSides, 4, 8);
    const culmN = evenSides(g.culmSides, 6, 12);
    const bw = windowFor(bladeN);
    const cw = windowFor(culmN);
    const pitch = Math.max(bw.w, bw.h) + 1;
    const bsx = Math.ceil((cw.w + 1) / pitch);
    const bsy = Math.ceil((cw.h + 1) / pitch);
    const blades = Math.max(0, Math.round(g.blades));
    const culms = Math.max(0, Math.round(g.culms));
    const need = blades + culms * bsx * bsy;
    const S = Math.max(bsx + 1, bsy + 1, Math.ceil(Math.sqrt((Math.max(1, need) * 1.18) / 0.7)));
    const K = S * pitch + 2;
    const cell = (2 * R) / K;

    // Dome geometry (square grid mapped onto the disc with the elliptical mapping).
    const domePoint = (u01: number, v01: number): V3 => {
      const u = 2 * u01 - 1;
      const v = 2 * v01 - 1;
      const x = u * Math.sqrt(Math.max(0, 1 - (v * v) / 2));
      const z = v * Math.sqrt(Math.max(0, 1 - (u * u) / 2));
      const r = Math.min(1, Math.sqrt(x * x + z * z));
      const y = -sink + H * (1 - Math.pow(r, prof));
      return { x: R * x, y, z: R * z };
    };
    const domeNormal = (u01: number, v01: number): V3 => {
      const e = 0.5 / K;
      const du = sub(domePoint(Math.min(1, u01 + e), v01), domePoint(Math.max(0, u01 - e), v01));
      const dv = sub(domePoint(u01, Math.min(1, v01 + e)), domePoint(u01, Math.max(0, v01 - e)));
      const n = cross(dv, du);
      return lengthSq(n) < 1e-18 ? { x: 0, y: 1, z: 0 } : normalize(n);
    };

    // Sites: one per pitch×pitch block of cells.
    type Site = { i: number; k: number; r: number; az: number; free: boolean };
    const sites: Site[][] = [];
    for (let i = 0; i < S; i++) {
      sites.push([]);
      for (let k = 0; k < S; k++) {
        const ca = 1 + i * pitch + pitch / 2;
        const cb = 1 + k * pitch + pitch / 2;
        const p = domePoint(ca / K, cb / K);
        const r = Math.sqrt(p.x * p.x + p.z * p.z) / R;
        sites[i].push({ i, k, r, az: Math.atan2(p.z, p.x), free: r <= 0.955 });
      }
    }

    const windows: CrownWindow[] = [];
    const rngL = this.rng;

    // Culms first: they claim blocks of sites in the middle of the tuft.
    let culmsPlaced = 0;
    for (let c = 0; c < culms; c++) {
      let placed = false;
      for (let attempt = 0; attempt < 120 && !placed; attempt++) {
        const i = rngL.int(Math.max(1, S - bsx + 1));
        const k = rngL.int(Math.max(1, S - bsy + 1));
        const centre = sites[Math.min(S - 1, i + (bsx - 1) / 2) | 0][Math.min(S - 1, k + (bsy - 1) / 2) | 0];
        if (centre.r > 0.82 && attempt < 100) continue;
        let ok = true;
        for (let a = i; a < i + bsx && ok; a++) for (let b = k; b < k + bsy; b++) if (a >= S || b >= S || !sites[a][b].free) ok = false;
        if (!ok) continue;
        for (let a = i; a < i + bsx; a++) for (let b = k; b < k + bsy; b++) sites[a][b].free = false;
        const a0 = 1 + i * pitch + Math.floor((bsx * pitch - cw.w) / 2);
        const b0 = 1 + k * pitch + Math.floor((bsy * pitch - cw.h) / 2);
        const rc = this.culmRng;
        const vals = {
          lean: g.culmLean + g.culmLeanV * rc.uniform(),
          az: centre.az + rc.uniform() * 1.0,
          height: g.culmHeight * (1 + g.culmHeightV * rc.uniform()),
          phase: rc.next(),
          curveJ: rc.uniform(),
          leafAz: rc.next() * TAU,
        };
        windows.push({ a0, b0, w: cw.w, h: cw.h, make: (exit) => this.makeCulm(exit, vals) });
        placed = true;
        culmsPlaced++;
      }
      if (!placed) this.drop('no room on crown');
    }

    // Blades: weighted choice of the remaining sites.
    const free: Site[] = [];
    for (const row of sites) for (const s of row) if (s.free) free.push(s);
    const bias = g.centreBias;
    const keyed = free.map((s) => {
      const wgt = bias >= 0 ? Math.exp(-3 * bias * s.r) : Math.exp(3 * bias * (1 - s.r));
      return { s, key: -Math.log(Math.max(1e-9, rngL.next())) / Math.max(1e-6, wgt) };
    });
    keyed.sort((a, b) => a.key - b.key);
    const nBlades = Math.min(blades, keyed.length);
    if (nBlades < blades) this.drop('no room on crown', blades - nBlades);
    for (let n = 0; n < nBlades; n++) {
      const s = keyed[n].s;
      s.free = false;
      const rb = this.bladeRng;
      const az = s.az + rb.uniform() * (0.6 + 1.6 * (1 - s.r));
      const lean = (g.lean * (0.25 + 0.75 * s.r) + g.leanV * rb.uniform()) * DEG2RAD;
      const vals = {
        az,
        lean,
        length: Math.max(0.01, g.bladeLength * (1 + g.bladeLengthV * rb.uniform())),
        width: Math.max(0.0005, g.bladeWidth * (1 + 0.15 * rb.uniform())),
        droop: (g.droop + g.droopV * rb.uniform()) * DEG2RAD,
        twist: (g.twist + g.twistV * rb.uniform()) * DEG2RAD * (rb.next() < 0.5 ? -1 : 1),
        wavePhase: rb.next(),
        phase: rb.next(),
      };
      // A 1×2 window has its long side along the blade's width axis, which is
      // perpendicular to the lean direction (the blade leans in its normal plane).
      let w = bw.w;
      let h = bw.h;
      if (w !== h) {
        const leanAlongX = Math.abs(Math.cos(az)) >= Math.abs(Math.sin(az));
        w = leanAlongX ? Math.min(bw.w, bw.h) : Math.max(bw.w, bw.h);
        h = leanAlongX ? Math.max(bw.w, bw.h) : Math.min(bw.w, bw.h);
      }
      const a0 = 1 + s.i * pitch + Math.floor((pitch - w) / 2);
      const b0 = 1 + s.k * pitch + Math.floor((pitch - h) / 2);
      windows.push({ a0, b0, w, h, make: (exit) => this.makeBlade(exit, { ...vals, level: 2, limb0: 0, pivot: exit.pos, segments: g.bladeSegments }) });
    }
    this.stats.culms = culmsPlaced;
    this.stats.blades = nBlades;

    // ---- Grid vertices -----------------------------------------------------
    const cellWin = new Int32Array(K * K).fill(-1);
    windows.forEach((win, id) => {
      for (let a = win.a0; a < win.a0 + win.w; a++) for (let b = win.b0; b < win.b0 + win.h; b++) cellWin[a * K + b] = id;
    });
    const occ = (a: number, b: number): boolean => a >= 0 && b >= 0 && a < K && b < K && cellWin[a * K + b] >= 0;
    const interior = (a: number, b: number): boolean => occ(a - 1, b - 1) && occ(a - 1, b) && occ(a, b - 1) && occ(a, b);
    const vid = new Int32Array((K + 1) * (K + 1)).fill(-1);
    const still: VertexWind = { height: 0, limb: 0, phase: 0, detail: 0 };
    const origin = { x: 0, y: 0, z: 0 };
    for (let a = 0; a <= K; a++) {
      for (let b = 0; b <= K; b++) {
        if (interior(a, b)) continue;
        const p = domePoint(a / K, b / K);
        vid[a * (K + 1) + b] = mesh.addVertex(p.x, p.y, p.z, still, origin, 0, 0);
      }
    }
    const V = (a: number, b: number): number => vid[a * (K + 1) + b];
    this.stats.organs++;
    this.stats.perLevel[0]++;

    // Top quads (skipping window cells): (a,b) → (a,b+1) → (a+1,b+1) → (a+1,b) faces up.
    for (let a = 0; a < K; a++) {
      for (let b = 0; b < K; b++) {
        if (cellWin[a * K + b] >= 0) continue;
        mesh.addQuad(V(a, b), V(a, b + 1), V(a + 1, b + 1), V(a + 1, b), [a / K, b / K, a / K, (b + 1) / K, (a + 1) / K, (b + 1) / K, (a + 1) / K, b / K]);
      }
    }

    // Root ball: the rim is carried straight down, inset, and closed with a ladder cap (invisible below ground).
    const depth = Math.max(0.02, 0.35 * R);
    this.groundDepth = sink + depth;
    const rim: number[] = [];
    for (let a = 0; a < K; a++) rim.push(V(a, K));
    for (let b = K; b > 0; b--) rim.push(V(K, b));
    for (let a = K; a > 0; a--) rim.push(V(a, 0));
    for (let b = 0; b < K; b++) rim.push(V(0, b));
    const below: number[] = [];
    for (const v of rim) below.push(mesh.addVertex(mesh.positions[v * 3] * 0.8, -sink - depth, mesh.positions[v * 3 + 2] * 0.8, still, origin, 0, 0));
    const M = rim.length;
    for (let j = 0; j < M; j++) {
      const j1 = (j + 1) % M;
      mesh.addQuad(below[j], below[j1], rim[j1], rim[j], [j / M, -0.1, (j + 1) / M, -0.1, (j + 1) / M, 0, j / M, 0]);
    }
    this.cap([...below].reverse());

    // ---- Tillers -------------------------------------------------------------
    for (const win of windows) {
      const loop: number[] = [];
      const { a0, b0, w, h } = win;
      // Counter-clockwise seen from above: +a along b0+h, −b along a0+w, −a along b0, +b along a0.
      for (let a = a0; a < a0 + w; a++) loop.push(V(a, b0 + h));
      for (let b = b0 + h; b > b0; b--) loop.push(V(a0 + w, b));
      for (let a = a0 + w; a > a0; a--) loop.push(V(a, b0));
      for (let b = b0; b < b0 + h; b++) loop.push(V(a0, b));
      const uc = (a0 + w / 2) / K;
      const vc = (b0 + h / 2) / K;
      const exit: Exit = { pos: domePoint(uc, vc), normal: domeNormal(uc, vc), dir: { x: 0, y: 1, z: 0 }, size: Math.max(w, h) * cell, N: loop.length };
      const organ = win.make(exit);
      this.meshTube(organ, loop, exit, 1);
    }
  }

  // ---------------------------------------------------------------------------
  // Organs
  // ---------------------------------------------------------------------------

  private makeBlade(
    exit: Exit,
    o: {
      az: number;
      lean: number;
      length: number;
      width: number;
      droop: number;
      twist: number;
      wavePhase: number;
      phase: number;
      level: number;
      limb0: number;
      pivot: V3;
      segments: number;
      dir0?: V3;
      right0?: V3;
    },
  ): Organ {
    const g = this.g;
    const L = o.length;
    let dir0: V3;
    let right0: V3;
    if (o.dir0 && o.right0) {
      dir0 = o.dir0;
      right0 = o.right0;
    } else {
      const A = { x: Math.cos(o.az), y: 0, z: Math.sin(o.az) };
      dir0 = normalize(add(scale(UP, Math.cos(o.lean)), scale(A, Math.sin(o.lean))));
      right0 = normalize(cross(UP, A));
    }
    const steps = Math.max(6, Math.round(o.segments) * 2);
    const p = Math.max(1, g.droopPower);
    const W = g.wave * DEG2RAD;
    const line = growLine(exit.pos, dir0, right0, L, steps, (t0, t1) => ({
      gravity: o.droop * (Math.pow(t1, p) - Math.pow(t0, p)),
      roll: o.twist * (t1 - t0),
      yaw: W * (Math.sin(TAU * (1.5 * t1 + o.wavePhase)) - Math.sin(TAU * (1.5 * t0 + o.wavePhase))),
    }));
    const taperP = 1 + 3 * clamp(g.bladeTaper, 0, 1);
    const keel = clamp(g.keel, 0, 1);
    const rolled = clamp(g.rolled, 0, 1);
    const thick = Math.max(0.02, g.bladeThickness);
    const halfW0 = o.width * 0.5;
    const sStart = Math.min(0.35 * L, Math.max(1.6 * halfW0, 1.0 * exit.size, this.collarLen(halfW0)));
    const limb0 = o.limb0;
    const flutter = this.flutter;
    const wind = (s: number, y: number): VertexWind => {
      const t = clamp(s / L, 0, 1);
      return { height: clamp(y / this.plantH, 0, 1), limb: clamp(limb0 + (1 - limb0) * t, 0, 1), phase: o.phase, detail: clamp((t - 0.35) / 0.65, 0, 1) * 0.5 * flutter };
    };
    const widthAt = (s: number): number => {
      const t = clamp(s / L, 0, 1);
      return o.width * ((1 - Math.pow(t, taperP)) * 0.92 + 0.08);
    };
    return {
      level: o.level,
      line,
      sStart,
      profile: (s, j, N) => {
        const t = clamp(s / L, 0, 1);
        const w = widthAt(s);
        const th = o.width * thick * (1 - 0.7 * t);
        return bladePoint(N, j, w, th, keel, rolled);
      },
      radius: (s) => 0.5 * widthAt(s),
      round: false,
      extra: [],
      spacing: Math.max(1e-4, (L - sStart) / Math.max(1, Math.round(o.segments))),
      children: [],
      wind,
      pivot: o.pivot,
      r0: halfW0,
    };
  }

  private makeCulm(exit: Exit, v: { lean: number; az: number; height: number; phase: number; curveJ: number; leafAz: number }): Organ {
    const g = this.g;
    const N = exit.N;
    const Lc = Math.max(0.02, v.height);
    const hasHead = g.head !== 'none';
    const Lh = hasHead ? Math.max(0.005, g.headLength) : 0;
    const L = Lc + Lh;
    const A = { x: Math.cos(v.az), y: 0, z: Math.sin(v.az) };
    const lean = v.lean * DEG2RAD;
    const dir0 = normalize(add(scale(UP, Math.cos(lean)), scale(A, Math.sin(lean))));
    const right0 = normalize(cross(UP, A));
    const curve = g.culmCurve * (1 + 0.4 * v.curveJ) * DEG2RAD;
    const nod = g.headDroop * DEG2RAD;
    const angleAt = (s: number): number => (s <= Lc ? curve * Math.pow(s / Lc, 1.6) : curve + (Lh > 0 ? nod * Math.pow((s - Lc) / Lh, 0.8) : 0));
    const steps = Math.max(8, Math.round(g.culmSegments) * 2 + (hasHead ? 8 : 0));
    const line = growLine(exit.pos, dir0, right0, L, steps, (t0, t1) => ({
      gravity: angleAt(t1 * L) - angleAt(t0 * L),
      yaw: 1.5 * DEG2RAD * (Math.sin(TAU * (1.2 * t1 + v.phase)) - Math.sin(TAU * (1.2 * t0 + v.phase))),
    }));
    const rBase = Math.max(0.0003, g.culmRadius);
    const rTop = rBase * 0.7;
    const base = (s: number): number => rBase * (1 - 0.3 * (s / Lc));

    // Nodes, sheaths and the leaf windows at the top of the sheaths.
    const nodes = Math.max(0, Math.round(g.nodes));
    const nodeS: number[] = [];
    for (let k = 0; k < nodes; k++) nodeS.push(Lc * Math.pow((k + 1) / (nodes + 1), 1.25));
    const sheath = 0.45;
    // Leaf window: as many columns (the leaf's width axis) as the culm ring allows.
    const bladeN = evenSides(g.bladeSides, 4, 8);
    const lw = { w: Math.min(bladeN / 2 - 1, N / 2 - 1), h: 1 };
    lw.h = bladeN / 2 - lw.w;
    const nodeHH = nodeS.map((s) => 0.5 * lw.h * ((TAU * base(s) * (1 + sheath)) / N));
    const sheathTop: number[] = [];
    const leafS: number[] = [];
    for (let k = 0; k < nodes; k++) {
      const next = k + 1 < nodes ? nodeS[k + 1] : Lc;
      const len = Math.max(4.5 * nodeHH[k], Math.min(0.55 * (next - nodeS[k]), next - nodeS[k] - 3 * nodeHH[k]));
      sheathTop.push(nodeS[k] + len);
      leafS.push(nodeS[k] + len - nodeHH[k]);
    }
    const swell = Math.max(0, g.nodeSwell);
    const radius = (s: number): number => {
      if (s <= Lc) {
        let r = base(s);
        for (let k = 0; k < nodes; k++) {
          const hh = nodeHH[k];
          if (swell > 0) {
            const d = (s - nodeS[k]) / Math.max(1e-6, 1.1 * hh);
            r *= 1 + swell * Math.exp(-d * d);
          }
          const up = clamp((s - nodeS[k] - 0.5 * hh) / (1.5 * hh), 0, 1);
          const down = 1 - clamp((s - sheathTop[k]) / (2 * hh), 0, 1);
          r *= 1 + sheath * up * down;
        }
        return r;
      }
      const t = (s - Lc) / Math.max(1e-6, Lh);
      return rTop * (1 - 0.5 * t);
    };
    const wind = (s: number, y: number): VertexWind => ({ height: clamp(y / this.plantH, 0, 1), limb: clamp(s / L, 0, 1), phase: v.phase, detail: s > Lc ? 0.5 * this.flutter : 0 });
    const pivot = exit.pos;
    const extra: number[] = [];
    for (let k = 0; k < nodes; k++) {
      const hh = nodeHH[k];
      extra.push(nodeS[k] - 1.5 * hh, nodeS[k], nodeS[k] + 0.5 * hh, nodeS[k] + 1.25 * hh, nodeS[k] + 2 * hh, sheathTop[k] + hh, sheathTop[k] + 2 * hh);
    }
    if (hasHead) extra.push(Lc);
    else extra.push(L - 0.5 * rTop);

    const children: Attachment[] = [];
    // Culm leaves: distichous (alternate sides), the flag leaf shortest.
    for (let k = 0; k < nodes; k++) {
      const s = leafS[k];
      const az = v.leafAz + k * Math.PI + this.culmRng.uniform() * 0.25;
      const frac = nodes > 1 ? k / (nodes - 1) : 0;
      const len = Math.max(0.01, g.bladeLength * g.culmLeafLength * (1 - 0.3 * frac) * (1 + 0.15 * this.culmRng.uniform()));
      const width = Math.max(0.0005, g.bladeWidth * g.culmLeafWidth);
      const angle = g.culmLeafAngle * DEG2RAD;
      const droop = (g.droop * 0.8 + g.droopV * 0.5 * this.culmRng.uniform()) * DEG2RAD;
      const twist = (g.twist * 0.7 + g.twistV * 0.5 * this.culmRng.uniform()) * DEG2RAD * (this.culmRng.next() < 0.5 ? -1 : 1);
      const wavePhase = this.culmRng.next();
      const limb0 = clamp(s / L, 0, 1);
      children.push({
        s,
        az,
        w: lw.w,
        h: lw.h,
        hh: nodeHH[k],
        j0: 0,
        row0: 0,
        row1: 0,
        make: (ex) => {
          const d0 = normalize(add(scale(ex.dir, Math.cos(angle)), scale(ex.normal, Math.sin(angle))));
          const r0 = normalize(cross(ex.dir, ex.normal));
          return this.makeBlade(ex, { az: 0, lean: 0, length: len, width, droop, twist, wavePhase, phase: v.phase, level: 2, limb0, pivot, segments: g.bladeSegments, dir0: d0, right0: r0 });
        },
      });
    }
    if (hasHead) this.planHead(children, Lc, Lh, radius, N, v.phase, pivot);

    return {
      level: 1,
      line,
      sStart: Math.min(0.3 * L, Math.max(0.45 * exit.size, this.collarLen(rBase))),
      profile: (s, j, n) => {
        const r = radius(s);
        const th = (TAU * j) / n;
        return { x: r * Math.cos(th), y: r * Math.sin(th) };
      },
      radius,
      round: true,
      extra,
      spacing: Math.max(1e-4, Lc / Math.max(1, Math.round(g.culmSegments))),
      children,
      wind,
      pivot,
      r0: rBase,
    };
  }

  /**
   * Attach the inflorescence parts to the rachis (the continuation of the culm
   * beyond Lc). All head windows share one row height so that windows in
   * different columns line up on the same ring stations.
   */
  private planHead(children: Attachment[], Lc: number, Lh: number, radius: (s: number) => number, N: number, phase: number, pivot: V3): void {
    const g = this.g;
    const rng = this.headRng;
    const count = Math.max(0, Math.round(g.headBranches));
    if (count === 0) return;
    const colStep = TAU / N;
    const chordAt = (s: number): number => (TAU * radius(s)) / N;
    const angle = clamp(g.headBranchAngle, 3, 100) * DEG2RAD;
    const spikelet = Math.max(0, g.spikelet);
    const awn = Math.max(0, g.awn);
    const kind = g.head;
    const flutter = this.flutter;
    const headWind = (s: number, y: number, L: number): VertexWind => ({ height: clamp(y / this.plantH, 0, 1), limb: 1, phase, detail: clamp(0.5 + (0.5 * s) / L, 0, 1) * flutter });
    // Branchlets cannot be thicker than the window they leave through.
    const rbFor = (s: number): number => Math.max(0.00012, Math.min(g.headBranchRadius, 0.42 * chordAt(s)));
    const sRef = Lc + 0.1 * Lh;
    const hh = Math.max(0.25 * chordAt(sRef), 1.25 * rbFor(sRef));
    const rowH = 2 * hh;
    const s0 = Lc + 0.02 * Lh;
    const nRows = Math.max(1, Math.floor((0.96 * Lh) / rowH));
    const rowS = (row: number): number => s0 + (clamp(row, 0, nRows - 1) + 0.5) * rowH;
    const colAz = (c: number): number => (mod(c, N) + 0.5) * colStep;

    const push = (s: number, az: number, vals: HeadVals): void => {
      children.push({ s, az, w: 1, h: 1, hh, j0: 0, row0: 0, row1: 0, make: (exit) => this.makeBranchlet(exit, vals, pivot, headWind, 0) });
    };

    if (kind === 'spike') {
      // Two opposite rows of sessile, appressed spikelets.
      const c0 = rng.int(N);
      const used = Math.min(count, nRows);
      if (used < count) this.drop('head too short', count - used);
      for (let k = 0; k < used; k++) {
        const row = Math.round(((nRows - 1) * k) / Math.max(1, used - 1));
        const s = rowS(row);
        const rb = rbFor(s);
        const body = Math.max(spikelet, 1.3 * rb);
        const stalk = Math.max(2 * rb, 0.6 * body);
        // Appressed, but clear of the rachis: the body centre must sit at least one body radius out.
        const minAngle = Math.asin(clamp(body / (stalk + 1.6 * body), 0, 0.95));
        push(s, colAz(c0 + (k % 2) * (N / 2)), { length: stalk, droop: g.headBranchDroop * DEG2RAD, secondaries: 0, bodyR: body, awnL: awn, angle: Math.max(angle, minAngle), rb, phase });
      }
    } else if (kind === 'foxtail') {
      // Dense whorls of bristles, alternating column parity from row to row.
      const perWhorl = Math.max(1, Math.floor(N / 2));
      const whorls = Math.ceil(count / perWhorl);
      const used = Math.min(whorls, nRows);
      if (used < whorls) this.drop('head too short', (whorls - used) * perWhorl);
      let k = 0;
      for (let wI = 0; wI < used && k < count; wI++) {
        const row = Math.round(((nRows - 1) * wI) / Math.max(1, used - 1));
        const s = rowS(row);
        const rb = rbFor(s);
        const t = (s - Lc) / Lh;
        for (let m = 0; m < perWhorl && k < count; m++, k++) {
          const c = 2 * m + (row % 2);
          let length = Math.max(2 * rb, (g.headWidth * 0.5 - radius(s)) / Math.max(0.15, Math.sin(angle)));
          length *= (1 + 0.2 * rng.uniform()) * (1 - 0.35 * Math.pow(Math.max(0, t - 0.7) / 0.3, 2));
          push(s, colAz(c), { length, droop: (g.headBranchDroop + 10 * rng.uniform()) * DEG2RAD, secondaries: 0, bodyR: 0, awnL: 0, angle, rb, phase });
        }
      }
    } else {
      // Panicle / plume: branchlets spread over the lower part of the head, shorter towards the tip.
      const spread = clamp(g.headBranchSpread, 0.05, 1);
      for (let k = 0; k < count; k++) {
        const t = (k + 0.5) / count;
        const row = Math.round((spread * nRows - 1) * t);
        const s = rowS(row);
        const rb = rbFor(s);
        const az = colAz(Math.round((k * GOLDEN) / colStep));
        const length = Math.max(3 * rb, g.headBranchLength * Lh * (1 - 0.45 * t) * (1 + 0.2 * rng.uniform()));
        const droop = g.headBranchDroop * (0.8 + 0.4 * rng.next()) * DEG2RAD;
        push(s, az, { length, droop, secondaries: Math.max(0, Math.round(g.headSecondary)), bodyR: kind === 'panicle' ? spikelet : 0, awnL: kind === 'panicle' ? awn : 0, angle, rb, phase });
      }
    }
  }

  /** A branchlet, bristle or spikelet: a thin tube with an optional spikelet bulge and awn, and optional secondaries. */
  private makeBranchlet(exit: Exit, v: HeadVals, pivot: V3, headWind: (s: number, y: number, L: number) => VertexWind, depth: number): Organ {
    const g = this.g;
    const rng = this.headRng;
    const N = exit.N;
    const Lb = Math.max(0.002, v.length);
    const hasBody = v.bodyR > v.rb * 1.05;
    const Ls = hasBody ? 3.2 * v.bodyR : 0;
    const La = v.awnL > 0 ? v.awnL : 0;
    const L = Lb + Ls + La;
    const dir0 = normalize(add(scale(exit.dir, Math.cos(v.angle)), scale(exit.normal, Math.sin(v.angle))));
    const right0 = normalize(cross(exit.dir, exit.normal));
    const segs = Math.max(1, Math.round(g.branchletSegments));
    const steps = segs * 2 + (hasBody ? 4 : 0) + (La > 0 ? 3 : 0);
    const angleAt = (s: number): number => (s <= Lb ? v.droop * Math.pow(s / Lb, 1.3) : v.droop);
    const line = growLine(exit.pos, dir0, right0, L, steps, (t0, t1) => ({ gravity: angleAt(t1 * L) - angleAt(t0 * L) }));
    const rAwn = 0.45 * v.rb;
    const radius = (s: number): number => {
      if (s <= Lb) return v.rb * (1 - 0.25 * (s / Lb));
      if (s <= Lb + Ls) {
        const t = clamp((s - Lb) / Ls, 0, 1);
        const b0 = 0.75 * v.rb;
        const end = La > 0 ? rAwn : 0.4 * v.rb;
        return b0 + (end - b0) * t + (v.bodyR - b0) * Math.pow(Math.sin(Math.PI * t), 0.8);
      }
      const t = (s - Lb - Ls) / Math.max(1e-6, La);
      return rAwn * (1 - 0.5 * t);
    };
    const extra: number[] = [];
    if (hasBody) extra.push(Lb, Lb + 0.2 * Ls, Lb + 0.5 * Ls, Lb + 0.8 * Ls, Lb + Ls);
    if (La > 0) extra.push(Lb + Ls + 0.4 * La);
    if (!hasBody && La === 0) extra.push(L - 0.6 * v.rb);
    const children: Attachment[] = [];
    if (v.secondaries > 0 && depth < 1) {
      const n = v.secondaries;
      const chord = (TAU * radius(0.5 * Lb)) / N;
      const rb2 = Math.max(0.0001, Math.min(v.rb * 0.7, 0.42 * chord));
      const hh = Math.max(0.25 * chord, 1.25 * rb2);
      const rows = Math.max(1, Math.floor((0.6 * Lb) / (2 * hh)));
      for (let k = 0; k < n; k++) {
        const t = 0.3 + (0.6 * (k + 0.5)) / n;
        const row = Math.round((rows - 1) * ((k + 0.5) / n));
        const s = 0.3 * Lb + (row + 0.5) * 2 * hh;
        const az = ((k % 2) * (N / 2) + 0.5) * (TAU / N);
        const vals: HeadVals = {
          length: Math.max(3 * rb2, Lb * (0.5 - 0.25 * t) * (1 + 0.2 * rng.uniform())),
          droop: v.droop * 0.8,
          secondaries: 0,
          bodyR: v.bodyR * 0.85,
          awnL: v.awnL * 0.8,
          angle: (30 + 15 * rng.uniform()) * DEG2RAD,
          rb: rb2,
          phase: v.phase,
        };
        children.push({ s, az, w: 1, h: 1, hh, j0: 0, row0: 0, row1: 0, make: (ex) => this.makeBranchlet(ex, vals, pivot, headWind, depth + 1) });
      }
    }
    return {
      level: 3,
      line,
      sStart: Math.min(0.3 * L, Math.max(0.45 * exit.size, this.collarLen(v.rb))),
      profile: (s, j, n) => {
        const r = radius(s);
        const th = (TAU * j) / n;
        return { x: r * Math.cos(th), y: r * Math.sin(th) };
      },
      radius,
      round: true,
      extra,
      spacing: Math.max(1e-5, Lb / segs, La / 4),
      children,
      wind: (s, y) => headWind(s, y, L),
      pivot,
      r0: v.rb,
    };
  }

  /** Distance from the exit to the first full ring: a sheath-like collar proportional to the child's size. */
  private collarLen(r: number): number {
    return Math.max(2.2 * r, 0.0015);
  }

  // ---------------------------------------------------------------------------
  // Tubes
  // ---------------------------------------------------------------------------

  private meshTube(o: Organ, loop: number[], exit: Exit, depth: number): void {
    const mesh = this.mesh;
    const N = loop.length;
    const L = o.line.length;
    const colStep = TAU / N;
    this.stats.organs++;
    this.stats.perLevel[Math.min(3, o.level)]++;

    // Round organs: rotate the ring frame so ring vertex k faces loop vertex k.
    if (o.round) {
      const f0 = o.line.at(o.sStart);
      const ph = this.fitPhase(loop, f0);
      if (Math.abs(ph) > 1e-6) {
        const rs = o.line.rights;
        for (let i = 0; i < rs.length; i++) rs[i] = rotateAxis(rs[i], o.line.dirs[i], ph);
      }
    }

    // ---- Plan the windows of the children.
    const accepted: Attachment[] = [];
    const cands = o.children.slice().sort((a, b) => a.s - b.s);
    for (const c of cands) {
      const jBase = Math.round(c.az / colStep - c.w / 2);
      const sMin = o.sStart + 0.6 * c.hh;
      const sMax = L - 0.6 * c.hh;
      const epsSame = 0.6 * c.hh;
      let placed = false;
      for (const [dr, dj] of OFFSETS) {
        const s = c.s + dr * 2 * c.hh;
        const lo = s - c.hh;
        const hi = s + c.hh;
        if (lo < sMin || hi > sMax) continue;
        const j0 = mod(jBase + dj, N);
        let ok = true;
        for (const a of accepted) {
          const aLo = a.s - a.hh;
          const aHi = a.s + a.hh;
          if (circularOverlap(j0, c.w, a.j0, a.w, N)) {
            // Same columns: keep a clear band of tube between the windows.
            if (lo < aHi + epsSame && aLo < hi + epsSame) {
              ok = false;
              break;
            }
          } else if (circularOverlap(j0 - 1, c.w + 2, a.j0, a.w, N)) {
            // Neighbouring columns: the windows may touch corner to corner but not overlap.
            if (lo < aHi - 1e-9 && aLo < hi - 1e-9) {
              ok = false;
              break;
            }
          }
        }
        if (!ok) continue;
        c.s = s;
        c.j0 = j0;
        c.az = (j0 + c.w / 2) * colStep;
        accepted.push(c);
        placed = true;
        break;
      }
      if (!placed) this.drop(L - o.sStart < 2.4 * c.hh ? 'organ too short' : 'window conflict');
    }

    // ---- Ring stations.
    const st = this.stations(o, accepted);
    const K = st.length;
    const occupied = new Uint8Array(Math.max(1, K - 1) * N);
    const rows: Attachment[] = [];
    for (const c of accepted) {
      const row0 = nearestIndex(st, c.s - c.hh);
      const row1 = nearestIndex(st, c.s + c.hh);
      if (row0 < 1 || row1 > K - 1 || row1 - row0 !== c.h) {
        this.drop('no room on parent');
        continue;
      }
      let free = true;
      for (let i = row0; i < row1 && free; i++) for (let j = 0; j < c.w; j++) if (occupied[i * N + mod(c.j0 + j, N)]) free = false;
      if (!free) {
        this.drop('window conflict');
        continue;
      }
      for (let i = row0; i < row1; i++) for (let j = 0; j < c.w; j++) occupied[i * N + mod(c.j0 + j, N)] = 1;
      c.row0 = row0;
      c.row1 = row1;
      c.s = 0.5 * (st[row0] + st[row1]);
      rows.push(c);
    }

    // ---- Rings.
    const cellOcc = (i: number, j: number): boolean => i >= 0 && i < K - 1 && occupied[i * N + mod(j, N)] === 1;
    const interior = (i: number, j: number): boolean => cellOcc(i - 1, j - 1) && cellOcc(i - 1, j) && cellOcc(i, j - 1) && cellOcc(i, j);
    // Mitre: tilt the first ring towards the parent surface.
    const a0 = o.line.dirs[0];
    const theta = Math.acos(clamp(dot(a0, exit.normal), -1, 1));
    const alpha = theta * 0.5 * clamp(o.sStart / Math.max(1e-6, 2.2 * o.r0), 0, 1);
    const tilt = alpha > 1e-3 ? rotateTowards(a0, exit.normal, alpha) : undefined;
    const rings: Ring[] = [];
    for (let i = 0; i < K; i++) rings.push(this.buildRing(o, st[i], N, i === 0 ? tilt : undefined, (j) => interior(i, j)));

    // ---- Collar, tube, cap.
    this.collar(loop, rings[0], exit, o);
    for (let i = 0; i < K - 1; i++) {
      const a = rings[i].idx;
      const b = rings[i + 1].idx;
      const va = st[i] / L;
      const vb = st[i + 1] / L;
      for (let j = 0; j < N; j++) {
        if (occupied[i * N + j]) continue;
        const j1 = (j + 1) % N;
        mesh.addQuad(a[j], a[j1], b[j1], b[j], [j / N, va, (j + 1) / N, va, (j + 1) / N, vb, j / N, vb]);
      }
    }
    this.cap(rings[K - 1].idx);

    // ---- Children.
    for (const c of rows) {
      const childLoop = this.holeLoop(rings, c, N);
      const f = o.line.at(c.s);
      const radial = normalize(add(scale(f.right, Math.cos(c.az)), scale(f.up, Math.sin(c.az))));
      const pos = addScaled(f.pos, radial, o.radius(c.s));
      const childExit: Exit = { pos, normal: radial, dir: f.dir, size: Math.max(c.w * colStep * o.radius(c.s), 2 * c.hh), N: childLoop.length };
      const child = c.make(childExit);
      this.meshTube(child, childLoop, childExit, depth + 1);
    }
  }

  /**
   * Ring stations along an organ: the collar ring, the tip, the rows of every
   * window, the organ's own feature stations and regular fill in between.
   * Nothing is ever inserted inside a window span, so a window always gets
   * exactly the rows it was planned with.
   */
  private stations(o: Organ, windows: Attachment[]): number[] {
    const L = o.line.length;
    const sStart = o.sStart;
    if (L - sStart < 1e-6) return [sStart];
    type St = { s: number; pri: number };
    const spans: [number, number][] = windows.map((w) => [w.s - w.hh, w.s + w.hh]);
    const inside = (s: number): boolean => spans.some(([a, b]) => s > a + 1e-9 && s < b - 1e-9);
    const mand: St[] = [
      { s: sStart, pri: 3 },
      { s: L, pri: 3 },
    ];
    let minRow = Infinity;
    for (const w of windows) {
      mand.push({ s: w.s - w.hh, pri: 2 }, { s: w.s + w.hh, pri: 2 });
      for (let r = 1; r < w.h; r++) mand.push({ s: w.s - w.hh + (2 * w.hh * r) / w.h, pri: 2 });
      const cell = (2 * w.hh) / w.h;
      if (cell < minRow) minRow = cell;
    }
    for (const s of o.extra) if (s > sStart + 1e-6 && s < L - 1e-6 && !inside(s)) mand.push({ s, pri: 1 });
    const spacing = o.spacing;
    const eps = Math.min(spacing * 0.3, isFinite(minRow) ? minRow * 0.3 : Infinity);
    mand.sort((a, b) => a.s - b.s || b.pri - a.pri);
    const kept: St[] = [];
    for (const st of mand) {
      const last = kept[kept.length - 1];
      if (last && st.s - last.s < eps) {
        if (st.pri > last.pri) kept[kept.length - 1] = st;
        continue;
      }
      kept.push(st);
    }
    kept[kept.length - 1] = { s: L, pri: 3 };
    if (kept.length >= 2 && kept[kept.length - 1].s - kept[kept.length - 2].s < eps * 0.5) kept.splice(kept.length - 2, 1);
    const out: number[] = [];
    for (let i = 0; i < kept.length; i++) {
      out.push(kept[i].s);
      if (i === kept.length - 1) break;
      const gap = kept[i + 1].s - kept[i].s;
      if (inside(kept[i].s + 0.5 * gap)) continue;
      const n = Math.floor(gap / spacing);
      if (n >= 1) {
        const step = gap / (n + 1);
        for (let k = 1; k <= n; k++) out.push(kept[i].s + k * step);
      }
    }
    return out;
  }

  private buildRing(o: Organ, s: number, N: number, tiltNormal: V3 | undefined, skip: (j: number) => boolean): Ring {
    const f = o.line.at(s);
    const idx: number[] = new Array(N);
    const an = tiltNormal ? dot(f.dir, tiltNormal) : 1;
    for (let j = 0; j < N; j++) {
      if (skip(j)) {
        idx[j] = -1;
        continue;
      }
      const q = o.profile(s, j, N);
      const off = { x: q.x * f.right.x + q.y * f.up.x, y: q.x * f.right.y + q.y * f.up.y, z: q.x * f.right.z + q.y * f.up.z };
      if (tiltNormal && an > 0.3) {
        const lambda = -dot(off, tiltNormal) / an;
        off.x += f.dir.x * lambda;
        off.y += f.dir.y * lambda;
        off.z += f.dir.z * lambda;
      }
      const p = add(f.pos, off);
      idx[j] = this.mesh.addVertex(p.x, p.y, p.z, o.wind(s, p.y), o.pivot, o.level, 0);
    }
    return { idx, s, f };
  }

  /**
   * Fit a regular angular spacing to a closed loop as seen along the organ's
   * axis: the starting angle that best aligns ring vertex k with loop vertex k
   * (angles measured from the projected centroid).
   */
  private fitPhase(loop: number[], f: Frame): number {
    const M = loop.length;
    const xs: number[] = new Array(M);
    const ys: number[] = new Array(M);
    let cx = 0;
    let cy = 0;
    for (let k = 0; k < M; k++) {
      const v = sub(this.pos(loop[k]), f.pos);
      xs[k] = dot(v, f.right);
      ys[k] = dot(v, f.up);
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

  /** Bridge the window loop to the first ring through `collarRings` fillet loops. */
  private collar(loop: number[], first: Ring, exit: Exit, o: Organ): void {
    const mesh = this.mesh;
    const M = loop.length;
    const shift = this.bestShift(loop, first.idx);
    const lp: number[] = new Array(M);
    for (let k = 0; k < M; k++) lp[k] = loop[(k + shift) % M];
    const n = Math.max(0, Math.round(this.g.collarRings));
    const a0 = o.line.dirs[0];
    const radial = exit.normal;
    const cosT = Math.max(0.2, dot(a0, radial));
    const fillet = 0.7;
    let prev = lp;
    for (let q = 0; q < n; q++) {
      const k = (q + 1) / (n + 1);
      const mid: number[] = [];
      for (let i = 0; i < M; i++) {
        const a = this.pos(lp[i]);
        const b = this.pos(first.idx[i]);
        const chord = lerp(a, b, k);
        let mu = dot(sub(b, a), radial) / cosT;
        mu = clamp(mu, 0, 4 * o.r0);
        const c = addScaled(b, a0, -mu);
        const w0 = (1 - k) * (1 - k);
        const w1 = 2 * k * (1 - k);
        const w2 = k * k;
        const bez = { x: a.x * w0 + c.x * w1 + b.x * w2, y: a.y * w0 + c.y * w1 + b.y * w2, z: a.z * w0 + c.z * w1 + b.z * w2 };
        const p = lerp(chord, bez, fillet);
        mid.push(mesh.addVertex(p.x, p.y, p.z, o.wind(first.s * k, p.y), o.pivot, o.level, 1));
      }
      this.bridge(prev, mid);
      prev = mid;
    }
    this.bridge(prev, first.idx);
    for (const v of loop) mesh.junction[v] = 1;
    for (const v of first.idx) mesh.junction[v] = 1;
    this.stats.junctions++;
  }

  /** Cyclic shift of the loop that pairs each loop vertex with the nearest ring vertex (least twisted collar). */
  private bestShift(loop: number[], ring: number[]): number {
    const M = loop.length;
    const p = this.mesh.positions;
    let best = 0;
    let bestCost = Infinity;
    for (let sh = 0; sh < M; sh++) {
      let cost = 0;
      for (let k = 0; k < M; k++) {
        const a = loop[(k + sh) % M] * 3;
        const b = ring[k] * 3;
        const dx = p[a] - p[b];
        const dy = p[a + 1] - p[b + 1];
        const dz = p[a + 2] - p[b + 2];
        cost += dx * dx + dy * dy + dz * dz;
      }
      if (cost < bestCost) {
        bestCost = cost;
        best = sh;
      }
    }
    return best;
  }

  private bridge(a: number[], b: number[]): void {
    const M = a.length;
    for (let k = 0; k < M; k++) {
      const k1 = (k + 1) % M;
      this.mesh.addQuad(a[k], a[k1], b[k1], b[k], [k / M, -0.05, (k + 1) / M, -0.05, (k + 1) / M, 0, k / M, 0]);
    }
  }

  /** Ladder cap of a ring that runs counter-clockwise seen from outside. */
  private cap(ring: number[]): void {
    const M = ring.length;
    for (let i = 0; 2 * i <= M - 3; i++) {
      this.mesh.addQuad(ring[i], ring[i + 1], ring[M - 2 - i], ring[M - 1 - i], [0, 0, 1, 0, 1, 1, 0, 1]);
    }
  }

  /** Closed loop of vertex indices around a window, counter-clockwise seen from outside. */
  private holeLoop(rings: Ring[], h: Attachment, N: number): number[] {
    const loop: number[] = [];
    const { row0, row1, j0, w } = h;
    for (let j = 0; j <= w; j++) loop.push(rings[row0].idx[mod(j0 + j, N)]);
    for (let i = row0 + 1; i <= row1; i++) loop.push(rings[i].idx[mod(j0 + w, N)]);
    for (let j = w - 1; j >= 0; j--) loop.push(rings[row1].idx[mod(j0 + j, N)]);
    for (let i = row1 - 1; i >= row0 + 1; i--) loop.push(rings[i].idx[mod(j0, N)]);
    for (const v of loop) if (v < 0) throw new Error('holeLoop: window touches a skipped vertex');
    return loop;
  }

  private pos(i: number): V3 {
    const p = this.mesh.positions;
    return { x: p[i * 3], y: p[i * 3 + 1], z: p[i * 3 + 2] };
  }

  private drop(reason: string, n = 1): void {
    this.stats.dropped += n;
    this.stats.dropReasons[reason] = (this.stats.dropReasons[reason] ?? 0) + n;
  }
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

function evenSides(n: number, lo: number, hi: number): number {
  return clamp(Math.round(n / 2) * 2, lo, hi);
}

/** Window (w × h cells) whose boundary loop has N vertices: 2(w + h) = N. */
function windowFor(N: number): { w: number; h: number } {
  switch (N) {
    case 4:
      return { w: 1, h: 1 };
    case 6:
      return { w: 1, h: 2 };
    case 8:
      return { w: 2, h: 2 };
    case 10:
      return { w: 2, h: 3 };
    default:
      return { w: 3, h: 3 };
  }
}

/**
 * Cross-section of a blade with N vertices, counter-clockwise seen from the
 * tip: right edge → adaxial (upper) face → left edge → abaxial face. `keel`
 * folds the blade into a V along the midrib, `rolled` curls it towards a
 * tube (edges up and inwards).
 */
export function bladePoint(N: number, j: number, width: number, thickness: number, keel: number, rolled: number): { x: number; y: number } {
  const m = N / 2;
  let u: number;
  let top: boolean;
  if (j <= m) {
    u = 1 - (2 * j) / m;
    top = true;
  } else {
    u = -1 + (2 * (j - m)) / m;
    top = false;
  }
  const hw = width * 0.5;
  const th = thickness * 0.5 * (1 - Math.pow(u, 4));
  // Flat, keeled profile.
  const yk = keel * width * Math.pow(Math.abs(u), 1.5);
  const fx = u * hw;
  const fy = yk + (top ? th : -th);
  if (rolled <= 1e-4) return { x: fx, y: fy };
  // Rolled: an arc of angle phi whose midrib stays at the origin; edges curl up and inwards.
  const phi = 5.8 * Math.max(0.15, rolled);
  const gap = TAU - phi;
  const Rr = width / phi;
  const ang = Math.PI / 2 + gap / 2 + ((u + 1) / 2) * phi;
  const rho = Rr + (top ? -th : th);
  const ax = rho * Math.cos(ang);
  const ay = Rr + rho * Math.sin(ang);
  return { x: fx + (ax - fx) * rolled, y: fy + (ay - fy) * rolled };
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
