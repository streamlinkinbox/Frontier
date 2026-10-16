import { useConfig } from "../../store";
import { cn } from "../../utils/cn";

export default function ClimateBar() {
  const temp = useConfig((s) => s.temp);
  const fan = useConfig((s) => s.fan);
  const climateAuto = useConfig((s) => s.climateAuto);
  const sync = useConfig((s) => s.sync);
  const set = useConfig((s) => s.set);

  // temperature gradient bar (16 - 30)
  const bars = 28;
  const activeIndex = Math.round(((temp - 16) / (30 - 16)) * (bars - 1));

  return (
    <div className="pointer-events-auto flex items-stretch gap-3 rounded-[26px] bg-black/40 p-2.5 ring-1 ring-white/10 backdrop-blur-2xl">
      {/* fan */}
      <div className="flex items-center gap-3 rounded-2xl bg-white/[0.04] px-4 ring-1 ring-white/[0.06]">
        <svg viewBox="0 0 24 24" className="h-5 w-5 text-sky-300" fill="currentColor">
          <path d="M12 12c1.5-4 0-9-1.5-9S8 6 10 10c-4-1.5-8 .5-8 2s4 2.5 8 1c-2 4-1.5 7.5 0 7.5S14 17 12.5 13" opacity="0.9" />
          <circle cx="12" cy="12" r="1.6" />
        </svg>
        <span className="w-5 text-center text-lg font-semibold text-white tabular-nums">{fan}</span>
        <div className="flex flex-col gap-1">
          <button onClick={() => set({ fan: Math.min(7, fan + 1) })} className="flex h-5 w-7 items-center justify-center rounded-md bg-white/10 text-white/80 hover:bg-white/20">+</button>
          <button onClick={() => set({ fan: Math.max(0, fan - 1) })} className="flex h-5 w-7 items-center justify-center rounded-md bg-white/10 text-white/80 hover:bg-white/20">–</button>
        </div>
      </div>

      {/* temperature */}
      <div className="flex flex-1 items-center gap-4 rounded-2xl bg-white/[0.04] px-5 ring-1 ring-white/[0.06]">
        <button
          onClick={() => set({ temp: Math.max(16, temp - 0.5) })}
          className="flex h-8 w-8 items-center justify-center rounded-lg bg-white/10 text-lg text-white/80 hover:bg-white/20"
        >
          −
        </button>
        <div className="flex items-end gap-2">
          <span className="text-3xl font-bold leading-none text-white tabular-nums">{temp.toFixed(temp % 1 ? 1 : 0)}</span>
          <span className="pb-1 text-sm font-medium text-white/50">°C</span>
        </div>

        <div className="flex flex-1 items-center gap-[3px]">
          {Array.from({ length: bars }).map((_, i) => {
            const ratio = i / (bars - 1);
            // blue -> white -> red
            const hue = 210 - ratio * 210;
            const activeBar = i <= activeIndex;
            return (
              <span
                key={i}
                className="flex-1 rounded-full transition-all"
                style={{
                  height: i === activeIndex ? 26 : 16,
                  backgroundColor: activeBar ? `hsl(${hue}, 85%, 58%)` : "rgba(255,255,255,0.1)",
                  boxShadow: i === activeIndex ? `0 0 12px hsl(${hue},85%,58%)` : "none",
                }}
              />
            );
          })}
        </div>

        <button
          onClick={() => set({ temp: Math.min(30, temp + 0.5) })}
          className="flex h-8 w-8 items-center justify-center rounded-lg bg-white/10 text-lg text-white/80 hover:bg-white/20"
        >
          +
        </button>
      </div>

      {/* auto / sync */}
      <div className="flex items-center gap-2">
        <button
          onClick={() => set({ climateAuto: !climateAuto })}
          className={cn(
            "h-full rounded-2xl px-5 text-sm font-semibold tracking-wide ring-1 transition-colors",
            climateAuto ? "bg-sky-400/90 text-black ring-sky-300" : "bg-white/[0.04] text-white/70 ring-white/[0.06] hover:text-white"
          )}
        >
          AUTO
        </button>
        <button
          onClick={() => set({ sync: !sync })}
          className={cn(
            "h-full rounded-2xl px-5 text-sm font-semibold tracking-wide ring-1 transition-colors",
            sync ? "bg-white/90 text-black ring-white" : "bg-white/[0.04] text-white/70 ring-white/[0.06] hover:text-white"
          )}
        >
          SYNC
        </button>
      </div>
    </div>
  );
}
