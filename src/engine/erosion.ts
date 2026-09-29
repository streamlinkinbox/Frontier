import { SeededNoise } from './noise';
import {
  evaluateResolutionForOperation,
  minmod,
  normalizeSDFNearSurface,
} from './resolutionGuard';
import {
  SDFTerrainVolume,
  Active3DStrataConfig,
  getTotalSurfaceHeight,
  snapshotSurfaceHeights,
  applyHeightDeltaToSDF,
} from './terrainState';

export interface CliffStrataParams {
  cliffSteepness: number;     // Escarpment sharpening [0..1]
  strataFrequency: number;    // Scale multiplier for stratigraphic column
  strataStrength: number;     // XY Push-Out strength [0..1]
  talusReposeAngle: number;   // Scree angle of repose in degrees (e.g., 34°)
  talusRate: number;          // Amount of talus scree deposited at cliff base [0..1]
  undercut3D: number;         // XY Pull-In / Undercut recess strength [0..1]
  faultOffset?: number;       // Vertical tectonic fault displacement in meters (e.g., 14m)
  faultAngle?: number;        // Horizontal fault strike angle in degrees (e.g., 32°)
  faultDip?: number;          // 3D inclination/dip of fault plane (e.g., 0.28)
}

// Irregular Stratigraphic Column Definition (16 distinct geological rock units)
// Relative thicknesses vary by up to 11x: massive 30m+ monolith cliffs next to thin 2.5m shale slots!
const STRATA_REL_THICKNESS: ReadonlyArray<number> = [
  1.8,  // 0: Basal conglomerate slope
  0.32, // 1: Thin deep-recessed shale seam (pulls IN in XY)
  2.95, // 2: Massive wide Navajo sandstone face (pushed OUT in XY, minimal internal striping)
  0.28, // 3: Thin undercut mudstone notch (cuts early laterally)
  0.42, // 4: Sharp cantilevered limestone ledge (pushed OUT strongly in XY)
  1.55, // 5: Mixed cross-bedded aeolian unit (diagonal cross-strata + interfingered lenses)
  0.30, // 6: Thin friable siltstone slot (pulled IN in XY)
  0.36, // 7: Thin hard caprock rib (pushed OUT in XY)
  2.65, // 8: Wide massive vertical cliff formation (bold XY buttress protrusions)
  0.34, // 9: Deep recessed alcove seam (pulled IN in XY, pinches out laterally)
  1.35, // 10: Mixed laminated & nodular breccia bed
  0.26, // 11: Razor-thin shale parting
  2.40, // 12: Upper massive caprock escarpment (strong XY overhang)
  0.40, // 13: Recessed bench seam
  1.60, // 14: Upper mixed cross-bedded sandstone
  1.90, // 15: Summit caprock formation
];

// Base XY displacement character per unit:
// +1 = Pushed OUT in XY (protruding ledge/buttress)
// -1 = Pulled IN in XY (recessed slot/alcove)
//  0 = Mixed / Cross-bedded / Laterally alternating XY push & pull
const STRATA_XY_TYPE: ReadonlyArray<number> = [
  +0.4, // 0
  -1.0, // 1: Deep inward slot
  +0.85,// 2: Massive outward face
  -0.95,// 3: Deep undercut notch
  +1.1, // 4: Strong outward ledge
   0.0, // 5: Mixed cross-bedded
  -0.9, // 6: Inward slot
  +0.95,// 7: Outward rib
  +0.75,// 8: Massive outward cliff
  -1.1, // 9: Deep alcove undercut
   0.0, // 10: Mixed laminated
  -0.85,// 11: Thin inward parting
  +1.15,// 12: Strong overhanging caprock
  -0.8, // 13: Recessed bench
   0.0, // 14: Mixed cross-bedded
  +0.7, // 15: Summit caprock
];

// Base mineral color index [0..1] in SatMap cliff ramp per unit
const STRATA_COLOR_TONE: ReadonlyArray<number> = [
  0.22, 0.08, 0.64, 0.12, 0.86, 0.48, 0.15, 0.78,
  0.58, 0.06, 0.42, 0.18, 0.88, 0.14, 0.52, 0.92,
];

const CUMULATIVE_STRATA_NORM: Float32Array = (() => {
  const n = STRATA_REL_THICKNESS.length;
  const out = new Float32Array(n + 1);
  let sum = 0;
  for (let i = 0; i < n; i++) sum += STRATA_REL_THICKNESS[i];
  let acc = 0;
  out[0] = 0;
  for (let i = 0; i < n; i++) {
    acc += STRATA_REL_THICKNESS[i] / sum;
    out[i + 1] = acc;
  }
  return out;
})();

// Precomputed orthogonal golden-angle phase weights for fast 2D boundary lens pinch-outs
const BOUNDARY_COS: Float32Array = new Float32Array(16);
const BOUNDARY_SIN: Float32Array = new Float32Array(16);
for (let k = 0; k < 16; k++) {
  const angle = (k + 1) * 2.3999632;
  BOUNDARY_COS[k] = Math.cos(angle);
  BOUNDARY_SIN[k] = Math.sin(angle);
}

function smoothstep(edge0: number, edge1: number, x: number): number {
  const t = Math.max(0, Math.min(1, (x - edge0) / Math.max(1e-5, edge1 - edge0)));
  return t * t * (3.0 - 2.0 * t);
}

/**
 * Evaluates our Non-Uniform Stratigraphic Column with:
 * 1) Radically different band widths (from 2.5m thin slots to 35m wide massive rock faces).
 * 2) True Lateral Pinch-Outs ("some can cut sooner"): boundary elevations shift laterally in (x,z)
 *    so individual bands taper to zero thickness and terminate partway across the cliff!
 * 3) Per-Band & Along-Strike Signed XY Push-Out (+) vs Pull-In (-) in 3D space.
 * 4) Mixed bands with internal diagonal cross-bedding & lateral rock-type interfingering.
 * 5) 3D Dipping Tectonic Fault Line that vertically offsets strata layers across the fault scar.
 */
export function sample3DStrataProfile(
  wx: number,
  wy: number,
  wz: number,
  maxHeight: number,
  strataFrequency: number,
  noise: SeededNoise,
  strataConfig?: Partial<Active3DStrataConfig>
): {
  bedId: number;
  bedThicknessMeters: number;
  xyDisplacement: number;   // Signed horizontal XY push (+) vs inward recess (-)
  caprockOutward: number;   // Positive on protruding ledges (pushes rock OUT in XY)
  shaleRecess: number;      // Positive in recessed slots (carves rock IN in XY)
  stairStepDeltaH: number;  // Non-uniform plateau/escarpment step shaping
  strataColorValue: number; // Per-band varied 3D color lookup [0..1]
  faultScarMask: number;    // [0..1] fracture intensity right along the 3D fault plane
} {
  const numUnits = STRATA_REL_THICKNESS.length;

  // 1. 3D Dipping Tectonic Fault Plane + Regional Dip + 3D Fold
  const faultAngleRad = (((strataConfig?.faultAngle ?? 32.0) * Math.PI) / 180.0);
  const faultDip = strataConfig?.faultDip ?? 0.28;
  const faultThrow = strataConfig?.faultOffset ?? 14.0;

  const nA = noise.simplex2D(wx * 0.0082, wz * 0.0082);
  const nB = noise.simplex2D(wx * 0.0082 + 73.1, wz * 0.0082 - 41.9);

  // Curved 3D fault plane distance: d_fault = 0 along the inclined fault surface
  const faultCoord =
    wx * Math.cos(faultAngleRad) +
    wz * Math.sin(faultAngleRad) +
    (wy - 95.0) * faultDip +
    nA * 22.0;

  const faultTanh = Math.tanh(faultCoord * 0.095);
  const faultStepY = faultTanh * faultThrow;
  const faultScarMask = Math.max(0, 1.0 - faultTanh * faultTanh); // sech^2(faultCoord) peak at fault line

  const dip = wx * 0.036 - wz * 0.028;
  // Multi-block tectonic warp & secondary cross-fault so different monoliths sit at different stratigraphic offsets!
  const blockOffset =
    noise.simplex2D(wx * 0.0048 - 29.4, wz * 0.0048 + 53.7) * 26.0 +
    Math.tanh((wx * -0.55 + wz * 0.83 + nB * 28.0) * 0.065) * (faultThrow * 0.65);
  const fold3D = (nA * 0.65 + nB * 0.35) * 14.5 + blockOffset;

  const effectiveY = wy + dip + faultStepY + fold3D;
  const colScale = Math.max(0.65, Math.min(1.6, (strataFrequency || 11) / 11.0));
  const totalColHeight = maxHeight / colScale;

  // 2. Compute laterally-warped boundary elevations H_k(x, z) for each unit
  // When boundary k+1 drops to or below boundary k, unit k PINCHES OUT (cuts off early!)
  let prevBoundaryY = -120.0;
  let activeBed = numUnits - 1;
  let bedBotY = 0.0;
  let bedTopY = totalColHeight;

  for (let k = 0; k < numUnits; k++) {
    const rawTopNorm = CUMULATIVE_STRATA_NORM[k + 1];
    let topY = rawTopNorm * totalColHeight;

    if (k < numUnits - 1) {
      const lensWave = BOUNDARY_COS[k] * nA + BOUNDARY_SIN[k] * nB;
      // Strong lateral pinch-out waves so beds cut off and merge into massive sandstone!
      const pinchAmp = STRATA_REL_THICKNESS[k] < 0.6 ? 16.5 : 11.5;
      topY += lensWave * pinchAmp;
    }

    const clampedTopY = Math.max(prevBoundaryY, topY);
    if (effectiveY >= prevBoundaryY && effectiveY <= clampedTopY && clampedTopY - prevBoundaryY > 0.15) {
      activeBed = k;
      bedBotY = prevBoundaryY;
      bedTopY = clampedTopY;
      break;
    }
    prevBoundaryY = clampedTopY;
  }

  const bedThickness = Math.max(0.5, bedTopY - bedBotY);
  const u = Math.max(0, Math.min(1, (effectiveY - bedBotY) / bedThickness)); // [0..1] inside active bed

  const baseXYType = STRATA_XY_TYPE[activeBed];
  const relWidth = STRATA_REL_THICKNESS[activeBed];

  // Smooth fade when a band pinches out laterally so pinched seams taper cleanly
  const pinchOutFade = smoothstep(0.65, 4.2, bedThickness);

  // 3D Massive Sandstone Facies Mask:
  // Where massiveMask is high, horizontal seams fade out into a sheer, un-banded eolian cliff face!
  const massiveRaw = noise.simplex3D(wx * 0.0095 - 41.2, wy * 0.0075, wz * 0.0095 + 18.6);
  const massiveFaceAtten = 1.0 - 0.78 * smoothstep(0.12, 0.52, massiveRaw);

  // Lateral modulation along the cliff strike so even a single band varies in XY push/pull
  const strikeNoise = noise.simplex3D(
    wx * 0.014 + activeBed * 29.1,
    wy * 0.009,
    wz * 0.014 - activeBed * 17.7
  );
  const earlyCutNoise = BOUNDARY_SIN[activeBed] * nA - BOUNDARY_COS[activeBed] * nB;
  const lateralCutMask = smoothstep(-0.18, 0.28, earlyCutNoise) * pinchOutFade * massiveFaceAtten;

  let xyDisp = 0.0;
  let mixedColorMod = 0.0;

  if (Math.abs(baseXYType) < 0.1) {
    // MIXED BAND ("some bands mixed"):
    // Combines tilted aeolian cross-bedding + laterally alternating pushed-out & recessed lenses!
    const crossBedPhase =
      u * 2.6 * Math.PI * 2.0 + (wx * 0.052 - wz * 0.041) * (activeBed % 2 === 0 ? 1 : -1);
    const crossWave = Math.sin(crossBedPhase);
    const lensPushPull = strikeNoise * 1.25;
    xyDisp = (lensPushPull * Math.sin(u * Math.PI) + crossWave * 0.38) * lateralCutMask;
    mixedColorMod = crossWave * 0.09 + strikeNoise * 0.12;
  } else if (baseXYType > 0) {
    // PUSHED-OUT BAND (+XY):
    if (relWidth > 2.0) {
      // Wide massive sandstone monolith: bold blocky outward buttresses + overhanging upper cap
      const verticalButtress = 0.38 + 0.62 * smoothstep(-0.3, 0.45, strikeNoise);
      const capOverhang = u > 0.64 ? Math.pow((u - 0.64) / 0.36, 0.75) * 0.58 : 0.0;
      const baseUndercut = u < 0.12 ? -Math.sin((u / 0.12) * Math.PI) * 0.48 : 0.0;
      const bodyPush = Math.pow(Math.sin(u * Math.PI), 0.48) * 0.88;
      xyDisp = (bodyPush + capOverhang + baseUndercut) * baseXYType * verticalButtress * lateralCutMask;
      mixedColorMod = strikeNoise * 0.08;
    } else {
      // Sharp protruding cornice / caprock ledge pushed strongly OUT in XY!
      const ledgeProfile = Math.pow(u, 0.48) * Math.pow(1.0 - u, 0.38) * 1.75;
      xyDisp = ledgeProfile * baseXYType * (0.5 + 0.55 * Math.max(0, strikeNoise + 0.3)) * lateralCutMask;
    }
  } else {
    // PULLED-IN / RECESSED BAND (-XY):
    // Soft shale/mudstone slot carved deeply INWARD into the cliff face in XY!
    const slotProfile = Math.pow(Math.sin(u * Math.PI), 0.68);
    const alcoveDepthMod = 0.55 + 0.6 * Math.max(0, strikeNoise + 0.35);
    xyDisp = baseXYType * slotProfile * alcoveDepthMod * lateralCutMask;
  }

  // Carve a subtle 3D tectonic fault-line crevice right along the fault plane
  if (faultScarMask > 0.05 && Math.abs(faultThrow) > 1.0) {
    xyDisp -= faultScarMask * 0.55;
  }

  const caprockOutward = Math.max(0.0, xyDisp);
  const shaleRecess = Math.max(0.0, -xyDisp);

  // Structural bench step shaping proportional to actual variable bed thickness
  const smoothU = u * u * (3.0 - 2.0 * u);
  const sharpU =
    u < 0.5 ? 0.5 * Math.pow(2.0 * u, 2.4) : 1.0 - 0.5 * Math.pow(2.0 * (1.0 - u), 2.4);
  const stairStepDeltaH = (sharpU - smoothU) * Math.min(20.0, bedThickness * 0.5) * lateralCutMask;

  // Blend per-bed tone with 3D regional mineral facies & vertical desert varnish streaks
  // so no two monoliths share the same horizontal color barcode!
  const regionalMineralShift =
    noise.simplex3D(wx * 0.0072 + 19.4, wy * 0.0055, wz * 0.0072 - 61.8) * 0.28;
  const verticalVarnishCurtain =
    wy > 52.0
      ? Math.max(0.0, noise.simplex2D(wx * 0.048, wz * 0.048) - 0.15) *
        Math.min(1.0, (wy - 52.0) / 75.0) *
        0.26
      : 0.0;
  const baseTone =
    STRATA_COLOR_TONE[activeBed] * (0.35 + 0.65 * lateralCutMask) +
    0.54 * (1.0 - (0.35 + 0.65 * lateralCutMask));

  const strataColorValue = Math.max(
    0.02,
    Math.min(
      0.98,
      baseTone +
        regionalMineralShift +
        xyDisp * 0.14 +
        mixedColorMod -
        verticalVarnishCurtain -
        faultScarMask * 0.18
    )
  );

  return {
    bedId: activeBed,
    bedThicknessMeters: bedThickness,
    xyDisplacement: xyDisp,
    caprockOutward,
    shaleRecess,
    stairStepDeltaH,
    strataColorValue,
    faultScarMask,
  };
}

/**
 * Sculpts True 3D Horizontal XY/XZ Push-Out Ledges and Pull-In Slots into the 3D SDF Volume!
 */
export function applyVolumetric3DStrataToSDF(vol: SDFTerrainVolume): void {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, domain, sdfGrid, bandMinY, bandMaxY, strataConfig } =
    vol;
  if (!strataConfig.enabled) return;

  const noise = new SeededNoise(9012);
  const halfWorld = domain.worldSize * 0.5;
  const strideY = nx;
  const strideZ = nx * ny;

  const strength = strataConfig.strataStrength ?? 0.72;
  const undercut = strataConfig.undercut3D ?? 0.68;
  const freq = strataConfig.strataFrequency || 11;

  for (let z = 2; z < nz - 2; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;

    for (let x = 2; x < nx - 2; x++) {
      const idx2D = zOff2D + x;
      const cliff = vol.cliffMask[idx2D];
      if (cliff < 0.10) continue;

      const wx = x * voxelSizeXZ - halfWorld;
      const surfH = Math.max(getTotalSurfaceHeight(vol, idx2D), vol.monolithSummitH[idx2D]);
      const talusTopY = vol.bedrockHeight[idx2D] + vol.talusHeight[idx2D];

      // For non-monolith heightfield cliffs, attenuate by 2D loose cover;
      // for 3D CSG monoliths, evaluate talus cover in 3D using wy vs talusTopY!
      const looseCover2D = vol.has3DMonoliths
        ? 0.0
        : Math.min(1.0, (vol.talusHeight[idx2D] + vol.sedimentHeight[idx2D] * 0.6) / 3.0);
      const baseExposedMask = cliff * (1.0 - looseCover2D * 0.85);
      if (baseExposedMask < 0.07) continue;

      const yStart = Math.max(2, bandMinY[idx2D] - 2);
      const yEnd = Math.min(ny - 3, bandMaxY[idx2D] + 2);
      bandMinY[idx2D] = yStart;
      bandMaxY[idx2D] = yEnd;

      for (let y = yStart; y <= yEnd; y++) {
        const idx3D = zOff3D + y * strideY + x;
        const phi = sdfGrid[idx3D];
        if (Math.abs(phi) > voxelSizeXZ * 3.8) continue;

        const wy = y * voxelSizeY;
        const minStrataY = vol.has3DMonoliths ? Math.max(18.0, talusTopY + 1.5) : 8.0;
        if (wy < minStrataY || wy > surfH + voxelSizeY * 1.5) continue;

        // 3D elevation fade above the talus apron & below the flat summit caprock rim
        let mask3D = baseExposedMask;
        if (vol.has3DMonoliths) {
          const aboveTalusFade = Math.max(0.0, Math.min(1.0, (wy - talusTopY - 1.5) / 6.0));
          const belowRimFade = Math.max(0.15, Math.min(1.0, (surfH - wy) / 5.5));
          mask3D *= aboveTalusFade * belowRimFade;
        }

        const strata = sample3DStrataProfile(
          wx,
          wy,
          wz,
          domain.maxHeight,
          freq,
          noise,
          strataConfig
        );

        // Strong, smooth 3D XY push-out (Δφ < 0) and XY pull-in recess (Δφ > 0)
        // On 3D natural arches, moderate inward pull so free-spanning arch bridges remain structurally thick
        const archRecessSafety = vol.has3DArches ? 0.58 : 1.0;
        const outwardPushMeters =
          strata.caprockOutward * strength * mask3D * voxelSizeXZ * 2.05;
        const inwardPullMeters =
          strata.shaleRecess * undercut * mask3D * voxelSizeXZ * 2.15 * archRecessSafety;

        const deltaPhi = inwardPullMeters - outwardPushMeters;
        sdfGrid[idx3D] = phi + deltaPhi;
      }
    }
  }

  // 2-Pass Narrow-Band Eikonal Regularization:
  // Eliminates voxel-grid staircasing inside recessed overhang alcoves while keeping ledges sharp!
  normalizeSDFNearSurface(sdfGrid, nx, ny, nz, voxelSizeXZ, bandMinY, bandMaxY);
  normalizeSDFNearSurface(sdfGrid, nx, ny, nz, voxelSizeXZ, bandMinY, bandMaxY);
}

/**
 * Simulates Non-Uniform 3D Stratified Cliffs, Escarpment Benches, 3D Fault Scarps & Smooth Conical Talus Aprons.
 */
export function applyCliffAndStrataErosion(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: CliffStrataParams
): void {
  const { nx, nz, voxelSizeXZ, domain } = vol;

  const report = evaluateResolutionForOperation(
    domain,
    nodeId,
    'cliff',
    voxelSizeXZ * 2.0
  );
  vol.reports[nodeId] = report;
  if (!report.allowed) return;

  vol.strataConfig = {
    enabled: true,
    strataFrequency: params.strataFrequency || 11,
    strataStrength: params.strataStrength ?? 0.72,
    cliffSteepness: params.cliffSteepness ?? 0.80,
    undercut3D: params.undercut3D ?? 0.68,
    faultOffset: params.faultOffset ?? 14.0,
    faultAngle: params.faultAngle ?? 32.0,
    faultDip: params.faultDip ?? 0.28,
  };

  // When 3D CSG Monoliths are active, strata & overhangs are sculpted directly in 3D via applyVolumetric3DStrataToSDF
  // so we never apply 2D heightmap spalling that would fight the 3D CSG monolith distance field!
  if (vol.has3DMonoliths) {
    return;
  }

  const prevHeights = snapshotSurfaceHeights(vol);
  const noise = new SeededNoise(9012);
  const nextBedrock = new Float32Array(vol.bedrockHeight);
  const halfWorld = domain.worldSize * 0.5;
  const strataCount = Math.max(5, params.strataFrequency || 11);

  // 1. Non-Uniform Structural Benches & Escarpment Risers
  for (let z = 1; z < nz - 1; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    for (let x = 1; x < nx - 1; x++) {
      const wx = x * voxelSizeXZ - halfWorld;
      const idx = z * nx + x;
      const h = vol.bedrockHeight[idx];

      const dhdx = (vol.bedrockHeight[idx + 1] - vol.bedrockHeight[idx - 1]) / (2.0 * voxelSizeXZ);
      const dhdz = (vol.bedrockHeight[idx + nx] - vol.bedrockHeight[idx - nx]) / (2.0 * voxelSizeXZ);
      const slope = Math.sqrt(dhdx * dhdx + dhdz * dhdz);

      const cliffWeight = Math.min(1.0, Math.max(0.0, (slope - 0.24) / 0.72));
      if (cliffWeight > 0.01) {
        const strata = sample3DStrataProfile(
          wx,
          h,
          wz,
          domain.maxHeight,
          strataCount,
          noise,
          vol.strataConfig
        );
        const stepBlend = cliffWeight * (params.cliffSteepness ?? 0.8) * (params.strataStrength ?? 0.72);
        nextBedrock[idx] = Math.max(6.0, h + strata.stairStepDeltaH * stepBlend);
      }

      vol.cliffMask[idx] = cliffWeight;
    }
  }
  vol.bedrockHeight.set(nextBedrock);

  // 2. Smooth Thermal Talus / Scree Apron Cascade
  const talusTan = Math.tan(((params.talusReposeAngle || 34) * Math.PI) / 180.0);
  const cliffTan = Math.tan((53.0 * Math.PI) / 180.0);
  const maxTalusDrop = talusTan * voxelSizeXZ;

  const d8 = [
    { dx: -1, dz: 0, dist: 1.0 },
    { dx: 1, dz: 0, dist: 1.0 },
    { dx: 0, dz: -1, dist: 1.0 },
    { dx: 0, dz: 1, dist: 1.0 },
    { dx: -1, dz: -1, dist: Math.SQRT2 },
    { dx: 1, dz: -1, dist: Math.SQRT2 },
    { dx: -1, dz: 1, dist: Math.SQRT2 },
    { dx: 1, dz: 1, dist: Math.SQRT2 },
  ];

  const downW = new Float32Array(8);
  for (let pass = 0; pass < 3; pass++) {
    for (let z = 2; z < nz - 2; z++) {
      for (let x = 2; x < nx - 2; x++) {
        const idx = z * nx + x;
        const h = vol.bedrockHeight[idx];
        let maxSlope = 0;
        let downWeightSum = 0;

        for (let k = 0; k < 8; k++) {
          const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
          const s = (h - vol.bedrockHeight[nIdx]) / (voxelSizeXZ * d8[k].dist);
          if (s > 0) {
            if (s > maxSlope) maxSlope = s;
            downW[k] = s;
            downWeightSum += s;
          } else {
            downW[k] = 0;
          }
        }

        if (maxSlope > cliffTan && downWeightSum > 0) {
          const spall = Math.min(
            1.0,
            (maxSlope - cliffTan) * voxelSizeXZ * 0.12 * (params.talusRate ?? 0.72)
          );
          vol.bedrockHeight[idx] -= spall * 0.35;
          for (let k = 0; k < 8; k++) {
            if (downW[k] > 0) {
              const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
              vol.talusHeight[nIdx] += spall * 0.72 * (downW[k] / downWeightSum);
            }
          }
        }
      }
    }
  }

  // Cascade & smooth talus downslope into clean 34° conical scree aprons
  const ex = new Float32Array(8);
  const relaxPasses = 20;
  for (let pass = 0; pass < relaxPasses; pass++) {
    for (let z = 1; z < nz - 1; z++) {
      for (let x = 1; x < nx - 1; x++) {
        const idx = z * nx + x;
        const avail = vol.talusHeight[idx];
        if (avail <= 0.005) continue;

        const hHere = getTotalSurfaceHeight(vol, idx);
        let totalExcess = 0;

        for (let k = 0; k < 8; k++) {
          const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
          const diff = hHere - getTotalSurfaceHeight(vol, nIdx);
          const allowed = maxTalusDrop * d8[k].dist;
          if (diff > allowed) {
            ex[k] = diff - allowed;
            totalExcess += ex[k];
          } else {
            ex[k] = 0;
          }
        }

        if (totalExcess > 0) {
          const move = Math.min(avail * 0.65, totalExcess * 0.38);
          vol.talusHeight[idx] -= move;
          for (let k = 0; k < 8; k++) {
            if (ex[k] > 0) {
              const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
              vol.talusHeight[nIdx] += move * (ex[k] / totalExcess);
            }
          }
        }
      }
    }
  }

  // 3-pass 3x3 isotropic Gaussian smoothing on talus blanket so scree cones are silky smooth
  const smoothTalus = new Float32Array(vol.talusHeight.length);
  for (let sPass = 0; sPass < 3; sPass++) {
    smoothTalus.set(vol.talusHeight);
    for (let z = 1; z < nz - 1; z++) {
      for (let x = 1; x < nx - 1; x++) {
        const idx = z * nx + x;
        if (vol.talusHeight[idx] <= 0.005) continue;
        smoothTalus[idx] =
          vol.talusHeight[idx] * 0.36 +
          (vol.talusHeight[idx - 1] +
            vol.talusHeight[idx + 1] +
            vol.talusHeight[idx - nx] +
            vol.talusHeight[idx + nx]) *
            0.11 +
          (vol.talusHeight[idx - nx - 1] +
            vol.talusHeight[idx - nx + 1] +
            vol.talusHeight[idx + nx - 1] +
            vol.talusHeight[idx + nx + 1]) *
            0.05;
      }
    }
    vol.talusHeight.set(smoothTalus);
  }

  applyHeightDeltaToSDF(vol, prevHeights);
}

export interface HydraulicRiverParams {
  rainIntensity: number;
  riverCarveDepth: number;
  channelThreshold: number;
  channelWidth: number;
  dropletIterations: number;
  riverBankUndercut3D: number;
}

export function applyHydraulicAndRiverErosion(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: HydraulicRiverParams
): void {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, domain } = vol;
  const total2D = nx * nz;

  const report = evaluateResolutionForOperation(
    domain,
    nodeId,
    'hydraulic',
    Math.max(params.channelWidth || 12, voxelSizeXZ * 1.8)
  );
  vol.reports[nodeId] = report;
  if (!report.allowed) return;

  const prevHeights = snapshotSurfaceHeights(vol);

  const brushOffsets: Array<{ dx: number; dz: number; weight: number }> = [];
  let weightSum = 0;
  const brushRadius = 2;
  for (let dz = -brushRadius; dz <= brushRadius; dz++) {
    for (let dx = -brushRadius; dx <= brushRadius; dx++) {
      const dist = Math.sqrt(dx * dx + dz * dz);
      if (dist <= 2.25) {
        const w = Math.max(0, 1.0 - dist / 2.35);
        brushOffsets.push({ dx, dz, weight: w });
        weightSum += w;
      }
    }
  }
  for (const b of brushOffsets) b.weight /= weightSum;

  const numDroplets = Math.min(42000, Math.round(total2D * 0.95 * (params.rainIntensity || 1.0)));
  const inertia = 0.12;
  const sedimentCapacityFactor = 4.0;
  const minSedimentCapacity = 0.01;
  const erodeSpeed = 0.24;
  const depositSpeed = 0.22;
  const evaporateSpeed = 0.022;
  const gravity = 4.0;
  const maxDropletSteps = 56;

  let rngState = 133791;
  const rand01 = () => {
    rngState = (rngState * 1664525 + 1013904223) | 0;
    return (rngState >>> 0) / 4294967296.0;
  };

  for (let drop = 0; drop < numDroplets; drop++) {
    let posX = 3.0 + rand01() * (nx - 7.0);
    let posZ = 3.0 + rand01() * (nz - 7.0);
    let dirX = 0.0;
    let dirZ = 0.0;
    let speed = 1.0;
    let water = 1.0;
    let sediment = 0.0;

    for (let step = 0; step < maxDropletSteps; step++) {
      const nodeX = Math.floor(posX);
      const nodeZ = Math.floor(posZ);
      if (nodeX < 3 || nodeX >= nx - 4 || nodeZ < 3 || nodeZ >= nz - 4) break;

      const cellOffsetX = posX - nodeX;
      const cellOffsetZ = posZ - nodeZ;

      const i00 = nodeZ * nx + nodeX;
      const i10 = i00 + 1;
      const i01 = i00 + nx;
      const i11 = i01 + 1;

      const h00 = getTotalSurfaceHeight(vol, i00);
      const h10 = getTotalSurfaceHeight(vol, i10);
      const h01 = getTotalSurfaceHeight(vol, i01);
      const h11 = getTotalSurfaceHeight(vol, i11);

      const gradX = (h10 - h00) * (1 - cellOffsetZ) + (h11 - h01) * cellOffsetZ;
      const gradZ = (h01 - h00) * (1 - cellOffsetX) + (h11 - h10) * cellOffsetX;
      const heightOld =
        h00 * (1 - cellOffsetX) * (1 - cellOffsetZ) +
        h10 * cellOffsetX * (1 - cellOffsetZ) +
        h01 * (1 - cellOffsetX) * cellOffsetZ +
        h11 * cellOffsetX * cellOffsetZ;

      dirX = dirX * inertia - gradX * (1 - inertia);
      dirZ = dirZ * inertia - gradZ * (1 - inertia);

      const len = Math.sqrt(dirX * dirX + dirZ * dirZ);
      if (len < 1e-5) {
        const angle = rand01() * Math.PI * 2.0;
        dirX = Math.cos(angle);
        dirZ = Math.sin(angle);
      } else {
        dirX /= len;
        dirZ /= len;
      }

      posX += dirX;
      posZ += dirZ;

      if (posX < 3 || posX >= nx - 4 || posZ < 3 || posZ >= nz - 4) break;

      const newNodeX = Math.floor(posX);
      const newNodeZ = Math.floor(posZ);
      const nOffX = posX - newNodeX;
      const nOffZ = posZ - newNodeZ;
      const ni00 = newNodeZ * nx + newNodeX;
      const heightNew =
        getTotalSurfaceHeight(vol, ni00) * (1 - nOffX) * (1 - nOffZ) +
        getTotalSurfaceHeight(vol, ni00 + 1) * nOffX * (1 - nOffZ) +
        getTotalSurfaceHeight(vol, ni00 + nx) * (1 - nOffX) * nOffZ +
        getTotalSurfaceHeight(vol, ni00 + nx + 1) * nOffX * nOffZ;

      const deltaHeight = heightNew - heightOld;
      const sedimentCapacity = Math.max(
        -deltaHeight * speed * water * sedimentCapacityFactor,
        minSedimentCapacity
      );

      if (sediment > sedimentCapacity || deltaHeight > 0) {
        const amountToDeposit = Math.min(
          0.45,
          deltaHeight > 0
            ? Math.min(deltaHeight, sediment)
            : (sediment - sedimentCapacity) * depositSpeed
        );
        sediment -= amountToDeposit;

        const w00 = (1 - cellOffsetX) * (1 - cellOffsetZ);
        const w10 = cellOffsetX * (1 - cellOffsetZ);
        const w01 = (1 - cellOffsetX) * cellOffsetZ;
        const w11 = cellOffsetX * cellOffsetZ;

        vol.sedimentHeight[i00] += amountToDeposit * w00;
        vol.sedimentHeight[i10] += amountToDeposit * w10;
        vol.sedimentHeight[i01] += amountToDeposit * w01;
        vol.sedimentHeight[i11] += amountToDeposit * w11;
      } else {
        const amountToErode = Math.min(
          (sedimentCapacity - sediment) * erodeSpeed,
          -deltaHeight * 0.9,
          1.2
        );
        sediment += amountToErode;

        for (let b = 0; b < brushOffsets.length; b++) {
          const bo = brushOffsets[b];
          const bIdx = (nodeZ + bo.dz) * nx + (nodeX + bo.dx);
          let rem = amountToErode * bo.weight;
          const sTake = Math.min(vol.sedimentHeight[bIdx], rem);
          vol.sedimentHeight[bIdx] -= sTake;
          rem -= sTake;
          vol.bedrockHeight[bIdx] = Math.max(5.0, vol.bedrockHeight[bIdx] - rem);
        }
      }

      speed = Math.sqrt(Math.max(0.05, speed * speed + Math.max(-2.0, -deltaHeight) * gravity));
      water *= 1.0 - evaporateSpeed;
    }
  }

  // Smooth droplet sediment deposits so droplets never leave 1-voxel spiky bumps at cliff bases
  const smoothSed = new Float32Array(total2D);
  for (let pass = 0; pass < 2; pass++) {
    smoothSed.set(vol.sedimentHeight);
    for (let z = 1; z < nz - 1; z++) {
      for (let x = 1; x < nx - 1; x++) {
        const idx = z * nx + x;
        if (vol.sedimentHeight[idx] <= 0.005) continue;
        smoothSed[idx] =
          vol.sedimentHeight[idx] * 0.4 +
          (vol.sedimentHeight[idx - 1] +
            vol.sedimentHeight[idx + 1] +
            vol.sedimentHeight[idx - nx] +
            vol.sedimentHeight[idx + nx]) *
            0.15;
      }
    }
    vol.sedimentHeight.set(smoothSed);
  }

  // PART 2: Continuous-Angle Sinuous Meandering & Dendritic River Network Solver
  // Eliminates D8 45°/90° mathematical straight lines by combining a domain-warped
  // meander potential with sub-voxel continuous vector-field streamline tracing & tributary capture!
  const meanderNoise = new SeededNoise(8841);
  const elev = new Float32Array(total2D);
  const filled = new Float32Array(total2D);

  for (let z = 0; z < nz; z++) {
    for (let x = 0; x < nx; x++) {
      const idx = z * nx + x;
      const rawH = getTotalSurfaceHeight(vol, idx);
      // Domain-warped multi-scale meander trough potential breaks flat plains into winding thalwegs
      const w1 = meanderNoise.simplex2D(x * 0.018 + 19.3, z * 0.018 - 41.7) * 26.0;
      const w2 = meanderNoise.simplex2D(x * 0.018 - 73.1, z * 0.018 + 53.9) * 26.0;
      const mWave1 = Math.sin((x + w1) * 0.055 + (z + w2) * 0.042);
      const mWave2 = meanderNoise.simplex2D((x + w1 * 0.6) * 0.032, (z + w2 * 0.6) * 0.032);
      // Stronger meander potential on gentler valley floors
      const valleyWeight = 1.0 - Math.min(0.85, vol.cliffMask[idx] * 0.9);
      const meanderOffset = (mWave1 * 1.6 + mWave2 * 2.2) * valleyWeight;

      elev[idx] = rawH + meanderOffset;
      filled[idx] = x === 0 || x === nx - 1 || z === 0 || z === nz - 1 ? elev[idx] : 1e6;
    }
  }

  const d8 = [
    { dx: -1, dz: 0, dist: 1.0 },
    { dx: 1, dz: 0, dist: 1.0 },
    { dx: 0, dz: -1, dist: 1.0 },
    { dx: 0, dz: 1, dist: 1.0 },
    { dx: -1, dz: -1, dist: Math.SQRT2 },
    { dx: 1, dz: -1, dist: Math.SQRT2 },
    { dx: -1, dz: 1, dist: Math.SQRT2 },
    { dx: 1, dz: 1, dist: Math.SQRT2 },
  ];

  // Non-uniform spatially-modulated depression fill (prevents planar 45° octant cones!)
  let changed = true;
  let pass = 0;
  while (changed && pass < 60) {
    changed = false;
    pass++;
    for (let z = 1; z < nz - 1; z++) {
      for (let x = 1; x < nx - 1; x++) {
        const idx = z * nx + x;
        if (filled[idx] <= elev[idx]) continue;
        const localEps =
          0.015 *
          (1.15 + 0.85 * meanderNoise.simplex2D(x * 0.045 + 11.7, z * 0.045 - 23.1));
        let minN = 1e6;
        for (let k = 0; k < 8; k++) {
          const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
          const cand = filled[nIdx] + localEps * d8[k].dist;
          if (cand < minN) minN = cand;
        }
        const nextVal = Math.max(elev[idx], minN);
        if (filled[idx] - nextVal > 1e-4) {
          filled[idx] = nextVal;
          changed = true;
        }
      }
    }
  }

  // Smooth `filled` over 5 isotropic passes so its 2D gradient is continuous in 360° (never quantized to 45°)
  const smoothPot = new Float32Array(filled);
  const tempPot = new Float32Array(total2D);
  for (let sPass = 0; sPass < 5; sPass++) {
    tempPot.set(smoothPot);
    for (let z = 1; z < nz - 1; z++) {
      for (let x = 1; x < nx - 1; x++) {
        const idx = z * nx + x;
        tempPot[idx] =
          smoothPot[idx] * 0.36 +
          (smoothPot[idx - 1] +
            smoothPot[idx + 1] +
            smoothPot[idx - nx] +
            smoothPot[idx + nx]) *
            0.11 +
          (smoothPot[idx - nx - 1] +
            smoothPot[idx - nx + 1] +
            smoothPot[idx + nx - 1] +
            smoothPot[idx + nx + 1]) *
            0.05;
      }
    }
    smoothPot.set(tempPot);
  }

  // Multi-receiver Quinn/Freeman continuous-angle catchment pre-accumulation to locate natural headwaters
  const order = new Int32Array(total2D);
  for (let i = 0; i < total2D; i++) order[i] = i;
  order.sort((a, b) => smoothPot[b] - smoothPot[a]);

  const flowCells = new Float32Array(total2D);
  flowCells.fill(1.0);
  const recW = new Float32Array(8);

  for (let i = 0; i < total2D; i++) {
    const idx = order[i];
    const x = idx % nx;
    const z = (idx / nx) | 0;
    if (x <= 1 || x >= nx - 2 || z <= 1 || z >= nz - 2) continue;

    const hHere = smoothPot[idx];
    let wSum = 0;
    for (let k = 0; k < 8; k++) {
      const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
      const drop = hHere - smoothPot[nIdx];
      if (drop > 0) {
        const w = Math.pow(drop / d8[k].dist, 1.15);
        recW[k] = w;
        wSum += w;
      } else {
        recW[k] = 0;
      }
    }
    if (wSum > 0) {
      const f = flowCells[idx];
      for (let k = 0; k < 8; k++) {
        if (recW[k] > 0) {
          const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
          flowCells[nIdx] += f * (recW[k] / wSum);
        }
      }
    }
  }

  // Select well-spaced headwater sources in high-catchment mountain ravines
  const minHeadwaterCatchment = Math.max(
    42,
    Math.round(total2D * Math.max(0.005, (params.channelThreshold || 0.05) * 0.14))
  );
  const candidates: number[] = [];
  for (let z = 8; z < nz - 8; z += 2) {
    for (let x = 8; x < nx - 8; x += 2) {
      const idx = z * nx + x;
      if (
        flowCells[idx] >= minHeadwaterCatchment &&
        getTotalSurfaceHeight(vol, idx) > domain.maxHeight * 0.16
      ) {
        candidates.push(idx);
      }
    }
  }
  candidates.sort((a, b) => flowCells[b] - flowCells[a]);

  const sources: number[] = [];
  const minSourceSpacingSq = Math.pow(Math.max(9, Math.round(nx * 0.055)), 2);
  for (let i = 0; i < candidates.length && sources.length < 44; i++) {
    const cIdx = candidates[i];
    const cx = cIdx % nx;
    const cz = (cIdx / nx) | 0;
    let tooClose = false;
    for (let j = 0; j < sources.length; j++) {
      const sIdx = sources[j];
      const sx = sIdx % nx;
      const sz = (sIdx / nx) | 0;
      const dSq = (cx - sx) * (cx - sx) + (cz - sz) * (cz - sz);
      if (dSq < minSourceSpacingSq) {
        tooClose = true;
        break;
      }
    }
    if (!tooClose) sources.push(cIdx);
  }

  // Trace Continuous Sub-Voxel Meandering River Streamlines with Tributary Capture!
  const riverDischarge = new Float32Array(total2D);
  const stepLen = 0.46;
  const maxSteps = Math.round(nx * 3.2);

  for (let s = 0; s < sources.length; s++) {
    const startIdx = sources[s];
    let px = (startIdx % nx) + 0.5;
    let pz = ((startIdx / nx) | 0) + 0.5;
    let vx = 0.0;
    let vz = 0.0;
    let arcLen = 0.0;
    const phaseSeed = (s * 1.732 + 0.618) * Math.PI * 2.0;
    let streamWeight = 1.0 + Math.min(2.5, flowCells[startIdx] / (minHeadwaterCatchment * 3.0));

    for (let step = 0; step < maxSteps; step++) {
      const ix = Math.floor(px);
      const iz = Math.floor(pz);
      if (ix < 3 || ix >= nx - 3 || iz < 3 || iz >= nz - 3) break;

      const fx = px - ix;
      const fz = pz - iz;
      const idx00 = iz * nx + ix;

      // Bilinearly interpolated continuous gradient of `smoothPot` (360° angle freedom!)
      const gx00 = (smoothPot[idx00 + 1] - smoothPot[idx00 - 1]) * 0.5;
      const gz00 = (smoothPot[idx00 + nx] - smoothPot[idx00 - nx]) * 0.5;
      const gx10 = (smoothPot[idx00 + 2] - smoothPot[idx00]) * 0.5;
      const gz10 = (smoothPot[idx00 + 1 + nx] - smoothPot[idx00 + 1 - nx]) * 0.5;
      const gx01 = (smoothPot[idx00 + nx + 1] - smoothPot[idx00 + nx - 1]) * 0.5;
      const gz01 = (smoothPot[idx00 + nx * 2] - smoothPot[idx00]) * 0.5;
      const gx11 = (smoothPot[idx00 + nx + 2] - smoothPot[idx00 + nx]) * 0.5;
      const gz11 = (smoothPot[idx00 + nx * 2 + 1] - smoothPot[idx00 + 1]) * 0.5;

      let dx = -((gx00 * (1 - fx) + gx10 * fx) * (1 - fz) + (gx01 * (1 - fx) + gx11 * fx) * fz);
      let dz = -((gz00 * (1 - fx) + gz10 * fx) * (1 - fz) + (gz01 * (1 - fx) + gz11 * fx) * fz);

      const gMag = Math.sqrt(dx * dx + dz * dz);
      if (gMag > 1e-6) {
        dx /= gMag;
        dz /= gMag;
      } else {
        dx = vx || Math.cos(phaseSeed);
        dz = vz || Math.sin(phaseSeed);
      }

      // Perpendicular unit normal for S-curve lateral meandering
      const perpX = -dz;
      const perpZ = dx;

      // Sinuosity increases smoothly as the river enters gentler valleys & plains
      const localSlope = gMag / voxelSizeXZ;
      const valleySinuosity = 0.24 + 0.68 / (1.0 + localSlope * 5.5);

      // Multi-frequency S-curve meander wave + curl-like 2D simplex bend
      const waveBend =
        Math.sin(arcLen * 0.13 + phaseSeed) * 0.58 +
        Math.cos(arcLen * 0.055 - phaseSeed * 0.7) * 0.34 +
        meanderNoise.simplex2D(px * 0.042 + s * 7.1, pz * 0.042 - s * 5.3) * 0.65;

      const lateralPush = waveBend * valleySinuosity;

      // Tributary confluence: gently steer toward any nearby established trunk river channel
      let pullX = 0.0;
      let pullZ = 0.0;
      let maxNearbyQ = riverDischarge[idx00];
      for (let oz = -3; oz <= 3; oz++) {
        for (let ox = -3; ox <= 3; ox++) {
          if (ox === 0 && oz === 0) continue;
          const nQ = riverDischarge[(iz + oz) * nx + (ix + ox)];
          if (nQ > maxNearbyQ * 1.15 && smoothPot[(iz + oz) * nx + (ix + ox)] <= smoothPot[idx00] + 0.8) {
            maxNearbyQ = nQ;
            const dLen = Math.sqrt(ox * ox + oz * oz);
            pullX = ox / dLen;
            pullZ = oz / dLen;
          }
        }
      }

      const targetX = dx + perpX * lateralPush + pullX * 0.55;
      const targetZ = dz + perpZ * lateralPush + pullZ * 0.55;
      const tLen = Math.sqrt(targetX * targetX + targetZ * targetZ) || 1.0;

      // Smooth streamline momentum prevents any sharp corners
      const inertia = 0.76;
      vx = vx * inertia + (targetX / tLen) * (1.0 - inertia);
      vz = vz * inertia + (targetZ / tLen) * (1.0 - inertia);
      const vLen = Math.sqrt(vx * vx + vz * vz) || 1.0;
      vx /= vLen;
      vz /= vLen;

      px += vx * stepLen;
      pz += vz * stepLen;
      arcLen += stepLen;
      streamWeight += 0.012; // River gains discharge downstream

      const nix = Math.floor(px);
      const niz = Math.floor(pz);
      if (nix < 2 || nix >= nx - 2 || niz < 2 || niz >= nz - 2) break;

      const subX = px - nix;
      const subZ = pz - niz;
      const nIdx = niz * nx + nix;

      // Bilinear sub-voxel splat of river discharge
      riverDischarge[nIdx] += streamWeight * (1 - subX) * (1 - subZ);
      riverDischarge[nIdx + 1] += streamWeight * subX * (1 - subZ);
      riverDischarge[nIdx + nx] += streamWeight * (1 - subX) * subZ;
      riverDischarge[nIdx + nx + 1] += streamWeight * subX * subZ;
    }
  }

  let maxQ = 4.0;
  for (let i = 0; i < total2D; i++) {
    if (riverDischarge[i] > maxQ) maxQ = riverDischarge[i];
  }

  const channelCarve = new Float32Array(total2D);
  const channelFlow = new Float32Array(total2D);

  for (let i = 0; i < total2D; i++) {
    const q = riverDischarge[i];
    if (q > 0.35) {
      const qNorm = Math.min(1.0, Math.log(q + 1.0) / Math.log(maxQ * 0.85 + 1.0));
      channelFlow[i] = Math.pow(qNorm, 0.58);
      channelCarve[i] = (params.riverCarveDepth || 19) * 0.38 * Math.pow(qNorm, 0.52);
    }
  }

  // Sub-voxel Euclidean radial kernel widening + 2-pass isotropic smoothing for silky-smooth river meanders
  vol.riverFlow.fill(0);
  vol.riverDepth.fill(0);

  const kernelRadius = 2;
  for (let z = kernelRadius; z < nz - kernelRadius; z++) {
    for (let x = kernelRadius; x < nx - kernelRadius; x++) {
      const idx = z * nx + x;
      let cMax = 0.0;
      let fMax = 0.0;
      let fAvg = 0.0;
      let wSum = 0.0;

      for (let dz = -kernelRadius; dz <= kernelRadius; dz++) {
        for (let dx = -kernelRadius; dx <= kernelRadius; dx++) {
          const dist = Math.sqrt(dx * dx + dz * dz);
          if (dist > 2.35) continue;
          const nIdx = (z + dz) * nx + (x + dx);
          const falloff = Math.max(0.0, 1.0 - (dist / 2.35) * (dist / 2.35));
          const cVal = channelCarve[nIdx] * falloff;
          const fVal = channelFlow[nIdx] * falloff;
          if (cVal > cMax) cMax = cVal;
          if (fVal > fMax) fMax = fVal;
          fAvg += channelFlow[nIdx] * falloff;
          wSum += falloff;
        }
      }

      const finalCarve = cMax;
      const finalFlow = Math.min(1.0, fMax * 0.72 + (fAvg / Math.max(1e-5, wSum)) * 0.45);

      if (finalCarve > 0.02) {
        let rem = finalCarve;
        const tTake = Math.min(vol.talusHeight[idx], rem);
        vol.talusHeight[idx] -= tTake;
        rem -= tTake;
        const sTake = Math.min(vol.sedimentHeight[idx], rem);
        vol.sedimentHeight[idx] -= sTake;
        rem -= sTake;
        vol.bedrockHeight[idx] = Math.max(4.0, vol.bedrockHeight[idx] - rem);
        vol.riverDepth[idx] = finalCarve;
      }

      vol.riverFlow[idx] = finalFlow;
    }
  }

  applyHeightDeltaToSDF(vol, prevHeights);

  if ((params.riverBankUndercut3D ?? 0.5) > 0.02) {
    const strideY = nx;
    const strideZ = nx * ny;
    for (let z = 2; z < nz - 2; z++) {
      for (let x = 2; x < nx - 2; x++) {
        const idx2D = z * nx + x;
        if (vol.cliffMask[idx2D] < 0.25) continue;

        let riverH = -1;
        let riverStrength = 0;
        for (let k = 0; k < 8; k++) {
          const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
          if (vol.riverFlow[nIdx] > 0.2) {
            riverH = getTotalSurfaceHeight(vol, nIdx);
            riverStrength = Math.max(riverStrength, vol.riverFlow[nIdx]);
          }
        }

        if (riverStrength > 0.2 && riverH > 0) {
          const yCenter = Math.round((riverH + voxelSizeY * 0.8) / voxelSizeY);
          for (let dy = -1; dy <= 1; dy++) {
            const yy = yCenter + dy;
            if (yy <= 1 || yy >= ny - 1) continue;
            const idx3D = z * strideZ + yy * strideY + x;
            const undercut =
              riverStrength *
              (params.riverBankUndercut3D ?? 0.5) *
              voxelSizeXZ *
              (0.85 - Math.abs(dy) * 0.35);
            if (undercut > 0) {
              vol.sdfGrid[idx3D] += undercut;
            }
          }
        }
      }
    }
  }
}

export interface AlluvialParams {
  depositionStrength: number;
  criticalSlope: number;
  fanSpread: number;
  floodplainSmoothing: number;
}

export function applyAlluvialSedimentation(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: AlluvialParams
): void {
  const { nx, nz, voxelSizeXZ, domain } = vol;
  const total2D = nx * nz;

  const report = evaluateResolutionForOperation(
    domain,
    nodeId,
    'alluvial',
    voxelSizeXZ * 2.0
  );
  vol.reports[nodeId] = report;
  if (!report.allowed) return;

  const prevHeights = snapshotSurfaceHeights(vol);

  const order = new Int32Array(total2D);
  for (let i = 0; i < total2D; i++) order[i] = i;
  order.sort((a, b) => getTotalSurfaceHeight(vol, b) - getTotalSurfaceHeight(vol, a));

  const sedSupply = new Float32Array(total2D);
  const d8 = [
    { dx: -1, dz: 0, dist: 1.0 },
    { dx: 1, dz: 0, dist: 1.0 },
    { dx: 0, dz: -1, dist: 1.0 },
    { dx: 0, dz: 1, dist: 1.0 },
    { dx: -1, dz: -1, dist: Math.SQRT2 },
    { dx: 1, dz: -1, dist: Math.SQRT2 },
    { dx: -1, dz: 1, dist: Math.SQRT2 },
    { dx: 1, dz: 1, dist: Math.SQRT2 },
  ];

  const crit = Math.max(0.12, params.criticalSlope || 0.28);

  for (let z = 1; z < nz - 1; z++) {
    for (let x = 1; x < nx - 1; x++) {
      const idx = z * nx + x;
      const h = getTotalSurfaceHeight(vol, idx);
      let maxDrop = 0;
      for (let k = 0; k < 8; k++) {
        const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
        const s = (h - getTotalSurfaceHeight(vol, nIdx)) / (voxelSizeXZ * d8[k].dist);
        if (s > maxDrop) maxDrop = s;
      }
      if (maxDrop > crit) {
        sedSupply[idx] =
          (maxDrop - crit) * (0.20 + vol.riverFlow[idx] * 0.85) * (params.depositionStrength || 1.0);
      }
    }
  }

  const spreadExp = Math.max(0.9, 2.5 - (params.fanSpread || 0.75) * 1.4);
  const w = new Float32Array(8);

  for (let i = 0; i < total2D; i++) {
    const idx = order[i];
    const x = idx % nx;
    const z = (idx / nx) | 0;
    if (x <= 0 || x >= nx - 1 || z <= 0 || z >= nz - 1) continue;

    const load = sedSupply[idx];
    if (load <= 0.001) continue;

    const hHere = getTotalSurfaceHeight(vol, idx);
    let maxSlope = 0;
    let totalW = 0;

    for (let k = 0; k < 8; k++) {
      const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
      const drop = hHere - getTotalSurfaceHeight(vol, nIdx);
      if (drop > 0) {
        const s = drop / (voxelSizeXZ * d8[k].dist);
        if (s > maxSlope) maxSlope = s;
        w[k] = Math.pow(s, spreadExp);
        totalW += w[k];
      } else {
        w[k] = 0;
      }
    }

    const depositFrac =
      maxSlope < crit ? Math.min(0.45, ((crit - maxSlope) / crit) * 0.38 + 0.08) : 0.025;

    const riverClear = Math.max(0.15, 1.0 - vol.riverFlow[idx] * 0.85);
    const dep = Math.min(2.8, load * depositFrac * riverClear);
    vol.sedimentHeight[idx] += dep;

    const rem = Math.max(0, load - dep);
    if (totalW > 0 && rem > 0) {
      for (let k = 0; k < 8; k++) {
        if (w[k] > 0) {
          const nIdx = (z + d8[k].dz) * nx + (x + d8[k].dx);
          sedSupply[nIdx] += rem * (w[k] / totalW);
        }
      }
    }
  }

  // Smooth Pediment & Alluvial Fan Relaxation (uses floodplainSmoothing):
  // Sweeps loose sediment & talus at the cliff base into smooth concave aprons without bumpy diamonds!
  const smoothStrength = Math.max(0.25, Math.min(0.9, params.floodplainSmoothing ?? 0.65));
  const sedBuf = new Float32Array(total2D);
  const talBuf = new Float32Array(total2D);

  for (let pass = 0; pass < 5; pass++) {
    sedBuf.set(vol.sedimentHeight);
    talBuf.set(vol.talusHeight);
    for (let z = 1; z < nz - 1; z++) {
      for (let x = 1; x < nx - 1; x++) {
        const idx = z * nx + x;
        const loose = vol.sedimentHeight[idx] + vol.talusHeight[idx];
        if (loose <= 0.01 || vol.riverFlow[idx] > 0.35) continue;

        const avgSed =
          (vol.sedimentHeight[idx - 1] +
            vol.sedimentHeight[idx + 1] +
            vol.sedimentHeight[idx - nx] +
            vol.sedimentHeight[idx + nx]) *
            0.16 +
          (vol.sedimentHeight[idx - nx - 1] +
            vol.sedimentHeight[idx - nx + 1] +
            vol.sedimentHeight[idx + nx - 1] +
            vol.sedimentHeight[idx + nx + 1]) *
            0.09;

        const avgTal =
          (vol.talusHeight[idx - 1] +
            vol.talusHeight[idx + 1] +
            vol.talusHeight[idx - nx] +
            vol.talusHeight[idx + nx]) *
            0.16 +
          (vol.talusHeight[idx - nx - 1] +
            vol.talusHeight[idx - nx + 1] +
            vol.talusHeight[idx + nx - 1] +
            vol.talusHeight[idx + nx + 1]) *
            0.09;

        sedBuf[idx] = vol.sedimentHeight[idx] * (1.0 - smoothStrength * 0.55) + avgSed * (smoothStrength * 0.55);
        talBuf[idx] = vol.talusHeight[idx] * (1.0 - smoothStrength * 0.45) + avgTal * (smoothStrength * 0.45);
      }
    }
    vol.sedimentHeight.set(sedBuf);
    vol.talusHeight.set(talBuf);
  }

  applyHeightDeltaToSDF(vol, prevHeights);
}

export interface WindErosionParams {
  windAngleDeg: number;
  abrasionStrength: number;
  duneRippleScale: number;
  duneStrength: number;
}

export function applyWindErosion(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: WindErosionParams
): void {
  const { nx, nz, voxelSizeXZ, domain } = vol;
  const report = evaluateResolutionForOperation(
    domain,
    nodeId,
    'wind',
    Math.max(params.duneRippleScale * 0.5, voxelSizeXZ * 2.0)
  );
  vol.reports[nodeId] = report;
  if (!report.allowed) return;

  const prevHeights = snapshotSurfaceHeights(vol);

  const rad = ((params.windAngleDeg || 38) * Math.PI) / 180.0;
  const wx = Math.cos(rad);
  const wz = Math.sin(rad);

  const noise = new SeededNoise(7723);
  const rippleFreq =
    1.0 / Math.max(report.nyquistMinFeatureMeters * 1.4, params.duneRippleScale || 26.0);
  const halfWorld = domain.worldSize * 0.5;

  for (let z = 1; z < nz - 1; z++) {
    const worldZ = z * voxelSizeXZ - halfWorld;
    for (let x = 1; x < nx - 1; x++) {
      const worldX = x * voxelSizeXZ - halfWorld;
      const idx = z * nx + x;
      const h = getTotalSurfaceHeight(vol, idx);

      const dhx =
        minmod(
          getTotalSurfaceHeight(vol, idx + 1) - h,
          h - getTotalSurfaceHeight(vol, idx - 1)
        ) / voxelSizeXZ;
      const dhz =
        minmod(
          getTotalSurfaceHeight(vol, idx + nx) - h,
          h - getTotalSurfaceHeight(vol, idx - nx)
        ) / voxelSizeXZ;

      const windDot = -(dhx * wx + dhz * wz);
      const slopeMag = Math.sqrt(dhx * dhx + dhz * dhz);

      if (windDot > 0.08) {
        const abrade = Math.min(2.0, windDot * (params.abrasionStrength || 0.42) * 1.0);
        vol.bedrockHeight[idx] = Math.max(5.0, vol.bedrockHeight[idx] - abrade);
        vol.windMask[idx] = Math.min(1.0, windDot * (params.abrasionStrength || 0.42));
      } else if (slopeMag < 0.32 && vol.talusHeight[idx] < 0.4) {
        const riverNear = Math.max(
          vol.riverFlow[idx],
          vol.riverFlow[idx - 1],
          vol.riverFlow[idx + 1],
          vol.riverFlow[idx - nx],
          vol.riverFlow[idx + nx]
        );
        const riverCorridorFade = Math.max(0.0, 1.0 - riverNear * 4.2);
        if (riverCorridorFade > 0.02) {
          const alongWind = worldX * wx + worldZ * wz;
          const warp = noise.simplex2D(worldX * rippleFreq * 0.35, worldZ * rippleFreq * 0.35) * 1.5;
          const wave = Math.sin(alongWind * rippleFreq * Math.PI * 2.0 + warp);
          const duneProfile = Math.pow(wave * 0.5 + 0.5, 2.0);
          const duneAdd =
            duneProfile *
            (params.duneStrength || 0.48) *
            1.3 *
            (1.0 - slopeMag * 2.6) *
            riverCorridorFade;
          if (duneAdd > 0) {
            vol.sedimentHeight[idx] += duneAdd;
            vol.windMask[idx] = Math.min(
              1.0,
              duneProfile * (params.duneStrength || 0.48) * riverCorridorFade
            );
          }
        }
      }
    }
  }

  applyHeightDeltaToSDF(vol, prevHeights);
}
