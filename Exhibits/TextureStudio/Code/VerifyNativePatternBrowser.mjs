//============================================================================================================================================
//                                                     VERIFYNATIVEPATTERNBROWSER.MJS
//============================================================================================================================================
// 🧩 Verify browser pointer/key transport reaches native C++ pen editing without reconstructing workspace controls in HTML.

import { createRequire } from 'node:module';
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';

const Require = createRequire(process.cwd() + '/_ClaudeScratch/tmp/PatternTools/package.json');
const Puppeteer = Require('puppeteer-core');
const Chromium = Require('@sparticuz/chromium');
const Browser = await Puppeteer.launch({executablePath: '/tmp/chromium', args: Chromium.args, headless: true,
    env: {...process.env, LD_LIBRARY_PATH: process.cwd() + '/_ClaudeScratch/tmp/NativeBrowser/lib'}});
try
{
    const Page = await Browser.newPage();
    await Page.setViewport({width: 1440, height: 1020, deviceScaleFactor: 1});
    const Failures = [];
    Page.on('pageerror', Failure => Failures.push(Failure.message));
    let Latest = null;
    Page.on('response', async Response =>
    {
        if (Response.url().endsWith('/input') && Response.ok()) Latest = await Response.json();
    });
    await Page.goto(process.argv[2] || 'http://127.0.0.1:8777', {waitUntil: 'networkidle2'});
    const Idle = async () => await Page.waitForFunction(() => !Busy && !Queue.length && Serial > 0, {timeout: 60000});
    await Idle();
    const Initial = {...Latest};
    const Extent = await Page.$eval('#Pixels', Image => ({x: Image.getBoundingClientRect().x, y: Image.getBoundingClientRect().y}));
    await Page.click('#Pixels', {offset: {x: 20, y: 100}});
    await Idle();
    await Page.keyboard.press('v'); await Idle();
    await Page.mouse.click(Extent.x + 850, Extent.y + 700); await Idle();
    await Page.keyboard.press('p');
    await Idle();
    for (const [X, Y] of [[590, 455], [720, 455], [675, 580]])
    {
        await Page.mouse.move(Extent.x + X, Extent.y + Y); await Idle();
        await Page.mouse.down(); await Idle();
        await Page.mouse.up(); await Idle();
    }
    const Pending = {...Latest};
    if (Pending.revision !== Initial.revision || Pending.layers !== Initial.layers + 1)
        throw new Error('Browser pointer input did not create a native-only pen preview');
    await Page.screenshot({path: 'Exhibits/TextureStudio/Images/NativePatternStream.png', fullPage: true});
    await Page.keyboard.press('Enter'); await Idle();
    const Finished = {...Latest};
    if (Finished.revision !== Initial.revision + 1 || Finished.layers !== Initial.layers + 1)
        throw new Error('Browser Enter did not finalize one native pen history entry');
    const Html = await Page.$eval('body', Body => Body.innerText);
    if (Html.includes('Pending pen path') || await Page.$$eval('canvas', Canvases => Canvases.length) !== 0)
        throw new Error('Workspace controls were reconstructed in browser DOM instead of native pixels');
    if (Failures.length) throw new Error(Failures.join('\n'));
    const Files = ['Exhibits/TextureStudio/Code/VerifyNativePatternBrowser.mjs',
        'Exhibits/TextureStudio/NativePatternStream.html', 'Exhibits/TextureStudio/Code/ServeNativePattern.py',
        'Exhibits/TextureStudio/Code/PatternWorkspaceValidation.cpp'];
    const Hashes = {};
    for (const Name of Files) Hashes[Name] = createHash('sha256').update(await readFile(Name)).digest('hex');
    const Report = {result: 'passed', browser_executed: true, browser: await Browser.version(),
        native_initial: Initial, native_pending: Pending, native_finished: Finished, javascript_errors: Failures,
        os_window_created: false, windows_executed: false, source_sha256: Hashes};
    await writeFile('Exhibits/TextureStudio/Results/NativePatternBrowserChecks.json', JSON.stringify(Report, null, 2) + '\n');
    console.log('PASS: actual browser pointer/keyboard events changed native C++ pen geometry and history.');
}
finally
{
    await Browser.close();
}
