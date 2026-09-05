'use strict';
// ══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//  AcousticLaFerrariReference — measures a REAL LaFerrari recording against the rev-2 synth (report row, no DSP change)
// ══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//
//  🧪 Sandbox proof for References/AcousticPhaseA-VoicingReport.md §10. The only LaFerrari-labelled recording reachable from
//  this sandbox (video / sound hosts are blocked) is the sample set of the open RC sound project
//  TheDIYGuy999/Rc_Engine_Sound_ESP32 (src/vehicles/sounds/LaFerrari{Idle,Rev,Start}.h — 22 050 Hz, 8-bit PCM as C arrays,
//  no licence in that repository). Provenance, from its own commit log and README:
//    · LaFerrariStart.h  — labelled "Ferrari LaFerrari, V12"; 2.5 s, not looped → the most trustworthy clip.
//    · LaFerrariRev.h    — labelled "Ferrari LaFerrari, V12"; a 166 ms loop, RATE-SHIFTED by construction (README: the rev
//                          sample is slowed until it is as long as the idle loop) → shape usable, absolute frequencies not.
//    · LaFerrariIdle.h   — labelled "Jaguar XJS V12" in the vehicle profile since v5.4 (2020-09), the same commit that replaced
//                          its content → probably not the LaFerrari; kept here as a V12-idle shape control only.
//  Usage: node Scratchpad/AcousticLaFerrariReference.js [dir-with-the-.h-files] [--fetch]   (default dir: Scratchpad/LaFerrariReference/,
//         git-ignored; --fetch downloads the three headers there through api.github.com — they are third-party, never committed)
//  Writes Scratchpad/AcousticLaFerrariReference.log and Scratchpad/AcousticLaFerrari_RealVsRev2.png.
//
//  Method: the loops are exactly periodic, so 8 tiled repeats + one rectangular DFT give lines on a 6.0 Hz lattice with no
//  leakage. Two crank readings are reported for each loop, because no clip carries a tachometer:
//    R1 — the project's own cutting rule (one loop = one 720° cycle = 12 firings) → crank = 2 / loop length;
//    R2 — the harmonic-series test (the largest lattice multiple whose series carries ≥ 90 % of the line energy).
//  The rev-2 synth is rendered from the shipped Tools/AudioEditor/index.html at both readings' rpm and read the same way.

const fs = require('fs'), path = require('path'), zlib = require('zlib');
const ROOT = path.resolve(__dirname, '..');
const REF_DIR = process.argv.slice(2).find(a => !a.startsWith('--')) || path.join(__dirname, 'LaFerrariReference');
const RATE = 48000;

// ------------------------------------------------------------ DSP from the shipped editor -------------------------------------------
const html = fs.readFileSync(path.join(ROOT, 'Tools/AudioEditor/index.html'), 'utf8');
const dspText = html.slice(html.indexOf('<script id="dsp" type="text/plain">') + '<script id="dsp" type="text/plain">'.length, html.indexOf('</script>', html.indexOf('<script id="dsp"')));
const D = (() => { const module = { exports: {} }; new Function('module', 'exports', dspText)(module, module.exports); return module.exports; })();
const CARS = {}; for (const m of html.matchAll(/<script type="text\/toml" data-car="([^"]+)">([\s\S]*?)<\/script>/g)) CARS[m[1]] = D.parseToml(m[2]);

const lines = [];
function out(text) { lines.push(text); console.log(text); }

// ------------------------------------------------------------ reference clips ------------------------------------------------------
function parseHeader(name)
{
    const file = path.join(REF_DIR, name + '.h');
    if (!fs.existsSync(file)) return null;
    const t = fs.readFileSync(file, 'utf8');
    const rate = +(/SampleRate\s*=\s*(\d+)/i.exec(t)[1]);
    const body = t.slice(t.indexOf('{') + 1, t.lastIndexOf('}'));
    const v = body.split(/[\s,]+/).filter(s => /^-?\d+$/.test(s)).map(Number);
    return { rate, x: Float64Array.from(v, a => a / 128) };
}

// ------------------------------------------------------------ maths ----------------------------------------------------------------
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
// exact line spectrum of a seamless loop: tile it `reps` times, rectangular window → lines sit on multiples of rate/loopLength
function loopLines(x, rate, reps)
{
    const n = x.length * reps, size = 1 << Math.ceil(Math.log2(n));
    const re = new Float64Array(size), im = new Float64Array(size);
    for (let r = 0; r < reps; ++r) re.set(x, r * x.length);
    fft(re, im);
    const latticeHz = rate / x.length, binHz = rate / size, result = [];
    for (let m = 1; m * latticeHz < rate / 2 - latticeHz; ++m)
    {
        const k = Math.round(m * latticeHz / binHz); let best = -999;
        for (let j = k - 2; j <= k + 2; ++j) best = Math.max(best, 20 * Math.log10(2 * Math.hypot(re[j], im[j]) / n + 1e-12));
        result.push({ m, hz: m * latticeHz, db: best });
    }
    return { latticeHz, lines: result };
}
// which multiple of the lattice is the crank? the candidate whose harmonic series carries the most line energy (lines within 30 dB of the top)
function crankFromLines(ll)
{
    const top = Math.max(...ll.lines.map(l => l.db));
    const strong = ll.lines.filter(l => l.db > top - 30);
    const total = strong.reduce((s, l) => s + Math.pow(10, l.db / 10), 0);
    const candidates = [];
    for (let m0 = 1; m0 <= 12; ++m0)
    {
        const on = strong.filter(l => l.m % m0 === 0).reduce((s, l) => s + Math.pow(10, l.db / 10), 0);
        candidates.push({ m0, hz: m0 * ll.latticeHz, share: on / total });
    }
    // the largest m0 that still explains ≥ 90 % (a series of f0 also "explains" f0/2, f0/3 … trivially)
    let pick = candidates[0]; for (const c of candidates) if (c.share >= 0.9) pick = c;
    return { pick, candidates, top };
}
function hannSpectrumDb(x, start, len, size, rate)
{
    const re = new Float64Array(size), im = new Float64Array(size); let ws = 0;
    for (let i = 0; i < len; ++i) { const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / len); re[i] = (x[start + i] || 0) * w; ws += w; }
    fft(re, im);
    const db = new Float64Array(size / 2); for (let k = 0; k < size / 2; ++k) db[k] = 20 * Math.log10(2 * Math.hypot(re[k], im[k]) / ws + 1e-12);
    return db;
}
function orderSheet(db, size, rate, crankHz, maxOrder)
{
    const sheet = {}; let top = -999;
    for (let k = 2; k < Math.min(db.length, 8000 * size / rate); ++k) top = Math.max(top, db[k]);
    for (let o = 0.5; o <= maxOrder; o += 0.5)
    {
        const k = Math.round(o * crankHz * size / rate); let m = -999;
        for (let j = k - 2; j <= k + 2; ++j) if (j > 0 && j < db.length) m = Math.max(m, db[j]);
        sheet[o] = m - top;
    }
    return sheet;
}
function bandDb(db, size, rate, lo, hi) { let e = 0; for (let k = Math.max(1, Math.floor(lo * size / rate)); k < Math.min(db.length, hi * size / rate); ++k) e += Math.pow(10, db[k] / 10); return 10 * Math.log10(e + 1e-20); }
function centroidHz(db, size, rate, maxHz) { let num = 0, den = 0; for (let k = 2; k < Math.min(db.length, maxHz * size / rate); ++k) { const p = Math.pow(10, db[k] / 10); num += p * k * rate / size; den += p; } return num / den; }
function fmtOrders(sheet, list) { return list.map(o => o + ':' + (sheet[o] > -99 ? Math.round(sheet[o]) : '·')).join(' '); }

// ------------------------------------------------------------ synth render ---------------------------------------------------------
function renderSynth(rpm, load, seconds, pure)
{
    const s = D.structureFromToml(CARS.FerrariLaFerrari);
    const ig = new D.AcousticIntegrator(RATE, s, 0x5EED1234); ig.rpmS = rpm; if (pure) ig.pureTone = true;
    const total = Math.floor(seconds * RATE), L = new Float64Array(total), R = new Float64Array(total);
    for (let d = 0; d < total; d += 128) { const n = Math.min(128, total - d); ig.assignDemand(rpm, load, load, 0); ig.render(L.subarray(d, d + n), R.subarray(d, d + n), n); }
    const M = new Float64Array(total); for (let i = 0; i < total; ++i) M[i] = 0.5 * (L[i] + R[i]);
    return { L, R, M };
}

// ------------------------------------------------------------ PNG (8-bit palette) --------------------------------------------------
function crc32Sheet() { const t = new Uint32Array(256); for (let n = 0; n < 256; ++n) { let c = n; for (let k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; }
const CRC = crc32Sheet();
function crc32(buf) { let c = 0xFFFFFFFF; for (let i = 0; i < buf.length; ++i) c = CRC[(c ^ buf[i]) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }
function pngChunk(type, payload) { const len = Buffer.alloc(4); len.writeUInt32BE(payload.length); const td = Buffer.concat([Buffer.from(type, 'ascii'), payload]); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td)); return Buffer.concat([len, td, crc]); }
function writePng(file, width, height, indices, palette)
{
    const raw = Buffer.alloc((width + 1) * height);
    for (let y = 0; y < height; ++y) { raw[y * (width + 1)] = 0; indices.copy(raw, y * (width + 1) + 1, y * width, (y + 1) * width); }
    const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4); ihdr[8] = 8; ihdr[9] = 3;
    fs.writeFileSync(file, Buffer.concat([Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]), pngChunk('IHDR', ihdr), pngChunk('PLTE', palette), pngChunk('IDAT', zlib.deflateSync(raw, { level: 9 })), pngChunk('IEND', Buffer.alloc(0))]));
}
const PALETTE = Buffer.alloc(256 * 3);
for (let i = 0; i < 250; ++i) { const t = i / 249; const r = Math.min(1, Math.max(0, 1.6 * t - 0.1)), g = Math.max(0, Math.min(1, 2.2 * t - 1.1)), b = t < 0.5 ? 0.15 + 1.2 * t : Math.max(0, 2.0 - 2.4 * t); PALETTE[i * 3] = Math.round(255 * r); PALETTE[i * 3 + 1] = Math.round(255 * g); PALETTE[i * 3 + 2] = Math.round(255 * b); }
PALETTE.set([0x0A, 0x0A, 0x0A], 250 * 3); PALETTE.set([0xF0, 0xF0, 0xF0], 251 * 3); PALETTE.set([0x6C, 0x77, 0xFF], 252 * 3); PALETTE.set([0x88, 0x88, 0x88], 253 * 3); PALETTE.set([0xF5, 0x9E, 0x0B], 254 * 3);
const INK_BACKGROUND = 250, INK_WHITE = 251, INK_BLUE = 252, INK_GREY = 253, INK_AMBER = 254;
// 3×5 glyphs for the labels
const GLYPHS = { '0': '111101101101111', '1': '010110010010111', '2': '111001111100111', '3': '111001111001111', '4': '101101111001001', '5': '111100111001111', '6': '111100111101111', '7': '111001001001001', '8': '111101111101111', '9': '111101111001111', '.': '000000000000010', '-': '000000111000000', ' ': '000000000000000', 'H': '101101111101101', 'z': '000111010100111', 'R': '110101110101101', 'E': '111100110100111', 'A': '010101111101101', 'L': '100100100100111', 'V': '101101101101010', '2': '111001111100111', 'S': '111100111001111', 'Y': '101101010010010', 'N': '101111111111101', 'T': '111010010010010', 'O': '111101101101111', 'D': '110101101101110', 'I': '111010010010111', 'M': '101111111101101', 'B': '110101110101110', 'k': '100101110101101', 'r': '000110100100100', 'p': '000110101110100', 'm': '000110111101101', 'd': '001011101101011', 'l': '010010010010001', 'o': '000010101101010', 'a': '000110011101011', 'e': '000010111100011', 'i': '010000010010010', 'n': '000110101101101', 'u': '000101101101011', 'f': '011010111010010', 'c': '000011100100011', 't': '010111010010001', 's': '000011110001110', 'h': '100110101101101', 'v': '000101101101010', '(': '010100100100010', ')': '010001001001010', '/': '001001010100100', ':': '000010000010000', 'x': '000101010010101', 'g': '000011101011110', 'y': '000101101011110', 'w': '000101101111101', 'b': '100110101101110', 'q': '000011101011001', '%': '101001010100101', '+': '000010111010000', 'C': '111100100100111', 'F': '111100110100100', 'P': '110101110100100', 'W': '101101111111101', 'G': '111100101101111', 'U': '101101101101111', 'J': '001001001101111', 'K': '101101110101101', '=': '000111000111000', 'X': '101101010101101', 'Z': '111001010100111', 'Q': '111101101111001', '½': '100101010011001', 'j': '001000001001110' };
function text(img, W, x, y, str, ink)
{
    let cx = x;
    for (const ch of str)
    {
        const g = GLYPHS[ch] || GLYPHS[' '];
        for (let r = 0; r < 5; ++r) for (let c = 0; c < 3; ++c) if (g[r * 3 + c] === '1') { const px = cx + c, py = y + r; if (px >= 0 && px < W && py >= 0) img[py * W + px] = ink; }
        cx += 4;
    }
}
// order diagram panel: one column per clip / render, rows = orders ½ … 24, cell brightness = level re the loudest order
function drawSheetPanel(img, W, x0, y0, w, h, sheets, labels, title)
{
    text(img, W, x0, y0 - 8, title, INK_WHITE);
    const orders = []; for (let o = 0.5; o <= 24; o += 0.5) orders.push(o);
    const colW = Math.floor((w - 30) / sheets.length), rowH = h / orders.length;
    for (let i = 0; i < orders.length; ++i)
    {
        const o = orders[i], y = y0 + Math.floor(i * rowH), yh = Math.max(1, Math.floor(rowH) - 1);
        if (Number.isInteger(o) && o % 3 === 0) text(img, W, x0, y + 1, String(o), o === 6 ? INK_AMBER : INK_GREY);
        for (let c = 0; c < sheets.length; ++c)
        {
            const v = sheets[c][o]; const t = v === undefined || v < -60 ? 0 : (v + 60) / 60;
            const idx = Math.max(0, Math.min(249, Math.round(249 * t)));
            for (let yy = 0; yy < yh; ++yy) for (let xx = 0; xx < colW - 2; ++xx) img[(y + yy) * W + x0 + 30 + c * colW + xx] = idx;
        }
    }
    for (let c = 0; c < sheets.length; ++c) text(img, W, x0 + 30 + c * colW, y0 + h + 3, labels[c], INK_WHITE);
    // amber tick at order 6 (firing order)
    const y6 = y0 + Math.floor(orders.indexOf(6) * rowH); for (let xx = 0; xx < w; ++xx) if (xx % 3 === 0) img[(y6 - 1) * W + x0 + xx] = INK_AMBER;
}

// ═══════════════════════════════════════════════════════════════ run ══════════════════════════════════════════════════════════════════
out('AcousticLaFerrariReference — ' + new Date().toISOString() + ' — real LaFerrari loops vs rev-2 synth (Tools/AudioEditor/index.html)');
out('reference dir ' + REF_DIR);
// --fetch: pull the three headers through the GitHub contents lookup (base64 JSON; curl ships with Windows 10+ and every Linux)
if (process.argv.includes('--fetch'))
{
    const { execFileSync } = require('child_process');
    fs.mkdirSync(REF_DIR, { recursive: true });
    for (const name of ['LaFerrariIdle', 'LaFerrariRev', 'LaFerrariStart'])
    {
        const url = 'https://api.github.com/repos/TheDIYGuy999/Rc_Engine_Sound_ESP32/contents/src/vehicles/sounds/' + name + '.h';
        const reply = JSON.parse(execFileSync('curl', ['-s', '-m', '60', '-H', 'Accept: application/vnd.github+json', url], { encoding: 'utf8' }));
        if (!reply.content) { out('fetch failed for ' + name + ': ' + (reply.message || 'no content')); process.exit(2); }
        fs.writeFileSync(path.join(REF_DIR, name + '.h'), Buffer.from(reply.content, 'base64'));
        out('fetched ' + name + '.h (' + reply.size + ' bytes)');
    }
}
const idle = parseHeader('LaFerrariIdle'), rev = parseHeader('LaFerrariRev'), start = parseHeader('LaFerrariStart');
if (!idle || !rev) { out('reference clips missing — run with --fetch, or copy src/vehicles/sounds/LaFerrari{Idle,Rev,Start}.h from github.com/TheDIYGuy999/Rc_Engine_Sound_ESP32 into ' + REF_DIR + ' (third-party clips, git-ignored, never committed)'); process.exit(2); }

const results = {};
const READ = {};   // per loop: { R1: {crankHz, rpm}, R2: {crankHz, rpm} }
function circularAcf(x)
{
    const n = x.length; let mean = 0; for (const v of x) mean += v; mean /= n;
    const y = Float64Array.from(x, v => v - mean); let e = 0; for (const v of y) e += v * v;
    return lag => { let s = 0; for (let i = 0; i < n; ++i) s += y[i] * y[(i + lag) % n]; return s / e; };
}
for (const [name, clip] of [['idle', idle], ['rev', rev]])
{
    const n = clip.x.length, loopS = n / clip.rate;
    let rms = 0; for (const v of clip.x) rms += v * v; rms = 10 * Math.log10(rms / n);
    out('\n[' + name + ' loop] ' + n + ' samples @ ' + clip.rate + ' Hz = ' + (1000 * loopS).toFixed(2) + ' ms · rms ' + rms.toFixed(1) + ' dBFS');
    const ll = loopLines(clip.x, clip.rate, 8);
    const { pick, candidates, top } = crankFromLines(ll);
    const loudest = ll.lines.reduce((a, l) => l.db > a.db ? l : a);
    out('  line lattice ' + ll.latticeHz.toFixed(3) + ' Hz; loudest line ' + top.toFixed(1) + ' dBFS at ' + loudest.hz.toFixed(1) + ' Hz');
    out('  lines within 26 dB of the top [Hz:dB]: ' + ll.lines.filter(l => l.db > top - 26).map(l => l.hz.toFixed(0) + ':' + Math.round(l.db - top)).join(' '));
    const acf = circularAcf(clip.x);
    out('  circular ACF at loop/k: ' + [2, 3, 4, 6, 9, 12, 18].map(k => 'k=' + k + ' (' + (1000 * loopS / k).toFixed(1) + ' ms) ' + acf(Math.round(n / k)).toFixed(2)).join(' · ') + '  → the sound\'s own period is where r ≈ 0.9; under R2 that lag is one crank revolution (idle: 55.5 ms; rev: 27.7 ms), under R1 it is 240° / 120° — no engine period');
    out('  harmonic-series test (share of the line energy within 30 dB of the top on each candidate fundamental): ' + candidates.filter(c => c.m0 <= 8).map(c => c.hz.toFixed(1) + ' Hz → ' + (100 * c.share).toFixed(0) + ' %').join(' · '));
    // R2 — engine reading: the shortest lag the sound repeats at (r ≥ 0.8) is one firing or one revolution of a V12; take the
    // interpretation whose crank series carries ≥ 90 % of the line energy (the bare series test cannot tell 12 / 18 / 36 Hz apart on
    // a loop this sparse — a series of f also "explains" f/2 and f/3).
    let shortest = 0; for (let lag = 20; lag < n / 2; ++lag) if (acf(lag) >= 0.8 && acf(lag) > acf(lag - 1) && acf(lag) >= acf(lag + 1)) { shortest = lag; break; }
    // a seamless loop only holds lattice multiples, so each interpretation snaps to the nearest lattice line before the series test
    const snap = hz => Math.max(1, Math.round(hz / ll.latticeHz)) * ll.latticeHz;
    const share = hz => { const c = candidates.find(c => c.m0 === Math.round(hz / ll.latticeHz)); return c ? c.share : 0; };
    const asFiring = shortest ? snap(clip.rate / shortest / 6) : 0, asRevolution = shortest ? snap(clip.rate / shortest) : 0;
    const engineCrank = shortest && clip.rate / shortest / 6 >= ll.latticeHz * 0.9 && share(asFiring) >= 0.9 ? asFiring : shortest && share(asRevolution) >= 0.9 ? asRevolution : pick.hz;
    const R1 = { crankHz: 2 / loopS, rpm: 120 / loopS }, R2 = { crankHz: engineCrank, rpm: 60 * engineCrank };
    READ[name] = { R1, R2 };
    out('  shortest lag with r ≥ 0.8: ' + (1000 * shortest / clip.rate).toFixed(2) + ' ms → as one firing of a V12: crank ' + (clip.rate / shortest / 6).toFixed(1) + ' Hz' + (clip.rate / shortest / 6 < ll.latticeHz * 0.9 ? ' (below the lattice — impossible)' : ' → lattice line ' + asFiring.toFixed(1) + ' Hz, series share ' + Math.round(100 * share(asFiring)) + ' %') + ' · as one revolution: crank ' + (clip.rate / shortest).toFixed(1) + ' Hz → lattice line ' + asRevolution.toFixed(1) + ' Hz, series share ' + Math.round(100 * share(asRevolution)) + ' %');
    out('  reading R1 (loop = one 720° cycle, the project\'s cutting rule): crank ' + R1.crankHz.toFixed(2) + ' Hz = ' + R1.rpm.toFixed(0) + ' rpm → loudest line = order ' + (loudest.hz / R1.crankHz).toFixed(1));
    out('  reading R2 (ACF period + series test, the engine reading):     crank ' + R2.crankHz.toFixed(2) + ' Hz = ' + R2.rpm.toFixed(0) + ' rpm → loudest line = order ' + (loudest.hz / R2.crankHz).toFixed(1));
    // Hann-window order sheets on the tiled loop (same reader the synth gets), under both readings
    const tiled = new Float64Array(n * 8); for (let i = 0; i < 8; ++i) tiled.set(clip.x, i * n);
    const size = 65536, db = hannSpectrumDb(tiled, 0, tiled.length, size, clip.rate);
    const b0 = bandDb(db, size, clip.rate, 20, 200);
    const bands = [[200, 500], [500, 1000], [1000, 2000], [2000, 4000], [4000, 8000]].map(([lo, hi]) => Math.round(bandDb(db, size, clip.rate, lo, hi) - b0));
    results[name] = { bands, centroid: centroidHz(db, size, clip.rate, 8000), b0, sheets: {} };
    for (const [tag, r] of [['R1', R1], ['R2', R2]])
    {
        const sheet = orderSheet(db, size, clip.rate, r.crankHz, 24);
        results[name].sheets[tag] = sheet;
        out('  orders under ' + tag + ' (' + r.rpm.toFixed(0) + ' rpm), integer, re top: ' + fmtOrders(sheet, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 21, 24]));
        const halfOnLattice = Math.abs(0.5 * r.crankHz / ll.latticeHz - Math.round(0.5 * r.crankHz / ll.latticeHz)) < 0.05;
        out('                                   half:            ' + fmtOrders(sheet, [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5, 10.5, 11.5, 12.5, 13.5]) + (halfOnLattice ? '   (on the loop lattice → a real measurement; per-line 8-bit floor ≈ ' + Math.round(-49.9 - 10 * Math.log10(n / 2) - top) + ' dB re top)' : '   (OFF the loop lattice under this reading → a seamless loop cannot hold them; these are Hann skirts, not evidence — use the R1 row and the start clip for half-orders)'));
    }
    out('  bands re 20–200 Hz [200–500 500–1k 1k–2k 2k–4k 4k–8k]: ' + bands.join(' ') + ' dB · 20–200 band ' + b0.toFixed(1) + ' dBFS · centroid < 8 kHz ' + results[name].centroid.toFixed(0) + ' Hz · 8-bit floor per band ≈ −54 … −66 dBFS, so these bands are recorded signal (≥ 10 dB above the floor), not the medium');
    const dense = ll.lines.filter(l => l.hz < 1000 && l.db > top - 26);
    out('  line density: ' + dense.length + ' lattice lines within 26 dB of the top below 1 kHz, highest of them ' + Math.max(...dense.map(l => l.hz)).toFixed(0) + ' Hz');
}
if (start)
{
    // the settled part of the start clip (1.3–2.5 s): LaFerrari-labelled, not looped, not rate-shifted → cleanest band shape we have
    const s0 = Math.round(1.3 * start.rate), len = Math.round(1.2 * start.rate), size = 65536;
    const db = hannSpectrumDb(start.x, s0, len, size, start.rate);
    const b0 = bandDb(db, size, start.rate, 20, 200);
    const bands = [[200, 500], [500, 1000], [1000, 2000], [2000, 4000], [4000, 8000]].map(([lo, hi]) => Math.round(bandDb(db, size, start.rate, lo, hi) - b0));
    results.startSettled = { bands, centroid: centroidHz(db, size, start.rate, 8000) };
    let top = -999; for (let k = 2; k < 8000 * size / start.rate; ++k) top = Math.max(top, db[k]);
    let count = 0, highest = 0; for (let k = 3; k < 1000 * size / start.rate - 1; ++k) if (db[k] > db[k - 1] && db[k] >= db[k + 1] && db[k] > db[k - 2] && db[k] > db[k + 2] && db[k] > top - 26) { ++count; highest = k * start.rate / size; }
    out('\n[start clip, settled 1.3–2.5 s] bands re 20–200 Hz [200–500 500–1k 1k–2k 2k–4k 4k–8k]: ' + bands.join(' ') + ' dB · centroid < 8 kHz ' + results.startSettled.centroid.toFixed(0) + ' Hz · ' + count + ' peaks within 26 dB of the top below 1 kHz (highest ' + highest.toFixed(0) + ' Hz; the pitch glides 136 → 118 Hz in this window so lines smear)');
}

// synth at both crank readings of the idle loop
const synthSheets = {};
for (const tag of ['R1', 'R2'])
{
    const rpmRef = READ.idle[tag].rpm;
    out('\n[rev-2 synth] LaFerrari at ' + rpmRef.toFixed(0) + ' rpm (idle-loop reading ' + tag + '), full chain, L+R');
    for (const [label, load, pure] of [['load 0', 0.0, false], ['load 1', 1.0, false], ['pure load 1', 1.0, true]])
    {
        const { L, M } = renderSynth(rpmRef, load, 3.0, pure);
        const size = 65536, db = hannSpectrumDb(M, M.length - size, size, size, RATE);
        const sheet = orderSheet(db, size, RATE, rpmRef / 60, 24);
        const b0 = bandDb(db, size, RATE, 20, 200);
        synthSheets[tag + ' ' + label] = sheet;
        // one channel alone: the mono clips have to be compared with what one ear gets, and the parity pan puts the half-orders
        // anti-phase between L and R, so the L+R sum hides them
        const left = orderSheet(hannSpectrumDb(L, L.length - size, size, size, RATE), size, RATE, rpmRef / 60, 24);
        synthSheets[tag + ' ' + label + ' left'] = left;
        out('  ' + label.padEnd(12) + ' L+R  integer orders re top: ' + fmtOrders(sheet, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 21, 24]));
        out('  ' + ' '.repeat(12) + '      half orders re top:    ' + fmtOrders(sheet, [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5]));
        out('  ' + ' '.repeat(12) + ' left integer orders re top: ' + fmtOrders(left, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 18, 20, 21, 24]));
        out('  ' + ' '.repeat(12) + '      half orders re top:    ' + fmtOrders(left, [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5]) + '   ← what one ear / one speaker gets');
        let top = -999; for (let k = 2; k < 8000 * size / RATE; ++k) top = Math.max(top, db[k]);
        let count = 0, highest = 0; for (let k = 3; k < 1000 * size / RATE - 1; ++k) if (db[k] > db[k - 1] && db[k] >= db[k + 1] && db[k] > db[k - 2] && db[k] > db[k + 2] && db[k] > top - 26) { ++count; highest = k * RATE / size; }
        out('  ' + ' '.repeat(12) + ' bands re 20–200 Hz: ' + [[200, 500], [500, 1000], [1000, 2000], [2000, 4000], [4000, 8000]].map(([lo, hi]) => Math.round(bandDb(db, size, RATE, lo, hi) - b0)).join(' ') + ' dB · centroid < 8 kHz ' + centroidHz(db, size, RATE, 8000).toFixed(0) + ' Hz · ' + count + ' peaks within 26 dB of the top below 1 kHz (highest ' + highest.toFixed(0) + ' Hz)');
    }
}

// distance: mean |Δ| over integer orders 1 … 14 (both sheets re their own top) and the half-order excess, real vs synth under the same reading
function distance(a, b, list) { let s = 0, n = 0; for (const o of list) if (a[o] > -70 && b[o] > -70) { s += Math.abs(a[o] - b[o]); ++n; } return s / n; }
const ints = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14], halves = [0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5];
out('\n[distance] real loops vs rev-2 under the same crank reading (mean |Δ| over integer orders 1–14, each sheet re its own top; half-order means where the loop can hold them)');
for (const tag of ['R1', 'R2'])
    for (const name of ['idle', 'rev'])
        for (const label of ['load 0', 'load 1'])
        {
            const real = results[name].sheets[tag], synth = synthSheets[tag + ' ' + label];
            const realHalf = halves.reduce((s, o) => s + Math.max(real[o], -65), 0) / halves.length, synthHalf = halves.reduce((s, o) => s + synth[o], 0) / halves.length;
            const left = synthSheets[tag + ' ' + label + ' left'], leftHalf = halves.reduce((s, o) => s + left[o], 0) / halves.length;
            const halfNote = tag === 'R1' ? 'half-orders (on-lattice) real ' + realHalf.toFixed(0) + ' dB vs synth L+R ' + synthHalf.toFixed(0) + ' / left ' + leftHalf.toFixed(0) + ' dB' : 'half-orders: real off-lattice under R2 (none by construction) vs synth L+R ' + synthHalf.toFixed(0) + ' / left ' + leftHalf.toFixed(0) + ' dB mean (left 3½ at ' + Math.round(left[3.5]) + ')';
            out('  ' + tag + ' ' + READ.idle[tag].rpm.toFixed(0) + ' rpm · real ' + name.padEnd(4) + ' vs rev-2 ' + label.padEnd(7) + ': mean |Δ| integer orders L+R ' + distance(real, synth, ints).toFixed(1) + ' / left ' + distance(real, left, ints).toFixed(1) + ' dB · ' + halfNote + ' · bands real ' + results[name].bands.join('/'));
        }

// start clip: harmonic-comb rpm track (informational — the loudest line per 100 ms and the best comb fundamental)
if (start)
{
    out('\n[start clip] ' + (start.x.length / start.rate).toFixed(2) + ' s — best harmonic-comb fundamental per 100 ms (8–60 Hz, k = 1 … 24) and the loudest harmonic');
    const win = 4096, hop = 2205, size = 16384;
    const track = [];
    for (let s = 0; s + win <= start.x.length; s += hop)
    {
        const db = hannSpectrumDb(start.x, s, win, size, start.rate);
        let best = 0, bestScore = -1e9;
        for (let f0 = 8; f0 <= 60; f0 += 0.1) { let sc = 0; for (let k = 1; k <= 24; ++k) { const b = Math.round(k * f0 * size / start.rate); let m = -999; for (let j = b - 1; j <= b + 1; ++j) m = Math.max(m, db[j]); sc += Math.pow(10, m / 10) / k; } if (sc > bestScore) { bestScore = sc; best = f0; } }
        let lk = 0, lm = -999; for (let k = 1; k <= 24; ++k) { const b = Math.round(k * best * size / start.rate); if (db[b] > lm) { lm = db[b]; lk = k; } }
        let e = 0; for (let i = 0; i < win; ++i) e += start.x[s + i] * start.x[s + i];
        track.push('t=' + (s / start.rate).toFixed(1) + ' f0=' + best.toFixed(1) + ' loud k=' + lk + ' (' + (lk * best).toFixed(0) + ' Hz) rms ' + (10 * Math.log10(e / win)).toFixed(0));
    }
    out('  ' + track.join('\n  '));
    // peak ladders: in 0.4 s windows, every peak within 30 dB of the loudest below 700 Hz, as a ratio to the loudest line
    out('  peak ladders per 0.4 s window (ratio to the loudest line : dB) — integer/simple ratios = one crank series, no half-orders:');
    const w2 = Math.round(0.4 * start.rate), size2 = 32768;
    for (const t0 of [0.05, 0.55, 0.8, 1.1, 1.5, 1.9, 2.1])
    {
        const s0 = Math.round(t0 * start.rate); if (s0 + w2 > start.x.length) continue;
        const db = hannSpectrumDb(start.x, s0, w2, size2, start.rate);
        let top = -999, kTop = 0; for (let k = 2; k < 700 * size2 / start.rate; ++k) if (db[k] > top) { top = db[k]; kTop = k; }
        const fTop = kTop * start.rate / size2, ladder = [];
        for (let k = 3; k < 700 * size2 / start.rate - 1; ++k)
            if (db[k] > db[k - 1] && db[k] >= db[k + 1] && db[k] > db[k - 2] && db[k] > db[k + 2] && db[k] > top - 24) ladder.push((k * start.rate / size2 / fTop).toFixed(2) + ':' + Math.round(db[k] - top));
        out('    t=' + t0.toFixed(2) + '–' + (t0 + 0.4).toFixed(2) + ' s loudest ' + fTop.toFixed(0) + ' Hz (' + (fTop / 6 * 60).toFixed(0) + ' rpm if order 6, ' + (fTop / 3 * 60).toFixed(0) + ' if order 3): ' + ladder.join(' '));
    }
    // fixed-frequency ridges: 0.2 s windows, peaks 60–320 Hz within 22 dB of the loudest — a line that stays put while the loudest glides is a resonance, not an order
    out('  ridge tracker, 0.2 s windows, peaks 60–320 Hz within 22 dB of the loudest [Hz:dB]:');
    const w3 = Math.round(0.2 * start.rate), size3 = 32768;
    for (let t0 = 0.5; t0 + 0.2 <= start.x.length / start.rate + 1e-9; t0 += 0.1)
    {
        const s0 = Math.round(t0 * start.rate), db = hannSpectrumDb(start.x, s0, w3, size3, start.rate);
        const lo = Math.floor(60 * size3 / start.rate), hi = Math.floor(320 * size3 / start.rate);
        let top = -999; for (let k = lo; k < hi; ++k) top = Math.max(top, db[k]);
        const pk = [];
        for (let k = lo; k < hi; ++k) { const a = db[k - 1], b = db[k], c = db[k + 1]; if (b > a && b >= c && b > top - 22 && b > db[k - 3] && b > db[k + 3]) pk.push(((k + 0.5 * (a - c) / (a - 2 * b + c)) * start.rate / size3).toFixed(0) + ':' + Math.round(b - top)); }
        out('    t=' + t0.toFixed(1) + ' ' + pk.join(' '));
    }
    out('  reading: 0–0.5 s cranking (compression thuds 26–37 Hz, 50 Hz hum, starter whine 1–2 kHz); catch at ≈ 0.5 s with the loudest line at 108–136 Hz; flare to 145–155 Hz at 0.9–1.3 s; then a smooth glide 141 → 117 Hz from 1.5 to 2.3 s (≈ −30 Hz/s = −300 rpm/s read as order 6), still falling toward the 108 Hz of the loops when the clip ends.');
    out('  companions that glide with it: 1/2 × (−16 dB at the flare — order 3 if the loudest is order 6), 3/2 × (−2 … −12 dB during 0.9–1.2 s, then gone — order 9) and 2 × (−10 — order 12); a weak 2/3 × line (−19 … −22 dB — order 4) that no firing train makes: mechanical or ancillary, flagged. From 1.5 s on the sound is one line with everything else ≥ 19 dB down.');
    out('  lines that stay put while the loudest glides = fixed ridges: ≈ 172–188 Hz (−2 … −17 dB from 0.8 to 1.3 s) and ≈ 207–223 Hz (present from the catch to 2.0 s at −5 … −22 dB, peaking at −2 … 0 dB when the 3/2 line sweeps through it at 0.9–1.0 s). Nothing within 25 dB above 300 Hz after the catch; the dense ladders in the fast-glide windows are smear of the same few lines.');
}

// image: real loops under both readings next to rev 2 at the matching rpm
{
    const W = 760, H = 430, img = Buffer.alloc(W * H, INK_BACKGROUND);
    text(img, W, 10, 6, 'LaFerrari order sheets  rows = crank orders 1/2 .. 24 (6 = V12 firing order, amber)  cell = level re loudest order, 60 dB range', INK_WHITE);
    const r1 = READ.idle.R1.rpm.toFixed(0), r2 = READ.idle.R2.rpm.toFixed(0);
    drawSheetPanel(img, W, 10, 40, 740, 340,
        [results.idle.sheets.R1, results.rev.sheets.R1, synthSheets['R1 load 0'], results.idle.sheets.R2, results.rev.sheets.R2, synthSheets['R2 load 0'], synthSheets['R2 load 0 left']],
        ['REAL idle R1 ' + r1, 'REAL rev R1', 'rev2 ' + r1 + ' L+R', 'REAL idle R2 ' + r2, 'REAL rev R2', 'rev2 ' + r2 + ' L+R', 'rev2 ' + r2 + ' left'],
        'RC sample loops (R1 = loop is one 720 deg cycle, R2 = series test) vs rev-2 synth at the matching rpm');
    text(img, W, 10, H - 14, 'Scratchpad/AcousticLaFerrariReference.js  clips: TheDIYGuy999/Rc_Engine_Sound_ESP32 LaFerrari{Idle,Rev}.h 22050 Hz 8-bit loops (idle labelled Jaguar XJS V12)', INK_GREY);
    writePng(path.join(__dirname, 'AcousticLaFerrari_RealVsRev2.png'), W, H, img, PALETTE);
    out('\nwrote Scratchpad/AcousticLaFerrari_RealVsRev2.png');
}
fs.writeFileSync(path.join(__dirname, 'AcousticLaFerrariReference.log'), lines.join('\n') + '\n');
