import {
  normalizePaintSettings,
  paintTool,
  PAINT_TOOLS,
} from "./paintToolCatalogue.js";
import { PAINT_CHANNELS } from "./paintChannelModel.js";
import { normalizeTextureSource } from "./textureSource.js";
export const SOURCE_STROKE_POINT_LIMIT = 1024;
export const SOURCE_STROKE_SCHEMA = "alloy.source-stroke.v1";
// Paint strokes are first-class, replayable records stored on their layer.
// A layer only carries a `strokes` key once it actually has strokes, so an
// unwritten layer still allocates nothing (see docs/texture-painting-plan.md).
export const LAYER_STROKE_LIMIT = 48;
export const layerStrokes = (layer) =>
  Array.isArray(layer?.strokes) ? layer.strokes : [];
export const channelStrokes = (layer, channelId) =>
  layerStrokes(layer).filter((s) => s.channelId === channelId);
const validId = (id) =>
  typeof id === "string" && /^[a-zA-Z0-9_-]{1,80}$/.test(id);
export function normalizeSourceStroke(raw) {
  if (
    !raw ||
    typeof raw !== "object" ||
    raw.schema !== SOURCE_STROKE_SCHEMA ||
    !validId(raw.id) ||
    !validId(raw.layerId) ||
    !PAINT_CHANNELS.some(
      (c) => c.id === raw.channelId && c.edit !== "derived",
    ) ||
    !PAINT_TOOLS.some((t) => t.id === raw.settings?.toolId) ||
    !Array.isArray(raw.points) ||
    !raw.points.length ||
    raw.points.length > SOURCE_STROKE_POINT_LIMIT
  )
    return null;
  const image = normalizeTextureSource(raw.image);
  if (!image) return null;
  const points = [];
  for (const point of raw.points) {
    if (
      !point ||
      ![point.x, point.y, point.pressure].every(
        (v) => typeof v === "number" && Number.isFinite(v),
      )
    )
      return null;
    points.push({
      x: Number(Math.min(1, Math.max(0, point.x)).toFixed(5)),
      y: Number(Math.min(1, Math.max(0, point.y)).toFixed(5)),
      pressure: Number(Math.min(1, Math.max(0, point.pressure)).toFixed(3)),
    });
  }
  const settings = normalizePaintSettings(raw.settings);
  // Optional snapshot of the channel raster as it was before the layer's
  // first stroke on that channel. It is the replay base: deleting strokes
  // replays the remaining ones over exactly this content, never over a
  // guess. An invalid base invalidates the record rather than dropping it.
  let base;
  if (raw.base != null) {
    base = normalizeTextureSource(raw.base);
    if (!base) return null;
  }
  return {
    schema: SOURCE_STROKE_SCHEMA,
    id: raw.id,
    layerId: raw.layerId,
    layerName:
      typeof raw.layerName === "string"
        ? raw.layerName.replace(/[\u0000-\u001f\u007f]/g, "").slice(0, 80) ||
          "Layer"
        : "Layer",
    channelId: raw.channelId,
    settings,
    operation: paintTool(settings.toolId).key === "eraser" ? "erase" : "paint",
    points,
    image,
    ...(base ? { base } : {}),
  };
}
// The strokes recorded on one layer, normalized against their container.
// The container is authoritative for `layerId`: a record's identity is its
// owning layer, so duplicating a layer remaps its strokes to the copy.
// Malformed records are rejected loudly (throw), never silently dropped —
// dropping a stroke would lose user painting on project open.
export function normalizeLayerStrokes(raw, layerId) {
  if (raw == null) return [];
  if (!Array.isArray(raw))
    throw new Error("A layer's paint strokes must be a stroke list.");
  if (raw.length > LAYER_STROKE_LIMIT)
    throw new Error(
      `A layer supports up to ${LAYER_STROKE_LIMIT} recorded paint strokes.`,
    );
  return raw.map((entry) => {
    const stroke = normalizeSourceStroke({ ...entry, layerId });
    if (!stroke)
      throw new Error(
        "A layer contains an invalid or unbounded paint stroke record.",
      );
    return stroke;
  });
}
// What a channel's raster is rebuilt from: the first recorded stroke's
// pre-stroke base snapshot (if any) plus every stroke in recorded order.
export function strokeReplayPlan(strokes, channelId) {
  const ordered = (strokes || []).filter((s) => s.channelId === channelId);
  return { base: ordered[0]?.base || null, strokes: ordered };
}
export function sourceStrokeOptions(raw) {
  const settings = normalizePaintSettings(raw),
    erase = paintTool(settings.toolId).key === "eraser";
  return {
    color: settings.color,
    size: Math.min(256, Math.max(1, settings.params.size || 8)),
    alpha: Math.min(
      1,
      Math.max(
        0,
        (((erase
          ? (settings.params.strength ?? 100)
          : (settings.params.opacity ?? 100)) /
          100) *
          (settings.params.flow ?? 100)) /
          100,
      ),
    ),
    operation: erase ? "erase" : "paint",
  };
}
// A small deterministic round-tip source rasterizer, NOT a mesh/UV painter or
// a simulation of all 102 instrument families. Alpha is applied once per stroke.
export function drawSourceStroke(ctx, points, raw) {
  const options = sourceStrokeOptions(raw),
    width = ctx.canvas.width,
    height = ctx.canvas.height;
  if (!points.length || options.alpha === 0) return false;
  const px = (point) => ({
    x: point.x * width,
    y: point.y * height,
    p: point.pressure,
  });
  const path = points.map(px);
  let distance = 0;
  for (let i = 1; i < path.length; i++)
    distance += Math.hypot(
      path[i].x - path[i - 1].x,
      path[i].y - path[i - 1].y,
    );
  const size = (options.size * Math.max(width, height)) / 256,
    step = Math.max(0.5, size * 0.2, distance / 15000);
  ctx.save();
  ctx.fillStyle = options.operation === "erase" ? "#f2e9d6" : options.color;
  ctx.globalAlpha = 1;
  ctx.globalCompositeOperation = "source-over";
  const dab = (x, y, p) => {
    if (p <= 0) return;
    ctx.beginPath();
    ctx.arc(x, y, size * 0.5 * p, 0, Math.PI * 2);
    ctx.fill();
  };
  dab(path[0].x, path[0].y, path[0].p);
  for (let i = 1; i < path.length; i++) {
    const a = path[i - 1],
      b = path[i],
      count = Math.max(1, Math.ceil(Math.hypot(b.x - a.x, b.y - a.y) / step));
    for (let n = 1; n <= count; n++) {
      const t = n / count;
      dab(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.p + (b.p - a.p) * t);
    }
  }
  ctx.restore();
  return true;
}
// Rebuild a channel raster from its recorded strokes: start from `base`
// (a canvas holding the replay-plan base snapshot, or null for transparent)
// and re-apply every stroke in recorded order with the same round-tip
// rasterizer that produced the live painting, so replay is deterministic.
// `target`, `scratch` and `mark` are caller-owned canvases of equal size.
export function replayChannelStrokes(target, scratch, mark, list, baseCanvas = null) {
  const tctx = target.getContext("2d"),
    sctx = scratch.getContext("2d");
  sctx.clearRect(0, 0, scratch.width, scratch.height);
  if (baseCanvas)
    sctx.drawImage(baseCanvas, 0, 0, scratch.width, scratch.height);
  tctx.clearRect(0, 0, target.width, target.height);
  tctx.drawImage(scratch, 0, 0);
  for (const stroke of list) {
    paintSourceStroke(target, scratch, mark, stroke.points, stroke.settings);
    sctx.clearRect(0, 0, scratch.width, scratch.height);
    sctx.drawImage(target, 0, 0);
  }
  return target;
}
export function paintSourceStroke(target, base, mark, points, settings) {
  const opts = sourceStrokeOptions(settings),
    ctx = target.getContext("2d"),
    markCtx = mark.getContext("2d");
  markCtx.clearRect(0, 0, mark.width, mark.height);
  drawSourceStroke(markCtx, points, settings);
  ctx.clearRect(0, 0, target.width, target.height);
  ctx.drawImage(base, 0, 0);
  ctx.save();
  ctx.globalAlpha = opts.alpha;
  ctx.globalCompositeOperation =
    opts.operation === "erase" ? "destination-out" : "source-over";
  ctx.drawImage(mark, 0, 0);
  ctx.restore();
}
