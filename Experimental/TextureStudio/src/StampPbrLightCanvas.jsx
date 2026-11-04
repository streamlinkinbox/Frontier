import React, { useEffect, useRef } from "react";
import { Sun } from "lucide-react";

// 💡 Movable PBR light for the stamp composition canvas: the composed artwork
// is shaded with the stamp's channel targets — height from artwork luminance
// (normals derived from it, like a placed decal), metalness, roughness,
// occlusion, emission and opacity from the channel values — against a point
// light dragged over the board. Display-space math tuned for legibility,
// mirroring the native lit board: drag the sun and metals glint, height rakes.
const EDGE = 256;
const clamp01 = (Value) => Math.min(1, Math.max(0, Value));
const hexRgb = (Hex) =>
  /^#[\da-f]{6}$/i.test(Hex || "")
    ? [1, 3, 5].map((Index) => parseInt(Hex.slice(Index, Index + 2), 16) / 255)
    : [0, 0, 0];

export default function StampPbrLightCanvas({ Preview, Channels, Values, Light, OnLight }) {
  const CanvasElement = useRef(null);
  const Source = useRef(null); // { width, height, data } composed pixels
  const Heights = useRef(null); // Float32Array height field for Source
  const Output = useRef(null); // reused ImageData
  const DecodeSequence = useRef(0);
  const Latest = useRef({ Channels, Values, Light });
  Latest.current = { Channels, Values, Light };

  function Shade() {
    const { Channels: Enabled, Values: V, Light: L } = Latest.current;
    const Src = Source.current,
      Canvas = CanvasElement.current;
    if (!Src || !Canvas) return;
    const W = Src.width,
      H = Src.height,
      Data = Src.data,
      Count = W * H;
    if (!Heights.current) {
      const Field = new Float32Array(Count);
      const HeightAmount = Enabled.includes("height") ? clamp01(Number(V.height) || 0) : 0;
      for (let Index = 0; Index < Count; Index++) {
        const Offset = Index * 4,
          Alpha = Data[Offset + 3] / 255;
        const Luminance =
          (0.2126 * Data[Offset] + 0.7152 * Data[Offset + 1] + 0.0722 * Data[Offset + 2]) / 255;
        Field[Index] = HeightAmount * Luminance * Alpha;
      }
      Heights.current = Field;
    }
    const Field = Heights.current;
    const Roughness = Enabled.includes("roughness") ? clamp01(Number(V.roughness) || 0) : 0.9;
    const Metalness = Enabled.includes("metalness") ? clamp01(Number(V.metalness) || 0) : 0;
    const Occlusion = Enabled.includes("occlusion") ? clamp01(Number(V.occlusion) || 0) : 1;
    const Opacity = Enabled.includes("opacity") ? clamp01(Number(V.opacity) || 0) : 1;
    const Emission = Enabled.includes("emission") ? hexRgb(V.emission) : null;
    // The Normal channel is derived from Height; enabled deepens the relief.
    const Relief = 5 * (Enabled.includes("normal") ? 1.6 : 1);
    const Exponent = 4 + (1 - Roughness) * (1 - Roughness) * 240;
    const LightX = L.x * W,
      LightY = L.y * H,
      LightZ = 0.55 * W;
    if (!Output.current || Output.current.width !== W || Output.current.height !== H)
      Output.current = new ImageData(W, H);
    const Out = Output.current.data;
    for (let Y = 0; Y < H; Y++) {
      const Row = Y * W,
        RowUp = Math.max(Y - 1, 0) * W,
        RowDown = Math.min(Y + 1, H - 1) * W;
      for (let X = 0; X < W; X++) {
        const Index = Row + X,
          Offset = Index * 4,
          Alpha = Data[Offset + 3] / 255;
        if (Alpha <= 0.003) {
          Out[Offset + 3] = 0;
          continue;
        }
        const AlbedoR = Data[Offset] / 255,
          AlbedoG = Data[Offset + 1] / 255,
          AlbedoB = Data[Offset + 2] / 255;
        // Screen-space normal from the height gradient (y grows downward).
        let Nx = (Field[Row + Math.max(X - 1, 0)] - Field[Row + Math.min(X + 1, W - 1)]) * Relief;
        let Ny = (Field[RowUp + X] - Field[RowDown + X]) * Relief;
        const NormalScale = 1 / Math.hypot(Nx, Ny, 1);
        Nx *= NormalScale;
        Ny *= NormalScale;
        const Nz = NormalScale;
        let Lx = LightX - X,
          Ly = LightY - Y,
          Lz = LightZ;
        const Distance = Math.hypot(Lx, Ly, Lz);
        Lx /= Distance;
        Ly /= Distance;
        Lz /= Distance;
        const Attenuation = 1 / (1 + 1.6 * (Distance / W) * (Distance / W));
        const NdotL = Math.max(Nx * Lx + Ny * Ly + Nz * Lz, 0);
        let Hx = Lx,
          Hy = Ly,
          Hz = Lz + 1;
        const HalfScale = 1 / Math.hypot(Hx, Hy, Hz);
        const NdotH = Math.max((Nx * Hx + Ny * Hy + Nz * Hz) * HalfScale, 0);
        const Specular = NdotL > 0 ? Math.pow(NdotH, Exponent) * Attenuation * 1.7 : 0;
        const Diffuse = (0.34 + NdotL * Attenuation * 1.35) * (1 - Metalness * 0.8) * Occlusion;
        const F0R = 0.05 + (AlbedoR - 0.05) * Metalness,
          F0G = 0.05 + (AlbedoG - 0.05) * Metalness,
          F0B = 0.05 + (AlbedoB - 0.05) * Metalness;
        Out[Offset] = clamp01(AlbedoR * Diffuse + F0R * Specular + (Emission ? Emission[0] * 0.85 : 0)) * 255;
        Out[Offset + 1] = clamp01(AlbedoG * Diffuse + F0G * Specular + (Emission ? Emission[1] * 0.85 : 0)) * 255;
        Out[Offset + 2] = clamp01(AlbedoB * Diffuse + F0B * Specular + (Emission ? Emission[2] * 0.85 : 0)) * 255;
        Out[Offset + 3] = Alpha * Opacity * 255;
      }
    }
    Canvas.getContext("2d").putImageData(Output.current, 0, 0);
  }

  // Decode the composed preview once per artwork change, then shade.
  useEffect(() => {
    const Sequence = ++DecodeSequence.current;
    Source.current = null;
    Heights.current = null;
    if (!Preview) return;
    const ImageElement = new Image();
    ImageElement.onload = () => {
      if (DecodeSequence.current !== Sequence) return;
      const Raster = document.createElement("canvas");
      Raster.width = Raster.height = EDGE;
      const Context = Raster.getContext("2d", { willReadFrequently: true });
      Context.drawImage(ImageElement, 0, 0, EDGE, EDGE);
      try {
        Source.current = Context.getImageData(0, 0, EDGE, EDGE);
      } catch {
        Source.current = null;
      }
      Heights.current = null;
      Shade();
    };
    ImageElement.src = Preview;
  }, [Preview]);

  // Channel edits rebuild the height field; light moves only reshade. Both are
  // cheap at 256², so one effect covers them.
  useEffect(() => {
    Heights.current = null;
    Shade();
  }, [Channels, Values, Light]);

  function LightFromEvent(Event) {
    const Rect = CanvasElement.current?.getBoundingClientRect();
    if (!Rect?.width || !Rect?.height) return;
    OnLight?.({
      x: clamp01((Event.clientX - Rect.left) / Rect.width),
      y: clamp01((Event.clientY - Rect.top) / Rect.height),
    });
  }
  return (
    <>
      <canvas ref={CanvasElement} className="stamp-pbr-canvas" width={EDGE} height={EDGE} aria-hidden="true" />
      <button
        type="button"
        className="stamp-pbr-light-handle"
        aria-label="Move PBR preview light"
        title="Drag to move the PBR preview light"
        style={{ left: `${Light.x * 100}%`, top: `${Light.y * 100}%` }}
        onPointerDown={(Event) => {
          Event.preventDefault();
          Event.stopPropagation();
          Event.currentTarget.setPointerCapture(Event.pointerId);
          LightFromEvent(Event);
        }}
        onPointerMove={(Event) => {
          if (Event.buttons) LightFromEvent(Event);
        }}
        onKeyDown={(Event) => {
          const Step = Event.shiftKey ? 0.1 : 0.02;
          const Moves = {
            ArrowLeft: [-Step, 0],
            ArrowRight: [Step, 0],
            ArrowUp: [0, -Step],
            ArrowDown: [0, Step],
          }[Event.key];
          if (!Moves) return;
          Event.preventDefault();
          OnLight?.({ x: clamp01(Light.x + Moves[0]), y: clamp01(Light.y + Moves[1]) });
        }}
      >
        <Sun size={15} />
      </button>
    </>
  );
}
