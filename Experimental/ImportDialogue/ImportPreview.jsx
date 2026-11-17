import React, { useRef, useEffect, useState } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import { RoomEnvironment } from "three/examples/jsm/environments/RoomEnvironment.js";
import { mergeVertices } from "three/examples/jsm/utils/BufferGeometryUtils.js";
import { VertexNormalsHelper } from "three/examples/jsm/helpers/VertexNormalsHelper.js";
import { GeometryBounds } from "./GeometryCompile.mjs";
export function TransformedGeometry(Source, Settings) {
  if (!Source) return null;
  const Unit = { m: 1, cm: 0.01, mm: 0.001 }[Settings.Units] || 1,
    Rotation = new THREE.Euler(
      ...Settings.Rotation.map((V) => (V * Math.PI) / 180),
      "XYZ",
    ),
    Transform = new THREE.Matrix4().makeRotationFromEuler(Rotation);
  const Axis = new THREE.Matrix4().makeRotationX(
    Settings.Up === "Y" ? Math.PI / 2 : Settings.Up === "X" ? -Math.PI / 2 : 0,
  );
  if (Settings.Up === "X") Axis.makeRotationY(-Math.PI / 2);
  Transform.premultiply(Axis);
  Transform.scale(new THREE.Vector3(...Settings.Scale.map((V) => V * Unit)));
  const NormalTransform = new THREE.Matrix3().getNormalMatrix(Transform),
    Positions = new Float32Array(Source.Positions.length),
    Normals = new Float32Array(Source.Positions.length),
    P = new THREE.Vector3(),
    N = new THREE.Vector3();
  for (let I = 0; I < Source.Positions.length; I += 3) {
    P.fromArray(Source.Positions, I)
      .applyMatrix4(Transform)
      .toArray(Positions, I);
    N.fromArray(Source.Normals || [], I);
    if (!Number.isFinite(N.x)) N.set(0, 0, 1);
    N.applyMatrix3(NormalTransform).normalize().toArray(Normals, I);
  }
  const Bounds = GeometryBounds(Positions),
    Pivot =
      Settings.Pivot === "Origin"
        ? [0, 0, 0]
        : Settings.Pivot === "Bottom"
          ? [...Bounds.Centre.slice(0, 2), Bounds.Minimum[2]]
          : Settings.Pivot === "Custom"
            ? Settings.PivotXYZ
            : Bounds.Centre;
  for (let I = 0; I < Positions.length; I += 3)
    for (let A = 0; A < 3; A++)
      Positions[I + A] += Settings.Translation[A] - Pivot[A];
  const Uvs = Source.Uvs
    ? Float32Array.from(Source.Uvs, (V, I) => {
        let Q = I % 2 && Settings.FlipV ? 1 - V : V;
        return Q * Settings.UvScale + Settings.UvOffset[I % 2];
      })
    : null;
  if (Settings.Flip)
    for (let I = 0; I < Positions.length; I += 9) {
      for (let A = 0; A < 3; A++) {
        const T = Positions[I + 3 + A];
        Positions[I + 3 + A] = Positions[I + 6 + A];
        Positions[I + 6 + A] = T;
      }
      for (let A = 0; A < 3; A++) {
        const T = Normals[I + 3 + A];
        Normals[I + 3 + A] = Normals[I + 6 + A];
        Normals[I + 6 + A] = T;
      }
      for (let A = 0; A < 9; A++) Normals[I + A] *= -1;
      if (Uvs) {
        const U = (I / 3) * 2;
        for (let A = 0; A < 2; A++) {
          const T = Uvs[U + 2 + A];
          Uvs[U + 2 + A] = Uvs[U + 4 + A];
          Uvs[U + 4 + A] = T;
        }
      }
    }
  if (Settings.Normals !== "Source") {
    let Buffer = new THREE.BufferGeometry();
    Buffer.setAttribute("position", new THREE.BufferAttribute(Positions, 3));
    if (Uvs) Buffer.setAttribute("uv", new THREE.BufferAttribute(Uvs, 2));
    if (Settings.Normals === "Smooth") {
      const Merged = mergeVertices(Buffer, 1e-4);
      Buffer.dispose();
      Buffer = Merged;
    }
    Buffer.computeVertexNormals();
    let Flat = Buffer.index ? Buffer.toNonIndexed() : Buffer;
    const Result = {
      ...Source,
      Positions: Flat.attributes.position.array.slice(),
      Normals: Flat.attributes.normal.array.slice(),
      Uvs: Flat.attributes.uv?.array.slice() || null,
    };
    if (Flat !== Buffer) Flat.dispose();
    Buffer.dispose();
    return Result;
  }
  return { ...Source, Positions, Normals, Uvs };
}
const Colours = [
  "#8baec9",
  "#9caf9a",
  "#c5b38a",
  "#9d95b6",
  "#8eb6b0",
  "#bb9290",
  "#b9bfc4",
  "#8b9fba",
];
export function GeometryPreview({
  Geometry,
  Settings,
  Mode,
  Clusters,
  OnCapture,
  FrameSignal = 0,
  SourceKey,
  OnFailure,
}) {
  const Host = useRef(null),
    Runtime = useRef(null),
    Latest = useRef({}),
    [Failure, SetFailure] = useState("");
  Latest.current = { Geometry, Settings, Mode, Clusters, OnCapture };
  useEffect(() => {
    let R;
    try {
      R = new THREE.WebGLRenderer({
        antialias: true,
        alpha: true,
        preserveDrawingBuffer: true,
      });
    } catch (E) {
      SetFailure(
        "WebGL2 is unavailable. Geometry settings and UV inspection remain available.",
      );
      OnFailure?.(E.message);
      return;
    }
    R.setPixelRatio(Math.min(devicePixelRatio, 2));
    R.outputColorSpace = THREE.SRGBColorSpace;
    R.toneMapping = THREE.ACESFilmicToneMapping;
    R.toneMappingExposure = 0.95;
    R.shadowMap.enabled = true;
    R.shadowMap.type = THREE.PCFSoftShadowMap;
    Host.current.appendChild(R.domElement);
    const Scene = new THREE.Scene(),
      Camera = new THREE.PerspectiveCamera(34, 1, 0.01, 2000);
    Camera.up.set(0, 0, 1);
    Camera.position.set(4, -5, 3.6);
    const Controls = new OrbitControls(Camera, R.domElement);
    Controls.enableDamping = true;
    Controls.dampingFactor = 0.08;
    Controls.minDistance = 0.05;
    Controls.maxDistance = 1000;
    const Environment = new RoomEnvironment(),
      Pmrem = new THREE.PMREMGenerator(R),
      Target = Pmrem.fromScene(Environment, 0.04);
    Scene.environment = Target.texture;
    Environment.dispose();
    Pmrem.dispose();
    Scene.add(new THREE.HemisphereLight(0xdce6e4, 0x293536, 1.5));
    const Key = new THREE.DirectionalLight(0xfff0df, 2.2);
    Key.position.set(-3, -5, 7);
    Key.castShadow = true;
    Key.shadow.mapSize.set(1024, 1024);
    Key.shadow.camera.left = Key.shadow.camera.bottom = -8;
    Key.shadow.camera.right = Key.shadow.camera.top = 8;
    Key.shadow.bias = -0.001;
    Scene.add(Key);
    const Rim = new THREE.DirectionalLight(0x9cbdd6, 1.8);
    Rim.position.set(5, 3, 4);
    Scene.add(Rim);
    const Floor = new THREE.Mesh(
      new THREE.CircleGeometry(100, 96),
      new THREE.ShadowMaterial({ color: 0x000000, opacity: 0.35 }),
    );
    Floor.receiveShadow = true;
    Scene.add(Floor);
    const Holder = new THREE.Group();
    Scene.add(Holder);
    const Origin = new THREE.AxesHelper(0.65);
    Scene.add(Origin);
    const Resize = () => {
      if (!Host.current) return;
      const B = Host.current.getBoundingClientRect();
      R.setSize(B.width, B.height, false);
      Camera.aspect = B.width / Math.max(1, B.height);
      Camera.updateProjectionMatrix();
    };
    const Observer = new ResizeObserver(Resize);
    Observer.observe(Host.current);
    Resize();
    let Tick;
    const Paint = () => {
      Tick = requestAnimationFrame(Paint);
      Controls.autoRotate = !!Latest.current.Settings?.Turntable;
      Controls.autoRotateSpeed = 0.65;
      Controls.update();
      R.render(Scene, Camera);
    };
    Paint();
    Runtime.current = {
      R,
      Scene,
      Camera,
      Controls,
      Holder,
      Floor,
      Origin,
      Helpers: [],
      LastSource: null,
    };
    return () => {
      cancelAnimationFrame(Tick);
      Observer.disconnect();
      Controls.dispose();
      Scene.traverse((P) => {
        P.geometry?.dispose();
        if (P.material)
          for (const M of Array.isArray(P.material) ? P.material : [P.material])
            M.dispose();
      });
      Target.dispose();
      R.dispose();
      R.forceContextLoss();
      R.domElement.remove();
      Runtime.current = null;
    };
  }, []);
  useEffect(() => {
    const T = Runtime.current;
    if (!T || !Geometry) return;
    const { Holder, Scene } = T;
    while (Holder.children.length) {
      const P = Holder.children[0];
      P.geometry?.dispose();
      for (const M of Array.isArray(P.material)
        ? P.material
        : P.material
          ? [P.material]
          : [])
        M.dispose();
      Holder.remove(P);
    }
    for (const H of T.Helpers) {
      Scene.remove(H);
      H.geometry?.dispose();
      H.material?.dispose();
    }
    T.Helpers = [];
    let Buffer = new THREE.BufferGeometry();
    Buffer.setAttribute(
      "position",
      new THREE.BufferAttribute(Geometry.Positions, 3),
    );
    if (Geometry.Uvs)
      Buffer.setAttribute("uv", new THREE.BufferAttribute(Geometry.Uvs, 2));
    if (Geometry.Normals)
      Buffer.setAttribute(
        "normal",
        new THREE.BufferAttribute(Geometry.Normals, 3),
      );
    else if (Settings.Normals === "Smooth") {
      const Merged = mergeVertices(Buffer, 1e-4);
      Buffer.dispose();
      Buffer = Merged;
      Buffer.computeVertexNormals();
    } else Buffer.computeVertexNormals();
    if (Buffer.index && Geometry.MaterialIds) {
      const Flat = Buffer.toNonIndexed();
      Buffer.dispose();
      Buffer = Flat;
    }
    let Material;
    if (Mode === "Normals")
      Material = new THREE.MeshNormalMaterial({ side: THREE.DoubleSide });
    else if (Mode === "Clusters" && Clusters) {
      // Per-triangle colours must retain nonindexed order even if smooth normals
      // were computed through a temporary shared-vertex representation.
      if (Buffer.index) {
        const Flat = Buffer.toNonIndexed();
        Buffer.dispose();
        Buffer = Flat;
      }
      const Cs = new Float32Array(Geometry.Positions.length);
      for (let I = 0; I < Geometry.Positions.length / 9; I++) {
        const C = new THREE.Color(Colours[Clusters.Ids[I] % Colours.length]);
        for (let K = 0; K < 3; K++) C.toArray(Cs, I * 9 + K * 3);
      }
      Buffer.setAttribute("color", new THREE.BufferAttribute(Cs, 3));
      Material = new THREE.MeshStandardMaterial({
        vertexColors: true,
        metalness: 0.12,
        roughness: 0.55,
        side: THREE.DoubleSide,
      });
    } else {
      Material = new THREE.MeshStandardMaterial({
        color: Mode === "Checker" ? "#e2e5de" : "#9da8a6",
        metalness: 0.58,
        roughness: 0.31,
        side: THREE.DoubleSide,
      });
      if (Mode === "Checker" && Geometry.Uvs) {
        const C = document.createElement("canvas");
        C.width = C.height = 256;
        const X = C.getContext("2d");
        for (let Y = 0; Y < 16; Y++)
          for (let Z = 0; Z < 16; Z++) {
            X.fillStyle = (Y + Z) % 2 ? "#91aaa6" : "#34484a";
            X.fillRect(Z * 16, Y * 16, 16, 16);
          }
        const Texture = new THREE.CanvasTexture(C);
        Texture.wrapS = Texture.wrapT = THREE.RepeatWrapping;
        Texture.colorSpace = THREE.SRGBColorSpace;
        Material.map = Texture;
      }
    }
    if (
      ["Shaded", "Wireframe"].includes(Mode) &&
      Settings.Materials?.length &&
      Geometry.MaterialIds
    ) {
      Material.dispose();
      Material = Settings.Materials.map((Spec, Id) => {
        const Muted =
          Settings.HighlightMaterial && Id !== Settings.MaterialSelection;
        const Surface = new THREE.MeshStandardMaterial({
          name: Spec.Name,
          color: Muted ? "#303735" : Spec.Colour,
          metalness: Spec.Metalness,
          roughness: Spec.Roughness,
          opacity: Muted ? 0.22 : Spec.Opacity,
          transparent: Muted || Spec.Opacity < 1,
          depthWrite: !Muted && Spec.Opacity > 0.9,
          emissive: Spec.Emissive,
          emissiveIntensity: Muted ? 0 : Spec.EmissiveIntensity,
          side: Spec.DoubleSided ? THREE.DoubleSide : THREE.FrontSide,
        });
        for (const [Key, Record] of [
          ["map", Spec.BaseMap],
          ["normalMap", Spec.NormalMap],
          ["roughnessMap", Spec.RoughnessMap],
          ["metalnessMap", Spec.MetalnessMap],
        ])
          if (Record?.Image && Geometry.Uvs) {
            const Texture = new THREE.TextureLoader().load(Record.Image);
            if (Key === "map") Texture.colorSpace = THREE.SRGBColorSpace;
            Texture.flipY = Record.FlipY ?? true;
            Texture.wrapS = Record.WrapS ?? THREE.RepeatWrapping;
            Texture.wrapT = Record.WrapT ?? THREE.RepeatWrapping;
            Texture.repeat.fromArray(Record.Repeat || [1, 1]);
            Texture.offset.fromArray(Record.Offset || [0, 0]);
            Texture.center.fromArray(Record.Centre || [0, 0]);
            Texture.rotation = Record.Rotation || 0;
            // Only UV0 is retained in this standalone reference.
            Surface[Key] = Texture;
          }
        return Surface;
      });
      Buffer.clearGroups();
      let Start = 0;
      const Ids = Geometry.MaterialIds;
      for (let I = 1; I <= Ids.length; I++)
        if (I === Ids.length || Ids[I] !== Ids[Start]) {
          Buffer.addGroup(
            Start * 3,
            (I - Start) * 3,
            Math.min(Ids[Start], Material.length - 1),
          );
          Start = I;
        }
    }
    const Model = new THREE.Mesh(Buffer, Material);
    Model.castShadow = true;
    Model.receiveShadow = true;
    Holder.add(Model);
    if (Mode === "Wireframe") {
      const Wire = new THREE.LineSegments(
        new THREE.WireframeGeometry(Buffer),
        new THREE.LineBasicMaterial({
          color: "#a9cfcb",
          transparent: true,
          opacity: 0.3,
        }),
      );
      Holder.add(Wire);
    }
    const Bounds = new THREE.Box3().setFromObject(Holder),
      Size = Bounds.getSize(new THREE.Vector3()),
      Span = Math.max(Size.x, Size.y, Size.z, 0.01);
    T.Floor.position.z = Bounds.min.z - 0.015 * Span;
    T.Origin.scale.setScalar(Span * 0.27);
    T.Origin.visible = Settings.ShowPivot;
    if (Settings.ShowBounds) {
      const Box = new THREE.Box3Helper(Bounds, 0x7d9e99);
      Scene.add(Box);
      T.Helpers.push(Box);
    }
    if (Settings.ShowNormals) {
      const Normals = new VertexNormalsHelper(Model, Span * 0.025, 0xa8c8df);
      Scene.add(Normals);
      T.Helpers.push(Normals);
    }
    const Frame = () => {
      const Centre = Bounds.getCenter(new THREE.Vector3());
      T.Controls.target.copy(Centre);
      T.Camera.position.copy(
        Centre.clone().add(
          new THREE.Vector3(1.45, -1.8, 1.15).multiplyScalar(Span),
        ),
      );
      T.Camera.near = Span / 1000;
      T.Camera.far = Span * 100;
      T.Camera.updateProjectionMatrix();
      T.Controls.update();
    };
    T.Frame = Frame;
    if (T.LastSource !== SourceKey) {
      Frame();
      T.LastSource = SourceKey;
    }
    OnCapture.current = () => {
      T.R.render(T.Scene, T.Camera);
      return T.R.domElement.toDataURL("image/png");
    };
    return () => {
      for (const M of Array.isArray(Material) ? Material : [Material])
        for (const Key of ["map", "normalMap", "roughnessMap", "metalnessMap"])
          M[Key]?.dispose();
    };
  }, [
    Geometry,
    Mode,
    Clusters,
    Settings.Normals,
    Settings.ShowBounds,
    Settings.Materials,
    Settings.MaterialSelection,
    Settings.HighlightMaterial,
    Settings.ShowPivot,
    Settings.ShowNormals,
    SourceKey,
  ]);
  useEffect(() => {
    Runtime.current?.Frame?.();
  }, [FrameSignal]);
  return (
    <div
      className="geometry-render"
      ref={Host}
      aria-label="Interactive geometry preview"
      role="img"
    >
      {Failure && <p className="preview-failure">{Failure}</p>}
    </div>
  );
}
export function UvPreview({ Geometry, Settings, OnSettings }) {
  const Canvas = useRef(null),
    [Zoom, SetZoom] = useState(1),
    [Checker, SetChecker] = useState(true);
  useEffect(() => {
    const X = Canvas.current.getContext("2d"),
      W = 800,
      H = 800;
    X.clearRect(0, 0, W, H);
    const Margin = 66,
      Size = 668 * Zoom,
      Start = (800 - Size) / 2;
    X.save();
    X.beginPath();
    X.rect(40, 40, 720, 720);
    X.clip();
    if (Checker)
      for (let Y = 0; Y < 16; Y++)
        for (let Z = 0; Z < 16; Z++) {
          X.fillStyle = (Y + Z) % 2 ? "#1c2221" : "#151a19";
          X.fillRect(
            Start + (Z * Size) / 16,
            Start + (Y * Size) / 16,
            Size / 16,
            Size / 16,
          );
        }
    if (Geometry?.Uvs) {
      const U = Geometry.Uvs;
      for (let I = 0; I < U.length; I += 6) {
        const Id =
          (Math.floor(U[I] * 7) + Math.floor(U[I + 1] * 7) * 7) %
          Colours.length;
        X.beginPath();
        for (let K = 0; K < 3; K++) {
          const Px = Start + U[I + K * 2] * Size,
            Py = Start + (1 - U[I + K * 2 + 1]) * Size;
          K ? X.lineTo(Px, Py) : X.moveTo(Px, Py);
        }
        X.closePath();
        X.fillStyle = Colours[Id] + "16";
        X.fill();
        X.strokeStyle = "#94beb270";
        X.lineWidth = 0.65;
        X.stroke();
      }
    }
    X.strokeStyle = "#afc4ba80";
    X.lineWidth = 1.5;
    X.strokeRect(Start, Start, Size, Size);
    X.restore();
    X.fillStyle = "#969b94";
    X.font = '17px "DM Sans"';
    X.textAlign = "left";
    X.fillText("0", 45, 784);
    X.fillText("1001", 66, 55);
    X.textAlign = "right";
    X.fillText("U →", 750, 784);
    X.fillText("V ↑", 38, 55);
  }, [Geometry, Zoom, Checker]);
  return (
    <div className="uv-preview">
      <div className="uv-toolbar">
        <span>
          UV0 <small>Primary coordinates</small>
        </span>
        <button aria-pressed={Checker} onClick={() => SetChecker(!Checker)}>
          Checker
        </button>
        <button onClick={() => SetZoom(Math.max(0.5, Zoom - 0.25))}>−</button>
        <span>{Math.round(Zoom * 100)}%</span>
        <button onClick={() => SetZoom(Math.min(3, Zoom + 0.25))}>+</button>
        <button onClick={() => SetZoom(1)}>Fit</button>
      </div>
      <canvas
        ref={Canvas}
        width="800"
        height="800"
        role="img"
        aria-label="UV0 coordinate layout"
      />
      {!Geometry?.Uvs && (
        <p className="uv-empty">
          This source has no UV0 coordinates.
          <br />
          Native UV unwrapping is not connected.
        </p>
      )}
      <div className="preview-caption">
        Primary tile · UV0<span>Triangle edges · source coordinates</span>
      </div>
    </div>
  );
}
export function MediaPreview({ Source, Settings }) {
  const [Failure, SetFailure] = useState("");
  const [Dimensions, SetDimensions] = useState("");
  useEffect(() => {
    SetFailure("");
    SetDimensions("");
    if (Source.Kind !== "Font") return;
    let Font,
      Mounted = true;
    try {
      Font = new FontFace("ImportedSpecimen", `url(${Source.Url})`);
      Font.load()
        .then((F) => {
          if (Mounted) document.fonts.add(F);
        })
        .catch(() => {
          if (Mounted)
            SetFailure(
              "The browser cannot decode this font. The source can still be imported.",
            );
        });
    } catch {
      SetFailure("Font preview unavailable.");
    }
    return () => {
      Mounted = false;
      if (Font) document.fonts.delete(Font);
    };
  }, [Source]);
  return (
    <div className={`media-preview kind-${Source.Kind.toLowerCase()}`}>
      {Source.Kind === "Image" && (
        <>
          <div className="image-checker">
            <img
              alt={Source.Name}
              src={Source.Url}
              style={{
                filter:
                  Settings.Channel === "Red"
                    ? "url(#red-channel)"
                    : Settings.Channel === "Grayscale"
                      ? "grayscale(1)"
                      : "none",
                imageRendering:
                  Settings.Filter === "Nearest" ? "pixelated" : "auto",
              }}
              onLoad={(E) =>
                SetDimensions(
                  `${E.target.naturalWidth} × ${E.target.naturalHeight} px`,
                )
              }
              onError={() =>
                SetFailure(
                  "This image format has no browser decoder. The original will still be retained.",
                )
              }
            />
          </div>
          <span className="media-dimensions">
            {Dimensions || "Image source"}
          </span>
          <svg width="0" height="0">
            <filter id="red-channel">
              <feColorMatrix
                type="matrix"
                values="1 0 0 0 0  1 0 0 0 0  1 0 0 0 0  0 0 0 1 0"
              />
            </filter>
          </svg>
        </>
      )}
      {Source.Kind === "Audio" && (
        <>
          <AudioWave Source={Source} />
          <audio
            controls
            src={Source.Url}
            loop={Settings.Loop}
            onError={() =>
              SetFailure(
                "Audio codec not supported by this browser; original source retained.",
              )
            }
          />
        </>
      )}
      {Source.Kind === "Video" && (
        <video
          controls
          src={Source.Url}
          loop={Settings.Loop}
          onError={() =>
            SetFailure(
              "Video codec not supported by this browser; original source retained.",
            )
          }
        />
      )}
      {Source.Kind === "Font" && (
        <div
          className="font-specimen"
          style={{ fontFamily: "ImportedSpecimen,serif" }}
        >
          <span>Aa</span>
          <p>
            Sphinx of black quartz,
            <br />
            judge my vow.
          </p>
          <small>
            ABCDEFGHIJKLMNOPQRSTUVWXYZ
            <br />
            abcdefghijklmnopqrstuvwxyz
            <br />
            0123456789
          </small>
        </div>
      )}
      {Source.Kind === "Document" && (
        <pre className="document-specimen">{Source.Text}</pre>
      )}
      {Source.Kind === "Binary" && (
        <div className="binary-specimen">
          <img src={globalThis.NativeAssets.Icons[Source.Icon]} alt="" />
          <h2>{Source.Name}</h2>
          <p>No browser decoder · original bytes retained</p>
          <pre>{Source.Hex}</pre>
        </div>
      )}
      {Failure && (
        <p className="media-failure" role="status">
          {Failure}
        </p>
      )}
    </div>
  );
}
function AudioWave({ Source }) {
  const Canvas = useRef(null),
    [Duration, SetDuration] = useState("");
  useEffect(() => {
    let Active = true;
    const Context = new (window.AudioContext || window.webkitAudioContext)();
    fetch(Source.Url)
      .then((R) => R.arrayBuffer())
      .then((B) => Context.decodeAudioData(B))
      .then((Buffer) => {
        if (!Active) return;
        SetDuration(
          `${Buffer.duration.toFixed(2)} s · ${Buffer.sampleRate.toLocaleString()} Hz · ${Buffer.numberOfChannels} channel(s)`,
        );
        const X = Canvas.current.getContext("2d"),
          P = Buffer.getChannelData(0);
        X.clearRect(0, 0, 1000, 340);
        X.strokeStyle = "#8fb5b0";
        X.lineWidth = 1.5;
        X.beginPath();
        for (let I = 0; I < 900; I++) {
          const Start = Math.floor((I / 900) * P.length),
            End = Math.floor(((I + 1) / 900) * P.length);
          let Peak = 0;
          for (let J = Start; J < End; J++)
            Peak = Math.max(Peak, Math.abs(P[J]));
          X.moveTo(50 + I, 170 - Peak * 260);
          X.lineTo(50 + I, 170 + Peak * 260);
        }
        X.stroke();
      })
      .catch(() => SetDuration("Waveform unavailable for this codec"))
      .finally(() => Context.close());
    return () => {
      Active = false;
    };
  }, [Source]);
  return (
    <div className="audio-wave">
      <span>
        WAVEFORM <small>{Duration}</small>
      </span>
      <canvas
        ref={Canvas}
        width="1000"
        height="340"
        role="img"
        aria-label="Decoded audio waveform"
      />
    </div>
  );
}
