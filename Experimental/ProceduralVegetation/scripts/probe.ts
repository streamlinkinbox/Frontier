import { PRESETS, cloneParams } from '../src/tree/params';
import { generateTree } from '../src/tree/generate';

const argv: string[] = (globalThis as unknown as { process: { argv: string[] } }).process.argv;
const only = argv[2];
const seeds = (argv[3] ?? '1,2,3').split(',').map(Number);

for (const preset of PRESETS) {
  if (only && !preset.name.toLowerCase().includes(only.toLowerCase())) continue;
  for (const seed of seeds) {
    const p = cloneParams(preset);
    p.seed = seed;
    const r = generateTree(p, { obstacleMeshes: false });
    const rep = r.report;
    const t = r.timings;
    console.log(
      `${preset.name.padEnd(22)} seed=${seed} stems=${r.summary.stems} lvl=${r.summary.stemsPerLevel.join('/')} roots=${r.summary.primaryRoots}/${r.summary.rootStems} obst=${r.summary.obstacles} V=${rep.vertices} F=${rep.faces} quads=${(rep.quadRatio * 100).toFixed(1)}% ` +
        `bnd=${rep.boundaryEdges} nm=${rep.nonManifoldEdges} inc=${rep.inconsistentEdges} deg=${rep.degenerateFaces} chi=${rep.eulerCharacteristic} comp=${rep.components} genus=${rep.genus} dropped=${r.stats.droppedStems} (roots ${r.stats.droppedRoots}) ${JSON.stringify(r.stats.dropReasons)} ` +
        `t=${t.skeleton.toFixed(0)}/${t.roots.toFixed(0)}/${t.mesh.toFixed(0)}/${t.validate.toFixed(0)}ms`,
    );
  }
}
