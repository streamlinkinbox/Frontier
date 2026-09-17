/**
 * roofField.ts — the unified roof surface.
 *
 * THE KEY IDEA
 * ------------
 * A roof is never built face-by-face. Instead ONE height field is defined over
 * the plan:
 *
 *      t(face) = (perpendicular distance from that face's eave purlin) / (its depth)
 *      S(x,z)  = min over all faces of t            <- the governing face
 *      y(x,z)  = P( S * B/2 )                       <- P is the juzhe profile
 *
 * Consequences, proven rather than tuned:
 *
 *  1. Every face's cross-section IS the juzhe polyline from juzhe.ts.
 *  2. Where two faces meet, both compute the same S, so they compute the same
 *     y. The hip ridge (垂脊/角梁) is therefore the exact intersection of the two
 *     faces — it is FOUND, never guessed. A hip can never float and can never
 *     cross a face wrongly.
 *  3. The hip rafter's plan run is sqrt(2)× a face's run, so its slope is
 *     shallower by the same factor. This is the real, counter-intuitive
 *     carpenter's result (MDPI Buildings 15(14):2582 §3.2) and it is why
 *     corners need 生頭木 pad blocks.
 *  4. The face region in (u,t) space is a trapezoid with straight edges:
 *          |u| <= base - inset * min(t, uClip)
 *     In the eave region (t < 0) this same expression widens outward and turns
 *     out to be *exactly* the 45° mitre of a hip corner. Both faces share that
 *     boundary, so the corner is watertight with no gap and no overlap.
 *
 *   t = 0  : eave purlin (檐檩) centre
 *   t = 1  : ridge (脊槫) / apex
 *   t < 0  : the eave overhang, out to the flying-rafter tip at t = -eaveProjection/depth
 */

import { Vector3 } from 'three';
import {
  evalProfile,
  solveJuzhe,
  type JuzheResult,
  type JuzheSpec,
  type ProfileNode,
} from '../core/juzhe';

export type RoofType = 'gable' | 'hip' | 'hipGable' | 'pyramid';

export interface RoofFieldSpec {
  type: RoofType;
  /** Width of the column / eave-purlin rectangle, along X. */
  width: number;
  /** Depth of the column / eave-purlin rectangle, along Z — B = this. */
  depth: number;

  riseRatio: number;
  steps: number;
  eaveProjection: number;
  flyOverEave: number;
  flyPitch: number;

  /**
   * Hip plan inset, as a multiple of depth/2. 1.0 ⇒ a 45° hip in plan, which
   * is the classical standard. Keep it at 1.0 unless you have a reason.
   */
  hipInsetRatio: number;

  /** 歇山/入母屋 only: the break line's normalised height (0..1) up the skirt. */
  breakT: number;

  /** 悬山 only: how far the gable oversails the wall (metres). */
  gableOverhang: number;

  /**
   * Height of the tile bedding plane above the purlin-top (juzhe) profile:
   * rafter depth + sheathing thickness. The juzhe profile positions PURLINS,
   * so the tiles stand on the rafter+sheathing stack above it. Everything —
   * tiles, ridge caps, ornaments — is placed through yAt(), so this one number
   * keeps the whole roof sitting on its timber.
   */
  surfaceLift: number;

  /** 角翹/翼角 — how far the eave tip at a corner is thrown up (metres, ~0.1·H0). */
  cornerUpturn: number;
  /** How far in from a corner that upturn reaches (metres). */
  cornerRadius: number;
}

export interface Face {
  id: string;
  kind: 'slope' | 'end';
  sign: 1 | -1;
  /** Half-extent of the eave line: ex for slope faces, ez for end faces. */
  base: number;
  /** Rate at which the face narrows: |u| <= base - uRate·min(t, uClip). */
  uRate: number;
  /**
   * The t past which the face stops narrowing. For a 歇山's slopes this is the
   * 博脊: above it the roof is the gable proper, which keeps its full width
   * because the 山牆 stands vertically there. For an end face this equals
   * `endClip` — the hip skirt simply ends at the break.
   */
  uClip: number;
  /** Plan depth of this face, eave purlin → ridge. */
  depth: number;
  /** Plan direction along the eave (unit, horizontal). */
  uVec: Vector3;
  /** Plan direction from the eave inboard toward the ridge (unit, horizontal). */
  vVec: Vector3;
  /** The eave purlin centre, in plan. */
  eaveOrigin: Vector3;
  /**
   * Normalised t past which the face does not exist at all
   * (the end faces of an 入母屋 stop at the 博脊; the slopes run to the ridge).
   */
  endClip: number;
}

export interface Governing {
  /** Normalised position of the governing face, 0 = eave purlin, 1 = ridge. */
  s: number;
  face: Face;
}

export interface FaceNode {
  t: number;
  y: number;
  /** Cumulative surface arc length from the eave tip at this node. */
  arc: number;
}

export interface RidgeRun {
  kind: 'mainRidge' | 'hip' | 'break' | 'bargeboard';
  /** Plan-projected polyline of the run's centre line. */
  points: Vector3[];
}

const EPS = 1e-9;

export class RoofField {
  readonly spec: RoofFieldSpec;
  readonly juzhe: JuzheResult;
  /**
   * The juzhe profile offset along its own normal by `surfaceLift` — the true
   * tile bedding plane, i.e. the parallel curve of the purlin-top line. A
   * vertical shift would not be: on a 1:1.5 slope a 0.15 m vertical shift is a
   * 0.22 m stagger over the roof, which is exactly the kind of "everything
   * floats by a hand-span" error this project has to avoid. Mitered at the
   * purlins and at the 飛椽 kink, so the sheathing wraps that kink correctly.
   */
  readonly lifted: JuzheResult;

  /** Half width of the eave-purlin rectangle in X. */
  readonly ex: number;
  /** Half depth of the eave-purlin rectangle in Z; equals halfSpan. */
  readonly ez: number;
  /** Hip plan inset. */
  readonly inset: number;
  /** Length of the ridge (main 正脊). */
  readonly ridgeLen: number;
  /** For 入母屋: plan half-width of the gable core. */
  readonly gableCoreX: number;
  /** Normalised t where the 入母屋 skirt is cut. */
  readonly endClip: number;

  readonly faces: Face[];

  private readonly _cache = new Map<string, FaceNode[]>();

  constructor(spec: RoofFieldSpec) {
    this.spec = spec;
    this.ex = spec.width / 2;
    this.ez = spec.depth / 2;

    const juzheSpec: JuzheSpec = {
      halfSpan: this.ez,
      riseRatio: spec.riseRatio,
      steps: spec.steps,
      eaveProjection: spec.eaveProjection,
      flyOverEave: spec.flyOverEave,
      flyPitch: spec.flyPitch,
    };
    this.juzhe = solveJuzhe(juzheSpec);

    const isPyramid = spec.type === 'pyramid';
    /**
     * 45° hips are the classical rule, and they are also what makes this field
     * continuous: at a hip the two faces must agree, and they agree exactly when
     * both measure t against the same run — i.e. when inset === ez. A non-45°
     * hip would put the two faces in different planes along the shared seam, so
     * the hip seam would tear. Keep it at ez.
     *
     * A 攢尖 is then simply the case where the ridge has shrunk to nothing:
     * ridgeLen = width - 2·ez, which is zero on a square plan.
     */
    this.inset = this.ez;
    void isPyramid;
    void spec.hipInsetRatio;

    const hasEndFaces = spec.type === 'hip' || spec.type === 'hipGable' || isPyramid;
    this.endClip = spec.type === 'hipGable' ? clamp(spec.breakT, 0.1, 0.9) : 1;

    this.ridgeLen = hasEndFaces
      ? Math.max(0, spec.width - 2 * this.inset * this.endClip)
      : spec.width + 2 * spec.gableOverhang;
    this.gableCoreX = hasEndFaces ? Math.max(0, this.ex - this.inset * this.endClip) : this.ex;

    this.lifted = liftProfile(this.juzhe, spec.surfaceLift);

    const faces: Face[] = [];
    const mkFace = (
      kind: 'slope' | 'end',
      sign: 1 | -1,
      base: number,
      uRate: number,
      depth: number,
      endClip: number,
      uClip: number,
    ): Face => {
      if (kind === 'slope') {
        return {
          id: `slope${sign < 0 ? 'S' : 'N'}`,
          kind,
          sign,
          base,
          uRate,
          uClip,
          depth,
          uVec: new Vector3(1, 0, 0),
          vVec: new Vector3(0, 0, -sign),
          eaveOrigin: new Vector3(0, 0, sign * this.ez),
          endClip,
        };
      }
      // an end face's eave purlin line runs along z at x = ±ex — note ex, not ez
      return {
        id: `end${sign < 0 ? 'W' : 'E'}`,
        kind,
        sign,
        base,
        uRate,
        depth,
        uVec: new Vector3(0, 0, 1),
        vVec: new Vector3(-sign, 0, 0),
        eaveOrigin: new Vector3(sign * this.ex, 0, 0),
        endClip,
        uClip,
      };
    };

    if (hasEndFaces) {
      // u = z: the face spans |z| <= ez·(1 - t), and its own depth inboard is inset
      faces.push(mkFace('end', -1, this.ez, this.ez, this.inset, this.endClip, this.endClip));
      faces.push(mkFace('end', +1, this.ez, this.ez, this.inset, this.endClip, this.endClip));
    }
    // u = x: bounded by the hip, |x| <= ex - inset·min(t, uClip). The slopes run
    // all the way to the ridge — only their WIDTH stops growing at the 博脊.
    faces.push(mkFace('slope', -1, this.ex, this.inset, this.ez, 1, this.endClip));
    faces.push(mkFace('slope', +1, this.ex, this.inset, this.ez, 1, this.endClip));
    this.faces = faces;
  }

  /** Which face governs a plan point, and at what normalised t. */
  governing(x: number, z: number): Governing {
    let best: Governing | null = null;
    for (const f of this.faces) {
      const t = this.tOnFace(f, x, z);
      // An 入母屋/歇山 end face only exists outboard of the gable core. Past the
      // break line the surface is bounded by the vertical gable wall, not by the
      // skirt — without this the ridge would sag toward the break instead of
      // running level into the 破風.
      if (f.kind === 'end' && t >= f.endClip - 1e-9) continue;
      if (best === null || t < best.s) best = { s: t, face: f };
    }
    if (!best) throw new Error('RoofField has no faces');
    return best;
  }

  /**
   * Normalised t of a plan point measured on a specific face (unclipped).
   * Measured from the face's OWN eave purlin line: z = ±ez for a slope face,
   * x = ±ex for an end face. (An end face's u-limit base is ez, its eave
   * position is ex — conflating the two puts the end faces in the wrong plane,
   * which drags the hips and ridge with them.)
   */
  tOnFace(f: Face, x: number, z: number): number {
    if (f.kind === 'slope') {
      return (Math.abs(f.eaveOrigin.z) - Math.abs(z)) / f.depth;
    }
    return (Math.abs(f.eaveOrigin.x) - Math.abs(x)) / f.depth;
  }

  /** Height of the TILE BEDDING PLANE at normalised t, without corner upturn. */
  profileY(s: number, depth: number): number {
    if (s >= 0) return evalProfile(this.lifted, s * this.ez);
    // eave overhang: measured in real plan metres so the overhang is the same
    // length all round the building whatever the face's depth is.
    return evalProfile(this.lifted, s * depth);
  }

  /** Height of the 舉折 purlin-top line (the structural datum) at t. */
  timberProfileY(s: number, depth: number): number {
    if (s >= 0) return evalProfile(this.juzhe, s * this.ez);
    return evalProfile(this.juzhe, s * depth);
  }

  /**
   * 角翹 jiaoqiao / 翼角 — the corner upturn.
   * A plain height offset, maximal at each corner, fading up the slope to zero
   * at the ridge and fading along the eave away from the corner. Because it is
   * a function of the plan position only, both faces meeting at a hip add the
   * identical amount there, so the corner lifts without tearing the seam.
   */
  uplift(x: number, z: number, s: number): number {
    const spec = this.spec;
    if (spec.cornerUpturn <= 0) return 0;
    const R = Math.max(0.001, spec.cornerRadius);
    let w = 0;
    for (const sx of [-1, 1]) {
      for (const sz of [-1, 1]) {
        const d = Math.hypot(x - sx * this.ex, z - sz * this.ez);
        const k = 1 - clamp(d / R, 0, 1);
        w = Math.max(w, k * k);
      }
    }
    if (w <= 0) return 0;
    // Height fade: ZERO at the eave purlin and growing out into the eave
    // overhang. This matters structurally, not just visually — the purlin log
    // and the column tops therefore stay on the flat 舉折 line, exactly as a
    // real 檐檩 does, and the corner sweep is carried by the 翼角 rafters and
    // the 生頭木 pad blocks above it. It also keeps every hip and ridge seam
    // lifted by the identical amount on both faces, because the amount depends
    // only on the plan position and the governing t.
    const tipT = Math.max(1e-6, this.juzhe.eaveTipRun / this.ez);
    const out = Math.min(1, Math.max(0, -s) / (0.5 * tipT));
    const hf = Math.pow(out, 1.4);
    return spec.cornerUpturn * w * hf;
  }

  /** World Y of the tile bedding plane at a plan point. */
  yAt(x: number, z: number): number {
    const g = this.governing(x, z);
    return this.profileY(g.s, g.face.depth) + this.uplift(x, z, g.s);
  }

  /**
   * World Y of the purlin-top (舉折) profile — the STRUCTURAL datum, i.e. the
   * line the 檩 sit on. Distinct from yAt(), which is surfaceLift higher: that
   * offset is exactly the rafter + sheathing stack the tiles stand on, so
   * reading timbers from here is what makes the rafter soffit land on the
   * purlin top instead of hovering a hand-span above it.
   */
  timberY(x: number, z: number): number {
    const g = this.governing(x, z);
    return this.timberProfileY(g.s, g.face.depth) + this.uplift(x, z, g.s);
  }

  /**
   * World position on a face at (t, u), using THAT face's own parameter.
   * At a hip two faces share the same t, so this still lands both on the same
   * point — but it keeps a point placed on the gable skirt on the skirt rather
   * than snapping it up to whichever face wins the global min.
   */
  pos(face: Face, t: number, u: number, out = new Vector3()): Vector3 {
    const x = face.eaveOrigin.x + u * face.uVec.x + t * face.depth * face.vVec.x;
    const z = face.eaveOrigin.z + u * face.uVec.z + t * face.depth * face.vVec.z;
    // The face's OWN t, not the governing one. They agree everywhere except at
    // a 歇山's gable wall, where the height field is deliberately a cliff: an
    // object placed ON the skirt belongs on the skirt, not on the ridge above it.
    const y = this.profileY(t, face.depth) + this.uplift(x, z, t);
    return out.set(x, y, z);
  }

  /** Half-extent of a face in u at parameter t — the face's trapezoid edge. */
  uLimit(face: Face, t: number): number {
    // 懸山: the roof runs straight on past the wall as a 挑山 overhang.
    if (face.kind === 'slope' && this.spec.type === 'gable')
      return face.base + this.spec.gableOverhang;
    return Math.max(0, face.base - face.uRate * Math.min(t, face.uClip));
  }

  /** Unit surface normal, computed from the field itself so it is always consistent. */
  normalAt(x: number, z: number, out = new Vector3()): Vector3 {
    const d = 2e-3;
    let yx = (this.yAt(x + d, z) - this.yAt(x - d, z)) / (2 * d);
    let yz = (this.yAt(x, z + d) - this.yAt(x, z - d)) / (2 * d);
    // A 歇山's gable wall is a cliff in the height field and a hip is a crease;
    // neither has a usable finite-difference normal, so clamp, which turns the
    // degenerate case into "the steepest believable roof" instead of "a wall".
    const MAX = 4;
    const m = Math.hypot(yx, yz);
    if (m > MAX) {
      const k = MAX / m;
      yx *= k;
      yz *= k;
    }
    return out.set(-yx, 1, -yz).normalize();
  }

  // ---------------------------------------------------------------- arc length

  /**
   * The face's profile resampled into its own t-space, with the surface arc
   * length from the eave tip accumulated. Used to space tile courses by true
   * surface distance (壓六露四 is measured along the slope, not in plan).
   */
  faceNodes(face: Face): FaceNode[] {
    const key = face.id;
    const hit = this._cache.get(key);
    if (hit) return hit;

    const dep = face.depth;
    const nodes: FaceNode[] = [];
    for (const p of this.lifted.profile) {
      // The profile is authored in q (plan metres out from the eave purlin).
      // main body: t = q/ez ; eave overhang: t = q/dep (so the overhang is the
      // same real length on every face).
      const t = p.q >= 0 ? p.q / this.ez : p.q / dep;
      nodes.push({ t, y: p.h, arc: 0 });
    }
    nodes.sort((a, b) => a.t - b.t);
    // dedupe near-identical t (happens when dep == ez, the normal case)
    const out: FaceNode[] = [];
    for (const n of nodes) {
      const last = out[out.length - 1];
      if (last && Math.abs(last.t - n.t) < 1e-7) {
        last.y = Math.max(last.y, n.y);
        continue;
      }
      out.push(n);
    }
    let arc = 0;
    for (let i = 1; i < out.length; i++) {
      const dt = out[i].t - out[i - 1].t;
      const dy = out[i].y - out[i - 1].y;
      // plan run per unit t is dep, rise is dy
      arc += Math.hypot(dt * dep, dy);
      out[i].arc = arc;
    }
    this._cache.set(key, out);
    return out;
  }

  /** Total arc length of a face from tip to its endClip. */
  faceArcEnd(face: Face): number {
    const nodes = this.faceNodes(face);
    if (face.endClip >= 1) return nodes[nodes.length - 1].arc;
    return this.arcAtT(face, face.endClip);
  }

  arcAtT(face: Face, t: number): number {
    const nodes = this.faceNodes(face);
    if (t <= nodes[0].t) return 0;
    for (let i = 0; i < nodes.length - 1; i++) {
      const a = nodes[i];
      const b = nodes[i + 1];
      if (t >= a.t && t <= b.t) {
        const k = (t - a.t) / (b.t - a.t);
        return a.arc + k * (b.arc - a.arc);
      }
    }
    return nodes[nodes.length - 1].arc;
  }

  tAtArc(face: Face, s: number, maxT: number): number {
    const nodes = this.faceNodes(face);
    if (s <= 0) return nodes[0].t;
    for (let i = 0; i < nodes.length - 1; i++) {
      const a = nodes[i];
      const b = nodes[i + 1];
      if (s >= a.arc && s <= b.arc) {
        const k = b.arc === a.arc ? 0 : (s - a.arc) / (b.arc - a.arc);
        return Math.min(maxT, a.t + k * (b.t - a.t));
      }
    }
    return maxT;
  }

  /** Local slope dy/d(plan) at normalised t — used to lay tiles on the curve. */
  slopeAtT(face: Face, t: number): number {
    const nodes = this.faceNodes(face);
    for (let i = 0; i < nodes.length - 1; i++) {
      const a = nodes[i];
      const b = nodes[i + 1];
      if (t >= a.t && t <= b.t) {
        const run = (b.t - a.t) * face.depth;
        return run < EPS ? 0 : (b.y - a.y) / run;
      }
    }
    if (t <= nodes[0].t) {
      const b = nodes[1];
      const run = (b.t - nodes[0].t) * face.depth;
      return run < EPS ? 0 : (b.y - nodes[0].y) / run;
    }
    const n = nodes.length;
    const run = (nodes[n - 1].t - nodes[n - 2].t) * face.depth;
    return run < EPS ? 0 : (nodes[n - 1].y - nodes[n - 2].y) / run;
  }

  /**
   * The 3-D ridge runs. Because hips are the intersection of the faces, we only
   * have to emit the locus where the two faces' t values are equal — the height
   * is then read straight off the field, so these polylines lie exactly on the
   * roof by construction.
   */
  ridgeRuns(): RidgeRun[] {
    const runs: RidgeRun[] = [];
    const { type } = this.spec;

    // 正脊 main ridge along z = 0
    if (type !== 'pyramid' || this.ridgeLen > 1e-6) {
      const pts: Vector3[] = [];
      const n = 24;
      for (let i = 0; i <= n; i++) {
        const x = -this.ridgeLen / 2 + (i / n) * this.ridgeLen;
        pts.push(new Vector3(x, this.yAt(x, 0), 0));
      }
      runs.push({ kind: 'mainRidge', points: pts });
    }

    const hasEnd = type === 'hip' || type === 'hipGable' || type === 'pyramid';
    if (hasEnd) {
      const n = 28;
      for (const sx of [-1, 1]) {
        for (const sz of [-1, 1]) {
          // from the corner (t = -eave tip) up the hip to t = endClip
          const tStart = -this.juzhe.eaveTipRun / this.ez;
          const pts: Vector3[] = [];
          for (let i = 0; i <= n; i++) {
            const t = tStart + (i / n) * (this.endClip - tStart);
            const tc = Math.max(t, -0.999999);
            const x = sx * (this.ex - this.inset * Math.min(tc, this.endClip));
            const z = sz * this.ez * (1 - tc);
            const g = this.governing(x, z);
            const y = this.profileY(g.s, g.face.depth) + this.uplift(x, z, g.s);
            pts.push(new Vector3(x, y, z));
          }
          runs.push({ kind: 'hip', points: pts });

          if (type === 'hipGable') {
            // 破風 bargeboard: the straight gable edge above the break
            const xb = sx * this.gableCoreX;
            const zb = sz * this.ez * (1 - this.endClip);
            const bp: Vector3[] = [];
            for (let i = 0; i <= 12; i++) {
              const k = i / 12;
              const z = zb * (1 - k);
              // the 破風 edge IS the slope surface at the 山牆 plane
              bp.push(new Vector3(xb, this.yAt(xb + sx * 1e-4, z), z));
            }
            runs.push({ kind: 'bargeboard', points: bp });
          }
        }
      }
      if (type === 'hipGable') {
        // 博脊 the horizontal break line on each end. Nudged a few centimetres
        // out onto the skirt: the wall itself is a vertical cliff in the height
        // field, where a normal is meaningless, and the 博脊 is bedded on the
        // tiles anyway — its inner edge just runs into the wall.
        const z1 = this.ez * (1 - this.endClip);
        for (const sx of [-1, 1]) {
          const xb = sx * (this.gableCoreX + 0.03);
          const bp: Vector3[] = [];
          for (let i = 0; i <= 12; i++) {
            const z = -z1 + (i / 12) * 2 * z1;
            bp.push(new Vector3(xb, this.yAt(xb, z), z));
          }
          runs.push({ kind: 'break', points: bp });
        }
      }
    } else {
      // 悬山/硬山 gable: the roof edge is the gable line, running down the slope
      for (const sx of [-1, 1]) {
        const x = sx * (this.ex + this.spec.gableOverhang);
        const pts: Vector3[] = [];
        const n = 24;
        const tStart = -this.juzhe.eaveTipRun / this.ez;
        for (let i = 0; i <= n; i++) {
          const t = tStart + (i / n) * (1 - tStart);
          const z = -this.ez * (1 - t);
          pts.push(new Vector3(x, this.yAt(x, z), z));
        }
        runs.push({ kind: 'bargeboard', points: pts });
      }
    }
    return runs;
  }
}

function clamp(v: number, a: number, b: number): number {
  return v < a ? a : v > b ? b : v;
}

/**
 * Offset a juzhe profile along its own normal by `lift`, mitering at the
 * vertices — the true parallel curve. Mitering is what makes the sheathing and
 * tiles wrap the 飛椽 kink at the eave, which is the detail that gives a real
 * eave its break.
 */
export function liftProfile(res: JuzheResult, lift: number): JuzheResult {
  const p = res.profile;
  if (lift === 0) return { ...res, profile: p.map((n) => ({ ...n })) };

  // unit upward normal of every segment, in (q, h)
  const segN: [number, number][] = [];
  for (let i = 0; i < p.length - 1; i++) {
    const dq = p[i + 1].q - p[i].q;
    const dh = p[i + 1].h - p[i].h;
    const L = Math.hypot(dq, dh) || 1;
    segN.push([-dh / L, dq / L]);
  }

  const profile: ProfileNode[] = [];
  for (let i = 0; i < p.length; i++) {
    // At the ridge there is no segment beyond, so treat it as a horizontal cap
    // (the two roof faces meeting) — i.e. lift straight up there.
    const nA = i > 0 ? segN[i - 1] : segN[0];
    const nB = i < p.length - 1 ? segN[i] : ([0, 1] as [number, number]);
    const mx = nA[0] + nB[0];
    const my = nA[1] + nB[1];
    const ml = Math.hypot(mx, my);
    const m: [number, number] = ml < 1e-6 ? [0, 1] : [mx / ml, my / ml];
    const cosHalf = m[0] * nA[0] + m[1] * nA[1];
    const k = Math.abs(cosHalf) > 0.08 ? lift / cosHalf : lift;
    profile.push({ q: p[i].q + k * m[0], h: p[i].h + k * m[1], purlin: p[i].purlin });
  }
  return { ...res, profile };
}
