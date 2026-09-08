import { useEffect, useRef, useState } from "react";
import {
  ArrowUpRight,
  Box,
  Check,
  ChevronDown,
  Download,
  Eye,
  Focus,
  Gem,
  Layers3,
  Minus,
  Play,
  Plus,
  RotateCcw,
  Save,
  Shuffle,
  Sun,
  Upload,
  X,
} from "lucide-react";
import {
  SATMAP_LIBRARY,
  filterSatmaps,
  getBuiltinSatmap,
} from "../engine/satmaps/catalog";
import { paletteGradient } from "../engine/satmaps/satmap";
import {
  DEFAULT_STONE,
  LAB_VIEWS,
  STONE_PRESETS,
  STONE_RANGES,
  clamp,
  encodeStoneRecipe,
  validateStoneRecipe,
  type StoneSettings,
} from "./model";
import { StoneRenderer, type LabStatus } from "./renderer";
import "./styles.css";
const STORAGE_KEY = "frontier.sdf-material-lab.v1";
const viewNames = {
  lit: "Surface",
  clay: "Clay",
  albedo: "Albedo",
  normals: "Normals",
  relief: "SDF relief",
  silhouette: "Silhouette",
  steps: "March steps",
};
function download(blob: Blob, name: string) {
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function initialSettings(): StoneSettings {
  let s = { ...DEFAULT_STONE };
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) s = validateStoneRecipe(JSON.parse(raw));
  } catch {
    /* Invalid/blocked browser storage must not prevent startup. */
  }
  const query = new URLSearchParams(location.search);
  if (query.get("quality") === "draft") s.quality = "draft";
  return s;
}
function ValueSlider({
  label,
  value,
  min,
  max,
  step = 1,
  unit = "",
  scale = 1,
  onChange,
  disabled = false,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  unit?: string;
  scale?: number;
  onChange: (n: number) => void;
  disabled?: boolean;
}) {
  return (
    <label className={`lab-slider ${disabled ? "is-disabled" : ""}`}>
      <span className="lab-slider-label">
        {label}
        <span className="lab-number">
          <input
            aria-label={`${label} numeric value`}
            type="number"
            min={min * scale}
            max={max * scale}
            step={step * scale}
            value={Number((value * scale).toFixed(3))}
            disabled={disabled}
            onChange={(e) => {
              const v = e.target.valueAsNumber;
              if (Number.isFinite(v)) onChange(clamp(v / scale, min, max));
            }}
          />
          <small>{unit}</small>
        </span>
      </span>
      <input
        aria-label={label}
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        disabled={disabled}
        onChange={(e) => onChange(Number(e.target.value))}
      />
    </label>
  );
}
export default function MaterialLab() {
  const [settings, setSettings] = useState(initialSettings),
    [preset, setPreset] = useState(
      () =>
        (STONE_PRESETS.find((p) =>
          [
            "seed",
            "form",
            "facets",
            "chips",
            "bedding",
            "layerBreakup",
            "spacing",
            "tilt",
            "crackDepth",
            "crackWidth",
            "crackSpacing",
            "crackBranching",
            "crackChipping",
            "porosity",
            "poreSize",
            "poreIrregularity",
            "grain",
            "grainSize",
            "palette",
          ].every(
            (k) =>
              p.values[k as keyof StoneSettings] ===
              settings[k as keyof StoneSettings],
          ),
        )?.id as string) || "custom",
    ),
    [modified, setModified] = useState(false);
  const [status, setStatus] = useState<LabStatus>({
    ready: false,
    width: 0,
    height: 0,
    error: "",
    footprint: 0,
    draws: 0,
  });
  const [query, setQuery] = useState(""),
    [notice, setNotice] = useState(""),
    [busy, setBusy] = useState(false),
    [mobileTab, setMobileTab] = useState<"specimen" | "controls" | null>(null);
  const canvas = useRef<HTMLCanvasElement>(null),
    renderer = useRef<StoneRenderer | null>(null),
    state = useRef(settings),
    file = useRef<HTMLInputElement>(null);
  state.current = settings;
  useEffect(() => {
    try {
      const r = new StoneRenderer(canvas.current!, state.current, setStatus);
      renderer.current = r;
      if (import.meta.env.DEV)
        (window as unknown as Record<string, unknown>).__materialLab = {
          renderer: r,
          getSettings: () => state.current,
          apply: (patch: Partial<StoneSettings>) =>
            setSettings((s) => ({ ...s, ...patch })),
        };
      return () => {
        r.dispose();
        renderer.current = null;
        if (import.meta.env.DEV)
          delete (window as unknown as Record<string, unknown>).__materialLab;
      };
    } catch (error) {
      setStatus((s) => ({
        ...s,
        error: error instanceof Error ? error.message : String(error),
      }));
    }
  }, []);
  useEffect(() => renderer.current?.update(settings), [settings]);
  useEffect(() => {
    if (!notice) return;
    const t = setTimeout(() => setNotice(""), 6000);
    return () => clearTimeout(t);
  }, [notice]);
  const change = <K extends keyof StoneSettings>(
    key: K,
    value: StoneSettings[K],
  ) => {
    setSettings((s) => ({ ...s, [key]: value }));
    setModified(true);
  };
  const numeric = (key: keyof typeof STONE_RANGES) => (v: number) =>
    change(key, v);
  const active = STONE_PRESETS.find((p) => p.id === preset),
    asset = getBuiltinSatmap(settings.palette)!;
  const palettes = filterSatmaps(query);
  const snapshot = async () => {
    if (!renderer.current) return;
    setBusy(true);
    try {
      download(
        await renderer.current.capture(),
        `frontier-stone-${settings.seed}-${settings.view}.png`,
      );
    } catch (error) {
      setNotice(String(error));
    } finally {
      setBusy(false);
    }
  };
  const save = () => {
    try {
      localStorage.setItem(STORAGE_KEY, encodeStoneRecipe(settings));
      setNotice(
        "Recipe saved in this browser. The terrain project is untouched.",
      );
      setModified(false);
    } catch {
      setNotice("Browser storage is unavailable. Use Export recipe instead.");
    }
  };
  const importRecipe = async (f?: File) => {
    if (!f) return;
    try {
      if (f.size > 64 * 1024)
        throw new Error("Stone recipes must be smaller than 64 KB.");
      const value = validateStoneRecipe(JSON.parse(await f.text()));
      setSettings(value);
      setPreset("custom");
      setModified(false);
      setNotice("Stone recipe loaded. No image detail maps involved.");
    } catch (error) {
      setNotice(error instanceof Error ? error.message : String(error));
    }
  };
  return (
    <div className="material-lab">
      <header className="lab-header">
        <a className="lab-brand" href={`${import.meta.env.BASE_URL}index.html`}>
          <Gem size={23} />
          <strong>FRONTIER</strong>
        </a>
        <span className="lab-header-divider" />
        <div className="lab-page-title">
          <span>Material editor</span>
          <small>
            SDF SURFACE LAB <i /> EXPERIMENTAL
          </small>
        </div>
        <div className="lab-header-actions">
          <a
            className="lab-terrain-link"
            href={`${import.meta.env.BASE_URL}index.html`}
          >
            Terrain studio <ArrowUpRight size={13} />
          </a>
          <button
            onClick={save}
            className="lab-icon-button"
            aria-label="Save stone recipe"
            title="Save recipe locally"
          >
            <Save size={17} />
          </button>
          <button
            className="lab-primary"
            onClick={() => void snapshot()}
            disabled={!status.ready || !!status.error || busy}
          >
            <Download size={14} />
            {busy ? "Capturing…" : "Export PNG"}
          </button>
        </div>
      </header>
      <div className="lab-mobile-nav">
        <button
          className={mobileTab === "specimen" ? "selected" : ""}
          onClick={() =>
            setMobileTab(mobileTab === "specimen" ? null : "specimen")
          }
        >
          Specimens
        </button>
        <button
          className={mobileTab === null ? "selected" : ""}
          onClick={() => setMobileTab(null)}
        >
          Viewport
        </button>
        <button
          className={mobileTab === "controls" ? "selected" : ""}
          onClick={() =>
            setMobileTab(mobileTab === "controls" ? null : "controls")
          }
        >
          Surface controls
        </button>
      </div>
      <main className="lab-workspace">
        <aside
          className={`lab-presets ${mobileTab === "specimen" ? "mobile-open" : ""}`}
          aria-label="Stone specimens"
        >
          <div className="lab-panel-title">
            <span>Specimens</span>
            <small>01—06</small>
          </div>
          <p className="lab-panel-note">
            One organic form.
            <br />A different surface story.
          </p>
          <div className="lab-specimen-list">
            {STONE_PRESETS.map((p, i) => (
              <button
                key={p.id}
                className={preset === p.id ? "selected" : ""}
                aria-pressed={preset === p.id}
                onClick={() => {
                  setSettings((previous) => ({
                    ...p.values,
                    quality: previous.quality,
                    view: previous.view,
                    compare: previous.compare,
                    split: previous.split,
                    turntable: previous.turntable,
                  }));
                  setPreset(p.id);
                  setModified(false);
                  setMobileTab(null);
                }}
              >
                <div
                  className="lab-specimen-graphic"
                  style={{ "--stone-color": p.color } as React.CSSProperties}
                >
                  <span />
                  <small>0{i + 1}</small>
                  {preset === p.id && <Check size={12} />}
                </div>
                <span className="lab-specimen-text">
                  <strong>{p.name}</strong>
                  <small>{p.category}</small>
                </span>
              </button>
            ))}
          </div>
          <div className="lab-recipe-actions">
            <button
              onClick={() => {
                download(
                  new Blob([encodeStoneRecipe(settings)], {
                    type: "application/json",
                  }),
                  `frontier-stone-${settings.seed}.json`,
                );
              }}
            >
              <Download size={13} /> Export recipe
            </button>
            <button onClick={() => file.current?.click()}>
              <Upload size={13} /> Import recipe
            </button>
            <input
              ref={file}
              hidden
              type="file"
              accept=".json,application/json"
              aria-label="Import stone recipe"
              onChange={(e) => {
                void importRecipe(e.target.files?.[0]);
                e.target.value = "";
              }}
            />
          </div>
          <div className="lab-side-footnote">
            <Box size={16} />
            <p>
              Not a normal-map preview.
              <br />
              The ray hits the detailed
              <br />
              signed surface itself.
            </p>
          </div>
        </aside>
        <section className="lab-stage" aria-label="SDF stone preview">
          <div className="lab-stage-heading">
            <div>
              <small>
                SPECIMEN /{" "}
                {preset === "custom"
                  ? "CUSTOM"
                  : String(
                      STONE_PRESETS.findIndex((p) => p.id === preset) + 1,
                    ).padStart(2, "0")}
              </small>
              <h1>
                {active?.name ?? "Custom stone"}
                <span>{modified ? "Modified" : ""}</span>
              </h1>
            </div>
            <span className="lab-renderer-tag">
              <i />
              WEBGL2
            </span>
          </div>
          <div
            className="lab-view-tabs"
            role="group"
            aria-label="Stone inspection views"
          >
            {LAB_VIEWS.map((v) => (
              <button
                key={v}
                aria-pressed={settings.view === v}
                onClick={() => change("view", v)}
              >
                {viewNames[v]}
              </button>
            ))}
          </div>
          <div className="lab-canvas-wrap">
            <canvas
              ref={canvas}
              className="lab-canvas"
              tabIndex={0}
              aria-label="Interactive SDF stone. Drag to orbit, scroll to zoom, F to frame, hold C for the smooth form."
            />
            <div className="lab-canvas-tag">
              <Layers3 size={12} />
              <span>SIGNED SURFACE DETAIL</span>
              <small>SatMap color only · no image relief</small>
            </div>
            {!status.ready && !status.error && (
              <div className="lab-loading">Tracing the stone surface…</div>
            )}
            {status.error && (
              <div className="lab-error" role="alert">
                <strong>Graphics needs attention</strong>
                <p>{status.error}</p>
                <button onClick={() => location.reload()}>
                  Reload material editor
                </button>
              </div>
            )}
            {settings.compare && (
              <>
                <div
                  className="lab-split-line"
                  style={{ left: `${settings.split * 100}%` }}
                />
                <span className="lab-split-label left">SMOOTH FORM</span>
                <span className="lab-split-label right">SDF DETAIL</span>
              </>
            )}
            <div className="lab-view-tools">
              <button
                aria-label="Zoom into stone"
                onClick={() => renderer.current?.zoomBy(0.8)}
              >
                <Plus size={15} />
              </button>
              <button
                aria-label="Zoom out from stone"
                onClick={() => renderer.current?.zoomBy(1.25)}
              >
                <Minus size={15} />
              </button>
              <button
                className={settings.turntable ? "selected" : ""}
                aria-pressed={settings.turntable}
                aria-label="Rotate stone automatically"
                onClick={() => change("turntable", !settings.turntable)}
              >
                <Play size={14} />
              </button>
              <button
                aria-label="Frame stone"
                onClick={() => renderer.current?.resetCamera()}
              >
                <Focus size={16} />
              </button>
            </div>
            <div className="lab-orbit-help">
              DRAG Orbit <i /> SCROLL Zoom <i /> HOLD C Smooth form
            </div>
          </div>
          <div className="lab-stage-bottom">
            <label className="lab-check">
              <input
                type="checkbox"
                checked={settings.compare}
                onChange={(e) => change("compare", e.target.checked)}
              />
              <span>Compare smooth / detailed</span>
            </label>
            {settings.compare && (
              <input
                className="lab-split-range"
                aria-label="Comparison split"
                type="range"
                min={0.05}
                max={0.95}
                step={0.01}
                value={settings.split}
                onChange={(e) => change("split", Number(e.target.value))}
              />
            )}
            <span className="lab-resolution">
              {status.width} × {status.height} <i />{" "}
              {status.footprint ? (status.footprint * 1000).toFixed(2) : "—"} mm
              footprint
            </span>
          </div>
          <div className="lab-proof-note">
            <Eye size={14} />
            <span>
              {settings.view === "silhouette"
                ? "White is the actual ray-hit silhouette. Turn detail off to compare the boundary."
                : settings.view === "relief"
                  ? "Blue = inward carving. Warm = outward relief. Derived from the signed field at the hit point."
                  : settings.view === "steps"
                    ? "Mint shows raymarch work. Red marks an exhausted step budget—not a valid surface hit."
                    : "Inspect in Clay or Silhouette to separate geometric detail from the SatMap colors."}
            </span>
          </div>
        </section>
        <aside
          className={`lab-controls ${mobileTab === "controls" ? "mobile-open" : ""}`}
          aria-label="SDF surface controls"
        >
          <div className="lab-panel-title">
            <span>Surface stack</span>
            <button
              className="lab-icon-button"
              aria-label="Reset stone material"
              onClick={() => {
                setSettings((previous) => ({
                  ...DEFAULT_STONE,
                  quality: previous.quality,
                }));
                setPreset("sandstone");
                setModified(false);
              }}
            >
              <RotateCcw size={14} />
            </button>
          </div>
          <div className="lab-color-card">
            <small>BASE COLOR / SATMAP</small>
            <strong>{asset.name}</strong>
            <div
              className="lab-palette-ramp"
              style={{ background: paletteGradient(asset.palette) }}
            />
            <input
              className="lab-palette-search"
              aria-label="Search stone palettes"
              placeholder={`Search ${SATMAP_LIBRARY.length} palettes…`}
              value={query}
              onChange={(e) => setQuery(e.target.value)}
            />
            <select
              aria-label="Stone SatMap palette"
              value={
                palettes.some((p) => p.id === settings.palette)
                  ? settings.palette
                  : ""
              }
              onChange={(e) => {
                if (e.target.value)
                  change("palette", e.target.value as StoneSettings["palette"]);
              }}
            >
              {!palettes.some((p) => p.id === settings.palette) && (
                <option value="">
                  {palettes.length
                    ? "Choose a matching palette"
                    : "No matching palettes"}
                </option>
              )}
              {palettes.map((p) => (
                <option key={p.id} value={p.id}>
                  {p.category} · {p.name}
                </option>
              ))}
            </select>
            <span className="lab-palette-origin">
              {asset.origin === "satellite"
                ? "Satellite-derived CLUT"
                : asset.origin === "fantasy"
                  ? "Fantasy · authored CLUT"
                  : "Terrain-inspired · authored CLUT"}{" "}
              · COLOR ONLY
            </span>
          </div>
          <details className="lab-control-section" open>
            <summary>
              <span>01</span> Organic form <ChevronDown size={12} />
            </summary>
            <ValueSlider
              label="Form irregularity"
              value={settings.form}
              min={0}
              max={1}
              step={0.01}
              scale={100}
              unit="%"
              onChange={numeric("form")}
            />
            <ValueSlider
              label="Angular facets"
              value={settings.facets}
              min={0}
              max={1}
              step={0.01}
              scale={100}
              unit="%"
              onChange={numeric("facets")}
            />
            <div className="lab-seed">
              <label>
                Seed
                <input
                  type="number"
                  aria-label="Stone seed"
                  min={0}
                  max={99999}
                  value={settings.seed}
                  onChange={(e) => {
                    if (Number.isFinite(e.target.valueAsNumber))
                      change(
                        "seed",
                        Math.round(clamp(e.target.valueAsNumber, 0, 99999)),
                      );
                  }}
                />
              </label>
              <button
                aria-label="New stone seed"
                onClick={() =>
                  change(
                    "seed",
                    crypto.getRandomValues(new Uint32Array(1))[0] % 100000,
                  )
                }
              >
                <Shuffle size={14} />
              </button>
            </div>
          </details>
          <div className="lab-detail-toggle">
            <label className="lab-check">
              <input
                type="checkbox"
                checked={settings.detail}
                onChange={(e) => change("detail", e.target.checked)}
              />
              <span>SDF surface detail</span>
            </label>
            <small>CHANGES THE ZERO SURFACE</small>
          </div>
          <details className="lab-control-section" open>
            <summary>
              <span>02</span> Chips & cleavage <ChevronDown size={12} />
            </summary>
            <ValueSlider
              label="Chipped relief"
              value={settings.chips}
              min={0}
              max={12}
              step={0.25}
              unit="mm"
              onChange={numeric("chips")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Crack depth"
              value={settings.crackDepth}
              min={0}
              max={12}
              step={0.25}
              unit="mm"
              onChange={numeric("crackDepth")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Crack aperture"
              value={settings.crackWidth}
              min={0.2}
              max={2}
              step={0.05}
              unit="mm"
              onChange={numeric("crackWidth")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Crack branching"
              value={settings.crackBranching}
              min={0}
              max={1}
              step={0.01}
              scale={100}
              unit="%"
              onChange={numeric("crackBranching")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Fracture edge chipping"
              value={settings.crackChipping}
              min={0}
              max={1}
              step={0.01}
              scale={100}
              unit="%"
              onChange={numeric("crackChipping")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Crack spacing"
              value={settings.crackSpacing}
              min={45}
              max={180}
              step={5}
              unit="mm"
              onChange={numeric("crackSpacing")}
              disabled={!settings.detail}
            />
          </details>
          <details className="lab-control-section">
            <summary>
              <span>03</span> Bedding <ChevronDown size={12} />
            </summary>
            <ValueSlider
              label="Bedding relief"
              value={settings.bedding}
              min={0}
              max={6}
              step={0.1}
              unit="mm"
              onChange={numeric("bedding")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Layer spacing"
              value={settings.spacing}
              min={8}
              max={60}
              step={1}
              unit="mm"
              onChange={numeric("spacing")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Sheet breakup"
              value={settings.layerBreakup}
              min={0}
              max={1}
              step={0.01}
              scale={100}
              unit="%"
              onChange={numeric("layerBreakup")}
              disabled={!settings.detail}
            />
            <p>
              Uneven sheet thickness, chipped ledges and partial
              delamination—not wrapping sine-wave ribs.
            </p>
            <ValueSlider
              label="Bedding tilt"
              value={settings.tilt}
              min={-70}
              max={70}
              step={1}
              unit="°"
              onChange={numeric("tilt")}
              disabled={!settings.detail}
            />
          </details>
          <details className="lab-control-section" open>
            <summary>
              <span>04</span> Pores & grain <ChevronDown size={12} />
            </summary>
            <ValueSlider
              label="Pore coverage"
              value={settings.porosity}
              min={0}
              max={1}
              step={0.01}
              scale={100}
              unit="%"
              onChange={numeric("porosity")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Pore spacing"
              value={settings.poreSize}
              min={6}
              max={30}
              step={1}
              unit="mm"
              onChange={numeric("poreSize")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Pore irregularity"
              value={settings.poreIrregularity}
              min={0}
              max={1}
              step={0.01}
              scale={100}
              unit="%"
              onChange={numeric("poreIrregularity")}
              disabled={!settings.detail}
            />
            <p>
              Rotated, elongated and joined vesicles with uneven walls. Zero
              returns to round pores.
            </p>
            <ValueSlider
              label="Grain relief"
              value={settings.grain}
              min={0}
              max={1.5}
              step={0.05}
              unit="mm"
              onChange={numeric("grain")}
              disabled={!settings.detail}
            />
            <ValueSlider
              label="Grain size"
              value={settings.grainSize}
              min={0.6}
              max={6}
              step={0.1}
              unit="mm"
              onChange={numeric("grainSize")}
              disabled={!settings.detail}
            />
            <p>
              Zoom in to resolve finer grain. Sub-pixel bands fade out in the
              field, not into oversized noise.
            </p>
          </details>
          <details className="lab-control-section">
            <summary>
              <span>05</span> Light & response <Sun size={12} />
            </summary>
            <ValueSlider
              label="Roughness"
              value={settings.roughness}
              min={0.3}
              max={1}
              step={0.01}
              scale={100}
              unit="%"
              onChange={numeric("roughness")}
            />
            <ValueSlider
              label="Palette contrast"
              value={settings.contrast}
              min={0.5}
              max={2.5}
              step={0.05}
              onChange={numeric("contrast")}
            />
            <ValueSlider
              label="Palette bias"
              value={settings.bias}
              min={-0.4}
              max={0.4}
              step={0.01}
              onChange={numeric("bias")}
            />
            <ValueSlider
              label="Saturation"
              value={settings.saturation}
              min={0}
              max={1.5}
              step={0.01}
              scale={100}
              unit="%"
              onChange={numeric("saturation")}
            />
            <ValueSlider
              label="Light azimuth"
              value={settings.sunAzimuth}
              min={-180}
              max={180}
              step={1}
              unit="°"
              onChange={numeric("sunAzimuth")}
            />
            <ValueSlider
              label="Light elevation"
              value={settings.sunElevation}
              min={12}
              max={80}
              step={1}
              unit="°"
              onChange={numeric("sunElevation")}
            />
            <ValueSlider
              label="Exposure"
              value={settings.exposure}
              min={0.5}
              max={1.8}
              step={0.05}
              onChange={numeric("exposure")}
            />
          </details>
          <label className="lab-quality">
            Preview budget
            <select
              aria-label="Stone preview quality"
              value={settings.quality}
              onChange={(e) =>
                change("quality", e.target.value as StoneSettings["quality"])
              }
            >
              <option value="draft">Draft · lower GPU cost</option>
              <option value="balanced">Balanced</option>
              <option value="closeup">Close-up · more detail</option>
            </select>
          </label>
          <a
            className="lab-docs"
            href={`${import.meta.env.BASE_URL}research/sdf-stone-materials.md`}
            target="_blank"
            rel="noreferrer"
          >
            Method, limits & verification <ArrowUpRight size={12} />
          </a>
        </aside>
      </main>
      <footer className="lab-footer">
        <span>
          <i /> SDF GEOMETRY · NOT A SCANNED MATERIAL
        </span>
        <span>Separate experiment · terrain project unchanged</span>
      </footer>
      {notice && (
        <div className="lab-toast" role="status">
          {notice}
          <button aria-label="Dismiss message" onClick={() => setNotice("")}>
            <X size={14} />
          </button>
        </div>
      )}
    </div>
  );
}
