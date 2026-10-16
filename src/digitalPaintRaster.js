import { projectScreenOperation } from "./screenPaintProjection.js";
import * as THREE from "three";
import { digitalBrushDabs, drawDigitalDabs } from "./digitalBrush.js";
import {
  DIGITAL_PAINT_CHANNELS,
  paintedChannelIds,
} from "./textureDigitalPaint.js";
import {
  maskGeometryCache,
  MASK_ATLAS_WIDTH,
  MASK_TILE_EDGE,
} from "./maskPaintRaster.js";

export function digitalPaintCanvas() {
  const c = document.createElement("canvas");
  c.width = MASK_ATLAS_WIDTH;
  c.height = MASK_TILE_EDGE;
  return c;
}
const scalarColour = (value, c) => {
  const v = Math.round(((value - c.min) / (c.max - c.min)) * 255)
    .toString(16)
    .padStart(2, "0");
  return `#${v}${v}${v}`;
};
// One scratch image for an operation. Stroke opacity is applied ONCE when
// compositing it; flow and pressure are per dab and cannot accidentally turn a
// 50% opacity stroke into an opaque mark when the pointer emits more events.
export function rasterDigitalOperation(
  op,
  geometry,
  color = op.brush.color,
  dynamics = true,
) {
  if (op.projection)
    return projectScreenOperation(op, geometry, color, dynamics);
  const canvas = digitalPaintCanvas(),
    ctx = canvas.getContext("2d");
  const cache = maskGeometryCache(geometry);
  ctx.save();
  ctx.setTransform(MASK_TILE_EDGE, 0, 0, -MASK_TILE_EDGE, 0, MASK_TILE_EDGE);
  ctx.clip(cache.path(op.region));
  if (op.kind === "fill") {
    ctx.fillStyle = color;
    ctx.fill(cache.path(op.region));
  } else {
    ctx.setTransform(1, 0, 0, -1, 0, MASK_TILE_EDGE);
    drawDigitalDabs(
      ctx,
      digitalBrushDabs(
        op.points,
        op.brush,
        op.seed,
        MASK_TILE_EDGE,
        color,
        dynamics,
      ),
      op.brush,
    );
  }
  ctx.restore();
  return canvas;
}
export function rasterizeDigitalChannels(layer, geometry) {
  const ids = paintedChannelIds(layer),
    canvases = new Map(ids.map((id) => [id, digitalPaintCanvas()]));
  for (const op of layer.paint?.operations || []) {
    for (const id of ids) {
      if (!Object.hasOwn(op.channels, id)) continue;
      const c = DIGITAL_PAINT_CHANNELS.find((c) => c.id === id),
        target = canvases.get(id).getContext("2d");
      const color =
        c.edit === "color" ? op.channels[id] : scalarColour(op.channels[id], c);
      const stroke = rasterDigitalOperation(
        op,
        geometry,
        op.kind === "erase" ? "#ffffff" : color,
        id === "baseColor" && op.kind !== "erase",
      );
      target.globalCompositeOperation =
        op.kind === "erase" ? "destination-out" : "source-over";
      target.globalAlpha = op.brush.opacity;
      target.drawImage(stroke, 0, 0);
      target.globalAlpha = 1;
      target.globalCompositeOperation = "source-over";
    }
  }
  return canvases;
}
export function createDigitalPaintAtlas(layer, geometry) {
  const canvases = rasterizeDigitalChannels(layer, geometry),
    ids = [...canvases.keys()],
    sliceSize = MASK_ATLAS_WIDTH * MASK_TILE_EDGE * 4;
  const bytes = new Uint8Array(sliceSize * (ids.length + 1)),
    slots = new Int32Array(DIGITAL_PAINT_CHANNELS.length).fill(-1),
    union = new Uint8Array(sliceSize);
  ids.forEach((id, slice) => {
    slots[DIGITAL_PAINT_CHANNELS.findIndex((c) => c.id === id)] = slice;
    const pixels = canvases
      .get(id)
      .getContext("2d")
      .getImageData(0, 0, MASK_ATLAS_WIDTH, MASK_TILE_EDGE).data;
    for (let y = 0; y < MASK_TILE_EDGE; y++) {
      const source = y * MASK_ATLAS_WIDTH * 4,
        dest = (MASK_TILE_EDGE - y - 1) * MASK_ATLAS_WIDTH * 4;
      const isColour =
        DIGITAL_PAINT_CHANNELS.find((c) => c.id === id).edit === "color";
      for (let x = 0; x < MASK_ATLAS_WIDTH; x++) {
        const from = source + x * 4,
          to = slice * sliceSize + dest + x * 4,
          alpha = pixels[from + 3] / 255;
        for (let c = 0; c < 3; c++) {
          let value = pixels[from + c] / 255;
          if (isColour)
            value =
              value <= 0.04045
                ? value / 12.92
                : Math.pow((value + 0.055) / 1.055, 2.4);
          bytes[to + c] = Math.round(value * alpha * 255);
        }
        bytes[to + 3] = pixels[from + 3];
      }
      for (let x = 0; x < MASK_ATLAS_WIDTH; x++) {
        const k = dest + x * 4,
          a = pixels[source + x * 4 + 3];
        union[k] =
          union[k + 1] =
          union[k + 2] =
          union[k + 3] =
            Math.max(union[k], a);
      }
    }
  });
  bytes.set(union, ids.length * sliceSize);
  const texture = new THREE.DataArrayTexture(
    bytes,
    MASK_ATLAS_WIDTH,
    MASK_TILE_EDGE,
    ids.length + 1,
  );
  texture.colorSpace = THREE.NoColorSpace;
  texture.minFilter = texture.magFilter = THREE.LinearFilter;
  texture.generateMipmaps = false;
  texture.needsUpdate = true;
  return { texture, ids, slots, unionSlot: ids.length, canvases };
}
export function digitalStrokeThumbnail(operations, geometry, mask = false) {
  const atlas = digitalPaintCanvas(),
    ctx = atlas.getContext("2d");
  for (const op of operations) {
    const mark = rasterDigitalOperation(
      op,
      geometry,
      op.kind === "erase"
        ? "#d5d1c3"
        : mask
          ? op.brush.color
          : op.channels.baseColor || op.brush.color,
      !mask,
    );
    ctx.globalAlpha = op.brush.opacity;
    ctx.drawImage(mark, 0, 0);
  }
  ctx.globalAlpha = 1;
  const pixels = ctx.getImageData(0, 0, atlas.width, atlas.height).data;
  let x0 = atlas.width,
    x1 = 0,
    y0 = atlas.height,
    y1 = 0;
  for (let y = 0; y < atlas.height; y++)
    for (let x = 0; x < atlas.width; x++)
      if (pixels[(y * atlas.width + x) * 4 + 3]) {
        x0 = Math.min(x0, x);
        x1 = Math.max(x1, x);
        y0 = Math.min(y0, y);
        y1 = Math.max(y1, y);
      }
  const out = document.createElement("canvas");
  out.width = 256;
  out.height = 128;
  if (x0 <= x1) {
    const w = x1 - x0 + 1,
      h = y1 - y0 + 1,
      scale = Math.min(240 / w, 112 / h);
    out
      .getContext("2d")
      .drawImage(
        atlas,
        x0,
        y0,
        w,
        h,
        (256 - w * scale) / 2,
        (128 - h * scale) / 2,
        w * scale,
        h * scale,
      );
  }
  return {
    name: "Digital stroke footprint",
    origin: "paint",
    width: 256,
    height: 128,
    dataUrl: out.toDataURL("image/png"),
  };
}
