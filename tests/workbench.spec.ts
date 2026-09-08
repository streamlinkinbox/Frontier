import { test, expect } from "@playwright/test";
import { readFile } from "node:fs/promises";
import { expectSandstone } from "./pixels";

test("SDF erosion, sculpting, exact undo, exports and responsive tools", async ({
  page,
}, testInfo) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  // This is the GPU simulation test; a silent fallback is not a GPU pass.
  expect(await page.evaluate(() => window.__frontier!.backend.name)).toBe(
    "WebGPU",
  );

  await page.evaluate(async () => {
    const e = window.__frontier!;
    (window as any).before = await e.backend.readVolume();
  });
  await page.getByRole("button", { name: /^Run erosion/ }).click();
  await page.waitForFunction(() => window.__frontier!.steps >= 4);
  await page.getByRole("button", { name: /^Pause erosion/ }).click();
  const changed = await page.evaluate(async () => {
    const data = await window.__frontier!.backend.readVolume();
    let changes = 0;
    for (let i = 0; i < data.length; i += 4)
      if (Math.abs(data[i] - (window as any).before[i]) > 0.001) changes++;
    return changes;
  });
  expect(changed).toBeGreaterThan(100);

  const beforeStep = await page.evaluate(() => window.__frontier!.steps);
  await page.getByRole("button", { name: "Simulate one iteration" }).click();
  await page.waitForFunction(
    (n) => window.__frontier!.steps === n + 1,
    beforeStep,
  );
  const beforePreview = beforeStep + 1;
  await page.getByRole("slider", { name: "Rainfall", exact: true }).focus();
  await page.keyboard.press("End");
  await page.waitForFunction(
    (n) => window.__frontier!.steps === n + 12,
    beforePreview,
  );

  await page.getByRole("button", { name: /^Carve/ }).click();
  const bounds = (await page.locator(".viewport-canvas").boundingBox())!;
  await page.mouse.move(
    bounds.x + bounds.width * 0.33,
    bounds.y + bounds.height * 0.61,
  );
  // Hover/pick and the snapshot complete before the first edit, even on a slow GPU.
  await page.waitForFunction(() => !!(window.__frontier as any).brush);
  await page.evaluate(async () => {
    (window as any).beforeBrush = await window.__frontier!.backend.readVolume();
  });
  await page.mouse.down();
  await page.waitForFunction(
    () => (window.__frontier as any).mode === "sculpt",
  );
  await page.waitForTimeout(1800);
  await page.mouse.up();
  const carved = await page.evaluate(async () => {
    const data = await window.__frontier!.backend.readVolume();
    return data.some(
      (v, i) => i % 4 === 0 && v > (window as any).beforeBrush[i] + 0.01,
    );
  });
  expect(carved).toBe(true);
  await page.getByRole("button", { name: "Undo", exact: true }).click();
  await page.waitForFunction(() => !window.__frontier!.busy);
  const undoError = await page.evaluate(async () => {
    const data = await window.__frontier!.backend.readVolume();
    let max = 0;
    for (let i = 0; i < data.length; i++)
      max = Math.max(max, Math.abs(data[i] - (window as any).beforeBrush[i]));
    return max;
  });
  expect(undoError).toBe(0);
  await page.getByRole("button", { name: "Redo", exact: true }).click();
  await page.waitForFunction(() => !window.__frontier!.busy);
  await page.keyboard.down("c");
  await expect(page.locator(".compare-banner")).toBeVisible();
  await page.keyboard.up("c");
  await expect(page.locator(".compare-banner")).toHaveCount(0);

  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Saved", exact: true }),
  ).toBeVisible();
  await page.getByRole("button", { name: "Export", exact: true }).click();
  const meshDownload = page.waitForEvent("download");
  await page.getByRole("menuitem", { name: /Terrain mesh/ }).click();
  const mesh = await meshDownload;
  expect(mesh.suggestedFilename()).toMatch(/\.glb$/);
  await mesh.saveAs(testInfo.outputPath("terrain.glb"));
  await page.waitForFunction(
    () => !document.querySelector(".viewport-loading"),
  );

  await page.getByRole("button", { name: /Sandstone arches/ }).click();
  await page.getByRole("button", { name: "Continue", exact: true }).click();
  await page.waitForFunction(
    () =>
      window.__frontier!.settings.preset === "arches" &&
      !window.__frontier!.busy &&
      !document.querySelector(".viewport-loading"),
  );
  await page.getByRole("button", { name: "Quick guide" }).click();
  await expect(page.getByRole("dialog")).toBeVisible();
  await page.keyboard.press("Escape");
  await expect(page.getByRole("dialog")).toHaveCount(0);

  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    390,
  );
  await page.getByRole("button", { name: "Toggle sculpting panel" }).click();
  await expect(page.locator(".left-sidebar")).toBeVisible();
  expect(errors).toEqual([]);
});

test("explicit fallback keeps a 3D volume and working environmental controls", async ({
  page,
}) => {
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend?.name === "WebGL2" &&
      !document.querySelector(".viewport-loading"),
  );
  await expect(page.locator(".gpu-status")).toContainText("CPU FALLBACK");
  await page.getByRole("tab", { name: "Environment", exact: true }).click();
  await page.getByRole("slider", { name: "Sun elevation" }).focus();
  await page.keyboard.press("End");
  expect(await page.evaluate(() => window.__frontier!.settings.sunAngle)).toBe(
    85,
  );
  await page
    .getByRole("switch", { name: "Water surface", exact: true })
    .click();
  expect(await page.evaluate(() => window.__frontier!.settings.water)).toBe(
    false,
  );
  await page.getByRole("button", { name: "Simulate one iteration" }).click();
  await page.waitForFunction(() => window.__frontier!.steps === 1);
  expect(await page.evaluate(() => window.__frontier!.backend.size)).toEqual({
    x: 96,
    y: 48,
    z: 96,
  });
});

test("late failure from an obsolete renderer cannot hide the replacement canvas", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  const result = await page.evaluate(async () => {
    const main = window.__frontier!;
    main.busy = true;
    const { TerrainEngine } = await import("/src/engine/TerrainEngine.ts");
    const host = document.createElement("div");
    host.className = "viewport-canvas";
    host.style.cssText =
      "position:fixed;top:0;left:0;width:320px;height:240px;overflow:hidden";
    document.body.append(host);
    const originalRequest = navigator.gpu.requestAdapter.bind(navigator.gpu);
    let rejectFirst!: (error: Error) => void;
    let calls = 0;
    navigator.gpu.requestAdapter = (options) =>
      ++calls === 1
        ? new Promise((_, reject) => {
            rejectFirst = reject;
          })
        : originalRequest(options);
    const callbacks = {
      stats() {},
      changed() {},
      history() {},
      notice() {},
      error() {},
      ready() {},
    };
    const obsolete = new TerrainEngine(host, main.settings, callbacks);
    const pending = obsolete.initialize();
    obsolete.dispose();
    const replacement = new TerrainEngine(host, main.settings, callbacks);
    try {
      await replacement.initialize();
      replacement.busy = true;
      rejectFirst(new Error("Late startup failure from a disposed renderer"));
      await pending;
      return {
        canvases: host.querySelectorAll("canvas").length,
        activeIsFirst: host.firstElementChild === replacement.canvas,
        offsetY:
          replacement.canvas.getBoundingClientRect().top -
          host.getBoundingClientRect().top,
        stopped: (obsolete as any).stopped,
      };
    } finally {
      navigator.gpu.requestAdapter = originalRequest;
      replacement.dispose();
      host.remove();
      main.busy = false;
    }
  });
  expect(result).toEqual({
    canvases: 1,
    activeIsFirst: true,
    offsetY: 0,
    stopped: true,
  });
});

test("an unrendered GPU frame falls back instead of reporting an empty viewport as ready", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  const result = await page.evaluate(async () => {
    const main = window.__frontier!;
    main.busy = true;
    const { TerrainEngine } = await import("/src/engine/TerrainEngine.ts");
    const host = document.createElement("div");
    host.className = "viewport-canvas";
    host.style.cssText = "position:fixed;top:0;left:0;width:320px;height:240px";
    document.body.append(host);
    let ready = "";
    const engine = new TerrainEngine(host, main.settings, {
      stats() {},
      changed() {},
      history() {},
      notice() {},
      error() {},
      ready: (value) => {
        ready = value;
      },
    });
    const pending = engine.initialize();
    // Patch this actual instance, not a second module imported without Vite's
    // HMR timestamp. The replacement WebGL backend keeps its real verifier.
    engine.backend.verifyFrame = async () => false;
    try {
      await pending;
      engine.busy = true;
      return {
        ready,
        backend: engine.backend.name,
        canvases: host.querySelectorAll("canvas").length,
      };
    } finally {
      engine.dispose();
      host.remove();
      main.busy = false;
    }
  });
  expect(result).toEqual({ ready: "WebGL2", backend: "WebGL2", canvases: 1 });
});

test("a blank-view recovery option is reachable from the viewport", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  await page.getByRole("button", { name: "Renderer options" }).click();
  await page
    .getByRole("menuitem", { name: /Use compatibility renderer/ })
    .click();
  await expect(page).toHaveURL(/renderer=webgl/);
  await page.waitForFunction(
    () =>
      window.__frontier?.backend?.name === "WebGL2" &&
      !document.querySelector(".viewport-loading"),
  );
  await expect(page.locator(".viewport-canvas canvas")).toHaveCount(1);
});

test("WASD/QE fly controls rotate in place, stop on blur and never move while typing", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  await page.getByRole("button", { name: "Camera controls" }).click();
  await page.getByRole("menuitem", { name: /^Fly camera/ }).click();
  const bounds = (await page.locator(".viewport-canvas").boundingBox())!;
  const original = await page.evaluate(
    () => window.__frontier!.camera.basis(1.5).eye,
  );
  await page.mouse.move(
    bounds.x + bounds.width * 0.5,
    bounds.y + bounds.height * 0.5,
  );
  await page.mouse.down({ button: "right" });
  const beforeLook = await page.evaluate(() => [
    ...window.__frontier!.camera.position,
  ]);
  await page.mouse.move(
    bounds.x + bounds.width * 0.55,
    bounds.y + bounds.height * 0.53,
    { steps: 5 },
  );
  expect(await page.evaluate(() => window.__frontier!.camera.position)).toEqual(
    beforeLook,
  );
  await page.keyboard.down("w");
  await page.waitForFunction(
    (p) =>
      Math.hypot(
        ...window.__frontier!.camera.position.map((n, i) => n - p[i]),
      ) > 1,
    beforeLook,
  );
  await page.keyboard.up("w");
  await page.mouse.up({ button: "right" });
  const beforeUp = await page.evaluate(
    () => window.__frontier!.camera.position[1],
  );
  await page.keyboard.down("e");
  await page.waitForFunction(
    (y) => window.__frontier!.camera.position[1] > y + 0.5,
    beforeUp,
  );
  await page.keyboard.up("e");
  await page.keyboard.down("w");
  await page.evaluate(() => window.dispatchEvent(new Event("blur")));
  await page.keyboard.up("w");
  expect(await page.evaluate(() => window.__frontier!.camera.moving)).toBe(
    false,
  );
  const position = await page.evaluate(() => [
    ...window.__frontier!.camera.position,
  ]);
  await page.getByRole("spinbutton", { name: "World seed" }).focus();
  await page.keyboard.press("w");
  await page.keyboard.press("e");
  expect(await page.evaluate(() => window.__frontier!.camera.position)).toEqual(
    position,
  );
  await page
    .getByRole("button", { name: "Frame terrain", exact: true })
    .click();
  expect(await page.evaluate(() => window.__frontier!.camera.mode)).toBe("fly");
  expect(
    await page.evaluate(() => window.__frontier!.camera.basis(1.5).eye),
  ).toEqual(original);
});

test("resizing is deferred until a draw, not allowed to clear a paused viewport", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  const previous = await page.evaluate(() => {
    const e = window.__frontier!;
    e.busy = true;
    return { w: e.canvas.width, h: e.canvas.height };
  });
  await page.setViewportSize({ width: 1280, height: 800 });
  await page.waitForTimeout(150);
  expect(
    await page.evaluate(() => ({
      w: window.__frontier!.canvas.width,
      h: window.__frontier!.canvas.height,
    })),
  ).toEqual(previous);
  await page.evaluate(() => {
    const e = window.__frontier!;
    e.setQuality("native");
    e.busy = false;
  });
  await page.waitForFunction(
    () =>
      window.__frontier!.canvas.width ===
      document.querySelector(".viewport-canvas")!.clientWidth,
  );
  expect(
    await page.evaluate(async () => {
      const e = window.__frontier!;
      e.busy = true;
      try {
        await e.backend.sync();
        return await e.backend.verifyFrame((e as any).makeFrame());
      } finally {
        e.busy = false;
      }
    }),
  ).toBe(true);
});

for (const renderer of ["webgpu", "webgl"] as const) {
  test(`${renderer}: adaptive scaling and PNG capture never resize the visible canvas`, async ({
    page,
  }) => {
    await page.goto(`/?renderer=${renderer}`);
    await page.waitForFunction(
      () =>
        window.__frontier?.backend &&
        !document.querySelector(".viewport-loading"),
    );
    expect(await page.evaluate(() => window.__frontier!.backend.name)).toBe(
      renderer === "webgpu" ? "WebGPU" : "WebGL2",
    );
    const initial = await page.evaluate(() => {
      const canvas = window.__frontier!.canvas;
      (window as any).canvasResizes = [];
      const observer = new MutationObserver((records) =>
        (window as any).canvasResizes.push(
          ...records.map((r) => r.attributeName),
        ),
      );
      observer.observe(canvas, {
        attributes: true,
        attributeFilter: ["width", "height"],
      });
      return { width: canvas.width, height: canvas.height };
    });
    for (const quality of [
      "native",
      "adaptive",
      "native",
      "adaptive",
    ] as const) {
      await page.evaluate((q) => window.__frontier!.setQuality(q), quality);
      await page.waitForFunction((q) => {
        const e = window.__frontier!;
        const width = (e as any).frame.width;
        return q === "native"
          ? width === e.canvas.width
          : width < e.canvas.width;
      }, quality);
    }
    expectSandstone(await page.locator(".viewport-canvas").screenshot());
    await page.getByRole("button", { name: "Export", exact: true }).click();
    const pending = page.waitForEvent("download");
    await page.getByRole("menuitem", { name: /Viewport image/ }).click();
    const png = await pending;
    const size = expectSandstone(await readFile((await png.path())!));
    expect(size).toEqual(initial);
    expect(await page.evaluate(() => (window as any).canvasResizes)).toEqual(
      [],
    );
    expectSandstone(await page.locator(".viewport-canvas").screenshot());
    await expect(page.locator(".has-error")).toHaveCount(0);
  });
}

test("a stalled GPU reports recovery instead of silently freezing, but busy work is exempt", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  await page.evaluate(() => {
    const e = window.__frontier!;
    e.busy = true;
    (e as any).frameWaitStarted = performance.now() - 20_000;
  });
  await page.waitForFunction(
    () => (window.__frontier as any).frameWaitStarted === 0,
  );
  await expect(page.locator(".has-error")).toHaveCount(0);
  await page.evaluate(() => {
    const e = window.__frontier!;
    e.backend.ready = () => false;
    (e as any).frameWaitStarted = performance.now() - 20_000;
    e.busy = false;
  });
  await expect(page.locator(".has-error")).toContainText("stopped responding");
  await expect(
    page.getByRole("button", { name: "Try compatibility renderer" }),
  ).toBeVisible();
  await expect(page.locator(".fps")).toContainText("0 FPS");
});

test("GPU device loss exposes a working compatibility recovery action", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  expect(await page.evaluate(() => window.__frontier!.backend.name)).toBe(
    "WebGPU",
  );
  await page.evaluate(() =>
    (window.__frontier!.backend as any).device.destroy(),
  );
  await expect(page.locator(".has-error")).toBeVisible();
  await expect(page.locator(".gpu-status")).toContainText(
    "Graphics interrupted",
  );
  await page
    .getByRole("button", { name: "Try compatibility renderer" })
    .click();
  await page.waitForFunction(
    () =>
      window.__frontier?.backend?.name === "WebGL2" &&
      !document.querySelector(".viewport-loading"),
  );
  expectSandstone(await page.locator(".viewport-canvas").screenshot());
});

test("a compute failure is caught by the same visible recovery UI as a render failure", async ({
  page,
}) => {
  const unhandled: string[] = [];
  page.on("pageerror", (error) => unhandled.push(error.message));
  await page.goto("/");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  await page.evaluate(() => {
    const e = window.__frontier!;
    e.backend.step = () => {
      throw new Error("Injected erosion submission failure");
    };
    e.running = true;
  });
  await expect(page.locator(".has-error")).toContainText(
    "Injected erosion submission failure",
  );
  expect(await page.evaluate(() => window.__frontier!.running)).toBe(false);
  expect(unhandled).toEqual([]);
});
