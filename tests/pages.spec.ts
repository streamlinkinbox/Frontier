import { test, expect } from "@playwright/test";
import { readFile } from "node:fs/promises";
import { expectSandstone } from "./pixels";

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
    await expect(page.locator(".footer-version")).toContainText("v0.2.1");
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
