import AuthoringNameField from "./AuthoringNameField.jsx";
import React, { useEffect, useLayoutEffect, useRef, useState } from "react";
import {
  Type,
  Code2,
  ImagePlus,
  Stamp,
  Plus,
  Copy,
  Trash2,
  Eye,
  EyeOff,
  LockKeyhole,
  UnlockKeyhole,
  ArrowUp,
  ArrowDown,
  Undo2,
  Redo2,
  Save,
  FolderOpen,
  Download,
  Move,
  MousePointer2,
} from "lucide-react";
import { StudioHeader, StudioDockTab } from "./StudioUI.jsx";
import { Slider, ColorField } from "./MaterialControls.jsx";
import StampRichTextEditor from "./StampRichTextEditor.jsx";
import { PAINT_CHANNELS } from "./paintChannelModel.js";
import { importTextureSource } from "./textureSource.js";
import { sanitizePatternSVG } from "./patternImport.js";
import {
  composeStampSVG,
  outlineStampText,
  renderStamp,
  validatedStampDocument,
} from "./stampRaster.js";
import {
  STAMP_DRAFT_KEY,
  STAMP_LAYER_LIMIT,
  createStampDocument,
  createStampLayer,
  normalizeStampDocument,
  parseStampDocument,
  normalizeStampChannels,
  readStampPresets,
  saveStampPreset,
  removeStampPreset,
  stampId,
} from "./stampDocument.js";
import "./stampStudio.css";

const kindIcon = (kind) =>
  kind === "text" ? Type : kind === "svg" ? Code2 : ImagePlus;
function download(data, filename, type) {
  const url = URL.createObjectURL(new Blob([data], { type }));
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function restore(initial) {
  try {
    return initial
      ? normalizeStampDocument(initial)
      : localStorage.getItem(STAMP_DRAFT_KEY)
        ? parseStampDocument(localStorage.getItem(STAMP_DRAFT_KEY))
        : createStampDocument();
  } catch {
    return createStampDocument();
  }
}
export function StampChannelControls({
  channels,
  values,
  onChange,
  disabled = false,
  prefix = "Stamp",
}) {
  return (
    <section className="stamp-channel-controls">
      <h3>
        Channel targets <small>{channels.length} enabled</small>
      </h3>
      <div className="stamp-channel-chips">
        {PAINT_CHANNELS.map((channel) => (
          <button
            key={channel.id}
            disabled={disabled}
            aria-label={`${prefix} ${channel.label} channel`}
            aria-pressed={channels.includes(channel.id)}
            style={{ "--channel-color": channel.color }}
            onClick={() => {
              let next = channels.includes(channel.id)
                ? channels.filter((id) => id !== channel.id)
                : [...channels, channel.id];
              if (channel.id === "height" && !next.includes("height"))
                next = next.filter((id) => id !== "normal");
              onChange({ channels: normalizeStampChannels(next) });
            }}
          >
            {channel.short}
            <span>{channel.label}</span>
          </button>
        ))}
      </div>
      {PAINT_CHANNELS.filter(
        (c) =>
          channels.includes(c.id) && !["baseColor", "normal"].includes(c.id),
      ).map((channel) => (
        <div className="stamp-channel-value" key={channel.id}>
          {channel.edit === "color" ? (
            <ColorField
              label={channel.label}
              ariaLabel={`${prefix} ${channel.label}`}
              value={values[channel.id]}
              disabled={disabled}
              onChange={(value) =>
                onChange({ channelValues: { ...values, [channel.id]: value } })
              }
            />
          ) : (
            <Slider
              label={channel.label}
              ariaLabel={`${prefix} ${channel.label}`}
              value={values[channel.id]}
              min={channel.min}
              max={channel.max}
              step={channel.max > 1 ? 1 : 0.01}
              disabled={disabled}
              onChange={(value) =>
                onChange({ channelValues: { ...values, [channel.id]: value } })
              }
            />
          )}
        </div>
      ))}
      <p className="stamp-note">
        Colour comes from the artwork. Height/normal use its luminance. Disabled
        targets retain their values; no channels enabled means no surface decal.
      </p>
    </section>
  );
}
export default function StampStudio({ initial, onWorkspace, onClose, onUse }) {
  const [doc, setDoc] = useState(() => restore(initial));
  const current = useRef(doc);
  current.current = doc;
  const [selectedId, setSelectedId] = useState(doc.layers[0]?.id);
  const selectionRef = useRef(selectedId);
  selectionRef.current = selectedId;
  const undo = useRef([]),
    redo = useRef([]),
    coalesce = useRef(null);
  const [revision, setRevision] = useState(0);
  const [error, setError] = useState(""),
    [status, setStatus] = useState("Stamp draft · autosaved locally");
  const [presets, setPresets] = useState(readStampPresets);
  const [preview, setPreview] = useState(""),
    [fontWarning, setFontWarning] = useState("");
  const [draft, setDraft] = useState(null),
    draftRef = useRef(null),
    gesture = useRef(null),
    board = useRef(null);
  draftRef.current = draft;
  const input = useRef(null),
    previewURLs = useRef(new Set()),
    previewWanted = useRef(null),
    importKind = useRef("project"),
    importJob = useRef(0),
    [busy, setBusy] = useState(false),
    job = useRef(0),
    mounted = useRef(true);
  const [mobilePanel, setMobilePanel] = useState("canvas");
  const selected = doc.layers.find((l) => l.id === selectedId);
  const previewDoc = draft
    ? {
        ...doc,
        layers: doc.layers.map((l) =>
          l.id === draft.id ? { ...l, ...draft } : l,
        ),
      }
    : doc;
  const shown = previewDoc.layers.find((l) => l.id === selectedId);
  function commit(input, key = null, selection = selectedId) {
    try {
      const next = normalizeStampDocument(input);
      if (JSON.stringify(next) === JSON.stringify(current.current))
        return false;
      if (!key || coalesce.current !== key)
        undo.current = [
          ...undo.current,
          { doc: current.current, selection: selectionRef.current },
        ].slice(-80);
      redo.current = [];
      coalesce.current = key;
      current.current = next;
      setDoc(next);
      setSelectedId(selection);
      setRevision((n) => n + 1);
      setError("");
      return true;
    } catch (e) {
      setError(e.message);
      return false;
    }
  }
  function patchLayer(changes, key) {
    const owner = current.current.layers.find(
      (l) => l.id === selectionRef.current,
    );
    if (
      !owner ||
      (owner.locked &&
        !Object.hasOwn(changes, "locked") &&
        !Object.hasOwn(changes, "visible"))
    )
      return false;
    return commit(
      {
        ...current.current,
        layers: current.current.layers.map((l) =>
          l.id === owner.id ? { ...l, ...changes } : l,
        ),
      },
      key ? `${owner.id}:${key}` : null,
    );
  }
  function choose(id) {
    setSelectedId(id);
    selectionRef.current = id;
    coalesce.current = null;
  }
  function travel(direction) {
    if (gesture.current) {
      endGesture(true);
      return;
    }
    const source = direction === "undo" ? undo : redo,
      target = direction === "undo" ? redo : undo;
    if (!source.current.length) return;
    target.current.push({
      doc: current.current,
      selection: selectionRef.current,
    });
    const next = source.current.pop();
    current.current = next.doc;
    setDoc(next.doc);
    setSelectedId(next.selection);
    coalesce.current = null;
    setRevision((n) => n + 1);
  }
  useEffect(() => {
    const timer = setTimeout(() => {
      try {
        localStorage.setItem(STAMP_DRAFT_KEY, JSON.stringify(doc));
        setStatus("Stamp draft saved locally");
      } catch {
        setStatus("Local storage unavailable · export a project to keep it");
      }
    }, 400);
    return () => clearTimeout(timer);
  }, [doc]);
  useEffect(() => {
    let cancelled = false,
      url = "";
    const timer = setTimeout(
      async () => {
        try {
          const result = await composeStampSVG(previewDoc);
          if (cancelled) return;
          url = URL.createObjectURL(
            new Blob([result.svg], { type: "image/svg+xml" }),
          );
          previewURLs.current.add(url);
          previewWanted.current = url;
          setPreview(url);
          setFontWarning(
            result.unsupported.length
              ? `These characters are not in the bundled fonts: ${result.unsupported.join(" ")}`
              : "",
          );
        } catch (e) {
          if (!cancelled) setError(e.message);
        }
      },
      draft ? 30 : 90,
    );
    return () => {
      cancelled = true;
      clearTimeout(timer);
    };
  }, [JSON.stringify(previewDoc)]);
  useLayoutEffect(() => {
    // The img already points at the newly committed URL. Only now retire the
    // previous preview; revoking in a document-effect cleanup can race img loads.
    for (const url of previewURLs.current)
      if (url !== preview && url !== previewWanted.current) {
        URL.revokeObjectURL(url);
        previewURLs.current.delete(url);
      }
  }, [preview]);
  useEffect(() => {
    mounted.current = true;
    const change = () => setPresets(readStampPresets());
    window.addEventListener("alloy-stamp-library-change", change);
    return () => {
      mounted.current = false;
      job.current++;
      for (const url of previewURLs.current) URL.revokeObjectURL(url);
      previewURLs.current.clear();
      window.removeEventListener("alloy-stamp-library-change", change);
      try {
        localStorage.setItem(STAMP_DRAFT_KEY, JSON.stringify(current.current));
      } catch {
        /* Export remains available. */
      }
    };
  }, []);
  useEffect(() => {
    const key = (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "z") {
        e.preventDefault();
        travel(e.shiftKey ? "redo" : "undo");
      }
      if (e.key === "Escape") endGesture(true);
    };
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  }, []);
  function add(kind, options = {}) {
    if (current.current.layers.length >= STAMP_LAYER_LIMIT) {
      setError("The 24-component limit has been reached.");
      return;
    }
    const component = createStampLayer(kind, options);
    commit(
      { ...current.current, layers: [component, ...current.current.layers] },
      null,
      component.id,
    );
  }
  function startGesture(e, layer, mode = "move") {
    if (e.button !== 0 || layer.locked) return;
    e.preventDefault();
    e.stopPropagation();
    choose(layer.id);
    e.currentTarget.setPointerCapture(e.pointerId);
    const rect = board.current.getBoundingClientRect();
    gesture.current = {
      id: layer.id,
      before: JSON.stringify(layer),
      layer,
      mode,
      pointerId: e.pointerId,
      x: e.clientX,
      y: e.clientY,
      factor: 512 / rect.width,
    };
  }
  function moveGesture(e) {
    const g = gesture.current;
    if (!g || e.pointerId !== g.pointerId) return;
    const dx = (e.clientX - g.x) * g.factor,
      dy = (e.clientY - g.y) * g.factor;
    const snap = (v) => (e.shiftKey ? Math.round(v / 8) * 8 : v);
    const next =
      g.mode === "resize"
        ? {
            id: g.id,
            width: Math.max(1, snap(g.layer.width + dx * 2)),
            height: Math.max(1, snap(g.layer.height + dy * 2)),
          }
        : { id: g.id, x: snap(g.layer.x + dx), y: snap(g.layer.y + dy) };
    draftRef.current = next;
    setDraft(next);
  }
  function endGesture(cancel = false) {
    const g = gesture.current,
      next = draftRef.current;
    gesture.current = null;
    draftRef.current = null;
    setDraft(null);
    if (!g || !next || cancel) return;
    const owner = current.current.layers.find((l) => l.id === g.id);
    if (!owner || owner.locked || JSON.stringify(owner) !== g.before) return;
    commit(
      {
        ...current.current,
        layers: current.current.layers.map((l) =>
          l.id === g.id ? { ...l, ...next } : l,
        ),
      },
      null,
      g.id,
    );
  }
  function pickFile(kind) {
    importKind.current = kind;
    input.current.accept =
      kind === "image"
        ? "image/png,image/jpeg,image/webp"
        : kind === "svg"
          ? ".svg,image/svg+xml"
          : ".json,.stamp,application/json";
    input.current.click();
  }
  async function importFile(file) {
    if (!file) return;
    const kind = importKind.current,
      before = current.current,
      beforeSelection = selectionRef.current,
      ticket = ++importJob.current;
    const accepts = () =>
      mounted.current &&
      ticket === importJob.current &&
      current.current === before &&
      selectionRef.current === beforeSelection;
    try {
      let result;
      if (kind === "image")
        result = {
          kind: "image",
          name: file.name,
          image: await importTextureSource(file),
        };
      else if (kind === "svg") {
        if (file.size > 200000)
          throw new Error("SVG components are limited to 200 KB.");
        result = {
          kind: "svg",
          name: file.name,
          svg: sanitizePatternSVG(await file.text()),
        };
      } else {
        if (file.size > 600000)
          throw new Error("Stamp files must be smaller than 600 KB.");
        result = await validatedStampDocument(
          parseStampDocument(await file.text()),
        );
      }
      if (!accepts()) return;
      if (kind === "project") commit(result, null, result.layers[0]?.id);
      else add(kind, result);
    } catch (e) {
      if (accepts()) setError(e.message);
    }
  }
  async function savePreset(use = false, asNew = false) {
    const snapshot = current.current,
      signature = JSON.stringify(snapshot),
      ticket = ++job.current;
    setBusy(true);
    setError("");
    try {
      const source = await validatedStampDocument(snapshot);
      const project = asNew
        ? { ...source, id: stampId(), name: `${source.name} copy`.slice(0, 80) }
        : source;
      const { raster } = await renderStamp(project);
      if (
        !mounted.current ||
        ticket !== job.current ||
        JSON.stringify(current.current) !== signature
      )
        return;
      const preset = saveStampPreset({ project, raster });
      if (asNew) commit(project, null, selectedId);
      setStatus(`Saved preset: ${preset.name}`);
      if (use) onUse(preset);
    } catch (e) {
      if (mounted.current && ticket === job.current) setError(e.message);
    } finally {
      if (mounted.current && ticket === job.current) setBusy(false);
    }
  }
  async function exportArtwork(kind) {
    const snapshot = current.current;
    try {
      const project = await validatedStampDocument(snapshot);
      if (kind === "json")
        download(
          JSON.stringify(project, null, 2),
          "alloy-stamp.stamp.json",
          "application/json",
        );
      else if (kind === "svg")
        download(
          (await composeStampSVG(project)).svg,
          "alloy-stamp.svg",
          "image/svg+xml",
        );
      else {
        const { raster } = await renderStamp(project);
        const a = document.createElement("a");
        a.href = raster.dataUrl;
        a.download = "alloy-stamp.png";
        a.click();
      }
    } catch (e) {
      setError(e.message);
    }
  }
  return (
    <section
      className={`stamp-studio studio-workspace stamp-mobile-${mobilePanel}`}
      aria-label="Stamp studio"
    >
      <StudioHeader
        workspace="stamp"
        name={doc.name}
        extension=".stamp"
        onWorkspace={onWorkspace}
        onClose={onClose}
      >
        <button
          aria-label="New stamp project"
          title="New stamp composition"
          onClick={() => {
            const next = createStampDocument();
            commit(
              { ...next, name: "Untitled stamp" },
              null,
              next.layers[0]?.id,
            );
          }}
        >
          <Plus size={15} />
        </button>
        <button
          aria-label="Open stamp project"
          title="Open project"
          onClick={() => pickFile("project")}
        >
          <FolderOpen size={15} />
        </button>
        <button
          aria-label="Save stamp project"
          title="Export editable project"
          onClick={() => exportArtwork("json")}
        >
          <Save size={15} />
        </button>
        <button
          className="stamp-use"
          disabled={busy}
          onClick={() => savePreset(true)}
        >
          <Stamp size={14} /> Use in Texture
        </button>
      </StudioHeader>
      <input
        type="file"
        ref={input}
        hidden
        onChange={(e) => {
          const file = e.target.files[0];
          e.target.value = "";
          importFile(file);
        }}
      />
      <nav className="stamp-mobile-nav">
        {["layers", "canvas", "inspector"].map((id) => (
          <button
            key={id}
            aria-pressed={mobilePanel === id}
            onClick={() => setMobilePanel(id)}
          >
            {id}
          </button>
        ))}
      </nav>
      <main className="stamp-layout">
        <aside className="stamp-left">
          <StudioDockTab icon={Stamp}>Stamp composition</StudioDockTab>
          <div className="stamp-left-title">
            <h1>Components</h1>
            <small>{doc.layers.length} / 24</small>
          </div>
          <div className="stamp-add-tools">
            <button
              onClick={() => add("text")}
              disabled={doc.layers.length >= 24}
            >
              <Type size={15} /> Text
            </button>
            <button
              onClick={() => add("svg")}
              disabled={doc.layers.length >= 24}
            >
              <Code2 size={15} /> SVG
            </button>
            <button
              onClick={() => pickFile("image")}
              disabled={doc.layers.length >= 24}
            >
              <ImagePlus size={15} /> Image
            </button>
          </div>
          <div
            className="stamp-component-list"
            role="list"
            aria-label="Stamp components"
          >
            {doc.layers.map((layer) => {
              const Icon = kindIcon(layer.kind);
              return (
                <div
                  className={`stamp-component-row ${selectedId === layer.id ? "selected" : ""}`}
                  key={layer.id}
                  role="listitem"
                >
                  <button
                    onClick={() => choose(layer.id)}
                    aria-label={`Select stamp component ${layer.name}`}
                  >
                    <Icon size={20} />
                    <span>
                      <strong>{layer.name}</strong>
                      <small>
                        {layer.kind.toUpperCase()} ·{" "}
                        {Math.round(layer.opacity * 100)}%
                      </small>
                    </span>
                  </button>
                  <button
                    aria-label={`${layer.visible ? "Hide" : "Show"} stamp component ${layer.name}`}
                    onClick={() => {
                      choose(layer.id);
                      commit({
                        ...doc,
                        layers: doc.layers.map((l) =>
                          l.id === layer.id ? { ...l, visible: !l.visible } : l,
                        ),
                      });
                    }}
                  >
                    {layer.visible ? <Eye size={13} /> : <EyeOff size={13} />}
                  </button>
                </div>
              );
            })}
          </div>
          <div className="stamp-layer-actions">
            <button
              aria-label="Move stamp component up"
              disabled={
                !selected ||
                selected.locked ||
                doc.layers.indexOf(selected) === 0
              }
              onClick={() => {
                const layers = [...doc.layers],
                  i = layers.indexOf(selected);
                [layers[i - 1], layers[i]] = [layers[i], layers[i - 1]];
                commit({ ...doc, layers });
              }}
            >
              <ArrowUp size={15} />
            </button>
            <button
              aria-label="Move stamp component down"
              disabled={
                !selected ||
                selected.locked ||
                doc.layers.indexOf(selected) === doc.layers.length - 1
              }
              onClick={() => {
                const layers = [...doc.layers],
                  i = layers.indexOf(selected);
                [layers[i + 1], layers[i]] = [layers[i], layers[i + 1]];
                commit({ ...doc, layers });
              }}
            >
              <ArrowDown size={15} />
            </button>
            <button
              aria-label="Duplicate stamp component"
              disabled={!selected || selected.locked || doc.layers.length >= 24}
              onClick={() =>
                add(selected.kind, {
                  ...selected,
                  id: stampId(),
                  name: `${selected.name} copy`,
                  x: selected.x + 12,
                  y: selected.y + 12,
                })
              }
            >
              <Copy size={15} />
            </button>
            <button
              aria-label="Delete stamp component"
              disabled={!selected || selected.locked}
              onClick={() => {
                const layers = doc.layers.filter((l) => l.id !== selectedId);
                commit({ ...doc, layers }, null, layers[0]?.id);
              }}
            >
              <Trash2 size={15} />
            </button>
          </div>
          <div className="stamp-preset-heading">
            <h2>Stamp presets</h2>
            <button
              disabled={busy}
              onClick={() => savePreset()}
              aria-label="Save stamp preset"
            >
              <Plus size={14} />
            </button>
          </div>
          <div className="stamp-preset-list">
            {presets.length ? (
              presets.map((preset) => (
                <div className="stamp-preset-row" key={preset.id}>
                  <button
                    aria-label={`Open stamp preset ${preset.name}`}
                    onClick={() =>
                      commit(preset.project, null, preset.project.layers[0]?.id)
                    }
                  >
                    <img src={preset.raster.dataUrl} alt="" />
                    <span>
                      {preset.name}
                      <small>
                        {preset.project.layers.length} components ·{" "}
                        {preset.project.channels.length} channels
                      </small>
                    </span>
                  </button>
                  <button
                    aria-label={`Delete stamp preset ${preset.name}`}
                    title="Remove from library; placed decals keep their embedded snapshot"
                    onClick={() => {
                      try {
                        removeStampPreset(preset.id);
                      } catch (e) {
                        setError(e.message);
                      }
                    }}
                  >
                    <Trash2 size={12} />
                  </button>
                </div>
              ))
            ) : (
              <p className="stamp-note">
                Save a reusable preset, then stamp it onto the teapot in
                Texture.
              </p>
            )}
          </div>
        </aside>
        <section className="stamp-centre">
          <StudioDockTab icon={MousePointer2}>
            Stamp canvas <span className="stamp-canvas-size">512 × 512</span>
          </StudioDockTab>
          <div className="stamp-canvas-toolbar">
            <span>
              <Move size={13} /> Drag components · Shift snaps
            </span>
            <button onClick={() => exportArtwork("svg")}>SVG</button>
            <button onClick={() => exportArtwork("png")}>
              PNG <Download size={12} />
            </button>
          </div>
          <div className="stamp-canvas-surround">
            <div
              className="stamp-canvas"
              ref={board}
              aria-label="Stamp composition canvas"
            >
              {preview && (
                <img
                  src={preview}
                  alt="Composed stamp preview"
                  draggable={false}
                />
              )}
              {previewDoc.layers
                .filter((l) => l.visible)
                .map((component) => (
                  <button
                    key={component.id}
                    className={`stamp-canvas-object ${selectedId === component.id ? "selected" : ""}`}
                    aria-label={`Canvas stamp component ${component.name}`}
                    style={{
                      left: `${(component.x / 512) * 100}%`,
                      top: `${(component.y / 512) * 100}%`,
                      width: `${(component.width / 512) * 100}%`,
                      height: `${(component.height / 512) * 100}%`,
                      transform: `translate(-50%,-50%) rotate(${component.rotation}deg)`,
                      zIndex:
                        doc.layers.length -
                        doc.layers.findIndex((l) => l.id === component.id),
                    }}
                    onClick={() => choose(component.id)}
                    onPointerDown={(e) => startGesture(e, component)}
                    onPointerMove={moveGesture}
                    onPointerUp={() => endGesture()}
                    onPointerCancel={() => endGesture(true)}
                    onLostPointerCapture={() => endGesture(true)}
                    onKeyDown={(e) => {
                      if (
                        [
                          "ArrowLeft",
                          "ArrowRight",
                          "ArrowUp",
                          "ArrowDown",
                        ].includes(e.key) &&
                        !component.locked
                      ) {
                        e.preventDefault();
                        const amount = e.shiftKey ? 10 : 1;
                        choose(component.id);
                        commit({
                          ...doc,
                          layers: doc.layers.map((l) =>
                            l.id === component.id
                              ? {
                                  ...l,
                                  x:
                                    l.x +
                                    (e.key === "ArrowLeft"
                                      ? -amount
                                      : e.key === "ArrowRight"
                                        ? amount
                                        : 0),
                                  y:
                                    l.y +
                                    (e.key === "ArrowUp"
                                      ? -amount
                                      : e.key === "ArrowDown"
                                        ? amount
                                        : 0),
                                }
                              : l,
                          ),
                        });
                      }
                    }}
                  >
                    {selectedId === component.id && (
                      <>
                        <span className="stamp-object-label">
                          {component.name}
                        </span>
                        <span
                          role="button"
                          tabIndex={0}
                          aria-label="Resize selected stamp component"
                          className="stamp-resize-handle"
                          onPointerDown={(e) =>
                            startGesture(e, component, "resize")
                          }
                        />
                      </>
                    )}
                  </button>
                ))}
            </div>
          </div>
          <div className="stamp-canvas-caption">
            <Stamp size={15} />
            <span>
              One reusable stamp
              <small>
                Text outlines + static SVG + embedded images · transparent
                background
              </small>
            </span>
          </div>
        </section>
        <aside className="stamp-right">
          <StudioDockTab icon={Type}>Stamp inspector</StudioDockTab>
          <div className="stamp-inspector-scroll">
            <div className="stamp-identity">
              <Stamp size={26} />
              <div>
                <small>STAMP WORKSPACE</small>
                <h1>Artwork & targets</h1>
                <p>A simpler composition space for surface marks.</p>
              </div>
            </div>
            <label className="stamp-field">
              Stamp name
              <AuthoringNameField
                ariaLabel="Stamp name"
                value={doc.name}
                onChange={(name) =>
                  commit({ ...current.current, name }, "stamp-name")
                }
              />
            </label>
            {selected && (
              <section className="stamp-component-properties">
                <div className="stamp-property-heading">
                  <h2>
                    {selected.kind === "text"
                      ? "Rich text"
                      : selected.kind === "svg"
                        ? "SVG artwork"
                        : "Image source"}
                  </h2>
                  <button
                    aria-label={
                      selected.locked
                        ? "Unlock stamp component"
                        : "Lock stamp component"
                    }
                    onClick={() => patchLayer({ locked: !selected.locked })}
                  >
                    {selected.locked ? (
                      <LockKeyhole size={14} />
                    ) : (
                      <UnlockKeyhole size={14} />
                    )}
                  </button>
                </div>
                <label className="stamp-field">
                  Component name
                  <AuthoringNameField
                    ariaLabel="Stamp component name"
                    disabled={selected.locked}
                    value={selected.name}
                    onChange={(name) => patchLayer({ name }, "name")}
                  />
                </label>
                {selected.kind === "text" && (
                  <StampRichTextEditor
                    key={selected.id}
                    runs={selected.runs}
                    disabled={selected.locked}
                    onChange={(runs, meta) => {
                      try {
                        if (
                          JSON.stringify(runs) === JSON.stringify(selected.runs)
                        )
                          return false;
                        const oldLayout = outlineStampText(selected.runs),
                          nextLayout = outlineStampText(runs);
                        return patchLayer(
                          {
                            runs,
                            width:
                              selected.width *
                              (nextLayout.width / Math.max(1, oldLayout.width)),
                            height:
                              selected.height *
                              (nextLayout.height /
                                Math.max(1, oldLayout.height)),
                          },
                          meta?.key ? `rich-text-${meta.key}` : null,
                        );
                      } catch (e) {
                        setError(e.message);
                        return false;
                      }
                    }}
                  />
                )}
                {selected.kind === "svg" && (
                  <SVGMarkupEditor
                    key={selected.id}
                    layer={selected}
                    onChange={(svg) => patchLayer({ svg })}
                    onImport={() => pickFile("svg")}
                  />
                )}
                {selected.kind === "image" && (
                  <div className="stamp-image-source">
                    <img
                      src={selected.image.dataUrl}
                      alt={selected.image.name}
                    />
                    <button
                      disabled={selected.locked}
                      onClick={() => pickFile("image")}
                    >
                      <ImagePlus size={14} /> Add image
                    </button>
                  </div>
                )}
                <div className="stamp-shared-controls">
                  <Slider
                    label="Position X"
                    ariaLabel="Stamp position X"
                    min={-512}
                    max={1024}
                    step={1}
                    value={shown.x}
                    disabled={selected.locked}
                    onChange={(x) => patchLayer({ x }, "x")}
                  />
                  <Slider
                    label="Position Y"
                    ariaLabel="Stamp position Y"
                    min={-512}
                    max={1024}
                    step={1}
                    value={shown.y}
                    disabled={selected.locked}
                    onChange={(y) => patchLayer({ y }, "y")}
                  />
                  <Slider
                    label="Width"
                    ariaLabel="Stamp width"
                    min={1}
                    max={1024}
                    step={1}
                    value={shown.width}
                    disabled={selected.locked}
                    onChange={(width) => patchLayer({ width }, "width")}
                  />
                  <Slider
                    label="Height"
                    ariaLabel="Stamp height"
                    min={1}
                    max={1024}
                    step={1}
                    value={shown.height}
                    disabled={selected.locked}
                    onChange={(height) => patchLayer({ height }, "height")}
                  />
                  <Slider
                    label="Rotation"
                    ariaLabel="Stamp rotation"
                    min={-180}
                    max={180}
                    step={1}
                    unit="°"
                    value={shown.rotation}
                    disabled={selected.locked}
                    onChange={(rotation) =>
                      patchLayer({ rotation }, "rotation")
                    }
                  />
                  <Slider
                    label="Opacity"
                    ariaLabel="Stamp component opacity"
                    percentage
                    value={selected.opacity}
                    disabled={selected.locked}
                    onChange={(opacity) => patchLayer({ opacity }, "opacity")}
                  />
                </div>
              </section>
            )}
            <StampChannelControls
              channels={doc.channels}
              values={doc.channelValues}
              onChange={(changes) =>
                commit(
                  { ...doc, ...changes },
                  changes.channelValues ? "channel-values" : null,
                )
              }
            />
            <label className="stamp-field">
              Mask interpretation
              <select
                aria-label="Stamp mask interpretation"
                value={doc.maskMode}
                onChange={(e) => commit({ ...doc, maskMode: e.target.value })}
              >
                <option value="alpha">Artwork alpha / silhouette</option>
                <option value="luminance">Luminance × alpha</option>
              </select>
            </label>
            <p className="stamp-note">
              The same preset can be a decal component or a surface-projected
              layer mask. Masks ignore colour/material channel targets.
            </p>
            <button
              className="stamp-save-preset"
              disabled={busy}
              onClick={() => savePreset()}
            >
              <Save size={15} />{" "}
              {busy ? "Rendering stamp…" : "Save stamp preset"}
            </button>
            <button
              className="stamp-save-copy"
              disabled={busy}
              onClick={() => savePreset(false, true)}
            >
              <Copy size={14} /> Save as new preset
            </button>
            {fontWarning && (
              <p className="stamp-note" role="status">
                {fontWarning}
              </p>
            )}
            {error && (
              <p className="stamp-error" role="alert">
                {error}
              </p>
            )}
          </div>
        </aside>
      </main>
      <footer className="stamp-status">
        <span>{status}</span>
        <span>
          <button
            aria-label="Undo stamp edit"
            disabled={!undo.current.length}
            onClick={() => travel("undo")}
          >
            <Undo2 size={14} />
          </button>
          <button
            aria-label="Redo stamp edit"
            disabled={!redo.current.length}
            onClick={() => travel("redo")}
          >
            <Redo2 size={14} />
          </button>
          <small>
            {doc.layers.length} components · {doc.channels.length} targets ·{" "}
            {revision} edits
          </small>
        </span>
      </footer>
    </section>
  );
}
function SVGMarkupEditor({ layer, onChange, onImport }) {
  const [draft, setDraft] = useState(layer.svg),
    [error, setError] = useState("");
  useEffect(() => setDraft(layer.svg), [layer.svg]);
  return (
    <div className="stamp-svg-editor">
      <textarea
        aria-label="Stamp SVG markup"
        spellCheck={false}
        disabled={layer.locked}
        value={draft}
        maxLength={200000}
        onChange={(e) => setDraft(e.target.value)}
      />
      <div>
        <button
          disabled={layer.locked}
          onClick={() => {
            try {
              onChange(sanitizePatternSVG(draft));
              setError("");
            } catch (e) {
              setError(e.message);
            }
          }}
        >
          <Code2 size={13} /> Apply SVG
        </button>
        <button disabled={layer.locked} onClick={onImport}>
          Import SVG
        </button>
      </div>
      <p className="stamp-note">
        Static markup only. Scripts, handlers, external resources, animation and
        HTML are rejected.
      </p>
      {error && (
        <p className="stamp-error" role="alert">
          {error}
        </p>
      )}
    </div>
  );
}
