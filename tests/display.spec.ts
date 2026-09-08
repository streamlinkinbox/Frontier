import { test, expect } from "@playwright/test";
import { expectSandstone } from "./pixels";

const windows =
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/151.0.0.0 Safari/537.36";

test("Windows safe display renders real GPU erosion at DPR 0.5 without ever requesting a native GPU canvas", async ({
  browser,
}, testInfo) => {
  const context = await browser.newContext({
    userAgent: windows,
    deviceScaleFactor: 0.5,
    viewport: { width: 1578, height: 1154 },
  });
  const page = await context.newPage();
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.addInitScript(() => {
    const original = HTMLCanvasElement.prototype.getContext;
    (window as any).nativeCanvasRequests = 0;
    HTMLCanvasElement.prototype.getContext = function (...args: any[]) {
      if (args[0] === "webgpu") {
        (window as any).nativeCanvasRequests++;
        throw new Error("Simulated broken native WebGPU canvas");
      }
      return original.apply(this, args as any);
    } as typeof original;
  });
  await page.goto(testInfo.project.use.baseURL!);
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  const state = await page.evaluate(() => ({
    backend: window.__frontier!.backend.name,
    display: window.__frontier!.backend.displayMode,
    size: window.__frontier!.backend.size,
    nativeRequests: (window as any).nativeCanvasRequests,
    bitmapFrames: window.__frontier!.backend.getDiagnostics().bitmapFrames,
  }));
  expect(state).toMatchObject({
    backend: "WebGPU",
    display: "safe",
    size: { x: 144, y: 72, z: 144 },
    nativeRequests: 0,
  });
  expect(state.bitmapFrames).toBeGreaterThan(0);
  await expect(page.locator(".gpu-status")).toContainText("SAFE DISPLAY");
  // Software SwiftShader + Canvas2D safe presentation can continuously dirty
  // the compositor during a screenshot. Settle one real displayed GPU frame,
  // then freeze only while taking the browser screenshot (not an owned-image
  // substitute). Resume before testing erosion/undo and live diagnostics.
  const displayedScreenshot = async (path?: string) => {
    await page.evaluate(async () => {
      const e = window.__frontier!;
      e.busy = true;
      await e.backend.sync();
      await e.backend.refreshTerrainMaps();
      e.backend.render({ ...(e as any).makeFrame(), brush: null });
      await e.backend.sync();
    });
    try {
      return await page.locator(".viewport-canvas").screenshot({ path });
    } finally {
      await page.evaluate(() => {
        window.__frontier!.busy = false;
      });
    }
  };
  expectSandstone(
    await displayedScreenshot(testInfo.outputPath("windows-safe-display.png")),
  );
  await page.evaluate(async () => {
    (window as any).beforeErosion =
      await window.__frontier!.backend.readVolume();
  });
  await page.getByRole("button", { name: "Simulate one iteration" }).click();
  await page.waitForFunction(() => window.__frontier!.steps === 1);
  expect(
    await page.evaluate(async () => {
      const data = await window.__frontier!.backend.readVolume();
      return data.some(
        (v, i) =>
          i % 4 === 0 &&
          Math.abs(v - (window as any).beforeErosion[i]) > 0.0001,
      );
    }),
  ).toBe(true);
  await page.getByRole("button", { name: "Undo", exact: true }).click();
  await page.waitForFunction(() => !window.__frontier!.busy);
  expect(
    await page.evaluate(async () => {
      const data = await window.__frontier!.backend.readVolume();
      return data.every((v, i) => v === (window as any).beforeErosion[i]);
    }),
  ).toBe(true);
  expectSandstone(await displayedScreenshot());
  await page.getByRole("button", { name: "GPU logs", exact: true }).click();
  await page
    .getByRole("button", { name: "Check viewport", exact: true })
    .click();
  await expect(
    page.getByRole("textbox", { name: "GPU diagnostic report" }),
  ).toHaveValue(/Viewport checks complete/);
  expect(errors).toEqual([]);
  await context.close();
});

test("a native surface waits for visibility and is renewed after returning from a hidden tab", async ({
  page,
}, testInfo) => {
  await page.addInitScript(() => {
    (window as any).testVisible = false;
    (window as any).configureEvents = [];
    Object.defineProperty(document, "hidden", {
      configurable: true,
      get: () => !(window as any).testVisible,
    });
    Object.defineProperty(document, "visibilityState", {
      configurable: true,
      get: () => ((window as any).testVisible ? "visible" : "hidden"),
    });
    const configure = GPUCanvasContext.prototype.configure;
    GPUCanvasContext.prototype.configure = function (options) {
      (window as any).configureEvents.push({ hidden: document.hidden });
      return configure.call(this, options);
    };
  });
  await page.goto("/?display=native");
  await page.waitForFunction(
    () => window.__frontier?.backend?.getDiagnostics().initialized === true,
  );
  expect(await page.evaluate(() => (window as any).configureEvents)).toEqual(
    [],
  );
  expect(
    await page.evaluate(
      () => window.__frontier!.backend.getDiagnostics().submittedFrames,
    ),
  ).toBe(0);
  await page.evaluate(() => {
    (window as any).testVisible = true;
    document.dispatchEvent(new Event("visibilitychange"));
  });
  await page.waitForFunction(
    () => !document.querySelector(".viewport-loading"),
  );
  expect(await page.evaluate(() => (window as any).configureEvents)).toEqual([
    { hidden: false },
  ]);
  const before = await page.evaluate(
    () => (window as any).configureEvents.length,
  );
  await page.evaluate(() => {
    (window as any).testVisible = false;
    document.dispatchEvent(new Event("visibilitychange"));
  });
  await page.waitForTimeout(150);
  await page.evaluate(() => {
    (window as any).testVisible = true;
    document.dispatchEvent(new Event("visibilitychange"));
  });
  await page.waitForFunction(
    (n) => (window as any).configureEvents.length > n,
    before,
  );
  expect(
    await page.evaluate(() =>
      (window as any).configureEvents.every((event: any) => !event.hidden),
    ),
  ).toBe(true);
  expect(await page.evaluate(() => window.__frontier!.steps)).toBe(0);
  expectSandstone(
    await page
      .locator(".viewport-canvas")
      .screenshot({ path: testInfo.outputPath("native-after-visibility.png") }),
  );
});
