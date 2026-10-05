// Boots the full editor shell (App.js → Viewport.js → Gizmo.js) against stubbed DOM/three and exercises the UI.
// This is the regression net for module-evaluation order, missing element ids and handler wiring — the class of bug
// that a geometry test cannot see. Run with: node tools/boot-test.mjs
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { installDom, installThreeStub } from './dom-stub.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');

let failures = 0;
const check = (name, condition, detail = '') => {
  if (condition) console.log(`  ok   ${name}`);
  else {
    console.log(`  FAIL ${name} ${detail}`);
    failures++;
  }
};

// every id the markup declares, so a typo in either file shows up here
const html = readFileSync(resolve(root, 'index.html'), 'utf8');
const ids = [...html.matchAll(/id="([A-Za-z0-9_]+)"/g)].map((m) => m[1]);

installThreeStub();
const dom = installDom(ids);

console.log('\n— boot —');
let bootError = null;
try {
  await import('../src/App.js');
} catch (error) {
  bootError = error;
}
check('module evaluates without throwing', !bootError, bootError ? `${bootError.name}: ${bootError.message}` : '');
if (bootError) {
  console.log(bootError.stack);
  process.exit(1);
}

const el = dom.el;
check('loading overlay dismissed', el('Loading').hidden === true);
check('status updated past "Starting up"', !/Starting up/.test(el('Status').textContent), `"${el('Status').textContent}"`);
check('status reports merged junctions', /junction/i.test(el('Status').textContent), `"${el('Status').textContent}"`);
check('triangle readout populated', /triangles/.test(el('TriangleCount').textContent) && !/^—/.test(el('TriangleCount').textContent), `"${el('TriangleCount').textContent}"`);
check('no build failure banner', el('Failure').hidden !== false);
check('outliner lists the demo corridors', el('CorridorList').children.length === 6, `${el('CorridorList').children.length} rows`);
check('census shows corridors', el('CensusCorridors').textContent === '6', el('CensusCorridors').textContent);
check('census shows junctions', Number(el('CensusJunctions').textContent) >= 3, el('CensusJunctions').textContent);
check('inspector rendered sections', el('InspectorBody').children.length >= 4, `${el('InspectorBody').children.length} blocks`);
check('document name bound', el('DocumentName').value === 'Estuary crossing', el('DocumentName').value);

console.log('\n— render loop —');
const before = dom.pendingFrames();
dom.pump(1);
check('render loop re-queues itself', dom.pendingFrames() >= 1, `before=${before} after=${dom.pendingFrames()}`);

console.log('\n— toolbar —');
el('ToolRoad').click();
check('draw-road tool activates', el('ToolRoad').classList.contains('Active') && !el('ToolSelect').classList.contains('Active'));
el('ToolSelect').click();
check('select tool restores', el('ToolSelect').classList.contains('Active'));
el('ViewWire').click();
check('wireframe mode activates', el('ViewWire').classList.contains('Active'));
el('ViewShaded').click();
check('shaded mode restores', el('ViewShaded').classList.contains('Active'));
for (const id of ['Frame', 'ViewTop', 'ViewIso', 'AddRoad', 'AddBridge']) {
  let threw = null;
  try {
    el(id).click();
  } catch (error) {
    threw = error;
  }
  check(`${id} handler runs`, !threw, threw?.message);
}
el('ToolSelect').click();

console.log('\n— selection & inspector ─');
const firstRow = el('CorridorList').children[0];
firstRow.dispatch('click');
check('clicking a corridor selects it', el('SelectionName').textContent === 'Harbour Avenue', el('SelectionName').textContent);
check('inspector title follows selection', el('InspectorTitle').textContent === 'Harbour Avenue', el('InspectorTitle').textContent);
const sliders = el('InspectorBody').all((n) => n.tagName === 'INPUT' && n.attributes.type === undefined && n.type === 'range');
check('inspector exposes range controls', sliders.length > 6, `${sliders.length} sliders`);

// drive a slider and make sure a rebuild is scheduled and completes without throwing
const widthSlider = sliders[2];
widthSlider.value = String(Number(widthSlider.value) + 1);
let sliderError = null;
try {
  widthSlider.dispatch('input');
  dom.pump(2);
  await new Promise((r) => setTimeout(r, 400));
  dom.pump(2);
} catch (error) {
  sliderError = error;
}
check('slider edit rebuilds cleanly', !sliderError, sliderError?.message);
check('no failure banner after edit', el('Failure').hidden !== false);

console.log('\n— bridge inspector —');
const bridgeRow = el('CorridorList').children[4];
bridgeRow.dispatch('click');
check('bridge corridor selected', /Viaduct/.test(el('SelectionName').textContent), el('SelectionName').textContent);
const summaries = el('InspectorBody').all((n) => n.tagName === 'SUMMARY').map((n) => n.textContent);
check('bridge sections present', summaries.some((s) => s.startsWith('Bridge · deck')) && summaries.some((s) => s.startsWith('Bridge · substructure')), summaries.join(', '));
const selects = el('InspectorBody').all((n) => n.tagName === 'SELECT');
const typeSelect = selects.find((s) => s.children.some((o) => o.textContent === 'Warren truss'));
check('structure type select present', !!typeSelect);
if (typeSelect) {
  typeSelect.value = 'truss';
  let err = null;
  try {
    typeSelect.dispatch('change');
    dom.pump(2);
    await new Promise((r) => setTimeout(r, 400));
    dom.pump(2);
  } catch (error) {
    err = error;
  }
  check('switching structure type rebuilds cleanly', !err, err?.message);
}

console.log('\n— pointer & keyboard —');
const canvas = el('SceneCanvas');
let pointerError = null;
try {
  canvas.dispatch('pointerdown', { button: 0, clientX: 300, clientY: 300, pointerId: 1 });
  canvas.dispatch('pointermove', { button: 0, clientX: 340, clientY: 320, pointerId: 1 });
  canvas.dispatch('pointerup', { button: 0, clientX: 340, clientY: 320, pointerId: 1 });
  canvas.dispatch('pointerdown', { button: 2, clientX: 300, clientY: 300, pointerId: 1 });
  canvas.dispatch('pointermove', { button: 2, clientX: 280, clientY: 290, pointerId: 1 });
  canvas.dispatch('pointerup', { button: 2, clientX: 280, clientY: 290, pointerId: 1 });
  canvas.dispatch('wheel', { deltaY: 120 });
  canvas.dispatch('dblclick', {});
} catch (error) {
  pointerError = error;
}
check('orbit / pan / zoom handlers run', !pointerError, pointerError?.message);

let keyError = null;
try {
  for (const key of ['f', '2', 'Escape', '1', 'Enter']) dom.window.dispatch('keydown', { key });
  dom.window.dispatch('resize');
} catch (error) {
  keyError = error;
}
check('keyboard shortcuts run', !keyError, keyError?.message);

console.log('\n— draw a corridor —');
let drawError = null;
try {
  dom.window.dispatch('keydown', { key: '2' });
  for (const [x, y] of [[200, 200], [400, 260], [600, 300]]) {
    canvas.dispatch('pointerdown', { button: 0, clientX: x, clientY: y, pointerId: 2 });
    canvas.dispatch('pointerup', { button: 0, clientX: x, clientY: y, pointerId: 2 });
  }
  dom.window.dispatch('keydown', { key: 'Enter' });
  dom.pump(2);
  await new Promise((r) => setTimeout(r, 400));
  dom.pump(2);
} catch (error) {
  drawError = error;
}
check('draw tool places points and finishes', !drawError, drawError?.message);
check('new corridor appears in the outliner', el('CorridorList').children.length === 7, `${el('CorridorList').children.length} rows`);

console.log('\n— export —');
let exportError = null;
try {
  el('ExportObj').click();
  el('SaveDoc').click();
  el('NewDoc').click();
  dom.pump(2);
} catch (error) {
  exportError = error;
}
check('export / save / new run', !exportError, exportError?.message);
check('new document restores the demo', el('CorridorList').children.length === 6, `${el('CorridorList').children.length} rows`);

console.log(`\n${failures === 0 ? 'ALL CHECKS PASSED' : `${failures} CHECK(S) FAILED`}\n`);
process.exit(failures === 0 ? 0 : 1);
