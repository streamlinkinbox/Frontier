import NameField from "./AuthoringNameField.jsx";
import DecalComponentsPanel from "./DecalComponentsPanel.jsx";
import PointGradientEditor from "./PointGradientEditor.jsx";
import { defaultPointGradient } from "./pointGradient.js";
import { Slider, ColorField } from "./MaterialControls.jsx";
import ColourMaskPreview from "./ColourMaskPreview.jsx";
import ChannelPropertyPanel from "./ChannelPropertyPanel.jsx";
import React from "react";
import {
  Check,
  CircleHelp,
  Eye,
  EyeOff,
  Layers3,
  LockKeyhole,
  RotateCcw,
  SlidersHorizontal,
  Trash2,
  UnlockKeyhole,
} from "lucide-react";
import {
  TEXTURE_BLENDS,
  TEXTURE_CHANNELS,
  TEXTURE_KINDS,
  textureAncestors,
  textureSubtree,
  isTextureLayerLocked,
  isTextureSubtreeProtected,
} from "./textureDocument.js";
import { DockTab, LayerThumbnail, MaskIcon } from "./TextureLayerStack.jsx";

function Amount(props) {
  return (
    <div className="tp-shared-controls">
      <Slider {...props} min={0} max={100} step={1} unit="%" />
    </div>
  );
}
export default function TextureLayerInspector({
  layer,
  doc,
  thumbs = {},
  maskSelected,
  index,
  count,
  onPatch,
  onMask,
  onRemoveMask,
  onSelectLayer,
  onFolderChange,
  onUngroup,
  showTab = true,
  painting,
  onStroke,
  onOpenTools,
  materialWorkspace,
  gradientContext,
  onGradientActivate,
  decalContext,
}) {
  const locked = !!layer && isTextureLayerLocked(doc, layer.id),
    inheritedLock =
      !!layer && textureAncestors(doc, layer.id).some((l) => l.locked);
  const descendants = layer ? textureSubtree(doc, layer.id) : [],
    subtreeIds = new Set(descendants.map((l) => l.id));
  const groupBlocked = !!layer && isTextureSubtreeProtected(doc, layer.id);
  return (
    <>
      {showTab && <DockTab icon={SlidersHorizontal}>Inspector</DockTab>}
      <div className="tp-inspector-scroll">
        {!layer ? (
          <div className="tp-empty">
            <SlidersHorizontal size={28} />
            <strong>No layer selected</strong>
            <p>
              Add a layer or select an existing one to inspect its settings.
            </p>
          </div>
        ) : (
          <>
            <div className="tp-inspector-trail">
              <Layers3 size={11} />
              <span>Teapot</span>
              <span>/</span>
              <span>
                {maskSelected
                  ? layer.mask?.kind === "color"
                    ? "Colour mask"
                    : layer.mask?.kind === "gradient"
                      ? "Gradient mask"
                      : "Layer mask"
                  : layer.kind === "folder"
                    ? "Layer folder"
                    : "Surface layer"}
              </span>
            </div>
            <div className="tp-inspector-identity">
              <div className="tp-identity-icon">
                {maskSelected ? (
                  <MaskIcon mask={layer.mask} size={32} />
                ) : (
                  <LayerThumbnail layer={layer} thumbs={thumbs} size={42} />
                )}
              </div>
              <div>
                <small>
                  {maskSelected
                    ? "MASK PROPERTIES"
                    : `${TEXTURE_KINDS[layer.kind].label.toUpperCase()} LAYER`}
                </small>
                <h1>
                  {maskSelected
                    ? layer.mask?.kind === "color"
                      ? "Colour mask"
                      : layer.mask?.kind === "gradient"
                        ? "Gradient mask"
                        : "Layer mask"
                    : layer.name}
                </h1>
                <p>
                  {maskSelected
                    ? `On ${layer.name}`
                    : `Position ${index + 1} of ${count} · top to bottom`}
                </p>
              </div>
            </div>
            <div className="tp-inspector-status">
              <button
                className={`tp-visibility-pill ${layer.visible ? "" : "off"}`}
                aria-label={
                  layer.visible ? "Hide selected layer" : "Show selected layer"
                }
                onClick={() => onPatch({ visible: !layer.visible })}
              >
                {layer.visible ? <Eye size={12} /> : <EyeOff size={12} />}{" "}
                {layer.visible ? "Visible" : "Hidden"}
              </button>
              <button
                className="tp-lock-pill"
                aria-label={
                  layer.locked ? "Unlock selected layer" : "Lock selected layer"
                }
                disabled={inheritedLock}
                onClick={() => onPatch({ locked: !layer.locked })}
              >
                {locked ? (
                  <LockKeyhole size={12} />
                ) : (
                  <UnlockKeyhole size={12} />
                )}{" "}
                {locked ? "Locked" : "Unlocked"}
              </button>
            </div>
            {locked && (
              <div className="tp-lock-notice">
                <LockKeyhole size={12} />{" "}
                {inheritedLock
                  ? "This layer inherits a locked folder. Unlock the folder to edit its contents."
                  : "Unlock this layer to edit its properties."}
              </div>
            )}
            {maskSelected && layer.mask ? (
              <>
                <div className="tp-section-label">
                  <span>MASK SETTINGS</span>
                  <small>
                    {layer.kind === "folder"
                      ? "Folder & descendants"
                      : "Layer source"}
                  </small>
                </div>
                <section className="tp-property-card tp-shared-controls">
                  <h2>
                    <i />
                    {layer.mask.kind === "gradient"
                      ? "Gradient coverage"
                      : layer.mask.kind === "color"
                        ? "Colour selection"
                        : "Coverage mask"}
                    <button
                      className="tp-card-reset"
                      aria-label="Reset mask properties"
                      disabled={locked}
                      onClick={() =>
                        onMask({
                          enabled: true,
                          kind: layer.mask.kind,
                          fill: 1,
                          inverted: false,
                          strength: 100,
                          color: "#b87333",
                          tolerance: 0.1,
                          softness: 0.1,
                          ...(layer.mask.kind === "gradient"
                            ? { gradient: defaultPointGradient(true) }
                            : {}),
                        })
                      }
                    >
                      <RotateCcw size={12} />
                    </button>
                  </h2>
                  <div
                    className="tp-source-pills"
                    role="group"
                    aria-label="Mask type"
                  >
                    {[
                      ["fill", "Fill"],
                      ["color", "Colour"],
                      ["gradient", "Gradient"],
                    ].map(([kind, label]) => (
                      <button
                        key={kind}
                        disabled={locked}
                        aria-pressed={layer.mask.kind === kind}
                        onClick={() => onMask({ kind })}
                      >
                        {label}
                      </button>
                    ))}
                  </div>
                  {layer.mask.kind === "gradient" ? (
                    <PointGradientEditor
                      gradient={layer.mask.gradient}
                      label="Mask gradient"
                      mask={layer.mask}
                      disabled={locked}
                      editor={
                        gradientContext?.layerId === layer.id &&
                        gradientContext.mask
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
                  ) : layer.mask.kind === "color" ? (
                    <>
                      <ColorField
                        label="Selected colour"
                        ariaLabel="Colour mask colour"
                        disabled={locked}
                        value={layer.mask.color}
                        onChange={(color) => onMask({ color })}
                      />
                      <Slider
                        label="Tolerance"
                        ariaLabel="Colour mask tolerance"
                        disabled={locked}
                        percentage
                        value={layer.mask.tolerance}
                        onChange={(tolerance) => onMask({ tolerance })}
                      />
                      <Slider
                        label="Softness"
                        ariaLabel="Colour mask softness"
                        disabled={locked}
                        percentage
                        value={layer.mask.softness}
                        onChange={(softness) => onMask({ softness })}
                      />
                      <ColourMaskPreview
                        layer={layer}
                        sourceLayer={
                          layer.kind === "folder"
                            ? descendants.find((l) =>
                                l.channels.some(
                                  (id) =>
                                    l.channelSettings[id]?.texture ||
                                    l.channelSettings[id]?.generator?.result,
                                ),
                              ) || layer
                            : layer
                        }
                      />
                    </>
                  ) : (
                    <>
                      <div className="tp-mask-preview">
                        <MaskIcon mask={layer.mask} size={62} />
                        <div>
                          <strong>
                            {(layer.mask.fill === 1) !== layer.mask.inverted
                              ? "White"
                              : "Black"}
                          </strong>
                          <small>
                            {layer.mask.inverted
                              ? "Inverted fill"
                              : "Uniform fill"}{" "}
                            · source setting
                          </small>
                        </div>
                      </div>
                      <label className="tp-field-row">
                        <span>Fill</span>
                        <select
                          aria-label="Mask fill"
                          value={layer.mask.fill}
                          disabled={locked}
                          onChange={(e) =>
                            onMask({ fill: Number(e.target.value) })
                          }
                        >
                          <option value={1}>White</option>
                          <option value={0}>Black</option>
                        </select>
                      </label>
                    </>
                  )}
                  <label className="tp-toggle-row">
                    <span>Mask enabled</span>
                    <input
                      type="checkbox"
                      aria-label="Mask enabled"
                      disabled={locked}
                      checked={layer.mask.enabled}
                      onChange={(e) => onMask({ enabled: e.target.checked })}
                    />
                  </label>
                  <label className="tp-toggle-row">
                    <span>Invert mask</span>
                    <input
                      type="checkbox"
                      aria-label="Invert mask"
                      disabled={locked}
                      checked={layer.mask.inverted}
                      onChange={(e) => onMask({ inverted: e.target.checked })}
                    />
                  </label>
                  <Amount
                    label="Mask strength"
                    value={layer.mask.strength}
                    disabled={locked}
                    onChange={(strength) => onMask({ strength })}
                  />
                  <p className="tp-card-note">
                    {layer.kind === "folder"
                      ? "Saved on this folder for its descendants. "
                      : "Saved on this layer. "}
                    {layer.mask.kind === "color"
                      ? "Colour matching uses normalized sRGB RGB distance; the preview evaluates source pixels only. "
                      : "White includes and black excludes. "}
                    These masks do not composite the teapot yet.
                  </p>
                </section>
                <div className="tp-inspector-mask-actions">
                  <button disabled={locked} onClick={onRemoveMask}>
                    <Trash2 size={13} /> Remove layer mask
                  </button>
                  <button onClick={onSelectLayer}>Back to layer</button>
                </div>
              </>
            ) : (
              <>
                <div className="tp-section-label">
                  <span>LAYER PROPERTIES</span>
                  <small>Authoring setup</small>
                </div>
                <section className="tp-property-card">
                  <h2>
                    <i />
                    Composition <small>Setup only</small>
                  </h2>
                  <label className="tp-name-field">
                    <span>Name</span>
                    <NameField
                      key={layer.id}
                      value={layer.name}
                      disabled={locked}
                      onChange={(name) => onPatch({ name })}
                    />
                  </label>
                  <label className="tp-field-row">
                    <span>Folder</span>
                    <select
                      aria-label="Layer folder"
                      value={layer.parentId || ""}
                      disabled={locked || groupBlocked}
                      onChange={(e) => onFolderChange(e.target.value || null)}
                    >
                      <option value="">Root / ungrouped</option>
                      {doc.layers
                        .filter(
                          (l) => l.kind === "folder" && !subtreeIds.has(l.id),
                        )
                        .map((l) => (
                          <option
                            key={l.id}
                            value={l.id}
                            disabled={isTextureLayerLocked(doc, l.id)}
                          >
                            {"— ".repeat(textureAncestors(doc, l.id).length)}
                            {l.name}
                          </option>
                        ))}
                    </select>
                  </label>
                  {layer.kind === "folder" && (
                    <div className="tp-folder-summary">
                      <span>{descendants.length - 1} descendant layers</span>
                      <button
                        disabled={locked || groupBlocked}
                        onClick={onUngroup}
                      >
                        Ungroup folder
                      </button>
                    </div>
                  )}
                  <label className="tp-field-row">
                    <span>Blend mode</span>
                    <select
                      aria-label="Layer blend mode"
                      value={layer.blend}
                      disabled={locked}
                      onChange={(e) => onPatch({ blend: e.target.value })}
                    >
                      {TEXTURE_BLENDS.map((v) => (
                        <option key={v}>{v}</option>
                      ))}
                    </select>
                  </label>
                  <Amount
                    label="Layer opacity"
                    value={layer.opacity}
                    disabled={locked}
                    onChange={(opacity) => onPatch({ opacity })}
                  />
                  <p className="tp-card-note">
                    Decal opacity and inherited coverage affect the real surface
                    preview. Source strokes and blend modes are not a full
                    UV-stack compositor.
                  </p>
                </section>
                {layer.kind !== "folder" && decalContext && (
                  <DecalComponentsPanel
                    key={layer.id}
                    layer={layer}
                    doc={doc}
                    locked={locked}
                    context={decalContext}
                  />
                )}
                {!["folder", "decal"].includes(layer.kind) && (
                  <>
                    <div className="tp-section-label">
                      <span>CHANNEL TARGETS</span>
                      <small>{layer.channels.length} enabled</small>
                    </div>
                    <ChannelPropertyPanel
                      key={layer.id}
                      layer={layer}
                      disabled={locked}
                      painting={painting}
                      materialWorkspace={materialWorkspace}
                      gradientContext={gradientContext}
                      onGradientActivate={onGradientActivate}
                      onStroke={onStroke}
                      onOpenTools={onOpenTools}
                      onPatch={onPatch}
                    />
                  </>
                )}
                <section className="tp-property-card tp-source-card">
                  <h2>
                    <i className="warm" />
                    {layer.kind === "folder"
                      ? "Folder coverage"
                      : "Layer source"}{" "}
                    <small>Source setup</small>
                  </h2>
                  <div className="tp-source-summary">
                    <LayerThumbnail layer={layer} thumbs={thumbs} size={36} />
                    <div>
                      <strong>{TEXTURE_KINDS[layer.kind].label}</strong>
                      <p>
                        {layer.sourceAsset
                          ? `Assigned ${layer.sourceAsset.type}: ${layer.sourceAsset.name}`
                          : TEXTURE_KINDS[layer.kind].description}
                      </p>
                    </div>
                  </div>
                  {layer.mask ? (
                    <button
                      className="tp-inspect-mask"
                      onClick={() => onMask(null)}
                    >
                      <MaskIcon mask={layer.mask} size={20} /> Inspect layer
                      mask <span>↗</span>
                    </button>
                  ) : (
                    <button
                      className="tp-inspect-mask"
                      disabled={locked}
                      onClick={() =>
                        onMask({
                          enabled: true,
                          fill: 1,
                          inverted: false,
                          strength: 100,
                        })
                      }
                    >
                      <span className="tp-mask-tool" /> Add layer mask{" "}
                      <span>+</span>
                    </button>
                  )}
                  {!layer.mask && (
                    <button
                      className="tp-inspect-mask tp-add-colour-mask"
                      disabled={locked}
                      onClick={() => onMask({ kind: "color" })}
                    >
                      <span className="tp-colour-mask-dot" /> Add colour mask{" "}
                      <span>+</span>
                    </button>
                  )}
                </section>
              </>
            )}
            <div className="tp-stage-note">
              <CircleHelp size={13} />
              <p>
                Real surface-projected decals and selected fill / mask previews.
                2D source strokes remain separate; whole-stack UV texture-atlas
                compositing is not active.
              </p>
            </div>
          </>
        )}
      </div>
      <footer className="tp-inspector-footer">
        <span>
          <span className="tp-little-dot" /> Layer sources
        </span>
        <small>
          {layer ? (locked ? "Read only" : "Editable") : "No selection"}
        </small>
      </footer>
    </>
  );
}
