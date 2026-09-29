// ASCII visualisation of erosion results (rivers / cliffs / deposition)
import { makeHeightField } from '../core/heightfield';
import { ridgedMultifractal2, fbm2 } from '../core/noise';
import { hydraulicErosion } from '../core/erosion/hydraulic';
import { thermalErosion, strataHardness } from '../core/erosion/thermal';
import { alluvialDeposition } from '../core/erosion/alluvial';
import { computeFlow, fillPits, accumulationMask } from '../core/erosion/flow';
import { slopeAt } from '../core/heightfield';

const N = 128;
const h = 4;
const hf = makeHeightField(N, N, h);
for (let z = 0; z < N; z++) {
  for (let x = 0; x < N; x++) {
    const r = ridgedMultifractal2(x / 90, z / 90, { octaves: 5, lacunarity: 2.1, gain: 0.55, frequency: 1, seed: 42 }, 1, 2.2);
    const d = fbm2(x / 200, z / 200, { octaves: 3, lacunarity: 2, gain: 0.5, frequency: 1, seed: 7 });
    hf.data[z * N + x] = 30 + r * 150 + d * 30;
  }
}

const shade = ' .:-=+*#%@';
function dump(H: Float32Array, title: string, overlay?: Float32Array) {
  console.log(`\n=== ${title} ===`);
  let mn = Infinity, mx = -Infinity;
  for (let i = 0; i < H.length; i++) { mn = Math.min(mn, H[i]); mx = Math.max(mx, H[i]); }
  for (let z = 0; z < N; z += 2) {
    let line = '';
    for (let x = 0; x < N; x += 1) {
      const t = (H[z * N + x] - mn) / (mx - mn || 1);
      let c = shade[Math.min(9, Math.floor(t * 10))];
      if (overlay && overlay[z * N + x] > 0.62) c = 'R';   // river
      line += c;
    }
    console.log(line);
  }
  console.log(`height range ${mn.toFixed(1)}..${mx.toFixed(1)} m`);
}

dump(hf.data, 'base multifractal');

const hyd = hydraulicErosion(hf, {
  iterations: 30, rainfall: 1, erodibility: 0.6, capacityKc: 1.0, expM: 0.5,
  depositRate: 0.35, maxErodeStep: 0.6, channelWidth: 12, talusMix: 0.1,
}, () => {});
const flow = computeFlow(hf, fillPits(hyd));
dump(hyd.data, 'after hydraulic (R = river network)', accumulationMask(flow));

const hard = strataHardness(hyd, 22, 0.9, 5);
const therm = thermalErosion(hyd, { iterations: 40, talusAngle: 38, strength: 0.7, creep: 0.03, hardnessContrast: 0.9 }, hard, () => {});
dump(therm.data, 'after thermal (cliffs + talus)');

const alluv = alluvialDeposition(therm, { iterations: 12, supply: 0.6, slopeThreshold: 0.2, fanStrength: 0.8, maxDepositStep: 0.35, apron: 0.7 }, () => {});
dump(alluv.data, 'after alluvial (fans/floodplain)');

// slope stats: cliff fraction
let cliff = 0, flat = 0;
for (let z = 1; z < N - 1; z++) for (let x = 1; x < N - 1; x++) {
  const s = slopeAt(alluv, x, z);
  if (s > 1.0) cliff++;
  if (s < 0.15) flat++;
}
console.log(`cliff cells (slope>1): ${cliff}  flat cells (<0.15): ${flat} of ${(N - 2) * (N - 2)}`);
