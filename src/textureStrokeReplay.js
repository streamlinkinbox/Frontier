// Browser-side rebuild of a painted channel raster from its recorded
// strokes. The model (textureStrokes.js) decides *what* to replay — the
// replay plan's base snapshot plus the remaining strokes in recorded order —
// this module only loads that base image and runs the deterministic
// round-tip rasterizer over it. Painting, deleting and re-deleting therefore
// always agree on the resulting pixels.
import {
  replayChannelStrokes,
  strokeReplayPlan,
} from "./textureStrokes.js";
import { normalizeTextureSource } from "./textureSource.js";

const canvasOf = (w, h) => {
  const canvas = document.createElement("canvas");
  canvas.width = w;
  canvas.height = h;
  return canvas;
};
function loadImage(dataUrl) {
  return new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error("Stored source could not be decoded."));
    image.src = dataUrl;
  });
}
// Returns the channel's next texture source after `strokes` (the full
// remaining stroke list for the layer) — or null when nothing remains and
// there was no pre-stroke base to restore.
export async function rebuildChannelTexture(layer, channelId, strokes, label) {
  const plan = strokeReplayPlan(strokes, channelId);
  if (!plan.strokes.length) return plan.base || null;
  const width = plan.base?.width || plan.strokes[0].image.width || 256,
    height = plan.base?.height || plan.strokes[0].image.height || 256;
  const target = canvasOf(width, height),
    scratch = canvasOf(width, height),
    mark = canvasOf(width, height);
  let baseCanvas = null;
  if (plan.base) {
    baseCanvas = canvasOf(width, height);
    baseCanvas
      .getContext("2d")
      .drawImage(await loadImage(plan.base.dataUrl), 0, 0, width, height);
  }
  replayChannelStrokes(target, scratch, mark, plan.strokes, baseCanvas);
  return normalizeTextureSource({
    origin: "paint",
    name: `${layer.name} · ${label} source`,
    width,
    height,
    dataUrl: target.toDataURL("image/png"),
  });
}
