'use strict';
// ============================================================================================================================================
//                                                   ACOUSTICEDITORRENDER.JS
// ============================================================================================================================================
// 🧪 Sandbox proof for the HTML row: renders the three vehicles with the editor's own DSP, writes WAV (git-ignored) +
//    spectrogram / order-diagram PNGs (committed as Scratchpad/AcousticEditor_<Car>_Pull.png) and checks the A2 invariants
//    the C++ port must reproduce: the firing-order line (N/2 × rpm/60) within 1 Hz and within 20 dB of the loudest line,
//    the voice-restart hash past 16 kHz below −30 dB re peak (every firing restarts its cylinder's voice from phase 0 —
//    that step is part of the reference's bite; the sheet is read linearly so the lookup itself adds nothing above −52 dB),
//    slice-size invariance, ±1 clip honoured, transients never dropped, firing count = ∫ rpm·N/120.
//    Rev 2 voice (event synth after RevSim): the dominant order is no longer N/2 by construction — the parity bank pan and
//    the firing-time walk deliberately put energy on orders below the firing order (the half-order rumble under the note).
//    Rev 3 voice (`voice.crank_pulse`, the LaFerrari): section [6] measures the References/AcousticPhaseA-VoicingReport.md
//    §10.4 idle targets per channel (half-orders, order 3, orders 18 / 24, centroid, content above 300 Hz, line count) and
//    the loaded-range headroom (no clipped samples on the trackside pull / overrun / limiter sequences, cockpit too).
const fs = require('fs'), path = require('path'), zlib = require('zlib');
// Loads the DSP and the three vehicle TOMLs straight out of Tools/AudioEditor/index.html, so the proof measures the
// shipped page, not a copy.   Run:  node Scratchpad/AcousticEditorRender.js [outDir]   (node ≥ 18, no packages)
const repo = path.resolve(__dirname, '..');
const html = fs.readFileSync(path.join(repo, 'Tools', 'AudioEditor', 'index.html'), 'utf8');
const dspText = html.slice(html.indexOf('<script id="dsp" type="text/plain">') + '<script id="dsp" type="text/plain">'.length, html.indexOf('</script>', html.indexOf('<script id="dsp"')));
const D = (() => { const module = { exports: {} }; new Function('module', 'exports', dspText)(module, module.exports); return module.exports; })();
const CARS = {}, CAR_ORDER = [];
for (const m of html.matchAll(/<script type="text\/toml" data-car="([^"]+)">([\s\S]*?)<\/script>/g)) { CARS[m[1]] = D.parseToml(m[2]); CAR_ORDER.push(m[1]); }

const RATE = 48000;
const outDir = process.argv[2] || path.join(repo, 'Scratchpad');
fs.mkdirSync(outDir, { recursive: true });
let pass = 0, fail = 0;
function check(ok, text) { if (ok) ++pass; else ++fail; console.log((ok ? '  PASS  ' : '  FAIL  ') + text); }

// ---------------------------------------------------------------- render ---------------------------------------------------------------
function renderPull(structure, pullName, seconds, sliceFrames, opts)
{
    const ig = new D.AcousticIntegrator(RATE, structure, 0x5EED1234);
    if (opts && opts.pure) ig.pureTone = true;
    if (opts && opts.listener) ig.assignListener(opts.listener);
    const pull = new D.DynoSequence(); pull.select(pullName, structure.vehicle.redline_rpm, structure.vehicle.idle_rpm);
    const total = Math.floor(seconds * RATE);
    const L = new Float64Array(total), R = new Float64Array(total);
    const trace = [];
    let done = 0, time = 0.0;
    while (done < total)
    {
        const n = Math.min(sliceFrames, total - done);
        const dt = n / RATE;
        pull.advance(dt);
        const rec = D.scriptedRecord(pull, structure.vehicle, time);
        ig.assignDemand(rec.rpm, rec.throttle, rec.load, 0.0);
        ig.render(L.subarray(done, done + n), R.subarray(done, done + n), n);
        done += n; time += dt;
        if (trace.length === 0 || time - trace[trace.length - 1].t >= 0.05) trace.push({ t: time, rpm: rec.rpm, throttle: rec.throttle, load: rec.load });
    }
    return { L, R, ig, trace };
}

function renderSteady(structure, rpm, throttle, seconds, opts)
{
    const ig = new D.AcousticIntegrator(RATE, structure, 0x5EED1234);
    if (opts && opts.pure) ig.pureTone = true;
    ig.rpmS = rpm; ig.assignDemand(rpm, throttle, throttle, 0.0);
    const total = Math.floor(seconds * RATE);
    const L = new Float64Array(total), R = new Float64Array(total);
    for (let done = 0; done < total; done += 256) { const n = Math.min(256, total - done); ig.render(L.subarray(done, done + n), R.subarray(done, done + n), n); }
    return { L, R, ig };
}

// ---------------------------------------------------------------- WAV ------------------------------------------------------------------
function writeWav(file, L, R)
{
    const n = L.length, bytes = Buffer.alloc(44 + n * 4);
    bytes.write('RIFF', 0); bytes.writeUInt32LE(36 + n * 4, 4); bytes.write('WAVE', 8); bytes.write('fmt ', 12);
    bytes.writeUInt32LE(16, 16); bytes.writeUInt16LE(1, 20); bytes.writeUInt16LE(2, 22); bytes.writeUInt32LE(RATE, 24);
    bytes.writeUInt32LE(RATE * 4, 28); bytes.writeUInt16LE(4, 32); bytes.writeUInt16LE(16, 34); bytes.write('data', 36); bytes.writeUInt32LE(n * 4, 40);
    for (let i = 0; i < n; ++i) { bytes.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(L[i] * 32767))), 44 + i * 4); bytes.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(R[i] * 32767))), 46 + i * 4); }
    fs.writeFileSync(file, bytes);
}

// ---------------------------------------------------------------- FFT ------------------------------------------------------------------
function fft(re, im)
{
    const n = re.length;
    for (let i = 1, j = 0; i < n; ++i) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (let len = 2; len <= n; len <<= 1)
    {
        const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
        for (let i = 0; i < n; i += len)
        {
            let cr = 1, ci = 0;
            for (let j = 0; j < len / 2; ++j)
            {
                const ur = re[i + j], ui = im[i + j], vr = re[i + j + len / 2] * cr - im[i + j + len / 2] * ci, vi = re[i + j + len / 2] * ci + im[i + j + len / 2] * cr;
                re[i + j] = ur + vr; im[i + j] = ui + vi; re[i + j + len / 2] = ur - vr; im[i + j + len / 2] = ui - vi;
                const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}
function spectrumDb(x, start, size)
{
    const re = new Float64Array(size), im = new Float64Array(size);
    let wsum = 0;
    for (let i = 0; i < size; ++i) { const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / size); re[i] = (x[start + i] || 0) * w; wsum += w; }
    fft(re, im);
    const out = new Float64Array(size / 2);
    for (let k = 0; k < size / 2; ++k) { const mag = 2 * Math.sqrt(re[k] * re[k] + im[k] * im[k]) / wsum; out[k] = 20 * Math.log10(mag + 1e-12); }
    return out;
}
function peakBin(db, lo, hi) { let best = lo; for (let k = lo; k <= hi; ++k) if (db[k] > db[best]) best = k; return best; }
function interpolatedPeakHz(db, k, size)
{
    const a = db[k - 1], b = db[k], c = db[k + 1];
    const p = 0.5 * (a - c) / (a - 2 * b + c);
    return (k + (Number.isFinite(p) ? p : 0)) * RATE / size;
}

// ---------------------------------------------------------------- PNG ------------------------------------------------------------------
function crc32Sheet() { const t = new Uint32Array(256); for (let n = 0; n < 256; ++n) { let c = n; for (let k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; }
const CRC = crc32Sheet();
function crc32(buf) { let c = 0xFFFFFFFF; for (let i = 0; i < buf.length; ++i) c = CRC[(c ^ buf[i]) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }
function pngChunk(type, payload) { const len = Buffer.alloc(4); len.writeUInt32LE(0); len.writeUInt32BE(payload.length); const td = Buffer.concat([Buffer.from(type, 'ascii'), payload]); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td)); return Buffer.concat([len, td, crc]); }
// Indexed-colour PNG (8-bit palette): the heat ramp has 256 entries, so one byte per pixel — a third of the RGB size.
function writePng(file, width, height, indices, palette)
{
    const raw = Buffer.alloc((width + 1) * height);
    for (let y = 0; y < height; ++y) { raw[y * (width + 1)] = 0; indices.copy(raw, y * (width + 1) + 1, y * width, (y + 1) * width); }
    const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4); ihdr[8] = 8; ihdr[9] = 3; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    fs.writeFileSync(file, Buffer.concat([Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]), pngChunk('IHDR', ihdr), pngChunk('PLTE', palette), pngChunk('IDAT', zlib.deflateSync(raw, { level: 9 })), pngChunk('IEND', Buffer.alloc(0))]));
}
// palette: 0 … 249 heat ramp (dark blue → purple → orange → white), 250 background, 251 white, 252 blue marker, 253 grey
const PALETTE = Buffer.alloc(256 * 3);
for (let i = 0; i < 250; ++i)
{
    const t = i / 249;
    const r = Math.min(1, Math.max(0, 1.6 * t - 0.1)), g = Math.max(0, Math.min(1, 2.2 * t - 1.1)), b = t < 0.5 ? 0.15 + 1.2 * t : Math.max(0, 2.0 - 2.4 * t);
    PALETTE[i * 3] = Math.round(255 * r); PALETTE[i * 3 + 1] = Math.round(255 * g); PALETTE[i * 3 + 2] = Math.round(255 * b);
}
PALETTE.set([0x0A, 0x0A, 0x0A], 250 * 3); PALETTE.set([0xF0, 0xF0, 0xF0], 251 * 3); PALETTE.set([0x6C, 0x77, 0xFF], 252 * 3); PALETTE.set([0x88, 0x88, 0x88], 253 * 3);
const INK_BACKGROUND = 250, INK_WHITE = 251, INK_BLUE = 252, INK_GREY = 253;
function heat(v) { return Math.max(0, Math.min(249, Math.round(249 * v))); }   // 0..1 → palette index
// Spectrogram (top) + order diagram (bottom): x = time, y = frequency (log) / order.
function proofImage(file, L, R, trace, structure, title)
{
    const W = 1000, HS = 360, HO = 220, PAD = 30, H = HS + HO + PAD * 3;
    const ink = Buffer.alloc(W * H, INK_BACKGROUND);
    const size = 4096, hop = Math.floor(L.length / W);
    const mono = new Float64Array(L.length); for (let i = 0; i < L.length; ++i) mono[i] = 0.5 * (L[i] + R[i]);
    const fLo = 30, fHi = 20000, N = structure.vehicle.cylinder_count;
    const maxOrder = Math.max(12, N * 2);
    const rpmAt = (t) => { let best = trace[0]; for (const p of trace) if (p.t <= t) best = p; return best.rpm; };
    const put = (x, y, c) => { if (x < 0 || y < 0 || x >= W || y >= H) return; ink[y * W + x] = c; };
    for (let x = 0; x < W; ++x)
    {
        const start = Math.min(mono.length - size, x * hop);
        const db = spectrumDb(mono, start, size);
        for (let y = 0; y < HS; ++y)
        {
            const f = fLo * Math.pow(fHi / fLo, 1 - y / HS);
            const k = Math.min(size / 2 - 1, Math.max(0, Math.round(f * size / RATE)));
            put(x, PAD + y, heat(Math.round((db[k] + 100) / 2) * 2 / 100));
        }
        const rpm = rpmAt(start / RATE), f0 = rpm / 60;
        for (let y = 0; y < HO; ++y)
        {
            const order = maxOrder * (1 - y / HO);
            const f = order * f0;
            const k = Math.round(f * size / RATE);
            if (k >= 2 && k < size / 2 - 2) { let m = -200; for (let j = k - 2; j <= k + 2; ++j) m = Math.max(m, db[j]); put(x, PAD * 2 + HS + y, heat(Math.round((m + 90) / 2) * 2 / 90)); }
        }
    }
    // order gridlines on the order diagram: every integer order white tick at left, dominant N/2 order marked in blue
    for (let o = 1; o <= maxOrder; ++o) { const y = PAD * 2 + HS + Math.round(HO * (1 - o / maxOrder)); for (let x = 0; x < (o === N / 2 ? 40 : (o % 2 === 0 ? 14 : 7)); ++x) put(x, y, o === N / 2 ? INK_BLUE : INK_WHITE); }
    // rpm trace over the spectrogram (thin white line at the dominant order N/2 frequency) for orientation
    for (let x = 0; x < W; ++x) { const rpm = rpmAt(Math.min(mono.length - size, x * hop) / RATE), f = rpm / 60 * N / 2; const y = Math.round(HS * (1 - Math.log(f / fLo) / Math.log(fHi / fLo))); put(x, PAD + y, INK_WHITE); }
    // frequency decade ticks
    for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) { const y = PAD + Math.round(HS * (1 - Math.log(f / fLo) / Math.log(fHi / fLo))); for (let x = 0; x < 10; ++x) put(x, y, INK_GREY); }
    writePng(file, W, H, ink, PALETTE);
    console.log('  wrote ' + path.relative(repo, file) + '  (' + title + ')');
}

// ---------------------------------------------------------------- checks ---------------------------------------------------------------
console.log('AcousticEditor DSP proof — ' + new Date().toISOString() + '  rate ' + RATE + '  cars ' + CAR_ORDER.join(', '));
for (const key of CAR_ORDER)
{
    const structure = D.structureFromToml(CARS[key]);
    const N = structure.vehicle.cylinder_count;
    console.log('\n[' + key + '] ' + structure.vehicle.name + ' — ' + N + ' cylinders');

    // [1] firing-order line at steady state, pure-tone mode (3 speeds): N/2 × rpm/60 within 1 Hz, within 12 dB of the loudest order
    for (const rpm of [3000, 6000, Math.min(8500, structure.vehicle.redline_rpm - 200)])
    {
        const r = renderSteady(structure, rpm, 1.0, 3.0, { pure: true });
        const size = 65536, db = spectrumDb(r.L, r.L.length - size, size);
        const f0 = rpm / 60;
        let bestOrder = 0, bestDb = -999, firingDb = -999;
        for (let o = 0.5; o <= 2 * N; o += 0.5) { const k = Math.round(o * f0 * size / RATE); let m = -999; for (let j = k - 2; j <= k + 2; ++j) m = Math.max(m, db[j]); if (m > bestDb) { bestDb = m; bestOrder = o; } if (o === N / 2) firingDb = m; }
        const kExp = Math.round(N / 2 * f0 * size / RATE);
        const kPeak = peakBin(db, kExp - 3, kExp + 3);
        const fPeak = interpolatedPeakHz(db, kPeak, size);
        check(Math.abs(fPeak - N / 2 * f0) < 1.0, 'firing frequency @ ' + rpm + ' rpm = ' + fPeak.toFixed(2) + ' Hz (expected ' + (N / 2 * f0).toFixed(2) + '), ' + firingDb.toFixed(1) + ' dBFS');
        check(bestDb - firingDb < 20.0, 'loudest order @ ' + rpm + ' rpm = ' + bestOrder + ' at ' + bestDb.toFixed(1) + ' dBFS, firing order ' + N / 2 + ' within ' + (bestDb - firingDb).toFixed(1) + ' dB (limit 20)');
        // [2] voice-restart hash: everything past 16 kHz stays below −30 dB relative to the peak (pure mode, linear sheet read)
        let maxHigh = -999; for (let k = Math.round(16000 * size / RATE); k < size / 2; ++k) maxHigh = Math.max(maxHigh, db[k]);
        check(maxHigh - bestDb < -30, 'above 16 kHz: ' + (maxHigh - bestDb).toFixed(1) + ' dB re peak (limit −30)');
    }

    // [3] slice invariance of the full chain with held demand: 1 / 37 / 64 / 256 frame slices identical
    {
        const renders = [1, 37, 64, 256].map(slice =>
        {
            const ig = new D.AcousticIntegrator(RATE, structure, 77);
            ig.assignDemand(4200, 0.6, 0.6, 0.0);
            const total = 24000, L = new Float64Array(total), R = new Float64Array(total);
            for (let done = 0; done < total; done += slice) { const n = Math.min(slice, total - done); ig.render(L.subarray(done, done + n), R.subarray(done, done + n), n); }
            return L;
        });
        let maxDiff = 0; for (let i = 1; i < renders.length; ++i) for (let n = 0; n < renders[0].length; ++n) maxDiff = Math.max(maxDiff, Math.abs(renders[0][n] - renders[i][n]));
        check(maxDiff === 0, 'held-demand render identical across 1/37/64/256-frame slices (max |Δ| = ' + maxDiff + ')');
    }

    // [4] full WOT pull render → WAV + PNG; sanity on level, limiter, pops
    {
        const t0 = Date.now();
        const r = renderPull(structure, 'pull', 15.0, 256);
        const ms = Date.now() - t0;
        let peak = 0, rms = 0; for (let i = 0; i < r.L.length; ++i) { peak = Math.max(peak, Math.abs(r.L[i]), Math.abs(r.R[i])); rms += r.L[i] * r.L[i]; }
        rms = Math.sqrt(rms / r.L.length);
        check(peak <= 1.0 && peak > 0.3, 'pull peak ' + peak.toFixed(3) + ' (±1 clip holds, level useful > 0.3), rms ' + (20 * Math.log10(rms)).toFixed(1) + ' dBFS, clipped samples ' + r.ig.clippedCount + ' (' + (100 * r.ig.clippedCount / r.L.length).toFixed(1) + ' %)');
        check(r.ig.transients.dropped === 0, 'transient slots never overflowed (dropped ' + r.ig.transients.dropped + ', spawned ' + r.ig.transients.spawned + ', firings ' + r.ig.firingCount + ')');
        check(r.ig.popCount > 3, 'overrun pops after lift: ' + r.ig.popCount);
        check(!Number.isNaN(peak), 'no NaN');
        console.log('  render 15 s in ' + ms + ' ms → ' + (15000 / ms).toFixed(1) + '× realtime (node)');
        writeWav(path.join(outDir, 'AcousticEditor_' + key + '_Pull.wav'), r.L, r.R);
        proofImage(path.join(outDir, 'AcousticEditor_' + key + '_Pull.png'), r.L, r.R, r.trace, structure, structure.vehicle.name + ' WOT pull');
        // firing count sanity: integral of rpm/60*N/2 over the pull
        let expected = 0; for (let i = 1; i < r.trace.length; ++i) expected += (r.trace[i].t - r.trace[i - 1].t) * r.trace[i - 1].rpm / 60 * N / 2;
        check(Math.abs(r.ig.firingCount - expected) / expected < 0.03, 'firing count ' + r.ig.firingCount + ' vs ∫ rpm·N/120 dt ≈ ' + expected.toFixed(0) + ' (±3 %)');
    }
    // [5] idle + blip renders for listening
    {
        const b = renderPull(structure, 'blip', 8.0, 256); writeWav(path.join(outDir, 'AcousticEditor_' + key + '_Blip.wav'), b.L, b.R);
        const i = renderPull(structure, 'idle', 5.0, 256); writeWav(path.join(outDir, 'AcousticEditor_' + key + '_Idle.wav'), i.L, i.R);
        let peak = 0; for (let n = 0; n < i.L.length; ++n) peak = Math.max(peak, Math.abs(i.L[n]));
        check(peak > (structure.voice.crank_pulse ? 0.01 : 0.05), 'idle audible (peak ' + peak.toFixed(3) + ')');
    }
    // [6] rev 3 only: the §10.4 idle targets, measured on the LEFT channel of the full chain at 1081 rpm (the real clips' crank speed)
    if (structure.voice.crank_pulse)
    {
        const rpm = 1081;
        const idleSpectrum = (s) =>
        {
            const ig = new D.AcousticIntegrator(RATE, s, 0x5EED1234);
            ig.rpmS = rpm; ig.assignDemand(rpm, 0.0, 0.06, 0.0);
            const total = Math.floor(2.5 * RATE), L = new Float64Array(total), R = new Float64Array(total);
            for (let done = 0; done < total; done += 256) { const n = Math.min(256, total - done); ig.render(L.subarray(done, done + n), R.subarray(done, done + n), n); }
            return spectrumDb(L, total - 65536, 65536);
        };
        const size = 65536, db = idleSpectrum(structure), f0 = rpm / 60;
        const orderOf = (spectrum, o) => { const k = Math.round(o * f0 * size / RATE); let m = -999; for (let j = k - 2; j <= k + 2; ++j) if (j > 0 && j < spectrum.length) m = Math.max(m, spectrum[j]); return m; };
        const order = (o) => orderOf(db, o);
        const ref = order(6), halves = [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5];
        // half-orders twice: the shipped structure (cycle-to-cycle variance on → the physical floor between the lines, which the real
        // loops also carry at −36 … −42 dB mean) and the same structure with the variance off (the STRUCTURAL half-orders of the
        // firing geometry and the pan — the §10.4 "< −40 dB" target, what rev 2's parity pan failed)
        let halfMax = -999, halfMean = 0; for (const h of halves) { const v = order(h) - ref; halfMax = Math.max(halfMax, v); halfMean += v / halves.length; }
        const frozen = JSON.parse(JSON.stringify(structure)); frozen.combustion.jitter_amp = 0.0; frozen.combustion.timing_jitter_deg = 0.0;
        const dbFrozen = idleSpectrum(frozen), refFrozen = orderOf(dbFrozen, 6);
        let structuralMax = -999; for (const h of halves) structuralMax = Math.max(structuralMax, orderOf(dbFrozen, h) - refFrozen);
        let top = -999; for (let k = 2; k < 8000 * size / RATE; ++k) top = Math.max(top, db[k]);
        let above300 = -999; for (let k = Math.ceil(300 * size / RATE); k < 20000 * size / RATE; ++k) above300 = Math.max(above300, db[k]);
        let num = 0, den = 0; for (let k = 2; k < 8000 * size / RATE; ++k) { const p = Math.pow(10, db[k] / 10); num += p * k * RATE / size; den += p; }
        const centroid = num / den;
        let lines = 0; for (let k = 3; k < 1000 * size / RATE; ++k) if (db[k] > top - 26 && db[k] > db[k - 1] && db[k] >= db[k + 1] && db[k] > db[k - 2] && db[k] >= db[k + 2]) ++lines;
        let bestOrder = 0, bestDb = -999; for (let o = 0.5; o <= 24; o += 0.5) { const m = order(o); if (m > bestDb) { bestDb = m; bestOrder = o; } }
        check(bestOrder === 6, 'idle 1081 rpm (L): loudest order = ' + bestOrder + ' (real clips: 6)');
        check(structuralMax <= -40, 'idle structural half-orders ½ … 6½ (variance off) ≤ −40 dB re order 6 (worst ' + structuralMax.toFixed(1) + '; rev 2 left channel had 3½ at 0 dB)');
        check(halfMean <= -36 && halfMax <= -25, 'idle between-order floor with the cycle variance on: mean ' + halfMean.toFixed(1) + ' / worst ' + halfMax.toFixed(1) + ' dB re order 6 (real loops mean −36 … −42, worst −22; limits −36 / −25)');
        check(order(3) - ref <= -20, 'idle order 3 = ' + (order(3) - ref).toFixed(1) + ' dB re order 6 (target ≈ −35, limit −20; rev 2 was −16 … −18)');
        check(order(18) - ref <= -30 && order(24) - ref <= -40, 'idle orders 18 / 24 = ' + (order(18) - ref).toFixed(1) + ' / ' + (order(24) - ref).toFixed(1) + ' dB re order 6 (real −40 / −56; limits −30 / −40)');
        check(centroid >= 100 && centroid <= 200, 'idle centroid < 8 kHz = ' + centroid.toFixed(0) + ' Hz (real 107–179; limits 100–200)');
        check(above300 - top <= -25, 'idle content above 300 Hz = ' + (above300 - top).toFixed(1) + ' dB re the loudest line (real: nothing within 25 dB)');
        check(lines <= 15, 'idle lines within 26 dB below 1 kHz = ' + lines + ' (real rev loop 10; limit 15)');
        console.log('  orders re 6: ' + [1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 18, 24].map(o => o + ':' + (order(o) - ref).toFixed(0)).join(' '));
        // headroom: no clipped samples on the trackside and cockpit sequences (rev 3 leaves the ±1 clip idle; rev 2 leans on it)
        for (const listener of ['trackside', 'cockpit'])
            for (const [pullName, seconds] of [['pull', 12], ['overrun', 10], ['limiter', 11]])
            {
                const r = renderPull(structure, pullName, seconds, 256, { listener });
                let pk = 0; for (let n = 0; n < r.L.length; ++n) pk = Math.max(pk, Math.abs(r.L[n]), Math.abs(r.R[n]));
                check(r.ig.clippedCount === 0 && pk > 0.3, listener + ' ' + pullName + ': clipped ' + r.ig.clippedCount + ', peak ' + pk.toFixed(2) + ' (no clipping, peak > 0.3)');
            }
    }
}
console.log('\n' + (fail === 0 ? 'ALL PASS' : fail + ' FAIL') + ' — ' + pass + ' pass, ' + fail + ' fail');
process.exit(fail === 0 ? 0 : 1);
