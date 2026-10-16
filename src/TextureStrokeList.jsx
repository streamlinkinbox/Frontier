import React from "react";
import { Brush, Eraser, Trash2 } from "lucide-react";
import { paintTool } from "./paintToolCatalogue.js";
import { PAINT_CHANNELS } from "./paintChannelModel.js";
import { layerStrokes } from "./textureStrokes.js";
// The selected layer's recorded paint strokes, newest first. Deleting a
// stroke replays the remaining strokes for that channel over the recorded
// pre-stroke base (TexturePaintStudio.removeStroke), so the raster stays
// exactly what the remaining gestures produced.
export default function TextureStrokeList({ layer, disabled, onRemoveStroke }) {
  const strokes = layerStrokes(layer);
  if (!strokes.length) return null;
  return (
    <section
      className="tp-property-card tp-stroke-card"
      aria-label={`Paint strokes on ${layer.name}`}
    >
      <h2>
        <i className="warm" />
        Paint strokes <small>{strokes.length} recorded</small>
      </h2>
      <ul className="tp-stroke-list">
        {[...strokes].reverse().map((stroke) => (
          <li key={stroke.id} data-stroke-id={stroke.id}>
            <img src={stroke.image.dataUrl} alt="" />
            <span>
              <strong>
                {stroke.operation === "erase" ? (
                  <Eraser size={10} />
                ) : (
                  <Brush size={10} />
                )}{" "}
                {paintTool(stroke.settings.toolId).label}
              </strong>
              <small>
                {PAINT_CHANNELS.find((c) => c.id === stroke.channelId)
                  ?.label || stroke.channelId}{" "}
                · {stroke.operation} · {stroke.points.length} pts
              </small>
            </span>
            <button
              aria-label={`Delete paint stroke on ${layer.name}`}
              title="Delete this stroke and replay the remaining strokes"
              disabled={disabled}
              onClick={() => onRemoveStroke(layer.id, stroke.id)}
            >
              <Trash2 size={12} />
            </button>
          </li>
        ))}
      </ul>
      <p className="tp-card-note">
        Strokes are stored on this layer and replayed in order. Deleting one
        rebuilds the channel from the remaining strokes; undo restores it.
      </p>
    </section>
  );
}
