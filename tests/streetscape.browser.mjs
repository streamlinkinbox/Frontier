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
const get = (fn) => page.evaluate(fn),
  pass = (s) => console.log(`PASS ${s}`);
try {
  await page.goto(process.env.FRONTIER_URL || "http://localhost:5173");
  await page.waitForFunction(() => !!window.frontier);
  if (process.env.FRONTIER_TEST_PHASE !== "storage") {
    assert.equal(
      await get(() => frontier.world.camera.isPerspectiveCamera),
      true,
    );
    await page.click("#inspect-curb");
    const before = await get(() => frontier.world.camera.position.toArray());
    await page.mouse.move(750, 500);
    await page.mouse.down({ button: "right" });
    assert.equal(await get(() => frontier.world.fly.active), true);
    await page.keyboard.down("w");
    await page.waitForTimeout(250);
    await page.keyboard.up("w");
    const after = await get(() => frontier.world.camera.position.toArray());
    assert.ok(Math.hypot(...after.map((v, i) => v - before[i])) > 0.05);
    pass("RMB + W moves the perspective camera");
    await page.keyboard.down("d");
    await page.waitForTimeout(120);
    await page.keyboard.up("d");
    assert.equal(await get(() => frontier.state.tool), "select");
    pass("D strafes without activating the delete tool");
    const y = await get(() => frontier.world.camera.position.y);
    await page.keyboard.down("e");
    await page.waitForTimeout(120);
    await page.keyboard.up("e");
    assert.ok((await get(() => frontier.world.camera.position.y)) > y);
    pass("E raises camera height");
    const speed = await get(() => frontier.world.fly.speed);
    await page.mouse.wheel(0, -100);
    assert.ok((await get(() => frontier.world.fly.speed)) > speed);
    const rotation = await get(() =>
      frontier.world.camera.quaternion.toArray(),
    );
    await page.mouse.move(790, 525, { steps: 3 });
    assert.notDeepEqual(
      await get(() => frontier.world.camera.quaternion.toArray()),
      rotation,
    );
    pass("RMB look and wheel speed adjustment");
    await page.keyboard.down("w");
    await page.mouse.up({ button: "right" });
    assert.equal(await get(() => frontier.world.fly.keys.size), 0);
    const released = await get(() => frontier.world.camera.position.toArray());
    await page.waitForTimeout(100);
    assert.deepEqual(
      await get(() => frontier.world.camera.position.toArray()),
      released,
    );
    await page.keyboard.up("w");
    pass("release clears held movement keys without drift");
    await page.mouse.down({ button: "right" });
    await page.keyboard.down("a");
    await page.evaluate(() => window.dispatchEvent(new Event("blur")));
    assert.equal(await get(() => frontier.world.fly.active), false);
    assert.equal(await get(() => frontier.world.fly.keys.size), 0);
    await page.keyboard.up("a");
    await page.mouse.up({ button: "right" });
    pass("window blur safely stops flying");
    await page.click('[data-view="2d"]');
    assert.equal(
      await get(() => frontier.world.camera.isOrthographicCamera),
      true,
    );
    assert.equal(await get(() => frontier.world.fly.enabled), false);
    await page.click('[data-view="3d"]');
    await page.click("#camera-navigation");
    assert.equal(await get(() => frontier.world.navigation), "orbit");
    await page.click("#camera-navigation");
    assert.equal(await get(() => frontier.world.fly.enabled), true);
    pass("orthographic editing and optional orbit navigation remain available");
    await page.click("#street-presets");
    assert.equal(await page.locator("[data-street-preset]").count(), 9);
    await page.click('[data-street-preset="granite-channel"]');
    assert.equal(
      await get(
        () =>
          frontier.project.edges.find((e) => e.id === frontier.state.selected)
            .drainage,
      ),
      "channel",
    );
    assert.equal(
      await get(
        () =>
          frontier.project.edges.find((e) => e.id === frontier.state.selected)
            .sidewalk,
      ),
      2.4,
    );
    await page.click("#inspect-drain");
    assert.equal(await get(() => frontier.world.highlight.visible), false);
    pass(
      "reference presets apply real curb/drain geometry and street-level framing",
    );
    await page.selectOption("#street-rampStyle", "grooved");
    await page.selectOption("#street-tactile", "red");
    assert.ok(
      (await get(() => frontier.world.streetscape.instances.red.length)) > 0,
    );
    await page.fill("#street-height", "0.18");
    await page.locator("#street-height").press("Tab");
    assert.equal(
      await get(
        () =>
          frontier.project.edges.find((e) => e.id === frontier.state.selected)
            .curbHeight,
      ),
      0.18,
    );
    pass("curb dimensions, ramp design and tactile material controls");
  } else {
    await page.click("#street-presets");
    await page.click('[data-street-preset="granite-channel"]');
    await page.fill("#street-height", "0.18");
    await page.locator("#street-height").press("Tab");
  }
  if (process.env.FRONTIER_TEST_PHASE !== "camera") {
    await page.click("#save");
    await page.reload();
    await page.waitForFunction(() => !!window.frontier);
    assert.equal(
      await get(
        () =>
          frontier.project.edges.find((e) => e.id === frontier.state.selected)
            .curbHeight,
      ),
      0.18,
    );
    pass("new streetscape properties persist");
    await page.click("#export");
    const [download] = await Promise.all([
      page.waitForEvent("download"),
      page.click("#download-street"),
    ]);
    assert.equal(download.suggestedFilename(), "frontier-streetscape.obj");
    await page.click("#close-modal");
    pass("selected streetscape quad OBJ download");
    await page.click("#street-audit");
    assert.equal(
      await page.locator("#modal-title").textContent(),
      "Street geometry, not stacked blocks.",
    );
    await page.click("#close-modal");
    assert.deepEqual(errors, []);
    pass("street diagnostics and zero uncaught browser errors");
  }
  assert.deepEqual(errors, []);
  console.log("All requested camera and streetscape browser checks passed.");
} finally {
  await page.close();
  await browser.close();
}
