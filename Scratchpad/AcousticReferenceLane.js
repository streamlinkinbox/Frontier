'use strict';
// ============================================================================================================================================
//                                                   ACOUSTICREFERENCELANE.JS
// ============================================================================================================================================
// 🧪 Sandbox proof for row A4 (the reference lane of Tools/AudioEditor/index.html): the rpm tracker and the order bank are loaded
//    straight out of the page's <script id="analysis"> block (with the <script id="dsp"> voice as the model of the car), so the
//    proof measures the shipped page. No real recordings exist in the tree (none are obtainable here), so the "recordings" are
//    the page's own renders of the scripted dyno sequences, which carry an exact rpm timeline the tracker never sees:
//    [1] twelve cases (five cars on `pull`, plus idle / blip / overrun / sweep) tracked BLIND — bars per case: median error of the
//        tracked frames < 1 %, no octave slips (a tracked frame within 8 % of 2× or 4 % of ½× the truth), coverage > 60 %;
//        over all cases ≤ 5 % of the tracked frames more than 3 % off
//    [2] the same on degraded recordings: pink noise 12 dB under the signal, a room (three reflections + 6 kHz roll-off), a
//        telephone band (300 Hz … 3.4 kHz, clipped) — the same bars, coverage > 40 %
//    [3] the order bank of a car reads its OWN pull best: a recording tracked with another car's bank (same cylinder count)
//        is reported, not barred — the lane uses the selected car's bank, so a wrong pick shows as a wrong trace, by design
//    [4] the lane's order-sheet agreement: a 16-bit recording of the Demon's pull at another seed, tracked blind, the synth
//        rendered along the trace with the heuristic throttle and read as the same mono mix — mean |Δ| ≤ 3 dB over the cells
//        ≥ −40 dB (two noise realisations differ by ≈ 2 dB per cell where the sweep never dwells); halving the trace (the ÷2
//        button's job) must worsen it by > 3 dB, so the agreement line tells a user when the octave is wrong
//    [5] the #dsp block is bit-neutral against the previous commit for the identity reference set (the A4 change to the DSP
//        text is DynoSequence.assignKeys / the worklet's trace plumbing — sequencing, not synthesis)
//    Run:  node Scratchpad/AcousticReferenceLane.js [--quick]     (node ≥ 18, no packages; ≈ 4 min, --quick skips [2])
const fs = require('fs'), path = require('path'), { execSync } = require('child_process');
const repo = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(repo, 'Tools', 'AudioEditor', 'index.html'), 'utf8');
function blockOf(text, id) { const tag = '<script id="' + id + '" type="text/plain">'; const a = text.indexOf(tag) + tag.length; return text.slice(a, text.indexOf('</script>', a)); }
function loadBlock(text) { const module = { exports: {} }; new Function('module', 'exports', text)(module, module.exports); return module.exports; }
const D = loadBlock(blockOf(html, 'dsp')), A = loadBlock(blockOf(html, 'analysis'));
const CARS = {};
for (const m of html.matchAll(/<script type="text\/toml" data-car="([^"]+)">([\s\S]*?)<\/script>/g)) CARS[m[1]] = D.parseToml(m[2]);
const RATE = 48000, SEED = 0x5EED1234, quick = process.argv.includes('--quick');
let pass = 0, fail = 0;
const check = (ok, text) => { if (ok) ++pass; else ++fail; console.log((ok ? '  PASS  ' : '  FAIL  ') + text); };
const rangeOf = (v) => ({ rpmLo: Math.max(300, v.idle_rpm * 0.7), rpmHi: Math.ceil((v.redline_rpm + 400) / 1000) * 1000 });

// ---- renders ------------------------------------------------------------------------------------------------------------------------
function renderSequence(structure, pullName, seed, slice)
{
    const v = structure.vehicle, integrator = new D.AcousticIntegrator(RATE, structure, seed), seq = new D.DynoSequence();
    seq.select(pullName, v.redline_rpm, v.idle_rpm);
    const total = Math.floor(seq.duration() * RATE), L = new Float64Array(total), R = new Float64Array(total); let done = 0, time = 0;
    while (done < total) { const n = Math.min(slice, total - done), dt = n / RATE; seq.advance(dt); const rec = D.scriptedRecord(seq, v, time); integrator.assignDemand(rec.rpm, rec.throttle, rec.load, 0); integrator.render(L.subarray(done, done + n), R.subarray(done, done + n), n); done += n; time += dt; }
    return [L, R];
}
function renderKeys(structure, keys, seconds, seed, slice)
{
    const v = structure.vehicle, integrator = new D.AcousticIntegrator(RATE, structure, seed), seq = new D.DynoSequence();
    seq.assignKeys(keys, 'reference');
    const total = Math.floor(seconds * RATE), L = new Float64Array(total), R = new Float64Array(total); let done = 0, time = 0;
    while (done < total) { const n = Math.min(slice, total - done), dt = n / RATE; seq.advance(dt); const rec = D.scriptedRecord(seq, v, time); integrator.assignDemand(rec.rpm, rec.throttle, rec.load, 0); integrator.render(L.subarray(done, done + n), R.subarray(done, done + n), n); done += n; time += dt; }
    return [L, R];
}
const monoOf = (L, R) => { const m = new Float32Array(L.length); for (let i = 0; i < L.length; ++i) m[i] = 0.5 * (L[i] + R[i]); return m; };
const banks = {};
function bankOf(car)
{
    if (banks[car]) return banks[car];
    const s = D.structureFromToml(CARS[car]), { rpmLo, rpmHi } = rangeOf(s.vehicle), t0 = Date.now();
    banks[car] = A.orderBankOf(D.AcousticIntegrator, s, RATE, rpmLo, rpmHi, SEED);
    console.log('  (order bank ' + car + ': ' + banks[car].rpms.length + ' rpm points × ' + banks[car].throttles + ' throttles, ' + (Date.now() - t0) + ' ms)');
    return banks[car];
}
// tracks a mono clip of a scripted sequence blind and measures it against the sequence
async function trackCase(car, pullName, mono, bank)
{
    const s = D.structureFromToml(CARS[car]), v = s.vehicle, { rpmLo, rpmHi } = rangeOf(v);
    const t0 = Date.now(), trace = await A.trackReference(mono, RATE, v.cylinder_count, rpmLo, rpmHi, { idleRpm: v.idle_rpm, bank }), ms = Date.now() - t0;
    const seq = new D.DynoSequence(); seq.select(pullName, v.redline_rpm, v.idle_rpm);
    const errs = []; let bad = 0, slips = 0;
    for (let f = 0; f < trace.frames; ++f)
    {
        if (!trace.tracked[f]) continue;
        seq.sample(trace.keys[f][0]); const e = trace.rpm[f] / seq.rpm;
        errs.push(Math.abs(e - 1)); if (Math.abs(e - 1) > 0.03) ++bad; if (Math.abs(e - 2) < 0.08 || Math.abs(e - 0.5) < 0.04) ++slips;
    }
    errs.sort((a, b) => a - b);
    return { trace, ms, tracked: errs.length, median: errs.length ? errs[errs.length >> 1] : 1, p90: errs.length ? errs[Math.floor(errs.length * 0.9)] : 1, bad, slips, coverage: trace.coverage };
}
const fmt = (r) => Math.round(r.coverage * 100) + ' % tracked, median ' + (r.median * 100).toFixed(2) + ' %, p90 ' + (r.p90 * 100).toFixed(2) + ' %, > 3 %: ' + r.bad + '/' + r.tracked + ', octave slips ' + r.slips + ' (' + r.ms + ' ms)';

(async () =>
{
    console.log('AudioEditor reference lane proof — ' + new Date().toISOString());
    const cases = [['FerrariLaFerrari', 'pull'], ['Porsche918Spyder', 'pull'], ['NissanGtrNismo', 'pull'], ['KoenigseggAgeraR', 'pull'], ['DodgeDemon', 'pull'],
                   ['FerrariLaFerrari', 'idle'], ['FerrariLaFerrari', 'blip'], ['FerrariLaFerrari', 'overrun'], ['Porsche918Spyder', 'blip'], ['DodgeDemon', 'idle'], ['NissanGtrNismo', 'overrun'], ['KoenigseggAgeraR', 'sweep']];
    const clips = {};
    console.log('\n[1] blind rpm tracking of the scripted sequences (the tracker sees audio only; the bank is the car\'s own voicing)');
    let allTracked = 0, allBad = 0;
    for (const [car, pullName] of cases)
    {
        const s = D.structureFromToml(CARS[car]);
        const [L, R] = renderSequence(s, pullName, SEED, 64); clips[car + '/' + pullName] = monoOf(L, R);
        const r = await trackCase(car, pullName, clips[car + '/' + pullName], bankOf(car));
        allTracked += r.tracked; allBad += r.bad;
        check(r.median < 0.01 && r.slips === 0 && r.coverage > 0.6, (car + '/' + pullName).padEnd(28) + fmt(r));
    }
    check(allBad <= allTracked * 0.05, 'over all cases ' + allBad + ' of ' + allTracked + ' tracked frames more than 3 % off (' + (100 * allBad / allTracked).toFixed(1) + ' %, limit 5 %)');

    if (!quick)
    {
        console.log('\n[2] degraded recordings (pink noise −12 dB · room · telephone band) on the five pulls + the Demon idle');
        const degrade = (mono, kind) =>
        {
            const out = new Float32Array(mono.length); let x = 12345; const rnd = () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296 - 0.5; };
            if (kind === 'noise')
            {
                let rms = 0; for (const v of mono) rms += v * v; rms = Math.sqrt(rms / mono.length); let b0 = 0, b1 = 0, b2 = 0; const g = rms * Math.pow(10, -12 / 20) * 3;
                for (let i = 0; i < mono.length; ++i) { const w = rnd(); b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913; out[i] = mono[i] + g * (b0 + b1 + b2 + w * 0.1848); }
            }
            else if (kind === 'room')
            {
                const d = [336, 1104, 1968], gn = [0.5, -0.35, 0.25], a = Math.exp(-2 * Math.PI * 6000 / RATE); let y = 0;
                for (let i = 0; i < mono.length; ++i) { let v = mono[i]; for (let j = 0; j < 3; ++j) if (i >= d[j]) v += gn[j] * mono[i - d[j]]; y = a * y + (1 - a) * v; out[i] = y; }
            }
            else
            {
                const ah = Math.exp(-2 * Math.PI * 300 / RATE), al = Math.exp(-2 * Math.PI * 3400 / RATE); let hp = 0, prev = 0, lp = 0;
                for (let i = 0; i < mono.length; ++i) { hp = ah * (hp + mono[i] - prev); prev = mono[i]; lp = al * lp + (1 - al) * hp; out[i] = Math.max(-0.5, Math.min(0.5, lp * 2)); }
            }
            return out;
        };
        for (const kind of ['noise', 'room', 'telephone'])
        {
            let worstMedian = 0, slips = 0, minCoverage = 1, n = 0;
            for (const [car, pullName] of cases.slice(0, 5).concat([['DodgeDemon', 'idle']]))
            {
                const r = await trackCase(car, pullName, degrade(clips[car + '/' + pullName], kind), bankOf(car));
                worstMedian = Math.max(worstMedian, r.median); slips += r.slips; minCoverage = Math.min(minCoverage, r.coverage); ++n;
                console.log('        ' + kind.padEnd(10) + (car + '/' + pullName).padEnd(28) + fmt(r));
            }
            check(worstMedian < 0.01 && slips === 0 && minCoverage > 0.4, kind + ': worst median ' + (worstMedian * 100).toFixed(2) + ' %, octave slips ' + slips + ', lowest coverage ' + Math.round(minCoverage * 100) + ' % over ' + n + ' cases');
        }
    }

    console.log('\n[3] the bank is the car: a recording tracked with another car\'s bank (same cylinder count) — reported, not barred');
    for (const [car, other] of [['Porsche918Spyder', 'KoenigseggAgeraR'], ['KoenigseggAgeraR', 'DodgeDemon'], ['DodgeDemon', 'Porsche918Spyder']])
    {
        const own = await trackCase(car, 'pull', clips[car + '/pull'], bankOf(car)), wrong = await trackCase(car, 'pull', clips[car + '/pull'], bankOf(other));
        console.log('        ' + car + ' pull with its own bank: ' + fmt(own) + '\n        ' + ' '.repeat(car.length) + ' with the ' + other + ' bank: ' + fmt(wrong));
        check(own.bad <= wrong.bad && own.coverage >= wrong.coverage - 0.05, car + ': own bank tracks at least as well as the ' + other + ' bank (' + own.bad + ' vs ' + wrong.bad + ' frames off, coverage ' + Math.round(own.coverage * 100) + ' vs ' + Math.round(wrong.coverage * 100) + ' %)');
    }

    console.log('\n[4] the lane\'s comparison: a 16-bit Demon pull at another seed, tracked blind, synth along the trace, Split / Δ agreement');
    {
        const car = 'DodgeDemon', s = D.structureFromToml(CARS[car]), v = s.vehicle, N = v.cylinder_count, { rpmLo, rpmHi } = rangeOf(v);
        const [L, R] = renderSequence(s, 'pull', 0xC0FFEE, 64), rec = new Float32Array(L.length); let peak = 1e-6;
        for (let i = 0; i < L.length; ++i) { rec[i] = Math.round(Math.max(-1, Math.min(1, 0.5 * (L[i] + R[i]) * 0.7)) * 32767) / 32767; peak = Math.max(peak, Math.abs(rec[i])); }
        for (let i = 0; i < rec.length; ++i) rec[i] *= 0.9 / peak;   // the page peak-normalises a dropped clip to −1 dBFS
        const r = await trackCase(car, 'pull', rec, bankOf(car)), trace = r.trace;
        check(r.median < 0.01 && r.slips === 0 && r.coverage > 0.6, 'the 16-bit recording tracks: ' + fmt(r));
        const compare = (keys, rpmAt) =>
        {
            const [sL, sR] = renderKeys(s, keys, Math.min(trace.duration, 180), SEED, 128), mix = monoOf(sL, sR);
            const recording = A.orderSheetOf(rec, RATE, rpmAt, N, rpmLo, rpmHi, 160), synth = A.orderSheetOf(mix, RATE, rpmAt, N, rpmLo, rpmHi, 160);
            const align = A.alignOrderSheets(recording, synth);
            return Object.assign(A.compareOrderSheets(A.shiftOrderSheet(recording, align.offset), synth), { offset: align.offset });
        };
        const agreement = compare(trace.keys, (t) => A.traceRpmAt(trace, t, true));
        check(agreement.count > 500 && agreement.mean <= 3.0, 'recording vs synth along the trace: mean |Δ| ' + agreement.mean.toFixed(2) + ' dB, worst ' + agreement.worst.toFixed(1) + ' dB over ' + agreement.count + ' cells ≥ −40 dB, recording lifted ' + agreement.offset.toFixed(1) + ' dB (limit: mean 3 dB)');
        const halvedTrace = A.scaleTrace(trace, 0.5), halved = compare(halvedTrace.keys, (t) => A.traceRpmAt(halvedTrace, t, true));
        check(halved.mean > agreement.mean + 3.0, '÷2 (the trace an octave low): mean |Δ| ' + halved.mean.toFixed(2) + ' dB — the agreement line exposes a wrong octave by ' + (halved.mean - agreement.mean).toFixed(1) + ' dB');
        const truthSeq = new D.DynoSequence(); truthSeq.select('pull', v.redline_rpm, v.idle_rpm);
        const truthKeys = trace.keys.map(k => { truthSeq.sample(k[0]); return [k[0], truthSeq.rpm, truthSeq.throttle]; });
        const ideal = compare(truthKeys, (t) => { truthSeq.sample(t); return truthSeq.rpm; });
        console.log('        (the same comparison along the true rpm with the true throttle: mean |Δ| ' + ideal.mean.toFixed(2) + ' dB — the floor set by the two noise realisations; the blind trace costs ' + (agreement.mean - ideal.mean).toFixed(2) + ' dB)');
    }

    console.log('\n[5] the #dsp block against the previous commit: the identity reference set renders bit-identically');
    {
        let previous = null;
        try { previous = execSync('git show HEAD:Tools/AudioEditor/index.html', { cwd: repo, maxBuffer: 1 << 26 }).toString(); } catch (e) { console.log('  (git show unavailable: ' + (e.message || e).toString().split('\n')[0] + ')'); }
        if (previous)
        {
            const P = loadBlock(blockOf(previous, 'dsp')), same = blockOf(previous, 'dsp') === blockOf(html, 'dsp');
            const set = [['FerrariLaFerrari', 'idle'], ['FerrariLaFerrari', 'pull'], ['FerrariLaFerrari', 'blip'], ['FerrariLaFerrari', 'overrun'], ['FerrariLaFerrari', 'limiter'], ['Porsche918Spyder', 'pull'], ['NissanGtrNismo', 'overrun'], ['KoenigseggAgeraR', 'pull'], ['DodgeDemon', 'blip']];
            let worst = 0;
            for (const [car, pullName] of set)
            {
                const render = (M) => { const s = M.structureFromToml(M.parseToml(CARS[car])), v = s.vehicle, ig = new M.AcousticIntegrator(RATE, s, SEED), seq = new M.DynoSequence(); seq.select(pullName, v.redline_rpm, v.idle_rpm); const total = Math.floor(seq.duration() * RATE), L = new Float64Array(total), R = new Float64Array(total); let done = 0, time = 0; while (done < total) { const n = Math.min(64, total - done), dt = n / RATE; seq.advance(dt); const rec = M.scriptedRecord(seq, v, time); ig.assignDemand(rec.rpm, rec.throttle, rec.load, 0); ig.render(L.subarray(done, done + n), R.subarray(done, done + n), n); done += n; time += dt; } return [L, R]; };
                const [aL, aR] = render(P), [bL, bR] = render(D); let d = 0;
                for (let i = 0; i < aL.length; ++i) { const x = Math.abs(aL[i] - bL[i]), y = Math.abs(aR[i] - bR[i]); if (x > d) d = x; if (y > d) d = y; }
                worst = Math.max(worst, d);
            }
            check(worst === 0, '#dsp ' + (same ? 'unchanged' : 'text changed') + ' — ' + set.length + ' reference renders max |Δ| ' + worst + (worst === 0 ? ' (bit-neutral)' : ''));
        }
    }
    console.log('\n' + (fail === 0 ? 'ALL PASS' : fail + ' FAIL') + ' — ' + pass + ' pass, ' + fail + ' fail');
    process.exit(fail === 0 ? 0 : 1);
})().catch(e => { console.error('ERROR ' + (e.stack || e.message)); process.exit(2); });
