/*============================================================================================================================================
                                                     CAPTUREPATTERNCOLOURTOOLTIPS.MJS
============================================================================================================================================*/
// 🧩 Actual HTML Pattern colour-source, layer-panel and shared-tooltip captures.

import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { mkdir, writeFile } from "node:fs/promises";
const Require = createRequire(new URL("../../../Experimental/TextureStudio/package.json", import.meta.url));
const { chromium: Chromium } = Require("playwright");
const ImageDirectory = fileURLToPath(new URL("../Images/", import.meta.url));
const ResultDirectory = fileURLToPath(new URL("../Results/", import.meta.url));
await mkdir(ImageDirectory, { recursive: true });
await mkdir(ResultDirectory, { recursive: true });
const Browser = await Chromium.launch({ ...(process.env.PLAYWRIGHT_EXECUTABLE_PATH ? { executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH } : {}), headless: true, args: ["--no-sandbox", "--disable-dev-shm-usage", "--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--in-process-gpu", "--no-zygote"] });
const Page = await Browser.newPage({ viewport: { width: 1600, height: 1000 } });
Page.setDefaultTimeout(15000);
const BrowserFailures = [];
Page.on("pageerror", Failure => BrowserFailures.push(Failure.message));
async function AddMotif(Caption)
{
    await Page.getByRole("button", { name: "Add pattern layer", exact: true }).click();
    await Page.getByRole("group", { name: "Add a pattern motif", exact: true }).getByRole("button", { name: Caption, exact: true }).click();
}
async function SetField(Caption, Value)
{
    await Page.getByLabel(Caption, { exact: true }).fill(String(Value));
}
try
{
    await Page.goto(`${process.env.STUDIO_EVIDENCE_URL || "http://127.0.0.1:5173"}/?studio=pattern&pattern=Blank`);
    await AddMotif("Rectangle");
    await SetField("Layer name", "Gradient foundation");
    await SetField("Width", 350); await SetField("Height", 350);
    await Page.getByLabel("Motif fill source", { exact: true }).selectOption("linear");
    await SetField("Motif fill stop 1 colour", "#15424a"); await SetField("Motif fill stop 2 colour", "#648c79");
    await Page.getByLabel("Motif stroke source", { exact: true }).selectOption("linear");
    await SetField("Motif stroke stop 1 colour", "#d7ba81"); await SetField("Motif stroke stop 2 colour", "#7d9b87");
    await SetField("Stroke width", 5);
    await AddMotif("Rosette");
    await SetField("Layer name", "Radial rosette"); await SetField("Width", 230); await SetField("Height", 230);
    await Page.getByLabel("Motif fill source", { exact: true }).selectOption("radial");
    await SetField("Motif fill stop 1 colour", "#f4ddb3"); await SetField("Motif fill stop 2 colour", "#bb825a");
    await SetField("Motif stroke stop 1 colour", "#f6dfae"); await SetField("Motif stroke stop 2 colour", "#dbb477");
    await SetField("Stroke width", 2);
    await AddMotif("Ring"); await SetField("Layer name", "Metallic outline"); await SetField("Width", 300); await SetField("Height", 300); await SetField("Stroke width", 4);
    await Page.getByRole("list", { name: "Pattern layers", exact: true }).getByRole("button", { name: "Radial rosette", exact: true }).click();
    await Page.locator('.pattern-property-card > summary').filter({ hasText: /^Transform$/ }).click();
    await Page.getByLabel("Motif fill source", { exact: true }).scrollIntoViewIfNeeded();
    await Page.evaluate(() => document.fonts.ready);
    await Page.getByRole("button", { name: "Rectangle tool (R)", exact: true }).hover();
    await Page.getByRole("tooltip").waitFor({ state: "visible" });
    await Page.screenshot({ path: `${ImageDirectory}/PatternColourSources.png` });
    const Tooltip = await Page.getByRole("tooltip").evaluate(Element => ({ Caption: Element.innerText, Placement: Element.dataset.placement, Background: getComputedStyle(Element).backgroundColor, Radius: getComputedStyle(Element).borderRadius }));
    await Page.keyboard.press("Escape");
    await Page.getByRole("button", { name: "Add pattern layer", exact: true }).click();
    const Menu = Page.getByRole("group", { name: "Add a pattern motif", exact: true });
    await Menu.waitFor({ state: "visible" });
    const AdditionTargets = await Menu.getByRole("button").evaluateAll(Elements => Elements.map(Element =>
    {
        const Bounds = Element.getBoundingClientRect();
        return { Caption: Element.textContent.trim(), HitVisible: Element.contains(document.elementFromPoint(Bounds.left + Bounds.width / 2, Bounds.top + Bounds.height / 2)) };
    }));
    if (AdditionTargets.some(Target => !Target.HitVisible)) throw new Error("A layer addition is clipped or covered.");
    await Page.mouse.move(1100, 800);
    await Page.screenshot({ path: `${ImageDirectory}/PatternLayerAdditions.png` });
    await Page.keyboard.press("Escape");
    await Page.getByRole("list", { name: "Pattern layers", exact: true }).getByRole("button", { name: "Gradient foundation", exact: true }).click();
    const EncodedImage = await Page.evaluate(() =>
    {
        const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 256;
        const Context = Canvas.getContext("2d"); Context.fillStyle = "#254b56"; Context.fillRect(0, 0, 256, 256);
        for (let Row = 0; Row < 8; Row++) for (let Column = 0; Column < 8; Column++)
        {
            Context.fillStyle = (Row + Column) % 2 ? "#74998c" : "#d5be91";
            Context.fillRect(Column * 32 + 5, Row * 32 + 5, 22, 22);
        }
        return Canvas.toDataURL().split(",")[1];
    });
    await Page.getByLabel("Motif fill image file", { exact: true }).setInputFiles({ name: "PatternColourEvidence.png", mimeType: "image/png", buffer: Buffer.from(EncodedImage, "base64") });
    await Page.getByLabel("Motif fill image fit", { exact: true }).waitFor();
    await Page.getByLabel("Motif fill source", { exact: true }).scrollIntoViewIfNeeded();
    await Page.mouse.move(1100, 800);
    await Page.screenshot({ path: `${ImageDirectory}/PatternImageSource.png` });
    const DownloadPromise = Page.waitForEvent("download");
    await Page.getByRole("button", { name: "Save document", exact: true }).click();
    const Chunks = [];
    for await (const Chunk of await (await DownloadPromise).createReadStream()) Chunks.push(Chunk);
    await writeFile(`${ResultDirectory}/PatternColourEvidence.pattern.json`, Buffer.concat(Chunks));
    await writeFile(`${ResultDirectory}/PatternHtmlCaptureChecks.json`, JSON.stringify({ Date: "2026-10-18", Surface: "Actual React/Vite HTML prototype in Chromium, not native C++ or Windows execution", Tooltip, AdditionTargets, BrowserFailures, Captures: ["PatternColourSources.png", "PatternLayerAdditions.png", "PatternImageSource.png"] }, null, 2) + "\n");
    if (BrowserFailures.length) throw new Error(BrowserFailures.join("\n"));
    console.log(JSON.stringify({ Tooltip, AdditionTargets, BrowserFailures }, null, 2));
}
finally
{
    await Browser.close();
}
