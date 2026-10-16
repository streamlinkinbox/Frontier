import { useConfig, getColor, getWheel, getMode } from "../../store";

function Stat({ value, unit, label }: { value: string; unit?: string; label: string }) {
  return (
    <div>
      <p className="flex items-baseline gap-0.5 text-2xl font-bold leading-none text-white">
        {value}
        {unit && <span className="text-xs font-medium text-white/50">{unit}</span>}
      </p>
      <p className="mt-1 text-[10px] uppercase tracking-wider text-white/40">{label}</p>
    </div>
  );
}

export default function SpecCard() {
  const colorId = useConfig((s) => s.colorId);
  const wheelId = useConfig((s) => s.wheelId);
  const modeId = useConfig((s) => s.modeId);
  const color = getColor(colorId);
  const wheel = getWheel(wheelId);
  const mode = getMode(modeId);

  // playful price that reacts to config
  const base = 108700;
  const price =
    base +
    (color.metallic ? 1500 : 900) +
    (wheel.machined ? 3200 : 1800) +
    (modeId === "watts" ? 4500 : 0);

  return (
    <div className="pointer-events-auto w-[320px] rounded-[26px] bg-black/35 p-5 ring-1 ring-white/10 backdrop-blur-2xl">
      <div className="flex items-center justify-between">
        <div>
          <p className="text-[10px] font-semibold uppercase tracking-[0.3em] text-white/40">Edition 1</p>
          <h2 className="text-xl font-bold leading-tight text-white">HUMMER EV</h2>
        </div>
        <span
          className="rounded-full px-3 py-1 text-[11px] font-semibold"
          style={{ color: mode.accent, backgroundColor: `${mode.accent}1f` }}
        >
          {mode.name}
        </span>
      </div>

      <div className="my-4 grid grid-cols-3 gap-3">
        <Stat value="329" unit="mi" label="Range" />
        <Stat value="3.0" unit="s" label="0–60 mph" />
        <Stat value="1000" unit="hp" label="Power" />
      </div>

      <div className="mb-4 flex items-center gap-3 rounded-2xl bg-white/[0.04] px-4 py-2.5 ring-1 ring-white/[0.06]">
        <svg viewBox="0 0 24 24" className="h-5 w-5 text-emerald-300" fill="none" stroke="currentColor" strokeWidth={1.6}>
          <rect x="2" y="7" width="16" height="10" rx="2" />
          <rect x="20" y="10" width="1.6" height="4" rx="0.8" fill="currentColor" />
          <rect x="4" y="9" width="10" height="6" rx="1" fill="currentColor" />
        </svg>
        <div className="flex-1">
          <div className="h-1.5 overflow-hidden rounded-full bg-white/10">
            <div className="h-full rounded-full bg-emerald-400" style={{ width: "80%" }} />
          </div>
        </div>
        <span className="text-sm font-semibold text-white tabular-nums">80%</span>
      </div>

      <div className="flex items-end justify-between">
        <div>
          <p className="text-[10px] uppercase tracking-wider text-white/40">Est. Total</p>
          <p className="text-2xl font-bold text-white tabular-nums">${price.toLocaleString()}</p>
        </div>
        <button
          className="rounded-2xl bg-white px-5 py-3 text-sm font-semibold text-black transition-transform hover:scale-[1.03] active:scale-95"
        >
          Reserve →
        </button>
      </div>
    </div>
  );
}
