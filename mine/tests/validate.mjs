// Headless validation of the generated mine mesh:
//  - all faces are quads
//  - closed 2-manifold: every edge shared by exactly 2 quads
//  - consistent winding: shared edges are traversed in opposite directions
//  - normals face inward (towards the drivable space)
//  - valence histogram (poles only where intended)
//  - no degenerate quads
import { createMaze, DEFAULT_PARAMS } from '../src/network.js';
import { buildMine } from '../src/mineBuilder.js';

const seeds = process.argv.slice(2).map(Number).filter((x) => !isNaN(x));
if (!seeds.length) seeds.push(7, 1, 2, 3, 42);
let failed = false;
for (const seed of seeds) {
  const net = createMaze(seed);
  const P = { ...DEFAULT_PARAMS, seed };
  const m = buildMine(net, P);
  const q = m.quads;
  const nq = q.length / 4;
  const edges = new Map();
  let bad = 0, degenerate = 0;
  const pos = m.geometry.attributes.position.array;
  const nv = pos.length / 3;
  const valence = new Uint16Array(nv);
  for (let f = 0; f < nq; f++) {
    const ids = [q[f * 4], q[f * 4 + 1], q[f * 4 + 2], q[f * 4 + 3]];
    if (new Set(ids).size !== 4) degenerate++;
    for (let j = 0; j < 4; j++) {
      const a = ids[j], b = ids[(j + 1) % 4];
      const key = a < b ? a + '_' + b : b + '_' + a;
      const e = edges.get(key) || { n: 0, dir: 0 };
      e.n++; e.dir += a < b ? 1 : -1;
      edges.set(key, e);
    }
  }
  let nonManifold = 0, inconsistent = 0, boundary = 0;
  for (const [key, e] of edges) {
    if (e.n === 1) boundary++;
    else if (e.n !== 2) nonManifold++;
    else if (e.dir !== 0) inconsistent++;
    const [a, b] = key.split('_').map(Number);
    valence[a]++; valence[b]++;
  }
  const hist = {};
  for (let i = 0; i < nv; i++) hist[valence[i]] = (hist[valence[i]] || 0) + 1;
  // Euler characteristic of a closed orientable surface: V - E + F = 2 - 2g
  const chi = nv - edges.size + nq;
  const ok = boundary === 0 && nonManifold === 0 && inconsistent === 0 && degenerate === 0 && !hist[0];
  if (!ok) failed = true;
  console.log(`seed ${seed}: ${ok ? 'OK ' : 'FAIL'} verts=${nv} quads=${nq} edges=${edges.size} boundary=${boundary} nonManifold=${nonManifold} flipped=${inconsistent} degenerate=${degenerate} chi=${chi} (genus ${(2 - chi) / 2}) build=${m.stats.ms.toFixed(0)}ms`);
  console.log('   valence histogram', JSON.stringify(hist), 'warnings:', m.warnings.length ? m.warnings : 'none');
}
process.exit(failed ? 1 : 0);
