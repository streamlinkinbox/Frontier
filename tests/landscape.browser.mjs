import assert from "node:assert/strict";
import fs from "node:fs";
import zlib from "node:zlib";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { chromium } from "playwright";
import chrome from "@sparticuz/chromium";
const libs = path.join(os.tmpdir(), "frontier-browser-libs");
fs.mkdirSync(libs, { recursive: true });
const archive = path.join(libs, "al2023.tar");
fs.writeFileSync(
  archive,
  zlib.brotliDecompressSync(
    fs.readFileSync(
      new URL(
        "../node_modules/@sparticuz/chromium/bin/al2023.tar.br",
        import.meta.url,
      ),
    ),
  ),
);
execFileSync("tar", ["xf", archive, "-C", libs]);
const browser = await chromium.launch({
  headless: true,
  executablePath: await chrome.executablePath(),
  args: chrome.args,
  env: {
    ...process.env,
    LD_LIBRARY_PATH: `${libs}/lib:${process.env.LD_LIBRARY_PATH || ""}`,
  },
});
const page = await browser.newPage({ viewport: { width: 1440, height: 960 } }),
  errors = [];
page.on("pageerror", (e) => errors.push(e.message));
const pass = (s) => console.log(`PASS ${s}`),
  phase = process.env.LANDSCAPE_TEST_PHASE || "visual";
try {
  if (phase === "migration") {
    const { seedProject } = await import("../src/model.js");
    const p = seedProject();
    p.edges.find((e) => e.id === "r89").paving = "brick";
    await page.addInitScript((value) => {
      if (!localStorage.getItem("frontier-project-v1"))
        localStorage.setItem("frontier-project-v1", value);
    }, JSON.stringify(p));
  }
  await page.goto(process.env.FRONTIER_URL || "http://localhost:5173");
  await page.waitForFunction(() => !!window.frontier);
  fs.mkdirSync(".playwright", { recursive: true });
  assert.equal(
    await page.evaluate(
      () =>
        frontier.project.edges.filter((e) => e.roundaboutRole === "ring")
          .length,
    ),
    12,
  );
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  assert.deepEqual(await page.evaluate(() => frontier.audit().issues), []);
  if (phase === "visual") {
    await page.click('[data-tour="roundabout"]');
    assert.equal(
      await page.locator("#lane-width").evaluate((el) => el.checkValidity()),
      true,
    );
    await page.screenshot({
      animations: "disabled",
      path: ".playwright/starter-roundabout.png",
    });
    pass("default roundabout is present and one-click framing works");
    for (const mode of ["garden", "cycle", "tram"]) {
      await page.click(`[data-tour="${mode}"]`);
      assert.equal(await page.locator("#landscape-layout").inputValue(), mode);
      await page.evaluate(() => {
        frontier.world.highlight.visible = false;
        frontier.world.needsRender = true;
      });
      await page.screenshot({
        animations: "disabled",
        path: `.playwright/landscape-${mode}.png`,
      });
    }
    pass("all three seeded profiles are visible and selectable");
    await page.click("#landscape-references");
    assert.equal(
      await page.locator(".landscape-reference-grid img").count(),
      5,
    );
    assert.equal(
      await page
        .locator(".landscape-reference-grid img")
        .evaluateAll((imgs) =>
          imgs.every((i) => i.complete && i.naturalWidth > 0),
        ),
      true,
    );
    await page.screenshot({
      animations: "disabled",
      path: ".playwright/landscape-references.png",
    });
    await page.click("#close-modal");
    await page.click('[data-view="2d"]');
    await page.click('[data-tour="garden"]');
    await page.screenshot({
      animations: "disabled",
      path: ".playwright/landscape-plan.png",
    });
    pass("reference gallery and linked plan view");
  }
  if (phase === "edit") {
    await page.click('[data-tour="garden"]');
    await page.selectOption("#landscape-layout", "cycle");
    assert.equal(
      await page.evaluate(() => frontier.world.pavementCheck.ok),
      true,
    );
    assert.deepEqual(await page.evaluate(() => frontier.audit().issues), []);
    await page.click("#undo");
    assert.equal(
      await page.locator("#landscape-layout").inputValue(),
      "garden",
    );
    await page.click("#redo");
    assert.equal(await page.locator("#landscape-layout").inputValue(), "cycle");
    const before = await page.evaluate(() => JSON.stringify(frontier.project));
    await page.locator('label:has([data-property="bridge"])').click();
    assert.equal(
      await page.evaluate(() => JSON.stringify(frontier.project)),
      before,
    );
    pass("profile edit/undo/redo and incompatible bridge rollback");
    await page.click("#save");
    await page.reload();
    await page.waitForFunction(() => !!window.frontier);
    assert.equal(
      await page.evaluate(
        () => frontier.project.edges.find((e) => e.id === "r15").landscape,
      ),
      "cycle",
    );
    await page.click("#export");
    const [download] = await Promise.all([
      page.waitForEvent("download"),
      page.click("#download-obj"),
    ]);
    const obj = fs.readFileSync(await download.path(), "utf8");
    assert.match(obj, /usemtl planting/);
    assert.match(obj, /usemtl trackgrass/);
    assert.match(obj, /usemtl cycle/);
    pass("persistence and partitioned surface OBJ export");
  }
  if (phase === "draw") {
    await page.click('[data-view="2d"]');
    await page.evaluate(() => {
      const w = frontier.world;
      w.controls.target.set(20, 0, 222);
      w.camera.position.set(20, 650, 222.001);
      w.camera.zoom = 1.5;
      w.camera.updateProjectionMatrix();
      w.controls.update();
      w.camera.updateMatrixWorld(true);
      w.needsRender = true;
    });
    const before = await page.evaluate(() => frontier.project.edges.length);
    await page.click('[data-preset="garden"]');
    for (const x of [-50, 110]) {
      const point = await page.evaluate(
          (x) => frontier.world.project(x, 0, 222),
          x,
        ),
        box = await page.locator("#world-view").boundingBox();
      await page.mouse.click(box.x + point.x, box.y + point.y);
    }
    await page.keyboard.press("Escape");
    assert.equal(
      await page.evaluate(() => frontier.project.edges.length),
      before + 1,
    );
    assert.equal(
      await page.evaluate(() => frontier.project.edges.at(-1).landscape),
      "garden",
    );
    assert.equal(
      await page.evaluate(() => frontier.world.pavementCheck.ok),
      true,
    );
    assert.deepEqual(await page.evaluate(() => frontier.audit().issues), []);
    await page.click('[data-view="3d"]');
    await page.evaluate(() => {
      frontier.world.focus(frontier.project.edges.at(-1).id);
      frontier.world.highlight.visible = false;
      frontier.world.needsRender = true;
    });
    await page.screenshot({
      animations: "disabled",
      path: ".playwright/drawn-garden-boulevard.png",
    });
    await page.click("#undo");
    assert.equal(
      await page.evaluate(() => frontier.project.edges.length),
      before,
    );
    pass("new landscape preset drawing and atomic undo");
  }
  if (phase === "migration") {
    const result = await page.evaluate(() => ({
      backup: JSON.parse(localStorage.getItem("frontier-before-landscape-v1")),
      p: frontier.project,
    }));
    assert.equal(
      result.backup.edges.some((e) => e.roundabout),
      false,
    );
    assert.equal(result.p.edges.find((e) => e.id === "r89").paving, "brick");
    await page.click("#landscape-references");
    await page.click("#install-landscape-examples");
    assert.equal(
      await page.locator("#landscape-layout").inputValue(),
      "garden",
    );
    assert.equal(
      await page.evaluate(() => frontier.world.pavementCheck.ok),
      true,
    );
    assert.equal(
      await page.evaluate(
        () => frontier.project.edges.find((e) => e.id === "r89").paving,
      ),
      "brick",
    );
    await page.click("#undo");
    assert.equal(
      await page.evaluate(() =>
        frontier.project.edges.some((e) => e.landscape === "garden"),
      ),
      false,
    );
    await page.click("#redo");
    await page.click("#save");
    await page.reload();
    await page.waitForFunction(() => !!window.frontier);
    assert.equal(
      await page.evaluate(
        () =>
          frontier.project.edges.filter((e) => e.roundaboutRole === "ring")
            .length,
      ),
      12,
    );
    assert.equal(
      await page.evaluate(
        () =>
          frontier.project.edges.filter((e) =>
            ["garden", "tram", "cycle"].includes(e.landscape),
          ).length,
      ),
      3,
    );
    pass(
      "old-save backup, non-destructive ring upgrade, example installation, undo and reload",
    );
  }
  assert.deepEqual(errors, []);
  pass("no browser errors");
} finally {
  await page.close();
  await browser.close();
}
