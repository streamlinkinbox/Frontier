import { useRef } from "react";
import { useConfig } from "../store";

interface DragOpts {
  kind: "paint" | "wheel";
  id: string;
  name: string;
  hex: string;
  /** Called on drop (with stage NDC coords) or on simple click (null). */
  apply: (ndc: [number, number] | null) => void;
}

function isOverPanel(x: number, y: number) {
  const el = document.elementFromPoint(x, y);
  return !!el?.closest("[data-nodrop]");
}

/**
 * Pointer-based drag & drop for material swatches.
 * Drag past a small threshold to pick the material up; release over the
 * 3D stage to drop it there, release over the panel to cancel, or just
 * click to apply from the default origin.
 */
export function useMaterialDrag({ kind, id, name, hex, apply }: DragOpts) {
  const start = useRef<{ x: number; y: number } | null>(null);
  const dragging = useRef(false);

  const onPointerDown = (e: React.PointerEvent) => {
    start.current = { x: e.clientX, y: e.clientY };
    dragging.current = false;

    const move = (ev: PointerEvent) => {
      if (!start.current) return;
      const dx = ev.clientX - start.current.x;
      const dy = ev.clientY - start.current.y;
      if (!dragging.current && Math.hypot(dx, dy) > 7) dragging.current = true;
      if (dragging.current) {
        useConfig.getState().set({
          dragMat: {
            kind,
            id,
            name,
            hex,
            x: ev.clientX,
            y: ev.clientY,
            droppable: !isOverPanel(ev.clientX, ev.clientY),
          },
        });
      }
    };

    const up = (ev: PointerEvent) => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
      const wasDragging = dragging.current;
      dragging.current = false;
      start.current = null;
      useConfig.getState().set({ dragMat: null });

      if (wasDragging) {
        if (!isOverPanel(ev.clientX, ev.clientY)) {
          const ndc: [number, number] = [
            (ev.clientX / window.innerWidth) * 2 - 1,
            -((ev.clientY / window.innerHeight) * 2 - 1),
          ];
          apply(ndc);
        }
        // released over the panel -> cancel
      } else {
        apply(null);
      }
    };

    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  };

  return { onPointerDown };
}
