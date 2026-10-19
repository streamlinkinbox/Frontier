import assert from "node:assert/strict";
import fs from "node:fs";
import zlib from "node:zlib";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { chromium } from "playwright";
import bundledChromium from "@sparticuz/chromium";

// The npm-hosted browser makes the smoke test usable in restricted sandboxes.
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
  executablePath: await bundledChromium.executablePath(),
  args: bundledChromium.args,
  env: {
    ...process.env,
    LD_LIBRARY_PATH: `${libs}/lib:${process.env.LD_LIBRARY_PATH || ""}`,
  },
});
const page = await browser.newPage({ viewport: { width: 1440, height: 960 } }),
  errors = [];
page.on("pageerror", (e) => errors.push(e.message));
const check = (name) => console.log(`PASS ${name}`);
const counts = () =>
  page.evaluate(() => ({
    edges: frontier.project.edges.length,
    lanes: frontier.project.edges.find((e) => e.id === frontier.state.selected)
      ?.lanes,
    brushes: frontier.project.brushes.length,
    corridors: frontier.project.corridors.length,
  }));
async function clickWorld(x, z) {
  const pos = await page.evaluate(
    ({ x, z }) => {
      const w = frontier.world,
        p = w.project(x, 2, z),
        r = w.container.getBoundingClientRect();
      return { x: r.x + p.x, y: r.y + p.y };
    },
    { x, z },
  );
  await page.mouse.click(pos.x, pos.y);
}
try {
  await page.goto(process.env.FRONTIER_URL || "http://localhost:5173");
  await page.waitForFunction(() => !!window.frontier);
  assert.deepEqual(await page.evaluate(() => frontier.audit().issues), []);
  check("initial scene, graph and topology report");
  const original = await counts();
  await page.click('[data-step="1"]');
  assert.equal((await counts()).lanes, original.lanes + 1);
  await page.click("#undo");
  assert.equal((await counts()).lanes, original.lanes);
  await page.click("#redo");
  assert.equal((await counts()).lanes, original.lanes + 1);
  await page.click("#undo");
  check("lane edits, undo and redo");
  await page.fill("#lane-width", "3.4");
  await page.locator("#lane-width").press("Tab");
  assert.equal(
    await page.evaluate(
      () =>
        frontier.project.edges.find((e) => e.id === frontier.state.selected)
          .laneWidth,
    ),
    3.4,
  );
  await page.click("#undo");
  check("numeric profile controls");
  await page.click('[data-view="2d"]');
  const originalSelection = await page.evaluate(() => frontier.state.selected);
  await page.click("#focus-road");
  const drag = await page.evaluate(() => {
    const w = frontier.world,
      e = frontier.project.edges.find((e) => e.id === frontier.state.selected),
      n = frontier.project.nodes.find((n) => n.id === e.a),
      r = w.container.getBoundingClientRect(),
      a = w.project(n.x, 2, n.z),
      b = w.project(n.x + 6, 2, n.z + 3);
    return {
      a: { x: a.x + r.x, y: a.y + r.y },
      b: { x: b.x + r.x, y: b.y + r.y },
    };
  });
  await page.mouse.move(drag.a.x, drag.a.y);
  await page.mouse.down();
  await page.mouse.move(drag.b.x, drag.b.y, { steps: 4 });
  await page.mouse.up();
  assert.equal(
    await page.evaluate(() => frontier.state.actions.at(-1)),
    "Moved road node",
  );
  assert.deepEqual(await page.evaluate(() => frontier.audit().issues), []);
  await page.click("#undo");
  await page.evaluate((id) => {
    frontier.selectRoad(id);
    frontier.world.home();
  }, originalSelection);
  check("dragging an existing shared road endpoint");
  assert.equal(await page.evaluate(() => frontier.world.mode), "2d");
  await page.click('[data-preset="local"]');
  await clickWorld(-249, -65);
  await clickWorld(-265, -27);
  await page.keyboard.press("Escape");
  assert.equal((await counts()).edges, original.edges + 1);
  assert.equal(await page.evaluate(() => frontier.audit().degenerate), 0);
  check("drawing from actual pointer input in the 2D viewport");
  await page.click("#undo");
  assert.equal((await counts()).edges, original.edges);
  check("undo drawn road");
  await page.click('[data-library="structures"]');
  await page.click('[data-structure="rail"]');
  await clickWorld(-195, -155);
  await clickWorld(-173, -138);
  await page.keyboard.press("Escape");
  assert.equal((await counts()).corridors, original.corridors + 1);
  await page.click("#undo");
  assert.equal((await counts()).corridors, original.corridors);
  await page.click('[data-library="roads"]');
  check("rail corridor drawing and undo");
  await page.click('[data-tool="terrain"]');
  await clickWorld(35, 170);
  assert.equal((await counts()).brushes, original.brushes + 1);
  await page.click("#undo");
  assert.equal((await counts()).brushes, original.brushes);
  await page.keyboard.press("Escape");
  check("terrain brush and terrain undo");
  await page.click("#overlays");
  await page.click('[data-overlay="wireframe"]');
  assert.equal(await page.evaluate(() => !!frontier.world.wire), true);
  await page.click("#overlays");
  await page.click('[data-overlay="wireframe"]');
  await page.keyboard.press("Escape");
  check("quad overlay");
  await page.click('[data-view="split"]');
  assert.equal(await page.locator("canvas").count(), 2);
  await page.click('[data-view="3d"]');
  assert.equal(await page.locator("#plan-view").isVisible(), false);
  check("linked split view");
  await page.click('[data-inspector="layers"]');
  await page.click('[data-layer="buildings"]');
  assert.equal(
    await page.evaluate(() => frontier.world.layers.buildings.visible),
    false,
  );
  await page.click('[data-layer="buildings"]');
  await page.click('[data-inspector="properties"]');
  check("scene visibility controls");
  await page.click("#topology-footer");
  assert.equal(
    await page.locator("#modal-title").textContent(),
    "Built to connect.",
  );
  await page.click("#close-modal");
  await page.click("#export");
  const [download] = await Promise.all([
    page.waitForEvent("download"),
    page.click("#download-obj"),
  ]);
  assert.equal(download.suggestedFilename(), "westhaven-roads.obj");
  await page.click("#close-modal");
  check("diagnostics dialog and OBJ download");
  await page.click("#save");
  await page.reload();
  await page.waitForFunction(() => !!window.frontier);
  assert.equal((await counts()).edges, original.edges);
  check("project persistence");
  fs.mkdirSync(".playwright", { recursive: true });
  await page.screenshot({ path: ".playwright/studio.png" });
  assert.deepEqual(errors, []);
  check("no uncaught browser errors");
  console.log("All browser smoke checks passed.");
} finally {
  await page.close();
  await browser.close();
}
