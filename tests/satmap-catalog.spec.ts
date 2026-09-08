import { test, expect, type Page } from "@playwright/test";
import { readFile } from "node:fs/promises";
import { PNG } from "pngjs";

async function ready(page: Page, renderer = "webgl") {
  await page.goto(`/?renderer=${renderer}&display=safe&panel=materials`);
  await page.waitForFunction(
    () =>
      document.querySelector(".has-error") ||
      (window.__frontier?.backend &&
        !document.querySelector(".viewport-loading")),
  );
  await expect(page.locator(".has-error")).toHaveCount(0);
  expect(await page.evaluate(() => window.__frontier!.backend.name)).toBe(
    renderer === "webgl" ? "WebGL2" : "WebGPU",
  );
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.pause();
    e.busy = true;
    await e.backend.sync();
  });
}
for (const renderer of ["webgl", "webgpu"])
  test(`${renderer}: all 100 CLUTs render and match the CPU color sampler`, async ({
    page,
  }) => {
    const errors: string[] = [];
    page.on("pageerror", (e) => errors.push(e.message));
    page.on("console", (m) => {
      if (m.type() === "error") errors.push(m.text());
    });
    await ready(page, renderer);
    const result = await page.evaluate(async () => {
      const [{ SATMAP_LIBRARY, samplePalette }, { hexToBytes }] =
        await Promise.all([
          import("/src/engine/satmaps/satmap.ts"),
          import("/src/engine/satmaps/pixels.ts"),
        ]);
      const e = window.__frontier!,
        b = e.backend,
        n = b.size,
        data = new Float32Array(n.x * n.y * n.z * 4);
      for (let z = 0; z < n.z; z++)
        for (let y = 0; y < n.y; y++)
          for (let x = 0; x < n.x; x++)
            data[((z * n.y + y) * n.x + x) * 4] =
              -10 + ((y + 0.5) * 48) / n.y - 2;
      b.writeVolume(data);
      await b.refreshTerrainMaps();
      const before = await b.readVolume();
      const canvas = document.createElement("canvas");
      canvas.width = canvas.height = 1;
      const ctx = canvas.getContext("2d")!;
      const toDisplay = (v: number) =>
        Math.round(
          255 * (v <= 0.0031308 ? v * 12.92 : 1.055 * v ** (1 / 2.4) - 0.055),
        );
      let worst = 0;
      const colors = new Set<string>();
      for (const asset of SATMAP_LIBRARY) {
        const settings = {
          ...e.settings,
          textureMode: "satmap" as const,
          satmap: asset.id,
          foamQuality: "low" as const,
          satmapPreview: "albedo" as const,
          view: "lit" as const,
          satmapHeight: 0,
          satmapSlope: 0,
          satmapCurvature: 0,
          satmapAO: 0,
          satmapDetail: 0,
          satmapFlow: 0,
          satmapSediment: 0,
          satmapBias: 0,
          satmapContrast: 1,
          satmapLow: 0,
          satmapHigh: 1,
          satmapSaturation: 1,
          satmapReverse: false,
        };
        const blob = await b.capture({
          eye: [0, 20, 0],
          forward: [0, -1, 0],
          right: [1, 0, 0],
          up: [0, 0, -1],
          width: 1,
          height: 1,
          time: 0,
          brush: null,
          tool: "orbit",
          compare: false,
          settings,
        });
        const bitmap = await createImageBitmap(blob);
        ctx.drawImage(bitmap, 0, 0);
        bitmap.close();
        const actual = [...ctx.getImageData(0, 0, 1, 1).data].slice(0, 3);
        const expected = samplePalette(hexToBytes(asset.palette), 0.5, 1).map(
          toDisplay,
        );
        worst = Math.max(
          worst,
          ...actual.map((v, i) => Math.abs(v - expected[i])),
        );
        colors.add(actual.join(","));
      }
      const after = await b.readVolume();
      return {
        count: SATMAP_LIBRARY.length,
        worst,
        unique: colors.size,
        unchanged: before.every((v, i) => v === after[i]),
      };
    });
    expect(result.count).toBe(100);
    expect(result.worst).toBeLessThanOrEqual(2);
    expect(result.unique).toBeGreaterThan(90);
    expect(result.unchanged).toBe(true);
    expect(errors).toEqual([]);
  });

test("100-map browser: pagination, filters, attribution, PNG export and project restoration", async ({
  page,
}, info) => {
  const images = new Set<string>();
  page.on("request", (r) => {
    if (/\/satmaps\/[^/]+\.(webp|png|jpg)/.test(r.url()))
      images.add(r.url().split("/").pop()!);
  });
  await ready(page);
  await expect(page.locator(".satmap-count")).toHaveText("100 MAPS");
  const visited = new Set<string>();
  for (let p = 0; p < 9; p++) {
    await page
      .getByLabel("SatMap page", { exact: true })
      .selectOption(String(p));
    for (const id of await page
      .locator(".satmap-card")
      .evaluateAll((nodes) =>
        nodes.map((n) => n.getAttribute("data-satmap-id")!),
      ))
      visited.add(id);
  }
  expect(visited.size).toBe(100);
  await expect(
    page.getByRole("button", { name: "Next SatMap page" }),
  ).toBeDisabled();
  await page.getByLabel("SatMap environment").selectOption("forest");
  await expect(page.locator(".satmap-card")).toHaveCount(10);
  await expect(page.locator(".satmap-thumbnail img")).toHaveCount(0);
  await page.getByLabel("Search SatMaps").fill("moss");
  await expect(
    page.locator('[data-satmap-id="forest-mosswood"]'),
  ).toBeVisible();
  await page.getByLabel("SatMap provenance").selectOption("satellite");
  await expect(page.locator(".satmap-empty")).toBeVisible();
  await page.getByRole("button", { name: "Reset library filters" }).click();
  await expect(page.locator(".satmap-card")).toHaveCount(12);
  await page.getByLabel("SatMap environment").selectOption("fantasy");
  await page.locator('[data-satmap-id="fantasy-amethyst"]').click();
  await expect(page.locator(".material-context")).toContainText(
    "Amethyst kingdom",
  );
  await expect(page.locator(".satmap-source-line")).toContainText(
    "authored by Frontier",
  );
  await expect(page.locator(".satmap-detail-credit")).toContainText(
    "Shared luminance detail",
  );
  await page.getByLabel("Search SatMaps").fill("no-such-map");
  await page
    .getByRole("button", { name: "Show selected", exact: true })
    .click();
  await expect(
    page.locator('[data-satmap-id="fantasy-amethyst"]'),
  ).toHaveAttribute("aria-pressed", "true");
  const lutPending = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Export satellite palette PNG" })
    .click();
  const lut = PNG.sync.read(await readFile((await (await lutPending).path())!));
  expect([lut.width, lut.height]).toEqual([256, 1]);
  await page.getByLabel("SatMap environment").selectOption("quarry");
  await expect(page.locator(".satmap-card")).toHaveCount(8);
  await page.locator('[data-satmap-id="quarry-copper"]').click();
  await page.getByRole("button", { name: "Export", exact: true }).click();
  const pending = page.waitForEvent("download");
  await page.getByRole("menuitem", { name: /SDF project/ }).click();
  const bytes = await readFile((await (await pending).path())!);
  await ready(page);
  await page
    .getByLabel("Import Frontier project")
    .setInputFiles({
      name: "quarry.frontier",
      mimeType: "application/octet-stream",
      buffer: bytes,
    });
  await expect(
    page.locator('[data-satmap-id="quarry-copper"]'),
  ).toHaveAttribute("aria-pressed", "true");
  expect(await page.evaluate(() => window.__frontier!.settings.satmap)).toBe(
    "quarry-copper",
  );
  expect([...images].sort()).toEqual(
    [
      "namib.webp",
      "canyonlands.webp",
      "iceland.webp",
      "white-sands.webp",
    ].sort(),
  );
  await page.waitForFunction(() => !window.__frontier!.busy);
  await page.evaluate(async () => {
    window.__frontier!.busy = true;
    await window.__frontier!.backend.sync();
  });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.locator(".satmap-search").scrollIntoViewIfNeeded();
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    390,
  );
  await page.screenshot({ path: info.outputPath("satmap-library-mobile.png") });
});
