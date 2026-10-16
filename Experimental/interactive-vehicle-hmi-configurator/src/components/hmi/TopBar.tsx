import { useEffect, useState } from "react";
import { useConfig, getMode } from "../../store";

function Clock() {
  const [time, setTime] = useState("");
  useEffect(() => {
    const tick = () => {
      const d = new Date();
      setTime(
        d.toLocaleTimeString("en-US", { hour: "2-digit", minute: "2-digit", hour12: false })
      );
    };
    tick();
    const id = setInterval(tick, 1000 * 20);
    return () => clearInterval(id);
  }, []);
  return <span className="tabular-nums">{time}</span>;
}

function SignalBars() {
  return (
    <div className="flex items-end gap-[2px]">
      {[5, 8, 11, 14].map((h, i) => (
        <span key={i} className="w-[3px] rounded-sm bg-white/80" style={{ height: h }} />
      ))}
    </div>
  );
}

export default function TopBar() {
  const modeId = useConfig((s) => s.modeId);
  const mode = getMode(modeId);

  return (
    <div className="pointer-events-none flex items-center justify-between px-6 py-3 text-[13px] font-medium text-white/80">
      {/* left cluster */}
      <div className="flex items-center gap-5">
        <div className="pointer-events-auto flex h-9 w-9 items-center justify-center rounded-full bg-white/5 ring-1 ring-white/10 backdrop-blur">
          <svg viewBox="0 0 24 24" className="h-5 w-5" fill="none" stroke="currentColor" strokeWidth={1.6}>
            <circle cx="12" cy="8" r="3.4" />
            <path d="M5 20c0-3.6 3-5.5 7-5.5s7 1.9 7 5.5" />
          </svg>
        </div>
        <svg viewBox="0 0 24 24" className="h-5 w-5 text-white/50" fill="none" stroke="currentColor" strokeWidth={1.6}>
          <path d="M11 5 6 9H3v6h3l5 4V5z" />
          <path d="M16 9a5 5 0 0 1 0 6" />
          <path d="m19 4-14 14" />
        </svg>
        <svg viewBox="0 0 24 24" className="h-5 w-5 text-white/60" fill="none" stroke="currentColor" strokeWidth={1.6}>
          <polygon points="6 4 20 12 6 20 6 4" />
        </svg>
        <span className="text-[15px] font-semibold tracking-wide text-white/90">
          <Clock />
        </span>
      </div>

      {/* center title */}
      <div className="hidden items-center gap-2 md:flex">
        <span
          className="h-2 w-2 rounded-full"
          style={{ backgroundColor: mode.accent, boxShadow: `0 0 10px ${mode.accent}` }}
        />
        <span className="text-xs font-semibold uppercase tracking-[0.35em] text-white/70">
          GMC Hummer EV · {mode.name}
        </span>
      </div>

      {/* right cluster */}
      <div className="flex items-center gap-4">
        <div className="flex items-center gap-1.5">
          <SignalBars />
          <span className="text-[11px] font-semibold text-white/70">5G</span>
        </div>
        <span className="text-[13px] font-semibold text-white/85">18°C</span>
        <div className="flex items-center gap-1 rounded-md bg-emerald-400/10 px-1.5 py-0.5 text-[10px] font-semibold text-emerald-300 ring-1 ring-emerald-400/20">
          <span>PM</span>
          <span className="tabular-nums">2.5</span>
        </div>
        <svg viewBox="0 0 24 24" className="h-4 w-4 text-sky-300" fill="none" stroke="currentColor" strokeWidth={1.6}>
          <path d="m7 7 10 10-5 4V3l5 4L7 17" />
        </svg>
        <div className="flex items-center gap-1 text-white/80">
          <svg viewBox="0 0 24 24" className="h-5 w-5" fill="none" stroke="currentColor" strokeWidth={1.6}>
            <rect x="2" y="7" width="18" height="10" rx="2.5" />
            <rect x="21" y="10" width="1.5" height="4" rx="0.75" fill="currentColor" />
          </svg>
          <span className="text-[11px] font-semibold">88%</span>
        </div>
      </div>
    </div>
  );
}
