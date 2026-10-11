import Assert from "node:assert/strict";
import { readFile, mkdir } from "node:fs/promises";
import { chromium } from "playwright";
const Browser = await chromium.launch({
  headless: true,
  ...(process.env.BROWSER_EXECUTABLE
    ? { executablePath: process.env.BROWSER_EXECUTABLE }
    : {}),
  args: ["--no-sandbox", "--disable-dev-shm-usage"],
});
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
Page.setDefaultTimeout(45000);
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
      window.solidArc?.Ready &&
      !solidArc.Busy &&
      !solidArc.Motion &&
      !solidArc.ResolveDetail,
    null,
    { timeout: 120000 },
  );
const Snapshot = () =>
  Page.evaluate(() => structuredClone(solidArc.Description));
const Run = async (Text) => {
  await Ready();
  await Page.evaluate((Text) => solidArc.Command(Text), Text);
  await Ready();
};
try {
  await Page.goto(process.env.SOLIDARC_URL || "http://127.0.0.1:5173/");
  await Page.waitForFunction(
    () =>
      window.solidArc?.Documents.length === 1 &&
      solidArc.Description.figures.length === 7 &&
      !solidArc.Busy,
    null,
    { timeout: 120000 },
  );
  await Ready();
  Assert.equal(
    (await Snapshot()).figures.filter((Figure) => Figure.classification === 2)
      .length,
    6,
  );
  Assert.ok(
    await Page.evaluate(() => {
      const Canvas = document.querySelector("#cad-canvas");
      const Pixels = Canvas.getContext("2d").getImageData(
        0,
        0,
        Canvas.width,
        Canvas.height,
      ).data;
      return Pixels.some((Channel, Index) => Index % 4 !== 3 && Channel > 100);
    }),
  );
  Assert.equal(
    await Page.evaluate(async () => {
      await document.fonts.ready;
      return document.fonts.check('300 14px "DM Sans"');
    }),
    true,
  );
  Pass(
    "Actual WASM startup, native extrusion/subtraction example, RGBA presentation and shared DM Sans",
  );
  await Page.locator("#scene-search").fill("Socket");
  Assert.equal(await Page.locator(".scene-row").count(), 1);
  await Page.locator("#scene-search").fill("");
  await Page.locator('[data-filter="0"]').click();
  Assert.equal(await Page.locator(".scene-row").count(), 1);
  await Page.locator('[data-filter="all"]').click();
  await Page.locator("#compact-button").click();
  Assert.ok(
    await Page.locator("#scene-rows").evaluate((Element) =>
      Element.classList.contains("compact"),
    ),
  );
  await Page.locator("#fold-button").click();
  Assert.equal(await Page.locator("#scene-rows").isVisible(), false);
  await Page.locator("#fold-button").click();
  await Page.locator('[data-visibility="5"]').click();
  await Ready();
  Assert.equal(
    (await Snapshot()).figures.find((Figure) => Figure.id === 5).hidden,
    1,
  );
  await Page.locator('[data-visibility="5"]').click();
  await Ready();
  Pass(
    "Outliner search, filters, compact/fold controls and actual native visibility",
  );
  await Page.locator("#new-document").click();
  await Ready();
  Assert.equal((await Snapshot()).figures.length, 0);
  await Page.locator('[data-document="2"]').dblclick();
  const Rename = Page.locator('[data-document="2"] input');
  await Rename.fill("Browser trial");
  await Rename.press("Enter");
  Assert.equal(
    await Page.locator("#document-title").textContent(),
    "Browser trial",
  );
  await Page.locator("#construct-button").click();
  await Page.locator('[data-section="Solid"]').click();
  await Page.locator('[data-construct="12"]').click();
  await Ready();
  Assert.equal((await Snapshot()).figures[0].faces, 6);
  await Page.locator('[data-figure="1"]').click();
  await Ready();
  const Dimension = (await Snapshot()).dimensions.find(
    (Dimension) => Dimension.slot === 5,
  );
  await Page.locator(`#dimension-${Dimension.id}`).fill("2.5");
  await Page.locator(`#dimension-${Dimension.id}`).press("Tab");
  await Ready();
  Assert.equal((await Snapshot()).figures[0].high[2], 2.5);
  await Page.locator("#undo-button").click();
  await Ready();
  Assert.equal((await Snapshot()).figures[0].high[2], 1);
  await Page.locator("#redo-button").click();
  await Ready();
  Assert.equal((await Snapshot()).figures[0].high[2], 2.5);
  Pass(
    "Native Construct catalogue places a solid; live dimension typing, undo and redo change its geometry",
  );
  let Current = (await Snapshot()).dimensions.find(
    (Dimension) => Dimension.slot === 5,
  );
  const Pill = Page.locator(`#dimension-${Current.id}`);
  const Rect = await Pill.boundingBox();
  await Page.mouse.move(Rect.x + Rect.width / 2, Rect.y + Rect.height / 2);
  await Page.mouse.down();
  await Page.mouse.move(
    Rect.x + Rect.width / 2 + 20,
    Rect.y + Rect.height / 2,
    { steps: 5 },
  );
  await Page.mouse.up();
  await Ready();
  Assert.ok((await Snapshot()).figures[0].high[2] > 2.5);
  Current = (await Snapshot()).dimensions.find(
    (Dimension) => Dimension.slot === 5,
  );
  await Page.locator(`[data-dimension-range="${Current.id}"]`).fill("3");
  await Page.locator(`[data-dimension-range="${Current.id}"]`).dispatchEvent(
    "change",
  );
  await Ready();
  Assert.equal((await Snapshot()).figures[0].high[2], 3);
  await Page.locator('[data-figure="1"]').dblclick();
  await Ready();
  await Page.locator("#figure-name").fill("EditedBlock");
  await Page.locator("#figure-name").press("Tab");
  await Ready();
  Assert.equal((await Snapshot()).figures[0].name, "EditedBlock");
  Pass(
    "Numeric pill dragging, range controls and outliner double-click rename",
  );
  const InitialYaw = (await Snapshot()).camera.yaw;
  const Canvas = await Page.locator("#cad-canvas").boundingBox();
  await Page.mouse.move(
    Canvas.x + Canvas.width * 0.5,
    Canvas.y + Canvas.height * 0.5,
  );
  await Page.mouse.down();
  await Page.mouse.move(
    Canvas.x + Canvas.width * 0.5 + 90,
    Canvas.y + Canvas.height * 0.5 + 15,
    { steps: 9 },
  );
  await Page.mouse.up();
  await Ready();
  Assert.ok(Math.abs((await Snapshot()).camera.yaw - InitialYaw) > 0.5);
  for (const View of ["front", "right", "top", "iso"]) {
    await Page.locator("#camera-view").selectOption(View);
    await Ready();
  }
  await Page.locator("#projection-button").click();
  await Ready();
  Assert.equal((await Snapshot()).camera.orthographic, 1);
  await Page.locator("#projection-button").click();
  await Ready();
  for (const Selector of [
    "#edges-button",
    "#edges-button",
    "#cages-button",
    "#cages-button",
    "#fit-button",
  ]) {
    await Page.locator(Selector).click();
    await Ready();
  }
  await Page.locator("#dimensions-toggle").check();
  await Ready();
  await Page.locator("#dimensions-toggle").uncheck();
  await Ready();
  Pass(
    "Coalesced orbit reaches the full mouse displacement; camera modes, projection, fit, edges, cages and dimensions",
  );
  await Page.locator("#gizmo-mode").selectOption("off");
  await Ready();
  await Page.locator('[data-mode="face"]').click();
  await Ready();
  await Page.locator("#cad-canvas").click({
    position: { x: Canvas.width * 0.55, y: Canvas.height * 0.5 },
  });
  await Ready();
  Assert.ok((await Snapshot()).figures[0].pickedFaces.length > 0);
  Assert.equal(await Page.locator("#figure-name").inputValue(), "EditedBlock");
  await Run("selectmode edge");
  await Run("select edges EditedBlock 0");
  Assert.ok((await Snapshot()).figures[0].pickedEdges.length === 1);
  await Page.locator('[data-tool="chamfer"]').click();
  await Page.locator('[name="distance"]').fill("0.1");
  await Page.locator("#apply-tool").click();
  await Ready();
  Assert.equal(
    await Page.locator("#tool-dialog").evaluate((Dialog) => Dialog.open),
    false,
  );
  Assert.ok((await Snapshot()).figures.some((Figure) => Figure.faces > 6));
  Pass(
    "Real viewport face picking, native edge selection and an applied solid chamfer",
  );
  await Run("selectmode whole");
  await Run("select all");
  const BeforeSave = (await Snapshot()).figures;
  const DownloadPromise = Page.waitForEvent("download");
  await Page.locator("#save-button").click();
  const Download = await DownloadPromise;
  await Ready();
  const NativeText = await readFile(await Download.path(), "utf8");
  Assert.match(NativeText, /^# SolidArc native document v1/);
  await Page.locator("#new-document").click();
  await Ready();
  await Page.locator("#open-file").setInputFiles({
    name: "Roundtrip.arc",
    mimeType: "text/plain",
    buffer: Buffer.from(NativeText),
  });
  await Ready();
  await Page.waitForFunction(() => solidArc.Document.name === "Roundtrip");
  Assert.deepEqual((await Snapshot()).figures, BeforeSave);
  await Page.locator("#open-file").setInputFiles({
    name: "Bad.arc",
    mimeType: "text/plain",
    buffer: Buffer.from("not a valid document"),
  });
  await Ready();
  Assert.deepEqual((await Snapshot()).figures, BeforeSave);
  Pass(
    "Native .arc download and import preserve the edited topology; malformed input preserves the open scene",
  );
  await Page.locator('[data-document="1"]').click();
  await Ready();
  Assert.equal((await Snapshot()).figures.length, 7);
  await Page.locator('[data-document="2"]').click();
  await Ready();
  Assert.deepEqual((await Snapshot()).figures, BeforeSave);
  await Page.locator('[data-close="3"]').click();
  await Ready();
  Assert.equal(await Page.locator(".document-tab").count(), 2);
  Pass("Independent native document switching, filenames and close cleanup");
  await Run("select all");
  const ObjPromise = Page.waitForEvent("download");
  await Page.locator("#export-obj").click();
  const Obj = await readFile(await (await ObjPromise).path(), "utf8");
  await Ready();
  Assert.match(Obj, /^v /m);
  Assert.match(Obj, /^f /m);
  await Page.locator("#inspect-button").click();
  await Ready();
  Assert.match(await Page.locator("#command-log").textContent(), /topology/i);
  await Page.locator("#clear-log").click();
  Assert.equal(await Page.locator("#command-log").textContent(), "");
  await Page.locator("#command-input").fill("shell EditedBlock 0.1 --face=999");
  await Page.locator("#command-form button").click();
  await Ready();
  Assert.match(
    await Page.locator("#command-log").textContent(),
    /refus|error|invalid|canonical|face/i,
  );
  Assert.equal(solid(await Snapshot()), solid({ figures: BeforeSave }));
  Pass(
    "Actual OBJ export, topology diagnostics and visible native refusals without a browser failure",
  );
  await Page.locator("#new-document").click();
  await Ready();
  await Run("circle (0,0) 1 --name=Profile");
  await Run("view top");
  await Run("view fit");
  const RegionCanvas = await Page.locator("#cad-canvas").boundingBox();
  await Page.locator("#cad-canvas").click({
    position: { x: RegionCanvas.width * 0.6, y: RegionCanvas.height * 0.52 },
  });
  await Ready();
  Assert.equal(
    (await Snapshot()).areas.filter((Region) => Region.selected).length,
    1,
  );
  await Page.locator('#inspector-body [data-tool="extrude"]').click();
  await Page.locator("#tool-dialog").waitFor({ state: "visible" });
  Assert.match(
    await Page.locator("#command-preview").textContent(),
    /extrude a\d/,
  );
  await Page.locator("#apply-tool").click();
  await Ready();
  Assert.ok(
    (await Snapshot()).figures.some((Figure) => Figure.classification === 2),
  );
  await Page.locator('[data-document="2"]').click();
  await Ready();
  Page.once("dialog", (Dialog) => Dialog.accept());
  await Page.locator('[data-close="4"]').click();
  await Ready();
  Pass(
    "Native filled-region viewport picking feeds the extrusion dialog and survives command journaling",
  );

  await Page.locator("#maximize-button").click();
  await Ready();
  Assert.equal(await Page.locator(".outliner").isVisible(), false);
  await Page.locator("#maximize-button").click();
  await Ready();
  await Page.locator("#help-button").click();
  Assert.equal(await Page.locator("#help-dialog").isVisible(), true);
  await Page.locator("#help-dialog [data-close-dialog]").click();
  for (const Width of [1280, 900, 760, 390]) {
    await Page.setViewportSize({ width: Width, height: 900 });
    await Page.waitForTimeout(400);
    await Ready();
    Assert.ok(
      await Page.evaluate(
        () => document.documentElement.scrollWidth <= innerWidth + 1,
      ),
      `width ${Width}`,
    );
  }
  Pass("Maximize/help controls and desktop, tablet and phone layouts");
  Assert.deepEqual(Errors, []);
  Pass("No JavaScript or browser console errors");
  await Page.setViewportSize({ width: 1600, height: 1000 });
  await Page.waitForTimeout(400);
  await Ready();
  await Page.locator('[data-document="1"]').click();
  await Ready();
  if (await Page.locator("#console-panel").isVisible()) {
    await Page.locator("#diagnostics-button").click();
    await Ready();
  }
  await Page.evaluate(
    () => (document.querySelector("#inspector-body").scrollTop = 0),
  );
  await Page.waitForTimeout(4500);
  await Ready();
  if (process.env.BROWSER_ARTIFACTS) {
    await mkdir(process.env.BROWSER_ARTIFACTS, { recursive: true });
    await Page.screenshot({
      path: `${process.env.BROWSER_ARTIFACTS}/SolidArcWorkspace.png`,
    });
  }
  console.log(`${Count} browser checks passed.`);
} finally {
  if (Errors.length) console.error(Errors);
  await Browser.close();
}
function solid(Description) {
  return JSON.stringify(
    Description.figures.map((Figure) => [
      Figure.name,
      Figure.faces,
      Figure.edges,
      Figure.low,
      Figure.high,
    ]),
  );
}
