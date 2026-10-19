import assert from "node:assert/strict";
import fs from "node:fs";
import { landscapedProject } from "../src/starter.js";
import { addSampleCity } from "../src/sample-city.js";
import { installTransportSamples } from "../src/transport-samples.js";
const baseline = landscapedProject();
addSampleCity(baseline);
installTransportSamples(baseline);
import { chromium } from "playwright";
import chrome from "@sparticuz/chromium";
const browser = await chromium.launch({
  headless: true,
  executablePath: await chrome.executablePath(),
  args: chrome.args,
  env: {
    ...process.env,
    LD_LIBRARY_PATH: `/tmp/frontier-browser-libs/lib:${process.env.LD_LIBRARY_PATH || ""}`,
  },
});
try {
  const page = await browser.newPage({
      viewport: { width: 1500, height: 1000 },
    }),
    errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("console", (m) => {
    if (m.type() === "error") console.log("CONSOLE", m.text());
  });
  const original = JSON.stringify(baseline);
  await page.addInitScript((value) => {
    if (!localStorage.getItem("frontier-project-v1"))
      localStorage.setItem("frontier-project-v1", value);
  }, original);
  await page.goto(process.env.FRONTIER_URL || "http://localhost:5173", {
    waitUntil: "domcontentloaded",
  });
  await page.waitForFunction(
    () => window.frontierStartup?.ready || window.frontierStartup?.failed,
    {},
    { timeout: 90000 },
  );
  assert.equal(
    await page.evaluate(() => window.frontierStartup.failed),
    false,
    await page.evaluate(
      () => document.querySelector("#boot-error")?.textContent,
    ),
  );
  const data = await page.evaluate(() => ({
    version: frontier.project.bridgeNetworkVersion,
    roads: frontier.project.edges.length,
    audit: frontier.audit().issues,
    paving: frontier.world.pavementCheck.ok,
    piers: frontier.world.networkPiers?.length,
    island: frontier.world.networkIslandDeck?.faces.length,
    preserved: JSON.parse(
      localStorage.getItem("frontier-before-bridge-networks-v1"),
    ).edges.every(
      (e) =>
        JSON.stringify(frontier.project.edges.find((n) => n.id === e.id)) ===
        JSON.stringify(e),
    ),
    backup: localStorage.getItem("frontier-before-bridge-networks-v1"),
  }));
  assert.equal(data.version, 1);
  assert.deepEqual(data.audit, []);
  assert.equal(data.paving, true);
  assert.ok(data.piers > 50);
  assert.ok(data.island > 100);
  assert.equal(data.preserved, true);
  assert.equal(data.backup, original);
  await page.click("#undo");
  assert.equal(
    await page.evaluate(() => frontier.project.bridgeNetworkVersion),
    undefined,
  );
  assert.equal(await page.evaluate(() => frontier.world.networkDeck), null);
  await page.click("#redo");
  assert.equal(
    await page.evaluate(() => frontier.project.edges.length),
    data.roads,
  );
  for (const id of ["loop", "roundabout", "branch"]) {
    await page.selectOption("#city-sample", `network:${id}`);
    await page.screenshot({ path: `.playwright/network-${id}.png` });
    // Detail view shows the assembled deck and clearance, not just the road plan.
    await page.evaluate((id) => {
      const o = frontier.project.bridgeNetworks.find((n) => n.id === id).origin;
      frontier.world.controls.target.set(o.x, 9, o.z);
      frontier.world.camera.position.set(o.x + 250, 240, o.z + 300);
      frontier.world.controls.update();
      frontier.world.needsRender = true;
    }, id);
    await page.screenshot({ path: `.playwright/network-${id}-detail.png` });
  }
  await page.click("#tour-networks");
  assert.equal(await page.locator(".network-cards [data-network]").count(), 3);
  await page.screenshot({ path: ".playwright/network-picker.png" });
  await page.click('[data-network="loop"]');
  await page.click('[data-view="2d"]');
  await page.screenshot({ path: ".playwright/network-plan.png" });
  await page.reload({ waitUntil: "domcontentloaded" });
  await page.waitForFunction(
    () => window.frontierStartup?.ready || window.frontierStartup?.failed,
    {},
    { timeout: 90000 },
  );
  assert.equal(
    await page.evaluate(() => frontier.project.edges.length),
    data.roads,
  );
  assert.deepEqual(errors, []);
  console.log(
    "NETWORK BROWSER PASS",
    JSON.stringify({ ...data, backup: "preserved byte-for-byte", errors }),
  );
} finally {
  await browser.close();
}
