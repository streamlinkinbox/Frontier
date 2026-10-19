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
  fs.mkdirSync(".playwright", { recursive: true });
  const colors = await page.evaluate(() =>
    [".app-header", ".library", ".inspector"].map(
      (s) => getComputedStyle(document.querySelector(s)).backgroundColor,
    ),
  );
  for (const color of colors) {
    const values = color.match(/\d+/g).map(Number);
    assert.equal(values[0], values[1]);
    assert.equal(values[1], values[2]);
    assert.ok(values[0] <= 32);
  }
  assert.equal(
    await page.evaluate(() => frontier.world.scene.background.getHexString()),
    "1c1c1c",
  );
  pass("charcoal/black surfaces with no navy tint");
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.missingCorners),
    0,
  );
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.seamErrors),
    0,
  );
  async function frame(node, road, path) {
    await page.evaluate(
      ({ node, road }) => {
        frontier.selectRoad(road);
        const w = frontier.world,
          n = frontier.project.nodes.find((n) => n.id === node);
        w.camera.position.set(n.x + 15, 26, n.z + 24);
        w.camera.lookAt(n.x, 1.5, n.z);
        w.camera.updateMatrixWorld(true);
        w.highlight.visible = false;
        w.needsRender = true;
      },
      { node, road },
    );
    await page.screenshot({ animations: "disabled", path });
  }
  await frame("n141", "r144", ".playwright/continuous-rural-bend.png");
  await frame("n2", "r67", ".playwright/continuous-rural-crossing.png");
  await page.click("#street-audit");
  assert.match(
    await page.locator(".modal-body").textContent(),
    /Missing junction edge returns/,
  );
  assert.match(
    await page.locator(".modal-body").textContent(),
    /Pavement geometry checks passed/,
  );
  await page.click("#close-modal");
  await page.click("#export");
  const [download] = await Promise.all([
    page.waitForEvent("download"),
    page.click("#download-street"),
  ]);
  assert.match(fs.readFileSync(await download.path(), "utf8"), /^g shoulder$/m);
  await page.click("#close-modal");
  pass("rural returns, visible seam checks and shoulder OBJ export");
  await page.evaluate(() => frontier.selectRoad("r21"));
  await page.selectOption("#street-paving", "brick");
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  await frame("n20", "r21", ".playwright/mixed-paving-return.png");
  assert.ok(
    await page.evaluate(() =>
      frontier.world.streetscape.transitions.some(
        (t) => t.mode === "crossing-ramp" || t.mode === "hard-joint",
      ),
    ),
  );
  await page.click("#undo");
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  pass("mixed paving edit uses complete corner ownership and survives undo");
  await frame("n83", "r84", ".playwright/paving-to-shoulder.png");
  await page.click("#theme-toggle");
  assert.equal(
    await page.evaluate(() => document.documentElement.dataset.theme),
    "light",
  );
  await page.click("#theme-toggle");
  await page.reload();
  await page.waitForFunction(() => !!window.frontier);
  assert.equal(
    await page.evaluate(() => document.documentElement.dataset.theme),
    "dark",
  );
  assert.deepEqual(errors, []);
  pass("theme persistence and no browser errors");
} finally {
  await page.close();
  await browser.close();
}
