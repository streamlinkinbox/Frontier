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
page.on("pageerror", (e) => {
  errors.push(e.message);
  console.error("PAGE ERROR", e.message);
});
try {
  if (process.env.TRANSPORT_PHASE === "repair") {
    const { landscapedProject } = await import("../src/starter.js");
    const { addSampleCity } = await import("../src/sample-city.js");
    const { addRoad } = await import("../src/model.js");
    const p = landscapedProject();
    addSampleCity(p);
    const e = p.edges.find((e) => e.name.includes("Campus Cycleway")),
      a = p.nodes.find((n) => n.id === e.a),
      b = p.nodes.find((n) => n.id === e.b),
      length = Math.hypot(b.x - a.x, b.z - a.z),
      point = {
        x: a.x + ((b.x - a.x) * 2) / length,
        z: a.z + ((b.z - a.z) * 2) / length,
      };
    addRoad(p, point, { x: point.x, z: point.z - 90 }, "local", {
      district: "eastbank",
      name: "Broken generated edit",
    });
    addRoad(p, { x: -600, z: -400 }, { x: -500, z: -400 }, "country", {
      name: "Keep this custom road",
    });
    await page.addInitScript((payload) => {
      if (!localStorage.getItem("frontier-project-v1"))
        localStorage.setItem("frontier-project-v1", payload);
    }, JSON.stringify(p));
  }
  await page.goto(process.env.FRONTIER_URL || "http://localhost:5173", {
    timeout: 120000,
  });
  await page.waitForFunction(() => !!window.frontier, {}, { timeout: 120000 });
  assert.deepEqual(await page.evaluate(() => frontier.audit().issues), []);
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  assert.equal(await page.evaluate(() => frontier.project.transportVersion), 1);
  assert.equal(
    await page.evaluate(() => frontier.world.railCrossings.length),
    2,
  );
  if (process.env.TRANSPORT_PHASE === "repair") {
    assert.match(
      await page.locator(".transport-notice").innerText(),
      /rebuilt/,
    );
    assert.equal(
      await page.evaluate(
        () =>
          frontier.project.edges.filter(
            (e) => e.name === "Keep this custom road",
          ).length,
      ),
      1,
    );
    assert.equal(
      await page.evaluate(() =>
        JSON.parse(
          localStorage.getItem("frontier-before-transport-v1"),
        ).edges.some((e) => e.name === "Broken generated edit"),
      ),
      true,
    );
    await page.reload({ timeout: 120000 });
    await page.waitForFunction(
      () => !!window.frontier,
      {},
      { timeout: 120000 },
    );
    assert.equal(await page.evaluate(() => frontier.project.edges.length), 307);
  } else {
    await page.click('[data-tour="parking"]');
    await page.screenshot({ path: ".playwright/transport-parking.png" });
    await page.click("#tour-bridge");
    await page.screenshot({ path: ".playwright/transport-bridge.png" });
    await page.selectOption("#city-sample", "rail:eastbank-rail-exit");
    await page.screenshot({ path: ".playwright/transport-rail-curve.png" });
    await page.selectOption("#city-sample", "rail:eastbank-monorail-exit");
    await page.screenshot({ path: ".playwright/transport-monorail.png" });
    await page.click("#tour-crossing");
    await page.screenshot({ path: ".playwright/transport-crossing.png" });
    await page.click("#rail-exit-tool");
    await page.fill("#exit-radius", "90");
    await page.click("#apply-rail-exit");
    await page.waitForFunction(
      () => !document.querySelector("#apply-rail-exit"),
      {},
      { timeout: 90000 },
    );
    assert.equal(
      await page.evaluate(() =>
        frontier.project.corridors.some((c) => c.turnRadius === 90),
      ),
      true,
    );
    await page.click("#undo");
    assert.equal(
      await page.evaluate(() =>
        frontier.project.corridors.some((c) => c.turnRadius === 90),
      ),
      false,
    );
    await page.click('[data-view="2d"]');
    await page.click("#tour-crossing");
    await page.screenshot({ path: ".playwright/transport-crossing-plan.png" });
  }
  assert.deepEqual(errors, []);
  console.log(
    "TRANSPORT BROWSER PASS",
    process.env.TRANSPORT_PHASE || "fresh",
    await page.evaluate(() => ({
      roads: frontier.project.edges.length,
      crossings: frontier.world.railCrossings.length,
      issues: frontier.audit().issues,
    })),
  );
} finally {
  await browser.close();
}
