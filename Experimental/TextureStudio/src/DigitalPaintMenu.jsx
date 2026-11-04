import React, { useEffect, useRef, useState } from "react";
import {
  Brush,
  Eraser,
  PaintBucket,
  X,
  RotateCcw,
  SlidersHorizontal,
  Layers3,
  Palette,
  ChevronDown,
  Check,
  ArrowRight,
  Circle,
  Image,
  Sparkles,
  Trash2,
  Upload,
} from "lucide-react";
import useDialogFocus from "./useDialogFocus.js";
import { Slider } from "./MaterialControls.jsx";
import ColourPicker from "./ColourPicker.jsx";
import {
  DIGITAL_BRUSH_PRESETS,
  selectDigitalBrush,
  digitalBrushDabs,
  drawDigitalDabs,
  digitalPreviewPoints,
} from "./digitalBrush.js";
import {
  PAINT_CHANNELS,
  CHANNEL_GROUPS,
  CHANNEL_GENERATORS,
  togglePaintChannel,
  normalizeChannelSettings,
  normalizeGenerator,
} from "./paintChannelModel.js";
import { MATERIAL_GENERATOR_ID } from "./materialGeneratorModel.js";
import { importTextureSource } from "./textureSource.js";
import PaintToolMenu from "./PaintToolMenu.jsx";
import "./channelPropertyPanel.css";
import "./digitalPainting.css";

const tools = [
  ["paint", "Paint", Brush],
  ["erase", "Erase", Eraser],
  ["fill", "Fill", PaintBucket],
];
const SOURCE_MODES = [
  ["value", "Value", Circle],
  ["texture", "Texture", Image],
  ["generator", "Generator", Sparkles],
];
export function DigitalStrokePreview({ brush, mask = false, ink = 1 }) {
  const ref = useRef(null),
    path = useRef(null),
    gesture = useRef(null),
    [revision, setRevision] = useState(0);
  function repaint(points = path.current || digitalPreviewPoints()) {
    const c = ref.current;
    if (!c) return;
    const ctx = c.getContext("2d");
    ctx.clearRect(0, 0, c.width, c.height);
    const mark = document.createElement("canvas");
    mark.width = c.width;
    mark.height = c.height;
    const color = mask ? (ink ? "#ffffff" : "#000000") : brush.color;
    const settings = { ...brush, color, tool: "paint" };
    if (brush.tool === "fill") {
      const m = mark.getContext("2d");
      m.fillStyle = color;
      m.fillRect(120, 45, 740, 120);
    } else
      drawDigitalDabs(
        mark.getContext("2d"),
        digitalBrushDabs(points, settings, 41, 256, color, !mask),
        settings,
      );
    if (brush.tool === "erase") {
      ctx.fillStyle = "#686b67";
      ctx.fillRect(0, 0, c.width, c.height);
      ctx.globalCompositeOperation = "destination-out";
    }
    ctx.globalAlpha = brush.opacity;
    ctx.drawImage(mark, 0, 0);
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = "source-over";
    const pixels = ctx.getImageData(0, 0, c.width, c.height).data;
    let hash = 2166136261;
    for (let i = 0; i < pixels.length; i += 101)
      hash = Math.imul(hash ^ pixels[i], 16777619);
    c.dataset.previewHash = String(hash >>> 0);
  }
  useEffect(() => {
    repaint();
  }, [brush, mask, ink, revision]);
  function point(e) {
    const c = ref.current,
      r = c.getBoundingClientRect();
    return [
      ((e.clientX - r.left) * c.width) / r.width / 256,
      ((e.clientY - r.top) * c.height) / r.height / 256,
      e.pointerType === "mouse" ? 1 : Math.max(0.02, e.pressure || 1),
    ];
  }
  return (
    <section
      className="digital-stroke-preview"
      aria-label="Live digital stroke preview"
    >
      <header>
        <span>
          <Brush size={13} />{" "}
          {brush.tool === "fill" ? "Live fill preview" : "Stroke preview"}
        </span>
        <small>Same digital engine · draw here</small>
        <button
          aria-label="Reset digital stroke preview"
          onClick={() => {
            path.current = null;
            setRevision((n) => n + 1);
          }}
        >
          <RotateCcw size={12} />
        </button>
      </header>
      <canvas
        ref={ref}
        width="1024"
        height="210"
        tabIndex={0}
        aria-label="Digital brush stroke preview canvas"
        onPointerDown={(e) => {
          if (e.button !== 0 || gesture.current != null) return;
          e.preventDefault();
          gesture.current = e.pointerId;
          path.current = [point(e)];
          e.currentTarget.setPointerCapture(e.pointerId);
          repaint();
        }}
        onPointerMove={(e) => {
          if (gesture.current !== e.pointerId) return;
          if (path.current.length < 512) path.current.push(point(e));
          repaint();
        }}
        onPointerUp={(e) => {
          if (gesture.current !== e.pointerId) return;
          gesture.current = null;
          if (e.currentTarget.hasPointerCapture(e.pointerId))
            e.currentTarget.releasePointerCapture(e.pointerId);
        }}
        onPointerCancel={() => {
          gesture.current = null;
          path.current = null;
          setRevision((n) => n + 1);
        }}
        onLostPointerCapture={() => {
          gesture.current = null;
        }}
        onKeyDown={(e) => {
          if (e.key === "Enter" || e.key === " ") {
            e.preventDefault();
            path.current = null;
            setRevision((n) => n + 1);
          }
        }}
      />
      <p>
        {brush.tool === "erase"
          ? "Erase removes authored pixels and reveals the layer below; masks restore their original source."
          : brush.tool === "fill"
            ? "Fill uses channel values and opacity. Click a face, island or mesh component."
            : "Pressure, taper, shape, grain and colour dynamics are rendered, not illustrated."}
      </p>
    </section>
  );
}
function BrushCard({ title, icon: Icon = SlidersHorizontal, children, note }) {
  return (
    <section
      className="tp-property-card digital-brush-card"
      aria-label={`${title} properties`}
    >
      <h2>
        <Icon size={13} />
        {title}
      </h2>
      <div className="tp-shared-controls">{children}</div>
      {note && <p className="digital-card-note">{note}</p>}
    </section>
  );
}
export function DigitalBrushProperties({ brush, onChange, mask = false }) {
  const set = (key, value) => onChange({ ...brush, [key]: value });
  const slider = (key, label, max = 1, min = 0, unit = "") => (
    <Slider
      key={key}
      label={label}
      ariaLabel={`Digital brush ${label}`}
      value={brush[key]}
      onChange={(value) => set(key, value)}
      min={min}
      max={max}
      step={max > 2 ? 1 : 0.01}
      percentage={max === 1 && min === 0}
      unit={unit}
      disabled={
        brush.tool === "fill" && !["opacity", "heightStrength"].includes(key)
      }
    />
  );
  return (
    <div className="digital-brush-cards">
      <BrushCard
        title="Stroke path"
        note="Size is the diameter in screen (CSS) pixels. The tip stays circular at every zoom and projects across UV seams. Zero size or opacity paints nothing."
      >
        {slider("size", "Size", 128, 0, "px")}
        {slider("opacity", "Opacity")}
        {slider("flow", "Flow")}
        {slider("spacing", "Spacing", 2, 0.05)}
        {slider("stabilization", "Stabilization")}
        {slider("taperStart", "Start taper")}
        {slider("taperEnd", "End taper")}
      </BrushCard>
      <BrushCard title="Shape">
        <label className="digital-select">
          Tip shape
          <select
            aria-label="Digital brush tip shape"
            value={brush.shape}
            disabled={brush.tool === "fill"}
            onChange={(e) => set("shape", e.target.value)}
          >
            <option value="round">Round / ellipse</option>
            <option value="square">Square</option>
          </select>
          <ChevronDown size={11} />
        </label>
        {slider("hardness", "Hardness")}
        {slider("roundness", "Roundness")}
        {slider("rotation", "Rotation", 360, 0, "°")}
        {slider("rotationJitter", "Rotation jitter")}
        <label className="digital-switch">
          <input
            type="checkbox"
            aria-label="Digital brush follow stroke"
            checked={brush.follow}
            disabled={brush.tool === "fill"}
            onChange={(e) => set("follow", e.target.checked)}
          />
          Follow stroke direction
        </label>
      </BrushCard>
      <BrushCard
        title="Grain"
        note="Procedural digital tip texture. No paper or physical-media simulation."
      >
        <label className="digital-select">
          Texture
          <select
            aria-label="Digital brush grain texture"
            value={brush.grain}
            disabled={brush.tool === "fill"}
            onChange={(e) => set("grain", e.target.value)}
          >
            <option value="none">None</option>
            <option value="noise">Noise</option>
            <option value="lines">Lines</option>
            <option value="checker">Checker</option>
          </select>
          <ChevronDown size={11} />
        </label>
        {slider("grainAmount", "Grain amount")}
        {slider("grainScale", "Grain scale", 32, 1, "px")}
      </BrushCard>
      <BrushCard title="Stroke dynamics">
        {slider("sizeJitter", "Size jitter")}
        {slider("opacityJitter", "Opacity jitter")}
        {slider("scatter", "Scatter")}
      </BrushCard>
      <BrushCard
        title="Colour dynamics"
        icon={Palette}
        note={
          mask
            ? "Masks use grayscale ink. Colour dynamics apply to Base color painting only."
            : "Seeded hue, saturation and brightness variations are saved with each stroke."
        }
      >
        {[
          ["strokeHue", "Stroke hue"],
          ["strokeSaturation", "Stroke saturation"],
          ["strokeBrightness", "Stroke brightness"],
          ["dabHue", "Dab hue"],
          ["dabSaturation", "Dab saturation"],
          ["dabBrightness", "Dab brightness"],
          ["pressureBrightness", "Pressure brightness"],
        ].map(([key, label]) => slider(key, label))}
      </BrushCard>
      <BrushCard
        title="Pressure and surface"
        note="Mouse uses full pressure; pens and touch use PointerEvent pressure. Normal is derived from painted Height, composed with baked detail."
      >
        {slider("sizePressure", "Pressure size")}
        {slider("opacityPressure", "Pressure opacity")}
        {slider("heightStrength", "Height to normal", 2)}
      </BrushCard>
    </div>
  );
}
function DigitalChannelProperties({
  layer,
  brush,
  mask,
  ink,
  onInk,
  onChange,
  onPatch,
  disabled,
}) {
  const [opened, setOpened] = useState("baseColor"),
    [importing, setImporting] = useState(null),
    [error, setError] = useState("");
  const file = useRef(null),
    importChannel = useRef(null),
    alive = useRef(true),
    current = useRef({ layer, disabled, onPatch });
  current.current = { layer, disabled, onPatch };
  useEffect(() => {
    alive.current = true;
    return () => {
      alive.current = false;
    };
  }, []);
  if (!layer)
    return (
      <div className="digital-target-empty">
        Select a layer or mask to paint.
      </div>
    );
  if (mask)
    return (
      <section
        className="digital-channel-panel"
        aria-label="Digital mask target"
      >
        <h2>
          <Layers3 size={13} /> Mask coverage
        </h2>
        <p>
          Paint changes grayscale coverage. Erase removes the painted overlay,
          preserving the original mask source.
        </p>
        <div className="digital-mask-inks">
          {[
            [1, "White", "#ffffff"],
            [0, "Black", "#000000"],
          ].map(([value, label, color]) => (
            <button
              key={value}
              aria-label={`${label} digital mask ink`}
              aria-pressed={ink === value}
              disabled={disabled}
              onClick={() => onInk(value)}
            >
              <i style={{ background: color }} />
              {label}
            </button>
          ))}
        </div>
        <p>
          Layer colour channels are not changed when painting a mask. Existing
          face / island / object selections constrain the stroke.
        </p>
      </section>
    );
  const enabled = layer.channels,
    settings = normalizeChannelSettings(layer.channelSettings);
  const toggle = (id, on) =>
    onPatch({ channels: togglePaintChannel(enabled, id, on) });
  // 📝 Same channelSettings writes as the layer inspector's ChannelPropertyPanel:
  // source mode, texture and generator are the layer's own authoring targets.
  const change = (id, patch, field) =>
    onPatch(
      { channelSettings: { ...settings, [id]: { ...settings[id], ...patch } } },
      {
        discrete: !field,
        key: field ? `channel-${layer.id}-${id}-${field}` : undefined,
      },
    );
  const setValue = (id, value) => {
    if (id === "baseColor") onChange({ ...brush, color: value });
    else change(id, { value }, "value");
  };
  const valueOf = (c) =>
    c.id === "baseColor" ? brush.color : settings[c.id].value;
  async function readTexture(e) {
    const input = e.target.files[0],
      id = importChannel.current;
    e.target.value = "";
    if (!input || !id || disabled) return;
    setImporting(id);
    setError("");
    try {
      const texture = await importTextureSource(input);
      if (!alive.current || current.current.disabled) return;
      const latest = normalizeChannelSettings(
        current.current.layer.channelSettings,
      );
      if (
        latest[id].mode !== "texture" ||
        !current.current.layer.channels.includes(id)
      )
        return;
      current.current.onPatch(
        {
          channelSettings: {
            ...latest,
            [id]: { ...latest[id], mode: "texture", texture },
          },
        },
        { discrete: true },
      );
    } catch (err) {
      if (alive.current) setError(err.message);
    } finally {
      if (alive.current) setImporting(null);
    }
  }
  // 🧩 Same channel-property frame as the layer inspector: a targets card
  // (chips + grouped checklist) over one accordion card per enabled channel,
  // each with its Source (Value | Texture | Generator) and the value a stroke
  // deposits (digitalChannelValues).
  return (
    <section
      className="digital-channel-panel channel-property-panel"
      aria-label="Digital painting channels"
    >
      <input
        ref={file}
        type="file"
        hidden
        accept="image/png,image/jpeg,image/webp"
        aria-label="Import digital channel texture file"
        onChange={readTexture}
      />
      <div className="channel-target-card tp-property-card">
        <div className="channel-target-heading">
          <Layers3 size={14} />
          <h2>Channel targets</h2>
          <span className="channel-count">{enabled.length}</span>
        </div>
        <p className="digital-channel-intro">
          Enable the channels this brush writes. Erase only affects enabled
          channels. Disabled values and strokes are retained.
        </p>
        <div className="channel-chips">
          {PAINT_CHANNELS.filter((c) => enabled.includes(c.id)).map((c) => (
            <span className="channel-chip" key={c.id}>
              <i style={{ background: c.color }} />
              {c.label}
              <button
                aria-label={`Disable ${c.label} channel`}
                disabled={disabled}
                onClick={() => toggle(c.id, false)}
              >
                <X size={10} />
              </button>
            </span>
          ))}
        </div>
        <div
          className="channel-picker"
          role="group"
          aria-label="Available painting channels"
        >
          {CHANNEL_GROUPS.map((group) => (
            <section key={group}>
              <h3>{group}</h3>
              {PAINT_CHANNELS.filter((c) => c.group === group).map((c) => (
                <label key={c.id}>
                  <i style={{ background: c.color }} />
                  <span>{c.label}</span>
                  <input
                    type="checkbox"
                    aria-label={`Digital ${c.label} channel`}
                    checked={enabled.includes(c.id)}
                    disabled={disabled}
                    onChange={(e) => toggle(c.id, e.target.checked)}
                  />
                </label>
              ))}
            </section>
          ))}
        </div>
        <button
          className="digital-clear-channels"
          disabled={disabled || !enabled.length}
          onClick={() => onPatch({ channels: [] })}
        >
          Disable all channels
        </button>
        {!enabled.length && (
          <p role="status">No channels enabled — painting is a no-op.</p>
        )}
      </div>
      {error && (
        <p className="channel-import-error" role="alert">
          {error}
          <button
            aria-label="Dismiss texture import error"
            onClick={() => setError("")}
          >
            <X size={12} />
          </button>
        </p>
      )}
      {PAINT_CHANNELS.filter((c) => enabled.includes(c.id)).map((c) => {
        const isOpen = opened === c.id,
          setting = settings[c.id],
          value = valueOf(c),
          generator = CHANNEL_GENERATORS.find(
            (g) => g.id === setting.generator?.id,
          );
        return (
          <section
            key={c.id}
            className={`chan-panel tp-property-card ${isOpen ? "" : "collapsed"}`}
            style={{ "--channel-tint": c.color }}
          >
            <button
              className="chan-head"
              aria-label={`Digital ${c.label} properties`}
              aria-expanded={isOpen}
              disabled={disabled}
              onClick={() => setOpened(isOpen ? null : c.id)}
            >
              <i />
              <span>{c.label}</span>
              <small>
                {setting.mode === "derived"
                  ? "Height-derived"
                  : setting.mode === "gradient"
                    ? "Gradient"
                    : SOURCE_MODES.find((m) => m[0] === setting.mode)?.[1]}
              </small>
              <ChevronDown size={12} />
            </button>
            {isOpen && (
              <div className="chan-body">
                {c.edit === "derived" ? (
                  <p className="chan-note">
                    Derived from painted Height. Enabling Normal also enables
                    Height.
                  </p>
                ) : (
                  <>
                    <div
                      className="channel-mode"
                      role="group"
                      aria-label={`Digital ${c.label} source`}
                    >
                      {SOURCE_MODES.map(([mode, label, Icon]) => (
                        <button
                          key={mode}
                          disabled={disabled}
                          aria-pressed={setting.mode === mode}
                          onClick={() => change(c.id, { mode })}
                        >
                          <Icon size={11} />
                          {label}
                        </button>
                      ))}
                    </div>
                    {setting.mode === "gradient" ? (
                      <p className="chan-note">
                        Gradient source — edit its points in the layer
                        inspector. Strokes deposit the channel Value below.
                      </p>
                    ) : setting.mode === "texture" ? (
                      <div className="channel-texture-source">
                        {setting.texture ? (
                          <>
                            <div className="channel-source-preview">
                              <img
                                src={setting.texture.dataUrl}
                                alt={`${c.label} imported texture`}
                              />
                              <div>
                                <strong>{setting.texture.name}</strong>
                                <small>
                                  {setting.texture.width} ×{" "}
                                  {setting.texture.height} · stored source
                                </small>
                              </div>
                            </div>
                            <button
                              className="channel-source-remove"
                              disabled={disabled}
                              onClick={() => change(c.id, { texture: null })}
                            >
                              <Trash2 size={12} />
                              Remove texture
                            </button>
                          </>
                        ) : (
                          <div className="channel-texture-empty">
                            <Image size={23} />
                            <span>Import a texture source</span>
                          </div>
                        )}
                        <button
                          className="channel-import-button"
                          disabled={disabled || !!importing}
                          onClick={() => {
                            importChannel.current = c.id;
                            file.current.click();
                          }}
                        >
                          <Upload size={12} />
                          {importing === c.id
                            ? "Importing…"
                            : setting.texture
                              ? "Replace texture"
                              : "Import texture"}
                        </button>
                        <p className="chan-note">
                          PNG, JPEG or WebP · portable source up to 256 px.
                          Strokes deposit the channel Value below.
                        </p>
                      </div>
                    ) : setting.mode === "generator" ? (
                      <>
                        <label className="channel-generator-label">
                          <span>Generator</span>
                          <select
                            aria-label={`Digital ${c.label} generator`}
                            value={setting.generator?.id || ""}
                            disabled={disabled}
                            onChange={(e) =>
                              change(c.id, {
                                generator: normalizeGenerator({
                                  id: e.target.value,
                                }),
                              })
                            }
                          >
                            <option value="">Choose a generator…</option>
                            {setting.generator?.id ===
                              MATERIAL_GENERATOR_ID && (
                              <option value={MATERIAL_GENERATOR_ID} disabled>
                                Material studio · custom source
                              </option>
                            )}
                            {["Mask", "Wear", "Procedural"].map((group) => (
                              <optgroup label={group} key={group}>
                                {CHANNEL_GENERATORS.filter(
                                  (g) => g.group === group,
                                ).map((g) => (
                                  <option key={g.id} value={g.id}>
                                    {g.label}
                                  </option>
                                ))}
                              </optgroup>
                            ))}
                          </select>
                        </label>
                        {generator ? (
                          <>
                            <p className="chan-note">
                              {generator.note} · source configuration. Strokes
                              deposit the channel Value below.
                            </p>
                            {generator.parameters.map((p) => (
                              <Slider
                                key={p.key}
                                label={p.label}
                                ariaLabel={`Digital ${c.label} ${generator.label} ${p.label}`}
                                value={setting.generator.parameters[p.key]}
                                disabled={disabled}
                                onChange={(v) =>
                                  change(
                                    c.id,
                                    {
                                      generator: {
                                        ...setting.generator,
                                        parameters: {
                                          ...setting.generator.parameters,
                                          [p.key]: v,
                                        },
                                      },
                                    },
                                    `generator-${generator.id}-${p.key}`,
                                  )
                                }
                              />
                            ))}
                          </>
                        ) : (
                          setting.generator?.id ===
                            MATERIAL_GENERATOR_ID && (
                            <p className="chan-note">
                              Material studio · custom source — edit it in the
                              layer inspector. Strokes deposit the channel
                              Value below.
                            </p>
                          )
                        )}
                      </>
                    ) : null}
                    <div className="digital-channel-value tp-shared-controls">
                      {c.edit === "color" ? (
                        <ColourPicker
                          value={value}
                          ariaLabel={`Digital ${c.label} colour`}
                          disabled={disabled}
                          onChange={(v) => setValue(c.id, v)}
                        />
                      ) : (
                        <Slider
                          label="Amount"
                          ariaLabel={`Digital ${c.label} target`}
                          value={value}
                          disabled={disabled}
                          onChange={(v) => setValue(c.id, v)}
                          min={c.min}
                          max={c.max}
                          step={c.max > 3 ? 1 : 0.01}
                          unit={c.unit || ""}
                        />
                      )}
                    </div>
                  </>
                )}
                <div className="channel-value-preview">
                  <i
                    style={{
                      backgroundImage: "none",
                      backgroundColor:
                        c.edit === "color"
                          ? value
                          : c.edit === "scalar"
                            ? `rgb(${Math.round(((value - c.min) / (c.max - c.min)) * 255)},${Math.round(((value - c.min) / (c.max - c.min)) * 255)},${Math.round(((value - c.min) / (c.max - c.min)) * 255)})`
                            : undefined,
                    }}
                  />
                  <div className="channel-preview-text">
                    <span>Stroke deposit</span>
                    <small>{c.placement}</small>
                  </div>
                </div>
              </div>
            )}
          </section>
        );
      })}
      <p className="digital-card-note">
        Base color uses the brush colour. Other targets use their saved channel
        values. Subsurface is a diffuse tint, not volumetric scattering.
      </p>
    </section>
  );
}
export default function DigitalPaintMenu({
  open,
  initialCatalogue = false,
  onClose,
  painting,
  layer,
  mask,
  ink,
  onInk,
  onPatch,
  disabled,
  legacySettings,
  onLegacyChange,
  onSave,
  onUndo,
  onRedo,
}) {
  const [catalogue, setCatalogue] = useState(false),
    [tab, setTab] = useState("brush"),
    ref = useRef(null);
  useDialogFocus(open && !catalogue, ref);
  useEffect(() => {
    if (open) {
      setCatalogue(initialCatalogue);
      setTab("brush");
    }
  }, [open, initialCatalogue]);
  useEffect(() => {
    if (!open || catalogue) return;
    const key = (e) => {
      const command = e.ctrlKey || e.metaKey,
        letter = e.key.toLowerCase();
      if (command && ["s", "z", "y"].includes(letter)) {
        e.preventDefault();
        e.stopPropagation();
        if (letter === "s") onSave();
        else if (letter === "y" || e.shiftKey) onRedo();
        else onUndo();
      } else if (e.key === "Escape") {
        e.preventDefault();
        e.stopPropagation();
        onClose();
      }
    };
    window.addEventListener("keydown", key, true);
    return () => window.removeEventListener("keydown", key, true);
  }, [open, catalogue, onClose, onSave, onUndo, onRedo]);
  if (!open) return null;
  if (catalogue)
    return (
      <PaintToolMenu
        open
        settings={legacySettings}
        onChange={onLegacyChange}
        onClose={onClose}
        onSave={onSave}
        onUndo={onUndo}
        onRedo={onRedo}
      />
    );
  const brush = painting.brush,
    name = DIGITAL_BRUSH_PRESETS.find((p) => p.id === brush.preset)?.name;
  const useTool = () => {
    painting.arm(brush.tool);
    onClose();
  };
  return (
    <div
      className="paint-menu-backdrop"
      onPointerDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <section
        className="alloy-digital-menu"
        role="dialog"
        aria-modal="true"
        aria-label="Digital brush studio"
        tabIndex={-1}
        ref={ref}
        data-properties-tab={tab}
      >
        <header className="digital-menu-heading">
          <Brush size={17} />
          <div>
            <strong>Digital brush studio</strong>
            <small>
              {mask ? "Mask coverage" : "Layer channels"} ·{" "}
              {layer?.name || "Select a layer"}
            </small>
          </div>
          <span>Digital only</span>
          <button aria-label="Close digital brush studio" onClick={onClose}>
            <X size={17} />
          </button>
        </header>
        <div className="digital-menu-toolbar">
          <div
            className="digital-tool-circles"
            role="group"
            aria-label="Digital painting tools"
          >
            {tools.map(([id, label, Icon]) => (
              <button
                key={id}
                aria-label={`Digital tool ${label}`}
                aria-pressed={brush.tool === id}
                onClick={() =>
                  painting.changeBrush(
                    { ...brush, tool: id },
                    { discrete: true },
                  )
                }
              >
                <i>
                  <Icon size={21} />
                </i>
                <span>{label}</span>
              </button>
            ))}
          </div>
          <div
            className="digital-preset-circles"
            role="group"
            aria-label="Digital brush presets"
          >
            {DIGITAL_BRUSH_PRESETS.map((p) => (
              <button
                key={p.id}
                title={p.note}
                aria-label={`Digital brush ${p.name}`}
                aria-pressed={brush.preset === p.id}
                onClick={() =>
                  painting.changeBrush(selectDigitalBrush(brush, p.id), {
                    discrete: true,
                  })
                }
              >
                <i>
                  <span className={`digital-tip-icon tip-${p.id}`} />
                  <Check size={10} />
                </i>
                <span>{p.name}</span>
              </button>
            ))}
          </div>
        </div>
        <DigitalStrokePreview brush={brush} mask={mask} ink={ink} />
        <nav
          className="digital-property-tabs"
          aria-label="Digital brush property tabs"
        >
          <button
            aria-pressed={tab === "brush"}
            onClick={() => setTab("brush")}
          >
            <SlidersHorizontal size={12} /> Brush properties
          </button>
          <button
            aria-pressed={tab === "channels"}
            onClick={() => setTab("channels")}
          >
            <Layers3 size={12} /> Channel properties
          </button>
        </nav>
        <div className="digital-menu-body">
          <div className="digital-brush-column">
            <div className="digital-brush-summary">
              <div>
                <small>ACTIVE DIGITAL TIP</small>
                <h1>{name}</h1>
              </div>
              <button
                aria-label="Reset digital brush properties"
                onClick={() =>
                  painting.changeBrush(
                    selectDigitalBrush(brush, brush.preset),
                    { discrete: true },
                  )
                }
              >
                <RotateCcw size={12} /> Reset
              </button>
            </div>
            {brush.tool === "fill" && (
              <label className="digital-fill-scope digital-select">
                Fill region
                <select
                  aria-label="Digital fill region"
                  value={brush.fillScope}
                  onChange={(e) =>
                    painting.changeBrush(
                      { ...brush, fillScope: e.target.value },
                      { discrete: true },
                    )
                  }
                >
                  <option value="object">Hit mesh component</option>
                  <option value="island">UV island</option>
                  <option value="face">Face</option>
                  <option value="tile">UV tile (UV view only)</option>
                </select>
                <ChevronDown size={12} />
              </label>
            )}
            <DigitalBrushProperties
              brush={brush}
              onChange={painting.changeBrush}
              mask={mask}
            />
          </div>
          <DigitalChannelProperties
            layer={layer}
            brush={brush}
            mask={mask}
            ink={ink}
            onInk={onInk}
            onChange={painting.changeBrush}
            onPatch={onPatch}
            disabled={disabled}
          />
        </div>
        <footer className="digital-menu-footer">
          <button
            className="digital-catalogue-link"
            onClick={() => setCatalogue(true)}
          >
            Instrument catalogue <small>configuration only</small>
          </button>
          <span>
            {painting.available
              ? mask
                ? "Painting the selected mask"
                : `${layer.channels.length} channel targets · undo per gesture`
              : "Select an unlocked, visible target in Lit or packed UV view"}
          </span>
          <button
            className="digital-use-tool"
            disabled={!painting.available}
            onClick={useTool}
          >
            {brush.tool === "erase" ? (
              <Eraser size={14} />
            ) : brush.tool === "fill" ? (
              <PaintBucket size={14} />
            ) : (
              <Brush size={14} />
            )}{" "}
            Use {brush.tool} <ArrowRight size={12} />
          </button>
        </footer>
      </section>
    </div>
  );
}
export function DigitalPaintToolbar({ painting, onOpen, mask = false }) {
  return (
    <div
      className="digital-viewport-tools"
      role="toolbar"
      aria-label="Digital surface painting"
    >
      {tools.map(([id, label, Icon]) => (
        <button
          key={id}
          aria-label={`${label} ${mask ? "mask digitally" : "layer channels"}`}
          aria-pressed={painting.active && painting.brush.tool === id}
          disabled={!painting.available}
          onClick={() => painting.arm(id)}
        >
          <Icon size={12} />
          {label}
        </button>
      ))}
      <button
        className="digital-brush-settings"
        aria-label="Digital brush settings"
        onClick={onOpen}
      >
        <SlidersHorizontal size={12} />
        {Math.round(painting.brush.size)} px
      </button>
    </div>
  );
}
