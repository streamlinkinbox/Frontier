import { test, expect, type Page } from "@playwright/test";
import { PNG } from "pngjs";
import { readFile } from "node:fs/promises";
async function ready(page: Page) {
  await page.goto("/material-editor.html?quality=draft");
  await page.waitForFunction(
    () =>
      ((window as any).__materialLab &&
        !document.querySelector(".lab-loading")) ||
      document.querySelector(".lab-error"),
  );
  await expect(page.locator(".lab-error")).toHaveCount(0);
  await expect(page).toHaveTitle("Material Editor — Frontier");
}
async function capture(page: Page, view: string, detail = true) {
  return Buffer.from(
    await page.evaluate(
      async ({ view, detail }) => {
        const blob = await (window as any).__materialLab.renderer.capture(
          view,
          detail,
        );
        return new Promise<string>((resolve) => {
          const r = new FileReader();
          r.onload = () => resolve((r.result as string).split(",")[1]);
          r.readAsDataURL(blob);
        });
      },
      { view, detail },
    ),
    "base64",
  );
}
const difference = (a: Buffer, b: Buffer) => {
  const x = PNG.sync.read(a).data,
    y = PNG.sync.read(b).data;
  let changed = 0,
    sum = 0;
  for (let i = 0; i < x.length; i += 4) {
    const d =
      Math.abs(x[i] - y[i]) +
      Math.abs(x[i + 1] - y[i + 1]) +
      Math.abs(x[i + 2] - y[i + 2]);
    if (d > 0) changed++;
    sum += d;
  }
  return { changed, mean: sum / ((x.length / 4) * 3) };
};

test("SDF material editor: GPU field agrees with CPU geometry for all six specimens", async ({
  page,
}) => {
  const errors: string[] = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await ready(page);
  const result = await page.evaluate(async () => {
    const { STONE_PRESETS } = await import("/src/material-lab/model.ts");
    const { stoneBase, stoneField } =
      await import("/src/material-lab/field.ts");
    const r = (window as any).__materialLab.renderer;
    let worst = 0,
      changed = 0;
    for (const preset of STONE_PRESETS) {
      const s = { ...preset.values, quality: "draft" };
      r.update(s);
      const points: [number, number, number][] = [];
      for (let i = 0; i < 24; i++) {
        const n = [
          Math.cos(i * 2.399),
          0.25 + (i % 7) * 0.12,
          Math.sin(i * 2.399),
        ];
        const len = Math.hypot(...n);
        let lo = 0,
          hi = 0.47;
        for (let j = 0; j < 24; j++) {
          const mid = (lo + hi) * 0.5;
          const p = n.map((x) => (x / len) * mid) as [number, number, number];
          if (stoneBase(p, s) > 0) hi = mid;
          else lo = mid;
        }
        points.push(
          n.map(
            (x) => (x / len) * ((lo + hi) * 0.5 + ((i % 3) - 1) * 0.001),
          ) as [number, number, number],
        );
      }
      const actual = r.probe(points, true),
        base = r.probe(points, false),
        fp = r.getDiagnostics().footprint;
      points.forEach((p, i) => {
        worst = Math.max(
          worst,
          Math.abs(actual[i] - stoneField(p, s, fp)),
          Math.abs(base[i] - stoneBase(p, s)),
        );
        if (Math.abs(actual[i] - base[i]) > 0.0001) changed++;
      });
    }
    return {
      worst,
      changed,
      diag: r.getDiagnostics(),
      terrainHook: !!window.__frontier,
    };
  });
  expect(result.worst).toBeLessThan(0.00003);
  expect(result.changed).toBeGreaterThan(30);
  expect(result.diag.materialTextures).toBe(1);
  expect(result.diag.glError).toBe(0);
  expect(result.terrainHook).toBe(false);
  expect(errors).toEqual([]);
});

test("detail changes the silhouette and clay surface; SatMap edits change only albedo", async ({
  page,
}, info) => {
  const images: string[] = [];
  page.on("request", (r) => {
    if (/\.(png|jpe?g|webp)(\?|$)/i.test(r.url())) images.push(r.url());
  });
  await ready(page);
  await page.evaluate(() => {
    const lab = (window as any).__materialLab;
    lab.renderer.setCamera(0.4, 0.45, 0.95);
    lab.renderer.update({
      ...lab.getSettings(),
      quality: "draft",
      chips: 10,
      bedding: 1.2,
      grain: 0.3,
      porosity: 0.45,
      compare: false,
    });
  });
  const base = await capture(page, "silhouette", false),
    detail = await capture(page, "silhouette", true);
  expect(difference(base, detail).changed).toBeGreaterThan(35);
  const clayBase = await capture(page, "clay", false),
    clay = await capture(page, "clay", true);
  expect(difference(clayBase, clay).mean).toBeGreaterThan(0.4);
  const albedo = await capture(page, "albedo", true);
  await page.evaluate(() => {
    const lab = (window as any).__materialLab;
    lab.renderer.update({
      ...lab.getSettings(),
      quality: "draft",
      chips: 10,
      bedding: 1.2,
      grain: 0.3,
      porosity: 0.45,
      compare: false,
      palette: "fantasy-amethyst",
    });
  });
  expect(
    difference(detail, await capture(page, "silhouette", true)).changed,
  ).toBe(0);
  expect(
    difference(albedo, await capture(page, "albedo", true)).mean,
  ).toBeGreaterThan(3);
  const steps = PNG.sync.read(await capture(page, "steps", true));
  let exhausted = 0;
  for (let i = 0; i < steps.data.length; i += 4)
    if (steps.data[i] > 200 && steps.data[i + 1] < 100) exhausted++;
  expect(exhausted / (steps.width * steps.height)).toBeLessThan(0.01);
  await info.attach("geometric-detail-clay", {
    body: clay,
    contentType: "image/png",
  });
  await info.attach("detailed-silhouette", {
    body: detail,
    contentType: "image/png",
  });
  expect(images).toEqual([]);
});

test("stone controls, comparison, safe recipes and mobile navigation stay independent of terrain", async ({
  page,
}, info) => {
  await page.addInitScript(() =>
    localStorage.setItem("frontier.terrain.untouched", "preserve-this"),
  );
  await ready(page);
  await page.getByRole("button", { name: /Vesicular basalt/ }).click();
  await expect(page.getByLabel("Stone preview quality")).toHaveValue("draft");
  await page
    .getByRole("spinbutton", { name: "Chipped relief numeric value" })
    .fill("9");
  await page.getByLabel("Compare smooth / detailed", { exact: true }).check();
  await expect(page.locator(".lab-split-line")).toBeVisible();
  await page.getByLabel("SDF surface detail", { exact: true }).uncheck();
  await expect(
    page.getByRole("slider", { name: "Grain relief", exact: true }),
  ).toBeDisabled();
  await page.getByLabel("SDF surface detail", { exact: true }).check();
  const before = await page.evaluate(() =>
    (window as any).__materialLab.getSettings(),
  );
  const pending = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Export recipe", exact: true })
    .click();
  const bytes = await readFile((await (await pending).path())!);
  expect(JSON.parse(bytes.toString()).settings).toEqual(before);
  await page.getByRole("button", { name: "Reset stone material" }).click();
  await page
    .getByLabel("Import stone recipe")
    .setInputFiles({
      name: "stone.json",
      mimeType: "application/json",
      buffer: bytes,
    });
  await expect(
    page.getByRole("spinbutton", { name: "Chipped relief numeric value" }),
  ).toHaveValue("9");
  await page.getByRole("button", { name: "Save stone recipe" }).click();
  expect(
    await page.evaluate(() =>
      localStorage.getItem("frontier.terrain.untouched"),
    ),
  ).toBe("preserve-this");
  await page
    .getByLabel("Import stone recipe")
    .setInputFiles({
      name: "bad.json",
      mimeType: "application/json",
      buffer: Buffer.from('{"kind":"wrong","settings":{}}'),
    });
  await expect(page.getByRole("status")).toContainText("Unsupported");
  expect(
    await page.evaluate(() => (window as any).__materialLab.getSettings()),
  ).toEqual(before);
  await page.setViewportSize({ width: 390, height: 844 });
  await page
    .getByRole("button", { name: "Surface controls", exact: true })
    .click();
  await expect(page.getByLabel("Stone SatMap palette")).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    390,
  );
  await page.screenshot({
    path: info.outputPath("material-editor-mobile.png"),
  });
});

test("missing WebGL2 explains the problem while recipe controls remain available", async ({
  page,
}) => {
  await page.addInitScript(() => {
    const original = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function (...args: any[]) {
      return args[0] === "webgl2" ? null : original.apply(this, args as any);
    } as any;
  });
  await page.goto("/material-editor.html?quality=draft");
  await expect(page.getByRole("alert")).toContainText("WebGL2 is unavailable");
  await expect(
    page.getByRole("button", { name: "Export recipe", exact: true }),
  ).toBeEnabled();
});
