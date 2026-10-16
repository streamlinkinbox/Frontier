import { AnimatePresence, motion } from "framer-motion";
import { useConfig } from "../../store";

export default function DragGhost() {
  const dragMat = useConfig((s) => s.dragMat);

  return (
    <AnimatePresence>
      {dragMat && (
        <>
          {/* floating material chip following the pointer */}
          <motion.div
            key="chip"
            initial={{ scale: 0.7, opacity: 0, rotate: 0 }}
            animate={{ scale: 1, opacity: 1, rotate: -4 }}
            exit={{ scale: 0.8, opacity: 0 }}
            transition={{ type: "spring", stiffness: 500, damping: 28 }}
            className="pointer-events-none fixed z-[60]"
            style={{
              left: dragMat.x,
              top: dragMat.y,
              transform: "translate(-50%, -130%)",
            }}
          >
            <div
              className={`flex items-center gap-2.5 rounded-2xl bg-black/70 py-2 pl-2 pr-3.5 ring-1 backdrop-blur-xl transition-colors ${
                dragMat.droppable ? "ring-white/30" : "ring-white/10 opacity-60"
              }`}
              style={{ boxShadow: "0 18px 40px rgba(0,0,0,0.55)" }}
            >
              <span
                className="h-9 w-9 rounded-full ring-1 ring-black/50"
                style={{
                  background: `radial-gradient(circle at 32% 28%, ${dragMat.hex}ee, ${dragMat.hex} 55%, #000 165%)`,
                  boxShadow: `0 0 18px ${dragMat.hex}90`,
                }}
              />
              <span>
                <span className="block text-[13px] font-semibold leading-tight text-white">
                  {dragMat.name}
                </span>
                <span
                  className={`block text-[10px] font-medium leading-tight ${
                    dragMat.droppable ? "text-emerald-300" : "text-white/40"
                  }`}
                >
                  {dragMat.droppable ? "Release to apply" : "Release to cancel"}
                </span>
              </span>
            </div>
          </motion.div>

          {/* bottom drop hint */}
          <motion.div
            key="hint"
            initial={{ opacity: 0, y: 12 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: 8 }}
            className="pointer-events-none fixed bottom-28 left-1/2 z-[60] -translate-x-1/2"
          >
            <div className="flex items-center gap-2 rounded-full bg-black/60 px-4 py-2 ring-1 ring-white/15 backdrop-blur-xl">
              <span className="relative flex h-2 w-2">
                <span className="absolute h-full w-full animate-ping rounded-full bg-emerald-300/60" />
                <span className="h-2 w-2 rounded-full bg-emerald-300" />
              </span>
              <span className="text-xs font-medium text-white/85">
                Drop on the vehicle to apply{" "}
                <span className="font-semibold text-white">{dragMat.name}</span>
              </span>
            </div>
          </motion.div>
        </>
      )}
    </AnimatePresence>
  );
}
