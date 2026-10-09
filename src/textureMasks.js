import {
  normalizePointGradient,
  defaultPointGradient,
} from "./pointGradient.js";
// Mask authoring is separate from the paint compositor. Colour matches can be
// inspected against imported source pixels; no teapot coverage is fabricated.
export function normalizeTextureMask(raw) {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return null;
  const bounded = (value, fallback, min, max) =>
    Number.isFinite(Number(value))
      ? Math.min(max, Math.max(min, Number(value)))
      : fallback;
  return {
    kind: ["color", "gradient"].includes(raw.kind) ? raw.kind : "fill",
    enabled: raw.enabled !== false,
    fill: raw.fill === 0 ? 0 : 1,
    inverted: raw.inverted === true,
    strength: bounded(raw.strength ?? 100, 100, 0, 100),
    color: /^#[a-f\d]{6}$/i.test(raw.color || "")
      ? raw.color.toLowerCase()
      : "#b87333",
    tolerance: bounded(raw.tolerance ?? 0.1, 0.1, 0, 1),
    softness: bounded(raw.softness ?? 0.1, 0.1, 0, 1),
    ...(raw.gradient || raw.kind === "gradient"
      ? {
          gradient: raw.gradient
            ? normalizePointGradient(raw.gradient, true)
            : defaultPointGradient(true),
        }
      : {}),
  };
}
const rgbOf = (hex) =>
  [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16) / 255);
// Explicit sRGB RGB distance, not a claimed perceptual / mesh-ID selection.
export function colourMaskMatch(rgb, mask) {
  const settings = normalizeTextureMask(mask);
  if (
    !settings ||
    !Array.isArray(rgb) ||
    rgb.length < 3 ||
    rgb.some((v) => !Number.isFinite(v))
  )
    return 0;
  const target = rgbOf(settings.color),
    distance = Math.hypot(...target.map((v, i) => v - rgb[i])) / Math.sqrt(3);
  let match;
  if (settings.softness === 0)
    match = distance <= settings.tolerance + 1e-9 ? 1 : 0;
  else {
    const t = Math.min(
      1,
      Math.max(0, (distance - settings.tolerance) / settings.softness),
    );
    match = 1 - t * t * (3 - 2 * t);
  }
  return settings.inverted ? 1 - match : match;
}
export function colourMaskSwatch(hex, mask) {
  return /^#[a-f\d]{6}$/i.test(hex || "")
    ? colourMaskMatch(rgbOf(hex), mask)
    : 0;
}
