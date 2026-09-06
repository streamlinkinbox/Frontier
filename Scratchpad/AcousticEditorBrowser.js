'use strict';
// ============================================================================================================================================
//                                                   ACOUSTICEDITORBROWSER.JS
// ============================================================================================================================================
// 🧪 Headless-Chromium proof for Tools/AudioEditor/index.html:
//   [1] the page loads without console errors, the worklet module compiles, all three cars appear
//   [2] OfflineAudioContext render through the page's own worklet == node render of the same DSP (per-order ±0.1 dB, sample-level max |Δ|)
//   [3] the live AudioContext runs (fake audio device), reports arrive, meters move, pulls run
//   [4] screenshots of the editor for Diagnostics/
// Run:  node Scratchpad/AcousticEditorBrowser.js <puppeteerModulesDir> [chromiumExecutable]
//   sandbox: npm i @sparticuz/chromium@129 puppeteer-core@23 in a scratch dir (the Lambda build ships its own libnss)
//   desktop: point it at an installed Chrome/Edge instead; the WebAudio path is identical.
const fs = require('fs'), path = require('path'), http = require('http');
const modulesDir = process.argv[2] || '/tmp/pw/node_modules';
const puppeteer = require(path.join(modulesDir, 'puppeteer-core'));
let executablePath = process.argv[3] || null;
const repo = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(repo, 'Tools', 'AudioEditor', 'index.html'), 'utf8');
const dspText = html.slice(html.indexOf('<script id="dsp" type="text/plain">') + '<script id="dsp" type="text/plain">'.length, html.indexOf('</script>', html.indexOf('<script id="dsp"')));
const D = (() => { const module = { exports: {} }; new Function('module', 'exports', dspText)(module, module.exports); return module.exports; })();
const CAR_ORDER = [...html.matchAll(/<script type="text\/toml" data-car="([^"]+)">/g)].map(m => m[1]);
const shots = path.join(repo, 'Diagnostics');
fs.mkdirSync(shots, { recursive: true });
const RATE = 48000, SECONDS = 4.0, PULL = 'pull';
let pass = 0, fail = 0;
const check = (ok, text) => { if (ok) ++pass; else ++fail; console.log((ok ? '  PASS  ' : '  FAIL  ') + text); };

// ---- node reference render (same pull, same seed, same slice length as the worklet: 128 frames) --------------------------
function referenceRender(structure, seconds)
{
    const ig = new D.AcousticIntegrator(RATE, structure, 0x5EED1234);
    const pull = new D.DynoSequence(); pull.select(PULL, structure.vehicle.redline_rpm, structure.vehicle.idle_rpm);
    const total = Math.floor(seconds * RATE), L = new Float64Array(total), R = new Float64Array(total);
    let time = 0.0;
    for (let done = 0; done < total; done += 128)
    {
        const n = Math.min(128, total - done), dt = n / RATE;
        pull.advance(dt);
        const rec = D.scriptedRecord(pull, structure.vehicle, time);
        ig.assignDemand(rec.rpm, rec.throttle, rec.load, 0.0);
        ig.render(L.subarray(done, done + n), R.subarray(done, done + n), n);
        time += dt;
    }
    return { L, R };
}
function fft(re, im)
{
    const n = re.length;
    for (let i = 1, j = 0; i < n; ++i) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (let len = 2; len <= n; len <<= 1) { const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang); for (let i = 0; i < n; i += len) { let cr = 1, ci = 0; for (let j = 0; j < len / 2; ++j) { const ur = re[i + j], ui = im[i + j], vr = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci, vi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr; re[i + j] = ur + vr; im[i + j] = ui + vi; re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi; const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } } }
}
function orderLevels(x, start, size, rpm, N)
{
    const re = new Float64Array(size), im = new Float64Array(size); let ws = 0;
    for (let i = 0; i < size; ++i) { const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / size); re[i] = x[start + i] * w; ws += w; }
    fft(re, im);
    const f0 = rpm / 60, out = [];
    for (let o = 0.5; o <= 2 * N; o += 0.5) { const k = Math.round(o * f0 * size / RATE); let m = 0; for (let j = k - 2; j <= k + 2; ++j) m = Math.max(m, Math.hypot(re[j], im[j])); out.push(20 * Math.log10(2 * m / ws + 1e-12)); }
    return out;
}

(async () => {
    // serve the repo so the page has an http origin (file:// works in a real browser too; http keeps headless flags simple)
    const server = http.createServer((req, res) => {
        const file = path.join(repo, decodeURIComponent(req.url.split('?')[0]));
        fs.readFile(file, (err, bytes) => { if (err) { res.writeHead(404); res.end(); return; } res.writeHead(200, { 'Content-Type': file.endsWith('.html') ? 'text/html; charset=utf-8' : 'application/octet-stream' }); res.end(bytes); });
    });
    await new Promise(r => server.listen(0, '127.0.0.1', r));
    const url = 'http://127.0.0.1:' + server.address().port + '/Tools/AudioEditor/index.html';

    let launchArgs = ['--autoplay-policy=no-user-gesture-required', '--use-fake-device-for-media-stream', '--window-size=1600,1000', '--font-render-hinting=none'];
    if (!executablePath)
    {
        process.env.AWS_EXECUTION_ENV = 'AWS_Lambda_nodejs20.x';                    // makes the Lambda chromium build unpack its own libnss into /tmp/al2023
        process.env.LD_LIBRARY_PATH = '/tmp/al2023/lib' + (process.env.LD_LIBRARY_PATH ? ':' + process.env.LD_LIBRARY_PATH : '');
        const chromium = require(path.join(modulesDir, '@sparticuz', 'chromium'));
        executablePath = await chromium.executablePath();
        launchArgs = [...chromium.args, ...launchArgs];
    }
    const browser = await puppeteer.launch({ executablePath, headless: true, args: launchArgs });
    const page = await browser.newPage();
    await page.setViewport({ width: 1600, height: 1000, deviceScaleFactor: 1 });
    const errors = [];
    page.on('pageerror', e => errors.push('pageerror: ' + e.message));
    page.on('console', m => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
    await page.goto(url, { waitUntil: 'load' });
    await new Promise(r => setTimeout(r, 400));

    console.log('AudioEditor browser proof — ' + new Date().toISOString() + ' — ' + await browser.version());
    console.log('\n[0] double-click path (file:// origin, no server)');
    {
        const filePage = await browser.newPage();
        const fileErrors = [];
        filePage.on('pageerror', e => fileErrors.push(e.message)); filePage.on('console', m => { if (m.type() === 'error') fileErrors.push(m.text()); });
        await filePage.goto('file://' + path.join(repo, 'Tools', 'AudioEditor', 'index.html'), { waitUntil: 'load' });
        await new Promise(r => setTimeout(r, 300));
        const peak = await filePage.evaluate(async () => { const [L] = await window.FrontierAudioEditor.renderOffline('Porsche918Spyder', 'pull', 1.0); let p = 0; for (let i = 0; i < L.length; ++i) p = Math.max(p, Math.abs(L[i])); return p; });
        check(fileErrors.length === 0 && peak > 0.05, 'file:// page loads and its worklet renders (peak ' + peak.toFixed(3) + ', errors ' + fileErrors.length + ')' + (fileErrors.length ? ' → ' + fileErrors[0] : ''));
        await filePage.close();
    }

    console.log('\n[1] page + worklet');
    const cars = await page.evaluate(() => Object.keys(window.FrontierAudioEditor.cars));
    check(errors.length === 0, 'no console / page errors on load' + (errors.length ? ' → ' + errors.join(' | ') : ''));
    check(cars.length === 3 && cars.join() === 'Porsche918Spyder,FerrariLaFerrari,NissanGtrNismo', 'three vehicles embedded: ' + cars.join(', '));
    const pickCount = await page.$$eval('#carPick button', b => b.length);
    check(pickCount === 3, 'car picker has 3 entries');
    const sliderCount = await page.$$eval('#inspectorBody input[type=range]', b => b.length);
    check(sliderCount > 60, 'inspector built ' + sliderCount + ' sliders from the schema');
    await page.screenshot({ path: path.join(shots, 'AudioEditor_01_Gate_StartAudio.png') });

    console.log('\n[2] worklet ⇄ node identity (OfflineAudioContext, ' + SECONDS + ' s of the "' + PULL + '" pull)');
    for (const key of CAR_ORDER)
    {
        const t0 = Date.now();
        const rendered = await page.evaluate(async (k, sec) => { const [L, R] = await window.FrontierAudioEditor.renderOffline(k, 'pull', sec); return [Array.from(L), Array.from(R)]; }, key, SECONDS);
        const ms = Date.now() - t0;
        const structure = await page.evaluate((k) => window.FrontierAudioEditor.cars[k].current, key);
        const ref = referenceRender(D.structureFromToml(structure), SECONDS);
        const n = ref.L.length;
        check(rendered[0].length === n, key + ': worklet rendered ' + rendered[0].length + ' frames (' + ms + ' ms)');
        let maxDiff = 0, peak = 0, firstDiff = -1;
        for (let i = 0; i < n; ++i) { const d = Math.abs(rendered[0][i] - ref.L[i]); if (d > maxDiff) maxDiff = d; if (d > 1e-6 && firstDiff < 0) firstDiff = i; peak = Math.max(peak, Math.abs(ref.L[i])); }
        check(maxDiff < 2e-6, key + ': sample-level max |worklet − node| = ' + maxDiff.toExponential(2) + ' (float32 output rounding only; peak ' + peak.toFixed(3) + ')' + (firstDiff >= 0 ? ' first Δ > 1e-6 at sample ' + firstDiff : ''));
        // per-order comparison over the last 1.36 s (the pull is at ≈ 4.4–5.7 k rpm there)
        const N = structure.vehicle.cylinder_count, size = 65536;
        const pull = new D.DynoSequence(); pull.select(PULL, structure.vehicle.redline_rpm, structure.vehicle.idle_rpm); pull.sample(SECONDS - size / RATE / 2);
        const a = orderLevels(Float64Array.from(rendered[0]), n - size, size, pull.rpm, N), b = orderLevels(ref.L, n - size, size, pull.rpm, N);
        let worst = 0; for (let i = 0; i < a.length; ++i) if (b[i] > -70) worst = Math.max(worst, Math.abs(a[i] - b[i]));
        check(worst < 0.1, key + ': order diagram agreement ±' + worst.toFixed(4) + ' dB over orders ½ … ' + 2 * N + ' (limit 0.1 dB)');
    }

    console.log('\n[3] live audio (fake device)');
    // the user's preview showed every panel black: CanvasRenderingContext2D.roundRect threw inside the frame loop (older engines).
    // Prove the loop survives without roundRect and that all four panels carry pixels at the small preview viewport.
    {
        const small = await browser.newPage();
        await small.setViewport({ width: 790, height: 600, deviceScaleFactor: 1 });
        await small.evaluateOnNewDocument(() => { delete CanvasRenderingContext2D.prototype.roundRect; });
        const smallErrors = [];
        small.on('pageerror', e => smallErrors.push(e.message));
        await small.goto(url, { waitUntil: 'load' });
        await small.evaluate(async () => { await window.FrontierAudioEditor.startAudio(); document.getElementById('gate').dataset.on = 'false'; document.getElementById('pureToggle').click(); document.querySelector('#listenerSeg button[data-listener="cockpit"]').click(); });
        await new Promise(r => setTimeout(r, 1500));
        const lit = await small.evaluate(() => { const out = {}; for (const id of ['chain', 'scope', 'spectrum', 'orders', 'gauge']) { const c = document.getElementById(id), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let lit = 0; for (let i = 0; i < d.length; i += 16) if (d[i] + d[i + 1] + d[i + 2] > 40) ++lit; out[id] = lit; } out.rpm = document.getElementById('rRpm').textContent; return out; });
        check(smallErrors.length === 0, 'frame loop survives without CanvasRenderingContext2D.roundRect (errors ' + smallErrors.length + ')' + (smallErrors.length ? ' → ' + smallErrors[0] : ''));
        check(lit.chain > 200 && lit.scope > 200 && lit.spectrum > 200 && lit.orders > 200 && lit.gauge > 200 && /^\d+$/.test(lit.rpm), 'all panels drawn at 790×600 · Pure · Cockpit: ' + JSON.stringify(lit));
        await small.screenshot({ path: path.join(shots, 'AudioEditor_06_Preview_790x600_Pure_Cockpit.png') });
        await small.close();
    }
    await page.click('#btnStart');
    await new Promise(r => setTimeout(r, 1500));
    const st1 = await page.evaluate(() => ({ state: window.FrontierAudioEditor.editor.ctx && window.FrontierAudioEditor.editor.ctx.state, reports: window.FrontierAudioEditor.editor.reports, rate: window.FrontierAudioEditor.editor.ctx && window.FrontierAudioEditor.editor.ctx.sampleRate }));
    check(st1.state === 'running', 'AudioContext running at ' + st1.rate + ' Hz');
    check(st1.reports > 10, 'worklet reports arriving: ' + st1.reports + ' in 1.5 s');
    const idle = await page.evaluate(() => window.FrontierAudioEditor.editor.last && window.FrontierAudioEditor.editor.last.rpm);
    check(idle > 800 && idle < 1100, 'free-rev idle settled at ' + Math.round(idle) + ' rpm (918)');
    await page.screenshot({ path: path.join(shots, 'AudioEditor_02_918_Idle_FreeRev.png') });
    // hold space (throttle) for 1 s → rpm must rise fast (light flywheel)
    // sample the mechanical bus on the way up: at the limiter the fuel cut silences the hybrid motor (load 0) and the
    // rpm × 2.8 gear whine has faded past the 0.4·fs guard — both by design, so the peak over the climb is what counts
    await page.keyboard.down('Space');
    let mechPeak = 0;
    for (let i = 0; i < 10; ++i) { await new Promise(r => setTimeout(r, 100)); mechPeak = Math.max(mechPeak, await page.evaluate(() => window.FrontierAudioEditor.editor.last.meters[5])); }
    const revved = await page.evaluate(() => window.FrontierAudioEditor.editor.last.rpm);
    await page.screenshot({ path: path.join(shots, 'AudioEditor_03_918_SpaceHeld_WOT.png') });
    await page.keyboard.up('Space');
    check(revved > 6000, 'Space held 1 s → ' + Math.round(revved) + ' rpm (free-rev inertia model)');
    check(mechPeak > 0, '918 gear whine + hybrid motor on the mechanical bus during the climb: peak ' + (20 * Math.log10(mechPeak + 1e-9)).toFixed(0) + ' dB');
    await new Promise(r => setTimeout(r, 800));
    const pops = await page.evaluate(() => window.FrontierAudioEditor.editor.last.pops);
    check(pops > 0, 'lift-off produced overrun pops: ' + pops);
    // scripted pull on the LaFerrari, screenshot mid-pull
    await page.evaluate(() => window.FrontierAudioEditor.selectCar('FerrariLaFerrari'));
    await page.evaluate(() => window.FrontierAudioEditor.runPull('pull'));
    await new Promise(r => setTimeout(r, 5500));
    const mid = await page.evaluate(() => ({ rpm: window.FrontierAudioEditor.editor.last.rpm, pull: window.FrontierAudioEditor.editor.last.pull, load: window.FrontierAudioEditor.editor.last.load_pct, over: window.FrontierAudioEditor.editor.last.overruns, dropped: window.FrontierAudioEditor.editor.last.dropped, meters: Array.from(window.FrontierAudioEditor.editor.last.meters) }));
    check(mid.pull === 'pull' && mid.rpm > 5000, 'LaFerrari scripted pull running: ' + Math.round(mid.rpm) + ' rpm at t ≈ 5.5 s');
    check(mid.dropped === 0, 'no pulses dropped (' + mid.dropped + ')');
    // rev 2 carries the intake on the howl layer (meter 4); the rev-3 crank-degree voice carries it on the intake voice (meter 11)
    const lafRev3 = await page.evaluate(() => { const E = window.FrontierAudioEditor, s = E.cars[E.carKey].current; return !!(s.voice.crank_pulse && s.intake && s.intake.level > 0); });
    const intakeMeter = lafRev3 ? 11 : 4;
    check(mid.meters[0] > 0 && mid.meters[2] > 0 && mid.meters[intakeMeter] > 0 && mid.meters[8] > 0, 'voice / exhaust bus / intake (' + (lafRev3 ? 'rev 3 intake voice, meter 11' : 'rev 2 howl, meter 4') + ') / output meters live: ' + mid.meters.slice(0, 12).map(v => (20 * Math.log10(v + 1e-9)).toFixed(0)).join(' ') + ' dB');
    console.log('  worklet load ' + mid.load.toFixed(1) + ' % (headless software Chromium — the user\'s desktop will be far lower), overruns ' + mid.over);
    await page.screenshot({ path: path.join(shots, 'AudioEditor_04_LaFerrari_ScriptedPull.png') });
    // GT-R: turbo layer shows boost during a pull
    await page.evaluate(() => window.FrontierAudioEditor.selectCar('NissanGtrNismo'));
    await page.evaluate(() => window.FrontierAudioEditor.runPull('pull'));
    await new Promise(r => setTimeout(r, 4500));
    const gtr = await page.evaluate(() => ({ boost: window.FrontierAudioEditor.editor.last.boost, shaft: window.FrontierAudioEditor.editor.last.shaft, rpm: window.FrontierAudioEditor.editor.last.rpm, turbo: window.FrontierAudioEditor.editor.last.meters[7] }));
    check(gtr.boost > 0.5 && gtr.turbo > 0, 'GT-R boost under load: ' + gtr.boost.toFixed(2) + ' bar, spool ' + (gtr.shaft * 100).toFixed(0) + ' % at ' + Math.round(gtr.rpm) + ' rpm, turbo meter ' + (20 * Math.log10(gtr.turbo + 1e-9)).toFixed(0) + ' dB');
    await page.screenshot({ path: path.join(shots, 'AudioEditor_05_GTR_ScriptedPull_Boost.png') });
    // inspector edit propagates: change bank_pan via the schema path and confirm the worklet accepted a structure message (no errors)
    await page.evaluate(() => { const c = window.FrontierAudioEditor.cars.NissanGtrNismo.current; c.exhaust.bank_pan = 0.9; window.FrontierAudioEditor.editor.worklet.port.postMessage({ type: 'structure', structure: c }); });
    await new Promise(r => setTimeout(r, 600));
    const after = await page.evaluate(() => window.FrontierAudioEditor.editor.last.rpm);
    check(after > 0 && errors.length === 0, 'live structure edit accepted mid-pull (rpm ' + Math.round(after) + ', errors ' + errors.length + ')');
    // TOML export text round-trips through the parser to the same structure
    const roundTrip = await page.evaluate(() => { const E = window.FrontierAudioEditor; const s = E.cars.Porsche918Spyder.current; const text = E.Dsp.serialiseToml(s, []); const back = E.Dsp.structureFromToml(E.Dsp.parseToml(text)); return JSON.stringify(back) === JSON.stringify(s); });
    check(roundTrip, 'serialiseToml → parseToml round trip is lossless (918)');
    check(errors.length === 0, 'no console / page errors during the live session' + (errors.length ? ' → ' + errors.slice(0, 3).join(' | ') : ''));

    await browser.close(); server.close();
    console.log('\n' + (fail === 0 ? 'ALL PASS' : fail + ' FAIL') + ' — ' + pass + ' pass, ' + fail + ' fail');
    process.exit(fail === 0 ? 0 : 1);
})().catch(e => { console.error('ERROR ' + (e.stack || e.message)); process.exit(2); });
