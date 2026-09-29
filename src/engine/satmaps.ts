import { SeededNoise } from './noise';
import { evaluateResolutionForOperation } from './resolutionGuard';
import { SDFTerrainVolume, getTotalSurfaceHeight } from './terrainState';

export interface ColorStop {
  pos: number; // [0..1]
  rgb: [number, number, number]; // [0..1]
}

export interface SatMapPreset {
  id: string;
  name: string;
  region: string;
  bedrockRamp: ColorStop[];
  cliffStrataRamp: ColorStop[];
  talusColor: [number, number, number];
  alluvialColor: [number, number, number];
  riverWaterColor: [number, number, number];
  riverBankColor: [number, number, number];
}

export const SATMAP_PRESETS: SatMapPreset[] = [
  {
    id: 'monument_valley',
    name: 'Monument Valley — De Chelly Monoliths',
    region: 'Arizona / Utah Border, USA',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.29, 0.22, 0.15] },  // Dark dusty desert scrub & playa floor
      { pos: 0.14, rgb: [0.45, 0.33, 0.22] }, // Sunlit sandy desert pediment
      { pos: 0.32, rgb: [0.58, 0.27, 0.17] }, // Lower Organ Rock shale & talus skirt
      { pos: 0.64, rgb: [0.69, 0.33, 0.20] }, // Vertical De Chelly sandstone monolith wall
      { pos: 0.88, rgb: [0.76, 0.41, 0.25] }, // Sun-baked upper sandstone face
      { pos: 1.0, rgb: [0.42, 0.23, 0.16] },  // Manganese desert-varnish caprock crown
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.24, 0.12, 0.08] },  // Deep iron-oxide crevice & desert varnish
      { pos: 0.28, rgb: [0.64, 0.28, 0.17] }, // Rich burnt-sienna monolith sandstone
      { pos: 0.58, rgb: [0.48, 0.21, 0.13] }, // Recessed mudstone bedding band
      { pos: 0.82, rgb: [0.75, 0.38, 0.23] }, // Glowing terracotta cliff face
      { pos: 1.0, rgb: [0.35, 0.18, 0.13] },  // Dark weathered caprock rim
    ],
    talusColor: [0.52, 0.26, 0.17],
    alluvialColor: [0.44, 0.32, 0.21],
    riverWaterColor: [0.10, 0.33, 0.42],
    riverBankColor: [0.27, 0.19, 0.14],
  },
  {
    id: 'utah_badlands',
    name: 'Utah Badlands — Navajo Sandstone',
    region: 'Colorado Plateau, USA',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.28, 0.19, 0.14] },
      { pos: 0.24, rgb: [0.46, 0.26, 0.17] },
      { pos: 0.52, rgb: [0.62, 0.35, 0.22] },
      { pos: 0.78, rgb: [0.72, 0.46, 0.30] },
      { pos: 1.0, rgb: [0.79, 0.58, 0.42] },
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.29, 0.14, 0.09] },
      { pos: 0.32, rgb: [0.58, 0.27, 0.15] },
      { pos: 0.65, rgb: [0.44, 0.20, 0.12] },
      { pos: 1.0, rgb: [0.74, 0.43, 0.26] },
    ],
    talusColor: [0.49, 0.28, 0.18],
    alluvialColor: [0.56, 0.37, 0.25],
    riverWaterColor: [0.08, 0.32, 0.44],
    riverBankColor: [0.26, 0.18, 0.13],
  },
  {
    id: 'wadi_rum',
    name: 'Wadi Rum — Jebel Towers & Crimson Erg',
    region: 'Aqaba Governorate, Jordan',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.58, 0.31, 0.20] },
      { pos: 0.22, rgb: [0.66, 0.36, 0.23] },
      { pos: 0.50, rgb: [0.52, 0.24, 0.16] },
      { pos: 0.78, rgb: [0.41, 0.21, 0.15] },
      { pos: 1.0, rgb: [0.29, 0.17, 0.13] },
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.21, 0.11, 0.09] },
      { pos: 0.38, rgb: [0.57, 0.26, 0.17] },
      { pos: 0.72, rgb: [0.43, 0.19, 0.13] },
      { pos: 1.0, rgb: [0.68, 0.37, 0.23] },
    ],
    talusColor: [0.59, 0.29, 0.18],
    alluvialColor: [0.64, 0.35, 0.22],
    riverWaterColor: [0.09, 0.35, 0.43],
    riverBankColor: [0.36, 0.20, 0.14],
  },
  {
    id: 'canyonlands_wingate',
    name: 'Canyonlands — Wingate & Chinle Formation',
    region: 'Moab / Island in the Sky, Utah',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.34, 0.25, 0.24] },  // Purple-grey Chinle mudstone base
      { pos: 0.25, rgb: [0.49, 0.31, 0.24] }, // Moenkopi slope
      { pos: 0.55, rgb: [0.67, 0.34, 0.20] }, // Sheer Wingate sandstone cliff
      { pos: 0.82, rgb: [0.74, 0.45, 0.28] }, // Kayenta ledge
      { pos: 1.0, rgb: [0.81, 0.64, 0.46] },  // White Rim caprock
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.31, 0.16, 0.13] },
      { pos: 0.35, rgb: [0.66, 0.31, 0.18] },
      { pos: 0.68, rgb: [0.51, 0.25, 0.19] },
      { pos: 1.0, rgb: [0.78, 0.54, 0.36] },
    ],
    talusColor: [0.50, 0.30, 0.23],
    alluvialColor: [0.46, 0.33, 0.26],
    riverWaterColor: [0.12, 0.36, 0.39],
    riverBankColor: [0.32, 0.22, 0.18],
  },
  {
    id: 'zhangjiajie_pillars',
    name: 'Zhangjiajie — Quartzite Spire Forest',
    region: 'Wulingyuan, Hunan',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.16, 0.25, 0.16] },  // Subtropical mossy gorge floor
      { pos: 0.26, rgb: [0.34, 0.36, 0.31] }, // Lower talus & foliage
      { pos: 0.56, rgb: [0.54, 0.49, 0.44] }, // Devonian quartz-sandstone pillar wall
      { pos: 0.84, rgb: [0.66, 0.60, 0.53] }, // Upper weathered spire face
      { pos: 1.0, rgb: [0.22, 0.35, 0.20] },  // Pine-crowned summit flat
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.26, 0.25, 0.23] },
      { pos: 0.45, rgb: [0.55, 0.49, 0.43] },
      { pos: 0.78, rgb: [0.43, 0.39, 0.35] },
      { pos: 1.0, rgb: [0.68, 0.62, 0.55] },
    ],
    talusColor: [0.35, 0.37, 0.31],
    alluvialColor: [0.24, 0.32, 0.21],
    riverWaterColor: [0.07, 0.44, 0.41],
    riverBankColor: [0.25, 0.27, 0.23],
  },
  {
    id: 'painted_desert',
    name: 'Painted Desert — Chinle & Bentonite Clay',
    region: 'Petrified Forest, Arizona',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.38, 0.32, 0.34] },  // Ash-violet bentonite floor
      { pos: 0.25, rgb: [0.58, 0.29, 0.27] }, // Maroon mudstone band
      { pos: 0.52, rgb: [0.71, 0.43, 0.32] }, // Terracotta siltstone
      { pos: 0.76, rgb: [0.56, 0.46, 0.48] }, // Lavender-grey marl
      { pos: 1.0, rgb: [0.78, 0.64, 0.52] },  // Buff sandstone cap
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.36, 0.20, 0.21] },
      { pos: 0.33, rgb: [0.66, 0.33, 0.26] },
      { pos: 0.66, rgb: [0.52, 0.41, 0.44] },
      { pos: 1.0, rgb: [0.77, 0.58, 0.45] },
    ],
    talusColor: [0.54, 0.35, 0.32],
    alluvialColor: [0.49, 0.39, 0.38],
    riverWaterColor: [0.13, 0.38, 0.45],
    riverBankColor: [0.35, 0.26, 0.26],
  },
  {
    id: 'grand_canyon',
    name: 'Grand Canyon — Kaibab to Redwall',
    region: 'Arizona, USA',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.25, 0.19, 0.17] },
      { pos: 0.22, rgb: [0.48, 0.21, 0.15] },
      { pos: 0.48, rgb: [0.60, 0.31, 0.20] },
      { pos: 0.76, rgb: [0.69, 0.47, 0.32] },
      { pos: 1.0, rgb: [0.76, 0.63, 0.48] },
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.37, 0.15, 0.10] },
      { pos: 0.33, rgb: [0.62, 0.31, 0.19] },
      { pos: 0.66, rgb: [0.51, 0.22, 0.14] },
      { pos: 1.0, rgb: [0.74, 0.55, 0.38] },
    ],
    talusColor: [0.51, 0.28, 0.18],
    alluvialColor: [0.57, 0.39, 0.26],
    riverWaterColor: [0.10, 0.33, 0.38],
    riverBankColor: [0.31, 0.22, 0.17],
  },
  {
    id: 'sahara_hamada',
    name: 'Sahara Erg — Tassili Sandstone & Dunes',
    region: 'Tassili n’Ajjer, Algeria',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.64, 0.48, 0.32] },
      { pos: 0.35, rgb: [0.55, 0.38, 0.25] },
      { pos: 0.68, rgb: [0.38, 0.27, 0.20] },
      { pos: 1.0, rgb: [0.25, 0.19, 0.15] },
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.22, 0.16, 0.13] },
      { pos: 0.5, rgb: [0.40, 0.28, 0.20] },
      { pos: 1.0, rgb: [0.57, 0.41, 0.29] },
    ],
    talusColor: [0.58, 0.42, 0.28],
    alluvialColor: [0.68, 0.52, 0.35],
    riverWaterColor: [0.12, 0.36, 0.44],
    riverBankColor: [0.44, 0.33, 0.24],
  },
  {
    id: 'mars_valles',
    name: 'Mars — Valles Marineris & Hematite',
    region: 'Coprates Chasma, Mars',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.24, 0.18, 0.16] },  // Dark basaltic chasma floor
      { pos: 0.28, rgb: [0.45, 0.24, 0.16] }, // Iron-oxide dust apron
      { pos: 0.58, rgb: [0.62, 0.31, 0.19] }, // Layered sulfate & hematite wall
      { pos: 0.85, rgb: [0.71, 0.40, 0.24] }, // Upper chasma rim
      { pos: 1.0, rgb: [0.52, 0.31, 0.22] },  // Cratered plateau regolith
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.21, 0.13, 0.11] },
      { pos: 0.4, rgb: [0.59, 0.28, 0.17] },
      { pos: 0.75, rgb: [0.44, 0.22, 0.15] },
      { pos: 1.0, rgb: [0.70, 0.39, 0.24] },
    ],
    talusColor: [0.43, 0.23, 0.16],
    alluvialColor: [0.36, 0.22, 0.17],
    riverWaterColor: [0.28, 0.19, 0.15],
    riverBankColor: [0.22, 0.15, 0.13],
  },
  {
    id: 'alpine_granite',
    name: 'Alpine Granite & Glacial Valley',
    region: 'Swiss Alps / Dolomites',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.16, 0.22, 0.15] },
      { pos: 0.26, rgb: [0.26, 0.31, 0.23] },
      { pos: 0.54, rgb: [0.39, 0.40, 0.38] },
      { pos: 0.78, rgb: [0.54, 0.56, 0.58] },
      { pos: 1.0, rgb: [0.82, 0.85, 0.88] },
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.20, 0.21, 0.23] },
      { pos: 0.5, rgb: [0.34, 0.35, 0.37] },
      { pos: 1.0, rgb: [0.49, 0.50, 0.52] },
    ],
    talusColor: [0.38, 0.39, 0.37],
    alluvialColor: [0.29, 0.35, 0.25],
    riverWaterColor: [0.07, 0.36, 0.52],
    riverBankColor: [0.21, 0.24, 0.23],
  },
  {
    id: 'icelandic_basalt',
    name: 'Icelandic Basalt & Braided Delta',
    region: 'Landmannalaugar, Iceland',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.13, 0.14, 0.16] },
      { pos: 0.32, rgb: [0.24, 0.30, 0.18] },
      { pos: 0.62, rgb: [0.37, 0.42, 0.21] },
      { pos: 0.85, rgb: [0.48, 0.39, 0.29] },
      { pos: 1.0, rgb: [0.74, 0.76, 0.79] },
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.11, 0.12, 0.14] },
      { pos: 0.5, rgb: [0.20, 0.21, 0.24] },
      { pos: 1.0, rgb: [0.30, 0.29, 0.31] },
    ],
    talusColor: [0.18, 0.19, 0.21],
    alluvialColor: [0.22, 0.24, 0.21],
    riverWaterColor: [0.09, 0.39, 0.54],
    riverBankColor: [0.14, 0.15, 0.17],
  },
  {
    id: 'karst_gorge',
    name: 'Karst Limestone & Emerald River',
    region: 'Guilin / Dinaric Karst',
    bedrockRamp: [
      { pos: 0.0, rgb: [0.15, 0.26, 0.16] },
      { pos: 0.35, rgb: [0.22, 0.36, 0.20] },
      { pos: 0.65, rgb: [0.45, 0.47, 0.43] },
      { pos: 0.88, rgb: [0.61, 0.63, 0.60] },
      { pos: 1.0, rgb: [0.73, 0.74, 0.71] },
    ],
    cliffStrataRamp: [
      { pos: 0.0, rgb: [0.31, 0.33, 0.30] },
      { pos: 0.5, rgb: [0.52, 0.53, 0.49] },
      { pos: 1.0, rgb: [0.66, 0.65, 0.61] },
    ],
    talusColor: [0.42, 0.43, 0.39],
    alluvialColor: [0.31, 0.38, 0.26],
    riverWaterColor: [0.06, 0.41, 0.38],
    riverBankColor: [0.32, 0.34, 0.30],
  },
];

export function sampleRamp(ramp: ColorStop[], t: number): [number, number, number] {
  const clamped = Math.max(0, Math.min(1, t));
  for (let i = 0; i < ramp.length - 1; i++) {
    const a = ramp[i];
    const b = ramp[i + 1];
    if (clamped >= a.pos && clamped <= b.pos) {
      const localT = (clamped - a.pos) / Math.max(1e-5, b.pos - a.pos);
      return [
        a.rgb[0] + (b.rgb[0] - a.rgb[0]) * localT,
        a.rgb[1] + (b.rgb[1] - a.rgb[1]) * localT,
        a.rgb[2] + (b.rgb[2] - a.rgb[2]) * localT,
      ];
    }
  }
  return ramp[ramp.length - 1].rgb;
}

export interface SatMapNodeParams {
  presetId: string;
  driverMode: 'composite_gaea' | 'elevation' | 'slope_cliff' | 'river_alluvial';
  strataContrast: number;      // [0..1]
  riverHighlight: number;      // [0..1]
  alluvialTintStrength: number;// [0..1]
  jitter: number;              // Natural satellite grain jitter [0..1]
  reverse: boolean;
}

export function applySatMapTexturing(
  vol: SDFTerrainVolume,
  nodeId: string,
  params: SatMapNodeParams
): void {
  const { nx, nz, domain, colorMap, curvatureGrid } = vol;
  const report = evaluateResolutionForOperation(domain, nodeId, 'satmap', 1.0);
  vol.reports[nodeId] = report;

  const preset =
    SATMAP_PRESETS.find((p) => p.id === params.presetId) || SATMAP_PRESETS[0];
  vol.activeSatMapId = preset.id;
  const noise = new SeededNoise(5521);

  for (let z = 0; z < nz; z++) {
    for (let x = 0; x < nx; x++) {
      const idx = z * nx + x;
      const baseSurfH = getTotalSurfaceHeight(vol, idx);
      const h =
        vol.has3DMonoliths && vol.monolithDist[idx] < 1.5
          ? Math.max(baseSurfH, vol.monolithSummitH[idx])
          : baseSurfH;
      let normH = Math.max(0, Math.min(1, (h - 10.0) / (domain.maxHeight * 0.72)));
      if (params.reverse) normH = 1.0 - normH;

      const jitterVal =
        params.jitter > 0
          ? noise.simplex2D(x * 0.35, z * 0.35) * 0.038 * params.jitter
          : 0;

      // 1. Base Bedrock by Elevation + Curvature Ridge Highlight
      const curv = curvatureGrid[idx]; // [-1..1]
      const lookupT = Math.max(0, Math.min(1, normH + curv * 0.06 + jitterVal));
      let [r, g, b] = sampleRamp(preset.bedrockRamp, lookupT);

      // 2. Parent-Rock Scree / Thermal Talus Aprons at Cliff Bases:
      const talusH = vol.talusHeight[idx];
      const talusW = Math.min(0.88, (talusH / 2.4) * 0.88);
      if (talusW > 0.02) {
        const parentRockRGB = sampleRamp(
          preset.cliffStrataRamp,
          Math.max(0.15, Math.min(0.85, normH * 0.8 + 0.15 + jitterVal))
        );
        const gravelGrain = noise.simplex2D(x * 0.85 + 19.3, z * 0.85 - 41.7) * 0.045;
        const cliffFootAO = 0.88 + 0.12 * Math.min(1.0, talusH / 3.5);
        const screeR = (preset.talusColor[0] * 0.55 + parentRockRGB[0] * 0.45 + gravelGrain) * cliffFootAO;
        const screeG = (preset.talusColor[1] * 0.55 + parentRockRGB[1] * 0.45 + gravelGrain * 0.85) * cliffFootAO;
        const screeB = (preset.talusColor[2] * 0.55 + parentRockRGB[2] * 0.45 + gravelGrain * 0.7) * cliffFootAO;

        r = r * (1.0 - talusW) + screeR * talusW;
        g = g * (1.0 - talusW) + screeG * talusW;
        b = b * (1.0 - talusW) + screeB * talusW;
      }

      // 3. Alluvial Fans, Floodplain Pediment & Aeolian Dunes:
      const sedH = vol.sedimentHeight[idx];
      const sedW = Math.min(
        0.84,
        ((sedH / 3.2) + vol.windMask[idx] * 0.32) *
          (params.alluvialTintStrength ?? 0.82)
      );
      if (sedW > 0.02) {
        const washGrain = noise.simplex2D(x * 0.22 - 71.1, z * 0.22 + 33.9) * 0.035;
        const alluvR = preset.alluvialColor[0] + washGrain;
        const alluvG = preset.alluvialColor[1] + washGrain * 0.85;
        const alluvB = preset.alluvialColor[2] + washGrain * 0.7;

        r = r * (1.0 - sedW) + alluvR * sedW;
        g = g * (1.0 - sedW) + alluvG * sedW;
        b = b * (1.0 - sedW) + alluvB * sedW;
      }

      // 4. Gully & Ravine Ambient Occlusion
      const looseFactor = Math.min(1.0, (talusH + sedH) / 1.8);
      if (curv < -0.04) {
        const gullyShade = Math.max(0.76, 1.0 + curv * (0.24 - looseFactor * 0.14));
        r *= gullyShade;
        g *= gullyShade;
        b *= gullyShade;
      } else if (curv > 0.06) {
        const ridgeBoost = Math.min(1.12, 1.0 + curv * (0.12 - looseFactor * 0.08));
        r *= ridgeBoost;
        g *= ridgeBoost;
        b *= ridgeBoost;
      }

      // 5. Focused Dendritic River Channels & Wet Riverbanks
      const flow = vol.riverFlow[idx] * (params.riverHighlight ?? 1.0);
      if (flow > 0.04) {
        const bankW = Math.min(1.0, flow * 1.8);
        r = r * (1.0 - bankW * 0.72) + preset.riverBankColor[0] * (bankW * 0.72);
        g = g * (1.0 - bankW * 0.72) + preset.riverBankColor[1] * (bankW * 0.72);
        b = b * (1.0 - bankW * 0.72) + preset.riverBankColor[2] * (bankW * 0.72);

        if (flow > 0.18) {
          const waterW = Math.min(0.95, Math.pow((flow - 0.18) / 0.82, 0.5) * 0.95);
          r = r * (1.0 - waterW) + preset.riverWaterColor[0] * waterW;
          g = g * (1.0 - waterW) + preset.riverWaterColor[1] * waterW;
          b = b * (1.0 - waterW) + preset.riverWaterColor[2] * waterW;
        }
      }

      colorMap[idx * 3 + 0] = Math.pow(Math.max(0.01, Math.min(1, r)), 1.48);
      colorMap[idx * 3 + 1] = Math.pow(Math.max(0.01, Math.min(1, g)), 1.48);
      colorMap[idx * 3 + 2] = Math.pow(Math.max(0.01, Math.min(1, b)), 1.48);
    }
  }
}

export function getSatMapCssGradient(preset: SatMapPreset): string {
  const stops = preset.bedrockRamp
    .map((s) => {
      const r = Math.round(Math.min(1, s.rgb[0] * 1.15) * 255);
      const g = Math.round(Math.min(1, s.rgb[1] * 1.15) * 255);
      const b = Math.round(Math.min(1, s.rgb[2] * 1.15) * 255);
      return `rgb(${r}, ${g}, ${b}) ${Math.round(s.pos * 100)}%`;
    })
    .join(', ');
  return `linear-gradient(90deg, ${stops})`;
}
