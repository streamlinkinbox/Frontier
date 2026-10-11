import { QualityGlyph } from "./FractureProjection.js";
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import {
  Materials,
  CreateGeometry,
  Bounds,
  Volume,
  Validate,
  RenderGeometry,
  Fracture,
  EncodeParts,
  DecodeParts,
} from "./FractureStructure.js";
import {
  Defaults,
  MaterialNames,
  Prefix,
  Normalize,
  Signature,
  ReadRecord,
  Publish,
} from "./FractureSpecification.js";
import { ReadFragments, WriteFragments } from "./FractureDepot.js";
const ById = (Id) => document.getElementById(Id),
  Query = new URLSearchParams(location.search),
  Linked = Query.has("object"),
  Id = Query.get("object") || "standalone-specimen";
const Initial = ReadRecord(Id);
let Owner = Initial?.Owner || {
    Id,
    Name: Linked ? "Missing scene object" : "Sphere specimen",
    Primitive: Linked ? "unknown" : "sphere",
    Scale: [1, 1, 1],
  },
  Settings = Normalize(Initial?.Settings || { ...Defaults, Enabled: !Linked }),
  Root = null,
  Result = null,
  Receipt = null,
  Busy = false,
  DisplayingFragments = false,
  Separation = 0.18,
  Wire = false,
  Sequence = 0;
let Scene, Camera, Renderer, Controls, Assembly, Impact, Floor;
const Bodies = [],
  Wires = [];
function Status(Text) {
  ById("status").textContent = Text;
}
function Persist() {
  Settings.EditedAt = Date.now();
  try {
    Publish(Owner, Settings);
    ById("storage-label").textContent = "Browser · per object";
  } catch {
    ById("storage-label").textContent = "UNSAVED";
    Status(
      "Browser storage is unavailable or full. Export your work before closing.",
    );
  }
}
function Ready() {
  return !!Receipt && Receipt.Signature === Signature(Owner, Settings);
}
function BusyControls() {
  for (const Name of [
    "fracture",
    "bake",
    "export",
    "clear-bake",
    "source",
    "fragments",
  ])
    ById(Name).disabled = Busy;
  ById("fracture").disabled =
    Busy ||
    !Root ||
    !Settings.Enabled ||
    (Settings.Mode === "baked" && !Ready());
  ById("bake").disabled = Busy || !Root || !Settings.Enabled;
  ById("clear-bake").disabled = Busy || !Receipt;
  ById("fragments").disabled = Busy || !Result;
  document.body.classList.toggle("busy", Busy);
}
function Assign(Key, NumberValue) {
  Settings = Normalize({ ...Settings, [Key]: NumberValue });
  Sequence++;
  Persist();
  if (!["PieceSdf", "SdfResolution"].includes(Key)) {
    Result = null;
    DisplaySource();
  }
  Refresh();
}
function Slider(Target, Key, Label, Minimum, Maximum, Step, Unit) {
  const Field = document.createElement("label");
  Field.className = "field";
  Field.innerHTML =
    '<span></span><div class="slider-pill"><div class="split-value"><input type="number"><small></small></div><input type="range"></div>';
  Field.querySelector("span").textContent = Label;
  Field.querySelector("small").textContent = Unit;
  for (const Input of Field.querySelectorAll("input")) {
    Input.min = Minimum;
    Input.max = Maximum;
    Input.step = Step;
    Input.dataset.setting = Key;
    Input.setAttribute(
      "aria-label",
      Label + (Input.type === "number" ? " value" : ""),
    );
    Input.addEventListener("input", () => Assign(Key, Number(Input.value)));
  }
  ById(Target).append(Field);
}
Slider("impact-controls", "Energy", "Impact energy", 0, 50000, 10, "J");
Slider("impact-controls", "Seed", "Pattern seed", 1, 999999, 1, "#");
Slider("quality-controls", "Ceiling", "Fragment ceiling", 2, 160, 1, "pcs");
Slider(
  "quality-controls",
  "MinimumSize",
  "Minimum span",
  0.002,
  0.3,
  0.001,
  "m",
);
for (const Axis of ["X", "Y", "Z"]) {
  const Label = document.createElement("label");
  Label.textContent = Axis;
  const Input = document.createElement("input");
  Input.type = "number";
  Input.step = ".01";
  Input.dataset.setting = Axis;
  Input.setAttribute("aria-label", "Impact " + Axis);
  Input.addEventListener("input", () => Assign(Axis, Number(Input.value)));
  Label.append(Input);
  ById("impact-coordinates").append(Label);
}
for (const [Key, Name] of Object.entries(MaterialNames)) {
  const Button = document.createElement("button");
  Button.dataset.material = Key;
  Button.innerHTML = "<i></i><span></span>";
  Button.querySelector("span").textContent = Name;
  Button.querySelector("i").style.background =
    "#" + Materials[Key].color.toString(16).padStart(6, "0");
  Button.addEventListener("click", () => Assign("Material", Key));
  ById("material-options").append(Button);
}
const Descriptions = {
  concrete: "Energy-limited bulk cuts. Reinforcement is not represented.",
  rock: "Brittle stone fracture with its own crack resistance; no surface-noise dressing.",
  wood: "Local X grain biases longitudinal splits; cross-grain cuts cost more energy.",
  glass:
    "Brittle response. Thin panes split through their thickness with no discarded crack cells.",
  tempered:
    "Stored elastic energy supports finer fracture, bounded by the quality controls.",
  plastic:
    "High crack resistance limits fragmentation. Plastic deformation is not simulated.",
};
function Refresh() {
  document.querySelector(".target-selected svg").innerHTML =
    Owner.Primitive === "sphere"
      ? '<circle cx="30" cy="30" r="23"/><ellipse cx="30" cy="30" rx="10" ry="23"/><ellipse cx="30" cy="30" rx="23" ry="9"/>'
      : Owner.Primitive === "cylinder"
        ? '<ellipse cx="30" cy="13" rx="21" ry="8"/><path d="M9 13v33c0 11 42 11 42 0V13M9 46c0-11 42-11 42 0"/>'
        : Owner.Primitive === "cone"
          ? '<path d="M9 46 30 6l21 40"/><ellipse cx="30" cy="46" rx="21" ry="8"/>'
          : '<path d="m30 5 23 13v26L30 56 7 44V18Z M7 18l23 13 23-13M30 31v25M30 5v26"/>';
  ById("owner-name").textContent = Owner.Name;
  ById("owner-primitive").textContent = Owner.Primitive;
  ById("owner-id").textContent = Owner.Id;
  ById("breadcrumb").textContent = Owner.Name + " / Fracture";
  ById("viewport-name").textContent = Owner.Name;
  document.title = Owner.Name + " · Fracture";
  ById("specimen-controls").hidden = Linked;
  ById("primitive").value = Owner.Primitive;
  ById("object-scale").textContent = Owner.Scale.map((Coordinate) =>
    Coordinate.toFixed(2),
  ).join(" × ");
  ById("source-label").textContent =
    Owner.Primitive === "pane"
      ? "1.7 × 1.15 m · 14 mm plate"
      : "Analytical " + Owner.Primitive;
  for (const Input of document.querySelectorAll("[data-setting]")) {
    Input.value = Settings[Input.dataset.setting];
    Input.disabled = Busy || !Settings.Enabled;
    if (Input.type === "range")
      Input.style.setProperty(
        "--fill",
        (100 * (Number(Input.value) - Number(Input.min))) /
          (Number(Input.max) - Number(Input.min)) +
          "%",
      );
  }
  for (const Button of document.querySelectorAll("[data-material]")) {
    Button.setAttribute(
      "aria-pressed",
      Button.dataset.material === Settings.Material,
    );
    Button.disabled = Busy || !Settings.Enabled;
  }
  ById("material-description").textContent = Descriptions[Settings.Material];
  ById("toughness").textContent =
    Materials[Settings.Material].Gc.toLocaleString();
  ById("density").textContent =
    Materials[Settings.Material].density.toLocaleString();
  for (const Mode of ["dynamic", "baked"]) {
    ById(Mode + "-mode").setAttribute("aria-pressed", Settings.Mode === Mode);
    ById(Mode + "-mode").disabled = Busy || !Settings.Enabled;
  }
  ById("execution-label").textContent =
    (Settings.Enabled ? Settings.Mode.toUpperCase() : "DISABLED") +
    " / GEOMETRY";
  ById("mode-description").textContent =
    Settings.Mode === "dynamic"
      ? "Generate geometry on demand from this object’s recipe."
      : "Use the stored fragment geometry—no fracture generation during replay.";
  ById("sdf-authoring").hidden = Settings.Mode !== "baked";
  ById("piece-sdf").checked = Settings.PieceSdf;
  ById("piece-sdf").disabled = Busy || !Settings.Enabled;
  ById("sdf-resolution").value = Settings.SdfResolution;
  ById("sdf-resolution").disabled = Busy || !Settings.Enabled;
  ById("sdf-resolution-field").hidden = !Settings.PieceSdf;
  ById("sdf-pending").hidden = !Settings.PieceSdf;
  ById("fracture").textContent =
    Settings.Mode === "dynamic" ? "Fracture object" : "Show baked fracture";
  ById("bake-status").textContent = Ready()
    ? Settings.PieceSdf
      ? "Geometry ready · SDF pending"
      : "Baked · ready"
    : Receipt
      ? "Bake is stale"
      : "Not baked";
  ById("bake-detail").textContent = Receipt
    ? Receipt.Parts.length +
      " closed pieces · " +
      (Receipt.Bytes / 1024).toFixed(1) +
      " KiB" +
      (Ready() ? " · per object" : " · geometry / recipe changed")
    : "No stored fragment geometry";
  ById("bake-dot").style.background = Ready()
    ? "#89a591"
    : Receipt
      ? "#aa795a"
      : "#555";
  ById("bake").textContent = Settings.PieceSdf
    ? "Bake geometry"
    : Receipt
      ? "Rebake object"
      : "Bake object";
  if (Root) {
    const Extent = Bounds(Root),
      Metrics = Result || Validate(Root);
    ById("dimensions").textContent =
      Extent.size
        .toArray()
        .map((Coordinate) => Coordinate.toFixed(3))
        .join(" × ") + " m";
    ById("source-volume").textContent = Volume(Root).toPrecision(5) + " m³";
    ById("metric-pieces").textContent = Result ? Result.parts.length : 1;
    ById("metric-volume").innerHTML =
      ((1 - (Result?.volumeError || 0)) * 100).toFixed(3) + "<small>%</small>";
    ById("metric-quality").textContent = Metrics.quality.toFixed(3);
    ById("metric-closure").textContent = "Closed";
    ById("receipt-triangles").textContent = Metrics.triangles.toLocaleString();
    ById("receipt-rejected").textContent = Result?.rejected ?? "—";
    ById("receipt-error").textContent = Result
      ? Result.volumeError.toExponential(2)
      : "—";
    ById("receipt-time").textContent = Result
      ? Result.replay
        ? "Baked replay"
        : Result.milliseconds.toFixed(1) + " ms"
      : "—";
  }
  const Illustration = ById("quality-illustration");
  const Sizing = Settings.Ceiling + ":" + Settings.MinimumSize;
  if (Illustration.dataset.sizing !== Sizing) {
    Illustration.innerHTML = QualityGlyph(
      Settings.Ceiling,
      Settings.MinimumSize,
    );
    Illustration.dataset.sizing = Sizing;
  }
  ById("viewport-caption").textContent = Owner.Removed
    ? "Object removed from the scene"
    : !Settings.Enabled
      ? "Fracture disabled in the object inspector"
      : DisplayingFragments
        ? Result?.parts.length +
          " closed fragments · " +
          MaterialNames[Settings.Material]
        : "Source geometry · " + MaterialNames[Settings.Material] + " response";
  ById("source").setAttribute("aria-pressed", !DisplayingFragments);
  ById("fragments").setAttribute("aria-pressed", DisplayingFragments);
  BusyControls();
}
function DisposeAssembly() {
  if (!Assembly) return;
  for (const Child of [...Assembly.children]) {
    Child.traverse((Part) => {
      Part.geometry?.dispose();
      if (Array.isArray(Part.material))
        Part.material.forEach((Material) => Material.dispose());
      else Part.material?.dispose();
    });
    Assembly.remove(Child);
  }
  Bodies.length = 0;
  Wires.length = 0;
}
function Display(Parts, Fragments) {
  if (!Root || !Assembly) return;
  DisposeAssembly();
  DisplayingFragments = Fragments;
  const Material = Materials[Settings.Material];
  for (const Part of Parts) {
    const Geometry = RenderGeometry(Part),
      Surface = new THREE.Mesh(Geometry, [
        new THREE.MeshStandardMaterial({
          color: Material.color,
          roughness: 0.72,
          flatShading: true,
        }),
        new THREE.MeshStandardMaterial({
          color: Material.interiorColor,
          roughness: 0.95,
          flatShading: true,
        }),
      ]);
    const Outline = new THREE.LineSegments(
      new THREE.EdgesGeometry(Geometry, 23),
      new THREE.LineBasicMaterial({
        color: 0x141915,
        transparent: true,
        opacity: 0.45,
      }),
    );
    Surface.add(Outline);
    const WireGeometry = new THREE.LineSegments(
      new THREE.WireframeGeometry(Geometry),
      new THREE.LineBasicMaterial({
        color: 0x151a17,
        transparent: true,
        opacity: 0.36,
      }),
    );
    WireGeometry.visible = Wire;
    Surface.add(WireGeometry);
    Wires.push(WireGeometry);
    Surface.userData.Center = Bounds(Part).center;
    Surface.userData.Fragments = Fragments;
    Assembly.add(Surface);
    Bodies.push(Surface);
  }
  Arrange();
  Refresh();
}
function Arrange() {
  if (!Root) return;
  const Center = Bounds(Root).center;
  for (const Body of Bodies)
    Body.position
      .copy(Body.userData.Center)
      .sub(Center)
      .multiplyScalar(Body.userData.Fragments ? Separation * 2.6 : 0);
  ById("separation").value = ById("separation-number").value = Math.round(
    Separation * 100,
  );
  ById("separation").style.setProperty("--fill", Separation * 100 + "%");
}
function DisplaySource() {
  if (Root) Display([Root], false);
}
function Fit() {
  if (!Root) return;
  const Extent = Bounds(Root),
    Radius = Extent.size.length();
  Controls.target.copy(Extent.center);
  const Direction =
    Owner.Primitive === "pane"
      ? new THREE.Vector3(0.32, 0.15, 1.65)
      : new THREE.Vector3(0.9, 0.62, 1.22);
  Camera.position.copy(Extent.center).addScaledVector(Direction, Radius);
  Camera.near = Math.max(0.00001, Radius * 0.001);
  Camera.far = Radius * 100;
  Camera.updateProjectionMatrix();
  Controls.minDistance = Radius * 0.3;
  Controls.maxDistance = Radius * 12;
  Controls.update();
}
function Rebuild() {
  Sequence++;
  Result = null;
  DisposeAssembly();
  Root = null;
  ById("unsupported").hidden = true;
  try {
    Root = CreateGeometry(Owner.Primitive, Owner.Scale);
    const Extent = Bounds(Root);
    Floor.position.y = Extent.min.y - 0.06 * Extent.size.length();
    Floor.scale.setScalar(Extent.size.length());
    Impact.scale.setScalar(Extent.size.length() * 0.011);
    Impact.position.set(Settings.X, Settings.Y, Settings.Z);
    DisplaySource();
    Fit();
  } catch (Error) {
    ById("unsupported").hidden = false;
    ById("unsupported").textContent = Error.message;
    for (const Name of [
      "metric-pieces",
      "metric-quality",
      "metric-volume",
      "metric-closure",
    ])
      ById(Name).textContent = "—";
    Status(Error.message);
  }
  Refresh();
}
async function Execute(Store = false) {
  if (Busy || !Root || !Settings.Enabled) return;
  Busy = true;
  Refresh();
  const Current = ++Sequence,
    GenerationOwner = { ...Owner, Scale: [...Owner.Scale] },
    GenerationSettings = { ...Settings };
  Status(
    Store
      ? "Baking closed fragments…"
      : Settings.Mode === "baked"
        ? "Reading stored fragments…"
        : "Generating closed fragments…",
  );
  await new Promise((Resolve) => setTimeout(Resolve, 35));
  try {
    let Generated;
    if (!Store && Settings.Mode === "baked") {
      if (!Ready())
        throw new Error("Bake is missing or stale. Bake this object first.");
      const Parts = DecodeParts(Receipt.Parts),
        Metrics = Parts.map(Validate);
      Generated = {
        parts: Parts,
        milliseconds: 0,
        replay: true,
        volumeError:
          Math.abs(
            Parts.reduce((Sum, Part) => Sum + Volume(Part), 0) - Volume(Root),
          ) / Volume(Root),
        quality: Math.min(...Metrics.map((Measurement) => Measurement.quality)),
        triangles: Metrics.reduce(
          (Sum, Measurement) => Sum + Measurement.triangles,
          0,
        ),
        rejected: Receipt.Rejected,
      };
    } else Generated = Fracture(Root, GenerationSettings);
    if (Generated.volumeError > 1e-7)
      throw new Error(
        "Stored geometry failed volume conservation; rebake the object.",
      );
    if (Current !== Sequence)
      throw new Error(
        "Geometry changed during generation. Run the current recipe again.",
      );
    if (Store) {
      const Encoded = EncodeParts(Generated.parts),
        Next = {
          Signature: Signature(GenerationOwner, GenerationSettings),
          Parts: Encoded,
          Rejected: Generated.rejected,
          Bytes: new Blob([JSON.stringify(Encoded)]).size,
        };
      await WriteFragments(GenerationOwner.Id, Next);
      Receipt = Next;
      Settings.Baked = {
        Signature: Next.Signature,
        Fragments: Next.Parts.length,
        Bytes: Next.Bytes,
      };
      Persist();
      if (Current !== Sequence)
        throw new Error(
          "Recipe changed during storage. Stored pattern is stale; rebake the current geometry.",
        );
    }
    Result = Generated;
    Display(Result.parts, true);
    Status(
      (Store ? "Baked" : Generated.replay ? "Replayed" : "Generated") +
        " " +
        Result.parts.length +
        " closed fragments · occupied volume " +
        ((1 - Result.volumeError) * 100).toFixed(6) +
        "%" +
        (Result.parts.length === 1
          ? " · increase energy or relax minimum span"
          : ""),
    );
  } catch (Error) {
    Status(Error.message);
  } finally {
    Busy = false;
    Refresh();
  }
}
async function LoadReceipt() {
  try {
    Receipt = await ReadFragments(Owner.Id);
  } catch {
    Status(
      "Baked storage is unavailable. Dynamic geometry and recipe export remain available.",
    );
  }
  Refresh();
}
function InitialiseScene() {
  Scene = new THREE.Scene();
  Scene.background = new THREE.Color(0x171717);
  Camera = new THREE.PerspectiveCamera(38, 1, 0.001, 100);
  Renderer = new THREE.WebGLRenderer({
    antialias: true,
    preserveDrawingBuffer: true,
  });
  Renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
  Renderer.outputColorSpace = THREE.SRGBColorSpace;
  Renderer.toneMapping = THREE.ACESFilmicToneMapping;
  Renderer.toneMappingExposure = 1.15;
  ById("viewport").prepend(Renderer.domElement);
  Renderer.domElement.setAttribute("aria-label", "Object fracture geometry");
  Controls = new OrbitControls(Camera, Renderer.domElement);
  Controls.enableDamping = true;
  Controls.dampingFactor = 0.12;
  Scene.add(new THREE.HemisphereLight(0xe8eeea, 0x454e47, 2.2));
  const Key = new THREE.DirectionalLight(0xfff0db, 3.1);
  Key.position.set(4, 6, 5);
  Scene.add(Key);
  const Fill = new THREE.DirectionalLight(0xc4ddeb, 1.4);
  Fill.position.set(-4, 1, -3);
  Scene.add(Fill);
  Assembly = new THREE.Group();
  Scene.add(Assembly);
  Floor = new THREE.GridHelper(4, 24, 0x343434, 0x242424);
  Scene.add(Floor);
  Impact = new THREE.Mesh(
    new THREE.SphereGeometry(1, 12, 8),
    new THREE.MeshBasicMaterial({
      color: 0x8ab69a,
      depthTest: false,
      transparent: true,
      opacity: 0.8,
    }),
  );
  Impact.renderOrder = 5;
  Scene.add(Impact);
  const Resize = new ResizeObserver(() => {
    const Rect = ById("viewport").getBoundingClientRect();
    Renderer.setSize(Rect.width, Rect.height, false);
    Camera.aspect = Rect.width / Math.max(1, Rect.height);
    Camera.updateProjectionMatrix();
  });
  Resize.observe(ById("viewport"));
  Renderer.setAnimationLoop(() => {
    Controls.update();
    Impact.position.set(Settings.X, Settings.Y, Settings.Z);
    Impact.visible = !!Root && Settings.Enabled;
    Renderer.render(Scene, Camera);
  });
  Renderer.domElement.addEventListener("click", (Event) => {
    if (!Event.shiftKey || Busy || !Settings.Enabled || !Root) return;
    const Rect = Renderer.domElement.getBoundingClientRect(),
      Pointer = new THREE.Vector2(
        (2 * (Event.clientX - Rect.left)) / Rect.width - 1,
        1 - (2 * (Event.clientY - Rect.top)) / Rect.height,
      ),
      Ray = new THREE.Raycaster();
    Ray.setFromCamera(Pointer, Camera);
    const Proxy = new THREE.Mesh(
      RenderGeometry(Root),
      new THREE.MeshBasicMaterial(),
    );
    Proxy.updateMatrixWorld();
    const Hits = Ray.intersectObject(Proxy);
    Proxy.geometry.dispose();
    Proxy.material.dispose();
    if (!Hits.length) return;
    const Point = Hits[0].point;
    Settings = Normalize({
      ...Settings,
      X: +Point.x.toFixed(4),
      Y: +Point.y.toFixed(4),
      Z: +Point.z.toFixed(4),
    });
    Persist();
    Result = null;
    Sequence++;
    DisplaySource();
    Refresh();
    Status("Impact placed on the source surface.");
  });
}
ById("fracture").onclick = () => Execute();
ById("bake").onclick = () => Execute(true);
ById("source").onclick = DisplaySource;
ById("fragments").onclick = () => Result && Display(Result.parts, true);
ById("fit").onclick = Fit;
ById("wire").onclick = () => {
  Wire = !Wire;
  Wires.forEach((Line) => (Line.visible = Wire));
  ById("wire").setAttribute("aria-pressed", Wire);
};
for (const Name of ["separation", "separation-number"])
  ById(Name).oninput = (Event) => {
    Separation = Math.max(0, Math.min(1, Number(Event.target.value) / 100));
    Arrange();
  };
ById("assemble").onclick = () => {
  Separation = 0;
  Arrange();
  Status(
    "Fragments reassembled at their original positions; no cells or shards removed.",
  );
};
ById("center-impact").onclick = () => {
  const Center = Root ? Bounds(Root).center : new THREE.Vector3();
  Settings = Normalize({ ...Settings, X: Center.x, Y: Center.y, Z: Center.z });
  Result = null;
  Sequence++;
  Persist();
  DisplaySource();
  Refresh();
};
for (const Mode of ["dynamic", "baked"])
  ById(Mode + "-mode").onclick = () => Assign("Mode", Mode);
ById("primitive").onchange = () => {
  Owner = {
    ...Owner,
    Primitive: ById("primitive").value,
    Name: ById("primitive").selectedOptions[0].textContent + " specimen",
  };
  Settings = { ...Settings, X: 0, Y: 0, Z: 0 };
  Persist();
  Rebuild();
};
ById("clear-bake").onclick = async () => {
  if (Busy) return;
  Busy = true;
  Refresh();
  try {
    await WriteFragments(Owner.Id, null);
    Receipt = null;
    delete Settings.Baked;
    Result = null;
    DisplaySource();
    Persist();
    Status("Stored fragments cleared for this object only.");
  } catch (Error) {
    Status("Could not clear fragments: " + Error.message);
  } finally {
    Busy = false;
    Refresh();
  }
};
ById("piece-sdf").onchange = (Event) =>
  Assign("PieceSdf", Event.target.checked);
ById("sdf-resolution").onchange = (Event) =>
  Assign("SdfResolution", Number(Event.target.value));
function ExportRecord() {
  return {
    Format: "Frontier.Fracture.Html.v2",
    Owner,
    Settings,
    Signature: Signature(Owner, Settings),
    Geometry: Ready() ? Receipt.Parts : null,
    Sdf: {
      Requested: Settings.Mode === "baked" && Settings.PieceSdf,
      PerFragment: true,
      Resolution: Settings.SdfResolution,
      Format: "R16F",
      Generated: false,
    },
    NativeCompatible: false,
  };
}
ById("export").onclick = () => {
  const Record = ExportRecord(),
    BlobUrl = URL.createObjectURL(
      new Blob([JSON.stringify(Record, null, 2)], { type: "application/json" }),
    ),
    Link = document.createElement("a");
  Link.href = BlobUrl;
  Link.download = Owner.Name.replace(/[^\w.-]/g, "_") + ".fracture.json";
  Link.click();
  setTimeout(() => URL.revokeObjectURL(BlobUrl), 1000);
  Status(
    "Exported object recipe" +
      (Record.Geometry
        ? " and matching baked fragments"
        : " (no current baked fragments)") +
      ".",
  );
};
window.addEventListener("storage", (Event) => {
  if (Event.key !== Prefix + Owner.Id || !Event.newValue) return;
  try {
    const Record = JSON.parse(Event.newValue);
    if (Record.Owner?.Id !== Owner.Id) return;
    const GeometryChanged =
      Record.Owner.Primitive !== Owner.Primitive ||
      JSON.stringify(Record.Owner.Scale) !== JSON.stringify(Owner.Scale);
    const ModeChanged = Settings.Mode !== Record.Settings?.Mode;
    const RecipeChanged =
      Signature(Owner, Settings) !== Signature(Record.Owner, Record.Settings);
    Owner = Record.Owner;
    Settings = Normalize(Record.Settings);
    Sequence++;
    if (GeometryChanged) Rebuild();
    else if (RecipeChanged || ModeChanged || !Settings.Enabled) {
      Result = null;
      DisplaySource();
    }
    Refresh();
    LoadReceipt();
    Status(
      Owner.Removed
        ? "This object was removed from the scene. Fracture is disabled."
        : "Updated from the object inspector" +
            (!Settings.Enabled ? " · fracture disabled" : "") +
            ".",
    );
  } catch (Error) {
    Status("Could not apply object settings: " + Error.message);
  }
});
window.FrontierFracture = {
  snapshot: () => ({
    Owner,
    Settings,
    Linked,
    Busy,
    Supported: !!Root,
    DisplayingFragments,
    Baked: Ready(),
    Receipt: Receipt
      ? {
          Signature: Receipt.Signature,
          Bytes: Receipt.Bytes,
          Count: Receipt.Parts.length,
        }
      : null,
    Result: Result
      ? {
          Count: Result.parts.length,
          VolumeError: Result.volumeError,
          Quality: Result.quality,
          Triangles: Result.triangles,
          Replay: !!Result.replay,
        }
      : null,
    Bounds: Root ? Bounds(Root).size.toArray() : null,
    Signature: Signature(Owner, Settings),
  }),
  geometry: () =>
    Result ? EncodeParts(Result.parts) : Root ? EncodeParts([Root]) : [],
  export: ExportRecord,
};
try {
  InitialiseScene();
  Rebuild();
  LoadReceipt();
  if (Linked && !Initial)
    Status(
      "This object is not available. Open Fracture from the object inspector in Project-Zero.",
    );
  else
    Status(
      Linked
        ? "Editing " +
            Owner.Name +
            " · settings are linked to its object inspector."
        : "Standalone geometry specimen · use Project-Zero for per-object authoring.",
    );
} catch (Error) {
  ById("unsupported").hidden = false;
  ById("unsupported").textContent =
    "WebGL preview unavailable: " + Error.message;
  Status("WebGL could not start.");
}
