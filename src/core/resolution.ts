// ---------------------------------------------------------------------------
// Frontier core / resolution guard
// Every operation that can *blur* a distance field declares the feature size it
// needs, in metres. The guard converts that to voxels at the current grid
// spacing and BLOCKS the node (amber badge + disabled cook) when the feature
// would land under MIN_FEATURE_VOXELS — eroding a 3-voxel river at 64 res is
// how SDF terrain turns to mush, so we refuse instead of guessing.
// ---------------------------------------------------------------------------

export const MIN_FEATURE_VOXELS = 3;

export interface ResolutionContext {
  domainSize: [number, number, number];
  res: number;          // grid resolution along X/Z
  aspectY: number;
}

export interface ResolutionRequirement {
  /** metres of the smallest feature this op needs to resolve */
  featureMetres: number;
  label: string;
}

export interface ResolutionVerdict {
  ok: boolean;
  h: number;                       // current voxel size (m)
  featureVoxels: number;           // feature / h
  requiredRes: number;             // smallest res that satisfies the guard
  requirement: ResolutionRequirement;
  reason: string;
}

export function voxelSize(ctx: ResolutionContext): number {
  return ctx.domainSize[0] / ctx.res;
}

export function evaluateResolution(ctx: ResolutionContext, req: ResolutionRequirement): ResolutionVerdict {
  const h = voxelSize(ctx);
  const featureVoxels = req.featureMetres / h;
  const ok = featureVoxels >= MIN_FEATURE_VOXELS;
  const requiredRes = Math.ceil((MIN_FEATURE_VOXELS * ctx.domainSize[0]) / Math.max(1e-6, req.featureMetres));
  return {
    ok,
    h,
    featureVoxels,
    requiredRes,
    requirement: req,
    reason: ok
      ? `${req.label}: ${featureVoxels.toFixed(1)} voxels across (>= ${MIN_FEATURE_VOXELS})`
      : `${req.label} is ${featureVoxels.toFixed(1)} voxels across at res ${ctx.res} — under ${MIN_FEATURE_VOXELS}, the operator would blur the SDF. Raise resolution to >= ${requiredRes} or widen the feature.`,
  };
}

/** what a node chain needs = the strictest requirement among enabled nodes */
export function strictest(verdicts: ResolutionVerdict[]): ResolutionVerdict | null {
  let worst: ResolutionVerdict | null = null;
  for (const v of verdicts) {
    if (!worst || v.featureVoxels < worst.featureVoxels) worst = v;
  }
  return worst;
}

export interface BudgetInfo {
  voxels: number;
  bandVoxelsEstimate: number;
  megabytes: number;      // ground + void + scratch
  h: number;
}

export function budget(ctx: ResolutionContext, aspectY: number): BudgetInfo {
  const nx = ctx.res, nz = ctx.res;
  const ny = Math.max(32, Math.round(ctx.res * aspectY));
  const voxels = nx * ny * nz;
  const h = ctx.domainSize[0] / ctx.res;
  // surface shell estimate: nx*nz columns * band thickness both sides
  const bandVoxelsEstimate = Math.min(voxels, nx * nz * 6 * 4);
  const megabytes = (voxels * 4 * 3) / (1024 * 1024);
  return { voxels, bandVoxelsEstimate, megabytes, h };
}
