/*============================================================================================================================================
                                                     CAPTURENATIVEPATTERNREFERENCE.MJS
============================================================================================================================================*/
// 🧩 Measure the approved live Pattern HTML and retain actual screenshots for the native UI port.

import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { writeFileSync } from "node:fs";
const Require = createRequire(new URL("../../../Experimental/TextureStudio/package.json", import.meta.url));
const { chromium: Chromium } = Require("playwright");
const Browser = await Chromium.launch({ executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH, headless: true, args: ["--no-sandbox", "--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--in-process-gpu"] });
try
{
    const Page = await Browser.newPage({ viewport: { width: 1440, height: 900 } });
    await Page.goto("http://127.0.0.1:5173/?studio=pattern&pattern=Blank");
    await Page.getByRole("button", { name: "Add pattern layer", exact: true }).click();
    await Page.getByRole("group", { name: "Add a pattern motif", exact: true }).getByRole("button", { name: "Rectangle", exact: true }).click();
    const HideLibrary = Page.getByRole("button", { name: "Hide pattern library", exact: true });
    if (await HideLibrary.count()) await HideLibrary.click();
    await Page.evaluate(() => document.fonts.ready);
    const Reference = await Page.evaluate(() => Object.fromEntries([".pe-header", ".pe-library", ".pe-layers-dock", ".pe-workspace", ".pe-inspector", ".pe-mode-switch", ".pe-canvas-heading", ".se-toolbar-wrap", ".se-viewport", ".pattern-layer-heading", ".pe-repeat", ".pattern-channel-card"].map(Selector =>
    {
        const Element = document.querySelector(Selector);
        if (!Element) return [Selector, null];
        const Rectangle = Element.getBoundingClientRect(), Style = getComputedStyle(Element);
        return [Selector, { X: Rectangle.x, Y: Rectangle.y, Width: Rectangle.width, Height: Rectangle.height, Background: Style.backgroundColor, Font: Style.fontFamily, FontSize: Style.fontSize, Padding: Style.padding }];
    })));
    const Destination = fileURLToPath(new URL("../Results/NativePatternHtmlReference.json", import.meta.url));
    writeFileSync(Destination, JSON.stringify({ Viewport: { Width: 1440, Height: 900 }, Document: "Blank plus one rectangle; library collapsed", Reference }, null, 2) + "\n");
    await Page.screenshot({ path: fileURLToPath(new URL("../Images/NativePatternHtmlReference.png", import.meta.url)) });
    console.log(JSON.stringify(Reference, null, 2));
}
finally
{
    await Browser.close();
}
