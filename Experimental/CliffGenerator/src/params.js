// Parameter defaults, presets and the control schema the inspector is built from.
// Groups map 1:1 onto outliner rows; `stage` says what has to be recomputed when a value changes:
//   'terrain' → full heightfield pipeline (worker), 'rocks' → rock placement only, 'live' → uniforms.

export const defaults = {
  // Landform
  seed: 428,
  resolution: 512,
  worldSize: 2048,          // [m]
  mountainHeight: 520,      // [m]
  baseElevation: 40,        // [m]
  baseFrequency: 1.7,
  ridgeSharpness: 2.0,
  peakPower: 1.35,
  warpStrength: 0.6,
  reliefFrequency: 1.4,
  reliefContrast: 0.6,
  plateauStrength: 0,
  plateauHeight: 380,       // [m]
  canyonDepth: 0,           // [m]
  cliffHeight: 0,           // [m]
  cliffSharpness: 0.8,
  cliffStacks: 0,
  canyonWidth: 260,         // [m]
  duneAmount: 0,            // [m]
  duneWavelength: 140,      // [m]
  duneDirection: 30,        // [°]
  duneAsymmetry: 0.68,
  duneCoverage: 0.6,
  // Rugged outcrops (lateral push–pull of the steep faces)
  ruggedOn: 1,
  ruggedAmount: 14,         // [m]
  ruggedScale: 70,          // [m]
  ruggedBlockiness: 0.7,
  ruggedLedges: 0.5,
  ruggedBands: 3,
  ruggedSlope: 28,          // [°]
  ruggedAfter: 0.5,
  // Boulder outcrops (tors / core-stone clusters unioned into the landform)
  outcropDensity: 0,
  outcropSize: 16,          // [m]
  outcropSpacing: 220,      // [m]
  outcropCount: 5,
  outcropSpread: 1.6,
  outcropAspect: 0.7,
  outcropBury: 0.4,
  outcropRidge: 0.5,
  outcropSlopeMax: 30,      // [°]
  outcropWeather: 0.5,
  // Strata
  strataBand: 26,           // [m]
  strataStrength: 0.75,
  strataVariation: 0.6,
  strataPackaging: 0.6,
  strataHardShare: 0.35,
  strataLateral: 0.26,
  strataRecut: 0.5,
  strataDip: 4,             // [°]
  strataDipDirection: 35,   // [°]
  hardnessContrast: 0.8,
  // Erosion
  fluvialStrength: 0.7,
  fluvialIterations: 40,
  fluvialConcavity: 0.5,
  fluvialUplift: 0.3,
  fluvialFill: 0.8,
  fluvialDeposition: 0.6,
  fluvialSedimentShow: 2.5,
  fluvialPits: 8,
  stackHeight: 0,           // [m] Gaea-style rock stacks
  stackLevels: 6,
  stackScale: 220,          // [m]
  stackTaper: 0.5,
  stackSharpness: 0.8,
  stackChaos: 0.5,
  stackSpires: 0,
  stackCoverage: 0.5,
  stackPedestal: 0.4,
  stackSizeVar: 0.6,        // spread of stack size / height (0 = all alike, 1 = few big + many small)
  stackElongation: 0.4,
  stackCluster: 0.5,
  cliffProtect: 0,          // steep faces keep their pre-erosion shape (sea cliffs, quarry walls)
  cliffProtectAngle: 38,    // [°]
  rillStrength: 0.5,
  rillSteps: 6,
  fluvialDiffusion: 0.3,
  droplets: 180000,
  inertia: 0.08,
  sedimentCapacity: 5,
  erodeSpeed: 0.35,
  depositSpeed: 0.25,
  evaporation: 0.015,
  gravity: 4,
  dropletLifetime: 40,
  erosionRadius: 3,
  initialSpeed: 1,
  hardnessInfluence: 0.85,
  thermalIterations: 24,
  thermalRate: 0.5,
  talusSoft: 33,            // [°]
  talusHard: 78,            // [°]
  // Rocks (plain scatter)
  rocksEnabled: 0,          // the placer is off by default while the cliff faces are finished
  rockSeed: 7,
  rockDensity: 0.35,
  rockSlopeMin: 0,          // [°]
  rockSlopeMax: 55,         // [°]
  rockClustering: 0.5,
  rockClusterScale: 80,     // [m]
  rockTilt: 0.7,
  rockSizeMin: 0.8,         // [m]
  rockSizeMax: 9,           // [m]
  rockAngularity: 0.7,
  rockEmbed: 0.45,
  // where each rock comes from (the placer reads the simulated fields; these are the shares)
  rockInPlace: 0.8,         // bedrock breaking off in place on bare faces
  rockTalus: 0.85,          // the apron shed below a cliff
  rockChannel: 0.7,         // coarse lag the stream still has power to move
  rockBars: 0.6,            // where the flow slows: bars, canyon mouths, fans
  rockCaps: 0.5,            // resistant caps and tors left armoring the high ground
  rockErratics: 0.22,       // rare solitary wanderers on the flats
  rockShore: 0.55,          // cobble cord laid along standing water
  rockSoilHide: 0.7,
  rockSizeSpread: 0.7,
  pebblesOn: 0,
  pebbleDensity: 0.45,
  pebbleSize: 0.45,         // [m]
  // True-3D cliffs (SDF chunks)
  sdfOn: 1,
  sdfAngle: 58,             // [°]
  sdfBlend: 2,              // [cells]
  sdfChunk: 16,             // [cells]
  sdfVoxel: 0,              // voxels per cell (0 = auto from budget)
  sdfVoxelBudget: 36,       // [M voxels]
  sdfUndercut: 6,           // [m]
  sdfBedContrast: 1,
  sdfPockets: 12,           // [m]
  sdfPits: 0.3,
  sdfJoints: 0.5,
  sdfRough: 0.5,
  sdfPushPull: 2.5,         // [m]
  sdfLean: 0,               // [m]
  sdfVertical: 0,
  sdfLeanReach: 24,         // [m]
  sdfPushScale: 18,         // [m]
  sdfBlocks: 0,
  sdfBlockSwing: 0.6,       // how far the block lattice swings from world-aligned             // [m] rock blocks: per-block in/out of the face
  sdfBlockSize: 8,          // [m]
  sdfBlockLoss: 0.12,
  sdfMaxChunks: 400,
  // Cliff depth (mesh displacement)
  overhang: 3.5,            // [m]
  buttress: 0.5,
  ledgeNoise: 0.6,
  meshSubdivision: 3,
  detailRelief: 0.5,        // [m]
  detailScale: 7,           // [m]
  rockyAmount: 1,           // [m]
  rockyScale: 4,            // [m]
  rockyAngular: 0.7,
  cragAmount: 2.5,          // [m]
  cragScale: 12,            // [m]
  detailCliffBias: 0.8,
  // Rock material
  palette: 'granite',
  rockA: '#b3b1ae', rockB: '#94918f', rockC: '#c7c2ba', fresh: '#d6d3cd', oxide: '#ac947b',
  strataOn: 1,
  strataContrast: 0.7,
  strataBandScale: 1,
  seamStrength: 0.35,
  seamWidth: 0.08,
  laminae: 0.6,
  bedGradient: 0.6,
  hardnessTint: 0.8,
  grainOn: 1,
  // Jointing (cracks that break the face into blocks)
  jointOn: 1,
  jointStrength: 0.7,
  jointSpacing: 3,          // [m]
  texWarp: 0.55,            // how far the cell lattices are warped and stretched
  jointBend: 0.6,           // how much the joint sets swing across the massif
  jointVary: 0.7,           // block size varies panel to panel (the true-3D domain grid)
  facePatch: 0.5,           // some panels are massive, some are busy
  jointWidth: 0.22,         // [m]
  jointDepth: 0.35,         // [m]
  jointStagger: 1,
  jointBlocks: 0.6,
  jointDropout: 0.3,
  grainSize: 0.9,           // [m]
  grainStrength: 0.6,
  grainContrast: 0.5,
  grainFineness: 0.5,
  oxideOn: 1,
  oxideAmount: 0.5,
  oxideScale: 5,            // [m]
  cavityStrength: 0.8,
  bumpScale: 1,
  baseRoughness: 0.9,
  // Mineral flakes — three layers, each with its own colour / size / height / crystals
  flakesOn: 1,
  flakeStrength: 0.7,
  flakeSheen: 0.35,
  flakeEdge: 0.3,
  flakeOxide: 0.5,
  flakeSparkle: 0.6,
  flakeOn1: 1, flakeColor1: '#9d9893', flakeSize1: 1.6, flakeDensity1: 0.7, flakeHeight1: 6, flakeCrystal1: 0.0, flakeVar1: 0.5, flakeShape1: 0.2, flakeReveal1: 0.0,
  flakeOn2: 1, flakeColor2: '#bdb4a7', flakeSize2: 0.55, flakeDensity2: 0.75, flakeHeight2: 4, flakeCrystal2: 0.08, flakeVar2: 0.5, flakeShape2: 0.1, flakeReveal2: 0.3,
  flakeOn3: 1, flakeColor3: '#d9d6cf', flakeSize3: 0.18, flakeDensity3: 0.6, flakeHeight3: 2, flakeCrystal3: 0.3, flakeVar3: 0.6, flakeShape3: 0.0, flakeReveal3: 0.0,
  // Spalling (peel)
  peelOn: 1,
  peelStrength: 0.6,
  peelScale: 1.6,           // [m]
  peelCoverage: 0.3,
  peelThickness: 0.025,     // [m]
  peelSecond: 0.6,
  peelPits: 0.5,
  peelBedding: 0.4,
  peelFresh: 0.7,
  peelShadow: 0.6,
  // Ground cover
  runoffOn: 1,
  wetness: 0.6,
  streakScale: 6,           // [m]
  streakAmount: 1,
  gravelOn: 1,
  gravelAmount: 0.8,
  gravelScale: 0.14,        // [m]
  gravelRelief: 0.7,
  gravelVariation: 0.6,
  gravelColor: '#9a938a',
  vegOn: 1,
  vegetation: 0.6,
  vegScale: 20,             // [m]
  vegSlope: 44,             // [°]
  soilOn: 1,
  soilAmount: 0.85,
  soilSlope: 40,            // [°]
  soilClods: 0.7,
  soilMoisture: 0.8,
  soilAlluvium: 1,
  soilColor: '#5d4d3c',
  soilLight: '#917b60',
  // sediment: water- and weathering-laid fill, painted in beds (its own shader layer)
  sedimentOn: 1,
  sedimentAmount: 0.85,
  sedBedScale: 0.5,         // [m] typical bed thickness
  sedBands: 0.75,           // bed-to-bed contrast: how crisply the beds read
  sedLaminae: 0.5,          // cross-lamination inside a bed
  sedFacies: 0.7,           // sorting strength: gravel / sand / mud
  sedGrainBias: 0,          // -1 = all silt … +1 = all gravel, whatever the flow
  sedRipple: 0.45,
  sedRippleScale: 0.8,      // [m] ripple wavelength
  sedCracks: 0.3,           // mud cracks on the quiet flats
  sedGrain: 0.55,
  sedSlope: 48,             // [deg] flatter than this the fill is not scoured off
  sedDir: 35,               // [deg] the direction the ripples and bars align to
  sedColorGravel: '#8f8579',
  sedColorSand: '#c9b184',
  sedColorSilt: '#a08a6a',
  vegPatchiness: 0.5,
  dryness: 0.5,
  grassA: '#6f8051', grassB: '#a8a26f', dryColor: '#ac9c7b',
  mossOn: 1,
  mossiness: 0.5,
  mossScale: 4,             // [m]
  mossColor: '#687b4c',
  snowOn: 1,
  snowLine: 430,            // [m]
  snowSlope: 48,            // [°]
  snowSoftness: 1,
  snowRoughness: 0.75,
  snowColor: '#eef1f7',
  // Drawn features (roads · rivers · lakes)
  features: { roads: [], rivers: [], lakes: [] },
  showFeatureLines: 1,
  roadWidth: 6,             // [m]
  roadShoulder: 2,          // [m]
  roadSmoothing: 60,        // [m]
  roadCut: 60,              // [°]
  roadFill: 34,             // [°]
  roadColor: '#5f5b55',
  roadShading: 1,
  riverWidth: 14,           // [m]
  riverDepth: 4,            // [m]
  riverBank: 24,            // [°]
  riverMaxBank: 10,         // [m]
  riverMeander: 0.4,
  riverErosion: 0.6,
  riverWater: 1,
  riverWaterDepth: 1.2,     // [m]
  // Simulated drainage
  riverSim: 1,
  riverCatchment: 0.04,     // [km²]
  riverWidthScale: 22,      // [m per √km²]
  riverMaxWidth: 80,        // [m]
  riverFloodplain: 0.5,
  riverBankErosion: 0.6,
  riverBankSteps: 5,
  riverDepthScale: 0.7,
  riverWaterFrac: 0.7,
  riverDrySlope: 12,        // [°]
  riverDryBig: 1.5,         // [km²]
  riverBraiding: 0.35,
  riverLakes: 0,
  riverLakeFill: 0.8,
  riverLakeMax: 8,          // [% of map]
  riverLakeMin: 0.01,       // [km²]
  riverGuideFlow: 1.0,      // [km²]
  lakeDepth: 6,             // [m]
  lakeLevelOffset: 6,       // [m]
  lakeWater: 1,
  // Water
  waterMeshes: 0,
  waterColor: '#15303c',
  waterOpacity: 0.9,
  riverColor: '#1d4552',
  riverShallowColor: '#4f7f7a',
  riverOpacity: 0.85,
  riverClearDepth: 1.5,
  riverFoam: 1,
  shoreWet: 0.7,
  bedShading: 1,
  siltColor: '#b9ad94',
  // Sun & sky
  sunAzimuth: 215,          // [°]
  sunElevation: 24,         // [°]
  turbidity: 4,
  exposure: 0.55,
  fogDensity: 0.35,
  seaLevel: 0,              // [m]
  waterEnabled: 1,
  // Viewport
  wireframe: 0,
  showRocks: 1,
  autoRotate: 0,
  debugView: 0,
};

export const presets = {
  'Alpine granite': {
    palette: 'granite', mountainHeight: 560, baseFrequency: 1.7, ridgeSharpness: 2.2, peakPower: 1.4, warpStrength: 0.6,
    strataBand: 26, strataStrength: 0.6, strataDip: 6, hardnessContrast: 0.7, plateauStrength: 0, canyonDepth: 0,
    snowLine: 430, vegetation: 0.6, seaLevel: 0, waterEnabled: 1, talusSoft: 34, talusHard: 80, droplets: 180000, peelStrength: 0.45, peelCoverage: 0.3, flakeStrength: 0.65,
  },
  'Sandstone mesa': {
    palette: 'sandstone', mountainHeight: 320, baseElevation: 60, baseFrequency: 1.3, ridgeSharpness: 1.4, peakPower: 1.0, warpStrength: 0.45,
    strataBand: 18, strataStrength: 1.0, strataDip: 2, hardnessContrast: 1.0, plateauStrength: 0.85, plateauHeight: 300,
    canyonDepth: 0, snowLine: 5000, vegetation: 0.15, seaLevel: -100, waterEnabled: 0, talusSoft: 30, talusHard: 86,
    droplets: 140000, erodeSpeed: 0.3, peelStrength: 0.7, peelScale: 3.0, peelCoverage: 0.45, flakeStrength: 0.4,
    sunElevation: 32, sunAzimuth: 240, turbidity: 6,
    riverLakeMax: 0.5, fluvialFill: 0.8,
  },
  'Canyon': {
    palette: 'sandstone', mountainHeight: 260, baseElevation: 240, baseFrequency: 1.1, ridgeSharpness: 1.2, peakPower: 0.9, warpStrength: 0.4,
    plateauStrength: 0.9, plateauHeight: 420, canyonDepth: 380, canyonWidth: 420,
    strataBand: 16, strataStrength: 1.0, strataDip: 1.5, hardnessContrast: 1.0, snowLine: 5000, vegetation: 0.1, seaLevel: -20, waterEnabled: 1,
    talusSoft: 31, talusHard: 86, droplets: 160000,
    peelStrength: 0.7, peelScale: 3.2, peelCoverage: 0.45, flakeStrength: 0.4, sunElevation: 38, sunAzimuth: 200, turbidity: 5,
    riverLakeMax: 0.5, fluvialFill: 0.8,
  },
  'Sea cliffs': {
    palette: 'basalt', mountainHeight: 300, baseElevation: 20, baseFrequency: 1.4, ridgeSharpness: 1.6, peakPower: 1.1, warpStrength: 0.5,
    reliefContrast: 0.9, strataBand: 22, strataStrength: 0.85, strataDip: 8, strataDipDirection: 120, hardnessContrast: 0.9,
    plateauStrength: 0.5, plateauHeight: 220, canyonDepth: 0, snowLine: 5000, vegetation: 0.9, mossiness: 0.8, seaLevel: 70, waterEnabled: 1,
    talusSoft: 35, talusHard: 84, droplets: 150000,
    peelStrength: 0.5, peelScale: 1.8, flakeStrength: 0.7, sunElevation: 18, sunAzimuth: 290, turbidity: 3, fogDensity: 0.5,
  },
  'Coastal cliffs': {
    palette: 'shale', worldSize: 1024, mountainHeight: 110, baseElevation: -25, baseFrequency: 1.0, ridgeSharpness: 1.3, peakPower: 1.0, warpStrength: 0.4,
    reliefFrequency: 0.7, reliefContrast: 0.6, cliffHeight: 100, cliffSharpness: 1, cliffStacks: 0.6, plateauStrength: 0.3, plateauHeight: 150, canyonDepth: 0,
    ruggedAmount: 20, ruggedScale: 60, ruggedBlockiness: 0.8, ruggedLedges: 0.3, ruggedSlope: 30, ruggedAfter: 0.4,
    strataBand: 7, strataStrength: 0.9, strataVariation: 0.5, strataHardShare: 0.6, strataDip: 1.5, hardnessContrast: 0.5, strataRecut: 0.6,
    cliffProtect: 0.9, fluvialStrength: 0.65, fluvialIterations: 35, droplets: 60000, erodeSpeed: 0.3, thermalIterations: 25, talusSoft: 40, talusHard: 89, rillStrength: 0.45,
    snowOn: 0, vegetation: 0.95, vegSlope: 32, vegPatchiness: 0.2, mossiness: 0.25, soilAmount: 0.6, dryness: 0.15,
    seaLevel: 0, waterEnabled: 1, waterMeshes: 0, riverCatchment: 0.4, riverLakes: 0,
    sdfOn: 1, sdfAngle: 50, sdfUndercut: 3, sdfJoints: 0.5, sdfPits: 0.1, sdfRough: 0.25, sdfPushPull: 1, sdfPushScale: 14,
    sdfBlocks: 2.5, sdfBlockSize: 9, sdfBlockLoss: 0.12, sdfVertical: 1.15, sdfLean: 1.5, sdfLeanReach: 30, sdfChunk: 32, sdfMaxChunks: 800, overhang: 2,
    jointStrength: 1.0, jointSpacing: 2.2, jointWidth: 0.2, jointDepth: 0.4, jointStagger: 1, jointBlocks: 0.7, jointDropout: 0.3,
    peelStrength: 0.25, flakeStrength: 0.3, oxideAmount: 0.25, wetness: 0.8, rockDensity: 0.45, rockSlopeMax: 30, rockSizeMin: 0.6, rockSizeMax: 6, gravelAmount: 1,
    sunElevation: 38, sunAzimuth: 235, turbidity: 3, fogDensity: 0.3,
  },
  'Quarry walls': {
    palette: 'shale', worldSize: 1024, mountainHeight: 90, baseElevation: 20, baseFrequency: 1.0, ridgeSharpness: 1.2, peakPower: 1.0, warpStrength: 0.3,
    reliefFrequency: 0.8, reliefContrast: 0.9, cliffHeight: 70, cliffSharpness: 1, plateauStrength: 0.6, plateauHeight: 100, canyonDepth: 0,
    ruggedAmount: 14, ruggedScale: 45, ruggedBlockiness: 0.9, ruggedLedges: 0.5, ruggedSlope: 30, ruggedAfter: 0.5,
    strataBand: 6, strataStrength: 0.8, strataVariation: 0.4, strataHardShare: 0.6, strataDip: 4, hardnessContrast: 0.6, strataRecut: 0.5,
    cliffProtect: 0.9, fluvialStrength: 0.4, fluvialIterations: 25, droplets: 40000, erodeSpeed: 0.25, thermalIterations: 20, talusSoft: 40, talusHard: 89, rillStrength: 0.3,
    snowOn: 0, vegetation: 0.35, vegSlope: 30, vegPatchiness: 0.7, mossiness: 0.1, soilAmount: 0.7, dryness: 0.4,
    seaLevel: -100, waterEnabled: 0, riverCatchment: 0.6, riverLakes: 0,
    sdfOn: 1, sdfAngle: 50, sdfUndercut: 2, sdfJoints: 0.6, sdfPits: 0.05, sdfRough: 0.2, sdfPushPull: 1, sdfPushScale: 10,
    sdfBlocks: 3, sdfBlockSize: 7, sdfBlockLoss: 0.1, sdfVertical: 1.1, sdfLean: 1, sdfLeanReach: 24, sdfMaxChunks: 800, overhang: 1.5,
    jointStrength: 1.2, jointSpacing: 1.8, jointWidth: 0.18, jointDepth: 0.45, jointStagger: 0.6, jointBlocks: 0.8, jointDropout: 0.25,
    peelStrength: 0.3, flakeStrength: 0.35, oxideAmount: 0.3, wetness: 0.4, rockDensity: 0.9, rockSlopeMax: 35, rockSizeMin: 0.5, rockSizeMax: 8, gravelAmount: 1,
    sunElevation: 42, sunAzimuth: 200, turbidity: 3, fogDensity: 0.15,
  },
  'Desert stacks': {
    palette: 'sandstone', worldSize: 1024, mountainHeight: 30, baseElevation: 40, baseFrequency: 1.2, ridgeSharpness: 1.0, peakPower: 1.4, warpStrength: 0.3,
    reliefFrequency: 1.2, reliefContrast: 0.4, plateauStrength: 0, canyonDepth: 0, cliffHeight: 0,
    stackHeight: 140, stackLevels: 7, stackScale: 190, stackTaper: 0.5, stackSharpness: 0.85, stackChaos: 0.7, stackSpires: 0.35, stackCoverage: 0.65, stackPedestal: 0.5,
    stackSizeVar: 0.9, stackElongation: 0.6, stackCluster: 0.5,
    ruggedAmount: 10, ruggedScale: 40, ruggedBlockiness: 0.7, ruggedLedges: 0.5, ruggedSlope: 30, ruggedAfter: 0.4,
    strataBand: 9, strataStrength: 1, strataVariation: 0.5, strataHardShare: 0.55, strataDip: 1, hardnessContrast: 0.9, strataRecut: 0.6,
    cliffProtect: 0, fluvialStrength: 0.35, fluvialIterations: 25, droplets: 60000, erodeSpeed: 0.3, thermalIterations: 30, talusSoft: 33, talusHard: 85, rillStrength: 0.35,
    snowOn: 0, vegetation: 0.05, vegSlope: 25, mossiness: 0, soilAmount: 0.5, soilColor: '#8a5a3c', dryness: 0.8,
    seaLevel: -100, waterEnabled: 0, riverWater: 0, riverCatchment: 0.1, riverLakes: 0,
    sdfOn: 1, sdfAngle: 55, sdfUndercut: 4, sdfJoints: 0.5, sdfPits: 0.2, sdfRough: 0.3, sdfPushPull: 1.5, sdfPushScale: 12,
    sdfBlocks: 1.5, sdfBlockSize: 7, sdfBlockLoss: 0.08, sdfVertical: 0, sdfLean: 0, sdfChunk: 32, sdfMaxChunks: 300, overhang: 2,
    jointStrength: 0.8, jointSpacing: 2.5, jointWidth: 0.2, jointDepth: 0.35, jointStagger: 1, jointBlocks: 0.6, jointDropout: 0.35,
    peelStrength: 0.3, flakeStrength: 0.2, oxideAmount: 0.75, wetness: 0.1, rockDensity: 0.6, rockSlopeMax: 32, rockSizeMin: 0.5, rockSizeMax: 7, gravelAmount: 1,
    sunElevation: 46, sunAzimuth: 225, turbidity: 7, fogDensity: 0.15,
  },
  'Stack cliffs': {
    palette: 'sandstone', worldSize: 1024, mountainHeight: 25, baseElevation: 40, baseFrequency: 1.2, ridgeSharpness: 1.0, peakPower: 1.4, warpStrength: 0.3,
    reliefFrequency: 1.2, reliefContrast: 0.4, plateauStrength: 0, canyonDepth: 0, cliffHeight: 0,
    stackHeight: 150, stackLevels: 5, stackScale: 240, stackTaper: 0.3, stackSharpness: 1, stackChaos: 0.55, stackSpires: 0.25, stackCoverage: 0.6, stackPedestal: 0.25,
    stackSizeVar: 0.8, stackElongation: 0.5, stackCluster: 0.32,
    ruggedAmount: 14, ruggedScale: 45, ruggedBlockiness: 0.9, ruggedLedges: 0.5, ruggedSlope: 30, ruggedAfter: 0.5,
    strataBand: 7, strataStrength: 0.9, strataVariation: 0.4, strataHardShare: 0.6, strataDip: 1, hardnessContrast: 0.7, strataRecut: 0.5,
    cliffProtect: 0.9, fluvialStrength: 0.4, fluvialIterations: 25, droplets: 40000, erodeSpeed: 0.25, thermalIterations: 20, talusSoft: 36, talusHard: 89, rillStrength: 0.3,
    snowOn: 0, vegetation: 0.08, vegSlope: 25, mossiness: 0, soilAmount: 0.5, soilColor: '#8a5a3c', dryness: 0.75,
    seaLevel: -100, waterEnabled: 0, riverWater: 0, riverCatchment: 0.1, riverLakes: 0,
    sdfOn: 1, sdfAngle: 50, sdfUndercut: 2.5, sdfJoints: 0.6, sdfPits: 0.05, sdfRough: 0.2, sdfPushPull: 1, sdfPushScale: 10,
    sdfBlocks: 3, sdfBlockSize: 7, sdfBlockLoss: 0.1, sdfVertical: 1.1, sdfLean: 1, sdfLeanReach: 24, sdfChunk: 32, sdfMaxChunks: 300, overhang: 1.5,
    jointStrength: 1.1, jointSpacing: 1.8, jointWidth: 0.18, jointDepth: 0.45, jointStagger: 0.6, jointBlocks: 0.8, jointDropout: 0.25,
    peelStrength: 0.3, flakeStrength: 0.25, oxideAmount: 0.7, wetness: 0.1, rockDensity: 0.7, rockSlopeMax: 35, rockSizeMin: 0.5, rockSizeMax: 8, gravelAmount: 1,
    sunElevation: 42, sunAzimuth: 215, turbidity: 6, fogDensity: 0.12,
  },
  'Limestone escarpment': {
    palette: 'limestone', mountainHeight: 420, baseElevation: 30, baseFrequency: 1.2, ridgeSharpness: 1.7, peakPower: 1.2, warpStrength: 0.7,
    reliefContrast: 0.95, strataBand: 30, strataStrength: 0.9, strataDip: 10, strataDipDirection: 300, hardnessContrast: 0.9,
    plateauStrength: 0.35, plateauHeight: 400, canyonDepth: 0, snowLine: 5000, vegetation: 0.75, mossiness: 0.5, seaLevel: 0, waterEnabled: 0,
    talusSoft: 33, talusHard: 82, droplets: 170000,
    peelStrength: 0.55, peelScale: 2.4, flakeStrength: 0.5, sunElevation: 28, sunAzimuth: 160, turbidity: 4,
    riverLakeMax: 2,
  },
};

Object.assign(presets, {
  'Fjord': {
    palette: 'slate', mountainHeight: 720, baseElevation: -140, baseFrequency: 1.1, ridgeSharpness: 2.6, peakPower: 1.5, warpStrength: 0.7,
    reliefContrast: 0.95, strataBand: 40, strataStrength: 0.4, strataDip: 8, hardnessContrast: 0.5, plateauStrength: 0, canyonDepth: 0,
    snowOn: 1, snowLine: 520, vegetation: 0.5, mossiness: 0.7, seaLevel: 0, waterEnabled: 1, talusSoft: 36, talusHard: 84, droplets: 160000,
    rockDensity: 0.3, peelStrength: 0.35, sunElevation: 20, sunAzimuth: 300, turbidity: 3, fogDensity: 0.55,
  },
  'Badlands': {
    palette: 'sandstone', mountainHeight: 180, baseElevation: 80, baseFrequency: 2.6, ridgeSharpness: 1.3, peakPower: 0.9, warpStrength: 0.5,
    reliefFrequency: 2.2, reliefContrast: 0.5, strataBand: 9, strataStrength: 1, strataDip: 1, hardnessContrast: 0.9, plateauStrength: 0.6, plateauHeight: 220,
    canyonDepth: 0, snowOn: 0, vegetation: 0.05, mossiness: 0.1, waterEnabled: 0, seaLevel: -100, droplets: 320000, erodeSpeed: 0.6, sedimentCapacity: 7, evaporation: 0.01,
    talusSoft: 32, talusHard: 80, rockDensity: 0.15, oxideAmount: 0.8, peelStrength: 0.3, sunElevation: 40, sunAzimuth: 215, turbidity: 7, fogDensity: 0.25, riverWater: 0, riverLakes: 0, riverCatchment: 0.08,
    riverLakeMax: 0.3, fluvialFill: 0.8,
  },
  'Dolomite towers': {
    palette: 'limestone', mountainHeight: 650, baseElevation: 120, baseFrequency: 2.0, ridgeSharpness: 3.2, peakPower: 1.9, warpStrength: 0.45,
    reliefContrast: 1, strataBand: 34, strataStrength: 0.9, strataDip: 3, hardnessContrast: 1, plateauStrength: 0, canyonDepth: 0,
    snowOn: 1, snowLine: 560, vegetation: 0.7, mossiness: 0.3, waterEnabled: 0, seaLevel: -100, talusSoft: 35, talusHard: 88, droplets: 150000,
    overhang: 5, rockDensity: 0.35, sunElevation: 30, sunAzimuth: 190, turbidity: 4, fogDensity: 0.3,
    riverLakeMax: 1,
  },
  'Desert buttes': {
    palette: 'sandstone', mountainHeight: 240, baseElevation: 40, baseFrequency: 1.0, ridgeSharpness: 1.2, peakPower: 1.6, warpStrength: 0.3,
    reliefFrequency: 1.8, reliefContrast: 1, plateauStrength: 1, plateauHeight: 230, canyonDepth: 0, strataBand: 14, strataStrength: 1, hardnessContrast: 1,
    snowOn: 0, vegetation: 0.03, mossiness: 0, waterEnabled: 0, seaLevel: -100, talusSoft: 31, talusHard: 88, droplets: 120000, thermalIterations: 40,
    rockDensity: 0.4, oxideAmount: 0.7, sunElevation: 48, sunAzimuth: 230, turbidity: 8, fogDensity: 0.2, riverWater: 0, riverLakes: 0, riverCatchment: 0.08,
  },
  'Volcanic island': {
    palette: 'basalt', mountainHeight: 520, baseElevation: -90, baseFrequency: 0.8, ridgeSharpness: 1.5, peakPower: 1.8, warpStrength: 0.5,
    reliefFrequency: 0.8, reliefContrast: 1, strataBand: 22, strataStrength: 0.5, hardnessContrast: 0.6, plateauStrength: 0, canyonDepth: 0,
    snowOn: 0, vegetation: 0.9, mossiness: 0.8, seaLevel: 0, waterEnabled: 1, droplets: 240000, erodeSpeed: 0.45, talusSoft: 33, talusHard: 80,
    rockDensity: 0.3, fogDensity: 0.4, turbidity: 5, sunElevation: 35, sunAzimuth: 120,
  },
  'Highland glens': {
    palette: 'granite', mountainHeight: 380, baseElevation: 20, baseFrequency: 1.3, ridgeSharpness: 1.6, peakPower: 1.1, warpStrength: 0.8,
    reliefContrast: 0.7, strataBand: 30, strataStrength: 0.35, hardnessContrast: 0.5, plateauStrength: 0, canyonDepth: 0,
    snowOn: 0, vegetation: 0.95, mossiness: 0.8, dryness: 0.6, seaLevel: -10, waterEnabled: 1, droplets: 220000, talusSoft: 34, talusHard: 78,
    rockDensity: 0.5, peelStrength: 0.3, sunElevation: 22, sunAzimuth: 250, turbidity: 4, fogDensity: 0.5,
  },
  'Boulder field': {
    palette: 'granite', mountainHeight: 420, baseElevation: 30, baseFrequency: 1.5, ridgeSharpness: 1.4, peakPower: 1.0, warpStrength: 0.7,
    reliefContrast: 0.6, strataBand: 36, strataStrength: 0.3, hardnessContrast: 0.4, plateauStrength: 0, canyonDepth: 0,
    snowOn: 0, vegetation: 0.25, mossiness: 0.5, dryness: 0.7, waterEnabled: 0, seaLevel: -100, droplets: 200000, thermalIterations: 40, talusSoft: 36, talusHard: 76,
    rockDensity: 0.95, rockSlopeMax: 50, rockClustering: 0.35, rockSizeMin: 1.5, rockSizeMax: 16, rockEmbed: 0.4, pebbleDensity: 0.7,
    outcropDensity: 0.35, outcropSize: 12, outcropCount: 4, outcropSpacing: 200, outcropRidge: 0.3, outcropWeather: 0.6,
    peelStrength: 0.4, sunElevation: 30, sunAzimuth: 225, turbidity: 4, fogDensity: 0.25,
  },
  'Scree slopes': {
    palette: 'slate', mountainHeight: 600, baseElevation: 40, baseFrequency: 1.4, ridgeSharpness: 2.6, peakPower: 1.5, warpStrength: 0.55,
    reliefContrast: 0.9, strataBand: 24, strataStrength: 0.6, strataDip: 12, hardnessContrast: 0.8, plateauStrength: 0, canyonDepth: 0,
    snowOn: 1, snowLine: 620, vegetation: 0.15, mossiness: 0.3, waterEnabled: 0, seaLevel: -100, droplets: 160000, thermalIterations: 60, thermalRate: 0.7, talusSoft: 37, talusHard: 84,
    rockDensity: 0.8, rockSlopeMin: 15, rockSlopeMax: 48, rockClustering: 0.2, rockSizeMin: 0.5, rockSizeMax: 6, pebbleDensity: 0.9, pebbleSize: 0.6, gravelAmount: 1,
    peelStrength: 0.5, sunElevation: 26, sunAzimuth: 200, turbidity: 3, fogDensity: 0.35,
    riverLakeMax: 1,
  },
  'Granite domes': {
    palette: 'granite', mountainHeight: 520, baseElevation: 60, baseFrequency: 0.9, ridgeSharpness: 1.1, peakPower: 1.7, warpStrength: 0.35,
    reliefFrequency: 1.2, reliefContrast: 1, strataBand: 60, strataStrength: 0.15, hardnessContrast: 0.3, plateauStrength: 0.2, plateauHeight: 520, canyonDepth: 0,
    snowOn: 0, vegetation: 0.35, mossiness: 0.2, waterEnabled: 0, seaLevel: -100, droplets: 90000, erodeSpeed: 0.2, thermalIterations: 10, talusSoft: 38, talusHard: 85,
    rockDensity: 0.5, rockSlopeMax: 35, rockClustering: 0.6, rockSizeMin: 2, rockSizeMax: 22, rockEmbed: 0.55, pebbleDensity: 0.3,
    peelStrength: 0.85, peelCoverage: 0.45, peelScale: 4, peelThickness: 0.05, flakeStrength: 0.8, sunElevation: 42, sunAzimuth: 240, turbidity: 5, fogDensity: 0.15,
    outcropDensity: 0.4, outcropSize: 18, outcropCount: 5, outcropSpacing: 260, outcropAspect: 0.65, outcropBury: 0.45, outcropRidge: 0.6, outcropWeather: 0.8,
    riverLakeFill: 0.6, riverLakeMax: 3,
  },
  'Granite tors': {
    palette: 'granite', mountainHeight: 260, baseElevation: 80, baseFrequency: 1.1, ridgeSharpness: 1.0, peakPower: 0.9, warpStrength: 0.6, reliefFrequency: 0.9, reliefContrast: 0.35,
    strataBand: 40, strataStrength: 0.1, hardnessContrast: 0.3, plateauStrength: 0, canyonDepth: 0,
    snowOn: 0, vegetation: 0.9, vegSlope: 38, mossiness: 0.7, dryness: 0.5, waterEnabled: 0, seaLevel: -100, droplets: 160000, erodeSpeed: 0.3, thermalIterations: 30, talusSoft: 33, talusHard: 84,
    outcropDensity: 0.7, outcropSize: 16, outcropCount: 7, outcropSpread: 1.5, outcropSpacing: 170, outcropAspect: 0.75, outcropBury: 0.35, outcropRidge: 0.85, outcropSlopeMax: 25, outcropWeather: 0.75,
    rockDensity: 0.45, rockSlopeMax: 40, rockClustering: 0.7, rockSizeMin: 1, rockSizeMax: 6, rockEmbed: 0.5, pebbleDensity: 0.4, gravelAmount: 0.3,
    sdfOn: 1, sdfAngle: 55, sdfUndercut: 3, sdfPockets: 9, sdfJoints: 0.3, sdfPits: 0.4, peelStrength: 0.5, peelCoverage: 0.3, peelScale: 3,
    riverCatchment: 0.08, riverLakeMax: 2, sunElevation: 26, sunAzimuth: 235, turbidity: 4, fogDensity: 0.35,
  },
  'Rocky coast': {
    palette: 'basalt', mountainHeight: 220, baseElevation: -40, baseFrequency: 1.8, ridgeSharpness: 1.8, peakPower: 1.2, warpStrength: 0.6,
    reliefContrast: 0.8, strataBand: 16, strataStrength: 0.8, strataDip: 6, hardnessContrast: 0.9, plateauStrength: 0.4, plateauHeight: 140, canyonDepth: 0,
    snowOn: 0, vegetation: 0.5, mossiness: 0.7, seaLevel: 0, waterEnabled: 1, droplets: 180000, talusSoft: 34, talusHard: 86,
    rockDensity: 0.9, rockSlopeMax: 60, rockClustering: 0.4, rockSizeMin: 0.8, rockSizeMax: 12, pebbleDensity: 0.9, pebbleSize: 0.5, gravelAmount: 1,
    peelStrength: 0.4, sunElevation: 20, sunAzimuth: 280, turbidity: 4, fogDensity: 0.45, wetness: 0.9,
  },
  'Talus canyon': {
    palette: 'sandstone', mountainHeight: 300, baseElevation: 220, baseFrequency: 1.1, ridgeSharpness: 1.3, peakPower: 0.9, warpStrength: 0.4,
    plateauStrength: 0.9, plateauHeight: 440, canyonDepth: 360, canyonWidth: 520, strataBand: 20, strataStrength: 1, strataDip: 2, hardnessContrast: 1,
    snowOn: 0, vegetation: 0.08, mossiness: 0.05, seaLevel: -20, waterEnabled: 0, droplets: 170000, thermalIterations: 50, thermalRate: 0.7, talusSoft: 33, talusHard: 87,
    rockDensity: 0.9, rockSlopeMin: 10, rockSlopeMax: 55, rockClustering: 0.3, rockSizeMin: 0.8, rockSizeMax: 14, pebbleDensity: 0.8, pebbleSize: 0.6, gravelAmount: 1,
    peelStrength: 0.6, peelScale: 3, oxideAmount: 0.7, sunElevation: 40, sunAzimuth: 205, turbidity: 6, fogDensity: 0.2, riverWater: 0, riverLakes: 0, riverCatchment: 0.08,
    riverLakeMax: 0.5, fluvialFill: 0.8,
  },
  'Rocky mountains': {
    palette: 'granite', mountainHeight: 900, baseElevation: 60, baseFrequency: 1.5, ridgeSharpness: 2.8, peakPower: 1.6, warpStrength: 0.7,
    reliefFrequency: 1.1, reliefContrast: 0.85, strataBand: 34, strataStrength: 0.7, strataDip: 8, hardnessContrast: 0.9, plateauStrength: 0, canyonDepth: 0,
    snowOn: 1, snowLine: 700, snowSlope: 50, vegetation: 0.45, vegSlope: 38, mossiness: 0.3, seaLevel: -100, waterEnabled: 0, droplets: 220000, erodeSpeed: 0.4, thermalIterations: 40, talusSoft: 35, talusHard: 84,
    rockDensity: 0.7, rockSlopeMax: 50, rockSizeMin: 1, rockSizeMax: 14, pebbleDensity: 0.6, gravelAmount: 0.9,
    sdfOn: 1, sdfAngle: 56, sdfUndercut: 8, sdfJoints: 0.7, overhang: 3, riverCatchment: 0.05, riverBraiding: 0.2, riverLakes: 0,
    peelStrength: 0.5, flakeStrength: 0.7, sunElevation: 32, sunAzimuth: 215, turbidity: 3, fogDensity: 0.3,
  },
  'Sand dunes': {
    palette: 'sandstone', rockA: '#e3c49a', rockB: '#d2ab7c', rockC: '#f0d9b5', fresh: '#f6e4c4', oxide: '#c89a6e', gravelColor: '#d9b98f',
    mountainHeight: 70, baseElevation: 60, baseFrequency: 1.2, ridgeSharpness: 1.0, peakPower: 0.8, warpStrength: 0.4, reliefFrequency: 1.0, reliefContrast: 0.2,
    duneAmount: 48, duneWavelength: 300, duneDirection: 40, duneAsymmetry: 0.8, duneCoverage: 0.85,
    strataStrength: 0, strataBand: 60, hardnessContrast: 0.1, plateauStrength: 0, canyonDepth: 0,
    droplets: 6000, erodeSpeed: 0.1, thermalIterations: 30, thermalRate: 0.6, talusSoft: 33, talusHard: 36,
    snowOn: 0, vegOn: 0, vegetation: 0, mossOn: 0, mossiness: 0, oxideAmount: 0.15, waterEnabled: 0, seaLevel: -100,
    rocksEnabled: 0, pebblesOn: 0, gravelAmount: 0, peelOn: 0, flakeStrength: 0.25, flakeSize3: 0.08, flakeCrystal3: 0.5, flakeSparkle: 1.2,
    sdfOn: 0, overhang: 0, detailRelief: 0.15, rockyAmount: 0, cragAmount: 0, fluvialStrength: 0, rillStrength: 0, riverSim: 0, riverWater: 0, riverLakes: 0, streakAmount: 0, wetness: 0.1,
    sunElevation: 28, sunAzimuth: 250, turbidity: 8, fogDensity: 0.15,
  },
  'Icelandic highlands': {
    palette: 'basalt', rockA: '#5d5a56', rockB: '#3f3d3b', rockC: '#7a756d', fresh: '#8d8a84', oxide: '#8a6a4e', gravelColor: '#4a4846', mossColor: '#6f7f3f', grassA: '#6b7a3c', grassB: '#9a9a5a', dryColor: '#7f7a5a',
    mountainHeight: 620, baseElevation: 120, baseFrequency: 1.3, ridgeSharpness: 1.6, peakPower: 1.1, warpStrength: 0.7, reliefFrequency: 1.0, reliefContrast: 0.55,
    plateauStrength: 0.45, plateauHeight: 560, canyonDepth: 0, strataBand: 22, strataStrength: 0.9, strataDip: 2, hardnessContrast: 1.0,
    droplets: 260000, erodeSpeed: 0.45, sedimentCapacity: 6, thermalIterations: 40, talusSoft: 34, talusHard: 86,
    snowOn: 1, snowLine: 520, snowSlope: 42, snowSoftness: 2.5, vegetation: 0.5, vegSlope: 30, vegPatchiness: 0.8, dryness: 0.3, mossiness: 0.9, mossScale: 6,
    waterEnabled: 0, seaLevel: -100, riverCatchment: 0.03, riverWidthScale: 28, riverMaxWidth: 110, riverBraiding: 0.8, riverLakes: 0, riverLakeFill: 0.9,
    rockDensity: 0.4, rockSizeMax: 6, pebbleDensity: 0.6, gravelAmount: 1, sdfOn: 1, sdfAngle: 60, sdfUndercut: 5,
    outcropDensity: 0.2, outcropSize: 10, outcropCount: 6, outcropSpacing: 180, outcropAspect: 0.55, outcropBury: 0.5, outcropRidge: 0.2, outcropWeather: 0.3,
    oxideAmount: 0.6, peelStrength: 0.35, flakeStrength: 0.5, sunElevation: 22, sunAzimuth: 200, turbidity: 4, fogDensity: 0.5,
  },
  'Icelandic river plains': {
    palette: 'basalt', rockA: '#5a5753', rockB: '#3d3b39', rockC: '#77726a', fresh: '#8a8781', oxide: '#86684d', gravelColor: '#3f3d3b', mossColor: '#6c7c3c', grassA: '#66753a', grassB: '#9b9a5c', dryColor: '#7b765a',
    mountainHeight: 420, baseElevation: 30, baseFrequency: 1.1, ridgeSharpness: 1.5, peakPower: 1.2, warpStrength: 0.6, reliefFrequency: 0.9, reliefContrast: 0.75,
    plateauStrength: 0.2, plateauHeight: 420, canyonDepth: 0, strataBand: 20, strataStrength: 0.8, strataDip: 1.5, hardnessContrast: 0.9,
    droplets: 280000, erodeSpeed: 0.5, sedimentCapacity: 7, depositSpeed: 0.35, thermalIterations: 36, talusSoft: 33, talusHard: 84,
    snowOn: 1, snowLine: 380, snowSlope: 40, snowSoftness: 3, vegetation: 0.55, vegSlope: 28, vegPatchiness: 0.85, dryness: 0.35, mossiness: 0.85, mossScale: 7,
    waterEnabled: 1, seaLevel: 0, riverCatchment: 0.02, riverWidthScale: 35, riverMaxWidth: 150, riverDepthScale: 0.8, riverBraiding: 1.0, riverLakes: 0, riverLakeFill: 0.95, riverGuideFlow: 2,
    rockDensity: 0.25, rockSizeMax: 5, pebbleDensity: 0.8, pebbleSize: 0.5, gravelAmount: 1, sdfOn: 1, sdfAngle: 60, sdfUndercut: 4,
    oxideAmount: 0.5, peelStrength: 0.3, flakeStrength: 0.45, sunElevation: 26, sunAzimuth: 190, turbidity: 5, fogDensity: 0.45,
  },
  'Himalayan peaks': {
    palette: 'slate', worldSize: 4096, mountainHeight: 2100, baseElevation: 260, baseFrequency: 1.3, ridgeSharpness: 3.4, peakPower: 1.9, warpStrength: 0.55,
    reliefFrequency: 0.9, reliefContrast: 0.95, strataBand: 42, strataStrength: 0.55, strataDip: 22, strataDipDirection: 150, hardnessContrast: 0.9, plateauStrength: 0, canyonDepth: 0,
    snowOn: 1, snowLine: 720, snowSlope: 62, snowSoftness: 2.2, vegetation: 0.08, vegSlope: 30, mossiness: 0.05, soilAmount: 0.45, soilColor: '#574d44', soilLight: '#8a8070',
    waterEnabled: 0, seaLevel: -100, droplets: 260000, erodeSpeed: 0.4, thermalIterations: 50, thermalRate: 0.6, talusSoft: 36, talusHard: 86,
    fluvialStrength: 0.9, fluvialIterations: 50, fluvialConcavity: 0.45, rillStrength: 0.6,
    rockDensity: 0.55, rockSlopeMax: 48, rockSizeMin: 1.5, rockSizeMax: 18, gravelAmount: 0.9,
    sdfOn: 1, sdfAngle: 58, sdfUndercut: 6, sdfJoints: 0.8, overhang: 2,
    riverCatchment: 0.3, riverWidthScale: 26, riverMaxWidth: 90, riverBraiding: 0.5, riverFloodplain: 0.9, riverDrySlope: 10, riverLakes: 0,
    peelStrength: 0.4, flakeStrength: 0.5, sunElevation: 28, sunAzimuth: 205, turbidity: 2, fogDensity: 0.25,
  },
  'Alpine peaks': {
    palette: 'granite', worldSize: 3072, mountainHeight: 1500, baseElevation: 160, baseFrequency: 1.5, ridgeSharpness: 3.0, peakPower: 1.7, warpStrength: 0.6,
    reliefFrequency: 1.0, reliefContrast: 0.9, strataBand: 34, strataStrength: 0.6, strataDip: 12, hardnessContrast: 0.85, plateauStrength: 0, canyonDepth: 0,
    snowOn: 1, snowLine: 600, snowSlope: 56, snowSoftness: 1.8, vegetation: 0.6, vegSlope: 36, vegPatchiness: 0.6, mossiness: 0.35, soilAmount: 0.8,
    waterEnabled: 0, seaLevel: -100, droplets: 240000, erodeSpeed: 0.4, thermalIterations: 44, talusSoft: 35, talusHard: 85,
    fluvialStrength: 0.8, fluvialIterations: 46, rillStrength: 0.55,
    rockDensity: 0.6, rockSlopeMax: 50, rockSizeMin: 1, rockSizeMax: 14, gravelAmount: 0.85,
    sdfOn: 1, sdfAngle: 56, sdfUndercut: 7, sdfJoints: 0.7, overhang: 3,
    riverCatchment: 0.06, riverWidthScale: 24, riverMaxWidth: 80, riverBraiding: 0.4, riverFloodplain: 0.8, riverLakes: 0,
    peelStrength: 0.5, flakeStrength: 0.7, sunElevation: 34, sunAzimuth: 220, turbidity: 2.5, fogDensity: 0.3,
  },
  'Karst pinnacles': {
    palette: 'limestone', mountainHeight: 300, baseElevation: 30, baseFrequency: 3.2, ridgeSharpness: 3.0, peakPower: 1.7, warpStrength: 0.4,
    reliefFrequency: 2.4, reliefContrast: 0.9, strataBand: 20, strataStrength: 0.5, hardnessContrast: 0.8, plateauStrength: 0, canyonDepth: 0,
    snowOn: 0, vegetation: 0.85, mossiness: 0.9, waterEnabled: 1, seaLevel: 0, droplets: 200000, talusSoft: 36, talusHard: 88,
    overhang: 4, rockDensity: 0.25, sunElevation: 45, sunAzimuth: 170, turbidity: 6, fogDensity: 0.45,
    riverLakeMax: 1,
  },
  // ── the sediment-filled worlds: the fill is the subject, not the rock ─────────────────────────
  'Braided river plain': {
    palette: 'sandstone', mountainHeight: 190, baseElevation: 25, baseFrequency: 1.1, ridgeSharpness: 0.85, peakPower: 0.7,
    warpStrength: 0.85, reliefFrequency: 1.2, reliefContrast: 0.55, strataBand: 24, strataStrength: 0.7, hardnessContrast: 0.5,
    strataLateral: 0.45, fluvialStrength: 0.5, fluvialConcavity: 0.5, fluvialFill: 1, fluvialDeposition: 1, fluvialDiffusion: 0.4,
    rillStrength: 0.3, talusSoft: 32, talusHard: 40, sdfOn: 0, overhang: 0, detailRelief: 0.12, rockyAmount: 0, cragAmount: 0,
    peelOn: 0, flakeStrength: 0.2, jointStrength: 0.4, vegetation: 0.35, vegSlope: 32, dryness: 0.55, soilAmount: 0.9,
    sedimentAmount: 1, sedBedScale: 0.35, sedBands: 0.85, sedLaminae: 0.75, sedRipple: 0.6, sedRippleScale: 0.7, sedCracks: 0.25, sedGrainBias: 0.45, sedFacies: 0.95,
    riverSim: 1, riverCatchment: 0.03, riverWidthScale: 3.2, riverMaxWidth: 62, riverFloodplain: 1.5, riverBraiding: 0.85,
    riverBankErosion: 0.9, riverDepthScale: 0.7, riverWater: 1, riverLakes: 1, riverLakeFill: 0.7, riverLakeMax: 1.2,
    waterEnabled: 0, seaLevel: -60, snowOn: 0, gravelAmount: 0.9,
    rocksEnabled: 1, pebblesOn: 1, rockDensity: 0.5, rockSizeMax: 4.5, rockInPlace: 0.2, rockTalus: 0.25, rockChannel: 1, rockBars: 1,
    rockCaps: 0.1, rockErratics: 0.15, rockShore: 0.6, rockSizeSpread: 0.85, pebbleDensity: 0.8, pebbleSize: 0.5,
    sunElevation: 34, sunAzimuth: 215, turbidity: 5, fogDensity: 0.35,
  },
  'Alluvial fan': {
    palette: 'limestone', mountainHeight: 430, baseElevation: 30, baseFrequency: 1.5, ridgeSharpness: 1.7, peakPower: 1.2,
    warpStrength: 0.5, plateauStrength: 0.25, plateauHeight: 380, canyonDepth: 45, canyonWidth: 0.5, strataBand: 20, strataStrength: 0.9,
    hardnessContrast: 0.85, strataLateral: 0.35, fluvialStrength: 0.75, fluvialConcavity: 0.45, fluvialFill: 1, fluvialDeposition: 1,
    sedimentCapacity: 0.35, depositSpeed: 0.5, rillStrength: 0.6, talusSoft: 34, talusHard: 74, sdfOn: 1, sdfAngle: 62, sdfBlocks: 1.4,
    overhang: 2, peelStrength: 0.55, flakeStrength: 0.35, vegetation: 0.1, dryness: 0.85, soilAmount: 0.55,
    sedimentAmount: 1, sedBedScale: 0.5, sedBands: 0.9, sedLaminae: 0.6, sedRipple: 0.45, sedRippleScale: 1.1, sedCracks: 0.45, sedGrainBias: 0.25, sedFacies: 1,
    riverSim: 1, riverCatchment: 0.05, riverWidthScale: 2.4, riverMaxWidth: 44, riverFloodplain: 1.3, riverBraiding: 0.6,
    riverWater: 0, riverLakes: 0, waterEnabled: 0, seaLevel: -80, snowOn: 0, gravelAmount: 1, gravelScale: 1.6,
    rocksEnabled: 1, pebblesOn: 1, rockDensity: 0.7, rockSizeMin: 0.7, rockSizeMax: 9, rockInPlace: 0.7, rockTalus: 1, rockBars: 0.9,
    rockChannel: 0.7, rockCaps: 0.35, rockErratics: 0.2, rockSizeSpread: 0.9, pebbleDensity: 0.85,
    oxideAmount: 0.6, sunElevation: 41, sunAzimuth: 232, turbidity: 7, fogDensity: 0.2,
  },
  'Delta plain': {
    palette: 'shale', mountainHeight: 140, baseElevation: 10, baseFrequency: 1.0, ridgeSharpness: 0.7, peakPower: 0.6,
    warpStrength: 1.0, reliefContrast: 0.4, strataBand: 16, strataStrength: 0.55, hardnessContrast: 0.35, strataLateral: 0.6,
    fluvialStrength: 0.4, fluvialConcavity: 0.6, fluvialFill: 1, fluvialDeposition: 1, fluvialDiffusion: 0.55, sdfOn: 0, overhang: 0,
    detailRelief: 0.08, rockyAmount: 0, cragAmount: 0, peelOn: 0, flakeStrength: 0.2, vegetation: 0.5, vegSlope: 34, dryness: 0.3,
    soilAmount: 0.9, mossiness: 0.7, sedimentAmount: 1, sedBedScale: 0.28, sedBands: 0.7, sedLaminae: 0.85, sedRipple: 0.55,
    sedRippleScale: 0.55, sedCracks: 0.5, sedGrainBias: -0.15, sedFacies: 0.75, sedColorSilt: '#6f6a5e',
    riverSim: 1, riverCatchment: 0.02, riverWidthScale: 3.6, riverMaxWidth: 76, riverFloodplain: 1.8, riverBraiding: 0.7,
    riverWater: 1, riverLakes: 1, riverLakeFill: 0.9, riverLakeMax: 1.6, waterEnabled: 1, seaLevel: 0, shoreWet: 1,
    rocksEnabled: 1, pebblesOn: 1, rockDensity: 0.3, rockSizeMax: 3, rockInPlace: 0.1, rockTalus: 0.1, rockChannel: 0.7, rockBars: 0.9,
    rockShore: 1, rockCaps: 0.05, rockErratics: 0.1, pebbleDensity: 0.9, pebbleSize: 0.4,
    sunElevation: 28, sunAzimuth: 200, turbidity: 6, fogDensity: 0.55,
  },
  'Playa & mudflats': {
    palette: 'limestone', mountainHeight: 260, baseElevation: 10, baseFrequency: 1.25, ridgeSharpness: 1.1, peakPower: 0.9,
    warpStrength: 0.7, reliefContrast: 0.6, strataBand: 14, strataStrength: 0.7, hardnessContrast: 0.55, strataLateral: 0.5,
    fluvialStrength: 0.35, fluvialConcavity: 0.5, fluvialFill: 1, fluvialDeposition: 1, evaporation: 0.9, sedimentCapacity: 0.25,
    rillStrength: 0.55, talusSoft: 32, talusHard: 58, sdfOn: 0, overhang: 0, detailRelief: 0.1, rockyAmount: 0, peelOn: 0,
    vegetation: 0.04, dryness: 1, soilAmount: 0.4, sedimentAmount: 1, sedBedScale: 0.22, sedBands: 0.55, sedLaminae: 0.4,
    sedRipple: 0.35, sedRippleScale: 0.9, sedCracks: 1, sedGrain: 0.35, sedGrainBias: -0.3, sedFacies: 0.5, sedColorSilt: '#cdbc95', sedColorSand: '#d6c8a6',
    riverSim: 1, riverCatchment: 0.08, riverWidthScale: 2.0, riverMaxWidth: 30, riverFloodplain: 1.5, riverWater: 0, riverLakes: 1,
    riverLakeFill: 0.2, riverLakeMax: 2.2, waterEnabled: 0, seaLevel: -100, snowOn: 0, gravelAmount: 0.35,
    oxideAmount: 0.5, sunElevation: 46, sunAzimuth: 250, turbidity: 8, fogDensity: 0.12,
  },
  // ── rock worlds: the walls are the subject ────────────────────────────────────────────────────
  'Tepui': {
    palette: 'sandstone', mountainHeight: 820, baseElevation: 170, baseFrequency: 1.15, ridgeSharpness: 1.2, peakPower: 1.1,
    warpStrength: 0.55, plateauStrength: 1, plateauHeight: 700, cliffHeight: 520, cliffSharpness: 1.35, cliffStacks: 0.5,
    strataBand: 34, strataStrength: 0.8, hardnessContrast: 0.9, strataLateral: 0.25, fluvialStrength: 0.6, fluvialConcavity: 0.3,
    rillStrength: 0.8, talusSoft: 40, talusHard: 88, sdfOn: 1, sdfAngle: 72, sdfUndercut: 7, sdfBlocks: 1.6, sdfBlockSize: 12,
    sdfVertical: 1.1, overhang: 5, buttress: 0.7, detailRelief: 0.8, rockyAmount: 0.9, peelStrength: 0.6, peelScale: 3.4,
    flakeStrength: 0.35, jointSpacing: 3.4, vegetation: 0.55, vegSlope: 26, soilAmount: 0.7, sedGrainBias: -0.15, sedimentAmount: 0.75, sedBedScale: 0.4,
    riverSim: 1, riverCatchment: 0.05, riverWidthScale: 1.6, riverMaxWidth: 26, riverFloodplain: 0.6, riverWater: 1, riverLakes: 1,
    waterEnabled: 0, seaLevel: -40, snowOn: 0, fogDensity: 1.1, turbidity: 4, sunElevation: 52, sunAzimuth: 160,
    rocksEnabled: 1, pebblesOn: 1, rockDensity: 0.45, rockSizeMax: 7, rockTalus: 1, rockCaps: 0.5, rockInPlace: 0.6, rockSizeSpread: 0.8,
  },
  'Sandstone forest': {
    palette: 'sandstone', mountainHeight: 420, baseElevation: 240, baseFrequency: 2.2, ridgeSharpness: 2.4, peakPower: 1.5,
    warpStrength: 0.5, strataBand: 22, strataStrength: 0.8, hardnessContrast: 0.75, stackHeight: 300, stackLevels: 5, stackScale: 0.9,
    stackTaper: 0.75, stackSharpness: 0.9, stackChaos: 0.7, stackSpires: 0.7, stackCoverage: 0.75, stackPedestal: 0.5, stackSizeVar: 0.9,
    stackElongation: 0.4, stackCluster: 0.6, fluvialStrength: 0.7, fluvialConcavity: 0.4, rillStrength: 0.7, talusSoft: 38,
    talusHard: 82, sdfOn: 1, sdfAngle: 68, sdfUndercut: 5, sdfBlocks: 1.2, overhang: 3, detailRelief: 0.7, rockyAmount: 0.8,
    peelStrength: 0.5, flakeStrength: 0.4, vegetation: 0.9, vegSlope: 30, mossiness: 1, soilAmount: 0.85, sedGrainBias: -0.25, sedimentAmount: 0.6,
    riverSim: 1, riverWater: 1, riverLakes: 1, waterEnabled: 0, seaLevel: -30, fogDensity: 1.3, turbidity: 3, sunElevation: 44,
    rocksEnabled: 1, pebblesOn: 1, rockDensity: 0.4, rockTalus: 1, rockSizeMax: 5,
  },
  'Glacial valley': {
    palette: 'granite', mountainHeight: 780, baseElevation: 40, baseFrequency: 1.45, ridgeSharpness: 2.0, peakPower: 1.3,
    warpStrength: 0.45, fluvialStrength: 0.25, fluvialConcavity: 0.15, fluvialDiffusion: 0.5, rillStrength: 0.35,
    talusSoft: 40, talusHard: 84, thermalRate: 0.6, thermalIterations: 12, strataBand: 30, strataStrength: 0.5, hardnessContrast: 0.8,
    outcropDensity: 0.5, outcropSize: 12, outcropCount: 4, outcropBury: 0.5, outcropWeather: 0.6, outcropRidge: 0.4, outcropSlopeMax: 34,
    sdfOn: 1, sdfAngle: 60, sdfUndercut: 4, overhang: 2.5, detailRelief: 0.9, rockyAmount: 1.0, cragAmount: 0.5, peelStrength: 0.5,
    flakeStrength: 0.5, jointSpacing: 2.6, vegetation: 0.28, vegSlope: 30, dryness: 0.4, soilAmount: 0.6, sedimentAmount: 0.85,
    sedBedScale: 0.6, sedBands: 0.6, sedGrainBias: -0.1, sedFacies: 0.9, sedCracks: 0.1, snowOn: 1, snowLine: 300, snowSlope: 62, snowSoftness: 2.4,
    riverSim: 1, riverCatchment: 0.06, riverWidthScale: 2.2, riverMaxWidth: 34, riverFloodplain: 1.2, riverWater: 1, riverLakes: 1,
    riverLakeFill: 0.6, riverLakeMax: 1.4, waterEnabled: 0, seaLevel: -120, gravelAmount: 0.9,
    rocksEnabled: 1, pebblesOn: 1, rockDensity: 0.55, rockSizeMax: 10, rockTalus: 1, rockErratics: 0.8, rockCaps: 0.5, rockInPlace: 0.6,
    pebbleDensity: 0.7, sunElevation: 24, sunAzimuth: 300, turbidity: 3, fogDensity: 0.5,
  },
  'Chalk cliffs': {
    palette: 'limestone', mountainHeight: 300, baseElevation: 0, baseFrequency: 1.7, ridgeSharpness: 1.5, peakPower: 1.1,
    warpStrength: 0.7, cliffHeight: 190, cliffSharpness: 1.3, plateauStrength: 0.55, plateauHeight: 150, strataBand: 40,
    strataStrength: 0.5, hardnessContrast: 0.45, strataLateral: 0.4, fluvialStrength: 0.55, rillStrength: 0.7, talusSoft: 36,
    talusHard: 78, sdfOn: 1, sdfAngle: 74, sdfUndercut: 9, sdfPockets: 0.8, sdfBlocks: 1.8, sdfBlockSize: 8, sdfVertical: 0.9,
    overhang: 5.5, buttress: 0.6, ledgeNoise: 0.5, detailRelief: 0.55, rockyAmount: 0.7, peelStrength: 0.45, flakeStrength: 0.3,
    jointSpacing: 2.2, vegetation: 0.7, vegSlope: 28, soilAmount: 0.7, sedimentAmount: 0.8, sedBedScale: 0.3, sedRipple: 0.7,
    sedRippleScale: 0.6, sedGrainBias: -0.1, sedFacies: 0.8, sedColorSand: '#d8cdb4', sedColorSilt: '#c6bda6',
    seaLevel: 0, waterEnabled: 1, shoreWet: 1, riverSim: 1, riverCatchment: 0.08, riverWater: 1, riverLakes: 0,
    rocksEnabled: 1, pebblesOn: 1, rockDensity: 0.55, rockSizeMax: 5.5, rockTalus: 1, rockShore: 1, rockInPlace: 0.5, rockCaps: 0.3,
    pebbleDensity: 0.9, pebbleSize: 0.45, sunElevation: 30, sunAzimuth: 255, turbidity: 4, fogDensity: 0.5,
  },
  'Basin & range': {
    palette: 'sandstone', mountainHeight: 520, baseElevation: 20, baseFrequency: 1.35, ridgeSharpness: 1.6, peakPower: 1.15,
    warpStrength: 0.6, strataBand: 26, strataStrength: 1.0, hardnessContrast: 0.9, strataDip: 22, strataDipDirection: 115,
    strataLateral: 0.5, fluvialStrength: 0.65, fluvialConcavity: 0.5, fluvialFill: 1, fluvialDeposition: 1, rillStrength: 0.55,
    talusSoft: 34, talusHard: 76, sdfOn: 1, sdfAngle: 62, sdfBlocks: 1.4, overhang: 2.5, peelStrength: 0.6, flakeStrength: 0.35,
    vegetation: 0.08, dryness: 0.95, soilAmount: 0.5, sedimentAmount: 1, sedBedScale: 0.45, sedBands: 0.85, sedGrainBias: 0.2, sedFacies: 1,
    sedRipple: 0.5, sedRippleScale: 1.4, sedCracks: 0.5, duneAmount: 0.35, duneWavelength: 90, duneCoverage: 0.5, duneAsymmetry: 0.4,
    riverSim: 1, riverCatchment: 0.06, riverWidthScale: 2.2, riverMaxWidth: 36, riverFloodplain: 1.4, riverBraiding: 0.7,
    riverWater: 0, riverLakes: 1, riverLakeFill: 0.3, waterEnabled: 0, seaLevel: -90, gravelAmount: 0.9, oxideAmount: 0.75,
    rocksEnabled: 1, pebblesOn: 1, rockDensity: 0.6, rockSizeMax: 8, rockTalus: 1, rockBars: 0.8, rockInPlace: 0.5, pebbleDensity: 0.8,
    sunElevation: 38, sunAzimuth: 245, turbidity: 7, fogDensity: 0.18,
  },
  'Tufa terraces': {
    palette: 'limestone', mountainHeight: 340, baseElevation: 40, baseFrequency: 1.5, ridgeSharpness: 1.0, peakPower: 0.8,
    warpStrength: 0.5, reliefContrast: 0.55, strataBand: 12, strataStrength: 0.5, hardnessContrast: 0.4, strataLateral: 0.55,
    fluvialStrength: 0.45, fluvialConcavity: 0.45, fluvialFill: 1, fluvialPits: 0.2, rillStrength: 0.5, talusSoft: 30, talusHard: 46,
    sdfOn: 1, sdfAngle: 48, sdfUndercut: 3, sdfBlocks: 0.8, overhang: 1, detailRelief: 0.3, rockyAmount: 0.25, peelStrength: 0.25,
    flakeStrength: 0.25, vegetation: 0.22, vegSlope: 34, soilAmount: 0.5, sedimentAmount: 1, sedBedScale: 0.18, sedBands: 0.7,
    sedLaminae: 0.9, sedRipple: 0.5, sedRippleScale: 0.5, sedCracks: 0.2, sedGrainBias: 0.15, sedFacies: 0.3, sedColorSilt: '#e2dbc6', sedColorSand: '#ded3ba',
    riverSim: 1, riverCatchment: 0.04, riverWidthScale: 1.8, riverMaxWidth: 26, riverFloodplain: 1.2, riverWater: 1, riverLakes: 1,
    riverLakeFill: 0.5, riverLakeMax: 0.9, riverLakeMin: 0.02, lakeDepth: 1.6, waterEnabled: 1, seaLevel: -18, shoreWet: 1,
    waterColor: '#2f6d6a', riverColor: '#3a7d78', riverShallowColor: '#7fc3bb', riverClearDepth: 2.6,
    sunElevation: 40, sunAzimuth: 200, turbidity: 4, fogDensity: 0.3,
  },
});

export const palettes = {
  granite:   { name: 'Granite',   rockA: '#b3b1ae', rockB: '#94918f', rockC: '#c7c2ba', fresh: '#d6d3cd', oxide: '#ac947b', flakeColor1: '#9d9893', flakeColor2: '#c9bfb3', flakeColor3: '#e4e0d8', gravelColor: '#9a938a' },
  sandstone: { name: 'Sandstone', rockA: '#e1b794', rockB: '#bd8f72', rockC: '#eed6b7', fresh: '#f3dec2', oxide: '#a8755a', flakeColor1: '#c9a07c', flakeColor2: '#e5c8a4', flakeColor3: '#f2e3c9', gravelColor: '#c7a383' },
  limestone: { name: 'Limestone', rockA: '#d9d3c7', rockB: '#b3aea4', rockC: '#e9e4d9', fresh: '#f1ede4', oxide: '#baa080', flakeColor1: '#c4bdae', flakeColor2: '#e2dccf', flakeColor3: '#f3f0e8', gravelColor: '#bdb6a8' },
  basalt:    { name: 'Basalt',    rockA: '#858588', rockB: '#616165', rockC: '#a09e9c', fresh: '#b1b0ae', oxide: '#a08068', flakeColor1: '#6c6c70', flakeColor2: '#8e8d8a', flakeColor3: '#b5b2ad', gravelColor: '#7a7a7c' },
  shale:     { name: 'Dark shale', rockA: '#4b4a47', rockB: '#2c2c2b', rockC: '#6a6762', fresh: '#7d7a74', oxide: '#6e5a48', flakeColor1: '#3a3a39', flakeColor2: '#575551', flakeColor3: '#7b7872', gravelColor: '#4f4e4b' },
  slate:     { name: 'Slate',     rockA: '#9498a0', rockB: '#6f7580', rockC: '#b0b3ba', fresh: '#c2c6ca', oxide: '#a48a75', flakeColor1: '#767b86', flakeColor2: '#a3a7ae', flakeColor3: '#cfd2d6', gravelColor: '#8a8e96' },
};
export const paletteKeys = ['rockA', 'rockB', 'rockC', 'fresh', 'oxide', 'flakeColor1', 'flakeColor2', 'flakeColor3', 'gravelColor'];

// Inspector schema. Each group = one outliner row. Each control: [key, label, min, max, step, unit, hint]
export const groups = [
  {
    id: 'landform', name: 'Landform', type: 'Base relief & ridges', stage: 'terrain', color: '#d6a078',
    cards: [
      { title: 'Preset', kind: 'presets' },
      { title: 'Elevation', controls: [
        ['mountainHeight', 'Mountain height', 100, 2500, 10, 'm', 'Peak elevation above the datum'],
        ['baseElevation', 'Base elevation', -200, 400, 5, 'm', 'Elevation of the plains'],
        ['worldSize', 'World size', 1024, 4096, 128, 'm', 'Side length of the generated tile'],
      ] },
      { title: 'Relief', controls: [
        ['baseFrequency', 'Ridge frequency', 0.6, 4, 0.05, '', 'Number of ridge systems across the tile'],
        ['ridgeSharpness', 'Ridge sharpness', 1, 4, 0.05, '', 'Crest profile — rounded to knife-edge'],
        ['peakPower', 'Peak emphasis', 0.6, 2.5, 0.05, '', 'Concentrates height into the summits'],
        ['warpStrength', 'Domain warp', 0, 1.5, 0.05, '', 'Bends ridgelines into realistic curves'],
        ['reliefContrast', 'Massif contrast', 0.1, 1, 0.05, '', 'How sharply mountains rise from plains'],
        ['reliefFrequency', 'Massif frequency', 0.6, 3, 0.1, '', 'Scale of the mountain / plains pattern'],
      ] },
      { title: 'Mesa & canyon', controls: [
        ['plateauStrength', 'Plateau flattening', 0, 1, 0.05, '', 'Soft-clamps summits into flat tops'],
        ['plateauHeight', 'Plateau height', 50, 1000, 10, 'm', 'Elevation of the caprock surface'],
        ['canyonDepth', 'Canyon depth', 0, 600, 10, 'm', 'Incision of the meandering gorge'],
        ['canyonWidth', 'Canyon width', 60, 900, 10, 'm', 'Rim-to-rim width'],
        ['cliffHeight', 'Escarpment', 0, 400, 5, 'm', 'A near-vertical cliff line along the edge of the massif (coastal cliffs, quarry walls): the high side is lifted onto a bench by this much within a cell or two'],
        ['cliffSharpness', 'Escarpment edge', 0, 1, 0.05, '', 'How abrupt the drop is (1 = a wall)'],
        ['cliffStacks', 'Stacks', 0, 1, 0.05, '', 'Pillars of the former cliff left standing just off the line (sea stacks, quarry remnants)'],
      ] },
      { title: 'Rugged outcrops', controls: [
        ['ruggedOn', 'Enable push–pull', 0, 1, 1, '', 'Lateral (XZ) push–pull of the steep faces: blocky buttresses stand out of the face, recesses are cut back into it — applied to the base shape and again, finer, after erosion'],
        ['ruggedAmount', 'Push–pull', 0, 60, 1, 'm', 'How far a face is moved sideways'],
        ['ruggedScale', 'Block scale', 15, 300, 5, 'm', 'Size of the buttresses / recesses'],
        ['ruggedBlockiness', 'Blockiness', 0, 1, 0.05, '', 'Plateaus and sharp risers (1) vs. rounded waves (0)'],
        ['ruggedBands', 'Levels', 1, 6, 1, '', 'How many push–pull levels a face steps between'],
        ['ruggedLedges', 'Ledges', 0, 1.5, 0.05, '', 'Vertical share of the push–pull: the blocks also step up and down'],
        ['ruggedSlope', 'From slope', 10, 60, 1, '°', 'Ground gentler than this is not moved (valleys, plains, rivers keep their shape)'],
        ['ruggedAfter', 'After erosion', 0, 1, 0.05, '', 'Fraction of the push–pull re-applied on the eroded faces (erosion smears the lateral structure)'],
      ] },
      { title: 'Rock stacks (Gaea Stacks)', controls: [
        ['stackHeight', 'Stack height', 0, 400, 5, 'm', 'Tiered rock towers added to the relief (Gaea Stacks): a mask is thresholded at a rising level per tier, so every tier sits inside the one below — buttes, mesas, hoodoos and spires. 0 = off'],
        ['stackLevels', 'Tiers', 1, 16, 1, '', 'Number of tiers in a stack (each has its own thickness)'],
        ['stackScale', 'Stack size', 20, 1000, 10, 'm', 'Plan size of the largest tier'],
        ['stackTaper', 'Taper', 0, 1, 0.05, '', 'How much each tier shrinks relative to the one below (0 = a straight tower, 1 = a steep pyramid of steps)'],
        ['stackSharpness', 'Edge sharpness', 0, 1, 0.05, '', 'Steepness of the tier walls (1 = vertical)'],
        ['stackChaos', 'Chaos', 0, 1, 0.05, '', 'Every tier is offset in plan and its outline wobbled, so the pile leans and steps irregularly instead of a wedding cake'],
        ['stackSpires', 'Spires', 0, 1, 0.05, '', 'Blends the mask from broad warped noise (buttes, mesas) towards cellular cones (isolated spires / hoodoos)'],
        ['stackCoverage', 'Coverage', 0, 1, 0.05, '', 'Share of the map with stack fields'],
        ['stackPedestal', 'Pedestal', 0, 1, 0.05, '', 'Talus ramp up to the foot of the lowest tier'],
        ['stackSizeVar', 'Size spread', 0, 1, 0.05, '', 'How different the stacks are from each other: 0 = an even field of alike towers, 1 = a few anchor towers among many small ones (sizes and heights are drawn from a heavy-tailed distribution)'],
        ['stackElongation', 'Elongation', 0, 1, 0.05, '', 'Stretches the plan of each stack into ridges and fins along its own direction, so outlines are not discs'],
        ['stackCluster', 'Clustering', 0, 1, 0.05, '', 'Groups the stacks into clusters with bare ground between: neighbours touch and merge into irregular compounds instead of standing on a lattice'],
      ] },
      { title: 'Grid', controls: [
        ['seed', 'Seed', 1, 9999, 1, '', 'Deterministic noise seed'],
        ['resolution', 'Resolution', 128, 2048, 128, 'px', 'Heightfield side in samples. 512 ≈ 2 s, 1024 ≈ 15 s, 2048 ≈ 1 min and a 4 M-vertex mesh'],
      ] },
    ],
  },
  {
    id: 'deposition', name: 'Dunes & outcrops', type: 'Loose material piled onto the landform', stage: 'terrain', color: '#d7b47c',
    cards: [
      { title: 'Dunes', controls: [
        ['duneAmount', 'Dune height', 0, 80, 1, 'm', 'Transverse sand dunes added to the relief: long windward slope, short slip face'],
        ['duneWavelength', 'Dune spacing', 30, 600, 5, 'm', ''],
        ['duneDirection', 'Wind direction', 0, 360, 5, '°', ''],
        ['duneAsymmetry', 'Asymmetry', 0.5, 0.9, 0.01, '', 'Position of the crest along the wavelength (0.5 symmetric, 0.9 steep slip face)'],
        ['duneCoverage', 'Coverage', 0, 1, 0.05, '', 'Fraction of the tile with dune fields'],
      ] },
      { title: 'Boulder outcrops', controls: [
        ['outcropDensity', 'Outcrop density', 0, 1, 0.05, '', 'Clusters of large core-stones built into the relief before strata and erosion — tors, woolsacks, "encampments". 0 = none'],
        ['outcropSize', 'Boulder size', 4, 60, 1, 'm', 'Radius of the largest boulder in a cluster'],
        ['outcropCount', 'Boulders per cluster', 1, 12, 1, '', ''],
        ['outcropSpread', 'Cluster spread', 0.8, 3, 0.1, '', 'How far the satellites sit from the main stone (×size)'],
        ['outcropSpacing', 'Cluster spacing', 60, 800, 10, 'm', 'Site grid for clusters; density decides how many sites are used'],
        ['outcropAspect', 'Height / width', 0.3, 1.3, 0.05, '', 'Flat slabs to tall stacks'],
        ['outcropBury', 'Burial', 0.1, 0.8, 0.05, '', 'How deep the stones sit in the ground'],
        ['outcropWeather', 'Rounding', 0, 1, 0.05, '', '0 = blocky joint-bounded blocks, 1 = fully rounded woolsacks'],
        ['outcropRidge', 'Prefer high ground', 0, 1, 0.05, '', 'Bias clusters towards ridges and hill tops (tors) rather than anywhere'],
        ['outcropSlopeMax', 'Max ground slope', 5, 60, 1, '°', 'Clusters avoid ground steeper than this'],
      ] },
    ],
  },
  {
    id: 'strata', name: 'Strata', type: 'Cliff layering', stage: 'terrain', color: '#c29583',
    cards: [
      { title: 'Bedding', controls: [
        ['strataBand', 'Bed thickness', 6, 80, 1, 'm', 'Typical vertical thickness of a rock layer'],
        ['strataStrength', 'Terracing', 0, 1, 0.05, '', 'How strongly beds step into benches and faces'],
        ['hardnessContrast', 'Caprock contrast', 0, 1, 0.05, '', 'Resistant layers hold vertical faces; soft layers slope'],
      ] },
      { title: 'Stratigraphic column', controls: [
        ['strataVariation', 'Thickness variation', 0, 1.5, 0.05, '', 'Spread of bed thicknesses around the typical value (0 = all beds alike)'],
        ['strataPackaging', 'Packaging', 0, 1, 0.05, '', 'How much beds group into thin-bedded (shale) and massive (sandstone / limestone) packages instead of a random stack'],
        ['strataHardShare', 'Hard share', 0, 1, 0.05, '', 'Share of resistant beds in the mixed packages'],
        ['strataLateral', 'Lateral change', 0, 0.8, 0.01, '', 'Beds thicken, thin, rise and dip across the map: at 0 every wall in the world shows the same bed rhythm at the same heights, which is the fastest way to make a cliff read as a repeated tile'],
        ['strataRecut', 'Re-cut after erosion', 0, 1, 0.05, '', 'Applies the beds once more (at this share of the strength) on the eroded surface so the ledges stay crisp — Gaea Stratify after Erosion'],
      ] },
      { title: 'Geological dip', controls: [
        ['strataDip', 'Dip angle', 0, 25, 0.5, '°', 'Tilt of the bedding planes'],
        ['strataDipDirection', 'Dip direction', 0, 360, 5, '°', 'Compass direction the beds dip towards'],
      ] },
    ],
  },
  {
    id: 'erosion', name: 'Erosion', type: 'Fluvial, hydraulic & thermal', stage: 'terrain', color: '#81b8c8',
    cards: [
      { title: 'Fluvial incision', controls: [
        ['fluvialStrength', 'Incision', 0, 1, 0.05, '', 'Stream-power erosion: rivers cut valleys in proportion to the water they gather, shaping a branching drainage network. 0 = off'],
        ['fluvialIterations', 'Time steps', 0, 80, 1, '', 'Number of implicit erosion steps (each is a long geological interval)'],
        ['fluvialConcavity', 'Concavity', 0.3, 0.7, 0.05, '', 'Area exponent m: higher = big rivers dominate, flatter valley floors and steeper headwaters'],
        ['fluvialUplift', 'Uplift', 0, 1, 0.05, '', 'Tectonic uplift of the massifs during incision: keeps the relief high while the valleys deepen; 0 lets everything grade towards base level'],
        ['fluvialDeposition', 'Sedimentation', 0, 1, 0.05, '', 'Eroded material is carried downstream and dropped where the stream can no longer carry it: alluvial fans where slopes flatten, valley fills, deltas and basin floors. 0 = everything leaves the map'],
        ['fluvialSedimentShow', 'Alluvium shows at', 0.5, 10, 0.5, 'm', 'Sediment this thick is painted as full alluvium (sand / gravel) by the surface'],
        ['fluvialFill', 'Basin fill', 0, 1, 0.05, '', 'How fast closed basins silt up to their spill level: high = lakes become flat valley floors with a river through them, low = lakes survive'],
        ['fluvialPits', 'Silt up pits', 0, 30, 1, 'm', 'After the droplet erosion, small hollows (and large ones shallower than this) are silted up so the valleys drain as one network; large basins deeper than 1.5 m stay lakes'],
        ['cliffProtect', 'Cliff protection', 0, 1, 0.05, '', 'Faces steeper than the protection angle keep their pre-erosion shape through every erosion stage, so the ground above can be eroded into a full landscape (valleys, rills, streams hanging at the crest) while the wall stays a wall — sea cliffs, quarry walls, escarpments. 0 = off'],
        ['cliffProtectAngle', 'Protection angle', 25, 60, 1, '°', 'Slope above which a face counts as protected cliff'],
        ['rillStrength', 'Rills (flow lines)', 0, 1, 0.05, '', 'Fine converging runoff channels cut into the slopes after the droplet erosion — the flow-line texture of eroded mountains. 0 = off'],
        ['rillSteps', 'Rill steps', 1, 20, 1, '', 'Erosion steps for the rills (more = deeper, longer lines)'],
        ['fluvialDiffusion', 'Hillslope diffusion', 0, 1, 0.05, '', 'Soil creep rounding the interfluves between valleys'],
      ] },
      { title: 'Hydraulic erosion', controls: [
        ['droplets', 'Droplets', 0, 600000, 10000, '', 'Rain particles traced downhill'],
        ['erodeSpeed', 'Cutting rate', 0.05, 1, 0.05, '', 'How fast flowing water removes material'],
        ['depositSpeed', 'Deposition rate', 0.05, 1, 0.05, '', 'How readily sediment settles into fans'],
        ['sedimentCapacity', 'Sediment capacity', 1, 12, 0.5, '', 'Load a fast droplet can carry'],
        ['evaporation', 'Evaporation', 0.002, 0.05, 0.001, '', 'Droplet lifetime limit by drying'],
        ['inertia', 'Inertia', 0, 0.5, 0.01, '', 'Momentum of flow around obstacles'],
        ['erosionRadius', 'Brush radius', 1, 6, 1, 'px', 'Width of the carved channel'],
        ['hardnessInfluence', 'Rock resistance', 0, 1, 0.05, '', 'How much caprock resists cutting'],
      ] },
      { title: 'Thermal weathering', controls: [
        ['thermalIterations', 'Slump passes', 0, 80, 1, '', 'Iterations of talus slumping'],
        ['talusSoft', 'Repose angle · soft', 20, 45, 1, '°', 'Scree settles to this slope'],
        ['talusHard', 'Repose angle · hard', 45, 89, 1, '°', 'Caprock holds faces up to this angle'],
        ['thermalRate', 'Slump rate', 0.1, 1, 0.05, '', 'Fraction of excess moved per pass'],
      ] },
    ],
  },
  {
    id: 'relief', name: 'Cliff depth', type: 'True-3D cliffs, overhangs & undercuts', stage: 'mesh', color: '#b9a3d6',
    cards: [
      { title: 'True-3D cliffs (SDF chunks)', controls: [
        ['sdfOn', 'Enable 3D cliffs', 0, 1, 1, '', 'Steep parts of the terrain are re-meshed from a 3D field (heightfield distance + strata carving) in chunks that are watertight with the heightfield mesh — real undercuts, overhangs, notches and shelters'],
        ['sdfAngle', 'Cliff angle', 35, 80, 1, '°', 'Cells steeper than this get a 3D chunk'],
        ['sdfUndercut', 'Undercut depth', 0, 20, 0.5, 'm', 'How far soft strata beds are carved back under hard beds (follows the strata model: band, dip, hardness)'],
        ['sdfBedContrast', 'Bed contrast', 0.3, 3, 0.1, '', 'Higher = only the softest beds recede'],
        ['sdfPockets', 'Pocket scale', 3, 40, 1, 'm', 'Scale of the 3D noise that breaks undercuts into alcoves'],
        ['sdfJoints', 'Joints', 0, 1.5, 0.05, '', 'Near-vertical joint cuts and chimneys'],
        ['sdfPits', 'Pits', 0, 1, 0.05, '', 'Weathering hollows'],
        ['sdfRough', 'Roughness', 0, 1.5, 0.05, '', 'Fine 3D roughness on the carved faces'],
        ['sdfPushPull', '3D push–pull', 0, 10, 0.25, 'm', 'Blocky buttresses stand out of the carved face and recesses go back into it, varying with height so blocks overhang the recess below (blockiness from Landform → Rugged outcrops)'],
        ['sdfPushScale', 'Block scale', 4, 80, 1, 'm', 'Size of the 3D blocks'],
        ['sdfBlocks', 'Rock blocks', 0, 6, 0.25, 'm', 'The face becomes a wall of discrete rock blocks — bed rows × two vertical joint families, staggered bed to bed — each block proud of or set back from the face by up to this much, with a bevelled rim and a crack groove around it (3-D geometry, not shading). Blocks smaller than ~3 voxels cannot be resolved: lower the world size or raise the voxel budget for finer blocks'],
        ['sdfBlockSize', 'Rock block size', 2, 40, 0.5, 'm', 'Width of the rock blocks (each bed varies it; thick beds give wider blocks)'],
        ['sdfBlockLoss', 'Missing blocks', 0, 1, 0.05, '', 'Share of blocks that have fallen out of the face, leaving a deep recess'],
        ['sdfBlockSwing', 'Block swing', 0, 1, 0.05, '', 'How far the block lattice rotates across the cliff: joint sets curve around a dome in the real thing, and one axis-aligned grid over every wall is what reads as tiling'],
        ['sdfVertical', 'Vertical wall', 0, 1.6, 0.05, '', 'Shears every steep face so its crest moves out and its foot moves in: 1 = the face stands truly vertical (|) whatever the heightfield slope, above 1 it overhangs (\\ /). Cliffs only — the ground before and behind the face is left alone'],
        ['sdfLean', 'Lean (| /)', 0, 12, 0.5, 'm', 'The face is cut back progressively from crest to foot, so a cliff stands vertical to overhanging (| /) instead of leaning back (| \\)'],
        ['sdfLeanReach', 'Lean reach', 8, 80, 2, 'm', 'Plan distance over which crest and foot of a face are found'],
        ['sdfBlend', 'Blend margin', 1, 5, 1, 'cells', 'Cells over which the 3D field fades back to the plain heightfield'],
        ['sdfChunk', 'Chunk size', 8, 32, 8, 'cells', ''],
        ['sdfVoxel', 'Voxels per cell', 0, 4, 1, '', '0 = auto: the finest resolution that fits the voxel budget. 2 = voxels half the grid cell (8× the work), 4 = a quarter (64× — only the steepest walls need it)'],
        ['sdfVoxelBudget', 'Voxel budget', 2, 160, 2, 'M', 'Total voxels (in the surface band) auto mode may spend; raise it for finer cliffs if the machine can take it'],
        ['sdfMaxChunks', 'Chunk budget', 50, 2000, 50, '', 'Chunks with the most cliff area are built first; the rest fall back to the displaced heightfield'],
      ] },
      { title: 'Face displacement', controls: [
        ['overhang', 'Caprock overhang', 0, 8, 0.25, 'm', 'Hard beds pushed out of the face, soft beds recessed — real overhangs a heightmap cannot hold (capped at ~85% of the grid cell)'],
        ['buttress', 'Buttress bulge', 0, 1, 0.05, '', 'Large-scale swelling of the faces into ribs and alcoves'],
        ['ledgeNoise', 'Ledge irregularity', 0, 1, 0.05, '', 'Breaks ledges into blocks and notches'],
      ] },
    ],
  },
  {
    id: 'mesh', name: 'Mesh & detail', type: 'Subdivision & displaced micro-relief', stage: 'mesh', color: '#8fd0c0',
    cards: [
      { title: 'Mesh detail', controls: [
        ['meshSubdivision', 'Mesh subdivision', 1, 6, 1, '×', 'Vertices per heightfield cell (bicubic). Higher = finer relief detail on the faces; the mesh side is capped at 4097 vertices'],
        ['detailRelief', 'Detail relief', 0, 3, 0.05, 'm', 'Fine bumps and knobs pushed along the surface normal — independent of the heightfield'],
        ['detailScale', 'Detail scale', 1, 40, 0.5, 'm', ''],
        ['detailCliffBias', 'Cliff bias', 0, 1, 0.05, '', '0 = everywhere, 1 = steep rock only'],
        ['rockyAmount', 'Rocky facets', 0, 3, 0.05, 'm', 'Angular joint-bounded blocks on steep hard rock (soft beds stay smooth) — the "rocky" look of fractured faces'],
        ['rockyScale', 'Block size', 1, 20, 0.5, 'm', ''],
        ['rockyAngular', 'Blockiness', 0, 1, 0.05, '', '0 = rounded knobs, 1 = sharp-edged blocks'],
        ['cragAmount', 'Crags', 0, 8, 0.25, 'm', 'Larger joint-bounded blocks on steep hard rock, each set back or protruding by its own amount — breaks a cliff face into rugged masses (also in the 3-D chunks)'],
        ['cragScale', 'Crag size', 4, 40, 1, 'm', 'Size of the crag blocks'],
      ] },
    ],
  },
  {
    id: 'features', name: 'Rivers & lakes', type: 'Simulated drainage & drawn guides', stage: 'mesh', color: '#7fb3d5',
    cards: [
      { title: 'Draw', kind: 'draw' },
      { title: 'Features', kind: 'featureList' },
      { title: 'Simulated rivers', stage: 'terrain', controls: [
        ['riverSim', 'Simulate drainage', 0, 1, 1, '', 'Fill depressions, route D8 flow over the eroded surface and carve a river network where the catchment is large enough. Drawn rivers become guides that inject flow'],
        ['riverCatchment', 'Min. catchment', 0.01, 2, 0.01, 'km²', 'Drainage area needed before a channel forms — smaller = denser network'],
        ['riverWidthScale', 'Width per √km²', 4, 120, 1, 'm', 'Channel width = this × √catchment'],
        ['riverMaxWidth', 'Max width', 10, 400, 5, 'm', ''],
        ['riverFloodplain', 'Floodplain', 0, 2, 0.05, '', 'Valley floor planed flat beside the river, in channel widths — the river valley of a mature stream. 0 = banks only'],
        ['riverBankErosion', 'Bank gullying', 0, 2, 0.05, '', 'Soil erosion on the valley sides beside the rivers: runoff cuts small gullies into the soft bank material (never below the water line)'],
        ['riverBankSteps', 'Gullying steps', 1, 12, 1, '', ''],
        ['riverDepthScale', 'Depth scale', 0.2, 3, 0.05, '', 'Channel depth ≈ scale × width^0.45'],
        ['riverWaterFrac', 'Water fill', 0.2, 0.9, 0.05, '', 'Fraction of the channel depth filled with water'],
        ['riverDrySlope', 'Dry above grade', 2, 45, 1, '°', 'Reaches steeper than this show the carved gully (wet rock, gravel) instead of standing water — mountain torrents do not read as flat water'],
        ['riverDryBig', 'Always wet above', 0.1, 20, 0.1, 'km²', 'Rivers with a catchment this large keep water on any grade'],
        ['riverBraiding', 'Braiding', 0, 1, 0.05, '', 'Gravel bars and split threads on wide, gentle reaches (river plains)'],
        ['riverLakes', 'Lakes in depressions', 0, 1, 1, '', 'Standing water in closed basins. Off by default: with sedimentation on, basins silt up and the river runs through them'],
        ['riverLakeFill', 'Lake fill', 0, 1, 0.05, '', '1 = basins fill to their spill level · lower leaves a dry floor with the river crossing it'],
        ['riverLakeMax', 'Lake area cap', 0, 40, 1, '%', 'Largest/deepest basins are filled first until this share of the map is lake; beyond it only a small pond remains where a river ends in a hollow'],
        ['riverLakeMin', 'Min. lake area', 0.001, 0.2, 0.001, 'km²', 'Smaller depressions stay dry unless a river feeds them'],
        ['riverGuideFlow', 'Guide flow', 0, 5, 0.1, 'km²', 'Catchment injected at the head of each drawn river so it always carries water'],
      ] },
      { title: 'Drawn river guides', stage: 'terrain', controls: [
        ['riverWidth', 'Channel width', 2, 120, 1, 'm', 'Width of the carved channel'],
        ['riverDepth', 'Channel depth', 0.5, 40, 0.5, 'm', 'Incision below the smoothed valley profile'],
        ['riverBank', 'Bank angle', 10, 80, 1, '°', 'Slope of the cut banks'],
        ['riverMaxBank', 'Bank height', 2, 200, 1, 'm', 'Banks are shaped up to this height (the cut fades out towards it); keep it low for valleys that follow the terrain, high for gorges'],
        ['riverMeander', 'Meander', 0, 1, 0.05, '', 'Lateral sinuosity added to the drawn line'],
        ['riverErosion', 'Water erosion', 0, 1, 0.05, '', 'Share of erosion droplets that start in the river and how much water they carry — carves the bed and drains the slopes into it. Rivers flow from their higher end'],
        ['riverWaterDepth', 'Water depth', 0.2, 10, 0.1, 'm', 'Depth of water standing in the channel'],
      ] },
      { title: 'Lakes', controls: [
        ['lakeLevelOffset', 'Level above click', 0, 60, 1, 'm', 'New lakes fill to the clicked ground height plus this'],
        ['lakeDepth', 'Bed depth', 1, 40, 0.5, 'm', 'Flat silt bed this far below the level'],
      ] },
      { title: 'Water in channels', controls: [
        ['riverWater', 'Water in rivers', 0, 1, 1, '', 'Off = dried river beds (cobbles and silt)'],
        ['lakeWater', 'Water in lakes', 0, 1, 1, '', 'Off = dried lake beds'],
      ] },
      { title: 'Overlay', controls: [
        ['showFeatureLines', 'Show feature lines', 0, 1, 1, '', 'Guide lines over roads, rivers and lake markers'],
      ] },
    ],
  },
  {
    id: 'roads', name: 'Roads', type: 'Cut, fill and shading', stage: 'mesh', color: '#b8c48f',
    cards: [
      { title: 'Roads', controls: [
        ['roadWidth', 'Road width', 2, 30, 0.5, 'm', '6 m ≈ two-lane; 3.5 m ≈ track'],
        ['roadShoulder', 'Shoulder', 0, 10, 0.5, 'm', 'Bare verge either side'],
        ['roadSmoothing', 'Grade smoothing', 10, 300, 5, 'm', 'Length over which the road profile is averaged'],
        ['roadCut', 'Cut slope', 30, 85, 1, '°', 'Rock cut above the road'],
        ['roadFill', 'Fill slope', 20, 60, 1, '°', 'Embankment below the road'],
        ['roadShading', 'Road surface', 0, 1, 1, '', 'Shade the carriageway (off = bare carved ground)'],
        ['roadColor', 'Road colour', 0, 0, 0, 'color', ''],
      ] },
    ],
  },
  {
    id: 'rocks', name: 'Rocks', type: 'Where they come from', stage: 'rocks', color: '#a8bbeb',
    cards: [
      { title: 'Scatter', controls: [
        ['rocksEnabled', 'Enable rocks', 0, 1, 1, '', ''],
        ['rockDensity', 'Density', 0, 1, 0.05, '', 'How many candidate sites are tried (the geology decides which of them get a rock)'],
        ['rockClustering', 'Patchiness', 0, 1, 0.05, '', 'Even spread → grouped into patches, so no category covers everything evenly'],
        ['rockClusterScale', 'Patch size', 10, 400, 5, 'm', ''],
        ['rockTilt', 'Follow slope', 0, 1, 0.05, '', 'Upright → aligned to the ground normal'],
      ] },
      { title: 'Origins', controls: [
        ['rockInPlace', 'Bedrock in place', 0, 1, 0.05, '', 'Angular blocks broken off the rock where it is steep, bare and hard — they sit flush with the face, deep into the ground'],
        ['rockTalus', 'Talus & scree', 0, 1, 0.05, '', 'Debris the faces above shed: it piles at the foot and fines away down the apron, with the biggest blocks nearest the cliff'],
        ['rockChannel', 'Channel lag', 0, 1, 0.05, '', 'Boulders a stream only keeps where it has the power to move them — big in the steep headwaters, smaller downstream, water-rounded'],
        ['rockBars', 'Bars & fans', 0, 1, 0.05, '', 'Where the flow slows it drops its load: inside bars, gravel sheets at canyon mouths, braided fans'],
        ['rockCaps', 'Caps & tors', 0, 1, 0.05, '', 'A resistant bed armours a hill top and survives as a cap or a tor on the high ground'],
        ['rockErratics', 'Erratics', 0, 1, 0.05, '', 'Rare solitary boulders on the flats, half buried and weathered, far from any face'],
        ['rockShore', 'Shore cord', 0, 1, 0.05, '', 'Cobbles laid along the edge of standing water, rounded by the waves'],
        ['rockSoilHide', 'Soil hides rock', 0, 1, 0.05, '', 'How strongly fine cover (soil, silt, sand) buries rock: at 1 a rock only shows where soil cannot cling'],
      ] },
      { title: 'Rock shape', controls: [
        ['rockSizeMin', 'Smallest rock', 0.3, 6, 0.1, 'm', ''],
        ['rockSizeMax', 'Largest rock', 2, 40, 0.5, 'm', ''],
        ['rockAngularity', 'Angularity', 0, 1, 0.05, '', 'Planar cleavage cuts vs. rounded boulders'],
        ['rockEmbed', 'Embed depth', 0, 1, 0.05, '', 'How deep rocks sit in the ground'],
        ['rockSizeSpread', 'Size spread', 0, 1, 0.05, '', 'Heavy tail of the size distribution: 0 = all alike, 1 = many pebbles and a few monsters'],
        ['rockSeed', 'Rock seed', 1, 999, 1, '', ''],
      ] },
      { title: 'Gravel stones', controls: [
        ['pebblesOn', 'Enable stones', 0, 1, 1, '', 'Real small stone meshes on scree aprons and river beds, on top of the pebble shading'],
        ['pebbleDensity', 'Stone density', 0, 1, 0.05, '', 'Up to 40 000 instances'],
        ['pebbleSize', 'Largest stone', 0.1, 2, 0.05, 'm', ''],
      ] },
    ],
  },
  {
    id: 'material', name: 'Rock colour', type: 'Palette, oxide & material response', stage: 'live', color: '#cab281',
    cards: [
      { title: 'Rock type', kind: 'palette' },
      { title: 'Colours', controls: [
        ['rockA', 'Base rock', 0, 0, 0, 'color', 'Dominant bed colour'],
        ['rockB', 'Dark beds', 0, 0, 0, 'color', 'Recessive / shaly beds'],
        ['rockC', 'Pale beds', 0, 0, 0, 'color', 'Caprock and resistant beds'],
        ['fresh', 'Fresh break', 0, 0, 0, 'color', 'Unweathered rock exposed by spalling'],
        ['oxide', 'Oxide stain', 0, 0, 0, 'color', 'Iron-oxide pocket colour'],
      ] },
      { title: 'Oxide & cavity', controls: [
        ['oxideOn', 'Enable oxide', 0, 1, 1, '', ''],
        ['oxideAmount', 'Oxide amount', 0, 1, 0.05, '', 'Coverage of iron-oxide pockets'],
        ['oxideScale', 'Oxide scale', 0.5, 30, 0.5, 'm', 'Size of the staining pockets'],
        ['cavityStrength', 'Cavity shading', 0, 1, 0.05, '', 'Darken concavities, lighten convex edges'],
      ] },
      { title: 'Material response', controls: [
        ['bumpScale', 'Bump strength', 0, 2, 0.05, '', 'Global multiplier on all normal detail'],
        ['baseRoughness', 'Base roughness', 0.3, 1, 0.05, '', ''],
      ] },
    ],
  },
  {
    id: 'bedding', name: 'Painted beds', type: 'Strata colour in the shader', stage: 'live', color: '#c9a6dd',
    cards: [
      { title: 'Strata', controls: [
        ['strataOn', 'Enable strata', 0, 1, 1, '', ''],
        ['strataContrast', 'Bed contrast', 0, 1, 0.05, '', 'Tonal difference between beds'],
        ['strataBandScale', 'Band scale', 0.25, 4, 0.05, '×', 'Painted beds relative to the carved beds (1 = the colour follows the stratigraphic column exactly)'],
        ['laminae', 'Laminae', 0, 1, 0.05, '', 'Thin sub-bands inside each bed'],
        ['bedGradient', 'Bed shading', 0, 1, 0.05, '', 'Darkening towards the base of each bed'],
        ['seamStrength', 'Seam darkness', 0, 1, 0.05, '', 'Dark lines along bedding planes'],
        ['seamWidth', 'Seam width', 0.01, 0.3, 0.01, '', 'Fraction of the bed taken by the seam'],
        ['hardnessTint', 'Caprock tint', 0, 1, 0.05, '', 'Hard beds paler, soft beds warmer'],
      ] },
    ],
  },
  {
    id: 'jointing', name: 'Jointing', type: 'Block lattice on the face', stage: 'live', color: '#9db6e8',
    cards: [
      { title: 'Jointing', controls: [
        ['jointOn', 'Enable jointing', 0, 1, 1, '', 'Cracks that break the face into blocks: two vertical joint sets (staggered bed to bed like brickwork) and the bedding planes, each joint with its own width, some missing, each block with its own tilt and tone'],
        ['jointStrength', 'Jointing', 0, 1.5, 0.05, '', ''],
        ['jointSpacing', 'Joint spacing', 0.6, 20, 0.1, 'm', 'Block size along the face (scaled by bed thickness)'],
        ['jointBend', 'Joint swing', 0, 1, 0.05, '', 'How much the two joint sets curve and rotate across the massif, so the lattice is never square to the view over a whole cliff'],
        ['jointVary', 'Block size varies', 0, 1, 0.05, '', 'Coarse panels of different block size, on the same grid the true-3D blocks are varied by — massive slabs here, closely jointed rock there'],
        ['texWarp', 'Pattern warp', 0, 1, 0.05, '', 'Warps and stretches the cells of every face pattern (joints, spalls, flakes, gravel) so nothing marches along the world axes'],
        ['facePatch', 'Detail patchiness', 0, 1, 0.05, '', 'How much the *amount* of jointing, spalling and flaking varies from patch to patch — 0 = the whole cliff is finished the same way'],
        ['jointWidth', 'Crack width', 0.05, 1, 0.01, 'm', ''],
        ['jointDepth', 'Crack depth', 0, 1.5, 0.05, 'm', 'How far the grooves read into the face'],
        ['jointStagger', 'Stagger', 0, 1, 0.05, '', 'Offset of the vertical joints from bed to bed (0 = continuous columns, 1 = brickwork)'],
        ['jointBlocks', 'Block relief', 0, 1, 0.05, '', 'Per-block tilt and tone'],
        ['jointDropout', 'Missing joints', 0, 0.8, 0.05, '', 'Share of joints that do not show, so block sizes vary'],
      ] },
    ],
  },
  {
    id: 'grain', name: 'Grain', type: 'Aggregate texture', stage: 'live', color: '#c8c3a4',
    cards: [
      { title: 'Grain', controls: [
        ['grainOn', 'Enable grain', 0, 1, 1, '', ''],
        ['grainSize', 'Grain size', 0.1, 3, 0.05, 'm', 'Scale of the aggregate bump'],
        ['grainStrength', 'Grain relief', 0, 1, 0.05, '', 'Bump strength of the grain'],
        ['grainContrast', 'Grain mottle', 0, 1, 0.05, '', 'Colour mottling from the grain field'],
        ['grainFineness', 'Fine grain', 0, 1, 0.05, '', 'Scale of the secondary fine grain'],
      ] },
    ],
  },
  {
    id: 'flakes', name: 'Mineral flakes', type: 'Layered grain', stage: 'live', color: '#d8c38a',
    cards: [
      { title: 'Flakes', controls: [
        ['flakesOn', 'Enable flakes', 0, 1, 1, '', ''],
        ['flakeStrength', 'Coverage', 0, 1, 0.05, '', 'Overall amount of plates on exposed rock'],
        ['flakeEdge', 'Rim highlight', 0, 1, 0.05, '', 'Bright bevel along plate edges'],
        ['flakeSheen', 'Plate sheen', 0, 1, 0.05, '', 'Lower roughness on plates so they catch light'],
        ['flakeSparkle', 'Crystal glitter', 0, 2, 0.05, '', 'Sun glints on crystal plates (view dependent)'],
        ['flakeOxide', 'Oxidise with runoff', 0, 1, 0.05, '', 'Plates take the oxide colour where water flows and in oxide pockets — the erosion channels drive the tint'],
      ] },
      { title: 'Layer 1 · base chips', controls: [
        ['flakeOn1', 'Enable layer', 0, 1, 1, '', 'Largest plates at the bottom of the stack'],
        ['flakeColor1', 'Layer colour', 0, 0, 0, 'color', ''],
        ['flakeSize1', 'Plate size', 0.03, 8, 0.01, 'm', ''],
        ['flakeDensity1', 'Density', 0.1, 1, 0.05, '', 'Fraction of cells that grow a plate'],
        ['flakeHeight1', 'Plate height', 0, 30, 0.5, 'mm', 'Tilt and rim bevel in the normal'],
        ['flakeVar1', 'Tint variation', 0, 1, 0.05, '', 'Per-plate lightness / warmth jitter around the layer colour'],
        ['flakeShape1', 'Shape', 0, 1, 0.05, '', 'Angular chips → rounded grains'],
        ['flakeCrystal1', 'Crystals', 0, 1, 0.05, '', 'Share of plates that are crystals: brighter, glossy, they glint in the sun'],
        ['flakeReveal1', 'Reveal by hardness', -1, 1, 0.05, '', '−1 only on soft / eroded beds · 0 everywhere · +1 only on hard caprock'],
      ] },
      { title: 'Layer 2 · laminae', controls: [
        ['flakeOn2', 'Enable layer', 0, 1, 1, '', 'Mid-size plates over the chips'],
        ['flakeColor2', 'Layer colour', 0, 0, 0, 'color', ''],
        ['flakeSize2', 'Plate size', 0.03, 8, 0.01, 'm', ''],
        ['flakeDensity2', 'Density', 0.1, 1, 0.05, '', 'Fraction of cells that grow a plate'],
        ['flakeHeight2', 'Plate height', 0, 30, 0.5, 'mm', 'Tilt and rim bevel in the normal'],
        ['flakeVar2', 'Tint variation', 0, 1, 0.05, '', 'Per-plate lightness / warmth jitter around the layer colour'],
        ['flakeShape2', 'Shape', 0, 1, 0.05, '', 'Angular chips → rounded grains'],
        ['flakeCrystal2', 'Crystals', 0, 1, 0.05, '', 'Share of plates that are crystals: brighter, glossy, they glint in the sun'],
        ['flakeReveal2', 'Reveal by hardness', -1, 1, 0.05, '', '−1 only on soft / eroded beds · 0 everywhere · +1 only on hard caprock'],
      ] },
      { title: 'Layer 3 · fine grain', controls: [
        ['flakeOn3', 'Enable layer', 0, 1, 1, '', 'Smallest plates on top; the usual place for crystals'],
        ['flakeColor3', 'Layer colour', 0, 0, 0, 'color', ''],
        ['flakeSize3', 'Plate size', 0.03, 8, 0.01, 'm', ''],
        ['flakeDensity3', 'Density', 0.1, 1, 0.05, '', 'Fraction of cells that grow a plate'],
        ['flakeHeight3', 'Plate height', 0, 30, 0.5, 'mm', 'Tilt and rim bevel in the normal'],
        ['flakeVar3', 'Tint variation', 0, 1, 0.05, '', 'Per-plate lightness / warmth jitter around the layer colour'],
        ['flakeShape3', 'Shape', 0, 1, 0.05, '', 'Angular chips → rounded grains'],
        ['flakeCrystal3', 'Crystals', 0, 1, 0.05, '', 'Share of plates that are crystals: brighter, glossy, they glint in the sun'],
        ['flakeReveal3', 'Reveal by hardness', -1, 1, 0.05, '', '−1 only on soft / eroded beds · 0 everywhere · +1 only on hard caprock'],
      ] },
    ],
  },
  {
    id: 'exfoliation', name: 'Spalling', type: 'Sheets flaked off the face', stage: 'live', color: '#c9a37c',
    cards: [
      { title: 'Spalls', controls: [
        ['peelOn', 'Enable spalling', 0, 1, 1, '', 'Irregular patches where a thin sheet has flaked off: a shallow step with a bevelled rim, paler fresh rock inside'],
        ['peelStrength', 'Amount', 0, 1, 0.05, '', ''],
        ['peelCoverage', 'Coverage', 0, 1, 0.05, '', 'Share of the rock face that has spalled'],
        ['peelScale', 'Patch size', 0.2, 12, 0.1, 'm', ''],
        ['peelThickness', 'Sheet thickness', 0, 0.15, 0.005, 'm', 'Height of the step at the patch edge'],
        ['peelSecond', 'Small spalls', 0, 1, 0.05, '', 'Second generation of smaller patches stepping down inside and around the first'],
        ['peelPits', 'Weathering pits', 0, 1, 0.05, '', 'Sparse small holes'],
        ['peelBedding', 'Follow bedding', 0, 1, 0.05, '', 'Stretch patches horizontally along the beds'],
        ['peelFresh', 'Fresh contrast', 0, 1, 0.05, '', 'How pale the freshly exposed rock is'],
        ['peelShadow', 'Rim shadow', 0, 1, 0.05, '', 'Soft darkening in the bevel'],
      ] },
    ],
  },
  {
    id: 'sediment', name: 'Sediment', type: 'Beds, facies, ripples & mud cracks', stage: 'live', color: '#e0c07a',
    cards: [
      { title: 'Sediment', controls: [
        ['sedimentOn', 'Enable sediment', 0, 1, 1, '', 'Water- and weathering-laid fill, drawn as beds instead of a flat tint'],
        ['sedimentAmount', 'Sediment cover', 0, 1, 0.05, '', 'How far the fill reads as layered sediment rather than soil: it covers every deposit, and every gentle soil-covered flat on top of it'],
        ['sedBedScale', 'Bed thickness', 0.06, 4, 0.02, 'm', 'Thickness of one bed - the strata of the *fill*, not of the rock beneath it (coarse facies lay thicker beds)'],
        ['sedBands', 'Bed contrast', 0, 1, 0.05, '', 'Tone step from bed to bed, and the dark parting on the bedding plane'],
        ['sedLaminae', 'Cross-lamination', 0, 1, 0.05, '', 'The fine set of laminae inside each bed, inclined and wandering sideways'],
        ['sedFacies', 'Sorting', 0, 1, 0.05, '', 'How strictly the grain follows the energy that carried it: gravel on steep fast ground, sand on the bars, silt and mud on the quiet flats'],
        ['sedGrainBias', 'Grain offset', -1, 1, 0.05, '', 'The whole plain coarse or fine regardless of the flow: a braided stream in a dry range carries gravel everywhere, a lake plain is mud everywhere'],
        ['sedRipple', 'Ripples', 0, 1, 0.05, '', 'Current ripple relief on the surface of the fill - light on the stoss side, shadow on the lee'],
        ['sedRippleScale', 'Ripple wavelength', 0.15, 6, 0.05, 'm', 'Set by the grain: coarse facies carry wider ripples'],
        ['sedDir', 'Current direction', 0, 360, 5, 'deg', 'The direction the ripple trains run along, wandering patch to patch'],
        ['sedCracks', 'Mud cracks', 0, 1, 0.05, '', 'Desiccation polygons on the open silt flats'],
        ['sedGrain', 'Grain', 0, 1, 0.05, '', 'Grittiness of the fill itself (raise Gravel in the same group for real stones)'],
        ['sedSlope', 'Slope limit', 15, 75, 1, 'deg', 'Steeper than this the fill is scoured off and the rock shows'],
        ['sedColorGravel', 'Gravel colour', 0, 0, 0, 'color', 'Coarse facies: the bars and fans'],
        ['sedColorSand', 'Sand colour', 0, 0, 0, 'color', 'Mid facies: ripples, dunes, low bars'],
        ['sedColorSilt', 'Silt & mud colour', 0, 0, 0, 'color', 'Fine facies: floodplain, lake and lagoon quiet water'],
      ] },
    ],
  },
  {
    id: 'cover', name: 'Soil & gravel', type: 'Regolith, scree and runoff staining', stage: 'live', color: '#9fb57a',
    cards: [
      { title: 'Runoff', controls: [
        ['runoffOn', 'Enable runoff', 0, 1, 1, '', ''],
        ['wetness', 'Wetness', 0, 1, 0.05, '', 'Dark wet staining along drainage'],
        ['streakAmount', 'Face streaks', 0, 1, 0.05, '', 'Vertical water streaks down the faces'],
        ['streakScale', 'Streak scale', 1, 30, 0.5, 'm', 'Spacing of the streaks'],
      ] },
      { title: 'Soil', controls: [
        ['soilOn', 'Enable soil', 0, 1, 1, '', ''],
        ['soilAmount', 'Soil cover', 0, 1, 0.05, '', 'Regolith over gentle, soft, concave ground; rock stays exposed on hard convex knolls and ribs'],
        ['soilSlope', 'Slope limit', 15, 70, 1, '°', 'Steeper ground is bare rock'],
        ['soilClods', 'Clods & grit', 0, 1, 0.05, '', 'Tonal variation and micro-relief of the soil surface'],
        ['soilMoisture', 'Moisture', 0, 1, 0.05, '', 'Darker, less rough soil along flow lines and just above the water'],
        ['soilAlluvium', 'Alluvium', 0, 1, 0.05, '', 'Silt / sand the rivers dropped, painted on the soil - the bedded look of that fill is under Sediment below'],
        ['soilColor', 'Soil', 0, 0, 0, 'color', ''],
        ['soilLight', 'Soil · pale', 0, 0, 0, 'color', 'Dry clods and grit'],
      ] },
      { title: 'Gravel', controls: [
        ['gravelOn', 'Enable gravel', 0, 1, 1, '', ''],
        ['gravelAmount', 'Gravel on scree', 0, 1, 0.05, '', 'Fine debris texture where sediment collected'],
        ['gravelScale', 'Stone size', 0.03, 1.5, 0.01, 'm', 'Largest pebbles; a second generation at 0.45× fills the gaps'],
        ['gravelRelief', 'Stone relief', 0, 1, 0.05, '', 'Dome normal on each stone'],
        ['gravelVariation', 'Stone variation', 0, 1, 0.05, '', 'Tone and warmth jitter per stone'],
        ['gravelColor', 'Stone colour', 0, 0, 0, 'color', 'Matrix between the stones is a mix of this and the silt colour'],
      ] },
    ],
  },
  {
    id: 'plants', name: 'Plants & snow', type: 'Vegetation, moss and snow', stage: 'live', color: '#9fb57a',
    cards: [
      { title: 'Vegetation', controls: [
        ['vegOn', 'Enable vegetation', 0, 1, 1, '', ''],
        ['vegetation', 'Vegetation', 0, 1, 0.05, '', 'Grass and scrub on gentle ground'],
        ['vegSlope', 'Slope limit', 15, 70, 1, '°', 'Steeper ground stays bare'],
        ['vegScale', 'Patch scale', 4, 80, 1, 'm', ''],
        ['vegPatchiness', 'Patchiness', 0, 1, 0.05, '', 'Broken patches vs. continuous cover'],
        ['dryness', 'Dry grass', 0, 1, 0.05, '', 'Mix in dry straw patches'],
        ['grassA', 'Grass', 0, 0, 0, 'color', ''],
        ['grassB', 'Grass · pale', 0, 0, 0, 'color', ''],
        ['dryColor', 'Dry grass', 0, 0, 0, 'color', ''],
      ] },
      { title: 'Moss', controls: [
        ['mossOn', 'Enable moss', 0, 1, 1, '', ''],
        ['mossiness', 'Moss on rock', 0, 1, 0.05, '', 'Lichen and moss in sheltered cracks'],
        ['mossScale', 'Moss scale', 0.5, 20, 0.5, 'm', ''],
        ['mossColor', 'Moss colour', 0, 0, 0, 'color', ''],
      ] },
      { title: 'Snow', controls: [
        ['snowOn', 'Enable snow', 0, 1, 1, '', ''],
        ['snowLine', 'Snow line', 0, 5000, 10, 'm', 'Snow settles above this elevation'],
        ['snowSlope', 'Slope limit', 10, 80, 1, '°', 'Steeper faces shed snow'],
        ['snowSoftness', 'Transition', 0.1, 3, 0.1, '', 'Width of the snow-line band'],
        ['snowRoughness', 'Snow roughness', 0.3, 1, 0.05, '', ''],
        ['snowColor', 'Snow colour', 0, 0, 0, 'color', ''],
      ] },
    ],
  },
  {
    id: 'water', name: 'Water', type: 'Sea level & water bodies', stage: 'live', color: '#5aa7c9',
    cards: [
      { title: 'Sea level', controls: [
        ['waterMeshes', 'Water as meshes', 0, 1, 1, '', 'Off (Gaea-style): water is painted onto the terrain surface as a mask — flat colour, clear in the shallows — with no water geometry. On: a sea plane and river / lake sheets'],
        ['waterEnabled', 'Sea', 0, 1, 1, '', 'Global water level: everything below is sea (painted or plane)'],
        ['seaLevel', 'Sea level', -200, 800, 1, 'm', 'Everything below this is flooded — live'],
      ] },
      { title: 'Appearance', controls: [
        ['waterColor', 'Sea colour', 0, 0, 0, 'color', ''],
        ['waterOpacity', 'Sea opacity', 0.2, 1, 0.05, '', ''],
        ['riverColor', 'River / lake colour', 0, 0, 0, 'color', 'Colour of deep river and lake water'],
        ['riverShallowColor', 'Shallows tint', 0, 0, 0, 'color', 'Tint of the clear water over the bed near the shore'],
        ['riverOpacity', 'River / lake opacity', 0.2, 1, 0.05, '', ''],
        ['riverClearDepth', 'Clear depth', 0.2, 6, 0.1, 'm', 'Water shallower than this is see-through, showing the gravel bed; deeper water takes the river colour'],
        ['riverFoam', 'White water', 0, 1, 0.05, '', 'Foam on rapids and falls — where the water surface drops steeply'],
        ['shoreWet', 'Shoreline wetness', 0, 1, 0.05, '', 'Dark wet band just above the waterline'],
        ['bedShading', 'Bed shading', 0, 1, 1, '', 'Silt / cobble shading on river and lake beds'],
        ['siltColor', 'Silt colour', 0, 0, 0, 'color', ''],
      ] },
    ],
  },
  {
    id: 'sun', name: 'Sun & atmosphere', type: 'Lighting', stage: 'live', color: '#e8b65f',
    cards: [
      { title: 'Sun direction', controls: [
        ['sunAzimuth', 'Azimuth', 0, 360, 1, '°', ''],
        ['sunElevation', 'Elevation', 2, 80, 1, '°', 'Low sun rakes the cliff faces'],
      ] },
      { title: 'Atmosphere', controls: [
        ['turbidity', 'Turbidity', 1, 12, 0.5, '', 'Haze and aerosol density'],
        ['fogDensity', 'Valley fog', 0, 1, 0.05, '', 'Distance haze strength'],
        ['exposure', 'Exposure', 0.1, 1.5, 0.05, '', ''],
      ] },
    ],
  },
  {
    id: 'viewport', name: 'Viewport', type: 'Display & export', stage: 'live', color: '#99aafa',
    cards: [
      { title: 'Display', controls: [
        ['wireframe', 'Wireframe', 0, 1, 1, '', ''],
        ['showRocks', 'Show rocks', 0, 1, 1, '', ''],
        ['autoRotate', 'Auto-rotate', 0, 1, 1, '', ''],
      ] },
      { title: 'Isolate layer', controls: [
        ['debugView', 'Show only', 0, 8, 1, 'enum', 'Off|Strata|Grain|Flakes|Spalls|Pebbles|Cover|Masks|Features'],
      ] },
      { title: 'Export', kind: 'export' },
    ],
  },
];

export const outlinerSections = [
  { label: 'TERRAIN', ids: ['landform', 'deposition', 'strata', 'erosion'] },
  { label: 'CLIFFS & MESH', ids: ['relief', 'mesh'] },
  { label: 'WATER & ACCESS', ids: ['features', 'roads'] },
  { label: 'PROPS', ids: ['rocks'] },
  { label: 'SURFACE', ids: ['material', 'bedding', 'jointing', 'grain', 'flakes', 'exfoliation', 'sediment', 'cover', 'plants'] },
  { label: 'ENVIRONMENT', ids: ['sun', 'water', 'viewport'] },
];

export function paramsForStage(values, stage) {
  const keys = new Set();
  for (const g of groups) for (const c of g.cards) if ((c.stage || g.stage) === stage) for (const ctl of c.controls || []) keys.add(ctl[0]);
  const out = {};
  for (const k of keys) out[k] = values[k];
  return out;
}

export function stageOf(key) {
  for (const g of groups) for (const c of g.cards) for (const ctl of c.controls || []) if (ctl[0] === key) return c.stage || g.stage;
  if (key === 'palette') return 'live';
  if (key === 'features') return 'mesh';
  return 'terrain';
}
