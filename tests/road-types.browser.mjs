import { seedProject } from "../src/model.js";
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
const pass = (s) => console.log(`PASS ${s}`);
try {
  // The live starter now already contains a ring. Manual-placement regression
  // uses the original county, with a distinct name to opt out of starter upgrades.
  if (process.env.FRONTIER_TEST_PHASE !== "channels") {
    const fixture = seedProject();
    fixture.name = "Roundabout placement fixture";
    await page.addInitScript((value) => {
      if (!sessionStorage.getItem("placement-fixture")) {
        localStorage.setItem("frontier-project-v1", value);
        sessionStorage.setItem("placement-fixture", "1");
      }
    }, JSON.stringify(fixture));
  }
  await page.goto(process.env.FRONTIER_URL || "http://localhost:5173");
  await page.waitForFunction(() => !!window.frontier);
  fs.mkdirSync(".playwright", { recursive: true });
  if (process.env.FRONTIER_TEST_PHASE !== "roundabout") {
    await page.evaluate(() => frontier.selectRoad("r89"));
    for (const id of ["brick-channel", "weep-channel", "slot-channel"]) {
      await page.click("#street-presets");
      assert.equal(await page.locator("[data-street-preset]").count(), 9);
      await page.click(`[data-street-preset="${id}"]`);
      assert.equal(
        await page.evaluate(
          () =>
            frontier.project.edges.find((e) => e.id === frontier.state.selected)
              .drainage,
        ),
        id,
      );
      assert.equal(
        await page.evaluate(() => frontier.world.pavementCheck.ok),
        true,
      );
      assert.equal(
        await page.evaluate(() => frontier.world.highlight.visible),
        false,
      );
      const holes = await page.evaluate(
        () =>
          frontier.world.streetscape.drains.find(
            (d) => d.road === frontier.state.selected,
          ).openings.length,
      );
      assert.equal(
        holes,
        { "brick-channel": 6, "weep-channel": 4, "slot-channel": 1 }[id],
      );
      await page.screenshot({
        animations: "disabled",
        path: `.playwright/${id}.png`,
      });
      pass(`${id}: applied real profile and collector, geometry checks clean`);
    }
    await page.click("#export");
    const [download] = await Promise.all([
      page.waitForEvent("download"),
      page.click("#download-street"),
    ]);
    assert.match(fs.readFileSync(await download.path(), "utf8"), /^vt /m);
    await page.click("#close-modal");
    await page.click("#save");
    await page.reload();
    await page.waitForFunction(() => !!window.frontier);
    assert.equal(
      await page.evaluate(
        () => frontier.project.edges.find((e) => e.id === "r89").drainage,
      ),
      "slot-channel",
    );
    pass("new channel persistence and actual quad/UV download");
  }
  if (process.env.FRONTIER_TEST_PHASE !== "channels") {
    await page.click('[data-view="2d"]');
    await page.evaluate(() => {
      const w = frontier.world;
      w.home();
      w.controls.target.set(-230, 0, 100);
      w.camera.zoom = 1;
      w.camera.updateProjectionMatrix();
      w.camera.position.set(-230, 650, 100.001);
      w.controls.update();
      w.camera.updateMatrixWorld(true);
      w.needsRender = true;
    });
    const before = await page.evaluate(() => frontier.project.edges.length);
    await page.click('[data-preset="roundabout"]');
    await page.fill("#roundabout-radius", "28");
    await page.click("#activate-roundabout");
    assert.equal(await page.evaluate(() => frontier.state.tool), "roundabout");
    const snapshot = await page.evaluate(() =>
      JSON.stringify(frontier.project),
    );
    const occupied = await page.evaluate(() => {
      const w = frontier.world,
        n = frontier.project.nodes[0],
        v = w.project(n.x, 0, n.z),
        r = w.renderer.domElement.getBoundingClientRect();
      return { x: r.left + v.x, y: r.top + v.y };
    });
    await page.mouse.click(occupied.x, occupied.y);
    assert.equal(
      await page.evaluate(() => JSON.stringify(frontier.project)),
      snapshot,
    );
    assert.match(await page.locator("#toast").textContent(), /clear space/);
    pass("occupied placement rejected without touching existing roads");

    const coords = await page.evaluate(() => {
      const w = frontier.world,
        v = w.project(-230, 0, 100),
        r = w.renderer.domElement.getBoundingClientRect();
      return { x: r.left + v.x, y: r.top + v.y };
    });
    await page.mouse.move(coords.x, coords.y);
    await page.mouse.click(coords.x, coords.y);
    assert.equal(await page.evaluate(() => frontier.state.tool), "select");
    const group = await page.evaluate(
      () =>
        frontier.project.edges.find((e) => e.id === frontier.state.selected)
          .roundabout,
    );
    assert.ok(group);
    assert.equal(
      await page.evaluate(
        (group) =>
          frontier.project.edges.filter(
            (e) => e.roundabout === group && e.roundaboutRole === "ring",
          ).length,
        group,
      ),
      12,
    );
    assert.equal(
      await page.evaluate(() => frontier.world.pavementCheck.ok),
      true,
    );
    pass("pointer-placed roundabout, connected ring, clean paving");
    await page.screenshot({
      animations: "disabled",
      path: ".playwright/roundabout-plan.png",
    });
    await page.click("#undo");
    assert.equal(
      await page.evaluate(() => frontier.project.edges.length),
      before,
    );
    await page.click("#redo");
    assert.ok(
      await page.evaluate(() =>
        frontier.project.edges.some((e) => e.roundaboutRole === "ring"),
      ),
    );
    pass("assembly placement is one undo/redo operation");
    await page.click('[data-view="3d"]');
    await page.evaluate(
      (group) => frontier.world.frameRoundabout(group),
      group,
    );
    await page.screenshot({
      animations: "disabled",
      path: ".playwright/roundabout-world.png",
    });
    await page.click("#save");
    await page.reload();
    await page.waitForFunction(() => !!window.frontier);
    assert.equal(
      await page.evaluate(
        (group) =>
          frontier.project.edges.filter(
            (e) => e.roundabout === group && e.roundaboutRole === "ring",
          ).length,
        group,
      ),
      12,
    );
    pass("roundabout graph metadata persists");
    await page.evaluate(
      (group) =>
        frontier.selectRoad(
          frontier.project.edges.find(
            (e) => e.roundabout === group && e.roundaboutRole === "ring",
          ).id,
        ),
      group,
    );
    const otherRoads = await page.evaluate(
      (group) =>
        frontier.project.edges
          .filter((e) => e.roundabout !== group)
          .map((e) => e.id),
      group,
    );
    await page.click("#delete-selected");
    assert.match(
      await page.locator("#modal-title").textContent(),
      /Remove this roundabout/,
    );
    await page.click("#confirm-delete-roundabout");
    assert.deepEqual(
      await page.evaluate(() => frontier.project.edges.map((e) => e.id)),
      otherRoads,
    );
    assert.equal(
      await page.evaluate(
        (group) => frontier.project.edges.some((e) => e.roundabout === group),
        group,
      ),
      false,
    );
    await page.click("#undo");
    assert.equal(
      await page.evaluate(
        (group) =>
          frontier.project.edges.filter(
            (e) => e.roundabout === group && e.roundaboutRole === "ring",
          ).length,
        group,
      ),
      12,
    );
    pass("explicit assembly removal and undo retain connected roads");
  }
  assert.deepEqual(errors, []);
  pass("no uncaught browser errors");
} finally {
  await page.close();
  await browser.close();
}
