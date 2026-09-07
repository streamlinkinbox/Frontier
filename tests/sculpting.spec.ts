import { test, expect } from "@playwright/test";
import { expectSandstone } from "./pixels";

test("all feature brushes match between CPU and GPU and change the actual SDF", async ({
  page,
}) => {
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  const results = await page.evaluate(async () => {
    const [
      { WebGPUBackend },
      { sculptField },
      { makeBrushStamp },
      { DEFAULT_SETTINGS },
    ] = await Promise.all([
      import("/src/engine/WebGPUBackend.ts"),
      import("/src/engine/field.ts"),
      import("/src/engine/brush.ts"),
      import("/src/engine/types.ts"),
    ]);
    window.__frontier!.busy = true;
    const size = { x: 24, y: 12, z: 24 },
      s = { ...DEFAULT_SETTINGS, radius: 9, strength: 0.9 },
      data = new Float32Array(24 * 12 * 24 * 4);
    for (let z = 0; z < 24; z++)
      for (let y = 0; y < 12; y++)
        for (let x = 0; x < 24; x++)
          data[((z * 12 + y) * 24 + x) * 4] = Math.max(
            -24,
            Math.min(
              32,
              -10 + (y + 0.5) * 4 - 8 + (-48 + (x + 0.5) * 4) * 0.125,
            ),
          );
    const gpu = new WebGPUBackend(document.createElement("canvas"), "safe");
    Object.assign(gpu.size, size);
    const stamp = makeBrushStamp([0, 8, 0], [0, 1, 0], [1, 0, 0], 234);
    const results = [];
    try {
      await gpu.initialize(s);
      for (const tool of [
        "ridges",
        "flatten",
        "crack",
        "crevice",
        "boulder",
      ] as const) {
        const cpu = data.slice();
        sculptField(cpu, size, [0, 8, 0], tool, s, stamp);
        gpu.reset();
        gpu.writeVolume(data);
        gpu.sculpt([0, 8, 0], tool, s, stamp);
        const out = await gpu.readVolume();
        let difference = 0,
          changes = 0;
        for (let i = 0; i < data.length; i += 4) {
          difference = Math.max(difference, Math.abs(cpu[i] - out[i]));
          if (Math.abs(out[i] - data[i]) > 0.001) changes++;
        }
        results.push({
          tool,
          difference,
          changes,
          finite: out.every(Number.isFinite),
        });
      }
    } finally {
      gpu.dispose();
      window.__frontier!.busy = false;
    }
    return results;
  });
  for (const r of results) {
    expect(r.finite, r.tool).toBe(true);
    expect(r.changes, r.tool).toBeGreaterThan(5);
    expect(r.difference, r.tool).toBeLessThan(0.035);
  }
});

test("WebGL planar strokes stay locked, sculpting is undoable, and boulders do not pile up while held", async ({
  page,
}, testInfo) => {
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  for (const name of ["Flatten", "Ridges", "Cracks", "Crevice", "Boulder"])
    await expect(
      page.getByRole("button", { name, exact: false }).first(),
    ).toBeVisible();
  await page.getByRole("button", { name: /^Flatten/ }).click();
  await page.getByRole("button", { name: "Horizontal", exact: true }).click();
  const box = (await page.locator(".viewport-canvas").boundingBox())!;
  await page.mouse.move(box.x + box.width * 0.33, box.y + box.height * 0.61);
  await page.waitForFunction(() => !!(window.__frontier as any).brush);
  await page.evaluate(async () => {
    (window as any).beforePlanar =
      await window.__frontier!.backend.readVolume();
  });
  await page.mouse.down();
  await page.waitForFunction(
    () => (window.__frontier as any).mode === "sculpt",
  );
  const plane = await page.evaluate(() =>
    (window.__frontier as any).stroke.origin.slice(),
  );
  await page.mouse.move(box.x + box.width * 0.38, box.y + box.height * 0.63, {
    steps: 8,
  });
  await page.waitForTimeout(300);
  expect(
    await page.evaluate(() => (window.__frontier as any).stroke.origin),
  ).toEqual(plane);
  await page.mouse.up();
  await page.getByRole("button", { name: "Undo", exact: true }).click();
  await page.waitForFunction(() => !window.__frontier!.busy);
  expect(
    await page.evaluate(async () => {
      const a = await window.__frontier!.backend.readVolume();
      return a.every((v, i) => v === (window as any).beforePlanar[i]);
    }),
  ).toBe(true);
  await page.getByRole("button", { name: /^Boulder/ }).click();
  await page.mouse.move(box.x + box.width * 0.33, box.y + box.height * 0.61);
  await page.waitForFunction(() => !!(window.__frontier as any).brush);
  await page.mouse.down();
  await page.waitForFunction(
    () =>
      (window.__frontier as any).dabSequence === 1 &&
      (window.__frontier as any).mode === "sculpt",
  );
  await page.waitForTimeout(350);
  expect(
    await page.evaluate(() => (window.__frontier as any).dabSequence),
  ).toBe(1);
  await page.mouse.up();
  expectSandstone(
    await page
      .locator(".viewport-canvas")
      .screenshot({ path: testInfo.outputPath("sculpted-boulder.png") }),
  );
  await page.getByRole("button", { name: /Rain cuts/ }).click();
  expect(
    await page.evaluate(() => ({
      thermal: window.__frontier!.settings.thermal,
      channeling: window.__frontier!.settings.channeling,
    })),
  ).toEqual({ thermal: 0, channeling: 1 });
  await page.getByRole("button", { name: /Wind streaks/ }).click();
  await expect(
    page.getByRole("slider", { name: "Wind abrasion" }),
  ).toBeVisible();
  expect(
    await page.evaluate(() => window.__frontier!.settings.windErosion),
  ).toBe(0.9);
});

test("rain cuts and wind streaks alter clay-shaded canyon geometry on WebGL", async ({
  page,
}, testInfo) => {
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.pause();
    e.busy = true;
    await e.backend.sync();
    e.camera.lookAt([-7, 10, 25], [9, 12, 5]);
    e.update({ ...e.settings, water: false, view: "clay", autoPreview: false });
    (window as any).reviewBase = await e.backend.readVolume();
    e.backend.render({ ...(e as any).makeFrame(), time: 0 });
    await e.backend.sync();
  });
  await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("clay-before.png") });
  const rain = await page.evaluate(async () => {
    const e = window.__frontier!;
    const s = {
      ...e.settings,
      rainfall: 0.95,
      erosion: 1,
      channeling: 1,
      cohesion: 0.05,
      thermal: 0,
      windErosion: 0,
      sediment: 0.85,
      settling: 0.2,
      evaporation: 0.12,
    };
    e.update(s);
    e.backend.step(s, 80);
    await e.backend.sync();
    const data = await e.backend.readVolume();
    let cleared = 0;
    for (let i = 0; i < data.length; i += 4)
      if ((window as any).reviewBase[i] < 0 && data[i] >= 0) cleared++;
    e.backend.render({ ...(e as any).makeFrame(), time: 0 });
    await e.backend.sync();
    return cleared;
  });
  expect(rain).toBeGreaterThan(100);
  await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("clay-rain-cuts.png") });
  const wind = await page.evaluate(async () => {
    const e = window.__frontier!;
    e.backend.writeVolume((window as any).reviewBase);
    const s = {
      ...e.settings,
      rainfall: 0,
      erosion: 0,
      thermal: 0,
      windErosion: 1,
      windDirection: 0,
    };
    e.update(s);
    e.backend.step(s, 80);
    await e.backend.sync();
    const data = await e.backend.readVolume();
    let cleared = 0;
    for (let i = 0; i < data.length; i += 4)
      if ((window as any).reviewBase[i] < 0 && data[i] >= 0) cleared++;
    e.backend.render({ ...(e as any).makeFrame(), time: 0 });
    await e.backend.sync();
    return cleared;
  });
  expect(wind).toBeGreaterThan(50);
  await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("clay-wind-streaks.png") });
  await page.evaluate(() => {
    window.__frontier!.busy = false;
  });
});
