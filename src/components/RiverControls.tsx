import { Slider } from "./Controls";
import type { Settings } from "../engine/types";

export function RiverControls({
  settings,
  onChange,
}: {
  settings: Settings;
  onChange: <K extends keyof Settings>(key: K, value: Settings[K]) => void;
}) {
  return (
    <details className="process-details river-controls" open>
      <summary>Current & ripple shape</summary>
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
        disabled={!settings.water}
        help="Surface flow direction: 0° follows +X and 90° follows +Z."
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
      <p>
        Non-repeating surface flow. Wind adds smaller ripples; current carries
        the larger patterns.
      </p>
    </details>
  );
}
