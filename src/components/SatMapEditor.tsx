import { useEffect, useRef, useState } from "react";
import {
  Satellite,
  Upload,
  Download,
  RotateCcw,
  ScanLine,
  SlidersHorizontal,
  ExternalLink,
  Layers3,
  Check,
  ArrowRight,
} from "lucide-react";
import { Slider, SectionHeading, Toggle } from "./Controls";
import {
  SATMAP_LIBRARY,
  getSatmap,
  paletteGradient,
} from "../engine/satmaps/satmap";
import { importSatmapFile } from "../engine/satmaps/import";
import { paletteRGBA, type PaletteExtraction } from "../engine/satmaps/pixels";
import { download } from "../engine/project";
import {
  DEFAULT_SETTINGS,
  type SatmapId,
  type SatmapView,
  type Settings,
} from "../engine/types";

export const MAP_PREVIEWS: { id: SatmapView; label: string; hint: string }[] = [
  {
    id: "beauty",
    label: "Surface",
    hint: "Satellite albedo with terrain lighting, source-image relief and water.",
  },
  {
    id: "albedo",
    label: "Albedo",
    hint: "Unlit satellite color. No sun, shadows, exposure or water in this view.",
  },
  {
    id: "texture",
    label: "Texture mask",
    hint: "The mixed data mask before color remapping. Dark → low end of the palette; light → high end.",
  },
  {
    id: "height",
    label: "Height",
    hint: "Elevation normalized to this terrain’s current upper-surface range.",
  },
  {
    id: "slope",
    label: "Slope",
    hint: "Black = flat upward surface. White = steep cliff or underside. Derived from the SDF normal.",
  },
  {
    id: "curvature",
    label: "Curvature",
    hint: "Black = concave, mid-gray = flat, white = convex. A signed local SDF curvature proxy.",
  },
  {
    id: "ao",
    label: "Occlusion",
    hint: "SDF ambient occlusion: dark in sheltered cavities, light on exposed surfaces.",
  },
  {
    id: "normals",
    label: "Normals",
    hint: "World-space geometric normals, RGB = XYZ. Not a tangent-space normal texture.",
  },
  {
    id: "flow",
    label: "Flow",
    hint: "Terrain-derived D8 catchment plus live simulation runoff. Closed basins retain flow; roofs do not project onto cave floors.",
  },
  {
    id: "sediment",
    label: "Deposition",
    hint: "Actual deposited material from signed simulation displacement. Initially black: run erosion and let sediment settle. Shows net deposition, not suspended sediment.",
  },
  {
    id: "detail",
    label: "Photo detail",
    hint: "Triplanar source-image luminance. This artistic detail is not a measured height or normal map.",
  },
];
const WEIGHTS: {
  key:
    | "satmapHeight"
    | "satmapSlope"
    | "satmapCurvature"
    | "satmapAO"
    | "satmapFlow"
    | "satmapSediment"
    | "satmapDetail";
  label: string;
  help: string;
}[] = [
  {
    key: "satmapHeight",
    label: "Elevation influence",
    help: "Distribute colors by normalized terrain height.",
  },
  {
    key: "satmapSlope",
    label: "Slope influence",
    help: "Separate steep exposed rock from flatter ground using actual surface normals.",
  },
  {
    key: "satmapCurvature",
    label: "Curvature influence",
    help: "Separate convex ridges from concave gullies, including volumetric formations.",
  },
  {
    key: "satmapAO",
    label: "Cavity influence",
    help: "Use geometric occlusion to select sheltered colors. Lighting AO remains separate.",
  },
  {
    key: "satmapFlow",
    label: "Flow influence",
    help: "Darken the palette coordinate along terrain catchments and actual runoff.",
  },
  {
    key: "satmapSediment",
    label: "Deposition influence",
    help: "Select the light end of the palette where simulated sediment has settled.",
  },
  {
    key: "satmapDetail",
    label: "Photo detail influence",
    help: "Add real source-image luminance to the texture mask, not procedural color noise.",
  },
];

export function SatMapEditor({
  settings: s,
  onChange,
}: {
  settings: Settings;
  onChange: (patch: Partial<Settings>) => void;
}) {
  const asset = getSatmap(s),
    selected = SATMAP_LIBRARY.find((item) => item.id === s.satmap);
  const [importMode, setImportMode] = useState<PaletteExtraction>("photo");
  const [importing, setImporting] = useState(false);
  const [message, setMessage] = useState("");
  const [failed, setFailed] = useState(false);
  const fileRef = useRef<HTMLInputElement>(null);
  const importSequence = useRef(0);
  useEffect(
    () => () => {
      importSequence.current++;
    },
    [],
  );
  const preview = MAP_PREVIEWS.find((v) => v.id === s.satmapPreview)!;
  const set = <K extends keyof Settings>(key: K, value: Settings[K]) =>
    onChange({ [key]: value });
  const resetColor = () =>
    onChange({
      satmapBias: 0,
      satmapContrast: DEFAULT_SETTINGS.satmapContrast,
      satmapLow: DEFAULT_SETTINGS.satmapLow,
      satmapHigh: DEFAULT_SETTINGS.satmapHigh,
      satmapReverse: false,
      satmapSaturation: DEFAULT_SETTINGS.satmapSaturation,
    });
  const importImage = async (file?: File) => {
    if (!file) return;
    const token = ++importSequence.current;
    setImporting(true);
    setMessage("");
    try {
      const patch = await importSatmapFile(file, importMode);
      if (token !== importSequence.current) return;
      onChange(patch);
      setFailed(false);
      setMessage(
        `${patch.satmapName} imported. Palette and detail are embedded when you save the project.`,
      );
    } catch (error) {
      if (token === importSequence.current) {
        setMessage(error instanceof Error ? error.message : String(error));
        setFailed(true);
      }
    } finally {
      if (token === importSequence.current) setImporting(false);
    }
  };
  const exportPalette = async () => {
    const canvas = document.createElement("canvas");
    canvas.width = 256;
    canvas.height = 1;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    ctx.putImageData(
      new ImageData(new Uint8ClampedArray(paletteRGBA(asset.palette)), 256, 1),
      0,
      0,
    );
    const blob = await new Promise<Blob | null>((resolve) =>
      canvas.toBlob(resolve, "image/png"),
    );
    if (blob) download(blob, `frontier-satmap-${s.satmap}.png`);
  };
  return (
    <div className="satmap-editor">
      <div
        className="satmap-pipeline"
        aria-label="Terrain masks to satellite palette to surface color"
      >
        <span>
          <ScanLine size={12} /> Terrain data
        </span>
        <ArrowRight size={11} />
        <span>
          <Satellite size={12} /> SatMap
        </span>
        <ArrowRight size={11} />
        <span>Albedo</span>
      </div>
      <SectionHeading detail={<span className="satmap-count">04 MAPS</span>}>
        <Satellite size={14} /> Satellite library
      </SectionHeading>
      <div
        className="satmap-library"
        role="group"
        aria-label="Satellite palette library"
      >
        {SATMAP_LIBRARY.map((item, index) => (
          <button
            key={item.id}
            className={`satmap-card ${s.satmap === item.id ? "selected" : ""}`}
            aria-pressed={s.satmap === item.id}
            title={`${item.region} · ${item.note}`}
            onClick={() => {
              importSequence.current++;
              setImporting(false);
              onChange({ satmap: item.id as SatmapId });
              setMessage("");
            }}
          >
            <div className="satmap-thumbnail">
              <img
                src={`${import.meta.env.BASE_URL}satmaps/${item.id}.webp`}
                alt=""
              />
              <span className="satmap-category">{item.category}</span>
              <span className="satmap-card-index">0{index + 1}</span>
              {s.satmap === item.id && (
                <i>
                  <Check size={11} />
                </i>
              )}
            </div>
            <strong>{item.name}</strong>
            <small>{item.region}</small>
          </button>
        ))}
      </div>
      {s.satmapPalette && (
        <button
          className="satmap-custom"
          aria-pressed={s.satmap === "custom"}
          onClick={() => onChange({ satmap: "custom" })}
        >
          <span style={{ background: paletteGradient(s.satmapPalette) }} />
          <strong>{s.satmapName || "Imported palette"}</strong>
          <small>Custom</small>
          {s.satmap === "custom" && <Check size={12} />}
        </button>
      )}
      <div className="satmap-ramp-header">
        <strong>{asset.name}</strong>
        <span>256 samples · sRGB</span>
      </div>
      <div
        className="satmap-ramp"
        style={{ background: paletteGradient(asset.palette) }}
        aria-label={`${asset.name} satellite color palette`}
      >
        <i
          className="satmap-clip low"
          style={{ left: `${s.satmapLow * 100}%` }}
        />
        <i
          className="satmap-clip high"
          style={{ left: `${s.satmapHigh * 100}%` }}
        />
      </div>
      <div className="satmap-ramp-labels">
        <span>Low values</span>
        <span>High values</span>
      </div>
      <div className="satmap-source-line">
        {selected ? (
          <a href={selected.source} target="_blank" rel="noopener noreferrer">
            USGS / NASA · satellite composite <ExternalLink size={10} />
          </a>
        ) : (
          <span>Imported locally · embedded in project</span>
        )}
        <button
          onClick={() => void exportPalette()}
          title="Export the original 256 × 1 color strip"
          aria-label="Export satellite palette PNG"
        >
          <Download size={13} />
        </button>
      </div>
      <details className="satmap-import process-details">
        <summary>
          <Upload size={12} /> Import your own map
        </summary>
        <div
          className="satmap-import-modes"
          role="group"
          aria-label="Satellite import method"
        >
          <button
            aria-pressed={importMode === "photo"}
            onClick={() => setImportMode("photo")}
          >
            Reference image
          </button>
          <button
            aria-pressed={importMode === "strip"}
            onClick={() => setImportMode("strip")}
          >
            Color strip
          </button>
        </div>
        <p>
          {importMode === "photo"
            ? "Extract a color map from your satellite or aerial image, ordered by luminance."
            : "Read a horizontal or vertical LUT strip in its original order. No luminance sorting."}
        </p>
        <input
          ref={fileRef}
          type="file"
          accept="image/png,image/jpeg,image/webp"
          aria-label="Import satellite image"
          className="satmap-file"
          onChange={(e) => {
            void importImage(e.target.files?.[0]);
            e.target.value = "";
          }}
        />
        <button
          className="satmap-import-button"
          disabled={importing}
          onClick={() => fileRef.current?.click()}
        >
          <Upload size={13} />
          {importing ? "Extracting colors…" : "Choose image"}
          <small>PNG / JPG / WebP</small>
        </button>
        <p className="satmap-local-note">
          Local processing only · up to 12 MB. Use imagery you have rights to.
        </p>
      </details>
      {message && (
        <p
          className={`satmap-import-message ${failed ? "error" : ""}`}
          role={failed ? "alert" : "status"}
        >
          {message}
        </p>
      )}
      <section className="satmap-section">
        <SectionHeading
          detail={
            <span className="satmap-live">
              <i /> LIVE DATA
            </span>
          }
        >
          <ScanLine size={14} /> Inspect maps
        </SectionHeading>
        <div
          className="satmap-previews"
          role="group"
          aria-label="Terrain texture map preview"
        >
          {MAP_PREVIEWS.map((view) => (
            <button
              key={view.id}
              aria-pressed={s.satmapPreview === view.id && s.view === "lit"}
              onClick={() => onChange({ satmapPreview: view.id, view: "lit" })}
              title={view.hint}
            >
              {view.label}
            </button>
          ))}
        </div>
        <p className="satmap-view-hint">
          {s.view === "lit"
            ? preview.hint
            : "Choose a map above to leave Clay / Flow view and inspect texturing data."}
        </p>
      </section>
      <details className="satmap-section satmap-mixer process-details" open>
        <summary>
          <SlidersHorizontal size={13} /> Terrain mask mixer
        </summary>
        <div
          className="satmap-mix-presets"
          role="group"
          aria-label="Terrain texture recipes"
        >
          <button
            onClick={() =>
              onChange(
                Object.fromEntries(
                  WEIGHTS.map(({ key }) => [key, DEFAULT_SETTINGS[key]]),
                ),
              )
            }
          >
            Balanced
          </button>
          <button
            onClick={() =>
              onChange({
                satmapHeight: 0.2,
                satmapSlope: 0.9,
                satmapCurvature: 0.85,
                satmapAO: 0.5,
                satmapFlow: 0.3,
                satmapSediment: 0.5,
                satmapDetail: 0.65,
              })
            }
          >
            Exposed rock
          </button>
          <button
            onClick={() =>
              onChange({
                satmapHeight: 0.25,
                satmapSlope: 0.5,
                satmapCurvature: 0.35,
                satmapAO: 0.2,
                satmapFlow: 0.95,
                satmapSediment: 1,
                satmapDetail: 0.85,
              })
            }
          >
            Alluvial
          </button>
        </div>
        {WEIGHTS.map(({ key, label, help }) => (
          <Slider
            key={key}
            label={label}
            value={s[key]}
            help={help}
            onChange={(v) => set(key, v)}
          />
        ))}
        <p className="satmap-view-hint">
          Topography selects the palette. Flow darkens channels; settled
          sediment selects lighter colors. No terrain regeneration.
        </p>
      </details>
      <section className="satmap-section">
        <SectionHeading
          detail={
            <button
              className="icon-button"
              aria-label="Reset satellite color mapping"
              onClick={resetColor}
            >
              <RotateCcw size={13} />
            </button>
          }
        >
          Color mapping
        </SectionHeading>
        <Slider
          label="Palette bias"
          value={s.satmapBias}
          min={-1}
          max={1}
          step={0.02}
          format={(v) => `${v > 0 ? "+" : ""}${v.toFixed(2)}`}
          onChange={(v) => set("satmapBias", v)}
        />
        <Slider
          label="Input contrast"
          value={s.satmapContrast}
          min={0.25}
          max={4}
          step={0.05}
          format={(v) => `${v.toFixed(2)}×`}
          onChange={(v) => set("satmapContrast", v)}
        />
        <div className="satmap-clip-controls">
          <Slider
            label="Clip low"
            value={s.satmapLow}
            min={0}
            max={0.99}
            onChange={(v) => set("satmapLow", Math.min(v, s.satmapHigh - 0.01))}
          />
          <Slider
            label="Clip high"
            value={s.satmapHigh}
            min={0.01}
            max={1}
            onChange={(v) => set("satmapHigh", Math.max(v, s.satmapLow + 0.01))}
          />
        </div>
        <Slider
          label="Palette saturation"
          value={s.satmapSaturation}
          min={0}
          max={2}
          onChange={(v) => set("satmapSaturation", v)}
        />
        <div className="simple-toggle-row">
          <span>Reverse palette</span>
          <Toggle
            small
            label="Reverse satellite palette"
            checked={s.satmapReverse}
            onChange={(v) => set("satmapReverse", v)}
          />
        </div>
      </section>
      <details className="satmap-section process-details">
        <summary>
          <Layers3 size={13} /> Image detail & surface response
        </summary>
        <Slider
          label="Photo detail scale"
          value={s.satmapScale}
          min={0.5}
          max={24}
          step={0.5}
          format={(v) => `${v.toFixed(1)} m`}
          onChange={(v) => set("satmapScale", v)}
        />
        <Slider
          label="Photo relief"
          value={s.satmapRelief}
          min={0}
          max={80}
          step={1}
          format={(v) => `${v} mm`}
          onChange={(v) => set("satmapRelief", v)}
          help="Artistic bump from source luminance. Not measured relief and not exported geometry."
        />
        <Slider
          label="Surface roughness"
          value={s.materialRoughness}
          min={0.12}
          max={1}
          onChange={(v) => set("materialRoughness", v)}
        />
        <Slider
          label="Surface moisture"
          value={s.materialMoisture}
          onChange={(v) => set("materialMoisture", v)}
        />
        <p className="satmap-view-hint">
          Image detail is mip-filtered and triplanar, including cliffs and
          overhangs. It is not a scanned PBR material.
        </p>
      </details>
      <a
        className="material-research-link"
        href={`${import.meta.env.BASE_URL}research/satellite-texturing.md`}
        target="_blank"
        rel="noopener noreferrer"
      >
        How it works, sources & limitations <ExternalLink size={11} />
      </a>
    </div>
  );
}
