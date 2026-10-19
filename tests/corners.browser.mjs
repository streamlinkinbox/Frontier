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
  await page.goto(process.env.FRONTIER_URL || "http://localhost:5173");
  await page.waitForFunction(() => !!window.frontier);
  assert.equal(
    await page.evaluate(() => document.documentElement.dataset.theme),
    "dark",
  );
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  pass("dark default and clean whole-network pavement checks");
  await page.click("#theme-toggle");
  assert.equal(
    await page.evaluate(() => document.documentElement.dataset.theme),
    "light",
  );
  await page.reload();
  await page.waitForFunction(() => !!window.frontier);
  assert.equal(
    await page.evaluate(() => document.documentElement.dataset.theme),
    "light",
  );
  await page.click("#theme-toggle");
  pass("theme toggle and persisted light/dark preference");
  await page.evaluate(() => frontier.selectRoad("r90"));
  const before = await page.evaluate(() => ({
    project: JSON.stringify(frontier.project),
    history: frontier.state.history.length,
  }));
  await page.fill("#street-width", "6");
  await page.locator("#street-width").press("Tab");
  assert.equal(
    await page.evaluate(() => JSON.stringify(frontier.project)),
    before.project,
  );
  assert.equal(
    await page.evaluate(() => frontier.state.history.length),
    before.history,
  );
  assert.match(await page.locator("#toast").textContent(), /rejected/);
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  pass(
    "impossible tight-corner widening rejected atomically, preserving history and geometry",
  );
  await page.evaluate(() => frontier.selectRoad("r98"));
  await page.fill("#street-width", "2.8");
  await page.locator("#street-width").press("Tab");
  assert.equal(
    await page.evaluate(
      () =>
        frontier.project.edges.find((e) => e.id === frontier.state.selected)
          .sidewalk,
    ),
    2.8,
  );
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  await page.click("#undo");
  assert.equal(
    await page.evaluate(
      () =>
        frontier.project.edges.find((e) => e.id === frontier.state.selected)
          .sidewalk,
    ),
    2.4,
  );
  pass("safe widening and undo stay usable");
  await page.click("#street-audit");
  assert.match(
    await page.locator(".modal-body").textContent(),
    /Paving overlap pairs/,
  );
  assert.match(
    await page.locator(".modal-body").textContent(),
    /Pavement geometry checks passed/,
  );
  await page.click("#close-modal");
  pass("visible folded/inverted/overlap/clearance diagnostics");
  fs.mkdirSync(".playwright", { recursive: true });
  await page.evaluate(() => frontier.world.home());
  await page.screenshot({ path: ".playwright/dark-clean-overview.png" });
  for (const [id, x, z] of [
    ["r130", 142, 96],
    ["r29", 22, -124],
  ]) {
    await page.evaluate(
      ({ id, x, z }) => {
        frontier.selectRoad(id);
        const w = frontier.world;
        w.camera.position.set(x + 11, 17, z + 15);
        w.camera.lookAt(x, 1.7, z);
        w.camera.updateMatrixWorld(true);
        w.highlight.visible = false;
        w.needsRender = true;
      },
      { id, x, z },
    );
    await page.screenshot({ path: `.playwright/fixed-${id}.png` });
  }
  pass("visual regression captures of Foundry and tight Parkside corners");
  assert.deepEqual(errors, []);
  pass("no uncaught browser errors");
} finally {
  await page.close();
  await browser.close();
}
