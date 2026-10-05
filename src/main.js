import * as THREE from 'three';
import { RoadScene } from './scene.js';
import {
  addRoadPath,
  createInitialNetwork,
  deleteSelection,
  edgeLength,
  getDegrees,
  getNetworkMetrics,
  makeId,
  mergePlanarCrossings,
  moveNode,
  nearestNode,
  nearestPointOnEdge,
  pointOnEdge,
  sampleEdge,
  validateNetwork,
} from './road-network.js';

const STORAGE_KEY = 'roadworks-editor-project-v1';
const MAX_HISTORY = 60;
const app = document.querySelector('#app');
const viewport = document.querySelector('#viewport');
const scene = new RoadScene(document.querySelector('#scene-host'));

let network = readSavedNetwork() ?? createInitialNetwork();
let selection = { type: 'network', id: 'network' };
let activeTool = 'select';
let pendingPoints = [];
let cursorPoint = null;
let cursorSnap = null;
let previousDrawClick = null;
let drag = null;
let history = [];
let redoHistory = [];
let liveEditKey = null;
let saveTimer = 0;
let drawMouseDown = false;
let expandedTree = true;
let planView = false;

scene.setNetwork(network, selection);
scene.fit(network);
renderAll();

function readSavedNetwork() {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    return raw ? validateNetwork(JSON.parse(raw)) : null;
  } catch (error) {
    console.warn('Saved RoadWorks project could not be loaded.', error);
    return null;
  }
}

function icon(name) {
  return `<svg aria-hidden="true"><use href="#i-${name}"></use></svg>`;
}

function escapeHtml(value = '') {
  return String(value).replace(/[&<>"']/g, (character) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]);
}

function displayNumber(value, places = 1) {
  return Number(value ?? 0).toFixed(places).replace(/\.0$/, '');
}

function saveSnapshot() {
  history.push(JSON.stringify(network));
  if (history.length > MAX_HISTORY) history.shift();
  redoHistory = [];
  updateHistoryButtons();
}

function beginLiveEdit(key) {
  if (liveEditKey === key) return;
  saveSnapshot();
  liveEditKey = key;
}

function finishLiveEdit() {
  liveEditKey = null;
  persistNetwork();
}

function persistNetwork() {
  window.clearTimeout(saveTimer);
  const status = document.querySelector('#save-status');
  const statusDot = document.querySelector('.document-status .status-dot');
  if (status) status.textContent = 'Saving locally…';
  if (statusDot) statusDot.style.background = 'var(--accent)';
  saveTimer = window.setTimeout(() => {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(network));
      if (status) status.textContent = 'Saved locally';
      if (statusDot) statusDot.style.background = 'var(--green)';
    } catch (error) {
      console.warn('Local save is unavailable.', error);
      if (status) status.textContent = 'Local storage full';
      if (statusDot) statusDot.style.background = 'var(--red)';
    }
  }, 320);
}

function undo() {
  if (!history.length) return;
  redoHistory.push(JSON.stringify(network));
  network = validateNetwork(JSON.parse(history.pop()));
  selection = { type: 'network', id: 'network' };
  finishLiveEdit();
  refreshAll('Undo complete');
  updateHistoryButtons();
}

function redo() {
  if (!redoHistory.length) return;
  history.push(JSON.stringify(network));
  network = validateNetwork(JSON.parse(redoHistory.pop()));
  selection = { type: 'network', id: 'network' };
  finishLiveEdit();
  refreshAll('Redo complete');
  updateHistoryButtons();
}

function updateHistoryButtons() {
  const undoButton = document.querySelector('#undo-button');
  const redoButton = document.querySelector('#redo-button');
  if (undoButton) undoButton.disabled = history.length === 0;
  if (redoButton) redoButton.disabled = redoHistory.length === 0;
}

function refreshAll(message = null) {
  if (scene) scene.setNetwork(network, selection);
  renderAll();
  persistNetwork();
  if (message) setBuildStatus(message);
}

function refreshSceneOnly() {
  scene.setNetwork(network, selection);
  renderOutliner();
  updateMetrics();
  updateProjectLabels();
  updateSelectionButtons();
  persistNetwork();
}

function setBuildStatus(message, tone = 'ready') {
  const label = document.querySelector('#build-status');
  const detail = document.querySelector('#build-detail');
  const light = document.querySelector('.status-light');
  if (!label || !detail || !light) return;
  label.textContent = tone === 'building' ? 'Building' : tone === 'error' ? 'Check geometry' : 'Ready';
  detail.textContent = message;
  light.style.background = tone === 'error' ? 'var(--red)' : tone === 'building' ? 'var(--accent)' : 'var(--green)';
}

function toast(message, type = 'success') {
  const stack = document.querySelector('#toast-stack');
  const item = document.createElement('div');
  item.className = `toast${type === 'error' ? ' error' : ''}`;
  item.innerHTML = `<i></i><span>${escapeHtml(message)}</span>`;
  stack.append(item);
  window.setTimeout(() => item.remove(), 3100);
}

function renderAll() {
  updateProjectLabels();
  renderOutliner();
  renderInspector();
  updateMetrics();
  updateSelectionButtons();
  updateHistoryButtons();
  updateToolButtons();
  syncLayerInputs();
  document.title = `${network.name || 'Untitled network'} · RoadWorks Editor`;
}

function updateProjectLabels() {
  const name = network.name || 'Untitled network';
  for (const selector of ['#project-title', '#document-name', '#viewport-project-name']) {
    const element = document.querySelector(selector);
    if (element) element.textContent = name;
  }
}

function gradeSeparatedCrossings() {
  const bridgeEdges = network.edges.filter((edge) => edge.bridge?.enabled);
  const roadEdges = network.edges.filter((edge) => !edge.bridge?.enabled);
  const hits = [];
  for (const bridge of bridgeEdges) {
    const samples = sampleEdge(bridge, network.nodes, 160);
    for (const sample of samples) {
      if (sample.t < 0.14 || sample.t > 0.86 || sample.y < 1.5) continue;
      let nearest = null;
      for (const road of roadEdges) {
        const hit = nearestPointOnEdge(road, network.nodes, sample, 40);
        if (hit.distance < 0.38 && (!nearest || hit.distance < nearest.distance)) nearest = hit;
      }
      if (nearest && !hits.some((point) => Math.hypot(point.x - nearest.point.x, point.z - nearest.point.z) < 1.1)) {
        hits.push(nearest.point);
      }
    }
  }
  return hits.length;
}

function updateMetrics() {
  const metrics = getNetworkMetrics(network);
  const crossingCount = gradeSeparatedCrossings();
  const set = (id, value) => {
    const element = document.querySelector(`#${id}`);
    if (element) element.textContent = String(value);
  };
  set('summary-nodes', metrics.nodes);
  set('summary-roads', metrics.roads);
  set('summary-bridges', metrics.bridges);
  set('status-nodes', metrics.nodes);
  set('status-segments', network.edges.length);
  set('status-junctions', metrics.junctions);
  set('status-length', `${Math.round(metrics.length)} m`);
  set('topology-readout', `${metrics.junctions} ${metrics.junctions === 1 ? 'connected junction' : 'connected junctions'}`);
  set('crossing-readout', `${crossingCount} ${crossingCount === 1 ? 'grade-separated crossing' : 'grade-separated crossings'}`);
  const treeCount = document.querySelector('.tree-group-label .count-pill');
  if (treeCount) treeCount.textContent = String(network.edges.length);
}

function rowForSelection(type, id) {
  return selection.type === type && selection.id === id ? ' selected' : '';
}

function renderOutliner() {
  const tree = document.querySelector('#outliner-tree');
  const metrics = getNetworkMetrics(network);
  const query = document.querySelector('#outliner-search')?.value.trim().toLowerCase() ?? '';
  const edges = network.edges.filter((edge) => `${edge.name} ${edge.bridge?.enabled ? 'bridge' : 'road'}`.toLowerCase().includes(query));
  const degreeMap = metrics.degrees;
  const junctions = Object.values(network.nodes).filter((node) => (degreeMap[node.id] ?? 0) >= 3 && `${node.name ?? ''} junction intersection`.toLowerCase().includes(query));
  const roadRows = expandedTree ? edges.map((edge) => `
    <button class="tree-row indent${rowForSelection('edge', edge.id)}" data-select-type="edge" data-select-id="${escapeHtml(edge.id)}" title="Select ${escapeHtml(edge.name)}">
      ${icon(edge.bridge?.enabled ? 'bridge' : 'road')}<span class="tree-name">${escapeHtml(edge.name)}</span><span class="tree-type">${edge.bridge?.enabled ? 'BRIDGE' : 'ROAD'}</span><span class="tree-state${edge.bridge?.enabled ? ' bridge' : ''}"></span>
    </button>`).join('') : '';
  const junctionRows = junctions.map((node) => `
    <button class="tree-row indent${rowForSelection('node', node.id)}" data-select-type="node" data-select-id="${escapeHtml(node.id)}" title="Select ${escapeHtml(node.name ?? 'junction')}">
      ${icon('node')}<span class="tree-name">${escapeHtml(node.name ?? 'Intersection')}</span><span class="tree-type">${degreeMap[node.id]}-WAY</span>
    </button>`).join('');
  tree.innerHTML = `
    <div class="tree-group-label">${icon('layers')}<span>Scene collection</span><span class="count-pill">${network.edges.length}</span></div>
    <button class="tree-row${rowForSelection('network', 'network')}" data-select-type="network" data-select-id="network">${icon('road')}<span class="tree-name">Road network</span><span class="tree-type">${network.edges.length} EDGES</span></button>
    ${roadRows}
    ${junctions.length || !query ? `<div class="tree-group-label">${icon('node')}<span>Junctions</span><span class="count-pill">${junctions.length}</span></div>${junctionRows || (!query ? '<div class="tree-row placeholder-row">No additional nodes</div>' : '')}` : ''}
    ${!query ? `<div class="tree-group-label">${icon('grid')}<span>Reference</span></div><button class="tree-row" data-select-type="network" data-select-id="network">${icon('grid')}<span class="tree-name">Construction grid</span><span class="tree-type">1 m</span></button>` : ''}
    ${query && !edges.length && !junctions.length ? '<div class="empty-inspector">No scene items match this filter.</div>' : ''}
  `;
}

function objectCard(title, subtitle, glyph, badge = '') {
  return `<div class="inspector-object"><span class="object-glyph">${icon(glyph)}</span><div class="object-copy"><strong>${escapeHtml(title)}</strong><small>${escapeHtml(subtitle)}</small></div>${badge ? `<span class="object-badge">${escapeHtml(badge)}</span>` : ''}</div>`;
}

function propAttribute(scope) {
  return ({ node: 'data-node-prop', default: 'data-default-prop', bridge: 'data-bridge-prop', setting: 'data-setting-prop', handle: 'data-handle-prop' })[scope] ?? 'data-prop';
}

function numberField(label, prop, value, unit = 'm', scope = 'prop', options = {}) {
  const min = options.min ?? 0;
  const max = options.max ?? 999;
  const step = options.step ?? 0.1;
  return `<label class="control-field${options.full ? ' full' : ''}"><span class="control-label">${label}</span><span class="input-wrap"><input type="number" ${propAttribute(scope)}="${prop}" value="${escapeHtml(value)}" min="${min}" max="${max}" step="${step}" /><span class="input-unit">${unit}</span></span></label>`;
}

function selectField(label, prop, value, choices, scope = 'prop', full = false) {
  return `<label class="control-field${full ? ' full' : ''}"><span class="control-label">${label}</span><span class="input-wrap input-select"><select ${propAttribute(scope)}="${prop}">${choices.map(([option, caption]) => `<option value="${option}"${String(value) === String(option) ? ' selected' : ''}>${caption}</option>`).join('')}</select></span></label>`;
}

function rangeField(label, prop, value, min, max, step, unit = 'm', scope = 'prop', formatter = null) {
  const shown = formatter ? formatter(value) : `${displayNumber(value, step < 1 ? 2 : 0)} ${unit}`;
  return `<label class="range-field"><span class="control-label">${label}<output data-range-output="${prop}" data-range-scope="${scope}">${shown}</output></span><input type="range" ${propAttribute(scope)}="${prop}" min="${min}" max="${max}" step="${step}" value="${value}" /></label>`;
}

function checkField(label, prop, checked, scope = 'prop', help = '') {
  return `<label class="check-row"><input type="checkbox" ${propAttribute(scope)}="${prop}"${checked ? ' checked' : ''} /><span>${label}</span>${help ? `<small class="check-help">${help}</small>` : ''}</label>`;
}

function profileSection(edge, scope = 'prop') {
  const source = edge;
  const count = Math.max(1, Math.round(Number(source.laneCount ?? 2)));
  return `
    <details class="inspector-section" open>
      <summary><span>${icon('road')}Road profile</span><small>${displayNumber(count * Number(source.laneWidth ?? 3.5), 1)} m carriageway</small></summary>
      <div class="control-grid">
        ${selectField('Travel lanes', 'laneCount', count, Array.from({ length: 8 }, (_, i) => [i + 1, `${i + 1} ${i === 0 ? 'lane' : 'lanes'}`]), scope)}
        ${numberField('Lane width', 'laneWidth', displayNumber(source.laneWidth, 1), 'm', scope, { min: 2.4, max: 5.2, step: 0.1 })}
        ${rangeField('Shoulder', 'shoulder', Number(source.shoulder ?? 0.3), 0, 1.8, 0.05, 'm', scope)}
        ${rangeField('Curve tension', 'curveTension', Number(source.curveTension ?? 1), 0.25, 1.8, 0.05, '×', scope, (value) => `${Number(value).toFixed(2)}×`)}
        ${checkField('Automatic tangent handles', 'autoCurve', source.autoCurve !== false, scope, 'curve')}
      </div>
    </details>
    <details class="inspector-section" open>
      <summary><span>${icon('layers')}Pavement & curb</span><small>separate profiles</small></summary>
      <div class="control-grid">
        ${checkField('Generate pavement', 'pavementEnabled', source.pavementEnabled !== false, scope)}
        ${rangeField('Pavement width', 'pavementWidth', Number(source.pavementWidth ?? 1.8), 0, 4.5, 0.1, 'm', scope)}
        ${numberField('Curb width', 'curbWidth', displayNumber(source.curbWidth ?? 0.22, 2), 'm', scope, { min: 0.08, max: 0.65, step: 0.02 })}
        ${rangeField('Curb rise', 'curbHeight', Number(source.curbHeight ?? 0.18), 0, 0.42, 0.01, 'm', scope)}
        ${checkField('Lane markings', 'markings', source.markings !== false, scope)}
      </div>
    </details>
  `;
}

function bridgeSection(edge) {
  if (!edge.bridge?.enabled) {
    return `
      <details class="inspector-section" open>
        <summary><span>${icon('bridge')}Bridge structure</span><small>not assigned</small></summary>
        <div class="control-grid">
          <div class="inspector-note full"><b>Grade separated</b><br />Turn this spline into a bridge span. Crossings beneath the span remain independent instead of becoming junctions.</div>
          <button class="mini-action full" data-action="enable-bridge">${icon('bridge')}Generate bridge deck</button>
        </div>
      </details>`;
  }
  const bridge = edge.bridge;
  const deckChoices = [
    ['box-girder', 'Box girder'], ['t-girder', 'Multi-girder'], ['truss', 'Lattice truss'], ['arch-rib', 'Arch ribs'],
  ];
  const supportChoices = [
    ['twin-column', 'Twin columns'], ['hammerhead', 'Hammerhead pier'], ['portal', 'Portal frame'], ['inverted-y', 'Inverted Y pier'], ['arch', 'Arch cradle'],
  ];
  return `
    <details class="inspector-section" open>
      <summary><span>${icon('bridge')}Bridge structure</span><small>procedural span</small></summary>
      <div class="control-grid">
        ${selectField('Deck system', 'deckType', bridge.deckType ?? 'box-girder', deckChoices, 'bridge', true)}
        ${rangeField('Deck clearance', 'height', Number(bridge.height ?? 7), 2.5, 18, 0.25, 'm', 'bridge')}
        ${selectField('Pier family', 'supportType', bridge.supportType ?? 'twin-column', supportChoices, 'bridge', true)}
        ${rangeField('Support spacing', 'supportSpacing', Number(bridge.supportSpacing ?? 12), 6, 24, 1, 'm', 'bridge')}
        ${rangeField('Approach ramp', 'rampFraction', Number(bridge.rampFraction ?? 0.2), 0.08, 0.44, 0.01, '% span', 'bridge', (value) => `${Math.round(Number(value) * 100)}% span`)}
        ${checkField('Guardrail parapets', 'parapet', bridge.parapet !== false, 'bridge')}
        <div class="inspector-note full"><b>Structural section</b><br /><strong>${escapeHtml(deckChoices.find(([value]) => value === bridge.deckType)?.[1] ?? 'Box girder')}</strong> deck with <strong>${escapeHtml(supportChoices.find(([value]) => value === bridge.supportType)?.[1] ?? 'Twin columns')}</strong> supports. Geometry rebuilds from span length, width and clearance.</div>
        <button class="mini-action full danger-mini" data-action="disable-bridge">${icon('close')}Convert back to at-grade road</button>
      </div>
    </details>`;
}

function selectedEdge() {
  return selection.type === 'edge' ? network.edges.find((edge) => edge.id === selection.id) : null;
}

function renderInspector() {
  const content = document.querySelector('#inspector-content');
  const kindLabel = document.querySelector('#inspector-kind');
  if (!content) return;
  const edge = selectedEdge();
  const selectedNode = selection.type === 'node' ? network.nodes[selection.id] : null;
  const metrics = getNetworkMetrics(network);

  if (edge) {
    kindLabel.textContent = edge.bridge?.enabled ? 'Bridge segment' : 'Road segment';
    const length = edgeLength(edge, network.nodes, 48);
    content.innerHTML = `
      ${objectCard(edge.name, `${displayNumber(length, 1)} m spline corridor · ${edge.bridge?.enabled ? 'grade separated' : 'at grade'}`, edge.bridge?.enabled ? 'bridge' : 'road', edge.bridge?.enabled ? 'BRIDGE' : 'ROAD')}
      <div class="inspector-quick-actions"><button class="mini-action" data-action="focus-edge">${icon('fit')}Focus spline</button><button class="mini-action" data-action="rename-edge">${icon('curve')}Rename</button></div>
      ${profileSection(edge, 'prop')}
      <details class="inspector-section" open>
        <summary><span>${icon('curve')}Spline handles</span><small>cubic bezier</small></summary>
        <div class="control-grid">
          <div class="inspector-note full"><b>Editable curve</b><br />Select either gold handle in the viewport and drag to reshape this spline. Endpoints remain welded to the shared road graph.</div>
          ${checkField('Automatic tangent handles', 'autoCurve', edge.autoCurve !== false, 'prop', 'smooth')}
          ${numberField('Start handle X', 'c1x', displayNumber(edge.c1?.x ?? 0, 2), 'm', 'handle', { min: -150, max: 150, step: 0.1 })}
          ${numberField('Start handle Z', 'c1z', displayNumber(edge.c1?.z ?? 0, 2), 'm', 'handle', { min: -150, max: 150, step: 0.1 })}
          ${numberField('End handle X', 'c2x', displayNumber(edge.c2?.x ?? 0, 2), 'm', 'handle', { min: -150, max: 150, step: 0.1 })}
          ${numberField('End handle Z', 'c2z', displayNumber(edge.c2?.z ?? 0, 2), 'm', 'handle', { min: -150, max: 150, step: 0.1 })}
          <button class="mini-action full" data-action="reset-handles">${icon('rotate')}Reset to straight tangents</button>
        </div>
      </details>
      ${bridgeSection(edge)}
      <div class="metric-row"><div class="metric-chip"><b>${displayNumber(length, 1)} m</b><small>segment length</small></div><div class="metric-chip"><b>${edge.laneCount} × ${displayNumber(edge.laneWidth, 1)} m</b><small>lane profile</small></div><div class="metric-chip"><b>${edge.bridge?.enabled ? 'ELEVATED' : 'LEVEL 00'}</b><small>network level</small></div></div>
      ${edge.bridge?.enabled ? '<div class="inspector-note"><b>Layer-aware topology</b><br />This elevated span is intentionally excluded from at-grade crossing merges. The lower road keeps its own continuous junction graph.</div>' : ''}
    `;
  } else if (selectedNode) {
    const degree = metrics.degrees[selectedNode.id] ?? 0;
    const connected = network.edges.filter((candidate) => candidate.from === selectedNode.id || candidate.to === selectedNode.id);
    kindLabel.textContent = degree >= 3 ? 'Junction node' : 'Control node';
    content.innerHTML = `
      ${objectCard(selectedNode.name ?? 'Road node', `${degree >= 3 ? `${degree}-way shared junction` : `${degree} connected ${degree === 1 ? 'segment' : 'segments'}`} · graph vertex`, 'node', degree >= 3 ? `${degree}-WAY` : 'NODE')}
      <details class="inspector-section" open><summary><span>${icon('move')}Transform</span><small>world space</small></summary><div class="control-grid">${numberField('Position X', 'x', displayNumber(selectedNode.x, 2), 'm', 'node', { min: -150, max: 150, step: 0.1 })}${numberField('Position Z', 'z', displayNumber(selectedNode.z, 2), 'm', 'node', { min: -150, max: 150, step: 0.1 })}</div><div class="coordinate-readout"><span>DEGREE<b>${degree}</b></span><span>VERTEX<b>${selectedNode.id.slice(-6).toUpperCase()}</b></span></div></details>
      <details class="inspector-section" open><summary><span>${icon('node')}Connected segments</span><small>${connected.length} links</small></summary><div class="control-grid">${connected.length ? connected.map((item) => `<button class="mini-action full" data-select-type="edge" data-select-id="${escapeHtml(item.id)}">${icon(item.bridge?.enabled ? 'bridge' : 'road')}${escapeHtml(item.name)}</button>`).join('') : '<div class="inspector-note full">No road segments are attached to this node.</div>'}</div></details>
      <div class="inspector-note"><b>Shared vertex</b><br />At-grade crossings are split into a single shared graph node. Moving this point keeps every connected spline welded.</div>
    `;
  } else {
    kindLabel.textContent = 'Road network';
    const profile = network.settings.defaultProfile;
    content.innerHTML = `
      ${objectCard(network.name || 'Untitled network', 'Procedural corridor graph · metres', 'road', 'NETWORK')}
      <details class="inspector-section" open><summary><span>${icon('road')}Default road profile</span><small>new splines</small></summary><div class="control-grid">${selectField('Travel lanes', 'laneCount', profile.laneCount, Array.from({ length: 8 }, (_, i) => [i + 1, `${i + 1} ${i === 0 ? 'lane' : 'lanes'}`]), 'default')}${numberField('Lane width', 'laneWidth', displayNumber(profile.laneWidth, 1), 'm', 'default', { min: 2.4, max: 5.2, step: 0.1 })}${rangeField('Shoulder', 'shoulder', Number(profile.shoulder), 0, 1.8, 0.05, 'm', 'default')}${rangeField('Pavement width', 'pavementWidth', Number(profile.pavementWidth), 0, 4.5, 0.1, 'm', 'default')}${rangeField('Curb rise', 'curbHeight', Number(profile.curbHeight), 0, 0.42, 0.01, 'm', 'default')}${checkField('Generate pavement', 'pavementEnabled', profile.pavementEnabled !== false, 'default')}${checkField('Lane markings', 'markings', profile.markings !== false, 'default')}</div></details>
      <details class="inspector-section" open><summary><span>${icon('node')}Intersection topology</span><small>live graph</small></summary><div class="metric-row"><div class="metric-chip"><b>${metrics.nodes}</b><small>shared nodes</small></div><div class="metric-chip"><b>${metrics.junctions}</b><small>junctions</small></div><div class="metric-chip"><b>${gradeSeparatedCrossings()}</b><small>overpasses</small></div></div><div class="inspector-note"><b>Graph builder</b><br />Road centerlines are sampled for true curve crossings, inserted as shared vertices and split without changing the Bezier shape. Bridge spans stay on a separate elevation layer.</div></details>
      <details class="inspector-section"><summary><span>${icon('settings')}Network settings</span><small>editor</small></summary><div class="control-grid">${checkField('Snap to existing roads', 'snap', network.settings.snap !== false, 'setting')}${checkField('Show spline control points', 'showPoints', network.settings.showPoints !== false, 'setting')}${numberField('Grid spacing', 'gridSize', displayNumber(network.settings.gridSize ?? 1, 1), 'm', 'setting', { min: 0.25, max: 10, step: 0.25 })}</div></details>
      <div class="inspector-note"><b>Rebuild-safe surfaces</b><br />Road footprints are unioned into continuous pavement-free asphalt areas. Curb loops follow the resulting network boundary; bridge supports are regenerated from measured span length.</div>
    `;
  }
  for (const range of content.querySelectorAll('input[type="range"]')) updateRangeOutput(range);
}

function updateSelectionButtons() {
  const deletable = selection.type === 'edge' || selection.type === 'node';
  const button = document.querySelector('#delete-button');
  if (button) button.disabled = !deletable;
  const inspectorKind = document.querySelector('#inspector-kind');
  if (inspectorKind && selection.type === 'network') inspectorKind.textContent = 'Road network';
}

function syncLayerInputs() {
  for (const input of document.querySelectorAll('[data-layer]')) {
    const key = input.dataset.layer;
    const setting = ({ road: 'showRoadSurface', pavement: 'showPavement', curbs: 'showCurbs', bridge: 'showBridgeStructure', markings: 'showMarkings', points: 'showPoints', grid: 'grid' })[key];
    input.checked = network.settings?.[setting] !== false;
  }
}

function updateToolButtons() {
  for (const button of document.querySelectorAll('[data-tool]')) button.classList.toggle('active', button.dataset.tool === activeTool);
  document.querySelector('#snap-button')?.classList.toggle('active', network.settings.snap !== false);
  viewport.classList.toggle('draw-mode', activeTool !== 'select');
  const card = document.querySelector('#draw-card');
  const isDrawing = activeTool !== 'select';
  card.classList.toggle('hidden', !isDrawing);
  if (isDrawing) {
    const bridge = activeTool === 'bridge';
    document.querySelector('#draw-card-title').textContent = bridge ? 'Lay a bridge span' : 'Draw a road spline';
    document.querySelector('#draw-card-copy').textContent = bridge
      ? 'Place two abutments · use the gold handles to shape the bridge alignment.'
      : 'Click to place spline points · double-click or press Enter to finish.';
    document.querySelector('.draw-card-icon').innerHTML = icon(bridge ? 'bridge' : 'road');
    document.querySelector('#finish-draw').disabled = pendingPoints.length < 2;
    document.querySelector('#finish-draw').innerHTML = `${bridge ? 'Create span' : 'Finish'} <kbd>↵</kbd>`;
    document.querySelector('#viewport-hint').innerHTML = bridge
      ? '<span><b>Click</b> first abutment</span><i>·</i><span><b>Click</b> second abutment</span><i>·</i><span><b>Enter</b> finish span</span><i>·</i><span><b>Esc</b> cancel</span>'
      : '<span><b>Click</b> add spline point</span><i>·</i><span><b>Double-click</b> finish</span><i>·</i><span><b>Enter</b> commit</span><i>·</i><span><b>Esc</b> cancel</span>';
  } else {
    document.querySelector('#viewport-hint').innerHTML = '<span><b>Drag</b> orbit</span><i>·</i><span><b>Right-drag</b> pan</span><i>·</i><span><b>Scroll</b> zoom</span><i>·</i><span><b>Double-click</b> inspect</span>';
  }
}

function setSelection(nextSelection) {
  selection = nextSelection ?? { type: 'network', id: 'network' };
  scene.setSelection(selection);
  renderOutliner();
  renderInspector();
  updateSelectionButtons();
}

function setTool(tool) {
  if (!['select', 'road', 'bridge'].includes(tool)) return;
  if (activeTool !== tool) {
    scene.controls.enabled = true;
    drawMouseDown = false;
    press = null;
    activeTool = tool;
    pendingPoints = [];
    previousDrawClick = null;
    scene.setDrawPreview(null);
    cursorSnap = null;
    document.querySelector('#snap-label').classList.add('hidden');
    updateToolButtons();
  }
  setBuildStatus(tool === 'select' ? 'Generated surfaces are up to date' : tool === 'bridge' ? 'Click two abutments to place a bridge' : 'Click to place road spline points');
}

function snapCandidate(point) {
  if (!network.settings.snap) return { point, kind: null };
  const nodeHit = nearestNode(network, point, 1.4, null, false);
  if (nodeHit) return { point: { x: nodeHit.node.x, z: nodeHit.node.z }, kind: nodeHit.node.name?.toLowerCase().includes('junction') ? 'junction' : 'node', id: nodeHit.node.id };
  if (activeTool !== 'bridge') {
    let best = null;
    for (const edge of network.edges) {
      if (edge.bridge?.enabled) continue;
      const hit = nearestPointOnEdge(edge, network.nodes, point, 80);
      if (hit.distance < 1.05 && hit.t > 0.035 && hit.t < 0.965 && (!best || hit.distance < best.distance)) best = { ...hit, edgeId: edge.id };
    }
    if (best) return { point: best.point, kind: 'road', id: best.edgeId };
  }
  return { point, kind: null };
}

function updateCursor(clientX, clientY) {
  if (activeTool === 'select' || drawMouseDown) return;
  const point = scene.groundAt(clientX, clientY);
  if (!point) return;
  cursorSnap = snapCandidate(point);
  cursorPoint = cursorSnap.point;
  scene.setDrawPreview({ points: pendingPoints, cursor: cursorPoint, snapPoint: cursorSnap.kind ? cursorPoint : null });
  const label = document.querySelector('#snap-label');
  if (cursorSnap.kind) {
    label.classList.remove('hidden');
    label.querySelector('small').textContent = cursorSnap.kind;
  } else label.classList.add('hidden');
}

function addDrawPoint(point) {
  const snapped = snapCandidate(point);
  const target = snapped.point;
  if (pendingPoints.length && Math.hypot(pendingPoints.at(-1).x - target.x, pendingPoints.at(-1).z - target.z) < 0.4) return;
  if (activeTool === 'bridge' && pendingPoints.length >= 2) pendingPoints[1] = target;
  else pendingPoints.push(target);
  cursorPoint = target;
  cursorSnap = snapped;
  scene.setDrawPreview({ points: pendingPoints, cursor: target, snapPoint: snapped.kind ? target : null });
  document.querySelector('#finish-draw').disabled = pendingPoints.length < 2;
  document.querySelector('#finish-draw').innerHTML = `${activeTool === 'bridge' ? 'Create span' : 'Finish'} <kbd>↵</kbd>`;
}

function finishDrawing() {
  if (pendingPoints.length < 2) return;
  const mode = activeTool;
  const pointCopy = pendingPoints.map((point) => ({ x: point.x, z: point.z }));
  saveSnapshot();
  const result = addRoadPath(network, pointCopy, mode);
  if (!result.added) {
    const previous = history.pop();
    if (previous) network = validateNetwork(JSON.parse(previous));
    updateHistoryButtons();
    toast('The new segment needs two distinct endpoints.', 'error');
    return;
  }
  const latestEdge = network.edges.at(-1);
  selection = latestEdge ? { type: 'edge', id: latestEdge.id } : { type: 'network', id: 'network' };
  pendingPoints = [];
  previousDrawClick = null;
  activeTool = 'select';
  cursorSnap = null;
  scene.setDrawPreview(null);
  updateToolButtons();
  refreshAll();
  if (result.crossings) toast(`${result.crossings} at-grade crossing${result.crossings === 1 ? '' : 's'} merged into shared junctions.`);
  else toast(mode === 'bridge' ? 'Bridge deck and support system generated.' : 'Road spline added to the network.');
  setBuildStatus('Network rebuilt · surfaces and topology are in sync');
}

function cancelDrawing() {
  scene.controls.enabled = true;
  drawMouseDown = false;
  press = null;
  pendingPoints = [];
  previousDrawClick = null;
  cursorSnap = null;
  activeTool = 'select';
  scene.setDrawPreview(null);
  document.querySelector('#snap-label').classList.add('hidden');
  updateToolButtons();
  setBuildStatus('Drawing cancelled');
}

function nearestEdgeAt(point, bridgePreference = null) {
  let best = null;
  for (const edge of network.edges) {
    if (bridgePreference !== null && Boolean(edge.bridge?.enabled) !== bridgePreference) continue;
    const hit = nearestPointOnEdge(edge, network.nodes, point, 90);
    if (!best || hit.distance < best.distance) best = { ...hit, edge };
  }
  return best;
}

function selectFromViewport(clientX, clientY) {
  const picked = scene.pick(clientX, clientY);
  if (picked?.type === 'node') {
    setSelection({ type: 'node', id: picked.id });
    return;
  }
  if (picked?.type === 'handle') {
    setSelection({ type: 'edge', id: picked.edgeId });
    return;
  }
  const surfaceHit = scene.roadHit(clientX, clientY);
  const point = surfaceHit ?? scene.groundAt(clientX, clientY);
  if (!point) return;
  const roadHit = nearestEdgeAt(point, surfaceHit ? surfaceHit.bridge : null);
  if (roadHit && roadHit.distance < (Number(roadHit.edge.laneCount ?? 2) * Number(roadHit.edge.laneWidth ?? 3.5)) / 2 + 2.8) {
    setSelection({ type: 'edge', id: roadHit.edge.id });
  } else {
    setSelection({ type: 'network', id: 'network' });
  }
}

function startDrag(picked, clientX, clientY, event) {
  const pointerGround = scene.groundAt(clientX, clientY);
  if (!pointerGround) return false;
  if (picked?.type === 'node' && network.nodes[picked.id]) {
    drag = { type: 'node', id: picked.id, start: pointerGround, origin: { x: network.nodes[picked.id].x, z: network.nodes[picked.id].z }, moved: false, saved: false };
    setSelection({ type: 'node', id: picked.id });
  } else if (picked?.type === 'handle') {
    const edge = network.edges.find((item) => item.id === picked.edgeId);
    if (!edge) return false;
    drag = { type: 'handle', edgeId: picked.edgeId, handle: picked.handle, start: pointerGround, origin: { ...edge[picked.handle] }, moved: false, saved: false };
    setSelection({ type: 'edge', id: picked.edgeId });
  } else return false;
  scene.controls.enabled = false;
  scene.renderer.domElement.setPointerCapture(event.pointerId);
  return true;
}

function handlePointerMove(event) {
  if (drag) {
    const point = scene.groundAt(event.clientX, event.clientY);
    if (!point) return;
    const movedDistance = Math.hypot(point.x - drag.start.x, point.z - drag.start.z);
    if (movedDistance < 0.08 && !drag.moved) return;
    if (!drag.saved) {
      saveSnapshot();
      drag.saved = true;
    }
    drag.moved = true;
    if (drag.type === 'node') {
      let target = { x: drag.origin.x + (point.x - drag.start.x), z: drag.origin.z + (point.z - drag.start.z) };
      if (network.settings.snap) {
        const snap = nearestNode(network, target, 0.65, drag.id, false);
        if (snap) target = { x: snap.node.x, z: snap.node.z };
      }
      moveNode(network, drag.id, target);
    } else {
      const edge = network.edges.find((item) => item.id === drag.edgeId);
      if (!edge) return;
      edge.autoCurve = false;
      edge[drag.handle] = { x: drag.origin.x + (point.x - drag.start.x), z: drag.origin.z + (point.z - drag.start.z) };
    }
    scene.setNetwork(network, selection);
    renderOutliner();
    updateMetrics();
    updateProjectLabels();
    renderInspector();
    return;
  }
  if (activeTool !== 'select') updateCursor(event.clientX, event.clientY);
}

function handlePointerUp(event) {
  if (drag) {
    const completed = drag;
    drag = null;
    scene.controls.enabled = true;
    if (completed.moved) {
      mergePlanarCrossings(network);
      refreshAll('Spline geometry updated');
    } else if (completed.saved && history.length) {
      history.pop();
      updateHistoryButtons();
    }
    return;
  }
  if (activeTool !== 'select' && drawMouseDown) {
    drawMouseDown = false;
    scene.controls.enabled = true;
    press = null;
    if (!event || event.button !== 0) return;
    const point = scene.groundAt(event.clientX, event.clientY);
    if (!point) return;
    const now = Date.now();
    const closeRepeat = previousDrawClick && now - previousDrawClick.time < 330
      && Math.hypot(event.clientX - previousDrawClick.x, event.clientY - previousDrawClick.y) < 8;
    if (closeRepeat) {
      previousDrawClick = null;
      if (pendingPoints.length >= 2) finishDrawing();
      return;
    }
    addDrawPoint(point);
    previousDrawClick = { time: now, x: event.clientX, y: event.clientY };
    return;
  }
  if (event?.button === 0 && press && !press.dragged && activeTool === 'select') selectFromViewport(event.clientX, event.clientY);
  press = null;
}

let press = null;
const canvas = scene.renderer.domElement;
canvas.addEventListener('pointerdown', (event) => {
  if (event.button !== 0) return;
  if (activeTool !== 'select') {
    drawMouseDown = true;
    press = { x: event.clientX, y: event.clientY, dragged: false };
    scene.controls.enabled = false;
    canvas.setPointerCapture(event.pointerId);
    return;
  }
  const picked = scene.pick(event.clientX, event.clientY);
  if ((picked?.type === 'node' || picked?.type === 'handle') && startDrag(picked, event.clientX, event.clientY, event)) {
    press = null;
    return;
  }
  press = { x: event.clientX, y: event.clientY, dragged: false };
});
canvas.addEventListener('pointermove', (event) => {
  if (press && Math.hypot(event.clientX - press.x, event.clientY - press.y) > 5) press.dragged = true;
  handlePointerMove(event);
});
canvas.addEventListener('pointerup', handlePointerUp);
canvas.addEventListener('pointercancel', (event) => {
  if (drag) {
    drag = null;
    scene.controls.enabled = true;
  }
  if (drawMouseDown) {
    drawMouseDown = false;
    scene.controls.enabled = true;
  }
  press = null;
  try { canvas.releasePointerCapture(event.pointerId); } catch { /* pointer was already released */ }
});
canvas.addEventListener('pointerleave', () => {
  if (activeTool !== 'select' && !drawMouseDown) {
    cursorPoint = null;
    scene.setDrawPreview({ points: pendingPoints });
    document.querySelector('#snap-label').classList.add('hidden');
  }
});

function updateRangeOutput(input) {
  const scope = input.dataset.bridgeProp ? 'bridge' : input.dataset.defaultProp ? 'default' : 'prop';
  const prop = input.dataset.bridgeProp ?? input.dataset.defaultProp ?? input.dataset.prop;
  const output = document.querySelector(`[data-range-output="${prop}"][data-range-scope="${scope}"]`);
  if (!output) return;
  const value = Number(input.value);
  if (prop === 'rampFraction') output.textContent = `${Math.round(value * 100)}% span`;
  else if (prop === 'curveTension') output.textContent = `${value.toFixed(2)}×`;
  else output.textContent = `${displayNumber(value, Number(input.step) < 1 ? 2 : 0)} m`;
  const range = (value - Number(input.min)) / (Number(input.max) - Number(input.min));
  input.style.setProperty('--range-progress', `${Math.max(0, Math.min(1, range)) * 100}%`);
}

function normalizeInputValue(prop, value) {
  if (typeof value === 'boolean') return value;
  if (['laneCount'].includes(prop)) return Math.max(1, Math.min(8, Math.round(Number(value))));
  if (['laneWidth'].includes(prop)) return Math.max(2.4, Math.min(5.2, Number(value)));
  if (prop === 'curveTension') return Math.max(0.25, Math.min(1.8, Number(value)));
  return Number.isFinite(Number(value)) ? Number(value) : value;
}

function applyTension(edge, value) {
  const start = network.nodes[edge.from];
  const end = network.nodes[edge.to];
  if (!start || !end) return;
  const linearC1 = { x: start.x + (end.x - start.x) / 3, z: start.z + (end.z - start.z) / 3 };
  const linearC2 = { x: start.x + ((end.x - start.x) * 2) / 3, z: start.z + ((end.z - start.z) * 2) / 3 };
  const previous = Number(edge.curveTension ?? 1);
  const ratio = previous > 1e-4 ? value / previous : 0;
  edge.c1 = { x: linearC1.x + (edge.c1.x - linearC1.x) * ratio, z: linearC1.z + (edge.c1.z - linearC1.z) * ratio };
  edge.c2 = { x: linearC2.x + (edge.c2.x - linearC2.x) * ratio, z: linearC2.z + (edge.c2.z - linearC2.z) * ratio };
  edge.curveTension = value;
}

function handleControlChange(input, eventType) {
  const isLive = input.type === 'range' || input.type === 'number';
  const scope = input.dataset.bridgeProp ? 'bridge' : input.dataset.defaultProp ? 'default' : input.dataset.nodeProp ? 'node' : input.dataset.settingProp ? 'setting' : input.dataset.handleProp ? 'handle' : 'prop';
  const prop = input.dataset.bridgeProp ?? input.dataset.defaultProp ?? input.dataset.nodeProp ?? input.dataset.settingProp ?? input.dataset.handleProp ?? input.dataset.prop;
  if (!prop) return;
  const key = `${scope}:${selection.type}:${selection.id}:${prop}`;
  if (eventType === 'input') {
    if (!isLive) return;
    beginLiveEdit(key);
  } else if (eventType === 'change') {
    if (isLive && liveEditKey !== key) beginLiveEdit(key);
    if (!isLive) beginLiveEdit(key);
  }

  let value = input.type === 'checkbox' ? input.checked : input.type === 'number' || input.type === 'range' ? Number(input.value) : input.value;
  value = normalizeInputValue(prop, value);

  if (scope === 'node') {
    const node = network.nodes[selection.id];
    if (node) {
      const next = { x: node.x, z: node.z };
      next[prop] = value;
      moveNode(network, selection.id, next);
    }
  } else if (scope === 'handle') {
    const edge = selectedEdge();
    if (edge && ['c1x', 'c1z', 'c2x', 'c2z'].includes(prop)) {
      const name = prop.startsWith('c1') ? 'c1' : 'c2';
      const axis = prop.endsWith('x') ? 'x' : 'z';
      edge.autoCurve = false;
      edge[name][axis] = value;
    }
  } else if (scope === 'bridge') {
    const edge = selectedEdge();
    if (edge?.bridge) edge.bridge[prop] = value;
  } else if (scope === 'default') {
    network.settings.defaultProfile[prop] = value;
  } else if (scope === 'setting') {
    network.settings[prop] = value;
  } else if (scope === 'prop') {
    const edge = selectedEdge();
    if (edge) {
      if (prop === 'curveTension') applyTension(edge, value);
      else if (prop === 'autoCurve' && value) {
        const start = network.nodes[edge.from];
        const end = network.nodes[edge.to];
        edge.c1 = { x: start.x + (end.x - start.x) / 3, z: start.z + (end.z - start.z) / 3 };
        edge.c2 = { x: start.x + ((end.x - start.x) * 2) / 3, z: start.z + ((end.z - start.z) * 2) / 3 };
        edge.autoCurve = true;
      } else if (prop === 'autoCurve') edge.autoCurve = false;
      else edge[prop] = value;
    }
  }

  if (input.type === 'range') updateRangeOutput(input);
  if (eventType === 'change' && (scope === 'node' || scope === 'handle' || (scope === 'prop' && ['curveTension', 'autoCurve'].includes(prop)))) mergePlanarCrossings(network);
  if (scope === 'setting' && prop === 'grid') scene.setVisibility(network.settings);
  if (scope === 'setting' && prop === 'snap') updateToolButtons();
  scene.setNetwork(network, selection);
  renderOutliner();
  updateMetrics();
  updateProjectLabels();
  if (eventType === 'change') {
    finishLiveEdit();
    renderInspector();
  }
}

const inspectorContent = document.querySelector('#inspector-content');
inspectorContent.addEventListener('input', (event) => {
  const input = event.target.closest('[data-prop],[data-bridge-prop],[data-default-prop],[data-node-prop],[data-setting-prop],[data-handle-prop]');
  if (input) handleControlChange(input, 'input');
});
inspectorContent.addEventListener('change', (event) => {
  const input = event.target.closest('[data-prop],[data-bridge-prop],[data-default-prop],[data-node-prop],[data-setting-prop],[data-handle-prop]');
  if (input) handleControlChange(input, 'change');
});
inspectorContent.addEventListener('click', (event) => {
  const action = event.target.closest('[data-action]');
  if (action) handleInspectorAction(action.dataset.action);
  const row = event.target.closest('[data-select-type]');
  if (row) setSelection({ type: row.dataset.selectType, id: row.dataset.selectId });
});

function handleInspectorAction(action) {
  const edge = selectedEdge();
  if (action === 'enable-bridge' && edge) {
    saveSnapshot();
    edge.bridge = { enabled: true, height: 7, rampFraction: 0.2, deckType: 'box-girder', supportType: 'twin-column', supportSpacing: 12, parapet: true };
    refreshAll('Bridge structure generated');
    toast('Bridge deck, parapets and procedural supports added.');
  } else if (action === 'disable-bridge' && edge) {
    saveSnapshot();
    edge.bridge = null;
    mergePlanarCrossings(network);
    refreshAll('Span converted to an at-grade road');
    toast('Bridge converted to road. At-grade crossings were merged.');
  } else if (action === 'focus-edge' && edge) {
    const points = [network.nodes[edge.from], network.nodes[edge.to]];
    const center = new THREE.Vector3((points[0].x + points[1].x) / 2, edge.bridge?.enabled ? Number(edge.bridge.height) * 0.45 : 1, (points[0].z + points[1].z) / 2);
    scene.controls.target.copy(center);
    scene.controls.update();
  } else if (action === 'rename-edge' && edge) {
    const name = window.prompt('Name this corridor', edge.name);
    if (name?.trim()) {
      saveSnapshot();
      edge.name = name.trim();
      refreshAll('Corridor renamed');
    }
  } else if (action === 'reset-handles' && edge) {
    saveSnapshot();
    const start = network.nodes[edge.from];
    const end = network.nodes[edge.to];
    edge.c1 = { x: start.x + (end.x - start.x) / 3, z: start.z + (end.z - start.z) / 3 };
    edge.c2 = { x: start.x + ((end.x - start.x) * 2) / 3, z: start.z + ((end.z - start.z) * 2) / 3 };
    edge.autoCurve = true;
    edge.curveTension = 1;
    refreshAll('Spline tangents reset');
  }
}

document.querySelector('#outliner-tree').addEventListener('click', (event) => {
  const row = event.target.closest('[data-select-type]');
  if (row) setSelection({ type: row.dataset.selectType, id: row.dataset.selectId });
});
document.querySelector('#outliner-search').addEventListener('input', renderOutliner);
document.querySelector('#expand-tree').addEventListener('click', () => {
  expandedTree = !expandedTree;
  renderOutliner();
});

document.querySelectorAll('[data-tool]').forEach((button) => button.addEventListener('click', () => setTool(button.dataset.tool)));
document.querySelector('#add-road-shortcut').addEventListener('click', () => setTool('road'));
document.querySelector('#cancel-draw').addEventListener('click', cancelDrawing);
document.querySelector('#finish-draw').addEventListener('click', finishDrawing);
document.querySelector('#undo-button').addEventListener('click', undo);
document.querySelector('#redo-button').addEventListener('click', redo);
document.querySelector('#snap-button').addEventListener('click', () => {
  saveSnapshot();
  network.settings.snap = !network.settings.snap;
  scene.setVisibility(network.settings);
  updateToolButtons();
  persistNetwork();
  toast(network.settings.snap ? 'Road and node snapping enabled.' : 'Snapping disabled.');
});

document.querySelectorAll('[data-layer]').forEach((input) => input.addEventListener('change', () => {
  const key = input.dataset.layer;
  const setting = ({ road: 'showRoadSurface', pavement: 'showPavement', curbs: 'showCurbs', bridge: 'showBridgeStructure', markings: 'showMarkings', points: 'showPoints', grid: 'grid' })[key];
  saveSnapshot();
  network.settings[setting] = input.checked;
  if (setting === 'showPoints') scene.rebuildHelpers();
  scene.setVisibility(network.settings);
  updateToolButtons();
  persistNetwork();
}));

document.querySelector('#save-project').addEventListener('click', () => {
  persistNetwork();
  window.clearTimeout(saveTimer);
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(network));
    document.querySelector('#save-status').textContent = 'Saved locally';
    document.querySelector('.document-status .status-dot').style.background = 'var(--green)';
    toast('Project saved in this browser.');
  } catch {
    toast('Browser storage is full. Export a project file to keep your work.', 'error');
  }
});
document.querySelector('#project-name').addEventListener('click', () => {
  const name = window.prompt('Rename this road network', network.name || 'Untitled network');
  if (name?.trim()) {
    saveSnapshot();
    network.name = name.trim();
    refreshAll('Project renamed');
  }
});

document.querySelector('#export-menu-button').addEventListener('click', (event) => {
  event.stopPropagation();
  document.querySelector('#export-menu').classList.toggle('hidden');
  document.querySelector('#viewport-menu').classList.add('hidden');
});
document.querySelector('#viewport-menu-button').addEventListener('click', (event) => {
  event.stopPropagation();
  document.querySelector('#viewport-menu').classList.toggle('hidden');
  document.querySelector('#export-menu').classList.add('hidden');
});
document.addEventListener('click', (event) => {
  if (!event.target.closest('.menu-anchor')) document.querySelector('#export-menu').classList.add('hidden');
  if (!event.target.closest('.toolbar-menu-anchor')) document.querySelector('#viewport-menu').classList.add('hidden');
});

document.querySelector('#export-json').addEventListener('click', () => {
  downloadFile(JSON.stringify(network, null, 2), `${fileBaseName(network.name)}.rwn.json`, 'application/json');
  document.querySelector('#export-menu').classList.add('hidden');
  toast('Editable RoadWorks project exported.');
});
document.querySelector('#export-obj').addEventListener('click', () => {
  downloadFile(exportObj(), `${fileBaseName(network.name)}.obj`, 'text/plain');
  document.querySelector('#export-menu').classList.add('hidden');
  toast('Generated road and bridge mesh exported as OBJ.');
});
document.querySelector('#export-object-button').addEventListener('click', () => {
  downloadFile(exportObj(), `${fileBaseName(network.name)}.obj`, 'text/plain');
  toast('Generated road and bridge mesh exported as OBJ.');
});
document.querySelector('#import-project').addEventListener('click', () => document.querySelector('#project-file').click());
document.querySelector('#project-file').addEventListener('change', async (event) => {
  const file = event.target.files?.[0];
  if (!file) return;
  try {
    const imported = validateNetwork(JSON.parse(await file.text()));
    saveSnapshot();
    network = imported;
    selection = { type: 'network', id: 'network' };
    activeTool = 'select';
    scene.setNetwork(network, selection);
    scene.fit(network);
    renderAll();
    persistNetwork();
    toast(`Loaded ${file.name}.`);
  } catch (error) {
    toast(error.message || 'Could not read that project file.', 'error');
  } finally {
    event.target.value = '';
  }
});

function fileBaseName(value) {
  return (value || 'roadworks-network').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'roadworks-network';
}

function downloadFile(content, filename, type) {
  const blob = new Blob([content], { type });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = filename;
  link.click();
  window.setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function exportObj() {
  const objectGroups = [scene.layers.groundRoad, scene.layers.bridgeRoad, scene.layers.pavement, scene.layers.curbs, scene.layers.markings, scene.layers.bridgeStructure];
  scene.world.updateMatrixWorld(true);
  const lines = ['# RoadWorks Editor generated corridor mesh', `# ${network.name}`, '# Units: metres'];
  let nextVertex = 1;
  for (const group of objectGroups) {
    group.updateMatrixWorld(true);
    group.traverse((object) => {
      if (!object.isMesh || !object.geometry?.attributes?.position) return;
      const geometry = object.geometry;
      const position = geometry.attributes.position;
      const index = geometry.index;
      lines.push(`o ${fileBaseName(object.name || 'mesh')}`);
      const base = nextVertex;
      const vertex = new THREE.Vector3();
      for (let i = 0; i < position.count; i += 1) {
        vertex.fromBufferAttribute(position, i).applyMatrix4(object.matrixWorld);
        lines.push(`v ${vertex.x.toFixed(4)} ${vertex.y.toFixed(4)} ${vertex.z.toFixed(4)}`);
      }
      const elementCount = index ? index.count : position.count;
      for (let i = 0; i + 2 < elementCount; i += 3) {
        const a = base + (index ? index.getX(i) : i);
        const b = base + (index ? index.getX(i + 1) : i + 1);
        const c = base + (index ? index.getX(i + 2) : i + 2);
        lines.push(`f ${a} ${b} ${c}`);
      }
      nextVertex += position.count;
    });
  }
  return `${lines.join('\n')}\n`;
}

document.querySelector('#delete-button').addEventListener('click', () => {
  if (selection.type !== 'edge' && selection.type !== 'node') return;
  saveSnapshot();
  if (!deleteSelection(network, selection)) return;
  selection = { type: 'network', id: 'network' };
  mergePlanarCrossings(network);
  refreshAll('Selection removed');
  toast('Selected road graph object deleted.');
});

document.querySelector('#rebuild-button').addEventListener('click', () => {
  setBuildStatus('Rebuilding surfaces and solving intersections…', 'building');
  window.setTimeout(() => {
    const result = mergePlanarCrossings(network);
    scene.setNetwork(network, selection);
    renderAll();
    persistNetwork();
    setBuildStatus('Generated surfaces are up to date');
    toast(result.crossings ? `${result.crossings} crossing${result.crossings === 1 ? '' : 's'} merged into the graph.` : 'Road, pavement, curb and bridge geometry rebuilt.');
  }, 24);
});

document.querySelector('#fit-view-button').addEventListener('click', () => scene.fit(network));
document.querySelector('#frame-button').addEventListener('click', () => {
  scene.fit(network);
  document.querySelector('#viewport-menu').classList.add('hidden');
});
document.querySelector('#reset-view-button').addEventListener('click', () => {
  scene.fit(network);
  document.querySelector('#viewport-menu').classList.add('hidden');
});
document.querySelector('#plan-view-button').addEventListener('click', () => {
  planView = !planView;
  scene.setPlanView(planView);
  document.querySelector('#plan-view-button').classList.toggle('active', planView);
  document.querySelector('#view-mode-label').textContent = planView ? 'PLAN VIEW' : 'PERSPECTIVE';
});
document.querySelector('#zoom-in-button').addEventListener('click', () => zoomCamera(0.83));
document.querySelector('#zoom-out-button').addEventListener('click', () => zoomCamera(1.2));
document.querySelector('#show-grid-button').addEventListener('click', () => {
  saveSnapshot();
  network.settings.grid = !network.settings.grid;
  scene.setVisibility(network.settings);
  syncLayerInputs();
  persistNetwork();
  document.querySelector('#viewport-menu').classList.add('hidden');
});
function zoomCamera(factor) {
  const target = scene.controls.target;
  scene.camera.position.sub(target).multiplyScalar(factor).add(target);
  scene.controls.update();
  const zoom = Math.round((56 / scene.camera.position.distanceTo(target)) * 100);
  document.querySelector('#zoom-readout').textContent = `${zoom}%`;
}

document.querySelector('#inspector-more').addEventListener('click', () => {
  toast('Tip: use the viewport tools to add connected roads and grade-separated bridges.');
});

window.addEventListener('keydown', (event) => {
  const target = event.target;
  const isEditing = target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement || target?.isContentEditable;
  if (isEditing) return;
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z') {
    event.preventDefault();
    if (event.shiftKey) redo(); else undo();
    return;
  }
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'y') {
    event.preventDefault();
    redo();
    return;
  }
  const key = event.key.toLowerCase();
  if (key === 'v') setTool('select');
  else if (key === 'r') setTool('road');
  else if (key === 'b') setTool('bridge');
  else if (key === 'f') scene.fit(network);
  else if (key === 'enter' && activeTool !== 'select') finishDrawing();
  else if (key === 'escape' && activeTool !== 'select') cancelDrawing();
  else if ((key === 'delete' || key === 'backspace') && (selection.type === 'edge' || selection.type === 'node')) document.querySelector('#delete-button').click();
});

window.addEventListener('beforeunload', () => {
  try { localStorage.setItem(STORAGE_KEY, JSON.stringify(network)); } catch { /* storage may be blocked */ }
});

// Keep the range fill in sync after a panel render.
inspectorContent.addEventListener('input', (event) => {
  if (event.target.type === 'range') updateRangeOutput(event.target);
});

function updateProjectTitleFromName() {
  updateProjectLabels();
}

// Expose a tiny debug surface for local smoke tests without polluting the UI.
window.roadWorksDebug = {
  get network() { return network; },
  get selection() { return selection; },
  get scene() { return scene; },
  get metrics() { return getNetworkMetrics(network); },
  addRoad(points) { const result = addRoadPath(network, points); refreshAll(); return result; },
};
