/**
 * tiles.ts — laying the tile courses.
 *
 * Courses are spaced by TRUE SURFACE ARC LENGTH, not plan distance, because the
 * overlap rule 壓六露四 ("press six, expose four" — 40 % of each tile exposed,
 * 60 % covered by the course above) is measured along the slope. On a curved
 * juzhe roof a course spaced evenly in arc length LOOKS evenly spaced; one
 * spaced in plan bunches up at the ridge. That difference is the difference
 * between a roof that reads correctly and one that does not.
 *
 * Columns run straight up the slope: the grid is fixed on the eave row and
 * carried to the ridge. Where a column meets a hip (or the gable of an 入母屋)
 * the tile is CUT — its width trimmed to the face boundary and its yaw turned
 * so its outer edge lies ALONG the hip line. Since a hip line is a straight
 * line in plan, the cut edge follows it exactly instead of stepping.
 *
 * The lowest course swaps in the proper components: 滴水 dīshuǐ for the pan
 * tiles and 瓦當 wǎdāng for the barrels — that is what actually finishes a real
 * eave, and it is why the eave line of a real building reads as a row of discs.
 */

import { Matrix4, Vector3 } from 'three';
import type { Face, RoofField } from './roofField';

export type TileKind = 'pan' | 'barrel' | 'eaveRound' | 'drip';

export interface TileSpec {
  /** Centre-to-centre distance between barrel rows = the pan tile's width. */
  pitch: number;
  /** Nominal tile length along the slope. */
  length: number;
  /** Fraction of each tile left exposed: 0.4 = Song 壓六露四, 0.3 = Qing. */
  exposure: number;
  panSagitta: number;
  barrelSagitta: number;
  /** Barrel chord as a fraction of the pitch. */
  barrelRatio: number;
  thickness: number;
  taper: number;
  bow: number;
  /** Two-layer 筒瓦 system (true) vs single-layer 桟瓦 (false). */
  hasBarrels: boolean;
  /** Emit 瓦當 + 滴水 on the lowest course. */
  hasEaveRow: boolean;
  /**
   * How much wider than its column the 板瓦 is drawn, so neighbours LAP. Laid
   * edge to edge with a taper, the wedge between two tiles is a slot straight
   * through to the 望板 — which is exactly what the small overlap prevents.
   */
  panOverlap: number;
  /**
   * 灰漿 setting bed. 板瓦 are not nailed to the 望板, they are bedded in lime
   * mortar on it — this is that mortar's thickness. It also keeps the tiled
   * surface from being coplanar with the sheathing, which is both wrong and
   * invisible in a renderer.
   */
  bedHeight: number;
}

export interface TileInstance {
  kind: TileKind;
  m: Matrix4;
  /** Nominal geometry width this instance was authored at. */
  nominalWidth: number;
  /** Nominal geometry length this instance was authored at. */
  nominalLength: number;
  /** Provenance, so the verifier can check the tile against its OWN face. */
  face: string;
  t0: number;
  t1: number;
  uC: number;
  lift: number;
}

export interface LayResult {
  instances: TileInstance[];
  byKind: Record<TileKind, number>;
  truncated: boolean;
}

export function tilePreset(
  style: 'chinese' | 'japanese' | 'qing' | 'temple',
  scale = 1,
): TileSpec {
  const base: TileSpec = {
    pitch: 0.2 * scale,
    length: 0.22 * scale,
    exposure: 0.4,
    panSagitta: 0.032 * scale,
    barrelSagitta: 0.058 * scale,
    barrelRatio: 0.62,
    thickness: 0.018 * scale,
    taper: 0.12,
    bow: 0.01 * scale,
    hasBarrels: true,
    hasEaveRow: true,
    panOverlap: 0.09,
    bedHeight: 0.015,
  };
  switch (style) {
    case 'qing': // 青瓦 — unglazed grey, the common vernacular roof
      return { ...base, pitch: 0.18 * scale, length: 0.2 * scale, exposure: 0.3, panSagitta: 0.026 * scale };
    case 'japanese': // 桟瓦 sangawara — flatter, tighter, heavier overlap
      return {
        ...base,
        pitch: 0.22 * scale,
        length: 0.24 * scale,
        exposure: 0.35,
        panSagitta: 0.03 * scale,
        barrelSagitta: 0.05 * scale,
        barrelRatio: 0.56,
      };
    case 'temple': // 琉璃瓦 — big, heavy glazed imperial tiles
      return {
        ...base,
        pitch: 0.26 * scale,
        length: 0.3 * scale,
        panSagitta: 0.04 * scale,
        barrelSagitta: 0.075 * scale,
      };
    default:
      return base;
  }
}

/**
 * How much two neighbouring 板瓦 lap. On a 筒瓦 roof the barrels close the seams
 * and a token lap is enough. On a 板瓦-only roof there is nothing over the seam,
 * so the pans themselves must lap far enough sideways to bury each other's
 * up-standing flanks — that is the real reason single-layer tile roofs read as
 * stacked channels. Derived from the FINAL spec, not the preset, because the
 * caller can switch the barrels off.
 */
export function panOverlapOf(spec: TileSpec): number {
  if (spec.hasBarrels) return spec.panOverlap;
  return Math.min(0.42, Math.max(0.12, (spec.panSagitta / spec.pitch) * 1.15));
}

/** Rendered chord of one 板瓦, including its lateral lap. */
export function panWidth(spec: TileSpec): number {
  return spec.pitch * (1 + panOverlapOf(spec));
}

/** Height of a 筒瓦 crown above the bedding plane — the stack the 脊 bed on. */
export function barrelCrown(spec: TileSpec): number {
  return spec.bedHeight + spec.panSagitta + spec.barrelSagitta * 0.8;
}

/**
 * Instance count, for the UI and for budget checks. It must integrate along the
 * slope: a face narrows from its eave to the ridge, so multiplying the EAVE
 * column count by the row count over-counts a hip roof by nearly double.
 */
export function estimateTileCount(field: RoofField, spec: TileSpec): number {
  const advance = Math.max(0.02, spec.length * (1 - spec.exposure));
  const perRow = spec.hasBarrels ? 2 : 1;
  let n = 0;
  for (const face of field.faces) {
    const nodes = field.faceNodes(face);
    if (nodes.length < 2) continue;
    const tTip = nodes[0].t;
    const tEnd = Math.min(1, face.endClip);
    const sEnd = field.arcAtT(face, tEnd);
    const rows = Math.ceil(sEnd / advance);
    // five probes down the slope are plenty: the width is piecewise linear
    for (let r = 0; r < rows; r++) {
      const t = tTip + ((r + 0.5) / rows) * (tEnd - tTip);
      const cols = Math.max(1, Math.ceil((2 * field.uLimit(face, t)) / spec.pitch));
      n += cols * perRow;
    }
  }
  return Math.round(n);
}

export function layTiles(field: RoofField, spec: TileSpec, maxTiles = 400_000): LayResult {
  const instances: TileInstance[] = [];
  const byKind: Record<TileKind, number> = { pan: 0, barrel: 0, eaveRound: 0, drip: 0 };
  let truncated = false;

  for (const face of field.faces) {
    layFace(field, face, spec, instances, (extra) => {
      if (instances.length + extra > maxTiles) {
        truncated = true;
        return false;
      }
      return true;
    });
    if (truncated) break;
  }

  for (const t of instances) byKind[t.kind]++;
  return { instances, byKind, truncated };
}

interface EmitArgs {
  field: RoofField;
  face: Face;
  kind: TileKind;
  /** Centre of the low (down-slope) end of the tile. */
  pLow: Vector3;
  /** Centre of the high (up-slope) end. */
  pHigh: Vector3;
  /** Nominal geometry width. */
  nominalWidth: number;
  /** Nominal geometry length. */
  nominalLength: number;
  /** Actual width of this instance. */
  width: number;
  /**
   * Rendered length. This is the ARC it has to cover, not the chord between its
   * two end points: on a concave roof the chord is shorter than the surface, so
   * a chord-length tile leaves a sliver of bare sheathing at every course break.
   */
  arcLen: number;
  /** Lift off the sheathing along the surface normal. */
  lift: number;
  /** Turn the outer edge to lie along the hip cut. */
  alignToCut: boolean;
  t0: number;
  t1: number;
  uCut: number;
  uC: number;
}

function layFace(
  field: RoofField,
  face: Face,
  spec: TileSpec,
  out: TileInstance[],
  canEmit: (extra: number) => boolean,
): void {
  const nodes = field.faceNodes(face);
  if (nodes.length < 2) return;

  const tTip = nodes[0].t;
  const tEnd = Math.min(1, face.endClip);
  const sEnd = field.arcAtT(face, tEnd);
  const advance = Math.max(0.02, spec.length * (1 - spec.exposure));
  const pitch = spec.pitch;
  const halfW = pitch * 0.5;

  const uEave = field.uLimit(face, tTip);
  const nCol = Math.max(1, Math.ceil((2 * uEave) / pitch));

  // Stacking heights, measured UP from the sheathing's top face, which is the
  // bedding plane: 望板 → 灰漿 → 板瓦 → 筒瓦.
  const panLift = spec.bedHeight;
  const barrelLift = panLift + spec.panSagitta + spec.barrelSagitta * 0.8;

  let row = 0;
  let s = 0;
  while (s < sEnd - 1e-6) {
    const t0 = field.tAtArc(face, s, tEnd);
    const s1 = Math.min(s + spec.length, sEnd);
    const t1 = field.tAtArc(face, s1, tEnd);
    if (t1 - t0 < 1e-5) break;
    const arcLen = s1 - s;

    // limit at the middle of the course: a hip line is straight, so a tile whose
    // cut edge touches it at the mid-course lies along it for its whole length
    const tm = (t0 + t1) * 0.5;
    const uLim = field.uLimit(face, tm);
    const isEaveRow = row === 0 && spec.hasEaveRow;
    const needed = nCol * (spec.hasBarrels ? 2 : 1);
    if (!canEmit(needed)) return;

    for (let k = 0; k < nCol; k++) {
      const uc = -uEave + pitch * (k + 0.5);
      // Draw each 板瓦 a little wider than its column so neighbours lap; then
      // clamp to the face edge so a hip tile still ends exactly on the hip.
      const lap = (pitch * panOverlapOf(spec)) / 2;
      let lo = uc - halfW - lap;
      let hi = uc + halfW + lap;
      let cut = false;
      if (lo < -uLim) {
        lo = -uLim;
        cut = true;
      }
      if (hi > uLim) {
        hi = uLim;
        cut = true;
      }
      const w = hi - lo;
      if (w <= 1e-3) continue;

      const ucTile = (lo + hi) * 0.5;
      const pLow = field.pos(face, t0, ucTile, new Vector3());
      const pHigh = field.pos(face, t1, ucTile, new Vector3());

      out.push(
        place({
          field,
          face,
          kind: isEaveRow ? 'drip' : 'pan',
          pLow,
          pHigh,
          // authored at the full lapped width, so an uncut tile draws at scale 1
          nominalWidth: pitch + pitch * panOverlapOf(spec),
          nominalLength: spec.length,
          width: w,
          arcLen,
          lift: panLift,
          alignToCut: cut,
          t0,
          t1,
          uCut: uLim,
          uC: ucTile,
        }),
      );

      if (spec.hasBarrels) {
        // The barrel caps the seam between this column and the next, so it is
        // centred on the column's far edge.
        const seam = uc + halfW;
        if (seam < uLim - 1e-3 && seam > -uLim + 0) {
          const bLow = field.pos(face, t0, seam, new Vector3());
          const bHigh = field.pos(face, t1, seam, new Vector3());
          out.push(
            place({
              field,
              face,
              kind: isEaveRow ? 'eaveRound' : 'barrel',
              pLow: bLow,
              pHigh: bHigh,
              nominalWidth: pitch * spec.barrelRatio,
              nominalLength: spec.length,
              width: pitch * spec.barrelRatio,
              arcLen,
              lift: barrelLift,
              alignToCut: false,
              t0,
              t1,
              uCut: uLim,
              uC: seam,
            }),
          );
        }
      }
    }
    s += advance;
    row++;
  }
}

const _z = new Vector3();
const _x = new Vector3();
const _y = new Vector3();
const _c = new Vector3();
const _cut = new Vector3();
const _edgeA = new Vector3();
const _edgeB = new Vector3();

function place(a: EmitArgs): TileInstance {
  const { field, face } = a;

  _z.subVectors(a.pHigh, a.pLow);
  const len = _z.length();
  _z.divideScalar(len || 1);

  _x.copy(face.uVec);
  if (a.alignToCut) {
    // the hip's direction in 3-D, taken from two points on the boundary
    _edgeA.copy(field.pos(face, a.t0, a.uCut, new Vector3()));
    _edgeB.copy(field.pos(face, a.t1, a.uCut, new Vector3()));
    _cut.subVectors(_edgeB, _edgeA);
    if (_cut.lengthSq() > 1e-9) {
      _cut.normalize();
      _x.copy(_cut);
    }
  }
  // X must be perpendicular to the slope direction
  _x.addScaledVector(_z, -_x.dot(_z));
  if (_x.lengthSq() < 1e-9) _x.copy(face.uVec).addScaledVector(_z, -face.uVec.dot(_z));
  _x.normalize();
  _y.crossVectors(_z, _x).normalize();

  _c.addVectors(a.pLow, a.pHigh).multiplyScalar(0.5);
  const n = field.normalAt(_c.x, _c.z, new Vector3());
  _c.addScaledVector(n, a.lift);

  const m = new Matrix4();
  m.makeBasis(_x, _y, _z);
  m.setPosition(_c);
  // render the ARC, not the chord, so consecutive courses overlap by exactly
  // the exposure instead of opening a sliver of sheathing between them
  m.multiply(
    new Matrix4().makeScale(a.width / a.nominalWidth, 1, a.arcLen / a.nominalLength),
  );

  return {
    kind: a.kind,
    m,
    nominalWidth: a.nominalWidth,
    nominalLength: a.nominalLength,
    face: face.id,
    t0: a.t0,
    t1: a.t1,
    uC: a.uC,
    lift: a.lift,
  };
}
