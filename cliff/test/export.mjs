/*
 * Verifies the OBJ export path end-to-end with the browser APIs stubbed out.
 * Run with:  node test/export.mjs
 */
import { generateCliff } from '../src/build.js';
import { DEFAULT_PARAMS } from '../src/palette.js';

// --- browser stubs -------------------------------------------------------
let captured = null;
globalThis.document = {
  createElement: () => ({ href: '', download: '', click() {}, remove() {} }),
  body: { appendChild() {}, removeChild() {} },
};
globalThis.URL = {
  createObjectURL: (blob) => { captured = blob; return 'blob:fake'; },
  revokeObjectURL() {},
};

const { exportOBJ } = await import('../src/export.js');

const result = generateCliff({ ...DEFAULT_PARAMS, seed: 'export-test' });
const geos = [result.terrainGeo, result.cliffGeo];
const info = exportOBJ(geos, 'cliff.obj');

const text = await captured.text();
const lines = text.split('\n');
const vCount = lines.filter((l) => l.startsWith('v ')).length;
const vnCount = lines.filter((l) => l.startsWith('vn ')).length;
const fCount = lines.filter((l) => l.startsWith('f ')).length;

console.log(`reported: ${info.vertices} verts / ${info.faces} faces`);
console.log(`file:     ${vCount} v / ${vnCount} vn / ${fCount} f`);

let fail = 0;
const ok = (c, m) => { if (!c) { fail++; console.log('FAIL: ' + m); } };
ok(info.vertices === vCount, 'vertex count mismatch');
ok(info.faces === fCount, 'face count mismatch');
ok(vnCount === vCount, 'normal count mismatch');
ok(text.startsWith('# Frontier Cliff Forge'), 'missing header');
ok(/^v -?\d+\.\d+ -?\d+\.\d+ -?\d+\.\d+ [01]\.\d+ [01]\.\d+ [01]\.\d+$/m.test(lines.find((l) => l.startsWith('v '))),
  'vertex line is not "x y z r g b"');
ok(/^f \d+\/\/\d+ \d+\/\/\d+ \d+\/\/\d+$/.test(lines.find((l) => l.startsWith('f '))),
  'face line is not "v//vn v//vn v//vn"');
// every index must be in range
let maxIdx = 0;
for (const l of lines) {
  if (!l.startsWith('f ')) continue;
  for (const tok of l.slice(2).split(' ')) maxIdx = Math.max(maxIdx, parseInt(tok.split('//')[0], 10));
}
ok(maxIdx === vCount, `face index out of range (${maxIdx} > ${vCount})`);
// colours must be in 0..1
for (const l of lines) {
  if (!l.startsWith('v ')) continue;
  const p = l.split(' ');
  for (const c of p.slice(4)) ok(+c >= 0 && +c <= 1, 'colour channel out of range: ' + c);
  break;
}

console.log(`\n${fail === 0 ? 'OBJ EXPORT OK' : fail + ' CHECK(S) FAILED'}`);
process.exit(fail === 0 ? 0 : 1);
