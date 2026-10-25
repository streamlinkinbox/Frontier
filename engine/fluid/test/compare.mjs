// Before/after contact sheet: rows = shots, columns = foam_before_* | foam_after_* (half size).
//   node test/compare.mjs ford_close ford_trail beach rapids   -> test/out/foam_compare.png
import fs from 'node:fs';
import path from 'node:path';
import { PNG } from 'pngjs';

const out = path.join(path.dirname(new URL(import.meta.url).pathname), 'out');
const shots = process.argv.slice(2).length ? process.argv.slice(2) : ['ford_close', 'ford_trail', 'beach', 'rapids'];
const load = (f) => PNG.sync.read(fs.readFileSync(path.join(out, f)));
const first = load(`foam_before_${shots[0]}.png`);
const w = first.width >> 1, h = first.height >> 1, gap = 6, label = 22;
const W = w * 2 + gap * 3, H = shots.length * (h + gap) + gap + label;
const img = new PNG({ width: W, height: H });
img.data.fill(24);
for (let i = 3; i < img.data.length; i += 4) img.data[i] = 255;

// 5x7 bitmap glyphs for the two labels
const G = {
  B: ['11110', '10001', '10001', '11110', '10001', '10001', '11110'], E: ['11111', '10000', '10000', '11110', '10000', '10000', '11111'],
  F: ['11111', '10000', '10000', '11110', '10000', '10000', '10000'], O: ['01110', '10001', '10001', '10001', '10001', '10001', '01110'],
  R: ['11110', '10001', '10001', '11110', '10100', '10010', '10001'], A: ['01110', '10001', '10001', '11111', '10001', '10001', '10001'],
  T: ['11111', '00100', '00100', '00100', '00100', '00100', '00100'], ' ': ['00000', '00000', '00000', '00000', '00000', '00000', '00000'],
};
function text(str, x0, y0, s = 2) {
  [...str].forEach((ch, n) => G[ch].forEach((row, y) => [...row].forEach((b, x) => {
    if (b !== '1') return;
    for (let dy = 0; dy < s; dy++) for (let dx = 0; dx < s; dx++) {
      const o = ((y0 + y * s + dy) * W + x0 + (n * 6 + x) * s + dx) * 4;
      img.data[o] = img.data[o + 1] = img.data[o + 2] = 235;
    }
  })));
}
text('BEFORE', gap + (w >> 1) - 35, 5);
text('AFTER', gap * 2 + w + (w >> 1) - 29, 5);

shots.forEach((name, r) => {
  ['before', 'after'].forEach((tag, c) => {
    const src = load(`foam_${tag}_${name}.png`);
    const x0 = gap + c * (w + gap), y0 = label + gap + r * (h + gap);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) for (let k = 0; k < 3; k++) {
      const s = (a, b) => src.data[((2 * y + a) * src.width + 2 * x + b) * 4 + k];
      img.data[((y0 + y) * W + x0 + x) * 4 + k] = (s(0, 0) + s(0, 1) + s(1, 0) + s(1, 1) + 2) >> 2;
    }
  });
});
fs.writeFileSync(path.join(out, 'foam_compare.png'), PNG.sync.write(img));
console.log('wrote', path.join(out, 'foam_compare.png'), `${W}x${H}`);
