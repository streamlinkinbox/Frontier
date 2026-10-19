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
  errors = [],
  migration = process.env.SECOND_CITY_PHASE === "migration";
page.on("pageerror", (e) => errors.push(e.message));
try {
  if (migration) {
    const { landscapedProject } = await import("../src/starter.js");
    const saved = landscapedProject();
    saved.edges.find((e) => e.id === "r89").name = "Retain this street";
    await page.addInitScript((payload) => {
      if (!localStorage.getItem("frontier-project-v1"))
        localStorage.setItem("frontier-project-v1", payload);
    }, JSON.stringify(saved));
  }
  await page.goto(process.env.FRONTIER_URL || "http://localhost:5173", {
    timeout: 90000,
  });
  await page.waitForFunction(() => !!window.frontier, {}, { timeout: 90000 });
  assert.equal(
    await page.evaluate(() => frontier.project.sampleCityVersion),
    1,
  );
  assert.equal(
    await page.evaluate(() => frontier.world.pavementCheck.ok),
    true,
  );
  assert.equal(await page.evaluate(() => frontier.world.tramLinks.length), 2);
  if (migration) {
    const state = await page.evaluate(() => ({
      backup: JSON.parse(
        localStorage.getItem("frontier-before-second-city-v1"),
      ),
      p: frontier.project,
    }));
    assert.equal(state.backup.edges.length, 100);
    assert.equal(state.p.edges.length, 306);
    assert.equal(
      state.p.edges.find((e) => e.id === "r89").name,
      "Retain this street",
    );
    for (const e of state.backup.edges)
      assert.deepEqual(
        state.p.edges.find((x) => x.id === e.id),
        e,
      );
    await page.click("#save");
    await page.reload({ timeout: 90000 });
    await page.waitForFunction(() => !!window.frontier, {}, { timeout: 90000 });
    assert.equal(await page.evaluate(() => frontier.project.edges.length), 306);
    assert.equal(
      await page.evaluate(
        () =>
          frontier.project.edges.find((e) => e.id === frontier.state.selected)
            .district,
      ),
      "eastbank",
    );
    assert.equal(await page.evaluate(() => frontier.world.tramLinks.length), 2);
    console.log(
      "PASS automatic old-save upgrade, backup, original-edit preservation, reload without duplication and Eastbank arrival",
    );
  } else {
    fs.mkdirSync(".playwright", { recursive: true });
    await page.screenshot({
      path: ".playwright/second-city-arrival.png",
      animations: "disabled",
    });
    await page.click('[data-tour="tram"]');
    await page.evaluate(() => {
      frontier.world.highlight.visible = false;
      frontier.world.needsRender = true;
    });
    await page.screenshot({
      path: ".playwright/eastbank-tram.png",
      animations: "disabled",
    });
    await page.click("#show-eastbank");
    await page.screenshot({
      path: ".playwright/eastbank-overview.png",
      animations: "disabled",
    });
    const options = await page
      .locator("#city-sample option")
      .evaluateAll((options) => options.map((o) => o.value).filter(Boolean));
    assert.equal(options.length, 33);
    for (const value of options) {
      await page.selectOption("#city-sample", value);
      if (!value.startsWith("rail:")) {
        const e = await page.evaluate(() =>
            frontier.project.edges.find(
              (e) => e.id === frontier.state.selected,
            ),
          ),
          [kind, id] = value.split(":");
        if (kind === "road") assert.equal(e.preset, id);
        if (kind === "curb") assert.equal(e.curbSample, id);
        if (kind === "id") assert.equal(e.id, id);
        if (kind === "network") assert.equal(e.bridgeNetwork, id);
      } else
        assert.equal(
          await page.evaluate(() => frontier.world.layers.rail.visible),
          true,
        );
      if (value === "curb:slot-channel")
        await page.screenshot({
          path: ".playwright/eastbank-drain.png",
          animations: "disabled",
        });
    }
    console.log("PASS all 33 sample destinations");
    await page.click('[data-view="2d"]');
    await page.click("#show-eastbank");
    await page.screenshot({
      path: ".playwright/eastbank-plan.png",
      timeout: 90000,
      animations: "disabled",
    });
    const bounds = await page.locator("#mini-map-svg").getAttribute("viewBox");
    assert.ok(Number(bounds.split(" ")[2]) > 1000);
    console.log(
      "PASS default city arrival, continuous tram junctions, all 33 built sample destinations, 2D and expanded minimap",
    );
  }
  assert.deepEqual(errors, []);
  console.log("PASS no browser errors");
} finally {
  await page.close();
  await browser.close();
}
