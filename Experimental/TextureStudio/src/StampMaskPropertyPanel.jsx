import React, { useRef, useState } from "react";
import { ChevronDown, Pencil, RotateCcw, Trash2 } from "lucide-react";
import { Slider } from "./MaterialControls.jsx";
import { ArtworkMaskPreviewSVG, NormalizeArtworkMask } from "./ArtworkMask.js";
import "./maskPropertyPanel.css";

// 🧩 The Texture studio Mask property card for stamp components: the same
// frame — Enabled / Base mask / Invert / Strength, a rule, then the Source
// (Paint | Ellipse | Rectangle | Image) with its body, the painted-overlay
// slot and a preview row — driven by the artwork mask model, so the mask
// canvas, retained paint and SVG export keep working exactly as before.
const MASK_SOURCES = [
  ["paint", "Paint"],
  ["ellipse", "Ellipse"],
  ["rect", "Rectangle"],
  ["image", "Image"],
];
export default function StampMaskPropertyPanel({
  Mask,
  Locked = false,
  Editing = false,
  OnEdit,
  OnChange,
  OnError,
}) {
  const [collapsed, setCollapsed] = useState(false);
  const FileInput = useRef(null);
  const kindLabel = Mask
    ? MASK_SOURCES.find(([source]) => source === Mask.category)?.[1] || "Paint"
    : "None";
  const records = Mask?.paint?.length || 0;
  function Change(Patch, Key) {
    try {
      OnChange(NormalizeArtworkMask({ ...Mask, ...Patch }), Key);
    } catch (Failure) {
      OnError?.(Failure.message);
    }
  }
  async function Import(Event) {
    const File = Event.target.files[0];
    Event.target.value = "";
    if (!File) return;
    try {
      if (!["image/png", "image/jpeg", "image/webp"].includes(File.type) || File.size > 4 * 1024 * 1024) throw new Error("Choose a PNG, JPEG or WebP mask up to 4 MB; Stamp's 600 KB document limit still applies.");
      const Bitmap = await createImageBitmap(File);
      const Pixels = Bitmap.width * Bitmap.height;
      Bitmap.close();
      if (Pixels > 16777216) throw new Error("Mask images are limited to 16 megapixels.");
      const Encoded = await new Promise((Resolve, Reject) => {
        const Reader = new FileReader();
        Reader.onload = () => Resolve(Reader.result);
        Reader.onerror = () => Reject(new Error("Mask image could not be read."));
        Reader.readAsDataURL(File);
      });
      Change({ category: "image", image: Encoded });
    } catch (Failure) {
      OnError?.(Failure.message);
    }
  }
  const reset = () =>
    Change({
      enabled: true,
      invert: false,
      mode: "alpha",
      x: 50,
      y: 50,
      width: 100,
      height: 100,
      opacity: 1,
      ...(Mask?.category === "paint" ? { base: 1 } : {}),
      paint: [],
    });
  const preview = Mask
    ? `data:image/svg+xml;charset=utf-8,${encodeURIComponent(ArtworkMaskPreviewSVG(Mask))}`
    : "";
  const geometryRows = [
    ["x", "Centre X", -100, 200],
    ["y", "Centre Y", -100, 200],
    ["width", "Width", 1, 400],
    ["height", "Height", 1, 400],
  ];
  return (
    <div className="artwork-mask-properties">
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
          {Mask && (
            <button
              className="tp-card-reset"
              aria-label="Reset mask properties"
              disabled={Locked}
              onClick={reset}
            >
              <RotateCcw size={12} />
            </button>
          )}
        </div>
        <div className="mask-fold">
          <div className="mask-fold-clip">
            <div className="mask-fold-body">
              {!Mask ? (
                <>
                  <p className="tp-card-note">
                    No mask on this component. A mask limits where the
                    component shows; white includes and black excludes. Masks
                    ignore the colour/material channel targets.
                  </p>
                  <button
                    className="stamp-mask-add"
                    disabled={Locked}
                    onClick={() => {
                      try {
                        OnChange(NormalizeArtworkMask({ category: "paint", base: 1 }));
                      } catch (Failure) {
                        OnError?.(Failure.message);
                        return;
                      }
                      OnEdit?.();
                    }}
                  >
                    Add mask
                  </button>
                </>
              ) : (
                <>
                  <label className="tp-toggle-row">
                    <span>Mask enabled</span>
                    <input
                      type="checkbox"
                      aria-label="Mask enabled"
                      disabled={Locked}
                      checked={Mask.enabled}
                      onChange={(Event) => Change({ enabled: Event.target.checked })}
                    />
                  </label>
                  {Mask.category === "paint" && (
                    <div className="mask-prop-row">
                      <span>Base mask</span>
                      <select
                        aria-label="Mask fill"
                        value={Mask.base === 0 ? 0 : 1}
                        disabled={Locked}
                        onChange={(Event) => Change({ base: Number(Event.target.value) })}
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
                      disabled={Locked}
                      checked={Mask.invert}
                      onChange={(Event) => Change({ invert: Event.target.checked })}
                    />
                  </label>
                  <Slider
                    label="Mask strength"
                    ariaLabel="Mask strength"
                    min={0}
                    max={100}
                    step={1}
                    unit="%"
                    value={Math.round(Mask.opacity * 100)}
                    disabled={Locked}
                    onChange={(strength) => Change({ opacity: strength / 100 }, "mask-strength")}
                  />
                  <div className="mask-rule" />
                  <div className="mask-prop-row">
                    <span>Source</span>
                    <div
                      className="tp-source-pills mask-source-pills"
                      role="group"
                      aria-label="Mask type"
                    >
                      {MASK_SOURCES.map(([source, Label]) => (
                        <button
                          key={source}
                          disabled={Locked}
                          aria-pressed={Mask.category === source}
                          onClick={() =>
                            source === "image" && Mask.category !== "image"
                              ? FileInput.current?.click()
                              : Change({ category: source })
                          }
                        >
                          {Label}
                        </button>
                      ))}
                    </div>
                  </div>
                  {Mask.category === "paint" ? (
                    <p className="tp-card-note">
                      A filled base with retained paint. Open the mask canvas
                      to paint, erase and lay gradient strokes over it.
                    </p>
                  ) : (
                    <>
                      {Mask.category === "image" && (
                        <>
                          <div className="mask-prop-row">
                            <span>Coverage</span>
                            <select
                              aria-label="Artwork mask coverage"
                              value={Mask.mode}
                              disabled={Locked}
                              onChange={(Event) => Change({ mode: Event.target.value })}
                            >
                              <option value="alpha">Alpha</option>
                              <option value="luminance">Luminance</option>
                            </select>
                          </div>
                          <button
                            className="stamp-mask-add"
                            disabled={Locked}
                            onClick={() => FileInput.current?.click()}
                          >
                            Replace mask image
                          </button>
                        </>
                      )}
                      {geometryRows.map(([Key, Caption, Minimum, Maximum]) => (
                        <label className="mask-prop-row" key={Key}>
                          <span>{Caption}</span>
                          <input
                            type="number"
                            aria-label={`Artwork mask ${Key}`}
                            value={Mask[Key]}
                            min={Minimum}
                            max={Maximum}
                            step={1}
                            disabled={Locked}
                            onChange={(Event) => Change({ [Key]: +Event.target.value }, `mask-${Key}`)}
                          />
                        </label>
                      ))}
                    </>
                  )}
                  <div className="mask-slot-caption">Painted overlay</div>
                  <div className="mask-slot">
                    <div className="mask-slot-thumb">
                      <img src={preview} alt="" />
                    </div>
                    <div className="mask-slot-text">
                      <div className="mask-slot-name">
                        {records ? "Painted mask" : "No mask strokes yet"}
                      </div>
                      <div className="mask-slot-meta">
                        {records
                          ? `${records} records · retained paint over the source`
                          : "Paint on the mask canvas"}
                      </div>
                    </div>
                    <button
                      className="mask-verb"
                      aria-label="Edit layer mask"
                      title={Editing ? "Mask canvas is open" : "Edit layer mask"}
                      disabled={Locked || Editing}
                      onClick={OnEdit}
                    >
                      <Pencil size={13} />
                    </button>
                    <button
                      className="mask-verb danger"
                      aria-label="Clear painted mask overlay"
                      title="Clear painted mask overlay"
                      disabled={Locked || !records}
                      onClick={() => Change({ paint: [] })}
                    >
                      <Trash2 size={13} />
                    </button>
                  </div>
                  {!!records && (
                    <p className="mask-slot-note">
                      Retained strokes, shapes and gradients over this mask's
                      source. White includes; black excludes.
                    </p>
                  )}
                  <p className="tp-card-note">
                    Saved on this component. White includes and black excludes.
                    Shape and image sources are placed in component space;
                    painted masks stay editable geometry and survive SVG
                    export.
                  </p>
                  <div className="mask-preview-row">
                    <div className="mask-preview-tile">
                      <img src={preview} alt="" />
                    </div>
                    <div className="mask-preview-text">
                      <span>Mask preview</span>
                      <small>
                        {Mask.category === "paint"
                          ? `${(Mask.base !== 0) !== Mask.invert ? "White" : "Black"} base`
                          : Mask.invert
                            ? "Inverted source"
                            : "Source coverage"}
                        {" "}· {Math.round(Mask.opacity * 100)}% strength
                      </small>
                    </div>
                  </div>
                  <button
                    className="stamp-mask-remove"
                    disabled={Locked}
                    onClick={() => OnChange(undefined)}
                  >
                    Remove mask
                  </button>
                </>
              )}
            </div>
          </div>
        </div>
        <input
          ref={FileInput}
          type="file"
          hidden
          accept="image/png,image/jpeg,image/webp"
          aria-label="Artwork mask image"
          onChange={Import}
        />
      </section>
    </div>
  );
}
