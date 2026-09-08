import sources from "./library.json";
import {
  AUTHORED_RECIPES,
  SATMAP_FAMILIES,
  type AuthoredSatmapId,
  type SatelliteId,
  type SatmapFamily,
} from "./recipes";
import { PALETTE_SAMPLES } from "./pixels";
export { AUTHORED_RECIPES, SATMAP_FAMILIES } from "./recipes";
export type { SatmapFamily } from "./recipes";
export type BuiltinSatmapId = SatelliteId | AuthoredSatmapId;
export interface SatmapAsset {
  id: BuiltinSatmapId;
  name: string;
  family: SatmapFamily;
  category: string;
  origin: "satellite" | "authored" | "fantasy";
  region: string;
  tags: string;
  note: string;
  credit: string;
  license: string;
  source?: string;
  thumbnail?: string;
  palette: string;
  detail: string;
  detailId: SatelliteId;
  detailName: string;
  detailSource: string;
}
const sourceFamilies: Record<SatelliteId, SatmapFamily> = {
  namib: "desert",
  canyonlands: "badlands",
  iceland: "icelandic",
  "white-sands": "desert",
};
const detailSources: Record<SatmapFamily, SatelliteId> = {
  desert: "namib",
  grassland: "iceland",
  forest: "iceland",
  alpine: "canyonlands",
  fantasy: "white-sands",
  lake: "white-sands",
  volcanic: "canyonlands",
  icelandic: "iceland",
  beach: "white-sands",
  quarry: "canyonlands",
  wetland: "iceland",
  badlands: "canyonlands",
};
export const familyName = (family: SatmapFamily) =>
  SATMAP_FAMILIES.find((f) => f.id === family)!.name;
const linear = (v: number) =>
  v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4;
const srgb = (v: number) =>
  v <= 0.0031308 ? v * 12.92 : 1.055 * v ** (1 / 2.4) - 0.055;

/** Expand compact authored stops to the same 256-sample CLUT used by images.
 * Interpolate in linear RGB, then encode to sRGB storage. No runtime noise or
 * material shader change: the renderer still samples a real lookup texture. */
export function paletteFromStops(
  colors: string,
  positions?: readonly number[],
): string {
  const stops = colors.toLowerCase().trim().split(/\s+/);
  if (
    stops.length < 2 ||
    stops.length > 32 ||
    stops.some((c) => !/^[0-9a-f]{6}$/.test(c))
  )
    throw new Error("Invalid authored color stops.");
  const knots =
    positions ??
    (stops.length === 7
      ? [0, 0.12, 0.28, 0.46, 0.66, 0.84, 1]
      : stops.map((_, i) => i / (stops.length - 1)));
  if (
    knots.length !== stops.length ||
    knots[0] !== 0 ||
    knots.at(-1) !== 1 ||
    knots.some(
      (p, i) =>
        !Number.isFinite(p) || p < 0 || p > 1 || (i > 0 && p <= knots[i - 1]),
    )
  )
    throw new Error("Invalid authored color positions.");
  const rgb = stops.map((c) =>
    [0, 2, 4].map((i) => linear(parseInt(c.slice(i, i + 2), 16) / 255)),
  );
  let result = "",
    segment = 0;
  for (let i = 0; i < PALETTE_SAMPLES; i++) {
    const t = i / (PALETTE_SAMPLES - 1);
    while (segment < knots.length - 2 && t > knots[segment + 1]) segment++;
    const f = (t - knots[segment]) / (knots[segment + 1] - knots[segment]);
    for (let c = 0; c < 3; c++) {
      const value = rgb[segment][c] * (1 - f) + rgb[segment + 1][c] * f;
      result += Math.max(0, Math.min(255, Math.round(srgb(value) * 255)))
        .toString(16)
        .padStart(2, "0");
    }
  }
  return result;
}

// Keep the original four IDs, order and pixels byte-for-byte for old projects.
export const SATELLITE_LIBRARY: readonly SatmapAsset[] = sources.map(
  (source) => {
    const id = source.id as SatelliteId;
    return {
      ...source,
      id,
      family: sourceFamilies[id],
      category: familyName(sourceFamilies[id]),
      origin: "satellite",
      tags: `${source.category} satellite landsat ${source.region}`,
      thumbnail: `satmaps/${id}.webp`,
      detailId: id,
      detailName: source.name,
      detailSource: source.source,
    };
  },
);
export const SATMAP_LIBRARY: readonly SatmapAsset[] = [
  ...SATELLITE_LIBRARY,
  ...AUTHORED_RECIPES.map((recipe) => {
    const detail = sources.find((s) => s.id === detailSources[recipe.family])!;
    const fantasy = recipe.family === "fantasy";
    return {
      id: recipe.id,
      name: recipe.name,
      family: recipe.family,
      category: familyName(recipe.family),
      origin: fantasy ? ("fantasy" as const) : ("authored" as const),
      region: fantasy
        ? "Fictional terrain palette"
        : "Terrain-inspired palette",
      tags: recipe.tags,
      note: `${fantasy ? "Fictional" : "Artist-authored"} CLUT, not a satellite extraction. Shared luminance detail: ${detail.name}; not measured relief.`,
      credit: "Frontier · original authored CLUT",
      license:
        "Original Frontier palette recipe; shared USGS image detail is public domain.",
      palette: paletteFromStops(
        recipe.colors,
        "knots" in recipe ? recipe.knots : undefined,
      ),
      detail: detail.detail,
      detailId: detail.id as SatelliteId,
      detailName: detail.name,
      detailSource: detail.source,
    };
  }),
];
const byId = new Map(SATMAP_LIBRARY.map((asset) => [asset.id, asset]));
export function isSatmapId(id: unknown): id is BuiltinSatmapId | "custom" {
  return (
    typeof id === "string" &&
    (id === "custom" || byId.has(id as BuiltinSatmapId))
  );
}
export function getBuiltinSatmap(id: string) {
  return byId.get(id as BuiltinSatmapId);
}
const searchable = (s: string) =>
  s
    .normalize("NFKD")
    .replace(/\p{Diacritic}/gu, "")
    .toLowerCase();
const searchIndex = new Map(
  SATMAP_LIBRARY.map((a) => [
    a.id,
    searchable(`${a.name} ${a.category} ${a.family} ${a.region} ${a.tags}`),
  ]),
);
export function filterSatmaps(
  query = "",
  family: SatmapFamily | "all" = "all",
  origin: "all" | "satellite" | "authored" = "all",
): readonly SatmapAsset[] {
  const terms = searchable(query.trim()).split(/\s+/).filter(Boolean);
  return SATMAP_LIBRARY.filter(
    (a) =>
      (family === "all" || a.family === family) &&
      (origin === "all" ||
        (origin === "satellite"
          ? a.origin === "satellite"
          : a.origin !== "satellite")) &&
      terms.every((t) => searchIndex.get(a.id)!.includes(t)),
  );
}
