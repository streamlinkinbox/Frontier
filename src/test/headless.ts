// headless pipeline smoke test (bundled by esbuild, run in node)
import { defaultGraph } from '../graph/graph';
import { cookGraph } from '../worker/eval';

const g = defaultGraph();
g.domain.res = 96;
g.domain.size = [512, 164, 512];

const t0 = Date.now();
const r = cookGraph(g, (f, s) => { if (Math.random() < 0.05) console.log(`  ${(f * 100) | 0}% ${s}`); }, () => false);
if (!r) { console.error('COOK RETURNED NULL'); process.exit(1); }
const m = r.mesh!;
let nan = 0;
for (let i = 0; i < m.positions.length; i++) if (!Number.isFinite(m.positions[i])) nan++;
let minY = Infinity, maxY = -Infinity;
for (let i = 1; i < m.positions.length; i += 3) { minY = Math.min(minY, m.positions[i]); maxY = Math.max(maxY, m.positions[i]); }
console.log(JSON.stringify({
  ms: Date.now() - t0,
  tris: m.triangleCount,
  verts: m.positions.length / 3,
  nan,
  yRange: [Number(minY.toFixed(1)), Number(maxY.toFixed(1))],
  splatSets: r.splat?.textures.length ?? 0,
  layers: r.splat?.layers.map((l) => l.name) ?? [],
  stats: r.stats,
}, null, 2));
if (nan > 0 || m.triangleCount === 0) { console.error('BAD MESH'); process.exit(1); }
console.log('PIPELINE OK');
