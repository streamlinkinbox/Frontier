'use strict';
// ============================================================================================================================================
//                                                   ACOUSTICEDITORRENDER.JS
// ============================================================================================================================================
// 🧪 Sandbox proof for the HTML row: renders the three vehicles with the editor's own DSP, writes WAV (git-ignored) +
//    spectrogram / order-diagram PNGs (committed as Scratchpad/AcousticEditor_<Car>_Pull.png) and checks the A2 invariants
//    the C++ port must reproduce: dominant order N/2 within 1 Hz, nothing above −90 dB past 16 kHz, slice-size invariance.
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

    // [1] dominant order at steady state, pure-tone mode: N/2 within 1 Hz (3 speeds)
    for (const rpm of [3000, 6000, Math.min(8500, structure.vehicle.redline_rpm - 200)])
    {
        const r = renderSteady(structure, rpm, 1.0, 3.0, { pure: true });
        const size = 65536, db = spectrumDb(r.L, r.L.length - size, size);
        // energy per order (sum of ±2 bins around each order up to 2N)
        const f0 = rpm / 60;
        let bestOrder = 0, bestDb = -999;
        for (let o = 1; o <= 2 * N; o += 0.5) { const k = Math.round(o * f0 * size / RATE); let m = -999; for (let j = k - 2; j <= k + 2; ++j) m = Math.max(m, db[j]); if (m > bestDb) { bestDb = m; bestOrder = o; } }
        const kExp = Math.round(N / 2 * f0 * size / RATE);
        const kPeak = peakBin(db, kExp - 3, kExp + 3);
        const fPeak = interpolatedPeakHz(db, kPeak, size);
        check(bestOrder === N / 2, 'dominant order @ ' + rpm + ' rpm = ' + bestOrder + ' (expected ' + N / 2 + '), level ' + bestDb.toFixed(1) + ' dBFS');
        check(Math.abs(fPeak - N / 2 * f0) < 1.0, 'firing frequency @ ' + rpm + ' rpm = ' + fPeak.toFixed(2) + ' Hz (expected ' + (N / 2 * f0).toFixed(2) + ')');
        // [2] aliasing guard: nothing above −90 dB relative to peak beyond 16 kHz (pure mode)
        let maxHigh = -999; for (let k = Math.round(16000 * size / RATE); k < size / 2; ++k) maxHigh = Math.max(maxHigh, db[k]);
        check(maxHigh - bestDb < -90, 'above 16 kHz: ' + (maxHigh - bestDb).toFixed(1) + ' dB re peak (limit −90)');
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
        check(peak <= 1.0 && peak > 0.3, 'pull peak ' + peak.toFixed(3) + ' (limiter holds ≤ 1.0, level useful > 0.3), rms ' + (20 * Math.log10(rms)).toFixed(1) + ' dBFS');
        check(r.ig.pulses.dropped === 0, 'pulse pool never overflowed (dropped ' + r.ig.pulses.dropped + ', spawned ' + r.ig.pulses.spawned + ', firings ' + r.ig.firingCount + ')');
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
        check(peak > 0.05, 'idle audible (peak ' + peak.toFixed(3) + ')');
    }
}
console.log('\n' + (fail === 0 ? 'ALL PASS' : fail + ' FAIL') + ' — ' + pass + ' pass, ' + fail + ' fail');
process.exit(fail === 0 ? 0 : 1);
