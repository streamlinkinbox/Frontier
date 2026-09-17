/**
 * 脊 · ridges — everything that sits ON the tiled surface.
 *
 * The tile field is laid continuously across the whole roof, so every ridge is
 * really "the tiles meeting at a line". What the ridge geometry adds is the
 * 脊瓦 that caps that line, and the ornaments at its ends. Both are placed by
 * reading the surface: a cap's rims hang down to about the barrel tiles'
 * mid-height, so a cap straddles the tiles underneath instead of floating.
 */

import { Matrix4, Vector3 } from 'three';
import type { RoofField } from './roofField';
import type { TileSpec } from './tiles';
import { barrelCrown as crownOf } from './tiles';
import {
  box,
  makeOrnament,
  makeRidgeCap,
  mergeGeoms,
  tileShape,
  cylinder,
} from './tileKit';

export type RidgeKind =
  | 'ridgeCap' // 正脊 — the main ridge
  | 'hipCap' // 垂脊 / 降棟 — down each hip
  | 'bargeCap' // 排山勾滴 — along a gable edge
  | 'breakCap' // 博脊 — the 歇山 break line
  | 'chiwen' // 正吻 / 鴟吻 at the main ridge ends
  | 'onigawara' // 鬼瓦 at the foot of a hip
  | 'finial'; // 寶頂 on a pyramidal apex

export interface RidgeInstance {
  kind: RidgeKind;
  m: Matrix4;
}

export interface RidgeSpec {
  /** Cap chord as a multiple of the tile pitch. */
  capWidthRatio: number;
  /** Cap length as a multiple of the tile length. */
  capLengthRatio: number;
  /** Cap rise above the tile bedding plane, in metres (0 = derive it). */
  capRise: number;
  /**
   * Step between 脊瓦 as a fraction of a cap length. They are laid overlapping
   * like tiles, so the step is shorter than the cap (0.6 ⇒ 40% overlap); a short
   * step also keeps a rigid cap from chording across the 翼角 kink.
   */
  capOverlap: number;
  /** Ornament height as a multiple of the tile pitch. */
  ornamentRatio: number;
  emitOrnaments: boolean;
  /** Overall cap scale for 排山勾滴 (they are one size down). */
  bargeScale: number;
}

export function ridgeSpec(over: Partial<RidgeSpec> = {}): RidgeSpec {
  return {
    capWidthRatio: 1.2,
    capLengthRatio: 1.15,
    capRise: 0,
    capOverlap: 0.6,
    ornamentRatio: 3.2,
    emitOrnaments: true,
    bargeScale: 0.72,
    ...over,
  };
}

/** The 脊瓦 geometry for a given tile: authored at the spec's cap size. */
export function ridgeCapGeom(tile: TileSpec, spec: RidgeSpec) {
  const w = tile.pitch * spec.capWidthRatio;
  return makeRidgeCap(
    tileShape({
      width: w,
      length: tile.length * spec.capLengthRatio,
      sagitta: w * 0.3,
      thickness: tile.thickness * 1.6,
      convex: true,
      taper: 0.02,
      bow: 0,
      arcSegments: 9,
    }),
  );
}

export function ornamentGeoms(tile: TileSpec, spec: RidgeSpec) {
  const h = tile.pitch * spec.ornamentRatio;
  // 正吻/鬼瓦 are wide and squat: a ridge-end ornament is as much a block as a
  // figure, and a tall thin one reads as a post from any distance.
  const chiwen = makeOrnament(h * 1.25, h * 0.78);
  const onigawara = makeOrnament(h * 0.75, h * 0.46, false);
  const finial = mergeGeoms([
    cylinder(h * 0.28, h * 0.1, 16),
    cylinder(h * 0.12, h * 0.42, 12),
    cylinder(h * 0.24, h * 0.06, 16),
    cylinder(h * 0.2, h * 0.06, 16),
    cylinder(h * 0.16, h * 0.06, 14),
    makeOrnament(h * 0.34, h * 0.5, false),
    box(h * 0.05, h * 0.16, h * 0.05),
  ]);
  return { chiwen, onigawara, finial };
}

/**
 * Points spaced ~step apart along a polyline, both ends included and NO
 * duplicates: a repeated point makes a zero-length direction, and a zero
 * direction silently produces a degenerate basis that erases whatever is
 * placed with it.
 */
function sampleByArc(pts: Vector3[], step: number): Vector3[] {
  const out: Vector3[] = [pts[0].clone()];
  let carry = 0;
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i];
    const b = pts[i + 1];
    const len = a.distanceTo(b);
    if (len < 1e-9) continue;
    // carry === 0 means the previous segment already ended exactly here
    let s = carry > 1e-9 ? carry : step;
    while (s < len - 1e-9) {
      out.push(a.clone().lerp(b, s / len));
      s += step;
    }
    carry = s - len;
  }
  const last = pts[pts.length - 1];
  if (out[out.length - 1].distanceTo(last) > step * 0.35) out.push(last.clone());
  return out;
}

export function generateRidges(
  field: RoofField,
  tile: TileSpec,
  spec: RidgeSpec,
): RidgeInstance[] {
  const out: RidgeInstance[] = [];
  const capW = tile.pitch * spec.capWidthRatio;
  const capSag = capW * 0.3;
  const capL = tile.length * spec.capLengthRatio;
  // Height of a 筒瓦 crown above the bedding plane — ONE definition, in tiles.ts.
  const barrelCrown = crownOf(tile);
  /**
   * How high a cap must sit. Its rims have to reach down to the tiles that are
   * actually under them — the 筒瓦 that run either side of the ridge, one half
   * pitch away. So: measure the roof's own fall over that half pitch and bed the
   * rim exactly there. On a steep roof the fall exceeds the tile stack and the
   * cap simply beds onto the sheathing (clamped at 0) rather than hovering.
   */
  const bedAt = (mid: Vector3, across: Vector3): number => {
    const o = tile.pitch * 0.5;
    const yHere = field.yAt(mid.x, mid.z);
    const yA = field.yAt(mid.x + across.x * o, mid.z + across.z * o);
    const yB = field.yAt(mid.x - across.x * o, mid.z - across.z * o);
    // The cap beds on whichever side the roof actually falls away — that is
    // where its 筒瓦 neighbours are. (On a 博脊 the other side runs straight
    // into the 山牆, which is the wall's business, not the cap's.)
    const fall = Math.max(0, yHere - yA, yHere - yB);
    return Math.max(0, barrelCrown - fall);
  };

  /** Cap rise at a point, for seating the ornaments on the same stack. */
  const riseAt = (p: Vector3, across: Vector3) => capSag + bedAt(p, across);

  const place = (
    kind: RidgeKind,
    from: Vector3,
    to: Vector3,
    up: Vector3,
    lift: number,
    renderLen: number,
  ) => {
    const dir = new Vector3().subVectors(to, from);
    const len = dir.length();
    if (len < 1e-5) return;
    dir.divideScalar(len);
    const u = up.clone().addScaledVector(dir, -up.dot(dir));
    if (u.lengthSq() < 1e-9) u.set(0, 1, 0).addScaledVector(dir, -dir.y);
    u.normalize();
    const across = new Vector3().crossVectors(dir, u).normalize();

    const m = new Matrix4();
    m.makeBasis(across, u, dir);
    // Drop the chord midpoint onto the surface before lifting it. The 翼角
    // curve has a kink at the 飛椽 head, and a straight chord across a kink sits
    // tens of millimetres off the true curve there — enough to leave a cap
    // hanging. Projecting first means every cap is measured from the surface it
    // is bedded on.
    const mid = new Vector3().addVectors(from, to).multiplyScalar(0.5);
    mid.set(mid.x, field.yAt(mid.x, mid.z), mid.z);
    mid.addScaledVector(u, lift);
    m.setPosition(mid);
    m.multiply(new Matrix4().makeScale(1, 1, renderLen / capL));
    out.push({ kind, m });
  };

  const runs = field.ridgeRuns();
  for (const run of runs) {
    const isBarge = run.kind === 'bargeboard';
    const scale = isBarge ? spec.bargeScale : 1;
    const kind: RidgeKind =
      run.kind === 'mainRidge'
        ? 'ridgeCap'
        : run.kind === 'hip'
          ? 'hipCap'
          : run.kind === 'break'
            ? 'breakCap'
            : 'bargeCap';

    /**
     * Lay the caps by walking a fine polyline and closing each cap when it has
     * either used up a cap-length or turned enough that a rigid tile would
     * chord the curve. That makes the caps SHORTEN automatically round the 翼角
     * sweep — which is what happens on a real corner, where the 脊瓦 fan round
     * in short pieces — and keeps every cap bedded on the surface it rides.
     */
    const fine = sampleByArc(run.points, 0.035);
    const maxLen = capL * scale * 0.62;
    const maxBend = 0.09;
    let a = 0;
    const runStart = fine[0];
    const runEnd = fine[fine.length - 1];

    const flush = (b: number, force: boolean, isLast: boolean, isFirst: boolean) => {
      const from = fine[a];
      const to = fine[b];
      const len = from.distanceTo(to);
      if (len < 0.02 && !force) return;
      const mid = new Vector3().addVectors(from, to).multiplyScalar(0.5);
      const midXZ = mid.clone().set(mid.x, field.yAt(mid.x, mid.z), mid.z);
      const up = field.normalAt(mid.x, mid.z, new Vector3());
      const seg = new Vector3().subVectors(to, from).setY(0).normalize();
      const across = new Vector3().crossVectors(seg, up).normalize();
      const bed = spec.capRise > 0 ? spec.capRise - capSag * scale : bedAt(midXZ, across);
      // A 脊瓦 is a real tile: it renders at its own length, and the step is
      // what makes them overlap. Letting the geometry stretch by the overlap
      // draws metre-long rubber caps.
      let renderLen = capL * scale;
      // A cap is centred on its chord, but a chord is shorter than a cap, so a
      // full-length cap overhangs its own chord by half the overlap. Near the end
      // of a run that overhang would hang PAST the run — over a 山牆 edge or off
      // an apex, where there is no tiled surface under it at all. Clamp it back.
      renderLen = Math.min(renderLen, 2 * midXZ.distanceTo(runEnd));
      renderLen = Math.min(renderLen, 2 * midXZ.distanceTo(runStart));
      void isLast;
      void isFirst;
      place(kind, from, to, up, bed + capSag * scale, renderLen);
    };
    for (let b = 1; b < fine.length; b++) {
      const cum = fine[a].distanceTo(fine[b]);
      const d0 = b > 1 ? new Vector3().subVectors(fine[b - 1], fine[a]).normalize() : null;
      const d1 = new Vector3().subVectors(fine[b], fine[a]).normalize();
      const bend = d0 ? Math.acos(Math.min(1, Math.max(-1, d0.dot(d1)))) : 0;
      const last = b === fine.length - 1;
      if (cum >= maxLen || bend > maxBend || last) {
        flush(b, last && b - a > 0, last, a === 0);
        a = b;
      }
    }
    const pts = fine;

    if (!spec.emitOrnaments) continue;
    if (run.kind === 'mainRidge' && pts.length >= 2) {
      // 正吻 at both ends, facing outward along the ridge
      for (const end of [0, pts.length - 1]) {
        const p = pts[end];
        const inward = end === 0 ? pts[1].clone() : pts[pts.length - 2].clone();
        const dir = new Vector3().subVectors(p, inward).setY(0).normalize();
        const across0 = new Vector3().crossVectors(dir, new Vector3(0, 1, 0)).normalize();
        const x = p.x;
        const z = p.z;
        const y = field.yAt(x, z) + riseAt(p, across0);
        const up = new Vector3(0, 1, 0);
        const across = new Vector3().crossVectors(dir, up).normalize();
        const u = new Vector3().crossVectors(across, dir).normalize();
        const m = new Matrix4();
        m.makeBasis(across, u, dir);
        m.setPosition(x, y, z);
        out.push({ kind: 'chiwen', m });
      }
    }
    if (run.kind === 'hip' && pts.length >= 2) {
      // 鬼瓦 at the foot of the hip, sitting on the corner
      const p = pts[0];
      const dir = new Vector3().subVectors(p, pts[1]).setY(0).normalize();
      const across0 = new Vector3().crossVectors(dir, new Vector3(0, 1, 0)).normalize();
      const x = p.x;
      const z = p.z;
      const y = field.yAt(x, z) + riseAt(p, across0);
      const up = new Vector3(0, 1, 0);
      const across = new Vector3().crossVectors(dir, up).normalize();
      const u = new Vector3().crossVectors(across, dir).normalize();
      const m = new Matrix4();
      m.makeBasis(across, u, dir);
      m.setPosition(x, y, z);
      out.push({ kind: 'onigawara', m });
    }
  }

  // 寶頂 on a 攢尖 apex: the four hips meet at a point and there is no 正脊 at
  // all. If the plan is not square the ridge does not collapse and the roof is
  // a 廡殿 with 正吻 instead, so this must be conditional.
  if (spec.emitOrnaments && field.ridgeLen <= 1e-6) {
    const apex = new Vector3(0, 0, 0);
    const gov = field.governing(0, 0);
    apex.set(0, field.yAt(0, 0), 0);
    void gov;
    out.push({
      kind: 'finial',
      m: new Matrix4()
        .makeBasis(new Vector3(1, 0, 0), new Vector3(0, 1, 0), new Vector3(0, 0, 1))
        .setPosition(apex.x, apex.y + capSag * 0.6, apex.z),
    });
  }

  return out;
}
