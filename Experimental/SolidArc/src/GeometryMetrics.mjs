import Verify from "node:test";
import Assert from "node:assert/strict";
import CreateGeometry from "../Runtime/GeometryModule.js";
import {
  Tools,
  Examples,
  ConstructCommand,
  NativeName,
  Escape,
} from "./CommandSpecification.js";
const Lines = [];
const Geometry = await CreateGeometry({
  print: (Text) => Lines.push(Text),
  printErr: (Text) => Lines.push(Text),
});
const Invoke = (Name, Result, Types = [], Arguments = []) =>
  Geometry.ccall(Name, Result, Types, Arguments);
const Command = (Text) =>
  !!Invoke("ExecuteCommand", "number", ["string"], [Text]);
const Describe = () => JSON.parse(Invoke("DescribeDocument", "string"));
const Fresh = (Action) => () => {
  const Identity = Invoke("CreateDocument", "number");
  try {
    Action();
  } finally {
    Invoke("ReleaseDocument", null, ["number"], [Identity]);
  }
};
Verify(
  "Every supplied study executes against the actual C++ WebAssembly module",
  Fresh(() => {
    for (const Example of Object.values(Examples)) {
      Assert.ok(Command("reset"));
      for (const Text of Example.commands) Assert.ok(Command(Text), Text);
      Assert.ok(Describe().figures.length > 0);
    }
  }),
);
Verify(
  "Native primitive topology and exact extents are exposed, not mock counts",
  Fresh(() => {
    Assert.ok(Command("box (0,0,0) 2 3 4 --name=Block"));
    const Figure = Describe().figures[0];
    Assert.equal(Figure.faces, 6);
    Assert.equal(Figure.edges, 12);
    Assert.equal(Figure.vertices, 8);
    Assert.deepEqual(Figure.high, [2, 3, 4]);
  }),
);
Verify(
  "A live dimension rebuilds geometry; native undo and redo restore it",
  Fresh(() => {
    Command("box (0,0,0) 2 3 4 --name=Block");
    Command("dim auto");
    const Dimension = Describe().dimensions.find(
      (Dimension) => Dimension.slot === 5,
    );
    Assert.ok(Command(`dim edit ${Dimension.id} 6`));
    Assert.equal(Describe().figures[0].high[2], 6);
    Assert.ok(Command("undo"));
    Assert.equal(Describe().figures[0].high[2], 4);
    Assert.ok(Command("redo"));
    Assert.equal(Describe().figures[0].high[2], 6);
  }),
);
Verify(
  "Native circle extrusion produces a solid with the requested height",
  Fresh(() => {
    Command("circle (0,0) 1 --name=Profile");
    Assert.ok(Command("extrude Profile 2"));
    const Body = Describe().figures.find(
      (Figure) => Figure.classification === 2,
    );
    Assert.ok(Body.faces > 0);
    Assert.equal(Body.high[2] - Body.low[2], 2);
  }),
);
Verify(
  "Cylinder subtraction produces the native through-hole topology",
  Fresh(() => {
    Command("cylinder (0,0,0) 1 2 --name=Stock");
    Command("cylinder (0,0,-1) 0.4 4 --name=Tool");
    Assert.ok(Command("boolean subtract Stock -- Tool"));
    Assert.equal(Describe().figures.length, 1);
    Assert.equal(Describe().figures[0].faces, 4);
  }),
);
Verify(
  "Unsupported shell is refused without replacing the sphere",
  Fresh(() => {
    Command("sphere (0,0,0) 1 --name=Ball");
    const Before = Describe().figures;
    Assert.equal(Command("shell Ball 0.1 --face=0"), false);
    Assert.deepEqual(Describe().figures, Before);
  }),
);
Verify(
  "Native save/reopen preserves edits and rejects malformed input atomically",
  Fresh(() => {
    Command("box (0,0,0) 2 3 4 --name=Block");
    Command("dim auto");
    const Dimension = Describe().dimensions.find(
      (Dimension) => Dimension.slot === 5,
    );
    Command(`dim edit ${Dimension.id} 5`);
    const Before = Describe().figures;
    Assert.ok(Command("save /roundtrip.arc"));
    Assert.match(
      Geometry.FS.readFile("/roundtrip.arc", { encoding: "utf8" }),
      /^# SolidArc native document v1/,
    );
    Command("reset");
    Assert.ok(Command("open /roundtrip.arc"));
    Assert.deepEqual(Describe().figures, Before);
    Geometry.FS.writeFile("/invalid.arc", "not a native document");
    Assert.equal(Command("open /invalid.arc"), false);
    Assert.deepEqual(Describe().figures, Before);
  }),
);
Verify(
  "Independent native documents retain their own geometry",
  Fresh(() => {
    Command("box (0,0,0) 2 3 4");
    const First = Describe().identity;
    const Second = Invoke("CreateDocument", "number");
    Command("sphere (0,0,0) 1");
    Assert.equal(Describe().figures[0].faces, 1);
    Invoke("ActivateDocument", "number", ["number"], [First]);
    Assert.equal(Describe().figures[0].faces, 6);
    Invoke("ReleaseDocument", null, ["number"], [Second]);
  }),
);
Verify(
  "Original software raster returns nonempty RGBA pixels and camera orbit changes them",
  Fresh(() => {
    Command("box (0,0,0) 2 3 4");
    Command("view fit");
    const Pointer = Invoke(
      "RenderDocument",
      "number",
      ["number", "number"],
      [320, 240],
    );
    const Before = Geometry.HEAPU8.slice(Pointer, Pointer + 320 * 240 * 4);
    Assert.ok(Before.some((Channel, Index) => Index % 4 !== 3 && Channel > 20));
    Invoke("OrbitDocument", null, ["number", "number"], [0.3, 0.1]);
    const Next = Invoke(
      "RenderDocument",
      "number",
      ["number", "number"],
      [320, 240],
    );
    Assert.notDeepEqual(
      Geometry.HEAPU8.slice(Next, Next + 320 * 240 * 4),
      Before,
    );
  }),
);
Verify(
  "Native OBJ export contains tessellated geometry",
  Fresh(() => {
    Command("box (0,0,0) 1 2 3");
    Assert.ok(Command("export /geometry.obj"));
    const Text = Geometry.FS.readFile("/geometry.obj", { encoding: "utf8" });
    Assert.match(Text, /^v /m);
    Assert.match(Text, /^f /m);
  }),
);
Verify(
  "All primitive UI defaults dispatch valid native commands",
  Fresh(() => {
    for (const [Key, Tool] of Object.entries(Tools)) {
      if (Tool.group === "Operations") continue;
      const Settings = Object.fromEntries(
        Tool.fields.map((Field) => [Field.key, Field.initial]),
      );
      Assert.ok(Command(ConstructCommand(Key, Settings, [])), Key);
    }
  }),
);
Verify(
  "Command forms reject nonfinite values, invalid torus radii and missing selections",
  () => {
    Assert.throws(() =>
      ConstructCommand("sphere", { radius: NaN, x: 0, y: 0, z: 0 }, []),
    );
    Assert.throws(() =>
      ConstructCommand("torus", { major: 1, minor: 2, x: 0, y: 0, z: 0 }, []),
    );
    Assert.throws(() => ConstructCommand("extrude", { distance: 1 }, []));
    Assert.throws(() =>
      ConstructCommand("fillet", { radius: 0.1 }, [{ id: 1, pickedEdges: [] }]),
    );
    Assert.throws(() =>
      ConstructCommand("subtract", {}, [{ id: 1, classification: 2 }]),
    );
  },
);
Verify("Names cannot inject commands or HTML into generated forms", () => {
  Assert.throws(() => NativeName("Box; reset"));
  Assert.throws(() => NativeName('Box"'));
  Assert.equal(NativeName("Bracket 01"), '"Bracket 01"');
  Assert.equal(
    Escape('<img onerror="1">'),
    "&lt;img onerror=&quot;1&quot;&gt;",
  );
});

Verify(
  "Native material finishes change the rendered pixels",
  Fresh(() => {
    Command("sphere (0,0,0) 1");
    Command("view fit");
    Command("show shading matcap");
    Command("matcap all steel");
    const First = Invoke(
      "RenderDocument",
      "number",
      ["number", "number"],
      [160, 120],
    );
    const Steel = Geometry.HEAPU8.slice(First, First + 160 * 120 * 4);
    Command("matcap all copper");
    const Next = Invoke(
      "RenderDocument",
      "number",
      ["number", "number"],
      [160, 120],
    );
    Assert.notDeepEqual(
      Geometry.HEAPU8.slice(Next, Next + 160 * 120 * 4),
      Steel,
    );
  }),
);
Verify(
  "Native box union, intersection and supported shell execute successfully",
  Fresh(() => {
    for (const Operation of ["union", "intersect"]) {
      Command("reset");
      Command("box (0,0,0) (2,2,2) --name=Stock");
      Command("box (1,1,1) (3,3,3) --name=Tool");
      Assert.ok(Command(`boolean ${Operation} Stock -- Tool`));
      Assert.equal(Describe().figures.length, 1);
      Assert.ok(Describe().figures[0].faces >= 6);
    }
    Command("reset");
    Command("box (0,0,0) (2,2,2) --name=Stock");
    Assert.ok(Command("shell Stock 0.1 --face=5"));
    Assert.ok(Describe().figures.some((Figure) => Figure.faces > 6));
  }),
);

Verify(
  "Native sketch-region extrusion retains inner holes and exposes control-point picks",
  Fresh(() => {
    Command("circle (0,0) 1 --name=Outer");
    Command("circle (0,0) 0.4 --name=Inner");
    const Region = Describe().areas.find((Region) => Region.holes === 1);
    Assert.ok(Region);
    Assert.ok(Command(`extrude a${Region.id} 1`));
    Assert.equal(
      Describe().figures.find((Figure) => Figure.classification === 2).faces,
      4,
    );
    Command("selectmode control");
    Command("select poles Outer 0");
    Assert.deepEqual(
      Describe().figures.find((Figure) => Figure.name === "Outer").pickedPoles,
      [0],
    );
  }),
);

const Render = (Width = 800, Height = 600) =>
  Invoke("RenderDocument", "number", ["number", "number"], [Width, Height]);
const CloseEnough = (Actual, Expected, Tolerance = 1e-8) =>
  Assert.ok(Math.abs(Actual - Expected) < Tolerance, `${Actual} ≠ ${Expected}`);
Verify(
  "All 22 native Construct catalogue entries create actual geometry",
  async () => {
    const { ConstructionCatalogue, PlaceConstruction } = await import(
      "./ConstructionSpecification.js"
    );
    Assert.equal(ConstructionCatalogue.length, 22);
    Assert.deepEqual(
      [...new Set(ConstructionCatalogue.map((Entry) => Entry.section))],
      ["Reference", "Sketch Draw", "Solid", "Surface"],
    );
    for (let Index = 0; Index < ConstructionCatalogue.length; ++Index) {
      Fresh(() => {
        Assert.ok(
          Command(PlaceConstruction(Index, Index)),
          ConstructionCatalogue[Index].label,
        );
        Assert.equal(Describe().figures.length, 1);
        Assert.ok(
          Describe().figures[0].poles > 0 || Describe().figures[0].faces > 0,
        );
      })();
    }
  },
);
Verify(
  "Empty selections expose no nonfinite gizmo coordinates",
  Fresh(() => {
    Assert.ok(Command("gizmo on"));
    Render();
    Assert.deepEqual(Describe().gizmo.grips, []);
    Assert.equal(
      Invoke("BeginGizmoDocument", "number", ["number", "number"], [400, 300]),
      0,
    );
  }),
);
Verify(
  "Affine inspector translation, rotation and scale are native, undoable and persistent",
  Fresh(() => {
    Command("box (0,0,0) 2 3 4 --name=Block");
    Command("select Block");
    Assert.ok(Command("transform selected --move=(1,2,3)"));
    Assert.deepEqual(Describe().figures[0].low, [1, 2, 3]);
    Command("dim auto");
    const Height = Describe().dimensions.find((Entry) => Entry.slot === 5);
    Assert.ok(Command(`dim edit ${Height.id} 8`));
    Assert.deepEqual(Describe().figures[0].low, [1, 2, 3]);
    Command("undo");
    Assert.ok(Command("transform selected --rotate=(0,0,90)"));
    const Rotated = Describe().figures[0];
    CloseEnough(Rotated.high[0] - Rotated.low[0], 3);
    CloseEnough(Rotated.high[1] - Rotated.low[1], 2);
    Assert.ok(Command("transform selected --scale=(2,1,1)"));
    const Scaled = Describe().figures[0];
    CloseEnough(Scaled.high[0] - Scaled.low[0], 6);
    Command("undo");
    CloseEnough(
      Describe().figures[0].high[0] - Describe().figures[0].low[0],
      3,
    );
    Command("redo");
    Assert.ok(Command("save /transform.arc"));
    const Saved = Describe().figures;
    Assert.ok(Command("reset"));
    Assert.ok(Command("open /transform.arc"));
    Assert.deepEqual(Describe().figures, Saved);
  }),
);
Verify(
  "Transform refusal preserves geometry for invalid and singular matrices",
  Fresh(() => {
    Command("box (0,0,0) 2 3 4 --name=Block");
    const Before = Describe().figures;
    for (const Instruction of [
      "transform Block --move=(nan,0,0)",
      "transform Block --scale=(0,1,1)",
      "transform Block 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1",
      "transform missing --move=(1,0,0)",
    ]) {
      Assert.equal(Command(Instruction), false, Instruction);
      Assert.deepEqual(Describe().figures, Before);
    }
  }),
);
Verify(
  "Original analytic gizmo drags, snaps, cancels and journals a camera-independent affine edit",
  Fresh(() => {
    Command("box (0,0,0) 2 3 4 --name=Block");
    Command("select Block");
    Command("view front");
    Command("view fit");
    Invoke("SeatDocument", null, ["number", "number", "number"], [800, 600, 1]);
    Render();
    const Grip = Describe().gizmo.grips.find((Entry) => Entry.id === 1);
    Assert.ok(Grip);
    const Before = Describe().figures;
    const History = Describe().history.length;
    Assert.equal(
      Invoke(
        "BeginGizmoDocument",
        "number",
        ["number", "number"],
        [Grip.u * 800, Grip.v * 600],
      ),
      1,
    );
    Invoke(
      "DragGizmoDocument",
      "number",
      ["number", "number", "number"],
      [Grip.u * 800 + 43, Grip.v * 600, 1],
    );
    Assert.notDeepEqual(Describe().figures, Before);
    Invoke("FinishGizmoDocument", "number", ["number"], [1]);
    Assert.deepEqual(Describe().figures, Before);
    Assert.equal(Describe().history.length, History);
    Render();
    Assert.equal(
      Invoke(
        "BeginGizmoDocument",
        "number",
        ["number", "number"],
        [Grip.u * 800, Grip.v * 600],
      ),
      1,
    );
    Invoke(
      "DragGizmoDocument",
      "number",
      ["number", "number", "number"],
      [Grip.u * 800 + 43, Grip.v * 600, 1],
    );
    Assert.equal(Invoke("FinishGizmoDocument", "number", ["number"], [0]), 1);
    const After = Describe().figures;
    const Movement = After[0].low[0] - Before[0].low[0];
    CloseEnough(Movement / 0.25, Math.round(Movement / 0.25));
    Assert.equal(Describe().history.length, History + 1);
    Command("undo");
    Assert.deepEqual(Describe().figures, Before);
    Command("redo");
    Assert.deepEqual(Describe().figures, After);
    Invoke("OrbitDocument", null, ["number", "number"], [0.3, 0.1]);
    Invoke("ZoomDocument", null, ["number"], [2]);
    Command("save /gizmo.arc");
    const Journal = Geometry.FS.readFile("/gizmo.arc", { encoding: "utf8" });
    Assert.match(Journal, /transform selected/);
    Assert.doesNotMatch(Journal, /^(click|pointer|release) /m);
    Command("reset");
    Assert.ok(Command("open /gizmo.arc"));
    Assert.deepEqual(Describe().figures, After);
  }),
);
Verify(
  "Gizmo control-pole edits preserve the other poles and survive undo",
  Fresh(() => {
    Command("spline (0,0,0) (1,2,0) (2,-1,0) (3,0,0) --name=Guide");
    Command("selectmode control");
    Command("select poles Guide 0");
    Command("view top");
    Command("view fit");
    Render();
    const Before = Describe().figures;
    const Grip = Describe().gizmo.grips.find((Entry) => Entry.id === 1);
    Assert.equal(
      Invoke(
        "BeginGizmoDocument",
        "number",
        ["number", "number"],
        [Grip.u * 800, Grip.v * 600],
      ),
      1,
    );
    Invoke(
      "DragGizmoDocument",
      "number",
      ["number", "number", "number"],
      [Grip.u * 800 - 40, Grip.v * 600, 0],
    );
    Assert.equal(Invoke("FinishGizmoDocument", "number", ["number"], [0]), 1);
    Assert.ok(Describe().figures[0].low[0] < Before[0].low[0]);
    Command("undo");
    Assert.deepEqual(Describe().figures, Before);
  }),
);
Verify(
  "Zoom remains finite at extremes and inverse wheel steps restore camera distance",
  Fresh(() => {
    const Before = Describe().camera.distance;
    Invoke("ZoomDocument", null, ["number"], [1]);
    Assert.ok(Describe().camera.distance < Before);
    Invoke("ZoomDocument", null, ["number"], [-1]);
    CloseEnough(Describe().camera.distance, Before);
    for (let Index = 0; Index < 30; ++Index)
      Invoke("ZoomDocument", null, ["number"], [-40]);
    Assert.equal(Describe().camera.distance, 1e8);
    Invoke("ZoomDocument", null, ["number"], [NaN]);
    Assert.equal(Describe().camera.distance, 1e8);
  }),
);
Verify(
  "Transformed derived solids retain their authored shape instead of snapping back to the recipe",
  Fresh(() => {
    Command("circle (0,0) 1 --name=Profile");
    Command("extrude Profile 2");
    Command("select Extrusion");
    Assert.ok(Command("transform selected --move=(5,0,0)"));
    Command("dim auto");
    const Body = Describe().figures.find(
      (Figure) => Figure.classification === 2,
    );
    CloseEnough(Body.low[0], 4);
    CloseEnough(Body.high[0], 6);
    Assert.equal(
      Describe().dimensions.filter(
        (Dimension) => Dimension.anchor === Body.id && Dimension.slot >= 0,
      ).length,
      0,
    );
    Assert.ok(Command("transform Profile --move=(0,3,0)"));
    Assert.deepEqual(
      Describe().figures.find((Figure) => Figure.id === Body.id),
      Body,
    );
    Command("undo");
    Command("undo");
    Command("dim auto");
    Assert.ok(
      Describe().dimensions.some(
        (Dimension) => Dimension.anchor === Body.id && Dimension.slot >= 0,
      ),
    );
  }),
);
