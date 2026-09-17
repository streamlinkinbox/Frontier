/**
 * Headless verification: builds every roof style × tile system combination
 * and asserts the "no floating" connectivity check passes.
 * Run: npx tsx scripts/verify.ts
 */

// ---- minimal DOM stub for procedural canvas textures (node has no document) ----
const fakeCtx = () => ({
  fillStyle: '', strokeStyle: '', lineWidth: 1,
  fillRect: () => {}, stroke: () => {}, beginPath: () => {}, moveTo: () => {},
  bezierCurveTo: () => {}, putImageData: () => {},
  getImageData: (_x: number, _y: number, w: number, h: number) => ({
    data: new Uint8ClampedArray(w * h * 4).fill(240), width: w, height: h,
  }),
});
(globalThis as unknown as { document: unknown }).document = {
  createElement: () => ({ width: 256, height: 256, getContext: () => fakeCtx() }),
};

import { buildRoof } from '../src/lib/buildRoof';
import { DEFAULT_PARAMS, PRESETS, RoofParams, RoofStyle, TileSystem } from '../src/lib/types';

let failures = 0;
function scenario(name: string, patch: Partial<RoofParams>) {
  const params: RoofParams = { ...DEFAULT_PARAMS, ...patch };
  const { checks, stats } = buildRoof(params);
  const fails = checks.filter((c) => c.status === 'fail');
  const warns = checks.filter((c) => c.status === 'warn');
  const ok = fails.length === 0 && stats.tileCount > 50 && (params.showRafters ? stats.rafterCount > 4 : true);
  console.log(
    `${ok ? 'PASS' : 'FAIL'}  ${name}  | tiles=${stats.tileCount} rafters=${stats.rafterCount} ` +
    `area=${stats.tileArea.toFixed(1)}m² eave=${stats.eaveY.toFixed(2)}m ridge=${stats.topY.toFixed(2)}m` +
    (warns.length > 0 ? `  warns=[${warns.map((w) => w.label).join('; ')}]` : '') +
    (fails.length > 0 ? `  FAILS=[${fails.map((f) => f.detail).join('; ')}]` : ''),
  );
  if (!ok) failures++;
}

const styles: RoofStyle[] = ['kirizuma', 'yosemune', 'irimoya', 'hogyo'];
const tiles: TileSystem[] = ['hongawara', 'sangawara', 'modern'];

for (const style of styles) {
  scenario(`style=${style}`, { style });
  for (const tile of tiles) scenario(`style=${style} tile=${tile}`, { style, tile });
}
for (const pr of PRESETS) scenario(`preset=${pr.id}`, pr.patch);
// rotated plan (ridge along Z), narrow, wide
scenario('rotated W<D', { width: 4.6, depth: 7.2, style: 'irimoya' });
scenario('rotated W<D hip', { width: 4.2, depth: 6.8, style: 'yosemune' });
scenario('square plan hip', { width: 5.5, depth: 5.5, style: 'yosemune' });
// extremes
scenario('extreme curves', { style: 'irimoya', pitch: 0.9, sori: 0.45, cornerLift: 0.45, hipSori: 0.3, overhang: 1.6 });
scenario('extreme flat', { style: 'kirizuma', pitch: 0.2, sori: 0, cornerLift: 0, overhang: 0.3, tile: 'modern' });
scenario('karahafu', { style: 'kirizuma', karahafu: true, sori: 0.3, cornerLift: 0.25 });
scenario('no rafters/structure', { style: 'yosemune', showRafters: false, showStructure: false });

// walls-hidden → grounding check must downgrade to info, never fail
{
  const { checks } = buildRoof({ ...DEFAULT_PARAMS, showWalls: false });
  const g = checks.find((c) => c.id === 'ground');
  const ok = g?.status === 'info';
  console.log(`${ok ? 'PASS' : 'FAIL'}  walls-hidden grounding=${g?.status}`);
  if (!ok) failures++;
}

console.log(failures === 0 ? '\nALL SCENARIOS VERIFIED ✔' : `\n${failures} SCENARIO(S) FAILED ✘`);
process.exit(failures === 0 ? 0 : 1);
