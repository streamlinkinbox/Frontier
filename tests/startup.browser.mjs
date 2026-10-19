import assert from "node:assert/strict";
import fs from "node:fs";
import { chromium } from "playwright";
import chrome from "@sparticuz/chromium";
const host = "sbx-dugw5jlh2ygxc25b.arena.site";
const browser = await chromium.launch({
  headless: true,
  executablePath: await chrome.executablePath(),
  args: [
    ...chrome.args,
    `--host-resolver-rules=MAP ${host} 127.0.0.1`,
    "--no-proxy-server",
  ],
  env: {
    ...process.env,
    LD_LIBRARY_PATH: `/tmp/frontier-browser-libs/lib:${process.env.LD_LIBRARY_PATH || ""}`,
  },
});
const url = `http://${host}:5173`;
try {
  const context = await browser.newContext({
    viewport: { width: 1440, height: 960 },
  });
  const page = await context.newPage();
  const errors = [],
    failures = [],
    scripts = [];
  page.on("pageerror", (e) => errors.push(e.message));
  page.on("response", (r) => {
    if (r.status() >= 400) failures.push(`${r.status()} ${r.url()}`);
    if (r.request().resourceType() === "script") scripts.push(r.url());
  });
  const started = Date.now();
  await page.goto(url, { waitUntil: "domcontentloaded" });
  await page.waitForFunction(
    () => window.frontierStartup?.ready,
    {},
    { timeout: 150000 },
  );
  const coldMs = Date.now() - started;
  assert.equal(await page.locator("#boot-screen").count(), 0);
  const result = await page.evaluate(() => ({
    phases: window.frontierStartup.phases,
    roads: window.frontier.project.edges.length,
    transport: window.frontier.project.transportVersion,
  }));
  assert.equal(result.transport, 1);
  assert.equal(result.phases.length, 7);
  assert.ok(
    scripts.length <= 3,
    `Expected bundled startup, got ${scripts.length} script requests`,
  );
  assert.ok(
    scripts.every((s) => !s.includes("/src/") && !s.includes("env.mjs")),
  );
  await page.screenshot({ path: ".playwright/loading-ready.png" });
  const reloadStarted = Date.now();
  await page.reload({ waitUntil: "domcontentloaded" });
  await page.waitForFunction(
    () => window.frontierStartup?.ready,
    {},
    { timeout: 150000 },
  );
  assert.equal(
    await page.evaluate(() => window.frontier.project.edges.length),
    result.roads,
  );
  assert.deepEqual(errors, []);
  assert.deepEqual(failures, []);
  console.log(
    JSON.stringify(
      {
        status: "PASS",
        host,
        coldMs,
        reloadMs: Date.now() - reloadStarted,
        ...result,
        scripts,
      },
      null,
      2,
    ),
  );

  // A missing main bundle must show recovery controls and preserve raw storage.
  const failed = await context.newPage();
  await page.close();
  const saved = JSON.stringify({
    recoveryTest: true,
    note: "Do not discard saved user data",
  });
  await failed.addInitScript(
    (value) => localStorage.setItem("frontier-project-v1", value),
    saved,
  );
  await failed.route("**/assets/main-*.js", (route) => route.abort("failed"));
  await failed.goto(url, { waitUntil: "domcontentloaded" });
  await failed.waitForFunction(() => window.frontierStartup?.failed);
  assert.equal(
    await failed.locator("#boot-title").textContent(),
    "The editor could not start",
  );
  assert.equal(
    await failed.evaluate(() => localStorage.getItem("frontier-project-v1")),
    saved,
  );
  const downloadPromise = failed.waitForEvent("download");
  await failed.locator("#boot-backup").click();
  const download = await downloadPromise;
  assert.equal(fs.readFileSync(await download.path(), "utf8"), saved);
  assert.ok(await failed.locator("#boot-retry").isVisible());
  await failed.screenshot({ path: ".playwright/loading-recovery.png" });
  console.log(
    "PASS: failed-bundle recovery and unchanged downloadable saved map",
  );
} finally {
  await browser.close();
}
