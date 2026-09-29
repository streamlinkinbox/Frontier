import { SDFDomainConfig, getVerticalResolution, NodeResolutionReport } from './resolutionGuard';

export interface Active3DStrataConfig {
  enabled: boolean;
  strataFrequency: number;
  strataStrength: number;
  cliffSteepness: number;
  undercut3D: number;
  faultOffset: number;     // Vertical strata displacement across tectonic fault (meters)
  faultAngle: number;      // Horizontal fault strike angle in degrees
  faultDip: number;        // 3D inclination/dip of fault plane
}

export interface ArchSplineControl {
  id: string;
  name: string;
  enabled: boolean;
  // 2D Plan-view Quadratic Bezier endpoints & control point in meters [-256..+256]
  x0: number;
  z0: number;
  xc: number;
  zc: number;
  x1: number;
  z1: number;
  // Elevation profile along normalized span u in [0..1]
  pierHeight0: number;      // Left buttress height above base (m)
  crownHeight: number;      // Peak extrados arch crest height above base (m)
  pierHeight1: number;      // Right buttress height above base (m)
  crownPosU: number;        // Horizontal position of arch apex along u [0.25..0.75]
  // Window opening (intrados vault) parameters
  windowWidthFrac: number;  // Fraction of span open to the sky [0.28..0.82]
  windowCenterU: number;    // Center of window opening along u [0.25..0.75]
  windowApexHeight: number; // Vault ceiling height above base (m)
  sillHeight: number;       // Bottom sill / saddle floor height of window above base (m)
  bridgeThickness: number;  // Minimum rock thickness of the arch ribbon at crown (m)
  finHalfWidth: number;     // Half-thickness of the sandstone fin at the span (m)
  buttressRadius: number;   // Radius of the anchoring buttress piers at ends (m)
  alcoveFlare: number;      // Conchoidal outward alcove flaring on both faces [0..1]
}

export interface SDFTerrainVolume {
  domain: SDFDomainConfig;
  nx: number;
  ny: number;
  nz: number;
  voxelSizeXZ: number;
  voxelSizeY: number;

  // 3D Signed Distance Field: φ(x, y, z) < 0 inside solid rock/terrain, > 0 in air/caves
  // Indexed as: z * (nx * ny) + y * nx + x
  sdfGrid: Float32Array;

  // Narrow-band bounds per (x, z) column [yMin, yMax] for fast 3D Marching Cubes
  bandMinY: Uint16Array;
  bandMaxY: Uint16Array;

  // Coupled Stratigraphy & Hydrological 2D Grids (nx * nz)
  bedrockHeight: Float32Array;   // Base solid rock elevation (meters)
  sedimentHeight: Float32Array;  // Alluvial / fluvial loose sediment thickness (meters)
  talusHeight: Float32Array;     // Thermal cliff scree / talus thickness (meters)
  riverFlow: Float32Array;       // Focused river channel water mask [0..1]
  riverDepth: Float32Array;      // Carved river channel depth (meters)
  cliffMask: Float32Array;       // Exposed vertical cliff & strata intensity [0..1]
  windMask: Float32Array;        // Aeolian abrasion & dune ripple intensity [0..1]
  curvatureGrid: Float32Array;   // Ridge convexity (+) vs ravine concavity (-) [-1..1]
  caveMask2D: Float32Array;      // Surface projection of 3D caves/arches [0..1]

  // 2D Surface RGB color map from SatMaps (nx * nz * 3)
  colorMap: Float32Array;
  activeSatMapId: string;
  strataConfig: Active3DStrataConfig;

  // 3D CSG Monolith & Butte distance fields (nx * nz)
  has3DMonoliths: boolean;
  has3DArches: boolean;
  archSplines: ArchSplineControl[];
  monolithDist: Float32Array;    // Horizontal signed distance to monolith cliff wall (meters)
  monolithSummitH: Float32Array; // Local monolith turret summit height (meters)

  // Resolution guard reports per node
  reports: Record<string, NodeResolutionReport>;
}

export function createEmptySDFVolume(domain: SDFDomainConfig): SDFTerrainVolume {
  const nx = Math.max(24, Math.min(256, Math.round(domain.sdfResolution)));
  const nz = nx;
  const ny = getVerticalResolution(nx);

  const voxelSizeXZ = domain.worldSize / (nx - 1);
  const voxelSizeY = domain.maxHeight / (ny - 1);

  const total3D = nx * ny * nz;
  const total2D = nx * nz;

  const sdfGrid = new Float32Array(total3D);
  const bandMinY = new Uint16Array(total2D);
  const bandMaxY = new Uint16Array(total2D);
  const bedrockHeight = new Float32Array(total2D);
  const sedimentHeight = new Float32Array(total2D);
  const talusHeight = new Float32Array(total2D);
  const riverFlow = new Float32Array(total2D);
  const riverDepth = new Float32Array(total2D);
  const cliffMask = new Float32Array(total2D);
  const windMask = new Float32Array(total2D);
  const curvatureGrid = new Float32Array(total2D);
  const caveMask2D = new Float32Array(total2D);
  const colorMap = new Float32Array(total2D * 3);
  const monolithDist = new Float32Array(total2D);
  const monolithSummitH = new Float32Array(total2D);

  for (let i = 0; i < total2D; i++) {
    bedrockHeight[i] = 12.0;
    monolithDist[i] = 999.0;
    monolithSummitH[i] = 12.0;
    colorMap[i * 3 + 0] = 0.44;
    colorMap[i * 3 + 1] = 0.36;
    colorMap[i * 3 + 2] = 0.29;
  }

  const vol: SDFTerrainVolume = {
    domain,
    nx,
    ny,
    nz,
    voxelSizeXZ,
    voxelSizeY,
    sdfGrid,
    bandMinY,
    bandMaxY,
    bedrockHeight,
    sedimentHeight,
    talusHeight,
    riverFlow,
    riverDepth,
    cliffMask,
    windMask,
    curvatureGrid,
    caveMask2D,
    colorMap,
    activeSatMapId: 'monument_valley',
    strataConfig: {
      enabled: false,
      strataFrequency: 11,
      strataStrength: 0.72,
      cliffSteepness: 0.80,
      undercut3D: 0.65,
      faultOffset: 14.0,
      faultAngle: 32.0,
      faultDip: 0.28,
    },
    has3DMonoliths: false,
    has3DArches: false,
    archSplines: [],
    monolithDist,
    monolithSummitH,
    reports: {},
  };

  syncHeightfieldToSDF(vol);
  return vol;
}

export function cloneSDFVolume(src: SDFTerrainVolume): SDFTerrainVolume {
  return {
    domain: { ...src.domain },
    nx: src.nx,
    ny: src.ny,
    nz: src.nz,
    voxelSizeXZ: src.voxelSizeXZ,
    voxelSizeY: src.voxelSizeY,
    sdfGrid: new Float32Array(src.sdfGrid),
    bandMinY: new Uint16Array(src.bandMinY),
    bandMaxY: new Uint16Array(src.bandMaxY),
    bedrockHeight: new Float32Array(src.bedrockHeight),
    sedimentHeight: new Float32Array(src.sedimentHeight),
    talusHeight: new Float32Array(src.talusHeight),
    riverFlow: new Float32Array(src.riverFlow),
    riverDepth: new Float32Array(src.riverDepth),
    cliffMask: new Float32Array(src.cliffMask),
    windMask: new Float32Array(src.windMask),
    curvatureGrid: new Float32Array(src.curvatureGrid),
    caveMask2D: new Float32Array(src.caveMask2D),
    colorMap: new Float32Array(src.colorMap),
    activeSatMapId: src.activeSatMapId,
    strataConfig: { ...src.strataConfig },
    has3DMonoliths: src.has3DMonoliths,
    has3DArches: src.has3DArches,
    archSplines: src.archSplines.map((s) => ({ ...s })),
    monolithDist: new Float32Array(src.monolithDist),
    monolithSummitH: new Float32Array(src.monolithSummitH),
    reports: { ...src.reports },
  };
}

export function getTotalSurfaceHeight(vol: SDFTerrainVolume, idx2D: number): number {
  return Math.max(
    4.0,
    Math.min(
      vol.domain.maxHeight * 0.94,
      vol.bedrockHeight[idx2D] + vol.sedimentHeight[idx2D] + vol.talusHeight[idx2D]
    )
  );
}

export function snapshotSurfaceHeights(vol: SDFTerrainVolume): Float32Array {
  const total2D = vol.nx * vol.nz;
  const out = new Float32Array(total2D);
  for (let i = 0; i < total2D; i++) {
    out[i] = getTotalSurfaceHeight(vol, i);
  }
  return out;
}

/**
 * Updates surface curvature, slope, cliffMask, and narrow-band bounds,
 * and applies incremental ΔH(x, z) to the 3D SDF volume WITHOUT erasing
 * existing 3D volumetric strata overhangs, 3D cliff undercuts, or 3D caves!
 */
export function applyHeightDeltaToSDF(
  vol: SDFTerrainVolume,
  prevHeights: Float32Array
): void {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, sdfGrid, bandMinY, bandMaxY } = vol;
  const strideY = nx;
  const strideZ = nx * ny;

  for (let z = 0; z < nz; z++) {
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;
    const zPrev = Math.max(0, z - 1) * nx;
    const zNext = Math.min(nz - 1, z + 1) * nx;

    for (let x = 0; x < nx; x++) {
      const idx2D = zOff2D + x;
      const hNew = getTotalSurfaceHeight(vol, idx2D);
      const dH = hNew - prevHeights[idx2D];

      const xPrev = Math.max(0, x - 1);
      const xNext = Math.min(nx - 1, x + 1);
      const hL = getTotalSurfaceHeight(vol, zOff2D + xPrev);
      const hR = getTotalSurfaceHeight(vol, zOff2D + xNext);
      const hD = getTotalSurfaceHeight(vol, zPrev + x);
      const hU = getTotalSurfaceHeight(vol, zNext + x);

      const dhdx = (hR - hL) / (2.0 * voxelSizeXZ);
      const dhdz = (hU - hD) / (2.0 * voxelSizeXZ);
      const slope = Math.sqrt(dhdx * dhdx + dhdz * dhdz);

      const laplacian = (hNew - 0.25 * (hL + hR + hD + hU)) / voxelSizeXZ;
      vol.curvatureGrid[idx2D] = Math.max(-1.0, Math.min(1.0, laplacian * 2.2));

      const talusCover = Math.min(1.0, (vol.talusHeight[idx2D] + vol.sedimentHeight[idx2D] * 0.5) / 3.2);
      vol.cliffMask[idx2D] = Math.max(
        vol.cliffMask[idx2D] * 0.65,
        Math.max(0, Math.min(1, ((slope - 0.42) / 0.88) * (1.0 - talusCover * 0.72)))
      );

      const minH = Math.min(hNew, hL, hR, hD, hU);
      const maxH = Math.max(hNew, hL, hR, hD, hU);
      const yMin = Math.max(0, Math.floor(minH / voxelSizeY) - 4);
      const yMax = Math.min(ny - 2, Math.ceil(maxH / voxelSizeY) + 4);
      bandMinY[idx2D] = Math.min(bandMinY[idx2D] || yMin, yMin);
      bandMaxY[idx2D] = Math.max(bandMaxY[idx2D] || yMax, yMax);

      if (Math.abs(dH) > 1e-4) {
        // Protect 3D CSG monolith walls from 2D column height shifts that would create needle spikes
        if (vol.has3DMonoliths && vol.monolithDist[idx2D] < 4.0) {
          const clampedDH = Math.max(-2.5, Math.min(2.5, dH));
          for (let y = 1; y < ny; y++) {
            const idx3D = zOff3D + y * strideY + x;
            sdfGrid[idx3D] -= clampedDH * 0.25;
          }
        } else {
          for (let y = 1; y < ny; y++) {
            const idx3D = zOff3D + y * strideY + x;
            sdfGrid[idx3D] -= dH;
          }
        }
        sdfGrid[zOff3D + x] = Math.min(sdfGrid[zOff3D + x], -voxelSizeY);
      }
    }
  }
}

/**
 * Initializes the base 3D Signed Distance Field φ(x, y, z) = worldY - H(x, z)
 * (used only by initial base generator nodes).
 */
export function syncHeightfieldToSDF(vol: SDFTerrainVolume): void {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, sdfGrid, bandMinY, bandMaxY } = vol;
  const strideY = nx;
  const strideZ = nx * ny;

  for (let z = 0; z < nz; z++) {
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;
    const zPrev = Math.max(0, z - 1) * nx;
    const zNext = Math.min(nz - 1, z + 1) * nx;

    for (let x = 0; x < nx; x++) {
      const idx2D = zOff2D + x;
      const h = getTotalSurfaceHeight(vol, idx2D);

      const xPrev = Math.max(0, x - 1);
      const xNext = Math.min(nx - 1, x + 1);
      const hL = getTotalSurfaceHeight(vol, zOff2D + xPrev);
      const hR = getTotalSurfaceHeight(vol, zOff2D + xNext);
      const hD = getTotalSurfaceHeight(vol, zPrev + x);
      const hU = getTotalSurfaceHeight(vol, zNext + x);

      const dhdx = (hR - hL) / (2.0 * voxelSizeXZ);
      const dhdz = (hU - hD) / (2.0 * voxelSizeXZ);
      const slope = Math.sqrt(dhdx * dhdx + dhdz * dhdz);

      const laplacian = (h - 0.25 * (hL + hR + hD + hU)) / voxelSizeXZ;
      vol.curvatureGrid[idx2D] = Math.max(-1.0, Math.min(1.0, laplacian * 2.2));

      const talusCover = Math.min(1.0, vol.talusHeight[idx2D] / 3.0);
      vol.cliffMask[idx2D] = Math.max(
        0,
        Math.min(1, ((slope - 0.42) / 0.88) * (1.0 - talusCover * 0.65))
      );

      const minH = Math.min(h, hL, hR, hD, hU);
      const maxH = Math.max(h, hL, hR, hD, hU);
      const yMin = Math.max(0, Math.floor(minH / voxelSizeY) - 4);
      const yMax = Math.min(ny - 2, Math.ceil(maxH / voxelSizeY) + 4);
      bandMinY[idx2D] = yMin;
      bandMaxY[idx2D] = yMax;

      for (let y = 0; y < ny; y++) {
        const worldY = y * voxelSizeY;
        const idx3D = zOff3D + y * strideY + x;
        let phi = worldY - h;
        if (y === 0) {
          phi = Math.min(phi, -voxelSizeY);
        }
        sdfGrid[idx3D] = phi;
      }
    }
  }
}
