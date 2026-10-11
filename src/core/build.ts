/**
 * Scene builder: document → geometry.
 *
 * Order of operations matters and is deliberately explicit:
 *   1. node handles (auto tangents + junction angle constraint)
 *   2. corridors (quad-friendly chains) and their arms
 *   3. superelevation with junction runout
 *   4. junction solve (kerb returns, mouths, rings)
 *   5. surface meshes: corridors, junctions, corner sectors, turning heads
 *   6. decal sheet registration for markings
 */

import { MeshBuilder } from './mesh';
import { compileProfile, type CompiledProfile, type ZoneKind } from './profile';
import {
  applyBanking,
  buildArms,
  buildCorridors,
  resolveHandles,
  type Arm,
  type Corridor,
  type EndState,
  type SplineDiagnostic,
} from './spline';
import { buildCornerSectors, buildJunctionSurface, solveJunction, type JunctionMeshContext, type JunctionSolve } from './junction';
import { buildCorridorMesh, buildTurningHead, corridorStations } from './road';
import type { MaterialDef, QualityProfile, RoadDoc, SurfaceKind } from './model';
import { MAT_INDEX } from './mesh';
import type { Id } from './model';
import type { V2, V3 } from './math';

export interface BuildStats {
  corridors: number;
  junctions: number;
  vertices: number;
  quads: number;
  triangles: number;
  sheetQuads: number;
  milliseconds: number;
}

export interface BuildResult {
  builder: MeshBuilder;
  sheetQuads: Array<[V3, V3, V3, V3]>;
  corridors: Corridor[];
  junctions: JunctionSolve[];
  armsByNode: Map<Id, Arm[]>;
  cpByEdge: Map<string, CompiledProfile>;
  endStates: Map<string, EndState>;
  diagnostics: SplineDiagnostic[];
  stats: BuildStats;
  /** material id per document material id, for per-vertex lookup */
  matIndex: Map<string, number>;
}

export const SURFACE_OF_MATERIAL = (m: MaterialDef): SurfaceKind => m.surface;

export interface BuildOptions {
  /** stop after a stage, for isolating geometry while debugging */
  stopAfter?: 'corridors' | 'junctions' | 'all';
  /** skip corridor meshes entirely (inspect junction topology alone) */
  junctionOnly?: boolean;
}

export function buildScene(doc: RoadDoc, quality: QualityProfile, opts: BuildOptions = {}): BuildResult {
  const t0 = Date.now();
  const diagnostics: SplineDiagnostic[] = [];
  const builder = new MeshBuilder();
  const sheetQuads: Array<[V3, V3, V3, V3]> = [];

  const { handles } = resolveHandles(doc, diagnostics);
  const { corridors, incident } = buildCorridors(doc, quality, diagnostics, handles);
  const armsByNode = buildArms(corridors);

  // compiled profiles per edge + material index map (document materials override base surfaces)
  const cpByEdge = new Map<string, CompiledProfile>();
  const matIndex = new Map<string, number>();
  for (const m of doc.materials) matIndex.set(m.id, MAT_INDEX[m.surface] ?? 0);
  for (const edge of doc.edges) cpByEdge.set(edge.id, compileProfile(edge.spec));

  const matOf = (s: SurfaceKind): number => MAT_INDEX[s] ?? 0;

  // superelevation: relax to zero approaching every junction node
  for (const corridor of corridors) {
    const zones: Array<[number, number]> = [];
    for (const st of corridor.nodeStations) zones.push([st - 35, st + 35]);
    applyBanking(corridor, zones);
  }

  // ---- junctions ----------------------------------------------------------
  const junctions: JunctionSolve[] = [];
  const endStates = new Map<string, EndState>();
  const junctionByNode = new Map<Id, JunctionSolve>();
  const nodeIndexById = new Map<Id, number>();
  doc.nodes.forEach((n, i) => nodeIndexById.set(n.id, i));
  for (const [nodeId, arms] of armsByNode) {
    if (arms.length < 3) continue;
    const node = doc.nodes.find((n) => n.id === nodeId);
    if (!node) continue;
    const solve = solveJunction(nodeIndexById.get(nodeId) ?? 0, arms, quality, cpByEdge);
    junctions.push(solve);
    junctionByNode.set(nodeId, solve);
    for (const [k, v] of solve.endStates) endStates.set(k, v);
    diagnostics.push(...solve.diagnostics.map((m) => ({ level: 'info' as const, message: m, nodeId })));
    diagnostics.push(...solve.warnings.map((m) => ({ level: 'warn' as const, message: m, nodeId })));
  }

  // ---- surfaces -----------------------------------------------------------
  const ctx: JunctionMeshContext = {
    builder,
    sheetQuads,
    junctionMat: matOf(junctionSurfaceKind(doc, junctions)),
    quality,
    matOf,
  };

  for (const corridor of corridors) {
    if (corridor.hidden || opts.junctionOnly) continue;
    const cp = cpByEdge.get(corridor.edges[0].id);
    if (!cp) continue;
    const startArm = findArm(armsByNode, corridor, 'start');
    const endArm = findArm(armsByNode, corridor, 'end');
    const corridorEnds: EndState[] = [];
    const s0 = endStates.get(`${corridor.id}|start`);
    const s1 = endStates.get(`${corridor.id}|end`);
    if (s0) corridorEnds.push(s0);
    if (s1) corridorEnds.push(s1);
    // only build the corridor when it has usable length between its mouths
    const lo = startArm ? startArm.corridorStation((startArm.stationLeft + startArm.stationRight) / 2) : 0;
    const hi = endArm ? endArm.corridorStation((endArm.stationLeft + endArm.stationRight) / 2) : corridor.length;
    if (Math.abs(hi - lo) < 0.6) continue;
    const stats = buildCorridorMesh(corridor, cp, startArm, endArm, corridorEnds, ctx, cpByEdge);
    if (!stats.rows) {
      diagnostics.push({ level: 'warn', edgeId: corridor.edges[0].id, message: `${corridor.name}: corridor too short to build (mouths overlap).` });
    }
    if (!startArm && corridor.length > 8 && corridor.spec.class !== 'footpath') {
      buildTurningHead(corridor, cp, 'start', ctx);
    }
    if (!endArm && corridor.length > 8 && corridor.spec.class !== 'footpath') {
      buildTurningHead(corridor, cp, 'end', ctx);
    }
    void corridorStations;
  }

  const stop = opts.stopAfter ?? 'all';
  for (const solve of junctions) {
    if (stop === 'corridors') break;
    buildJunctionSurface(solve, ctx);
    if (stop !== 'junctions') buildCornerSectors(solve, ctx);
  }

  const packed = builder.pack();
  void packed;
  const stats: BuildStats = {
    corridors: corridors.length,
    junctions: junctions.length,
    vertices: builder.vertexCount,
    quads: builder.quadCount,
    triangles: builder.triCount,
    sheetQuads: sheetQuads.length,
    milliseconds: Date.now() - t0,
  };

  return {
    builder,
    sheetQuads,
    corridors,
    junctions,
    armsByNode,
    cpByEdge,
    endStates,
    diagnostics,
    stats,
    matIndex,
  };
}

function findArm(armsByNode: Map<Id, Arm[]>, corridor: Corridor, end: 'start' | 'end'): JunctionSolve['arms'][number] | null {
  const nodeId = end === 'start' ? corridor.start.nodeId : corridor.end.nodeId;
  const arms = armsByNode.get(nodeId);
  if (!arms || arms.length < 3) return null;
  const arm = arms.find((a) => a.corridor.id === corridor.id && a.end === end);
  if (!arm) return null;
  return {
    arm,
    index: -1,
    bearing: 0,
    outDir: arm.dirOut,
    rightDir: [arm.dirOut[1], -arm.dirOut[0]],
    kerb: { left: 0, right: 0 },
    nodeStation: arm.nodeStation,
    end: arm.end,
    stationLeft: 0,
    stationRight: 0,
    floor: 0,
    mouthOffsets: [],
    mouthProfile: [],
    ringStart: -1,
    ringEnd: -1,
    roadOffset: (u: number) => u,
    flipOffset: (u: number) => u,
    corridorStation: (outward: number) => (arm.end === 'start' ? arm.nodeStation + outward : arm.nodeStation - outward),
    outwardNormal: (side: 'left' | 'right') => ([side === 'right' ? arm.dirOut[1] : -arm.dirOut[1], side === 'right' ? -arm.dirOut[0] : arm.dirOut[0]] as V2),
  };
}

function junctionSurfaceKind(doc: RoadDoc, junctions: JunctionSolve[]): SurfaceKind {
  void doc;
  void junctions;
  return 'asphalt';
}

/** Human-readable zone label, used by the UI status bar. */
export const ZONE_LABEL: Record<ZoneKind, string> = {
  verge: 'Verge',
  planting: 'Planting',
  footway: 'Footway',
  'kerb-top': 'Kerb top',
  'kerb-face': 'Kerb face',
  gutter: 'Channel',
  lane: 'Traffic lane',
  shoulder: 'Shoulder',
  parking: 'Parking bay',
  bike: 'Cycle lane',
  bus: 'Bus lane',
  'median-flush': 'Painted median',
  'median-top': 'Median island',
  'median-face': 'Median kerb',
  barrier: 'Barrier',
  'tram-track': 'Tram track',
  wall: 'Retaining wall',
  'rail-bed': 'Ballast',
  'rail-sleeper': 'Sleepers',
  'rail-rail': 'Rails',
};
