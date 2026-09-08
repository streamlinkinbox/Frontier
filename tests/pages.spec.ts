import { test, expect } from "@playwright/test";
import { PNG } from "pngjs";
import { readFile } from "node:fs/promises";
import { expectSandstone } from "./pixels";
import { readFileSync } from "node:fs";
const { version } = JSON.parse(
  readFileSync(new URL("../package.json", import.meta.url), "utf8"),
);

for (const renderer of ["webgpu", "webgl"] as const) {
  test(`${renderer}: compiled Pages site loads its assets, renders terrain and exports`, async ({
    page,
    request,
  }, testInfo) => {
    const failures: string[] = [];
    const errors: string[] = [];
    page.on("response", (r) => {
      if (r.status() >= 400) failures.push(`${r.status()} ${r.url()}`);
    });
    page.on("pageerror", (e) => errors.push(e.message));
    await page.goto(`./?renderer=${renderer}#terrain`);
    const nested = testInfo.project.name === "root-source-recovery";
    await expect(page).toHaveURL(
      new RegExp(
        `/Frontier/${nested ? "docs/" : ""}\\?renderer=${renderer}#terrain$`,
      ),
    );
    await expect(
      page.getByRole("button", { name: /^Run erosion/ }),
    ).toBeEnabled();
    await expect(page.locator(".viewport-loading")).toHaveCount(0);
    await expect(page.locator(".renderer-mode-label")).toHaveText(
      renderer === "webgpu" ? "WebGPU" : "WebGL2",
    );
    expect(await page.evaluate(() => window.__frontier)).toBeUndefined();
    await expect(page.locator(".footer-version")).toContainText(`v${version}`);
    const image = await page.locator(".viewport-canvas").screenshot();
    expectSandstone(image);
    await testInfo.attach("rendered-canyon", {
      body: image,
      contentType: "image/png",
    });
    for (const path of [
      "favicon.svg",
      "presets/canyon.webp",
      "presets/arches.webp",
      "presets/badlands.webp",
      "satmaps/namib.webp",
      "satmaps/canyonlands.webp",
      "satmaps/iceland.webp",
      "satmaps/white-sands.webp",
      "research/satellite-texturing.md",
      "research/foam-implementation.md",
    ])
      expect((await request.get(new URL(path, page.url()).href)).status()).toBe(
        200,
      );
    expect(
      (
        await request.get(new URL("assets/does-not-exist.js", page.url()).href)
      ).status(),
    ).toBe(404);

    await page.getByRole("button", { name: "Export", exact: true }).click();
    const pending = page.waitForEvent("download");
    await page.getByRole("menuitem", { name: /Viewport image/ }).click();
    const png = await pending;
    expect(png.suggestedFilename()).toMatch(/\.png$/);
    expectSandstone(await readFile((await png.path())!));
    await expect(page.locator(".viewport-loading")).toHaveCount(0);

    // The module worker URL is relative to the built JS, including /docs when
    // recovering from a mistakenly root-published repository.
    if (renderer === "webgl") {
      await page.getByRole("button", { name: "Export", exact: true }).click();
      const pendingMesh = page.waitForEvent("download");
      await page.getByRole("menuitem", { name: /Terrain mesh/ }).click();
      const mesh = await pendingMesh;
      const bytes = await readFile((await mesh.path())!);
      expect(bytes.readUInt32LE(0)).toBe(0x46546c67);
    }
    await page.getByRole("tab", { name: "Materials", exact: true }).click();
    await expect(page.locator(".satmap-card")).toHaveCount(12);
    await expect(page.locator(".satmap-card.selected")).toContainText(
      "Namib dunes",
    );
    await expect
      .poll(() =>
        page
          .locator(".satmap-thumbnail img")
          .evaluateAll(
            (images) =>
              images.length === 4 &&
              images.every(
                (image) =>
                  (image as HTMLImageElement).complete &&
                  (image as HTMLImageElement).naturalWidth > 0,
              ),
          ),
      )
      .toBe(true);
    await page
      .locator(".satmap-library")
      .getByRole("button", { name: /Volcanic coast/ })
      .click();
    await expect(page.locator(".material-context")).toContainText(
      "Volcanic coast",
    );
    await page.getByRole("tab", { name: "Environment", exact: true }).click();
    await expect(
      page.getByRole("button", { name: /Standard TRANSPORT/ }),
    ).toHaveAttribute("aria-pressed", "true");
    await page.getByLabel("Foam workload budget").selectOption("compact");
    await page.getByRole("button", { name: /Cinematic WHITEWATER/ }).click();
    await page.getByRole("button", { name: "Export", exact: true }).click();
    const cinematicDownload = page.waitForEvent("download");
    await page.getByRole("menuitem", { name: /Viewport image/ }).click();
    const cinematicPNG = PNG.sync.read(
      await readFile((await (await cinematicDownload).path())!),
    );
    expect(cinematicPNG.width).toBeGreaterThan(200);
    let min = 255,
      max = 0;
    for (let i = 0; i < cinematicPNG.data.length; i += 4) {
      min = Math.min(min, cinematicPNG.data[i]);
      max = Math.max(max, cinematicPNG.data[i]);
    }
    expect(max - min).toBeGreaterThan(30);
    await page.getByRole("button", { name: "GPU logs", exact: true }).click();
    await expect(
      page.getByRole("dialog", { name: "GPU diagnostics" }),
    ).toBeVisible();
    await expect(
      page.getByRole("textbox", { name: "GPU diagnostic report" }),
    ).toHaveValue(/FRONTIER GPU DIAGNOSTICS/);
    expect(failures).toEqual([]);
    expect(errors).toEqual([]);
  });
}

test("a missing application bundle gets an explanation instead of an empty page", async ({
  page,
}) => {
  await page.route("**/assets/*.js", (route) =>
    route.fulfill({ status: 404, body: "Not found" }),
  );
  await page.goto("./");
  await expect(page.locator("#frontier-boot-message")).toContainText(
    "application files could not finish loading",
  );
  await expect(page.getByRole("button", { name: "Reload page" })).toBeVisible();
});

test("compiled material-editor HTML is independent and works under nested static hosting", async ({
  page,
  request,
}, info) => {
  const failures: string[] = [],
    errors: string[] = [];
  page.on("response", (response) => {
    if (response.status() >= 400) failures.push(response.url());
  });
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("./material-editor.html?quality=draft");
  await expect(page).toHaveTitle("Material Editor — Frontier");
  await expect(page.locator(".lab-loading")).toHaveCount(0);
  await expect(page.locator(".lab-error")).toHaveCount(0);
  expect(
    await page.evaluate(() => (window as any).__materialLab),
  ).toBeUndefined();
  expect(await page.evaluate(() => window.__frontier)).toBeUndefined();
  const suffix = info.project.name === "root-source-recovery" ? "docs/" : "";
  await expect(page).toHaveURL(
    new RegExp(`/Frontier/${suffix}material-editor\\.html\\?quality=draft$`),
  );
  expect(
    (
      await request.get(
        new URL("research/sdf-stone-materials.md", page.url()).href,
      )
    ).status(),
  ).toBe(200);
  await page.getByRole("button", { name: "Silhouette", exact: true }).click();
  const pending = page.waitForEvent("download");
  await page.getByRole("button", { name: "Export PNG", exact: true }).click();
  const png = PNG.sync.read(await readFile((await (await pending).path())!));
  let hits = 0;
  for (let i = 0; i < png.data.length; i += 4) if (png.data[i] > 200) hits++;
  expect(hits).toBeGreaterThan(png.width * png.height * 0.05);
  expect(hits).toBeLessThan(png.width * png.height * 0.85);
  expect(failures).toEqual([]);
  expect(errors).toEqual([]);
});
