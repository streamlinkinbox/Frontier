import { useEffect, useRef } from "react";
import {
  X,
  Mountain,
  MousePointer2,
  Waves,
  Box,
  Keyboard,
  Download,
  Info,
  Move3D,
} from "lucide-react";
export function Dialog({
  title,
  children,
  onClose,
  className = "",
}: {
  title: string;
  children: React.ReactNode;
  onClose: () => void;
  className?: string;
}) {
  const ref = useRef<HTMLDialogElement>(null);
  useEffect(() => {
    const dialog = ref.current;
    dialog?.showModal();
    return () => dialog?.close();
  }, []);
  return (
    <dialog
      ref={ref}
      className={`dialog ${className}`}
      onCancel={(e) => {
        e.preventDefault();
        onClose();
      }}
      onClick={(e) => {
        if (e.target === ref.current) onClose();
      }}
      aria-label={title}
    >
      <div className="dialog-header">
        <div className="dialog-heading">
          <Mountain size={20} />
          <h2>{title}</h2>
        </div>
        <button
          className="icon-button"
          onClick={onClose}
          aria-label="Close dialog"
        >
          <X size={18} />
        </button>
      </div>
      {children}
    </dialog>
  );
}
export function Guide({ onClose }: { onClose: () => void }) {
  return (
    <Dialog
      title="A little guidance. A lot of possibility."
      onClose={onClose}
      className="guide-dialog"
    >
      <div className="guide-intro">
        <span className="eyebrow">WELCOME TO FRONTIER</span>
        <h3>
          Shape it. Weather it.
          <br />
          <em>Make it yours.</em>
        </h3>
        <p>
          A small piece of the world, built entirely from a 3D signed-distance
          field. Every cliff, cave, and cut is real geometry.
        </p>
      </div>
      <div className="guide-grid">
        <article>
          <MousePointer2 />
          <h4>01 / Find your angle</h4>
          <p>
            Select Orbit (1) to rotate/pan/zoom around the scene. Select Fly (2)
            for right-drag look, WASD/QE movement and wheel speed control.
            Neither mode switches itself when you drag or press movement keys. F
            frames the terrain while keeping the selected mode. Click the
            viewport to focus input.
          </p>
        </article>
        <article>
          <Move3D />
          <h4>02 / Sculpt in three dimensions</h4>
          <p>
            Add rock, carve caves, flatten to a locked plane, cut cracks or
            crevices, and stamp boulders. Brushes edit the volume, not a
            heightmap.
          </p>
        </article>
        <article>
          <Waves />
          <h4>03 / Let nature work</h4>
          <p>
            Run erosion to move water and sediment through the volume. Adjust
            rain, carrying capacity, and rock resistance while it runs. Live
            preview adds 12 iterations when a parameter changes.
          </p>
        </article>
        <article>
          <Download />
          <h4>04 / Take it into your engine</h4>
          <p>
            Export a GLB mesh with vertex-colored sandstone, or keep the
            editable volume in a .frontier project. Micro-grain is procedural
            shader detail, not extra mesh triangles. Water is a preview shader
            and is not baked into the GLB.
          </p>
        </article>
      </div>
      <div className="guide-shortcuts">
        <h4>
          <Keyboard size={15} /> A few useful shortcuts
        </h4>
        <div>
          {[
            ["1 / 2", "Orbit / Fly mode"],
            ["W A S D", "Fly / strafe (Fly mode)"],
            ["Q / E", "Down / up"],
            ["RMB", "Free look"],
            ["Shift", "3× fly speed"],
            ["V", "Navigate tool"],
            ["B", "Add rock"],
            ["X", "Carve"],
            ["M", "Smooth"],
            ["L / R", "Flatten / ridges"],
            ["K / U / O", "Cracks / crevice / boulder"],
            ["F", "Frame terrain"],
            ["Space", "Run / pause"],
            ["[ / ]", "Brush size"],
            ["Hold C", "Compare original"],
            ["⌘ / Ctrl Z", "Undo"],
            ["G", "Toggle grid"],
          ].map(([key, label]) => (
            <span key={key}>
              <kbd>{key}</kbd>
              {label}
            </span>
          ))}
        </div>
      </div>
      <div className="science-note">
        <Info size={17} />
        <p>
          <strong>A creative simulation, not a geological prediction.</strong>{" "}
          WebGPU runs a 3D finite-volume runoff and sediment model, coupled to
          surface-directed runoff incision, budgeted sediment exchange,
          directional dry-rock wind abrasion, and downhill talus transfer. Water
          waves and shoreline foam are visual, not a fluid free-surface solve.
          Resolution and simplified material physics limit realism; this is not
          Navier–Stokes, rock-fracture mechanics, or a calibrated geological
          timescale. WebGL2 uses the same lower-resolution CPU model.
        </p>
      </div>
      <div className="dialog-footer">
        <span>
          <Box size={14} /> 96 × 48 × 96 m · bounded volume · no texture assets
        </span>
        <button className="primary-button" onClick={onClose}>
          Let’s make something <span>↗</span>
        </button>
      </div>
    </Dialog>
  );
}
