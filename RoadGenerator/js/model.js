// Project model: intersections (nodes), roads (splines between nodes), manual props, settings.
// Everything here is plain JSON so projects can be saved, undone and shared.

export const ROAD_CLASSES = {
  local: {
    label: 'Local street', speed: 30, lanesPerDir: 1, laneWidth: 3.0, sidewalk: 'both', sidewalkWidth: 2.2,
    median: false, medianWidth: 0, railings: false, lightSpacing: 32, color: '#9fb7c9',
  },
  collector: {
    label: 'Collector', speed: 40, lanesPerDir: 1, laneWidth: 3.3, sidewalk: 'both', sidewalkWidth: 2.8,
    median: false, medianWidth: 0, railings: false, lightSpacing: 30, color: '#b7c58f',
  },
  arterial: {
    label: 'Arterial', speed: 50, lanesPerDir: 2, laneWidth: 3.5, sidewalk: 'both', sidewalkWidth: 3.0,
    median: true, medianWidth: 2.2, railings: false, lightSpacing: 34, color: '#d8b57a',
  },
  highway: {
    label: 'Highway', speed: 100, lanesPerDir: 3, laneWidth: 3.7, sidewalk: 'none', sidewalkWidth: 2.4,
    median: true, medianWidth: 4.0, railings: true, lightSpacing: 45, color: '#d98d76',
  },
};

export const BOLLARD_DESIGNS = {
  post: 'Steel post with dome',
  square: 'Square concrete',
  retract: 'Retractable (yellow band)',
  ornate: 'Cast iron ornamental',
};

export const MATERIAL_NAMES = ['Asphalt', 'Concrete', 'Pavers'];

export const CURB_HEIGHT = 0.15;
export const CURB_WIDTH = 0.25;

let idCounter = 1;
export function newId(prefix) {
  idCounter += 1;
  return `${prefix}${idCounter.toString(36)}${Math.floor(Math.random() * 1296).toString(36)}`;
}
export function resetIdCounter(n) { idCounter = Math.max(idCounter, n); }

export function defaultSettings() {
  return {
    sunAzimuth: 215,
    sunElevation: 38,
    wetness: 0.0,
    fogDensity: 0.0007,
    trafficCount: 14,
    trafficOn: true,
    markingsOn: true,
    showGrid: true,
    snap: false,
    shadowSize: 2048,
    textureTile: { asphalt: 6, concrete: 4, pavers: 2.4 },
    exposure: 1.0,
  };
}

export function makeRoad(opts) {
  const cls = ROAD_CLASSES[opts.cls || 'local'];
  return {
    id: opts.id || newId('r'),
    name: opts.name || '',
    a: opts.a,
    b: opts.b,
    mid: (opts.mid || []).map(p => ({ x: p.x, z: p.z })),
    cls: opts.cls || 'local',
    lanesPerDir: opts.lanesPerDir ?? cls.lanesPerDir,
    laneWidth: opts.laneWidth ?? cls.laneWidth,
    speed: opts.speed ?? cls.speed,
    sidewalk: opts.sidewalk ?? cls.sidewalk,
    sidewalkWidth: opts.sidewalkWidth ?? cls.sidewalkWidth,
    sidewalkMat: opts.sidewalkMat ?? 'concrete',
    surface: opts.surface ?? 'asphalt',
    median: opts.median ?? cls.median,
    medianWidth: opts.medianWidth ?? cls.medianWidth,
    markings: opts.markings ?? true,
    crosswalks: opts.crosswalks ?? true,
    lightSpacing: opts.lightSpacing ?? cls.lightSpacing,
    bollards: { on: false, design: 'post', spacing: 4, ...(opts.bollards || {}) },
    railings: { on: typeof opts.railings === 'object' && opts.railings ? !!opts.railings.on : (opts.railings ?? cls.railings), sides: 'both' },
    speedBumps: opts.speedBumps ?? 0,
    potholes: opts.potholes ?? 0,
    construction: { on: false, from: 0.35, to: 0.6, side: 'right', ...(opts.construction || {}) },
    traffic: opts.traffic ?? true,
    hidden: false,
    seed: opts.seed ?? Math.floor(Math.random() * 100000),
  };
}

export function makeNode(opts) {
  return { id: opts.id || newId('n'), name: opts.name || '', x: opts.x, z: opts.z, scale: opts.scale ?? 1, signal: !!opts.signal };
}

export function makeProp(opts) {
  return { id: opts.id || newId('p'), kind: opts.kind, x: opts.x, z: opts.z, rot: opts.rot || 0, design: opts.design || 'post', scale: opts.scale ?? 1 };
}

export function createEmptyProject() {
  return { version: 1, name: 'Untitled city', nodes: [], roads: [], props: [], settings: defaultSettings(), layers: null };
}

// Full ordered control polygon of a road, including its two end nodes.
export function roadCtrl(project, road) {
  const nodes = indexNodes(project);
  const na = nodes.get(road.a), nb = nodes.get(road.b);
  if (!na || !nb) return null;
  return [{ x: na.x, z: na.z }, ...road.mid, { x: nb.x, z: nb.z }];
}

export function indexNodes(project) {
  const m = new Map();
  for (const n of project.nodes) m.set(n.id, n);
  return m;
}

export function nodeArms(project) {
  // nodeId -> [{ road, atStart }]
  const arms = new Map();
  for (const n of project.nodes) arms.set(n.id, []);
  for (const r of project.roads) {
    if (!arms.has(r.a)) arms.set(r.a, []);
    if (!arms.has(r.b)) arms.set(r.b, []);
    arms.get(r.a).push({ road: r, atStart: true });
    arms.get(r.b).push({ road: r, atStart: false });
  }
  return arms;
}

export function roadLengthApprox(project, road) {
  const c = roadCtrl(project, road);
  if (!c) return 0;
  let L = 0;
  for (let i = 0; i < c.length - 1; i++) L += Math.hypot(c[i + 1].x - c[i].x, c[i + 1].z - c[i].z);
  return L;
}

// A small but realistic starter network: a 4-way arterial crossing, a T-junction with a
// collector and a local cul-de-sac, plus a highway on-ramp style merge. Used on first launch.
export function sampleProject() {
  const p = createEmptyProject();
  p.name = 'Sample district';
  const n = (id, x, z, name) => p.nodes.push(makeNode({ id, x, z, name }));
  n('nA', -90, 0, 'West gate');
  n('nB', 0, 0, 'Central crossing');
  n('nC', 90, 0, 'East gate');
  n('nD', 0, -90, 'South T');
  n('nE', 0, 110, 'Cul-de-sac');
  n('nF', 70, -70, 'Merge');
  p.roads.push(makeRoad({ id: 'rA', a: 'nA', b: 'nB', cls: 'arterial', mid: [{ x: -45, z: 8 }], name: 'Main West' }));
  p.roads.push(makeRoad({ id: 'rB', a: 'nB', b: 'nC', cls: 'arterial', mid: [{ x: 48, z: -6 }], name: 'Main East' }));
  p.roads.push(makeRoad({ id: 'rC', a: 'nD', b: 'nB', cls: 'collector', mid: [{ x: -6, z: -45 }], name: 'South Ave' }));
  p.roads.push(makeRoad({ id: 'rD', a: 'nB', b: 'nE', cls: 'local', mid: [{ x: 8, z: 55 }], name: 'Cul-de-sac Rd', speedBumps: 2, potholes: 3 }));
  p.roads.push(makeRoad({ id: 'rE', a: 'nD', b: 'nF', cls: 'local', mid: [{ x: 40, z: -95 }], name: 'Depot Lane', potholes: 2 }));
  p.roads.push(makeRoad({ id: 'rF', a: 'nC', b: 'nF', cls: 'highway', mid: [{ x: 110, z: -40 }], name: 'Ring Ramp' }));
  p.roads[1].lightSpacing = 30;
  p.roads[5].construction = { on: true, from: 0.3, to: 0.7, side: 'right' };
  p.roads[2].bollards = { on: true, design: 'retract', spacing: 3.5 };
  p.roads[3].bollards = { on: true, design: 'ornate', spacing: 5 };
  p.props.push(makeProp({ kind: 'bollard', x: 14, z: 16, rot: 0.4, design: 'square' }));
  p.props.push(makeProp({ kind: 'cone', x: -20, z: -28, rot: 0 }));
  p.props.push(makeProp({ kind: 'pothole', x: -63, z: -4 }));
  p.props.push(makeProp({ kind: 'streetlight', x: 22, z: -40 }));
  return p;
}
