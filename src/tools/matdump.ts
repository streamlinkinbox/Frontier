import { buildScene } from '../core/build';
import { SCENES } from '../app/scenes';
import { MAT_ORDER } from '../core/mesh';
import { MAT_DEFS } from '../gpu/materials';

const res = buildScene(SCENES[1].build(), 'high');
const tally = new Map<string, number>();
for (const v of res.builder.verts) {
  const name = MAT_ORDER[v.mat] ?? `?${v.mat}`;
  tally.set(name, (tally.get(name) ?? 0) + 1);
}
console.log('vertex materials:');
for (const [k, n] of [...tally].sort((a, b) => b[1] - a[1])) console.log(`  ${k.padEnd(16)} ${n}`);
console.log('\nindex space check:');
MAT_ORDER.forEach((m, i) => {
  const d = (MAT_DEFS as Record<string, { color: number[] }>)[m];
  console.log(`  ${String(i).padStart(2)} ${m.padEnd(16)} albedo ${d ? d.color.join(', ') : 'MISSING'}`);
});
