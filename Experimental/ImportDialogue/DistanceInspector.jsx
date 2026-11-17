import React, { useRef, useEffect } from "react";
export function DistancePreview({ Volume, Settings, Change }) {
  const Canvas = useRef(null),
    Axis = Settings.SdfAxis || "XY",
    N = Volume.Resolution,
    Fixed = { XY: 2, XZ: 1, YZ: 0 }[Axis],
    Plane = { XY: [0, 1], XZ: [0, 2], YZ: [1, 2] }[Axis],
    Slice = Math.min(
      N - 1,
      Math.max(0, Settings.SdfSlice ?? Math.floor(N / 2)),
    ),
    World = Volume.Bounds.Minimum[Fixed] + (Slice + 0.5) * Volume.Spacing;
  useEffect(() => {
    const C = Canvas.current,
      X = C.getContext("2d"),
      W = 720,
      Margin = 62,
      Size = 596;
    X.clearRect(0, 0, W, W);
    const Pixels = X.createImageData(N, N),
      Scale = Math.max(
        Volume.Spacing * 4,
        Math.max(
          Math.abs(Volume.MinimumDistance),
          Math.abs(Volume.MaximumDistance),
        ) * 0.6,
      );
    for (let Y = 0; Y < N; Y++)
      for (let U = 0; U < N; U++) {
        const P = [0, 0, 0];
        P[Fixed] = Slice;
        P[Plane[0]] = U;
        P[Plane[1]] = N - 1 - Y;
        const D = Volume.Values[(P[2] * N + P[1]) * N + P[0]],
          T = Math.min(1, Math.abs(D) / Scale),
          Edge = Math.max(0, 1 - Math.abs(D) / (Volume.Spacing * 0.7)),
          Base = D < 0 ? [190, 142, 91] : [98, 155, 181],
          I = (Y * N + U) * 4;
        for (let K = 0; K < 3; K++)
          Pixels.data[I + K] = Math.round(
            (Base[K] * (1 - T * 0.82) + [9, 15, 17][K] * T) *
              (0.8 + Edge * 0.2) +
              Edge * 85,
          );
        Pixels.data[I + 3] = 255;
      }
    const Temp = document.createElement("canvas");
    Temp.width = Temp.height = N;
    Temp.getContext("2d").putImageData(Pixels, 0, 0);
    X.imageSmoothingEnabled = true;
    X.drawImage(Temp, Margin, Margin, Size, Size);
    if (Volume.Signed) {
      X.strokeStyle = "#e7ebda";
      X.lineWidth = 1.2;
      const At = (U, Y) => {
        const P = [0, 0, 0];
        P[Fixed] = Slice;
        P[Plane[0]] = U;
        P[Plane[1]] = Y;
        return Volume.Values[(P[2] * N + P[1]) * N + P[0]];
      };
      for (let Y = 0; Y < N - 1; Y++)
        for (let U = 0; U < N - 1; U++) {
          const Corners = [
              [U, Y],
              [U + 1, Y],
              [U + 1, Y + 1],
              [U, Y + 1],
            ],
            Crossings = [];
          for (let E = 0; E < 4; E++) {
            const A = Corners[E],
              B = Corners[(E + 1) % 4],
              Da = At(...A),
              Db = At(...B);
            if (Da < 0 !== Db < 0) {
              const T = Da / (Da - Db);
              Crossings.push([
                Margin + ((A[0] + 0.5 + (B[0] - A[0]) * T) / N) * Size,
                Margin + (1 - (A[1] + 0.5 + (B[1] - A[1]) * T) / N) * Size,
              ]);
            }
          }
          for (let E = 0; E + 1 < Crossings.length; E += 2) {
            X.beginPath();
            X.moveTo(...Crossings[E]);
            X.lineTo(...Crossings[E + 1]);
            X.stroke();
          }
        }
    }
    X.strokeStyle = "#aec4b94d";
    X.strokeRect(Margin, Margin, Size, Size);
    X.fillStyle = "#9cae9b";
    X.font = '17px "DM Sans"';
    X.textAlign = "left";
    X.fillText(`${Axis} · ${"XYZ"[Fixed]} = ${World.toFixed(3)} m`, Margin, 34);
    X.fillText(Volume.Bounds.Minimum[Plane[0]].toFixed(2), Margin, 688);
    X.textAlign = "right";
    X.fillText(
      Volume.Bounds.Maximum[Plane[0]].toFixed(2) + " m",
      Margin + Size,
      688,
    );
    X.save();
    X.translate(26, Margin + Size / 2);
    X.rotate(-Math.PI / 2);
    X.textAlign = "center";
    X.fillText(`${"XYZ"[Plane[1]]} · world metres`, 0, 0);
    X.restore();
  }, [Volume, Axis, Slice]);
  return (
    <div className="distance-preview">
      <div
        className="distance-planes"
        role="group"
        aria-label="Distance field plane"
      >
        {["XY", "XZ", "YZ"].map((P) => (
          <button
            key={P}
            aria-pressed={Axis === P}
            onClick={() => Change("SdfAxis", P)}
          >
            {P}
          </button>
        ))}
        <span>
          {N}³ · {Volume.Signed ? "Signed distance" : "Unsigned distance"}
        </span>
      </div>
      <canvas
        ref={Canvas}
        width="720"
        height="720"
        role="img"
        aria-label="Baked distance field section"
        data-resolution={N}
        data-plane={Axis}
        data-slice={Slice}
      />
      <div className="distance-legend">
        {Volume.Signed && (
          <span>
            <i className="inside" />
            Inside
          </span>
        )}
        <span>
          <i className="surface" />
          {Volume.Signed ? "Zero surface" : "Near surface"}
        </span>
        <span>
          <i />
          Outside / distance
        </span>
      </div>
    </div>
  );
}
export default function DistanceInspector({
  Settings,
  Change,
  Controls,
  Volume,
  Fresh,
  Busy,
  Progress,
  Start,
  Cancel,
  Download,
}) {
  const { Section, Select, Scalar, Note, Readout, G } = Controls;
  return (
    <>
      <Section
        Title="Volume construction"
        Tag="LOCAL DISTANCE BAKE"
        Children={
          <>
            <div className="distance-diagram">
              <svg
                viewBox="0 0 300 120"
                role="img"
                aria-label="Distance field voxel domain"
              >
                <g stroke="#7ba9bb55" fill="none">
                  {[0, 1, 2, 3, 4].map((I) => (
                    <path
                      key={I}
                      d={`M${67 + I * 26} ${50 + I * 9}l65-25v66l-65 25z`}
                    />
                  ))}
                  <path d="M67 50l104 36 65-25-104-36zM67 116V50" />
                </g>
                <path
                  d="M119 53l39-15 37 13v38l-40 15-36-13z"
                  fill="#8baec92a"
                  stroke="#9ebbbd"
                />
                <path
                  d="m119 53 37 13 39-15m-39 15v38"
                  fill="none"
                  stroke="#bfd6d3"
                />
              </svg>
              <span>Triangle surface → closest distance → voxel field</span>
            </div>
            <Select
              Label="Voxel resolution"
              Value={String(Settings.SdfResolution)}
              Options={["16", "24", "32", "48"]}
              Change={(V) => Change("SdfResolution", +V)}
            />
            <Scalar
              Label="Volume padding"
              Value={Settings.SdfPadding}
              Min={0.02}
              Max={0.4}
              Step={0.01}
              Unit="×"
              Change={(V) => Change("SdfPadding", V)}
            />
            <Select
              Label="Distance sign"
              Value={Settings.SdfSigned ? "Signed · ray parity" : "Unsigned"}
              Options={["Unsigned", "Signed · ray parity"]}
              Change={(V) => Change("SdfSigned", V !== "Unsigned")}
            />
            <Readout
              Label="Geometry input"
              Value="Full-resolution transformed source"
            />
            <div className="bake-buttons">
              <button
                className="primary sdf-bake-button"
                disabled={Busy}
                onClick={Start}
              >
                <G Name="LayersSlabs" />
                {Busy
                  ? "Baking SDF…"
                  : Fresh
                    ? "Rebuild distance field"
                    : "Bake distance field"}
              </button>
              {Busy && <button onClick={Cancel}>Cancel</button>}
            </div>
            {Busy && (
              <div
                className="bake-progress"
                role="progressbar"
                aria-label="SDF bake progress"
                aria-valuenow={Math.round(Progress * 100)}
                aria-valuemin="0"
                aria-valuemax="100"
              >
                <i style={{ width: `${Progress * 100}%` }} />
              </div>
            )}
            {Volume && !Fresh && (
              <Note
                Warning
                Children="Geometry or bake settings changed. Rebuild the distance field before saving it."
              />
            )}
          </>
        }
      />
      {Fresh && (
        <Section
          Title="Inspect the field"
          Tag={Volume.Milliseconds.toFixed(0) + " ms"}
          Children={
            <>
              <Select
                Label="Section plane"
                Value={Settings.SdfAxis}
                Options={["XY", "XZ", "YZ"]}
                Change={(V) => Change("SdfAxis", V)}
              />
              <Scalar
                Label="Voxel section"
                Value={Math.min(Settings.SdfSlice, Volume.Resolution - 1)}
                Min={0}
                Max={Volume.Resolution - 1}
                Step={1}
                Change={(V) => Change("SdfSlice", V)}
              />
              <Readout
                Label="Field dimensions"
                Value={`${Volume.Resolution} × ${Volume.Resolution} × ${Volume.Resolution}`}
              />
              <Readout
                Label="Voxel spacing"
                Value={Volume.Spacing.toFixed(5) + " m"}
              />
              <Readout
                Label="Stored field"
                Value={
                  (Volume.Values.byteLength / 1024).toFixed(1) +
                  " KiB · float32"
                }
              />
              <Readout
                Label="Distance range"
                Value={`${Volume.MinimumDistance.toFixed(4)} … ${Volume.MaximumDistance.toFixed(4)} m`}
              />
              <button className="text-button" onClick={Download}>
                <G Name="ArrowDown" />
                Export distance samples
              </button>
            </>
          }
        />
      )}
      <Note Children="Distances are computed against actual source triangles using a local spatial tree. Samples are voxel-centred, in world metres. This is a CPU reference field, not a native GPU bake or sparse runtime representation." />
      <Note
        Warning={Settings.SdfSigned}
        Children={
          Settings.SdfSigned
            ? "Ray-parity sign requires a closed, non-self-intersecting surface. Open/non-manifold geometry and overlapping solids can produce ambiguous signs; overlaps use parity, not a boolean union. Use unsigned distance when unsure."
            : "Unsigned distance is safe to inspect for open surfaces. It does not classify an inside region. Enable ray-parity sign only for suitable closed geometry."
        }
      />
    </>
  );
}
