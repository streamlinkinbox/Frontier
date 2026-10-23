/**
 * One-shot generation pipeline:
 * skeleton → environment → roots → welded mesh → validation → GPU buffers.
 * Used by the worker, the tests and the CLI.
 */

import { TreeParams, DEFAULT_ROOTS, isGrass } from './params';
import { buildSkeleton, Skeleton } from './skeleton';
import { buildMesh, MesherStats } from './mesher';
import { validateTopology, TopologyReport } from './validate';
import { toGpuBuffers, GpuBuffers } from './export';
import { LeafMesh, QuadMesh } from './mesh';
import { Environment, ObstacleMeshData, DEFAULT_ENVIRONMENT } from '../env/environment';
import { buildRoots } from './roots';
import { GrassMesher, GrassStats } from '../plant/grassMesher';
import { DEFAULT_GRASS } from '../plant/grassParams';

export interface Timings {
  skeleton: number;
  roots: number;
  mesh: number;
  validate: number;
  buffers: number;
  total: number;
}

export interface SkeletonSummary {
  stems: number;
  stemsPerLevel: number[];
  leaves: number;
  height: number;
  treeScale: number;
  /** Primary roots grown / root stems in total (primaries + laterals + fork children). */
  primaryRoots: number;
  rootStems: number;
  obstacles: number;
}

export interface GenerateResult {
  /** Tree skeleton (null for grasses). */
  skeleton: Skeleton | null;
  environment: Environment;
  mesh: QuadMesh;
  leaves: LeafMesh;
  obstacles: ObstacleMeshData[];
  report: TopologyReport;
  stats: MesherStats;
  buffers: GpuBuffers;
  timings: Timings;
  summary: SkeletonSummary;
  /** How far the plant continues below the ground (metres). */
  groundDepth: number;
  /** Grass mesher statistics (grasses only). */
  grass?: GrassStats;
}

const now = (): number => (typeof performance !== 'undefined' ? performance.now() : Date.now());

/** Fill in fields older parameter sets (saved presets, tests) may lack. */
export function completeParams(params: TreeParams): TreeParams {
  const p = params as Partial<TreeParams> & { botany: TreeParams['botany']; mesh: TreeParams['mesh'] };
  if (!p.roots) p.roots = { ...DEFAULT_ROOTS };
  if (!p.environment) p.environment = { ...DEFAULT_ENVIRONMENT, scatter: { ...DEFAULT_ENVIRONMENT.scatter }, obstacles: [] };
  if (p.mesh.rootRadialSegments === undefined) p.mesh.rootRadialSegments = 12;
  return p as TreeParams;
}

export function generateTree(params: TreeParams, options: { validate?: boolean; obstacleMeshes?: boolean } = {}): GenerateResult {
  completeParams(params);
  if (isGrass(params)) return generateGrass(params, options);
  const t0 = now();
  const skeleton = buildSkeleton(params);
  const t1 = now();
  const environment = new Environment(params.environment);
  const primaries = buildRoots(skeleton, environment);
  const t2 = now();
  const { mesh, leaves, stats } = buildMesh(skeleton);
  const t3 = now();
  const report = options.validate === false ? emptyReport(mesh) : validateTopology(mesh);
  const t4 = now();
  const buffers = toGpuBuffers(mesh);
  const obstacles = options.obstacleMeshes === false ? [] : environment.meshAll();
  const t5 = now();
  return {
    skeleton,
    environment,
    mesh,
    leaves,
    obstacles,
    report,
    stats,
    buffers,
    timings: { skeleton: t1 - t0, roots: t2 - t1, mesh: t3 - t2, validate: t4 - t3, buffers: t5 - t4, total: t5 - t0 },
    summary: {
      stems: skeleton.stems.length,
      stemsPerLevel: skeleton.stemsPerLevel,
      leaves: leaves.count,
      height: skeleton.height,
      treeScale: skeleton.treeScale,
      primaryRoots: primaries.filter((r) => !r.dropped).length,
      rootStems: stats.rootStems,
      obstacles: environment.count,
    },
    groundDepth: skeleton.groundDepth,
  };
}

/**
 * Grass pipeline: crown + tillers welded by the grass mesher → validation →
 * GPU buffers. The result has the same shape as a tree's so the worker, the
 * exporters and the UI treat both alike; "stems" are organs here.
 */
function generateGrass(params: TreeParams, options: { validate?: boolean; obstacleMeshes?: boolean }): GenerateResult {
  const g = params.grass ?? { ...DEFAULT_GRASS };
  params.grass = g;
  const t0 = now();
  const built = new GrassMesher(g, params.seed).build();
  const t1 = now();
  const environment = new Environment(params.environment);
  const report = options.validate === false ? emptyReport(built.mesh) : validateTopology(built.mesh);
  const t2 = now();
  const buffers = toGpuBuffers(built.mesh);
  const obstacles = options.obstacleMeshes === false ? [] : environment.meshAll();
  const t3 = now();
  const gs = built.stats;
  const stats: MesherStats = {
    stems: gs.organs,
    droppedStems: gs.dropped,
    dropReasons: gs.dropReasons,
    junctions: gs.junctions,
    forks: 0,
    maxDepth: 3,
    rootStems: 0,
    droppedRoots: 0,
  };
  return {
    skeleton: null,
    environment,
    mesh: built.mesh,
    leaves: new LeafMesh(),
    obstacles,
    report,
    stats,
    buffers,
    timings: { skeleton: 0, roots: 0, mesh: t1 - t0, validate: t2 - t1, buffers: t3 - t2, total: t3 - t0 },
    summary: {
      stems: gs.organs,
      stemsPerLevel: [...gs.perLevel],
      leaves: 0,
      height: built.height,
      treeScale: built.height,
      primaryRoots: 0,
      rootStems: 0,
      obstacles: environment.count,
    },
    groundDepth: built.groundDepth,
    grass: gs,
  };
}

function emptyReport(mesh: QuadMesh): TopologyReport {
  const F = mesh.quadCount + mesh.triCount;
  return {
    vertices: mesh.vertexCount,
    edges: 0,
    faces: F,
    quads: mesh.quadCount,
    tris: mesh.triCount,
    quadRatio: F ? mesh.quadCount / F : 1,
    boundaryEdges: 0,
    nonManifoldEdges: 0,
    inconsistentEdges: 0,
    isolatedVertices: 0,
    eulerCharacteristic: 0,
    components: 0,
    closed: false,
    manifold: false,
    poles: 0,
    valenceHistogram: {},
    degenerateFaces: 0,
    minEdgeLength: 0,
  };
}
