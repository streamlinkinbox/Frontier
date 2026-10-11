import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
const Folder = path.dirname(fileURLToPath(import.meta.url)),
  Root = path.resolve(Folder, "../../.."),
  Require = createRequire(
    process.env.FRONTIER_BROWSER_PACKAGE ||
      path.join(Root, "_AgentScratch/Browser/package.json"),
  ),
  { chromium } = Require("playwright");
const Url =
    process.env.FRONTIER_FRACTURE_URL ||
    "http://127.0.0.1:4173/FractureEditor/",
  EditorUrl = new URL("../ProjectZeroEditor/?inspect=sphere", Url).href,
  Proof = path.join(Folder, "Captures"),
  Errors = [],
  Checks = [],
  FontFailures = [];
fs.mkdirSync(Proof, { recursive: true });
const Browser = await chromium.launch({
  executablePath: process.env.FRONTIER_BROWSER_EXECUTABLE || "/tmp/chromium",
  headless: true,
  args: [
    "--no-sandbox",
    "--enable-unsafe-swiftshader",
    "--use-angle=swiftshader",
  ],
});
const Context = await Browser.newContext({
  viewport: { width: 1440, height: 1000 },
  acceptDownloads: true,
});
Context.on("page", (Page) => {
  Page.on("pageerror", (Error) => Errors.push(Error.stack));
  Page.on("console", (Message) => {
    if (Message.type() !== "error") return;
    if (Message.location().url?.includes("fontshare.com"))
      FontFailures.push(Message.text());
    else if (!Message.location().url?.includes("favicon"))
      Errors.push(Message.text());
  });
});
async function Snapshot(Page) {
  return Page.evaluate(() => window.FrontierFracture.snapshot());
}
async function Ready(Page) {
  await Page.waitForFunction(
    () => window.FrontierFracture && !window.FrontierFracture.snapshot().Busy,
  );
}
async function Generate(Page) {
  await Page.locator("#fracture").click();
  await Page.waitForFunction(
    () =>
      window.FrontierFracture.snapshot().Result &&
      !window.FrontierFracture.snapshot().Busy,
  );
  const Reading = await Snapshot(Page);
  assert(Reading.Result.VolumeError < 1e-8);
  assert(Reading.Result.Quality > 0);
  return Reading;
}
async function Capture(Page, Name) {
  await Page.screenshot({ path: path.join(Proof, Name + ".png") });
}
async function ShapeSignature(Page) {
  return Page.evaluate(() =>
    JSON.stringify(window.FrontierFracture.geometry()),
  );
}
try {
  const Editor = await Context.newPage();
  await Editor.goto(EditorUrl);
  await Editor.getByRole("switch", {
    name: "Enable fracture",
    exact: true,
  }).click();
  assert.equal(
    await Editor.getByRole("switch", {
      name: "Enable fracture",
      exact: true,
    }).getAttribute("aria-checked"),
    "true",
  );
  await Editor.locator('[data-card="Fracture"]').scrollIntoViewIfNeeded();
  await Capture(Editor, "ObjectInspector");
  const Wait = Context.waitForEvent("page");
  await Editor.getByRole("button", {
    name: "Expand fracture editor",
    exact: true,
  }).click();
  const Sphere = await Wait;
  await Sphere.waitForLoadState();
  await Ready(Sphere);
  assert.equal((await Snapshot(Sphere)).Owner.Id, "sphere");
  assert.equal((await Snapshot(Sphere)).Owner.Primitive, "sphere");
  assert.equal((await Snapshot(Sphere)).Linked, true);
  assert.equal(await Sphere.locator("[data-material]").count(), 6);
  assert(
    !(await Sphere.locator("body").innerText())
      .toLowerCase()
      .includes("sheet metal"),
  );
  Checks.push(
    "Selected sphere enable → expand; six materials; no metal workflow",
  );
  assert.equal(
    await Sphere.locator(".target-note, .guarantees, .limits").count(),
    0,
  );
  assert(
    !(await Editor.locator(".fracture-card").innerText()).includes(
      "Analytical primitive preview",
    ),
  );
  assert(
    !(await Editor.locator(".fracture-card").innerText()).includes(
      "native execution pending",
    ),
  );
  assert((await Editor.locator(".fracture-diagram polygon").count()) > 20);
  assert((await Sphere.locator("#quality-illustration polygon").count()) > 20);
  const InitialIllustration = await Sphere.locator(
    "#quality-illustration",
  ).innerHTML();
  await Sphere.getByRole("spinbutton", {
    name: "Fragment ceiling value",
    exact: true,
  }).fill("96");
  assert.notEqual(
    await Sphere.locator("#quality-illustration").innerHTML(),
    InitialIllustration,
  );
  await Sphere.getByRole("spinbutton", {
    name: "Fragment ceiling value",
    exact: true,
  }).fill("48");
  await Sphere.getByRole("spinbutton", {
    name: "Minimum span value",
    exact: true,
  }).fill("0.1");
  assert.notEqual(
    await Sphere.locator("#quality-illustration").innerHTML(),
    InitialIllustration,
  );
  await Sphere.getByRole("spinbutton", {
    name: "Minimum span value",
    exact: true,
  }).fill("0.045");
  assert.equal(
    await Sphere.locator("#quality-illustration").innerHTML(),
    InitialIllustration,
  );
  Checks.push(
    "Explanatory blocks removed; shaded fracture illustrations replace wire diagrams and respond to both quality controls",
  );
  const SourceGeometry = await ShapeSignature(Sphere);
  await Sphere.locator('[data-material="wood"]').click();
  assert.equal(await ShapeSignature(Sphere), SourceGeometry);
  await Sphere.locator('[data-material="concrete"]').click();
  const Dynamic = await Generate(Sphere);
  assert(Dynamic.Result.Count > 10);
  await Capture(Sphere, "Sphere");
  Checks.push(
    "Material changes retain selected-object geometry; dynamic fracture is closed and volume preserving",
  );
  await Sphere.locator("#wire").click();
  assert.equal(
    await Sphere.locator("#wire").getAttribute("aria-pressed"),
    "true",
  );
  await Sphere.locator("#separation").fill("36");
  await Capture(Sphere, "SphereTopology");
  await Sphere.locator("#assemble").click();
  assert.equal(await Sphere.locator("#separation").inputValue(), "0");
  await Sphere.locator("#wire").click();
  Checks.push(
    "Wireframe and separated inspection; reassembly restores original coordinates",
  );
  await Sphere.locator("#baked-mode").click();
  assert(await Sphere.locator("#fracture").isDisabled());
  await Sphere.locator("#bake").click();
  await Sphere.waitForFunction(
    () =>
      window.FrontierFracture.snapshot().Baked &&
      !window.FrontierFracture.snapshot().Busy,
  );
  const StoredGeometry = await ShapeSignature(Sphere),
    Signature = (await Snapshot(Sphere)).Signature;
  await Sphere.locator(".bake-card").scrollIntoViewIfNeeded();
  await Capture(Sphere, "Baked");
  await Generate(Sphere);
  assert((await Snapshot(Sphere)).Result.Replay);
  assert.equal(await ShapeSignature(Sphere), StoredGeometry);
  const Download = Sphere.waitForEvent("download");
  await Sphere.locator("#export").click();
  const Export = await Download,
    Content = JSON.parse(fs.readFileSync(await Export.path(), "utf8"));
  assert.equal(Content.Owner.Id, "sphere");
  assert.equal(Content.NativeCompatible, false);
  assert(Content.Geometry.length > 1);
  assert.equal(Content.Signature, Signature);
  Checks.push(
    "Baked mode refuses missing geometry; IndexedDB bake/replay and actual JSON download agree",
  );
  await Sphere.reload();
  await Sphere.waitForFunction(() => window.FrontierFracture?.snapshot().Baked);
  await Generate(Sphere);
  assert.equal(await ShapeSignature(Sphere), StoredGeometry);
  await Editor.waitForFunction(() =>
    document
      .querySelector(".fracture-card")
      .textContent.includes("Browser bake ready"),
  );
  Checks.push(
    "Stored fragment geometry survives reopening; receipt returns to the object inspector",
  );
  await Editor.getByRole("spinbutton", { name: "Scale X", exact: true }).fill(
    "1.4",
  );
  await Sphere.waitForFunction(
    () => window.FrontierFracture.snapshot().Owner.Scale[0] === 1.4,
  );
  assert(!(await Snapshot(Sphere)).Baked);
  assert(await Sphere.locator("#fracture").isDisabled());
  assert.deepEqual((await Snapshot(Sphere)).Bounds, [1.4, 1, 1]);
  assert(
    (await Sphere.locator("#bake-status").textContent()).includes("stale"),
  );
  await Editor.getByRole("spinbutton", { name: "Scale X", exact: true }).fill(
    "1",
  );
  await Sphere.waitForFunction(() => window.FrontierFracture.snapshot().Baked);
  Checks.push(
    "Object scale is applied; changing source geometry invalidates baked replay",
  );
  await Sphere.locator('[data-setting="Seed"][type="number"]').fill("93");
  assert(!(await Snapshot(Sphere)).Baked);
  await Sphere.locator("#dynamic-mode").click();
  await Sphere.locator('[data-material="wood"]').click();
  await Editor.waitForFunction(
    () =>
      JSON.parse(localStorage.getItem("Frontier.Fracture.v2:sphere")).Settings
        .Material === "wood",
  );
  await Editor.goto(EditorUrl.replace("sphere", "cube"));
  await Editor.getByRole("switch", {
    name: "Enable fracture",
    exact: true,
  }).click();
  const CubeWait = Context.waitForEvent("page");
  await Editor.getByRole("button", {
    name: "Expand fracture editor",
    exact: true,
  }).click();
  const Cube = await CubeWait;
  await Cube.waitForLoadState();
  await Ready(Cube);
  assert.equal((await Snapshot(Cube)).Owner.Id, "cube");
  assert.equal((await Snapshot(Cube)).Settings.Material, "concrete");
  assert.equal((await Snapshot(Cube)).Baked, false);
  await Cube.locator('[data-material="glass"]').click();
  assert.equal((await Snapshot(Sphere)).Settings.Material, "wood");
  assert.equal((await Snapshot(Sphere)).Settings.Seed, 93);
  await Generate(Cube);
  await Capture(Cube, "Cube");
  Checks.push(
    "Cube and sphere retain independent materials, seeds and stored geometry",
  );
  await Editor.getByRole("switch", {
    name: "Enable fracture",
    exact: true,
  }).click();
  await Cube.waitForFunction(
    () => !window.FrontierFracture.snapshot().Settings.Enabled,
  );
  assert(await Cube.locator("#fracture").isDisabled());
  assert(await Cube.locator("#bake").isDisabled());
  assert(await Cube.locator('[data-material="wood"]').isDisabled());
  Checks.push(
    "Disabling fracture in the inspector disables its already-open editor",
  );
  await Editor.close();
  await Sphere.locator('[data-material="rock"]').click();
  const Reopened = await Context.newPage();
  await Reopened.goto(EditorUrl);
  await Reopened.getByRole("switch", {
    name: "Enable fracture",
    exact: true,
  }).waitFor();
  assert.equal(
    await Reopened.getByRole("switch", {
      name: "Enable fracture",
      exact: true,
    }).getAttribute("aria-checked"),
    "true",
  );
  assert.equal((await Snapshot(Sphere)).Settings.Material, "rock");
  const Persisted = await Reopened.evaluate(
    () =>
      Object.keys(localStorage)
        .filter((Key) => !Key.startsWith("Frontier.Fracture"))
        .map((Key) => {
          try {
            return JSON.parse(localStorage.getItem(Key));
          } catch {
            return null;
          }
        })
        .find((Record) => Record?.Values?.sphere)?.Values.sphere.Fracture,
  );
  assert.equal(Persisted.Material, "rock");
  Checks.push(
    "Reopening the main editor preserves edits made while it was closed",
  );
  // Duplicates own a new scene ID and cannot inherit another ID's baked receipt.
  await Reopened.locator(".outliner-row.selected").click({ button: "right" });
  await Reopened.getByRole("button", {
    name: "Duplicate",
    exact: true,
  }).click();
  const DuplicateWait = Context.waitForEvent("page");
  await Reopened.getByRole("button", {
    name: "Expand fracture editor",
    exact: true,
  }).click();
  const Duplicate = await DuplicateWait;
  await Duplicate.waitForLoadState();
  await Ready(Duplicate);
  assert.notEqual((await Snapshot(Duplicate)).Owner.Id, "sphere");
  assert.equal((await Snapshot(Duplicate)).Owner.Primitive, "sphere");
  assert.equal((await Snapshot(Duplicate)).Settings.Material, "rock");
  assert.equal((await Snapshot(Duplicate)).Settings.Baked, undefined);
  assert.equal((await Snapshot(Duplicate)).Baked, false);
  await Reopened.locator(".outliner-row.selected").click({ button: "right" });
  await Reopened.getByRole("button", {
    name: "Disable fracture",
    exact: true,
  }).click();
  await Duplicate.waitForFunction(
    () => !window.FrontierFracture.snapshot().Settings.Enabled,
  );
  await Reopened.locator(".outliner-row.selected").click({ button: "right" });
  await Reopened.getByRole("button", {
    name: "Enable fracture",
    exact: true,
  }).click();
  await Duplicate.waitForFunction(
    () => window.FrontierFracture.snapshot().Settings.Enabled,
  );
  await Reopened.locator(".outliner-row.selected").click({ button: "right" });
  await Reopened.getByRole("button", {
    name: "Remove from HTML scene",
    exact: true,
  }).click();
  await Duplicate.waitForFunction(
    () => window.FrontierFracture.snapshot().Owner.Removed,
  );
  assert(await Duplicate.locator("#fracture").isDisabled());
  assert((await Snapshot(Sphere)).Settings.Enabled);
  Checks.push(
    "Duplicate ownership, outliner enable/disable, and removal are isolated to the correct object",
  );
  await Sphere.locator("#clear-bake").click();
  await Sphere.waitForFunction(
    () =>
      !window.FrontierFracture.snapshot().Receipt &&
      !window.FrontierFracture.snapshot().Busy,
  );
  assert.equal((await Snapshot(Cube)).Baked, false);
  Checks.push("Clearing a bake only removes that object’s fragment receipt");
  await Reopened.getByRole("button", {
    name: "Outliner menu",
    exact: true,
  }).click();
  await Reopened.getByRole("button", {
    name: "Restore native baseline",
    exact: true,
  }).click();
  await Sphere.waitForFunction(
    () => !window.FrontierFracture.snapshot().Settings.Enabled,
  );
  await Reopened.goto(EditorUrl);
  assert.equal(
    await Reopened.getByRole("switch", {
      name: "Enable fracture",
      exact: true,
    }).getAttribute("aria-checked"),
    "false",
  );
  Checks.push(
    "Restoring the native baseline does not resurrect old per-object fracture settings on reload",
  );
  const Specimen = await Context.newPage();
  await Specimen.goto(Url);
  await Ready(Specimen);
  for (const [Primitive, Material, Name] of [
    ["pane", "glass", "GlassPane"],
    ["pane", "tempered", "TemperedGlass"],
    ["beam", "wood", "Wood"],
    ["rock", "rock", "Stone"],
    ["cube", "concrete", "Concrete"],
    ["cube", "plastic", "Plastic"],
  ]) {
    await Specimen.locator("#primitive").selectOption(Primitive);
    await Specimen.locator('[data-material="' + Material + '"]').click();
    await Specimen.locator("#separation").fill("18");
    const Reading = await Generate(Specimen);
    assert(Reading.Result.Count > 1);
    await Capture(Specimen, Name);
    if (Material === "glass") {
      await Specimen.locator("#assemble").click();
      await Capture(Specimen, "GlassReassembled");
    }
    Checks.push(
      Name +
        ": closed fragments, occupied volume " +
        ((1 - Reading.Result.VolumeError) * 100).toFixed(6) +
        "%",
    );
  }
  await Specimen.locator("#primitive").selectOption("sphere");
  await Specimen.locator('[data-material="glass"]').click();
  await Specimen.locator("#viewport canvas").click({
    position: { x: 440, y: 330 },
    modifiers: ["Shift"],
  });
  assert.notDeepEqual(
    [
      (await Snapshot(Specimen)).Settings.X,
      (await Snapshot(Specimen)).Settings.Y,
      (await Snapshot(Specimen)).Settings.Z,
    ],
    [0, 0, 0],
  );
  await Specimen.locator("#center-impact").click();
  Checks.push(
    "Shift-click places an actual surface impact; centre reset works",
  );
  for (const Width of [1024, 390]) {
    await Specimen.setViewportSize({ width: Width, height: 900 });
    assert(
      await Specimen.evaluate(
        () => document.documentElement.scrollWidth <= innerWidth,
      ),
    );
    await Generate(Specimen);
    await Capture(Specimen, Width === 390 ? "Narrow" : "Medium");
  }
  Checks.push("1024 px and 390 px layouts have no horizontal overflow");
  const Missing = await Context.newPage();
  await Missing.goto(Url + "?object=missing");
  await Ready(Missing);
  assert(!(await Snapshot(Missing)).Supported);
  assert(await Missing.locator("#fracture").isDisabled());
  const Torus = await Context.newPage();
  await Torus.goto(Url);
  await Torus.evaluate(() =>
    localStorage.setItem(
      "Frontier.Fracture.v2:torus",
      JSON.stringify({
        Owner: {
          Id: "torus",
          Name: "Torus",
          Primitive: "torus",
          Scale: [1, 1, 1],
        },
        Settings: { Enabled: true },
      }),
    ),
  );
  await Torus.goto(Url + "?object=torus");
  await Ready(Torus);
  assert(!(await Snapshot(Torus)).Supported);
  assert(
    (await Torus.locator("#unsupported").textContent()).includes(
      "hole will not be filled",
    ),
  );
  Checks.push(
    "Missing owners and concave torus geometry are refused without substituting a box / convex hull",
  );
  assert.deepEqual(Errors, []);
  console.log("Browser checks passed:", Checks.length);
} catch (Error) {
  Errors.push(Error.stack);
  console.error(Error);
  process.exitCode = 1;
} finally {
  fs.writeFileSync(
    path.join(Proof, "Checks.json"),
    JSON.stringify({ Revision: 2, Checks, FontFailures, Errors }, null, 2) +
      "\n",
  );
  await Browser.close();
}
