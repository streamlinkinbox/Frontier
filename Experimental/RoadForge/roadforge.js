const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const clamp = (n, lo, hi) => Math.max(lo, Math.min(hi, n));
const mix = (a, b, t) => a + (b - a) * t;
const V = (x = 0, y = 0, z = 0) => ({ x, y, z });
const add = (a, b) => V(a.x + b.x, a.y + b.y, a.z + b.z);
const sub = (a, b) => V(a.x - b.x, a.y - b.y, a.z - b.z);
const mul = (a, s) => V(a.x * s, a.y * s, a.z * s);
const dot = (a, b) => a.x * b.x + a.y * b.y + a.z * b.z;
const cross = (a, b) => V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
const length = a => Math.hypot(a.x, a.y, a.z);
const normalize = a => { const d = length(a) || 1; return mul(a, 1 / d); };
const horizontalLength = a => Math.hypot(a.x, a.z);
const icon = (name, extra = '') => `<svg class="icon ${extra}" aria-hidden="true"><use href="#i-${name}"></use></svg>`;
const ICON = { road: 'roadmark', junction: 'intersection', scene: 'grid', rail: 'train', car: 'car', surface: 'layers', bridge: 'bridge', damage: 'wrench', object: 'cube' };

const FEATURE_DEFAULTS = {
  markings: true, sidewalks: true, blocks: true, signals: true, damage: true,
  construction: true, rail: true, bridge: true, streetlights: true, parking: true,
  bollards: true, billboards: true, robots: true, traffic: true, train: true, network: true, streetscape: true,
};
const LAYER_DEFAULTS = [
  { id: 'road-base', name: 'Road base · asphalt', detail: 'PBR / ROUGHNESS 0.78', value: 0.94, enabled: true },
  { id: 'aggregate', name: 'Aggregate & grain', detail: 'PROCEDURAL / 4K', value: 0.72, enabled: true },
  { id: 'weathering', name: 'Weathering · oil wash', detail: 'MASK / 2K', value: 0.36, enabled: true },
  { id: 'cracks', name: 'Cracks & potholes', detail: 'DAMAGE / HYBRID', value: 0.24, enabled: true },
  { id: 'lane-paint', name: 'Thermoplastic markings', detail: 'ROAD MARKING / 1K', value: 0.82, enabled: true },
  { id: 'paving', name: 'Concrete paving', detail: 'PAVERS / 2K', value: 0.78, enabled: true },
];
const layerColors = { 'road-base': '#7d7c74', aggregate: '#baaa82', weathering: '#a37453', cracks: '#9d8068', 'lane-paint': '#d3c5a9', paving: '#a8a8a1' };
const materialNames = ['TERRAIN', 'ASPHALT', 'PAVING', 'MARKING', 'METAL', 'PAINT', 'GLASS', 'RAIL', 'REPAIR', 'EMISSIVE'];

const state = {
  preset: 'urban', seed: 1842, density: 0.68, blockScale: 18, trafficSide: 'right', region: 'na',
  features: { ...FEATURE_DEFAULTS }, trafficCount: 8, playing: true, wetness: 0.14,
  layers: LAYER_DEFAULTS.map(layer => ({ ...layer })), selectedLayer: 'aggregate',
  selected: { type: 'node', id: null }, tool: 'select', mode: 'network',
  drawStart: null, measureStart: null, showGrid: true,
};
let network = null;
let compiled = { edgePaths: new Map(), nodeArms: new Map(), junctions: new Map(), bounds: null };
let staticMesh = null;
let trafficMesh = null;
let staticBuffer = null;
let trafficBuffer = null;
let staticVertexCount = 0;
let trafficVertexCount = 0;
let gpu = null;
let fallbackContext = null;
let adapterInfo = null;
let resizeObserver = null;
let depthTexture = null;
let frameUniformBuffer = null;
let frameBindGroup = null;
let renderPipeline = null;
let gpuFormat = null;
let canvas = $('#viewport');
let viewportWrap = $('#viewportWrap');
let canvasWidth = 1;
let canvasHeight = 1;
let frameNumber = 0;
let fpsTimes = [];
let fpsValue = 0;
let lastFrame = 0;
let trafficRefreshTime = 0;
let geometryDirty = false;
let geometryRebuildAfter = 0;
let editorDrag = null;
let camera = { target: V(0, 0, 0), yaw: -0.78, pitch: 0.72, distance: 89, minDistance: 23, maxDistance: 180, top: false };
let cameraMatrices = { view: null, projection: null, viewProjection: null, inverseViewProjection: null };
let trafficAgents = [];
let parkedVehicleCount = 0;
let trainClock = 8.7;
let simulationSeconds = 0;
let selectedExport = 'obj';
let menuOpen = false;
let gridVisible = true;
let savedTimer = 0;
const undoStack = [];
const redoStack = [];

function seededRandom(seed) {
  let t = (seed >>> 0) + 0x6D2B79F5;
  return () => {
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
function hexToLinear(hex) {
  let s = hex.replace('#', '');
  if (s.length === 3) s = s.split('').map(c => c + c).join('');
  const v = [0, 2, 4].map(i => parseInt(s.slice(i, i + 2), 16) / 255);
  return v.map(c => c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);
}
function saveSnapshot() {
  if (!network) return;
  undoStack.push(JSON.stringify(network));
  if (undoStack.length > 40) undoStack.shift();
  redoStack.length = 0;
  updateUndoButtons();
}
function commitHistory(before) {
  if (before && before !== JSON.stringify(network)) {
    undoStack.push(before);
    if (undoStack.length > 40) undoStack.shift();
    redoStack.length = 0;
  }
  updateUndoButtons();
}
function undo() {
  if (!undoStack.length) return;
  redoStack.push(JSON.stringify(network));
  network = JSON.parse(undoStack.pop());
  state.selected = { type: 'node', id: network.nodes.find(n => n.degree >= 3)?.id ?? network.nodes[0]?.id };
  rebuildScene(); updateInspector(); updateOutliner(); updateMiniMap(); updateUndoButtons(); markSaved(false); toast('Undo applied', 'The previous road graph has been restored.');
}
function redo() {
  if (!redoStack.length) return;
  undoStack.push(JSON.stringify(network));
  network = JSON.parse(redoStack.pop());
  state.selected = { type: 'node', id: network.nodes.find(n => n.degree >= 3)?.id ?? network.nodes[0]?.id };
  rebuildScene(); updateInspector(); updateOutliner(); updateMiniMap(); updateUndoButtons(); markSaved(false); toast('Redo applied', 'The road graph change has been restored.');
}
function updateUndoButtons() {
  $('#undoButton').disabled = undoStack.length === 0;
  $('#redoButton').disabled = redoStack.length === 0;
}
function markSaved(saved = false) {
  const indicator = $('#saveIndicator');
  indicator.classList.toggle('unsaved', !saved);
  indicator.innerHTML = saved
    ? '<span class="save-dot"></span><span>Saved locally</span>'
    : '<span class="save-dot"></span><span>Unsaved changes</span>';
  clearTimeout(savedTimer);
  if (!saved) savedTimer = setTimeout(persistProject, 800);
}
function persistProject() {
  try {
    localStorage.setItem('roadforge-project-v1', JSON.stringify({
      preset: state.preset, seed: state.seed, density: state.density, blockScale: state.blockScale,
      trafficSide: state.trafficSide, region: state.region, features: state.features,
      trafficCount: state.trafficCount, wetness: state.wetness, layers: state.layers,
      network,
    }));
    markSaved(true);
    $('#projectSize').textContent = 'LOCAL';
  } catch (error) {
    console.warn('RoadForge could not persist this project locally.', error);
    $('#projectSize').textContent = 'MEMORY';
  }
}
function restoreProject() {
  try {
    const raw = localStorage.getItem('roadforge-project-v1');
    if (!raw) return false;
    const saved = JSON.parse(raw);
    if (!saved || !saved.network || !Array.isArray(saved.network.nodes) || !Array.isArray(saved.network.edges)) return false;
    Object.assign(state, {
      preset: saved.preset || 'urban', seed: Number(saved.seed) || 1842,
      density: Number(saved.density) || 0.68, blockScale: Number(saved.blockScale) || 18,
      trafficSide: saved.trafficSide || 'right', region: saved.region || 'na',
      features: { ...FEATURE_DEFAULTS, ...(saved.features || {}) }, trafficCount: clamp(Number(saved.trafficCount) || 0, 0, 25),
      wetness: Number.isFinite(saved.wetness) ? saved.wetness : .14,
      layers: Array.isArray(saved.layers) ? saved.layers : LAYER_DEFAULTS.map(layer => ({ ...layer })),
    });
    network = saved.network;
    state.preset = network.preset || state.preset;
    return true;
  } catch (error) {
    console.warn('RoadForge saved project was ignored.', error);
    return false;
  }
}

function makeNetwork(preset = state.preset, seed = state.seed, density = state.density, blockScale = state.blockScale) {
  const random = seededRandom((Number(seed) || 1) >>> 0);
  const nodes = [];
  const edges = [];
  const blocks = [];
  let nextNode = 1;
  let nextEdge = 1;
  const addNode = (x, z, options = {}) => {
    const node = {
      id: `node-${String(nextNode++).padStart(3, '0')}`,
      x, y: options.y ?? 0, z,
      name: options.name || `Junction ${String(nextNode - 1).padStart(2, '0')}`,
      kind: options.kind || 'intersection', radius: options.radius ?? 2.15,
      signalized: options.signalized ?? true,
    };
    nodes.push(node);
    return node;
  };
  const addEdge = (a, b, kind = 'local', options = {}) => {
    const laneCount = options.lanes ?? (kind === 'highway' ? 6 : kind === 'arterial' ? 4 : 2);
    const laneWidth = options.laneWidth ?? (state.region === 'uk' ? 3.05 : state.region === 'eu' ? 3.25 : 3.3);
    const shoulder = options.shoulder ?? (kind === 'highway' ? 1.35 : kind === 'arterial' ? 0.9 : 0.55);
    const edge = {
      id: `road-${String(nextEdge++).padStart(3, '0')}`,
      a: a.id, b: b.id, kind, name: options.name || (kind === 'highway' ? 'NORTH QUAY EXPRESS' : kind === 'arterial' ? 'HARBOR AVENUE' : 'LOCAL STREET'),
      lanes: laneCount, laneWidth, shoulder, width: options.width ?? laneCount * laneWidth + shoulder * 2,
      speed: options.speed ?? (kind === 'highway' ? 100 : kind === 'arterial' ? 50 : 30),
      bend: options.bend ?? ((random() - .5) * (kind === 'local' ? 1.25 : .65)),
      cornerRadius: options.cornerRadius ?? 2.15,
      oneWay: options.oneWay ?? false,
      surface: options.surface || 'asphalt',
    };
    edges.push(edge);
    return edge;
  };

  if (preset === 'suburban') {
    const hub = addNode(0, 0, { name: 'Three-way / West Grove', radius: 3.1 });
    const directions = [0, Math.PI * 2 / 3, Math.PI * 4 / 3];
    const ends = directions.map((angle, i) => addNode(Math.cos(angle) * 42, Math.sin(angle) * 42, { name: `Arterial end ${i + 1}`, kind: 'terminal', signalized: false }));
    directions.forEach((_, i) => {
      const middle = addNode(Math.cos(directions[i]) * 20, Math.sin(directions[i]) * 20, { name: `Collector ${i + 1}` });
      addEdge(hub, middle, 'arterial', { lanes: 2, width: 8.8, speed: 45, bend: (random() - .5) * 2 });
      addEdge(middle, ends[i], 'arterial', { lanes: 2, width: 8.8, speed: 45, bend: (random() - .5) * 2 });
      const dir = directions[i];
      const side = V(-Math.sin(dir), 0, Math.cos(dir));
      const spur = addNode(middle.x + side.x * 11, middle.z + side.z * 11, { name: `Crescent ${i + 1}` });
      const cul = addNode(middle.x + side.x * 22 + Math.cos(dir) * 2, middle.z + side.z * 22 + Math.sin(dir) * 2, { name: `Cul-de-sac ${i + 1}`, kind: 'terminal', signalized: false });
      addEdge(middle, spur, 'local', { bend: -1.2 + random() * 2.4, speed: 30 });
      addEdge(spur, cul, 'local', { bend: .8 - random() * 1.6, speed: 20 });
      const drive = addNode(ends[i].x + side.x * 8, ends[i].z + side.z * 8, { name: `Access spur ${i + 1}`, kind: 'terminal', signalized: false });
      addEdge(ends[i], drive, 'local', { width: 6.4, lanes: 2, speed: 20, bend: 1.4 });
    });
    for (let i = 0; i < 8; i++) {
      const angle = (i / 8) * Math.PI * 2;
      const radius = 43;
      const x = Math.cos(angle) * radius;
      const z = Math.sin(angle) * radius;
      const x2 = x + Math.cos(angle + .3) * (7 + random() * 4);
      const z2 = z + Math.sin(angle + .3) * (7 + random() * 4);
      blocks.push({ x0: Math.min(x, x2), x1: Math.max(x, x2), z0: Math.min(z, z2), z1: Math.max(z, z2), style: i % 3, seed: i });
    }
  } else if (preset === 'highway') {
    const westLow = addNode(-47, 0, { name: 'West approach', kind: 'terminal', signalized: false });
    const westHigh = addNode(-20, 3.5, { name: 'West flyover', radius: 2.5, signalized: false });
    const crown = addNode(0, 3.5, { name: 'Grade separation / A', radius: 3.2, signalized: false });
    const eastHigh = addNode(20, 3.5, { name: 'East flyover', radius: 2.5, signalized: false });
    const eastLow = addNode(47, 0, { name: 'East approach', kind: 'terminal', signalized: false });
    addEdge(westLow, westHigh, 'highway', { bend: 0, lanes: 6, width: 20.4, speed: 100, shoulder: 1.5 });
    addEdge(westHigh, crown, 'highway', { bend: 0, lanes: 6, width: 20.4, speed: 100, shoulder: 1.5 });
    addEdge(crown, eastHigh, 'highway', { bend: 0, lanes: 6, width: 20.4, speed: 100, shoulder: 1.5 });
    addEdge(eastHigh, eastLow, 'highway', { bend: 0, lanes: 6, width: 20.4, speed: 100, shoulder: 1.5 });
    const north = addNode(0, -34, { name: 'North arterial', kind: 'terminal', signalized: false });
    const cross = addNode(0, 0, { name: 'Ground crossing / B', radius: 2.6 });
    const south = addNode(0, 34, { name: 'South arterial', kind: 'terminal', signalized: false });
    addEdge(north, cross, 'arterial', { bend: 0, lanes: 4, width: 13.8, speed: 55 });
    addEdge(cross, south, 'arterial', { bend: 0, lanes: 4, width: 13.8, speed: 55 });
    const rampData = [
      [-20, -1, -37, -19], [-20, 1, -37, 19],
      [20, -1, 37, -19], [20, 1, 37, 19],
    ];
    rampData.forEach(([x, side, tx, tz], index) => {
      const rampStart = index < 2 ? westHigh : eastHigh;
      const rampNode = addNode(x, side * 18, { name: `Exit ramp ${index + 1} / gore`, y: 3.5, radius: 1.7, signalized: false });
      const merge = addNode(tx, tz, { name: `Ramp merge ${index + 1}`, kind: 'terminal', signalized: false });
      addEdge(rampStart, rampNode, 'ramp', { width: 7.2, lanes: 2, speed: 45, bend: side * 1.8, shoulder: .45 });
      addEdge(rampNode, merge, 'ramp', { width: 7.2, lanes: 2, speed: 45, bend: side * 2.6, shoulder: .45 });
    });
    for (let i = 0; i < 7; i++) {
      const z = -25 + i * 8;
      blocks.push({ x0: -36 + random() * 2, x1: -18 + random() * 2, z0: z, z1: z + 6.5, style: i % 3, seed: i });
      blocks.push({ x0: 18 + random() * 2, x1: 36 + random() * 2, z0: z, z1: z + 6.5, style: (i + 1) % 3, seed: i + 10 });
    }
  } else {
    const scale = clamp(Number(blockScale) || 18, 12, 30);
    const cols = density < .44 ? 4 : density > .84 ? 6 : 5;
    const rows = density < .4 ? 3 : density > .85 ? 5 : 4;
    const xCoords = Array.from({ length: cols }, (_, i) => (i - (cols - 1) / 2) * scale);
    const zCoords = Array.from({ length: rows }, (_, i) => (i - (rows - 1) / 2) * scale);
    const grid = zCoords.map((z, zi) => xCoords.map((x, xi) => addNode(x, z, {
      name: zi === Math.floor(rows / 2) && xi === Math.floor(cols / 2) ? 'Civic junction / 04' : `Junction ${String(zi * cols + xi + 1).padStart(2, '0')}`,
      radius: (xi === Math.floor(cols / 2) || zi === Math.floor(rows / 2)) ? 2.6 : 2.05,
    })));
    for (let zi = 0; zi < rows; zi++) {
      for (let xi = 0; xi < cols - 1; xi++) {
        const isBoulevard = zi === Math.floor(rows / 2);
        addEdge(grid[zi][xi], grid[zi][xi + 1], isBoulevard ? 'arterial' : 'local', {
          bend: (random() - .5) * (isBoulevard ? .45 : 1.15),
          speed: isBoulevard ? 50 : 30,
        });
      }
    }
    for (let zi = 0; zi < rows - 1; zi++) {
      for (let xi = 0; xi < cols; xi++) {
        const isBoulevard = xi === Math.floor(cols / 2);
        addEdge(grid[zi][xi], grid[zi + 1][xi], isBoulevard ? 'arterial' : 'local', {
          bend: (random() - .5) * (isBoulevard ? .4 : 1.05),
          speed: isBoulevard ? 50 : 30,
        });
      }
    }
    for (let zi = 0; zi < rows - 1; zi++) {
      for (let xi = 0; xi < cols - 1; xi++) {
        const x0 = xCoords[xi], x1 = xCoords[xi + 1], z0 = zCoords[zi], z1 = zCoords[zi + 1];
        const style = Math.floor(random() * 4);
        blocks.push({ x0, x1, z0, z1, style, seed: zi * (cols - 1) + xi });
      }
    }
  }
  return { preset, seed: Number(seed) || 1842, density, blockScale, nodes, edges, blocks, nextNode, nextEdge };
}

function nodeMapFor(net = network) { return new Map(net.nodes.map(node => [node.id, node])); }
function edgeWidth(edge) { return Number(edge.width) || edge.lanes * edge.laneWidth + edge.shoulder * 2; }
function cubicPoint(p0, p1, p2, p3, t) {
  const u = 1 - t;
  return V(
    u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
    u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y,
    u * u * u * p0.z + 3 * u * u * t * p1.z + 3 * u * t * t * p2.z + t * t * t * p3.z,
  );
}
function makeEdgeControlPoints(edge, nodes) {
  const a = nodes.get(edge.a), b = nodes.get(edge.b);
  const p0 = V(a.x, a.y, a.z), p3 = V(b.x, b.y, b.z);
  const d = sub(p3, p0);
  const planar = horizontalLength(d) || 1;
  const dir = V(d.x / planar, 0, d.z / planar);
  const side = V(-dir.z, 0, dir.x);
  const handle = Math.max(2, planar * .3);
  const bend = Number(edge.bend) || 0;
  const p1 = add(add(p0, mul(dir, handle)), mul(side, bend * .18));
  const p2 = add(add(p3, mul(dir, -handle)), mul(side, bend * .18));
  return [p0, p1, p2, p3];
}
function makeSpline(p0, p1, p2, p3, samples = 56) {
  const points = [];
  let total = 0;
  for (let i = 0; i <= samples; i++) {
    const point = cubicPoint(p0, p1, p2, p3, i / samples);
    if (i) total += length(sub(point, points[i - 1].p));
    points.push({ p: point, d: total });
  }
  return points;
}
function pointAtDistance(points, dist) {
  if (!points.length) return V();
  if (dist <= 0) return points[0].p;
  const last = points[points.length - 1];
  if (dist >= last.d) return last.p;
  let lo = 0, hi = points.length - 1;
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1;
    if (points[mid].d < dist) lo = mid; else hi = mid;
  }
  const a = points[lo], b = points[hi];
  return mixPoint(a.p, b.p, (dist - a.d) / (b.d - a.d || 1));
}
function mixPoint(a, b, t) { return V(mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t)); }
function tangentAtDistance(points, dist) {
  const p0 = pointAtDistance(points, Math.max(0, dist - .18));
  const p1 = pointAtDistance(points, Math.min(points[points.length - 1].d, dist + .18));
  return normalize(V(p1.x - p0.x, 0, p1.z - p0.z));
}
function clippedPath(points, startDist, endDist) {
  const total = points[points.length - 1].d;
  const lo = clamp(startDist, 0, total);
  const hi = clamp(total - endDist, lo, total);
  const out = [{ p: pointAtDistance(points, lo), d: 0 }];
  for (const item of points) if (item.d > lo + .001 && item.d < hi - .001) out.push({ p: item.p, d: item.d - lo });
  out.push({ p: pointAtDistance(points, hi), d: hi - lo });
  return out;
}
function convexHull2D(points) {
  if (points.length <= 3) return points.slice();
  const sorted = points.slice().sort((a, b) => a.x - b.x || a.z - b.z);
  const turn = (o, a, b) => (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
  const lower = [];
  for (const point of sorted) {
    while (lower.length >= 2 && turn(lower[lower.length - 2], lower[lower.length - 1], point) <= 0) lower.pop();
    lower.push(point);
  }
  const upper = [];
  for (let i = sorted.length - 1; i >= 0; i--) {
    const point = sorted[i];
    while (upper.length >= 2 && turn(upper[upper.length - 2], upper[upper.length - 1], point) <= 0) upper.pop();
    upper.push(point);
  }
  lower.pop(); upper.pop();
  return lower.concat(upper);
}
function compileNetwork(net) {
  const nodes = nodeMapFor(net);
  const incident = new Map(net.nodes.map(node => [node.id, []]));
  for (const edge of net.edges) {
    incident.get(edge.a)?.push(edge);
    incident.get(edge.b)?.push(edge);
  }
  const trimByNode = new Map();
  for (const node of net.nodes) {
    const connected = incident.get(node.id) || [];
    const radius = connected.length > 1
      ? Math.max(...connected.map(edge => edgeWidth(edge) * .5)) * .62 + (Number(node.radius) || 2) * .4
      : 0;
    trimByNode.set(node.id, radius);
    node.degree = connected.length;
  }

  // Resolve one shared clip radius per junction. Short links reduce both adjoining node radii
  // together, so no arm gets a private trim plane that could overlap its shared patch.
  const fullCurves = new Map();
  for (const edge of net.edges) {
    const controls = makeEdgeControlPoints(edge, nodes);
    const full = makeSpline(...controls, 64);
    fullCurves.set(edge.id, { controls, full, total: full[full.length - 1].d });
  }
  for (let iteration = 0; iteration < 8; iteration++) {
    let changed = false;
    for (const edge of net.edges) {
      const total = fullCurves.get(edge.id).total;
      const startTrim = trimByNode.get(edge.a) || 0;
      const endTrim = trimByNode.get(edge.b) || 0;
      const maxCombined = total * .78;
      if (startTrim + endTrim > maxCombined) {
        const scale = maxCombined / Math.max(.001, startTrim + endTrim);
        trimByNode.set(edge.a, startTrim * scale);
        trimByNode.set(edge.b, endTrim * scale);
        changed = true;
      }
    }
    if (!changed) break;
  }

  const edgePaths = new Map();
  const nodeArms = new Map(net.nodes.map(node => [node.id, []]));
  for (const edge of net.edges) {
    const curve = fullCurves.get(edge.id);
    const { controls, full, total } = curve;
    const startTrim = trimByNode.get(edge.a) || 0;
    const endTrim = trimByNode.get(edge.b) || 0;
    const path = clippedPath(full, startTrim, endTrim);
    const data = { edge, path, full, length: path[path.length - 1].d, fullLength: total, startTrim, endTrim, controls };
    edgePaths.set(edge.id, data);
    const width = edgeWidth(edge);
    const makeArm = (nodeId, atStart) => {
      const center = atStart ? path[0].p : path[path.length - 1].p;
      const tangent = atStart ? tangentAtDistance(full, startTrim) : normalize(mul(tangentAtDistance(full, total - endTrim), -1));
      const side = V(-tangent.z, 0, tangent.x);
      nodeArms.get(nodeId).push({ edge, center, tangent, side, half: width * .5, width, outer: width * .5 + 1.55 });
    };
    makeArm(edge.a, true);
    makeArm(edge.b, false);
  }
  const junctions = new Map();
  for (const node of net.nodes) {
    const arms = nodeArms.get(node.id) || [];
    if (arms.length < 2) continue;
    const roadPoints = [];
    const outerPoints = [];
    for (const arm of arms) {
      roadPoints.push(add(arm.center, mul(arm.side, arm.half)));
      roadPoints.push(add(arm.center, mul(arm.side, -arm.half)));
      outerPoints.push(add(arm.center, mul(arm.side, arm.outer)));
      outerPoints.push(add(arm.center, mul(arm.side, -arm.outer)));
    }
    const center = V(node.x, node.y, node.z);
    const roadHull = convexHull2D(roadPoints);
    const outerHull = convexHull2D(outerPoints);
    junctions.set(node.id, { node, arms, roadHull, outerHull, center, trim: trimByNode.get(node.id) || 0 });
  }
  const bounds = boundsForNetwork(net, edgePaths);
  return { edgePaths, nodeArms, junctions, incident, trimByNode, bounds };
}
function boundsForNetwork(net, edgePaths = null) {
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
  for (const node of net.nodes) { minX = Math.min(minX, node.x); maxX = Math.max(maxX, node.x); minZ = Math.min(minZ, node.z); maxZ = Math.max(maxZ, node.z); }
  if (!Number.isFinite(minX)) return { minX: -25, maxX: 25, minZ: -25, maxZ: 25 };
  for (const edge of net.edges) if (edgePaths?.has(edge.id)) {
    const path = edgePaths.get(edge.id).path;
    for (const item of path) { minX = Math.min(minX, item.p.x - edgeWidth(edge)); maxX = Math.max(maxX, item.p.x + edgeWidth(edge)); minZ = Math.min(minZ, item.p.z - edgeWidth(edge)); maxZ = Math.max(maxZ, item.p.z + edgeWidth(edge)); }
  }
  return { minX, maxX, minZ, maxZ };
}

class MeshBuilder {
  constructor() { this.vertices = []; this.quads = 0; this.triangles = 0; }
  vertex(p, n, uv, color, material) {
    const c = Array.isArray(color) ? color : hexToLinear(color || '#808080');
    this.vertices.push(p.x, p.y, p.z, n.x, n.y, n.z, uv[0], uv[1], c[0], c[1], c[2], 1, material);
  }
  triangle(a, b, c, material, color, normals = null, uvs = null) {
    const faceNormal = normalize(cross(sub(b, a), sub(c, a)));
    const ns = normals || [faceNormal, faceNormal, faceNormal];
    const tc = uvs || [[0, 0], [1, 0], [1, 1]];
    this.vertex(a, ns[0], tc[0], color, material);
    this.vertex(b, ns[1], tc[1], color, material);
    this.vertex(c, ns[2], tc[2], color, material);
    this.triangles++;
  }
  quad(a, b, c, d, material, color, normal = V(0, 1, 0), uvScale = [1, 1], normals = null) {
    const ns = normals || [normal, normal, normal, normal];
    this.vertex(a, ns[0], [0, 0], color, material);
    this.vertex(b, ns[1], [uvScale[0], 0], color, material);
    this.vertex(c, ns[2], [uvScale[0], uvScale[1]], color, material);
    this.vertex(a, ns[0], [0, 0], color, material);
    this.vertex(c, ns[2], [uvScale[0], uvScale[1]], color, material);
    this.vertex(d, ns[3], [0, uvScale[1]], color, material);
    this.quads++;
    this.triangles += 2;
  }
  polygon(points, y, material, color) {
    if (points.length < 3) return;
    const center = mul(points.reduce((s, p) => add(s, p), V()), 1 / points.length);
    center.y = y;
    for (let i = 0; i < points.length; i++) {
      const a = V(points[i].x, y, points[i].z);
      const b = V(points[(i + 1) % points.length].x, y, points[(i + 1) % points.length].z);
      this.triangle(center, a, b, material, color, [V(0, 1, 0), V(0, 1, 0), V(0, 1, 0)]);
    }
  }
}
// Avoid optional pipeline syntax in the rest of the file while keeping polygon code readable.

function makeViewMatrix(eye, target, up = V(0, 1, 0)) {
  const z = normalize(sub(eye, target));
  const x = normalize(cross(up, z));
  const y = cross(z, x);
  const out = new Float32Array(16);
  out[0] = x.x; out[1] = y.x; out[2] = z.x; out[3] = 0;
  out[4] = x.y; out[5] = y.y; out[6] = z.y; out[7] = 0;
  out[8] = x.z; out[9] = y.z; out[10] = z.z; out[11] = 0;
  out[12] = -dot(x, eye); out[13] = -dot(y, eye); out[14] = -dot(z, eye); out[15] = 1;
  return out;
}
function perspectiveMatrix(fov, aspect, near, far) {
  const f = 1 / Math.tan(fov / 2);
  const out = new Float32Array(16);
  out[0] = f / aspect; out[5] = f;
  out[10] = far / (near - far); out[11] = -1;
  out[14] = (near * far) / (near - far);
  return out;
}
function multiplyMatrix(a, b) {
  const out = new Float32Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
    out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
  }
  return out;
}
function invertMatrix(matrix) {
  const rows = Array.from({ length: 4 }, (_, r) => [matrix[r], matrix[4 + r], matrix[8 + r], matrix[12 + r], r === 0 ? 1 : 0, r === 1 ? 1 : 0, r === 2 ? 1 : 0, r === 3 ? 1 : 0]);
  for (let c = 0; c < 4; c++) {
    let pivot = c;
    for (let r = c + 1; r < 4; r++) if (Math.abs(rows[r][c]) > Math.abs(rows[pivot][c])) pivot = r;
    if (Math.abs(rows[pivot][c]) < 1e-12) return null;
    [rows[c], rows[pivot]] = [rows[pivot], rows[c]];
    const divisor = rows[c][c];
    for (let j = 0; j < 8; j++) rows[c][j] /= divisor;
    for (let r = 0; r < 4; r++) if (r !== c) {
      const factor = rows[r][c];
      for (let j = 0; j < 8; j++) rows[r][j] -= factor * rows[c][j];
    }
  }
  const out = new Float32Array(16);
  for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) out[c * 4 + r] = rows[r][c + 4];
  return out;
}
function transform4(matrix, v) {
  return [
    matrix[0] * v[0] + matrix[4] * v[1] + matrix[8] * v[2] + matrix[12] * v[3],
    matrix[1] * v[0] + matrix[5] * v[1] + matrix[9] * v[2] + matrix[13] * v[3],
    matrix[2] * v[0] + matrix[6] * v[1] + matrix[10] * v[2] + matrix[14] * v[3],
    matrix[3] * v[0] + matrix[7] * v[1] + matrix[11] * v[2] + matrix[15] * v[3],
  ];
}
function updateCameraMatrices() {
  if (!canvasWidth || !canvasHeight) return;
  const cp = Math.cos(camera.pitch);
  const dir = V(Math.cos(camera.yaw) * cp, Math.sin(camera.pitch), Math.sin(camera.yaw) * cp);
  const eye = add(camera.target, mul(dir, camera.distance));
  const view = makeViewMatrix(eye, camera.target);
  const projection = perspectiveMatrix(46 * Math.PI / 180, canvasWidth / canvasHeight, .1, 280);
  const viewProjection = multiplyMatrix(projection, view);
  cameraMatrices = { view, projection, viewProjection, inverseViewProjection: invertMatrix(viewProjection), eye };
}
function projectWorld(point) {
  const matrix = cameraMatrices.viewProjection;
  if (!matrix) return null;
  const clip = transform4(matrix, [point.x, point.y, point.z, 1]);
  if (clip[3] <= .01) return null;
  const nx = clip[0] / clip[3], ny = clip[1] / clip[3], nz = clip[2] / clip[3];
  return { x: (nx * .5 + .5) * canvasWidth, y: (1 - (ny * .5 + .5)) * canvasHeight, depth: nz, visible: Math.abs(nx) < 1.4 && Math.abs(ny) < 1.4 && nz >= 0 && nz <= 1 };
}
function rayOnGround(clientX, clientY, planeY = 0) {
  const bounds = canvas.getBoundingClientRect();
  const x = clientX - bounds.left, y = clientY - bounds.top;
  const ndcX = x / bounds.width * 2 - 1;
  const ndcY = 1 - y / bounds.height * 2;
  const inv = cameraMatrices.inverseViewProjection;
  if (!inv) return null;
  const near4 = transform4(inv, [ndcX, ndcY, 0, 1]);
  const far4 = transform4(inv, [ndcX, ndcY, 1, 1]);
  const near = V(near4[0] / near4[3], near4[1] / near4[3], near4[2] / near4[3]);
  const far = V(far4[0] / far4[3], far4[1] / far4[3], far4[2] / far4[3]);
  const direction = sub(far, near);
  if (Math.abs(direction.y) < 1e-5) return null;
  const t = (planeY - near.y) / direction.y;
  if (t < 0) return null;
  return add(near, mul(direction, t));
}

function addRoundedBox(builder, center, size, yaw, material, color, bevel = .045, divisions = 2) {
  const hx = size[0] / 2, hy = size[1] / 2, hz = size[2] / 2;
  const b = Math.min(bevel, hx * .45, hy * .45, hz * .45);
  const c = [Math.max(.001, hx - b), Math.max(.001, hy - b), Math.max(.001, hz - b)];
  const cos = Math.cos(yaw), sin = Math.sin(yaw);
  const localToWorld = p => V(center.x + p.x * cos - p.z * sin, center.y + p.y, center.z + p.x * sin + p.z * cos);
  const normalToWorld = n => normalize(V(n.x * cos - n.z * sin, n.y, n.x * sin + n.z * cos));
  const faces = [
    { axis: 0, sign: 1, u: 2, v: 1 }, { axis: 0, sign: -1, u: 1, v: 2 },
    { axis: 1, sign: 1, u: 0, v: 2 }, { axis: 1, sign: -1, u: 2, v: 0 },
    { axis: 2, sign: 1, u: 1, v: 0 }, { axis: 2, sign: -1, u: 0, v: 1 },
  ];
  const h = [hx, hy, hz];
  const pointOnFace = (face, u, v) => {
    const raw = [0, 0, 0];
    raw[face.axis] = face.sign * h[face.axis];
    raw[face.u] = mix(-h[face.u], h[face.u], u);
    raw[face.v] = mix(-h[face.v], h[face.v], v);
    const core = raw.map((value, i) => clamp(value, -c[i], c[i]));
    const delta = [raw[0] - core[0], raw[1] - core[1], raw[2] - core[2]];
    const d = Math.hypot(...delta);
    const normal = d > 1e-5 ? delta.map(vv => vv / d) : [face.axis === 0 ? face.sign : 0, face.axis === 1 ? face.sign : 0, face.axis === 2 ? face.sign : 0];
    const pos = d > 1e-5 ? core.map((value, i) => value + normal[i] * b) : raw;
    return { p: localToWorld(V(pos[0], pos[1], pos[2])), n: normalToWorld(V(...normal)) };
  };
  for (const face of faces) for (let iy = 0; iy < divisions; iy++) for (let ix = 0; ix < divisions; ix++) {
    const p00 = pointOnFace(face, ix / divisions, iy / divisions);
    const p10 = pointOnFace(face, (ix + 1) / divisions, iy / divisions);
    const p11 = pointOnFace(face, (ix + 1) / divisions, (iy + 1) / divisions);
    const p01 = pointOnFace(face, ix / divisions, (iy + 1) / divisions);
    builder.quad(p00.p, p10.p, p11.p, p01.p, material, color, p00.n, [1 / divisions, 1 / divisions], [p00.n, p10.n, p11.n, p01.n]);
  }
}
function addBox(builder, center, size, yaw, material, color) {
  addRoundedBox(builder, center, size, yaw, material, color, .012, 1);
}
function addCylinderBetween(builder, start, end, r0, r1, material, color, sides = 12) {
  const axis = normalize(sub(end, start));
  let u = normalize(cross(axis, Math.abs(axis.y) > .86 ? V(1, 0, 0) : V(0, 1, 0)));
  let v = normalize(cross(axis, u));
  for (let i = 0; i < sides; i++) {
    const a0 = i / sides * Math.PI * 2, a1 = (i + 1) / sides * Math.PI * 2;
    const radial = a => add(mul(u, Math.cos(a)), mul(v, Math.sin(a)));
    const n0 = radial(a0), n1 = radial(a1);
    const p0 = add(start, mul(n0, r0)), p1 = add(start, mul(n1, r0));
    const p2 = add(end, mul(n1, r1)), p3 = add(end, mul(n0, r1));
    builder.quad(p0, p1, p2, p3, material, color, normalize(add(add(n0, n1), axis)), [1 / sides, 1]);
    builder.triangle(start, p1, p0, material, color, [mul(axis, -1), mul(axis, -1), mul(axis, -1)]);
    builder.triangle(end, p3, p2, material, color, [axis, axis, axis]);
  }
}
function addCylinderY(builder, x, y, z, radius, height, material, color, sides = 16, topRadius = radius * .92) {
  addCylinderBetween(builder, V(x, y, z), V(x, y + height, z), radius, topRadius, material, color, sides);
}
function addSphere(builder, center, radius, material, color, segments = 16, rings = 9) {
  for (let iy = 0; iy < rings; iy++) {
    const v0 = iy / rings, v1 = (iy + 1) / rings;
    const phi0 = v0 * Math.PI, phi1 = v1 * Math.PI;
    for (let ix = 0; ix < segments; ix++) {
      const u0 = ix / segments, u1 = (ix + 1) / segments;
      const point = (u, phi) => V(center.x + Math.sin(phi) * Math.cos(u * Math.PI * 2) * radius, center.y + Math.cos(phi) * radius, center.z + Math.sin(phi) * Math.sin(u * Math.PI * 2) * radius);
      const p00 = point(u0, phi0), p10 = point(u1, phi0), p11 = point(u1, phi1), p01 = point(u0, phi1);
      const n = p => normalize(sub(p, center));
      builder.quad(p00, p10, p11, p01, material, color, V(0, 1, 0), [1 / segments, 1 / rings], [n(p00), n(p10), n(p11), n(p01)]);
    }
  }
}
function crossSection(center, tangent, side, offset, yOffset = 0) {
  return V(center.x + side.x * offset, center.y + yOffset, center.z + side.z * offset);
}
function samplePath(path, distance) {
  return pointAtDistance(path, distance);
}
function tangentOnPath(path, distance) {
  return tangentAtDistance(path, distance);
}
function pathLength(path) { return path?.length ? path[path.length - 1].d : 0; }
function pathOffsetPoint(path, distance, lateral) {
  const p = samplePath(path, distance);
  const t = tangentOnPath(path, distance);
  const side = V(-t.z, 0, t.x);
  return add(p, mul(side, lateral));
}
function addRibbon(builder, path, width, material, color, options = {}) {
  if (!path || path.length < 2 || width <= 0) return;
  const sections = [];
  const half = width * .5;
  const stepCount = options.steps ?? 8;
  const start = clamp(options.start ?? 0, 0, pathLength(path));
  const end = clamp(options.end ?? pathLength(path), start, pathLength(path));
  if (end - start < .05) return;
  for (let i = 0; i <= stepCount; i++) {
    const d = mix(start, end, i / stepCount);
    const center = samplePath(path, d);
    const tangent = tangentOnPath(path, d);
    const side = V(-tangent.z, 0, tangent.x);
    const left = add(center, mul(side, half));
    const right = add(center, mul(side, -half));
    const lift = options.lift ?? .045;
    left.y += lift; right.y += lift;
    if (options.crown) {
      left.y -= options.crown; right.y -= options.crown;
    }
    sections.push({ left, right, tangent, center });
  }
  for (let i = 0; i < sections.length - 1; i++) {
    const a = sections[i], b = sections[i + 1];
    builder.quad(a.left, b.left, b.right, a.right, material, color, options.normal || V(0, 1, 0), [1, width]);
  }
}
function addLaneRibbon(builder, path, edge, lateral, lineWidth, material, color, start = 0, end = pathLength(path), dash = null) {
  const total = pathLength(path);
  const lo = Math.max(start, 0), hi = Math.min(end, total);
  if (hi <= lo) return;
  if (!dash) {
    const count = Math.max(3, Math.ceil((hi - lo) / 2));
    const stations = [];
    for (let i = 0; i <= count; i++) stations.push(mix(lo, hi, i / count));
    for (let i = 0; i < stations.length - 1; i++) {
      const d0 = stations[i], d1 = stations[i + 1];
      const t0 = tangentOnPath(path, d0), t1 = tangentOnPath(path, d1);
      const s0 = V(-t0.z, 0, t0.x), s1 = V(-t1.z, 0, t1.x);
      const c0 = add(samplePath(path, d0), mul(s0, lateral)), c1 = add(samplePath(path, d1), mul(s1, lateral));
      const a = add(c0, mul(s0, lineWidth * .5)); const b = add(c1, mul(s1, lineWidth * .5));
      const c = add(c1, mul(s1, -lineWidth * .5)); const d = add(c0, mul(s0, -lineWidth * .5));
      [a, b, c, d].forEach(p => p.y += .105);
      builder.quad(a, b, c, d, material, color, V(0, 1, 0), [1, lineWidth]);
    }
    return;
  }
  const dashLength = dash[0], gap = dash[1];
  for (let d = lo + .5; d < hi - .3; d += dashLength + gap) {
    const s0 = d, s1 = Math.min(d + dashLength, hi);
    const c0 = pathOffsetPoint(path, s0, lateral), c1 = pathOffsetPoint(path, s1, lateral);
    const t0 = tangentOnPath(path, s0), t1 = tangentOnPath(path, s1);
    const side0 = V(-t0.z, 0, t0.x), side1 = V(-t1.z, 0, t1.x);
    const a = add(c0, mul(side0, lineWidth * .5)), b = add(c1, mul(side1, lineWidth * .5));
    const c = add(c1, mul(side1, -lineWidth * .5)), d0 = add(c0, mul(side0, -lineWidth * .5));
    [a, b, c, d0].forEach(p => p.y += .105);
    builder.quad(a, b, c, d0, material, color, V(0, 1, 0), [1, lineWidth]);
  }
}
function addRing(builder, inner, outer, y, material, color, segments = 56) {
  if (inner.length < 3 || outer.length < 3) return;
  const center = inner.reduce((acc, p) => add(acc, p), V());
  center.x /= inner.length; center.z /= inner.length;
  const rayLoop = polygon => Array.from({ length: segments }, (_, i) => {
    const angle = i / segments * Math.PI * 2;
    const dir = V(Math.cos(angle), 0, Math.sin(angle));
    let best = 0;
    for (let j = 0; j < polygon.length; j++) {
      const a = polygon[j], b = polygon[(j + 1) % polygon.length];
      const edge = V(b.x - a.x, 0, b.z - a.z);
      const rel = V(a.x - center.x, 0, a.z - center.z);
      const denominator = dir.x * edge.z - dir.z * edge.x;
      if (Math.abs(denominator) < 1e-8) continue;
      const t = (rel.x * edge.z - rel.z * edge.x) / denominator;
      const u = (rel.x * dir.z - rel.z * dir.x) / denominator;
      if (t >= 0 && u >= -1e-6 && u <= 1 + 1e-6) best = Math.max(best, t);
    }
    return V(center.x + dir.x * best, y, center.z + dir.z * best);
  });
  const iLoop = rayLoop(inner), oLoop = rayLoop(outer);
  for (let i = 0; i < segments; i++) builder.quad(oLoop[i], oLoop[(i + 1) % segments], iLoop[(i + 1) % segments], iLoop[i], material, color, V(0, 1, 0), [1 / segments, .9]);
}
function addJunctionSurface(builder, data, featureSidewalks) {
  const { node, arms, roadHull, outerHull, center } = data;
  const y = node.y + .055;
  builder.polygon(roadHull, y, 1, '#30312f');
  if (!featureSidewalks || outerHull.length < 3) return;
  addRing(builder, roadHull, outerHull, node.y + .031, 2, '#77766e', Math.max(40, roadHull.length * 10));
  const curbPoints = [];
  for (const arm of arms) {
    curbPoints.push(add(arm.center, mul(arm.side, arm.outer + .18)));
    curbPoints.push(add(arm.center, mul(arm.side, -arm.outer - .18)));
  }
  const curbHull = convexHull2D(curbPoints);
  addRing(builder, outerHull, curbHull, node.y + .052, 2, '#6f706d', Math.max(40, curbHull.length * 10));
}

function addCrosswalk(builder, arm) {
  const { center, tangent, side, width } = arm;
  const count = Math.max(7, Math.floor((width - 1.4) / 1.05));
  const span = width - 1.25;
  const stripeWidth = .42;
  for (let i = 0; i < count; i++) {
    const across = -span / 2 + (i + .5) * (span / count);
    const p = add(center, mul(tangent, 1.55));
    const c = add(p, mul(side, across));
    const halfAcross = span / count * .32;
    const along = tangent;
    const left = add(c, mul(side, halfAcross));
    const right = add(c, mul(side, -halfAcross));
    const a = add(left, mul(along, -stripeWidth / 2));
    const b = add(left, mul(along, stripeWidth / 2));
    const cc = add(right, mul(along, stripeWidth / 2));
    const d = add(right, mul(along, -stripeWidth / 2));
    [a, b, cc, d].forEach(q => q.y = arm.center.y + .108);
    builder.quad(a, b, cc, d, 3, '#ddd8c8', V(0, 1, 0), [1, .4]);
  }
}
function addRoadMarkings(builder, edge, data) {
  if (!state.features.markings) return;
  const path = data.path, total = pathLength(path), lanes = Math.max(2, edge.lanes || 2), laneW = (edge.width - edge.shoulder * 2) / lanes;
  const solidStart = Math.min(1.2, total * .08), solidEnd = Math.max(solidStart, total - 1.2);
  // Region-aware center markings; paired yellow lines are standard on the default North American set.
  const centerColor = state.region === 'na' ? '#d0ad72' : '#dddcd3';
  if (state.region === 'eu') addLaneRibbon(builder, path, edge, 0, .12, 3, centerColor, solidStart, solidEnd, [3.2, 2.8]);
  else {
    addLaneRibbon(builder, path, edge, -.105, .105, 3, centerColor, solidStart, solidEnd);
    addLaneRibbon(builder, path, edge, .105, .105, 3, centerColor, solidStart, solidEnd);
  }
  for (let separator = 1; separator < lanes; separator++) {
    if (lanes === 2 && separator === 1) continue;
    const lateral = -edge.width / 2 + edge.shoulder + laneW * separator;
    addLaneRibbon(builder, path, edge, lateral, .095, 3, '#d3d2c9', 0, total, [2.8, 3.25]);
  }
  const shoulderOffset = edge.width / 2 - edge.shoulder * .68;
  addLaneRibbon(builder, path, edge, shoulderOffset, .1, 3, '#c9c9bd', 0, total);
  addLaneRibbon(builder, path, edge, -shoulderOffset, .1, 3, '#c9c9bd', 0, total);
  const adjacent = compiled.nodeArms;
  const nodeAArms = adjacent.get(edge.a) || [], nodeBArms = adjacent.get(edge.b) || [];
  for (const arm of [...nodeAArms, ...nodeBArms].filter(a => a.edge.id === edge.id && a.width >= 7)) addCrosswalk(builder, arm);
  if (edge.speed <= 30 && total > 12 && state.features.damage) addSpeedBump(builder, path, edge.width, total * .61);
}
function addSpeedBump(builder, path, width, distance) {
  const center = samplePath(path, distance), tangent = tangentOnPath(path, distance), side = V(-tangent.z, 0, tangent.x);
  const halfLength = .48;
  const crossRoad = width - .7;
  const profile = [-.48, -.34, -.18, 0, .18, .34, .48].map(s => ({ s, h: .16 * Math.max(0, 1 - Math.abs(s) / halfLength) ** .7 }));
  for (let i = 0; i < profile.length - 1; i++) {
    const p0 = add(center, mul(tangent, profile[i].s));
    const p1 = add(center, mul(tangent, profile[i + 1].s));
    const a = add(p0, mul(side, crossRoad / 2)); const b = add(p1, mul(side, crossRoad / 2));
    const c = add(p1, mul(side, -crossRoad / 2)); const d = add(p0, mul(side, -crossRoad / 2));
    a.y += .06 + profile[i].h; b.y += .06 + profile[i + 1].h; c.y += .06 + profile[i + 1].h; d.y += .06 + profile[i].h;
    const stripIndex = i < 2 || i > 3 ? 3 : 1;
    builder.quad(a, b, c, d, stripIndex, stripIndex === 3 ? '#c5c0ae' : '#242522', V(0, 1, 0), [1, crossRoad]);
  }
}
function addCrackLine(builder, start, end, width = .045) {
  const d = sub(end, start); const tangent = normalize(V(d.x, 0, d.z)); const side = V(-tangent.z, 0, tangent.x);
  const a = add(start, mul(side, width)); const b = add(end, mul(side, width));
  const c = add(end, mul(side, -width)); const e = add(start, mul(side, -width));
  [a, b, c, e].forEach(p => p.y += .112);
  builder.quad(a, b, c, e, 8, '#161714', V(0, 1, 0), [1, width * 2]);
}
function addPothole(builder, point, tangent, rx = .85, rz = .54, seed = 0) {
  const side = V(-tangent.z, 0, tangent.x);
  const outer = [], inner = [];
  const count = 20;
  for (let i = 0; i < count; i++) {
    const a = i / count * Math.PI * 2;
    const wav = 1 + .12 * Math.sin(a * 5 + seed) + .075 * Math.cos(a * 7 + seed * .4);
    outer.push(add(add(point, mul(tangent, Math.cos(a) * rx * wav)), mul(side, Math.sin(a) * rz * wav)));
    const wav2 = .74 + .08 * Math.sin(a * 3 + seed);
    inner.push(add(add(point, mul(tangent, Math.cos(a) * rx * wav2)), mul(side, Math.sin(a) * rz * wav2)));
  }
  addRing(builder, inner, outer, point.y + .098, 8, '#42423d', 40);
  const center = V(point.x, point.y + .086, point.z);
  const dark = inner.map(p => V(p.x, point.y + .087, p.z));
  builder.polygon(dark, center.y, 8, '#20211f');
  for (let i = 0; i < 5; i++) {
    const a = (i * 1.7 + seed) % (Math.PI * 2);
    const s = add(add(point, mul(tangent, Math.cos(a) * rx * 1.15)), mul(side, Math.sin(a) * rz * 1.1));
    const e = add(add(point, mul(tangent, Math.cos(a + .35) * (rx + .65))), mul(side, Math.sin(a + .35) * (rz + .35)));
    addCrackLine(builder, V(s.x, point.y, s.z), V(e.x, point.y, e.z), .025);
  }
}

function buildTerrain(builder, bounds) {
  const minX = Math.min(-78, bounds.minX - 19), maxX = Math.max(78, bounds.maxX + 19);
  const minZ = Math.min(-70, bounds.minZ - 18), maxZ = Math.max(70, bounds.maxZ + 18);
  const step = 3.6;
  const rows = Math.ceil((maxZ - minZ) / step), cols = Math.ceil((maxX - minX) / step);
  const railZ = state.preset === 'suburban' ? -17 : -18;
  const cutRail = state.features.rail;
  for (let zi = 0; zi < rows; zi++) for (let xi = 0; xi < cols; xi++) {
    const x0 = minX + xi * step, x1 = Math.min(maxX, x0 + step);
    const z0 = minZ + zi * step, z1 = Math.min(maxZ, z0 + step);
    const cx = (x0 + x1) * .5, cz = (z0 + z1) * .5;
    const inRailCut = cutRail && cx > -49 && cx < 49 && Math.abs(cz - railZ) < 4.1;
    if (inRailCut) continue;
    const y = -.38 + Math.sin(cx * .038) * .045 + Math.cos(cz * .05) * .032;
    builder.quad(V(x0, y, z0), V(x1, y, z0), V(x1, y, z1), V(x0, y, z1), 0, '#51564b', V(0, 1, 0), [step, step]);
  }
  if (gridVisible) {
    const fine = new MeshBuilder();
    const gridColor = '#71756a';
    for (let x = Math.ceil(minX / 10) * 10; x < maxX; x += 10) {
      const path = [{ p: V(x, -.34, minZ), d: 0 }, { p: V(x, -.34, maxZ), d: maxZ - minZ }];
      // A whisper-thin editor grid, baked into the ground pass for this scene only.
      const n = V(0, 1, 0);
      for (let z = minZ; z < maxZ; z += 40) fine.quad(V(x - .012, -.333, z), V(x + .012, -.333, z), V(x + .012, -.333, Math.min(maxZ, z + 40)), V(x - .012, -.333, Math.min(maxZ, z + 40)), 3, gridColor, n, [1, 1]);
    }
    for (let z = Math.ceil(minZ / 10) * 10; z < maxZ; z += 10) {
      for (let x = minX; x < maxX; x += 40) fine.quad(V(x, -.332, z - .012), V(Math.min(maxX, x + 40), -.332, z - .012), V(Math.min(maxX, x + 40), -.332, z + .012), V(x, -.332, z + .012), 3, gridColor, V(0, 1, 0), [1, 1]);
    }
    builder.vertices.push(...fine.vertices); builder.quads += fine.quads; builder.triangles += fine.triangles;
  }
}
function buildCityBlocks(builder, net, random) {
  if (!state.features.blocks) return;
  const nodeMap = nodeMapFor(net);
  const xCoords = [...new Set(net.nodes.map(n => Number(n.x.toFixed(3))))].sort((a, b) => a - b);
  const zCoords = [...new Set(net.nodes.map(n => Number(n.z.toFixed(3))))].sort((a, b) => a - b);
  if (net.preset === 'urban') {
    for (let zi = 0; zi < zCoords.length - 1; zi++) for (let xi = 0; xi < xCoords.length - 1; xi++) {
      const x0 = xCoords[xi], x1 = xCoords[xi + 1], z0 = zCoords[zi], z1 = zCoords[zi + 1];
      const wL = nearestEdgeWidth(net, nodeMap, x0, z0, 'vertical') * .5;
      const wR = nearestEdgeWidth(net, nodeMap, x1, z0, 'vertical') * .5;
      const wT = nearestEdgeWidth(net, nodeMap, x0, z0, 'horizontal') * .5;
      const wB = nearestEdgeWidth(net, nodeMap, x0, z1, 'horizontal') * .5;
      const marginX0 = x0 + wL + 2.1, marginX1 = x1 - wR - 2.1;
      const marginZ0 = z0 + wT + 2.1, marginZ1 = z1 - wB - 2.1;
      if (marginX1 - marginX0 < 2 || marginZ1 - marginZ0 < 2) continue;
      buildBlockParcel(builder, { x0: marginX0, x1: marginX1, z0: marginZ0, z1: marginZ1, style: Math.floor(random() * 4), seed: zi * xCoords.length + xi });
    }
  } else {
    for (const block of net.blocks) buildBlockParcel(builder, block);
  }
}
function nearestEdgeWidth(net, nodes, x, z, axis) {
  const node = net.nodes.find(n => Math.abs(n.x - x) < .01 && Math.abs(n.z - z) < .01);
  const edges = node ? net.edges.filter(e => e.a === node.id || e.b === node.id) : [];
  const match = edges.find(edge => {
    const other = nodes.get(edge.a === node?.id ? edge.b : edge.a);
    if (!other) return false;
    return axis === 'vertical' ? Math.abs(other.z - z) > .1 : Math.abs(other.x - x) > .1;
  });
  return match ? edgeWidth(match) : 7.5;
}
function buildBlockParcel(builder, block) {
  const { x0, x1, z0, z1, style = 0, seed = 0 } = block;
  const w = x1 - x0, h = z1 - z0;
  if (w < 1 || h < 1) return;
  if (style % 4 === 1) {
    // Asphalt lot with a shallow perimeter curb and worn parking stripes.
    builder.quad(V(x0, -.085, z0), V(x1, -.085, z0), V(x1, -.085, z1), V(x0, -.085, z1), 1, '#393a38', V(0, 1, 0), [w / 3, h / 3]);
    addBlockBorder(builder, x0, x1, z0, z1, '#77756c');
    const stalls = Math.max(2, Math.floor(w / 2.5));
    for (let i = 0; i <= stalls; i++) {
      const x = mix(x0 + .55, x1 - .55, i / stalls);
      const p0 = V(x, -.068, z0 + .45), p1 = V(x, -.068, z1 - .45);
      const t = normalize(sub(p1, p0)), side = V(-t.z, 0, t.x);
      const a = add(p0, mul(side, .03)), b = add(p1, mul(side, .03)), c = add(p1, mul(side, -.03)), d = add(p0, mul(side, -.03));
      builder.quad(a, b, c, d, 3, '#bbb8aa', V(0, 1, 0), [1, h]);
    }
    const carCount = Math.min(2, Math.max(1, Math.floor(w / 7)));
    for (let i = 0; i < carCount; i++) {
      const cx = x0 + w * (.34 + i * .34), cz = z0 + h * .55;
      addParkedCar(builder, V(cx, -.055, cz), Math.PI / 2, ['#727d78', '#7b6a5c', '#424a53'][Math.floor((seed + i) % 3)]);
    }
  } else if (style % 4 === 2) {
    // A planted block lot; no buildings, only a small planted court and permeable paths.
    builder.quad(V(x0, -.12, z0), V(x1, -.12, z0), V(x1, -.12, z1), V(x0, -.12, z1), 0, '#555b4c', V(0, 1, 0), [w / 2, h / 2]);
    const pad = Math.min(w, h) * .12;
    builder.quad(V(x0 + pad, -.075, z0 + pad), V(x1 - pad, -.075, z0 + pad), V(x1 - pad, -.075, z1 - pad), V(x0 + pad, -.075, z1 - pad), 2, '#77776e', V(0, 1, 0), [w, h]);
    addBlockBorder(builder, x0, x1, z0, z1, '#777a6d');
    const trees = Math.max(1, Math.floor(Math.min(w, h) / 5));
    for (let i = 0; i < trees; i++) {
      const px = mix(x0 + pad * 1.5, x1 - pad * 1.5, (i + 1) / (trees + 1));
      const pz = z0 + h * (.28 + .38 * ((seed + i) % 2));
      addTree(builder, V(px, -.07, pz), .72 + .1 * (i % 2));
    }
  } else {
    builder.quad(V(x0, -.11, z0), V(x1, -.11, z0), V(x1, -.11, z1), V(x0, -.11, z1), 2, style % 4 === 3 ? '#75736c' : '#666760', V(0, 1, 0), [w * .75, h * .75]);
    addBlockBorder(builder, x0, x1, z0, z1, '#8a877e');
    // A fine paved inset makes each parcel legible without adding structures.
    if (w > 5 && h > 5) {
      const ix = Math.min(1.2, w * .14), iz = Math.min(1.1, h * .14);
      builder.quad(V(x0 + ix, -.071, z0 + iz), V(x1 - ix, -.071, z0 + iz), V(x1 - ix, -.071, z1 - iz), V(x0 + ix, -.071, z1 - iz), 2, '#8a8880', V(0, 1, 0), [w * 1.2, h * 1.2]);
      for (let i = 0; i < 2; i++) addPlanter(builder, V(x0 + w * (.35 + i * .32), -.05, z0 + h * .5), .65);
    }
  }
}
function addBlockBorder(builder, x0, x1, z0, z1, color) {
  const y = -.035, t = .14;
  builder.quad(V(x0, y, z0), V(x1, y, z0), V(x1, y, z0 + t), V(x0, y, z0 + t), 2, color, V(0, 1, 0), [1, 1]);
  builder.quad(V(x0, y, z1 - t), V(x1, y, z1 - t), V(x1, y, z1), V(x0, y, z1), 2, color, V(0, 1, 0), [1, 1]);
  builder.quad(V(x0, y, z0), V(x0 + t, y, z0), V(x0 + t, y, z1), V(x0, y, z1), 2, color, V(0, 1, 0), [1, 1]);
  builder.quad(V(x1 - t, y, z0), V(x1, y, z0), V(x1, y, z1), V(x1 - t, y, z1), 2, color, V(0, 1, 0), [1, 1]);
}
function addTree(builder, point, radius = .78) {
  addCylinderY(builder, point.x, point.y, point.z, .12, 1.7, 4, '#57483a', 12, .085);
  addSphere(builder, V(point.x, point.y + 2.18, point.z), radius, 0, '#697c5e', 18, 10);
  addSphere(builder, V(point.x + .12, point.y + 2.38, point.z - .07), radius * .63, 0, '#77876a', 16, 8);
}
function addPlanter(builder, point, radius) {
  addRoundedBox(builder, V(point.x, point.y + .13, point.z), [radius * 2, .28, radius * 1.2], 0, 2, '#555651', .06, 3);
  addCylinderY(builder, point.x, point.y + .27, point.z, .08, .7, 4, '#5e4d3d', 10, .055);
  addSphere(builder, V(point.x, point.y + 1.05, point.z), radius * .55, 0, '#738568', 14, 8);
}

function addOffsetRibbon(builder, path, innerOffset, outerOffset, material, color, lift = .025) {
  const steps = Math.max(5, Math.ceil(pathLength(path) / 1.5));
  for (let i = 0; i < steps; i++) {
    const d0 = pathLength(path) * i / steps, d1 = pathLength(path) * (i + 1) / steps;
    const c0 = samplePath(path, d0), c1 = samplePath(path, d1);
    const t0 = tangentOnPath(path, d0), t1 = tangentOnPath(path, d1);
    const s0 = V(-t0.z, 0, t0.x), s1 = V(-t1.z, 0, t1.x);
    const p0 = add(c0, mul(s0, innerOffset)), p1 = add(c1, mul(s1, innerOffset));
    const p2 = add(c1, mul(s1, outerOffset)), p3 = add(c0, mul(s0, outerOffset));
    p0.y += lift; p1.y += lift; p2.y += lift; p3.y += lift;
    builder.quad(p0, p1, p2, p3, material, color, V(0, 1, 0), [1, Math.abs(outerOffset - innerOffset)]);
  }
}
function buildRoadSurface(builder, edge, path) {
  const width = edgeWidth(edge), half = width * .5;
  const count = Math.max(4, Math.ceil(width / 1.7));
  const steps = Math.max(8, Math.ceil(pathLength(path) / .85));
  const stations = [];
  for (let i = 0; i <= steps; i++) {
    const d = pathLength(path) * i / steps;
    const center = samplePath(path, d), tangent = tangentOnPath(path, d), side = V(-tangent.z, 0, tangent.x);
    const row = [];
    for (let j = 0; j <= count; j++) {
      const lateral = -half + (j / count) * width;
      const point = add(center, mul(side, lateral));
      point.y += .055 - .075 * Math.abs(lateral / half);
      const normal = normalize(V(side.x * (lateral < 0 ? .035 : -.035), 1, side.z * (lateral < 0 ? .035 : -.035)));
      row.push({ point, normal });
    }
    stations.push(row);
  }
  for (let i = 0; i < stations.length - 1; i++) for (let j = 0; j < count; j++) {
    const a = stations[i][j], b = stations[i + 1][j], c = stations[i + 1][j + 1], d = stations[i][j + 1];
    builder.quad(a.point, b.point, c.point, d.point, 1, edge.kind === 'highway' ? '#30312f' : '#343532', V(0, 1, 0), [1, width / count], [a.normal, b.normal, c.normal, d.normal]);
  }
  if (state.features.sidewalks) {
    for (const sign of [-1, 1]) {
      const inOffset = sign * (half - .04), outOffset = sign * (half + 1.55);
      addOffsetRibbon(builder, path, inOffset, outOffset, 2, '#77766e', .014);
      // A narrow raised curb face and cap catch light along each carriageway edge.
      const curbSteps = Math.max(5, Math.ceil(pathLength(path) / 1.4));
      for (let i = 0; i < curbSteps; i++) {
        const d0 = pathLength(path) * i / curbSteps, d1 = pathLength(path) * (i + 1) / curbSteps;
        const c0 = samplePath(path, d0), c1 = samplePath(path, d1);
        const t0 = tangentOnPath(path, d0), t1 = tangentOnPath(path, d1);
        const s0 = V(-t0.z, 0, t0.x), s1 = V(-t1.z, 0, t1.x);
        const base0 = add(c0, mul(s0, inOffset)), base1 = add(c1, mul(s1, inOffset));
        const top0 = add(c0, mul(s0, sign * (half + .055))), top1 = add(c1, mul(s1, sign * (half + .055)));
        base0.y += .02; base1.y += .02; top0.y += .16; top1.y += .16;
        builder.quad(base0, base1, top1, top0, 2, '#85847b', normalize(V(s0.x * sign, .25, s0.z * sign)), [1, .15]);
      }
      addOffsetRibbon(builder, path, sign * (half + 1.49), sign * (half + 1.57), 2, '#a09d92', .04);
    }
  }
}
function buildNetworkSurfaces(builder, net) {
  const random = seededRandom((net.seed ^ 0x91e10da5) >>> 0);
  for (let index = 0; index < net.edges.length; index++) {
    const edge = net.edges[index];
    const data = compiled.edgePaths.get(edge.id);
    if (!data || data.length < 1) continue;
    buildRoadSurface(builder, edge, data.path);
    if ((edge.kind === 'highway' || edge.kind === 'ramp') && state.features.sidewalks) {
      buildGuardrail(builder, data.path, edge.width * .5 + .38, .94, 2.5);
      buildGuardrail(builder, data.path, -edge.width * .5 - .38, .94, 2.5);
    }
    addRoadMarkings(builder, edge, data);
    if (state.features.damage && edge.kind === 'local' && index % 9 === 3 && data.length > 9) {
      const d = data.length * (.34 + random() * .35);
      addPothole(builder, samplePath(data.path, d), tangentOnPath(data.path, d), .58 + random() * .42, .42 + random() * .22, random() * 8);
    }
    if (state.features.damage && edge.kind === 'arterial' && index % 11 === 2 && data.length > 15) {
      const d = data.length * .65;
      addPothole(builder, samplePath(data.path, d), tangentOnPath(data.path, d), .48, .3, random() * 7);
    }
  }
  for (const data of compiled.junctions.values()) addJunctionSurface(builder, data, state.features.sidewalks);
  if (state.features.construction) buildConstructionZone(builder, net, random);
}
function buildConstructionZone(builder, net, random) {
  const candidates = net.edges.filter(edge => edge.kind === 'local' && compiled.edgePaths.get(edge.id)?.length > 14);
  if (!candidates.length) return;
  const edge = candidates[Math.floor(random() * candidates.length)];
  const path = compiled.edgePaths.get(edge.id).path;
  const d = pathLength(path) * .59;
  const center = samplePath(path, d), tangent = tangentOnPath(path, d), side = V(-tangent.z, 0, tangent.x), yaw = Math.atan2(tangent.z, tangent.x);
  const lengthAlong = 5.4, widthAcross = edgeWidth(edge) * .62;
  addRoundedBox(builder, V(center.x, center.y + .066, center.z), [lengthAlong, .025, widthAcross], yaw, 8, '#242522', .025, 3);
  // Milled edge teeth and a fresh blacktop section communicate a live resurfacing pass.
  for (let i = 0; i < 7; i++) {
    const offset = -widthAcross * .42 + i * (widthAcross * .14);
    const p = add(center, mul(side, offset));
    addRoundedBox(builder, V(p.x, center.y + .094, p.z), [lengthAlong * .92, .035, .1], yaw, 1, '#41413d', .018, 2);
  }
  const front = add(center, mul(tangent, -lengthAlong * .59));
  for (let i = 0; i < 5; i++) {
    const p = add(front, mul(side, -widthAcross * .45 + i * widthAcross * .225));
    p.y += .04;
    addTrafficCone(builder, p, .44 + .05 * (i % 2));
  }
  for (const s of [-1, 1]) {
    const p = add(center, mul(side, s * (widthAcross * .56)));
    addBarrier(builder, p, yaw, 2.5);
  }
  addBillboard(builder, add(center, mul(side, widthAcross * .75)), yaw + Math.PI / 2, 'ROAD WORK', '#c77c45');
}
function addTrafficCone(builder, point, height = .5) {
  addRoundedBox(builder, V(point.x, point.y + .04, point.z), [.42, .08, .42], 0, 4, '#272522', .03, 2);
  addCylinderBetween(builder, V(point.x, point.y + .08, point.z), V(point.x, point.y + height, point.z), .16, .018, 4, '#c96f44', 12);
  addCylinderY(builder, point.x, point.y + height * .5, point.z, .086, .075, 3, '#e4d9c7', 12, .08);
}
function addBarrier(builder, center, yaw, lengthAlong = 2.5) {
  addRoundedBox(builder, V(center.x, center.y + .39, center.z), [lengthAlong, .75, .35], yaw, 2, '#8b8880', .075, 3);
  const cos = Math.cos(yaw), sin = Math.sin(yaw);
  for (let i = 0; i < 4; i++) {
    const localX = -lengthAlong * .37 + i * lengthAlong * .25;
    const x = center.x + localX * cos;
    const z = center.z + localX * sin;
    addRoundedBox(builder, V(x, center.y + .42, z), [.22, .62, .385], yaw, 3, i % 2 ? '#d8c4a1' : '#ba6f46', .035, 2);
  }
  addRoundedBox(builder, V(center.x, center.y + .79, center.z), [lengthAlong * .93, .08, .39], yaw, 4, '#a39f95', .03, 2);
}
function buildGuardrail(builder, path, sideOffset, height = 1.0, spacing = 2.2) {
  if (!path || pathLength(path) < 2) return;
  const total = pathLength(path);
  const points = [];
  for (let d = 0; d < total; d += spacing) {
    const p = pathOffsetPoint(path, d, sideOffset);
    points.push(V(p.x, p.y + height, p.z));
    addCylinderY(builder, p.x, p.y + .08, p.z, .075, height - .05, 4, '#747975', 10, .065);
  }
  const end = pathOffsetPoint(path, total, sideOffset);
  points.push(V(end.x, end.y + height, end.z));
  addCylinderY(builder, end.x, end.y + .08, end.z, .075, height - .05, 4, '#747975', 10, .065);
  for (let i = 0; i < points.length - 1; i++) {
    addCylinderBetween(builder, points[i], points[i + 1], .055, .055, 4, '#9b9f99', 8);
    const lowerA = V(points[i].x, points[i].y - .34, points[i].z);
    const lowerB = V(points[i + 1].x, points[i + 1].y - .34, points[i + 1].z);
    addCylinderBetween(builder, lowerA, lowerB, .032, .032, 4, '#848983', 8);
  }
}
function buildStreetFurniture(builder, net) {
  if (state.features.streetscape === false) return;
  const junctions = [...net.nodes].filter(node => (compiled.nodeArms.get(node.id) || []).length >= 3);
  const random = seededRandom((net.seed ^ 0x84222325) >>> 0);
  junctions.forEach((node, index) => {
    const data = compiled.junctions.get(node.id);
    if (!data) return;
    const r = Math.max(...data.arms.map(arm => arm.outer)) + 1.5;
    if (state.features.streetlights && index % 2 === 0) {
      const sign = index % 4 < 2 ? 1 : -1;
      addStreetlight(builder, V(node.x + sign * r * .78, node.y, node.z - r * .66), index % 3 === 0);
    }
    if (state.features.signals && node.signalized && index < 8) {
      const arm = data.arms[index % data.arms.length];
      const p = add(node, V(0, 0, 0));
      const q = add(arm.center, mul(arm.side, arm.outer + .55));
      q.x += arm.tangent.x * .75; q.z += arm.tangent.z * .75;
      addTrafficSignal(builder, q, Math.atan2(arm.tangent.z, arm.tangent.x));
    }
    if (state.features.bollards && index % 2 === 1) {
      for (let b = 0; b < 3; b++) {
        const arm = data.arms[b % data.arms.length];
        const p = add(arm.center, mul(arm.side, arm.outer + .25 + b * .48));
        p.x += arm.tangent.x * .25; p.z += arm.tangent.z * .25;
        addBollard(builder, p, b % 2 === 0);
      }
    }
    if (state.features.parking && index % 3 === 1 && data.arms.length >= 3) {
      const arm = data.arms[0];
      const p = add(arm.center, mul(arm.tangent, 4.5));
      addParkingSign(builder, add(p, mul(arm.side, arm.outer + .8)), Math.atan2(arm.tangent.z, arm.tangent.x));
    }
  });
  if (state.features.billboards && junctions.length) {
    const node = junctions[Math.min(3, junctions.length - 1)];
    const data = compiled.junctions.get(node.id);
    const arm = data?.arms?.[0];
    if (arm) {
      const p = add(arm.center, mul(arm.tangent, 8));
      p.x += arm.side.x * (arm.outer + 4); p.z += arm.side.z * (arm.outer + 4);
      addBillboard(builder, p, Math.atan2(arm.tangent.z, arm.tangent.x), 'NORTH QUAY', '#677d70');
    }
  }
  if (state.features.parking) buildParkingBayDetails(builder, net, random);
  if (state.features.robots) buildServiceRobots(builder, net);
}
function addServiceRobot(builder, point, yaw, accent = '#668c7e') {
  const forward = V(Math.cos(yaw), 0, Math.sin(yaw));
  const side = V(-Math.sin(yaw), 0, Math.cos(yaw));
  const local = (f, y, s) => add(add(point, mul(forward, f)), add(V(0, y, 0), mul(side, s)));
  // Compact delivery chassis, padded body and inset glass face; wheels remain fully exposed.
  addRoundedBox(builder, local(-.02, .22, 0), [.98, .3, .78], yaw, 5, '#434946', .09, 3);
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    const wheel = local(sx * .29, .2, sz * .405);
    addCylinderBetween(builder, add(wheel, mul(side, -.075)), add(wheel, mul(side, .075)), .17, .17, 4, '#252a28', 12);
    addCylinderBetween(builder, add(wheel, mul(side, sz * .078)), add(wheel, mul(side, sz * .087)), .077, .077, 4, '#8e938b', 10);
  }
  addRoundedBox(builder, local(0, .75, 0), [.96, .82, .74], yaw, 5, '#c5c6bd', .15, 4);
  addRoundedBox(builder, local(-.015, 1.23, 0), [.86, .16, .7], yaw, 5, '#aeb4aa', .07, 3);
  addRoundedBox(builder, local(-.06, .76, .377), [.72, .08, .026], yaw, 5, accent, .02, 2);
  addRoundedBox(builder, local(.49, .92, 0), [.035, .34, .47], yaw, 6, '#26393d', .035, 3);
  addRoundedBox(builder, local(.514, .96, 0), [.012, .055, .3], yaw, 9, '#b9d6c5', .006, 2);
  for (const s of [-1, 1]) {
    addRoundedBox(builder, local(.53, .79, s * .125), [.02, .055, .055], yaw, 9, '#e5d5a9', .01, 2);
    addRoundedBox(builder, local(-.5, .38, s * .15), [.05, .16, .23], yaw, 4, '#727974', .018, 2);
  }
  addCylinderBetween(builder, local(-.26, 1.27, 0), local(-.26, 1.49, 0), .035, .025, 4, '#5a625e', 10);
  addSphere(builder, local(-.26, 1.53, 0), .08, 9, accent, 12, 7);
  addRoundedBox(builder, local(-.44, .62, .38), [.12, .18, .025], yaw, 9, '#dfd2b7', .015, 2);
}
function buildServiceRobots(builder, net) {
  const candidates = net.nodes.filter(node => node.degree >= 3 && compiled.junctions.has(node.id));
  candidates.sort((a, b) => (Math.abs(a.x) + Math.abs(a.z)) - (Math.abs(b.x) + Math.abs(b.z)));
  candidates.slice(0, Math.min(3, candidates.length)).forEach((node, index) => {
    const data = compiled.junctions.get(node.id);
    const arm = data.arms[(index + 1) % data.arms.length];
    const sign = index % 2 ? -1 : 1;
    const point = add(add(arm.center, mul(arm.side, sign * (arm.outer + 1.35))), mul(arm.tangent, index % 2 ? -2.2 : 2.2));
    point.y = node.y + .015;
    const yaw = Math.atan2(arm.tangent.z, arm.tangent.x) + (index % 2 ? Math.PI : 0);
    addServiceRobot(builder, point, yaw, ['#668c7e', '#c18855', '#7689a0'][index % 3]);
  });
}
function addStreetlight(builder, point, double = false) {
  const poleHeight = 6.6;
  addCylinderY(builder, point.x, point.y, point.z, .115, poleHeight, 4, '#4f5553', 14, .075);
  addCylinderY(builder, point.x, point.y + .03, point.z, .22, .16, 2, '#65665e', 16, .2);
  const armEnd = V(point.x + 1.22, point.y + poleHeight + .04, point.z - .12);
  addCylinderBetween(builder, V(point.x, point.y + poleHeight - .05, point.z), V(point.x + .96, point.y + poleHeight + .28, point.z - .08), .075, .055, 4, '#535956', 10);
  addRoundedBox(builder, V(armEnd.x, armEnd.y, armEnd.z), [1.05, .18, .38], -.06, 4, '#555a56', .055, 3);
  addRoundedBox(builder, V(armEnd.x, armEnd.y - .105, armEnd.z), [.72, .025, .24], -.06, 9, '#d9c4a3', .02, 2);
  if (double) {
    const other = V(point.x - 1.1, point.y + poleHeight + .03, point.z + .1);
    addCylinderBetween(builder, V(point.x, point.y + poleHeight - .05, point.z), V(point.x - .9, point.y + poleHeight + .26, point.z + .08), .065, .05, 4, '#535956', 10);
    addRoundedBox(builder, other, [.88, .16, .34], .08, 4, '#555a56', .05, 3);
    addRoundedBox(builder, V(other.x, other.y - .1, other.z), [.62, .024, .2], .08, 9, '#d9c4a3', .015, 2);
  }
}
function addTrafficSignal(builder, point, yaw = 0) {
  addCylinderY(builder, point.x, point.y, point.z, .075, 4.7, 4, '#4c504e', 12, .06);
  addCylinderBetween(builder, V(point.x, point.y + 4.65, point.z), V(point.x + Math.cos(yaw) * 1.35, point.y + 4.65, point.z + Math.sin(yaw) * 1.35), .055, .045, 4, '#535856', 10);
  const head = V(point.x + Math.cos(yaw) * 1.32, point.y + 4.12, point.z + Math.sin(yaw) * 1.32);
  addRoundedBox(builder, head, [.37, 1.02, .32], yaw, 4, '#292e2d', .065, 3);
  for (let i = 0; i < 3; i++) {
    const c = V(head.x + Math.cos(yaw) * .19, head.y + .31 - i * .31, head.z + Math.sin(yaw) * .19);
    addSphere(builder, c, .105, 9, ['#da6654', '#dcaa61', '#7ba984'][i], 12, 7);
  }
}
function addBollard(builder, point, reflective = true) {
  addCylinderY(builder, point.x, point.y, point.z, .16, .12, 2, '#76736c', 14, .16);
  addCylinderY(builder, point.x, point.y + .08, point.z, .078, .73, 4, '#656a67', 14, .061);
  if (reflective) addCylinderY(builder, point.x, point.y + .48, point.z, .064, .12, 3, '#c9c0aa', 14, .064);
  addSphere(builder, V(point.x, point.y + .82, point.z), .065, 4, '#777b77', 12, 7);
}
function addParkingSign(builder, point, yaw = 0) {
  addCylinderY(builder, point.x, point.y, point.z, .045, 2.3, 4, '#717773', 10, .04);
  addRoundedBox(builder, V(point.x, point.y + 1.98, point.z), [.66, .8, .09], yaw, 4, '#e8e5dc', .03, 2);
  addRoundedBox(builder, V(point.x, point.y + 1.99, point.z - .055), [.42, .57, .025], yaw, 9, '#547162', .015, 2);
}
function addBillboard(builder, point, yaw, text, color = '#667a71') {
  const cy = point.y + 5.15;
  addCylinderY(builder, point.x - .85, point.y, point.z, .115, 4.05, 4, '#555a56', 14, .09);
  addCylinderY(builder, point.x + .85, point.y, point.z, .115, 4.05, 4, '#555a56', 14, .09);
  addRoundedBox(builder, V(point.x, cy, point.z), [5.4, 2.1, .3], yaw, 4, '#4e514d', .07, 3);
  addRoundedBox(builder, V(point.x, cy, point.z + .17), [5.06, 1.77, .055], yaw, 2, color, .035, 3);
  // Geometric lettering and route bars keep this prop self-contained and scale-correct.
  const cos = Math.cos(yaw), sin = Math.sin(yaw);
  const local = (x, y, z = .23) => V(point.x + x * cos - z * sin, cy + y, point.z + x * sin + z * cos);
  for (let i = 0; i < 5; i++) {
    const x = -1.85 + i * .15;
    const a = local(x, .39), b = local(x + (i % 2 ? .07 : 0), -.28);
    addCylinderBetween(builder, a, b, .025, .025, 3, '#e4e0d5', 6);
  }
  const barColor = text === 'ROAD WORK' ? '#edd0a9' : '#d3d5c9';
  addRoundedBox(builder, local(.83, -.18), [1.28, .08, .03], yaw, 3, barColor, .01, 1);
  addRoundedBox(builder, local(.83, -.42), [.82, .055, .03], yaw, 3, '#b6c5ad', .01, 1);
}
function buildParkingBayDetails(builder, net, random) {
  const locals = net.edges.filter(edge => edge.kind === 'local' && (compiled.edgePaths.get(edge.id)?.length || 0) > 14);
  locals.slice(0, 4).forEach((edge, index) => {
    const path = compiled.edgePaths.get(edge.id).path;
    const d = pathLength(path) * (.36 + (index % 2) * .26);
    const p = samplePath(path, d), t = tangentOnPath(path, d), side = V(-t.z, 0, t.x);
    const offset = edge.width * .28 * (index % 2 ? -1 : 1);
    const center = add(p, mul(side, offset));
    const angle = Math.atan2(t.z, t.x);
    for (let s = -1; s <= 1; s += 2) {
      const pos = add(center, mul(t, s * 2.55));
      addParkedCar(builder, V(pos.x, pos.y + .03, pos.z), angle, ['#78817d', '#886f5f', '#565e68', '#a29b88'][Math.floor(random() * 4)]);
    }
  });
}
function addParkedCar(builder, point, yaw, color) {
  const parkedBudget = Math.max(0, 25 - clamp(Number(state.trafficCount) || 0, 0, 25));
  if (parkedVehicleCount >= parkedBudget) return;
  parkedVehicleCount++;
  addVehicle(builder, point, yaw, color, 'compact', false);
}
function addVehicle(builder, point, yaw, color = '#78858a', style = 'sedan', animate = false) {
  const truck = style === 'truck';
  const lengthX = truck ? 6.3 : style === 'compact' ? 3.8 : 4.45;
  const width = truck ? 2.35 : style === 'compact' ? 1.76 : 1.92;
  const groundY = point.y;
  const bodyHeight = truck ? 1.42 : .84;
  const bodyY = groundY + (truck ? .72 : .59);
  addRoundedBox(builder, V(point.x, bodyY, point.z), [lengthX, bodyHeight, width], yaw, 5, color, .15, 3);
  if (truck) {
    addRoundedBox(builder, V(point.x + Math.cos(yaw) * (lengthX * .19), groundY + 1.65, point.z + Math.sin(yaw) * (lengthX * .19)), [2.35, 1.25, width * .88], yaw, 5, color, .14, 3);
    addRoundedBox(builder, V(point.x - Math.cos(yaw) * (lengthX * .24), groundY + 1.22, point.z - Math.sin(yaw) * (lengthX * .24)), [2.3, .74, width * .88], yaw, 4, '#777a74', .08, 2);
  } else {
    const cabin = V(point.x - Math.cos(yaw) * .12, groundY + 1.16, point.z - Math.sin(yaw) * .12);
    addRoundedBox(builder, cabin, [lengthX * .47, .72, width * .8], yaw, 6, '#53626a', .12, 3);
    addRoundedBox(builder, V(cabin.x, cabin.y + .37, cabin.z), [lengthX * .33, .055, width * .48], yaw, 5, color, .03, 2);
  }
  const forward = V(Math.cos(yaw), 0, Math.sin(yaw));
  const side = V(-Math.sin(yaw), 0, Math.cos(yaw));
  const wheelX = truck ? 2.05 : lengthX * .32;
  const wheelZ = width * .46;
  for (const dx of [-wheelX, wheelX]) for (const dz of [-wheelZ, wheelZ]) {
    const c = add(add(point, mul(forward, dx)), mul(side, dz));
    c.y = groundY + .31;
    const axis = mul(side, .28);
    addCylinderBetween(builder, add(c, mul(axis, -.5)), add(c, mul(axis, .5)), .34, .34, 4, '#222321', 14);
    const hub = add(c, mul(axis, .56));
    addCylinderBetween(builder, hub, add(hub, mul(axis, .06)), .15, .15, 4, '#88877d', 12);
  }
  const front = add(point, mul(forward, lengthX * .47));
  for (const s of [-1, 1]) {
    const light = add(front, mul(side, s * width * .31)); light.y = groundY + .66;
    addRoundedBox(builder, light, [.12, .19, .31], yaw, 9, '#e6d2a8', .035, 2);
    const tail = add(point, mul(forward, -lengthX * .47)); tail.y = groundY + .62; tail.x += side.x * s * width * .31; tail.z += side.z * s * width * .31;
    addRoundedBox(builder, tail, [.1, .18, .27], yaw, 9, '#ad594c', .025, 2);
  }
  if (animate) {
    const mirrorPoint = add(add(point, mul(forward, .3)), mul(side, width * .54)); mirrorPoint.y = groundY + .9;
    addRoundedBox(builder, mirrorPoint, [.34, .12, .16], yaw, 5, color, .04, 2);
  }
}

function smoothstep(a, b, x) { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
function makePolyline(points) {
  const path = [];
  let d = 0;
  for (let i = 0; i < points.length; i++) {
    if (i) d += length(sub(points[i], points[i - 1]));
    path.push({ p: points[i], d });
  }
  return path;
}
function makeViaductPath(z = 22) {
  const points = [];
  const left = -45, right = 45, rise = 4.1, ramp = 22;
  const samples = 180;
  for (let i = 0; i <= samples; i++) {
    const x = mix(left, right, i / samples);
    let y = rise;
    if (x < left + ramp) y = rise * smoothstep(left, left + ramp, x);
    else if (x > right - ramp) y = rise * (1 - smoothstep(right - ramp, right, x));
    points.push(V(x, y, z));
  }
  return makePolyline(points);
}
function buildViaduct(builder) {
  const path = makeViaductPath(network.preset === 'suburban' ? 20 : 22);
  const bridge = { width: 8.5, lanes: 2, laneWidth: 3.35, shoulder: .9, kind: 'arterial', speed: 70 };
  buildRoadSurface(builder, bridge, path);
  if (state.features.markings) {
    addLaneRibbon(builder, path, bridge, -.11, .1, 3, '#d4b274', 2, pathLength(path) - 2);
    addLaneRibbon(builder, path, bridge, .11, .1, 3, '#d4b274', 2, pathLength(path) - 2);
    addLaneRibbon(builder, path, bridge, -2.5, .1, 3, '#d1d0c7', 1, pathLength(path) - 1);
    addLaneRibbon(builder, path, bridge, 2.5, .1, 3, '#d1d0c7', 1, pathLength(path) - 1);
  }
  for (const side of [-1, 1]) buildGuardrail(builder, path, side * 4.32, 1.1, 2.4);
  // The continuous box girder, piers and cap beams provide an actual raised deck silhouette.
  for (let x = -40; x < 40; x += 4) {
    const mid = x + 2;
    let y = 4.1;
    if (mid < -23) y = 4.1 * smoothstep(-45, -23, mid);
    else if (mid > 23) y = 4.1 * (1 - smoothstep(23, 45, mid));
    addRoundedBox(builder, V(mid, y - .78, path[path.length - 1].p.z), [4.08, .95, 9.15], 0, 2, '#696c68', .12, 3);
  }
  for (const x of [-17, -2, 13, 28]) {
    let y = 4.1;
    if (x < -23) y = 4.1 * smoothstep(-45, -23, x);
    else if (x > 23) y = 4.1 * (1 - smoothstep(23, 45, x));
    if (y < .8) continue;
    addRoundedBox(builder, V(x, y - 1.06, path[0].p.z), [2.5, .55, 10.4], 0, 2, '#85847a', .08, 3);
    for (const side of [-1, 1]) {
      addRoundedBox(builder, V(x + side * 1.05, (y - 1.05) * .5 - .18, path[0].p.z), [.85, y - 1.15, .85], 0, 2, '#777971', .07, 3);
      addRoundedBox(builder, V(x + side * 1.05, -.18, path[0].p.z), [2.2, .38, 2.2], 0, 2, '#85847a', .09, 3);
    }
  }
  // Expansion joints and drains are deliberately sparse and scale-accurate.
  for (const x of [-24, -8, 8, 24]) {
    const y = x < -23 ? 4.1 * smoothstep(-45, -23, x) : x > 23 ? 4.1 * (1 - smoothstep(23, 45, x)) : 4.1;
    addRoundedBox(builder, V(x, y + .063, path[0].p.z), [.15, .012, 8.05], 0, 4, '#787872', .008, 1);
  }
}
function addPortalFrame(builder, x, railZ) {
  const wallColor = '#77766e';
  const bottom = -3.75, top = 1.7;
  const postHeight = top - bottom, postCenter = (top + bottom) * .5;
  addRoundedBox(builder, V(x, postCenter, railZ - 3.45), [.72, postHeight, .9], 0, 2, wallColor, .08, 3);
  addRoundedBox(builder, V(x, postCenter, railZ + 3.45), [.72, postHeight, .9], 0, 2, wallColor, .08, 3);
  addRoundedBox(builder, V(x, 1.34, railZ), [.9, .72, 7.85], 0, 2, '#85847b', .11, 3);
  addRoundedBox(builder, V(x + (x < 0 ? .38 : -.38), -.18, railZ), [.035, .035, 5.25], 0, 9, '#c4b89f', .01, 1);
  // Battered portal cheek plates and a dark recess make the rail bore read as a tunnel.
  for (const sign of [-1, 1]) {
    addRoundedBox(builder, V(x + (x < 0 ? -.46 : .46), -.2, railZ + sign * 4.05), [.56, 2.65, .62], 0, 2, '#696963', .08, 3);
  }
}
function buildRailUnderpass(builder) {
  const railZ = network.preset === 'suburban' ? -17 : network.preset === 'highway' ? 19 : -18;
  network.railZ = railZ;
  const bedY = -4.0;
  // Deep ballast, tie slabs and continuous steel rails.
  addRoundedBox(builder, V(0, bedY + .04, railZ), [100, .52, 5.2], 0, 7, '#554f45', .17, 4);
  addRoundedBox(builder, V(0, bedY + .28, railZ), [100, .12, 3.35], 0, 1, '#333330', .025, 2);
  for (let x = -49; x <= 49; x += 1.65) {
    addRoundedBox(builder, V(x, bedY + .4, railZ), [.26, .15, 3.2], 0, 7, '#746451', .035, 3);
    addRoundedBox(builder, V(x, bedY + .485, railZ), [.16, .035, 3.05], 0, 4, '#898a82', .012, 2);
  }
  for (const z of [railZ - .72, railZ + .72]) {
    addRoundedBox(builder, V(0, bedY + .57, z), [100, .2, .14], 0, 7, '#aaa99f', .055, 3);
    addRoundedBox(builder, V(0, bedY + .7, z), [100, .045, .07], 0, 4, '#c6c5bc', .018, 2);
  }
  if (state.features.rail) {
    for (const side of [-1, 1]) {
      addRoundedBox(builder, V(0, -1.72, railZ + side * 3.72), [100, 4.62, .62], 0, 2, '#77766e', .12, 3);
      addRoundedBox(builder, V(0, -.28, railZ + side * 3.32), [100, .35, .22], 0, 2, '#8e8c83', .07, 3);
    }
    // A short concrete roof under the boulevard is a bridge slab, not an overlapping road card.
    addRoundedBox(builder, V(0, -.19, railZ), [16.8, .25, 8.25], 0, 2, '#818078', .08, 3);
    addPortalFrame(builder, -8.15, railZ);
    addPortalFrame(builder, 8.15, railZ);
    for (const x of [-6.5, -3.5, 0, 3.5, 6.5]) {
      addRoundedBox(builder, V(x, -.285, railZ), [.12, .035, 5.8], 0, 9, '#d7c398', .015, 2);
    }
    for (const side of [-1, 1]) {
      addRoundedBox(builder, V(0, -1.98, railZ + side * 3.1), [13.2, .12, .24], 0, 4, '#9a9991', .035, 2);
    }
  }
}
function buildAllGeometry(net = network) {
  if (!net) return new MeshBuilder();
  compiled = compileNetwork(net);
  parkedVehicleCount = 0;
  const builder = new MeshBuilder();
  const random = seededRandom((net.seed ^ 0xc82b2ae3) >>> 0);
  buildTerrain(builder, compiled.bounds);
  buildCityBlocks(builder, net, random);
  if (state.features.network !== false) buildNetworkSurfaces(builder, net);
  if (state.features.bridge) buildViaduct(builder);
  if (state.features.rail) buildRailUnderpass(builder);
  if (state.features.streetlights || state.features.signals || state.features.bollards || state.features.billboards || state.features.robots || state.features.parking) buildStreetFurniture(builder, net);
  return builder;
}

function initializeTraffic() {
  const candidates = network.edges.filter(edge => {
    const data = compiled.edgePaths.get(edge.id);
    return data && data.length > 5.5 && edge.kind !== 'ramp';
  });
  if (!candidates.length) {
    trafficAgents = [];
    return;
  }
  const count = clamp(Number(state.trafficCount) || 0, 0, 25);
  const random = seededRandom((network.seed ^ 0x4f1bbcdc) >>> 0);
  trafficAgents = Array.from({ length: count }, (_, index) => {
    const candidateIndex = index % candidates.length;
    const edge = candidates[candidateIndex];
    const data = compiled.edgePaths.get(edge.id);
    const agentsOnEdge = Math.ceil((count - candidateIndex) / candidates.length);
    const slot = Math.floor(index / candidates.length);
    const fraction = clamp((slot + .5 + (random() - .5) * .12) / agentsOnEdge, .08, .92);
    const laneCount = Math.max(1, Math.floor(edge.lanes / 2));
    return {
      id: `vehicle-${String(index + 1).padStart(2, '0')}`, edgeId: edge.id,
      direction: index % 2 ? -1 : 1, s: fraction * data.length,
      laneIndex: index % laneCount, speed: 3.1 + random() * 2.0,
      color: ['#858c86', '#72818a', '#927660', '#626a75', '#9b9a8d', '#576970', '#9a5947', '#7a8075'][index % 8],
      style: index % 9 === 4 ? 'truck' : 'sedan', turns: Math.floor(random() * 5),
    };
  });
  updateVehicleCount();
}
function trafficLaneOffset(edge, direction, laneIndex) {
  const lanesPerDirection = Math.max(1, Math.floor(edge.lanes / 2));
  const laneW = (edge.width - edge.shoulder * 2) / edge.lanes;
  const lane = clamp(Math.trunc(laneIndex) || 0, 0, lanesPerDirection - 1);
  const lateral = edge.width / 2 - edge.shoulder - laneW * (lane + .5);
  let sign = direction > 0 ? -1 : 1;
  if (state.trafficSide === 'left') sign *= -1;
  return sign * lateral;
}
function nextTrafficEdge(agent, current, nodeId) {
  const connected = network.edges.filter(edge => (edge.a === nodeId || edge.b === nodeId) && edge.id !== current.id);
  if (!connected.length) return { edge: current, direction: agent.direction * -1 };
  const currentData = compiled.edgePaths.get(current.id);
  const incoming = agent.direction > 0
    ? tangentOnPath(currentData.path, currentData.length)
    : mul(tangentOnPath(currentData.path, 0), -1);
  let best = null, bestScore = -Infinity;
  for (const edge of connected) {
    const direction = edge.a === nodeId ? 1 : -1;
    const data = compiled.edgePaths.get(edge.id);
    if (!data || data.length < 2) continue;
    const outgoing = direction > 0 ? tangentOnPath(data.path, 0) : mul(tangentOnPath(data.path, data.length), -1);
    const score = dot(incoming, outgoing) + (Math.sin(agent.turns * 8.7 + edge.id.length) * .24);
    if (score > bestScore) { best = { edge, direction }; bestScore = score; }
  }
  return best || { edge: current, direction: agent.direction * -1 };
}
function makeTrafficTransition(agent, current, next) {
  const currentData = compiled.edgePaths.get(current.id);
  const nextData = compiled.edgePaths.get(next.edge.id);
  const atCurrentEnd = agent.direction > 0 ? currentData.length : 0;
  const p0 = samplePath(currentData.path, atCurrentEnd);
  const inTangent = agent.direction > 0
    ? tangentOnPath(currentData.path, currentData.length)
    : mul(tangentOnPath(currentData.path, 0), -1);
  const atNextStart = next.direction > 0 ? 0 : nextData.length;
  const p3 = samplePath(nextData.path, atNextStart);
  const outTangent = next.direction > 0
    ? tangentOnPath(nextData.path, 0)
    : mul(tangentOnPath(nextData.path, nextData.length), -1);
  const chord = Math.hypot(p3.x - p0.x, p3.z - p0.z);
  const currentTrim = agent.direction > 0 ? currentData.endTrim : currentData.startTrim;
  const nextTrim = next.direction > 0 ? nextData.startTrim : nextData.endTrim;
  const handle = Math.min(chord * .38, Math.max(.45, Math.min(currentTrim, nextTrim) * .64));
  const path = chord < .04
    ? makePolyline([p0])
    : makeSpline(p0, add(p0, mul(inTangent, handle)), sub(p3, mul(outTangent, handle)), p3, Math.max(8, Math.ceil(chord * 2.2)));
  const targetLane = Math.min(agent.laneIndex, Math.max(1, Math.floor(next.edge.lanes / 2)) - 1);
  return {
    path, length: pathLength(path), s: 0,
    nextEdgeId: next.edge.id, nextDirection: next.direction, nextLane: targetLane,
    fromOffset: trafficLaneOffset(current, agent.direction, agent.laneIndex),
    toOffset: trafficLaneOffset(next.edge, next.direction, targetLane),
  };
}
function enterTrafficEdge(agent, transition) {
  const edge = network.edges.find(item => item.id === transition.nextEdgeId);
  const data = edge && compiled.edgePaths.get(edge.id);
  agent.edgeId = transition.nextEdgeId;
  agent.direction = transition.nextDirection;
  agent.laneIndex = transition.nextLane;
  agent.s = agent.direction > 0 ? 0 : (data?.length || 0);
  agent.transition = null;
  agent.turns++;
}
function advanceTraffic(dt) {
  if (!state.playing || !trafficAgents.length) return;
  for (const agent of trafficAgents) {
    let remaining = dt * agent.speed;
    let safety = 0;
    while (remaining > 0 && safety++ < 6) {
      if (agent.transition) {
        const transition = agent.transition;
        const available = Math.max(0, transition.length - transition.s);
        if (remaining <= available) { transition.s += remaining; remaining = 0; }
        else { remaining -= available; enterTrafficEdge(agent, transition); }
        continue;
      }
      const edge = network.edges.find(item => item.id === agent.edgeId);
      const data = edge && compiled.edgePaths.get(edge.id);
      if (!edge || !data) break;
      const available = agent.direction > 0 ? data.length - agent.s : agent.s;
      if (remaining <= available) {
        agent.s += agent.direction > 0 ? remaining : -remaining;
        remaining = 0;
        continue;
      }
      remaining -= Math.max(0, available);
      const nodeId = agent.direction > 0 ? edge.b : edge.a;
      const next = nextTrafficEdge(agent, edge, nodeId);
      const transition = makeTrafficTransition(agent, edge, next);
      if (transition.length < .025) enterTrafficEdge(agent, transition);
      else agent.transition = transition;
    }
  }
}
function buildTrain(builder) {
  if (!state.features.rail || state.features.train === false) return;
  const z = network.railZ ?? -18;
  const trainX = ((trainClock * 2.3 + 39) % 96) - 48;
  for (let car = 0; car < 4; car++) {
    const x = trainX - car * 8.8;
    if (x < -49 || x > 49) continue;
    const locomotive = car === 0;
    const bodyColor = locomotive ? '#586d62' : car % 2 ? '#b7b6ad' : '#a9a99f';
    const railTop = -4.0 + .745;
    const baseY = railTop + .065;
    addRoundedBox(builder, V(x, baseY + .96, z), [locomotive ? 7.4 : 8.2, 1.92, 2.72], 0, 5, bodyColor, .17, 4);
    addRoundedBox(builder, V(x, baseY + 1.98, z), [locomotive ? 5.9 : 6.65, .18, 2.55], 0, 4, locomotive ? '#44554c' : '#87877f', .08, 3);
    if (locomotive) addRoundedBox(builder, V(x + 1.95, baseY + 1.82, z), [2.15, .78, 2.36], 0, 5, '#52645a', .13, 3);
    for (let win = -2; win <= 2; win++) {
      const wx = x + win * 1.16 - (locomotive ? .1 : 0);
      addRoundedBox(builder, V(wx, baseY + 1.37, z - 1.378), [.82, .56, .035], 0, 6, '#364650', .055, 3);
      addRoundedBox(builder, V(wx, baseY + 1.37, z + 1.378), [.82, .56, .035], 0, 6, '#364650', .055, 3);
    }
    for (const dx of [-2.55, 2.55]) for (const side of [-1, 1]) {
      const center = V(x + dx, baseY - .24, z + side * 1.2);
      addCylinderBetween(builder, V(center.x, center.y, center.z - .21), V(center.x, center.y, center.z + .21), .48, .48, 4, '#272927', 16);
      addCylinderBetween(builder, V(center.x, center.y, center.z + side * .22), V(center.x, center.y, center.z + side * .26), .22, .22, 4, '#8b8b83', 12);
    }
    for (const side of [-1, 1]) {
      addRoundedBox(builder, V(x + (locomotive ? 3.73 : 4.12), baseY + .42, z + side * .02), [.06, .22, .18], 0, 9, locomotive ? '#e2c88d' : '#bd6858', .03, 2);
    }
    if (car < 3) addCylinderBetween(builder, V(x - 3.9, baseY + .3, z), V(x - 4.8, baseY + .3, z), .1, .1, 4, '#3e413e', 10);
  }
}
function buildDynamicGeometry() {
  const builder = new MeshBuilder();
  if (state.features.traffic && state.trafficCount > 0) {
    for (const agent of trafficAgents.slice(0, Math.max(0, Math.min(25, 25 - parkedVehicleCount)))) {
      const edge = network.edges.find(item => item.id === agent.edgeId);
      const data = edge && compiled.edgePaths.get(edge.id);
      if (!edge || !data) continue;
      let point, tangent, laneOffset;
      if (agent.transition) {
        const transition = agent.transition;
        const turnDistance = clamp(transition.s, 0, transition.length);
        point = samplePath(transition.path, turnDistance);
        tangent = tangentOnPath(transition.path, turnDistance);
        const t = transition.length > .001 ? smoothstep(0, 1, turnDistance / transition.length) : 1;
        laneOffset = mix(transition.fromOffset, transition.toOffset, t);
      } else {
        const s = agent.direction > 0 ? agent.s : data.length - agent.s;
        point = samplePath(data.path, s);
        tangent = tangentOnPath(data.path, s);
        if (agent.direction < 0) tangent = mul(tangent, -1);
        laneOffset = trafficLaneOffset(edge, agent.direction, agent.laneIndex);
      }
      const side = V(-tangent.z, 0, tangent.x);
      point = add(point, mul(side, laneOffset));
      addVehicle(builder, V(point.x, point.y + .06, point.z), Math.atan2(tangent.z, tangent.x), agent.color, agent.style, true);
    }
  }
  if (state.features.train) buildTrain(builder);
  return builder;
}

const WGSL = /* wgsl */ `
struct Frame {
  viewProjection: mat4x4<f32>,
  camera: vec4<f32>,
  sun: vec4<f32>,
  surface: vec4<f32>,
  layers: vec4<f32>,
};
@group(0) @binding(0) var<uniform> frame: Frame;
struct VertexIn {
  @location(0) position: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) color: vec4<f32>,
  @location(4) material: f32,
};
struct VertexOut {
  @builtin(position) position: vec4<f32>,
  @location(0) world: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) color: vec4<f32>,
  @location(4) material: f32,
  @location(5) fogDepth: f32,
};
fn hash21(p: vec2<f32>) -> f32 {
  let q = vec3<f32>(p.x, p.y, p.x) * 0.1031;
  let r = q + dot(q, q.yzx + vec3<f32>(33.33, 33.33, 33.33));
  return fract((r.x + r.y) * r.z);
}
fn noise2(p: vec2<f32>) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let u = f * f * (vec2<f32>(3.0, 3.0) - 2.0 * f);
  return mix(mix(hash21(i), hash21(i + vec2<f32>(1.0, 0.0)), u.x),
             mix(hash21(i + vec2<f32>(0.0, 1.0)), hash21(i + vec2<f32>(1.0, 1.0)), u.x), u.y);
}
fn fbm(p0: vec2<f32>) -> f32 {
  var p = p0;
  var value = 0.0;
  var amplitude = 0.5;
  for (var i = 0; i < 4; i = i + 1) {
    value += noise2(p) * amplitude;
    p = p * 2.03 + vec2<f32>(7.1, 13.7);
    amplitude *= 0.48;
  }
  return value;
}
fn toneMap(color: vec3<f32>) -> vec3<f32> {
  let x = max(color, vec3<f32>(0.0));
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
}
@vertex fn vsMain(input: VertexIn) -> VertexOut {
  var output: VertexOut;
  let world = input.position;
  output.position = frame.viewProjection * vec4<f32>(world, 1.0);
  output.world = world;
  output.normal = normalize(input.normal);
  output.uv = input.uv;
  output.color = input.color;
  output.material = input.material;
  output.fogDepth = distance(frame.camera.xyz, world);
  return output;
}
@fragment fn fsMain(input: VertexOut) -> @location(0) vec4<f32> {
  let n = normalize(input.normal);
  let matId = i32(input.material + 0.5);
  let w = input.world.xz;
  let coarse = fbm(w * 0.09);
  let micro = noise2(w * 13.7);
  var base = input.color.rgb;
  var roughness = 0.8;
  var metalness = 0.0;
  var emissive = 0.0;

  if (matId == 0) {
    let colorNoise = fbm(w * 0.43);
    let blade = noise2(w * 5.7);
    base *= 0.78 + colorNoise * 0.44 + (blade - 0.5) * 0.1;
    let patch = smoothstep(0.54, 0.83, fbm(w * 0.16 + vec2<f32>(41.0, 8.0)));
    base = mix(base, base * vec3<f32>(0.78, 0.76, 0.67), patch * 0.32);
    roughness = 0.97;
  } else if (matId == 1) {
    let aggregate = noise2(w * 20.5);
    let variation = fbm(w * 0.68 + vec2<f32>(15.0, 31.0));
    let oil = smoothstep(0.61, 0.83, fbm(w * 0.42 + vec2<f32>(7.0, 39.0)));
    let wear = frame.surface.z * (0.22 + variation * 0.48);
    let grain = (aggregate - 0.5) * 0.19 * frame.surface.y;
    base *= 0.72 + variation * 0.33 + grain;
    base = mix(base, base * vec3<f32>(0.67, 0.66, 0.61), oil * wear);
    let fissure = abs(sin(w.x * 1.11 + sin(w.y * 0.9) * 2.1 + coarse * 7.0));
    let crack = (1.0 - smoothstep(0.0, 0.045, fissure)) * frame.surface.w;
    base = mix(base, base * 0.37, crack * 0.24);
    roughness = mix(0.84, 0.24, frame.surface.x);
  } else if (matId == 2) {
    let cell = fract(w * 1.32);
    let grout = max(1.0 - smoothstep(0.025, 0.065, min(cell.x, cell.y)), max(1.0 - smoothstep(0.025, 0.065, 1.0 - cell.x), 1.0 - smoothstep(0.025, 0.065, 1.0 - cell.y)));
    let paver = 0.91 + 0.12 * noise2(floor(w * 1.32) + vec2<f32>(coarse * 3.0, 2.0));
    base *= mix(paver, 0.58, grout * frame.layers.w);
    base *= 0.9 + 0.1 * noise2(w * 8.0);
    roughness = 0.74;
  } else if (matId == 3) {
    let pigment = noise2(w * 1.8);
    base *= 0.94 + (pigment - 0.5) * 0.07;
    base *= 1.0 - frame.layers.x * (0.08 + 0.08 * noise2(w * 0.75));
    roughness = 0.72;
  } else if (matId == 4) {
    metalness = 0.72;
    roughness = 0.33;
    base *= 0.82 + 0.18 * micro;
  } else if (matId == 5) {
    metalness = 0.48;
    roughness = 0.24;
    let coat = pow(max(dot(reflect(-normalize(frame.sun.xyz), n), normalize(frame.camera.xyz - input.world)), 0.0), 32.0);
    base = base * 0.72 + vec3<f32>(coat * 0.46);
  } else if (matId == 6) {
    roughness = 0.1;
    metalness = 0.22;
    base *= 0.76 + 0.12 * noise2(input.uv * 8.0);
  } else if (matId == 7) {
    metalness = 0.84;
    roughness = 0.27;
    base *= 0.85 + micro * 0.2;
  } else if (matId == 8) {
    roughness = 0.92;
    base *= 0.58 + 0.2 * noise2(w * 6.0);
  } else if (matId == 9) {
    emissive = 1.1;
    roughness = 0.32;
  }

  let lightDir = normalize(frame.sun.xyz);
  let viewDir = normalize(frame.camera.xyz - input.world);
  let halfDir = normalize(lightDir + viewDir);
  let ndl = max(dot(n, lightDir), 0.0);
  let ndv = max(dot(n, viewDir), 0.04);
  let ndh = max(dot(n, halfDir), 0.0);
  let f0 = mix(0.04, 0.62, metalness);
  let fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(viewDir, halfDir), 0.0), 5.0);
  let specPower = mix(96.0, 5.5, roughness);
  let specular = fresnel * pow(ndh, specPower) * (0.28 + 0.72 * (1.0 - roughness));
  let ambient = vec3<f32>(0.34, 0.36, 0.34) * (0.72 + 0.28 * max(n.y, 0.0));
  var lit = base * (ambient + vec3<f32>(0.78, 0.73, 0.64) * ndl * 0.78) + vec3<f32>(specular);
  if (matId == 9) { lit += base * emissive; }
  let rainGlint = frame.surface.x * pow(ndh, 18.0) * 0.19;
  if (matId == 1) { lit += vec3<f32>(rainGlint); }
  let distanceFog = smoothstep(82.0, 146.0, input.fogDepth);
  let fog = vec3<f32>(0.49, 0.53, 0.51);
  lit = mix(lit, fog, distanceFog * 0.76);
  let mapped = toneMap(lit * 1.18);
  return vec4<f32>(pow(mapped, vec3<f32>(1.0 / 2.2)), input.color.a);
}
`;

function updateSurfaceUniforms() {
  const stackSpan = Math.max(1, state.layers.length - 1);
  const value = group => clamp(state.layers.reduce((sum, layer, index) => {
    if (!layer.enabled || layerGroup(layer) !== group) return sum;
    const stackInfluence = .62 + .76 * (index / stackSpan);
    return sum + layer.value * stackInfluence;
  }, 0), 0, 1);
  const base = state.layers.find(layer => layer.id === 'road-base');
  const allLayers = state.layers.reduce((sum, layer) => sum + (layer.enabled ? layer.value : 0), 0);
  return {
    surface: [state.wetness, value('aggregate'), value('weathering'), value('cracks')],
    layers: [value('lane-paint') * .5, 1, base?.enabled ? .78 : .96, value('paving')],
    allLayers,
  };
}
function setRendererPill(kind, detail = '') {
  const pill = $('#rendererPill');
  const span = $('span', pill);
  pill.classList.toggle('fallback', kind !== 'webgpu');
  if (kind === 'webgpu') {
    span.textContent = adapterInfo ? `WEBGPU · ${adapterInfo}` : 'WEBGPU / ACTIVE';
    pill.title = detail || 'Rendering with WebGPU';
  } else {
    span.textContent = 'VECTOR PREVIEW';
    pill.title = detail || 'WebGPU unavailable — vector compatibility view';
  }
}
async function initializeRenderer() {
  if (!('gpu' in navigator) || !navigator.gpu) {
    initializeFallback('This browser does not expose navigator.gpu.');
    return;
  }
  try {
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('No WebGPU adapter was returned.');
    try {
      const info = await adapter.requestAdapterInfo?.();
      adapterInfo = [info?.vendor, info?.architecture].filter(Boolean).join(' / ');
      if (adapterInfo.length > 23) adapterInfo = adapterInfo.slice(0, 20) + '…';
    } catch { adapterInfo = ''; }
    const device = await adapter.requestDevice({ label: 'RoadForge WebGPU device' });
    const shader = device.createShaderModule({ label: 'RoadForge procedural road shader', code: WGSL });
    const info = await shader.getCompilationInfo();
    const errors = info.messages.filter(message => message.type === 'error');
    if (errors.length) throw new Error(errors.map(message => message.message).join('\n'));
    gpuFormat = navigator.gpu.getPreferredCanvasFormat();
    renderPipeline = await device.createRenderPipelineAsync({
      label: 'RoadForge PBR mesh pipeline', layout: 'auto',
      vertex: {
        module: shader, entryPoint: 'vsMain',
        buffers: [{ arrayStride: 52, attributes: [
          { shaderLocation: 0, offset: 0, format: 'float32x3' },
          { shaderLocation: 1, offset: 12, format: 'float32x3' },
          { shaderLocation: 2, offset: 24, format: 'float32x2' },
          { shaderLocation: 3, offset: 32, format: 'float32x4' },
          { shaderLocation: 4, offset: 48, format: 'float32' },
        ] }],
      },
      fragment: { module: shader, entryPoint: 'fsMain', targets: [{ format: gpuFormat }] },
      primitive: { topology: 'triangle-list', cullMode: 'none', frontFace: 'ccw' },
      depthStencil: { format: 'depth24plus', depthWriteEnabled: true, depthCompare: 'less' },
    });
    const context = canvas.getContext('webgpu');
    if (!context) throw new Error('The WebGPU canvas context could not be created.');
    context.configure({ device, format: gpuFormat, alphaMode: 'opaque' });
    frameUniformBuffer = device.createBuffer({ label: 'RoadForge frame uniforms', size: 128, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    frameBindGroup = device.createBindGroup({ layout: renderPipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: frameUniformBuffer } }] });
    gpu = { adapter, device, context };
    setRendererPill('webgpu', `Adapter: ${adapterInfo || 'high-performance WebGPU'}`);
    $('#webgpuWarning').hidden = true;
    resizeCanvas();
    uploadStaticMesh();
    rebuildTrafficBuffer();
    device.lost.then(info => {
      if (gpu?.device === device) {
        console.warn('WebGPU device was lost:', info.message);
        initializeFallback('The WebGPU device was lost. Reload to reconnect the GPU.');
      }
    });
  } catch (error) {
    console.error('RoadForge WebGPU initialization failed:', error);
    initializeFallback(error?.message || 'WebGPU could not initialize.');
  }
}
function initializeFallback(reason) {
  gpu = null;
  setRendererPill('fallback', reason);
  $('#webgpuWarning').hidden = false;
  const warningText = $('#webgpuWarning span');
  if (warningText) warningText.textContent = `${reason} A navigable vector preview is available; WebGPU features require a secure Chromium browser with GPU access.`;
  try { fallbackContext = canvas.getContext('2d'); } catch { fallbackContext = null; }
  resizeCanvas();
}
function ensureDepthTexture() {
  if (!gpu || !canvas.width || !canvas.height) return;
  if (depthTexture) depthTexture.destroy();
  depthTexture = gpu.device.createTexture({ size: [canvas.width, canvas.height], format: 'depth24plus', usage: GPUTextureUsage.RENDER_ATTACHMENT });
}
function resizeCanvas() {
  const rect = canvas.getBoundingClientRect();
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  const width = Math.max(1, Math.floor(rect.width * dpr));
  const height = Math.max(1, Math.floor(rect.height * dpr));
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width; canvas.height = height;
    canvasWidth = rect.width || 1; canvasHeight = rect.height || 1;
    updateCameraMatrices();
    if (gpu) ensureDepthTexture();
  } else {
    canvasWidth = rect.width || 1; canvasHeight = rect.height || 1;
    updateCameraMatrices();
    if (gpu && !depthTexture) ensureDepthTexture();
  }
}
function uploadBuilder(builder, existing = null, label = 'RoadForge mesh') {
  if (!gpu) return null;
  if (!builder || !builder.vertices.length) {
    if (existing) existing.destroy();
    return null;
  }
  const data = new Float32Array(builder.vertices);
  if (!existing || existing.size < data.byteLength) {
    if (existing) existing.destroy();
    existing = gpu.device.createBuffer({ label, size: Math.max(4, Math.ceil(data.byteLength / 4) * 4), usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
  }
  gpu.device.queue.writeBuffer(existing, 0, data);
  return existing;
}
function uploadStaticMesh() {
  if (!gpu || !staticMesh) return;
  staticBuffer = uploadBuilder(staticMesh, staticBuffer, 'RoadForge static scene mesh');
  staticVertexCount = staticMesh.vertices.length / 13;
  updateMeshStats();
}
function rebuildTrafficBuffer() {
  trafficMesh = buildDynamicGeometry();
  trafficVertexCount = trafficMesh.vertices.length / 13;
  if (gpu) trafficBuffer = uploadBuilder(trafficMesh, trafficBuffer, 'RoadForge live traffic mesh');
}
function renderFrame(now = 0) {
  requestAnimationFrame(renderFrame);
  frameNumber++;
  const dt = lastFrame ? Math.min(.05, (now - lastFrame) / 1000) : .016;
  lastFrame = now;
  if (state.playing) {
    simulationSeconds += dt;
    trainClock += dt;
    advanceTraffic(dt);
  }
  if (geometryDirty && now >= geometryRebuildAfter) {
    geometryDirty = false;
    rebuildScene({ quiet: true, preserveTraffic: true });
  }
  if (gpu) renderWebGPU();
  else renderFallback();
  if (state.playing && now - trafficRefreshTime > 82) {
    trafficRefreshTime = now;
    rebuildTrafficBuffer();
  }
  if (now - (fpsTimes[0] || 0) > 850) {
    fpsTimes.push(now);
    if (fpsTimes.length > 20) fpsTimes.shift();
    const elapsed = now - (fpsTimes[0] || now);
    fpsValue = elapsed > 0 ? Math.round((fpsTimes.length - 1) * 1000 / elapsed) : 0;
    $('#gpuFrameRate').textContent = gpu ? `GPU ${fpsValue || '—'} FPS` : `PREVIEW ${fpsValue || '—'} FPS`;
    const clock = new Date(Date.UTC(2026, 0, 1, 8, 42, 16 + Math.floor(simulationSeconds)));
    $('#simClock').textContent = clock.toISOString().slice(11, 19);
  }
  if (frameNumber % 3 === 0) updateGizmo();
}
function renderWebGPU() {
  if (!gpu || !renderPipeline || !frameBindGroup || !canvas.width || !canvas.height) return;
  updateCameraMatrices();
  const frameData = new Float32Array(32);
  frameData.set(cameraMatrices.viewProjection, 0);
  const eye = cameraMatrices.eye || V(0, 60, 60);
  frameData.set([eye.x, eye.y, eye.z, 1], 16);
  frameData.set([-.46, .82, -.34, 0], 20);
  const surface = updateSurfaceUniforms();
  frameData.set([surface.surface[0], surface.surface[1], surface.surface[2], surface.surface[3]], 24);
  frameData.set([surface.layers[0], surface.layers[1], surface.layers[2], surface.layers[3]], 28);
  gpu.device.queue.writeBuffer(frameUniformBuffer, 0, frameData);
  const colorView = gpu.context.getCurrentTexture().createView();
  const encoder = gpu.device.createCommandEncoder({ label: 'RoadForge viewport frame' });
  const pass = encoder.beginRenderPass({
    colorAttachments: [{ view: colorView, clearValue: { r: .49, g: .53, b: .51, a: 1 }, loadOp: 'clear', storeOp: 'store' }],
    depthStencilAttachment: { view: depthTexture.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'store' },
  });
  pass.setPipeline(renderPipeline);
  pass.setBindGroup(0, frameBindGroup);
  if (staticBuffer && staticVertexCount) { pass.setVertexBuffer(0, staticBuffer); pass.draw(staticVertexCount); }
  if (trafficBuffer && trafficVertexCount) { pass.setVertexBuffer(0, trafficBuffer); pass.draw(trafficVertexCount); }
  pass.end();
  try {
    gpu.device.queue.submit([encoder.finish()]);
  } catch (error) {
    console.warn('WebGPU frame submission failed:', error);
  }
}

function drawFallback() {
  if (!fallbackContext || !canvasWidth || !canvasHeight) return;
  resizeCanvas();
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  const ctx = fallbackContext;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const w = canvasWidth, h = canvasHeight;
  const sky = ctx.createLinearGradient(0, 0, 0, h * .65);
  sky.addColorStop(0, '#77827e'); sky.addColorStop(.56, '#a0a297'); sky.addColorStop(1, '#8b8475');
  ctx.fillStyle = sky; ctx.fillRect(0, 0, w, h);
  const ground = ctx.createLinearGradient(0, h * .32, 0, h);
  ground.addColorStop(0, '#79786c'); ground.addColorStop(.45, '#77786c'); ground.addColorStop(1, '#56574f');
  ctx.fillStyle = ground; ctx.fillRect(0, h * .28, w, h * .72);
  if (!network) return;
  const projected = p => projectWorld(p);
  const drawPolygon = (points, fill, stroke = null, lineWidth = 1) => {
    const pts = points.map(projected).filter(Boolean);
    if (pts.length < 3) return;
    ctx.beginPath(); ctx.moveTo(pts[0].x, pts[0].y);
    for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x, pts[i].y);
    ctx.closePath(); ctx.fillStyle = fill; ctx.fill();
    if (stroke) { ctx.lineWidth = lineWidth; ctx.strokeStyle = stroke; ctx.stroke(); }
  };
  const random = seededRandom(network.seed ^ 0xc452);
  for (const block of network.blocks) {
    const x0 = block.x0 + (network.preset === 'urban' ? 2.1 : 0), x1 = block.x1 - (network.preset === 'urban' ? 2.1 : 0);
    const z0 = block.z0 + (network.preset === 'urban' ? 2.1 : 0), z1 = block.z1 - (network.preset === 'urban' ? 2.1 : 0);
    if (x1 <= x0 || z1 <= z0) continue;
    const fill = block.style % 4 === 2 ? '#5d6554' : block.style % 4 === 1 ? '#55564f' : '#858279';
    drawPolygon([V(x0, -.09, z0), V(x1, -.09, z0), V(x1, -.09, z1), V(x0, -.09, z1)], fill, '#aaa69a55', 1);
    if (block.style % 4 === 2) {
      for (let i = 0; i < 3; i++) {
        const p = projected(V(mix(x0, x1, .25 + i * .23), -.02, mix(z0, z1, .38 + random() * .22)));
        if (p) { ctx.beginPath(); ctx.arc(p.x, p.y, 3.2, 0, Math.PI * 2); ctx.fillStyle = '#617256'; ctx.fill(); }
      }
    }
  }
  const drawRoad = (path, width, fill, outline = '#b4aea0') => {
    if (!path || path.length < 2) return;
    const stride = Math.max(1, Math.floor(path.length / 45));
    const left = [], right = [];
    for (let i = 0; i < path.length; i += stride) {
      const center = path[i].p, tangent = tangentAtDistance(path, path[i].d), side = V(-tangent.z, 0, tangent.x);
      left.push(projected(add(center, mul(side, width * .5)))); right.push(projected(add(center, mul(side, -width * .5))));
    }
    const end = path[path.length - 1];
    if (path.length > 2 && (path.length - 1) % stride) {
      const tangent = tangentAtDistance(path, end.d), side = V(-tangent.z, 0, tangent.x);
      left.push(projected(add(end.p, mul(side, width * .5)))); right.push(projected(add(end.p, mul(side, -width * .5))));
    }
    const pts = [...left, ...right.reverse()].filter(Boolean);
    if (pts.length < 3) return;
    ctx.beginPath(); ctx.moveTo(pts[0].x, pts[0].y); for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x, pts[i].y); ctx.closePath();
    ctx.fillStyle = fill; ctx.fill(); ctx.strokeStyle = outline; ctx.lineWidth = 1.1; ctx.stroke();
    const centerPts = path.filter((_, i) => i % stride === 0).map(item => projected(item.p)).filter(Boolean);
    if (centerPts.length > 1) {
      ctx.beginPath(); ctx.moveTo(centerPts[0].x, centerPts[0].y); for (let i = 1; i < centerPts.length; i++) ctx.lineTo(centerPts[i].x, centerPts[i].y);
      ctx.strokeStyle = '#d0b27c'; ctx.lineWidth = 1; ctx.setLineDash([5, 6]); ctx.stroke(); ctx.setLineDash([]);
    }
  };
  if (state.features.rail) {
    const railZ = network.railZ ?? -18;
    drawPolygon([V(-50, -2.5, railZ - 3.8), V(50, -2.5, railZ - 3.8), V(50, -2.5, railZ + 3.8), V(-50, -2.5, railZ + 3.8)], '#373632', '#aaa89e88', 1);
  }
  const sortedEdges = [...network.edges].sort((a, b) => {
    const ca = (nodeMapFor(network).get(a.a).z + nodeMapFor(network).get(a.b).z) * .5;
    const cb = (nodeMapFor(network).get(b.a).z + nodeMapFor(network).get(b.b).z) * .5;
    return ca - cb;
  });
  if (state.features.network !== false) for (const edge of sortedEdges) {
    const data = compiled.edgePaths.get(edge.id);
    if (!data) continue;
    drawRoad(data.path, edgeWidth(edge) + (state.features.sidewalks ? 3.1 : 0), '#85837a', '#aaa79a');
    drawRoad(data.path, edgeWidth(edge), edge.kind === 'highway' ? '#363834' : '#30312f', '#5f605b');
  }
  if (state.features.bridge) {
    const path = makeViaductPath(network.preset === 'suburban' ? 20 : 22);
    drawRoad(path, 8.5, '#464843', '#acaaa1');
  }
  if (state.features.rail) {
    const railZ = network.railZ ?? -18;
    for (const z of [railZ - .72, railZ + .72]) {
      const a = projected(V(-49, -1.6, z)), b = projected(V(49, -1.6, z));
      if (a && b) { ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.strokeStyle = '#d0cec3'; ctx.lineWidth = 2; ctx.stroke(); }
    }
  }
  for (const { node, roadHull } of compiled.junctions.values()) {
    const p = projected(V(node.x, node.y + .07, node.z));
    if (!p) continue;
    if (state.features.markings && node.degree >= 3) {
      const radius = Math.max(5, Math.min(18, canvasWidth / 80));
      ctx.strokeStyle = '#d6d2c6'; ctx.lineWidth = 1.2; ctx.setLineDash([2, 2]);
      for (const arm of compiled.nodeArms.get(node.id) || []) {
        const c = projected(add(arm.center, mul(arm.tangent, 1.6)));
        const side = V(-arm.tangent.z, 0, arm.tangent.x);
        const l = projected(add(add(arm.center, mul(arm.tangent, 1.6)), mul(side, arm.width * .34)));
        const r = projected(add(add(arm.center, mul(arm.tangent, 1.6)), mul(side, -arm.width * .34)));
        if (l && r) {
          ctx.beginPath(); ctx.moveTo(l.x, l.y); ctx.lineTo(r.x, r.y); ctx.stroke();
        }
      }
      ctx.setLineDash([]);
    }
    ctx.beginPath(); ctx.arc(p.x, p.y, state.selected.id === node.id ? 6 : 3.2, 0, Math.PI * 2);
    ctx.fillStyle = state.selected.id === node.id ? '#f2c498' : '#c6ad8d'; ctx.fill();
    ctx.strokeStyle = '#27221d'; ctx.lineWidth = 1.5; ctx.stroke();
  }
  if (state.features.traffic) for (const agent of trafficAgents.slice(0, 25)) {
    const edge = network.edges.find(item => item.id === agent.edgeId), data = edge && compiled.edgePaths.get(edge.id);
    if (!data) continue;
    const s = agent.direction > 0 ? agent.s : data.length - agent.s;
    const p = samplePath(data.path, s), q = projected(V(p.x, p.y + .45, p.z));
    if (q) {
      ctx.beginPath(); ctx.ellipse(q.x, q.y, 5, 2.5, 0, 0, Math.PI * 2); ctx.fillStyle = agent.color; ctx.fill();
      ctx.strokeStyle = '#d8d0bf'; ctx.lineWidth = .7; ctx.stroke();
    }
  }
  const overlay = ctx.createLinearGradient(0, h * .62, 0, h);
  overlay.addColorStop(0, '#16181600'); overlay.addColorStop(1, '#191a1870');
  ctx.fillStyle = overlay; ctx.fillRect(0, h * .62, w, h * .38);
}

function rebuildScene(options = {}) {
  if (!network) return;
  const quiet = !!options.quiet;
  if (!quiet) {
    const loading = $('#loadingState');
    loading.classList.remove('done');
    $('#toolInstruction').classList.remove('visible');
  }
  setTimeout(() => {
    staticMesh = buildAllGeometry(network);
    if (!options.preserveTraffic) initializeTraffic();
    else {
      for (const agent of trafficAgents) {
        const lengthNow = compiled.edgePaths.get(agent.edgeId)?.length;
        if (!lengthNow) {
          const replacement = network.edges.find(edge => compiled.edgePaths.get(edge.id)?.length > 5);
          if (replacement) { agent.edgeId = replacement.id; agent.direction = 1; agent.s = 0; }
        } else agent.s = clamp(agent.s, 0, lengthNow);
      }
    }
    if (gpu) uploadStaticMesh();
    rebuildTrafficBuffer();
    updateOutliner(); updateInspector(); updateMiniMap(); updateSceneLabels(); updateGizmo(); updateMeshStats();
    if (!quiet) setTimeout(() => $('#loadingState').classList.add('done'), 200);
  }, quiet ? 0 : 25);
}
function updateMeshStats() {
  const quads = staticMesh?.quads || 0;
  const verts = Math.floor((staticMesh?.vertices?.length || 0) / 13);
  $('#meshStats').textContent = `${formatCount(verts)} verts / ${formatCount(quads)} quads`;
  $('#exportMeshInfo').textContent = `${formatCount(quads)} quads · triangulated for export`;
  $('#projectSize').textContent = `${network?.nodes?.length || 0} NODES`;
}
function formatCount(n) { return Number(n).toLocaleString('en-US'); }
function updateVehicleCount() {
  state.trafficCount = clamp(Math.floor(state.trafficCount), 0, 25);
  $('#vehicleCountValue').textContent = String(state.trafficCount);
  $('#vehicleCountRange').value = String(state.trafficCount);
  $('#objectCount').textContent = String(6 + state.trafficCount).padStart(2, '0');
}

const treeOpen = new Set(['junctions']);
function treeItem({ id, label, type = '', icon: iconName = 'cube', selectionType = 'group', selectionId = id, feature = null, depth = 0, children = null, open = false }) {
  const hasChildren = Array.isArray(children) && children.length > 0;
  const selected = state.selected.id === selectionId && state.selected.type === selectionType;
  const hidden = feature ? state.features[feature] === false : false;
  const left = hasChildren
    ? `<span class="tree-disclosure ${treeOpen.has(id) || open ? 'open' : ''}">${icon('chevron')}</span>`
    : '<span class="tree-spacer"></span>';
  return `<div class="tree-group" data-tree-group="${id}">
    <div class="tree-row ${selected ? 'selected' : ''} ${hidden ? 'hidden-row' : ''}" data-select-type="${selectionType}" data-select-id="${selectionId}" data-label="${label.toLowerCase()}" style="padding-left:${5 + depth * 8}px">
      ${left}<span class="tree-icon">${icon(iconName)}</span><span class="tree-label">${label}</span><span class="tree-type">${type}</span>
      ${feature ? `<button class="tree-visibility" data-feature-toggle="${feature}" title="Toggle ${label}">${icon(hidden ? 'eyeoff' : 'eye')}</button>` : ''}
    </div>
    ${hasChildren ? `<div class="tree-children" ${treeOpen.has(id) || open ? '' : 'hidden'}>${children.map(child => treeItem({ ...child, depth: depth + 1 })).join('')}</div>` : ''}
  </div>`;
}
function outlinerModel() {
  const coreEdges = network.edges;
  const centralNode = [...network.nodes].filter(node => node.degree >= 3).sort((a, b) => (Math.abs(a.x) + Math.abs(a.z)) - (Math.abs(b.x) + Math.abs(b.z)))[0];
  const showRoads = coreEdges.slice(0, Math.min(5, coreEdges.length)).map((edge, i) => ({
    id: `edge-${edge.id}`, label: edge.name === 'LOCAL STREET' ? `${edge.kind === 'arterial' ? 'Harbor Avenue' : 'Local street'} ${String(i + 1).padStart(2, '0')}` : edge.name,
    type: `${edge.lanes} lanes`, icon: 'roadmark', selectionType: 'edge', selectionId: edge.id,
  }));
  const nodes = network.nodes.filter(node => node.degree >= 3).slice(0, 5).map(node => ({
    id: `node-${node.id}`, label: node.name, type: `${node.degree} legs`, icon: 'intersection', selectionType: 'node', selectionId: node.id,
  }));
  const groups = [
    { id: 'network', label: 'Road network', type: `${network.edges.length} seg`, icon: 'roadmark', selectionType: 'group', selectionId: 'roads', feature: 'network', children: showRoads },
    { id: 'junctions', label: 'Intersections', type: `${network.nodes.filter(n => n.degree >= 3).length}`, icon: 'intersection', selectionType: 'group', selectionId: 'junctions', open: true, children: nodes },
    { id: 'blocks', label: 'City blocks · no buildings', type: `${network.blocks.length} lots`, icon: 'grid', selectionType: 'group', selectionId: 'blocks', feature: 'blocks' },
    { id: 'streetscape', label: 'Streetscape & safety', type: 'DETAILS', icon: 'bridge', selectionType: 'group', selectionId: 'streetscape', feature: 'streetscape', children: [
      { id: 'service-robots', label: 'Autonomous delivery robots', type: '3 UNITS', icon: 'robot', selectionType: 'group', selectionId: 'robots', feature: 'robots' },
      { id: 'streetlights', label: 'Warm LED streetlights', type: 'POLES', icon: 'scene', selectionType: 'group', selectionId: 'streetlights', feature: 'streetlights' },
      { id: 'parking', label: 'Parking pockets & signs', type: 'KERBSIDE', icon: 'car', selectionType: 'group', selectionId: 'parking', feature: 'parking' },
      { id: 'bollards', label: 'Safety bollards', type: 'REFLECTIVE', icon: 'object', selectionType: 'group', selectionId: 'bollards', feature: 'bollards' },
      { id: 'billboards', label: 'Wayfinding panels', type: 'SIGNS', icon: 'object', selectionType: 'group', selectionId: 'billboards', feature: 'billboards' },
    ] },
    { id: 'wear', label: 'Road wear / work zone', type: 'PBR', icon: 'wrench', selectionType: 'group', selectionId: 'wear', feature: 'damage' },
    { id: 'transit', label: 'Rail underpass', type: 'CUT / 02', icon: 'train', selectionType: 'group', selectionId: 'rail', feature: 'rail', children: [
      { id: 'rail-bed', label: 'Ballast & double track', type: '2 rails', icon: 'train', selectionType: 'group', selectionId: 'rail' },
      { id: 'train', label: 'Regional train · AI', type: '4 cars', icon: 'train', selectionType: 'group', selectionId: 'train', feature: 'train' },
    ] },
    { id: 'viaduct', label: 'East Loop viaduct', type: 'BRIDGE', icon: 'bridge', selectionType: 'group', selectionId: 'bridge', feature: 'bridge' },
    { id: 'traffic', label: 'Traffic simulation', type: `${state.trafficCount} / 25`, icon: 'car', selectionType: 'group', selectionId: 'traffic', feature: 'traffic' },
  ];
  return groups;
}
function updateOutliner() {
  const tree = $('#outlinerTree');
  if (!network) return;
  const query = ($('#outlinerSearch').value || '').trim().toLowerCase();
  const items = outlinerModel().map(item => {
    if (query) treeOpen.add(item.id);
    return treeItem(item);
  });
  tree.innerHTML = items.join('');
  $('#objectCount').textContent = String(8 + network.edges.length + network.nodes.filter(node => node.degree >= 3).length).padStart(2, '0');
  for (const row of $$('.tree-row', tree)) {
    const matches = !query || row.dataset.label.includes(query);
    if (!matches) row.style.display = 'none';
  }
  for (const group of $$('.tree-group', tree)) {
    const row = $('.tree-row', group);
    const children = $('.tree-children', group);
    if (children && query && [...$$('.tree-row', children)].some(child => child.style.display !== 'none')) {
      children.hidden = false;
    }
    if (row && query && row.style.display === 'none' && children && [...$$('.tree-row', children)].some(child => child.style.display !== 'none')) row.style.display = 'flex';
  }
}
function selectionObject() {
  if (!state.selected?.id) return null;
  if (state.selected.type === 'node') return network.nodes.find(item => item.id === state.selected.id) || null;
  if (state.selected.type === 'edge') return network.edges.find(item => item.id === state.selected.id) || null;
  return null;
}
function updateInspector() {
  if (!network) return;
  const content = $('#inspectorContent');
  const selected = selectionObject();
  $('#selectionReadout').textContent = selected ? `${state.selected.type === 'node' ? 'JUNCTION' : 'ROAD'} / ${selected.name}` : state.selected.id ? String(state.selected.id).toUpperCase() : 'No selection';
  if (state.selected.type === 'node' && selected) {
    const node = selected;
    const arms = compiled.nodeArms.get(node.id) || [];
    const width = arms.length ? Math.max(...arms.map(arm => arm.width)) : 0;
    content.innerHTML = `
      <div class="inspect-object-title"><span class="inspect-object-icon">${icon('intersection')}</span><span class="inspect-object-copy"><b>${escapeHTML(node.name)}</b><small>MERGED NODE / ${node.id.toUpperCase()}</small></span><span class="inspect-state">LIVE</span></div>
      <div class="inspect-grid">
        <div class="inspect-field"><label>World X · metres</label><input type="number" step="0.1" value="${node.x.toFixed(1)}" data-node-axis="x" /></div>
        <div class="inspect-field"><label>World Z · metres</label><input type="number" step="0.1" value="${node.z.toFixed(1)}" data-node-axis="z" /></div>
        <div class="inspect-field"><label>Connected arms</label><span class="field-value">${String(node.degree).padStart(2, '0')} / ${arms.length} spline ends</span></div>
        <div class="inspect-field"><label>Max carriageway</label><span class="field-value">${width.toFixed(1)} m</span></div>
      </div>
      <div class="inspect-field full" style="margin-top:5px"><label>Corner radius <span data-radius-label>${Number(node.radius).toFixed(1)} m</span></label><div class="inspect-range-row"><input type="range" min="0.8" max="5.5" step="0.1" value="${node.radius}" data-node-radius /><output data-radius-output>${Number(node.radius).toFixed(1)} m</output></div></div>
      <div class="inspect-checkline"><span>Signal phase aware</span><button class="switch ${node.signalized ? 'on' : ''}" data-node-signal aria-label="Toggle signalized junction"><i></i></button></div>
      <div class="inspect-note">${icon('mesh')}<span><b>Topology resolved</b> · swept quad strips meet one shared patch; move gizmo to translate all ${node.degree} arms.</span></div>`;
    wireNodeInspector(node);
  } else if (state.selected.type === 'edge' && selected) {
    const edge = selected;
    const data = compiled.edgePaths.get(edge.id);
    content.innerHTML = `
      <div class="inspect-object-title"><span class="inspect-object-icon">${icon('roadmark')}</span><span class="inspect-object-copy"><b>${escapeHTML(edge.name)}</b><small>${edge.kind.toUpperCase()} / ${edge.id.toUpperCase()}</small></span><span class="inspect-state">LIVE</span></div>
      <div class="inspect-grid">
        <div class="inspect-field"><label>Road class</label><select data-edge-kind><option value="local" ${edge.kind === 'local' ? 'selected' : ''}>Local street</option><option value="arterial" ${edge.kind === 'arterial' ? 'selected' : ''}>Arterial</option><option value="highway" ${edge.kind === 'highway' ? 'selected' : ''}>Highway</option><option value="ramp" ${edge.kind === 'ramp' ? 'selected' : ''}>Ramp / connector</option></select></div>
        <div class="inspect-field"><label>Design speed</label><select data-edge-speed><option value="30" ${edge.speed === 30 ? 'selected' : ''}>30 km/h</option><option value="50" ${edge.speed === 50 ? 'selected' : ''}>50 km/h</option><option value="70" ${edge.speed === 70 ? 'selected' : ''}>70 km/h</option><option value="100" ${edge.speed === 100 ? 'selected' : ''}>100 km/h</option></select></div>
      </div>
      <div class="inspect-field full" style="margin-top:5px"><label>Carriageway width <span data-width-label>${edgeWidth(edge).toFixed(1)} m</span></label><div class="inspect-range-row"><input type="range" min="5.8" max="24" step="0.1" value="${edgeWidth(edge)}" data-edge-width /><output data-width-output>${edgeWidth(edge).toFixed(1)} m</output></div></div>
      <div class="inspect-grid" style="margin-top:5px">
        <div class="inspect-field"><label>Lane count</label><select data-edge-lanes>${[2,3,4,6,8].map(n => `<option value="${n}" ${edge.lanes === n ? 'selected' : ''}>${n} lanes</option>`).join('')}</select></div>
        <div class="inspect-field"><label>Spline length</label><span class="field-value">${(data?.length || 0).toFixed(1)} m</span></div>
      </div>
      <div class="inspect-note">${icon('spline')}<span><b>Curve-based road</b> · ${Math.max(2, Math.round((data?.length || 0) / .85))} quad sections · tangent continuity preserved.</span></div>`;
    wireEdgeInspector(edge);
  } else {
    const label = state.selected.id ? state.selected.id.replaceAll('-', ' ') : 'Road network';
    const summary = state.selected.id === 'traffic'
      ? `AI route agents are limited to ${state.trafficCount} of 25. Each vehicle follows a lane spline and hands off at graph nodes.`
      : state.selected.id === 'rail'
        ? 'Ballasted twin track drops below grade at a concrete portal. A regional train loops through the cut.'
        : state.selected.id === 'bridge'
          ? 'The East Loop is a crowned road deck with tapered approaches, box girder, piers and continuous safety rails.'
          : state.selected.id === 'blocks'
            ? 'Street parcels generate paving, parking courts and planted medians. No building geometry is created.'
            : 'Select a road or junction in the viewport to inspect and edit its real-world dimensions.';
    content.innerHTML = `<div class="inspect-object-title"><span class="inspect-object-icon">${icon(ICON[state.selected.id] || 'grid')}</span><span class="inspect-object-copy"><b>${escapeHTML(label.replace(/\b\w/g, c => c.toUpperCase()))}</b><small>SCENE COMPONENT / ${network.preset.toUpperCase()}</small></span><span class="inspect-state">ACTIVE</span></div><div class="inspect-empty">${icon('mesh')}<span>${escapeHTML(summary)}</span></div><div class="inspect-checkline"><span>Topology validation</span><span class="inspect-state">${network.nodes.length} NODES · ${network.edges.length} EDGES</span></div>`;
  }
  updateSceneLabels(); updateGizmo();
}
function wireNodeInspector(node) {
  const before = JSON.stringify(network);
  for (const input of $$('[data-node-axis]', $('#inspectorContent'))) {
    input.addEventListener('change', () => {
      const axis = input.dataset.nodeAxis;
      const value = Number(input.value);
      if (!Number.isFinite(value)) return;
      saveSnapshot();
      node[axis] = value;
      rebuildScene({ preserveTraffic: true });
      markSaved(false);
    });
  }
  const radius = $('[data-node-radius]', $('#inspectorContent'));
  if (radius) {
    radius.addEventListener('pointerdown', () => { editorDrag = { history: JSON.stringify(network), nodeId: node.id, kind: 'radius' }; });
    radius.addEventListener('input', () => {
      node.radius = Number(radius.value);
      $('[data-radius-output]', $('#inspectorContent')).textContent = `${node.radius.toFixed(1)} m`;
      geometryDirty = true; geometryRebuildAfter = performance.now() + 90;
      markSaved(false);
    });
    radius.addEventListener('change', () => {
      if (editorDrag?.history) commitHistory(editorDrag.history);
      editorDrag = null;
      rebuildScene({ quiet: true, preserveTraffic: true });
    });
  }
  const signal = $('[data-node-signal]', $('#inspectorContent'));
  signal?.addEventListener('click', () => {
    saveSnapshot(); node.signalized = !node.signalized; updateInspector(); rebuildScene({ quiet: true, preserveTraffic: true }); markSaved(false);
  });
}
function wireEdgeInspector(edge) {
  const updateEdge = () => {
    edge.width = Math.max(5.8, Number(edge.width));
    const data = compiled.edgePaths.get(edge.id);
    updateInspector();
    rebuildScene({ quiet: true, preserveTraffic: true });
    markSaved(false);
    if (data) updateMeshStats();
  };
  const width = $('[data-edge-width]', $('#inspectorContent'));
  width?.addEventListener('pointerdown', () => { editorDrag = { history: JSON.stringify(network), edgeId: edge.id, kind: 'width' }; });
  width?.addEventListener('input', () => {
    edge.width = Number(width.value);
    $('[data-width-output]', $('#inspectorContent')).textContent = `${edge.width.toFixed(1)} m`;
    geometryDirty = true; geometryRebuildAfter = performance.now() + 90;
    markSaved(false);
  });
  width?.addEventListener('change', () => {
    if (editorDrag?.history) commitHistory(editorDrag.history);
    editorDrag = null; updateEdge();
  });
  const lanes = $('[data-edge-lanes]', $('#inspectorContent'));
  lanes?.addEventListener('change', () => {
    saveSnapshot(); edge.lanes = Number(lanes.value); edge.width = edge.lanes * edge.laneWidth + edge.shoulder * 2; updateEdge();
  });
  const kind = $('[data-edge-kind]', $('#inspectorContent'));
  kind?.addEventListener('change', () => {
    saveSnapshot(); edge.kind = kind.value;
    edge.name = kind.value === 'highway' ? 'NORTH QUAY EXPRESS' : kind.value === 'arterial' ? 'HARBOR AVENUE' : kind.value === 'ramp' ? 'GRADE-SEPARATED RAMP' : 'LOCAL STREET';
    edge.lanes = kind.value === 'highway' ? 6 : kind.value === 'arterial' ? 4 : 2;
    edge.width = edge.lanes * edge.laneWidth + edge.shoulder * 2;
    edge.speed = kind.value === 'highway' ? 100 : kind.value === 'arterial' ? 50 : kind.value === 'ramp' ? 45 : 30;
    updateEdge();
  });
  const speed = $('[data-edge-speed]', $('#inspectorContent'));
  speed?.addEventListener('change', () => { saveSnapshot(); edge.speed = Number(speed.value); updateEdge(); });
}
function escapeHTML(text) {
  return String(text).replace(/[&<>"']/g, character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]);
}
function updateSceneLabels() {
  if (!network || !cameraMatrices.viewProjection) return;
  const labels = [];
  const selected = selectionObject();
  if (state.selected.type === 'node' && selected) {
    const point = projectWorld(V(selected.x, selected.y + .35, selected.z));
    if (point?.visible) labels.push({ p: point, text: `JUNCTION ${String(selected.id).slice(-2)} / ${selected.degree} LEGS`, color: 'junction' });
  }
  if (state.features.rail && network.railZ !== undefined) {
    const point = projectWorld(V(-8.6, .75, network.railZ));
    if (point?.visible) labels.push({ p: point, text: 'RAIL UNDERPASS / 2.1 M CLEARANCE', color: 'rail' });
  }
  $('#sceneLabels').innerHTML = labels.map(label => `<span class="scene-label ${label.color}" style="left:${label.p.x + 16}px;top:${label.p.y - 22}px"><i></i>${escapeHTML(label.text)}</span>`).join('');
}
function updateGizmo() {
  const layer = $('#gizmoLayer');
  const node = state.selected.type === 'node' ? selectionObject() : null;
  if (!node || !layer) { if (layer?.firstChild) layer.innerHTML = ''; return; }
  const position = projectWorld(V(node.x, node.y + .16, node.z));
  if (!position?.visible) { if (layer.firstChild) layer.innerHTML = ''; return; }
  let gizmo = $('.intersection-gizmo', layer);
  if (!gizmo || gizmo.dataset.node !== node.id) {
    layer.innerHTML = `<div class="intersection-gizmo" data-node="${node.id}"><button class="gizmo-cross" title="Drag to move this intersection and all connected road splines">${icon('move')}<span class="gizmo-label">MOVE / ${node.degree} ARMS</span></button></div>`;
    gizmo = $('.intersection-gizmo', layer);
    const handle = $('.gizmo-cross', gizmo);
    handle.addEventListener('pointerdown', event => {
      event.preventDefault(); event.stopPropagation();
      const item = network.nodes.find(n => n.id === gizmo.dataset.node);
      if (!item) return;
      editorDrag = { kind: 'node-move', nodeId: item.id, history: JSON.stringify(network), moved: false };
      handle.setPointerCapture(event.pointerId);
      handle.classList.add('dragging');
    });
    handle.addEventListener('pointermove', event => {
      if (!editorDrag || editorDrag.kind !== 'node-move' || editorDrag.nodeId !== gizmo.dataset.node) return;
      const p = rayOnGround(event.clientX, event.clientY, network.nodes.find(n => n.id === editorDrag.nodeId)?.y || 0);
      const item = network.nodes.find(n => n.id === editorDrag.nodeId);
      if (!p || !item) return;
      const nextX = Math.round(p.x * 10) / 10, nextZ = Math.round(p.z * 10) / 10;
      if (Math.abs(item.x - nextX) < .01 && Math.abs(item.z - nextZ) < .01) return;
      item.x = nextX; item.z = nextZ; editorDrag.moved = true;
      $('#coordinateReadout').innerHTML = `X ${item.x.toFixed(2)} <b>m</b> <span>·</span> Z ${item.z.toFixed(2)} <b>m</b>`;
      geometryDirty = true; geometryRebuildAfter = performance.now() + 100;
      updateMiniMap(); updateInspector(); markSaved(false);
    });
    const finish = () => {
      if (editorDrag?.kind === 'node-move') {
        const moved = editorDrag.moved;
        if (moved) commitHistory(editorDrag.history);
        editorDrag = null;
        handle.classList.remove('dragging');
        if (moved) {
          rebuildScene({ quiet: true, preserveTraffic: true });
          toast('Intersection moved', 'All connected spline ends were rebuilt as one junction patch.');
        }
      }
    };
    handle.addEventListener('pointerup', finish);
    handle.addEventListener('pointercancel', finish);
  }
  gizmo.style.left = `${position.x}px`;
  gizmo.style.top = `${position.y}px`;
  const label = $('.gizmo-label', gizmo);
  if (label) label.textContent = `MOVE / ${node.degree} ARMS`;
}
function updateMiniMap() {
  if (!network) return;
  const svg = $('#miniMap');
  const bounds = boundsForNetwork(network, compiled.edgePaths);
  const margin = 12, viewW = 160, viewH = 104;
  const spanX = Math.max(1, bounds.maxX - bounds.minX), spanZ = Math.max(1, bounds.maxZ - bounds.minZ);
  const scale = Math.min((viewW - margin * 2) / spanX, (viewH - margin * 2) / spanZ);
  const xOffset = (viewW - spanX * scale) / 2, yOffset = (viewH - spanZ * scale) / 2;
  const point = p => [xOffset + (p.x - bounds.minX) * scale, yOffset + (p.z - bounds.minZ) * scale];
  const mapPaths = network.edges.map(edge => {
    const path = compiled.edgePaths.get(edge.id)?.path;
    if (!path) return '';
    const coords = path.filter((_, i) => i % Math.max(1, Math.floor(path.length / 24)) === 0).map(item => point(item.p));
    if (coords.length < 2) return '';
    const d = coords.map((p, i) => `${i ? 'L' : 'M'}${p[0].toFixed(2)},${p[1].toFixed(2)}`).join(' ');
    const cls = edge.kind === 'highway' ? 'highway' : edge.kind === 'arterial' ? 'major' : '';
    return `<path d="${d}" class="map-road ${cls}"/><path d="${d}" class="map-center"/>`;
  }).join('');
  const nodes = network.nodes.filter(n => n.degree >= 2).map(node => {
    const [x, y] = point(node);
    return `<circle class="map-node ${state.selected.id === node.id ? 'selected' : ''}" data-map-node="${node.id}" cx="${x}" cy="${y}" r="${state.selected.id === node.id ? 3.1 : 1.65}"/>`;
  }).join('');
  svg.innerHTML = `<rect x="0" y="0" width="160" height="104" fill="transparent"/>${mapPaths}${nodes}`;
  $('#miniMapNodeCount').textContent = `${network.nodes.length} nodes`;
}
function selectObject(type, id) {
  state.selected = { type, id };
  updateOutliner(); updateInspector(); updateMiniMap(); updateGizmo(); updateSceneLabels();
}
function nearestSelection(point) {
  let nearestNode = null, nearestNodeDistance = Infinity;
  for (const node of network.nodes) {
    const d = Math.hypot(point.x - node.x, point.z - node.z);
    if (d < nearestNodeDistance) { nearestNodeDistance = d; nearestNode = node; }
  }
  if (nearestNode && nearestNodeDistance < Math.max(2.6, nearestNode.radius + 1.0)) return { type: 'node', item: nearestNode, distance: nearestNodeDistance };
  let nearestEdge = null, nearestDistance = Infinity;
  for (const edge of network.edges) {
    const path = compiled.edgePaths.get(edge.id)?.path;
    if (!path) continue;
    for (let i = 0; i < path.length; i += 2) {
      const p = path[i].p;
      const d = Math.hypot(point.x - p.x, point.z - p.z);
      if (d < nearestDistance) { nearestDistance = d; nearestEdge = edge; }
    }
  }
  if (nearestEdge && nearestDistance < Math.max(4.3, edgeWidth(nearestEdge) * .64)) return { type: 'edge', item: nearestEdge, distance: nearestDistance };
  return null;
}
function addNodeAt(point, name = 'New intersection') {
  const id = `node-${String(network.nextNode++).padStart(3, '0')}`;
  const node = { id, x: point.x, y: 0, z: point.z, name, kind: 'intersection', radius: 2.1, signalized: false };
  network.nodes.push(node);
  return node;
}
function addRoadBetween(start, end) {
  if (start.id === end.id) return null;
  const id = `road-${String(network.nextEdge++).padStart(3, '0')}`;
  const localWidth = state.preset === 'highway' ? 7.5 : 7.6;
  const edge = { id, a: start.id, b: end.id, kind: 'local', name: 'NEW STREET', lanes: 2, laneWidth: 3.3, shoulder: .5, width: localWidth, speed: 30, bend: 0, cornerRadius: 2.1, oneWay: false, surface: 'asphalt' };
  network.edges.push(edge);
  return edge;
}
function splitEdgeAt(point, hit) {
  const old = hit?.item;
  if (!old) return null;
  const original = JSON.parse(JSON.stringify(old));
  const a = network.nodes.find(node => node.id === original.a), b = network.nodes.find(node => node.id === original.b);
  const newNode = addNodeAt(point, `Junction ${String(network.nextNode - 1).padStart(2, '0')}`);
  const originalIndex = network.edges.findIndex(edge => edge.id === old.id);
  network.edges.splice(originalIndex, 1);
  const edge1 = { ...original, id: `road-${String(network.nextEdge++).padStart(3, '0')}`, b: newNode.id, bend: original.bend * .5, name: original.name };
  const edge2 = { ...original, id: `road-${String(network.nextEdge++).padStart(3, '0')}`, a: newNode.id, bend: original.bend * .5, name: original.name };
  network.edges.splice(originalIndex, 0, edge1, edge2);
  return newNode;
}
function handleViewportClick(event) {
  if (!network) return;
  const point = rayOnGround(event.clientX, event.clientY, 0);
  if (!point) return;
  $('#coordinateReadout').innerHTML = `X ${point.x.toFixed(2)} <b>m</b> <span>·</span> Z ${point.z.toFixed(2)} <b>m</b>`;
  if (state.tool === 'measure') {
    if (!state.measureStart) { state.measureStart = point; setToolInstruction('Click a second point to measure distance', 'Esc'); }
    else {
      const distance = Math.hypot(point.x - state.measureStart.x, point.z - state.measureStart.z);
      toast('Distance measured', `${distance.toFixed(2)} m · plan distance`);
      state.measureStart = null; setToolInstruction('Click a road or intersection to inspect', 'Q');
    }
    return;
  }
  if (state.tool === 'road') {
    let snap = nearestSelection(point);
    let startNode;
    if (snap?.type === 'node') startNode = snap.item;
    else startNode = addNodeAt(V(point.x, 0, point.z), 'Road end / A');
    if (!state.drawStart) {
      state.drawStart = startNode.id;
      setToolInstruction('Click the road endpoint · curves snap to junctions', 'Esc');
      toast('Road started', 'Click another point to set the curve endpoint.');
      updateMiniMap();
      return;
    }
    const from = network.nodes.find(node => node.id === state.drawStart);
    let endNode = startNode;
    if (from?.id === startNode.id) endNode = addNodeAt(V(point.x + 4, 0, point.z + 2), 'Road end / B');
    if (from && endNode) {
      const before = JSON.stringify(network);
      const edge = addRoadBetween(from, endNode);
      if (edge) {
        commitHistory(before); state.drawStart = null; state.selected = { type: 'edge', id: edge.id };
        rebuildScene(); markSaved(false); setToolInstruction('Click a road or intersection to inspect', 'Q');
        toast('Road generated', 'A quad ribbon and connected junction patch were added to the graph.');
      }
    }
    return;
  }
  if (state.tool === 'junction') {
    const hit = nearestSelection(point);
    if (!hit || hit.type !== 'edge') { toast('No road under cursor', 'Click near a road centerline to insert a junction.'); return; }
    const before = JSON.stringify(network);
    const node = splitEdgeAt(point, hit);
    if (node) {
      commitHistory(before); state.selected = { type: 'node', id: node.id }; rebuildScene(); markSaved(false);
      toast('Junction inserted', 'The source road was split into two connected splines.');
    }
    return;
  }
  if (state.tool === 'move') {
    const hit = nearestSelection(point);
    if (hit?.type === 'node') selectObject('node', hit.item.id);
    else if (hit?.type === 'edge') selectObject('edge', hit.item.id);
    return;
  }
  const hit = nearestSelection(point);
  if (hit) selectObject(hit.type, hit.item.id);
  else {
    state.selected = { type: 'group', id: null };
    updateInspector(); updateOutliner(); updateGizmo(); updateMiniMap();
  }
}
function setTool(tool) {
  state.tool = tool;
  $$('.tool-button[data-tool]').forEach(button => button.classList.toggle('active', button.dataset.tool === tool));
  const descriptions = {
    select: ['Click a road or intersection to inspect', 'Q'],
    move: ['Select a junction, then drag its gizmo to move every connected arm', 'W'],
    road: ['Click a start point, then an endpoint to draw a curved road', 'R'],
    junction: ['Click a road centerline to split it and add a shared junction', 'J'],
    measure: ['Click two points to measure their plan distance', 'M'],
  };
  setToolInstruction(...(descriptions[tool] || descriptions.select));
  if (tool !== 'road') state.drawStart = null;
}
function setToolInstruction(text, key = '') {
  const el = $('#toolInstruction');
  el.innerHTML = `<span class="instruction-dot"></span><span>${escapeHTML(text)}</span>${key ? `<kbd>${escapeHTML(key)}</kbd>` : ''}`;
  el.classList.add('visible');
  clearTimeout(setToolInstruction.timer);
  if (state.tool === 'select') setToolInstruction.timer = setTimeout(() => el.classList.remove('visible'), 2600);
}

function layerGroup(layer) {
  if (layer.group) return layer.group;
  if (layer.id.startsWith('aggregate')) return 'aggregate';
  if (layer.id.startsWith('weather')) return 'weathering';
  if (layer.id.startsWith('crack')) return 'cracks';
  if (layer.id.startsWith('paint') || layer.id.startsWith('lane')) return 'lane-paint';
  if (layer.id.startsWith('pav')) return 'paving';
  return layer.id;
}
function renderLayerStack() {
  const list = $('#layerList');
  list.innerHTML = state.layers.map(layer => `<div class="layer-row ${state.selectedLayer === layer.id ? 'selected' : ''} ${layer.enabled ? '' : 'disabled'}" data-layer="${layer.id}" draggable="true" title="Drag to change layer order">
    <span class="drag-grip" aria-hidden="true"></span><button class="layer-eye" data-layer-toggle="${layer.id}" title="Toggle layer">${icon(layer.enabled ? 'eye' : 'eyeoff')}</button>
    <span class="layer-swatch">${icon(layer.id.includes('paint') ? 'roadmark' : layer.id.includes('crack') ? 'wrench' : layer.id.includes('pav') ? 'grid' : 'layers')}</span>
    <span class="layer-copy"><b>${escapeHTML(layer.name)}</b><small>${escapeHTML(layer.detail || 'PROCEDURAL / 2K')}</small></span><span class="layer-value">${Math.round(layer.value * 100)}%</span><span class="layer-chevron">${icon('chevron')}</span>
  </div>`).join('');
  renderLayerProperties();
  list.querySelectorAll('.layer-row').forEach(row => {
    row.addEventListener('click', event => {
      if (event.target.closest('.layer-eye')) return;
      state.selectedLayer = row.dataset.layer;
      renderLayerStack();
    });
    row.addEventListener('dragstart', event => {
      row.classList.add('dragging');
      event.dataTransfer.effectAllowed = 'move';
      event.dataTransfer.setData('text/plain', row.dataset.layer);
    });
    row.addEventListener('dragend', () => row.classList.remove('dragging'));
    row.addEventListener('dragover', event => { event.preventDefault(); row.classList.add('drop-target'); });
    row.addEventListener('dragleave', () => row.classList.remove('drop-target'));
    row.addEventListener('drop', event => {
      event.preventDefault(); row.classList.remove('drop-target');
      const fromId = event.dataTransfer.getData('text/plain'), toId = row.dataset.layer;
      const from = state.layers.findIndex(layer => layer.id === fromId), to = state.layers.findIndex(layer => layer.id === toId);
      if (from < 0 || to < 0 || from === to) return;
      const [layer] = state.layers.splice(from, 1); state.layers.splice(to, 0, layer);
      renderLayerStack(); updateSurfaceUniforms(); markSaved(false);
    });
  });
  list.querySelectorAll('[data-layer-toggle]').forEach(button => button.addEventListener('click', event => {
    event.stopPropagation();
    const layer = state.layers.find(item => item.id === button.dataset.layerToggle);
    if (!layer) return;
    layer.enabled = !layer.enabled;
    renderLayerStack(); markSaved(false);
  }));
}
function renderLayerProperties() {
  const layer = state.layers.find(item => item.id === state.selectedLayer) || state.layers[0];
  if (!layer) return;
  const properties = $('#layerProperties');
  const group = layerGroup(layer);
  properties.innerHTML = `<div class="layer-properties-head"><b>${escapeHTML(layer.name)}</b><span>${Math.round(layer.value * 100)}% / BLEND</span></div>
    <div class="layer-prop-range"><label>Strength</label><input type="range" min="0" max="1" step="0.01" value="${layer.value}" data-layer-value /><output data-layer-output>${Math.round(layer.value * 100)}%</output></div>
    ${group === 'weathering' ? `<div class="layer-prop-range" style="margin-top:8px"><label>Wet film</label><input type="range" min="0" max="1" step="0.01" value="${state.wetness}" data-wetness /><output data-wetness-output>${Math.round(state.wetness * 100)}%</output></div>` : ''}
    <div class="layer-order-note">${group === 'lane-paint' ? 'Thermoplastic wear is evaluated against road exposure and traffic.' : group === 'cracks' ? 'Damage masks are baked into the road material pass; geometry remains editable.' : group === 'paving' ? 'Paver joints are world-space and remain aligned across a surface patch.' : 'Drag the layer grip to reorder the procedural surface stack.'}</div>`;
  $('[data-layer-value]', properties)?.addEventListener('input', event => {
    layer.value = Number(event.target.value);
    $('[data-layer-output]', properties).textContent = `${Math.round(layer.value * 100)}%`;
    $('.layer-properties-head span', properties).textContent = `${Math.round(layer.value * 100)}% / BLEND`;
    $('.layer-row[data-layer="' + layer.id + '"] .layer-value', $('#layerList')).textContent = `${Math.round(layer.value * 100)}%`;
    if (gpu) { /* The value is consumed by the next frame's shader uniform. */ }
    markSaved(false);
  });
  $('[data-wetness]', properties)?.addEventListener('input', event => {
    state.wetness = Number(event.target.value);
    $('[data-wetness-output]', properties).textContent = `${Math.round(state.wetness * 100)}%`;
    markSaved(false);
  });
}
function addLayer() {
  const choices = [
    { id: `dust-${Date.now()}`, name: 'Dust / salt residue', detail: 'ROAD FILM / 1K', group: 'weathering', value: .22 },
    { id: `surface-cracks-${Date.now()}`, name: 'Fine fatigue cracks', detail: 'DAMAGE MASK / 2K', group: 'cracks', value: .16 },
    { id: `paint-wear-${Date.now()}`, name: 'Reflective bead wear', detail: 'THERMOPLASTIC / 1K', group: 'lane-paint', value: .28 },
  ];
  const available = choices.find(item => !state.layers.some(layer => layer.name === item.name));
  if (!available) { toast('Surface stack is full', 'Six authored layers are active. Reorder or disable an existing layer.'); return; }
  state.layers.push({ ...available, enabled: true });
  state.selectedLayer = available.id;
  renderLayerStack(); markSaved(false);
  toast('Material layer added', `${available.name} was inserted above the base surface.`);
}
function setFeature(feature, enabled) {
  state.features[feature] = !!enabled;
  if (feature === 'traffic') {
    if (enabled && !trafficAgents.length && state.trafficCount) initializeTraffic();
    rebuildTrafficBuffer(); updateOutliner(); markSaved(false); return;
  }
  if (feature === 'train') { rebuildTrafficBuffer(); updateOutliner(); markSaved(false); return; }
  if (feature === 'network') {
    // Keep the graph editable; toggling the collection only hides road geometry on export/preview.
    state.features.network = !!enabled;
  }
  $$('.feature-toggle input[data-feature]').forEach(input => { if (input.dataset.feature === feature) input.checked = !!enabled; });
  rebuildScene({ quiet: true, preserveTraffic: true });
  markSaved(false);
}
function toggleFeature(feature) { setFeature(feature, !state.features[feature]); }
function setMode(mode) {
  state.mode = mode;
  $$('.top-tab').forEach(button => button.classList.toggle('active', button.dataset.mode === mode));
  const dock = $('.right-dock');
  const dockSurface = mode === 'surface';
  dock.classList.toggle('dock-surface-mode', dockSurface);
  $$('.dock-tab').forEach(button => button.classList.toggle('active', button.dataset.dock === (dockSurface ? 'surface' : 'scene')));
  if (mode === 'surface') {
    $('#viewportModeTitle').textContent = 'Surface laboratory';
    $('#viewportModeCaption').textContent = 'World-space PBR / layered road materials';
    state.selectedLayer ||= 'road-base';
    renderLayerStack();
  } else if (mode === 'traffic') {
    $('#viewportModeTitle').textContent = 'Traffic simulation';
    $('#viewportModeCaption').textContent = 'Live lane agents / max 25 vehicles';
    state.selected = { type: 'group', id: 'traffic' };
    updateInspector(); updateOutliner();
    setTimeout(() => $('.traffic-settings')?.scrollIntoView({ block: 'nearest', behavior: 'smooth' }), 50);
  } else {
    $('#viewportModeTitle').textContent = 'Network view';
    $('#viewportModeCaption').textContent = 'Perspective / real-world scale';
  }
}
function showContextMenu(x, y, menuType = 'create') {
  const menu = $('#contextMenu');
  const actions = menuType === 'create' ? [
    ['spline', 'Draw road spline', 'R', () => setTool('road')],
    ['intersection', 'Place junction', 'J', () => setTool('junction')],
    ['bridge', 'Add viaduct', '', () => setFeature('bridge', true)],
    ['train', 'Toggle rail underpass', '', () => setFeature('rail', !state.features.rail)],
    ['wrench', 'Toggle construction zone', '', () => setFeature('construction', !state.features.construction)],
    ['car', 'Traffic controls', '', () => setMode('traffic')],
  ] : [
    ['expand', 'Frame network', 'F', frameScene],
    ['grid', 'Toggle ground grid', 'G', toggleGrid],
    ['orbit', 'Top view', '7', setTopView],
    ['download', 'Export mesh', '⌘E', openExport],
  ];
  menu.innerHTML = actions.map(([iconName, label, key], index) => `${index === 2 && menuType === 'create' ? '<div class="menu-rule"></div>' : ''}<button data-menu-action="${index}">${icon(iconName)}<span>${label}</span><small>${key}</small></button>`).join('');
  menu.hidden = false;
  menu.style.left = `${Math.min(window.innerWidth - 190, x)}px`;
  menu.style.top = `${Math.min(window.innerHeight - 230, y)}px`;
  menuOpen = true;
  menu.querySelectorAll('button').forEach((button, index) => button.addEventListener('click', () => {
    actions[index][3](); menu.hidden = true; menuOpen = false;
  }));
}
function toast(title, description = '', type = 'success') {
  const region = $('#toastRegion');
  const element = document.createElement('div');
  element.className = `toast ${type === 'warning' ? 'warning' : ''}`;
  element.innerHTML = `${icon(type === 'warning' ? 'wrench' : 'check')}<span><strong>${escapeHTML(title)}</strong>${description ? ` · ${escapeHTML(description)}` : ''}</span>`;
  region.appendChild(element);
  setTimeout(() => { element.style.opacity = '0'; element.style.transform = 'translateY(5px)'; setTimeout(() => element.remove(), 220); }, 3100);
}
function toggleGrid() {
  gridVisible = !gridVisible;
  $('#gridButton').classList.toggle('active', gridVisible);
  rebuildScene({ quiet: true, preserveTraffic: true });
}
function frameScene() {
  if (!network) return;
  const bounds = boundsForNetwork(network, compiled.edgePaths);
  const width = bounds.maxX - bounds.minX, height = bounds.maxZ - bounds.minZ;
  camera.target = V((bounds.minX + bounds.maxX) * .5, 0, (bounds.minZ + bounds.maxZ) * .5);
  const aspect = canvasWidth / Math.max(1, canvasHeight);
  const vertical = Math.max(height, width / Math.max(.2, aspect));
  camera.distance = clamp(vertical * 1.22 / (2 * Math.tan(46 * Math.PI / 360)), 30, 170);
  camera.pitch = camera.top ? 1.48 : .72;
  updateCameraMatrices(); updateGizmo(); updateSceneLabels();
}
function setTopView() {
  camera.top = true; camera.pitch = 1.48;
  $('#topViewButton').classList.add('active'); $('#perspectiveButton').classList.remove('active');
  updateCameraMatrices(); updateGizmo(); updateSceneLabels();
}
function setPerspectiveView() {
  camera.top = false; camera.pitch = .72;
  $('#topViewButton').classList.remove('active'); $('#perspectiveButton').classList.add('active');
  updateCameraMatrices(); updateGizmo(); updateSceneLabels();
}
function setPreset(preset) {
  state.preset = preset;
  $$('.preset-card').forEach(button => button.classList.toggle('selected', button.dataset.preset === preset));
  $('#seedStatus').textContent = 'READY';
  $('#viewportModeTitle').textContent = preset === 'highway' ? 'Interchange generator' : preset === 'suburban' ? 'Suburban arterial' : 'Network view';
}
function generateNetwork() {
  const before = network ? JSON.stringify(network) : null;
  state.seed = Number($('#seedInput').value) || Math.floor(Math.random() * 999999);
  state.density = Number($('#densityRange').value);
  state.blockScale = Number($('#blockRange').value);
  state.trafficSide = $('#trafficSide').value;
  state.region = $('#regionSelect').value;
  network = makeNetwork(state.preset, state.seed, state.density, state.blockScale);
  if (before) commitHistory(before);
  compiled = compileNetwork(network);
  const central = [...network.nodes].filter(node => node.degree >= 3).sort((a, b) => Math.hypot(a.x, a.z) - Math.hypot(b.x, b.z))[0] || network.nodes[Math.floor(network.nodes.length / 2)];
  state.selected = { type: 'node', id: central?.id || null };
  $('#seedStatus').textContent = `SEED ${state.seed}`;
  $('#loadingState').querySelector('b').textContent = 'Building road surface';
  frameScene();
  rebuildScene(); markSaved(false);
  toast('Road network generated', `${network.nodes.length} nodes · ${network.edges.length} splines · shared junction patches.`);
}
function randomizeSeed() {
  const seed = Math.floor(Math.random() * 990000) + 1000;
  $('#seedInput').value = String(seed); state.seed = seed; $('#seedStatus').textContent = 'NEW SEED';
}
function updateRangeFill(input) {
  const min = Number(input.min), max = Number(input.max), value = Number(input.value);
  input.style.setProperty('--progress', `${((value - min) / (max - min)) * 100}%`);
}
function updateControlsFromState() {
  $('#seedInput').value = String(state.seed);
  $('#densityRange').value = String(state.density); updateRangeFill($('#densityRange'));
  $('#densityValue').textContent = Number(state.density).toFixed(2);
  $('#blockRange').value = String(state.blockScale); updateRangeFill($('#blockRange'));
  $('#blockValue').textContent = `${state.blockScale} m`;
  $('#trafficSide').value = state.trafficSide; $('#regionSelect').value = state.region;
  for (const input of $$('[data-feature]')) input.checked = !!state.features[input.dataset.feature];
  $('#vehicleCountRange').value = String(state.trafficCount); updateRangeFill($('#vehicleCountRange'));
  $('#vehicleCountValue').textContent = String(state.trafficCount);
  setPreset(state.preset);
  updatePlayButton();
  renderLayerStack(); updateUndoButtons();
}
function updatePlayButton() {
  const button = $('#simulationButton');
  button.innerHTML = `${icon(state.playing ? 'pause' : 'play')}<span>${state.playing ? 'Pause simulation' : 'Resume simulation'}</span>`;
}
function openExport() { $('#exportModal').hidden = false; selectedExport = 'obj'; $$('.export-option').forEach(option => option.classList.toggle('selected', option.dataset.export === selectedExport)); }
function closeExport() { $('#exportModal').hidden = true; }
function downloadFile(filename, content, mime) {
  const blob = content instanceof Blob ? content : new Blob([content], { type: mime });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a'); anchor.href = url; anchor.download = filename; document.body.appendChild(anchor); anchor.click(); anchor.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function buildProjectJSON() {
  return JSON.stringify({
    format: 'RoadForge Project', version: 1, createdWith: 'RoadForge Experimental WebGPU Road Studio',
    units: 'metres', seed: state.seed, preset: state.preset, density: state.density, blockScale: state.blockScale,
    trafficSide: state.trafficSide, region: state.region, features: state.features, trafficCount: Math.min(25, state.trafficCount),
    surfaceStack: state.layers, wetness: state.wetness,
    topology: { nodes: network.nodes.map(({ id, name, x, y, z, radius, signalized }) => ({ id, name, position: [x, y, z], cornerRadius: radius, signalized })),
      roads: network.edges.map(({ id, a, b, kind, name, lanes, laneWidth, width, speed, bend, oneWay }) => ({ id, from: a, to: b, class: kind, name, lanes, laneWidth, width, designSpeed: speed, splineBend: bend, oneWay })),
      cityBlocks: network.blocks, roadSurface: 'Quad ribbon strips; intersections are shared convex patch meshes; GPU index triangles are emitted at draw/export time.' },
  }, null, 2);
}
function colorToSRGB(value) { return value <= .0031308 ? value * 12.92 : 1.055 * Math.pow(value, 1 / 2.4) - .055; }
function exportOBJ() {
  if (!staticMesh) { toast('Nothing to export', 'Generate a network first.', 'warning'); return; }
  const vertices = staticMesh.vertices;
  const vertexCount = vertices.length / 13;
  const obj = [`# RoadForge · North Quay procedural road network`, `# Units: metres`, `# ${network.nodes.length} nodes / ${network.edges.length} splines`, `# Quad ribbons and shared junction surfaces; triangulated for interchange.`, `mtllib north-quay-roadforge.mtl`, `o North_Quay_RoadForge`];
  for (let i = 0; i < vertices.length; i += 13) obj.push(`v ${vertices[i].toFixed(5)} ${vertices[i + 1].toFixed(5)} ${vertices[i + 2].toFixed(5)}`);
  for (let i = 0; i < vertices.length; i += 13) obj.push(`vt ${vertices[i + 6].toFixed(5)} ${vertices[i + 7].toFixed(5)}`);
  for (let i = 0; i < vertices.length; i += 13) obj.push(`vn ${vertices[i + 3].toFixed(5)} ${vertices[i + 4].toFixed(5)} ${vertices[i + 5].toFixed(5)}`);
  let currentMaterial = -1;
  for (let i = 0; i < vertexCount; i += 3) {
    const vertexOffset = i * 13;
    const material = Math.round(vertices[vertexOffset + 12]);
    if (material !== currentMaterial) { obj.push(`usemtl ${materialNames[material] || 'Surface'}`); currentMaterial = material; }
    obj.push(`f ${i + 1}/${i + 1}/${i + 1} ${i + 2}/${i + 2}/${i + 2} ${i + 3}/${i + 3}/${i + 3}`);
  }
  const baseColors = ['#51564b', '#343532', '#77766e', '#d3d2c9', '#767975', '#748188', '#53626a', '#aaa99f', '#242522', '#d9c4a3'];
  const mtl = ['# RoadForge material palette'];
  materialNames.forEach((name, index) => {
    const rgb = hexToLinear(baseColors[index] || '#808080').map(colorToSRGB);
    mtl.push(`newmtl ${name}`, `Kd ${rgb.map(value => value.toFixed(4)).join(' ')}`, `Ka 0.08 0.08 0.08`, `Ks ${index === 5 || index === 6 ? '.28 .28 .28' : '.08 .08 .08'}`, `Ns ${index === 5 ? '64' : index === 6 ? '100' : '18'}`, '');
  });
  downloadFile('north-quay-roadforge.obj', obj.join('\n'), 'text/plain');
  downloadFile('north-quay-roadforge.mtl', mtl.join('\n'), 'text/plain');
  toast('OBJ mesh exported', `${formatCount(staticMesh.quads)} quads · ${formatCount(vertexCount)} vertices · matching MTL palette.`);
}
function confirmExport() {
  if (selectedExport === 'json') {
    downloadFile('north-quay-roadforge.roadforge.json', buildProjectJSON(), 'application/json');
    toast('Project exported', 'Editable road graph and procedural surface stack saved as JSON.');
  } else exportOBJ();
  closeExport();
}

let viewportPointer = null;
function attachViewportControls() {
  canvas.addEventListener('pointerdown', event => {
    if (event.button !== 0 && event.button !== 1 && event.button !== 2) return;
    if (event.target.closest('.gizmo-cross')) return;
    event.preventDefault();
    canvas.setPointerCapture?.(event.pointerId);
    viewportPointer = { id: event.pointerId, x: event.clientX, y: event.clientY, lastX: event.clientX, lastY: event.clientY, button: event.button, dragged: false, pan: event.button === 1 || event.button === 2 || event.metaKey || event.ctrlKey || event.altKey };
  });
  canvas.addEventListener('pointermove', event => {
    const point = rayOnGround(event.clientX, event.clientY, 0);
    if (point) $('#coordinateReadout').innerHTML = `X ${point.x.toFixed(2)} <b>m</b> <span>·</span> Z ${point.z.toFixed(2)} <b>m</b>`;
    if (!viewportPointer || viewportPointer.id !== event.pointerId) return;
    const dx = event.clientX - viewportPointer.lastX, dy = event.clientY - viewportPointer.lastY;
    if (Math.hypot(event.clientX - viewportPointer.x, event.clientY - viewportPointer.y) > 4) viewportPointer.dragged = true;
    if (viewportPointer.dragged) {
      if (viewportPointer.pan) {
        const right = V(Math.sin(camera.yaw), 0, -Math.cos(camera.yaw));
        const forward = V(-Math.cos(camera.yaw), 0, -Math.sin(camera.yaw));
        const units = camera.distance / Math.max(260, canvasHeight) * 1.1;
        camera.target = add(camera.target, add(mul(right, -dx * units), mul(forward, dy * units)));
      } else {
        camera.yaw -= dx * .0062;
        camera.pitch = clamp(camera.pitch + dy * .0052, .16, 1.52);
        camera.top = camera.pitch > 1.36;
        $('#topViewButton').classList.toggle('active', camera.top);
        $('#perspectiveButton').classList.toggle('active', !camera.top);
      }
      updateCameraMatrices(); updateGizmo(); updateSceneLabels();
    }
    viewportPointer.lastX = event.clientX; viewportPointer.lastY = event.clientY;
  });
  const finish = event => {
    if (!viewportPointer || (event && event.pointerId !== viewportPointer.id)) return;
    const pointer = viewportPointer; viewportPointer = null;
    try { if (canvas.hasPointerCapture?.(pointer.id)) canvas.releasePointerCapture(pointer.id); } catch { /* capture can be released by the browser */ }
    if (!pointer.dragged && pointer.button === 0) handleViewportClick(event);
  };
  canvas.addEventListener('pointerup', finish);
  canvas.addEventListener('pointercancel', finish);
  canvas.addEventListener('contextmenu', event => { event.preventDefault(); showContextMenu(event.clientX, event.clientY, 'viewport'); });
  canvas.addEventListener('wheel', event => {
    event.preventDefault();
    camera.distance = clamp(camera.distance * Math.exp(event.deltaY * .0011), camera.minDistance, camera.maxDistance);
    updateCameraMatrices(); updateGizmo(); updateSceneLabels();
  }, { passive: false });
  canvas.addEventListener('dblclick', event => { event.preventDefault(); frameScene(); });
}
function attachEventHandlers() {
  attachViewportControls();
  resizeObserver = new ResizeObserver(() => { resizeCanvas(); updateSceneLabels(); updateGizmo(); });
  resizeObserver.observe(viewportWrap);
  window.addEventListener('resize', resizeCanvas);

  $$('.preset-card').forEach(button => button.addEventListener('click', () => setPreset(button.dataset.preset)));
  $('#generateButton').addEventListener('click', generateNetwork);
  $('#seedInput').addEventListener('keydown', event => { if (event.key === 'Enter') generateNetwork(); });
  $('#randomSeed').addEventListener('click', randomizeSeed);
  $('#densityRange').addEventListener('input', event => {
    state.density = Number(event.target.value); $('#densityValue').textContent = state.density.toFixed(2); updateRangeFill(event.target);
    $('#seedStatus').textContent = 'UNCOMMITTED';
  });
  $('#blockRange').addEventListener('input', event => {
    state.blockScale = Number(event.target.value); $('#blockValue').textContent = `${state.blockScale} m`; updateRangeFill(event.target);
    $('#seedStatus').textContent = 'UNCOMMITTED';
  });
  $('#trafficSide').addEventListener('change', event => { state.trafficSide = event.target.value; rebuildTrafficBuffer(); markSaved(false); });
  $('#regionSelect').addEventListener('change', event => { state.region = event.target.value; generateNetwork(); });
  $$('[data-feature]').forEach(input => input.addEventListener('change', event => setFeature(event.target.dataset.feature, event.target.checked)));
  $('#vehicleCountRange').addEventListener('input', event => {
    state.trafficCount = clamp(Math.floor(Number(event.target.value)), 0, 25);
    updateRangeFill(event.target); updateVehicleCount(); initializeTraffic(); rebuildTrafficBuffer(); updateOutliner();
    geometryDirty = true; geometryRebuildAfter = performance.now() + 150;
    markSaved(false);
  });
  $('#simulationButton').addEventListener('click', () => { state.playing = !state.playing; updatePlayButton(); toast(state.playing ? 'Traffic resumed' : 'Traffic paused', `${state.trafficCount} AI agents · hard maximum 25.`); });

  $$('.top-tab').forEach(button => button.addEventListener('click', () => setMode(button.dataset.mode)));
  $$('.text-button[data-mode]').forEach(button => button.addEventListener('click', () => setMode(button.dataset.mode)));
  $$('.dock-tab').forEach(button => button.addEventListener('click', () => {
    const surface = button.dataset.dock === 'surface';
    $('.right-dock').classList.toggle('dock-surface-mode', surface);
    $$('.dock-tab').forEach(tab => tab.classList.toggle('active', tab === button));
    if (surface) { setMode('surface'); }
    else if (state.mode === 'surface') setMode('network');
  }));
  $('#outlinerTree').addEventListener('click', event => {
    const visibility = event.target.closest('[data-feature-toggle]');
    if (visibility) {
      event.preventDefault(); event.stopPropagation(); toggleFeature(visibility.dataset.featureToggle); updateOutliner(); return;
    }
    const row = event.target.closest('.tree-row');
    if (!row) return;
    if (event.target.closest('.tree-disclosure')) {
      const group = row.closest('.tree-group');
      const children = $('.tree-children', group);
      if (children) {
        children.hidden = !children.hidden;
        const disclosure = $('.tree-disclosure', row);
        disclosure?.classList.toggle('open', !children.hidden);
        if (children.hidden) treeOpen.delete(group.dataset.treeGroup); else treeOpen.add(group.dataset.treeGroup);
      }
      return;
    }
    selectObject(row.dataset.selectType, row.dataset.selectId);
  });
  $('#outlinerSearch').addEventListener('input', updateOutliner);
  $('#miniMap').addEventListener('click', event => {
    const circle = event.target.closest('[data-map-node]');
    if (circle) selectObject('node', circle.dataset.mapNode);
  });
  $('#miniMapToggle').addEventListener('click', () => $('.mini-map-wrap').classList.toggle('collapsed'));
  $('#addObjectButton').addEventListener('click', event => showContextMenu(event.clientX, event.clientY, 'create'));
  $('#generatorMenu').addEventListener('click', event => showContextMenu(event.clientX, event.clientY, 'create'));
  $('#viewportMenuButton').addEventListener('click', event => showContextMenu(event.clientX, event.clientY, 'viewport'));
  $('#dockOptions').addEventListener('click', event => showContextMenu(event.clientX, event.clientY, 'create'));
  document.addEventListener('click', event => {
    if (menuOpen && !event.target.closest('#contextMenu') && !event.target.closest('#generatorMenu') && !event.target.closest('#addObjectButton') && !event.target.closest('#viewportMenuButton') && !event.target.closest('#dockOptions')) {
      $('#contextMenu').hidden = true; menuOpen = false;
    }
  });
  $('#addLayerButton').addEventListener('click', addLayer);
  $('#resetSelectionButton').addEventListener('click', () => {
    const selected = selectionObject();
    if (state.selected.type === 'node' && selected) { saveSnapshot(); selected.radius = 2.15; selected.signalized = true; rebuildScene({ quiet: true, preserveTraffic: true }); updateInspector(); markSaved(false); }
    else if (state.selected.type === 'edge' && selected) { saveSnapshot(); selected.width = selected.lanes * selected.laneWidth + selected.shoulder * 2; rebuildScene({ quiet: true, preserveTraffic: true }); updateInspector(); markSaved(false); }
    else toast('Inspector reset', 'The active system uses generated defaults.');
  });
  $('#frameButton').addEventListener('click', frameScene);
  $('#frameViewportButton').addEventListener('click', frameScene);
  $('#perspectiveButton').addEventListener('click', setPerspectiveView);
  $('#topViewButton').addEventListener('click', setTopView);
  $('#gridButton').addEventListener('click', toggleGrid);
  $('#helpButton').addEventListener('click', () => toast('RoadForge controls', 'Q select · W move · R road · J junction · M measure · F frame · Space play/pause.'));
  $('#exportButton').addEventListener('click', openExport);
  $('#closeExport').addEventListener('click', closeExport);
  $('#exportModal').addEventListener('click', event => { if (event.target === $('#exportModal')) closeExport(); });
  $$('.export-option').forEach(option => option.addEventListener('click', () => {
    selectedExport = option.dataset.export;
    $$('.export-option').forEach(item => item.classList.toggle('selected', item === option));
    $('#exportMeshInfo').textContent = selectedExport === 'obj' ? `${formatCount(staticMesh?.quads || 0)} quads · triangulated for export` : 'Editable graph · settings · features';
  }));
  $('#confirmExport').addEventListener('click', confirmExport);
  $('#saveIndicator').addEventListener('click', persistProject);
  $('#undoButton').addEventListener('click', undo); $('#redoButton').addEventListener('click', redo);

  document.addEventListener('keydown', event => {
    const target = event.target;
    const typing = target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement;
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'z') {
      event.preventDefault(); event.shiftKey ? redo() : undo(); return;
    }
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'y') { event.preventDefault(); redo(); return; }
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'e') { event.preventDefault(); openExport(); return; }
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'k') { event.preventDefault(); $('#outlinerSearch').focus(); return; }
    if (typing) return;
    const key = event.key.toLowerCase();
    if (key === 'q') setTool('select');
    if (key === 'w') setTool('move');
    if (key === 'r') setTool('road');
    if (key === 'j') setTool('junction');
    if (key === 'm') setTool('measure');
    if (key === 'f') frameScene();
    if (key === 'g') toggleGrid();
    if (key === '7') setTopView();
    if (key === ' ') { event.preventDefault(); state.playing = !state.playing; updatePlayButton(); }
    if (key === 'escape') {
      state.drawStart = null; state.measureStart = null; setTool('select');
      $('#contextMenu').hidden = true; $('#exportModal').hidden = true; menuOpen = false;
    }
  });
}

function initializeProject() {
  const restored = restoreProject();
  if (!restored) network = makeNetwork(state.preset, state.seed, state.density, state.blockScale);
  if (!Array.isArray(state.layers) || !state.layers.length) state.layers = LAYER_DEFAULTS.map(layer => ({ ...layer }));
  state.features = { ...FEATURE_DEFAULTS, ...(state.features || {}) };
  state.features.traffic = state.features.traffic !== false;
  state.features.train = state.features.train !== false;
  if (network.nextNode == null) network.nextNode = network.nodes.length + 1;
  if (network.nextEdge == null) network.nextEdge = network.edges.length + 1;
  state.seed = Number(network.seed) || state.seed;
  $('#seedStatus').textContent = `SEED ${state.seed}`;
  updateControlsFromState();
  compiled = compileNetwork(network);
  const central = [...network.nodes].filter(node => node.degree >= 3).sort((a, b) => Math.hypot(a.x, a.z) - Math.hypot(b.x, b.z))[0];
  if (!state.selected.id || !network.nodes.some(node => node.id === state.selected.id) && state.selected.type === 'node') state.selected = { type: 'node', id: central?.id || network.nodes[0]?.id };
  resizeCanvas(); frameScene();
  attachEventHandlers();
  updateRangeFill($('#vehicleCountRange')); updateRangeFill($('#densityRange')); updateRangeFill($('#blockRange'));
  staticMesh = buildAllGeometry(network);
  initializeTraffic();
  rebuildTrafficBuffer();
  updateOutliner(); updateInspector(); updateMiniMap(); updateSceneLabels(); updateMeshStats();
  setTimeout(() => $('#loadingState').classList.add('done'), 500);
  requestAnimationFrame(renderFrame);
  initializeRenderer();
  if (!restored) persistProject();
}

initializeProject();
