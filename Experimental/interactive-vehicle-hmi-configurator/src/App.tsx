import { motion } from "framer-motion";
import Scene from "./components/Scene";
import TopBar from "./components/hmi/TopBar";
import LeftRail from "./components/hmi/LeftRail";
import ConfigPanel from "./components/hmi/ConfigPanel";
import ClimateBar from "./components/hmi/ClimateBar";
import SpecCard from "./components/hmi/SpecCard";
import DragGhost from "./components/hmi/DragGhost";
import { useConfig, getEnv } from "./store";

export default function App() {
  const envId = useConfig((s) => s.envId);
  const scan = useConfig((s) => s.scan);
  const env = getEnv(envId);

  return (
    <div className="relative h-screen w-screen overflow-hidden bg-black font-sans text-white antialiased select-none">
      {/* animated background gradient per environment */}
      <div className={`absolute inset-0 bg-gradient-to-b ${env.bg} transition-all duration-1000`} />
      <div className="absolute inset-0 bg-[radial-gradient(ellipse_at_50%_38%,rgba(120,150,200,0.18),transparent_60%)]" />
      {/* grid texture */}
      <div
        className="absolute inset-0 opacity-[0.07]"
        style={{
          backgroundImage:
            "linear-gradient(rgba(255,255,255,0.4) 1px, transparent 1px), linear-gradient(90deg, rgba(255,255,255,0.4) 1px, transparent 1px)",
          backgroundSize: "56px 56px",
        }}
      />

      {/* 3D stage */}
      <div className="absolute inset-0">
        <Scene />
      </div>

      {/* static vignette (CSS, no post-processing) */}
      <div
        className="pointer-events-none absolute inset-0"
        style={{
          background:
            "radial-gradient(ellipse at 50% 42%, transparent 52%, rgba(0,0,0,0.55) 100%)",
        }}
      />

      {/* scan-mode HUD overlays */}
      {scan && (
        <>
          <div className="pointer-events-none absolute inset-0 bg-[radial-gradient(ellipse_at_center,transparent_40%,rgba(8,20,30,0.55))] mix-blend-screen" />
          {/* dot matrix (like the medical-scan references) */}
          <div
            className="pointer-events-none absolute inset-0"
            style={{
              backgroundImage: "radial-gradient(rgba(125,211,252,0.35) 1px, transparent 1.4px)",
              backgroundSize: "24px 24px",
              opacity: 0.16,
              maskImage: "radial-gradient(ellipse at 50% 45%, black 25%, transparent 72%)",
              WebkitMaskImage: "radial-gradient(ellipse at 50% 45%, black 25%, transparent 72%)",
            }}
          />
          {/* sweeping scan line */}
          <motion.div
            className="pointer-events-none absolute left-0 right-0 h-px"
            style={{
              background: "linear-gradient(90deg, transparent, rgba(125,211,252,0.65) 30%, rgba(125,211,252,0.65) 70%, transparent)",
              boxShadow: "0 0 14px rgba(125,211,252,0.45)",
            }}
            initial={{ top: "8%" }}
            animate={{ top: ["8%", "92%", "8%"] }}
            transition={{ duration: 7, repeat: Infinity, ease: "easeInOut" }}
          />
          {/* corner HUD frame */}
          <div className="pointer-events-none absolute inset-x-10 top-16 bottom-28">
            {["left-0 top-0 border-l-2 border-t-2", "right-0 top-0 border-r-2 border-t-2", "bottom-0 left-0 border-b-2 border-l-2", "bottom-0 right-0 border-b-2 border-r-2"].map(
              (c) => (
                <span key={c} className={`absolute h-7 w-7 border-cyan-200/50 ${c}`} />
              )
            )}
            <span className="absolute -top-6 left-0 flex items-center gap-2 text-[10px] font-semibold uppercase tracking-[0.3em] text-cyan-200/80">
              <span className="h-1.5 w-1.5 animate-pulse rounded-full bg-cyan-300" />
              Perception Scan · Active
            </span>
            <span className="absolute -bottom-6 right-0 text-[10px] font-semibold uppercase tracking-[0.3em] text-cyan-200/60">
              LiDAR · 360°
            </span>
          </div>
        </>
      )}

      {/* ===== HMI OVERLAY ===== */}
      <div className="pointer-events-none absolute inset-0 flex flex-col">
        <TopBar />

        <div className="flex flex-1 items-stretch justify-between gap-4 px-6 pb-2 pt-1">
          {/* left nav */}
          <div className="flex items-center">
            <LeftRail />
          </div>

          {/* center spacer (bottom-left spec floats) */}
          <div className="flex flex-1 items-end pb-2">
            <SpecCard />
          </div>

          {/* right config */}
          <div className="flex items-stretch py-1">
            <ConfigPanel />
          </div>
        </div>

        {/* bottom climate */}
        <div className="px-6 pb-5">
          <ClimateBar />
        </div>
      </div>

      {/* drag & drop material ghost */}
      <DragGhost />
    </div>
  );
}
