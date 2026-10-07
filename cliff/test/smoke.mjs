/*
 * Headless sanity check for the generator.  Run with:  node test/smoke.mjs
 * Verifies that every preset (and a few edge cases) produces finite, sane
 * geometry with the expected shape.
 */
import { generateCliff } from '../src/build.js';
import { PRESETS, DEFAULT_PARAMS } from '../src/palette.js';

let failures = 0;
const ok = (cond, msg) => {
  if (!cond) { failures++; console.log('   FAIL: ' + msg); }
};

function inspect(geo, label) {
  const pos = geo.attributes.position;
  const nor = geo.attributes.normal;
  const col = geo.attributes.color;
  let nan = 0, badNormal = 0, minC = Infinity, maxC = -Infinity;
  let minY = Infinity, maxY = -Infinity, maxX = -Infinity, minX = Infinity;
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i), y = pos.getY(i), z = pos.getZ(i);
    if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) { nan++; continue; }
    minY = Math.min(minY, y); maxY = Math.max(maxY, y);
    minX = Math.min(minX, x); maxX = Math.max(maxX, x);
    if (nor) {
      const l = Math.hypot(nor.getX(i), nor.getY(i), nor.getZ(i));
      if (!Number.isFinite(l) || Math.abs(l - 1) > 1e-3) badNormal++;
    }
    if (col) {
      const r = col.getX(i), g = col.getY(i), b = col.getZ(i);
      if (!Number.isFinite(r) || !Number.isFinite(g) || !Number.isFinite(b)) { nan++; continue; }
      minC = Math.min(minC, r, g, b); maxC = Math.max(maxC, r, g, b);
    }
  }
  console.log(`   ${label}: verts=${pos.count} tris=${pos.count / 3} y=[${minY.toFixed(2)},${maxY.toFixed(2)}] x=[${minX.toFixed(1)},${maxX.toFixed(1)}] col=[${minC.toFixed(3)},${maxC.toFixed(3)}]`);
  ok(nan === 0, `${label}: ${nan} non-finite values`);
  ok(badNormal === 0, `${label}: ${badNormal} non-unit normals`);
  ok(minC >= 0 && maxC < 6, `${label}: colour out of range`);
  return { minY, maxY, minX, maxX };
}

function run(label, params) {
  console.log(`\n== ${label} ==`);
  const t0 = Date.now();
  const r = generateCliff(params);
  console.log(`   generated in ${r.stats.ms}ms - benches=${r.stats.benches} rocks=${r.stats.rocks} tris=${r.stats.totalTris}`);
  ok(r.stats.benches >= 1, 'no benches');
  ok(r.stats.rocks > 0, 'no rocks placed');
  ok(r.stats.totalTris > 5000, 'suspiciously few triangles');

  const cliff = inspect(r.cliffGeo, 'cliff');
  const ground = inspect(r.terrainGeo, 'ground');

  // the pit must actually be excavated
  const expectedBottom = r.pit.yTop - params.pitDepth;
  ok(cliff.minY < expectedBottom + 0.5, `pit floor not deep enough (${cliff.minY.toFixed(2)} vs ${expectedBottom.toFixed(2)})`);
  ok(cliff.minY > expectedBottom - 6, 'pit floor too deep');
  // bench faces must be near-vertical somewhere
  ok(cliff.maxY > r.pit.yTop - 1, 'cliff does not reach the rim');
  // terrain must surround the pit
  ok(ground.maxX > params.size * 0.4, 'terrain too small');
  if (r.puddle) {
    // signedRadial > 0 == inside the pit outline
    ok(r.pit.signedRadial(r.puddle.x, r.puddle.z) > 0, 'puddle is not inside the pit');
  }
  return r;
}

// --- every preset ---------------------------------------------------------
for (const [id, preset] of Object.entries(PRESETS)) {
  run(`preset: ${id}`, { ...DEFAULT_PARAMS, ...preset.params, preset: id, seed: 'quarry-01' });
}

// --- determinism ----------------------------------------------------------
const a = generateCliff({ ...DEFAULT_PARAMS, seed: 'abc' });
const b = generateCliff({ ...DEFAULT_PARAMS, seed: 'abc' });
const c = generateCliff({ ...DEFAULT_PARAMS, seed: 'abd' });
const pa = a.cliffGeo.attributes.position.array;
const pb = b.cliffGeo.attributes.position.array;
const pc = c.cliffGeo.attributes.position.array;
console.log('\n== determinism ==');
ok(pa.length === pb.length && pa.every((v, i) => v === pb[i]), 'same seed produced different geometry');
ok(pa.length !== pc.length || !pa.every((v, i) => v === pc[i]), 'different seed produced identical geometry');

// --- edge cases -----------------------------------------------------------
run('no road / no puddle', { ...DEFAULT_PARAMS, road: false, puddle: false, seed: 'x1' });
run('deep narrow pit', { ...DEFAULT_PARAMS, pitRadius: 16, pitDepth: 40, benchHeight: 3, benchWidth: 2, seed: 'x2' });
run('shallow wide pit', { ...DEFAULT_PARAMS, pitRadius: 48, pitDepth: 8, benchHeight: 8, benchWidth: 9, seed: 'x3' });
run('max roughness', { ...DEFAULT_PARAMS, roughness: 2.0, bulge: 2.0, edgeFray: 1.2, rockDensity: 2.0, seed: 'x4' });
run('no rocks', { ...DEFAULT_PARAMS, rockDensity: 0, rockScale: 0.2, seed: 'x5' });

console.log(`\n${failures === 0 ? 'ALL CHECKS PASSED' : failures + ' CHECK(S) FAILED'}\n`);
process.exit(failures === 0 ? 0 : 1);
