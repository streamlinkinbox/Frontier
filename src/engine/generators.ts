import { SeededNoise } from './noise';
import { evaluateResolutionForOperation, normalizeSDFNearSurface } from './resolutionGuard';
import {
  SDFTerrainVolume,
  ArchSplineControl,
  syncHeightfieldToSDF,
  getTotalSurfaceHeight,
} from './terrainState';

export interface GeneratorParams {
  seed: number;
  scale: number;        // Feature distance / wavelength in km (1km - 100km)
  amplitude: number;    // Elevation height in meters
  octaves: number;
  lacunarity: number;
  gain: number;
  ridgeSharpness: number;
  warpStrength: number; // Tectonic domain warp
  blendMode: 'replace' | 'add' | 'max' | 'multiply';
  valleyFloor: number;  // Valley basin / desert floor flatness [0..1]
  butteDensity?: number;      // Number / density of monolith clusters [0..1]
  verticalFissures?: number;  // Deep vertical joint cracks splitting monoliths [0..1]
  basalOverhang3D?: number;   // 3D SDF wind-sapped overhang at monolith bases [0..1]
  caprockCrown?: number;      // Pancake caprock layering at monolith summits [0..1]
  talusSkirtHeight?: number;  // Conical talus skirt height at monolith bases (meters)
  talusSpread?: number;       // Radial spread of talus apron (meters)
}

function smoothMin(a: number, b: number, k: number): number {
  if (k <= 1e-4) return Math.min(a, b);
  const h = Math.max(0.0, k - Math.abs(a - b)) / k;
  return Math.min(a, b) - h * h * k * 0.25;
}

function smoothMax(a: number, b: number, k: number): number {
  return -smoothMin(-a, -b, k);
}

/**
 * Normalizes the user's `scale` parameter (supports both 1..100 km and legacy 40..600 values)
 * into an effective wavelength on the 512-unit SDF computation domain.
 */
function resolveEffectiveWavelength(rawScale: number, worldSize: number): {
  wavelength: number;
  regionalComplexity: number;
  displayKm: number;
} {
  const km =
    rawScale <= 105
      ? Math.max(1.0, Math.min(100.0, rawScale))
      : Math.max(1.0, Math.min(100.0, rawScale / 1000.0));

  const logT = Math.log10(km) / 2.0; // [0..1] for 1km..100km
  const wavelength = worldSize * (0.62 - logT * 0.34);
  const regionalComplexity = 1.0 + logT * 0.85;

  return { wavelength, regionalComplexity, displayKm: km };
}

interface SubTowerStamp {
  cx: number;
  cz: number;
  rx: number;
  rz: number;
  cosA: number;
  sinA: number;
  summitH: number;
  flatness: number;      // 0 = jagged/domed ridge, 1 = crisp flat table caprock
  shapePower: number;    // 2.6 = weathered spire, 5.0 = fin, 7.5 = sharp angular fracture block
  wedgeSkew: number;     // Trapezoidal prow taper along u axis (-0.38..+0.38)
  bendCurve: number;     // Crescent/arc curvature along length
  tiltU: number;         // Caprock dip slope along u axis
  tiltV: number;         // Caprock dip slope along v axis
  stepRatio: number;     // Relative height of structural bench step (0 = none)
  stepSetback: number;   // Meters of horizontal step setback above stepRatio
  capBevel: number;      // Smoothness k of summit caprock edge (1.8 = razor table, 5.8 = weathered crown)
  seedOffset: number;
}

/**
 * NEW NODE 1: Desert Pediment Floor (`DesertPediment`)
 * Generates the wide, flat Monument Valley / Wadi Rum desert plain with low wind-swept
 * sand sheets and shallow dry desert arroyo washes.
 */
export function applyDesertPedimentNode(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: Record<string, any>
): void {
  const { nx, nz, voxelSizeXZ, domain } = vol;
  const report = evaluateResolutionForOperation(domain, nodeId, 'generator', voxelSizeXZ * 4.0);
  vol.reports[nodeId] = report;

  const seed = params.seed || 4217;
  const noise = new SeededNoise(seed);
  const washNoise = new SeededNoise(seed + 1913);
  const halfWorld = domain.worldSize * 0.5;
  const baseFloor = params.baseElevation ?? 15.0;
  const undulation = (params.undulationHeight ?? 9.0) * 0.36;
  const washDepth = params.arroyoWashDepth ?? params.arroyoDepth ?? 2.4;

  for (let z = 0; z < nz; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    for (let x = 0; x < nx; x++) {
      const wx = x * voxelSizeXZ - halfWorld;
      const idx = z * nx + x;

      const swell = noise.simplex2D(wx * 0.0032, wz * 0.0032) * undulation;
      const microSheet = noise.simplex2D(wx * 0.011 + 37.1, wz * 0.011 - 19.4) * (undulation * 0.28);

      // Sinuous dry desert wash / wadi channel across the open desert floor
      const wWarp = washNoise.simplex2D(wx * 0.0045, wz * 0.0045) * 65.0;
      const washDist = Math.abs(wx * 0.52 + wz * 0.48 + wWarp - 18.0);
      const arroyo = Math.exp(-(washDist * washDist) / (28.0 * 28.0)) * washDepth;

      const hFloor = Math.max(8.0, baseFloor + swell + microSheet - arroyo);
      vol.bedrockHeight[idx] = hFloor;
      vol.sedimentHeight[idx] = Math.max(0.0, (undulation * 0.35 + microSheet) * 0.5);
      vol.windMask[idx] = Math.max(0.0, Math.min(1.0, 0.35 + microSheet * 0.15));
      vol.monolithDist[idx] = 999.0;
      vol.monolithSummitH[idx] = hFloor;
    }
  }

  syncHeightfieldToSDF(vol);
}

/**
 * NEW NODE 2: 3D SDF Monolith Towers (`SDFMonolithTowers`)
 * Constructs True 3D CSG Sandstone Monoliths, Split Turrets & Flat-Topped Buttes:
 *   φ_tower(x, y, z) = smoothMax(d_horiz(x, z), y - H_top(x, z))
 * Because vertical walls are defined by the smooth horizontal Euclidean distance d_horiz(x, z)
 * where |∇φ| ≈ 1.0, 1-voxel vertical needle spikes are mathematically impossible!
 */
export function applySDFMonolithTowersNode(
  vol: SDFTerrainVolume,
  nodeId: string,
  stampStyleOrParams: 'monument_buttes' | 'mesa_plateau' | 'needle_spires' | Record<string, any>,
  maybeParams?: Record<string, any>
): void {
  const params: Record<string, any> =
    typeof stampStyleOrParams === 'string' ? maybeParams || {} : stampStyleOrParams || {};
  const stampStyle: 'monument_buttes' | 'mesa_plateau' | 'needle_spires' =
    typeof stampStyleOrParams === 'string'
      ? stampStyleOrParams
      : params.monolithPreset || 'monument_buttes';

  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, domain, sdfGrid, bandMinY, bandMaxY } = vol;
  const report = evaluateResolutionForOperation(domain, nodeId, 'generator', voxelSizeXZ * 2.5);
  vol.reports[nodeId] = report;

  const seed = params.seed || 4217;
  const noise = new SeededNoise(seed);
  const wallNoise = new SeededNoise(seed + 3319);
  const halfWorld = domain.worldSize * 0.5;

  const maxAmp = Math.min(
    domain.maxHeight * 0.90,
    params.towerHeight ?? params.amplitude ?? 182
  );
  const density = params.butteDensity ?? 0.75;
  const footprintScale = params.footprintScale ?? 1.0;
  const benchStrength = params.steppedBenchRatio ?? 0.72;
  const crownStrength = params.caprockCrown ?? 0.85;

  let rng = (seed * 1664525 + 1013904223) | 0;
  const rand = () => {
    rng = (rng * 1664525 + 1013904223) | 0;
    return (rng >>> 0) / 4294967296.0;
  };

  const widthScale =
    stampStyle === 'mesa_plateau' ? 1.42 : stampStyle === 'needle_spires' ? 0.74 : 1.0;

  const subTowers: SubTowerStamp[] = [];
  const addBlock = (b: Omit<SubTowerStamp, 'cosA' | 'sinA' | 'seedOffset'> & { angle: number }) => {
    subTowers.push({
      cx: b.cx,
      cz: b.cz,
      rx: Math.max(12.0, b.rx * footprintScale * widthScale),
      rz: Math.max(12.0, b.rz * footprintScale * widthScale),
      cosA: Math.cos(b.angle),
      sinA: Math.sin(b.angle),
      summitH: Math.min(domain.maxHeight * 0.92, b.summitH),
      flatness: b.flatness,
      shapePower: b.shapePower,
      wedgeSkew: b.wedgeSkew,
      bendCurve: b.bendCurve,
      tiltU: b.tiltU,
      tiltV: b.tiltV,
      stepRatio: b.stepRatio,
      stepSetback: b.stepSetback * benchStrength,
      capBevel: b.capBevel,
      seedOffset: subTowers.length * 19.37 + rand() * 50.0,
    });
  };

  // ============================================================================
  // GEOLOGICALLY DIVERSE MONOLITH ARCHETYPES (No two formations look alike!)
  // ============================================================================

  // ARCHETYPE 1 (Center-Left Hero): "Cathedral Fin & Split Sentinel Spire"
  // Tall narrow sandstone fin + angular stepped cathedral tower + lower broad bench + detached thumb needle
  addBlock({
    cx: -28, cz: -14, rx: 46, rz: 17, angle: 0.28,
    summitH: 15 + maxAmp * 1.02, flatness: 0.82, shapePower: 7.2,
    wedgeSkew: 0.28, bendCurve: 0.004, tiltU: 0.08, tiltV: -0.04,
    stepRatio: 0.58, stepSetback: 6.5, capBevel: 1.5,
  });
  addBlock({
    cx: -54, cz: 8, rx: 24, rz: 28, angle: -0.42,
    summitH: 15 + maxAmp * 0.81, flatness: 0.92, shapePower: 7.8,
    wedgeSkew: -0.22, bendCurve: 0.0, tiltU: -0.05, tiltV: 0.03,
    stepRatio: 0.44, stepSetback: 5.0, capBevel: 1.4,
  });
  addBlock({
    cx: 18, cz: -28, rx: 16, rz: 15, angle: 0.65,
    summitH: 15 + maxAmp * 0.94, flatness: 0.76, shapePower: 5.8,
    wedgeSkew: 0.15, bendCurve: 0.0, tiltU: 0.09, tiltV: 0.05,
    stepRatio: 0.0, stepSetback: 0.0, capBevel: 1.6,
  });
  addBlock({
    cx: -12, cz: 22, rx: 38, rz: 22, angle: 0.15,
    summitH: 15 + maxAmp * 0.48, flatness: 0.96, shapePower: 7.5,
    wedgeSkew: 0.32, bendCurve: -0.003, tiltU: 0.03, tiltV: -0.02,
    stepRatio: 0.0, stepSetback: 0.0, capBevel: 1.3,
  });

  // ARCHETYPE 2 (Right Foreground): "Great Overhanging Crescent Cliff Massif"
  // Massive elongated curved cliff wall (like the right foreground cliff in reference photo)
  addBlock({
    cx: 148, cz: 86, rx: 68, rz: 26, angle: 1.18,
    summitH: 15 + maxAmp * 0.96, flatness: 0.88, shapePower: 7.6,
    wedgeSkew: -0.30, bendCurve: 0.0065, tiltU: -0.07, tiltV: 0.05,
    stepRatio: 0.66, stepSetback: 7.5, capBevel: 1.5,
  });
  addBlock({
    cx: 116, cz: 128, rx: 36, rz: 22, angle: 0.72,
    summitH: 15 + maxAmp * 0.64, flatness: 0.94, shapePower: 7.2,
    wedgeSkew: 0.25, bendCurve: 0.0, tiltU: 0.04, tiltV: -0.03,
    stepRatio: 0.0, stepSetback: 0.0, capBevel: 1.4,
  });
  addBlock({
    cx: 172, cz: 32, rx: 22, rz: 19, angle: 1.45,
    summitH: 15 + maxAmp * 0.84, flatness: 0.80, shapePower: 6.2,
    wedgeSkew: 0.18, bendCurve: 0.0, tiltU: -0.08, tiltV: 0.0,
    stepRatio: 0.52, stepSetback: 4.5, capBevel: 1.6,
  });

  // ARCHETYPE 3 (Mid-Left): "Knife-Edge Hogback Fin & Cleft Twin Buttress"
  // Dramatic narrow elongated sandstone fin oriented diagonally with a low broken terrace
  addBlock({
    cx: -156, cz: -78, rx: 58, rz: 15, angle: -0.38,
    summitH: 15 + maxAmp * 0.89, flatness: 0.84, shapePower: 6.8,
    wedgeSkew: 0.34, bendCurve: -0.005, tiltU: 0.09, tiltV: 0.03,
    stepRatio: 0.62, stepSetback: 4.2, capBevel: 1.5,
  });
  addBlock({
    cx: -124, cz: -112, rx: 26, rz: 18, angle: 0.48,
    summitH: 15 + maxAmp * 0.72, flatness: 0.90, shapePower: 7.6,
    wedgeSkew: -0.24, bendCurve: 0.0, tiltU: -0.04, tiltV: 0.05,
    stepRatio: 0.0, stepSetback: 0.0, capBevel: 1.4,
  });
  addBlock({
    cx: -182, cz: -46, rx: 28, rz: 20, angle: -0.22,
    summitH: 15 + maxAmp * 0.39, flatness: 0.95, shapePower: 7.0,
    wedgeSkew: 0.20, bendCurve: 0.0, tiltU: 0.03, tiltV: -0.02,
    stepRatio: 0.0, stepSetback: 0.0, capBevel: 1.3,
  });

  // ARCHETYPE 4 (Distant Background Center-Right): "Tilted Table Mesa & Step Bench"
  // Wide flat-topped trapezoidal mesa with a crisp dipping caprock and low ruined wing
  addBlock({
    cx: 48, cz: -166, rx: 54, rz: 32, angle: 0.12,
    summitH: 15 + maxAmp * 0.78, flatness: 0.96, shapePower: 8.0,
    wedgeSkew: -0.28, bendCurve: 0.002, tiltU: -0.07, tiltV: 0.03,
    stepRatio: 0.54, stepSetback: 8.0, capBevel: 1.3,
  });
  addBlock({
    cx: -6, cz: -158, rx: 32, rz: 22, angle: 0.24,
    summitH: 15 + maxAmp * 0.44, flatness: 0.94, shapePower: 7.4,
    wedgeSkew: 0.26, bendCurve: 0.0, tiltU: 0.04, tiltV: 0.0,
    stepRatio: 0.0, stepSetback: 0.0, capBevel: 1.4,
  });

  // ARCHETYPE 5 (Foreground-Left): "Solitary Totem Obelisk & Low Weathered Stump"
  // High contrast between a slender vertical needle and a low eroded bedrock pedestal
  addBlock({
    cx: -136, cz: 108, rx: 16, rz: 14, angle: 0.82,
    summitH: 15 + maxAmp * 0.86, flatness: 0.78, shapePower: 5.8,
    wedgeSkew: 0.16, bendCurve: 0.0, tiltU: 0.08, tiltV: -0.05,
    stepRatio: 0.42, stepSetback: 3.5, capBevel: 1.6,
  });
  addBlock({
    cx: -104, cz: 92, rx: 28, rz: 19, angle: 0.25,
    summitH: 15 + maxAmp * 0.31, flatness: 0.94, shapePower: 7.2,
    wedgeSkew: -0.30, bendCurve: 0.0, tiltU: -0.03, tiltV: 0.02,
    stepRatio: 0.0, stepSetback: 0.0, capBevel: 1.3,
  });
  addBlock({
    cx: -158, cz: 132, rx: 21, rz: 16, angle: 1.15,
    summitH: 15 + maxAmp * 0.54, flatness: 0.88, shapePower: 6.5,
    wedgeSkew: 0.22, bendCurve: 0.0, tiltU: 0.06, tiltV: 0.0,
    stepRatio: 0.0, stepSetback: 0.0, capBevel: 1.5,
  });

  // ARCHETYPE 6 (Mid-Right): "Asymmetric Prow Buttress & Split Turret"
  addBlock({
    cx: 124, cz: -78, rx: 42, rz: 19, angle: -0.64,
    summitH: 15 + maxAmp * 0.88, flatness: 0.85, shapePower: 7.0,
    wedgeSkew: 0.36, bendCurve: -0.004, tiltU: 0.08, tiltV: -0.04,
    stepRatio: 0.60, stepSetback: 5.8, capBevel: 1.5,
  });
  addBlock({
    cx: 92, cz: -56, rx: 18, rz: 16, angle: 0.35,
    summitH: 15 + maxAmp * 0.58, flatness: 0.90, shapePower: 6.4,
    wedgeSkew: -0.20, bendCurve: 0.0, tiltU: -0.05, tiltV: 0.04,
    stepRatio: 0.0, stepSetback: 0.0, capBevel: 1.4,
  });

  // ARCHETYPE 7 (Distant Left Silhouette): "Broken Castle Ridge"
  addBlock({
    cx: -98, cz: -178, rx: 44, rz: 18, angle: 0.52,
    summitH: 15 + maxAmp * 0.74, flatness: 0.84, shapePower: 6.8,
    wedgeSkew: -0.26, bendCurve: 0.003, tiltU: 0.07, tiltV: 0.03,
    stepRatio: 0.48, stepSetback: 5.2, capBevel: 1.5,
  });

  // Optional extra formations when user increases density slider
  const extraCount = Math.max(0, Math.round((density - 0.75) * 8));
  for (let e = 0; e < extraCount; e++) {
    addBlock({
      cx: (rand() - 0.5) * domain.worldSize * 0.76,
      cz: (rand() - 0.5) * domain.worldSize * 0.76,
      rx: 16 + rand() * 34,
      rz: 14 + rand() * 20,
      angle: (rand() - 0.5) * Math.PI,
      summitH: 15 + maxAmp * (0.35 + rand() * 0.60),
      flatness: 0.78 + rand() * 0.18,
      shapePower: 5.8 + rand() * 2.2,
      wedgeSkew: (rand() - 0.5) * 0.55,
      bendCurve: (rand() - 0.5) * 0.006,
      tiltU: (rand() - 0.5) * 0.12,
      tiltV: (rand() - 0.5) * 0.08,
      stepRatio: rand() > 0.4 ? 0.45 + rand() * 0.25 : 0.0,
      stepSetback: 4.0 + rand() * 4.5,
      capBevel: 1.3 + rand() * 0.4,
    });
  }

  vol.has3DMonoliths = true;
  const strideY = nx;
  const strideZ = nx * ny;

  // Pre-allocate active block indices per (x, z) column to keep inner 3D loop fast
  const activeIdx = new Int32Array(subTowers.length);
  const activeD0 = new Float32Array(subTowers.length);
  const activeCapH = new Float32Array(subTowers.length);

  for (let z = 0; z < nz; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;

    for (let x = 0; x < nx; x++) {
      const wx = x * voxelSizeXZ - halfWorld;
      const idx2D = zOff2D + x;
      const floorH = vol.bedrockHeight[idx2D];

      // Multi-scale angular fracture-plane wall warp (chiseled rock faces, not smooth cylinders!)
      const coarseWarpX = wallNoise.simplex2D(wx * 0.013, wz * 0.013) * 8.5;
      const coarseWarpZ = wallNoise.simplex2D(wx * 0.013 + 91.3, wz * 0.013 - 57.1) * 8.5;
      // Sharp angular facet offset (folded absolute simplex creates planar rock faces & sharp corners)
      const angularFacet1 = Math.abs(wallNoise.simplex2D(wx * 0.029 + 17.4, wz * 0.029 - 43.8)) * 5.2 - 2.6;
      const angularFacet2 = Math.abs(wallNoise.simplex2D(wx * 0.052 - 63.1, wz * 0.052 + 28.9)) * 2.8 - 1.4;

      // Summit ridge crenellation & weathered saddle notches
      const ridgeSaddle =
        noise.simplex2D(wx * 0.024 + 11.7, wz * 0.024 - 39.2) * 6.8 +
        (Math.abs(noise.simplex2D(wx * 0.048, wz * 0.048)) - 0.5) * 4.5;

      let minHorizDist = 999.0;
      let minWallAbsDist = 999.0;
      let maxColumnSummitH = floorH;
      let activeCount = 0;

      for (let t = 0; t < subTowers.length; t++) {
        const st = subTowers[t];
        const dx = wx + coarseWarpX - st.cx;
        const dz = wz + coarseWarpZ - st.cz;

        const maxR = Math.max(st.rx, st.rz) * 2.6;
        if (Math.abs(dx) > maxR || Math.abs(dz) > maxR) continue;

        const uRaw = dx * st.cosA - dz * st.sinA;
        const vRaw = dx * st.sinA + dz * st.cosA;

        // Apply crescent curvature & trapezoidal wedge skew so footprints are asymmetric rock prows
        const vBent = vRaw + st.bendCurve * (uRaw * uRaw);
        const skewFactor = Math.max(0.58, Math.min(1.42, 1.0 + st.wedgeSkew * (uRaw / st.rx)));
        const localRz = st.rz * skewFactor;

        const ux = Math.abs(uRaw) / st.rx;
        const vz = Math.abs(vBent) / localRz;
        const p = st.shapePower;
        const normP = Math.pow(Math.pow(ux, p) + Math.pow(vz, p), 1.0 / p);
        const avgR = 0.5 * (st.rx + localRz);

        // Base horizontal signed distance with angular fracture facets
        const dBase = (normP - 1.0) * avgR + angularFacet1 + angularFacet2;

        minHorizDist = smoothMin(minHorizDist, dBase, 4.2);

        if (dBase < 22.0) {
          // Track closest vertical wall across all overlapping blocks (including inner stepped spires!)
          const wallAbs = Math.min(
            Math.abs(dBase),
            Math.abs(dBase + st.stepSetback + 1.8)
          );
          if (wallAbs < minWallAbsDist) minWallAbsDist = wallAbs;

          // Compute this block's tilted, weathered summit caprock elevation at (wx, wz)
          const tiltOffset = (uRaw / st.rx) * st.tiltU * 24.0 + (vBent / localRz) * st.tiltV * 24.0;
          const crownProfile =
            (1.0 - st.flatness) *
              (Math.min(14.0, Math.max(0.0, -dBase) * 0.52) + ridgeSaddle * 1.15) +
            st.flatness * (ridgeSaddle * 0.28);

          const blockCapH = Math.max(floorH + 14.0, st.summitH + tiltOffset + crownProfile);

          activeIdx[activeCount] = t;
          activeD0[activeCount] = dBase;
          activeCapH[activeCount] = blockCapH;
          activeCount++;

          if (dBase < 1.8 && blockCapH > maxColumnSummitH) {
            maxColumnSummitH = blockCapH;
          }
        }
      }

      vol.monolithDist[idx2D] = minHorizDist;
      vol.monolithSummitH[idx2D] = maxColumnSummitH;

      // Evaluate True Per-Block 3D CSG Union so tall spires rising from low benches have crisp vertical steps!
      if (activeCount > 0 && minHorizDist < 22.0) {
        const yMin = Math.max(0, Math.floor(floorH / voxelSizeY) - 3);
        const yMax = Math.min(ny - 2, Math.ceil(maxColumnSummitH / voxelSizeY) + 4);
        bandMinY[idx2D] = Math.min(bandMinY[idx2D], yMin);
        bandMaxY[idx2D] = Math.max(bandMaxY[idx2D], yMax);

        if (minWallAbsDist < 16.0) {
          vol.cliffMask[idx2D] = Math.max(
            vol.cliffMask[idx2D],
            Math.max(0.0, Math.min(1.0, 1.0 - minWallAbsDist / 16.0))
          );
        }

        // Purely 2D horizontal rock-face facet offset (strictly constant in Y so vertical walls never wobble!)
        const wallFacet2D =
          wallNoise.simplex2D(wx * 0.034, wz * 0.034) * 1.4;

        for (let y = 0; y < ny; y++) {
          const wy = y * voxelSizeY;
          const phiFloor = wy - floorH;

          let phiCluster = 999.0;

          for (let a = 0; a < activeCount; a++) {
            const st = subTowers[activeIdx[a]];
            const d0 = activeD0[a];
            const capH = activeCapH[a];
            const span = Math.max(24.0, capH - floorH);
            const hRel = Math.max(0.0, Math.min(1.2, (wy - floorH) / span));

            // Natural upward wall taper + per-block wedding-cake structural bench setback
            let stepOffset = hRel * (1.8 + (1.0 - st.flatness) * 1.8);
            if (st.stepRatio > 0.05 && hRel > st.stepRatio) {
              const stepT = Math.min(1.0, (hRel - st.stepRatio) / 0.05);
              stepOffset += stepT * stepT * (3.0 - 2.0 * stepT) * st.stepSetback;
            }

            // Stacked pancake caprock ledges at the summit crown (like the reference photo!)
            let capLipExpand = 0.0;
            if (crownStrength > 0.1 && hRel > 0.80 && hRel <= 1.0) {
              const crownT = (hRel - 0.80) / 0.20;
              const pancakeStacks = Math.sin(crownT * Math.PI * 3.0) * 0.85 + Math.sin(crownT * Math.PI) * 1.35;
              capLipExpand = pancakeStacks * crownStrength * st.flatness;
            }

            const phiWall = d0 + stepOffset + wallFacet2D - capLipExpand;
            const phiCap = wy - capH;
            const phiBlock = smoothMax(phiWall, phiCap, st.capBevel);

            phiCluster = smoothMin(phiCluster, phiBlock, 2.4);
          }

          // Smooth 3D CSG union of the multi-block monolith formation with the desert pediment floor
          let phi = smoothMin(phiFloor, phiCluster, 3.6);
          if (y === 0) phi = Math.min(phi, -voxelSizeY);
          sdfGrid[zOff3D + y * strideY + x] = phi;
        }
      }
    }
  }
}

/**
 * NEW NODE 3: Architectural Vertical Columnar Ribs, Dihedral Facets & Stepped Ledges (`VerticalJointFissures`)
 * Sculpts crisp, plumb vertical sandstone columns, dihedral fracture corners, and stepped
 * horizontal ledges directly onto existing monolith walls WITHOUT any square checkerboard
 * grid clipping (eliminating thin vertical pegs and boxy Lego steps!).
 */
export function applyVerticalJointFissuresNode(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: Record<string, any>
): void {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, domain, sdfGrid, bandMinY, bandMaxY } = vol;
  const report = evaluateResolutionForOperation(domain, nodeId, 'cliff', voxelSizeXZ * 2.5);
  vol.reports[nodeId] = report;
  if (!report.allowed) return;

  const fissureStrength =
    params.fissureIntensity ?? params.fissureDepth ?? params.verticalFissures ?? 0.85;
  const ribDepth = (params.chimneyDepth ?? 11.5) * 0.34 * fissureStrength;
  const spacingScale = 22.0 / Math.max(12.0, params.fissureSpacing ?? params.jointSpacing ?? 22.0);
  const ledgeStepStrength = params.beddingNotchStrength ?? params.masterNotch ?? 0.82;
  const flutingStrength = params.columnFluting ?? fissureStrength;

  const ribNoise = new SeededNoise((params.seed || 4217) + 5119);
  const tierNoise = new SeededNoise((params.seed || 4217) + 8837);

  const halfWorld = domain.worldSize * 0.5;
  const strideY = nx;
  const strideZ = nx * ny;

  const cosA = Math.cos(0.34);
  const sinA = Math.sin(0.34);

  for (let z = 2; z < nz - 2; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;

    for (let x = 2; x < nx - 2; x++) {
      const idx2D = zOff2D + x;
      const mDist = vol.monolithDist[idx2D];
      if (vol.cliffMask[idx2D] < 0.06 && (mDist < -14.0 || mDist > 12.0)) continue;

      const wx = x * voxelSizeXZ - halfWorld;
      const floorH = vol.bedrockHeight[idx2D];
      const summitH = vol.monolithSummitH[idx2D];
      const towerSpan = Math.max(30.0, summitH - floorH);

      // 1. Low-frequency Architectural Zone Mask:
      // Leaves ~42% of cliff walls as broad, sheer, unbroken planar sandstone faces!
      const zoneRaw = ribNoise.simplex2D(wx * 0.011 + 19.4, wz * 0.011 - 43.2);
      const activeZone = Math.max(0.0, Math.min(1.0, (zoneRaw + 0.18) / 0.62));
      if (activeZone < 0.02) continue;

      // 2. Continuous 2D Angular Dihedral Ribs & Vertical Chimneys (∂/∂y = 0 along each tier!)
      const u = (wx * cosA - wz * sinA) * spacingScale;
      const v = (wx * sinA + wz * cosA) * spacingScale;

      // Folded 2D ridge functions form sharp angular dihedral buttress ribs and vertical flutes
      // without any discontinuous square grid edges that could clip into pegs!
      const ridgeU = 1.0 - 2.0 * Math.abs(ribNoise.simplex2D(u * 0.036 + 11.3, v * 0.036 - 27.9));
      const ridgeV = 1.0 - 2.0 * Math.abs(ribNoise.simplex2D(u * 0.058 - 43.1, v * 0.058 + 19.4));

      // Projecting flat-faced buttress rib (< 0 pushes rock outward) vs recessed vertical chimney (> 0 carves inward)
      const buttressRib2D = (ridgeU * 0.68 + ridgeV * 0.32) * ribDepth * activeZone;

      // Secondary fine vertical columnar fluting
      const fineFlute2D =
        ribNoise.simplex2D(u * 0.095 + 7.1, v * 0.095 - 13.8) * 1.15 * flutingStrength * activeZone;

      // Per-column stepped tier heights (shifted by local fault block so ledges don't ring the whole scene)
      const tierShift = tierNoise.simplex2D(wx * 0.014 - 23.1, wz * 0.014 + 51.7) * 0.14;
      const stepH1 = 0.34 + tierShift;
      const stepH2 = 0.66 - tierShift * 0.7;

      const yMin = Math.max(2, bandMinY[idx2D]);
      const yMax = Math.min(ny - 3, bandMaxY[idx2D]);

      for (let y = yMin; y <= yMax; y++) {
        const idx3D = zOff3D + y * strideY + x;
        const phi = sdfGrid[idx3D];
        // Only sculpt existing monolith walls (never spawn detached floating geometry in open air!)
        if (phi < -12.0 || phi > 8.0) continue;

        const wy = y * voxelSizeY;
        const hRel = (wy - floorH) / towerSpan;
        // Fade out cleanly below the summit caprock rim (hRel < 0.94) so nothing EVER sticks up above a ledge!
        if (hRel < 0.06 || hRel > 0.95) continue;

        const rimGuard =
          Math.min(1.0, (hRel - 0.06) / 0.10) * Math.min(1.0, (0.95 - hRel) / 0.08);

        // Stepped tier envelope: lower pedestal projects further out, stepping back at stepH1 and stepH2!
        const t1 = Math.max(0.0, Math.min(1.0, (hRel - stepH1) / 0.045));
        const smoothStep1 = t1 * t1 * (3.0 - 2.0 * t1);
        const t2 = Math.max(0.0, Math.min(1.0, (hRel - stepH2) / 0.045));
        const smoothStep2 = t2 * t2 * (3.0 - 2.0 * t2);

        // Stepped horizontal bench setback at stepH1 and stepH2 where buttresses step inward
        const tierSetback =
          (smoothStep1 * 1.85 + smoothStep2 * 1.65) *
          ledgeStepStrength *
          Math.max(0.0, -ridgeU) *
          activeZone;

        // Vertical rib amplitude per tier (strictly constant in Y inside each tier!)
        const tierAmp = 1.0 - smoothStep1 * 0.32 - smoothStep2 * 0.28;

        const deltaPhi = (buttressRib2D * tierAmp + fineFlute2D + tierSetback) * rimGuard;
        sdfGrid[idx3D] = phi + deltaPhi;
      }
    }
  }
}

/**
 * NEW NODE 4: 3D Basal Wind-Sapped Overhangs (`BasalWindOverhangs`)
 * Carves deep 3D aeolian sand-blasted overhangs and recessed caves at the foot of the
 * sandstone monolith walls (matching the right foreground overhanging wall in 9ef6842f38a230fd469ac3683b7ca2c5.jpg).
 */
export function applyBasalWindOverhangsNode(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: Record<string, any>
): void {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, domain, sdfGrid, bandMinY, bandMaxY } = vol;
  const report = evaluateResolutionForOperation(domain, nodeId, 'wind', voxelSizeXZ * 2.5);
  vol.reports[nodeId] = report;
  if (!report.allowed) return;

  const undercutStrength =
    params.undercutDepth !== undefined
      ? params.undercutDepth / 14.5
      : params.overhangDepth ?? params.basalOverhang3D ?? 0.92;
  const browStrength =
    params.browOverhang !== undefined
      ? params.browOverhang / 8.5
      : undercutStrength;
  const alcoveHeightRatio =
    params.alcoveHeight !== undefined && params.alcoveHeight > 1.5
      ? Math.min(0.48, Math.max(0.15, params.alcoveHeight / 130.0))
      : params.alcoveHeight ?? 0.32;
  const dirBias = params.directionalBias ?? 0.68;
  const windAzimuth = ((params.windAngleDeg ?? 35) * Math.PI) / 180.0;
  const windX = Math.cos(windAzimuth);
  const windZ = Math.sin(windAzimuth);

  const overhangNoise = new SeededNoise((params.seed || 4217) + 7159);
  const halfWorld = domain.worldSize * 0.5;
  const strideY = nx;
  const strideZ = nx * ny;

  for (let z = 2; z < nz - 2; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;

    for (let x = 2; x < nx - 2; x++) {
      const idx2D = zOff2D + x;
      const mDist = vol.monolithDist[idx2D];
      if (mDist < -14.0 || mDist > 12.0) continue;

      const wx = x * voxelSizeXZ - halfWorld;
      const floorH = vol.bedrockHeight[idx2D];
      const summitH = vol.monolithSummitH[idx2D];
      const towerSpan = Math.max(30.0, summitH - floorH);

      // Estimate horizontal normal of monolith wall to emphasize windward basal sapping
      const gx = (vol.monolithDist[idx2D + 1] - vol.monolithDist[idx2D - 1]) / (2.0 * voxelSizeXZ);
      const gz = (vol.monolithDist[idx2D + nx] - vol.monolithDist[idx2D - nx]) / (2.0 * voxelSizeXZ);
      const windExposure =
        (1.0 - dirBias * 0.65) + dirBias * 0.65 * Math.max(0.0, -(gx * windX + gz * windZ));

      // Smooth 2D lateral modulation so basal overhangs form clean, sweeping horizontal rock arches/brows!
      const lateralArchMask = Math.max(
        0.0,
        0.55 + 0.55 * overhangNoise.simplex2D(wx * 0.015 + 17.3, wz * 0.015 - 29.1)
      );

      const yStart = Math.max(2, bandMinY[idx2D]);
      const yEnd = Math.min(ny - 3, bandMaxY[idx2D]);
      const startRatio = 0.13;
      const topAlcoveRatio = Math.max(startRatio + 0.12, alcoveHeightRatio + 0.06);

      for (let y = yStart; y <= yEnd; y++) {
        const wy = y * voxelSizeY;
        const hRel = (wy - floorH) / towerSpan;
        if (hRel < startRatio || hRel > topAlcoveRatio + 0.14) continue;

        const idx3D = zOff3D + y * strideY + x;
        const phi = sdfGrid[idx3D];
        if (Math.abs(phi) > 12.0) continue;

        if (hRel <= topAlcoveRatio) {
          // Smooth inward basal alcove recess above the talus foot
          const bell = Math.sin(
            ((hRel - startRatio) / Math.max(0.08, topAlcoveRatio - startRatio)) * Math.PI
          );
          const carveInwardMeters =
            bell * lateralArchMask * windExposure * undercutStrength * 4.4;
          sdfGrid[idx3D] = phi + carveInwardMeters;
        } else {
          // Protruding overhanging rock brow right above the basal alcove
          const browT = (hRel - topAlcoveRatio) / 0.14;
          const browBell = Math.sin(browT * Math.PI);
          const pushOutMeters =
            browBell * lateralArchMask * windExposure * browStrength * 2.6;
          sdfGrid[idx3D] = phi - pushOutMeters;
        }
      }
    }
  }
}

/**
 * NEW NODE 5: Conical Talus & Boulder Scree Skirt (`ConicalTalusSkirt`)
 * Builds smooth 34° conical talus aprons with radial rockfall ribs around the foot
 * of every monolith (like behind the hikers in 9ef6842f38a230fd469ac3683b7ca2c5.jpg)
 * and unions them smoothly into the base of the 3D SDF without erasing basal overhangs above!
 */
export function applyConicalTalusSkirtNode(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: Record<string, any>
): void {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, domain, sdfGrid, bandMinY, bandMaxY } = vol;
  const report = evaluateResolutionForOperation(domain, nodeId, 'alluvial', voxelSizeXZ * 2.0);
  vol.reports[nodeId] = report;
  if (!report.allowed) return;

  const apronMaxHeight =
    params.skirtHeight !== undefined
      ? params.skirtHeight * 0.48
      : params.talusSkirtHeight ?? 26.0;
  const reposeRad = (((params.reposeAngleDeg ?? 34) as number) * Math.PI) / 180.0;
  const apronSpanBase =
    params.talusSpread ?? Math.max(18.0, apronMaxHeight / Math.max(0.35, Math.tan(reposeRad)));
  const ribRoughness = params.gullyChuteStrength ?? params.rockfallRibs ?? 0.65;
  const boulderRough = params.boulderRoughness ?? 0.65;
  const chuteNoise = new SeededNoise((params.seed || 4217) + 8431);

  const halfWorld = domain.worldSize * 0.5;
  const strideY = nx;
  const strideZ = nx * ny;

  for (let z = 1; z < nz - 1; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;

    for (let x = 1; x < nx - 1; x++) {
      const idx2D = zOff2D + x;
      const mDist = vol.monolithDist[idx2D];
      if (mDist < -4.0 || mDist > apronSpanBase * 1.35) continue;

      const wx = x * voxelSizeXZ - halfWorld;
      const floorH = vol.bedrockHeight[idx2D];

      // Crisp V-shaped radial scree gullies & alluvial fan lobes along the talus cone
      const vGully =
        1.0 -
        Math.abs(chuteNoise.simplex2D(wx * 0.026 + 19.3, wz * 0.026 - 37.1));
      const chuteMod = 0.86 + 0.24 * ribRoughness * (vGully - 0.35);
      const localSpan = apronSpanBase * chuteMod;

      if (mDist < localSpan) {
        const distFromWall = Math.max(0.0, mDist + 2.0);
        const t = Math.max(0.0, 1.0 - distFromWall / (localSpan + 2.0));
        // Smooth concave-upward scree apron profile that feathers seamlessly into the desert floor
        const talusProfile = t * t * (1.85 - 0.85 * t) * Math.pow(t, 0.35);
        const gullyCarve =
          (1.0 - vGully) * 1.6 * boulderRough * Math.sin(t * Math.PI);
        const talusH = Math.max(0.0, talusProfile * apronMaxHeight * chuteMod - gullyCarve);
        vol.talusHeight[idx2D] = Math.max(vol.talusHeight[idx2D], talusH);

        const topTalusY = floorH + vol.talusHeight[idx2D];
        const yMaxTalus = Math.min(ny - 2, Math.ceil((topTalusY + 6.0) / voxelSizeY));
        if (yMaxTalus > bandMaxY[idx2D]) bandMaxY[idx2D] = yMaxTalus;

        // Smoothly union the conical talus skirt ONLY below topTalusY + 4m so overhanging cliffs above stay untouched!
        for (let y = 1; y <= yMaxTalus; y++) {
          const wy = y * voxelSizeY;
          const phiTalus = wy - topTalusY;
          const idx3D = zOff3D + y * strideY + x;
          sdfGrid[idx3D] = smoothMin(sdfGrid[idx3D], phiTalus, 3.8);
        }
      }
    }
  }
}

/**
 * Returns default editable 3D Arch Splines for each natural arch formation style.
 */
export function getDefaultArchSplines(
  archStyle: string = 'double_arch',
  seed: number = 4217
): ArchSplineControl[] {
  const rng = new SeededNoise(seed + 5119);
  const jx = (i: number) => (rng.white2D(i * 13 + 3, 7) - 0.5) * 10.0;
  const jz = (i: number) => (rng.white2D(i * 17 + 11, 19) - 0.5) * 10.0;

  if (archStyle === 'delicate_arch') {
    return [
      {
        id: 'arch-delicate-1',
        name: 'Freestanding Bowl Arch (Primary)',
        enabled: true,
        x0: -78 + jx(1),
        z0: 14 + jz(1),
        xc: 0,
        zc: -8,
        x1: 78 + jx(2),
        z1: 12 + jz(2),
        pierHeight0: 148,
        crownHeight: 196,
        pierHeight1: 164,
        crownPosU: 0.53,
        windowWidthFrac: 0.54,
        windowCenterU: 0.50,
        windowApexHeight: 158,
        sillHeight: 46,
        bridgeThickness: 24,
        finHalfWidth: 14.5,
        buttressRadius: 36,
        alcoveFlare: 0.85,
      },
    ];
  }

  if (archStyle === 'landscape_fins') {
    return [
      {
        id: 'arch-landscape-1',
        name: 'Landscape Ribbon Span',
        enabled: true,
        x0: -124 + jx(1),
        z0: 26 + jz(1),
        xc: -4,
        zc: 8,
        x1: 122 + jx(2),
        z1: 18 + jz(2),
        pierHeight0: 158,
        crownHeight: 188,
        pierHeight1: 166,
        crownPosU: 0.50,
        windowWidthFrac: 0.64,
        windowCenterU: 0.50,
        windowApexHeight: 162,
        sillHeight: 44,
        bridgeThickness: 19,
        finHalfWidth: 13.0,
        buttressRadius: 40,
        alcoveFlare: 0.82,
      },
      {
        id: 'arch-partition-2',
        name: 'Partition Side Window',
        enabled: true,
        x0: -92 + jx(3),
        z0: -62 + jz(3),
        xc: -18,
        zc: -72,
        x1: 58 + jx(4),
        z1: -54 + jz(4),
        pierHeight0: 132,
        crownHeight: 154,
        pierHeight1: 142,
        crownPosU: 0.48,
        windowWidthFrac: 0.52,
        windowCenterU: 0.48,
        windowApexHeight: 122,
        sillHeight: 42,
        bridgeThickness: 20,
        finHalfWidth: 13.0,
        buttressRadius: 34,
        alcoveFlare: 0.78,
      },
    ];
  }

  if (archStyle === 'multi_arch_canyon') {
    return [
      {
        id: 'arch-canyon-1',
        name: 'North Cathedral Span',
        enabled: true,
        x0: -102,
        z0: 44,
        xc: -8,
        zc: 24,
        x1: 96,
        z1: 32,
        pierHeight0: 146,
        crownHeight: 204,
        pierHeight1: 194,
        crownPosU: 0.58,
        windowWidthFrac: 0.56,
        windowCenterU: 0.48,
        windowApexHeight: 164,
        sillHeight: 48,
        bridgeThickness: 24,
        finHalfWidth: 14.5,
        buttressRadius: 38,
        alcoveFlare: 0.88,
      },
      {
        id: 'arch-canyon-2',
        name: 'Inner V-Window Span',
        enabled: true,
        x0: -88,
        z0: 18,
        xc: -26,
        zc: -32,
        x1: 32,
        z1: -56,
        pierHeight0: 128,
        crownHeight: 146,
        pierHeight1: 148,
        crownPosU: 0.46,
        windowWidthFrac: 0.54,
        windowCenterU: 0.46,
        windowApexHeight: 112,
        sillHeight: 42,
        bridgeThickness: 20,
        finHalfWidth: 12.5,
        buttressRadius: 33,
        alcoveFlare: 0.80,
      },
      {
        id: 'arch-canyon-3',
        name: 'East Alcove Bridge',
        enabled: true,
        x0: 42,
        z0: -52,
        xc: 94,
        zc: -14,
        x1: 134,
        z1: 42,
        pierHeight0: 148,
        crownHeight: 168,
        pierHeight1: 152,
        crownPosU: 0.52,
        windowWidthFrac: 0.50,
        windowCenterU: 0.50,
        windowApexHeight: 128,
        sillHeight: 46,
        bridgeThickness: 21,
        finHalfWidth: 13.0,
        buttressRadius: 34,
        alcoveFlare: 0.76,
      },
    ];
  }

  // Default: 'double_arch' — Double Arch Amphitheater (Arches National Park Entrada Sandstone)
  return [
    {
      id: 'arch-double-primary',
      name: 'Arch #1 — Primary High Span',
      enabled: true,
      x0: -102,
      z0: 38,
      xc: -4,
      zc: 24,
      x1: 98,
      z1: 18,
      pierHeight0: 144,
      crownHeight: 210,
      pierHeight1: 196,
      crownPosU: 0.58,
      windowWidthFrac: 0.62,
      windowCenterU: 0.47,
      windowApexHeight: 170,
      sillHeight: 46,
      bridgeThickness: 23,
      finHalfWidth: 14.5,
      buttressRadius: 38,
      alcoveFlare: 0.90,
      vaultPower: 1.95,
      rockDetail: 1.0,
    },
    {
      id: 'arch-double-secondary',
      name: 'Arch #2 — Lower Window Arch',
      enabled: true,
      x0: -88,
      z0: 14,
      xc: -26,
      zc: -34,
      x1: 32,
      z1: -58,
      pierHeight0: 128,
      crownHeight: 146,
      pierHeight1: 148,
      crownPosU: 0.46,
      windowWidthFrac: 0.58,
      windowCenterU: 0.46,
      windowApexHeight: 112,
      sillHeight: 38,
      bridgeThickness: 19,
      finHalfWidth: 12.5,
      buttressRadius: 32,
      alcoveFlare: 0.84,
      vaultPower: 1.85,
      rockDetail: 1.0,
    },
  ];
}

/**
 * Generates a distinct, well-placed procedural 3D Arch Spline for arch index >= 0
 */
export function generateProceduralArchSpline(
  archIdx: number,
  seed: number = 4217
): ArchSplineControl {
  const rng = new SeededNoise(seed + 7717 + archIdx * 313);
  const r = (k: number) => rng.white2D(archIdx * 19 + k * 7, k * 13 + 5);

  // Curated canyon placements so arches 1..8 spread across the terrain with varied angles & heights
  const slots: Array<{ cx: number; cz: number; angle: number; halfSpan: number; crown: number }> = [
    { cx: -4, cz: 26, angle: -0.10, halfSpan: 98, crown: 208 },
    { cx: -28, cz: -28, angle: -0.58, halfSpan: 72, crown: 146 },
    { cx: 82, cz: -24, angle: 1.12, halfSpan: 76, crown: 174 },
    { cx: -108, cz: -42, angle: 0.78, halfSpan: 78, crown: 166 },
    { cx: 14, cz: -112, angle: 0.18, halfSpan: 88, crown: 184 },
    { cx: -32, cz: 108, angle: -0.25, halfSpan: 74, crown: 156 },
    { cx: 128, cz: 64, angle: -0.82, halfSpan: 68, crown: 162 },
    { cx: -134, cz: 68, angle: 0.52, halfSpan: 70, crown: 158 },
  ];

  const slot = slots[archIdx % slots.length];
  const jitterX = (r(1) - 0.5) * 18.0;
  const jitterZ = (r(2) - 0.5) * 18.0;
  const angle = slot.angle + (r(3) - 0.5) * 0.24;
  const halfSpan = slot.halfSpan * (0.9 + r(4) * 0.22);
  const bend = (r(5) - 0.5) * 28.0;

  const cx = slot.cx + jitterX;
  const cz = slot.cz + jitterZ;
  const dx = Math.cos(angle) * halfSpan;
  const dz = Math.sin(angle) * halfSpan;

  const crownH = Math.round(slot.crown + (r(6) - 0.5) * 18.0);
  const bridgeT = Math.round(19 + r(7) * 6);
  const winApex = Math.round(crownH - bridgeT - 14 - r(8) * 10);
  const pier0 = Math.round(crownH * (0.68 + r(9) * 0.16));
  const pier1 = Math.round(crownH * (0.72 + r(10) * 0.18));

  return {
    id: `arch-spline-${archIdx + 1}-${seed}`,
    name: `Arch #${archIdx + 1} (${Math.round(halfSpan * 2)}m Span)`,
    enabled: true,
    x0: Math.round(cx - dx),
    z0: Math.round(cz - dz),
    xc: Math.round(cx - Math.sin(angle) * bend),
    zc: Math.round(cz + Math.cos(angle) * bend),
    x1: Math.round(cx + dx),
    z1: Math.round(cz + dz),
    pierHeight0: pier0,
    crownHeight: crownH,
    pierHeight1: pier1,
    crownPosU: Number((0.44 + r(11) * 0.14).toFixed(2)),
    windowWidthFrac: Number((0.54 + r(12) * 0.12).toFixed(2)),
    windowCenterU: Number((0.45 + r(13) * 0.10).toFixed(2)),
    windowApexHeight: Math.max(82, winApex),
    sillHeight: Math.round(38 + r(14) * 12),
    bridgeThickness: bridgeT,
    finHalfWidth: Number((12.5 + r(15) * 3.0).toFixed(1)),
    buttressRadius: Math.round(32 + r(16) * 8),
    alcoveFlare: Number((0.78 + r(17) * 0.18).toFixed(2)),
    vaultPower: Number((1.65 + r(18) * 0.65).toFixed(2)),
    rockDetail: 1.0,
  };
}

/**
 * Adjusts an existing ArchSplineControl[] list to match a target arch count (1..8)
 * while preserving all user customizations on existing splines!
 */
export function syncArchSplinesToCount(
  existing: ArchSplineControl[],
  targetCount: number,
  archStyle: string = 'double_arch',
  seed: number = 4217
): ArchSplineControl[] {
  const count = Math.max(1, Math.min(8, Math.round(targetCount)));
  const defaults = getDefaultArchSplines(archStyle, seed);
  const result: ArchSplineControl[] = [];

  for (let i = 0; i < count; i++) {
    if (i < existing.length) {
      result.push({ ...existing[i], enabled: true });
    } else if (i < defaults.length) {
      result.push({ ...defaults[i], enabled: true });
    } else {
      result.push(generateProceduralArchSpline(i, seed));
    }
  }

  return result;
}

interface PrecomputedArchSpline {
  ctrl: ArchSplineControl;
  samplesX: Float32Array;
  samplesZ: Float32Array;
  samplesU: Float32Array;
  spanLength: number;
  minX: number;
  maxX: number;
  minZ: number;
  maxZ: number;
}

function precomputeArchSpline(ctrl: ArchSplineControl): PrecomputedArchSpline {
  const N = 32;
  const samplesX = new Float32Array(N + 1);
  const samplesZ = new Float32Array(N + 1);
  const samplesU = new Float32Array(N + 1);

  let minX = Infinity;
  let maxX = -Infinity;
  let minZ = Infinity;
  let maxZ = -Infinity;
  let spanLength = 0;

  for (let i = 0; i <= N; i++) {
    const u = i / N;
    const om = 1.0 - u;
    const bx = om * om * ctrl.x0 + 2.0 * om * u * ctrl.xc + u * u * ctrl.x1;
    const bz = om * om * ctrl.z0 + 2.0 * om * u * ctrl.zc + u * u * ctrl.z1;
    samplesX[i] = bx;
    samplesZ[i] = bz;
    samplesU[i] = u;
    if (i > 0) {
      const dx = bx - samplesX[i - 1];
      const dz = bz - samplesZ[i - 1];
      spanLength += Math.sqrt(dx * dx + dz * dz);
    }
    if (bx < minX) minX = bx;
    if (bx > maxX) maxX = bx;
    if (bz < minZ) minZ = bz;
    if (bz > maxZ) maxZ = bz;
  }

  const margin = Math.max(ctrl.buttressRadius, ctrl.finHalfWidth) * 2.2 + 24.0;
  return {
    ctrl,
    samplesX,
    samplesZ,
    samplesU,
    spanLength: Math.max(30.0, spanLength),
    minX: minX - margin,
    maxX: maxX + margin,
    minZ: minZ - margin,
    maxZ: maxZ + margin,
  };
}

function projectToArchSpline(
  pre: PrecomputedArchSpline,
  wx: number,
  wz: number
): { u: number; dPerp: number; dEnd: number } {
  const { samplesX, samplesZ } = pre;
  const N = samplesX.length - 1;

  let bestDistSq = Infinity;
  let bestSeg = 0;
  let bestT = 0;

  for (let i = 0; i < N; i++) {
    const ax = samplesX[i];
    const az = samplesZ[i];
    const bx = samplesX[i + 1];
    const bz = samplesZ[i + 1];
    const abx = bx - ax;
    const abz = bz - az;
    const lenSq = abx * abx + abz * abz;
    let t = lenSq > 1e-6 ? ((wx - ax) * abx + (wz - az) * abz) / lenSq : 0.0;
    t = Math.max(0.0, Math.min(1.0, t));
    const px = ax + t * abx;
    const pz = az + t * abz;
    const dSq = (wx - px) * (wx - px) + (wz - pz) * (wz - pz);
    if (dSq < bestDistSq) {
      bestDistSq = dSq;
      bestSeg = i;
      bestT = t;
    }
  }

  // Allow smooth continuation past endpoints u=0 and u=1 for anchoring buttresses
  let u = (bestSeg + bestT) / N;
  let dEnd = -Math.min(u, 1.0 - u) * pre.spanLength;

  if (bestSeg === 0 && bestT <= 0.001) {
    const tx = samplesX[1] - samplesX[0];
    const tz = samplesZ[1] - samplesZ[0];
    const segLen = Math.sqrt(tx * tx + tz * tz) || 1.0;
    const proj = ((wx - samplesX[0]) * tx + (wz - samplesZ[0]) * tz) / segLen;
    if (proj < 0) {
      u = proj / pre.spanLength;
      dEnd = -proj - pre.ctrl.buttressRadius * 0.75;
    }
  } else if (bestSeg === N - 1 && bestT >= 0.999) {
    const tx = samplesX[N] - samplesX[N - 1];
    const tz = samplesZ[N] - samplesZ[N - 1];
    const segLen = Math.sqrt(tx * tx + tz * tz) || 1.0;
    const proj = ((wx - samplesX[N]) * tx + (wz - samplesZ[N]) * tz) / segLen;
    if (proj > 0) {
      u = 1.0 + proj / pre.spanLength;
      dEnd = proj - pre.ctrl.buttressRadius * 0.75;
    }
  }

  const dPerp = Math.sqrt(bestDistSq);
  return { u, dPerp, dEnd };
}

const ARCH_BED_THICKNESS = [0.075, 0.13, 0.085, 0.16, 0.07, 0.12, 0.09, 0.17, 0.10];
const ARCH_BED_OUTSET = [2.6, -1.6, 3.2, -2.3, 1.5, -1.2, 3.6, -2.8, 2.0];
const ARCH_BED_DIP = [-0.018, 0.012, 0.0, 0.026, -0.02, 0.01, -0.025, 0.018, 0.0];
const ARCH_BED_START = [0.0, 0.12, 0.0, 0.28, 0.06, 0.19, 0.0, 0.36, 0.08];
const ARCH_BED_END = [0.86, 0.64, 0.97, 0.82, 0.57, 0.91, 0.72, 0.98, 0.69];

/**
 * Deterministic sedimentary relief: alternating caprock ledges and recessed weak beds,
 * with hand-authored thicknesses, dips, and lateral pinch-outs. No sampled noise is used.
 */
function evaluateArchBedRelief(uInput: number, hInput: number, strength: number, formation = 0): number {
  const u = Math.max(0, Math.min(1, uInput));
  const h = Math.max(0, Math.min(1, hInput));
  const shiftedU = ((u + (formation % 5) * 0.071) % 1 + 1) % 1;
  let lower = 0;
  let previousOffset = 0;

  for (let bed = 0; bed < ARCH_BED_THICKNESS.length; bed++) {
    const thickness = ARCH_BED_THICKNESS[bed];
    const upper = lower + thickness + ARCH_BED_DIP[bed] * (shiftedU - 0.5);
    const start = ARCH_BED_START[bed];
    const end = ARCH_BED_END[bed];
    const startFade = Math.max(0, Math.min(1, (shiftedU - start) / 0.085));
    const endFade = Math.max(0, Math.min(1, (end - shiftedU) / 0.085));
    const startMask = startFade * startFade * (3 - 2 * startFade);
    const endMask = endFade * endFade * (3 - 2 * endFade);
    const targetOffset = ARCH_BED_OUTSET[bed] * startMask * endMask * strength;

    if (h <= upper || bed === ARCH_BED_THICKNESS.length - 1) {
      const local = Math.max(0, Math.min(1, (h - lower) / Math.max(0.02, upper - lower)));
      const blend = Math.max(0, Math.min(1, local / 0.14));
      const smoothBlend = blend * blend * (3 - 2 * blend);
      return previousOffset + (targetOffset - previousOffset) * smoothBlend;
    }

    lower = upper;
    previousOffset = targetOffset;
  }

  return previousOffset;
}

/**
 * NEW NODE 6: 3D SDF Natural Arches & Splines (`SDFNaturalArches`)
 * Generates Entrada Sandstone Natural Arches (like Double Arch, Delicate Arch, Landscape Arch)
 * using both procedural generation AND interactive user-editable 2D/3D Bezier arch splines,
 * complete with dual-flared conchoidal alcove breakthroughs, central-right Entrada dome turrets,
 * sloping cross-bedded slickrock amphitheater ledges, and fallen sandstone boulders.
 */
export function applySDFNaturalArchesNode(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: Record<string, any>
): void {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, domain, sdfGrid, bandMinY, bandMaxY } = vol;
  const report = evaluateResolutionForOperation(domain, nodeId, 'generator', voxelSizeXZ * 2.2);
  vol.reports[nodeId] = report;

  vol.has3DMonoliths = true;
  vol.has3DArches = true;

  const seed = params.seed ?? 4217;
  const clampParam = (value: unknown, fallback: number, min: number, max: number) => {
    const number = Number(value);
    return Math.max(min, Math.min(max, Number.isFinite(number) ? number : fallback));
  };
  vol.archFracture = {
    cellSizeMeters: clampParam(params.fractureCellSize, 2.4, 0.8, 5.0),
    gapMeters: clampParam(params.fractureGapMeters, 0.12, 0.05, 0.20),
    removalRate: clampParam(params.fractureRemovalRate, 0.01, 0.0, 0.15),
    patchCoverage: clampParam(params.fracturePatchCoverage, 0.18, 0.0, 0.7),
    seed,
  };
  const archStyle: string = params.archStyle || 'double_arch';
  const slickrockAmp = params.slickrockRamps ?? 0.85;
  const boulderDensity = params.boulderField ?? 0.82;
  const rockDetailStrength = params.rockDetailStrength ?? params.rockNoiseStrength ?? 1.0;
  const globalSpanScale = params.archHeightScale ?? 1.0;
  const globalWindowScale = params.windowOpenness ?? 1.0;

  // Resolve active splines (either user-customized splines or style preset splines)
  let baseSplines: ArchSplineControl[] = [];
  if (Array.isArray(params.archSplines) && params.archSplines.length > 0) {
    baseSplines = params.archSplines.map((s: ArchSplineControl) => ({ ...s }));
  } else {
    baseSplines = getDefaultArchSplines(archStyle, seed);
  }

  // If user specified a target total archCount or extra proceduralArchCount, sync the spline array
  const targetArchCount =
    params.archCount !== undefined
      ? Math.max(1, Math.min(8, Math.round(params.archCount)))
      : baseSplines.length + Math.max(0, Math.min(6, Math.round(params.proceduralArchCount ?? 0)));

  if (targetArchCount !== baseSplines.length) {
    baseSplines = syncArchSplinesToCount(baseSplines, targetArchCount, archStyle, seed);
  }

  const activeSplines = baseSplines.filter((s) => s.enabled !== false);
  vol.archSplines = baseSplines;

  const preSplines = activeSplines.map((s) => precomputeArchSpline(s));
  const halfWorld = domain.worldSize * 0.5;
  const maxAllowedH = domain.maxHeight * 0.91;

  // Flanking Stepped Sandstone Buttress Massifs & Fins (non-uniform angular shapes, not smooth cylinders!)
  interface EntradaButtress {
    cx: number;
    cz: number;
    rx: number;
    rz: number;
    cosA: number;
    sinA: number;
    summitH: number;
    wedgeSkew: number;
    stepRatio: number;
    stepSetback: number;
    bevel: number;
  }

  const buttresses: EntradaButtress[] = [
    // Central-right sunlit Entrada turret visible between Primary & Secondary Arch windows
    {
      cx: 54,
      cz: -36,
      rx: 28,
      rz: 22,
      cosA: Math.cos(0.38),
      sinA: Math.sin(0.38),
      summitH: Math.min(maxAllowedH, 164 * globalSpanScale),
      wedgeSkew: 0.24,
      stepRatio: 0.66,
      stepSetback: 9.5,
      bevel: 4.2,
    },
    // Right-foreground massive stepped cliff massif anchoring the upper right of the Primary Arch
    {
      cx: 122,
      cz: 24,
      rx: 46,
      rz: 34,
      cosA: Math.cos(-0.24),
      sinA: Math.sin(-0.24),
      summitH: Math.min(maxAllowedH, 212 * globalSpanScale),
      wedgeSkew: -0.28,
      stepRatio: 0.58,
      stepSetback: 12.0,
      bevel: 3.8,
    },
    // Left-foreground terraced sandstone fin shoulder
    {
      cx: -118,
      cz: 42,
      rx: 42,
      rz: 28,
      cosA: Math.cos(0.46),
      sinA: Math.sin(0.46),
      summitH: Math.min(maxAllowedH, 148 * globalSpanScale),
      wedgeSkew: 0.30,
      stepRatio: 0.62,
      stepSetback: 11.0,
      bevel: 3.6,
    },
    // Far-left jagged canyon fin wall
    {
      cx: -162,
      cz: -52,
      rx: 52,
      rz: 22,
      cosA: Math.cos(0.72),
      sinA: Math.sin(0.72),
      summitH: Math.min(maxAllowedH, 156 * globalSpanScale),
      wedgeSkew: -0.22,
      stepRatio: 0.54,
      stepSetback: 10.5,
      bevel: 3.4,
    },
    // Far-right background mesa fin
    {
      cx: 160,
      cz: -82,
      rx: 50,
      rz: 26,
      cosA: Math.cos(-0.54),
      sinA: Math.sin(-0.54),
      summitH: Math.min(maxAllowedH, 166 * globalSpanScale),
      wedgeSkew: 0.26,
      stepRatio: 0.60,
      stepSetback: 11.5,
      bevel: 3.5,
    },
  ];

  // Foreground fallen Entrada sandstone boulders resting on the sloping slickrock amphitheater
  interface SlickrockBoulder {
    cx: number;
    cz: number;
    rx: number;
    rz: number;
    height: number;
    cosA: number;
    sinA: number;
  }

  const boulders: SlickrockBoulder[] =
    boulderDensity > 0.05
      ? [
          { cx: -38, cz: 78, rx: 11.5, rz: 6.8, height: 7.8 * boulderDensity, cosA: 0.94, sinA: 0.34 },
          { cx: -54, cz: 62, rx: 9.0, rz: 6.5, height: 8.5 * boulderDensity, cosA: 0.88, sinA: -0.47 },
          { cx: -22, cz: 66, rx: 6.5, rz: 4.8, height: 5.4 * boulderDensity, cosA: 0.76, sinA: 0.65 },
          { cx: 36, cz: 68, rx: 12.0, rz: 8.2, height: 8.6 * boulderDensity, cosA: 0.91, sinA: -0.41 },
          { cx: 18, cz: 58, rx: 7.8, rz: 5.5, height: 6.2 * boulderDensity, cosA: 0.82, sinA: 0.57 },
          { cx: 56, cz: 82, rx: 13.5, rz: 8.5, height: 9.2 * boulderDensity, cosA: 0.96, sinA: -0.28 },
          { cx: -6, cz: 48, rx: 5.8, rz: 4.4, height: 4.8 * boulderDensity, cosA: 0.64, sinA: 0.77 },
        ]
      : [];

  const strideY = nx;
  const strideZ = nx * ny;

  // Per-spline scratch arrays for the current (x, z) column to keep the inner Y loop ultra-fast
  const colU = new Float32Array(preSplines.length);
  const colDPerp = new Float32Array(preSplines.length);
  const colDEnd = new Float32Array(preSplines.length);
  const colYTop = new Float32Array(preSplines.length);
  const colYVault0 = new Float32Array(preSplines.length);
  const colYSill = new Float32Array(preSplines.length);
  const colWinDeltaU = new Float32Array(preSplines.length);
  const colWinHalfMeters = new Float32Array(preSplines.length);
  const colHalfWidthBase = new Float32Array(preSplines.length);
  const colDetailAmp = new Float32Array(preSplines.length);
  const colActive = new Uint8Array(preSplines.length);

  for (let z = 0; z < nz; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;

    for (let x = 0; x < nx; x++) {
      const wx = x * voxelSizeXZ - halfWorld;
      const idx2D = zOff2D + x;

      // 1. Analytic slickrock amphitheater: bowl-shaped SDF ground and a broad, smooth ramp.
      // No random displacement is applied to the ground or the sandstone formations.
      const baseFloor = vol.bedrockHeight[idx2D];
      const amphDist = Math.sqrt(wx * wx * 0.68 + (wz - 12.0) * (wz - 12.0));
      const amphMask = Math.max(0.0, Math.min(1.0, 1.0 - amphDist / 225.0));
      const smoothAmph = amphMask * amphMask * (3.0 - 2.0 * amphMask);
      const broadRamp = Math.max(0.0, (110.0 - wz) * 0.085);
      const amphitheaterBowl = slickrockAmp * smoothAmph * (8.0 + broadRamp * 0.62);

      const slickrockFloorY = Math.max(8.0, baseFloor + amphitheaterBowl);

      // Rounded, hand-placed fallen Entrada sandstone blocks on the slickrock apron.
      let boulderPhi2D = 999.0;
      let boulderTopY = slickrockFloorY;
      for (let b = 0; b < boulders.length; b++) {
        const bld = boulders[b];
        const dx = wx - bld.cx;
        const dz = wz - bld.cz;
        const bu = (dx * bld.cosA + dz * bld.sinA) / bld.rx;
        const bv = (-dx * bld.sinA + dz * bld.cosA) / bld.rz;
        const bNorm = Math.pow(bu * bu, 1.8) + Math.pow(bv * bv, 1.8);
        if (bNorm < 2.4) {
          const bDist = (Math.pow(bNorm, 0.28) - 1.0) * Math.min(bld.rx, bld.rz);
          if (bDist < boulderPhi2D) {
            boulderPhi2D = bDist;
            boulderTopY = slickrockFloorY + bld.height;
          }
        }
      }

      vol.bedrockHeight[idx2D] = slickrockFloorY;

      // 2. Sandstone relief is authored from bed thickness and pinch-out profiles below.

      let maxColumnTopY = slickrockFloorY + 12.0;
      let minButtressDist2D = 999.0;
      let minArchWallDist = 999.0;
      let anySplineActive = false;

      // Pre-evaluate all Arch Splines at this (wx, wz) column
      for (let s = 0; s < preSplines.length; s++) {
        const pre = preSplines[s];
        if (wx < pre.minX || wx > pre.maxX || wz < pre.minZ || wz > pre.maxZ) {
          colActive[s] = 0;
          continue;
        }

        const { u, dPerp, dEnd } = projectToArchSpline(pre, wx, wz);
        const ctrl = pre.ctrl;
        const archDetail = (ctrl.rockDetail ?? ctrl.rockNoise ?? 1.0) * rockDetailStrength;
        colDetailAmp[s] = archDetail;

        if (dEnd > 22.0 || dPerp > Math.max(ctrl.buttressRadius, ctrl.finHalfWidth) * 2.25 + 24.0) {
          colActive[s] = 0;
          continue;
        }

        colActive[s] = 1;
        anySplineActive = true;
        colU[s] = u;
        // Keep the editable spline as the structural guide; explicit strata shape the rock surface.
        colDPerp[s] = dPerp;
        colDEnd[s] = dEnd;

        // Compute Yellow Arch (Extrados Top Spine) Elevation Y_top(u)
        const uClamped = Math.max(0.0, Math.min(1.0, u));
        const uc = Math.max(0.18, Math.min(0.82, ctrl.crownPosU));
        let archBell = 0.0;
        if (uClamped <= uc) {
          const t = uClamped / uc;
          archBell = 1.0 - Math.pow(1.0 - t, 1.85);
        } else {
          const t = (uClamped - uc) / (1.0 - uc);
          archBell = 1.0 - Math.pow(t, 1.85);
        }
        const pierLerp = ctrl.pierHeight0 * (1.0 - uClamped) + ctrl.pierHeight1 * uClamped;
        const yTop = Math.min(
          maxAllowedH,
          (pierLerp + (ctrl.crownHeight - pierLerp) * archBell) * globalSpanScale
        );
        colYTop[s] = yTop;
        if (yTop > maxColumnTopY) maxColumnTopY = yTop;

        // Compute Buttress-to-Fin Horizontal Half-Width W_half(u) with strong non-uniform swelling/pinching
        const pierDist0 = Math.max(0.0, 1.0 - Math.abs(u - 0.02) / 0.30);
        const pierDist1 = Math.max(0.0, 1.0 - Math.abs(u - 0.98) / 0.30);
        const pierBlend = Math.max(pierDist0 * pierDist0, pierDist1 * pierDist1);
        const halfW =
          ctrl.finHalfWidth +
          (ctrl.buttressRadius - ctrl.finHalfWidth) * pierBlend;
        colHalfWidthBase[s] = Math.max(8.5, halfW);

        // Compute Blue Arch (Intrados Window Opening) Coordinate deltaU
        // Supports asymmetric left/right window spans when windowCenterU is shifted!
        const winHalfU = Math.max(0.14, Math.min(0.45, ctrl.windowWidthFrac * 0.5 * globalWindowScale));
        const deltaU = (u - ctrl.windowCenterU) / winHalfU;
        colWinDeltaU[s] = deltaU;
        colWinHalfMeters[s] = winHalfU * pre.spanLength;

        // Compute Blue Arch Ceiling Y_vault0(u) and Rocky V-Saddle Sill Floor Y_sill(u)
        const minBridge = Math.max(14.5, ctrl.bridgeThickness);
        const rawApex = Math.min(yTop - minBridge, ctrl.windowApexHeight * globalSpanScale);
        // Rocky V-shaped saddle at the bottom of the Blue Arch window
        const ySill =
          ctrl.sillHeight +
          11.0 * Math.pow(Math.min(1.15, Math.abs(deltaU)), 1.6);
        colYSill[s] = ySill;

        const vaultPow = Math.max(1.25, Math.min(3.0, ctrl.vaultPower ?? 1.95));
        const vaultProfile = Math.pow(
          Math.max(0.0, 1.0 - Math.pow(Math.min(1.5, Math.abs(deltaU)), vaultPow)),
          0.78
        );
        const yVault0 = ySill + Math.max(10.0, rawApex - ySill) * vaultProfile;
        colYVault0[s] = Math.min(yTop - minBridge, yVault0);

        // Track 2D distance to solid buttress piers (outside the open Blue Arch window) for talus skirts
        const horizFinDist = Math.max(colDPerp[s] - colHalfWidthBase[s], colDEnd[s]);
        const absWall = Math.abs(horizFinDist);
        if (absWall < minArchWallDist) minArchWallDist = absWall;

        if (Math.abs(deltaU) > 0.96) {
          if (horizFinDist < minButtressDist2D) {
            minButtressDist2D = horizFinDist;
          }
        } else {
          // Inside Blue Arch window span: keep monolithDist positive so ConicalTalusSkirt never plugs the window!
          const distToNearestPier = (1.02 - Math.abs(deltaU)) * colWinHalfMeters[s];
          const safeDist = Math.max(horizFinDist, distToNearestPier * 0.75);
          if (safeDist < minButtressDist2D) {
            minButtressDist2D = safeDist;
          }
        }
      }

      // 3. Evaluate Flanking Stepped Entrada Buttress Massifs at this (wx, wz)
      let activeButtressCount = 0;
      for (let t = 0; t < buttresses.length; t++) {
        const but = buttresses[t];
        const dx = wx - but.cx;
        const dz = wz - but.cz;
        if (Math.abs(dx) > but.rx * 2.1 || Math.abs(dz) > but.rz * 2.1) continue;
        activeButtressCount++;
        if (but.summitH > maxColumnTopY) maxColumnTopY = but.summitH;

        const u = (dx * but.cosA + dz * but.sinA) / but.rx;
        const v = (-dx * but.sinA + dz * but.cosA) / but.rz;
        const vSkewed = v / Math.max(0.48, 1.0 + but.wedgeSkew * Math.max(-1.2, Math.min(1.2, u)));
        const pNorm = Math.pow(Math.pow(u * u, 2.4) + Math.pow(vSkewed * vSkewed, 2.4), 1.0 / 4.8);
        const hDist = (pNorm - 1.0) * Math.min(but.rx, but.rz);
        if (hDist < minButtressDist2D) minButtressDist2D = hDist;
        if (Math.abs(hDist) < minArchWallDist) minArchWallDist = Math.abs(hDist);
      }

      if (!anySplineActive && activeButtressCount === 0 && boulderPhi2D > 12.0) {
        const yMin = Math.max(0, Math.floor(slickrockFloorY / voxelSizeY) - 4);
        const yMax = Math.min(ny - 2, Math.ceil(slickrockFloorY / voxelSizeY) + 4);
        bandMinY[idx2D] = Math.min(bandMinY[idx2D], yMin);
        bandMaxY[idx2D] = Math.max(bandMaxY[idx2D], yMax);
        for (let y = 1; y <= yMax + 2 && y < ny; y++) {
          const wy = y * voxelSizeY;
          const idx3D = zOff3D + y * strideY + x;
          sdfGrid[idx3D] = Math.min(sdfGrid[idx3D], wy - slickrockFloorY);
        }
        continue;
      }

      vol.monolithDist[idx2D] = Math.min(vol.monolithDist[idx2D], minButtressDist2D);
      vol.monolithSummitH[idx2D] = Math.max(vol.monolithSummitH[idx2D], maxColumnTopY);
      const wallProximity = Math.max(0.0, Math.min(1.0, 1.0 - minArchWallDist / 24.0));
      vol.cliffMask[idx2D] = Math.max(vol.cliffMask[idx2D], wallProximity * 0.95);

      const yMaxCol = Math.min(ny - 2, Math.ceil((maxColumnTopY + 14.0) / voxelSizeY));
      bandMinY[idx2D] = Math.min(bandMinY[idx2D], 1);
      bandMaxY[idx2D] = Math.max(bandMaxY[idx2D], yMaxCol);

      // 4. Inner 3D Voxel Loop: Construct Craggy Fins & Stepped Buttresses -> Carve 1-to-1 Blue Arch Windows -> Protect Overhead Arch Ribbons
      for (let y = 1; y <= yMaxCol; y++) {
        const wy = y * voxelSizeY;
        const idx3D = zOff3D + y * strideY + x;

        // Base sloping slickrock amphitheater floor
        let phi = Math.min(sdfGrid[idx3D], wy - slickrockFloorY);

        // Foreground fallen sandstone boulders
        if (boulderPhi2D < 8.0) {
          const phiBoulder = smoothMax(boulderPhi2D, wy - boulderTopY, 1.6);
          phi = smoothMin(phi, phiBoulder, 2.0);
        }

        // No noise displacement: surface relief comes from discrete, non-uniform sediment beds.
        // Each bed is a smooth SDF ledge/recess with an authored dip and lateral pinch-out.

        // Flanking Stepped Entrada Sandstone Buttresses
        if (activeButtressCount > 0) {
          for (let t = 0; t < buttresses.length; t++) {
            const but = buttresses[t];
            const dx = wx - but.cx;
            const dz = wz - but.cz;
            if (Math.abs(dx) > but.rx * 2.0 || Math.abs(dz) > but.rz * 2.0) continue;

            const u = (dx * but.cosA + dz * but.sinA) / but.rx;
            const v = (-dx * but.sinA + dz * but.cosA) / but.rz;
            const vSkewed = v / Math.max(0.48, 1.0 + but.wedgeSkew * Math.max(-1.2, Math.min(1.2, u)));
            const pNorm = Math.pow(Math.pow(u * u, 2.4) + Math.pow(vSkewed * vSkewed, 2.4), 1.0 / 4.8);
            const hRel = Math.max(
              0.0,
              Math.min(1.2, (wy - slickrockFloorY) / Math.max(20.0, but.summitH - slickrockFloorY))
            );
            const buttressBedRelief = evaluateArchBedRelief(
              (u + 1.0) * 0.5,
              hRel,
              rockDetailStrength,
              t
            );
            // Stepped horizontal bench setback + non-uniform hard/soft bed contacts
            const benchTransition = Math.max(0.0, Math.min(1.0, (hRel - but.stepRatio) / 0.07));
            const benchSetback =
              benchTransition * benchTransition * (3.0 - 2.0 * benchTransition) * but.stepSetback;
            const hDist =
              (pNorm - 1.0) * Math.min(but.rx, but.rz) +
              benchSetback -
              buttressBedRelief;
            const phiButtress = smoothMax(hDist, wy - but.summitH, but.bevel);
            phi = smoothMin(phi, phiButtress, 4.0);
          }
        }

        // Step 4A: Union Solid Craggy Arch Fins & Buttress Piers
        if (anySplineActive) {
          for (let s = 0; s < preSplines.length; s++) {
            if (!colActive[s]) continue;
            const yTop = colYTop[s];
            if (wy > yTop + 10.0) continue;

            const dPerp = colDPerp[s];
            const dEnd = colDEnd[s];
            const halfWBase = colHalfWidthBase[s];
            const archDetail = colDetailAmp[s];
            const localHeight = (wy - slickrockFloorY) / Math.max(18.0, yTop - slickrockFloorY);
            const bedRelief = evaluateArchBedRelief(colU[s], localHeight, archDetail, s);

            // Authored caprock ledges and recessed weak beds shape the SDF wall directly.
            const distBelowCrest = Math.max(0.0, yTop - wy);
            const crestRoundTaper = Math.max(0.0, 12.0 - distBelowCrest) * 0.24;
            const halfW = Math.max(7.5, halfWBase - crestRoundTaper + bedRelief);

            const dHoriz = smoothMax(dPerp - halfW, dEnd, 3.6);
            const phiFin = smoothMax(dHoriz, wy - yTop, 3.8);
            phi = smoothMin(phi, phiFin, 4.2);
          }

          // Step 4B: Carve the exact 3D Blue Arch window, with a clean spline-defined ceiling.
          for (let s = 0; s < preSplines.length; s++) {
            if (!colActive[s]) continue;
            const deltaU = colWinDeltaU[s];
            if (Math.abs(deltaU) > 1.35) continue;

            const ctrl = preSplines[s].ctrl;
            const dPerp = colDPerp[s];
            // Wide through-corridor limit so adjacent buttresses or turrets never block the back of an arch window!
            const tunnelLimit = Math.max(62.0, ctrl.buttressRadius * 1.55 + ctrl.finHalfWidth);
            if (dPerp > tunnelLimit + 8.0) continue;

            const yVault0 = colYVault0[s];
            const ySill = colYSill[s];
            const yTop = colYTop[s];

            // Dual-sided Conchoidal Alcove Flaring:
            const flareNorm = Math.max(
              0.0,
              Math.min(1.0, (dPerp - 2.0) / Math.max(6.0, ctrl.finHalfWidth * 1.15))
            );
            const flareSmooth = flareNorm * flareNorm * (3.0 - 2.0 * flareNorm) * ctrl.alcoveFlare;

            // Concentric conchoidal exfoliation spall steps & 3D scalloped vault roughness
            const spallStep =
              flareSmooth > 0.12 ? Math.sin(flareSmooth * Math.PI * 3.0) * 2.2 : 0.0;
            const minBridge = Math.max(13.5, ctrl.bridgeThickness - flareSmooth * 4.5);
            const yVaultFlared = Math.min(
              yTop - minBridge,
              yVault0 + flareSmooth * 8.5 + spallStep
            );
            if (wy > yVaultFlared + 8.0 || wy < ySill - 8.0) continue;

            // Exact 1-to-1 signed distance inside the Blue Arch Curve:
            // - dCeil < 0 when wy is below the Blue Arch curve yVaultFlared(u)
            // - dFloor < 0 when wy is above the rocky sill floor ySill(u)
            // - dSpan < 0 when u is between the Blue Arch Left Foot and Right Foot (|deltaU| < 1)
            const spanFlare = 1.0 + 0.14 * flareSmooth;
            const dCeil = wy - yVaultFlared;
            const dFloor = ySill - wy;
            const dSpan = (Math.abs(deltaU) - spanFlare) * colWinHalfMeters[s];

            const phiVault2D = smoothMax(smoothMax(dCeil, dSpan, 5.2), dFloor, 4.0);
            const phiTunnel = smoothMax(phiVault2D, dPerp - tunnelLimit, 4.0);

            // Subtract the 3D Blue Arch window tunnel from the solid rock
            phi = smoothMax(phi, -phiTunnel, 3.2);
          }

          // Step 4C: Protect Every Arch's Overhead Bridge Ribbon [Y_vault0 .. Y_top] so Intersecting Arches Never Sever Each Other!
          for (let s = 0; s < preSplines.length; s++) {
            if (!colActive[s]) continue;
            const deltaU = colWinDeltaU[s];
            if (Math.abs(deltaU) > 1.06) continue;

            const yTop = colYTop[s];
            const yVault0 = colYVault0[s];
            if (wy < yVault0 - 1.5 || wy > yTop + 5.0) continue;

            const dPerp = colDPerp[s];
            const halfW = Math.max(7.5, colHalfWidthBase[s] - 1.5);
            const phiBridge = smoothMax(
              Math.max(dPerp - halfW, colDEnd[s]),
              Math.max(wy - yTop, yVault0 - wy),
              3.0
            );
            phi = smoothMin(phi, phiBridge, 2.5);
          }
        }

        sdfGrid[idx3D] = phi;
      }
    }
  }

  normalizeSDFNearSurface(sdfGrid, nx, ny, nz, voxelSizeXZ, bandMinY, bandMaxY);
}

/**
 * Combined Monument Buttes Generator (when used as a single all-in-one generator node)
 */
export function applyMonumentButtesGenerator(
  vol: SDFTerrainVolume,
  nodeId: string,
  stampStyle: 'monument_buttes' | 'mesa_plateau' | 'needle_spires',
  params: GeneratorParams
): void {
  applyDesertPedimentNode(vol, nodeId, params);
  applySDFMonolithTowersNode(vol, nodeId, stampStyle, params);
  applyVerticalJointFissuresNode(vol, nodeId, params);
  applyBasalWindOverhangsNode(vol, nodeId, params);
  applyConicalTalusSkirtNode(vol, nodeId, params);
}

export function applyGeneratorNode(
  vol: SDFTerrainVolume,
  nodeId: string,
  generatorType:
    | 'simplex'
    | 'perlin'
    | 'value'
    | 'multifractal'
    | 'cellular'
    | 'white'
    | 'monument_buttes'
    | 'mesa_plateau'
    | 'needle_spires',
  params: GeneratorParams
): void {
  if (
    generatorType === 'monument_buttes' ||
    generatorType === 'mesa_plateau' ||
    generatorType === 'needle_spires'
  ) {
    applyMonumentButtesGenerator(vol, nodeId, generatorType, params);
    return;
  }

  vol.has3DMonoliths = false;
  const { nx, nz, voxelSizeXZ, domain } = vol;
  const { wavelength, regionalComplexity } = resolveEffectiveWavelength(
    params.scale ?? 18,
    domain.worldSize
  );

  const report = evaluateResolutionForOperation(
    domain,
    nodeId,
    'generator',
    wavelength / Math.pow(params.lacunarity || 2, Math.max(0, (params.octaves || 6) - 2))
  );
  vol.reports[nodeId] = report;

  const noise = new SeededNoise(params.seed || 4217);
  const warpNoise = new SeededNoise((params.seed || 4217) + 7919);
  const basinNoise = new SeededNoise((params.seed || 4217) + 3571);
  const nyquistMin = Math.max(voxelSizeXZ * 2.1, report.nyquistMinFeatureMeters);
  const halfWorld = domain.worldSize * 0.5;

  for (let z = 0; z < nz; z++) {
    const worldZ = z * voxelSizeXZ - halfWorld;

    for (let x = 0; x < nx; x++) {
      const worldX = x * voxelSizeXZ - halfWorld;
      const idx = z * nx + x;

      let wx = worldX;
      let wz = worldZ;
      if (params.warpStrength > 0.01) {
        const warpFreq1 = 1.0 / (wavelength * 0.95);
        const warpFreq2 = 1.0 / (wavelength * 0.42);
        const w1x = warpNoise.simplex2D(worldX * warpFreq1, worldZ * warpFreq1);
        const w1z = warpNoise.simplex2D((worldX + 271) * warpFreq1, (worldZ - 189) * warpFreq1);
        const w2x = warpNoise.simplex2D((worldX - 113) * warpFreq2, (worldZ + 97) * warpFreq2) * 0.35;
        const w2z = warpNoise.simplex2D((worldX + 419) * warpFreq2, (worldZ + 311) * warpFreq2) * 0.35;

        const warpAmp = params.warpStrength * 1.65;
        wx += (w1x + w2x) * warpAmp;
        wz += (w1z + w2z) * warpAmp;
      }

      let val = 0;
      if (generatorType === 'multifractal') {
        const res = noise.ridgedMultifractal2D(
          wx,
          wz,
          wavelength,
          Math.min(7, params.octaves || 6),
          params.lacunarity || 2.05,
          params.gain || 0.52,
          nyquistMin,
          params.ridgeSharpness || 1.45
        );

        const bFreq = (1.65 * regionalComplexity) / domain.worldSize;
        const warpVal = warpNoise.simplex2D(worldX * bFreq * 0.85, worldZ * bFreq * 0.85) * 1.35;
        const ridgeChainA = Math.sin(
          (worldX * 0.78 - worldZ * 0.62) * bFreq * Math.PI + warpVal
        );
        const ridgeChainB = basinNoise.simplex2D(
          wx * bFreq * 1.15 + 43.2,
          wz * bFreq * 1.15 - 19.8
        );

        const basinEnvelope = Math.max(
          0.08,
          Math.min(1.0, 0.54 + 0.30 * ridgeChainA + 0.28 * ridgeChainB)
        );
        const smoothBasin = basinEnvelope * basinEnvelope * (3.0 - 2.0 * basinEnvelope);

        const valleyPower = 1.0 + (params.valleyFloor ?? 0.4) * 0.85;
        val = Math.pow(res.value, valleyPower) * (0.22 + 0.78 * smoothBasin);
      } else if (generatorType === 'cellular') {
        const freq = 1.0 / Math.max(nyquistMin * 3.0, wavelength * 0.65);
        const cell = noise.cellular2D(wx * freq, wz * freq, 0.82);
        const fbm = noise.fbm2D(wx, wz, wavelength * 0.75, 4, 2.0, 0.5, nyquistMin, 'simplex').value;
        val = (1.0 - cell.f1 * 0.7) * 0.55 + cell.edge * 0.25 + fbm * 0.2;
      } else if (generatorType === 'white') {
        const gx = Math.floor(worldX / Math.max(nyquistMin, wavelength * 0.1));
        const gz = Math.floor(worldZ / Math.max(nyquistMin, wavelength * 0.1));
        val = noise.white2D(gx, gz) * 0.15;
      } else {
        const res = noise.fbm2D(
          wx,
          wz,
          wavelength,
          params.octaves || 6,
          params.lacunarity || 2.0,
          params.gain || 0.5,
          nyquistMin,
          generatorType
        );
        const bFreq = (1.5 * regionalComplexity) / domain.worldSize;
        const basinMod =
          0.55 + 0.45 * basinNoise.simplex2D(wx * bFreq + 17.3, wz * bFreq - 29.1);
        val = Math.pow(res.value, 1.3) * basinMod;
      }

      const hNew = Math.max(
        10.0,
        Math.min(domain.maxHeight * 0.9, 12.0 + val * params.amplitude * 1.12)
      );

      const prev = vol.bedrockHeight[idx];
      if (params.blendMode === 'add') {
        vol.bedrockHeight[idx] = Math.min(domain.maxHeight * 0.9, prev + hNew * 0.45);
      } else if (params.blendMode === 'max') {
        vol.bedrockHeight[idx] = Math.max(prev, hNew);
      } else if (params.blendMode === 'multiply') {
        vol.bedrockHeight[idx] = (prev * hNew) / Math.max(1.0, params.amplitude);
      } else {
        vol.bedrockHeight[idx] = hNew;
      }
    }
  }

  syncHeightfieldToSDF(vol);
}

export interface SDFCaveParams {
  seed: number;
  caveScale: number;        // Spatial wavelength of cave network (meters)
  tunnelRadius: number;     // Tunnel radius in meters
  cavernStrength: number;   // Large karst chambers [0..1]
  elevationMin: number;     // Min height ratio [0..1]
  elevationMax: number;     // Max height ratio [0..1]
  overhangUndercut: number; // Cliff overhang intensity [0..1]
}

export function applySDFCavesAndOverhangs(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: SDFCaveParams
): void {
  const { nx, ny, nz, voxelSizeXZ, voxelSizeY, domain, sdfGrid, bandMinY, bandMaxY } = vol;

  const report = evaluateResolutionForOperation(
    domain,
    nodeId,
    'cave',
    params.tunnelRadius * 2.0
  );
  vol.reports[nodeId] = report;

  if (!report.allowed) {
    return;
  }

  const noiseA = new SeededNoise(params.seed || 6073);
  const noiseB = new SeededNoise((params.seed || 6073) + 2137);
  const halfWorld = domain.worldSize * 0.5;

  const effectiveRadius = Math.max(voxelSizeXZ * 1.85, Math.min(22.0, params.tunnelRadius || 13.0));
  const freq = 1.0 / Math.max(voxelSizeXZ * 8.0, (params.caveScale || 110) * 1.25);

  const minY = Math.max(14.0, domain.maxHeight * (params.elevationMin || 0.1));
  const maxY = domain.maxHeight * Math.min(0.68, params.elevationMax || 0.58);

  const strideY = nx;
  const strideZ = nx * ny;
  vol.caveMask2D.fill(0);

  for (let z = 3; z < nz - 3; z++) {
    const worldZ = z * voxelSizeXZ - halfWorld;
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;

    for (let x = 3; x < nx - 3; x++) {
      const worldX = x * voxelSizeXZ - halfWorld;
      const idx2D = zOff2D + x;
      const surfH = Math.max(getTotalSurfaceHeight(vol, idx2D), vol.monolithSummitH[idx2D]);
      const isSteepCliff = vol.cliffMask[idx2D] > 0.28;

      const requiredRoof = isSteepCliff ? effectiveRadius * 1.25 : effectiveRadius * 2.15;
      const maxAllowedCaveY = Math.min(maxY, surfH - requiredRoof);

      if (maxAllowedCaveY <= minY + effectiveRadius * 0.6) continue;

      const yStart = Math.max(2, Math.floor((minY - effectiveRadius) / voxelSizeY));
      const yEnd = Math.min(ny - 3, Math.ceil((maxAllowedCaveY + effectiveRadius) / voxelSizeY));

      let carvedAnyInColumn = false;

      for (let y = yStart; y <= yEnd; y++) {
        const worldY = y * voxelSizeY;
        const idx3D = zOff3D + y * strideY + x;
        const currentPhi = sdfGrid[idx3D];

        const n1 = noiseA.simplex3D(worldX * freq, worldY * freq * 1.4, worldZ * freq);
        const n2 = noiseB.simplex3D(
          (worldX + 97.3) * freq,
          (worldY - 53.1) * freq * 1.4,
          (worldZ + 181.7) * freq
        );

        const wormDist = Math.sqrt(n1 * n1 + n2 * n2) * (params.caveScale || 110) * 0.36;

        const floorFade = Math.min(1.0, Math.max(0.0, (worldY - minY) / (effectiveRadius * 1.2)));
        const roofFade = Math.min(
          1.0,
          Math.max(0.0, (maxAllowedCaveY - worldY) / (effectiveRadius * 1.3))
        );
        const env = Math.sqrt(floorFade * roofFade);

        const localRadius = effectiveRadius * env * (0.85 + 0.3 * (params.cavernStrength ?? 0.6));
        if (localRadius <= voxelSizeXZ * 0.5) continue;

        const cavePhi = wormDist - localRadius;
        const carvedAirPhi = -cavePhi;

        if (carvedAirPhi > currentPhi - voxelSizeXZ * 0.8) {
          const k = voxelSizeXZ * 0.75;
          const h = Math.max(0, k - Math.abs(currentPhi - carvedAirPhi)) / k;
          const nextPhi = Math.max(currentPhi, carvedAirPhi) + h * h * k * 0.25;
          if (nextPhi > currentPhi) {
            sdfGrid[idx3D] = nextPhi;
            if (nextPhi > -voxelSizeXZ) {
              carvedAnyInColumn = true;
            }
          }
        }
      }

      if (carvedAnyInColumn) {
        vol.caveMask2D[idx2D] = 1.0;
        if (yStart < bandMinY[idx2D]) {
          bandMinY[idx2D] = Math.max(1, yStart - 1);
        }
      }
    }
  }

  normalizeSDFNearSurface(sdfGrid, nx, ny, nz, voxelSizeXZ, bandMinY, bandMaxY);
}
