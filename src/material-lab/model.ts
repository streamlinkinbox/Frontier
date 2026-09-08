import {
  getBuiltinSatmap,
  type BuiltinSatmapId,
} from "../engine/satmaps/catalog";
export const LAB_VIEWS = [
  "lit",
  "clay",
  "albedo",
  "normals",
  "relief",
  "silhouette",
  "steps",
] as const;
export type LabView = (typeof LAB_VIEWS)[number];
export type LabQuality = "draft" | "balanced" | "closeup";
export interface StoneSettings {
  seed: number;
  form: number;
  facets: number;
  chips: number;
  bedding: number;
  spacing: number;
  tilt: number;
  crackDepth: number;
  crackWidth: number;
  crackSpacing: number;
  porosity: number;
  poreSize: number;
  grain: number;
  grainSize: number;
  detail: boolean;
  palette: BuiltinSatmapId;
  contrast: number;
  bias: number;
  saturation: number;
  roughness: number;
  exposure: number;
  sunAzimuth: number;
  sunElevation: number;
  view: LabView;
  quality: LabQuality;
  compare: boolean;
  split: number;
  turntable: boolean;
}
export const DEFAULT_STONE: StoneSettings = {
  seed: 214,
  form: 0.65,
  facets: 0.55,
  chips: 7,
  bedding: 0.65,
  spacing: 24,
  tilt: 14,
  crackDepth: 6,
  crackWidth: 1,
  crackSpacing: 110,
  porosity: 0.28,
  poreSize: 13,
  grain: 0.45,
  grainSize: 3,
  detail: true,
  palette: "quarry-sandstone",
  contrast: 1.3,
  bias: -0.05,
  saturation: 0.85,
  roughness: 0.79,
  exposure: 1.05,
  sunAzimuth: -42,
  sunElevation: 42,
  view: "lit",
  quality: "balanced",
  compare: false,
  split: 0.5,
  turntable: false,
};
export const STONE_PRESETS = [
  {
    id: "sandstone",
    name: "Desert sandstone",
    category: "SEDIMENTARY",
    description: "Broken beds, fine grit, dry fissures.",
    color: "#c7a079",
    values: { ...DEFAULT_STONE },
  },
  {
    id: "basalt",
    name: "Vesicular basalt",
    category: "VOLCANIC",
    description: "Rough fractured faces and open vesicles.",
    color: "#566263",
    values: {
      ...DEFAULT_STONE,
      palette: "volcanic-obsidian",
      facets: 0.8,
      chips: 8,
      bedding: 0,
      porosity: 0.9,
      poreSize: 20,
      grain: 0.32,
      grainSize: 2.6,
      crackDepth: 8,
      crackSpacing: 85,
      roughness: 0.86,
      bias: 0.12,
    },
  },
  {
    id: "granite",
    name: "Broken granite",
    category: "IGNEOUS",
    description: "Angular breakup and coarse geometric grain.",
    color: "#aab4b9",
    values: {
      ...DEFAULT_STONE,
      palette: "quarry-granite",
      facets: 0.75,
      chips: 5,
      bedding: 0,
      porosity: 0.08,
      poreSize: 10,
      grain: 0.85,
      grainSize: 4,
      crackDepth: 6,
      crackSpacing: 135,
      roughness: 0.7,
    },
  },
  {
    id: "slate",
    name: "Layered slate",
    category: "METAMORPHIC",
    description: "Tilted cleavage, thin ledges, sharp splits.",
    color: "#7a8496",
    values: {
      ...DEFAULT_STONE,
      palette: "quarry-slate",
      facets: 0.9,
      chips: 3,
      bedding: 3.5,
      spacing: 14,
      tilt: 36,
      porosity: 0.04,
      grain: 0.16,
      grainSize: 1.4,
      crackDepth: 7,
      crackWidth: 0.55,
      crackSpacing: 75,
      roughness: 0.7,
    },
  },
  {
    id: "limestone",
    name: "Weathered limestone",
    category: "SEDIMENTARY",
    description: "Soft broken relief and small solution pits.",
    color: "#d2d4bd",
    values: {
      ...DEFAULT_STONE,
      palette: "quarry-limestone",
      facets: 0.4,
      chips: 3.5,
      bedding: 0.9,
      spacing: 32,
      porosity: 0.65,
      poreSize: 16,
      grain: 0.3,
      grainSize: 2,
      crackDepth: 3,
      roughness: 0.84,
    },
  },
  {
    id: "riverstone",
    name: "River-worn stone",
    category: "WEATHERED",
    description: "A restrained, smoother comparison specimen.",
    color: "#8caaa3",
    values: {
      ...DEFAULT_STONE,
      palette: "beach-granite",
      facets: 0.15,
      form: 0.32,
      chips: 1.2,
      bedding: 0,
      porosity: 0.04,
      poreSize: 9,
      grain: 0.12,
      grainSize: 1,
      crackDepth: 0,
      roughness: 0.52,
    },
  },
] as const satisfies readonly {
  id: string;
  name: string;
  category: string;
  description: string;
  color: string;
  values: StoneSettings;
}[];
export const STONE_RANGES = {
  seed: [0, 99999],
  form: [0, 1],
  facets: [0, 1],
  chips: [0, 12],
  bedding: [0, 6],
  spacing: [8, 60],
  tilt: [-70, 70],
  crackDepth: [0, 12],
  crackWidth: [0.2, 2],
  crackSpacing: [45, 180],
  porosity: [0, 1],
  poreSize: [6, 30],
  grain: [0, 1.5],
  grainSize: [0.6, 6],
  contrast: [0.5, 2.5],
  bias: [-0.4, 0.4],
  saturation: [0, 1.5],
  roughness: [0.3, 1],
  exposure: [0.5, 1.8],
  sunAzimuth: [-180, 180],
  sunElevation: [12, 80],
  split: [0.05, 0.95],
} as const;
export function validateStoneRecipe(raw: unknown): StoneSettings {
  if (!raw || typeof raw !== "object" || Array.isArray(raw))
    throw new Error("This is not a stone material recipe.");
  const record = raw as Record<string, unknown>;
  if (
    record.version !== 1 ||
    record.kind !== "frontier-sdf-stone" ||
    !record.settings ||
    typeof record.settings !== "object"
  )
    throw new Error("Unsupported stone recipe format.");
  const input = record.settings as Record<string, unknown>,
    result = { ...DEFAULT_STONE };
  for (const [key, range] of Object.entries(STONE_RANGES)) {
    const value = input[key];
    if (
      typeof value !== "number" ||
      !Number.isFinite(value) ||
      value < range[0] ||
      value > range[1]
    )
      throw new Error(`Invalid ${key} value.`);
    (result as unknown as Record<string, unknown>)[key] = value;
  }
  if (!Number.isInteger(result.seed))
    throw new Error("Seed must be a whole number.");
  for (const key of ["detail", "compare", "turntable"] as const) {
    if (typeof input[key] !== "boolean")
      throw new Error(`Invalid ${key} option.`);
    result[key] = input[key];
  }
  if (typeof input.palette !== "string" || !getBuiltinSatmap(input.palette))
    throw new Error("Unknown SatMap in recipe.");
  result.palette = input.palette as BuiltinSatmapId;
  if (!LAB_VIEWS.includes(input.view as LabView))
    throw new Error("Invalid inspection view.");
  result.view = input.view as LabView;
  if (!["draft", "balanced", "closeup"].includes(input.quality as string))
    throw new Error("Invalid preview quality.");
  result.quality = input.quality as LabQuality;
  return result;
}
export const encodeStoneRecipe = (settings: StoneSettings) =>
  JSON.stringify({ kind: "frontier-sdf-stone", version: 1, settings }, null, 2);
export const clamp = (v: number, a: number, b: number) =>
  Math.max(a, Math.min(b, v));
export function detailWeight(footprint: number, size: number) {
  const t = clamp((footprint / Math.max(size, 1e-7) - 0.22) / 0.58, 0, 1);
  return 1 - t * t * (3 - 2 * t);
}
export function detailEnvelope(s: StoneSettings, fp: number) {
  return s.detail
    ? (s.chips * detailWeight(fp, 0.018) +
        s.bedding * 0.73 * detailWeight(fp, s.spacing * 0.0005) +
        s.grain *
          (detailWeight(fp, s.grainSize * 0.001) +
            0.35 * detailWeight(fp, s.grainSize * 0.0005))) *
        0.001
    : 0;
}
export function slopeBound(s: StoneSettings, fp: number) {
  const base = 1 + s.form * 5.2 * (0.016 * 4 + 0.007 * 11);
  if (!s.detail) return base;
  const chips =
    s.chips *
    0.001 *
    detailWeight(fp, 0.018) *
    5.2 *
    (0.6 * 22 + 0.28 * 47 + 0.12 * 93);
  const layers =
    s.bedding *
    0.001 *
    detailWeight(fp, s.spacing * 0.0005) *
    (0.91 * 6.2831853 * (1000 / s.spacing + 5.2 * (8 * 0.32 + 21 * 0.075)) +
      0.73 * 0.88 * 1.875 * 5.2 * 13);
  const grain =
    s.grain *
    0.001 *
    5.2 *
    ((detailWeight(fp, s.grainSize * 0.001) * 1000) / s.grainSize +
      (0.35 * detailWeight(fp, s.grainSize * 0.0005) * 2000) / s.grainSize);
  return Math.max(1.5, base + chips + layers + grain) * 1.1;
}
