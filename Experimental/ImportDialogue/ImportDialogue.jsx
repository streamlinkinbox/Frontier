import MaterialInspector from "./MaterialInspector.jsx";
import DistanceInspector, { DistancePreview } from "./DistanceInspector.jsx";
import { BakeDistanceField } from "./SdfBake.mjs";
import React, { useState, useEffect, useMemo, useRef } from "react";
import { createRoot } from "react-dom/client";
import {
  GeometryPreview,
  UvPreview,
  MediaPreview,
  TransformedGeometry,
} from "./ImportPreview.jsx";
import {
  SampleEntries,
  BuiltInSource,
  ReadSource,
  FileIcon,
} from "./ImportSources.js";
import {
  GeometryBounds,
  UvStatistics,
  BakeDetailLevels,
} from "./GeometryCompile.mjs";
import ActionIcon from "../FrontierWorldMirror/ActionIcon.jsx";
import "./ImportDialogue.css";
const A = globalThis.NativeAssets;
const NumberText = (N, D = 0) =>
  Number(N).toLocaleString("en", { maximumFractionDigits: D });
const Bytes = (N) =>
  N > 1048576
    ? `${(N / 1048576).toFixed(2)} MB`
    : N > 1024
      ? `${(N / 1024).toFixed(1)} KB`
      : `${N || 0} B`;
const Steps = [
  ["Source", "slate-document", "File & destination"],
  ["Geometry", "editor-move", "Transform & normals"],
  ["UVs", "editor-texture", "Coordinates & checker"],
  ["Detail levels", "slate-build", "LOD & spatial clusters"],
  ["Materials", "editor-material", "Surface & textures"],
  ["SDF bake", "editor-box-collider", "Distance field"],
  ["Metadata", "editor-prefab", "Identity & icon"],
];
const Defaults = () => ({
  Materials: [],
  MaterialSelection: 0,
  HighlightMaterial: false,
  SdfResolution: 24,
  SdfPadding: 0.12,
  SdfSigned: false,
  SdfAxis: "XY",
  SdfSlice: 12,
  Units: "m",
  Up: "Y",
  Translation: [0, 0, 0],
  Rotation: [0, 0, 0],
  Scale: [1, 1, 1],
  Uniform: true,
  Pivot: "Center",
  PivotXYZ: [0, 0, 0],
  Normals: "Source",
  Flip: false,
  ShowNormals: false,
  ShowBounds: false,
  ShowPivot: true,
  Turntable: false,
  UvScale: 1,
  UvOffset: [0, 0],
  FlipV: false,
  Levels: 4,
  Ratio: 0.5,
  Capacity: 128,
  UvAware: true,
  Author: "",
  License: "Unspecified",
  Tags: "mechanical, environment",
  Description: "",
  Custom: [],
  Icon: "",
  IconImage: "",
  Destination: "Content / Mechanical",
  Channel: "RGB",
  ColourSpace: "sRGB",
  Filter: "Linear",
  Loop: false,
  SampleRate: "Preserve",
  Encoding: "UTF-8",
});
function Icon({ Name, Size = 20, ...Props }) {
  return (
    <img
      width={Size}
      height={Size}
      src={A.Icons[Name] || A.Icons.package}
      alt=""
      {...Props}
    />
  );
}
function G({ Name, Size = 16 }) {
  return (
    <svg
      width={Size}
      height={Size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.6"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      <path d={A.Glyphs[Name] || A.Glyphs.CircleInfo} />
    </svg>
  );
}
function Section({ Title, Tag, Children }) {
  return (
    <section className="import-section">
      <header>
        <i />
        <h3>{Title}</h3>
        {Tag && <span>{Tag}</span>}
      </header>
      {Children}
    </section>
  );
}
function Field({ Label, Children, Note }) {
  return (
    <label className="field">
      <span>{Label}</span>
      {Children}
      {Note && <small>{Note}</small>}
    </label>
  );
}
function Select({ Label, Value, Options, Change }) {
  return (
    <Field
      Label={Label}
      Children={
        <select
          aria-label={Label}
          value={Value}
          onChange={(E) => Change(E.target.value)}
        >
          {Options.map((O) => (
            <option key={O}>{O}</option>
          ))}
        </select>
      }
    />
  );
}
function Scalar({ Label, Value, Min, Max, Step = 0.01, Unit = "", Change }) {
  const Write = (V) => {
    if (Number.isFinite(+V)) Change(Math.min(Max, Math.max(Min, +V)));
  };
  return (
    <Field
      Label={Label}
      Children={
        <div className="slider-pill">
          <div className="split-value">
            <input
              type="number"
              aria-label={Label + " value"}
              min={Min}
              max={Max}
              step={Step}
              value={Number(Value.toFixed(3))}
              onChange={(E) => Write(E.target.value)}
            />
            <small>{Unit || "·"}</small>
          </div>
          <input
            type="range"
            aria-label={Label}
            min={Min}
            max={Max}
            step={Step}
            value={Value}
            style={{ "--fill": `${((Value - Min) / (Max - Min)) * 100}%` }}
            onChange={(E) => Write(E.target.value)}
          />
        </div>
      }
    />
  );
}
function Toggle({ Name, On, Change, Symbol = "visible" }) {
  return (
    <button
      className={`toggle ${On ? "on" : ""}`}
      aria-pressed={!!On}
      onClick={() => Change(!On)}
    >
      <span className="quick-icon-seat">
        <ActionIcon Name={Symbol} Size={20} />
      </span>
      <span>{Name}</span>
      <small>{On ? "ON" : "OFF"}</small>
    </button>
  );
}
function Triple({
  Label,
  Values,
  Change,
  Unit = "",
  Min = -10000,
  Max = 10000,
}) {
  return (
    <div className="triple">
      <div className="triple-label">
        {Label}
        <small>{Unit}</small>
      </div>
      <div>
        {Values.map((V, I) => (
          <label key={I}>
            <span className={"axis axis-" + I}>{"XYZ"[I]}</span>
            <input
              aria-label={Label + " " + "XYZ"[I]}
              type="number"
              min={Min}
              max={Max}
              step="0.01"
              value={V}
              onChange={(E) => {
                if (!Number.isFinite(+E.target.value)) return;
                const Next = [...Values];
                Next[I] = Math.min(Max, Math.max(Min, +E.target.value));
                Change(Next, I);
              }}
            />
          </label>
        ))}
      </div>
    </div>
  );
}
function Readout({ Label, Value }) {
  return (
    <div className="readout">
      <span>{Label}</span>
      <output>{Value}</output>
    </div>
  );
}
function Note({ Children, Warning = false }) {
  return (
    <p className={`note ${Warning ? "warning" : ""}`}>
      <G Name={Warning ? "TriangleAlert" : "CircleInfo"} />
      <span>{Children}</span>
    </p>
  );
}
function DownloadJson(Name, Record) {
  const Url = URL.createObjectURL(
      new Blob(
        [
          JSON.stringify(
            Record,
            (_, V) => (ArrayBuffer.isView(V) ? Array.from(V) : V),
            2,
          ),
        ],
        { type: "application/json" },
      ),
    ),
    Link = document.createElement("a");
  Link.href = Url;
  Link.download = Name;
  Link.click();
  setTimeout(() => URL.revokeObjectURL(Url), 1000);
}
function SaveImport(Record) {
  return new Promise((Resolve, Reject) => {
    const Request = indexedDB.open("Frontier.ImportDialogue", 1);
    Request.onupgradeneeded = () =>
      Request.result.createObjectStore("Imports", { keyPath: "Id" });
    Request.onerror = () => Reject(Request.error);
    Request.onsuccess = () => {
      const Db = Request.result,
        Tx = Db.transaction("Imports", "readwrite");
      Tx.objectStore("Imports").put(Record);
      Tx.oncomplete = () => {
        Db.close();
        Resolve();
      };
      Tx.onerror = () => {
        Db.close();
        Reject(Tx.error);
      };
    };
  });
}
function ReadImports() {
  return new Promise((Resolve, Reject) => {
    const Request = indexedDB.open("Frontier.ImportDialogue", 1);
    Request.onupgradeneeded = () =>
      Request.result.createObjectStore("Imports", { keyPath: "Id" });
    Request.onerror = () => Reject(Request.error);
    Request.onsuccess = () => {
      const Db = Request.result,
        Tx = Db.transaction("Imports", "readonly"),
        Get = Tx.objectStore("Imports").getAll();
      Get.onsuccess = () => {
        Resolve(Get.result.sort((A, B) => B.Created.localeCompare(A.Created)));
        Db.close();
      };
      Get.onerror = () => {
        Reject(Get.error);
        Db.close();
      };
    };
  });
}
const ImportControls = {
  Section,
  Scalar,
  Select,
  Toggle,
  Field,
  Note,
  Readout,
  Icon,
  G,
};
function App() {
  const [Source, SetSource] = useState(null),
    [Settings, SetSettings] = useState(Defaults),
    [Name, SetName] = useState("Turbine_Housing"),
    [Page, SetPage] = useState(0),
    [Open, SetOpen] = useState(true),
    [Mode, SetMode] = useState("Shaded"),
    [Pending, SetPending] = useState(false),
    [Failure, SetFailure] = useState(""),
    [Notice, SetNotice] = useState(""),
    [Bake, SetBake] = useState(null),
    [Progress, SetProgress] = useState(0),
    [Baking, SetBaking] = useState(false),
    [Level, SetLevel] = useState(0),
    [Frame, SetFrame] = useState(0),
    [Library, SetLibrary] = useState([]),
    [Complete, SetComplete] = useState(false),
    [Saving, SetSaving] = useState(false),
    [DragOver, SetDragOver] = useState(false),
    [Sdf, SetSdf] = useState(null),
    [SdfBaking, SetSdfBaking] = useState(false),
    [SdfProgress, SetSdfProgress] = useState(0);
  const FilesInput = useRef(null),
    IconInput = useRef(null),
    Capture = useRef(null),
    BakeToken = useRef(0),
    SdfToken = useRef(0),
    LoadToken = useRef(0),
    PreviousSource = useRef(null),
    Dialog = useRef(null),
    Launch = useRef(null),
    RejectedFiles = useRef(null);
  const Close = () => {
    LoadToken.current++;
    SetPending(false);
    SetOpen(false);
  };
  const Change = (Key, Value) => SetSettings((P) => ({ ...P, [Key]: Value }));
  const IsGeometry = Source?.Kind === "Geometry",
    Geometry = useMemo(
      () =>
        IsGeometry ? TransformedGeometry(Source.Geometry, Settings) : null,
      [
        Source,
        Settings.Translation,
        Settings.Rotation,
        Settings.Scale,
        Settings.Units,
        Settings.Up,
        Settings.Pivot,
        Settings.PivotXYZ,
        Settings.Normals,
        Settings.Flip,
        Settings.UvScale,
        Settings.UvOffset,
        Settings.FlipV,
      ],
    ),
    Bounds = useMemo(
      () => GeometryBounds(Geometry?.Positions || []),
      [Geometry],
    ),
    Uvs = useMemo(() => UvStatistics(Geometry?.Uvs), [Geometry]);
  const Signature = useMemo(
    () =>
      JSON.stringify(
        IsGeometry
          ? {
              P: Settings.Translation,
              R: Settings.Rotation,
              S: Settings.Scale,
              U: Settings.Units,
              Up: Settings.Up,
              Pivot: Settings.Pivot,
              PivotXYZ: Settings.PivotXYZ,
              Flip: Settings.Flip,
              UvScale: Settings.UvScale,
              UvOffset: Settings.UvOffset,
              FlipV: Settings.FlipV,
              Normals: Settings.Normals,
              Levels: Settings.Levels,
              Ratio: Settings.Ratio,
              Capacity: Settings.Capacity,
              UvAware: Settings.UvAware,
            }
          : null,
      ),
    [Settings, IsGeometry],
  );
  const SdfSignature = JSON.stringify({
    P: Settings.Translation,
    R: Settings.Rotation,
    S: Settings.Scale,
    Up: Settings.Up,
    Units: Settings.Units,
    Pivot: Settings.Pivot,
    PivotXYZ: Settings.PivotXYZ,
    Normals: Settings.Normals,
    Flip: Settings.Flip,
    UvScale: Settings.UvScale,
    UvOffset: Settings.UvOffset,
    FlipV: Settings.FlipV,
    Resolution: Settings.SdfResolution,
    Padding: Settings.SdfPadding,
    Signed: Settings.SdfSigned,
  });
  const SdfFresh = !!Sdf && Sdf.Signature === SdfSignature;
  const CurrentSignature = useRef(Signature);
  CurrentSignature.current = Signature;
  const Fresh = Bake && Bake.Signature === Signature,
    DisplayGeometry =
      Page === 3 && Fresh ? Bake.Levels[Level]?.Geometry || Geometry : Geometry,
    DisplayClusters = Page === 3 && Fresh ? Bake.Levels[Level] : null;
  const Load = async (Entries, Sample = false, SourceOnly = false) => {
    // FileList is live: snapshot before the chooser resets its input value.
    const Selected = Sample ? Entries : Array.from(Entries || []);
    RejectedFiles.current = null;
    const Token = ++LoadToken.current;
    BakeToken.current++;
    SdfToken.current++;
    SetSdfBaking(false);
    SetSdfProgress(0);
    SetBaking(false);
    SetProgress(0);
    SetPending(true);
    SetFailure("");
    SetComplete(false);
    try {
      const Next = Sample
        ? await BuiltInSource(Entries)
        : await ReadSource(Selected, SourceOnly);
      if (Token !== LoadToken.current) {
        if (Next.Url) URL.revokeObjectURL(Next.Url);
        return;
      }
      SetSource(Next);
      SetName(Next.Name.replace(/\.[^.]+$/, ""));
      SetSettings({
        ...Defaults(),
        Tags: Next.Kind === "Geometry" ? "mechanical, environment" : "",
        Destination:
          "Content / " +
          {
            Geometry: "Mechanical",
            Image: "Textures",
            Audio: "Audio",
            Video: "Video",
            Font: "Fonts",
            Document: "Documents",
            Binary: "Files",
          }[Next.Kind],
        Icon: Next.Icon,
        Materials: structuredClone(Next.Geometry?.Materials || []),
      });
      SetBake(null);
      SetSdf(null);
      SetLevel(0);
      SetPage(0);
      SetMode("Shaded");
      SetNotice("");
      SetOpen(true);
    } catch (E) {
      if (Token === LoadToken.current) {
        if (!Sample) RejectedFiles.current = Selected;
        SetFailure(E.message || "Could not read this source.");
      }
    } finally {
      if (Token === LoadToken.current) SetPending(false);
    }
  };
  useEffect(() => {
    ReadImports()
      .then(SetLibrary)
      .catch(() => {});
    Load("housing", true);
    return () => {
      LoadToken.current++;
      BakeToken.current++;
      SdfToken.current++;
    };
  }, []);
  useEffect(() => {
    const Previous = PreviousSource.current;
    if (Previous?.Url && Previous !== Source) URL.revokeObjectURL(Previous.Url);
    PreviousSource.current = Source;
  }, [Source]);
  useEffect(() => {
    if (!Open) {
      Launch.current?.focus();
      return;
    }
    const Before = document.activeElement;
    requestAnimationFrame(() =>
      Dialog.current
        ?.querySelector(Complete ? ".completion-overlay button" : "button")
        ?.focus(),
    );
    const Keyboard = (E) => {
      if (E.key === "Escape") {
        if (!Baking && !SdfBaking && !Saving) Close();
        return;
      }
      if (E.key === "Tab") {
        const Focusable = [
          ...(Complete
            ? Dialog.current.querySelector(".completion-overlay")
            : Dialog.current
          ).querySelectorAll(
            'button:not(:disabled),input:not(:disabled),select:not(:disabled),textarea:not(:disabled),[tabindex="0"]',
          ),
        ].filter((N) => !N.closest("[inert]") && N.getClientRects().length);
        const First = Focusable[0],
          Last = Focusable.at(-1);
        if (E.shiftKey && document.activeElement === First) {
          E.preventDefault();
          Last?.focus();
        } else if (!E.shiftKey && document.activeElement === Last) {
          E.preventDefault();
          First?.focus();
        }
      }
    };
    document.addEventListener("keydown", Keyboard);
    return () => {
      document.removeEventListener("keydown", Keyboard);
      Before?.focus?.();
    };
  }, [Open, Baking, SdfBaking, Saving, Complete]);
  useEffect(() => {
    Dialog.current
      ?.querySelectorAll(".inspector-page")
      .forEach((P) => (P.scrollTop = 0));
  }, [Page]);
  useEffect(() => {
    const Tab = document.getElementById("import-tab-" + Page),
      Rail = Tab?.parentElement;
    if (Tab && Rail)
      Rail.scrollTo({
        left: Tab.offsetLeft - (Rail.clientWidth - Tab.clientWidth) / 2,
        behavior: "smooth",
      });
  }, [Page, Open]);
  const Go = (P) => {
    let Next = Math.max(0, Math.min(Steps.length - 1, P));
    if (!IsGeometry && [1, 2, 3, 4, 5].includes(Next))
      Next = P > Page ? Steps.length - 1 : 0;
    SetPage(Next);
    if (Next === 3 && Fresh) SetMode("Clusters");
    else if (Next === 2) SetMode("Checker");
    else SetMode("Shaded");
  };
  const StartBake = async () => {
    if (!Geometry || Baking || SdfBaking) return;
    const Token = ++BakeToken.current,
      Snapshot = Signature;
    SetBaking(true);
    SetFailure("");
    SetProgress(0);
    const Start = performance.now();
    try {
      const Levels = await BakeDetailLevels(
        Geometry,
        {
          Levels: Settings.Levels,
          Ratio: Settings.Ratio,
          Capacity: Settings.Capacity,
          UvAware: Settings.UvAware,
        },
        (V) => {
          if (Token === BakeToken.current) SetProgress(V);
        },
        () => Token !== BakeToken.current,
      );
      if (Token !== BakeToken.current) return;
      SetBake({
        Levels,
        Signature: Snapshot,
        Milliseconds: performance.now() - Start,
      });
      SetLevel(0);
      SetMode("Clusters");
      if (Snapshot !== CurrentSignature.current)
        SetNotice(
          "Bake completed for earlier settings. Rebuild to apply your latest changes.",
        );
    } catch (E) {
      if (Token === BakeToken.current) SetFailure(E.message);
    } finally {
      if (Token === BakeToken.current) SetBaking(false);
    }
  };
  const StartSdf = async () => {
    if (!Geometry || SdfBaking || Baking) return;
    const Token = ++SdfToken.current,
      Snapshot = SdfSignature,
      Start = performance.now();
    SetSdfBaking(true);
    SetSdfProgress(0);
    SetFailure("");
    try {
      const Volume = await BakeDistanceField(
        Geometry,
        {
          Resolution: Settings.SdfResolution,
          Padding: Settings.SdfPadding,
          Signed: Settings.SdfSigned,
        },
        (P) => {
          if (Token === SdfToken.current) SetSdfProgress(P);
        },
        () => Token !== SdfToken.current,
      );
      if (Token !== SdfToken.current) return;
      SetSdf({
        ...Volume,
        Signature: Snapshot,
        Milliseconds: performance.now() - Start,
      });
      Change("SdfSlice", Math.floor(Volume.Resolution / 2));
    } catch (E) {
      if (Token === SdfToken.current) SetFailure(E.message);
    } finally {
      if (Token === SdfToken.current) SetSdfBaking(false);
    }
  };
  const Manifest = () => ({
    Version: 1,
    Name: Name.trim(),
    Source: {
      Name: Source.Name,
      Kind: Source.Kind,
      Bytes: Source.Bytes,
      Companions: Source.Files.map((F) => ({ Name: F.name, Bytes: F.size })),
    },
    Settings,
    Geometry: Geometry
      ? {
          Triangles: Geometry.Positions.length / 9,
          Bounds,
          Uv0: !!Geometry.Uvs,
        }
      : null,
    DistanceField: SdfFresh
      ? {
          Method: Sdf.Method,
          Resolution: Sdf.Resolution,
          Bounds: Sdf.Bounds,
          Spacing: Sdf.Spacing,
          Signed: Sdf.Signed,
          MinimumDistance: Sdf.MinimumDistance,
          MaximumDistance: Sdf.MaximumDistance,
        }
      : null,
    ClusterBake: Fresh
      ? {
          Method:
            "Spatial vertex clustering + Morton triangle groups; not Nanite",
          Levels: Bake.Levels.map((L) => ({
            Level: L.Level,
            Triangles: L.Triangles,
            Clusters: L.Clusters.length,
            MaximumVertexDisplacement: L.Displacement,
          })),
        }
      : null,
  });
  const Submit = async () => {
    if (!Name.trim() || !Source || Pending || Saving || Baking || SdfBaking)
      return;
    SetSaving(true);
    SetFailure("");
    try {
      const Thumbnail = Settings.IconImage || null,
        Id = crypto.randomUUID(),
        Record = {
          Id,
          Name: Name.trim(),
          Kind: Source.Kind,
          Icon: Settings.Icon,
          Thumbnail: Thumbnail?.startsWith("data:") ? Thumbnail : null,
          Manifest: Manifest(),
          Files: Source.Files,
          Geometry: IsGeometry ? Geometry : null,
          SourceGeometry: Source.Geometry || null,
          BakeSignature: Fresh ? Signature : null,
          Levels: Fresh ? Bake.Levels : null,
          DistanceField: SdfFresh ? Sdf : null,
          Created: new Date().toISOString(),
        };
      await SaveImport(Record);
      SetLibrary((P) => [Record, ...P]);
      SetComplete(true);
    } catch (E) {
      SetFailure(
        "Could not save in browser storage: " +
          E.message +
          ". Use Export settings to keep this configuration.",
      );
    } finally {
      SetSaving(false);
    }
  };
  const Reopen = async (Record) => {
    SetPending(true);
    SetFailure("");
    SetComplete(false);
    try {
      let Next;
      if (Record.Kind === "Geometry")
        Next = {
          Name: Record.Manifest.Source.Name,
          Kind: Record.Kind,
          Icon: Record.Icon,
          Bytes: Record.Manifest.Source.Bytes,
          Files: Record.Files,
          Geometry: Record.SourceGeometry || Record.Geometry,
          Notice:
            "Restored from this browser’s local library. Original source retained.",
        };
      else Next = await ReadSource(Record.Files, Record.Kind === "Binary");
      SetSource(Next);
      SetName(Record.Name);
      SetSettings({ ...Defaults(), ...Record.Manifest.Settings });
      SetBake(
        Record.Levels
          ? {
              Levels: Record.Levels,
              Signature: Record.BakeSignature,
              Milliseconds: 0,
            }
          : null,
      );
      SetLevel(0);
      SetSdf(Record.DistanceField || null);
      SetMode("Shaded");
      SetPage(0);
      SetOpen(true);
    } catch (E) {
      SetFailure("Could not reopen this import: " + E.message);
    } finally {
      SetPending(false);
    }
  };
  const UploadIcon = async (F) => {
    if (!F) return;
    if (
      !["image/png", "image/jpeg", "image/webp", "image/svg+xml"].includes(
        F.type,
      ) ||
      F.size > 2 * 1024 * 1024
    ) {
      SetFailure("Choose a PNG, JPEG, WebP or SVG icon smaller than 2 MB.");
      return;
    }
    const Reader = new FileReader();
    Reader.onload = () => {
      Change("IconImage", Reader.result);
      SetNotice("Custom icon selected.");
    };
    Reader.onerror = () => SetFailure("Icon could not be read.");
    Reader.readAsDataURL(F);
  };
  const CaptureIcon = () => {
    const Url = Capture.current?.();
    if (Url) {
      Change("IconImage", Url);
      SetNotice("Current 3D view captured as the import icon.");
    } else SetFailure("A 3D preview is required to capture an icon.");
  };
  const IconPreview = Settings.IconImage ? (
    <img
      className="custom-icon"
      src={Settings.IconImage}
      alt="Selected import icon"
    />
  ) : (
    <Icon Name={Settings.Icon || Source?.Icon} Size={76} />
  );
  return (
    <div className="import-app">
      <header className="workspace-top" inert={Open ? "" : undefined}>
        <div className="frontier-mark">
          F
          <span>
            FRONTIER <small>WORLD MIRROR</small>
          </span>
        </div>
        <span className="workspace-project">
          Cornell–Studio Gallery <G Name="ChevronDown" />
        </span>
        <div>
          <span className="status-dot" /> All changes local{" "}
          <button
            ref={Launch}
            onClick={() => {
              SetComplete(false);
              SetOpen(true);
            }}
          >
            <G Name="ArrowDown" /> Import
          </button>
        </div>
      </header>
      <div className="workspace-body" inert={Open ? "" : undefined}>
        <aside className="library-nav">
          <span className="overline">CONTENT BROWSER</span>
          {[
            "All files",
            "Mechanical",
            "Textures",
            "Materials",
            "Audio",
            "Scenes",
          ].map((T, I) => (
            <div className={I === 1 ? "active" : ""} key={T}>
              <Icon
                Name={
                  [
                    "collection",
                    "asset-folder-models",
                    "asset-folder-textures",
                    "asset-folder-materials",
                    "asset-folder-audio",
                    "asset-folder-scenes",
                  ][I]
                }
              />
              {T}
              <small>{I === 1 ? Library.length + 4 : ""}</small>
            </div>
          ))}
          <p>
            This standalone reference saves imports in this browser. It does not
            write to a native Frontier project.
          </p>
        </aside>
        <main className="library-content">
          <div>
            <span className="overline">CONTENT / MECHANICAL</span>
            <h1>Mechanical</h1>
            <p>Geometry, surfaces & source files</p>
          </div>
          <div className="library-grid">
            {[...Library, ...SampleEntries].map((S, I) => (
              <button
                className="library-tile"
                key={S.Id + I}
                onClick={() => {
                  if (SampleEntries.some((E) => E.Id === S.Id))
                    Load(S.Id, true);
                  else Reopen(S);
                }}
              >
                {S.Thumbnail ? (
                  <img src={S.Thumbnail} alt="" />
                ) : (
                  <Icon Name={S.Icon} Size={90} />
                )}
                <span>{S.Name}</span>
                <small>
                  {S.Kind} {I < Library.length ? "· imported locally" : ""}
                </small>
              </button>
            ))}
            <button
              className="library-tile add-source"
              onClick={() => FilesInput.current.click()}
            >
              <G Name="PlusAdd" Size={34} />
              <span>Choose a source</span>
              <small>Any file type</small>
            </button>
          </div>
          <div className="workspace-bottom">
            <span>FRONTIER CONTENT PIPELINE</span>
            <span>Standalone import reference · WebGL2</span>
          </div>
        </main>
      </div>
      <input
        ref={FilesInput}
        type="file"
        multiple
        hidden
        aria-label="Choose source files"
        onChange={(E) => {
          Load(E.target.files);
          E.target.value = "";
        }}
      />
      <input
        ref={IconInput}
        type="file"
        accept="image/png,image/jpeg,image/webp,image/svg+xml"
        hidden
        aria-label="Choose custom icon"
        onChange={(E) => {
          UploadIcon(E.target.files[0]);
          E.target.value = "";
        }}
      />
      {Open && (
        <div className="dialog-backdrop">
          <section
            className={`import-dialog ${DragOver ? "drag-over" : ""}`}
            role="dialog"
            aria-modal="true"
            aria-labelledby="dialog-title"
            ref={Dialog}
            onDragOver={(E) => {
              E.preventDefault();
              SetDragOver(true);
            }}
            onDragLeave={(E) => {
              if (!E.currentTarget.contains(E.relatedTarget))
                SetDragOver(false);
            }}
            onDrop={(E) => {
              E.preventDefault();
              SetDragOver(false);
              if (E.dataTransfer.files.length) Load(E.dataTransfer.files);
            }}
          >
            <header className="dialog-top">
              <div className="dialog-ident">
                <span className="import-glyph">
                  <G Name="ArrowDown" Size={20} />
                </span>
                <div>
                  <span className="overline">CONTENT PIPELINE</span>
                  <h1 id="dialog-title">Import inspector</h1>
                </div>
                <span className="demo-tag">INTERACTIVE REFERENCE</span>
              </div>
              <button
                className="icon-button close-dialog"
                title="Close import inspector"
                aria-label="Close import inspector"
                disabled={Baking || SdfBaking || Saving}
                onClick={Close}
              >
                <G Name="CloseCross" Size={19} />
              </button>
            </header>
            <div className="source-heading">
              <div className="source-ident">
                <Icon Name={Source?.Icon || "package"} Size={44} />
                <div>
                  <h2>{Name || "Untitled import"}</h2>
                  <span>
                    {Source?.Name || "Reading source…"} <b>·</b>{" "}
                    {Source?.Kind || "Source"} <b>·</b>{" "}
                    {Source?.Files?.length
                      ? Bytes(Source.Bytes)
                      : "Built-in specimen"}
                  </span>
                </div>
              </div>
              <div className="source-actions">
                <span className={`source-status ${Pending ? "pending" : ""}`}>
                  <G Name={Pending ? "MotionActivity" : "CircleCheck"} />
                  {Pending ? "Reading source" : "Ready for review"}
                </span>
                <button
                  onClick={() => FilesInput.current.click()}
                  disabled={Pending}
                >
                  <G Name="FolderOpen" /> Change source
                </button>
              </div>
            </div>
            <nav
              className="step-navigation"
              role="tablist"
              aria-label="Import pages"
              onKeyDown={(E) => {
                if (E.key === "ArrowRight") {
                  E.preventDefault();
                  Go(Page + 1);
                }
                if (E.key === "ArrowLeft") {
                  E.preventDefault();
                  Go(Page - 1);
                }
              }}
            >
              {Steps.map(([Title, IconName, Subtitle], I) => (
                <button
                  role="tab"
                  tabIndex={Page === I ? 0 : -1}
                  aria-selected={Page === I}
                  aria-controls={"import-page-" + I}
                  id={"import-tab-" + I}
                  key={Title}
                  className={Page === I ? "active" : ""}
                  disabled={
                    !Source || (!IsGeometry && [1, 2, 3, 4, 5].includes(I))
                  }
                  onClick={() => Go(I)}
                >
                  <span className="step-number">0{I + 1}</span>
                  <Icon Name={IconName} Size={20} />
                  <span>
                    {Title}
                    <small>{Subtitle}</small>
                  </span>
                  {Page === I && <i />}
                </button>
              ))}
            </nav>
            {Failure && (
              <div className="message error" role="alert">
                <G Name="TriangleAlert" />
                <span>{Failure}</span>
                {RejectedFiles.current && (
                  <button
                    className="keep-source"
                    onClick={() => Load(RejectedFiles.current, false, true)}
                  >
                    Keep source only
                  </button>
                )}
                <button
                  aria-label="Dismiss error"
                  onClick={() => SetFailure("")}
                >
                  <G Name="CloseCross" />
                </button>
              </div>
            )}
            {Notice && (
              <div className="message" role="status">
                <G Name="CircleInfo" />
                <span>{Notice}</span>
                <button
                  aria-label="Dismiss notice"
                  onClick={() => SetNotice("")}
                >
                  <G Name="CloseCross" />
                </button>
              </div>
            )}
            <div className="dialog-body" inert={Saving ? "" : undefined}>
              <section className="preview-pane" aria-label="Source preview">
                <div className="preview-ribbon">
                  <span className="pane-name">
                    {Page === 2
                      ? "UV coordinates"
                      : Page === 3
                        ? "Cluster inspection"
                        : Page === 4
                          ? "Material inspection"
                          : Page === 5
                            ? "Distance volume"
                            : Source?.Kind === "Geometry"
                              ? "Import preview"
                              : (Source?.Kind || "Source") + " preview"}
                  </span>
                  <div>
                    {IsGeometry && Page !== 2 && !(Page === 5 && SdfFresh) && (
                      <>
                        <select
                          aria-label="Preview display"
                          value={Mode}
                          onChange={(E) => SetMode(E.target.value)}
                        >
                          {[
                            "Shaded",
                            "Wireframe",
                            "Normals",
                            "Checker",
                            ...(Fresh && Page === 3 ? ["Clusters"] : []),
                          ].map((M) => (
                            <option key={M}>{M}</option>
                          ))}
                        </select>
                        <button
                          title="Frame geometry"
                          aria-label="Frame geometry"
                          onClick={() => SetFrame((P) => P + 1)}
                        >
                          <G Name="ExpandDiagonal" />
                        </button>
                        <button
                          title="Turntable"
                          aria-label="Turntable"
                          aria-pressed={Settings.Turntable}
                          onClick={() =>
                            Change("Turntable", !Settings.Turntable)
                          }
                        >
                          <G Name="RotateClockwise" />
                        </button>
                      </>
                    )}
                  </div>
                </div>
                <div className="preview-interior">
                  {Pending && (
                    <div className="source-loading">
                      <G Name="MotionActivity" Size={30} />
                      <span>Reading local source…</span>
                    </div>
                  )}
                  {Source &&
                    !Pending &&
                    (IsGeometry ? (
                      <>
                        {Page === 5 && SdfFresh ? (
                          <DistancePreview
                            Volume={Sdf}
                            Settings={Settings}
                            Change={Change}
                          />
                        ) : Page === 2 ? (
                          <UvPreview Geometry={Geometry} Settings={Settings} />
                        ) : (
                          <GeometryPreview
                            Geometry={DisplayGeometry}
                            Settings={Settings}
                            Mode={Mode}
                            Clusters={DisplayClusters}
                            OnCapture={Capture}
                            FrameSignal={Frame}
                            SourceKey={Source}
                          />
                        )}
                        {Page !== 2 && !(Page === 5 && SdfFresh) && (
                          <div className="orientation-compass">
                            <span className="z">Z</span>
                            <span className="y">Y</span>
                            <span className="x">X</span>
                            <i />
                          </div>
                        )}
                      </>
                    ) : (
                      <MediaPreview Source={Source} Settings={Settings} />
                    ))}
                </div>
                <div className="preview-statistics">
                  {IsGeometry ? (
                    Page === 5 && SdfFresh ? (
                      <>
                        <div>
                          <span>FIELD</span>
                          <strong>{Sdf.Resolution}³ voxels</strong>
                        </div>
                        <div>
                          <span>SPACING · m</span>
                          <strong>{Sdf.Spacing.toFixed(4)}</strong>
                        </div>
                        <div>
                          <span>DISTANCE</span>
                          <strong>
                            {Sdf.Signed ? "Signed · parity" : "Unsigned"}
                          </strong>
                        </div>
                      </>
                    ) : (
                      <>
                        <div>
                          <span>TRIANGLES</span>
                          <strong>
                            {NumberText(
                              (DisplayGeometry?.Positions.length || 0) / 9,
                            )}
                          </strong>
                        </div>
                        <div>
                          <span>
                            {Page === 3 && Fresh ? "CLUSTERS" : "EXTENTS · m"}
                          </span>
                          <strong>
                            {Page === 3 && Fresh
                              ? NumberText(
                                  DisplayClusters?.Clusters.length || 0,
                                )
                              : Bounds.Size.map((V) => NumberText(V, 2)).join(
                                  " × ",
                                )}
                          </strong>
                        </div>
                        <div>
                          <span>COORDINATES</span>
                          <strong>
                            {Geometry?.Uvs ? "UV0 available" : "UV0 missing"}
                          </strong>
                        </div>
                      </>
                    )
                  ) : (
                    <>
                      <div>
                        <span>SOURCE</span>
                        <strong>{Source?.Kind || "—"}</strong>
                      </div>
                      <div>
                        <span>ENCODING</span>
                        <strong>Original retained</strong>
                      </div>
                      <div>
                        <span>SIZE</span>
                        <strong>{Bytes(Source?.Bytes)}</strong>
                      </div>
                    </>
                  )}
                </div>
                <div className="preview-footnote">
                  <span>
                    <span className="status-dot" />
                    {IsGeometry
                      ? Page === 5 && SdfFresh
                        ? "CPU triangle-distance field"
                        : "WebGL2 · studio lighting"
                      : "Local browser preview"}
                  </span>
                  <span>
                    {Page === 5 && SdfFresh
                      ? "Voxel-centred distances · metres"
                      : IsGeometry && Page !== 2
                        ? "Drag to orbit · scroll to zoom"
                        : Page === 2
                          ? "UV0 · original attributes"
                          : "Source-dependent decoding"}
                  </span>
                </div>
              </section>
              <aside
                className="import-inspector"
                aria-label="Import configuration"
              >
                <div
                  className="inspector-sliding-track"
                  style={{ transform: `translateX(-${Page * 100}%)` }}
                >
                  {Steps.map(([Title], I) => (
                    <div
                      className="inspector-page"
                      key={I}
                      id={"import-page-" + I}
                      role="tabpanel"
                      aria-labelledby={"import-tab-" + I}
                      inert={I === Page ? undefined : ""}
                    >
                      <div className="inspector-page-heading">
                        <span>
                          0{I + 1} / 0{Steps.length}
                        </span>
                        <h2>
                          {
                            [
                              "Source file",
                              "Geometry",
                              "UV coordinates",
                              "Cluster bake",
                              "Material inspector",
                              "Distance field bake",
                              "Identity & metadata",
                            ][I]
                          }
                        </h2>
                        <p>
                          {
                            [
                              "Choose what comes in. Keep the original intact.",
                              "Orient, scale and prepare the surface.",
                              "Inspect the coordinates before committing.",
                              "Build spatially grouped detail levels.",
                              "Inspect each surface. Shape its response to light.",
                              "Sample the geometry into a distance volume.",
                              "Make it recognisable. Make it searchable.",
                            ][I]
                          }
                        </p>
                      </div>
                      {I === 0 && (
                        <>
                          <Section
                            Title="Import identity"
                            Tag="DESTINATION"
                            Children={
                              <>
                                <Field
                                  Label="Import name"
                                  Children={
                                    <input
                                      aria-label="Import name"
                                      maxLength={120}
                                      value={Name}
                                      onChange={(E) => SetName(E.target.value)}
                                      placeholder="Name this import"
                                    />
                                  }
                                />
                                <Field
                                  Label="Destination folder"
                                  Note="Logical browser-library path, not a disk location."
                                  Children={
                                    <div className="path-input">
                                      <G Name="FolderClosed" />
                                      <input
                                        aria-label="Destination folder"
                                        value={Settings.Destination}
                                        onChange={(E) =>
                                          Change("Destination", E.target.value)
                                        }
                                      />
                                    </div>
                                  }
                                />
                              </>
                            }
                          />
                          <Section
                            Title="Source record"
                            Tag="READ ONLY"
                            Children={
                              <>
                                <Readout
                                  Label="Format"
                                  Value={
                                    Source?.Name.split(".")
                                      .pop()
                                      .toUpperCase() || "—"
                                  }
                                />
                                <Readout
                                  Label="Importer"
                                  Value={
                                    Source?.Kind === "Geometry"
                                      ? "Triangle geometry"
                                      : Source?.Kind === "Binary"
                                        ? "Source preservation"
                                        : "Browser " + (Source?.Kind || "")
                                  }
                                />
                                <Readout
                                  Label="Dependencies"
                                  Value={
                                    Source?.Files.length > 1
                                      ? `${Source.Files.length - 1} companion file(s)`
                                      : "Self-contained / specimen"
                                  }
                                />
                                {IsGeometry && (
                                  <Readout
                                    Label="Submeshes"
                                    Value={Source.Geometry.Parts}
                                  />
                                )}
                                <Note
                                  Children={Source?.Notice || "Reading source…"}
                                />
                              </>
                            }
                          />
                          {!IsGeometry && (
                            <Section
                              Title={Source?.Kind + " settings"}
                              Tag="TYPE ADAPTIVE"
                              Children={
                                <>
                                  {Source?.Kind === "Image" ? (
                                    <>
                                      <Select
                                        Label="Preview channel"
                                        Value={Settings.Channel}
                                        Options={["RGB", "Red", "Grayscale"]}
                                        Change={(V) => Change("Channel", V)}
                                      />
                                      <Select
                                        Label="Colour interpretation"
                                        Value={Settings.ColourSpace}
                                        Options={["sRGB", "Linear data"]}
                                        Change={(V) => Change("ColourSpace", V)}
                                      />
                                      <Select
                                        Label="Sampling"
                                        Value={Settings.Filter}
                                        Options={["Linear", "Nearest"]}
                                        Change={(V) => Change("Filter", V)}
                                      />
                                      <Note Children="Colour interpretation is stored as import intent. This reference does not transcode image pixels." />
                                    </>
                                  ) : ["Audio", "Video"].includes(
                                      Source?.Kind,
                                    ) ? (
                                    <>
                                      <Toggle
                                        Name="Loop preview"
                                        On={Settings.Loop}
                                        Change={(V) => Change("Loop", V)}
                                        Symbol="cycle"
                                      />
                                      <Readout
                                        Label="Transcoding"
                                        Value="Original stream retained"
                                      />
                                    </>
                                  ) : Source?.Kind === "Document" ? (
                                    <>
                                      <Readout
                                        Label="Text encoding"
                                        Value="UTF-8 preview"
                                      />
                                      <Readout
                                        Label="Processing"
                                        Value="No scripts executed"
                                      />
                                    </>
                                  ) : (
                                    <Note Children="Format-specific native conversion is not connected. The source, identity and icon remain available." />
                                  )}
                                </>
                              }
                            />
                          )}
                          <Section
                            Title="Try another source"
                            Tag="BUILT-IN SPECIMENS"
                            Children={
                              <>
                                <div className="sample-switcher">
                                  {SampleEntries.map((S) => (
                                    <button
                                      key={S.Id}
                                      onClick={() => Load(S.Id, true)}
                                      disabled={Pending}
                                    >
                                      <Icon Name={S.Icon} Size={27} />
                                      <span>{S.Kind}</span>
                                    </button>
                                  ))}
                                </div>
                                <button
                                  className="drop-source"
                                  onClick={() => FilesInput.current.click()}
                                  disabled={Pending}
                                >
                                  <G Name="PlusAdd" />
                                  <span>
                                    Choose files or drop them here
                                    <small>
                                      Any file · one primary + companions · up
                                      to 128 MB
                                    </small>
                                  </span>
                                </button>
                              </>
                            }
                          />
                        </>
                      )}
                      {I === 1 && (
                        <>
                          <Section
                            Title="Coordinate system"
                            Tag="WORLD · Z UP"
                            Children={
                              <>
                                <div className="coordinate-graphic">
                                  <svg
                                    viewBox="0 0 300 82"
                                    role="img"
                                    aria-label="Source coordinate conversion"
                                  >
                                    <path
                                      d="M43 57V14M43 57L77 67M43 57L20 74"
                                      stroke="#ffffff25"
                                      fill="none"
                                    />
                                    <path d="M43 57V14" stroke="#8baec9" />
                                    <text x="40" y="10" fill="#8baec9">
                                      {Settings.Up}
                                    </text>
                                    <path
                                      d="M115 43H181m-7-6 7 6-7 6"
                                      stroke="#969b94"
                                      fill="none"
                                    />
                                    <path
                                      d="M238 57V14M238 57L272 67M238 57L215 74"
                                      stroke="#ffffff25"
                                    />
                                    <path d="M238 57V14" stroke="#8baec9" />
                                    <text x="234" y="10" fill="#8baec9">
                                      Z
                                    </text>
                                    <text x="102" y="72" fill="#969b94">
                                      SOURCE → FRONTIER
                                    </text>
                                  </svg>
                                </div>
                                <div className="two-cols">
                                  <Select
                                    Label="Source up axis"
                                    Value={Settings.Up}
                                    Options={["Y", "Z", "X"]}
                                    Change={(V) => Change("Up", V)}
                                  />
                                  <Select
                                    Label="Source units"
                                    Value={Settings.Units}
                                    Options={["m", "cm", "mm"]}
                                    Change={(V) => Change("Units", V)}
                                  />
                                </div>
                              </>
                            }
                          />
                          <Section
                            Title="Transform"
                            Tag="NON-DESTRUCTIVE"
                            Children={
                              <>
                                <Triple
                                  Label="Translation"
                                  Values={Settings.Translation}
                                  Unit="m"
                                  Change={(V) => Change("Translation", V)}
                                />
                                <Triple
                                  Label="Rotation"
                                  Values={Settings.Rotation}
                                  Unit="degrees"
                                  Min={-360}
                                  Max={360}
                                  Change={(V) => Change("Rotation", V)}
                                />
                                <div className="scale-label">
                                  <span>Scale</span>
                                  <button
                                    aria-pressed={Settings.Uniform}
                                    onClick={() =>
                                      Change("Uniform", !Settings.Uniform)
                                    }
                                  >
                                    <G
                                      Name={
                                        Settings.Uniform
                                          ? "LockClosed"
                                          : "LockOpen"
                                      }
                                      Size={12}
                                    />
                                    {Settings.Uniform ? "Linked" : "Per-axis"}
                                  </button>
                                </div>
                                <Triple
                                  Label="Scale"
                                  Values={Settings.Scale}
                                  Unit="×"
                                  Min={0.001}
                                  Max={1000}
                                  Change={(V, A) =>
                                    Change(
                                      "Scale",
                                      Settings.Uniform ? [V[A], V[A], V[A]] : V,
                                    )
                                  }
                                />
                                <button
                                  className="text-button"
                                  onClick={() =>
                                    SetSettings((P) => ({
                                      ...P,
                                      Translation: [0, 0, 0],
                                      Rotation: [0, 0, 0],
                                      Scale: [1, 1, 1],
                                    }))
                                  }
                                >
                                  <G Name="RotateCounterClockwise" />
                                  Reset transform
                                </button>
                              </>
                            }
                          />
                          <Section
                            Title="Origin & surface"
                            Children={
                              <>
                                <Select
                                  Label="Pivot placement"
                                  Value={Settings.Pivot}
                                  Options={[
                                    "Center",
                                    "Bottom",
                                    "Origin",
                                    "Custom",
                                  ]}
                                  Change={(V) => Change("Pivot", V)}
                                />
                                {Settings.Pivot === "Custom" && (
                                  <Triple
                                    Label="Pivot"
                                    Values={Settings.PivotXYZ}
                                    Unit="m"
                                    Change={(V) => Change("PivotXYZ", V)}
                                  />
                                )}
                                <Select
                                  Label="Normals"
                                  Value={Settings.Normals}
                                  Options={["Source", "Smooth", "Flat"]}
                                  Change={(V) => Change("Normals", V)}
                                />
                                <div className="toggle-pair">
                                  <Toggle
                                    Name="Flip winding"
                                    On={Settings.Flip}
                                    Change={(V) => Change("Flip", V)}
                                    Symbol="orbit"
                                  />
                                  <Toggle
                                    Name="Show normals"
                                    On={Settings.ShowNormals}
                                    Change={(V) => Change("ShowNormals", V)}
                                    Symbol="diagnostics"
                                  />
                                </div>
                                <div className="toggle-pair">
                                  <Toggle
                                    Name="Show pivot"
                                    On={Settings.ShowPivot}
                                    Change={(V) => Change("ShowPivot", V)}
                                    Symbol="focus"
                                  />
                                  <Toggle
                                    Name="Bounds"
                                    On={Settings.ShowBounds}
                                    Change={(V) => Change("ShowBounds", V)}
                                    Symbol="split"
                                  />
                                </div>
                              </>
                            }
                          />
                        </>
                      )}
                      {I === 2 && (
                        <>
                          <Section
                            Title="Primary coordinates"
                            Tag="UV0"
                            Children={
                              <>
                                <div className="uv-record">
                                  <Icon Name="editor-texture" Size={42} />
                                  <div>
                                    <strong>
                                      {Geometry?.Uvs
                                        ? "Coordinates found"
                                        : "No UV coordinates"}
                                    </strong>
                                    <span>
                                      Primary channel · source topology
                                    </span>
                                  </div>
                                  <G
                                    Name={
                                      Geometry?.Uvs
                                        ? "CircleCheck"
                                        : "TriangleAlert"
                                    }
                                  />
                                </div>
                                <Readout
                                  Label="UV triangles"
                                  Value={NumberText(Uvs.Triangles)}
                                />
                                <Readout
                                  Label="Outside primary tile"
                                  Value={NumberText(Uvs.Outside)}
                                />
                                <Readout
                                  Label="Degenerate UV triangles"
                                  Value={NumberText(Uvs.Degenerate)}
                                />
                                <Readout
                                  Label="Triangle-sum area"
                                  Value={NumberText(Uvs.Area, 3) + " tiles"}
                                />
                                <Note Children="Area includes overlaps. This is not an occupancy or overlap test; multiple charts may share coordinates." />
                              </>
                            }
                          />
                          <Section
                            Title="Coordinate adjustment"
                            Tag="PRESERVE SOURCE"
                            Children={
                              <>
                                <Scalar
                                  Label="UV scale"
                                  Value={Settings.UvScale}
                                  Min={0.1}
                                  Max={8}
                                  Step={0.01}
                                  Unit="×"
                                  Change={(V) => Change("UvScale", V)}
                                />
                                <div className="two-cols">
                                  {["U", "V"].map((T, K) => (
                                    <Field
                                      key={T}
                                      Label={T + " offset"}
                                      Children={
                                        <input
                                          aria-label={T + " offset"}
                                          type="number"
                                          min={-16}
                                          max={16}
                                          step=".01"
                                          value={Settings.UvOffset[K]}
                                          onChange={(E) => {
                                            const Next = [...Settings.UvOffset];
                                            Next[K] = Math.min(
                                              16,
                                              Math.max(
                                                -16,
                                                +E.target.value || 0,
                                              ),
                                            );
                                            Change("UvOffset", Next);
                                          }}
                                        />
                                      }
                                    />
                                  ))}
                                </div>
                                <Toggle
                                  Name="Flip V"
                                  On={Settings.FlipV}
                                  Change={(V) => Change("FlipV", V)}
                                  Symbol="orbit"
                                />
                                <button
                                  className="text-button"
                                  onClick={() =>
                                    SetSettings((P) => ({
                                      ...P,
                                      UvScale: 1,
                                      UvOffset: [0, 0],
                                      FlipV: false,
                                    }))
                                  }
                                >
                                  <G Name="RotateCounterClockwise" />
                                  Reset coordinates
                                </button>
                              </>
                            }
                          />
                          <Section
                            Title="Surface inspection"
                            Children={
                              <>
                                <button
                                  className="wide-button"
                                  onClick={() => {
                                    Go(1);
                                    SetMode("Checker");
                                  }}
                                >
                                  <Icon Name="editor-texture" />
                                  View checker on geometry
                                  <G Name="ArrowRight" />
                                </button>
                                <Note Children="UVs are inspected and transformed here. Chart unwrapping, overlap resolution and texture baking require the native pipeline." />
                              </>
                            }
                          />
                        </>
                      )}
                      {I === 3 && (
                        <>
                          <Section
                            Title="Spatial detail hierarchy"
                            Tag="LOCAL BAKE"
                            Children={
                              <>
                                <div className="hierarchy-graphic">
                                  <svg
                                    viewBox="0 0 320 112"
                                    role="img"
                                    aria-label="Detail hierarchy schematic"
                                  >
                                    {[0, 1, 2].map((R) =>
                                      Array.from({ length: 2 ** R }, (_, J) => {
                                        const X = ((J + 0.5) * 320) / 2 ** R,
                                          Y = R * 35 + 12;
                                        return (
                                          <g key={R + "-" + J}>
                                            {R > 0 && (
                                              <path
                                                d={`M${((Math.floor(J / 2) + 0.5) * 320) / 2 ** (R - 1)} ${Y - 25}L${X} ${Y - 5}`}
                                                fill="none"
                                                stroke="#ffffff21"
                                              />
                                            )}
                                            <rect
                                              x={X - 18}
                                              y={Y}
                                              width="36"
                                              height="12"
                                              rx="3"
                                              fill={
                                                [
                                                  "#8baec9",
                                                  "#9caf9a",
                                                  "#c5b38a",
                                                ][R]
                                              }
                                              opacity={1 - R * 0.15}
                                            />
                                          </g>
                                        );
                                      }),
                                    )}
                                  </svg>
                                  <span>
                                    Finer triangles → bounded groups → coarse
                                    levels
                                  </span>
                                </div>
                                <Scalar
                                  Label="Detail levels"
                                  Value={Settings.Levels}
                                  Min={2}
                                  Max={6}
                                  Step={1}
                                  Change={(V) => Change("Levels", V)}
                                />
                                <Scalar
                                  Label="Triangle ratio per level"
                                  Value={Settings.Ratio}
                                  Min={0.2}
                                  Max={0.8}
                                  Step={0.05}
                                  Unit="×"
                                  Change={(V) => Change("Ratio", V)}
                                />
                                <Select
                                  Label="Cluster capacity"
                                  Value={String(Settings.Capacity)}
                                  Options={["32", "64", "128", "256"]}
                                  Change={(V) => Change("Capacity", +V)}
                                />
                                <Toggle
                                  Name="UV-aware reduction"
                                  On={Settings.UvAware}
                                  Change={(V) => Change("UvAware", V)}
                                  Symbol="image"
                                />
                                <div className="bake-buttons">
                                  <button
                                    className="primary bake-button"
                                    disabled={Baking || SdfBaking || !Geometry}
                                    onClick={StartBake}
                                  >
                                    <ActionIcon Name="bake" Size={17} />
                                    {Baking
                                      ? "Building…"
                                      : Fresh
                                        ? "Rebuild detail levels"
                                        : "Bake detail levels"}
                                  </button>
                                  {Baking && (
                                    <button
                                      onClick={() => {
                                        BakeToken.current++;
                                        SetBaking(false);
                                        SetProgress(0);
                                      }}
                                    >
                                      Cancel
                                    </button>
                                  )}
                                </div>
                                {Baking && (
                                  <div
                                    className="bake-progress"
                                    role="progressbar"
                                    aria-label="Cluster bake progress"
                                    aria-valuenow={Math.round(Progress * 100)}
                                    aria-valuemin="0"
                                    aria-valuemax="100"
                                  >
                                    <i
                                      style={{ width: `${Progress * 100}%` }}
                                    />
                                  </div>
                                )}
                                {Bake && !Fresh && (
                                  <Note
                                    Warning
                                    Children="Settings changed. Rebuild before inspecting or saving these detail levels."
                                  />
                                )}
                              </>
                            }
                          />
                          {Fresh && (
                            <Section
                              Title="Baked levels"
                              Tag={NumberText(Bake.Milliseconds, 0) + " ms"}
                              Children={
                                <>
                                  <div className="lod-table">
                                    <div>
                                      <span>Level</span>
                                      <span>Triangles</span>
                                      <span>Clusters</span>
                                      <span>Retained</span>
                                    </div>
                                    {Bake.Levels.map((L, J) => (
                                      <button
                                        key={J}
                                        aria-pressed={Level === J}
                                        onClick={() => {
                                          SetLevel(J);
                                          SetMode("Clusters");
                                        }}
                                      >
                                        <span>LOD {J}</span>
                                        <b>{NumberText(L.Triangles)}</b>
                                        <span>{L.Clusters.length}</span>
                                        <span>
                                          {NumberText(
                                            (L.Triangles /
                                              Bake.Levels[0].Triangles) *
                                              100,
                                          )}
                                          %
                                        </span>
                                      </button>
                                    ))}
                                  </div>
                                  <Readout
                                    Label="Max vertex displacement"
                                    Value={
                                      NumberText(
                                        Bake.Levels[Level]?.Displacement || 0,
                                        5,
                                      ) + " m"
                                    }
                                  />
                                  <button
                                    className="text-button"
                                    onClick={() =>
                                      DownloadJson(Name + ".clusters.json", {
                                        Method:
                                          "Morton-ordered triangle groups + spatial vertex reduction",
                                        Materials: Settings.Materials,
                                        Levels: Bake.Levels,
                                      })
                                    }
                                  >
                                    <G Name="ArrowDown" />
                                    Export baked geometry
                                  </button>
                                </>
                              }
                            />
                          )}
                          <Note Children="Nanite-inspired inspection, not Nanite. This demo computes vertex-cluster reductions and Morton-ordered triangle groups. No certified surface error, crack-free DAG, streaming or runtime selection is claimed. UV-aware grouping is a heuristic, not seam locking." />
                        </>
                      )}
                      {I === 4 && (
                        <MaterialInspector
                          Geometry={Geometry}
                          Settings={Settings}
                          Change={Change}
                          Controls={ImportControls}
                          Notify={SetNotice}
                        />
                      )}
                      {I === 5 && (
                        <DistanceInspector
                          Settings={Settings}
                          Change={Change}
                          Controls={ImportControls}
                          Volume={Sdf}
                          Fresh={SdfFresh}
                          Busy={SdfBaking}
                          Progress={SdfProgress}
                          Start={StartSdf}
                          Cancel={() => {
                            SdfToken.current++;
                            SetSdfBaking(false);
                            SetSdfProgress(0);
                          }}
                          Download={() =>
                            DownloadJson(Name + ".sdf.json", {
                              Version: 1,
                              ...Sdf,
                            })
                          }
                        />
                      )}
                      {I === 6 && (
                        <>
                          <Section
                            Title="Library icon"
                            Tag="IDENTITY"
                            Children={
                              <>
                                <div className="icon-editor">
                                  <div className="icon-preview">
                                    {IconPreview}
                                  </div>
                                  <div>
                                    <strong>{Name || "Untitled import"}</strong>
                                    <span>Thumbnail preview</span>
                                    <button
                                      onClick={() => IconInput.current.click()}
                                    >
                                      <G Name="ArrowUp" />
                                      Upload icon
                                    </button>
                                    {IsGeometry && (
                                      <button onClick={CaptureIcon}>
                                        <G Name="CameraBody" />
                                        Capture current view
                                      </button>
                                    )}
                                  </div>
                                </div>
                                <div
                                  className="icon-choices"
                                  aria-label="Import icon choices"
                                >
                                  {[
                                    Source?.Icon || "package",
                                    "mesh",
                                    "editor-prefab",
                                    "editor-material",
                                    "editor-texture",
                                    "package",
                                    "slate-document",
                                    "slate-animation-curve",
                                  ]
                                    .filter((V, J, L) => L.indexOf(V) === J)
                                    .map((K) => (
                                      <button
                                        key={K}
                                        title={K.replaceAll("-", " ")}
                                        aria-label={"Use " + K + " icon"}
                                        aria-pressed={
                                          !Settings.IconImage &&
                                          Settings.Icon === K
                                        }
                                        onClick={() =>
                                          SetSettings((P) => ({
                                            ...P,
                                            Icon: K,
                                            IconImage: "",
                                          }))
                                        }
                                      >
                                        <Icon Name={K} Size={27} />
                                      </button>
                                    ))}
                                </div>
                                <small className="muted">
                                  PNG, JPEG, WebP or SVG · maximum 2 MB
                                </small>
                              </>
                            }
                          />
                          <Section
                            Title="Description"
                            Tag="SEARCHABLE"
                            Children={
                              <>
                                <Field
                                  Label="Author / studio"
                                  Children={
                                    <input
                                      aria-label="Author / studio"
                                      value={Settings.Author}
                                      onChange={(E) =>
                                        Change("Author", E.target.value)
                                      }
                                    />
                                  }
                                />
                                <Select
                                  Label="License"
                                  Value={Settings.License}
                                  Options={[
                                    "Unspecified",
                                    "Proprietary",
                                    "CC0",
                                    "CC-BY-4.0",
                                    "Custom",
                                  ]}
                                  Change={(V) => Change("License", V)}
                                />
                                <Field
                                  Label="Tags"
                                  Note="Separate tags with commas."
                                  Children={
                                    <input
                                      aria-label="Tags"
                                      value={Settings.Tags}
                                      onChange={(E) =>
                                        Change("Tags", E.target.value)
                                      }
                                    />
                                  }
                                />
                                <div className="tag-preview">
                                  {Settings.Tags.split(",")
                                    .map((T) => T.trim())
                                    .filter(Boolean)
                                    .map((T, J) => (
                                      <span key={J}>{T}</span>
                                    ))}
                                </div>
                                <Field
                                  Label="Notes"
                                  Children={
                                    <textarea
                                      aria-label="Notes"
                                      rows={3}
                                      value={Settings.Description}
                                      onChange={(E) =>
                                        Change("Description", E.target.value)
                                      }
                                      placeholder="Purpose, provenance or import notes…"
                                    />
                                  }
                                />
                              </>
                            }
                          />
                          <Section
                            Title="Custom properties"
                            Tag={Settings.Custom.length + " FIELDS"}
                            Children={
                              <>
                                {Settings.Custom.map((Pair, J) => (
                                  <div className="custom-property" key={J}>
                                    <input
                                      aria-label={
                                        "Property " + (J + 1) + " key"
                                      }
                                      placeholder="Key"
                                      value={Pair.Key}
                                      onChange={(E) =>
                                        Change(
                                          "Custom",
                                          Settings.Custom.map((P, K) =>
                                            K === J
                                              ? { ...P, Key: E.target.value }
                                              : P,
                                          ),
                                        )
                                      }
                                    />
                                    <input
                                      aria-label={
                                        "Property " + (J + 1) + " value"
                                      }
                                      placeholder="Value"
                                      value={Pair.Value}
                                      onChange={(E) =>
                                        Change(
                                          "Custom",
                                          Settings.Custom.map((P, K) =>
                                            K === J
                                              ? { ...P, Value: E.target.value }
                                              : P,
                                          ),
                                        )
                                      }
                                    />
                                    <button
                                      aria-label={"Remove property " + (J + 1)}
                                      onClick={() =>
                                        Change(
                                          "Custom",
                                          Settings.Custom.filter(
                                            (_, K) => K !== J,
                                          ),
                                        )
                                      }
                                    >
                                      <G Name="TrashDelete" Size={14} />
                                    </button>
                                  </div>
                                ))}
                                <button
                                  className="text-button"
                                  onClick={() =>
                                    Change("Custom", [
                                      ...Settings.Custom,
                                      { Key: "", Value: "" },
                                    ])
                                  }
                                >
                                  <G Name="PlusAdd" />
                                  Add property
                                </button>
                              </>
                            }
                          />
                        </>
                      )}
                    </div>
                  ))}
                </div>
              </aside>
            </div>
            <footer className="dialog-footer">
              <div className="footer-context">
                <G Name="ShieldInput" />
                <span>
                  Original source preserved
                  <small>Import recipe + files saved in this browser</small>
                </span>
              </div>
              <div className="footer-actions">
                <button
                  onClick={Close}
                  disabled={Baking || SdfBaking || Saving}
                >
                  Cancel
                </button>
                <button
                  className="recipe-export"
                  disabled={!Source || Pending}
                  onClick={() =>
                    DownloadJson(
                      (Name || "import") + ".import.json",
                      Manifest(),
                    )
                  }
                >
                  <G Name="ArrowDown" />
                  Export settings
                </button>
                <div className="page-controls">
                  <button
                    aria-label="Previous import page"
                    disabled={Page === 0}
                    onClick={() => Go(!IsGeometry ? 0 : Page - 1)}
                  >
                    <G Name="ChevronLeft" />
                  </button>
                  <span>
                    0{Page + 1} <small>/ 0{Steps.length}</small>
                  </span>
                  <button
                    aria-label="Next import page"
                    disabled={Page === Steps.length - 1}
                    onClick={() =>
                      Go(!IsGeometry ? Steps.length - 1 : Page + 1)
                    }
                  >
                    <G Name="ChevronRight" />
                  </button>
                </div>
                <button
                  className="primary import-confirm"
                  disabled={
                    !Source ||
                    !Name.trim() ||
                    Pending ||
                    Baking ||
                    SdfBaking ||
                    Saving
                  }
                  onClick={Submit}
                >
                  <G Name="ArrowDown" />
                  {Saving ? "Saving…" : "Import to library"}
                </button>
              </div>
            </footer>
            {DragOver && (
              <div className="drop-overlay">
                <G Name="ArrowDown" Size={38} />
                <h2>Drop a source file</h2>
                <p>Include companion files for multi-file formats.</p>
              </div>
            )}
            {Complete && (
              <div className="completion-overlay">
                <div>
                  <span className="completion-check">
                    <G Name="CircleCheck" Size={32} />
                  </span>
                  <span className="overline">IMPORT COMPLETE</span>
                  <h2>{Name}</h2>
                  <p>
                    Source, settings{Fresh ? " and baked detail levels" : ""}{" "}
                    saved in this browser’s local library.
                    <br />
                    No native engine import was performed.
                  </p>
                  <div>
                    <button onClick={() => SetComplete(false)}>
                      Continue editing
                    </button>
                    <button
                      className="primary"
                      onClick={() => {
                        SetComplete(false);
                        SetOpen(false);
                      }}
                    >
                      Back to library
                      <G Name="ArrowRight" />
                    </button>
                  </div>
                </div>
              </div>
            )}
          </section>
        </div>
      )}
    </div>
  );
}
createRoot(document.getElementById("ImportDemo")).render(<App />);
