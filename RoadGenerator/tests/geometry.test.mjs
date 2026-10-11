// Geometry / topology checks for the road generator. Run: node tests/geometry.test.mjs
import assert from 'node:assert/strict';
import { sampleProject, makeRoad, makeNode, createEmptyProject, ROAD_CLASSES } from '../js/model.js';
import { buildNetwork } from '../js/roadgen.js';

let failures = 0;
const check = (name, fn) => {
  try { fn(); console.log('  ok  ', name); }
  catch (e) { failures++; console.log('  FAIL', name, '\n       ', e.message); }
};

// Segment intersection (proper crossing only, ignores shared endpoints).
function segCross(p1, p2, p3, p4) {
  const d = (a, b, c) => (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
  const d1 = d(p3, p4, p1), d2 = d(p3, p4, p2), d3 = d(p1, p2, p3), d4 = d(p1, p2, p4);
  return ((d1 > 1e-9 && d2 < -1e-9) || (d1 < -1e-9 && d2 > 1e-9)) && ((d3 > 1e-9 && d4 < -1e-9) || (d3 < -1e-9 && d4 > 1e-9));
}
function polygonSelfIntersects(pts) {
  const n = pts.length;
  for (let i = 0; i < n; i++) for (let j = i + 2; j < n; j++) {
    if (i === 0 && j === n - 1) continue;
    if (segCross(pts[i], pts[(i + 1) % n], pts[j], pts[(j + 1) % n])) return [i, j];
  }
  return null;
}

console.log('Sample district');
const project = sampleProject();
const net = buildNetwork(project);
const mainStats = net.main.build().stats;

check('builds without degenerate quads', () => {
  assert.equal(mainStats.degenerate, 0, `degenerate=${mainStats.degenerate}`);
});

check('quads dominate (>= 95% of faces are quads, triangles only in cores/caps)', () => {
  const ratio = mainStats.quads / (mainStats.quads + mainStats.tris);
  assert.ok(ratio >= 0.95, `quad ratio ${ratio.toFixed(3)}`);
});

check('junction level polygons are simple (no self-overlap)', () => {
  for (const rep of net.junctionReports) {
    for (let k = 0; k < 3; k++) {
      const bad = polygonSelfIntersects(rep.debugPolys[k]);
      assert.equal(bad, null, `node ${rep.name} level ${k} self-intersects at edges ${bad}`);
    }
  }
});

check('every junction is a single merged patch (one carriage fan per node)', () => {
  for (const rep of net.junctionReports) {
    assert.ok(rep.tris === rep.vertexCount, `${rep.name}: tris ${rep.tris} != ring vertices ${rep.vertexCount}`);
  }
});

check('approach roads terminate exactly on the junction cross-section (no gaps)', () => {
  // The strip start sample of each arm must equal the junction arm point (shared seam).
  for (const info of net.roadInfo.values()) {
    if (info.D0 > 0) assert.ok(Math.abs(info.s0 - info.D0) < 1e-9);
    assert.ok(info.s1 > info.s0, `road ${info.road.id} has empty strip`);
  }
});

check('4-way crossing has exactly 4 approaches merged into one centre', () => {
  const rep = net.junctionReports.find(r => r.name === 'Central crossing');
  assert.equal(rep.degree, 4);
  assert.equal(rep.vertexCount, 12);
});

console.log('\nStress: many random junctions');
{
  // Deterministic random star networks at varied angles; must stay simple and non-degenerate.
  let seed = 7;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
  let worstConcave = 0, bad = 0, runs = 0;
  for (let trial = 0; trial < 60; trial++) {
    const p = createEmptyProject();
    const arms = 2 + Math.floor(rnd() * 5);
    p.nodes.push(makeNode({ id: 'c', x: 0, z: 0 }));
    for (let i = 0; i < arms; i++) {
      const ang = (i / arms) * Math.PI * 2 + rnd() * 0.5;
      const far = 60 + rnd() * 40;
      p.nodes.push(makeNode({ id: 'a' + i, x: Math.cos(ang) * far, z: Math.sin(ang) * far }));
      const cls = ['local', 'collector', 'arterial', 'highway'][Math.floor(rnd() * 4)];
      p.roads.push(makeRoad({ id: 'r' + i, a: 'c', b: 'a' + i, cls, mid: [{ x: Math.cos(ang) * far * 0.5 + rnd() * 6, z: Math.sin(ang) * far * 0.5 }], seed: i }));
    }
    const nn = buildNetwork(p);
    runs++;
    for (const rep of nn.junctionReports) {
      for (let k = 0; k < 3; k++) if (polygonSelfIntersects(rep.debugPolys[k])) bad++;
    }
    const st = nn.main.build().stats;
    if (st.degenerate) bad++;
    worstConcave = Math.max(worstConcave, st.concave);
  }
  // Known limit: very dense nodes (6 arms, 60 deg apart, highway-width) can overlap at the outer
  // level. The generator reports these (report.overlap) instead of hiding them; the UI warns.
  check(`60 random star junctions: at most 1 crowded overlap (${bad} flagged)`, () => assert.ok(bad <= 1, `bad=${bad}`));
  console.log(`        worst concave-quad count in one network: ${worstConcave}`);
}

console.log('\nSummary:', failures === 0 ? 'all checks passed' : `${failures} failed`);
console.log('Mesh stats (sample district):', JSON.stringify(mainStats));
console.log('Junctions:', net.junctionReports.map(r => `${r.name}: deg ${r.degree}, quads ${r.quads}, tris ${r.tris}, concave ${r.concave}`).join(' | '));
process.exitCode = failures ? 1 : 0;
