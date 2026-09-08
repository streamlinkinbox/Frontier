import { Layers3, Gem, RotateCcw, Droplets } from "lucide-react";
import { Slider, SectionHeading } from "./Controls";
import { MATERIAL_PRESETS, dielectricF0 } from "../engine/materials";
import { SatMapEditor } from "./SatMapEditor";
import type { Settings } from "../engine/types";

function LegacyMaterialEditor({
  settings,
  onChange,
}: {
  settings: Settings;
  onChange: (patch: Partial<Settings>) => void;
}) {
  const selected = MATERIAL_PRESETS.find((p) => p.id === settings.material)!;
  const set = <K extends keyof Settings>(key: K, value: Settings[K]) =>
    onChange({ [key]: value });
  const sedimentary =
    settings.material === "sandstone" || settings.material === "limestone";
  return (
    <>
      <div className="inspector-intro material-intro">
        <span className="eyebrow">SURFACE LAB · DIELECTRIC PBR</span>
        <h2>Surface material</h2>
        <p>Choose a rock type and refine its optical response.</p>
      </div>
      <div
        className="material-presets"
        role="group"
        aria-label="Surface material presets"
      >
        {MATERIAL_PRESETS.map((p) => (
          <button
            key={p.id}
            className={`material-preset ${p.id}`}
            aria-pressed={settings.material === p.id}
            onClick={() =>
              onChange({ material: p.id, ...p.values, detail: 0.85 })
            }
            title={p.description}
          >
            <span className={`material-chip chip-${p.id}`} aria-hidden="true" />
            <strong>{p.name}</strong>
            <small>
              {p.id === "granite"
                ? "Interlocking crystals"
                : p.id === "basalt"
                  ? "Fine volcanic matrix"
                  : p.id === "limestone"
                    ? "Calcite · soft mottling"
                    : "Cemented grains"}
            </small>
          </button>
        ))}
      </div>
      <div className="environment-section material-rock-detail">
        <SectionHeading>
          <Layers3 size={14} />
          Layered rock detail
        </SectionHeading>
        <div className="rock-detail-presets" aria-label="Rock noise presets">
          <button
            onClick={() =>
              onChange({
                rockRelief: 2,
                rockNoiseScale: 0.8,
                rockOctaves: 3,
                rockRidges: 0.3,
                rockLayerRelief: 1,
              })
            }
          >
            Subtle
          </button>
          <button
            onClick={() =>
              onChange({
                rockRelief: 5.5,
                rockNoiseScale: 0.55,
                rockOctaves: 4,
                rockRidges: 0.6,
                rockLayerSpacing: 0.42,
                rockLayerRelief: 2.8,
                rockLayerWarp: 0.65,
              })
            }
          >
            Layered
          </button>
          <button
            onClick={() =>
              onChange({
                rockRelief: 9,
                rockNoiseScale: 0.38,
                rockOctaves: 5,
                rockRidges: 0.8,
                rockLayerSpacing: 0.28,
                rockLayerRelief: 3.8,
                rockLayerWarp: 0.85,
              })
            }
          >
            Weathered
          </button>
        </div>
        <Slider
          label="Rock relief"
          value={settings.rockRelief}
          min={0}
          max={15}
          step={0.5}
          format={(v) => `${v.toFixed(1)} cm`}
          onChange={(v) => set("rockRelief", v)}
          help="Centimeter-scale domain-warped rock relief. Shading detail, not a change to the voxel volume."
        />
        <Slider
          label="Noise scale"
          value={settings.rockNoiseScale}
          min={0.1}
          max={2}
          step={0.05}
          format={(v) => `${v.toFixed(2)} m`}
          onChange={(v) => set("rockNoiseScale", v)}
        />
        <Slider
          label="Layer spacing"
          value={settings.rockLayerSpacing}
          min={0.08}
          max={2}
          step={0.02}
          format={(v) => `${v.toFixed(2)} m`}
          onChange={(v) => set("rockLayerSpacing", v)}
          disabled={!sedimentary}
        />
        <Slider
          label="Layer relief"
          value={settings.rockLayerRelief}
          min={0}
          max={8}
          step={0.2}
          format={(v) => `${v.toFixed(1)} cm`}
          onChange={(v) => set("rockLayerRelief", v)}
          disabled={!sedimentary}
        />
        <details className="process-details">
          <summary>Noise shaping</summary>
          <Slider
            label="Noise octaves"
            value={settings.rockOctaves}
            min={1}
            max={5}
            step={1}
            format={(v) => String(v)}
            onChange={(v) => set("rockOctaves", v)}
          />
          <Slider
            label="Ridge strength"
            value={settings.rockRidges}
            onChange={(v) => set("rockRidges", v)}
          />
          <Slider
            label="Layer warp"
            value={settings.rockLayerWarp}
            onChange={(v) => set("rockLayerWarp", v)}
          />
        </details>
        <p className="material-scale-note">
          Broad rock relief, broken strata and fine grain are separate scales.
          Switch to Clay to see which features are shader relief versus saved
          geometry.
        </p>
      </div>
      <div className="environment-section material-surface-controls">
        <SectionHeading
          detail={
            <button
              className="icon-button"
              aria-label="Reset material parameters"
              onClick={() => onChange({ ...selected.values, detail: 0.85 })}
              title="Reset this material, not the terrain"
            >
              <RotateCcw size={14} />
            </button>
          }
        >
          <Gem size={14} />
          {selected.name}
        </SectionHeading>
        <div className="material-color-control">
          <label htmlFor="material-color">Base color</label>
          <input
            id="material-color"
            aria-label="Material base color"
            type="color"
            value={settings.materialColor}
            onChange={(e) => set("materialColor", e.target.value)}
          />
          <output>{settings.materialColor.toUpperCase()}</output>
        </div>
        <Slider
          label="Surface roughness"
          value={settings.materialRoughness}
          min={0.12}
          max={1}
          step={0.01}
          onChange={(v) => set("materialRoughness", v)}
          help="Controls the GGX reflection lobe; matte to polished. Not a noise amplitude."
        />
        <Slider
          label="Grain size"
          value={settings.materialGrain}
          min={0.05}
          max={20}
          step={0.05}
          format={(v) => `${v.toFixed(v < 1 ? 2 : 1)} mm`}
          onChange={(v) => set("materialGrain", v)}
          help="Physical grain/crystal size. Unresolved grains average into the material rather than becoming large spots."
        />
        <Slider
          label="Micro relief"
          value={settings.materialRelief}
          min={0}
          max={8}
          step={0.1}
          format={(v) => `${v.toFixed(1)} mm`}
          onChange={(v) => set("materialRelief", v)}
          help="Shaded pore/grain relief, not new mesh geometry."
        />
        <p className="material-scale-note">
          Grains are millimeters, not boulders. Fly close to resolve them; at a
          distance they contribute to the surface response.
        </p>
      </div>
      <details className="process-details material-advanced" open>
        <summary>Structure & weathering</summary>
        <Slider
          label="Mottle / pore scale"
          value={settings.materialScale}
          min={0.25}
          max={4}
          step={0.05}
          format={(v) => `${v.toFixed(2)}×`}
          onChange={(v) => set("materialScale", v)}
        />
        <Slider
          label="Pore coverage"
          value={settings.materialPorosity}
          onChange={(v) => set("materialPorosity", v)}
          help="Procedural coverage of surface pores; not a measured bulk porosity."
        />
        <Slider
          label="Layer contrast"
          value={settings.materialBedding}
          onChange={(v) => set("materialBedding", v)}
          disabled={!sedimentary}
          help="Sedimentary bedding is not applied to the igneous presets."
        />
        <Slider
          label="Mineral weathering"
          value={settings.materialWeathering}
          onChange={(v) => set("materialWeathering", v)}
          help="Pigment/mottling variation. Does not change erosion or sculpted geometry."
        />
      </details>
      <div className="environment-section">
        <SectionHeading>
          <Droplets size={14} />
          Optical response
        </SectionHeading>
        <Slider
          label="Refractive index"
          value={settings.materialIOR}
          min={1.3}
          max={1.8}
          step={0.01}
          format={(v) => v.toFixed(2)}
          onChange={(v) => set("materialIOR", v)}
        />
        <div className="material-f0">
          Normal-incidence reflection{" "}
          <strong>
            {(dielectricF0(settings.materialIOR) * 100).toFixed(1)}%
          </strong>{" "}
          · metalness 0
        </div>
        <Slider
          label="Surface moisture"
          value={settings.materialMoisture}
          onChange={(v) => set("materialMoisture", v)}
          help="Adds a water-film response to automatic shoreline/runoff wetness."
        />
        <Slider
          label="Surface detail"
          value={settings.detail}
          min={0}
          max={1.5}
          step={0.05}
          onChange={(v) => set("detail", v)}
          help="Final normal-detail gain, in addition to the physical relief scale."
        />
        <p className="material-scale-note">
          Moisture darkens porous surfaces and tightens their reflections. These
          are procedural, research-informed presets—not measured scans.
        </p>
      </div>
      <a
        className="material-research-link"
        href={`${import.meta.env.BASE_URL}research/materials-and-water.md`}
        target="_blank"
        rel="noopener noreferrer"
      >
        Research, sources & limitations ↗
      </a>
      <div className="parameter-note material-preserve-note">
        <Layers3 size={17} />
        <p>
          <strong>Your shape stays yours.</strong>Material changes do not
          regenerate the volume or alter erosion settings.
        </p>
      </div>
    </>
  );
}

export function MaterialEditor({
  settings,
  onChange,
}: {
  settings: Settings;
  onChange: (patch: Partial<Settings>) => void;
}) {
  return (
    <>
      <div className="inspector-intro satmap-intro">
        <span className="eyebrow">SURFACE LAB · TERRAIN-DRIVEN COLOR</span>
        <h2>Terrain texturing</h2>
        <p>Real-world palettes. Shaped by your terrain.</p>
      </div>
      <div
        className="texture-mode"
        role="group"
        aria-label="Terrain texturing method"
      >
        <button
          aria-pressed={settings.textureMode === "satmap"}
          onClick={() => onChange({ textureMode: "satmap" })}
        >
          SatMaps <span>COLOR MAPS</span>
        </button>
        <button
          aria-pressed={settings.textureMode === "legacy"}
          onClick={() =>
            onChange({ textureMode: "legacy", satmapPreview: "beauty" })
          }
        >
          Legacy rock
        </button>
      </div>
      {settings.textureMode === "satmap" ? (
        <SatMapEditor settings={settings} onChange={onChange} />
      ) : (
        <LegacyMaterialEditor settings={settings} onChange={onChange} />
      )}
    </>
  );
}
