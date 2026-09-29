import {
  SDFTerrainVolume,
  createEmptySDFVolume,
} from './terrainState';
import {
  applyGeneratorNode,
  applySDFCavesAndOverhangs,
  applyDesertPedimentNode,
  applySDFMonolithTowersNode,
  applyVerticalJointFissuresNode,
  applyBasalWindOverhangsNode,
  applyConicalTalusSkirtNode,
} from './generators';
import {
  applyCliffAndStrataErosion,
  applyHydraulicAndRiverErosion,
  applyAlluvialSedimentation,
  applyWindErosion,
  applyVolumetric3DStrataToSDF,
} from './erosion';
import { applySatMapTexturing } from './satmaps';
import { ExtractedSDFMesh, extractSDFMesh } from './marchingCubes';

export type NodeCategory = 'entry' | 'generators' | 'erosion' | 'sdf_caves' | 'texturing';

export interface GraphNodeData {
  id: string;
  type: string;
  title: string;
  subtitle: string;
  category: NodeCategory;
  x: number;
  y: number;
  collapsed?: boolean;
  locked?: boolean;
  bypassed?: boolean;
  params: Record<string, any>;
}

export interface GraphEdge {
  id: string;
  source: string;
  target: string;
}

export interface CatalogItem {
  type: string;
  title: string;
  subtitle: string;
  category: Exclude<NodeCategory, 'entry'>;
  defaultParams: Record<string, any>;
}

export const NODE_CATALOG: CatalogItem[] = [
  // Dedicated 3D CSG SDF Monolith Pipeline Nodes (Monument Valley / Wadi Rum)
  {
    type: 'DesertPediment',
    title: 'Desert Pediment',
    subtitle: 'Flat desert basin, washes & dunes',
    category: 'generators',
    defaultParams: {
      seed: 4217,
      scale: 18,
      baseElevation: 16,
      undulationHeight: 9,
      duneRippleStrength: 0.55,
      arroyoWashDepth: 3.5,
    },
  },
  {
    type: 'SDFMonolithTowers',
    title: 'SDF Monolith Towers',
    subtitle: 'True 3D CSG vertical sandstone towers',
    category: 'generators',
    defaultParams: {
      seed: 4217,
      monolithPreset: 'monument_buttes',
      towerHeight: 174,
      footprintScale: 1.0,
      wallVerticality: 0.92,
      caprockCrown: 0.85,
      steppedBenchRatio: 0.72,
    },
  },
  {
    type: 'VerticalJointFissures',
    title: 'Vertical Joint Fissures',
    subtitle: '3D tectonic chimneys & bedding notches',
    category: 'sdf_caves',
    defaultParams: {
      seed: 4217,
      fissureIntensity: 0.86,
      fissureSpacing: 22,
      chimneyDepth: 11.5,
      beddingNotchStrength: 0.82,
      columnFluting: 0.78,
    },
  },
  {
    type: 'BasalWindOverhangs',
    title: 'Basal Wind Overhangs',
    subtitle: '3D aeolian undercut alcoves & brows',
    category: 'sdf_caves',
    defaultParams: {
      seed: 4217,
      undercutDepth: 14.5,
      alcoveHeight: 38,
      browOverhang: 8.5,
      directionalBias: 0.68,
      windAngleDeg: 35,
    },
  },
  {
    type: 'ConicalTalusSkirt',
    title: 'Conical Talus Skirt',
    subtitle: '34° boulder & scree aprons at cliff foot',
    category: 'erosion',
    defaultParams: {
      seed: 4217,
      skirtHeight: 54,
      reposeAngleDeg: 34,
      gullyChuteStrength: 0.78,
      boulderRoughness: 0.65,
    },
  },

  // All-in-one 3D SDF Monolith Stamps
  {
    type: 'MonumentButtes',
    title: 'Monument Buttes Stamp',
    subtitle: 'Complete 3D SDF monolith stamp',
    category: 'generators',
    defaultParams: {
      seed: 4217,
      scale: 16,
      amplitude: 185,
      octaves: 6,
      lacunarity: 2.05,
      gain: 0.52,
      ridgeSharpness: 1.65,
      warpStrength: 24,
      valleyFloor: 0.75,
      butteDensity: 0.75,
      verticalFissures: 0.85,
      basalOverhang3D: 0.88,
      caprockCrown: 0.85,
      blendMode: 'replace',
    },
  },
  {
    type: 'MesaPlateauStamp',
    title: 'Table Mesa Stamp',
    subtitle: 'Stepped flat-topped mesas & slot canyons',
    category: 'generators',
    defaultParams: {
      seed: 5182,
      scale: 22,
      amplitude: 175,
      octaves: 6,
      lacunarity: 2.05,
      gain: 0.5,
      ridgeSharpness: 1.5,
      warpStrength: 20,
      valleyFloor: 0.7,
      butteDensity: 0.65,
      verticalFissures: 0.72,
      basalOverhang3D: 0.75,
      caprockCrown: 0.92,
      blendMode: 'replace',
    },
  },
  {
    type: 'NeedleSpiresStamp',
    title: 'Sandstone Needles Stamp',
    subtitle: 'Clustered rock pinnacles & hoodoo towers',
    category: 'generators',
    defaultParams: {
      seed: 7319,
      scale: 12,
      amplitude: 192,
      octaves: 6,
      lacunarity: 2.1,
      gain: 0.54,
      ridgeSharpness: 1.85,
      warpStrength: 26,
      valleyFloor: 0.8,
      butteDensity: 0.9,
      verticalFissures: 0.94,
      basalOverhang3D: 0.92,
      caprockCrown: 0.78,
      blendMode: 'replace',
    },
  },

  // Basic & Mountain Generators (matching Screenshot 3278)
  {
    type: 'SimplexNoise',
    title: 'Simplex Noise',
    subtitle: 'Continuous 2D/3D noise',
    category: 'generators',
    defaultParams: {
      seed: 1042,
      scale: 16,
      amplitude: 155,
      octaves: 6,
      lacunarity: 2.05,
      gain: 0.5,
      ridgeSharpness: 1.4,
      warpStrength: 22,
      valleyFloor: 0.25,
      blendMode: 'replace',
    },
  },
  {
    type: 'PerlinNoise',
    title: 'Perlin Noise',
    subtitle: 'Classic gradient noise',
    category: 'generators',
    defaultParams: {
      seed: 2081,
      scale: 18,
      amplitude: 150,
      octaves: 6,
      lacunarity: 2.0,
      gain: 0.5,
      ridgeSharpness: 1.2,
      warpStrength: 18,
      valleyFloor: 0.2,
      blendMode: 'replace',
    },
  },
  {
    type: 'ValueNoise',
    title: 'Value Noise',
    subtitle: 'Smooth interpolated noise',
    category: 'generators',
    defaultParams: {
      seed: 3190,
      scale: 15,
      amplitude: 140,
      octaves: 5,
      lacunarity: 2.0,
      gain: 0.48,
      ridgeSharpness: 1.0,
      warpStrength: 12,
      valleyFloor: 0.15,
      blendMode: 'replace',
    },
  },
  {
    type: 'MultiFractal',
    title: 'MultiFractal',
    subtitle: 'Complex ridged noise',
    category: 'generators',
    defaultParams: {
      seed: 4217,
      scale: 16,
      amplitude: 178,
      octaves: 6,
      lacunarity: 2.12,
      gain: 0.54,
      ridgeSharpness: 1.75,
      warpStrength: 28,
      valleyFloor: 0.42,
      blendMode: 'replace',
    },
  },
  {
    type: 'CellularVoronoi',
    title: 'Cellular (Voronoi)',
    subtitle: 'Distance-based cellular noise',
    category: 'generators',
    defaultParams: {
      seed: 5501,
      scale: 14,
      amplitude: 145,
      octaves: 4,
      lacunarity: 2.0,
      gain: 0.5,
      ridgeSharpness: 1.5,
      warpStrength: 16,
      valleyFloor: 0.2,
      blendMode: 'add',
    },
  },
  {
    type: 'WhiteNoise',
    title: 'White Noise',
    subtitle: 'Random static noise',
    category: 'generators',
    defaultParams: {
      seed: 8812,
      scale: 5,
      amplitude: 15,
      octaves: 1,
      lacunarity: 2.0,
      gain: 0.5,
      ridgeSharpness: 1.0,
      warpStrength: 0,
      valleyFloor: 0,
      blendMode: 'add',
    },
  },

  // Erosion & Cliffs
  {
    type: 'CliffStrata',
    title: 'Cliff & Strata',
    subtitle: 'Stratified cliffs & talus scree',
    category: 'erosion',
    defaultParams: {
      cliffSteepness: 0.82,
      strataFrequency: 11,
      strataStrength: 0.76,
      talusReposeAngle: 34,
      talusRate: 0.72,
      undercut3D: 0.68,
      faultOffset: 12,
      faultAngle: 32,
      faultDip: 0.28,
    },
  },
  {
    type: 'HydraulicRivers',
    title: 'Hydraulic & Rivers',
    subtitle: 'Basin rivers & gorge incision',
    category: 'erosion',
    defaultParams: {
      rainIntensity: 1.15,
      riverCarveDepth: 26,
      channelThreshold: 0.055,
      channelWidth: 16,
      dropletIterations: 1,
      riverBankUndercut3D: 0.7,
    },
  },
  {
    type: 'AlluvialSediment',
    title: 'Alluvial Sediment',
    subtitle: 'Canyon alluvial fans & plains',
    category: 'erosion',
    defaultParams: {
      depositionStrength: 1.25,
      criticalSlope: 0.26,
      fanSpread: 0.78,
      floodplainSmoothing: 0.6,
    },
  },
  {
    type: 'WindErosion',
    title: 'Wind (Aeolian)',
    subtitle: 'Yardang abrasion & sand dunes',
    category: 'erosion',
    defaultParams: {
      windAngleDeg: 38,
      abrasionStrength: 0.55,
      duneRippleScale: 24,
      duneStrength: 0.62,
    },
  },

  // SDF & Caves
  {
    type: 'SDFKarstCaves',
    title: 'SDF Karst Caves',
    subtitle: '3D worm tunnels & caverns',
    category: 'sdf_caves',
    defaultParams: {
      seed: 6073,
      caveScale: 105,
      tunnelRadius: 13.5,
      cavernStrength: 0.65,
      elevationMin: 0.08,
      elevationMax: 0.72,
      overhangUndercut: 0.72,
    },
  },

  // Texturing (Gaea SatMaps)
  {
    type: 'SatMapTexture',
    title: 'SatMap (Gaea)',
    subtitle: 'Satellite gradient texturing',
    category: 'texturing',
    defaultParams: {
      presetId: 'monument_valley',
      driverMode: 'composite_gaea',
      strataContrast: 0.85,
      riverHighlight: 1.0,
      alluvialTintStrength: 0.88,
      jitter: 0.32,
      reverse: false,
    },
  },
];

export interface EvaluationResult {
  volume: SDFTerrainVolume;
  mesh: ExtractedSDFMesh;
  evaluatedOrder: string[];
  buildTimeMs: number;
}

/**
 * Evaluates the connected node chain starting from 'Start' node (or up to previewNodeId if soloed).
 */
export function evaluateTerrainGraph(
  nodes: GraphNodeData[],
  edges: GraphEdge[],
  previewNodeId?: string | null
): EvaluationResult {
  const t0 = performance.now();

  const startNode = nodes.find((n) => n.type === 'Start') || nodes[0];
  const domain = {
    worldSize: Number(startNode?.params?.worldSize ?? 512),
    maxHeight: Number(startNode?.params?.maxHeight ?? 210),
    sdfResolution: Number(startNode?.params?.sdfResolution ?? 256),
    strictLowResBlock: Boolean(startNode?.params?.strictLowResBlock ?? true),
    blockSkirt: Boolean(startNode?.params?.blockSkirt ?? true),
  };

  const vol = createEmptySDFVolume(domain);

  // Follow directed chain from Start node
  const nodeMap = new Map<string, GraphNodeData>();
  nodes.forEach((n) => nodeMap.set(n.id, n));

  const visited = new Set<string>();
  const chain: GraphNodeData[] = [];

  let currentId: string | undefined = startNode?.id;
  while (currentId && !visited.has(currentId)) {
    visited.add(currentId);
    const node = nodeMap.get(currentId);
    if (node) {
      chain.push(node);
      if (previewNodeId && node.id === previewNodeId) {
        break;
      }
    }
    const nextEdge = edges.find((e) => e.source === currentId);
    currentId = nextEdge?.target;
  }

  let hasAppliedSatMap = false;
  const evaluatedOrder: string[] = [];

  for (const node of chain) {
    evaluatedOrder.push(node.id);
    if (node.bypassed || node.type === 'Start') continue;

    switch (node.type) {
      case 'DesertPediment':
        applyDesertPedimentNode(vol, node.id, node.params as any);
        break;
      case 'SDFMonolithTowers':
        applySDFMonolithTowersNode(vol, node.id, node.params as any);
        break;
      case 'VerticalJointFissures':
        applyVerticalJointFissuresNode(vol, node.id, node.params as any);
        break;
      case 'BasalWindOverhangs':
        applyBasalWindOverhangsNode(vol, node.id, node.params as any);
        break;
      case 'ConicalTalusSkirt':
        applyConicalTalusSkirtNode(vol, node.id, node.params as any);
        break;
      case 'MonumentButtes':
        applyGeneratorNode(vol, node.id, 'monument_buttes', node.params as any);
        break;
      case 'MesaPlateauStamp':
        applyGeneratorNode(vol, node.id, 'mesa_plateau', node.params as any);
        break;
      case 'NeedleSpiresStamp':
        applyGeneratorNode(vol, node.id, 'needle_spires', node.params as any);
        break;
      case 'SimplexNoise':
        applyGeneratorNode(vol, node.id, 'simplex', node.params as any);
        break;
      case 'PerlinNoise':
        applyGeneratorNode(vol, node.id, 'perlin', node.params as any);
        break;
      case 'ValueNoise':
        applyGeneratorNode(vol, node.id, 'value', node.params as any);
        break;
      case 'MultiFractal':
        applyGeneratorNode(vol, node.id, 'multifractal', node.params as any);
        break;
      case 'CellularVoronoi':
        applyGeneratorNode(vol, node.id, 'cellular', node.params as any);
        break;
      case 'WhiteNoise':
        applyGeneratorNode(vol, node.id, 'white', node.params as any);
        break;
      case 'CliffStrata':
        applyCliffAndStrataErosion(vol, node.id, node.params as any);
        break;
      case 'HydraulicRivers':
        applyHydraulicAndRiverErosion(vol, node.id, node.params as any);
        break;
      case 'AlluvialSediment':
        applyAlluvialSedimentation(vol, node.id, node.params as any);
        break;
      case 'WindErosion':
        applyWindErosion(vol, node.id, node.params as any);
        break;
      case 'SDFKarstCaves':
        applySDFCavesAndOverhangs(vol, node.id, node.params as any);
        break;
      case 'SatMapTexture':
        applySatMapTexturing(vol, node.id, node.params as any);
        hasAppliedSatMap = true;
        break;
      default:
        break;
    }
  }

  // If the user solo-previewed an upstream geometry node before the SatMap node,
  // still apply subtle geological shading so rivers, cliffs, talus & sediment are clearly readable!
  if (!hasAppliedSatMap && chain.length > 1) {
    applySatMapTexturing(vol, '__auto_preview__', {
      presetId: vol.has3DMonoliths ? 'monument_valley' : 'utah_badlands',
      driverMode: 'composite_gaea',
      strataContrast: 0.85,
      riverHighlight: 1.0,
      alluvialTintStrength: 0.85,
      jitter: 0.25,
      reverse: false,
    });
  }

  // Ensure any river gorges or 3D monoliths expose 3D volumetric strata ledges
  if (vol.strataConfig.enabled) {
    applyVolumetric3DStrataToSDF(vol);
  }

  const mesh = extractSDFMesh(vol);
  const buildTimeMs = Math.round(performance.now() - t0);

  return {
    volume: vol,
    mesh,
    evaluatedOrder,
    buildTimeMs,
  };
}

export type GraphPresetId = 'monument_valley' | 'colorado_rivers';

export function getInitialGraph(
  preset: GraphPresetId = 'monument_valley'
): { nodes: GraphNodeData[]; edges: GraphEdge[] } {
  if (preset === 'colorado_rivers') {
    const nodes: GraphNodeData[] = [
      {
        id: 'node-start',
        type: 'Start',
        title: 'Start',
        subtitle: 'Entry Point',
        category: 'entry',
        x: 40,
        y: 135,
        params: {
          worldSize: 512,
          maxHeight: 210,
          sdfResolution: 256,
          strictLowResBlock: true,
          blockSkirt: true,
        },
      },
      {
        id: 'node-multifractal',
        type: 'MultiFractal',
        title: 'MultiFractal',
        subtitle: 'Complex ridged noise',
        category: 'generators',
        x: 295,
        y: 135,
        params: {
          seed: 4217,
          scale: 16,
          amplitude: 178,
          octaves: 6,
          lacunarity: 2.12,
          gain: 0.54,
          ridgeSharpness: 1.75,
          warpStrength: 28,
          valleyFloor: 0.42,
          blendMode: 'replace',
        },
      },
      {
        id: 'node-cliffs',
        type: 'CliffStrata',
        title: 'Cliff & Strata',
        subtitle: 'Stratified cliffs & talus scree',
        category: 'erosion',
        x: 40,
        y: 290,
        params: {
          cliffSteepness: 0.78,
          strataFrequency: 11,
          strataStrength: 0.72,
          talusReposeAngle: 34,
          talusRate: 0.72,
          undercut3D: 0.65,
          faultOffset: 14,
          faultAngle: 32,
          faultDip: 0.28,
        },
      },
      {
        id: 'node-rivers',
        type: 'HydraulicRivers',
        title: 'Hydraulic & Rivers',
        subtitle: 'Basin rivers & gorge incision',
        category: 'erosion',
        x: 295,
        y: 290,
        params: {
          rainIntensity: 1.15,
          riverCarveDepth: 26,
          channelThreshold: 0.055,
          channelWidth: 16,
          dropletIterations: 1,
          riverBankUndercut3D: 0.7,
        },
      },
      {
        id: 'node-alluvial',
        type: 'AlluvialSediment',
        title: 'Alluvial Sediment',
        subtitle: 'Canyon alluvial fans & plains',
        category: 'erosion',
        x: 40,
        y: 445,
        params: {
          depositionStrength: 1.25,
          criticalSlope: 0.26,
          fanSpread: 0.78,
          floodplainSmoothing: 0.6,
        },
      },
      {
        id: 'node-wind',
        type: 'WindErosion',
        title: 'Wind (Aeolian)',
        subtitle: 'Yardang abrasion & sand dunes',
        category: 'erosion',
        x: 295,
        y: 445,
        params: {
          windAngleDeg: 38,
          abrasionStrength: 0.55,
          duneRippleScale: 24,
          duneStrength: 0.62,
        },
      },
      {
        id: 'node-caves',
        type: 'SDFKarstCaves',
        title: 'SDF Karst Caves',
        subtitle: '3D worm tunnels & caverns',
        category: 'sdf_caves',
        x: 40,
        y: 600,
        params: {
          seed: 6073,
          caveScale: 105,
          tunnelRadius: 13.5,
          cavernStrength: 0.65,
          elevationMin: 0.08,
          elevationMax: 0.72,
          overhangUndercut: 0.72,
        },
      },
      {
        id: 'node-satmap',
        type: 'SatMapTexture',
        title: 'SatMap (Gaea)',
        subtitle: 'Satellite gradient texturing',
        category: 'texturing',
        x: 295,
        y: 600,
        params: {
          presetId: 'utah_badlands',
          driverMode: 'composite_gaea',
          strataContrast: 0.78,
          riverHighlight: 1.0,
          alluvialTintStrength: 0.88,
          jitter: 0.35,
          reverse: false,
        },
      },
    ];

    const edges: GraphEdge[] = [
      { id: 'e-1', source: 'node-start', target: 'node-multifractal' },
      { id: 'e-2', source: 'node-multifractal', target: 'node-cliffs' },
      { id: 'e-3', source: 'node-cliffs', target: 'node-rivers' },
      { id: 'e-4', source: 'node-rivers', target: 'node-alluvial' },
      { id: 'e-5', source: 'node-alluvial', target: 'node-wind' },
      { id: 'e-6', source: 'node-wind', target: 'node-caves' },
      { id: 'e-7', source: 'node-caves', target: 'node-satmap' },
    ];

    return { nodes, edges };
  }

  // Dedicated 8-node Monument Valley 3D CSG SDF Monolith Pipeline
  const nodes: GraphNodeData[] = [
    {
      id: 'node-start',
      type: 'Start',
      title: 'Start',
      subtitle: 'Entry Point',
      category: 'entry',
      x: 40,
      y: 135,
      params: {
        worldSize: 512,
        maxHeight: 210,
        sdfResolution: 256,
        strictLowResBlock: true,
        blockSkirt: true,
      },
    },
    {
      id: 'node-pediment',
      type: 'DesertPediment',
      title: 'Desert Pediment',
      subtitle: 'Flat desert basin & dry washes',
      category: 'generators',
      x: 295,
      y: 135,
      params: {
        seed: 4217,
        scale: 18,
        baseElevation: 16,
        undulationHeight: 9,
        duneRippleStrength: 0.55,
        arroyoWashDepth: 3.5,
      },
    },
    {
      id: 'node-monoliths',
      type: 'SDFMonolithTowers',
      title: 'SDF Monolith Towers',
      subtitle: 'True 3D CSG sandstone monoliths',
      category: 'generators',
      x: 40,
      y: 290,
      params: {
        seed: 4217,
        monolithPreset: 'monument_buttes',
        towerHeight: 174,
        footprintScale: 1.0,
        wallVerticality: 0.92,
        caprockCrown: 0.85,
        steppedBenchRatio: 0.72,
      },
    },
    {
      id: 'node-fissures',
      type: 'VerticalJointFissures',
      title: 'Vertical Joint Fissures',
      subtitle: '3D tectonic chimneys & notches',
      category: 'sdf_caves',
      x: 295,
      y: 290,
      params: {
        seed: 4217,
        fissureIntensity: 0.86,
        fissureSpacing: 22,
        chimneyDepth: 11.5,
        beddingNotchStrength: 0.82,
        columnFluting: 0.78,
      },
    },
    {
      id: 'node-overhangs',
      type: 'BasalWindOverhangs',
      title: 'Basal Wind Overhangs',
      subtitle: '3D aeolian undercut alcoves',
      category: 'sdf_caves',
      x: 40,
      y: 445,
      params: {
        seed: 4217,
        undercutDepth: 14.5,
        alcoveHeight: 38,
        browOverhang: 8.5,
        directionalBias: 0.68,
        windAngleDeg: 35,
      },
    },
    {
      id: 'node-talus',
      type: 'ConicalTalusSkirt',
      title: 'Conical Talus Skirt',
      subtitle: '34° boulder & scree aprons',
      category: 'erosion',
      x: 295,
      y: 445,
      params: {
        seed: 4217,
        skirtHeight: 54,
        reposeAngleDeg: 34,
        gullyChuteStrength: 0.78,
        boulderRoughness: 0.65,
      },
    },
    {
      id: 'node-cliffs',
      type: 'CliffStrata',
      title: 'Cliff & Strata',
      subtitle: '16-bed non-uniform 3D strata',
      category: 'erosion',
      x: 40,
      y: 600,
      params: {
        cliffSteepness: 0.86,
        strataFrequency: 11,
        strataStrength: 0.62,
        talusReposeAngle: 34,
        talusRate: 0.65,
        undercut3D: 0.58,
        faultOffset: 10,
        faultAngle: 32,
        faultDip: 0.28,
      },
    },
    {
      id: 'node-satmap',
      type: 'SatMapTexture',
      title: 'SatMap (Gaea)',
      subtitle: 'Satellite gradient texturing',
      category: 'texturing',
      x: 295,
      y: 600,
      params: {
        presetId: 'monument_valley',
        driverMode: 'composite_gaea',
        strataContrast: 0.86,
        riverHighlight: 1.0,
        alluvialTintStrength: 0.85,
        jitter: 0.3,
        reverse: false,
      },
    },
  ];

  const edges: GraphEdge[] = [
    { id: 'e-1', source: 'node-start', target: 'node-pediment' },
    { id: 'e-2', source: 'node-pediment', target: 'node-monoliths' },
    { id: 'e-3', source: 'node-monoliths', target: 'node-fissures' },
    { id: 'e-4', source: 'node-fissures', target: 'node-overhangs' },
    { id: 'e-5', source: 'node-overhangs', target: 'node-talus' },
    { id: 'e-6', source: 'node-talus', target: 'node-cliffs' },
    { id: 'e-7', source: 'node-cliffs', target: 'node-satmap' },
  ];

  return { nodes, edges };
}
