import {
  geometricSetTwo,
  geometricSetThree,
  geometricSetFour,
} from "../src/geometricConstructions.js";
import { patternStarter } from "../src/patternDocument.js";
import { test, expect } from "@playwright/test";
import { readFile } from "node:fs/promises";
import * as THREE from "three";
import { unzipSync, strFromU8 } from "fflate";
import { materials } from "../src/materials.js";

const htmlPath = new URL("../site/index.html", import.meta.url);
const publicURL = process.env.ALLOY_PUBLIC_URL;
const localURL = "https://alloy-standalone.invalid/site/index.html";

async function downloadText(page, name) {
  await page.getByRole("button", { name: /Export material/ }).click();
  const downloading = page.waitForEvent("download");
  await page.getByRole("button", { name }).click();
  const stream = await (await downloading).createReadStream();
  const chunks = [];
  for await (const chunk of stream) chunks.push(chunk);
  return Buffer.concat(chunks).toString("utf8");
}

async function frame(page) {
  await expect(page.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page.evaluate(
    () =>
      new Promise((resolve) =>
        requestAnimationFrame(() => requestAnimationFrame(resolve)),
      ),
  );
}

// ALLOY_PUBLIC_URL enables a genuine remote check. Connection failures fail the
// test; it never silently substitutes the local build for an unreachable host.
test("standalone page renders, edits and exports without external assets", async ({
  page,
}, testInfo) => {
  test.setTimeout(600000);
  const html = await readFile(htmlPath, "utf8");
  const url = publicURL || localURL;
  const errors = [],
    unexpectedRequests = [];
  page.on("pageerror", (error) => errors.push(error.message));
  page.on("console", (message) => {
    if (message.type() === "error") errors.push(message.text());
  });
  await page.route("**/*", async (route) => {
    const request = route.request();
    if (request.url() === url && request.isNavigationRequest()) {
      if (publicURL) return route.continue();
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    }
    if (!/^https?:/.test(request.url())) return route.continue();
    unexpectedRequests.push(request.url());
    return route.abort("blockedbyclient");
  });

  const response = await page.goto(url);
  expect(response.status()).toBe(200);
  if (
    publicURL &&
    (await page.getByText("One more step", { exact: true }).isVisible())
  ) {
    // GitHack documents this notice for HTML pages. Confirm it like a visitor;
    // don't change the served content, suppress errors or use a local fallback.
    await page
      .getByRole("button", { name: "Open the page", exact: true })
      .click();
  }
  await expect(page.locator(".material-preview img")).toHaveCount(
    materials.length,
    { timeout: 300000 },
  );
  const thumbnails = await page
    .locator(".material-preview img")
    .evaluateAll((images) => images.map((img) => img.src));
  expect(new Set(thumbnails).size).toBe(materials.length);
  await expect(page.locator("h1")).toHaveText("Racing Green");
  await expect(
    page.locator("script[src], link[rel=stylesheet][href]"),
  ).toHaveCount(0);

  await page
    .getByRole("button", { name: "Apply Natural Cotton", exact: true })
    .click();
  await expect(page.getByLabel("Preview object")).toHaveValue("Draped cloth");
  await page.getByLabel("Thread scale value", { exact: true }).fill("12");
  await page.getByLabel("Weave construction").selectOption("herringbone");
  await page.getByLabel("Warp yarn · lengthwise hex").fill("#344f81");
  await page.getByLabel("Weft yarn · crosswise hex").fill("#dec39b");
  const cloth = JSON.parse(
    await downloadText(page, /Material preset All surface/),
  );
  expect(cloth.schema).toBe("alloy.material.v6");
  expect(cloth.material).toMatchObject({
    weavePattern: "herringbone",
    warpColor: "#344f81",
    weftColor: "#dec39b",
    detailScale: 12,
  });

  await page
    .getByRole("button", { name: "Apply Aurora Flip", exact: true })
    .click();
  await page.getByLabel("Preview object").selectOption("Shader ball");
  await page
    .getByLabel("Color-shift strength value", { exact: true })
    .fill("0");
  await frame(page);
  const before = await page
    .locator("canvas")
    .evaluate((canvas) => canvas.toDataURL());
  await page
    .getByLabel("Color-shift strength value", { exact: true })
    .fill("100");
  await page.getByLabel("Color phase value", { exact: true }).fill("68");
  await frame(page);
  const after = await page
    .locator("canvas")
    .evaluate((canvas) => canvas.toDataURL());
  expect(after).not.toBe(before);

  const exported = await downloadText(page, /Three.js procedural shader/);
  const factory = exported
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(/export const /g, "const ")
    .replace(/export (async )?function /g, "$1function ")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    );
  const material = new Function("THREE", factory)(THREE);
  try {
    expect(material.isMeshPhysicalMaterial).toBe(true);
    expect(material.iridescence).toBe(1);
    expect(material.userData.params.filmThickness).toBeCloseTo(629.2);
    expect(material.userData.params.recipeId).toBe("paint");
    const shader = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    material.onBeforeCompile(shader);
    expect(shader.uniforms.uFilmThickness.value).toBeCloseTo(629.2);
    expect(shader.fragmentShader).toContain(
      "material.iridescenceThickness=uFilmThickness",
    );
  } finally {
    material.dispose();
  }
  await page
    .getByRole("button", { name: "Apply Broadleaf Green", exact: true })
    .click();
  await frame(page);
  const leafSource = await downloadText(page, /Three.js procedural shader/);
  const leafFactory = leafSource
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(/export const /g, "const ")
    .replace(/export (async )?function /g, "$1function ")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    );
  const leafMaterial = new Function("THREE", leafFactory)(THREE);
  try {
    const shader = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    leafMaterial.onBeforeCompile(shader);
    expect(shader.fragmentShader).toContain("#define uType 31");
    expect(shader.fragmentShader).toContain("bioCell");
    expect(shader.uniforms.uCellScale.value).toBeGreaterThan(20);
    expect(leafMaterial.alphaMap).toBeNull();
  } finally {
    leafMaterial.dispose();
  }
  await page.getByRole("button", { name: /Export material/ }).click();
  await page.getByRole("button", { name: /Bake procedural maps/ }).click();
  await page.getByLabel("Bake resolution").selectOption("256");
  const baking = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Bake & download ZIP", exact: true })
    .click();
  const stream = await (await baking).createReadStream(),
    parts = [];
  for await (const chunk of stream) parts.push(chunk);
  const maps = unzipSync(Buffer.concat(parts));
  expect(Object.keys(maps)).toHaveLength(8);
  expect(JSON.parse(strFromU8(maps["material.json"])).schema).toBe(
    "alloy.surface-bake.v1",
  );
  expect(
    JSON.parse(strFromU8(maps["material.json"])).domain.projection,
  ).toContain("UV0");
  expect(Buffer.from(maps["normal.png"]).subarray(1, 4).toString()).toBe("PNG");
  await page.getByRole("button", { name: "Close baking studio" }).click();
  await page
    .getByRole("button", { name: "Apply Pure Aluminium", exact: true })
    .click();
  await page.getByRole("switch", { name: "Enable metal scratches" }).check();
  await page.getByLabel("Scratch density value", { exact: true }).fill("60");
  await frame(page);
  const scratchedSource = await downloadText(
    page,
    /Three.js procedural shader/,
  );
  const scratchedFactory = scratchedSource
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(/export const /g, "const ")
    .replace(/export (async )?function /g, "$1function ")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    );
  const scratchedMaterial = new Function("THREE", scratchedFactory)(THREE);
  try {
    expect(scratchedMaterial.userData.params.metalScratches).toBe(true);
    expect(scratchedMaterial.userData.params.scratchDensity).toBeCloseTo(7.2);
    const shader = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    scratchedMaterial.onBeforeCompile(shader);
    expect(shader.fragmentShader).toContain("#define uMetalScratches 1");
    expect(shader.fragmentShader).toContain("surfaceHeight+=metalCuts.y");
  } finally {
    scratchedMaterial.dispose();
  }
  await page
    .getByRole("button", { name: "Apply Crocodile Belly Leather", exact: true })
    .click();
  await expect(page.getByLabel("Preview object")).toHaveValue("Leather swatch");
  await frame(page);
  const leatherSource = await downloadText(page, /Three.js procedural shader/);
  const leatherFactory = leatherSource
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(/export const /g, "const ")
    .replace(/export (async )?function /g, "$1function ")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    );
  const leatherMaterial = new Function("THREE", leatherFactory)(THREE);
  try {
    const shader = {
      uniforms: {},
      vertexShader: THREE.ShaderLib.physical.vertexShader,
      fragmentShader: THREE.ShaderLib.physical.fragmentShader,
    };
    leatherMaterial.onBeforeCompile(shader);
    expect(shader.fragmentShader).toContain("#define uType 30");
    expect(shader.fragmentShader).toContain("vectorHide");
    expect(leatherMaterial.map).toBeNull();
  } finally {
    leatherMaterial.dispose();
  }
  if (process.env.ALLOY_CAPTURE === "leather") {
    for (const name of ["Crocodile Belly Leather", "Cognac Leather"]) {
      await page
        .getByRole("button", { name: "Apply " + name, exact: true })
        .click();
      await page.getByRole("button", { name: "Fit", exact: true }).click();
      await page.getByLabel("Viewport zoom").fill("2.32");
      await frame(page);
      await page
        .locator(".inspector-scroll")
        .evaluate((e) => (e.scrollTop = 0));
      await page.screenshot({
        path: ".playwright/review/" + name.replaceAll(" ", "-") + ".png",
      });
    }
  }
  if (process.env.ALLOY_CAPTURE === "1") {
    await page
      .getByRole("button", { name: "Apply Raw Selvedge Denim", exact: true })
      .click();
    await frame(page);
    await page.screenshot({ path: testInfo.outputPath("denim-fit.png") });
    await page.getByRole("button", { name: "Macro", exact: true }).click();
    await frame(page);
    await page.screenshot({ path: testInfo.outputPath("denim-macro.png") });
  }
  if (process.env.ALLOY_CAPTURE === "v6") {
    for (const name of ["Scratches", "LED Pixel Matrix"]) {
      await page.getByRole("button", { name: "Fit", exact: true }).click();
      await page
        .getByRole("button", { name: "Apply " + name, exact: true })
        .click();
      await frame(page);
      await page
        .locator(".inspector-scroll")
        .evaluate((el) => (el.scrollTop = 0));
      await page.screenshot({
        path: testInfo.outputPath(name.replaceAll(" ", "-") + "-fit.png"),
      });
      await page.getByRole("button", { name: "Macro", exact: true }).click();
      await frame(page);
      await page.screenshot({
        path: testInfo.outputPath(name.replaceAll(" ", "-") + "-macro.png"),
      });
    }
  }
  await page
    .getByRole("button", { name: "Pattern studio", exact: true })
    .click();
  await page.getByLabel("Pattern collection").selectOption("Originals");
  await page
    .getByRole("button", { name: "Painted blossoms", exact: true })
    .click();
  await page.getByLabel("Pattern base material").selectOption("pottery");
  await page
    .getByLabel("Motif material", { exact: true })
    .selectOption("ceramic");
  await page.getByLabel("Relief (mm)", { exact: true }).fill("0");
  await page
    .getByRole("button", { name: "Assign this finish to all motifs" })
    .click();
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  const editorPreview = page.getByRole("region", {
    name: "Pattern material preview",
  });
  await expect(
    editorPreview.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await expect(editorPreview.locator("canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page.getByLabel("Pattern preview zoom").fill("160");
  await page
    .getByRole("button", { name: "Apply to material", exact: false })
    .click();
  await expect(page.getByLabel("Preview object")).toHaveValue("Teapot");
  await frame(page);
  const decoratedSource = await downloadText(
    page,
    /Three.js procedural shader/,
  );
  const decoratedFactory = decoratedSource
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    )
    .replace(/export /g, "");
  const decorated = new Function("THREE", decoratedFactory)(THREE);
  expect(decorated.userData.params.pattern.layers.length).toBeGreaterThan(10);
  expect(decorated.userData.params.recipeId).toBe("pottery");
  decorated.dispose();
  // New collections must also work in the embedded page and independent export.
  await page
    .getByRole("button", { name: "Pattern studio", exact: true })
    .click();
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "legacy-fade.json",
    mimeType: "application/json",
    buffer: Buffer.from(JSON.stringify(patternStarter("Golden Cube Fade"))),
  });
  await page.getByLabel("Fade direction").selectOption("right");
  await page.getByRole("button", { name: /Apply to material/ }).click();
  await frame(page);
  const fadeSource = await downloadText(page, /Three.js procedural shader/);
  const fadeFactory = fadeSource
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    )
    .replace(/export /g, "");
  const fadeMaterial = new Function("THREE", fadeFactory)(THREE);
  expect(fadeMaterial.userData.params.pattern.fade.direction).toBe("right");
  const fadeShader = {
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  };
  fadeMaterial.onBeforeCompile(fadeShader);
  expect(fadeShader.uniforms.uPatternColor.value.wrapS).toBe(
    THREE.ClampToEdgeWrapping,
  );
  expect(fadeShader.uniforms.uPatternColor.value.wrapT).toBe(
    THREE.RepeatWrapping,
  );
  fadeMaterial.dispose();
  await page
    .getByRole("button", { name: "Pattern studio", exact: true })
    .click();
  await page.getByLabel("Pattern collection").selectOption("Stitch patterns");
  await expect(page.getByLabel("Textile family colorway")).toHaveCount(0);
  await page.getByLabel("Search patterns").fill("Chain");
  await page.getByRole("button", { name: "Chain stitch", exact: true }).click();
  await page.getByLabel("Stitch placement").selectOption("diagonal");
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await page.getByRole("button", { name: /Apply to material/ }).click();
  await frame(page);
  const stitchSource = await downloadText(page, /Three.js procedural shader/);
  const stitchFactory = stitchSource
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    )
    .replace(/export /g, "");
  const stitched = new Function("THREE", stitchFactory)(THREE);
  expect(stitched.userData.params.pattern.stitch).toMatchObject({
    type: "Chain stitch",
    layout: "diagonal",
    enabled: true,
  });
  expect(
    stitched.userData.params.pattern.layers.filter((l) => l.stitchRole),
  ).toHaveLength(3);
  stitched.dispose();
  // Ensure exported helpers, not only the serialized preset, are self-contained.
  const rebuilt = new Function(
    "THREE",
    stitchFactory.replace(
      "return createMaterial(preset);",
      "return createMaterial({...preset,pattern:patternStarter('Plain weave - Earth')});",
    ),
  )(THREE);
  expect(rebuilt.userData.params.pattern.library.family).toBe("Plain weave");
  rebuilt.dispose();
  const beaded = new Function(
    "THREE",
    stitchFactory.replace(
      "return createMaterial(preset);",
      "return createMaterial({...preset,pattern:patternStarter('Beaded Diamond Weave')});",
    ),
  )(THREE);
  expect(beaded.userData.params.pattern.beadwork).toEqual({
    columns: 84,
    rows: 64,
    height: 0.004,
  });
  const beadShader = {
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  };
  beaded.onBeforeCompile(beadShader);
  expect(beadShader.uniforms.uPatternBeads.value.toArray()).toEqual([
    84, 64, 0.004,
  ]);
  beaded.dispose();
  const inlay = new Function(
    "THREE",
    stitchFactory.replace(
      "return createMaterial(preset);",
      "return createMaterial({...preset,pattern:patternStarter('Four Gate Marquetry')});",
    ),
  )(THREE);
  expect(inlay.userData.params.pattern.construction).toBe(
    "four-gate-marquetry",
  );
  expect(inlay.userData.params.pattern.layers.length).toBeGreaterThan(15);
  inlay.dispose();

  await page
    .getByRole("button", { name: "Pattern studio", exact: true })
    .click();
  await page.locator('input[accept=".json"]').setInputFiles({
    name: "legacy-rug.json",
    mimeType: "application/json",
    buffer: Buffer.from(
      JSON.stringify(patternStarter("Saffron Rosette Court")),
    ),
  });
  await page.getByLabel("Rug detail level").selectOption("3");
  await page.getByRole("button", { name: "3D material", exact: true }).click();
  await expect(
    page.getByRole("status", { name: "Material preview status" }),
  ).toContainText("Live material · ready");
  await page.getByRole("button", { name: /Apply to material/ }).click();
  await frame(page);
  const rugSource = await downloadText(page, /Three.js procedural shader/);
  const rugFactory = rugSource
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    )
    .replace(/export /g, "");
  const rugMaterial = new Function("THREE", rugFactory)(THREE);
  expect(rugMaterial.userData.params.pattern.ornament).toMatchObject({
    id: "saffron-rosette-court",
    detail: 3,
  });
  expect(rugMaterial.userData.params.pattern.tileAxes).toBe("none");
  const rugShader = {
    uniforms: {},
    vertexShader: THREE.ShaderLib.physical.vertexShader,
    fragmentShader: THREE.ShaderLib.physical.fragmentShader,
  };
  rugMaterial.onBeforeCompile(rugShader);
  expect(rugShader.uniforms.uPatternColor.value.wrapS).toBe(
    THREE.ClampToEdgeWrapping,
  );
  expect(rugShader.uniforms.uPatternColor.value.wrapT).toBe(
    THREE.ClampToEdgeWrapping,
  );
  rugMaterial.dispose();
  const rugHelper = new Function(
    "THREE",
    rugFactory.replace(
      "return createMaterial(preset);",
      "return createMaterial({...preset,pattern:patternStarter('Raffia Labyrinth Panels')});",
    ),
  )(THREE);
  expect(rugHelper.userData.params.pattern.ornament.id).toBe(
    "raffia-labyrinth-panels",
  );
  rugHelper.dispose();
  // New modules must be present in the independent export, not just Vite.
  for (const name of [
    "Truchet circuits",
    "Satin weave",
    ...geometricSetTwo.map((p) => p.name),
    ...geometricSetThree.map((p) => p.name),
    ...geometricSetFour.map((p) => p.name),
  ]) {
    const made = new Function(
      "THREE",
      stitchFactory.replace(
        "return createMaterial(preset);",
        `return createMaterial({...preset,pattern:patternStarter('${name}')});`,
      ),
    )(THREE);
    expect(made.userData.params.pattern.name).toBe(name);
    if (name === "Satin weave")
      expect(made.userData.params.pattern.weave.draft).toBe("Five shaft satin");
    made.dispose();
  }
  expect(unexpectedRequests).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone shape authoring keeps native Bézier nodes and exports a self-contained material", async ({
  page,
}) => {
  test.setTimeout(180000);
  const html = await readFile(htmlPath, "utf8"),
    url =
      "https://alloy-standalone.invalid/native.html?studio=pattern&pattern=blank&material=natural-cotton";
  const errors = [],
    requests = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.route("**/*", (route) => {
    if (route.request().url() === url)
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    if (!/^https?:/.test(route.request().url())) return route.continue();
    requests.push(route.request().url());
    return route.abort();
  });
  await page.goto(url);
  const coordinate = (x, y) =>
    page.locator('[data-coordinate-space="tile"]').evaluate(
      (g, p) => {
        const v = new DOMPoint(p.x, p.y).matrixTransform(g.getScreenCTM());
        return { x: v.x, y: v.y };
      },
      { x, y },
    );
  await page.getByRole("button", { name: "Pen tool (P)", exact: true }).click();
  for (const [x, y] of [
    [90, 340],
    [256, 110],
    [420, 340],
  ]) {
    const p = await coordinate(x, y);
    await page.mouse.move(p.x, p.y);
    await page.mouse.down();
    if (x === 256) {
      const h = await coordinate(320, y);
      await page.mouse.move(h.x, h.y);
    }
    await page.mouse.up();
  }
  await page.keyboard.press("Enter");
  await page.getByLabel("Pattern name").fill("Standalone native motif");
  await page.getByLabel("Stroke width", { exact: true }).fill("8");
  await page.getByLabel("Motif stroke color", { exact: true }).fill("#ce8150");
  await page.getByLabel("Motif material", { exact: true }).selectOption("foil");
  const saved = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const chunks = [];
  for await (const chunk of await (await saved).createReadStream())
    chunks.push(chunk);
  const doc = JSON.parse(Buffer.concat(chunks).toString());
  expect(doc.layers[0].nodes).toHaveLength(3);
  expect(doc.layers[0].path).toContain(" C");
  expect(doc.layers[0]).toMatchObject({
    paint: true,
    stroke: "#ce8150",
    strokeWidth: 8,
    finish: "foil",
  });
  await page
    .getByRole("button", { name: "Apply to material", exact: false })
    .click();
  await frame(page);
  const source = await downloadText(page, /Three.js procedural shader/);
  const factory = source
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(
      /export default createMaterial\(preset\);/,
      "return { material: createMaterial(preset), svg: patternSVG(preset.pattern) };",
    )
    .replace(/export /g, "");
  const exported = new Function("THREE", factory)(THREE);
  expect(exported.material.userData.params.pattern).toEqual(doc);
  expect(exported.svg).toContain('stroke="#ce8150"');
  expect(exported.svg).not.toContain("vector-effect");
  exported.material.dispose();
  expect(requests).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone curvature paths retain automatic point types and tension in SVG and shader exports", async ({
  page,
}) => {
  test.setTimeout(180000);
  const html = await readFile(htmlPath, "utf8"),
    url =
      "https://alloy-standalone.invalid/curve.html?studio=pattern&pattern=blank&material=natural-cotton";
  const errors = [],
    requests = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.route("**/*", (route) => {
    if (route.request().url() === url)
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    if (!/^https?:/.test(route.request().url())) return route.continue();
    requests.push(route.request().url());
    return route.abort();
  });
  await page.goto(url);
  await page
    .getByRole("button", { name: "Curve tool (U)", exact: true })
    .click();
  for (const [x, y] of [
    [90, 340],
    [175, 140],
    [345, 150],
    [420, 340],
  ]) {
    const p = await page.locator('[data-coordinate-space="tile"]').evaluate(
      (g, p) => {
        const v = new DOMPoint(p.x, p.y).matrixTransform(g.getScreenCTM());
        return { x: v.x, y: v.y };
      },
      { x, y },
    );
    await page.mouse.click(p.x, p.y);
  }
  await page
    .getByRole("slider", { name: "Curve tension", exact: true })
    .focus();
  await page.keyboard.press("Home");
  for (let i = 0; i < 60; i++) await page.keyboard.press("ArrowRight");
  await page.getByRole("button", { name: "Close path", exact: true }).click();
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const chunks = [];
  for await (const c of await (await waiting).createReadStream())
    chunks.push(c);
  const doc = JSON.parse(Buffer.concat(chunks).toString());
  expect(doc.layers[0].curveTension).toBe(0.6);
  expect(doc.layers[0].closed).toBe(true);
  expect(doc.layers[0].nodes.every((n) => n.mode === "auto")).toBe(true);
  await page
    .getByRole("button", { name: "Apply to material", exact: false })
    .click();
  await frame(page);
  const source = await downloadText(page, /Three.js procedural shader/);
  const factory = source
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(
      /export default createMaterial\(preset\);/,
      "return { material: createMaterial(preset), svg: patternSVG(preset.pattern) };",
    )
    .replace(/export /g, "");
  const exported = new Function("THREE", factory)(THREE);
  expect(exported.material.userData.params.pattern).toEqual(doc);
  expect(exported.svg).toContain(" C");
  expect(exported.svg).not.toContain("vector-effect");
  exported.material.dispose();
  expect(requests).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone text and embedded SVG markup apply and export with no font or external-resource requests", async ({
  page,
}) => {
  test.setTimeout(240000);
  const html = await readFile(htmlPath, "utf8"),
    url =
      "https://alloy-standalone.invalid/svg-tools.html?studio=pattern&pattern=blank&material=natural-cotton";
  const errors = [],
    requests = [];
  page.on("pageerror", (e) => errors.push(e.message));
  await page.route("**/*", (route) => {
    if (route.request().url() === url)
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    if (!/^https?:/.test(route.request().url())) return route.continue();
    requests.push(route.request().url());
    return route.abort();
  });
  await page.goto(url);
  await page
    .getByRole("button", { name: "Text tool (X)", exact: true })
    .click();
  const text = page.getByLabel("Text content", { exact: true });
  await expect(text).toBeEnabled();
  await text.fill("VECTOR\nSTUDIO");
  await page
    .getByLabel("Text font family", { exact: true })
    .selectOption("Space Grotesk");
  await page
    .getByLabel("Text font weight", { exact: true })
    .selectOption("600");
  await page.getByLabel("Text font size", { exact: true }).fill("36");
  const p = await page
    .locator('[data-coordinate-space="tile"]')
    .evaluate((g) => {
      const p = new DOMPoint(70, 65).matrixTransform(g.getScreenCTM());
      return { x: p.x, y: p.y };
    });
  await page.mouse.click(p.x, p.y);
  await page
    .getByRole("button", { name: "Paste SVG source", exact: true })
    .first()
    .click();
  const modal = page.getByRole("dialog", {
    name: "SVG source editor",
    exact: true,
  });
  await modal
    .getByLabel("SVG markup", { exact: true })
    .fill(
      '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100"><defs><path id="tile" d="M0 0H45V45H0Z"/></defs><use href="#tile" x="20" y="20" style="fill:#c08052;stroke:#263c48;stroke-width:4"/><use href="#tile" x="125" y="20" fill="#263c48"/></svg>',
    );
  await modal.getByRole("button", { name: "Embed SVG", exact: true }).click();
  await expect(page.locator(".pe-layers button")).toHaveCount(2);
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Save document", exact: true })
    .click();
  const chunks = [];
  for await (const c of await (await waiting).createReadStream())
    chunks.push(c);
  const doc = JSON.parse(Buffer.concat(chunks).toString());
  expect(doc.layers[0].kind).toBe("text");
  expect(doc.layers[0].text.value).toBe("VECTOR\nSTUDIO");
  expect(doc.layers[0].path).toContain("M");
  expect(doc.layers[1].kind).toBe("svg");
  expect(doc.layers[1].svg).toContain('href="#tile"');
  expect(doc.layers[1].svg).not.toContain("style=");
  await page
    .getByRole("button", { name: "Apply to material", exact: false })
    .click();
  await frame(page);
  const source = await downloadText(page, /Three.js procedural shader/);
  const pureFactory = source
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(
      /export default createMaterial\(preset\);/,
      "return { params: preset, svg: patternSVG(preset.pattern), layers: validatePattern(preset.pattern).layers };",
    )
    .replace(/export /g, "");
  // Evaluate the exported SVG/document helpers in a genuine browser DOM. No
  // Three mock is used for rendering; frame() above verifies the actual shader.
  const result = await page.evaluate(
    (factory) => new Function("THREE", factory)({}),
    pureFactory,
  );
  expect(result.params.pattern).toEqual(doc);
  expect(result.layers).toEqual(doc.layers);
  expect(result.svg).toContain('href="#layer-1-tile"');
  expect(result.svg).not.toContain("<text");
  expect(result.svg).not.toContain("font-family");
  expect(requests).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone texture-layer workspace renders a teapot and saves editable metadata without external assets", async ({
  page,
}) => {
  const html = await readFile(htmlPath, "utf8");
  const address = new URL(publicURL || localURL);
  address.searchParams.set("studio", "texture");
  const url = address.href;
  const errors = [],
    unexpectedRequests = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.route("**/*", async (route) => {
    const request = route.request();
    if (request.url() === url && request.isNavigationRequest()) {
      if (publicURL) return route.continue();
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    }
    if (!/^https?:/.test(request.url())) return route.continue();
    unexpectedRequests.push(request.url());
    return route.abort("blockedbyclient");
  });
  const response = await page.goto(url);
  expect(response.status()).toBe(200);
  if (
    publicURL &&
    (await page.getByText("One more step", { exact: true }).isVisible())
  )
    await page
      .getByRole("button", { name: "Open the page", exact: true })
      .click();
  const canvas = page.locator(".tp-studio canvas");
  await expect(canvas).toHaveAttribute("data-material-ready", "true");
  await expect(canvas).toHaveAttribute("data-preview-shape", "Teapot");
  expect(await canvas.evaluate((c) => c.toDataURL().length)).toBeGreaterThan(
    10000,
  );
  await expect(
    page.locator("script[src], link[rel=stylesheet][href]"),
  ).toHaveCount(0);
  await page.getByRole("button", { name: "Add layer", exact: true }).click();
  await page.getByRole("menuitem", { name: /^Fill layer/ }).click();
  const name = page.getByRole("textbox", { name: "Layer name", exact: true });
  await name.fill("Offline fill setup");
  await name.press("Enter");
  await page
    .getByRole("spinbutton", { name: "Layer opacity value", exact: true })
    .fill("37");
  await page
    .getByRole("button", { name: "Add layer mask", exact: true })
    .click();
  await page
    .getByRole("checkbox", { name: "Invert mask", exact: true })
    .check();
  const waiting = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const parts = [];
  for await (const chunk of await (await waiting).createReadStream())
    parts.push(chunk);
  const doc = JSON.parse(Buffer.concat(parts).toString());
  expect(doc.schema).toBe("alloy.texture.v1");
  expect(doc.scene).toBe("teapot");
  expect(doc.layers.find((l) => l.name === "Offline fill setup")).toMatchObject(
    { kind: "fill", opacity: 37, mask: { inverted: true } },
  );
  await expect(
    page.getByText("Layer setup only · preview is not composited", {
      exact: true,
    }),
  ).toBeVisible();
  expect(unexpectedRequests).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone baking workspace runs the inline mesh worker and exports maps with no external assets", async ({
  page,
}) => {
  const html = await readFile(htmlPath, "utf8"),
    address = new URL(publicURL || localURL);
  address.searchParams.set("studio", "baking");
  const url = address.href,
    errors = [],
    unexpectedRequests = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.route("**/*", async (route) => {
    const request = route.request();
    if (request.url() === url && request.isNavigationRequest()) {
      if (publicURL) return route.continue();
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    }
    if (!/^https?:/.test(request.url())) return route.continue();
    unexpectedRequests.push(request.url());
    return route.abort("blockedbyclient");
  });
  const response = await page.goto(url);
  expect(response.status()).toBe(200);
  if (
    publicURL &&
    (await page.getByText("One more step", { exact: true }).isVisible())
  )
    await page
      .getByRole("button", { name: "Open the page", exact: true })
      .click();
  await expect(page.locator(".bk-studio canvas")).toHaveAttribute(
    "data-mesh-ready",
    "true",
  );
  await page
    .getByLabel("Mesh bake resolution", { exact: true })
    .selectOption("64");
  await page.getByLabel("AO samples", { exact: true }).selectOption("4");
  await page.getByRole("button", { name: "All maps", exact: true }).click();
  await expect(page.locator('.bk-map-tile[aria-pressed="true"]')).toHaveCount(
    27,
  );
  await expect(
    page.getByRole("button", { name: "Nodes", exact: true }),
  ).toHaveCount(0);
  await page
    .getByRole("button", { name: "Bake mesh maps", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Download maps ZIP", exact: true }),
  ).toBeVisible();
  const waiting = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Download maps ZIP", exact: true })
    .click();
  const parts = [];
  for await (const part of await (await waiting).createReadStream())
    parts.push(part);
  const files = unzipSync(Buffer.concat(parts)),
    manifest = JSON.parse(strFromU8(files["manifest.json"]));
  expect(manifest.schema).toBe("alloy.mesh-bake.v1");
  expect(manifest.sets[0].stats).toMatchObject({ charts: 32, misses: 0 });
  expect(Object.keys(files).filter((k) => k.endsWith(".png"))).toHaveLength(27);
  expect(manifest.sets[0].chartPalette).toHaveLength(32);
  expect(manifest.sets[0].channels["object-normal"].space).toBe("object");
  expect(manifest.graph).toBeUndefined();
  expect(manifest.workflow).toBe("mesh-maps");
  await page.getByRole("button", { name: "Pattern", exact: true }).click();
  await expect(
    page.getByLabel("Pattern design canvas", { exact: true }),
  ).toBeVisible();
  expect(unexpectedRequests).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone layered sand retains grain-bed editing and self-contained optical shader exports", async ({
  page,
}) => {
  const html = await readFile(htmlPath, "utf8"),
    address = new URL(publicURL || localURL);
  address.searchParams.set("material", "layered-sand");
  const url = address.href,
    errors = [],
    unexpectedRequests = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.route("**/*", async (route) => {
    const request = route.request();
    if (request.url() === url && request.isNavigationRequest()) {
      if (publicURL) return route.continue();
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    }
    if (!/^https?:/.test(request.url())) return route.continue();
    unexpectedRequests.push(request.url());
    return route.abort("blockedbyclient");
  });
  await page.goto(url);
  if (
    publicURL &&
    (await page.getByText("One more step", { exact: true }).isVisible())
  )
    await page
      .getByRole("button", { name: "Open the page", exact: true })
      .click();
  await frame(page);
  await expect(page.locator("h1")).toHaveText("Layered Sand");
  await page.getByLabel("Bed grain size value", { exact: true }).fill("1.1");
  await page.getByLabel("Bed roughness value", { exact: true }).fill("0.35");
  await page.getByLabel("Bed specular value", { exact: true }).fill("0.2");
  await page
    .getByLabel("Sand layer arrangement", { exact: true })
    .selectOption("bands");
  const doc = JSON.parse(
    await downloadText(page, /Material preset All surface/),
  );
  expect(doc.material.sandLayers[0]).toMatchObject({
    size: 1.1,
    roughness: 0.35,
    specular: 0.2,
  });
  expect(doc.material.metalness).toBe(0);
  expect(doc.material.coat).toBe(0);
  const text = await downloadText(page, /Three.js procedural shader/);
  expect(text).toContain("sandCellAt");
  expect(text).toContain("grainF0*sandSpecular");
  const code = text
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(/export (const|let|var|class|function|async function) /g, "$1 ")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    );
  const m = new Function("THREE", code)(THREE);
  expect(m.userData.params.sandLayers[0].specular).toBe(0.2);
  m.dispose();
  expect(unexpectedRequests).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone volume SDF worker exports a true Float32 cube volume without external assets", async ({
  page,
}) => {
  const html = await readFile(htmlPath, "utf8"),
    address = new URL(publicURL || localURL);
  address.searchParams.set("studio", "baking");
  const url = address.href,
    errors = [],
    unexpected = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.route("**/*", async (route) => {
    const request = route.request();
    if (request.url() === url && request.isNavigationRequest()) {
      if (publicURL) return route.continue();
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    }
    if (!/^https?:/.test(request.url())) return route.continue();
    unexpected.push(request.url());
    return route.abort("blockedbyclient");
  });
  await page.goto(url);
  if (
    publicURL &&
    (await page.getByText("One more step", { exact: true }).isVisible())
  )
    await page
      .getByRole("button", { name: "Open the page", exact: true })
      .click();
  await page.getByLabel("Bake demo mesh", { exact: true }).selectOption("Cube");
  await page.getByRole("button", { name: "Volume SDF", exact: true }).click();
  await page
    .getByLabel("SDF voxel resolution", { exact: true })
    .selectOption("16");
  await page.getByRole("button", { name: "Bake 3D SDF", exact: true }).click();
  await expect(
    page.getByRole("button", { name: "Download SDF volume ZIP", exact: true }),
  ).toBeVisible();
  const wait = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Download SDF volume ZIP", exact: true })
    .click();
  const stream = await (await wait).createReadStream(),
    chunks = [];
  for await (const chunk of stream) chunks.push(chunk);
  const files = unzipSync(Buffer.concat(chunks)),
    manifest = JSON.parse(strFromU8(files["manifest.json"]));
  expect(manifest.schema).toBe("alloy.volume-sdf.v1");
  expect(manifest.approximate).toBe(false);
  expect(files["distance.f32"].length).toBe(16 ** 3 * 4);
  expect(unexpected).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone paint instruments, channel targets and embedded Material workspace work without external requests", async ({
  page,
}) => {
  test.setTimeout(180000);
  const html = await readFile(htmlPath, "utf8"),
    url =
      "https://alloy-standalone.invalid/paint.html?studio=texture&material=corrugated-zinc",
    errors = [],
    requests = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.route("**/*", (route) => {
    if (route.request().url() === url)
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    if (!/^https?:/.test(route.request().url())) return route.continue();
    requests.push(route.request().url());
    return route.abort();
  });
  await page.goto(url);
  await expect(page.locator(".tp-scene-viewport canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await page
    .getByRole("button", { name: "Open paint tool menu", exact: true })
    .click();
  const menu = page.getByRole("dialog", {
    name: "Paint tool menu",
    exact: true,
  });
  await menu
    .getByRole("button", {
      name: "Select paint tool Classic Gold-Nib Fountain",
      exact: true,
    })
    .click();
  await menu.getByLabel("Paint Opacity value", { exact: true }).fill("0");
  await menu
    .getByRole("button", { name: "Close paint tool menu", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Add painting channel", exact: true })
    .click();
  await page
    .getByRole("checkbox", { name: "Sheen channel", exact: true })
    .check();
  await page
    .getByRole("button", { name: "Open content browser", exact: true })
    .click();
  const browser = page.getByRole("region", {
    name: "Content browser",
    exact: true,
  });
  await browser
    .getByLabel("Search content assets", { exact: true })
    .fill("Zinc");
  await browser
    .getByRole("button", { name: "Inspect asset Corrugated Zinc", exact: true })
    .click();
  await expect(browser.locator(".cb-material-preview canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  await browser
    .getByLabel("Corrugation profile", { exact: true })
    .selectOption("1");
  const waiting = page.waitForEvent("download");
  await browser.getByRole("button", { name: "Shader", exact: true }).click();
  const chunks = [];
  for await (const chunk of await (await waiting).createReadStream())
    chunks.push(chunk);
  const code = Buffer.concat(chunks)
    .toString()
    .replace(/import \* as THREE from ['"]three['"];?/, "")
    .replace(/export (const|let|var|class|function|async function) /g, "$1 ")
    .replace(
      /export default createMaterial\(preset\);/,
      "return createMaterial(preset);",
    );
  const material = new Function("THREE", code)(THREE);
  expect(material.userData.params.ribProfile).toBe(1);
  material.dispose();
  const saving = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const data = [];
  for await (const chunk of await (await saving).createReadStream())
    data.push(chunk);
  const doc = JSON.parse(Buffer.concat(data).toString());
  expect(doc.painting.params.opacity).toBe(0);
  expect(doc.layers.at(-1).channels).toContain("sheen");
  expect(requests).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone overlay, folders, imported Texture sources and colour-mask source preview work fully offline", async ({
  page,
}) => {
  test.setTimeout(180000);
  const html = await readFile(htmlPath, "utf8"),
    url = "https://alloy-standalone.invalid/folders.html?studio=texture",
    errors = [],
    requests = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.route("**/*", (route) => {
    if (route.request().url() === url)
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    if (!/^https?:/.test(route.request().url())) return route.continue();
    requests.push(route.request().url());
    return route.abort();
  });
  await page.goto(url);
  await expect(page.locator(".tp-scene-viewport canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  const scene = await page.locator(".tp-scene-viewport").boundingBox();
  await page
    .getByRole("button", { name: "Open content browser", exact: true })
    .click();
  expect(await page.locator(".tp-scene-viewport").boundingBox()).toEqual(scene);
  await expect(page.locator(".cb-grid>.cb-asset-tile")).toHaveCount(127);
  expect(
    (await page.locator(".cb-grid>.cb-asset-tile").first().boundingBox())
      .height,
  ).toBe(172);
  await page
    .getByRole("button", { name: "Collapse content browser", exact: true })
    .click();
  expect(await page.locator(".tp-scene-viewport").boundingBox()).toEqual(scene);
  await page
    .getByRole("button", { name: "Add layer folder", exact: true })
    .click();
  await page.getByLabel("Layer name", { exact: true }).fill("Offline surfaces");
  await page.getByLabel("Layer name", { exact: true }).press("Enter");
  const folderId = await page
    .locator(".tp-layer-row.selected")
    .getAttribute("data-layer-id");
  await page
    .getByRole("button", { name: "Select Base material", exact: true })
    .click();
  await page.getByLabel("Layer folder", { exact: true }).selectOption(folderId);
  await page
    .getByRole("group", { name: "Base color source", exact: true })
    .getByRole("button", { name: "Texture", exact: true })
    .click();
  const encoded = await page.evaluate(() => {
    const c = document.createElement("canvas");
    c.width = 128;
    c.height = 64;
    const ctx = c.getContext("2d");
    ctx.fillStyle = "#ff0000";
    ctx.fillRect(0, 0, 64, 64);
    ctx.fillStyle = "#0000ff";
    ctx.fillRect(64, 0, 64, 64);
    return c.toDataURL("image/png").split(",")[1];
  });
  const chooser = page.waitForEvent("filechooser");
  await page
    .getByRole("button", { name: "Import texture", exact: true })
    .first()
    .click();
  await (
    await chooser
  ).setFiles({
    name: "offline-source.png",
    mimeType: "image/png",
    buffer: Buffer.from(encoded, "base64"),
  });
  await expect(
    page.getByRole("img", { name: "Base color imported texture", exact: true }),
  ).toBeVisible();
  await expect(
    page.locator('[data-layer-id="base-material"] img'),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Select Offline surfaces", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Add colour mask", exact: true })
    .click();
  await page
    .getByLabel("Colour mask colour hex", { exact: true })
    .fill("#ff0000");
  await page
    .getByLabel("Colour mask tolerance value", { exact: true })
    .fill("0");
  await page
    .getByLabel("Colour mask softness value", { exact: true })
    .fill("0");
  await expect
    .poll(() =>
      page
        .getByRole("img", {
          name: "Colour-selection mask preview",
          exact: true,
        })
        .evaluate((img) => {
          const c = document.createElement("canvas");
          c.width = img.naturalWidth;
          c.height = img.naturalHeight;
          const ctx = c.getContext("2d");
          ctx.drawImage(img, 0, 0);
          return [
            ctx.getImageData(10, 10, 1, 1).data[0],
            ctx.getImageData(100, 10, 1, 1).data[0],
          ];
        }),
    )
    .toEqual([255, 0]);
  await page.getByLabel("Mask strength value", { exact: true }).fill("0");
  const waiting = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const chunks = [];
  for await (const c of await (await waiting).createReadStream())
    chunks.push(c);
  const doc = JSON.parse(Buffer.concat(chunks).toString());
  expect(doc.layers.find((l) => l.id === folderId).mask).toMatchObject({
    kind: "color",
    color: "#ff0000",
    strength: 0,
    tolerance: 0,
    softness: 0,
  });
  const imported = doc.layers.find((l) => l.id === "base-material");
  expect(imported.parentId).toBe(folderId);
  expect(imported.channelSettings.baseColor).toMatchObject({
    mode: "texture",
    texture: { name: "offline-source.png", width: 128, height: 64 },
  });
  // Source gestures, their last-stroke image and layer thumbnails also work
  // from the single inline file, without pretending to paint mesh UVs.
  await page
    .getByRole("button", { name: "Select Base material", exact: true })
    .click();
  await page
    .getByRole("button", { name: "Paint Base color texture", exact: true })
    .click();
  const sourceCanvas = page.getByLabel("Base color texture canvas", {
    exact: true,
  });
  await expect(sourceCanvas).toHaveAttribute("aria-disabled", "false");
  await sourceCanvas.scrollIntoViewIfNeeded();
  const box = await sourceCanvas.boundingBox();
  await page.mouse.move(box.x + box.width * 0.15, box.y + box.height * 0.2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * 0.5, box.y + box.height * 0.65, {
    steps: 8,
  });
  await page.mouse.move(box.x + box.width * 0.8, box.y + box.height * 0.4, {
    steps: 8,
  });
  await page.mouse.up();
  await page.getByRole("tab", { name: "History", exact: true }).click();
  const timeline = page.getByRole("tabpanel", { name: "History", exact: true });
  await expect(
    timeline.getByRole("img", {
      name: "Last recorded paint stroke",
      exact: true,
    }),
  ).toBeVisible();
  const saved = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const paintedBytes = [];
  for await (const c of await (await saved).createReadStream())
    paintedBytes.push(c);
  const painted = JSON.parse(Buffer.concat(paintedBytes).toString());
  expect(painted.lastStroke).toMatchObject({
    layerId: "base-material",
    operation: "paint",
    channelId: "baseColor",
  });
  expect(painted.lastStroke.points.length).toBeGreaterThan(3);
  expect(
    painted.layers.find((l) => l.id === "base-material").channelSettings
      .baseColor.texture.origin,
  ).toBe("paint");
  await timeline
    .getByRole("button", { name: "Undo history step", exact: true })
    .click();
  await page.getByRole("tab", { name: "Inspector", exact: true }).click();
  await expect(
    page.getByRole("img", { name: "Base color imported texture", exact: true }),
  ).toHaveAttribute("src", imported.channelSettings.baseColor.texture.dataUrl);
  expect(requests).toEqual([]);
  expect(errors).toEqual([]);
});

test("standalone shared colour picker, Material-studio generators and surface-anchored gradient fills / masks work offline", async ({
  page,
}) => {
  test.setTimeout(180000);
  const html = await readFile(htmlPath, "utf8"),
    url = "https://alloy-standalone.invalid/point-fields.html?studio=texture",
    requests = [],
    errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.route("**/*", (route) => {
    if (route.request().url() === url)
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    if (!/^https?:/.test(route.request().url())) return route.continue();
    requests.push(route.request().url());
    return route.abort();
  });
  await page.goto(url);
  await expect(page.locator(".tp-scene-viewport canvas")).toHaveAttribute(
    "data-material-ready",
    "true",
  );
  const source = page.getByRole("group", {
    name: "Base color source",
    exact: true,
  });
  await source.getByRole("button", { name: "Gradient", exact: true }).click();
  const gradient = page.getByRole("region", {
    name: "Base color gradient editor",
    exact: true,
  });
  await gradient
    .getByRole("button", { name: "Edit in viewport", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: /^Viewport gradient point/ }),
  ).toHaveCount(2);
  await gradient
    .getByLabel("Base color gradient point colour", { exact: true })
    .fill("#ff0000");
  await gradient
    .getByLabel("Base color gradient point radius value", { exact: true })
    .fill("0.4");
  await gradient
    .getByRole("button", { name: "Place point", exact: true })
    .click();
  const canvas = page.locator(".tp-scene-viewport canvas"),
    box = await canvas.boundingBox();
  await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.55);
  await expect(
    page.getByRole("button", { name: /^Viewport gradient point/ }),
  ).toHaveCount(3);
  await page
    .getByRole("button", { name: "Add layer mask", exact: true })
    .click();
  await page
    .getByRole("group", { name: "Mask type", exact: true })
    .getByRole("button", { name: "Gradient", exact: true })
    .click();
  await expect(
    page.getByRole("img", { name: "Gradient mask field slice", exact: true }),
  ).toBeVisible();
  await page.getByLabel("Mask strength value", { exact: true }).fill("0");
  const pending = page.waitForEvent("download");
  await page.getByRole("button", { name: "Save project", exact: true }).click();
  const parts = [];
  for await (const part of await (await pending).createReadStream())
    parts.push(part);
  const doc = JSON.parse(Buffer.concat(parts).toString());
  expect(
    doc.layers.at(-1).channelSettings.baseColor.gradient.points,
  ).toHaveLength(3);
  expect(doc.layers.at(-1).mask).toMatchObject({
    kind: "gradient",
    strength: 0,
  });
  await page
    .getByRole("button", { name: "Select Base material", exact: true })
    .click();
  await source.getByRole("button", { name: "Generator", exact: true }).click();
  await page
    .getByLabel("Base color generator", { exact: true })
    .selectOption("MaterialStudio");
  await expect(
    page.getByRole("img", {
      name: "Generated Material-studio field",
      exact: true,
    }),
  ).toBeVisible({ timeout: 90000 });
  await expect(
    page.getByLabel("Base color generator", { exact: true }),
  ).toHaveValue("MaterialStudio");
  await page
    .getByRole("button", { name: "Open paint tool menu", exact: true })
    .click();
  const menu = page.getByRole("dialog", {
    name: "Paint tool menu",
    exact: true,
  });
  await menu
    .getByRole("button", {
      name: "Select paint tool Classic Gold-Nib Fountain",
      exact: true,
    })
    .click();
  await expect(
    menu.getByRole("region", { name: "Paint tool color picker", exact: true }),
  ).toBeVisible();
  await menu.getByLabel("Paint tool color", { exact: true }).fill("#00ff00");
  await expect(
    menu.locator(".paint-control-group .slider-field"),
  ).not.toHaveCount(0);
  await menu
    .getByRole("button", { name: "Close paint tool menu", exact: true })
    .click();
  expect(requests).toEqual([]);
  expect(errors).toEqual([]);
});

test("offline Stamp rich text and reusable surface decals/masks use only embedded fonts and artwork", async ({
  page,
}) => {
  const html = await readFile(htmlPath, "utf8"),
    url = localURL + "?studio=stamp",
    errors = [],
    requests = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") errors.push(m.text());
  });
  await page.route("**/*", (route) => {
    if (route.request().isNavigationRequest() && route.request().url() === url)
      return route.fulfill({
        status: 200,
        contentType: "text/html; charset=utf-8",
        body: html,
      });
    if (!/^https?:/.test(route.request().url())) return route.continue();
    requests.push(route.request().url());
    return route.abort("blockedbyclient");
  });
  await page.goto(url);
  await expect(
    page.getByAltText("Composed stamp preview", { exact: true }),
  ).toBeVisible();
  await page
    .getByRole("textbox", { name: "Stamp rich text", exact: true })
    .fill("OFFLINE");
  await page
    .getByLabel("Stamp text font", { exact: true })
    .selectOption("DM Sans");
  await page.getByLabel("Stamp text size", { exact: true }).fill("48");
  const pending = page.waitForEvent("download");
  await page
    .locator(".stamp-canvas-toolbar")
    .getByRole("button", { name: "SVG", exact: true })
    .click();
  const chunks = [];
  for await (const chunk of await (await pending).createReadStream())
    chunks.push(chunk);
  const svg = Buffer.concat(chunks).toString();
  expect(svg).toContain("<path");
  expect(svg).not.toContain("<text");
  await page
    .getByRole("button", { name: "Use in Texture", exact: true })
    .click();
  const canvas = page.locator(".tp-scene-viewport canvas");
  await expect(canvas).toHaveAttribute("data-material-ready", "true");
  const box = await canvas.boundingBox();
  await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.5);
  await expect(canvas).toHaveAttribute("data-decal-count", "1");
  expect(
    Number(await canvas.getAttribute("data-decal-triangles")),
  ).toBeGreaterThan(0);
  await page
    .getByRole("button", { name: "Navigate teapot", exact: true })
    .click();
  await page
    .getByRole("checkbox", { name: "Use decal as mask", exact: true })
    .check();
  await expect(canvas).toHaveAttribute("data-decal-mask-count", "1");
  expect(requests).toEqual([]);
  expect(errors).toEqual([]);
});
