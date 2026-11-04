import RecipeInspector from "./RecipeInspector.jsx";
import {
  texturePart,
  normalizeSurfaceMaterial,
} from "./textureSurfaceModel.js";
import NameField from "./AuthoringNameField.jsx";
import DecalComponentsPanel from "./DecalComponentsPanel.jsx";
import { Slider } from "./MaterialControls.jsx";
import TextureMaskPropertyPanel from "./TextureMaskPropertyPanel.jsx";
import ChannelPropertyPanel from "./ChannelPropertyPanel.jsx";
import React from "react";
import {
  Check,
  Brush,
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
  onOpenDigitalTools,
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
                <TextureMaskPropertyPanel
                  layer={layer}
                  locked={locked}
                  onMask={onMask}
                  gradientContext={gradientContext}
                  onGradientActivate={onGradientActivate}
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
                    Composition{" "}
                    <small>
                      {layer.surfaceMaterial
                        ? layer.surfaceTarget
                          ? "Part assignment"
                          : "Stack material"
                        : "Layer setup"}
                    </small>
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
                    Digital strokes, native materials and decals follow stack
                    order, layer opacity and inherited masks. Brush channels
                    evaluate before PBR lighting. Digital colour blends are
                    supported; native material blend modes remain metadata.
                  </p>
                </section>
                {layer.kind !== "folder" && (
                  <section
                    className="tp-property-card"
                    aria-label="Layer digital painting"
                  >
                    <h2>
                      <i className="warm" />
                      Digital painting
                      <small>
                        {layer.paint?.operations.length || 0} segments / fills
                      </small>
                    </h2>
                    <p className="tp-card-note">
                      Paint or erase enabled channels in Lit or packed UV view.
                      Fill a real mesh component, island or face. Digital
                      strokes are portable and undoable.
                    </p>
                    <div className="tp-inspector-mask-actions">
                      <button onClick={onOpenDigitalTools}>
                        <Brush size={13} />
                        Brush studio
                      </button>
                      <button
                        aria-label="Clear layer digital paint"
                        disabled={locked || !layer.paint?.operations.length}
                        onClick={() => onPatch({ paint: null })}
                      >
                        <RotateCcw size={13} />
                        Clear paint
                      </button>
                    </div>
                  </section>
                )}
                {layer.surfaceMaterial && (
                  <section
                    className="tp-property-card tp-part-material-card tp-shared-controls"
                    aria-label={
                      layer.surfaceTarget
                        ? "Assigned part material"
                        : "Assigned stack material"
                    }
                  >
                    <h2>
                      <i className="warm" />
                      {layer.surfaceTarget
                        ? `${texturePart(layer.surfaceTarget.partId).name} only`
                        : "Whole object"}
                      <small>Native PBR</small>
                    </h2>
                    <p className="tp-part-material-summary">
                      {layer.surfaceMaterial.name || layer.sourceAsset?.name} ·
                      {layer.surfaceTarget
                        ? `UDIM ${texturePart(layer.surfaceTarget.partId).tile}`
                        : "All teapot parts"}
                    </p>
                    <p className="tp-card-note">
                      {layer.surfaceTarget
                        ? `This material affects only the ${texturePart(layer.surfaceTarget.partId).name.toLowerCase()} region. Body, lid, handle and spout stay independently assignable.`
                        : "This material covers lower layers on all teapot parts. Move it below a stamp to put that stamp on top. Opacity and masks reveal the layers underneath."}{" "}
                      The material snapshot is saved in this project.
                    </p>
                    <fieldset disabled={locked}>
                      <RecipeInspector
                        key={layer.id}
                        params={layer.surfaceMaterial}
                        update={(key, value) =>
                          onPatch(
                            {
                              surfaceMaterial: normalizeSurfaceMaterial({
                                ...layer.surfaceMaterial,
                                [key]: value,
                              }),
                            },
                            { key: `part-${layer.id}-${key}` },
                          )
                        }
                        setParams={(value) => {
                          const next =
                            typeof value === "function"
                              ? value(layer.surfaceMaterial)
                              : value;
                          onPatch(
                            { surfaceMaterial: normalizeSurfaceMaterial(next) },
                            { key: `part-${layer.id}-properties` },
                          );
                        }}
                      />
                    </fieldset>
                  </section>
                )}
                {layer.kind !== "folder" && decalContext && (
                  <DecalComponentsPanel
                    key={layer.id}
                    layer={layer}
                    doc={doc}
                    locked={locked}
                    context={decalContext}
                  />
                )}
                {layer.kind !== "folder" && (
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
                          paint: null,
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
                Materials and decals evaluate bottom to top. Locked Layer 0
                supplies white fallback and baked AO / normal support. 2D source
                paint is not deposited onto the mesh; this live surface stack is
                not a baked UV atlas.
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
