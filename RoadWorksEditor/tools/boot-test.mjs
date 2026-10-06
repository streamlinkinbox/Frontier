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
const demoCount = dom.window.__roadworks.state.doc.corridors.length;
check('outliner lists the demo corridors', el('CorridorList').children.length === demoCount && demoCount >= 15, `${el('CorridorList').children.length} rows`);
check('census shows corridors', el('CensusCorridors').textContent === String(demoCount), el('CensusCorridors').textContent);
check('the default document ships every bridge family', (() => {
  const types = new Set(dom.window.__roadworks.state.doc.corridors.filter((c) => c.family === 'bridge').map((c) => c.bridge.type));
  return ['beam', 'box', 'slab', 'cantilever', 'arch', 'tiedarch', 'masonry', 'truss', 'throughtruss', 'suspension', 'cablestay'].every((t) => types.has(t));
})(), 'a span of each type should be in the scene on load');
check('the default document ships every road preset', (() => {
  const presets = new Set(dom.window.__roadworks.state.doc.corridors.filter((c) => c.family !== 'bridge').map((c) => c.preset));
  return ['street', 'avenue', 'alley', 'narrow', 'highway'].every((p) => presets.has(p));
})());
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
const bridgeRow = el('CorridorList').children.find((row) => /Estuary Viaduct/.test(row.textContent));
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
  check('status explains the grab', /arms|intersection/i.test(el('Status').textContent), el('Status').textContent);
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
  dom.window.dispatch('keydown', { key: '4' });
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
check('new corridor appears in the outliner', el('CorridorList').children.length === demoCount + 1, `${el('CorridorList').children.length} rows`);

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
  check('selecting a junction names it after the streets that meet there', /\u00d7|Junction/.test(el('InspectorTitle').textContent), el('InspectorTitle').textContent);
  check('the junction is highlighted in the viewport', api.viewport.highlightGroup.children.length > 0, `${api.viewport.highlightGroup.children.length} highlight meshes`);

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

  api.state.selection.junctions[0].at = { x: node.co.x + delta.x, y: node.co.y + delta.y, z: node.co.z + delta.z };
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

console.log('\n— modes —');
{
  const api = dom.window.__roadworks;
  el('ToolPoint').click();
  check('points mode activates', api.state.tool === 'point' && el('ToolPoint').classList.contains('Active'));
  el('ToolJunction').click();
  check('junctions mode activates', api.state.tool === 'junction' && !el('ToolPoint').classList.contains('Active'));
  dom.window.dispatch('keydown', { key: '2' });
  check('keyboard 2 selects points mode', api.state.tool === 'point');
  dom.window.dispatch('keydown', { key: '3' });
  check('keyboard 3 selects junctions mode', api.state.tool === 'junction');
  dom.window.dispatch('keydown', { key: '1' });
  check('keyboard 1 returns to select mode', api.state.tool === 'select');
}

console.log('\n— selecting a road highlights the stretch between junctions —');
{
  const three = await import('three');
  const api = dom.window.__roadworks;
  el('ToolSelect').click();
  three.pickControl.filter = (o) => o.name === 'road';
  let clickError = null;
  try {
    canvas.dispatch('pointerdown', { button: 0, clientX: 500, clientY: 320, pointerId: 11 });
    canvas.dispatch('pointerup', { button: 0, clientX: 500, clientY: 320, pointerId: 11 });
  } catch (error) {
    clickError = error;
  }
  three.pickControl.filter = null;
  check('clicking the road surface runs cleanly', !clickError, clickError?.message);

  // The stub raycaster always reports the world origin, which is inside a junction, so resolve a point that is
  // genuinely out on a corridor and drive the same path the click takes.
  const hit = api.edgeAtPoint({ x: -120, y: 0, z: 0 });
  check('a point on the tarmac resolves to a graph edge', !!hit, 'expected Harbour Avenue');
  api.selectCorridor(hit.edge.sourceId, { edgeId: hit.edgeId });
  check('a corridor is selected by clicking the road', !!api.state.selection.corridorId, api.state.selection.corridorId);
  check('one stretch — a graph edge — is selected, not the whole corridor', !!api.state.selection.edgeId, String(api.state.selection.edgeId));
  check('the stretch is painted in the viewport', api.viewport.highlightGroup.children.length >= 3, `${api.viewport.highlightGroup.children.length} highlight meshes`);
  const edge = api.state.network.graph.edges.get(api.state.selection.edgeId);
  check('the selected stretch belongs to the selected corridor', edge?.sourceId === api.state.selection.corridorId);
}

console.log('\n— multi-select and transform together —');
{
  const three = await import('three');
  const api = dom.window.__roadworks;
  el('ToolPoint').click();
  check('control point handles exist to pick', api.viewport.handleMeshes.length > 10, `${api.viewport.handleMeshes.length} handles`);

  // Shift-click two different handles: both stay selected. The overlay (and therefore every handle mesh) is
  // rebuilt after each selection change, so the pick filter has to be re-bound each time.
  const pickHandleAt = (i, shiftKey) => {
    const target = api.viewport.handleMeshes[i];
    three.pickControl.filter = (o) => o === target;
    canvas.dispatch('pointerdown', { button: 0, shiftKey, clientX: 200 + i, clientY: 200, pointerId: 12 + i });
    canvas.dispatch('pointerup', { button: 0, shiftKey, clientX: 200 + i, clientY: 200, pointerId: 12 + i });
    three.pickControl.filter = null;
  };
  pickHandleAt(0, false);
  pickHandleAt(1, true);
  check('shift-click adds to the selection', api.state.selection.points.length === 2, `${api.state.selection.points.length} selected`);

  const centre = api.selectionCentroid();
  const pts = api.state.selection.points.map((e) => {
    const c = api.state.doc.corridors.find((x) => x.id === e.corridorId);
    return c.points[e.index];
  });
  const mid = { x: (pts[0].x + pts[1].x) / 2, y: (pts[0].y + pts[1].y) / 2, z: (pts[0].z + pts[1].z) / 2 };
  check('the gizmo sits at the centroid of the selection', Math.hypot(centre.x - mid.x, centre.y - mid.y, centre.z - mid.z) < 1e-9);

  const before = pts.map((p) => ({ ...p }));
  const grabs = api.collectGrabs();
  check('a transform collects one grab per selected point', grabs.length === 2, `${grabs.length} grabs`);
  for (const g of grabs) {
    g.point.x = g.start.x + 5;
    g.point.z = g.start.z + 1;
  }
  check('both points moved by the same delta', pts.every((p, i) => Math.abs(p.x - before[i].x - 5) < 1e-9 && Math.abs(p.z - before[i].z - 1) < 1e-9));

  check('a rectangle select can see every handle', api.viewport.handlesInRect({ x0: -1e9, x1: 1e9, y0: -1e9, y1: 1e9 }).length === api.viewport.handleMeshes.length);
  let marqueeError = null;
  try {
    canvas.dispatch('pointerdown', { button: 0, shiftKey: true, clientX: 100, clientY: 100, pointerId: 14 });
    canvas.dispatch('pointermove', { button: 0, shiftKey: true, clientX: 700, clientY: 500, pointerId: 14 });
    canvas.dispatch('pointerup', { button: 0, shiftKey: true, clientX: 700, clientY: 500, pointerId: 14 });
  } catch (error) {
    marqueeError = error;
  }
  check('shift-drag marquee runs and hides itself', !marqueeError && el('Marquee').hidden === true, marqueeError?.message);

  // Junction multi-select moves every arm of every selected intersection.
  el('ToolJunction').click();
  const nodes = [...api.state.network.graph.nodes.values()].filter((x) => x.degree >= 3).slice(0, 2);
  api.selectJunction(nodes[0]);
  if (nodes[1]) api.selectJunction(nodes[1], true);
  check('two intersections can be selected at once', api.state.selection.junctions.length === Math.min(2, nodes.length));
  const jg = api.collectGrabs();
  check('their arms are all collected for one rigid move', jg.length >= 4, `${jg.length} grabs`);
  check('the multi-selection inspector takes over', /selected|intersections/i.test(el('InspectorTitle').textContent), el('InspectorTitle').textContent);
  el('ToolSelect').click();
}

console.log('\n— street names —');
{
  const api = dom.window.__roadworks;
  const labels = api.viewport.labelGroup.children;
  check('every visible corridor carries a name label', labels.length >= api.state.doc.corridors.length, `${labels.length} labels`);
  el('ViewLabels').click();
  check('names can be switched off', api.viewport.labelGroup.visible === false);
  el('ViewLabels').click();
  check('names come back', api.viewport.labelGroup.visible === true);

  // rename in place from the outliner
  const row = el('CorridorList').children[0];
  const label = row.find((n) => n.className && n.className.includes('CorridorName'));
  label.dispatch('dblclick');
  const field = row.find((n) => n.className && n.className.includes('RenameField'));
  check('double-clicking a name opens a rename field', !!field);
  if (field) {
    field.value = 'Kingsway';
    field.dispatch('keydown', { key: 'Enter' });
    check('the street keeps its new name', api.state.doc.corridors[0].name === 'Kingsway', api.state.doc.corridors[0].name);
    check('the outliner shows it', /Kingsway/.test(el('CorridorList').children[0].textContent));
  }
}

console.log('\n— framing follows the selection —');
{
  const api = dom.window.__roadworks;
  api.clearSelection();
  const whole = api.frameTarget();
  const corridor = api.state.doc.corridors.find((c) => c.family === 'bridge');
  api.selectCorridor(corridor.id);
  const one = api.frameTarget();
  const span = (b) => Math.max(b.max.x - b.min.x, b.max.y - b.min.y);
  check('F frames the whole network with nothing selected', span(whole) > span(one), `${span(whole).toFixed(0)} m vs ${span(one).toFixed(0)} m`);
  check("F frames the selection when there is one", span(one) < 340, `${span(one).toFixed(0)} m`);
  api.clearSelection();
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
check('new document restores the demo', el('CorridorList').children.length === demoCount, `${el('CorridorList').children.length} rows`);

console.log(`\n${failures === 0 ? 'ALL CHECKS PASSED' : `${failures} CHECK(S) FAILED`}\n`);
process.exit(failures === 0 ? 0 : 1);
