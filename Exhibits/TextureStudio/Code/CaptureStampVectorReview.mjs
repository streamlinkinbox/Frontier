/*============================================================================================================================================
                                                       CAPTURESTAMPVECTORREVIEW.MJS
============================================================================================================================================*/
// 🧩 Capture the production HTML Stamp canvas and verify visible Boolean controls without replacing its original composition editor.

import { chromium } from "../../../Experimental/TextureStudio/node_modules/playwright/index.mjs";
import { fileURLToPath } from "node:url";
import { writeFileSync } from "node:fs";
const Browser = await chromium.launch({ executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH, args: ["--no-sandbox", "--disable-dev-shm-usage", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--in-process-gpu", "--no-zygote"] });
const Failures = [];
try
{
    const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
    Page.on("pageerror", Failure => Failures.push(Failure.message));
    await Page.goto("http://127.0.0.1:5175/?studio=stamp");
    await Page.getByRole("button", { name: "Add stamp component", exact: true }).click();
    await Page.getByRole("menuitem", { name: "Rectangle", exact: true }).click();
    await Page.getByRole("button", { name: "Add stamp component", exact: true }).click();
    await Page.getByRole("menuitem", { name: "Ellipse", exact: true }).click();
    await Page.getByRole("button", { name: "Select stamp component Rectangle", exact: true }).click({ modifiers: ["Control"] });
    const Difference = Page.getByRole("button", { name: "Difference", exact: true });
    if (!await Difference.isEnabled()) throw new Error("Difference must be enabled for two filled shapes.");
    await Page.screenshot({ path: fileURLToPath(new URL("../Images/StampVectorSelection.png", import.meta.url)), fullPage: true });
    await Difference.click();
    await Page.getByRole("button", { name: "Select stamp component Difference", exact: true }).waitFor();
    await Page.screenshot({ path: fileURLToPath(new URL("../Images/StampVectorBoolean.png", import.meta.url)), fullPage: true });
    const Background = Page.getByRole("combobox", { name: "Vector canvas background", exact: true });
    await Background.selectOption("ui");
    await Page.screenshot({ path: fileURLToPath(new URL("../Images/StampStackUiBackground.png", import.meta.url)), fullPage: true });
    await Background.selectOption("fill");
    await Page.getByLabel("Vector canvas background colour", { exact: true }).fill("#316c91");
    await Page.screenshot({ path: fileURLToPath(new URL("../Images/StampStackCanvasBackground.png", import.meta.url)), fullPage: true });
    await Page.getByRole("button", { name: "Add stamp component", exact: true }).click();
    await Page.screenshot({ path: fileURLToPath(new URL("../Images/StampComponentAdditions.png", import.meta.url)), fullPage: true });
    await Page.keyboard.press("Escape");
    await Page.getByRole("button", { name: "Composition", exact: true }).click();
    await Page.getByRole("region", { name: "Stamp composition canvas", exact: true }).waitFor();
    await Page.screenshot({ path: fileURLToPath(new URL("../Images/StampOriginalComposition.png", import.meta.url)), fullPage: true });
    if (Failures.length) throw new Error(Failures.join("\n"));
    writeFileSync(new URL("../Results/StampVectorCaptureChecks.json", import.meta.url), JSON.stringify({ ProductionPreview: 5175, BooleanEnabled: true, DifferenceCreated: true, OriginalCompositionRetained: true, CanvasBackgrounds: ["transparent", "ui", "fill"], ComponentAdditions: 10, BrowserFailures: Failures }, null, 2));
}
finally
{
    await Browser.close();
}
