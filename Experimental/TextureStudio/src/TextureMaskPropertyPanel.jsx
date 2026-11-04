import React, { useState } from "react";
import { ChevronDown, RotateCcw, Trash2 } from "lucide-react";
import { Slider, ColorField } from "./MaterialControls.jsx";
import PointGradientEditor from "./PointGradientEditor.jsx";
import { defaultPointGradient } from "./pointGradient.js";
import ColourMaskPreview from "./ColourMaskPreview.jsx";
import { MaskIcon } from "./TextureLayerStack.jsx";
import "./maskPropertyPanel.css";

// 🧩 The mask face of the layer inspector, shaped after MaskPropertyPanel.html:
// one collapsible Mask section — Enabled / Base mask / Invert / Strength, a rule,
// then the Source (Fill | Paint | Colour | Gradient) with its body, the painted
// overlay slot, and a preview row. Alloy keeps its real controls (fill select,
// checkboxes, colour + gradient editors) inside that frame.
const MASK_KINDS = [
  ["fill", "Fill"],
  ["paint", "Paint"],
  ["color", "Colour"],
  ["gradient", "Gradient"],
];
export default function TextureMaskPropertyPanel({
  layer,
  sourceLayer,
  locked,
  onMask,
  gradientContext,
  onGradientActivate,
}) {
  const mask = layer?.mask;
  const [collapsed, setCollapsed] = useState(false);
  if (!mask) return null;
  const kindLabel =
    MASK_KINDS.find(([kind]) => kind === mask.kind)?.[1] || "Fill";
  const records = mask.paint?.operations.length || 0;
  const reset = () =>
    onMask({
      enabled: true,
      kind: mask.kind,
      fill: 1,
      paint: null,
      inverted: false,
      strength: 100,
      color: "#b87333",
      tolerance: 0.1,
      softness: 0.1,
      ...(mask.kind === "gradient"
        ? { gradient: defaultPointGradient(true) }
        : {}),
    });
  return (
    <section
      className={`tp-property-card tp-shared-controls mask-property-card${collapsed ? " collapsed" : ""}`}
      aria-label="Mask property card"
    >
      <div className="mask-section-head">
        <button
          className="mask-section-toggle"
          aria-label="Mask properties"
          aria-expanded={!collapsed}
          onClick={() => setCollapsed(!collapsed)}
        >
          <ChevronDown size={12} />
          <i className="mask-dot" />
          <span>Mask</span>
          <small>{kindLabel}</small>
        </button>
        <button
          className="tp-card-reset"
          aria-label="Reset mask properties"
          disabled={locked}
          onClick={reset}
        >
          <RotateCcw size={12} />
        </button>
      </div>
      <div className="mask-fold">
        <div className="mask-fold-clip">
          <div className="mask-fold-body">
            <label className="tp-toggle-row">
              <span>Mask enabled</span>
              <input
                type="checkbox"
                aria-label="Mask enabled"
                disabled={locked}
                checked={mask.enabled}
                onChange={(e) => onMask({ enabled: e.target.checked })}
              />
            </label>
            {["fill", "paint"].includes(mask.kind) && (
              <div className="mask-prop-row">
                <span>Base mask</span>
                <select
                  aria-label="Mask fill"
                  value={mask.fill}
                  disabled={locked}
                  onChange={(e) => onMask({ fill: Number(e.target.value) })}
                >
                  <option value={1}>White</option>
                  <option value={0}>Black</option>
                </select>
              </div>
            )}
            <label className="tp-toggle-row">
              <span>Invert mask</span>
              <input
                type="checkbox"
                aria-label="Invert mask"
                disabled={locked}
                checked={mask.inverted}
                onChange={(e) => onMask({ inverted: e.target.checked })}
              />
            </label>
            <Slider
              label="Mask strength"
              min={0}
              max={100}
              step={1}
              unit="%"
              value={mask.strength}
              disabled={locked}
              onChange={(strength) => onMask({ strength })}
            />
            <div className="mask-rule" />
            <div className="mask-prop-row">
              <span>Source</span>
              <div
                className="tp-source-pills mask-source-pills"
                role="group"
                aria-label="Mask type"
              >
                {MASK_KINDS.map(([kind, label]) => (
                  <button
                    key={kind}
                    disabled={locked}
                    aria-pressed={mask.kind === kind}
                    onClick={() => onMask({ kind })}
                  >
                    {label}
                  </button>
                ))}
              </div>
            </div>
            {mask.kind === "gradient" ? (
              <PointGradientEditor
                gradient={mask.gradient}
                label="Mask gradient"
                mask={mask}
                disabled={locked}
                editor={
                  gradientContext?.layerId === layer.id && gradientContext.mask
                    ? gradientContext
                    : null
                }
                onActivate={(flags) =>
                  onGradientActivate({
                    layerId: layer.id,
                    mask: true,
                    ...flags,
                  })
                }
                onChange={(gradient, field) =>
                  onMask(
                    { gradient },
                    {
                      key: field
                        ? `mask-gradient-${layer.id}-${field}`
                        : undefined,
                    },
                  )
                }
              />
            ) : mask.kind === "color" ? (
              <>
                <ColorField
                  label="Selected colour"
                  ariaLabel="Colour mask colour"
                  disabled={locked}
                  value={mask.color}
                  onChange={(color) => onMask({ color })}
                />
                <Slider
                  label="Tolerance"
                  ariaLabel="Colour mask tolerance"
                  disabled={locked}
                  percentage
                  value={mask.tolerance}
                  onChange={(tolerance) => onMask({ tolerance })}
                />
                <Slider
                  label="Softness"
                  ariaLabel="Colour mask softness"
                  disabled={locked}
                  percentage
                  value={mask.softness}
                  onChange={(softness) => onMask({ softness })}
                />
                <ColourMaskPreview layer={layer} sourceLayer={sourceLayer} />
              </>
            ) : null}
            <div className="mask-slot-caption">Painted overlay</div>
            <div className="mask-slot">
              <div className="mask-slot-thumb">
                <MaskIcon mask={mask} size={38} />
              </div>
              <div className="mask-slot-text">
                <div className="mask-slot-name">
                  {records ? "Painted mask" : "No mask strokes yet"}
                </div>
                <div className="mask-slot-meta">
                  {records
                    ? `${records} records · strokes / region fills over the source`
                    : "Paint in Lit or packed UV view"}
                </div>
              </div>
              <button
                className="mask-verb danger"
                aria-label="Clear painted mask overlay"
                title="Clear painted mask overlay"
                disabled={locked || !records}
                onClick={() => onMask({ paint: null })}
              >
                <Trash2 size={13} />
              </button>
            </div>
            {!!records && (
              <p className="mask-slot-note">
                UV brush strokes / region fills over this mask's source. White
                includes; black excludes.
              </p>
            )}
            <p className="tp-card-note">
              {layer.kind === "folder"
                ? "Saved on this folder for its descendants. "
                : "Saved on this layer. "}
              {mask.kind === "color"
                ? "Colour matching uses normalized sRGB RGB distance; the preview evaluates source pixels only. "
                : "White includes and black excludes. "}
              Paint mask in Lit or packed UV space; strokes and region fills
              affect native part-material and decal coverage. Colour previews
              sample the source texture per UV tile, not the final shaded
              material. Digital Paint / Erase / Fill also edits this mask with
              pressure, shape and grain controls.
            </p>
            <div className="mask-preview-row">
              <div className="mask-preview-tile">
                <MaskIcon mask={mask} size={34} />
              </div>
              <div className="mask-preview-text">
                <span>Mask preview</span>
                <small>
                  {(mask.fill === 1) !== mask.inverted ? "White" : "Black"}{" "}
                  coverage · {Math.round(mask.strength)}% strength
                </small>
              </div>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}
