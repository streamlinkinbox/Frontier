/**
 * 屋頂生成器 · the roof assembly.
 *
 * One call takes a plain request object (the kind of thing a UI or a config
 * file hands over) and returns everything the renderer, the exporter and the
 * verifier need: the height field, the tile instances, the timber frame, the
 * ridge caps, and one unit geometry per instance kind.
 *
 * Nothing here decides what a roof LOOKS like — that is `RoofField`.
 * Nothing here checks that it is CORRECT — that is `verifyRoof`.
 * This file only wires the three together and hands out renderable pieces.
 */

import type { BufferGeometry } from 'three';
import {
  RoofField,
  type RoofFieldSpec,
  type RoofType,
} from './roofField';
import {
  layTiles,
  panWidth,
  tilePreset,
  type LayResult,
  type TileKind,
  type TileSpec,
} from './tiles';
import {
  frameSpec,
  generateFrame,
  type FrameKind,
  type FrameResult,
  type FrameSpec,
} from './frame';
import {
  generateRidges,
  ridgeSpec,
  type RidgeInstance,
  type RidgeKind,
  type RidgeSpec,
} from './ridges';
import {
  box,
  cylinder,
  makeDripTile,
  makeEaveRound,
  makeFinial,
  makeOgreTile,
  makeRidgeCap,
  makeTileSolid,
  mergeGeoms,
  tileShape,
} from './tileKit';

export type Style = 'chinese' | 'japanese';
export type TilePresetName = 'chinese' | 'qing' | 'japanese' | 'temple';

export interface RoofRequest {
  /** Which of the four classic forms. 歇山 | 廡殿 | 懸山 | 攢尖 */
  type: RoofType;
  /** Naming, tile default and ornament set. Geometry is identical. */
  style: Style;
  /** 通面闊 — the eave-purlin line, X. The roof oversails it. */
  width: number;
  /** 通進深 — the eave-purlin line, Z. */
  depth: number;
  /** 舉高 — rise ÷ half-span. 1/3 palace, 1/4 hall, 1/5 small. */
  riseRatio: number;
  /** 折屋 step count, 1…7. More steps = a deeper concave curve. */
  steps: number;
  /** 出檐 — how far the flying rafter carries past the eave purlin, in metres. */
  eaveProjection: number;
  /** 飛椽 as a fraction of the eave projection (0 = no flying rafter). */
  flyOverEave: number;
  /** Flying-rafter pitch as a multiple of the mean slope. 1.2 ≈ 飛魁. */
  flyPitch: number;
  /** Hip plan angle. 1.0 = 45° (the normal case). */
  hipInsetRatio: number;
  /** 歇山: the fraction of the slope where the hip gives way to the 破風. */
  breakT: number;
  /** 懸山: how far the roof oversails the gable wall, in metres. */
  gableOverhang: number;
  /** Tile bedding-plane offset along the profile normal. Sets the eave thickness. */
  surfaceLift: number;
  /** 起翹 — corner upturn at the eave tip, in metres. 0 = plain. */
  cornerUpturn: number;
  /** Falloff radius of the corner upturn, in metres. */
  cornerRadius: number;

  tile: TilePresetName;
  /** Uniform tile scale. 1 = the preset's historical size. */
  tileScale: number;
  /** 筒瓦 + 板瓦 (two layers) or 板瓦 only (single layer). */
  hasBarrels: boolean;
  /** Emit the 瓦當 / 滴水 eave course. */
  hasEaveRow: boolean;

  frame: Partial<FrameSpec>;
  ridge: Partial<RidgeSpec>;
}

export interface RoofBuild {
  request: RoofRequest;
  field: RoofField;
  tileSpec: TileSpec;
  tiles: LayResult;
  frame: FrameResult;
  ridges: RidgeInstance[];
  /** One geometry per instance kind, in the instance matrices' local frame. */
  geometries: {
    tile: Record<TileKind, BufferGeometry>;
    frame: Record<FrameKind, BufferGeometry>;
    ridge: Record<RidgeKind, BufferGeometry>;
  };
  stats: {
    tileCount: number;
    frameCount: number;
    ridgeCount: number;
    /** World-space bounding box of everything. */
    min: [number, number, number];
    max: [number, number, number];
    /** Height of the ridge above the eave purlin. */
    rise: number;
  };
}

export function defaultRoofRequest(over: Partial<RoofRequest> = {}): RoofRequest {
  return {
    type: 'hipGable',
    style: 'chinese',
    width: 8.4,
    depth: 6.2,
    riseRatio: 1 / 3,
    steps: 5,
    eaveProjection: 0.95,
    flyOverEave: 0.6,
    flyPitch: 1.2,
    hipInsetRatio: 1.0,
    breakT: 0.55,
    gableOverhang: 0.55,
    surfaceLift: 0.15,
    cornerUpturn: 0.26,
    cornerRadius: 3.2,
    tile: 'chinese',
    tileScale: 1,
    hasBarrels: true,
    hasEaveRow: true,
    frame: {},
    ridge: {},
    ...over,
  };
}

/** Named starting points. The UI's presets are exactly these. */
export const ROOF_PRESETS: Record<string, RoofRequest> = {
  'hall-hipGable': defaultRoofRequest(),
  'palace-hip': defaultRoofRequest({
    type: 'hip',
    width: 11.0,
    depth: 8.0,
    riseRatio: 1 / 3,
    steps: 6,
    eaveProjection: 1.15,
    cornerUpturn: 0.3,
    tile: 'temple',
  }),
  'house-gable': defaultRoofRequest({
    type: 'gable',
    width: 7.4,
    depth: 5.6,
    riseRatio: 1 / 3.4,
    steps: 4,
    eaveProjection: 0.8,
    cornerUpturn: 0.1,
    tile: 'qing',
    hasBarrels: false,
  }),
  'pavilion-pyramid': defaultRoofRequest({
    type: 'pyramid',
    width: 4.6,
    depth: 4.6,
    riseRatio: 1 / 2.6,
    steps: 5,
    eaveProjection: 0.85,
    cornerUpturn: 0.34,
    cornerRadius: 2.4,
    tile: 'temple',
  }),
  'minka-japanese': defaultRoofRequest({
    type: 'hipGable',
    style: 'japanese',
    width: 7.0,
    depth: 5.4,
    riseRatio: 1 / 2.5,
    steps: 4,
    eaveProjection: 1.2,
    cornerUpturn: 0.22,
    tile: 'japanese',
  }),
};

function fieldSpecFrom(req: RoofRequest): RoofFieldSpec {
  return {
    type: req.type,
    width: req.width,
    depth: req.depth,
    riseRatio: req.riseRatio,
    steps: req.steps,
    eaveProjection: req.eaveProjection,
    flyOverEave: req.flyOverEave,
    flyPitch: req.flyPitch,
    hipInsetRatio: req.hipInsetRatio,
    breakT: req.breakT,
    gableOverhang: req.gableOverhang,
    surfaceLift: req.surfaceLift,
    cornerUpturn: req.cornerUpturn,
    cornerRadius: req.cornerRadius,
  };
}

function tileSpecFrom(req: RoofRequest): TileSpec {
  const s = tilePreset(req.tile, req.tileScale);
  return { ...s, hasBarrels: req.hasBarrels, hasEaveRow: req.hasEaveRow };
}

/** Unit geometries, authored at the spec's own sizes so instances need no guess. */
function buildGeometries(req: RoofRequest, spec: TileSpec) {
  const panW = panWidth(spec);
  const barW = spec.pitch * spec.barrelRatio;
  const tile: Record<TileKind, BufferGeometry> = {
    pan: makeTileSolid(
      tileShape({
        width: panW,
        length: spec.length,
        sagitta: spec.panSagitta,
        thickness: spec.thickness,
        convex: false,
        // no taper: a tapered 板瓦 leaves a wedge gap to the sheathing between
        // neighbours, which is what the lateral lap above exists to close
        taper: 0,
        bow: spec.bow,
      }),
    ),
    barrel: makeTileSolid(
      tileShape({
        width: barW,
        length: spec.length,
        sagitta: spec.barrelSagitta,
        thickness: spec.thickness,
        convex: true,
        taper: spec.taper * 0.6,
        bow: spec.bow * 0.5,
      }),
    ),
    eaveRound: makeEaveRound(
      tileShape({
        width: barW,
        length: spec.length,
        sagitta: spec.barrelSagitta,
        thickness: spec.thickness,
        convex: true,
      }),
    ),
    drip: makeDripTile(
      tileShape({
        width: panW,
        length: spec.length,
        sagitta: spec.panSagitta,
        thickness: spec.thickness,
        convex: false,
        taper: spec.taper,
        bow: spec.bow,
      }),
    ),
  };

  // Frame members: every instance matrix scales a unit primitive. Round
  // members are radii-1 cylinders, square ones are 1×1×1 boxes.
  const round = cylinder(1, 1, 10);
  const square = box(1, 1, 1);
  const dougong = mergeGeoms([
    box(0.6, 0.16, 0.6),
    box(0.5, 0.14, 0.5),
    box(0.34, 0.5, 0.34),
  ]);
  const frame: Record<FrameKind, BufferGeometry> = {
    rafter: round.clone(),
    flyRafter: square.clone(),
    hipRafter: round.clone(),
    purlin: round.clone(),
    bargeboard: box(1, 1, 1),
    column: round.clone(),
    beam: box(1, 1, 1),
    post: round.clone(),
    dougong,
  };

  const rs = ridgeSpec(req.ridge);
  const capW = spec.pitch * rs.capWidthRatio;
  const capL = spec.length * rs.capLengthRatio;
  const orn = spec.pitch * rs.ornamentRatio;
  const capShape = tileShape({
    width: capW,
    length: capL,
    sagitta: capW * 0.45,
    thickness: spec.thickness * 1.6,
    convex: true,
    taper: 0.03,
    bow: 0,
    arcSegments: 9,
  });
  const cap = makeRidgeCap(capShape);
  const ridge: Record<RidgeKind, BufferGeometry> = {
    ridgeCap: cap,
    hipCap: cap.clone(),
    breakCap: cap.clone(),
    bargeCap: cap.clone(),
    chiwen: makeOgreTile(orn, orn * 1.15),
    onigawara: makeOgreTile(orn * 0.8, orn * 0.95),
    finial: makeFinial(orn * 1.1, orn * 2.1),
  };

  return { tile, frame, ridge };
}

type Bounds = { min: [number, number, number]; max: [number, number, number] };

function boundsOf(build: Omit<RoofBuild, 'stats'>): Bounds {
  const min: [number, number, number] = [Infinity, Infinity, Infinity];
  const max: [number, number, number] = [-Infinity, -Infinity, -Infinity];
  const eat = (x: number, y: number, z: number) => {
    if (x < min[0]) min[0] = x;
    if (y < min[1]) min[1] = y;
    if (z < min[2]) min[2] = z;
    if (x > max[0]) max[0] = x;
    if (y > max[1]) max[1] = y;
    if (z > max[2]) max[2] = z;
  };
  const e = { x: 0, y: 0, z: 0 };
  const push = (m: { elements: ArrayLike<number> }) => {
    e.x = m.elements[12];
    e.y = m.elements[13];
    e.z = m.elements[14];
    eat(e.x, e.y, e.z);
  };
  for (const t of build.tiles.instances) push(t.m);
  for (const f of build.frame.instances) push(f.m);
  for (const r of build.ridges) push(r.m);
  for (const g of build.frame.sheathing) {
    for (let i = 0; i < g.positions.length; i += 3) {
      eat(g.positions[i], g.positions[i + 1], g.positions[i + 2]);
    }
  }
  // the eave tips stick out past any member: take the field's own footprint
  const f = build.field;
  for (const face of f.faces) {
    const t = f.faceNodes(face)[0].t;
    const u = f.uLimit(face, t);
    for (const uu of [-u, 0, u]) {
      const p = f.pos(face, t, uu);
      eat(p.x, p.y, p.z);
    }
  }
  return { min, max };
}

export function generateRoof(reqIn: Partial<RoofRequest> = {}): RoofBuild {
  const req = defaultRoofRequest(reqIn);
  const field = new RoofField(fieldSpecFrom(req));
  const tileSpec = tileSpecFrom(req);
  const tiles = layTiles(field, tileSpec);
  const frame = generateFrame(field, frameSpec(req.frame), tileSpec.pitch);
  const ridges = generateRidges(field, tileSpec, ridgeSpec(req.ridge));
  const geometries = buildGeometries(req, tileSpec);

  const partial = { request: req, field, tileSpec, tiles, frame, ridges, geometries };
  const { min, max } = boundsOf(partial);

  return {
    ...partial,
    stats: {
      tileCount: tiles.instances.length,
      frameCount: frame.instances.length,
      ridgeCount: ridges.length,
      min,
      max,
      rise: field.juzhe.H0,
    },
  };
}

/* ------------------------------------------------------------------ helpers */

/**
 * 山牆 / 妻壁 — the triangular wall face that closes a 懸山 or the gable core of
 * an 歇山. Emitted as its own geometry (not instanced): the roof has to sit on
 * something, and this is the thing it sits on.
 */
export function gableWalls(
  build: RoofBuild,
  thickness = 0.16,
): { positions: number[]; indices: number[] } {
  const f = build.field;
  const out = { positions: [] as number[], indices: [] as number[] };
  const tri = (
    a: [number, number, number],
    b: [number, number, number],
    c: [number, number, number],
  ) => {
    const base = out.positions.length / 3;
    out.positions.push(...a, ...b, ...c);
    out.indices.push(base, base + 1, base + 2);
  };
  const xa = f.spec.type === 'gable' ? f.ex : f.gableCoreX;
  if (xa <= 0) return out;
  const eaveFace = f.faces.find((fc) => fc.kind === 'slope')!;
  const tEave = f.faceNodes(eaveFace)[0].t;
  const yEave = f.timberProfileY(0, eaveFace.depth);
  void tEave;
  const zEdge = f.ez;
  const yRidge = f.timberProfileY(1, eaveFace.depth);
  for (const sgn of [-1, 1] as const) {
    const x = sgn * (xa - thickness / 2);
    // quad down to the eave purlin line, then the triangle up to the ridge
    tri([x, yEave - 0.35, -zEdge], [x, yEave - 0.35, zEdge], [x, yEave, zEdge]);
    tri([x, yEave - 0.35, -zEdge], [x, yEave, zEdge], [x, yEave, -zEdge]);
    tri([x, yEave, -zEdge], [x, yEave, zEdge], [x, yRidge, 0]);
  }
  return out;
}
