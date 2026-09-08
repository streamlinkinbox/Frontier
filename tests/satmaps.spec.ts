import { test, expect, type Page } from "@playwright/test";
import { PNG } from "pngjs";
import { createHash } from "node:crypto";
import { readFile } from "node:fs/promises";

async function ready(page: Page, renderer = "webgl") {
  await page.goto(`/?renderer=${renderer}&display=safe&panel=materials`);
  await page.waitForFunction(
    () =>
      document.querySelector(".has-error") ||
      (window.__frontier?.backend &&
        !document.querySelector(".viewport-loading")),
  );
  expect(await page.locator(".has-error").count()).toBe(0);
  expect(await page.evaluate(() => window.__frontier!.backend.name)).toBe(
    renderer === "webgl" ? "WebGL2" : "WebGPU",
  );
  await page.evaluate(async () => {
    const engine = window.__frontier!;
    engine.pause();
    engine.busy = true;
    await engine.backend.sync();
    (window as any).satmapOriginal = await engine.backend.readVolume();
  });
}
async function capture(
  page: Page,
  patch: Record<string, unknown> = {},
  compare = false,
): Promise<Buffer> {
  const base64 = await page.evaluate(
    async ({ patch, compare }) => {
      const engine = window.__frontier!;
      const frame = {
        ...(engine as any).makeFrame(),
        width: 384,
        height: 288,
        time: 0,
        brush: null,
        compare,
        settings: { ...engine.settings, ...patch },
      };
      const blob = await engine.backend.capture(frame);
      return await new Promise<string>((resolve) => {
        const reader = new FileReader();
        reader.onload = () => resolve((reader.result as string).split(",")[1]);
        reader.readAsDataURL(blob);
      });
    },
    { patch, compare },
  );
  return Buffer.from(base64, "base64");
}
const difference = (a: Buffer, b: Buffer) => {
  const x = PNG.sync.read(a).data,
    y = PNG.sync.read(b).data;
  let total = 0;
  for (let i = 0; i < x.length; i++)
    if (i % 4 !== 3) total += Math.abs(x[i] - y[i]);
  return total / ((x.length / 4) * 3);
};
const mean = (buffer: Buffer) => {
  const pixels = PNG.sync.read(buffer).data;
  let total = 0;
  for (let i = 0; i < pixels.length; i += 4) total += pixels[i];
  return total / (pixels.length / 4);
};

for (const renderer of ["webgl", "webgpu"]) {
  test(`${renderer}: real satellite textures and every terrain-mask preview render without touching geometry`, async ({
    page,
  }, info) => {
    const errors: string[] = [];
    page.on("pageerror", (e) => errors.push(e.message));
    page.on("console", (m) => {
      if (m.type() === "error") errors.push(m.text());
    });
    await ready(page, renderer);
    await expect(
      page.getByRole("button", { name: /^SatMaps COLOR MAPS/ }),
    ).toHaveAttribute("aria-pressed", "true");
    const palettes: Buffer[] = [];
    for (const name of [
      "Namib dunes",
      "Canyonlands",
      "Volcanic coast",
      "White Sands",
    ]) {
      await page
        .locator(".satmap-library")
        .getByRole("button", { name: new RegExp(name) })
        .click();
      palettes.push(await capture(page, { satmapPreview: "albedo" }));
    }
    for (let i = 1; i < palettes.length; i++)
      expect(difference(palettes[i - 1], palettes[i])).toBeGreaterThan(2);
    await page
      .locator(".satmap-library")
      .getByRole("button", { name: /Namib dunes/ })
      .click();
    const views = [
      "Surface",
      "Albedo",
      "Texture mask",
      "Height",
      "Slope",
      "Curvature",
      "Occlusion",
      "Normals",
      "Flow",
      "Deposition",
      "Source detail",
    ];
    const hashes = new Set<string>();
    for (const label of views) {
      await page
        .locator(".satmap-previews")
        .getByRole("button", { name: label, exact: true })
        .click();
      const image = await capture(page);
      hashes.add(
        createHash("sha256").update(PNG.sync.read(image).data).digest("hex"),
      );
      if (["Albedo", "Curvature", "Normals", "Flow"].includes(label))
        await info.attach(`${renderer}-${label}`, {
          body: image,
          contentType: "image/png",
        });
    }
    expect(hashes.size).toBe(views.length);
    const unlit = await capture(page, {
      satmapPreview: "albedo",
      sunAngle: 10,
      exposure: 0.5,
      water: false,
    });
    const relit = await capture(page, {
      satmapPreview: "albedo",
      sunAngle: 85,
      exposure: 1.7,
      water: true,
    });
    expect(difference(unlit, relit)).toBe(0);
    // The entire legacy pigment/noise model is bypassed by the SatMap path.
    const legacyPigments = await capture(page, {
      satmapPreview: "albedo",
      material: "basalt",
      materialColor: "#ff00ff",
      materialBedding: 1,
      materialWeathering: 1,
    });
    expect(difference(unlit, legacyPigments)).toBe(0);
    expect(
      await page.evaluate(async () =>
        (await window.__frontier!.backend.readVolume()).every(
          (v, i) => v === (window as any).satmapOriginal[i],
        ),
      ),
    ).toBe(true);
    await page
      .locator(".satmap-previews")
      .getByRole("button", { name: "Surface", exact: true })
      .click();
    await page.evaluate(async () => {
      const e = window.__frontier!;
      e.backend.render({ ...(e as any).makeFrame(), time: 0, brush: null });
      await e.backend.sync();
    });
    await page
      .locator(".inspector-scroll")
      .evaluate((el) => (el.scrollTop = 0));
    await page.screenshot({
      path: info.outputPath(`${renderer}-satmap-studio.png`),
    });
    expect(errors).toEqual([]);
  });

  test(`${renderer}: rendered CLUT/mask pixels match the CPU export equations`, async ({
    page,
  }) => {
    await ready(page, renderer);
    const { actual, expected, heightError } = await page.evaluate(async () => {
      const [
        { textureMask, remapSatmap, samplePalette, SATMAP_LIBRARY },
        { buildTerrainMaps, extractHeightSurface },
        { hexToBytes },
      ] = await Promise.all([
        import("/src/engine/satmaps/satmap.ts"),
        import("/src/engine/satmaps/terrainMaps.ts"),
        import("/src/engine/satmaps/pixels.ts"),
      ]);
      const e = window.__frontier!,
        size = e.backend.size;
      const data = new Float32Array(size.x * size.y * size.z * 4);
      for (let z = 0; z < size.z; z++)
        for (let y = 0; y < size.y; y++)
          for (let x = 0; x < size.x; x++) {
            const i = ((z * size.y + y) * size.x + x) * 4;
            data[i] = -10 + ((y + 0.5) * 48) / size.y - 2;
            data[i + 1] = 0.2;
            data[i + 3] = -0.3;
          }
      e.backend.writeVolume(data);
      await e.backend.refreshTerrainMaps();
      const decoded = await e.backend.readVolume();
      const referenceMaps = buildTerrainMaps(
        extractHeightSurface(decoded, size),
      );
      const backendMaps = (e.backend as any).terrainMaps;
      let heightError = 0;
      backendMaps.heights.forEach((h: number, i: number) => {
        heightError = Math.max(
          heightError,
          Math.abs(h - referenceMaps.heights[i]),
        );
      });
      const base = {
        ...e.settings,
        satmap: "custom",
        satmapPalette: SATMAP_LIBRARY[2].palette,
        satmapDetailMap: "80".repeat(64 * 64),
        satmapHeight: 0,
        satmapSlope: 0.3,
        satmapCurvature: 0.7,
        satmapAO: 0.2,
        satmapDetail: 0.9,
        satmapFlow: 0.8,
        satmapSediment: 0.6,
        satmapContrast: 1.3,
        satmapBias: -0.2,
        satmapSaturation: 1.2,
        satmapPreview: "albedo",
        view: "lit",
      };
      const actual: number[][] = [],
        expected: number[][] = [];
      const display = (c: number) =>
        Math.round(
          255 *
            (c <= 0.0031308 ? 12.92 * c : 1.055 * Math.pow(c, 1 / 2.4) - 0.055),
        );
      for (const patch of [
        {},
        { satmapReverse: true, satmapBias: 0.3 },
        { satmapLow: 0.2, satmapHigh: 0.75, satmapSaturation: 0 },
      ]) {
        const settings = { ...base, ...patch } as any;
        const masks = {
          height: 0,
          slope: 0,
          curvature: 0,
          ao: 1,
          flow: decoded[1] * 3,
          sediment: -decoded[3] / 0.65,
          detail: 128 / 255,
        };
        expected.push(
          samplePalette(
            hexToBytes(settings.satmapPalette),
            remapSatmap(textureMask(masks, settings), settings),
            settings.satmapSaturation,
          ).map(display),
        );
        const blob = await e.backend.capture({
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
        const bitmap = await createImageBitmap(blob),
          canvas = document.createElement("canvas");
        canvas.width = canvas.height = 1;
        const context = canvas.getContext("2d")!;
        context.drawImage(bitmap, 0, 0);
        actual.push([...context.getImageData(0, 0, 1, 1).data].slice(0, 3));
        bitmap.close();
      }
      return { actual, expected, heightError };
    });
    expect(heightError).toBeLessThan(0.001);
    for (let i = 0; i < actual.length; i++)
      for (let c = 0; c < 3; c++)
        expect(Math.abs(actual[i][c] - expected[i][c])).toBeLessThanOrEqual(2);
  });

  test(`${renderer}: deposition uses actual displacement, drainage refreshes, compare and reset keep original maps`, async ({
    page,
  }) => {
    await ready(page, renderer);
    const initial = await capture(page, { satmapPreview: "sediment" });
    const originalFlow = await capture(page, { satmapPreview: "flow" }, true);
    await page.evaluate(async () => {
      const e = window.__frontier!,
        data = await e.backend.readVolume();
      // Suspended sediment alone must NOT appear as ground deposition.
      for (let i = 0; i < data.length; i += 4) data[i + 2] = 1;
      e.backend.writeVolume(data);
    });
    expect(
      difference(initial, await capture(page, { satmapPreview: "sediment" })),
    ).toBeLessThan(0.01);
    await page.evaluate(async () => {
      const e = window.__frontier!,
        data = await e.backend.readVolume();
      for (let i = 0; i < data.length; i += 4) data[i + 3] = -0.65;
      e.backend.writeVolume(data);
      await e.backend.refreshTerrainMaps();
    });
    expect(
      mean(await capture(page, { satmapPreview: "sediment" })),
    ).toBeGreaterThan(mean(initial) + 25);
    expect(
      difference(
        initial,
        await capture(page, { satmapPreview: "sediment" }, true),
      ),
    ).toBeLessThan(0.01);
    await page.evaluate(async () => {
      const e = window.__frontier!,
        data = await e.backend.readVolume(),
        size = e.backend.size;
      // A different physical upper envelope, not a seeded visual river curve.
      for (let z = 0; z < size.z; z++)
        for (let y = 0; y < size.y; y++)
          for (let x = 0; x < size.x; x++) {
            const i = ((z * size.y + y) * size.x + x) * 4;
            const wx = -48 + ((x + 0.5) * 96) / size.x,
              wz = -48 + ((z + 0.5) * 96) / size.z;
            data[i] =
              -10 +
              ((y + 0.5) * 48) / size.y -
              (2 + Math.abs(wx) * 0.12 + wz * 0.05);
            data[i + 1] = 0;
            data[i + 2] = 0;
            data[i + 3] = 0;
          }
      e.backend.writeVolume(data);
      e.busy = false; // exercise the automatic/coalesced render-loop refresh
    });
    await page.waitForFunction(() => {
      const maps = window.__frontier!.backend.getDiagnostics()
        .satelliteMaps as any;
      return (
        maps.dirty === false ||
        (maps.revision === maps.bakedRevision && !maps.pending)
      );
    });
    await page.evaluate(async () => {
      window.__frontier!.busy = true;
      await window.__frontier!.backend.sync();
    });
    const editedFlow = await capture(page, { satmapPreview: "flow" });
    expect(difference(originalFlow, editedFlow)).toBeGreaterThan(2);
    expect(
      difference(
        originalFlow,
        await capture(page, { satmapPreview: "flow" }, true),
      ),
    ).toBe(0);
    await page.evaluate(async () => {
      window.__frontier!.backend.reset();
      await window.__frontier!.backend.refreshTerrainMaps();
    });
    expect(
      difference(originalFlow, await capture(page, { satmapPreview: "flow" })),
    ).toBeLessThan(0.01);
    expect(
      difference(initial, await capture(page, { satmapPreview: "sediment" })),
    ).toBeLessThan(0.01);
  });
}

test("local image/strip import, validation, portable project and palette PNG export", async ({
  page,
}, info) => {
  await ready(page);
  await page.locator(".satmap-import > summary").click();
  await page.getByRole("button", { name: "Color strip", exact: true }).click();
  const strip = new PNG({ width: 256, height: 1 });
  for (let i = 0; i < 256; i++) strip.data.set([255 - i, 20, i, 255], i * 4);
  await page
    .getByLabel("Import satellite image", { exact: true })
    .setInputFiles({
      name: "my-satellite.png",
      mimeType: "image/png",
      buffer: PNG.sync.write(strip),
    });
  await expect(page.locator(".satmap-import-message")).toContainText(
    "my-satellite imported",
  );
  const imported = await page.evaluate(() => ({
    ...window.__frontier!.settings,
  }));
  expect(imported.satmap).toBe("custom");
  expect(imported.satmapPalette.slice(0, 6)).toBe("ff1400");
  expect(imported.satmapPalette.slice(-6)).toBe("0014ff");
  const image = await capture(page, { satmapPreview: "albedo" });
  const reverse = await capture(page, {
    satmapPreview: "albedo",
    satmapReverse: true,
  });
  expect(difference(image, reverse)).toBeGreaterThan(5);
  await page
    .getByLabel("Import satellite image", { exact: true })
    .setInputFiles({
      name: "broken.png",
      mimeType: "image/png",
      buffer: Buffer.from("not an image"),
    });
  await expect(page.locator(".satmap-import-message")).toHaveAttribute(
    "role",
    "alert",
  );
  expect(
    await page.evaluate(() => window.__frontier!.settings.satmapPalette),
  ).toBe(imported.satmapPalette);
  const lutDownload = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Export satellite palette PNG" })
    .click();
  const lut = PNG.sync.read(
    await readFile((await (await lutDownload).path())!),
  );
  expect([lut.width, lut.height]).toEqual([256, 1]);
  expect(lut.data).toEqual(strip.data);
  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Saved", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Export", exact: true }).click();
  const projectDownload = page.waitForEvent("download");
  await page.getByRole("menuitem", { name: /SDF project/ }).click();
  const projectPath = (await (await projectDownload).path())!;
  await page.reload();
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  await page.getByLabel("Import Frontier project").setInputFiles(projectPath);
  await expect
    .poll(() => page.evaluate(() => window.__frontier!.settings.satmap))
    .toBe("custom");
  await page.waitForFunction(
    () =>
      !document.querySelector(".busy-indicator") && !window.__frontier!.busy,
  );
  expect(
    await page.evaluate(() => window.__frontier!.settings.satmapPalette),
  ).toBe(imported.satmapPalette);
  expect(
    await page.evaluate(() => window.__frontier!.settings.satmapDetailMap),
  ).toBe(imported.satmapDetailMap);
  await page.evaluate(() => {
    window.__frontier!.pause();
    window.__frontier!.busy = true;
  });
  expect(
    difference(image, await capture(page, { satmapPreview: "albedo" })),
  ).toBeLessThan(0.15); // half-float archive quantization
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    390,
  );
  await expect(page.locator(".satmap-custom")).toBeVisible();
  await page.screenshot({ path: info.outputPath("satmap-mobile.png") });
});
