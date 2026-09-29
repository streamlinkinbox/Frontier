// SDF Resolution & World-Size Guardrail Manager
// Prevents low-resolution numerical diffusion (blurring), sub-voxel aliasing, and Eikonal degradation
// by evaluating physical voxel size (Δx = worldSize / sdfResolution) against operation feature scales.

export interface SDFDomainConfig {
  worldSize: number;          // Physical terrain width/depth in meters (e.g., 512m)
  maxHeight: number;          // Physical vertical height in meters (e.g., 210m)
  sdfResolution: number;      // Horizontal 3D SDF grid resolution N (default 256 -> 256 x 256 x 128)
  strictLowResBlock: boolean; // Block erosion/stencil ops when Δx is too coarse to avoid blurring
  blockSkirt?: boolean;       // Close the 4 outer sides of the 3D SDF volume as a geological cutaway block
}

export interface NodeResolutionReport {
  nodeId: string;
  allowed: boolean;
  status: 'optimal' | 'clamped' | 'blocked_low_res';
  voxelSizeMeters: number;
  nyquistMinFeatureMeters: number;
  requestedFeatureMeters: number;
  effectiveSubsteps: number;
  message: string;
}

/**
 * Computes vertical SDF resolution (ny = sdfRes / 2, e.g. 256 -> 128)
 * so a 512m x 512m x 256m volume has isotropic 1:1:1 cubic voxels (Δx = Δy = Δz ≈ 2.0m).
 */
export function getVerticalResolution(sdfRes: number): number {
  return Math.max(24, Math.round(sdfRes * 0.5));
}

export function evaluateResolutionForOperation(
  domain: SDFDomainConfig,
  nodeId: string,
  operationType: 'generator' | 'hydraulic' | 'alluvial' | 'wind' | 'cliff' | 'cave' | 'satmap',
  requestedFeatureSizeMeters: number
): NodeResolutionReport {
  const voxelSize = domain.worldSize / Math.max(8, domain.sdfResolution);
  const nyquistMin = voxelSize * 2.0;

  // Texture / SatMap operations are point-wise color lookups and never blur geometry
  if (operationType === 'satmap') {
    return {
      nodeId,
      allowed: true,
      status: 'optimal',
      voxelSizeMeters: voxelSize,
      nyquistMinFeatureMeters: nyquistMin,
      requestedFeatureMeters: requestedFeatureSizeMeters,
      effectiveSubsteps: 1,
      message: `Point-wise SatMap lookup (Δx = ${voxelSize.toFixed(1)}m)`,
    };
  }

  // Generators use analytical evaluation + Nyquist octave clamping (never finite-difference blur)
  if (operationType === 'generator') {
    const isClamped = requestedFeatureSizeMeters < nyquistMin * 2.5;
    return {
      nodeId,
      allowed: true,
      status: isClamped ? 'clamped' : 'optimal',
      voxelSizeMeters: voxelSize,
      nyquistMinFeatureMeters: nyquistMin,
      requestedFeatureMeters: Math.max(requestedFeatureSizeMeters, nyquistMin),
      effectiveSubsteps: 1,
      message: isClamped
        ? `Nyquist Guard: High-freq octaves < ${nyquistMin.toFixed(1)}m attenuated`
        : `Nyquist OK (Δx = ${voxelSize.toFixed(1)}m, λ_min = ${nyquistMin.toFixed(1)}m)`,
    };
  }

  // Stencil / PDE / Advection Operations (Hydraulic, Alluvial, Wind, Cliff, 3D Cave)
  const ratio = domain.worldSize / Math.max(1, domain.sdfResolution);
  const featureTooSmall = requestedFeatureSizeMeters < nyquistMin * 0.95;
  const gridTooCoarseForPDE = domain.sdfResolution < 44 || ratio > 14.0;

  if (gridTooCoarseForPDE && domain.strictLowResBlock) {
    return {
      nodeId,
      allowed: false,
      status: 'blocked_low_res',
      voxelSizeMeters: voxelSize,
      nyquistMinFeatureMeters: nyquistMin,
      requestedFeatureMeters: requestedFeatureSizeMeters,
      effectiveSubsteps: 0,
      message: `GUARD BLOCKED: Voxel Δx=${voxelSize.toFixed(1)}m (Res ${domain.sdfResolution}×${domain.sdfResolution}×${getVerticalResolution(domain.sdfResolution)} on ${domain.worldSize}m) is too low — erosion skipped to prevent SDF blurring.`,
    };
  }

  if (featureTooSmall || gridTooCoarseForPDE) {
    return {
      nodeId,
      allowed: true,
      status: 'clamped',
      voxelSizeMeters: voxelSize,
      nyquistMinFeatureMeters: nyquistMin,
      requestedFeatureMeters: Math.max(requestedFeatureSizeMeters, nyquistMin * 1.15),
      effectiveSubsteps: Math.max(1, Math.round(domain.sdfResolution / 32)),
      message: `Flux-Limited Clamp: Feature scaled to ${Math.max(requestedFeatureSizeMeters, nyquistMin * 1.15).toFixed(1)}m ≥ 2Δx (${nyquistMin.toFixed(1)}m) to prevent blur`,
    };
  }

  return {
    nodeId,
    allowed: true,
    status: 'optimal',
    voxelSizeMeters: voxelSize,
    nyquistMinFeatureMeters: nyquistMin,
    requestedFeatureMeters: requestedFeatureSizeMeters,
    effectiveSubsteps: Math.max(1, Math.round(domain.sdfResolution / 24)),
    message: `Sharp SDF Stencil OK (Δx = ${voxelSize.toFixed(1)}m ≤ Feature ${requestedFeatureSizeMeters.toFixed(1)}m)`,
  };
}

/**
 * Minmod Flux Limiter:
 * Prevents numerical oscillation and artificial diffusion (blurring) near sharp cliffs and canyon edges!
 */
export function minmod(a: number, b: number): number {
  if (a * b <= 0) return 0;
  return Math.abs(a) < Math.abs(b) ? a : b;
}

/**
 * Fast Narrow-Band 3D Eikonal SDF Regularization Pass
 * Regularizes |∇φ| ≈ 1 and removes voxel-grid staircasing inside recessed overhang alcoves
 * while preserving sharp horizontal strata ledges.
 * Uses optional column narrow-band bounds (bandMinY, bandMaxY) for 10x speed on 256x256x128 grids.
 */
export function normalizeSDFNearSurface(
  sdf: Float32Array,
  nx: number,
  ny: number,
  nz: number,
  voxelSize: number,
  bandMinY?: Uint16Array,
  bandMaxY?: Uint16Array
): void {
  const band = voxelSize * 3.6;
  const copy = new Float32Array(sdf);
  const strideY = nx;
  const strideZ = nx * ny;
  const inv2Voxel = 1.0 / (2.0 * voxelSize);

  for (let z = 1; z < nz - 1; z++) {
    const zOff = z * strideZ;
    const zRow2D = z * nx;

    for (let x = 1; x < nx - 1; x++) {
      const idx2D = zRow2D + x;
      const yStart = bandMinY ? Math.max(1, bandMinY[idx2D] - 1) : 1;
      const yEnd = bandMaxY ? Math.min(ny - 2, bandMaxY[idx2D] + 1) : ny - 2;

      for (let y = yStart; y <= yEnd; y++) {
        const idx = zOff + y * strideY + x;
        const phi = copy[idx];
        if (Math.abs(phi) > band) continue;

        const phiL = copy[idx - 1];
        const phiR = copy[idx + 1];
        const phiD = copy[idx - strideY];
        const phiU = copy[idx + strideY];
        const phiB = copy[idx - strideZ];
        const phiF = copy[idx + strideZ];

        const dx = (phiR - phiL) * inv2Voxel;
        const dy = (phiU - phiD) * inv2Voxel;
        const dz = (phiF - phiB) * inv2Voxel;
        const gradMag = Math.sqrt(dx * dx + dy * dy + dz * dz);

        if (gradMag > 0.15) {
          // Project toward unit Eikonal gradient |∇φ| = 1 + horizontal XZ harmonic smoothing
          // inside recessed undercuts to eliminate Marching Cubes grid waffle
          const normalized = phi / Math.max(0.48, Math.min(2.1, gradMag));
          const horizAvg = 0.25 * (phiL + phiR + phiB + phiF) + 0.5 * phiD + 0.5 * phiU - phi;
          const eikonalPhi = phi * 0.34 + normalized * 0.66;
          sdf[idx] = eikonalPhi * 0.86 + horizAvg * 0.14;
        }
      }
    }
  }
}
