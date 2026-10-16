import React, { useEffect, useState } from "react";
import { compositeContributors } from "./textureComposite.js";
// Live 2D composite of the layer stack for the Base color channel, drawn
// bottom-to-top with each layer's effective opacity and blend mode. This is
// where the layer system becomes visible: hide, reorder or fade a layer and
// the composite answers immediately. Rendered as an <img> (painted offscreen)
// so the studio's viewport canvas stays the only <canvas> in the document.
// It is a source composite, not a teapot UV mapping (the stock teapot UVs
// overlap; see texture-painting-plan.md).
const SIZE = 112;
export default function LayerCompositePreview({ doc }) {
  const [src, setSrc] = useState("");
  const contributors = compositeContributors(doc, "baseColor");
  const signature = contributors
    .map(
      (c) =>
        `${c.layerId}:${c.opacity.toFixed(3)}:${c.blend}:${c.texture.dataUrl.length}:${c.texture.dataUrl.slice(-24)}`,
    )
    .join("|");
  useEffect(() => {
    let cancelled = false;
    if (!contributors.length) {
      setSrc("");
      return;
    }
    (async () => {
      const canvas = document.createElement("canvas");
      canvas.width = SIZE;
      canvas.height = SIZE;
      const ctx = canvas.getContext("2d");
      for (const contributor of contributors) {
        const image = new Image();
        await new Promise((resolve) => {
          image.onload = resolve;
          image.onerror = resolve;
          image.src = contributor.texture.dataUrl;
        });
        if (cancelled) return;
        ctx.save();
        ctx.globalAlpha = contributor.opacity;
        ctx.globalCompositeOperation = contributor.composite;
        ctx.drawImage(image, 0, 0, SIZE, SIZE);
        ctx.restore();
      }
      if (!cancelled) setSrc(canvas.toDataURL("image/png"));
    })();
    return () => {
      cancelled = true;
    };
    // Contributors are re-derived from `signature`; doc identity changes on
    // every commit, including ones that cannot affect this channel.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [signature]);
  return (
    <figure
      className="tp-composite-preview"
      aria-label="Layer composite preview"
    >
      {src ? (
        <img src={src} alt="Layer composite preview" />
      ) : (
        <span className="tp-composite-empty" aria-hidden="true" />
      )}
      <figcaption>
        <strong>Layer composite</strong>
        <span>
          {contributors.length
            ? `${contributors.length} layer${contributors.length === 1 ? "" : "s"} · Base color · bottom-to-top`
            : "No painted Base color layers yet"}
        </span>
      </figcaption>
    </figure>
  );
}
