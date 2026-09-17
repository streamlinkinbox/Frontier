/**
 * frame.ts — the timber that carries the roof.
 *
 * THE STACK, bottom to top — which is why nothing here floats:
 *
 *     柱 column
 *       └ 梁 beam, with 瓜柱 posts standing on it
 *           └ 檩/槫 purlin   ── the purlin TOPS are exactly the juzhe profile
 *               └ 椽 rafter  ── straight timbers laid purlin to purlin
 *                   └ 望板 sheathing
 *                       └ 瓦 tiles
 *
 * 舉折 positions PURLINS, not surfaces. So the plane the tiles stand on is:
 *
 *     tileSurface = purlinTopProfile + rafterDepth + sheathingThickness
 *
 * That constant is `surfaceLift` on the RoofField, and every piece of timber is
 * placed by reading back down from it. The rafters are emitted strictly from
 * purlin node to purlin node — never free-ended — and the purlins sit on the
 * columns, beams and posts, so the assembly is supported by construction.
 *
 * In a 庑殿/寄棟 hip roof the four faces' purlins at the same juzhe step are at
 * the same height, so they close into concentric rectangular RINGS, and the
 * hips run corner to corner between them. That is what real 梁架 do.
 *
 * 飛椽 flying rafters: a ROUND eave rafter with a SQUARE flying rafter on its
 * back, width 8/10 and thickness 7/10 of the rafter — Yingzao Fashi proportions
 * (MDPI Buildings 15(14):2582 §3.1). The kink at their joint is the origin of
 * the concave eave, and the eave course of tiles is aligned to these rafters,
 * which is why a real eave reads as discs sitting on rafter heads.
 */

import { Matrix4, Vector3 } from 'three';
import type { Face, RoofField } from './roofField';

export type FrameKind =
  | 'rafter' // 椽 — round section
  | 'flyRafter' // 飛椽 — square section
  | 'purlin' // 檩/槫 — log along the ridge direction
  | 'hipRafter' // 角梁 — heavier member along a hip
  | 'bargeboard' // 博风板 — the board covering a gable edge
  | 'column' // 柱
  | 'beam' // 梁
  | 'post' // 瓜柱 / 侏儒柱 — short post standing on a beam
  | 'dougong'; // 斗拱 — bracket set

export interface FrameSpec {
  /** Nominal centre-to-centre rafter spacing; snapped to the tile grid. */
  rafterPitch: number;
  rafterDiameter: number;
  purlinDiameter: number;
  hipRafterDiameter: number;
  columnDiameter: number;
  beamWidth: number;
  beamDepth: number;
  /** Include 斗拱 bracket sets under the eaves. */
  dougong: boolean;
  /** Column height, floor to the underside of the eave purlin. */
  columnHeight: number;
  /** Column spacing along the facades. */
  bayWidth: number;
  /** Emit 博风板 bargeboards on gable edges. */
  bargeboards: boolean;
}

export function frameSpec(over: Partial<FrameSpec> = {}): FrameSpec {
  return {
    rafterPitch: 0.36,
    rafterDiameter: 0.12,
    purlinDiameter: 0.2,
    hipRafterDiameter: 0.24,
    columnDiameter: 0.34,
    beamWidth: 0.26,
    beamDepth: 0.42,
    dougong: true,
    columnHeight: 3.1,
    bayWidth: 3.3,
    bargeboards: true,
    ...over,
  };
}

export interface FrameInstance {
  kind: FrameKind;
  m: Matrix4;
  /** Provenance for the verifier: which face a rafter belongs to, and where. */
  face?: string;
  u?: number;
  tA?: number;
  tB?: number;
}

export interface FrameResult {
  instances: FrameInstance[];
  /** 望板 sheathing surfaces in world space (positions + indices). */
  sheathing: { positions: number[]; indices: number[] }[];
  counts: Partial<Record<FrameKind, number>>;
}

/** Place a unit primitive (length along +Y) along from→to. */
export function segmentMatrix(
  from: Vector3,
  to: Vector3,
  refX: Vector3,
  sx: number,
  sz: number,
): Matrix4 | null {
  const y = new Vector3().subVectors(to, from);
  const len = y.length();
  if (len < 1e-6) return null;
  y.divideScalar(len);

  const x = refX.clone();
  x.addScaledVector(y, -x.dot(y));
  if (x.lengthSq() < 1e-10) {
    x.set(Math.abs(y.y) < 0.9 ? 0 : 1, Math.abs(y.y) < 0.9 ? 1 : 0, 0);
    x.addScaledVector(y, -x.dot(y));
  }
  x.normalize();
  const z = new Vector3().crossVectors(x, y).normalize();

  const m = new Matrix4();
  m.makeBasis(x, y, z);
  m.setPosition(new Vector3().addVectors(from, to).multiplyScalar(0.5));
  m.multiply(new Matrix4().makeScale(sx, len, sz));
  return m;
}

export function generateFrame(
  field: RoofField,
  spec: FrameSpec,
  tilePitch: number,
): FrameResult {
  const instances: FrameInstance[] = [];
  const sheathing: { positions: number[]; indices: number[] }[] = [];
  const lift = field.spec.surfaceLift;
  const rafterDepth = spec.rafterDiameter;
  const sheathT = Math.max(0.02, lift - rafterDepth);

  // rafters share the tile column grid so the 瓦當 eave discs land on rafters
  const mult = Math.max(1, Math.round(spec.rafterPitch / tilePitch));
  const pitch = mult * tilePitch;

  for (const face of field.faces) {
    const nodes = field.faceNodes(face);
    const tTip = nodes[0].t;
    const tEnd = Math.min(1, face.endClip);

    sheathing.push(sheathingSlab(field, face, sheathT));

    // ---- 椽 rafters, strictly purlin to purlin
    const uEave = field.uLimit(face, tTip);
    const nR = Math.max(2, Math.ceil((2 * uEave) / pitch));
    for (let k = 0; k < nR; k++) {
      const u = -uEave + pitch * (k + 0.5);
      for (let i = 0; i < nodes.length - 1; i++) {
        const a = nodes[i];
        const b = nodes[i + 1];
        if (b.t > tEnd + 1e-9) continue;
        if (Math.abs(u) > field.uLimit(face, (a.t + b.t) * 0.5) - rafterDepth * 0.35) continue;

        const isFly = i === 0; // the outboard bay is the 飛椽
        const pa = field.pos(face, a.t, u, new Vector3());
        const pb = field.pos(face, b.t, u, new Vector3());
        // offset the segment along its OWN normal so the rafter stays parallel
        // to the bay it carries: its top then meets the sheathing's underside
        // exactly, and its ends stay on the purlins.
        const nm = new Vector3().subVectors(pb, pa).cross(face.uVec).normalize();
        if (nm.y < 0) nm.negate();
        const off = -(sheathT + (isFly ? rafterDepth * 0.45 : rafterDepth * 0.5));
        pa.addScaledVector(nm, off);
        pb.addScaledVector(nm, off);

        // flying rafter: width 8/10, thickness 7/10 of the rafter diameter (YZS)
        const sx = isFly ? rafterDepth * 0.8 : rafterDepth;
        const sz = isFly ? rafterDepth * 0.7 : rafterDepth;
        const m = segmentMatrix(pa, pb, face.uVec, sx, sz);
        if (m)
          instances.push({
            kind: isFly ? 'flyRafter' : 'rafter',
            m,
            face: face.id,
            u,
            tA: a.t,
            tB: b.t,
          });
      }
    }

    // ---- 檩/槫 purlins: one log per purlin step, along u
    for (let i = 0; i <= field.juzhe.steps; i++) {
      const t = i / field.juzhe.steps;
      if (t > tEnd + 1e-9) continue;
      const uLim = field.uLimit(face, t);
      if (uLim < 1e-4) continue;
      // purlin TOP is the juzhe line: offset down from the tile plane by
      // the full surfaceLift (sheathing + rafters) plus its own radius
      // A purlin is a horizontal log whose TOP is the 舉折 line, so it is
      // offset VERTICALLY down by its own radius — not along the roof normal.
      const pA = field.pos(face, t, -uLim, new Vector3());
      const pB = field.pos(face, t, uLim, new Vector3());
      const pa = pA.set(pA.x, field.timberY(pA.x, pA.z) - spec.purlinDiameter * 0.5, pA.z);
      const pb = pB.set(pB.x, field.timberY(pB.x, pB.z) - spec.purlinDiameter * 0.5, pB.z);
      const m = segmentMatrix(pa, pb, face.vVec, spec.purlinDiameter, spec.purlinDiameter);
      if (m) instances.push({ kind: 'purlin', m });
    }
  }

  // ---- 角梁 hip rafters along each hip run
  const hipDrop = -(lift + spec.hipRafterDiameter * 0.5); // from the lifted surface
  for (const run of field.ridgeRuns()) {
    if (run.kind === 'hip') {
      for (let i = 0; i < run.points.length - 1; i++) {
        const a = offsetDown(field, run.points[i], hipDrop);
        const b = offsetDown(field, run.points[i + 1], hipDrop);
        const m = segmentMatrix(a, b, new Vector3(1, 0, 0), spec.hipRafterDiameter, spec.hipRafterDiameter);
        if (m) instances.push({ kind: 'hipRafter', m });
      }
    } else if (run.kind === 'bargeboard' && spec.bargeboards) {
      for (let i = 0; i < run.points.length - 1; i++) {
        const a = offsetDown(field, run.points[i], -rafterDepth * 0.55);
        const b = offsetDown(field, run.points[i + 1], -rafterDepth * 0.55);
        const m = segmentMatrix(
          a,
          b,
          new Vector3(1, 0, 0),
          Math.max(0.12, rafterDepth * 0.5),
          Math.max(0.06, rafterDepth * 0.3),
        );
        if (m) instances.push({ kind: 'bargeboard', m });
      }
    }
  }

  // ---- 柱 columns, on the eave-purlin rectangle
  const { ex, ez } = field;
  const colD = spec.columnDiameter;
  // the eave purlin's underside: the juzhe datum (h = 0) is the purlin TOP,
  // which sits surfaceLift below the tile bedding plane
  const topOfColumn =
    field.timberY(0, field.ez) - spec.purlinDiameter /* purlin underside */ ;
  const stations: Vector3[] = [];
  const nX = Math.max(1, Math.round(field.spec.width / spec.bayWidth));
  const nZ = Math.max(1, Math.round(field.spec.depth / spec.bayWidth));
  for (let i = 0; i <= nX; i++) {
    const x = -ex + (i / nX) * 2 * ex;
    stations.push(new Vector3(x, 0, -ez), new Vector3(x, 0, ez));
  }
  for (let j = 1; j < nZ; j++) {
    const z = -ez + (j / nZ) * 2 * ez;
    stations.push(new Vector3(-ex, 0, z), new Vector3(ex, 0, z));
  }
  const beamTop = topOfColumn - spec.beamDepth * 0.5;
  for (const s of stations) {
    const m = segmentMatrix(
      new Vector3(s.x, topOfColumn - spec.columnHeight, s.z),
      new Vector3(s.x, topOfColumn, s.z),
      new Vector3(1, 0, 0),
      colD,
      colD,
    );
    if (m) instances.push({ kind: 'column', m });

    if (spec.dougong) {
      const m2 = segmentMatrix(
        new Vector3(s.x, topOfColumn - 0.02, s.z),
        new Vector3(s.x, topOfColumn + 0.3, s.z),
        new Vector3(1, 0, 0),
        Math.max(0.5, colD * 1.7),
        Math.max(0.5, colD * 1.7),
      );
      if (m2) instances.push({ kind: 'dougong', m: m2 });
    }
  }

  // ---- 梁 beams running the short direction at each column line, + 瓜柱 posts
  for (const x of unique(stations.map((s) => round3(s.x)))) {
    if (Math.abs(x) > ex - 1e-6) continue; // the end columns are carried by the frame
    const m = segmentMatrix(
      new Vector3(x, beamTop, -ez),
      new Vector3(x, beamTop, ez),
      new Vector3(1, 0, 0),
      spec.beamWidth,
      spec.beamDepth,
    );
    if (m) instances.push({ kind: 'beam', m });
  }

  // posts from the beam up to every purlin that passes over it
  const slopeFaces = field.faces.filter((f) => f.kind === 'slope');
  for (const x of unique(stations.map((s) => round3(s.x)))) {
    if (Math.abs(x) > ex - 1e-6) continue;
    for (const face of slopeFaces) {
      for (let i = 0; i <= field.juzhe.steps; i++) {
        const t = i / field.juzhe.steps;
        if (Math.abs(x) > field.uLimit(face, t) - 0.02) continue;
        const z = face.eaveOrigin.z + t * face.depth * face.vVec.z;
        const topY = field.timberY(x, z) - rafterDepth - spec.purlinDiameter * 0.5;
        const y0 = beamTop + spec.beamDepth * 0.5;
        if (topY - y0 < 0.05) continue;
        const m = segmentMatrix(
          new Vector3(x, y0, z),
          new Vector3(x, topY, z),
          new Vector3(1, 0, 0),
          Math.max(0.16, colD * 0.5),
          Math.max(0.16, colD * 0.5),
        );
        if (m) instances.push({ kind: 'post', m });
      }
    }
  }
  const counts: Partial<Record<FrameKind, number>> = {};
  for (const it of instances) counts[it.kind] = (counts[it.kind] ?? 0) + 1;

  return { instances, sheathing, counts };
}

function offsetDown(field: RoofField, p: Vector3, amount: number): Vector3 {
  const n = field.normalAt(p.x, p.z, new Vector3());
  return p.clone().addScaledVector(n, amount);
}

function round3(v: number): number {
  return Math.round(v * 1000) / 1000;
}

function unique(xs: number[]): number[] {
  return Array.from(new Set(xs)).sort((a, b) => a - b);
}

/**
 * 望板 sheathing as a real slab: its TOP face is exactly the tile bedding plane
 * (so every tile touches it) and its underside rests exactly on the rafter tops
 * (so the rafters are not floating under a plane that hovers above them).
 */
function sheathingSlab(
  field: RoofField,
  face: Face,
  thickness: number,
): { positions: number[]; indices: number[] } {
  const nu = Math.max(6, Math.round(field.spec.width / 0.45));
  const nv = 18;
  const positions: number[] = [];
  const indices: number[] = [];
  const nodes = field.faceNodes(face);
  const tTip = nodes[0].t;
  const tEnd = Math.min(1, face.endClip);
  const ts = Math.max(0.015, thickness);
  const N = (nu + 1) * (nv + 1);

  const ptAt = (j: number, i: number): { p: Vector3; n: Vector3 } => {
    const t = tTip + (j / nv) * (tEnd - tTip);
    const uLim = field.uLimit(face, t);
    const u = -uLim + (i / nu) * 2 * uLim;
    const p = field.pos(face, t, u, new Vector3());
    const n = field.normalAt(p.x, p.z, new Vector3());
    return { p, n };
  };

  for (let layer = 0; layer < 2; layer++) {
    for (let j = 0; j <= nv; j++) {
      for (let i = 0; i <= nu; i++) {
        const { p, n } = ptAt(j, i);
        const off = layer === 0 ? 0 : -ts;
        const q = p.clone().addScaledVector(n, off);
        positions.push(q.x, q.y, q.z);
      }
    }
  }

  const quad = (a: number, b: number, c: number, d: number, flip: boolean) => {
    if (!flip) indices.push(a, c, b, b, c, d);
    else indices.push(a, b, c, b, d, c);
  };
  for (let j = 0; j < nv; j++) {
    for (let i = 0; i < nu; i++) {
      const a = j * (nu + 1) + i;
      const b = a + 1;
      const c = a + nu + 1;
      const d = c + 1;
      quad(a, b, c, d, false); // top
      quad(N + a, N + b, N + c, N + d, true); // underside
    }
  }

  // close the rim so the slab has no open edge
  const rim = (
    order: { j: number; i: number }[],
    outDir: (j: number, i: number) => Vector3,
  ) => {
    for (let k = 0; k < order.length - 1; k++) {
      const { j: j0, i: i0 } = order[k];
      const { j: j1, i: i1 } = order[k + 1];
      const t0 = j0 * (nu + 1) + i0;
      const t1 = j1 * (nu + 1) + i1;
      const b0 = N + t0;
      const b1 = N + t1;
      const P = (idx: number) =>
        new Vector3(positions[idx * 3], positions[idx * 3 + 1], positions[idx * 3 + 2]);
      const nrm = new Vector3().subVectors(P(t1), P(t0)).cross(new Vector3().subVectors(P(b0), P(t0)));
      if (nrm.dot(outDir(j0, i0)) >= 0) indices.push(t0, t1, b1, t0, b1, b0);
      else indices.push(t0, b1, t1, t0, b0, b1);
    }
  };
  const uv = face.uVec;
  const vv = face.vVec;
  rim(
    Array.from({ length: nu + 1 }, (_, i) => ({ j: 0, i })),
    () => vv.clone().negate(),
  );
  rim(
    Array.from({ length: nu + 1 }, (_, i) => ({ j: nv, i })),
    () => vv.clone(),
  );
  rim(
    Array.from({ length: nv + 1 }, (_, j) => ({ j, i: 0 })),
    () => uv.clone().negate(),
  );
  rim(
    Array.from({ length: nv + 1 }, (_, j) => ({ j, i: nu })),
    () => uv.clone(),
  );

  return { positions, indices };
}
