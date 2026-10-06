// Boots the full editor shell (App.js → Viewport.js → Gizmo.js) against stubbed DOM/three and exercises the UI.
// This is the regression net for module-evaluation order, missing element ids and handler wiring — the class of bug
// that a geometry test cannot see. Run with: node tools/boot-test.mjs
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { canvasStats, installDom, installThreeStub } from './dom-stub.mjs';

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
check('outliner lists the demo corridors', el('CorridorList').children.length === 7, `${el('CorridorList').children.length} rows`);
check('census shows corridors', el('CensusCorridors').textContent === '7', el('CensusCorridors').textContent);
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
const bridgeRow = el('CorridorList').children[5];
bridgeRow.dispatch('click');
check('bridge corridor selected', /Viaduct/.test(el('SelectionName').textContent), el('SelectionName').textContent);
const summaries = el('InspectorBody').all((n) => n.tagName === 'SUMMARY').map((n) => n.textContent);
check('bridge sections present', summaries.some((s) => s.startsWith('Bridge · deck')) && summaries.some((s) => s.startsWith('Bridge · substructure')), summaries.join(', '));
const selects = el('InspectorBody').all((n) => n.tagName === 'SELECT');
const typeSelect = selects.find((s) => s.children.some((o) => /Warren truss/.test(o.textContent)));
check('structure type select present', !!typeSelect);
check('every bridge family is offered', typeSelect && typeSelect.children.length >= 11, `${typeSelect?.children.length} options`);
for (const want of ['Tied (bowstring) arch', 'Pratt through truss', 'Masonry viaduct', 'Haunched cantilever', 'Solid slab']) {
  check(`  ${want} listed`, !!typeSelect && typeSelect.children.some((o) => o.textContent === want));
}
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

console.log('\n— clicking a junction hub grabs the intersection —');
{
  const three = await import('three');
  const canvas = el('SceneCanvas');
  three.pickControl.filter = (o) => o.userData?.junction === true;
  canvas.dispatch('pointerdown', { button: 0, clientX: 300, clientY: 300, pointerId: 5 });
  check('pointer-down on a hub selects the junction', !!dom.window.__roadworks.state.selection.junctionId);
  check('status explains the grab', /Junction/i.test(el('Status').textContent), el('Status').textContent);
  canvas.dispatch('pointermove', { button: 0, clientX: 330, clientY: 280, pointerId: 5 });
  canvas.dispatch('pointerup', { button: 0, clientX: 330, clientY: 280, pointerId: 5 });
  check('no failure after dragging a junction', el('Failure').hidden !== false, el('Failure').textContent);
  three.pickControl.filter = null;
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
check('new corridor appears in the outliner', el('CorridorList').children.length === 8, `${el('CorridorList').children.length} rows`);

console.log('\n— procedural textures —');
check('texture generators ran', canvasStats.calls > 2000, `${canvasStats.calls} canvas ops`);
check('no non-finite coordinate in any pattern', canvasStats.bad.length === 0, canvasStats.bad.slice(0, 3).join(' | '));
{
  const api = dom.window.__roadworks;
  const names = [...api.viewport.meshes.keys()];
  const paving = names.filter((n) => n.startsWith('pavement#'));
  check('one pavement mesh per paving pattern', paving.length >= 3, paving.join(', '));
  check('paving materials carry a map and a normal map', paving.every((n) => {
    const m = api.viewport.meshes.get(n).material;
    return m.map && m.normalMap;
  }));
  check('paving tiles in metres (repeat = 1 / tile)', (() => {
    const m = api.viewport.meshes.get(paving[0]).material;
    return m.map.repeat.x > 0 && m.map.repeat.x < 1;
  })());
  check('roadbed / earth meshes exist under the elevated ramp', names.includes('earth') || names.includes('roadbed'), names.join(', '));
  api.viewport.setTextured(false);
  check('textures can be switched off', !api.viewport.meshes.get(paving[0]).material.map);
  api.viewport.setTextured(true);
}

console.log('\n— junction moves as one body —');
{
  const api = dom.window.__roadworks;
  const node = [...api.state.network.graph.nodes.values()].find((n) => n.degree >= 3);
  check('a merged junction exists to grab', !!node, `${api.state.network.stats.junctions} junctions`);
  const armNames = new Set(node.edgeIds.map((id) => api.state.network.graph.edges.get(id)?.sourceId));
  api.selectJunction(node);
  check('selecting a junction retitles the inspector', el('InspectorTitle').textContent === 'Junction', el('InspectorTitle').textContent);

  const before = api.state.doc.corridors.map((c) => c.points.length);
  const grabs = api.collectJunctionGrabs(node);
  const after = api.state.doc.corridors.map((c) => c.points.length);
  check('one grab per corridor meeting here (at least)', grabs.length >= armNames.size, `${grabs.length} grabs for ${armNames.size} corridors`);
  check('corridors crossing without a control point gain one', after.some((n, i) => n > before[i]) || grabs.length >= armNames.size);

  const delta = { x: 11, y: -7, z: 2.5 };
  for (const grab of grabs) {
    const p = grab.corridor.points[grab.index];
    p.x = grab.start.x + delta.x;
    p.y = grab.start.y + delta.y;
    p.z = grab.start.z + delta.z;
  }
  const spread = grabs.map((g) => {
    const p = g.corridor.points[g.index];
    return Math.hypot(p.x - (g.start.x + delta.x), p.y - (g.start.y + delta.y), p.z - (g.start.z + delta.z));
  });
  check('every arm point translated by the same delta', Math.max(...spread) < 1e-9);

  api.state.selection.junctionAt = { x: node.co.x + delta.x, y: node.co.y + delta.y, z: node.co.z + delta.z };
  api.rebuild();
  const target = { x: node.co.x + delta.x, y: node.co.y + delta.y, z: node.co.z + delta.z };
  const nodes = [...api.state.network.graph.nodes.values()];
  const moved = nodes.find((n) => n.degree >= 3 && Math.hypot(n.co.x - target.x, n.co.y - target.y) < 4);
  check('the intersection is still merged at its new position', !!moved, `arms ${moved?.degree}`);
  check('it kept all of its arms', moved && moved.degree === node.degree, `${moved?.degree} vs ${node.degree}`);
  const leftovers = nodes.filter((n) => n.degree >= 3 && Math.hypot(n.co.x - node.co.x, n.co.y - node.co.y) < 3);
  check('nothing was left torn apart at the old position', leftovers.length === 0, `${leftovers.length} stray nodes`);
  check('selection follows the junction to its new node', api.state.selection.junctionId === moved?.id);
}

console.log('\n— Unreal-style flight —');
{
  const api = dom.window.__roadworks;
  const before = api.viewport.camera.position.clone();
  canvas.dispatch('pointerdown', { button: 2, clientX: 400, clientY: 300, pointerId: 7 });
  canvas.dispatch('pointermove', { button: 2, clientX: 460, clientY: 260, pointerId: 7 });
  const looked = api.viewport.camera.position.distanceTo(before);
  check('right-drag looks without moving the camera', looked < 1e-6, `moved ${looked}`);

  const speed = api.viewport.flySpeed;
  canvas.dispatch('wheel', { deltaY: -240 });
  check('wheel trims flight speed while flying', api.viewport.flySpeed > speed, `${speed} → ${api.viewport.flySpeed}`);

  const start = api.viewport.camera.position.clone();
  dom.window.dispatch('keydown', { key: 'w' });
  dom.pump(1);
  dom.pump(1);
  const flew = api.viewport.camera.position.distanceTo(start);
  check('W flies the camera forward', flew > 0.1, `moved ${flew.toFixed(3)} m`);

  dom.window.dispatch('keyup', { key: 'w' });
  const held = api.viewport.camera.position.clone();
  dom.pump(2);
  check('releasing the key stops the camera', api.viewport.camera.position.distanceTo(held) < 1e-9);

  dom.window.dispatch('keydown', { key: 'e' });
  const up = api.viewport.camera.position.z;
  dom.pump(1);
  dom.pump(1);
  check('E rises', api.viewport.camera.position.z > up);
  dom.window.dispatch('keyup', { key: 'e' });
  canvas.dispatch('pointerup', { button: 2, clientX: 460, clientY: 260, pointerId: 7 });
}

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
check('new document restores the demo', el('CorridorList').children.length === 7, `${el('CorridorList').children.length} rows`);

console.log(`\n${failures === 0 ? 'ALL CHECKS PASSED' : `${failures} CHECK(S) FAILED`}\n`);
process.exit(failures === 0 ? 0 : 1);
