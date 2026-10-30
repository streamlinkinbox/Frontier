import React, { useEffect, useRef, useState } from "react";
import {
  ChevronDown,
  Circle,
  Image,
  Layers3,
  Plus,
  Sparkles,
  Trash2,
  Upload,
  X,
} from "lucide-react";
import { Slider, ColorField } from "./MaterialControls.jsx";
import {
  PAINT_CHANNELS,
  CHANNEL_GENERATORS,
  CHANNEL_GROUPS,
  togglePaintChannel,
  normalizeGenerator,
} from "./paintChannelModel.js";
import { MATERIAL_GENERATOR_ID } from "./materialGeneratorModel.js";
import { importTextureSource } from "./textureSource.js";
import "./channelPropertyPanel.css";
import "./channelTargetsPanel.css";

const SOURCE_MODES = [
  ["value", "Value", Circle],
  ["texture", "Texture", Image],
  ["generator", "Generator", Sparkles],
];
// 🧩 The ChannelPropertyPanel frame for flat channel targets (decal components,
// the stamp brush): a targets card with removable chips for the enabled
// channels and the same Add-channel grouped checklist dropdown as the layer
// inspector (uncheck or × returns a channel to the dropdown), then one
// card per enabled channel carrying the same Source (Value | Texture |
// Generator) choice as the layer inspector, its Colour / Amount value and a
// preview row. Values are a flat map (normalizeStampValues); sources are the
// sparse normalizeStampChannelSources map. Stamps deposit the channel Value —
// Texture/Generator entries are saved authoring targets.
export default function ChannelTargetsPanel({
  prefix = "Decal",
  heading = "Channel targets",
  channels,
  values,
  sources = {},
  onChannels,
  onValue,
  onSource,
  disabled = false,
  artworkBaseColor = false,
  // Optional replacement for the Base color editor: the stamp studio puts the
  // selected vector's colour-only paint (fill/stroke sources) on this card.
  baseColorBody = null,
}) {
  const [picker, setPicker] = useState(false);
  const [folded, setFolded] = useState(new Set()),
    [importing, setImporting] = useState(null),
    [error, setError] = useState("");
  const file = useRef(null),
    channel = useRef(null),
    alive = useRef(true),
    current = useRef({ disabled, onSource });
  current.current = { disabled, onSource };
  useEffect(() => {
    alive.current = true;
    return () => {
      alive.current = false;
    };
  }, []);
  const toggle = (id, on) => onChannels(togglePaintChannel(channels, id, on));
  async function readTexture(e) {
    const input = e.target.files[0],
      id = channel.current;
    e.target.value = "";
    if (!input || !id || disabled) return;
    setImporting(id);
    setError("");
    try {
      const texture = await importTextureSource(input);
      if (!alive.current || current.current.disabled) return;
      current.current.onSource(id, { mode: "texture", texture });
    } catch (err) {
      if (alive.current) setError(err.message);
    } finally {
      if (alive.current) setImporting(null);
    }
  }
  return (
    <section
      className="channel-property-panel channel-targets-panel"
      aria-label={`${prefix} channel property card`}
    >
      <input
        ref={file}
        type="file"
        hidden
        accept="image/png,image/jpeg,image/webp"
        aria-label={`Import ${prefix} channel texture file`}
        onChange={readTexture}
      />
      <div className="channel-target-card tp-property-card">
        <div className="channel-target-heading">
          <Layers3 size={14} />
          <h2>{heading}</h2>
          <span className="channel-count">{channels.length}</span>
          <button
            aria-label={`Add ${prefix} channel`}
            aria-expanded={picker}
            disabled={disabled}
            onClick={() => setPicker(!picker)}
          >
            <Plus size={14} />
          </button>
        </div>
        <div className="channel-chips">
          {PAINT_CHANNELS.filter((c) => channels.includes(c.id)).map((c) => (
            <span className="channel-chip" key={c.id}>
              <i style={{ background: c.color }} />
              {c.label}
              <button
                aria-label={`Disable ${prefix} ${c.label} channel`}
                disabled={disabled}
                onClick={() => toggle(c.id, false)}
              >
                <X size={10} />
              </button>
            </span>
          ))}
        </div>
        {!!channels.length && (
          <button
            className="channel-clear"
            disabled={disabled}
            onClick={() => onChannels([])}
          >
            Clear all
          </button>
        )}
        {picker && (
          <div
            className="channel-picker"
            role="group"
            aria-label={`Available ${prefix} channels`}
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
                      aria-label={`${prefix} ${c.label} channel`}
                      checked={channels.includes(c.id)}
                      disabled={disabled}
                      onChange={(e) => toggle(c.id, e.target.checked)}
                    />
                  </label>
                ))}
              </section>
            ))}
          </div>
        )}
        {!channels.length && (
          <p className="chan-note" role="status">
            No channels enabled. Existing channel values are retained.
          </p>
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
      {PAINT_CHANNELS.filter((c) => channels.includes(c.id)).map((c) => {
        const isFolded = folded.has(c.id),
          value = values[c.id],
          source = sources[c.id] || {
            mode: "value",
            generator: null,
            texture: null,
          },
          generator = CHANNEL_GENERATORS.find(
            (g) => g.id === source.generator?.id,
          );
        const valueEditor =
          c.id === "baseColor" && baseColorBody ? (
            baseColorBody
          ) : artworkBaseColor && c.id === "baseColor" ? (
            <p className="chan-note">
              Artwork colour. The stamp raster supplies Base color; disable
              the channel to stamp values only.
            </p>
          ) : c.edit === "color" ? (
            <ColorField
              label="Colour"
              ariaLabel={`${prefix} ${c.label}`}
              value={value}
              disabled={disabled}
              onChange={(v) => onValue(c.id, v)}
            />
          ) : (
            <Slider
              label="Amount"
              ariaLabel={`${prefix} ${c.label}`}
              value={value}
              min={c.min}
              max={c.max}
              step={c.max > 3 ? 1 : 0.01}
              unit={c.unit || ""}
              disabled={disabled}
              onChange={(v) => onValue(c.id, v)}
            />
          );
        return (
          <section
            key={c.id}
            className={`chan-panel tp-property-card ${isFolded ? "collapsed" : ""}`}
            style={{ "--channel-tint": c.color }}
          >
            <button
              className="chan-head"
              aria-label={`${prefix} ${c.label} properties`}
              aria-expanded={!isFolded}
              onClick={() =>
                setFolded((old) => {
                  const next = new Set(old);
                  next.has(c.id) ? next.delete(c.id) : next.add(c.id);
                  return next;
                })
              }
            >
              <i />
              <span>{c.label}</span>
              <small>
                {c.edit === "derived"
                  ? "Derived"
                  : SOURCE_MODES.find((m) => m[0] === source.mode)?.[1]}
              </small>
              <ChevronDown size={12} />
            </button>
            {!isFolded && (
              <div className="chan-body">
                {c.edit === "derived" ? (
                  <p className="chan-note">
                    Derived from the stamped Height. No value to author.
                  </p>
                ) : (
                  <>
                    <div
                      className="channel-mode"
                      role="group"
                      aria-label={`${prefix} ${c.label} source`}
                    >
                      {SOURCE_MODES.map(([mode, label, Icon]) => (
                        <button
                          key={mode}
                          disabled={disabled}
                          aria-pressed={source.mode === mode}
                          onClick={() => onSource(c.id, { mode })}
                        >
                          <Icon size={11} />
                          {label}
                        </button>
                      ))}
                    </div>
                    {source.mode === "texture" ? (
                      <div className="channel-texture-source">
                        {source.texture ? (
                          <>
                            <div className="channel-source-preview">
                              <img
                                src={source.texture.dataUrl}
                                alt={`${c.label} imported texture`}
                              />
                              <div>
                                <strong>{source.texture.name}</strong>
                                <small>
                                  {source.texture.width} ×{" "}
                                  {source.texture.height} · stored source
                                </small>
                              </div>
                            </div>
                            <button
                              className="channel-source-remove"
                              disabled={disabled}
                              onClick={() =>
                                onSource(c.id, { texture: null })
                              }
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
                            channel.current = c.id;
                            file.current.click();
                          }}
                        >
                          <Upload size={12} />
                          {importing === c.id
                            ? "Importing…"
                            : source.texture
                              ? "Replace texture"
                              : "Import texture"}
                        </button>
                        <p className="chan-note">
                          PNG, JPEG or WebP · portable source up to 256 px.
                          The stamp deposits the channel Value; the texture is
                          a saved authoring target.
                        </p>
                      </div>
                    ) : source.mode === "generator" ? (
                      <>
                        <label className="channel-generator-label">
                          <span>Generator</span>
                          <select
                            aria-label={`${prefix} ${c.label} generator`}
                            value={source.generator?.id || ""}
                            disabled={disabled}
                            onChange={(e) =>
                              onSource(c.id, {
                                generator: normalizeGenerator({
                                  id: e.target.value,
                                }),
                              })
                            }
                          >
                            <option value="">Choose a generator…</option>
                            {source.generator?.id ===
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
                              {generator.note} · source configuration. The
                              stamp deposits the channel Value.
                            </p>
                            {generator.parameters.map((p) => (
                              <Slider
                                key={p.key}
                                label={p.label}
                                ariaLabel={`${prefix} ${c.label} ${generator.label} ${p.label}`}
                                value={source.generator.parameters[p.key]}
                                disabled={disabled}
                                onChange={(v) =>
                                  onSource(c.id, {
                                    generator: {
                                      ...source.generator,
                                      parameters: {
                                        ...source.generator.parameters,
                                        [p.key]: v,
                                      },
                                    },
                                  })
                                }
                              />
                            ))}
                          </>
                        ) : (
                          source.generator?.id === MATERIAL_GENERATOR_ID && (
                            <p className="chan-note">
                              Material studio · custom source — edit it on a
                              layer channel in the inspector.
                            </p>
                          )
                        )}
                      </>
                    ) : null}
                    {valueEditor}
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
                    <span>
                      {c.edit === "derived"
                        ? "Derived preview"
                        : `${SOURCE_MODES.find((m) => m[0] === source.mode)?.[1]} preview`}
                    </span>
                    <small>{c.placement}</small>
                  </div>
                </div>
              </div>
            )}
          </section>
        );
      })}
    </section>
  );
}
