import { test, expect } from "@playwright/test";
import { PNG } from "pngjs";

test("Slate-inspired editor has precise value inputs, unclipped menus and a usable narrow layout", async ({
  page,
}, testInfo) => {
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      document.querySelector(".has-error") ||
      (window.__frontier?.backend &&
        !document.querySelector(".viewport-loading")),
  );
  expect(await page.locator(".has-error").count()).toBe(0);
  await page.getByRole("switch", { name: "Live erosion preview" }).click();
  await page
    .getByRole("button", { name: "Edit Rainfall value", exact: true })
    .click();
  await page
    .getByRole("spinbutton", { name: "Rainfall numeric value" })
    .fill("78");
  await page.keyboard.press("Enter");
  expect(await page.evaluate(() => window.__frontier!.settings.rainfall)).toBe(
    0.78,
  );
  await page
    .getByRole("button", { name: "Edit Rainfall value", exact: true })
    .click();
  await page
    .getByRole("spinbutton", { name: "Rainfall numeric value" })
    .fill("22");
  await page.keyboard.press("Escape");
  expect(await page.evaluate(() => window.__frontier!.settings.rainfall)).toBe(
    0.78,
  );
  await page.screenshot({ path: testInfo.outputPath("studio-desktop.png") });
  await page.getByRole("button", { name: /^Carve/ }).click();
  await page
    .getByRole("button", { name: "Renderer options", exact: true })
    .click();
  const menu = page.getByRole("menu");
  await expect(menu).toBeVisible();
  const b = (await menu.boundingBox())!;
  expect(b.x).toBeGreaterThanOrEqual(0);
  expect(b.y).toBeGreaterThanOrEqual(0);
  expect(b.x + b.width).toBeLessThanOrEqual(1440);
  expect(b.y + b.height).toBeLessThanOrEqual(900);
  await page.keyboard.press("ArrowDown");
  expect(
    await page.evaluate(() => document.activeElement?.getAttribute("role")),
  ).toBe("menuitem");
  await page.keyboard.press("Escape");
  await expect(menu).toHaveCount(0);
  expect(await page.evaluate(() => window.__frontier!.tool)).toBe("carve");
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    390,
  );
  await page.getByRole("button", { name: "Toggle sculpting panel" }).click();
  await expect(
    page.getByRole("complementary", { name: "Scene and sculpting tools" }),
  ).toBeVisible();
  await page.screenshot({
    path: testInfo.outputPath("studio-mobile-tools.png"),
  });
  await page.getByRole("button", { name: "Close sculpting drawer" }).click();
  await expect(page.locator(".left-sidebar")).not.toBeVisible();
});

test("river patterns move without tile-like phase repetition and expose independent flow controls", async ({
  page,
}, testInfo) => {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      document.querySelector(".has-error") ||
      (window.__frontier?.backend &&
        !document.querySelector(".viewport-loading")),
  );
  expect(await page.locator(".has-error").count()).toBe(0);
  await page.getByRole("tab", { name: "Environment", exact: true }).click();
  await page
    .getByRole("button", { name: "Edit Current speed value", exact: true })
    .click();
  await page
    .getByRole("spinbutton", { name: "Current speed numeric value" })
    .fill("0.8");
  await page.keyboard.press("Enter");
  expect(
    await page.evaluate(() => window.__frontier!.settings.waterCurrent),
  ).toBe(0.8);
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.pause();
    e.busy = true;
    await e.backend.sync();
    e.camera.lookAt([-9, 5.2, 29], [-1, 3.7, -5]);
    e.update({
      ...e.settings,
      waterLevel: 2.65,
      wind: 0.6,
      waterCurrent: 0.8,
      waterRippleScale: 1.8,
    });
    e.backend.render({ ...(e as any).makeFrame(), time: 1, brush: null });
    await e.backend.sync();
  });
  const first = await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("river-flow-a.png") });
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.backend.render({ ...(e as any).makeFrame(), time: 6, brush: null });
    await e.backend.sync();
  });
  const second = await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("river-flow-b.png") });
  const a = PNG.sync.read(first),
    b = PNG.sync.read(second);
  let changed = 0;
  for (let i = 0; i < a.data.length; i += 4)
    if (
      Math.abs(a.data[i] - b.data[i]) +
        Math.abs(a.data[i + 1] - b.data[i + 1]) +
        Math.abs(a.data[i + 2] - b.data[i + 2]) >
      15
    )
      changed++;
  expect(changed / (a.width * a.height)).toBeGreaterThan(0.005);
  expect(errors).toEqual([]);
});
