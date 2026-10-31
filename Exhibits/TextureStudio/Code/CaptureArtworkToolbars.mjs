/*============================================================================================================================================
                                                   CAPTUREARTWORKTOOLBARS.MJS
============================================================================================================================================*/
// 🧩 Actual browser captures of the compact Pattern and Stamp drawing controls and component panels.

import { chromium } from "../../../Experimental/TextureStudio/node_modules/playwright/index.mjs";
import { fileURLToPath } from "node:url";
const Browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_EXECUTABLE_PATH ? { executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH } : {}), args: ["--no-sandbox", "--disable-dev-shm-usage", "--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--in-process-gpu", "--no-zygote"] });
const Page = await Browser.newPage({ viewport: { width: 1440, height: 900 } });
const Failures = [];
Page.on("pageerror", Failure => Failures.push(Failure.message));
try
{
    await Page.goto("http://127.0.0.1:5173/?studio=stamp");
    await Page.getByRole("button", { name: "Vector canvas", exact: true }).click();
    await Page.getByRole("button", { name: "Add stamp component", exact: true }).click();
    await Page.getByRole("menuitem", { name: "Rectangle", exact: true }).click();
    await Page.mouse.move(1, 1);
    await Page.screenshot({ path: fileURLToPath(new URL("../Images/StampCompactTools.png", import.meta.url)), fullPage: true });
    await Page.goto("http://127.0.0.1:5173/?studio=pattern&pattern=Diamond%20weave");
    await Page.getByRole("toolbar", { name: "Drawing tools", exact: true }).waitFor();
    await Page.mouse.move(1, 1);
    await Page.screenshot({ path: fileURLToPath(new URL("../Images/PatternCompactToolsNormal.png", import.meta.url)), fullPage: true });
    if (Failures.length) throw new Error(Failures.join("\n"));
    console.log("Pattern and Stamp toolbar screenshots captured; no browser page errors.");
}
finally
{
    await Browser.close();
}
