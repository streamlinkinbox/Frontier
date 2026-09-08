import { useEffect, useRef, useState } from "react";
import { Waves, RotateCcw, ExternalLink, Pause, Play } from "lucide-react";
import { Slider, SectionHeading } from "./Controls";
import { FOAM_QUALITIES, FOAM_VIEWS, type Settings } from "../engine/types";
import { foamProfile } from "../engine/foam/model";

const tiers = {
  low: {
    name: "Low",
    tag: "LEGACY",
    text: "Stateless shore foam and procedural streaks. Minimum-memory fallback.",
  },
  standard: {
    name: "Standard",
    tag: "TRANSPORT",
    text: "Persistent density, advection, accumulation and lifetime. The GTX-first starting point.",
  },
  ultra: {
    name: "Ultra",
    tag: "PARTICLES",
    text: "GPU surface particles form coherent patches and filaments. A bounded flow field responds to the edited banks.",
  },
  cinematic: {
    name: "Cinematic",
    tag: "WHITEWATER",
    text: "Localized spray, surface foam and rising bubbles, with occlusion and underwater attenuation. Higher cost; no RTX-only requirement.",
  },
};
const views = {
  surface: "Surface",
  density: "Density",
  age: "Age",
  velocity: "Velocity",
  sources: "Sources",
  obstacles: "Banks & bed",
};
type Status = {
  quality?: string;
  supported?: boolean;
  fallback?: string;
  ticks?: number;
  particleCapacity?: number;
  bytes?: number;
  mapResolution?: number;
  droppedSeconds?: number;
  budget?: string;
};
export function FoamControls({
  settings,
  onChange,
  onReset,
  getStatus,
}: {
  settings: Settings;
  onChange: <K extends keyof Settings>(key: K, value: Settings[K]) => void;
  onReset: () => void;
  getStatus: () => unknown;
}) {
  const [status, setStatus] = useState<Status>({});
  const read = useRef(getStatus);
  read.current = getStatus;
  useEffect(() => {
    const refresh = () => setStatus((read.current() as Status) ?? {});
    refresh();
    const timer = setInterval(refresh, 1000);
    return () => clearInterval(timer);
  }, []);
  const tier = tiers[settings.foamQuality],
    profile = foamProfile(settings.foamQuality, settings.foamBudget);
  const active = settings.foamQuality !== "low",
    disabled = !settings.water || !active;
  return (
    <section className="foam-controls" aria-label="Foam simulation">
      <SectionHeading detail={<span className="foam-gpu-tag">GPU STATE</span>}>
        <Waves size={14} /> Foam simulation
      </SectionHeading>
      <div className="foam-tiers" role="group" aria-label="Foam quality">
        {FOAM_QUALITIES.map((id) => (
          <button
            key={id}
            className={settings.foamQuality === id ? "selected" : ""}
            aria-pressed={settings.foamQuality === id}
            onClick={() => onChange("foamQuality", id)}
            disabled={!settings.water}
          >
            <strong>{tiers[id].name}</strong>
            <small>{tiers[id].tag}</small>
          </button>
        ))}
      </div>
      <p className="foam-description">{tier.text}</p>
      {status.supported === false && active && (
        <p className="foam-warning" role="status">
          {status.fallback ||
            "Using Low: this renderer cannot create floating-point foam targets."}
        </p>
      )}
      {active && (
        <>
          <label className="foam-budget">
            Workload budget
            <select
              aria-label="Foam workload budget"
              value={settings.foamBudget}
              disabled={!settings.water}
              onChange={(e) =>
                onChange("foamBudget", e.target.value as Settings["foamBudget"])
              }
            >
              <option value="compact">Compact · lower load</option>
              <option value="balanced">Balanced · GTX starting point</option>
              <option value="expanded">Expanded · more GPU headroom</option>
            </select>
          </label>
          <div className="foam-stats" aria-label="Foam allocation">
            <span>{profile.map}² map</span>
            <span>
              {profile.particles
                ? `${(profile.particles / 1024).toFixed(0)}k particle slots`
                : "Density transport"}
            </span>
            {status.quality === settings.foamQuality &&
              status.budget === settings.foamBudget &&
              typeof status.bytes === "number" && (
                <span>~{(status.bytes / 1048576).toFixed(1)} MiB</span>
              )}
          </div>
          <div className="foam-actions">
            <button
              aria-pressed={settings.foamPaused}
              onClick={() => onChange("foamPaused", !settings.foamPaused)}
              disabled={!settings.water}
            >
              {settings.foamPaused ? <Play size={12} /> : <Pause size={12} />}
              {settings.foamPaused ? "Resume foam" : "Pause foam"}
            </button>
            <button
              onClick={onReset}
              disabled={!settings.water}
              title="Clear foam history only. Does not reset or erode the terrain."
            >
              <RotateCcw size={12} /> Restart foam
            </button>
          </div>
          <Slider
            label="Foam half-life"
            value={settings.foamLifetime}
            min={1}
            max={20}
            step={0.5}
            format={(v) => `${v.toFixed(1)} s`}
            onChange={(v) => onChange("foamLifetime", v)}
            disabled={disabled}
            help="Standard density half-life; sets the randomized particle lifetime scale in Ultra/Cinematic. Not tied to viewport FPS."
          />
          <Slider
            label="Flow stirring"
            value={settings.foamTurbulence}
            onChange={(v) => onChange("foamTurbulence", v)}
            disabled={disabled || settings.foamQuality === "standard"}
            help="Bounded surface-flow stirring in Ultra/Cinematic. This does not change the volumetric erosion solver."
          />
          <Slider
            label="Bubble detail scale"
            value={settings.foamDetailScale}
            min={0.2}
            max={2}
            step={0.05}
            format={(v) => `${v.toFixed(2)} m`}
            onChange={(v) => onChange("foamDetailScale", v)}
            disabled={disabled}
            help="Appearance scale of the filtered microstructure atlas, not a claim to simulate individual bubble films."
          />
          {settings.foamQuality === "cinematic" && (
            <>
              <Slider
                label="Airborne spray"
                value={settings.foamSpray}
                onChange={(v) => onChange("foamSpray", v)}
                disabled={disabled}
              />
              <Slider
                label="Entrained bubbles"
                value={settings.foamBubbles}
                onChange={(v) => onChange("foamBubbles", v)}
                disabled={disabled}
              />
            </>
          )}
          <details className="foam-inspect" open>
            <summary>Inspect water maps</summary>
            <div role="group" aria-label="Foam inspection views">
              {FOAM_VIEWS.map((id) => (
                <button
                  key={id}
                  aria-label={`Foam ${views[id].toLowerCase()}`}
                  aria-pressed={settings.foamView === id}
                  className={settings.foamView === id ? "selected" : ""}
                  onClick={() => onChange("foamView", id)}
                  disabled={disabled}
                >
                  {views[id]}
                </button>
              ))}
            </div>
          </details>
          <p className="foam-history-note">
            Tier/budget changes restart transient foam, not terrain. Projects
            save controls and reseed foam when reopened. Simulation is capped at
            30 Hz; slow frames skip catch-up work.
          </p>
        </>
      )}
      <a
        className="foam-research"
        href={`${import.meta.env.BASE_URL}research/foam-implementation.md`}
        target="_blank"
        rel="noreferrer"
      >
        Implementation & limits <ExternalLink size={11} />
      </a>
    </section>
  );
}
