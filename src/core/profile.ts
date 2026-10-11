/**
 * Cross-section compiler.
 *
 * A RoadSpec becomes an ordered list of lateral *zones* — verge, footway, kerb
 * top, kerb face, channel, bus lane, parking, traffic lanes, median, ... — each
 * with offsets, finished heights, surface material and travel direction.
 *
 * Sampling the zones yields the profile polyline consumed by every surface mesh,
 * kerb run, drainage object and marking decal. Vertical faces (kerb faces,
 * barrier sides) are emitted as two samples at one offset, so the mesh keeps
 * genuinely hard edges while never duplicating geometry.
 *
 * Convention: offset u is measured from the centreline, +u = RIGHT of the travel
 * direction a→b. World is right-handed: X east, Y up, Z north.
 */

import { clamp, lerp, smoothstep, type V2 } from './math';
import { KERB_PROFILES, type Id, type MedianType, type ParkingKind, type RoadSpec, type SurfaceKind } from './model';

export type ZoneKind =
  | 'verge'
  | 'planting'
  | 'footway'
  | 'kerb-top'
  | 'kerb-face'
  | 'gutter'
  | 'lane'
  | 'shoulder'
  | 'parking'
  | 'bike'
  | 'bus'
  | 'median-flush'
  | 'median-top'
  | 'median-face'
  | 'barrier'
  | 'tram-track'
  | 'wall'
  | 'rail-bed'
  | 'rail-sleeper'
  | 'rail-rail';

export interface ZoneSpan {
  index: number;
  kind: ZoneKind;
  /** lateral extents; for vertical faces from === to */
  from: number;
  to: number;
  yFrom: number;
  yTo: number;
  material: SurfaceKind;
  materialId?: Id;
  /** 1 = travels a→b, -1 = b→a, 0 = not a travel surface */
  dir: 0 | 1 | -1;
  laneIndex: number;
  density: number;
  name: string;
  vertical: boolean;
  upstand: number;
  side: -1 | 0 | 1;
}

export interface ProfileSample {
  u: number;
  y: number;
  zones: number[];
  index: number;
  kind: ZoneKind;
  upstand: number;
  /** surface material of the zone this sample sits on */
  mat: SurfaceKind;
}

export interface LaneRef {
  u: number;
  dir: 1 | -1;
  index: number;
  width: number;
  kind: ZoneKind;
}

export interface CompiledProfile {
  spec: RoadSpec;
  zones: ZoneSpan[];
  bounds: { left: number; right: number };
  /** offsets of the kerb faces (outer pavement edges) */
  kerb: { left: number; right: number };
  median: { from: number; to: number; type: MedianType; topY: number; kerbTop: number } | null;
  laneCentres: LaneRef[];
  carriagewayWidth: number;
  footways: { left: ZoneSpan | null; right: ZoneSpan | null };
  verges: { left: ZoneSpan | null; right: ZoneSpan | null };
  parking: { left: ZoneSpan | null; right: ZoneSpan | null };
  bike: { left: ZoneSpan | null; right: ZoneSpan | null };
  bus: { left: ZoneSpan | null; right: ZoneSpan | null };
  envelope: number;
}

export interface ProfileMods {
  widthAddLeft?: number;
  widthAddRight?: number;
  footwayAddLeft?: number;
  footwayAddRight?: number;
  kerbDropLeft?: number;
  kerbDropRight?: number;
  rampLength?: number;
  crownScale?: number;
  medianWidth?: number;
  shift?: number;
}

export const parkingAngleWidth = (kind: ParkingKind, depth: number): number => {
  switch (kind) {
    case 'angle-45':
      return depth / Math.sin(Math.PI / 4);
    case 'angle-60':
      return depth / Math.sin(Math.PI / 3);
    default:
      return depth;
  }
};

/** Carriageway crown height at |u| from the centreline, relative to the datum. */
export function surfaceY(spec: RoadSpec, absU: number, crownOverride?: number): number {
  const crown = crownOverride ?? spec.crown;
  const half = Math.max(0.5, spec.laneWidth * Math.max(spec.lanesA, spec.lanesB) + Math.max(spec.shoulderA, spec.shoulderB));
  const t = clamp(absU / half, 0, 3);
  return -crown * Math.pow(t, 1.5);
}

function sideCarriageway(spec: RoadSpec, side: -1 | 1): number {
  const s = side < 0 ? spec.left : spec.right;
  const lanes = side < 0 ? spec.lanesB : spec.lanesA;
  const shoulder = side < 0 ? spec.shoulderB : spec.shoulderA;
  return (
    lanes * spec.laneWidth +
    shoulder +
    s.bikeLane +
    (s.parking !== 'none' ? parkingAngleWidth(s.parking, s.parkingWidth) : 0) +
    s.busLane +
    s.taperOut
  );
}

interface SidePlan {
  side: -1 | 1;
  /** carriageway pieces from the median outward */
  cw: Array<{ kind: ZoneKind; width: number; material: SurfaceKind; dir: 0 | 1 | -1; laneIndex: number; name: string }>;
  /** island-side channel width */
  medianChannel: number;
  /** outer pavement-edge channel width */
  edgeChannel: number;
  kerbTop: number;
  kerbUpstand: number;
  /** pieces beyond the kerb, from the kerb outward */
  outer: Array<{ kind: ZoneKind; width: number; material: SurfaceKind; yIn: number; yOut: number; density: number; name: string; id?: Id }>;
  /** median half extent at this side (0 when flush) */
  medianHalf: number;
  medianKerbTop: number;
  medianRaised: boolean;
}

/** Compile the reference cross-section. Zone order and count are constant for every station. */
export function compileProfile(spec: RoadSpec): CompiledProfile {
  const kerb = KERB_PROFILES[spec.kerb];
  const gutter = spec.gutterWidth;
  const raisedMedian = spec.median === 'raised' || spec.median === 'grass' || spec.median === 'barrier';
  const paintedMedian = spec.median === 'painted';
  const medianHalf = raisedMedian || paintedMedian ? spec.medianWidth / 2 : 0;
  const medianKerbTop = raisedMedian && spec.median !== 'barrier' ? kerb.topWidth : 0;
  const medianChannel = raisedMedian && spec.median !== 'barrier' ? Math.min(gutter, 0.35) : 0;
  const crown = spec.crown;
  const kerbTopY = -crown + kerb.upstand;
  const channelY = -crown - 0.015;
  const medianTopY = paintedMedian ? -crown : raisedMedian ? -crown + kerb.upstand : 0;

  const sidePlan = (side: -1 | 1): SidePlan => {
    const s = side < 0 ? spec.left : spec.right;
    const lanes = side < 0 ? spec.lanesB : spec.lanesA;
    const shoulder = side < 0 ? spec.shoulderB : spec.shoulderA;
    // carriageway pieces listed from the median outward: lanes, shoulder, cycle,
    // bus, then kerb-side parking.
    const cw: SidePlan['cw'] = [];
    for (let i = 0; i < lanes; i++) {
      cw.push({ kind: 'lane', width: spec.laneWidth, material: 'asphalt', dir: side, laneIndex: i, name: `Lane ${side < 0 ? 'B' : 'A'}${i + 1}` });
    }
    if (shoulder > 0.001) cw.push({ kind: 'shoulder', width: shoulder, material: spec.shoulderSurface, dir: side, laneIndex: -1, name: `Shoulder ${side < 0 ? 'L' : 'R'}` });
    if (s.bikeLane > 0.001) cw.push({ kind: 'bike', width: s.bikeLane, material: s.bikeLaneSurface === 'raised' ? 'pavers' : 'asphalt', dir: side, laneIndex: -1, name: `Cycle ${side < 0 ? 'L' : 'R'}` });
    if (s.busLane > 0.001) cw.push({ kind: 'bus', width: s.busLane, material: 'asphalt', dir: side, laneIndex: -1, name: `Bus lane ${side < 0 ? 'L' : 'R'}` });
    if (s.parking !== 'none') cw.push({ kind: 'parking', width: parkingAngleWidth(s.parking, s.parkingWidth), material: s.parking === 'parallel' ? 'asphalt' : 'pavers', dir: side, laneIndex: -1, name: `Parking ${side < 0 ? 'L' : 'R'}` });
    const f = s.footway;
    const outer: SidePlan['outer'] = [];
    if (f && f.width > 0.01) {
      const yOut = kerbTopY + (f.crossfall / 100) * f.width;
      outer.push({ kind: 'footway', width: f.width, material: f.surface, yIn: kerbTopY, yOut, density: f.width > 2.6 ? 2 : 1, name: `Footway ${side < 0 ? 'L' : 'R'}`, id: spec.footwayMaterialId });
    }
    if (f && f.plantingWidth > 0.01) {
      outer.push({ kind: 'planting', width: f.plantingWidth, material: f.plantingSurface, yIn: kerbTopY + (f.crossfall / 100) * f.width + 0.02, yOut: kerbTopY + (f.crossfall / 100) * f.width + 0.02, density: 1, name: `Planting ${side < 0 ? 'L' : 'R'}` });
    }
    if (s.verge > 0.01) {
      const base = kerbTopY + (f ? (f.crossfall / 100) * f.width : 0) + (f ? f.plantingWidth * 0 : 0);
      outer.push({ kind: 'verge', width: s.verge, material: s.vergeSurface, yIn: base + 0.02, yOut: base + s.verge * 0.03, density: s.verge > 3 ? 2 : 1, name: `Verge ${side < 0 ? 'L' : 'R'}`, id: spec.vergeMaterialId });
    }
    return {
      side,
      cw,
      medianChannel,
      edgeChannel: gutter,
      kerbTop: kerb.topWidth,
      kerbUpstand: kerb.upstand,
      outer,
      medianHalf,
      medianKerbTop,
      medianRaised: raisedMedian,
    };
  };

  const zones: ZoneSpan[] = [];
  let idx = 0;
  const Z = (z: Partial<ZoneSpan> & Pick<ZoneSpan, 'kind' | 'from' | 'to'>): ZoneSpan => {
    const zone: ZoneSpan = {
      index: idx++,
      yFrom: 0,
      yTo: 0,
      material: 'asphalt',
      dir: 0,
      laneIndex: -1,
      density: 1,
      name: z.kind,
      vertical: false,
      upstand: 0,
      side: z.from + z.to < 0 ? -1 : z.from + z.to > 0 ? 1 : 0,
      ...z,
    } as ZoneSpan;
    zones.push(zone);
    return zone;
  };

  const buildSide = (plan: SidePlan) => {
    // Offsets measured as absolute distance from the centreline, then signed.
    const sgn = plan.side;
    const medianEdge = plan.medianRaised || paintedMedian ? plan.medianHalf + plan.medianKerbTop : 0;
    const out: ZoneSpan[] = [];
    const push = (z: Partial<ZoneSpan> & Pick<ZoneSpan, 'kind' | 'from' | 'to'>) => out.push(Z(z));
    let d = medianEdge;
    // island-side kerb face then its channel (traversed inner → outer)
    if (plan.medianKerbTop > 0.01) {
      push({
        kind: 'kerb-face',
        from: sgn * (plan.medianHalf + plan.medianKerbTop),
        to: sgn * (plan.medianHalf + plan.medianKerbTop),
        yFrom: medianTopY,
        yTo: channelY,
        material: 'kerb',
        materialId: spec.kerbMaterialId,
        vertical: true,
        density: 1,
        name: 'Median kerb face',
        upstand: kerb.upstand,
      });
    }
    if (plan.medianChannel > 0.01) {
      push({
        kind: 'gutter',
        from: sgn * (plan.medianHalf + plan.medianKerbTop),
        to: sgn * (plan.medianHalf + plan.medianKerbTop + plan.medianChannel),
        yFrom: channelY,
        yTo: -spec.crown,
        material: 'concrete',
        density: 1,
        name: 'Channel (median)',
      });
      d = plan.medianHalf + plan.medianKerbTop + plan.medianChannel;
    }
    for (const piece of plan.cw) {
      const from = sgn * d;
      const to = sgn * (d + piece.width);
      push({
        kind: piece.kind,
        from,
        to,
        yFrom: surfaceY(spec, d),
        yTo: surfaceY(spec, d + piece.width),
        material: piece.material,
        dir: piece.dir,
        laneIndex: piece.laneIndex,
        density: piece.width > 3.4 ? 2 : 1,
        name: piece.name,
      });
      d += piece.width;
    }
    if (plan.edgeChannel > 0.01) {
      push({
        kind: 'gutter',
        from: sgn * d,
        to: sgn * (d + plan.edgeChannel),
        yFrom: -spec.crown,
        yTo: channelY,
        material: 'concrete',
        density: 1,
        name: 'Channel (kerb)',
      });
      d += plan.edgeChannel;
    }
    const kerbLine = d;
    if (plan.kerbUpstand > 0.001 && spec.kerb !== 'none') {
      push({
        kind: 'kerb-face',
        from: sgn * d,
        to: sgn * d,
        yFrom: channelY,
        yTo: kerbTopY,
        material: 'kerb',
        materialId: spec.kerbMaterialId,
        vertical: true,
        density: 1,
        name: 'Kerb face',
        upstand: plan.kerbUpstand,
      });
    }
    if (plan.kerbTop > 0.01) {
      push({
        kind: 'kerb-top',
        from: sgn * d,
        to: sgn * (d + plan.kerbTop),
        yFrom: kerbTopY,
        yTo: kerbTopY,
        material: 'kerb',
        materialId: spec.kerbMaterialId,
        density: 1,
        name: 'Kerb',
        upstand: plan.kerbUpstand,
      });
      d += plan.kerbTop;
    }
    for (const piece of plan.outer) {
      push({
        kind: piece.kind,
        from: sgn * d,
        to: sgn * (d + piece.width),
        yFrom: piece.yIn,
        yTo: piece.yOut,
        material: piece.material,
        materialId: piece.id,
        density: piece.density,
        name: piece.name,
      });
      d += piece.width;
    }
    const kerbOffset = sgn * kerbLine;
    // the left side is built right-to-left, so it has to be flipped to run in
    // increasing u; the right side is already in order
    if (sgn < 0) out.reverse();
    return { zones: out, kerbOffset, extent: d };
  };

  const left = buildSide(sidePlan(-1));
  const right = buildSide(sidePlan(1));

  const medianZones: ZoneSpan[] = [];
  if (paintedMedian && medianHalf > 0.01) {
    medianZones.push(
      Z({
        kind: 'median-flush',
        from: -medianHalf,
        to: medianHalf,
        yFrom: surfaceY(spec, medianHalf),
        yTo: surfaceY(spec, medianHalf),
        material: 'asphalt',
        density: medianHalf > 1 ? 2 : 1,
        name: 'Painted median',
      }),
    );
  } else if (raisedMedian) {
    if (spec.median === 'barrier') {
      const w = Math.max(0.35, spec.medianWidth * 0.5);
      medianZones.push(
        Z({ kind: 'median-face', from: -w, to: -w, yFrom: -spec.crown, yTo: medianTopY, material: 'concrete', materialId: spec.medianMaterialId, vertical: true, density: 1, name: 'Barrier face L', upstand: kerb.upstand }),
        Z({ kind: 'barrier', from: -w, to: w, yFrom: medianTopY, yTo: medianTopY, material: 'concrete', materialId: spec.medianMaterialId, density: 2, name: 'Barrier' }),
        Z({ kind: 'median-face', from: w, to: w, yFrom: medianTopY, yTo: -spec.crown, material: 'concrete', materialId: spec.medianMaterialId, vertical: true, density: 1, name: 'Barrier face R', upstand: kerb.upstand }),
      );
    } else {
      medianZones.push(
        Z({ kind: 'median-top', from: -medianHalf, to: medianHalf, yFrom: medianTopY - 0.01, yTo: medianTopY - 0.01, material: spec.median === 'grass' ? 'grass' : 'concrete', materialId: spec.medianMaterialId, density: spec.medianWidth > 3 ? 2 : 1, name: 'Median' }),
        Z({ kind: 'kerb-top', from: medianHalf, to: medianHalf + medianKerbTop, yFrom: medianTopY, yTo: medianTopY, material: 'kerb', materialId: spec.kerbMaterialId, density: 1, name: 'Median kerb R', upstand: kerb.upstand }),
        Z({ kind: 'kerb-top', from: -medianHalf - medianKerbTop, to: -medianHalf, yFrom: medianTopY, yTo: medianTopY, material: 'kerb', materialId: spec.kerbMaterialId, density: 1, name: 'Median kerb L', upstand: kerb.upstand }),
      );
      medianZones.sort((a, b) => a.from - b.from || a.kind.localeCompare(b.kind));
    }
  }

  zones.length = 0;
  zones.push(...left.zones, ...medianZones, ...right.zones);
  zones.forEach((z, i) => {
    z.index = i;
    z.side = z.from + z.to < -1e-9 ? -1 : z.from + z.to > 1e-9 ? 1 : z.from < 0 ? -1 : z.from > 0 ? 1 : 0;
  });

  const laneCentres: LaneRef[] = [];
  for (const z of zones) {
    if (z.kind === 'lane') laneCentres.push({ u: (z.from + z.to) / 2, dir: z.dir === 0 ? 1 : z.dir, index: z.laneIndex, width: z.to - z.from, kind: z.kind });
  }
  laneCentres.sort((a, b) => a.u - b.u);
  const find = (kind: ZoneKind, side: -1 | 1) => zones.find((z) => z.kind === kind && (side < 0 ? z.to <= 0 : z.from >= 0)) ?? null;

  return {
    spec,
    zones,
    bounds: {
      left: Math.min(zones[0].from, zones[0].to),
      right: Math.max(zones[zones.length - 1].from, zones[zones.length - 1].to),
    },
    kerb: { left: left.kerbOffset, right: right.kerbOffset },
    median: spec.median === 'none' ? null : { from: -medianHalf, to: medianHalf, type: spec.median, topY: medianTopY, kerbTop: medianKerbTop },
    laneCentres,
    carriagewayWidth: right.kerbOffset - left.kerbOffset,
    footways: { left: find('footway', -1), right: find('footway', 1) },
    verges: { left: find('verge', -1), right: find('verge', 1) },
    parking: { left: find('parking', -1), right: find('parking', 1) },
    bike: { left: find('bike', -1), right: find('bike', 1) },
    bus: { left: find('bus', -1), right: find('bus', 1) },
    envelope: Math.max(Math.abs(Math.min(zones[0].from, zones[0].to)), Math.abs(Math.max(zones[zones.length - 1].from, zones[zones.length - 1].to))),
  };
}

/**
 * Sample the profile at a station. Zone order and sample count never change, so
 * every road stays a regular quad grid even as bays, tapers and dropped kerbs
 * reshape the section.
 */
export function profileAt(cp: CompiledProfile, mods: ProfileMods = {}, _quality?: string): ProfileSample[] {
  const spec = cp.spec;
  const crownScale = mods.crownScale ?? 1;
  void crownScale;
  const rampLength = Math.max(0.05, mods.rampLength ?? 1.2);
  const dropL = clamp(mods.kerbDropLeft ?? 0, 0, 1);
  const dropR = clamp(mods.kerbDropRight ?? 0, 0, 1);
  const addL = Math.max(0, mods.widthAddLeft ?? 0);
  const addR = Math.max(0, mods.widthAddRight ?? 0);
  const footL = Math.max(0, mods.footwayAddLeft ?? 0);
  const footR = Math.max(0, mods.footwayAddRight ?? 0);
  const shift = mods.shift ?? 0;
  const kerbTopY = -spec.crown + KERB_PROFILES[spec.kerb].upstand;
  const droppedY = -spec.crown + 0.02;

  // thresholds where widening starts (outer carriageway edge, then the kerb line)
  const cwEdgeL = cp.kerb.left + cp.spec.gutterWidth;
  const cwEdgeR = cp.kerb.right - cp.spec.gutterWidth;

  const baseMedianHalf = (Math.abs(cp.median?.from ?? 0) + Math.abs(cp.median?.to ?? 0)) / 2;
  const newMedianHalf = mods.medianWidth !== undefined ? Math.max(0, mods.medianWidth) / 2 : baseMedianHalf;
  const medianDelta = newMedianHalf - baseMedianHalf;
  const medianBand = baseMedianHalf + (cp.median?.kerbTop ?? 0) + 1e-6;

  const mapU = (u: number): number => {
    let d = Math.abs(u);
    const sgn = u < 0 ? -1 : 1;
    if (Math.abs(medianDelta) > 1e-9 && baseMedianHalf > 0.01 && d <= medianBand) {
      d = (d * newMedianHalf) / baseMedianHalf;
      return sgn * d + shift;
    }
    if (Math.abs(medianDelta) > 1e-9 && baseMedianHalf > 0.01 && d > medianBand) {
      d += medianDelta;
    }
    if (u < 0) {
      if (addL > 0 && d > Math.abs(cwEdgeL) + 1e-6) d += addL;
      if (footL > 0 && d > Math.abs(cp.kerb.left) + 1e-6) d += footL;
    } else {
      if (addR > 0 && d > Math.abs(cwEdgeR) + 1e-6) d += addR;
      if (footR > 0 && d > Math.abs(cp.kerb.right) + 1e-6) d += footR;
    }
    return sgn * d + shift;
  };

  const samples: ProfileSample[] = [];
  // One sample per zone boundary, always — the sample count is structural. Zones
  // that collapse (a median nose, a dropped kerb) simply produce coincident
  // samples; the mesh builder drops the zero-area quads that follow.
  const emit = (u: number, y: number, zone: ZoneSpan) => {
    samples.push({ u, y, zones: [zone.index], index: samples.length, kind: zone.kind, upstand: zone.upstand, mat: zone.material });
  };

  for (const z of cp.zones) {
    const segments = Math.max(1, z.density);
    const drop = z.side < 0 ? dropL : dropR;
    const from = mapU(z.from);
    const to = mapU(z.to);
    for (let i = 0; i <= segments; i++) {
      const t = segments === 0 ? 0 : i / segments;
      const u = lerp(from, to, t);
      let y = lerp(z.yFrom, z.yTo, t);
      if (crownScale !== 1 && z.kind !== 'median-top' && z.kind !== 'kerb-top' && z.kind !== 'kerb-face') y *= crownScale;
      if (drop > 0) {
        if (z.kind === 'kerb-top' || z.kind === 'kerb-face') {
          y = lerp(y, droppedY, drop);
        } else if (z.kind === 'footway') {
          const distFromKerb = Math.abs(u - from);
          const ramp = 1 - smoothstep(distFromKerb / rampLength);
          const target = droppedY + (distFromKerb * 1.2) / 100;
          y = lerp(y, lerp(y, target, ramp), drop);
        } else if (z.kind === 'gutter') {
          y = lerp(y, -spec.crown, drop * 0.6);
        }
      }
      if (z.kind === 'kerb-top' && drop > 0) y = lerp(kerbTopY, droppedY, drop);
      emit(u, y, z);
    }
  }
  samples.forEach((s, i) => (s.index = i));
  return samples;
}

/** Finished height at a lateral offset within a sampled profile. */
export function profileHeightAt(samples: ProfileSample[], u: number): number {
  if (!samples.length) return 0;
  if (u <= samples[0].u) return samples[0].y;
  for (let i = 1; i < samples.length; i++) {
    const a = samples[i - 1];
    const b = samples[i];
    if (u <= b.u + 1e-9) {
      if (Math.abs(b.u - a.u) < 1e-9) return Math.max(a.y, b.y);
      return lerp(a.y, b.y, clamp((u - a.u) / (b.u - a.u), 0, 1));
    }
  }
  return samples[samples.length - 1].y;
}

export function profileUpstandAt(cp: CompiledProfile, u: number): number {
  let best = 0;
  for (const z of cp.zones) {
    if (z.kind !== 'kerb-top' && z.kind !== 'kerb-face') continue;
    if (Math.abs((z.from + z.to) / 2 - u) < 0.75) best = Math.max(best, z.upstand);
  }
  return best;
}

export function profileOutline(samples: ProfileSample[]): V2[] {
  return samples.map((s) => [s.u, s.y] as V2);
}

export const totalWidth = (cp: CompiledProfile): number => cp.bounds.right - cp.bounds.left;

export function lanesInDirection(cp: CompiledProfile, dir: 1 | -1): LaneRef[] {
  return cp.laneCentres.filter((l) => l.dir === dir);
}

/** Zone index lookup by kind + side (authoring helpers). */
export function zoneOf(cp: CompiledProfile, kind: ZoneKind, side: -1 | 1): ZoneSpan | null {
  return cp.zones.find((z) => z.kind === kind && (side < 0 ? z.to <= 0 : z.from >= 0)) ?? null;
}

export const ZONE_IS_PAVED: Record<ZoneKind, boolean> = {
  verge: false,
  planting: false,
  footway: true,
  'kerb-top': true,
  'kerb-face': true,
  gutter: true,
  lane: true,
  shoulder: true,
  parking: true,
  bike: true,
  bus: true,
  'median-flush': true,
  'median-top': true,
  'median-face': true,
  barrier: true,
  'tram-track': true,
  wall: true,
  'rail-bed': false,
  'rail-sleeper': false,
  'rail-rail': false,
};
