// ============================================================================
// Frontier SDF Terrain — headless bake CLI (Node).
// Usage:
//   node bake.mjs <graph.json> --res 128 --seconds 20 --obj out.obj [--raw out.raw]
// Builds the SDF from the node graph, runs the full erosion sim headless,
// exports mesh (.obj) and/or volumes (.raw = N + sdf f32 + attrA + attrB).
// ============================================================================
import { readFileSync, writeFileSync } from 'node:fs';
import { Volume } from './js/sdf.js';
import { buildFieldEval, collectFx, sanitizeGraph } from './js/nodes.js';
import { ErosionSim } from './js/erosion.js';
import { generateVolume } from './js/fieldgen.js';
import { surfaceNets, paintVertices, exportOBJ } from './js/mesh.js';

const args = process.argv.slice(2);
const get = (flag, def) => {
  const i = args.indexOf(flag);
  return i >= 0 && args[i + 1] ? args[i + 1] : def;
};

async function main() {
  const graphPath = args[0];
  if (!graphPath) {
    console.log('Usage: node bake.mjs <graph.json> --res 128 --seconds 20 --obj out.obj [--raw out.raw]');
    process.exit(1);
  }
  const res = parseInt(get('--res', '128'), 10);
  const seconds = parseFloat(get('--seconds', '20'));
  const objPath = get('--obj', null);
  const rawPath = get('--raw', null);

  const graph = sanitizeGraph(JSON.parse(readFileSync(graphPath, 'utf8')));
  const field = buildFieldEval(graph);
  const fx = collectFx(graph);
  const vol = new Volume(res, 2);
  console.log(`[bake] generating field @${res}^3 …`);
  await generateVolume(vol, field, { onProgress: (p) => process.stdout.write(`\r[gen] ${(p * 100).toFixed(0)}%`) });
  console.log('\n[bake] eroding …');
  const sim = new ErosionSim(vol);
  sim.setFx(fx);
  for (let i = 0; i < 12; i++) sim.cache.update(1 / 2); // prime caches
  sim.bake(seconds, 1 / 30, (p) => process.stdout.write(`\r[erode] ${(p * 100).toFixed(0)}% particles=${sim.hydro.alive}`));
  console.log(`\n[bake] carved=${vol.carvedVolume.toFixed(4)} deposited=${vol.depositedVolume.toFixed(4)} (world^3)`);

  if (objPath) {
    console.log('[bake] meshing …');
    const mesh = surfaceNets(vol);
    const colors = paintVertices(vol, mesh, fx.material);
    writeFileSync(objPath, exportOBJ(mesh, colors));
    console.log(`[bake] wrote ${objPath} (${mesh.positions.length / 3 | 0} verts)`);
  }
  if (rawPath) {
    const header = new Uint32Array([res]);
    const sdfBytes = Buffer.from(vol.sdf.buffer);
    const aBytes = Buffer.from(vol.attrA.buffer);
    const bBytes = Buffer.from(vol.attrB.buffer);
    writeFileSync(rawPath, Buffer.concat([Buffer.from(header.buffer), sdfBytes, aBytes, bBytes]));
    console.log(`[bake] wrote ${rawPath}`);
  }
}

main().catch((e) => { console.error(e); process.exit(1); });
