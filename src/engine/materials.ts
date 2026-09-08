import { noise } from "./field";
import { strataCoordinate } from "./geology";
import { clamp, mix, smoothstep } from "./math";
import type { Settings, SurfaceMaterial, Vec3 } from "./types";

export const MATERIAL_IDS: SurfaceMaterial[] = [
  "sandstone",
  "limestone",
  "granite",
  "basalt",
];
export const MATERIAL_PRESETS: {
  id: SurfaceMaterial;
  name: string;
  description: string;
  values: Pick<
    Settings,
    | "materialColor"
    | "materialRoughness"
    | "materialGrain"
    | "materialRelief"
    | "materialScale"
    | "materialBedding"
    | "materialPorosity"
    | "materialWeathering"
    | "materialIOR"
    | "materialMoisture"
    | "rockRelief"
    | "rockNoiseScale"
    | "rockOctaves"
    | "rockRidges"
    | "rockLayerSpacing"
    | "rockLayerRelief"
    | "rockLayerWarp"
  >;
}[] = [
  {
    id: "sandstone",
    name: "Sandstone",
    description: "Cemented quartz grains · warm bedding",
    values: {
      materialColor: "#B19167",
      materialRoughness: 0.82,
      materialGrain: 0.6,
      materialRelief: 1.6,
      materialScale: 1,
      materialBedding: 0.55,
      materialPorosity: 0.32,
      materialWeathering: 0.28,
      materialIOR: 1.5,
      materialMoisture: 0,
      rockRelief: 5.5,
      rockNoiseScale: 0.55,
      rockOctaves: 4,
      rockRidges: 0.6,
      rockLayerSpacing: 0.42,
      rockLayerRelief: 2.8,
      rockLayerWarp: 0.65,
    },
  },
  {
    id: "limestone",
    name: "Limestone",
    description: "Fine calcite matrix · chalky mottling",
    values: {
      materialColor: "#C6C2B6",
      materialRoughness: 0.86,
      materialGrain: 0.12,
      materialRelief: 1.3,
      materialScale: 1,
      materialBedding: 0.22,
      materialPorosity: 0.26,
      materialWeathering: 0.2,
      materialIOR: 1.5,
      materialMoisture: 0,
      rockRelief: 4.5,
      rockNoiseScale: 0.65,
      rockOctaves: 4,
      rockRidges: 0.4,
      rockLayerSpacing: 0.65,
      rockLayerRelief: 1.6,
      rockLayerWarp: 0.75,
    },
  },
  {
    id: "granite",
    name: "Granite",
    description: "Interlocking quartz, feldspar & dark mica",
    values: {
      materialColor: "#99948F",
      materialRoughness: 0.57,
      materialGrain: 6,
      materialRelief: 0.8,
      materialScale: 1,
      materialBedding: 0,
      materialPorosity: 0.04,
      materialWeathering: 0.15,
      materialIOR: 1.5,
      materialMoisture: 0,
      rockRelief: 4,
      rockNoiseScale: 0.32,
      rockOctaves: 4,
      rockRidges: 0.7,
      rockLayerSpacing: 0.4,
      rockLayerRelief: 0,
      rockLayerWarp: 0.55,
    },
  },
  {
    id: "basalt",
    name: "Basalt",
    description: "Fine dark matrix · sparse vesicles",
    values: {
      materialColor: "#595B5C",
      materialRoughness: 0.73,
      materialGrain: 0.16,
      materialRelief: 1.1,
      materialScale: 1,
      materialBedding: 0,
      materialPorosity: 0.12,
      materialWeathering: 0.25,
      materialIOR: 1.5,
      materialMoisture: 0,
      rockRelief: 5,
      rockNoiseScale: 0.45,
      rockOctaves: 5,
      rockRidges: 0.75,
      rockLayerSpacing: 0.4,
      rockLayerRelief: 0,
      rockLayerWarp: 0.7,
    },
  },
];

export function srgbToLinear(value: number): number {
  return value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4;
}
export function colorToLinear(hex: string): Vec3 {
  return [1, 3, 5].map((i) =>
    srgbToLinear(parseInt(hex.slice(i, i + 2), 16) / 255),
  ) as Vec3;
}
export function dielectricF0(ior: number): number {
  return ((ior - 1) / (ior + 1)) ** 2;
}
export function featureVisibility(
  footprint: number,
  featureSize: number,
): number {
  return 1 - smoothstep(0.15, 0.75, footprint / Math.max(0.00001, featureSize));
}

/** Matching broad-band albedo for mesh vertex colors. Millimeter grains are
 * deliberately averaged out at mesh resolution rather than aliased into triangles.
 * Presets are authored plausible starting points, not measured rock specimens. */
export function materialMacroColor(
  p: Vec3,
  n: Vec3,
  s: Settings,
  displacement = 0,
): Vec3 {
  const q = p.map((v) => v / s.materialScale) as Vec3;
  const base = colorToLinear(s.materialColor);
  const broad = noise(q[0] * 0.31 + s.seed * 0.003, q[1] * 0.17, q[2] * 0.31);
  const fine = noise(q[0] * 1.7, q[1] * 1.3 + 4.7, q[2] * 1.7);
  const layer = Math.sin(strataCoordinate(...q) * 1.05);
  const stain = smoothstep(
    0.05,
    0.7,
    noise(q[0] * 0.67 + 3.1, q[1] * 0.18, q[2] * 0.67),
  );
  let factor = 1,
    oxide: Vec3 = [1, 1, 1],
    bedding: Vec3 = [1, 1, 1];
  if (s.material === "sandstone") {
    factor = 1 + broad * 0.12;
    const bed = smoothstep(-0.7, 0.7, layer);
    bedding = [
      mix(1, mix(0.64, 1.18, bed), s.materialBedding),
      mix(1, mix(0.46, 1.22, bed), s.materialBedding),
      mix(1, mix(0.27, 1.28, bed), s.materialBedding),
    ];
    oxide = [
      1 + stain * s.materialWeathering * 0.08,
      1 - stain * s.materialWeathering * 0.16,
      1 - stain * s.materialWeathering * 0.3,
    ];
  } else if (s.material === "limestone") {
    factor = 1 + broad * 0.12 + fine * 0.035 + layer * s.materialBedding * 0.13;
    oxide = [
      1,
      1 - stain * s.materialWeathering * 0.045,
      1 - stain * s.materialWeathering * 0.09,
    ];
  } else if (s.material === "granite") {
    factor = 1 + broad * 0.09 + fine * 0.025;
    oxide = [
      1 + stain * s.materialWeathering * 0.1,
      1 - stain * s.materialWeathering * 0.04,
      1 - stain * s.materialWeathering * 0.09,
    ];
  } else {
    factor = 1 + broad * 0.1 + fine * 0.045;
    oxide = [
      1 + stain * s.materialWeathering * 0.4,
      1 + stain * s.materialWeathering * 0.1,
      1 - stain * s.materialWeathering * 0.07,
    ];
  }
  const fresh = clamp(Math.max(displacement, 0) / 0.8, 0, 1);
  const deposited =
    clamp(Math.max(-displacement, 0) / 0.65, 0, 1) * smoothstep(0.2, 0.8, n[1]);
  return base.map((c, i) =>
    clamp(
      mix(
        c * factor * bedding[i] * mix(oxide[i], 1, fresh),
        c * 1.08,
        deposited * 0.4,
      ),
      0.008,
      0.85,
    ),
  ) as Vec3;
}
