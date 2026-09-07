'use strict';
// ============================================================================================================================================
//                                                   ACOUSTICEDITORBROWSER.JS
// ============================================================================================================================================
// 🧪 Headless-Chromium proof for Tools/AudioEditor/index.html:
//   [1] the page loads without console errors, the worklet module compiles, all five cars appear in the vehicle dropdown
//   [2] OfflineAudioContext render through the page's own worklet == node render of the same DSP (per-order ±0.1 dB, sample-level max |Δ|)
//   [3] the live AudioContext runs (fake audio device), reports arrive, meters move, pulls run
//   [4] screenshots of the editor for Diagnostics/
//   [5] row A2 · P5: a Project-Dyno --render WAV dropped on the page draws its order sheet over the live voice's (Split / Δ);
//       the two agree to ±0.1 dB per order cell; Export TOML text == AcousticStructure::Save output byte for byte
//       (the binary is built and run here when Scratchpad/Reference/FerrariLaFerrari_pull_cpp.wav is absent — Linux toolchain script)
//   [6] row A4 reference lane: a recording (here a node render of the Demon's pull — the recording feed knows nothing of the dyno
//       timeline) is dropped as a *recording*; the page builds the car's order bank in its Worker, tracks the rpm blind, plays the
//       clip along the trace, renders the synth along the trace and compares the two order sheets; the trace must follow the
//       pull's true rpm (median error < 1 %, no octave slips, coverage > 70 %), Split / Δ agreement ≤ 0.5 dB mean, ÷2 / ×2 rescale
//       the trace and re-compare, seeking moves the gauge with the clip
// Run:  node Scratchpad/AcousticEditorBrowser.js <puppeteerModulesDir> [chromiumExecutable]
//   sandbox: npm i @sparticuz/chromium@129 puppeteer-core@23 in a scratch dir (the Lambda build ships its own libnss)
//   desktop: point it at an installed Chrome/Edge instead; the WebAudio path is identical.
const fs = require('fs'), path = require('path'), http = require('http'), { execSync } = require('child_process');
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
    check(cars.length === 5 && cars.join() === 'Porsche918Spyder,FerrariLaFerrari,NissanGtrNismo,KoenigseggAgeraR,DodgeDemon', 'five vehicles embedded: ' + cars.join(', '));
    const pickCount = await page.$$eval('#carPick .opt', b => b.length);
    check(pickCount === 5, 'vehicle dropdown lists 5 entries');
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
    // dropdown (real clicks, the gate is down now): closed on load, the caret opens it, an option picks the car and closes it, an outside click closes it, keys 1 … 5 pick by position
    {
        const closed = await page.$eval('#carPick', d => !d.classList.contains('open') && getComputedStyle(d.querySelector('.dd-menu')).display === 'none');
        await page.click('#carPickBtn');
        const opened = await page.$eval('#carPick', d => d.classList.contains('open') && getComputedStyle(d.querySelector('.dd-menu')).display === 'flex');
        await page.click('#carPick .opt[data-car="DodgeDemon"]');
        const picked = await page.evaluate(() => ({ key: window.FrontierAudioEditor.carKey, open: document.getElementById('carPick').classList.contains('open'), cur: document.getElementById('carPickCur').textContent, sel: document.querySelector('#carPick .opt.sel').dataset.car, title: document.title }));
        check(closed && opened && !picked.open && picked.key === 'DodgeDemon' && picked.sel === 'DodgeDemon' && picked.cur.indexOf('SRT Demon') >= 0, 'dropdown: closed on load → caret opens → option picks the Demon and closes (pill "' + picked.cur + '", title "' + picked.title + '")');
        await page.click('#carPickBtn');
        await page.mouse.click(700, 400);
        const outside = await page.$eval('#carPick', d => !d.classList.contains('open'));
        await page.keyboard.press('4');
        const key4 = await page.evaluate(() => window.FrontierAudioEditor.carKey);
        await page.keyboard.press('1');
        const key1 = await page.evaluate(() => window.FrontierAudioEditor.carKey);
        check(outside && key4 === 'KoenigseggAgeraR' && key1 === 'Porsche918Spyder', 'dropdown: outside click closes it; key 4 → ' + key4 + ', key 1 → ' + key1);
    }
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
    // Demon: the supercharger layer (meter 10) carries the rotor pulsation under load, the boost readout follows rpm · load, the chain junction reads Supercharger
    await page.evaluate(() => window.FrontierAudioEditor.selectCar('DodgeDemon'));
    await page.evaluate(() => window.FrontierAudioEditor.runPull('pull'));
    await new Promise(r => setTimeout(r, 4500));
    const demon = await page.evaluate(() => ({ boost: window.FrontierAudioEditor.editor.last.boost, rpm: window.FrontierAudioEditor.editor.last.rpm, charger: window.FrontierAudioEditor.editor.last.meters[10], turbo: window.FrontierAudioEditor.editor.last.meters[7], readout: document.getElementById('rBoost').textContent }));
    check(demon.boost > 0.3 && demon.charger > 0 && demon.turbo === 0, 'Demon supercharger under load: ' + demon.boost.toFixed(2) + ' bar at ' + Math.round(demon.rpm) + ' rpm, charger meter ' + (20 * Math.log10(demon.charger + 1e-9)).toFixed(0) + ' dB, turbo meter silent, readout "' + demon.readout + '"');
    await page.screenshot({ path: path.join(shots, 'AudioEditor_09_Demon_ScriptedPull_Supercharger.png') });
    // Agera R: loud turbos (whine 4 → 12 kHz), 1.4 bar, wastegate blow-off with the 25 Hz flutter + thump on lift (spawned transients grow past the pops)
    await page.evaluate(() => window.FrontierAudioEditor.selectCar('KoenigseggAgeraR'));
    await page.evaluate(() => window.FrontierAudioEditor.runPull('pull'));
    await new Promise(r => setTimeout(r, 6500));
    const agera = await page.evaluate(() => ({ boost: window.FrontierAudioEditor.editor.last.boost, rpm: window.FrontierAudioEditor.editor.last.rpm, turbo: window.FrontierAudioEditor.editor.last.meters[7] }));
    check(agera.boost > 0.8 && agera.turbo > 0, 'Agera R twin turbo under load: ' + agera.boost.toFixed(2) + ' bar at ' + Math.round(agera.rpm) + ' rpm, turbo meter ' + (20 * Math.log10(agera.turbo + 1e-9)).toFixed(0) + ' dB');
    await page.screenshot({ path: path.join(shots, 'AudioEditor_10_AgeraR_ScriptedPull_Turbo.png') });
    await new Promise(r => setTimeout(r, 3000));
    const lifted = await page.evaluate(() => ({ pops: window.FrontierAudioEditor.editor.last.pops, active: window.FrontierAudioEditor.editor.last.active, dropped: window.FrontierAudioEditor.editor.last.dropped }));
    check(lifted.active > lifted.pops && lifted.dropped === 0, 'Agera R lift: ' + lifted.active + ' transients spawned for ' + lifted.pops + ' pops (blow-off + wastegate thump on top), dropped ' + lifted.dropped);
    // inspector edit propagates: change bank_pan via the schema path and confirm the worklet accepted a structure message (no errors)
    await page.evaluate(() => { const c = window.FrontierAudioEditor.cars.NissanGtrNismo.current; c.exhaust.bank_pan = 0.9; window.FrontierAudioEditor.editor.worklet.port.postMessage({ type: 'structure', structure: c }); });
    await new Promise(r => setTimeout(r, 600));
    const after = await page.evaluate(() => window.FrontierAudioEditor.editor.last.rpm);
    check(after > 0 && errors.length === 0, 'live structure edit accepted mid-pull (rpm ' + Math.round(after) + ', errors ' + errors.length + ')');
    // TOML export text round-trips through the parser to the same structure
    const roundTrip = await page.evaluate(() => { const E = window.FrontierAudioEditor; const s = E.cars.Porsche918Spyder.current; const text = E.Dsp.serialiseToml(s, []); const back = E.Dsp.structureFromToml(E.Dsp.parseToml(text)); return JSON.stringify(back) === JSON.stringify(s); });
    check(roundTrip, 'serialiseToml → parseToml round trip is lossless (918)');

    console.log('\n[5] C++ hand-over: --render WAV order overlay, Export TOML ⇄ AcousticStructure::Save');
    {
        const refDir = path.join(repo, 'Scratchpad', 'Reference'); fs.mkdirSync(refDir, { recursive: true });
        const cppWav = path.join(refDir, 'FerrariLaFerrari_pull_cpp.wav'), cppToml = path.join(refDir, 'FerrariLaFerrari_save_cpp.toml');
        const binary = path.join(repo, 'Projects', 'Project-Dyno', 'Build', 'Output', 'Linux', 'Release', 'Binary', 'Project-Dyno');
        let built = fs.existsSync(cppWav) && fs.existsSync(cppToml);
        if (!built)
        {
            try
            {
                execSync('bash Projects/Project-Dyno/Build/ToolchainSequence.sh', { cwd: repo, stdio: 'pipe' });
                // --slice 128: the worklet renders in 128-frame quanta, so the demand is sampled at the same instants and the two order sheets are the same render
                execSync('"' + binary + '" --render "' + cppWav + '" --car FerrariLaFerrari --pull pull --float --slice 128', { cwd: repo, stdio: 'pipe' });
                execSync('"' + binary + '" --save "' + cppToml + '" --car FerrariLaFerrari', { cwd: repo, stdio: 'pipe' });
                built = true;
            }
            catch (e) { console.log('  (Project-Dyno build / render unavailable here: ' + (e.message || e).toString().split('\n')[0] + ')'); }
        }
        check(built, 'Project-Dyno --render / --save outputs available (' + path.relative(repo, cppWav) + ')');
        if (built)
        {
            await page.evaluate(() => window.FrontierAudioEditor.selectCar('FerrariLaFerrari'));
            await new Promise(r => setTimeout(r, 300));
            const t0 = Date.now();
            const loaded = await page.evaluate(async (url) => {
                const r = await fetch(url); const blob = await r.blob();
                await window.FrontierAudioEditor.loadWav(new File([blob], 'FerrariLaFerrari_pull_cpp.wav', { type: 'audio/wav' }));
                const E = window.FrontierAudioEditor.editor;
                return { feed: E.feed, seconds: E.fileClip ? E.fileClip.duration : 0, channels: E.fileClip ? E.fileClip.numberOfChannels : 0, sheet: !!E.fileOrders, hits: E.fileOrders ? Array.from(E.fileOrders.hits).filter(h => h > 0).length : 0 };
            }, '/Scratchpad/Reference/FerrariLaFerrari_pull_cpp.wav');
            check(loaded.feed === 'file' && loaded.seconds > 14.9 && loaded.channels === 2 && loaded.sheet, 'C++ WAV dropped: file feed on, ' + loaded.seconds.toFixed(2) + ' s stereo decoded, order sheet built over ' + loaded.hits + ' rpm columns (' + (Date.now() - t0) + ' ms)');
            const t1 = Date.now();
            const agreement = await page.evaluate(async () => {
                const F = window.FrontierAudioEditor, E = F.editor;
                const live = await F.analyseLiveOrders(); live.pull = E.fileOrders.pull; live.car = F.carKey; E.liveOrders = live;
                E.fileOrderAgreement = F.compareOrderSheets(E.fileOrders, live);
                document.querySelector('#overlaySeg button[data-overlay="split"]').click();
                return E.fileOrderAgreement;
            });
            check(agreement.count > 1000 && agreement.mean <= 0.1, 'C++ WAV order sheet vs the worklet\'s own render of the same pull: mean |Δ| ' + agreement.mean.toFixed(4) + ' dB, worst ' + agreement.worst.toFixed(3) + ' dB over ' + agreement.count + ' order cells ≥ −40 dB (limit: mean 0.1 dB; ' + (Date.now() - t1) + ' ms)');
            await new Promise(r => setTimeout(r, 700));
            const ordersPanel = await page.evaluateHandle(() => document.getElementById('orders').closest('.panel'));
            await ordersPanel.screenshot({ path: path.join(shots, 'AudioEditor_07_CppOverlay.png') });
            await page.evaluate(() => document.querySelector('#overlaySeg button[data-overlay="diff"]').click());
            await new Promise(r => setTimeout(r, 400));
            await ordersPanel.screenshot({ path: path.join(shots, 'AudioEditor_08_CppOverlay_Delta.png') });
            const lit = await page.evaluate(() => { const c = document.getElementById('orders'), d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 0; i < d.length; i += 16) if (d[i] + d[i + 1] + d[i + 2] > 40) ++n; return n; });
            check(lit > 200, 'overlay drawn on the order diagram (' + lit + ' lit samples)');
            // Export TOML ⇄ C++ Save: same structure, same header lines → same bytes
            const saved = fs.readFileSync(cppToml, 'utf8');
            const header = saved.split('\n').filter((l, i, a) => { for (let j = 0; j <= i; ++j) if (!a[j].startsWith('#')) return false; return true; }).map(l => l.length > 2 ? l.slice(2) : '');
            const exported = await page.evaluate((h) => window.FrontierAudioEditor.serialiseToml(window.FrontierAudioEditor.cars.FerrariLaFerrari.current, h), header);
            let firstDiff = -1; for (let i = 0; i < Math.max(saved.length, exported.length); ++i) if (saved[i] !== exported[i]) { firstDiff = i; break; }
            check(firstDiff < 0, 'Export TOML text == Project-Dyno --save (AcousticStructure::Save): ' + saved.length + ' bytes identical' + (firstDiff >= 0 ? ' — FIRST DIFFERENCE at byte ' + firstDiff + ': ' + JSON.stringify(saved.slice(Math.max(0, firstDiff - 20), firstDiff + 30)) + ' vs ' + JSON.stringify(exported.slice(Math.max(0, firstDiff - 20), firstDiff + 30)) : ''));
            await page.evaluate(() => { document.querySelector('#overlaySeg button[data-overlay="off"]').click(); document.querySelector('#feedSeg button[data-feed="live"]').click(); });
        }
    }
    console.log('\n[6] row A4 reference lane: a recording tracked blind, played along its trace, compared with the synth');
    {
        // the "recording": a node render of the Demon's pull, written as a 16-bit WAV (what a phone would hand over), no rpm metadata
        const refDir = path.join(repo, 'Scratchpad', 'Reference'); fs.mkdirSync(refDir, { recursive: true });
        const recWav = path.join(refDir, 'DodgeDemon_pull_recording.wav');
        const demonStructure = D.structureFromToml(D.parseToml(html.match(/<script type="text\/toml" data-car="DodgeDemon">([\s\S]*?)<\/script>/)[1]));
        const seq = new D.DynoSequence(); seq.select('pull', demonStructure.vehicle.redline_rpm, demonStructure.vehicle.idle_rpm);
        const recSeconds = seq.duration();
        {
            const ig = new D.AcousticIntegrator(RATE, demonStructure, 0xC0FFEE);   // another seed: not the editor's own render
            const total = Math.floor(recSeconds * RATE), L = new Float64Array(total), R = new Float64Array(total); let done = 0, time = 0;
            while (done < total) { const n = Math.min(64, total - done), dt = n / RATE; seq.advance(dt); const rec = D.scriptedRecord(seq, demonStructure.vehicle, time); ig.assignDemand(rec.rpm, rec.throttle, rec.load, 0); ig.render(L.subarray(done, done + n), R.subarray(done, done + n), n); done += n; time += dt; }
            const bytes = new DataView(new ArrayBuffer(44 + total * 2)); const str = (o, t) => { for (let i = 0; i < t.length; ++i) bytes.setUint8(o + i, t.charCodeAt(i)); };
            str(0, 'RIFF'); bytes.setUint32(4, 36 + total * 2, true); str(8, 'WAVE'); str(12, 'fmt '); bytes.setUint32(16, 16, true); bytes.setUint16(20, 1, true); bytes.setUint16(22, 1, true); bytes.setUint32(24, RATE, true); bytes.setUint32(28, RATE * 2, true); bytes.setUint16(32, 2, true); bytes.setUint16(34, 16, true); str(36, 'data'); bytes.setUint32(40, total * 2, true);
            for (let i = 0; i < total; ++i) { const v = Math.max(-1, Math.min(1, 0.5 * (L[i] + R[i]) * 0.7)); bytes.setInt16(44 + i * 2, Math.round(v * 32767), true); }
            fs.writeFileSync(recWav, new Uint8Array(bytes.buffer));
        }
        await page.evaluate(() => window.FrontierAudioEditor.selectCar('DodgeDemon'));
        await new Promise(r => setTimeout(r, 300));
        const t0 = Date.now();
        const lane = await page.evaluate(async (url) => {
            const r = await fetch(url); const blob = await r.blob();
            await window.FrontierAudioEditor.loadReference(new File([blob], 'DodgeDemon_pull_recording.wav', { type: 'audio/wav' }));
            const E = window.FrontierAudioEditor.editor, R = E.reference, tr = R.trace;
            return { feed: E.feed, name: R.name, seconds: R.clip ? R.clip.duration : 0, templated: tr && tr.templated, coverage: tr ? tr.coverage : 0, frames: tr ? tr.frames : 0, segments: tr ? tr.segments : 0,
                     keys: tr ? tr.keys.filter((k, i) => tr.tracked[i]) : [], agreement: R.agreement, offset: R.offsetDb, busy: R.busy, row: getComputedStyle(document.getElementById('refRow')).display, playing: !!R.emitter, hear: R.hear };
        }, '/Scratchpad/Reference/DodgeDemon_pull_recording.wav');
        const ms = Date.now() - t0;
        check(lane.feed === 'reference' && lane.row !== 'none' && lane.seconds > recSeconds - 0.1, 'recording dropped: Reference feed on, lane visible, ' + lane.seconds.toFixed(2) + ' s decoded (' + ms + ' ms for bank + track + sheet + synth render)');
        check(lane.templated === true && lane.frames > 500, 'tracker ran in the Worker with the Demon\'s order bank (' + lane.frames + ' frames, ' + lane.segments + ' tracked segments)');
        // blind trace vs the pull's true rpm
        const truth = new D.DynoSequence(); truth.select('pull', demonStructure.vehicle.redline_rpm, demonStructure.vehicle.idle_rpm);
        const errs = []; let slips = 0;
        for (const k of lane.keys) { truth.sample(k[0]); const e = k[1] / truth.rpm; errs.push(Math.abs(e - 1)); if (Math.abs(e - 2) < 0.08 || Math.abs(e - 0.5) < 0.04) ++slips; }
        errs.sort((a, b) => a - b);
        const med = errs.length ? errs[errs.length >> 1] : 1, p90 = errs.length ? errs[Math.floor(errs.length * 0.9)] : 1;
        check(lane.coverage > 0.7 && med < 0.01 && slips === 0, 'blind rpm trace follows the Demon pull: ' + Math.round(lane.coverage * 100) + ' % tracked, median error ' + (med * 100).toFixed(2) + ' %, p90 ' + (p90 * 100).toFixed(2) + ' %, octave slips ' + slips + ' (bars: > 70 %, < 1 %, 0)');
        // same voice, other seed, 16-bit, 0.7 gain, along a tracked (not scripted) rpm — the per-cell running means of two independent noise
        // realisations differ by ≈ 2 dB where a cell is one or two 40 ms spectra deep (the sweep never dwells); the bar is the 3 dB an ear notices
        check(lane.agreement && lane.agreement.count > 500 && lane.agreement.mean <= 3.0, 'recording vs synth along the trace (Split / Δ): mean |Δ| ' + (lane.agreement ? lane.agreement.mean.toFixed(2) : '—') + ' dB over ' + (lane.agreement ? lane.agreement.count : 0) + ' order cells, recording lifted ' + lane.offset.toFixed(1) + ' dB (limit: mean 3 dB — same voice, other seed, 16-bit)');
        check(lane.playing && lane.hear === 'recording', 'clip playing along its trace (hear = ' + lane.hear + ')');
        await new Promise(r => setTimeout(r, 900));
        // the worklet walks the same trace: its report's pullTime must sit on the clip's playhead (a few slices of report latency) and its smoothed rpm on the trace there
        const gaugeFollows = await page.evaluate(() => { const F = window.FrontierAudioEditor, E = F.editor, R = E.reference; const t = F.referenceTime(); return { t, pullTime: E.last.pullTime, pull: E.last.pull, rpm: F.Analysis.traceRpmAt(R.trace, E.last.pullTime, false), worklet: E.last.rpm, gauge: document.getElementById('rRpm').textContent }; });
        check(gaugeFollows.t > 0.5 && gaugeFollows.pull === 'reference' && Math.abs(gaugeFollows.pullTime - gaugeFollows.t) < 0.15 && Math.abs(gaugeFollows.worklet / gaugeFollows.rpm - 1) < 0.02, 'worklet plays the synth along the trace in step with the clip: playhead ' + gaugeFollows.t.toFixed(2) + ' s, worklet at ' + gaugeFollows.pullTime.toFixed(2) + ' s of "' + gaugeFollows.pull + '", trace ' + Math.round(gaugeFollows.rpm) + ' rpm, worklet rpm ' + Math.round(gaugeFollows.worklet) + ' (gauge ' + gaugeFollows.gauge + ')');
        await page.evaluate(() => window.FrontierAudioEditor.seekReference(6.0));
        await new Promise(r => setTimeout(r, 300));
        const sought = await page.evaluate(() => { const F = window.FrontierAudioEditor; const t = F.referenceTime(); return { t, rpm: F.Analysis.traceRpmAt(F.editor.reference.trace, t, false) }; });
        truth.sample(sought.t);
        check(sought.t > 5.9 && sought.t < 6.8 && Math.abs(sought.rpm / truth.rpm - 1) < 0.03, 'seek to 6 s: playhead ' + sought.t.toFixed(2) + ' s, trace ' + Math.round(sought.rpm) + ' rpm vs pull ' + Math.round(truth.rpm) + ' rpm at that instant');
        await new Promise(r => setTimeout(r, 500));
        const refPanel = await page.evaluateHandle(() => document.getElementById('refRow'));
        await refPanel.screenshot({ path: path.join(shots, 'AudioEditor_11_ReferenceLane_Trace.png') });
        const ordersPanel = await page.evaluateHandle(() => document.getElementById('orders').closest('.panel'));
        await ordersPanel.screenshot({ path: path.join(shots, 'AudioEditor_12_ReferenceLane_Split.png') });
        // ÷2 then ×2: the trace halves (the synth would play an octave low), the sheets are re-read, ×2 restores it
        const halved = await page.evaluate(async () => { const F = window.FrontierAudioEditor; await F.rescaleReference(0.5); const R = F.editor.reference; return { scale: R.scale, rpm: R.trace.rpm[Math.floor(R.trace.frames / 2)], agreement: R.agreement && R.agreement.mean }; });
        const restored = await page.evaluate(async () => { const F = window.FrontierAudioEditor; await F.rescaleReference(2.0); const R = F.editor.reference; return { scale: R.scale, rpm: R.trace.rpm[Math.floor(R.trace.frames / 2)], agreement: R.agreement && R.agreement.mean }; });
        check(halved.scale === 0.5 && Math.abs(halved.rpm * 2 / restored.rpm - 1) < 1e-6 && restored.scale === 1 && halved.agreement > restored.agreement, '÷2 halves the trace (mid-clip ' + Math.round(halved.rpm) + ' rpm, agreement worsens to ' + halved.agreement.toFixed(2) + ' dB), ×2 restores it (' + Math.round(restored.rpm) + ' rpm, ' + restored.agreement.toFixed(3) + ' dB)');
        // Synth / Both hearing modes switch the emitters without errors; back to the live feed closes the lane
        // the clip keeps running (muted) in Synth mode so the playhead — and the synth's rpm — stay in step; the gains do the switching
        const hear = await page.evaluate(async () => { const F = window.FrontierAudioEditor, E = F.editor; const out = []; for (const h of ['synth', 'both', 'recording']) { document.querySelector('#hearSeg button[data-hear="' + h + '"]').click(); await new Promise(r => setTimeout(r, 250)); out.push(E.reference.hear + ':' + (E.reference.emitter ? 'clip' : 'noclip') + ':live' + E.liveGain.gain.value.toFixed(1) + ':file' + E.fileGain.gain.value.toFixed(1)); } document.querySelector('#feedSeg button[data-feed="live"]').click(); await new Promise(r => setTimeout(r, 200)); return { out, feed: E.feed, row: getComputedStyle(document.getElementById('refRow')).display, emitter: !!E.reference.emitter }; });
        check(hear.out.join(' ') === 'synth:clip:live1.0:file0.0 both:clip:live1.0:file1.0 recording:clip:live0.0:file1.0' && hear.feed === 'live' && hear.row === 'none' && !hear.emitter, 'hear Synth / Both / Recording switch the gains (' + hear.out.join(' ') + '); back to Live closes the lane and stops the clip');
    }
    check(errors.length === 0, 'no console / page errors during the live session' + (errors.length ? ' → ' + errors.slice(0, 3).join(' | ') : ''));

    await browser.close(); server.close();
    console.log('\n' + (fail === 0 ? 'ALL PASS' : fail + ' FAIL') + ' — ' + pass + ' pass, ' + fail + ' fail');
    process.exit(fail === 0 ? 0 : 1);
})().catch(e => { console.error('ERROR ' + (e.stack || e.message)); process.exit(2); });
