/*==========================================================================================================================================
                                                       HTMLNAVIGATIONEVIDENCE.MJS
==========================================================================================================================================*/
// 🧩 Capture the running HTML layer panels and real WebGL material viewport without generated artwork.

import { chromium } from "../../../Experimental/TextureStudio/node_modules/playwright/index.mjs";
import { mkdir, writeFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { fileURLToPath } from "node:url";

const Browser = await chromium.launch({
    executablePath: process.env.PLAYWRIGHT_EXECUTABLE_PATH,
    headless: true,
    args: ["--no-sandbox", "--disable-dev-shm-usage", "--use-gl=angle", "--use-angle=swiftshader",
        "--enable-unsafe-swiftshader", "--in-process-gpu", "--ignore-gpu-blocklist", "--no-zygote"],
});
try
{
    const Page = await Browser.newPage({ viewport: { width: 1440, height: 1000 } });
    const Errors = [];
    Page.on("pageerror", (Error) => Errors.push(Error.message));
    await Page.goto(`${process.env.STUDIO_EVIDENCE_URL || "http://127.0.0.1:5173"}/?studio=pattern`, { waitUntil: "networkidle" });
    await Page.getByRole("complementary", { name: "Pattern layers", exact: true }).waitFor();
    const Images = new URL("../Images/", import.meta.url);
    const Results = new URL("../Results/", import.meta.url);
    await mkdir(Images, { recursive: true });
    await mkdir(Results, { recursive: true });
    await Page.screenshot({ path: fileURLToPath(new URL("HtmlPatternLayers.png", Images)) });
    await Page.getByRole("button", { name: "3D material", exact: true }).click();
    await Page.locator('.pe-material-preview canvas[data-material-ready="true"]').waitFor({ timeout: 120000 });
    const Canvas = Page.locator(".pe-material-preview canvas");
    const BeforePose = await Canvas.getAttribute("data-camera-pose");
    const BeforePixels = createHash("sha256").update(await Canvas.screenshot()).digest("hex");
    const Bounds = await Canvas.boundingBox();
    await Page.mouse.move(Bounds.x + Bounds.width * 0.5, Bounds.y + Bounds.height * 0.5);
    await Page.mouse.down({ button: "middle" });
    await Page.mouse.move(Bounds.x + Bounds.width * 0.62, Bounds.y + Bounds.height * 0.57, { steps: 8 });
    await Page.mouse.up({ button: "middle" });
    await Page.waitForFunction((Previous) => document.querySelector(".pe-material-preview canvas").dataset.cameraPose !== Previous, BeforePose);
    const AfterPixels = createHash("sha256").update(await Canvas.screenshot()).digest("hex");
    if (BeforePixels === AfterPixels) throw new Error("Orbit did not change rendered canvas pixels.");
    await Page.screenshot({ path: fileURLToPath(new URL("HtmlPatternMaterial.png", Images)) });
    const Readout = await Page.locator(".pe-material-preview canvas").evaluate((Canvas) => ({
        CameraLagSeconds: Number(Canvas.dataset.cameraLag),
        CameraPosition: Canvas.dataset.cameraPose,
        PreviewShape: Canvas.dataset.previewShape,
        LayerRows: document.querySelectorAll(".pe-layers-dock .se-layer-row").length,
    }));
    if (Errors.length) throw new Error(Errors.join("\n"));
    await writeFile(new URL("HtmlNavigationEvidence.json", Results), JSON.stringify({
        Result: "passed", Capture: "Real HTML DOM and software-WebGL rendering; not native Windows evidence",
        Viewport: { Width: 1440, Height: 1000 }, BrowserVersion: Browser.version(), Readout, PageErrors: Errors,
        OrbitChangesPixels: true, BeforePixels, AfterPixels,
        Images: ["HtmlPatternLayers.png", "HtmlPatternMaterial.png"],
    }, null, 2) + "\n");
    console.log("PASS: real HTML layer-panel and WebGL material captures; no page errors.");
}
finally
{
    await Browser.close();
}
