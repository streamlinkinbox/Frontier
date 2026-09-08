import { Slider, Toggle } from "./Controls";
import type { Settings } from "../engine/types";

export function RiverControls({
  settings,
  onChange,
}: {
  settings: Settings;
  onChange: <K extends keyof Settings>(key: K, value: Settings[K]) => void;
}) {
  const channel =
    settings.waterFlowMode === "channel" && settings.preset === "canyon";
  return (
    <details className="process-details river-controls" open>
      <summary>Downstream current</summary>
      <div
        className="river-route-controls"
        role="group"
        aria-label="River flow route"
      >
        <button
          aria-pressed={channel}
          disabled={!settings.water || settings.preset !== "canyon"}
          onClick={() => onChange("waterFlowMode", "channel")}
        >
          Follow canyon
        </button>
        <button
          aria-pressed={!channel}
          disabled={!settings.water}
          onClick={() => onChange("waterFlowMode", "directional")}
        >
          Compass heading
        </button>
      </div>
      <div className="downstream-label">
        {settings.waterFlowMode === "channel" && settings.preset === "canyon"
          ? settings.waterReverse
            ? "Downstream: −Z → +Z, along the canyon"
            : "Downstream: +Z → −Z, along the canyon"
          : "Direction follows the compass heading below"}
      </div>
      <div className="simple-toggle-row">
        <span>Reverse current</span>
        <Toggle
          label="Reverse downstream current"
          checked={settings.waterReverse}
          onChange={(v) => onChange("waterReverse", v)}
        />
      </div>
      <Slider
        label="Current speed"
        value={settings.waterCurrent}
        min={0}
        max={2.5}
        step={0.05}
        format={(v) => `${v.toFixed(2)} m/s`}
        onChange={(v) => onChange("waterCurrent", v)}
        disabled={!settings.water}
        help="Advects the procedural river surface downstream; this does not change the erosion solver."
      />
      <Slider
        label="Flow direction"
        value={settings.waterDirection}
        min={0}
        max={360}
        step={5}
        format={(v) => `${v}°`}
        onChange={(v) => onChange("waterDirection", v)}
        disabled={
          !settings.water ||
          (settings.waterFlowMode === "channel" && settings.preset === "canyon")
        }
        help="Compass mode: 0° follows +X and 90° follows +Z. Channel mode follows the seeded canyon."
      />
      <Slider
        label="Ripple scale"
        value={settings.waterRippleScale}
        min={0.5}
        max={4}
        step={0.1}
        format={(v) => `${v.toFixed(1)} m`}
        onChange={(v) => onChange("waterRippleScale", v)}
        disabled={!settings.water}
        help="World-space ripple size. The field is warped and advected, not a repeating texture tile."
      />
      <Slider
        label="Current streaks"
        value={settings.waterStreaks}
        onChange={(v) => onChange("waterStreaks", v)}
        disabled={!settings.water || settings.foamQuality !== "low"}
        help="Low-only procedural streaks. Higher tiers use the evolving foam density and particle population instead."
      />
      <p>
        The route drives the current. Use compass mode for sculpted courses.
        Higher foam tiers add obstacle-aware GPU transport; this is separate
        from the volumetric erosion solver.
      </p>
    </details>
  );
}
