import Assert from "node:assert/strict";
import { mkdir, readFile } from "node:fs/promises";
import { chromium } from "playwright";
const Browser = await chromium.launch({
  headless: true,
  ...(process.env.BROWSER_EXECUTABLE
    ? { executablePath: process.env.BROWSER_EXECUTABLE }
    : {}),
  args: ["--no-sandbox", "--disable-dev-shm-usage"],
});
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
Page.setDefaultTimeout(30000);
const Errors = [];
Page.on("pageerror", (ErrorValue) => Errors.push(ErrorValue.message));
Page.on("console", (Message) => {
  if (Message.type() === "error") Errors.push(Message.text());
});
let Count = 0;
const Pass = (Text) => console.log(`PASS ${++Count}: ${Text}`);
const Ready = () =>
  Page.waitForFunction(
    () =>
      solidArc.Ready &&
      !solidArc.Busy &&
      !solidArc.Motion &&
      !solidArc.ResolveDetail &&
      !solidArc.ViewDrag,
  );
const Snapshot = () =>
  Page.evaluate(() => structuredClone(solidArc.Description));
const Run = async (Text) => {
  await Ready();
  await Page.evaluate((Text) => solidArc.Command(Text), Text);
  await Ready();
};
const Near = (Actual, Expected, Tolerance = 1e-6) =>
  Assert.ok(
    Math.abs(Actual - Expected) < Tolerance,
    `${Actual} differs from ${Expected}`,
  );
const Extent = (Figure, Axis) => Figure.high[Axis] - Figure.low[Axis];
const CanvasBounds = () => Page.locator("#cad-canvas").boundingBox();
const Mode = async (Value) => {
  await Page.locator("#gizmo-mode").selectOption(Value);
  await Ready();
};
const GripPoint = async (Identity) => {
  const Rectangle = await CanvasBounds();
  const Grip = (await Snapshot()).gizmo.grips.find(
    (Entry) => Entry.id === Identity,
  );
  Assert.ok(Grip, `Grip ${Identity} is exposed by the native gizmo`);
  return {
    x: Rectangle.x + Grip.u * Rectangle.width,
    y: Rectangle.y + Grip.v * Rectangle.height,
  };
};
const DragGrip = async (
  Identity,
  Horizontal,
  Vertical,
  Cancel = false,
  Snap = false,
) => {
  const Point = await GripPoint(Identity);
  await Page.mouse.move(Point.x, Point.y);
  await Ready();
  if (Snap) await Page.keyboard.down("Control");
  await Page.mouse.down();
  await Page.mouse.move(Point.x + Horizontal, Point.y + Vertical, { steps: 8 });
  if (Cancel) await Page.keyboard.press("Escape");
  await Page.mouse.up();
  if (Snap) await Page.keyboard.up("Control");
  await Ready();
};
const EditCell = async (Row, Axis, Value) => {
  const Input = Page.locator(`[data-transform="${Row}"][data-axis="${Axis}"]`);
  await Input.fill(String(Value));
  await Input.press("Tab");
  await Ready();
};
try {
  await Page.goto(process.env.SOLIDARC_URL || "http://127.0.0.1:5173/");
  await Page.waitForFunction(
    () => window.solidArc?.Description.figures.length === 7 && !solidArc.Busy,
  );
  await Ready();
  await Run([
    "reset",
    "box (0,0,0) 2 3 4 --name=Block",
    "select Block",
    "gizmo on",
    "view iso",
    "view fit",
  ]);
  let Rectangle = await CanvasBounds();
  await Page.mouse.move(
    Rectangle.x + Rectangle.width * 0.7,
    Rectangle.y + Rectangle.height * 0.65,
  );
  await Ready();
  let Before = (await Snapshot()).camera.distance;
  await Page.mouse.wheel(0, -100);
  await Ready();
  Near((await Snapshot()).camera.distance, Before * 0.85);
  await Page.mouse.wheel(0, 100);
  await Ready();
  Near((await Snapshot()).camera.distance, Before);
  await Page.locator("#cad-canvas").dispatchEvent("wheel", {
    deltaY: -1,
    deltaMode: 0,
  });
  await Ready();
  Near((await Snapshot()).camera.distance, Before * Math.pow(0.85, 0.01));
  await Page.locator("#cad-canvas").dispatchEvent("wheel", {
    deltaY: 0.0625,
    deltaMode: 1,
  });
  await Ready();
  Near((await Snapshot()).camera.distance, Before);
  Pass(
    "Wheel direction and magnitude are correct for mouse pixels, tiny trackpad deltas and line units",
  );

  const Camera = (await Snapshot()).camera;
  await Page.keyboard.down("Alt");
  await Page.mouse.down();
  // A single browser task deliberately delivers mixed events faster than the worker can render.
  await Page.evaluate(
    ({ Horizontal, Vertical }) => {
      const Canvas = document.querySelector("#cad-canvas");
      const Identity = solidArc.ViewDrag.identity;
      for (let Index = 1; Index <= 20; ++Index) {
        Canvas.dispatchEvent(
          new PointerEvent("pointermove", {
            pointerId: Identity,
            clientX: Horizontal + Index * 2,
            clientY: Vertical + Index * 0.5,
            buttons: 1,
          }),
        );
        Canvas.dispatchEvent(
          new WheelEvent("wheel", {
            deltaY: Index % 2 ? -3 : 2,
            deltaMode: 0,
            cancelable: true,
          }),
        );
      }
      Canvas.dispatchEvent(
        new PointerEvent("pointerup", {
          pointerId: Identity,
          clientX: Horizontal + 40,
          clientY: Vertical + 10,
          button: 0,
        }),
      );
    },
    {
      Horizontal: Rectangle.x + Rectangle.width * 0.7,
      Vertical: Rectangle.y + Rectangle.height * 0.65,
    },
  );
  await Page.mouse.up();
  await Page.keyboard.up("Alt");
  await Ready();
  const Mixed = (await Snapshot()).camera;
  Near(Mixed.yaw, Camera.yaw - 40 * 0.006);
  Near(Mixed.pitch, Camera.pitch + 10 * 0.006);
  Near(Mixed.distance, Camera.distance * Math.pow(0.85, 0.1));
  Assert.ok(Number.isFinite(Mixed.distance));
  Pass(
    "Rapid interleaved orbit and wheel events retain every delta while the worker is busy",
  );

  const PanBefore = (await Snapshot()).camera.pivot;
  await Page.mouse.move(
    Rectangle.x + Rectangle.width * 0.6,
    Rectangle.y + Rectangle.height * 0.6,
  );
  await Ready();
  await Page.mouse.down({ button: "right" });
  await Page.mouse.move(
    Rectangle.x + Rectangle.width * 0.6 + 35,
    Rectangle.y + Rectangle.height * 0.6 + 20,
    { steps: 7 },
  );
  await Page.mouse.up({ button: "right" });
  await Ready();
  Assert.notDeepEqual((await Snapshot()).camera.pivot, PanBefore);
  await Page.mouse.down({ button: "middle" });
  await Page.mouse.move(
    Rectangle.x + Rectangle.width * 0.6,
    Rectangle.y + Rectangle.height * 0.6,
    { steps: 7 },
  );
  await Page.mouse.up({ button: "middle" });
  await Ready();
  (await Snapshot()).camera.pivot.forEach((Value, Index) =>
    Near(Value, PanBefore[Index]),
  );
  const Selected = (await Snapshot()).figures;
  await Page.mouse.click(
    Rectangle.x + 50,
    Rectangle.y + Rectangle.height - 65,
    { button: "right" },
  );
  await Ready();
  Assert.deepEqual((await Snapshot()).figures, Selected);
  Pass(
    "Right/middle pan round-trips without jumps; a stationary right click does not change selection",
  );

  await Run(["view front", "view fit"]);
  await Mode("translate");
  let Original = (await Snapshot()).figures;
  const History = (await Snapshot()).history.length;
  await DragGrip(1, 43, 0, true);
  Assert.deepEqual((await Snapshot()).figures, Original);
  Assert.equal((await Snapshot()).history.length, History);
  await DragGrip(1, 43, 0, false, true);
  const Moved = (await Snapshot()).figures;
  Assert.notDeepEqual(Moved, Original);
  Near(
    (Moved[0].low[0] - Original[0].low[0]) / 0.25,
    Math.round((Moved[0].low[0] - Original[0].low[0]) / 0.25),
  );
  Assert.equal((await Snapshot()).history.length, History + 1);
  await Page.locator("#undo-button").click();
  await Ready();
  Assert.deepEqual((await Snapshot()).figures, Original);
  await Page.locator("#redo-button").click();
  await Ready();
  Assert.deepEqual((await Snapshot()).figures, Moved);
  const Downloading = Page.waitForEvent("download");
  await Page.locator("#save-button").click();
  const Download = await Downloading;
  const NativeText = await readFile(await Download.path(), "utf8");
  await Ready();
  Assert.match(NativeText, /transform selected/);
  Assert.doesNotMatch(NativeText, /^(pointer|click|release) /m);
  await Run("view top");
  await Page.locator("#open-file").setInputFiles({
    name: "Gizmo.arc",
    mimeType: "text/plain",
    buffer: Buffer.from(NativeText),
  });
  await Ready();
  Assert.deepEqual((await Snapshot()).figures, Moved);
  Pass(
    "Original C++ gizmo moves and Ctrl-snaps, Escape restores preview, one undo restores geometry, and .arc replays it",
  );

  await Run(["view front", "view fit"]);
  await Mode("scale");
  Original = (await Snapshot()).figures;
  await DragGrip(4, 30, 0);
  Assert.ok(Extent((await Snapshot()).figures[0], 0) > Extent(Original[0], 0));
  await Run("undo");
  Assert.deepEqual((await Snapshot()).figures, Original);
  await Run(["view top", "view fit"]);
  await Mode("rotate");
  const RotationPoint = await GripPoint(12);
  Rectangle = await CanvasBounds();
  const Centre = {
    x: Rectangle.x + Rectangle.width / 2,
    y: Rectangle.y + Rectangle.height / 2,
  };
  const Offset = {
    x: RotationPoint.x - Centre.x,
    y: RotationPoint.y - Centre.y,
  };
  const Angle = Math.PI / 4;
  await DragGrip(
    12,
    Offset.x * Math.cos(Angle) - Offset.y * Math.sin(Angle) - Offset.x,
    Offset.x * Math.sin(Angle) + Offset.y * Math.cos(Angle) - Offset.y,
  );
  Assert.notDeepEqual((await Snapshot()).figures, Original);
  await Run("undo");
  Assert.deepEqual((await Snapshot()).figures, Original);
  await Mode("combined");
  await DragGrip(9, 20, -15);
  const Planar = (await Snapshot()).figures[0];
  Assert.notEqual(Planar.low[0], Original[0].low[0]);
  Assert.notEqual(Planar.low[1], Original[0].low[1]);
  Near(Planar.low[2], Original[0].low[2]);
  await Run("undo");
  Pass(
    "Native scale cylinders, rotation sectors and planar grips all change real geometry with undo",
  );

  await Page.locator('[data-inspector="transform"]').click();
  Assert.equal(await Page.locator(".transform-table tbody tr").count(), 3);
  Assert.equal(await Page.locator("[data-transform]").count(), 9);
  await EditCell("position", 0, 5);
  Near(
    ((await Snapshot()).figures[0].low[0] +
      (await Snapshot()).figures[0].high[0]) /
      2,
    5,
  );
  await EditCell("rotation", 2, 90);
  await Page.evaluate(() => solidArc.Request("render"));
  await Ready();
  Assert.equal(
    await Page.locator(
      '[data-transform="rotation"][data-axis="2"]',
    ).inputValue(),
    "90",
  );
  Near(Extent((await Snapshot()).figures[0], 0), 3);
  Near(Extent((await Snapshot()).figures[0], 1), 2);
  await Page.locator('[data-transform-reset="rotation"]').click();
  await Ready();
  Near(Extent((await Snapshot()).figures[0], 0), 2);
  await EditCell("scale", 0, 2);
  Near(Extent((await Snapshot()).figures[0], 0), 4);
  await Page.locator('[data-transform-reset="scale"]').click();
  await Ready();
  Near(Extent((await Snapshot()).figures[0], 0), 2);
  await Page.locator("#uniform-scale").fill("1.5");
  await Page.locator("#uniform-scale").dispatchEvent("change");
  await Ready();
  Near(Extent((await Snapshot()).figures[0], 0), 3);
  await Page.locator('[data-transform-reset="position"]').click();
  await Ready();
  const Centred = (await Snapshot()).figures[0];
  Centred.low.forEach((Value, Index) =>
    Near((Value + Centred.high[Index]) / 2, 0),
  );
  const Input = await Page.locator(
    '[data-transform="position"][data-axis="0"]',
  ).boundingBox();
  await Page.mouse.move(Input.x + Input.width / 2, Input.y + Input.height / 2);
  await Page.mouse.down();
  await Page.mouse.move(
    Input.x + Input.width / 2 + 25,
    Input.y + Input.height / 2,
    { steps: 5 },
  );
  await Page.mouse.up();
  await Ready();
  Near(
    ((await Snapshot()).figures[0].low[0] +
      (await Snapshot()).figures[0].high[0]) /
      2,
    0.25,
  );
  Assert.ok(Errors.length === 0, Errors.join("\n"));
  Pass(
    "Transform XYZ cells type and scrub, rotate/scale about the selection centre, uniformly scale and reset each row",
  );

  await Page.locator("#cad-canvas").focus();
  await Page.keyboard.press("Tab");
  Assert.equal(await Page.locator("#construct-menu").isVisible(), true);
  for (const [Section, Amount] of [
    ["Reference", 1],
    ["Sketch Draw", 11],
    ["Solid", 5],
    ["Surface", 5],
  ]) {
    await Page.locator(`[data-section="${Section}"]`).click();
    Assert.equal(await Page.locator("[data-construct]").count(), Amount);
  }
  await Page.keyboard.press("Escape");
  Assert.equal(await Page.locator("#construct-menu").isVisible(), false);
  await Page.locator("#construct-button").click();
  await Page.locator('[data-section="Surface"]').click();
  await Page.locator('[data-construct="21"]').click();
  await Ready();
  Assert.equal((await Snapshot()).figures.at(-1).classification, 1);
  await Page.locator("#construct-button").click();
  await Page.keyboard.press("c");
  await Ready();
  Assert.equal((await Snapshot()).figures.at(-1).classification, 0);
  Pass(
    "Native catalogue sections/counts, Tab/Escape, accelerator and surface/sketch placement work in the browser",
  );

  for (const Width of [900, 390]) {
    await Page.setViewportSize({ width: Width, height: 900 });
    await Page.waitForTimeout(350);
    await Ready();
    await Page.locator("#construct-button").click();
    const Menu = await Page.locator("#construct-menu").boundingBox();
    Assert.ok(Menu.x >= 0 && Menu.x + Menu.width <= Width);
    await Page.keyboard.press("Escape");
    Assert.ok(
      await Page.evaluate(
        () => document.documentElement.scrollWidth <= innerWidth + 1,
      ),
    );
  }
  Assert.deepEqual(Errors, []);
  Pass(
    "Construct popup fits desktop/mobile; no JavaScript or browser console errors",
  );
  await Page.setViewportSize({ width: 1600, height: 1000 });
  await Page.waitForTimeout(350);
  await Ready();
  await Run([
    "reset",
    "rect (-2,-1.4) (2,1.4) --radius=.25 --name=Profile",
    "extrude Profile .24",
    "hide Profile",
    "cylinder (0,0,.24) .66 .85 --name=Housing",
    "cylinder (0,0,.18) .32 1.1 --name=Bore",
    "boolean subtract Housing -- Bore --name=Socket",
    "matcap all steel",
    "show cages off",
    "dim off",
    "gizmo on",
    "gizmo combined",
    "select Extrusion",
    "view iso",
    "view fit",
  ]);
  await Page.locator('[data-inspector="transform"]').click();
  if (await Page.locator("#console-panel").isVisible())
    await Page.locator("#diagnostics-button").click();
  await Page.mouse.move(10, 10);
  await Ready();
  if (process.env.BROWSER_ARTIFACTS) {
    await mkdir(process.env.BROWSER_ARTIFACTS, { recursive: true });
    await Page.waitForTimeout(4500);
    await Ready();
    await Page.screenshot({
      path: `${process.env.BROWSER_ARTIFACTS}/SolidArcTransform.png`,
    });
    await Page.locator("#construct-button").click();
    await Page.locator('[data-section="Sketch Draw"]').click();
    await Page.screenshot({
      path: `${process.env.BROWSER_ARTIFACTS}/SolidArcConstruct.png`,
    });
  }
  console.log(`${Count} camera, gizmo and layout checks passed.`);
} finally {
  if (Errors.length) console.error(Errors);
  await Browser.close();
}
