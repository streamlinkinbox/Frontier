/*==========================================================================================================================================
                                                          PROTOTYPECAPTURES.MJS
==========================================================================================================================================*/
// 🧩 Rendered evidence for the Force Field and Particle editor prototypes, plus runtime assertions, from headless Chromium.
//
//     node Exhibits/ForceFieldEditor/PrototypeCaptures.mjs <directory holding node_modules with @sparticuz/chromium and puppeteer-core>
//
// 📝 Serves the repository itself on a free local port (WebGPU needs a secure context: 127.0.0.1 is one), opens each page in
//    headless Chromium with SwiftShader Vulkan for WebGPU and CPU raster for 2D canvas, drives the UI through the DOM, and
//    writes JPEG captures to Images/ and the measured facts to CaptureReport.json. Each capture first calls the page's
//    capture hold (render one frame, stop the loop) because a software-rendered page animating at full rate starves capture.
import http from 'node:http';
import { readFileSync, writeFileSync, mkdirSync, statSync, existsSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, join, extname, normalize, resolve } from 'node:path';
import { createRequire } from 'node:module';

const Here = dirname(fileURLToPath(import.meta.url));
const Root = join(Here, '..', '..');
const Images = join(Here, 'Images');
mkdirSync(Images, { recursive: true });
const Packages = resolve(process.argv[2] || join(Root, '_ClaudeScratch', 'browser'));
const RequireFrom = createRequire(join(Packages, 'package.json'));
const Chromium = (await import(pathToFileURL(RequireFrom.resolve('@sparticuz/chromium')).href)).default;
const Puppeteer = (await import(pathToFileURL(RequireFrom.resolve('puppeteer-core')).href)).default;

//------------------------------------------------------------------------------------------------------------------------
//                                                     STATIC SERVER
//------------------------------------------------------------------------------------------------------------------------

const Types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.woff2': 'font/woff2', '.ttf': 'font/ttf',
                '.jpg': 'image/jpeg', '.png': 'image/png', '.svg': 'image/svg+xml', '.json': 'application/json' };
const Server = http.createServer((Request, Response) =>
{
    const Relative = decodeURIComponent(new URL(Request.url, 'http://local').pathname);
    let Path = normalize(join(Root, Relative));
    if (!Path.startsWith(Root)) { Response.writeHead(403); Response.end(); return; }
    if (existsSync(Path) && statSync(Path).isDirectory()) Path = join(Path, 'index.html');
    if (!existsSync(Path)) { Response.writeHead(404); Response.end(); return; }
    Response.writeHead(200, { 'Content-Type': Types[extname(Path)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
    Response.end(readFileSync(Path));
});
await new Promise(Resolve => Server.listen(0, '127.0.0.1', Resolve));
const Origin = `http://127.0.0.1:${Server.address().port}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                       BROWSER
//------------------------------------------------------------------------------------------------------------------------

const Dropped = ['--enable-features', '--headless', '--single-process', '--in-process-gpu', '--no-zygote', '--use-gl', '--use-angle'];
const Browser = await Puppeteer.launch({
    executablePath: await Chromium.executablePath(), headless: 'shell', protocolTimeout: 240000,
    args: [...Chromium.args.filter(Argument => !Dropped.some(Prefix => Argument.startsWith(Prefix))),
           '--enable-features=SharedArrayBuffer,Vulkan,WebGPU,SkiaGraphite', '--enable-unsafe-webgpu', '--use-vulkan=swiftshader',
           '--use-webgpu-adapter=swiftshader', '--use-angle=vulkan', '--disable-accelerated-2d-canvas'],
    env: { ...process.env, VK_ICD_FILENAMES: process.env.VK_ICD_FILENAMES || '/tmp/vk_swiftshader_icd.json' },
    defaultViewport: { width: 1720, height: 1000, deviceScaleFactor: 1 }
});
const Report = { Revision: process.env.CAPTURE_REVISION || 'working tree', Platform: `${process.platform} ${process.arch}`, Pages: {}, Assertions: [] };
const Pause = Milliseconds => new Promise(Resolve => setTimeout(Resolve, Milliseconds));
function Assert(Condition, Claim, Detail)
{
    Report.Assertions.push({ Claim, Passed: !!Condition, Detail });
    console.log(`  ${Condition ? 'PASS' : 'FAIL'}  ${Claim}${Detail !== undefined ? ' · ' + JSON.stringify(Detail) : ''}`);
}

async function OpenPage(Relative, Wait)
{
    const Page = await Browser.newPage();
    const Errors = [];
    Page.on('pageerror', Trouble => Errors.push(Trouble.message));
    Page.on('console', Message => { if (Message.type() === 'error' && !/404|Failed to load resource/.test(Message.text())) Errors.push(Message.text()); });
    await Page.goto(Origin + Relative, { waitUntil: 'load', timeout: 60000 });
    await Pause(Wait);
    return { Page, Errors };
}

async function Capture(Page, Hold, Name)
{
    if (Hold) await Page.evaluate(`${Hold}(true)`);
    await Pause(350);
    await Page.screenshot({ path: join(Images, Name), type: 'jpeg', quality: 86 });
    if (Hold) await Page.evaluate(`${Hold}(false)`);
    console.log(`  shot  ${Name}`);
}

const Click = Text => `[...document.querySelectorAll('.tree-row')].find(Row => Row.querySelector('.row-name').textContent === ${JSON.stringify(Text)}).click()`;

//------------------------------------------------------------------------------------------------------------------------
//                                                   FORCE FIELD EDITOR
//------------------------------------------------------------------------------------------------------------------------

console.log('\nForce Field editor');
{
    const { Page, Errors } = await OpenPage('/Experimental/ForceFieldEditor/index.html', 2500);
    await Page.evaluate("localStorage.clear()");
    await Pause(2500);
    await Capture(Page, 'ForceFieldEditorHold', 'ForceFieldMap6km.jpg');
    const Facts = await Page.evaluate(() => ({ Sources: ForceFieldEditorScene.Sources.length, Samples: ForceFieldEditorView.Statistics.Samples,
                                                Milliseconds: ForceFieldEditorView.Statistics.Milliseconds, Rows: document.querySelectorAll('.tree-row').length }));
    Assert(Facts.Sources === 14 && Facts.Rows === 16, 'the outliner shows the Force Field, its Wind guest and 14 localized forces', Facts);
    await Page.evaluate(Click('Wind'));
    await Pause(2500);
    await Capture(Page, 'ForceFieldEditorHold', 'ForceFieldWindPage.jpg');
    await Page.evaluate("document.getElementById('InspectorScroll').scrollTop = 1250");
    await Pause(1200);
    await Capture(Page, 'ForceFieldEditorHold', 'ForceFieldWindAltitude.jpg');
    await Page.evaluate(`${Click('Tornado')}; ForceFieldEditorScene.Visualization.Mode = 'Oblique'; InspectorControls.Broadcast();
                         [...document.querySelectorAll('.pill-button')].find(Button => Button.textContent.includes('Frame in view')).click()`);
    await Pause(2500);
    await Capture(Page, 'ForceFieldEditorHold', 'ForceFieldTornadoOblique.jpg');
    await Page.evaluate(`ForceFieldEditorScene.Visualization.Mode = 'Map'; ForceFieldEditorScene.Visualization.Cascades = true; ${Click('Force Field')};
                         Object.assign(ForceFieldEditorView.View, { CentreX: 0, CentreZ: 0, Span: 100000 }); InspectorControls.Broadcast()`);
    await Pause(2500);
    await Capture(Page, 'ForceFieldEditorHold', 'ForceFieldMap100km.jpg');
    const Wide = await Page.evaluate(() => ({ CellSpan: ForceFieldEditorView.Statistics.CellSpan, Span: ForceFieldEditorView.View.Span }));
    Assert(Wide.CellSpan >= 256 && Wide.CellSpan <= 1024, 'at 100 km the lattice samples at a few hundred metres (level of detail), not metres', Wide);
    await Page.evaluate(`ForceFieldEditorScene.Visualization.Cascades = false;
                         [...document.querySelectorAll('.pill-tab')].find(Button => Button.textContent.startsWith('Accelerate')).click();
                         Object.assign(ForceFieldEditorView.View, { CentreX: 0, CentreZ: 100, Span: 2400 }); InspectorControls.Broadcast()`);
    await Pause(2500);
    await Capture(Page, 'ForceFieldEditorHold', 'ForceFieldAccelerate.jpg');
    await Page.evaluate(`[...document.querySelectorAll('.pill-tab')].find(Button => Button.textContent.startsWith('Flow')).click(); document.querySelector('.add-row').click()`);
    await Pause(800);
    await Capture(Page, 'ForceFieldEditorHold', 'ForceFieldLibrary.jpg');
    await Page.evaluate(`[...document.querySelectorAll('.library-card')].find(Card => Card.querySelector('.card-name').textContent === 'Microburst').click()`);
    await Pause(2500);
    await Page.evaluate("document.getElementById('InspectorScroll').scrollTop = 1500");
    await Pause(1000);
    await Capture(Page, 'ForceFieldEditorHold', 'ForceFieldSourceSections.jpg');
    const Added = await Page.evaluate(() => ({ Sources: ForceFieldEditorScene.Sources.length, Log: [...document.querySelectorAll('.log-row .message b')].map(Row => Row.textContent).slice(0, 3) }));
    Assert(Added.Sources === 15 && Added.Log.some(Line => Line.startsWith('Added Microburst')), 'a library card adds a force at the view centre and logs it', Added);
    Assert(Errors.length === 0, 'no page errors on the Force Field editor', Errors);
    Report.Pages.ForceFieldEditor = { Facts, Wide, Added, Errors };
    await Page.close();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTICLE EDITOR
//------------------------------------------------------------------------------------------------------------------------

console.log('\nParticle editor');
{
    const { Page, Errors } = await OpenPage('/Experimental/ParticleEditor/index.html', 7000);
    await Capture(Page, 'ParticleEditorHold', 'ParticleFieldOverlay.jpg');
    const Boot = await Page.evaluate(() => ({ Adapter: ParticleEditorScene.AdapterName, Effects: ParticleEditorScene.Effects.map(Effect => [Effect.Name, Effect.Gpu.stats_ ? Effect.Gpu.stats_.alive : null]),
                                               Readbacks: ParticleEditorScene.Readback.Status.Count }));
    Assert(Boot.Effects.length === 4 && Boot.Effects.every(([, Alive]) => Alive > 0), 'the eos default scene runs on WebGPU: four effects with live particles', Boot);
    Assert(Boot.Readbacks > 0, 'the overlay has read the GPU wind lattice back', Boot.Readbacks);
    // 📝 Pause the clock so the lattice stops changing, let both readbacks refresh, then compare the engine's own probe texel with the
    //    overlay's copy of the same voxel: both are the GPU texture, so they must agree exactly.
    await Page.evaluate("ParticleEditorScene.Playing = false");
    await Pause(4000);
    const Agreement = await Page.evaluate(() =>
    {
        const Scene = ParticleEditorScene, Result = Scene.Engine.probeResult, [Columns, Rows] = [24, 12];
        const [X, Y, Z] = Result.at, Index = ((Z * Rows + Y) * Columns + X) * 3;
        const Overlay = Array.from(Scene.Readback.Vectors.slice(Index, Index + 3));
        return { Voxel: Result.at, Probe: Result.value, Overlay, Difference: Math.max(...Overlay.map((Reading, Axis) => Math.abs(Reading - Result.value[Axis]))) };
    });
    Assert(Agreement.Difference === 0, 'the overlay reads exactly the texel the engine probe reads (same GPU lattice)', Agreement);
    await Page.evaluate("ParticleEditorScene.Playing = true");
    await Page.evaluate(Click('Leaves'));
    await Pause(3000);
    await Capture(Page, 'ParticleEditorHold', 'ParticleEffectPage.jpg');
    await Page.evaluate("document.getElementById('InspectorScroll').scrollTop = 900");
    await Pause(1200);
    await Capture(Page, 'ParticleEditorHold', 'ParticleEffectSections.jpg');
    await Page.evaluate("document.querySelector('.filter-pill').click()");
    await Pause(1200);
    const Library = await Page.evaluate(() => ({ Cards: document.querySelectorAll('.library-card').length, Tabs: document.querySelectorAll('.library-tab').length }));
    Assert(Library.Cards === 52 && Library.Tabs === 15, 'the library offers the same 52 presets in 14 groups (plus All)', Library);
    await Capture(Page, 'ParticleEditorHold', 'ParticleLibrary.jpg');
    await Page.evaluate(`[...document.querySelectorAll('.library-card')].find(Card => Card.querySelector('.card-name').textContent === 'Black hole').click()`);
    await Pause(400);
    await Page.evaluate(`${Click('Force Field')}`);
    await Pause(300);
    await Page.evaluate(`[...document.querySelectorAll('.pill-tab')].find(Button => Button.textContent.startsWith('Accelerate')).click(); ${Click('Black hole')}`);
    await Pause(4000);
    await Capture(Page, 'ParticleEditorHold', 'ParticleBlackHoleAccelerate.jpg');
    await Page.evaluate(`ParticleEditorScene.Overlay.Contribution = 'Flow'; ${Click('Wind')};
                         [...document.querySelectorAll('.bar-choice button')].find(Button => Button.textContent === 'Top').click()`);
    await Pause(3500);
    await Capture(Page, 'ParticleEditorHold', 'ParticleTopViewWind.jpg');
    const Final = await Page.evaluate(() => ({ Effects: ParticleEditorScene.Effects.length, Packed: ParticleEditorScene.PackedFields[0], Lost: !document.getElementById('BootError').hidden }));
    Assert(Final.Effects === 5 && Final.Packed >= 1 && !Final.Lost, 'a library preset joins the scene; its attractor reaches fieldAccel; the device stays alive', Final);
    Assert(Errors.length === 0, 'no page errors on the Particle editor', Errors);
    Report.Pages.ParticleEditor = { Boot, Agreement, Library, Final, Errors };
    await Page.close();
}

//------------------------------------------------------------------------------------------------------------------------
//                                              RENDER SETTINGS (REGRESSION)
//------------------------------------------------------------------------------------------------------------------------

console.log('\nRender settings inspector (shared controls regression)');
{
    const { Page, Errors } = await OpenPage('/Experimental/RenderSettingsInspector/index.html', 2500);
    await Capture(Page, null, 'RenderSettingsRegression.jpg');
    const Facts = await Page.evaluate(() => ({ Sections: document.querySelectorAll('.section-card').length, Sliders: document.querySelectorAll('.track').length }));
    Assert(Facts.Sections >= 10 && Facts.Sliders > 20 && Errors.length === 0, 'Render settings still builds every section with the extended InspectorControls', { ...Facts, Errors });
    Report.Pages.RenderSettings = { Facts, Errors };
    await Page.close();
}

writeFileSync(join(Here, 'CaptureReport.json'), JSON.stringify(Report, null, 2) + '\n');
const Failed = Report.Assertions.filter(One => !One.Passed).length;
console.log(`\n${Failed ? 'FAIL' : 'PASS'} ${Report.Assertions.length - Failed} of ${Report.Assertions.length}`);
Browser.process()?.kill('SIGKILL');
Server.close();
process.exit(Failed ? 1 : 0);
