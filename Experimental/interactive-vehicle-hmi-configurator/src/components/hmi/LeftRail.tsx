import { motion } from "framer-motion";
import { useConfig } from "../../store";
import type { Tab } from "../../store";
import { cn } from "../../utils/cn";

const ICONS: Record<Tab, React.ReactNode> = {
  paint: (
    <>
      <path d="M12 3a9 9 0 1 0 0 18c1.1 0 2-.9 2-2 0-.5-.2-.9-.5-1.3-.3-.4-.5-.8-.5-1.2 0-1 .8-1.8 1.8-1.8H17a4 4 0 0 0 4-4c0-4.4-4-7.7-9-7.7z" />
      <circle cx="7.5" cy="10.5" r="1" fill="currentColor" stroke="none" />
      <circle cx="12" cy="7.5" r="1" fill="currentColor" stroke="none" />
      <circle cx="16.5" cy="10.5" r="1" fill="currentColor" stroke="none" />
    </>
  ),
  wheels: (
    <>
      <circle cx="12" cy="12" r="9" />
      <circle cx="12" cy="12" r="3" />
      <path d="M12 3v6M12 15v6M3 12h6M15 12h6" />
    </>
  ),
  lights: (
    <>
      <path d="M9 18h6M10 21h4" />
      <path d="M12 3a6 6 0 0 0-4 10.5c.7.6 1 1.4 1 2.3V16h6v-.2c0-.9.3-1.7 1-2.3A6 6 0 0 0 12 3z" />
    </>
  ),
  drive: (
    <>
      <circle cx="12" cy="12" r="9" />
      <path d="M12 12 8 8M12 12l5 2" />
      <circle cx="12" cy="12" r="1.4" fill="currentColor" stroke="none" />
    </>
  ),
  scene: (
    <>
      <rect x="3" y="5" width="18" height="14" rx="2" />
      <path d="m3 16 5-5 4 4 3-3 6 6" />
      <circle cx="9" cy="10" r="1.3" />
    </>
  ),
};

const TABS: { id: Tab; label: string }[] = [
  { id: "paint", label: "Paint" },
  { id: "wheels", label: "Wheels" },
  { id: "lights", label: "Lighting" },
  { id: "drive", label: "Drive" },
  { id: "scene", label: "Scene" },
];

export default function LeftRail() {
  const tab = useConfig((s) => s.tab);
  const set = useConfig((s) => s.set);

  return (
    <div className="pointer-events-auto flex flex-col items-center gap-2 rounded-[26px] bg-white/[0.04] p-2 ring-1 ring-white/10 backdrop-blur-2xl">
      {TABS.map((t) => {
        const active = tab === t.id;
        return (
          <button
            key={t.id}
            onClick={() => set({ tab: t.id })}
            className={cn(
              "group relative flex h-14 w-14 flex-col items-center justify-center gap-1 rounded-2xl transition-colors",
              active ? "text-white" : "text-white/45 hover:text-white/80"
            )}
          >
            {active && (
              <motion.span
                layoutId="rail-active"
                className="absolute inset-0 rounded-2xl bg-white/10 ring-1 ring-white/15"
                transition={{ type: "spring", stiffness: 420, damping: 34 }}
              />
            )}
            <svg
              viewBox="0 0 24 24"
              className="relative h-5 w-5"
              fill="none"
              stroke="currentColor"
              strokeWidth={1.6}
              strokeLinecap="round"
              strokeLinejoin="round"
            >
              {ICONS[t.id]}
            </svg>
            <span className="relative text-[9px] font-semibold uppercase tracking-wider">{t.label}</span>
          </button>
        );
      })}
    </div>
  );
}
