/**
 * Headless verification: builds roof style × tile system × ornament × lantern
 * combinations and asserts the "no floating" connectivity check passes.
 * Run: npm run verify
 */

// ---- minimal DOM stub for procedural canvas textures (node has no document) ----
const fakeCtx = () => ({
  fillStyle: '', strokeStyle: '', lineWidth: 1, font: '', textAlign: '', textBaseline: '',
  fillRect: () => {}, fill: () => {}, stroke: () => {}, beginPath: () => {}, moveTo: () => {},
  lineTo: () => {}, bezierCurveTo: () => {}, quadraticCurveTo: () => {},
  fillText: () => {}, strokeText: () => {}, putImageData: () => {},
  ellipse: () => {}, arc: () => {}, save: () => {}, restore: () => {},
  translate: () => {}, scale: () => {},
  getImageData: (_x: number, _y: number, w: number, h: number) => ({
    data: new Uint8ClampedArray(w * h * 4).fill(240), width: w, height: h,
  }),
});
(globalThis as unknown as { document: unknown }).document = {
  createElement: () => ({ width: 256, height: 256, getContext: () => fakeCtx() }),
};

import { buildRoof } from '../src/lib/buildRoof';
import { DEFAULT_PARAMS, DEFAULT_LAMP_GROUP, PRESETS, RoofParams, RoofStyle, TileSystem, LampDesign, LampMount, STONE_DESIGNS } from '../src/lib/types';

let failures = 0;
let count = 0;
function scenario(name: string, patch: Partial<RoofParams>) {
  count++;
  const params: RoofParams = { ...DEFAULT_PARAMS, ...patch };
  const { checks, stats } = buildRoof(params);
  const fails = checks.filter((c) => c.status === 'fail');
  const warns = checks.filter((c) => c.status === 'warn');
  const ok = fails.length === 0 && stats.tileCount > 50 && (params.showRafters ? stats.rafterCount > 4 : true);
  console.log(
    `${ok ? 'PASS' : 'FAIL'}  ${name}  | tiles=${stats.tileCount} rafters=${stats.rafterCount} ` +
    `lamps=${stats.lamps} eave=${stats.eaveY.toFixed(2)}m ridge=${stats.topY.toFixed(2)}m` +
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
// ornaments
scenario('chiwen+beasts+dougong palace', { style: 'irimoya', ornament: 'chiwen', hipBeasts: true, beastCount: 9, dougong: true });
scenario('chiwen beasts hip', { style: 'yosemune', ornament: 'chiwen', hipBeasts: true, beastCount: 3, dougong: true });
scenario('beasts pyramid', { style: 'hogyo', ornament: 'chiwen', hipBeasts: true, beastCount: 7, dougong: true });
scenario('beasts on gable (no-op)', { style: 'kirizuma', hipBeasts: true, dougong: true });
// lantern sweep: every design × every mount (stone designs force garden placement)
const designs: LampDesign[] = [
  'chochin-tube', 'chochin-round', 'kaku-chochin', 'andon', 'kiriko', 'bonbori',
  'gongdeng', 'zoumadeng', 'chorong', 'akari', 'toro', 'kasuga-toro',
];
const mounts: LampMount[] = ['hanging', 'standing', 'stone'];
for (const design of designs) {
  for (const mount of mounts) {
    if (STONE_DESIGNS.includes(design) && mount === 'hanging') {
      scenario(`lamp ${design} @ ${mount} (forced garden)`, {
        style: 'irimoya',
        lamps: [{ ...DEFAULT_LAMP_GROUP, enabled: true, design, mount, count: 2, text: '祭酒' }],
      });
    } else {
      scenario(`lamp ${design} @ ${mount}`, {
        style: 'irimoya',
        lamps: [{ ...DEFAULT_LAMP_GROUP, enabled: true, design, mount, count: 2, text: '祭酒' }],
      });
    }
  }
}
// lantern stress: all groups, big counts, big sizes
scenario('lanterns all-groups max', {
  style: 'yosemune',
  lamps: [
    { ...DEFAULT_LAMP_GROUP, enabled: true, design: 'zoumadeng', mount: 'hanging', count: 8, size: 1.4, text: '祭' },
    { ...DEFAULT_LAMP_GROUP, enabled: true, design: 'kasuga-toro', mount: 'stone', count: 4, size: 1.3, text: '' },
    { ...DEFAULT_LAMP_GROUP, enabled: true, design: 'gongdeng', mount: 'standing', count: 5, size: 1.2, text: '酒' },
  ],
});
scenario('lanterns no lights', {
  style: 'kirizuma', lampLights: false,
  lamps: [{ ...DEFAULT_LAMP_GROUP, enabled: true, design: 'bonbori', mount: 'hanging', count: 4 }],
});

// walls-hidden → grounding check must downgrade to info, never fail
{
  count++;
  const { checks } = buildRoof({ ...DEFAULT_PARAMS, showWalls: false });
  const g = checks.find((c) => c.id === 'ground');
  const ok = g?.status === 'info';
  console.log(`${ok ? 'PASS' : 'FAIL'}  walls-hidden grounding=${g?.status}`);
  if (!ok) failures++;
}

console.log(failures === 0 ? `\nALL ${count} SCENARIOS VERIFIED ✔` : `\n${failures}/${count} SCENARIO(S) FAILED ✘`);
process.exit(failures === 0 ? 0 : 1);
