import { SeededNoise } from './noise';
import { evaluateResolutionForOperation, normalizeSDFNearSurface } from './resolutionGuard';
import { SDFTerrainVolume, syncHeightfieldToSDF, getTotalSurfaceHeight } from './terrainState';

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
  flatness: number;     // 0 = rounded dome pinnacle, 1 = flat mesa table
  overhangDirX: number; // Windward basal overhang direction
  overhangDirZ: number;
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
    domain.maxHeight * 0.88,
    params.towerHeight ?? params.amplitude ?? 182
  );
  const density = params.butteDensity ?? 0.75;
  const footprintScale = params.footprintScale ?? 1.0;
  const regionalAzimuth = 0.36;

  let rng = (seed * 1664525 + 1013904223) | 0;
  const rand = () => {
    rng = (rng * 1664525 + 1013904223) | 0;
    return (rng >>> 0) / 4294967296.0;
  };

  // Hero monolith complexes arranged with wide desert sightlines (matching 9ef6842f38a230fd469ac3683b7ca2c5.jpg)
  const clusterAnchors: Array<{ x: number; z: number; scale: number; hMult: number }> = [
    // Central hero towering multi-turret monolith
    { x: -24, z: -16, scale: 1.15 * footprintScale, hMult: 1.0 },
    // Left twin-column castle monolith
    { x: -152, z: -86, scale: 1.06 * footprintScale, hMult: 0.92 },
    // Right foreground overhanging cliff massif
    { x: 144, z: 92, scale: 1.26 * footprintScale, hMult: 0.98 },
    // Distant center-left background silhouette butte
    { x: -92, z: -172, scale: 0.98 * footprintScale, hMult: 0.84 },
    // Right-midground sentinel tower complex
    { x: 118, z: -84, scale: 1.05 * footprintScale, hMult: 0.89 },
    // Foreground-left subsidiary pinnacle cluster
    { x: -138, z: 106, scale: 0.94 * footprintScale, hMult: 0.82 },
    // Far-right background mesa block
    { x: 36, z: -168, scale: 1.12 * footprintScale, hMult: 0.87 },
  ];

  const extraClusters = Math.max(0, Math.round((density - 0.5) * 4));
  for (let c = 0; c < extraClusters; c++) {
    clusterAnchors.push({
      x: (rand() - 0.5) * domain.worldSize * 0.74,
      z: (rand() - 0.5) * domain.worldSize * 0.74,
      scale: 0.85 + rand() * 0.35,
      hMult: 0.72 + rand() * 0.25,
    });
  }

  const widthScale =
    stampStyle === 'mesa_plateau' ? 1.5 : stampStyle === 'needle_spires' ? 0.72 : 1.0;

  const subTowers: SubTowerStamp[] = [];
  for (let c = 0; c < clusterAnchors.length; c++) {
    const anchor = clusterAnchors[c];
    const numSub = 5;
    for (let s = 0; s < numSub; s++) {
      const isMainSpire = s === 0;
      const isDetachedThumb = s === numSub - 1;
      const spread = isMainSpire ? 0 : isDetachedThumb ? 44 * anchor.scale : 25 * anchor.scale;
      const offsetAngle = s * 1.38 + rand() * 0.45;

      const cx = anchor.x + Math.cos(offsetAngle) * spread;
      const cz = anchor.z + Math.sin(offsetAngle) * spread;

      // Minimum radius >= 14m (7+ voxels) so sub-towers are solid blocky monuments, never thin needles!
      const baseRx = Math.max(
        14.0,
        (isMainSpire ? 28 : isDetachedThumb ? 16 : 20 + rand() * 11) * anchor.scale * widthScale
      );
      const baseRz = Math.max(
        14.0,
        (isMainSpire ? 24 : isDetachedThumb ? 15 : 18 + rand() * 10) * anchor.scale * widthScale
      );

      const angle = regionalAzimuth + (rand() - 0.5) * 0.24;
      const heightStep = isMainSpire
        ? 1.0
        : isDetachedThumb
        ? 0.76 + rand() * 0.12
        : 0.64 + s * 0.08 + rand() * 0.07;

      const summitH = Math.min(
        domain.maxHeight * 0.90,
        15.0 + maxAmp * anchor.hMult * Math.min(1.0, heightStep)
      );

      const windAngle = 0.55 + (rand() - 0.5) * 0.5;
      subTowers.push({
        cx,
        cz,
        rx: baseRx,
        rz: baseRz,
        cosA: Math.cos(angle),
        sinA: Math.sin(angle),
        summitH,
        flatness: stampStyle === 'mesa_plateau' ? 0.92 : isMainSpire ? 0.48 : 0.80,
        overhangDirX: Math.cos(windAngle),
        overhangDirZ: Math.sin(windAngle),
      });
    }
  }

  vol.has3DMonoliths = true;
  const strideY = nx;
  const strideZ = nx * ny;

  for (let z = 0; z < nz; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;

    for (let x = 0; x < nx; x++) {
      const wx = x * voxelSizeXZ - halfWorld;
      const idx2D = zOff2D + x;
      const floorH = vol.bedrockHeight[idx2D];

      // Smooth low-frequency blocky wall warp (Nyquist-safe wavelength >= 40m)
      const wallWarpX = wallNoise.simplex2D(wx * 0.016, wz * 0.016) * 6.5;
      const wallWarpZ = wallNoise.simplex2D(wx * 0.016 + 91.3, wz * 0.016 - 57.1) * 6.5;

      let minHorizDist = 999.0;
      let blendedSummitH = floorH;
      let towerWeightSum = 0.0;
      let towerHeightSum = 0.0;

      for (let t = 0; t < subTowers.length; t++) {
        const st = subTowers[t];
        const dx = wx + wallWarpX - st.cx;
        const dz = wz + wallWarpZ - st.cz;

        const maxR = Math.max(st.rx, st.rz) * 2.8;
        if (Math.abs(dx) > maxR || Math.abs(dz) > maxR) continue;

        const u = dx * st.cosA - dz * st.sinA;
        const v = dx * st.sinA + dz * st.cosA;

        // L4 super-quadric bevelled rectangular sandstone block distance in meters
        const ux = Math.abs(u) / st.rx;
        const vz = Math.abs(v) / st.rz;
        const norm4 = Math.pow(ux * ux * ux * ux + vz * vz * vz * vz, 0.25);
        const avgR = 0.5 * (st.rx + st.rz);
        const dTower = (norm4 - 1.0) * avgR;

        // Smoothly union horizontal distances of overlapping sub-towers in a cluster
        minHorizDist = smoothMin(minHorizDist, dTower, 6.0);

        if (dTower < 18.0) {
          const w = Math.exp(-Math.max(0.0, dTower + 6.0) * 0.22);
          const domeCrown =
            (1.0 - st.flatness) * Math.min(11.0, Math.max(0.0, -dTower) * 0.45) +
            noise.simplex2D(wx * 0.032 + t * 7.1, wz * 0.032) * 2.2;
          towerWeightSum += w;
          towerHeightSum += (st.summitH + domeCrown) * w;
        }
      }

      if (towerWeightSum > 1e-5) {
        blendedSummitH = towerHeightSum / towerWeightSum;
      }

      vol.monolithDist[idx2D] = minHorizDist;
      vol.monolithSummitH[idx2D] = blendedSummitH;

      // Update 3D SDF column using True 3D CSG Union of Floor and Monolith Solid!
      if (minHorizDist < 24.0) {
        const maxH = Math.max(floorH, blendedSummitH);
        const yMin = Math.max(0, Math.floor(floorH / voxelSizeY) - 3);
        const yMax = Math.min(ny - 2, Math.ceil(maxH / voxelSizeY) + 4);
        bandMinY[idx2D] = Math.min(bandMinY[idx2D], yMin);
        bandMaxY[idx2D] = Math.max(bandMaxY[idx2D], yMax);

        if (minHorizDist < 6.0) {
          vol.cliffMask[idx2D] = Math.max(
            vol.cliffMask[idx2D],
            Math.max(0.0, Math.min(1.0, 1.0 - Math.abs(minHorizDist) / 14.0))
          );
        }

        // Slight inward taper toward the summit (natural 86°–88° monument wall batter)
        const towerSpan = Math.max(30.0, blendedSummitH - floorH);

        for (let y = 0; y < ny; y++) {
          const wy = y * voxelSizeY;
          const phiFloor = wy - floorH;
          const hRel = Math.max(0.0, Math.min(1.2, (wy - floorH) / towerSpan));
          const wallTaper = hRel * 3.2; // Gently narrows by ~3.2m from base to crown

          // 3D CSG intersection of vertical monolith column and horizontal summit caprock:
          const phiWall = minHorizDist + wallTaper;
          const phiCap = wy - blendedSummitH;
          const phiMonolith = smoothMax(phiWall, phiCap, 3.8);

          // Smooth 3D CSG union with the desert floor
          let phi = smoothMin(phiFloor, phiMonolith, 4.5);
          if (y === 0) phi = Math.min(phi, -voxelSizeY);
          sdfGrid[zOff3D + y * strideY + x] = phi;
        }
      }
    }
  }
}

/**
 * NEW NODE 3: Vertical Joint Fissures & Chimneys (`VerticalJointFissures`)
 * Slices deep orthogonal vertical tectonic cracks, chimneys, and master horizontal
 * bedding notches into the 3D SDF monolith walls (like the split columns in 9ef6842f38a230fd469ac3683b7ca2c5.jpg).
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

  const fissureStrength = params.fissureIntensity ?? params.fissureDepth ?? params.verticalFissures ?? 0.85;
  const chimneyMeters = (params.chimneyDepth ?? 11.5) * 0.6;
  const jointSpacing = Math.max(18.0, (params.fissureSpacing ?? params.jointSpacing ?? 22.0) * 1.4);
  const masterNotchDepth = params.beddingNotchStrength ?? params.masterNotch ?? 0.82;
  const flutingStrength = params.columnFluting ?? fissureStrength;
  const fissureNoise = new SeededNoise((params.seed || 4217) + 5119);

  const halfWorld = domain.worldSize * 0.5;
  const strideY = nx;
  const strideZ = nx * ny;
  const regionalAzimuth = 0.36;
  const cosA = Math.cos(regionalAzimuth);
  const sinA = Math.sin(regionalAzimuth);
  const freq1 = Math.PI / jointSpacing;
  const freq2 = Math.PI / (jointSpacing * 0.82);

  for (let z = 2; z < nz - 2; z++) {
    const wz = z * voxelSizeXZ - halfWorld;
    const zOff3D = z * strideZ;
    const zOff2D = z * nx;

    for (let x = 2; x < nx - 2; x++) {
      const idx2D = zOff2D + x;
      const mDist = vol.monolithDist[idx2D];
      if (mDist < -18.0 || mDist > 12.0) continue;

      const wx = x * voxelSizeXZ - halfWorld;
      const floorH = vol.bedrockHeight[idx2D];
      const summitH = vol.monolithSummitH[idx2D];
      const towerSpan = Math.max(30.0, summitH - floorH);

      // Rotate into tectonic joint coordinate system
      const jU = wx * cosA - wz * sinA;
      const jV = wx * sinA + wz * cosA;
      const warp1 = fissureNoise.simplex2D(wx * 0.015, wz * 0.015) * 8.0;
      const warp2 = fissureNoise.simplex2D(wx * 0.022 + 47.3, wz * 0.022 - 31.8) * 6.0;

      // Smooth Nyquist-safe V-shaped vertical joint grooves (width ~ 8m-12m)
      const s1 = Math.abs(Math.sin((jU + warp1) * freq1));
      const s2 = Math.abs(Math.sin((jV + warp2) * freq2));
      const groove1 = Math.pow(Math.max(0.0, 1.0 - s1 / 0.26), 2.0);
      const groove2 = Math.pow(Math.max(0.0, 1.0 - s2 / 0.22), 2.0) * 0.82;
      const chimneyCarveMeters = Math.max(groove1, groove2) * fissureStrength * chimneyMeters;

      // Broad columnar fluting along the vertical wall
      const columnFlute =
        fissureNoise.simplex2D(wx * 0.055 + 13.1, wz * 0.055 - 29.7) * 1.45 * flutingStrength;

      const yStart = Math.max(2, bandMinY[idx2D]);
      const yEnd = Math.min(ny - 3, bandMaxY[idx2D]);

      for (let y = yStart; y <= yEnd; y++) {
        const idx3D = zOff3D + y * strideY + x;
        const phi = sdfGrid[idx3D];
        if (Math.abs(phi) > 14.0) continue;

        const wy = y * voxelSizeY;
        const hRel = (wy - floorH) / towerSpan;
        if (hRel < 0.08) continue;

        // Vertical chimneys deepen toward the upper 75% of the monolith, splitting turrets apart!
        const verticalProfile = Math.min(1.0, Math.max(0.0, (hRel - 0.08) / 0.35));

        // Master horizontal bedding notches at hRel ≈ 0.52 and hRel ≈ 0.72 (like the left monolith!)
        const notch1 = Math.exp(-Math.pow((hRel - 0.52) / 0.028, 2.0)) * masterNotchDepth * 3.4;
        const notch2 = Math.exp(-Math.pow((hRel - 0.72) / 0.022, 2.0)) * masterNotchDepth * 2.4;

        sdfGrid[idx3D] = phi + (chimneyCarveMeters * verticalProfile + columnFlute + notch1 + notch2);
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

      const yStart = Math.max(2, bandMinY[idx2D]);
      const yEnd = Math.min(ny - 3, bandMaxY[idx2D]);

      for (let y = yStart; y <= yEnd; y++) {
        const wy = y * voxelSizeY;
        const hRel = (wy - floorH) / towerSpan;
        if (hRel < 0.06 || hRel > alcoveHeightRatio + 0.14) continue;

        const idx3D = zOff3D + y * strideY + x;
        const phi = sdfGrid[idx3D];
        if (Math.abs(phi) > 12.0) continue;

        if (hRel <= alcoveHeightRatio) {
          // Deep inward basal alcove carve (positive Δφ turns solid rock into overhanging air cavity!)
          const bell = Math.sin(((hRel - 0.06) / Math.max(0.08, alcoveHeightRatio - 0.06)) * Math.PI);
          const caveMod =
            0.65 + 0.45 * overhangNoise.simplex3D(wx * 0.018, wy * 0.024, wz * 0.018);
          const carveInwardMeters = bell * caveMod * windExposure * undercutStrength * 5.8;
          sdfGrid[idx3D] = phi + carveInwardMeters;
        } else {
          // Protruding overhanging rock belly right above the basal alcove
          const browT = (hRel - alcoveHeightRatio) / 0.14;
          const browBell = Math.sin(browT * Math.PI);
          const pushOutMeters = browBell * windExposure * browStrength * 2.8;
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

      // Radial rockfall chutes along the talus cone
      const chuteMod =
        0.82 +
        0.32 *
          ribRoughness *
          chuteNoise.simplex2D(wx * 0.042 + 19.3, wz * 0.042 - 37.1);
      const localSpan = apronSpanBase * chuteMod;

      if (mDist < localSpan) {
        const distFromWall = Math.max(0.0, mDist + 2.0);
        const t = Math.max(0.0, 1.0 - distFromWall / (localSpan + 2.0));
        // Smooth concave-upward 34° scree apron profile + fallen caprock boulder bumps
        const boulderBump =
          Math.max(0.0, chuteNoise.simplex2D(wx * 0.085 - 41.2, wz * 0.085 + 63.7)) *
          1.8 *
          boulderRough *
          t;
        const talusH = Math.pow(t, 1.55) * apronMaxHeight * chuteMod + boulderBump;
        vol.talusHeight[idx2D] = Math.max(vol.talusHeight[idx2D], talusH);

        const topTalusY = floorH + vol.talusHeight[idx2D];
        const yMaxTalus = Math.min(ny - 2, Math.ceil((topTalusY + 6.0) / voxelSizeY));
        if (yMaxTalus > bandMaxY[idx2D]) bandMaxY[idx2D] = yMaxTalus;

        // Smoothly union the conical talus skirt ONLY below topTalusY + 4m so overhanging cliffs above stay untouched!
        for (let y = 1; y <= yMaxTalus; y++) {
          const wy = y * voxelSizeY;
          const phiTalus = wy - topTalusY;
          const idx3D = zOff3D + y * strideY + x;
          sdfGrid[idx3D] = smoothMin(sdfGrid[idx3D], phiTalus, 3.2);
        }
      }
    }
  }
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
