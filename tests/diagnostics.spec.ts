import { test, expect } from "@playwright/test";
import { readFile } from "node:fs/promises";

const ready = async (page: import("@playwright/test").Page) => {
  await page.waitForFunction(
    () =>
      window.__frontier?.backend &&
      !document.querySelector(".viewport-loading"),
  );
};

test("GPU logs capture startup, hidden-canvas CSS and real frame probes without editing the volume", async ({
  page,
}) => {
  await page.goto("/?renderer=webgpu");
  await ready(page);
  expect(await page.evaluate(() => window.__frontier!.backend.name)).toBe(
    "WebGPU",
  );
  await page.evaluate(async () => {
    (window as any).beforeDiagnostics =
      await window.__frontier!.backend.readVolume();
    // Reproduce the distinction we need to diagnose: GPU work can succeed while
    // CSS prevents any pixels from being visible. Do not call that a screen pass.
    window.__frontier!.canvas.style.visibility = "hidden";
    HTMLCanvasElement.prototype.toBlob = () => {
      throw new Error("Diagnostic probes must not route through PNG/Canvas2D");
    };
    Object.defineProperty(navigator, "clipboard", {
      configurable: true,
      value: {
        writeText: async (text: string) => {
          (window as any).copiedDiagnostics = text;
        },
      },
    });
  });
  await page.getByRole("button", { name: "GPU logs", exact: true }).click();
  const report = page.getByRole("textbox", { name: "GPU diagnostic report" });
  await expect(report).toHaveValue(/Adapter selected/);
  await expect(report).toHaveValue(/Shader compilation/);
  await expect(report).toHaveValue(/"visibility": "hidden"/);
  await page
    .getByRole("button", { name: "Check viewport", exact: true })
    .click();
  await expect(report).toHaveValue(/Viewport checks complete/);
  await expect(report).toHaveValue(/Owned render target pixel check/);
  await expect(report).toHaveValue(/SDF volume statistics/);
  await expect(report).toHaveValue(/"nonFiniteSDF": 0/);
  await expect(report).toHaveValue(/"nonUniformOpaque": true/);
  await page
    .getByRole("button", { name: "Copy GPU logs", exact: true })
    .click();
  await expect(
    page.getByRole("button", { name: "Copied GPU logs" }),
  ).toBeVisible();
  const copied = await page.evaluate(() => (window as any).copiedDiagnostics);
  expect(copied).toContain("FRONTIER GPU DIAGNOSTICS");
  expect(copied).toContain(
    "NOT proof that the browser compositor displayed the canvas",
  );
  expect(copied).toContain("Device created");
  expect(copied).not.toContain("?renderer=");
  expect(
    await page.evaluate(async () => {
      const e = window.__frontier!;
      const now = await e.backend.readVolume();
      return {
        unchanged: now.every(
          (v, i) => v === (window as any).beforeDiagnostics[i],
        ),
        steps: e.steps,
        busy: e.busy,
      };
    }),
  ).toEqual({ unchanged: true, steps: 0, busy: false });
});

test("blocked iframe clipboard access leaves selected logs and a working text download", async ({
  page,
}) => {
  await page.addInitScript(() => {
    Object.defineProperty(navigator, "clipboard", {
      configurable: true,
      value: {
        writeText: async () => {
          throw new DOMException("Clipboard blocked", "NotAllowedError");
        },
      },
    });
    document.execCommand = () => false;
  });
  await page.goto("/?renderer=webgl&diagnostics=1");
  await ready(page);
  await expect(
    page.getByRole("dialog", { name: "GPU diagnostics" }),
  ).toBeVisible();
  await page
    .getByRole("button", { name: "Copy GPU logs", exact: true })
    .click();
  await expect(page.locator(".diagnostics-feedback")).toContainText(
    "report is selected",
  );
  const report = page.getByRole("textbox", { name: "GPU diagnostic report" });
  const selection = await report.evaluate((area: HTMLTextAreaElement) => ({
    start: area.selectionStart,
    end: area.selectionEnd,
    size: area.value.length,
  }));
  expect(selection.start).toBe(0);
  expect(selection.end).toBe(selection.size);
  const pending = page.waitForEvent("download");
  await page
    .getByRole("button", { name: "Download logs", exact: true })
    .click();
  const file = await pending;
  expect(file.suggestedFilename()).toBe("frontier-gpu-diagnostics.txt");
  const text = await readFile((await file.path())!, "utf8");
  expect(text).toContain("FRONTIER GPU DIAGNOSTICS");
  expect(text).toContain("WebGL2");
  expect(text).toContain("unmaskedRenderer");
  await page.setViewportSize({ width: 390, height: 844 });
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(
    390,
  );
  await expect(
    page.getByRole("button", { name: "Download logs", exact: true }),
  ).toBeVisible();
});

test("logs remain accessible when neither renderer can start", async ({
  page,
}) => {
  await page.addInitScript(() => {
    navigator.gpu.requestAdapter = async () => {
      throw new Error("Diagnostic test: adapter unavailable");
    };
    const get = HTMLCanvasElement.prototype.getContext;
    HTMLCanvasElement.prototype.getContext = function (...args: any[]) {
      if (args[0] === "webgl2") return null;
      return get.apply(this, args as any);
    } as typeof get;
  });
  await page.goto("/");
  await expect(page.locator(".has-error")).toBeVisible();
  await page
    .getByRole("button", { name: "Show GPU logs", exact: true })
    .click();
  const report = page.getByRole("textbox", { name: "GPU diagnostic report" });
  await expect(report).toHaveValue(/Diagnostic test: adapter unavailable/);
  await expect(report).toHaveValue(/Neither WebGPU nor WebGL2/);
  await expect(
    page.getByRole("button", { name: "Copy GPU logs", exact: true }),
  ).toBeEnabled();
  await page
    .getByRole("button", { name: "Check viewport", exact: true })
    .click();
  await expect(report).toHaveValue(/GPU checks skipped/);
});
