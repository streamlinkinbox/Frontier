import { createRequire } from 'node:module';
import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const PackageRoot = process.env.BROWSER_PACKAGES_DIR || '/tmp/frontier-browser';
const Require = createRequire(path.join(PackageRoot, 'package.json'));
const Chromium = Require('@sparticuz/chromium');
const Puppeteer = Require('puppeteer-core');
const Target = process.env.BROWSER_TARGET || 'fluid';
const Root = process.env.FRONTIER_URL || 'http://127.0.0.1:8000';
const Directory = fileURLToPath(new URL('../Images/', import.meta.url));
const Routes = {
    fluid: '/Experimental/FluidEditor/FluidNodeEditor.html',
    particle: '/Experimental/ParticleEditor/index.html',
    vehicle: '/Experimental/VehicleHmiPrototype/index.html?quality=low&scale=.25&stresspool=0',
    gas: '/Experimental/VolumetricFlow/'
};
assert.ok(Routes[Target], `Unknown target ${Target}`);
const Browser = await Puppeteer.launch({
    executablePath: await Chromium.executablePath(),
    headless: 'shell',
    env: { ...process.env, LD_LIBRARY_PATH: '/tmp/al2023/lib:/tmp', VK_ICD_FILENAMES: '/tmp/vk_swiftshader_icd.json' },
    args: [...Chromium.args.filter(Arg => !['--single-process', '--in-process-gpu'].includes(Arg)),
        '--enable-unsafe-webgpu', '--use-webgpu-adapter=swiftshader', '--enable-features=Vulkan',
        '--use-vulkan=swiftshader', '--disable-vulkan-surface', '--enable-unsafe-swiftshader']
});
try {
    const Page = await Browser.newPage();
    await Page.setViewport({ width: 1280, height: 800, deviceScaleFactor: 1 });
    const Errors = [];
    Page.on('pageerror', Error => Errors.push(`page: ${Error.message}`));
    Page.on('console', Message => { if (Message.type() === 'error') Errors.push(`console: ${Message.text()}`); });
    if (Target === 'gas') {
        await Page.evaluateOnNewDocument(() => {
            const Original = window.requestAnimationFrame.bind(window);
            let Remaining = 2;
            window.requestAnimationFrame = Callback => Remaining-- > 0 ? Original(Callback) : 0;
        });
    }
    const Response = await Page.goto(Root + Routes[Target], { waitUntil: 'domcontentloaded', timeout: 60000 });
    assert.equal(Response.status(), 200);
    if (Target === 'fluid') {
        await Page.waitForFunction(() => document.querySelector('#ReadoutGrid')?.textContent.includes('64'), { timeout: 120000 });
        await Page.click('#ViewportSettingsAction');
        await Page.select('#ResolutionSelect', '32');
        await Page.waitForFunction(() => document.querySelector('#ReadoutGrid')?.textContent.includes('32'), { timeout: 120000 });
        await Page.select('#InspectSelect', 'pressure');
        assert.equal(await Page.$eval('#InspectSelect', Select => Select.value), 'pressure');
        await Page.click('#SimulationStep');
        await Page.waitForFunction(() => document.querySelector('#ReadoutFrame')?.textContent !== '—', { timeout: 120000 });
        await new Promise(Resolve => setTimeout(Resolve, 3000));
        await Page.click('#SimulationReset');
        await Page.select('#InspectSelect', 'surface');
        await Page.click('#ViewportSettingsAction');
        await Page.click('#SimulationStep');
        await new Promise(Resolve => setTimeout(Resolve, 5000));
        assert.equal(await Page.$eval('#AdapterNotice', Notice => Notice.classList.contains('Revealed')), false);
        console.log('Liquid readout:', await Page.$eval('#SolverReadout', E => E.innerText.replace(/\s+/g, ' ').trim()));
    } else if (Target === 'particle') {
        await Page.waitForFunction(() => window.ParticleEditorState?.frame >= 2, { timeout: 120000 });
        assert.equal(await Page.$eval('#boot-error', E => E.hidden), true);
        await Page.click('#btn-play');
        assert.equal(await Page.$eval('#st-state', E => E.textContent), 'Paused');
        await Page.click('#btn-arrows');
        assert.equal(await Page.$eval('#btn-arrows', E => E.getAttribute('aria-pressed')), 'false');
        await Page.click('#btn-arrows');
        await Page.click('#btn-play');
        assert.equal(await Page.$eval('#st-state', E => E.textContent), 'Live');
        console.log('Particle frames:', await Page.evaluate(() => window.ParticleEditorState.frame));
    } else if (Target === 'vehicle') {
        await Page.waitForFunction(() => window.IlluminationExhibit?.FrameIndex >= 1, { timeout: 180000, polling: 200 });
        console.log('Vehicle frames:', await Page.evaluate(() => window.IlluminationExhibit.FrameIndex));
    } else {
        await Page.waitForFunction(() => Number(document.querySelector('#ReadStepCounter')?.textContent) >= 1, { timeout: 180000, polling: 200 });
        console.log('Gas steps:', await Page.$eval('#ReadStepCounter', E => E.textContent));
    }
    const Destination = path.join(Directory, `${Target}.png`);
    await Page.screenshot({ path: Destination });
    console.log('Screenshot:', Destination);
    console.log('Page errors:', JSON.stringify(Errors));
    assert.equal(Errors.length, 0);
} finally {
    const Process = Browser.process();
    const Closed = await Promise.race([
        Browser.close().then(() => true),
        new Promise(Resolve => setTimeout(() => Resolve(false), 5000))
    ]);
    if (!Closed && Process) Process.kill('SIGKILL');
}
