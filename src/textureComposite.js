// Deterministic 2D composite of the texture layer stack for one channel.
// This is the layer system's contract made visible: layers are stored
// topmost-first, so compositing walks them bottom-to-top; a layer only
// contributes when it (and every folder above it) is visible, the channel
// is enabled and in Texture mode, and it has a raster. Effective opacity is
// the layer's own opacity multiplied by every ancestor folder's opacity.
// It is a 2D source composite — not a teapot UV/atlas mapping.
import {
  isTextureLayerVisible,
  textureAncestors,
} from "./textureDocument.js";
export const COMPOSITE_BLENDS = {
  Normal: "source-over",
  Multiply: "multiply",
  Screen: "screen",
  Overlay: "overlay",
  Add: "lighter",
  Darken: "darken",
};
export function compositeContributors(doc, channelId) {
  const contributors = [];
  // doc.layers is topmost-first; composite from the bottom of the stack up.
  for (const layer of [...doc.layers].reverse()) {
    if (layer.kind === "folder") continue;
    if (!isTextureLayerVisible(doc, layer.id)) continue;
    if (!layer.channels.includes(channelId)) continue;
    const settings = layer.channelSettings[channelId];
    if (settings?.mode !== "texture" || !settings.texture) continue;
    const opacity =
      textureAncestors(doc, layer.id).reduce(
        (acc, ancestor) => acc * (ancestor.opacity / 100),
        layer.opacity / 100,
      );
    if (opacity <= 0) continue;
    contributors.push({
      layerId: layer.id,
      name: layer.name,
      texture: settings.texture,
      opacity,
      blend: layer.blend,
      composite: COMPOSITE_BLENDS[layer.blend] || "source-over",
      strokes: (layer.strokes || []).filter(
        (s) => s.channelId === channelId,
      ).length,
    });
  }
  return contributors;
}
