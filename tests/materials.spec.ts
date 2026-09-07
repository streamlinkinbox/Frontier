import { test, expect } from "@playwright/test";
import { PNG } from "pngjs";

function changed(a: Buffer, b: Buffer) {
  const left = PNG.sync.read(a),
    right = PNG.sync.read(b);
  let changes = 0;
  for (let i = 0; i < left.data.length; i += 4)
    if (
      Math.abs(left.data[i] - right.data[i]) +
        Math.abs(left.data[i + 1] - right.data[i + 1]) +
        Math.abs(left.data[i + 2] - right.data[i + 2]) >
      20
    )
      changes++;
  return changes / (left.width * left.height);
}

test("WebGL water moves organically, clarity changes the shallows, and new erosion controls are usable", async ({
  page,
}, testInfo) => {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend?.name === "WebGL2" &&
      !document.querySelector(".viewport-loading"),
  );
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.pause();
    e.busy = true;
    await e.backend.sync();
    e.camera.lookAt([-9, 5.2, 29], [-1, 3.7, -5]);
    e.update({
      ...e.settings,
      waterLevel: 2.65,
      wind: 0.66,
      autoPreview: false,
      waterClarity: 0.8,
    });
    e.backend.render({ ...(e as any).makeFrame(), time: 1 });
    await e.backend.sync();
  });
  const first = await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("shoreline-clear.png") });
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.backend.render({ ...(e as any).makeFrame(), time: 5 });
    await e.backend.sync();
  });
  const moving = await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("shoreline-waves.png") });
  expect(changed(first, moving)).toBeGreaterThan(0.004);
  await page.evaluate(async () => {
    const e = window.__frontier!;
    e.update({ ...e.settings, waterClarity: 0 });
    e.backend.render({ ...(e as any).makeFrame(), time: 1 });
    await e.backend.sync();
  });
  const turbid = await page
    .locator(".viewport-canvas")
    .screenshot({ path: testInfo.outputPath("shoreline-turbid.png") });
  expect(changed(first, turbid)).toBeGreaterThan(0.005);
  await page.evaluate(() => {
    window.__frontier!.busy = false;
  });
  await page.getByText("Scour & sediment controls", { exact: true }).click();
  await page.getByRole("slider", { name: "Rock cohesion" }).focus();
  await page.keyboard.press("End");
  expect(await page.evaluate(() => window.__frontier!.settings.cohesion)).toBe(
    1,
  );
  await page.getByRole("tab", { name: "Thermal", exact: true }).click();
  await page.getByRole("slider", { name: "Talus angle" }).focus();
  await page.keyboard.press("End");
  expect(
    await page.evaluate(() => window.__frontier!.settings.talusAngle),
  ).toBe(55);
  await page.getByRole("tab", { name: "Environment", exact: true }).click();
  await page.getByRole("slider", { name: "Water clarity" }).focus();
  await page.keyboard.press("Home");
  expect(
    await page.evaluate(() => window.__frontier!.settings.waterClarity),
  ).toBe(0);
  expect(errors).toEqual([]);
});

test("GPU and CPU erosion/settling/talus agree on the same small volumetric fixture", async ({
  page,
}) => {
  await page.goto("/?renderer=webgl");
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
  const comparison = await page.evaluate(async () => {
    const [
      { WebGPUBackend },
      { computeFlux, evolveField, computeTalusFlux, settleTalus },
      { DEFAULT_SETTINGS },
    ] = await Promise.all([
      import("/src/engine/WebGPUBackend.ts"),
      import("/src/engine/simulation.ts"),
      import("/src/engine/types.ts"),
    ]);
    window.__frontier!.busy = true;
    const size = { x: 24, y: 12, z: 24 },
      settings = {
        ...DEFAULT_SETTINGS,
        rainfall: 0.4,
        cohesion: 0.15,
        settling: 0.6,
        thermal: 0.7,
        windErosion: 0.8,
        windDirection: 0,
      };
    const data = new Float32Array(size.x * size.y * size.z * 4);
    for (let z = 0; z < size.z; z++)
      for (let y = 0; y < size.y; y++)
        for (let x = 0; x < size.x; x++) {
          const i = ((z * size.y + y) * size.x + x) * 4;
          data[i] = Math.max(
            -24,
            Math.min(32, (y - 5.5) * 4 * 0.6 - (x - 12) * 4 * 0.8),
          );
          if (
            x > 3 &&
            x < 20 &&
            y > 3 &&
            y < 9 &&
            z > 3 &&
            z < 20 &&
            data[i] > -2
          ) {
            data[i + 1] = 0.25;
            data[i + 2] = 0.03125;
          }
        }
    let cpu = data.slice(),
      scratch = data.slice();
    const flux = new Float32Array(data.length);
    computeFlux(cpu, size, flux, settings);
    evolveField(cpu, size, settings, flux, scratch);
    [cpu, scratch] = [scratch, cpu];
    computeTalusFlux(cpu, size, settings, flux);
    settleTalus(cpu, size, flux, scratch);
    cpu = scratch;
    const gpu = new WebGPUBackend(document.createElement("canvas"), "safe");
    Object.assign(gpu.size, size);
    try {
      await gpu.initialize(settings);
      gpu.writeVolume(data);
      gpu.step(settings, 1);
      const result = await gpu.readVolume();
      let distance = 0,
        water = 0,
        sediment = 0;
      for (let i = 0; i < data.length; i += 4) {
        distance = Math.max(distance, Math.abs(cpu[i] - result[i]));
        water = Math.max(water, Math.abs(cpu[i + 1] - result[i + 1]));
        sediment = Math.max(sediment, Math.abs(cpu[i + 2] - result[i + 2]));
      }
      return {
        distance,
        water,
        sediment,
        finite: result.every(Number.isFinite),
      };
    } finally {
      gpu.dispose();
      window.__frontier!.busy = false;
    }
  });
  expect(comparison.finite).toBe(true);
  expect(comparison.distance).toBeLessThan(0.03); // RGBA16F storage rounds between GPU passes.
  expect(comparison.water).toBeLessThan(0.001);
  expect(comparison.sediment).toBeLessThan(0.001);
});
