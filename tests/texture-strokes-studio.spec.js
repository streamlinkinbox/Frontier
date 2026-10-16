import { test, expect } from "@playwright/test";
test.describe.configure({ timeout: 150000 });
test.use({ actionTimeout: 20000 });
const row = (page, id) => page.locator(`[data-layer-id="${id}"]`);
function errorsOf(page) {
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (
      m.type() === "error" ||
      /Invalid hook call|Multiple instances of Three/.test(m.text())
    )
      errors.push(m.text());
  });
  return errors;
}
async function open(page) {
  await page.goto("/?studio=texture");
  await expect(page.locator(".tp-scene-viewport canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
}
async function draft(page) {
  return page.evaluate(() =>
    JSON.parse(localStorage.getItem("alloy-texture-project-v1")),
  );
}
async function source(page) {
  await row(page, "surface-detail")
    .getByRole("button", { name: "Select Surface detail", exact: true })
    .click();
  await page
    .getByRole("group", { name: "Base color source", exact: true })
    .getByRole("button", { name: "Texture", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Paint Base color texture", exact: true })
    .click();
  const canvas = page.getByLabel("Base color texture canvas", { exact: true });
  await expect(canvas).toHaveAttribute("aria-disabled", "false");
  await canvas.scrollIntoViewIfNeeded();
  return canvas;
}
async function stroke(page, canvas, points) {
  await canvas.scrollIntoViewIfNeeded();
  const b = await canvas.boundingBox();
  await page.mouse.move(b.x + b.width * points[0][0], b.y + b.height * points[0][1]);
  await page.mouse.down();
  for (const [x, y] of points.slice(1))
    await page.mouse.move(b.x + b.width * x, b.y + b.height * y, { steps: 5 });
  await page.mouse.up();
}
async function compositePixels(page) {
  return page.evaluate(async () => {
    const img = document.querySelector(
      "figure[aria-label='Layer composite preview'] img",
    );
    if (!img) return 0;
    await img.decode().catch(() => {});
    const c = document.createElement("canvas");
    c.width = img.naturalWidth;
    c.height = img.naturalHeight;
    const ctx = c.getContext("2d");
    ctx.drawImage(img, 0, 0);
    const d = ctx.getImageData(0, 0, c.width, c.height).data;
    let n = 0;
    for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++;
    return n;
  });
}

test("paint strokes are recorded on their layer, listed, replayed after deletion and persisted", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  const canvas = await source(page);
  await stroke(page, canvas, [
    [0.15, 0.2],
    [0.5, 0.45],
    [0.85, 0.75],
  ]);
  await expect
    .poll(async () => (await draft(page))?.layers[0]?.strokes?.length)
    .toBe(1);
  let doc = await draft(page);
  const firstId = doc.layers[0].strokes[0].id;
  expect(doc.layers[0].strokes[0].layerId).toBe("surface-detail");
  expect(doc.layers[0].strokes[0].points.length).toBeGreaterThan(5);
  expect(doc.layers[0].strokes[0]).not.toHaveProperty("base");
  await expect(row(page, "surface-detail")).toContainText("1 stroke");
  const bothPixels = await compositePixels(page);
  expect(bothPixels).toBeGreaterThan(50);

  await stroke(page, canvas, [
    [0.2, 0.8],
    [0.5, 0.55],
    [0.8, 0.25],
  ]);
  await expect
    .poll(async () => (await draft(page))?.layers[0]?.strokes?.length)
    .toBe(2);
  doc = await draft(page);
  const secondId = doc.layers[0].strokes[1].id;
  const twoPixels = await compositePixels(page);
  expect(twoPixels).toBeGreaterThan(bothPixels);

  // Stroke list lives on the layer, newest first, one entry per gesture.
  await expect(page.locator(".tp-stroke-list li")).toHaveCount(2);
  await expect(
    page.getByText("2 recorded", { exact: true }),
  ).toBeVisible();

  // Deleting the FIRST stroke replays the second over a transparent base:
  // its pixels survive, the first stroke's pixels are gone.
  await page
    .getByRole("button", { name: "Delete paint stroke on Surface detail", exact: true })
    .last()
    .click();
  await expect
    .poll(async () => (await draft(page))?.layers[0]?.strokes?.length)
    .toBe(1);
  doc = await draft(page);
  expect(doc.layers[0].strokes[0].id).toBe(secondId);
  expect(doc.layers[0].strokes[0].id).not.toBe(firstId);
  await expect
    .poll(() => compositePixels(page))
    .toBeLessThan(twoPixels);
  expect(await compositePixels(page)).toBeGreaterThan(50);
  await expect(row(page, "surface-detail")).toContainText("1 stroke");

  // The layer system drives the composite: hiding the only painted layer
  // empties it, showing restores it, and the work survives a reload.
  await row(page, "surface-detail")
    .getByRole("button", { name: "Hide Surface detail", exact: true })
    .click();
  await expect.poll(() => compositePixels(page)).toBe(0);
  await row(page, "surface-detail")
    .getByRole("button", { name: "Show Surface detail", exact: true })
    .click();
  await expect.poll(() => compositePixels(page)).toBeGreaterThan(50);
  await page.reload();
  await expect(page.locator(".tp-scene-viewport canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  doc = await draft(page);
  expect(doc.layers[0].strokes.map((s) => s.id)).toEqual([secondId]);
  await expect(row(page, "surface-detail")).toContainText("1 stroke");
  await expect.poll(() => compositePixels(page)).toBeGreaterThan(50);
  expect(errors).toEqual([]);
});

test("deleting the only stroke on an empty channel clears its painted texture and stroke storage", async ({
  page,
}) => {
  const errors = errorsOf(page);
  await open(page);
  // Import a tiny raster as the Base color texture, then paint over it.
  const canvas = await source(page);
  await stroke(page, canvas, [
    [0.2, 0.2],
    [0.8, 0.8],
  ]);
  await expect
    .poll(async () => (await draft(page))?.layers[0]?.strokes?.length)
    .toBe(1);
  const doc = await draft(page);
  // First stroke over an empty channel records no base; the painted texture
  // is stroke-built and its deletion returns the channel to empty.
  await page
    .getByRole("button", { name: "Delete paint stroke on Surface detail", exact: true })
    .click();
  await expect
    .poll(async () => (await draft(page))?.layers[0]?.strokes?.length ?? 0)
    .toBe(0);
  const cleared = await draft(page);
  expect(cleared.layers[0]).not.toHaveProperty("strokes");
  expect(
    cleared.layers[0].channelSettings.baseColor.texture,
  ).toBe(null);
  await expect.poll(() => compositePixels(page)).toBe(0);
  expect(errors).toEqual([]);
});
