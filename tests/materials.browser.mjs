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
    await page.evaluate(() =>
      frontier.world.streetscape.mesh.groups.includes("brick"),
    ),
    false,
  );
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  await page.evaluate(() => frontier.selectRoad("r89"));
  assert.equal(
    await page.evaluate(
      () => frontier.world.highlight.children[0].isLineSegments,
    ),
    true,
  );
  pass("concrete gutter defaults and outline-only selection");
  await page.click("#inspect-curb");
  fs.mkdirSync(".playwright", { recursive: true });
  await page.screenshot({ path: ".playwright/materials-cedar-ramp.png" });
  await page.selectOption("#street-paving", "brick");
  assert.equal(
    await page.evaluate(
      () => frontier.project.edges.find((e) => e.id === "r89").paving,
    ),
    "brick",
  );
  assert.equal(
    await page.evaluate(() =>
      frontier.world.streetscape.mesh.groups.includes("brick"),
    ),
    true,
  );
  await page.selectOption("#street-paving", "slab");
  assert.equal(
    await page.evaluate(() =>
      frontier.world.streetscape.mesh.groups.includes("brick"),
    ),
    false,
  );
  pass("brick is opt-in, concrete slabs survive rebuild");
  await page.click("#export");
  const [download] = await Promise.all([
    page.waitForEvent("download"),
    page.click("#download-street"),
  ]);
  const obj = fs.readFileSync(await download.path(), "utf8");
  assert.match(obj, /^vt /m);
  assert.match(obj, /^f \d+\/\d+ \d+\/\d+ \d+\/\d+ \d+\/\d+$/m);
  await page.click("#close-modal");
  pass("actual streetscape download contains quad UV coordinates");
  await page.reload();
  await page.waitForFunction(() => !!window.frontier);
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  assert.equal(
    await page.evaluate(() =>
      frontier.world.streetscape.mesh.groups.includes("brick"),
    ),
    false,
  );
  await page.evaluate(() => {
    frontier.selectRoad("r130");
    const w = frontier.world;
    w.camera.position.set(151, 17, 111);
    w.camera.lookAt(140, 1.7, 96);
    w.camera.updateMatrixWorld(true);
    w.needsRender = true;
  });
  await page.screenshot({ path: ".playwright/materials-curves.png" });
  assert.deepEqual(errors, []);
  pass("reload, curved-corner capture and no browser exceptions");
} finally {
  await page.close();
  await browser.close();
}
