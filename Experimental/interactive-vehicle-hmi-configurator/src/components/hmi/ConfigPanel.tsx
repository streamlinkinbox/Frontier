import { AnimatePresence, motion } from "framer-motion";
import { useConfig, getColor } from "../../store";
import { COLORS, WHEELS, MODES, ENVS } from "../../data/config";
import type { ColorOption, WheelOption } from "../../data/config";
import { useMaterialDrag } from "../../hooks/useMaterialDrag";
import { cn } from "../../utils/cn";

/* ---------- shared bits ---------- */
function Toggle({ on, onClick }: { on: boolean; onClick: () => void }) {
  return (
    <button
      onClick={onClick}
      className={cn(
        "relative h-7 w-12 shrink-0 rounded-full transition-colors duration-300",
        on ? "bg-sky-400/90 shadow-[0_0_16px_rgba(56,189,248,0.6)]" : "bg-white/12"
      )}
    >
      <motion.span
        layout
        transition={{ type: "spring", stiffness: 600, damping: 36 }}
        className="absolute top-1 h-5 w-5 rounded-full bg-white shadow"
        style={{ left: on ? 26 : 4 }}
      />
    </button>
  );
}

function Row({ label, sub, children }: { label: string; sub?: string; children: React.ReactNode }) {
  return (
    <div className="flex items-center justify-between rounded-2xl bg-white/[0.03] px-4 py-3 ring-1 ring-white/[0.06]">
      <div>
        <p className="text-sm font-medium text-white/90">{label}</p>
        {sub && <p className="text-[11px] text-white/45">{sub}</p>}
      </div>
      {children}
    </div>
  );
}

const SWATCH_COLORS = ["#eaf4ff", "#38bdf8", "#f59e0b", "#ef4444", "#a855f7", "#22d3ee", "#34d399"];

function ColorChips({ value, onChange }: { value: string; onChange: (c: string) => void }) {
  return (
    <div className="flex gap-2">
      {SWATCH_COLORS.map((c) => (
        <button
          key={c}
          onClick={() => onChange(c)}
          className={cn(
            "h-6 w-6 rounded-full transition-transform",
            value.toLowerCase() === c.toLowerCase() ? "scale-110 ring-2 ring-white" : "ring-1 ring-white/20 hover:scale-105"
          )}
          style={{ backgroundColor: c, boxShadow: `0 0 10px ${c}80` }}
        />
      ))}
    </div>
  );
}

const panelMotion = {
  initial: { opacity: 0, y: 14 },
  animate: { opacity: 1, y: 0 },
  exit: { opacity: 0, y: -14 },
  transition: { duration: 0.28, ease: [0.22, 1, 0.36, 1] as [number, number, number, number] },
};

/* ---------- draggable material sources ---------- */
function PaintSwatch({ c }: { c: ColorOption }) {
  const colorId = useConfig((s) => s.colorId);
  const set = useConfig((s) => s.set);
  const active = c.id === colorId;
  const drag = useMaterialDrag({
    kind: "paint",
    id: c.id,
    name: c.name,
    hex: c.hex,
    apply: (ndc) =>
      set({ colorId: c.id, applyReq: { kind: "paint", id: c.id, ndc, nonce: Math.random() } }),
  });

  return (
    <button
      {...drag}
      className={cn(
        "group relative flex touch-none flex-col items-center gap-2 rounded-2xl p-2.5 ring-1 transition-all",
        "cursor-grab active:cursor-grabbing",
        active ? "bg-white/[0.07] ring-white/40" : "ring-white/[0.07] hover:bg-white/[0.04]"
      )}
    >
      <span
        className="h-10 w-10 rounded-full ring-1 ring-black/40"
        style={{
          background: `radial-gradient(circle at 32% 28%, ${c.hex}ee, ${c.hex} 55%, #000 160%)`,
          boxShadow: active ? `0 0 16px ${c.hex}aa` : "none",
        }}
      />
      <span className="text-center text-[9px] font-medium leading-tight text-white/70">{c.name}</span>
    </button>
  );
}

function WheelCard({ w }: { w: WheelOption }) {
  const wheelId = useConfig((s) => s.wheelId);
  const set = useConfig((s) => s.set);
  const active = w.id === wheelId;
  const drag = useMaterialDrag({
    kind: "wheel",
    id: w.id,
    name: w.name,
    hex: w.rim,
    apply: (ndc) =>
      set({ wheelId: w.id, applyReq: { kind: "wheel", id: w.id, ndc, nonce: Math.random() } }),
  });

  return (
    <button
      {...drag}
      className={cn(
        "flex w-full touch-none items-center gap-4 rounded-2xl p-3 ring-1 transition-all",
        "cursor-grab text-left active:cursor-grabbing",
        active ? "bg-white/[0.07] ring-white/40" : "ring-white/[0.07] hover:bg-white/[0.04]"
      )}
    >
      <WheelThumb spokes={w.spokes} rim={w.rim} />
      <div className="flex-1">
        <p className="text-sm font-medium text-white/90">{w.name}</p>
        <p className="text-[11px] text-white/45">{w.size} · {w.spokes}-spoke</p>
      </div>
      <span className={cn("h-4 w-4 rounded-full ring-2 transition-colors", active ? "border-sky-400 bg-sky-400 ring-sky-400" : "ring-white/25")} />
    </button>
  );
}

/* ---------- tab bodies ---------- */
function PaintTab() {
  const colorId = useConfig((s) => s.colorId);
  const finish = useConfig((s) => s.finish);
  const set = useConfig((s) => s.set);
  const color = getColor(colorId);

  return (
    <div className="space-y-5">
      <div>
        <p className="mb-1 text-xs uppercase tracking-[0.25em] text-white/40">Exterior Finish</p>
        <h3 className="text-2xl font-semibold text-white">{color.name}</h3>
        <p className="text-[11px] text-white/45">{color.metallic ? "Metallic" : "Matte"} · Premium ceramic coat</p>
        <p className="mt-2 flex items-center gap-1.5 text-[11px] font-medium text-sky-300/80">
          <svg viewBox="0 0 24 24" className="h-3.5 w-3.5" fill="none" stroke="currentColor" strokeWidth={1.8} strokeLinecap="round">
            <path d="M5 9l-3 3 3 3M9 5l3-3 3 3M15 19l-3 3-3-3M19 9l3 3-3 3M2 12h20M12 2v20" />
          </svg>
          Drag a finish onto the vehicle — or tap to apply
        </p>
      </div>

      <div className="grid grid-cols-4 gap-3">
        {COLORS.map((c) => (
          <PaintSwatch key={c.id} c={c} />
        ))}
      </div>

      <div className="flex gap-2 rounded-2xl bg-white/[0.04] p-1.5 ring-1 ring-white/[0.06]">
        {(["gloss", "matte"] as const).map((f) => (
          <button
            key={f}
            onClick={() =>
              set({ finish: f, applyReq: { kind: "paint", id: colorId, ndc: null, nonce: Math.random() } })
            }
            className={cn(
              "relative flex-1 rounded-xl py-2.5 text-sm font-medium capitalize transition-colors",
              finish === f ? "text-black" : "text-white/55 hover:text-white/80"
            )}
          >
            {finish === f && (
              <motion.span layoutId="finish-pill" className="absolute inset-0 rounded-xl bg-white" transition={{ type: "spring", stiffness: 400, damping: 32 }} />
            )}
            <span className="relative">{f}</span>
          </button>
        ))}
      </div>
    </div>
  );
}

function WheelsTab() {
  return (
    <div className="space-y-5">
      <div>
        <p className="mb-1 text-xs uppercase tracking-[0.25em] text-white/40">Wheels & Tires</p>
        <h3 className="text-2xl font-semibold text-white">Forged Alloy Set</h3>
        <p className="text-[11px] text-white/45">35" all-terrain · Beadlock capable</p>
      </div>
      <p className="-mb-2 flex items-center gap-1.5 text-[11px] font-medium text-sky-300/80">
        <svg viewBox="0 0 24 24" className="h-3.5 w-3.5" fill="none" stroke="currentColor" strokeWidth={1.8} strokeLinecap="round">
          <path d="M5 9l-3 3 3 3M9 5l3-3 3 3M15 19l-3 3-3-3M19 9l3 3-3 3M2 12h20M12 2v20" />
        </svg>
        Drag a wheel set onto the truck — or tap to apply
      </p>
      <div className="space-y-3">
        {WHEELS.map((w) => (
          <WheelCard key={w.id} w={w} />
        ))}
      </div>
    </div>
  );
}

function WheelThumb({ spokes, rim }: { spokes: number; rim: string }) {
  return (
    <svg viewBox="0 0 48 48" className="h-12 w-12 shrink-0">
      <circle cx="24" cy="24" r="22" fill="#0c0d0f" />
      <circle cx="24" cy="24" r="15" fill={rim} opacity="0.95" />
      <circle cx="24" cy="24" r="4.5" fill="#1a1c1f" />
      {Array.from({ length: spokes }).map((_, i) => {
        const a = (i / spokes) * Math.PI * 2;
        const x = 24 + Math.cos(a) * 14;
        const y = 24 + Math.sin(a) * 14;
        return <line key={i} x1="24" y1="24" x2={x} y2={y} stroke="#0c0d0f" strokeWidth="2.6" />;
      })}
    </svg>
  );
}

function LightsTab() {
  const s = useConfig();
  const setHeadlights = (on: boolean) =>
    s.set({
      headlights: on,
      highBeams: on ? s.highBeams : false,
    });

  return (
    <div className="space-y-4">
      <div>
        <p className="mb-1 text-xs uppercase tracking-[0.25em] text-white/40">Lighting Signature</p>
        <h3 className="text-2xl font-semibold text-white">Illumination</h3>
        <p className="mt-1 text-[11px] leading-relaxed text-white/45">
          Projector optics with physical road-cast beams
        </p>
      </div>
      <Row label="Exterior Lights" sub="Low beam & LED projector lenses">
        <Toggle on={s.headlights} onClick={() => setHeadlights(!s.headlights)} />
      </Row>
      <Row label="High Beam" sub="Long-range focused throw">
        <Toggle
          on={s.headlights && s.highBeams}
          onClick={() => s.set({ headlights: true, highBeams: !s.highBeams })}
        />
      </Row>
      <Row label="Fog Lights" sub="Wide, low amber road pattern">
        <Toggle on={s.fogLights} onClick={() => s.set({ fogLights: !s.fogLights })} />
      </Row>
      <Row label="Light Bar" sub="Full-width front signature">
        <Toggle on={s.lightBar} onClick={() => s.set({ lightBar: !s.lightBar })} />
      </Row>
      {s.lightBar && (
        <div className="rounded-2xl bg-white/[0.03] px-4 py-3 ring-1 ring-white/[0.06]">
          <p className="mb-2.5 text-[11px] uppercase tracking-wider text-white/40">Bar Color</p>
          <ColorChips value={s.lightBarColor} onChange={(c) => s.set({ lightBarColor: c })} />
        </div>
      )}
      <Row label="Underglow" sub="Ground effect lighting">
        <Toggle on={s.underglow} onClick={() => s.set({ underglow: !s.underglow })} />
      </Row>
      {s.underglow && (
        <div className="rounded-2xl bg-white/[0.03] px-4 py-3 ring-1 ring-white/[0.06]">
          <p className="mb-2.5 text-[11px] uppercase tracking-wider text-white/40">Glow Color</p>
          <ColorChips value={s.underglowColor} onChange={(c) => s.set({ underglowColor: c })} />
        </div>
      )}
    </div>
  );
}

function DriveTab() {
  const modeId = useConfig((s) => s.modeId);
  const set = useConfig((s) => s.set);
  return (
    <div className="space-y-5">
      <div>
        <p className="mb-1 text-xs uppercase tracking-[0.25em] text-white/40">Drive Modes</p>
        <h3 className="text-2xl font-semibold text-white">Dynamics</h3>
      </div>
      <div className="space-y-3">
        {MODES.map((m) => {
          const active = m.id === modeId;
          return (
            <button
              key={m.id}
              onClick={() => set({ modeId: m.id })}
              className={cn(
                "relative flex w-full items-center justify-between overflow-hidden rounded-2xl p-4 text-left ring-1 transition-all",
                active ? "ring-white/40" : "ring-white/[0.07] hover:bg-white/[0.04]"
              )}
              style={active ? { background: `linear-gradient(100deg, ${m.accent}22, transparent)` } : {}}
            >
              <span
                className="absolute left-0 top-0 h-full w-1 rounded-r"
                style={{ backgroundColor: active ? m.accent : "transparent", boxShadow: active ? `0 0 12px ${m.accent}` : "none" }}
              />
              <div className="pl-2">
                <p className="text-base font-semibold text-white">{m.name}</p>
                <p className="text-[11px] text-white/45">{m.desc}</p>
              </div>
              <span
                className="rounded-full px-2.5 py-1 text-[11px] font-semibold"
                style={{ color: m.accent, backgroundColor: `${m.accent}1f` }}
              >
                {m.power}
              </span>
            </button>
          );
        })}
      </div>
    </div>
  );
}

function SceneTab() {
  const s = useConfig();
  return (
    <div className="space-y-5">
      <div>
        <p className="mb-1 text-xs uppercase tracking-[0.25em] text-white/40">Environment</p>
        <h3 className="text-2xl font-semibold text-white">Studio & Effects</h3>
      </div>
      <div className="grid grid-cols-2 gap-3">
        {ENVS.map((e) => {
          const active = e.id === s.envId;
          return (
            <button
              key={e.id}
              onClick={() => s.set({ envId: e.id })}
              className={cn(
                "relative h-20 overflow-hidden rounded-2xl ring-1 transition-all",
                active ? "ring-white/50" : "ring-white/[0.07] hover:ring-white/25"
              )}
            >
              <span className={cn("absolute inset-0 bg-gradient-to-br", e.bg)} />
              <span className="absolute bottom-2 left-3 text-sm font-medium text-white/90">{e.name}</span>
              {active && <span className="absolute right-2 top-2 h-2.5 w-2.5 rounded-full bg-white shadow" />}
            </button>
          );
        })}
      </div>
      <Row label="X-Ray Scan" sub="Front-to-back pulse sweep">
        <Toggle on={s.scan} onClick={() => s.set({ scan: !s.scan })} />
      </Row>
      <Row label="Turntable" sub="Auto-rotate showcase">
        <Toggle on={s.autoRotate} onClick={() => s.set({ autoRotate: !s.autoRotate })} />
      </Row>
    </div>
  );
}

export default function ConfigPanel() {
  const tab = useConfig((s) => s.tab);
  return (
    <div
      data-nodrop
      className="pointer-events-auto flex h-full w-[360px] flex-col rounded-[28px] bg-black/35 p-6 ring-1 ring-white/10 backdrop-blur-2xl"
    >
      <AnimatePresence mode="wait">
        <motion.div key={tab} {...panelMotion} className="flex-1 overflow-y-auto pr-1 [scrollbar-width:none] [&::-webkit-scrollbar]:hidden">
          {tab === "paint" && <PaintTab />}
          {tab === "wheels" && <WheelsTab />}
          {tab === "lights" && <LightsTab />}
          {tab === "drive" && <DriveTab />}
          {tab === "scene" && <SceneTab />}
        </motion.div>
      </AnimatePresence>
    </div>
  );
}
