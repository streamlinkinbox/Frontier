// Runs the heightfield pipeline headlessly and prints statistics + an ASCII preview.
import { generateTerrain } from '../src/pipeline.js';
import { defaults, presets } from '../src/params.js';

const presetName = process.argv[2] || 'Alpine granite';
const res = Number(process.argv[3] || 256);
const params = { ...defaults, ...presets[presetName], resolution: res };
params.droplets = Math.round(params.droplets * (res * res) / (512 * 512));

let lastPhase = '';
const result = generateTerrain(params, (p) => { if (p.phase !== lastPhase) { lastPhase = p.phase; process.stdout.write(`  ${p.phase}\n`); } });
const { height, slope, deposit, flow, hardness, cavity, stats } = result;
const N = res;
const mean = (a) => a.reduce((s, v) => s + v, 0) / a.length;
const finite = height.every(Number.isFinite);
console.log(`preset=${presetName} N=${N} time=${stats.elapsedMs.toFixed(0)}ms finite=${finite}`);
console.log(`height min=${stats.min.toFixed(1)} max=${stats.max.toFixed(1)} mean=${mean(height).toFixed(1)}`);
let steep = 0; for (const s of slope) if (s > 1.2) steep++;
console.log(`cliff fraction (>50°)=${(steep / slope.length * 100).toFixed(1)}%  deposit mean=${mean(deposit).toFixed(3)} flow mean=${mean(flow).toFixed(3)} hardness mean=${mean(hardness).toFixed(3)} cavity[min,max]=${cavity.reduce((a,b)=>Math.min(a,b),1).toFixed(2)},${cavity.reduce((a,b)=>Math.max(a,b),-1).toFixed(2)}`);
const shades = ' .:-=+*#%@';
const step = Math.max(1, Math.floor(N / 64));
for (let j = 0; j < N; j += step * 2) {
  let line = '';
  for (let i = 0; i < N; i += step) {
    const t = (height[j * N + i] - stats.min) / (stats.max - stats.min);
    line += shades[Math.min(9, Math.floor(t * 9.99))];
  }
  console.log(line);
}
if (!finite) process.exit(1);
