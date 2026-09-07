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
            Right-drag looks around from your position. Use WASD to fly, Q/E to
            move down/up, and Shift for a 3× speed boost. RMB + wheel adjusts
            fly speed. Left-drag or Alt-drag orbits; middle-drag pans. F returns
            to the overview. Click the viewport to focus the keys.
          </p>
        </article>
        <article>
          <Move3D />
          <h4>02 / Sculpt in three dimensions</h4>
          <p>
            Add rock, carve a cave, soften an edge, or flatten a shelf. Brushes
            edit a sphere of voxels, not a heightmap.
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
            ["W A S D", "Fly / strafe"],
            ["Q / E", "Down / up"],
            ["RMB", "Free look"],
            ["Shift", "3× fly speed"],
            ["V", "Navigate tool"],
            ["B", "Add rock"],
            ["X", "Carve"],
            ["M", "Smooth"],
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
          narrow-band SDF erosion and curvature-based thermal relaxation.
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
