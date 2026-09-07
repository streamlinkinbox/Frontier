import { Layers3, Gem, RotateCcw, Droplets } from "lucide-react";
import { Slider, SectionHeading } from "./Controls";
import { MATERIAL_PRESETS, dielectricF0 } from "../engine/materials";
import type { Settings } from "../engine/types";

export function MaterialEditor({
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
        <h2>More than a color.</h2>
        <p>Choose a mineral structure, then tune its surface.</p>
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
          label="Surface relief"
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
