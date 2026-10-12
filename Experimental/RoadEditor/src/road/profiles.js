// Road cross-section library. A profile is an ordered list of lanes from the road's LEFT
// edge to its RIGHT edge (relative to the a→b direction of the edge). Drive-like lanes carry
// a direction: +1 travels a→b, -1 travels b→a, 0 is shared/bidirectional.

export const LEVEL = { raised: 0.15, road: 0, ground: -0.025 };

export const LANE_TYPES = {
  sidewalk:   { label: 'Sidewalk',        level: 'raised', mat: 'sidewalk',  color: '#a9a49a', zone: 'side',  w: 3.0 },
  planter:    { label: 'Tree lawn',       level: 'raised', mat: 'grass',     color: '#5f7d4c', zone: 'side',  w: 1.5 },
  cycletrack: { label: 'Cycle track',     level: 'raised', mat: 'bikeGreen', color: '#4f8f65', zone: 'side',  w: 2.0, bike: true },
  median:     { label: 'Median',          level: 'raised', mat: 'grass',     color: '#6b8a52', zone: 'median', w: 3.0 },
  drive:      { label: 'Travel lane',     level: 'road',   mat: 'surface',   color: '#3b3d42', w: 3.4, traffic: true },
  bus:        { label: 'Bus lane',        level: 'road',   mat: 'busRed',    color: '#8a3b33', w: 3.5, traffic: true },
  bike:       { label: 'Bike lane',       level: 'road',   mat: 'bikeGreen', color: '#3f7f57', w: 1.8, traffic: true, bike: true },
  parking:    { label: 'Parking lane',    level: 'road',   mat: 'surface',   color: '#4a4c52', w: 2.4 },
  shoulder:   { label: 'Shoulder',        level: 'road',   mat: 'surface',   color: '#45474c', w: 2.5 },
  buffer:     { label: 'Buffer',          level: 'road',   mat: 'surface',   color: '#55575c', w: 0.9 },
  tram:       { label: 'Tram lane',       level: 'road',   mat: 'surface',   color: '#5d5148', w: 3.2, traffic: true, rails: true },
  barrier:    { label: 'Jersey barrier',  level: 'road',   mat: 'surface',   color: '#9c9890', w: 0.8 },
  gravel:     { label: 'Gravel shoulder', level: 'ground', mat: 'gravel',    color: '#7d7466', w: 1.5 },
  verge:      { label: 'Grass verge',     level: 'ground', mat: 'grass',     color: '#55703f', w: 1.5 },
  path:       { label: 'Path',            level: 'road',   mat: 'surface',   color: '#8d877c', w: 3.0 },
};

const L = (type, width, dir, extra) => ({ type, width: width ?? LANE_TYPES[type].w, ...(dir !== undefined ? { dir } : {}), ...(extra || {}) });
const mirror = (lanes) => lanes.map(l => ({ ...l, dir: l.dir !== undefined ? -l.dir : undefined })).reverse();
// Build symmetric two-way profile from the right half (center → right edge).
const sym = (rightHalf) => [...mirror(rightHalf), ...rightHalf];

export const CATEGORIES = [
  { id: 'highway', label: 'Highways', blurb: 'Grade-separated, high speed' },
  { id: 'arterial', label: 'Arterials', blurb: 'Boulevards, avenues, transit' },
  { id: 'urban', label: 'Urban streets', blurb: 'Collectors, locals, one-ways' },
  { id: 'residential', label: 'Residential', blurb: 'Quiet, shared and historic' },
  { id: 'rural', label: 'Rural', blurb: 'Country roads, dirt and gravel' },
  { id: 'active', label: 'Active travel', blurb: 'Paths, cycleways, promenades' },
];

export const PROFILES = {
  freeway6: {
    name: 'Freeway · 6 lanes', category: 'highway', speed: 110, surface: 'asphalt', lamps: 'median', lampHeight: 12, lampSpacing: 45, elevatedDefault: 9,
    desc: 'Divided motorway, 3+3 lanes, concrete median barrier, wide shoulders.',
    lanes: sym([L('shoulder', 1.2), L('drive', 3.7, 1), L('drive', 3.7, 1), L('drive', 3.7, 1), L('shoulder', 3.0), L('barrier', 0.6)]),
  },
  freeway4: {
    name: 'Freeway · 4 lanes', category: 'highway', speed: 100, surface: 'asphalt', lamps: 'median', lampHeight: 12, lampSpacing: 45,
    desc: 'Divided motorway, 2+2 lanes with barrier median.',
    lanes: sym([L('shoulder', 1.0), L('drive', 3.7, 1), L('drive', 3.7, 1), L('shoulder', 2.8), L('barrier', 0.6)]),
  },
  ramp1: {
    name: 'Ramp · 1 lane', category: 'highway', speed: 60, surface: 'asphalt', lamps: 'right', lampHeight: 10, lampSpacing: 40, oneWay: true,
    desc: 'One-way interchange ramp with shoulders and barriers.',
    lanes: [L('barrier', 0.6), L('shoulder', 1.2), L('drive', 3.8, 1), L('shoulder', 2.4), L('barrier', 0.6)],
  },
  ramp2: {
    name: 'Ramp · 2 lanes', category: 'highway', speed: 70, surface: 'asphalt', lamps: 'right', lampHeight: 10, lampSpacing: 40, oneWay: true,
    desc: 'Two-lane collector/distributor ramp.',
    lanes: [L('barrier', 0.6), L('shoulder', 1.0), L('drive', 3.7, 1), L('drive', 3.7, 1), L('shoulder', 2.4), L('barrier', 0.6)],
  },
  rampLoop: {
    name: 'Ramp · loop', category: 'highway', speed: 40, surface: 'asphalt', lamps: 'right', lampHeight: 9, lampSpacing: 38, oneWay: true,
    desc: 'Cloverleaf loop: one wide lane, generous shoulders, barriers both sides, tight constant radius.',
    lanes: [L('barrier', 0.8), L('shoulder', 1.4), L('drive', 4.2, 1), L('shoulder', 2.0), L('barrier', 0.8)],
  },
  rampWide: {
    name: 'Ramp · 2 lanes wide', category: 'highway', speed: 80, surface: 'asphalt', lamps: 'right', lampHeight: 10, lampSpacing: 40, oneWay: true,
    desc: 'Direct connector: two full-width lanes with outside barrier, used for parclo and turbine diagonals.',
    lanes: [L('barrier', 0.8), L('shoulder', 1.2), L('drive', 3.8, 1), L('drive', 3.8, 1), L('shoulder', 2.4), L('barrier', 0.8)],
  },
  freeway8: {
    name: 'Freeway · 8 lanes + CD', category: 'highway', speed: 120, surface: 'asphalt', lamps: 'median', lampHeight: 12, lampSpacing: 42, elevatedDefault: 9,
    desc: 'Through lanes separated from collector-distributor lanes by barriers - the layout that lets interchanges weave without conflict.',
    lanes: sym([L('shoulder', 1.2), L('drive', 3.7, 1), L('drive', 3.7, 1), L('drive', 3.7, 1), L('drive', 3.7, 1), L('shoulder', 2.6), L('barrier', 0.6), L('drive', 3.5, 1), L('shoulder', 1.8), L('barrier', 0.6)]),
  },
  expressway: {
    name: 'Expressway', category: 'highway', speed: 90, surface: 'asphalt', lamps: 'none',
    desc: 'At-grade divided highway, grass median and gravel verges.',
    lanes: sym([L('shoulder', 1.0), L('drive', 3.6, 1), L('drive', 3.6, 1), L('shoulder', 2.5), L('gravel', 1.5), L('verge', 2.0)]),
    medianVerge: 7,
  },
  boulevard: {
    name: 'Grand boulevard', category: 'arterial', speed: 60, surface: 'asphalt', lamps: 'both', lampSpacing: 32, trees: true, busStops: true,
    desc: '2+2 lanes, curbside bus lanes, tree-lined median and tree lawns.',
    lanes: sym([L('median', 2.6, undefined, { trees: true }), L('drive', 3.3, 1), L('drive', 3.3, 1), L('bus', 3.5, 1), L('planter', 1.8, undefined, { trees: true }), L('sidewalk', 4.2)]),
  },
  arterial6: {
    name: 'Arterial · 6 lanes', category: 'arterial', speed: 60, surface: 'asphalt', lamps: 'both', lampSpacing: 34, trees: true, busStops: true,
    desc: 'Curb-sided 3+3 arterial with parking, a cycle track and wide walks - the road an interchange ramp merges into.',
    lanes: sym([L('median', 1.6), L('drive', 3.4, 1), L('drive', 3.4, 1), L('drive', 3.4, 1), L('parking', 2.3), L('bike', 1.8), L('sidewalk', 3.2)]),
  },
  parkway: {
    name: 'Parkway', category: 'arterial', speed: 70, surface: 'asphalt', lamps: 'median', lampSpacing: 40, trees: true,
    desc: 'Landscaped divided boulevard: no driveways, grass verge and a separated path beyond the shoulder.',
    lanes: sym([L('median', 2.8, undefined, { trees: true }), L('drive', 3.6, 1), L('drive', 3.6, 1), L('shoulder', 1.6), L('verge', 1.6), L('path', 2.4), L('gravel', 1.0)]),
  },
  avenueBike: {
    name: 'Avenue · protected bike', category: 'arterial', speed: 50, surface: 'asphalt', lamps: 'both', lampSpacing: 30, trees: true,
    desc: '2+2 lanes, parking-protected bike lanes with buffers.',
    lanes: sym([L('drive', 3.3, 1), L('drive', 3.3, 1), L('parking', 2.4), L('buffer', 0.9), L('bike', 1.9, 1), L('planter', 1.2, undefined, { trees: true }), L('sidewalk', 3.6)]),
  },
  arterialBRT: {
    name: 'Arterial · center BRT', category: 'arterial', speed: 50, surface: 'asphalt', lamps: 'both', lampSpacing: 30,
    desc: 'Center-running bus rapid transit with concrete islands.',
    lanes: sym([L('bus', 3.6, 1), L('median', 1.2, undefined, { mat: 'concrete' }), L('drive', 3.3, 1), L('drive', 3.3, 1), L('sidewalk', 4.0)]),
  },
  tramAvenue: {
    name: 'Tram avenue', category: 'arterial', speed: 50, surface: 'asphalt', lamps: 'both', lampSpacing: 30, trees: true,
    desc: 'Street-running tram in the center, traffic and parking either side.',
    lanes: sym([L('tram', 3.2, 1), L('drive', 3.4, 1), L('parking', 2.3), L('planter', 1.2, undefined, { trees: true }), L('sidewalk', 3.6)]),
  },
  arterial4: {
    name: 'Arterial · 4 lanes', category: 'arterial', speed: 60, surface: 'asphalt', lamps: 'both', lampSpacing: 34,
    desc: 'Undivided 2+2 arterial with double yellow center line.',
    lanes: sym([L('drive', 3.4, 1), L('drive', 3.4, 1), L('sidewalk', 3.2)]),
  },
  collector: {
    name: 'Collector street', category: 'urban', speed: 40, surface: 'asphalt', lamps: 'both', lampSpacing: 30, trees: true,
    desc: '1+1 lanes, painted bike lanes and parking both sides.',
    lanes: sym([L('drive', 3.3, 1), L('bike', 1.6, 1), L('parking', 2.3), L('planter', 1.2, undefined, { trees: true }), L('sidewalk', 3.0)]),
  },
  local: {
    name: 'Local street', category: 'urban', speed: 30, surface: 'asphalt', lamps: 'both', lampSpacing: 30, trees: true,
    desc: 'Two-way local with marked parking bays.',
    lanes: sym([L('drive', 3.2, 1), L('parking', 2.2), L('planter', 1.0, undefined, { trees: true }), L('sidewalk', 2.6)]),
  },
  oneway: {
    name: 'One-way street', category: 'urban', speed: 40, surface: 'asphalt', lamps: 'both', lampSpacing: 30, oneWay: true,
    desc: 'Two one-way lanes, contraflow-free, parking + bike lane.',
    lanes: [L('sidewalk', 3.0), L('parking', 2.3), L('drive', 3.3, 1), L('drive', 3.3, 1), L('bike', 1.7, 1), L('buffer', 0.6), L('parking', 2.3), L('sidewalk', 3.0)],
  },
  industrial: {
    name: 'Industrial road', category: 'urban', speed: 50, surface: 'concrete', lamps: 'right', lampSpacing: 40,
    desc: 'Wide truck lanes on concrete pavement, shoulders, narrow sidewalks.',
    lanes: sym([L('drive', 3.8, 1), L('shoulder', 2.4), L('sidewalk', 2.0)]),
  },
  alley: {
    name: 'Service alley', category: 'urban', speed: 15, surface: 'concrete', lamps: 'none',
    desc: 'Single shared lane, no curbs.',
    lanes: [L('drive', 5.0, 0)],
  },
  residential: {
    name: 'Residential street', category: 'residential', speed: 30, surface: 'asphalt', lamps: 'both', lampSpacing: 34, trees: true,
    desc: 'Unmarked two-way street with grass verges.',
    lanes: sym([L('drive', 3.1, 1), L('planter', 1.8, undefined, { trees: true }), L('sidewalk', 1.8)]),
    noCenterLine: true,
  },
  sharedStreet: {
    name: 'Shared street', category: 'residential', speed: 20, surface: 'pavers', lamps: 'both', lampSpacing: 22,
    desc: 'Level-surface woonerf — pavers, bollards, no curbs.',
    lanes: sym([L('drive', 2.8, 0), L('path', 2.6)]),
    noCenterLine: true, bollardsAt: 'path',
  },
  cobblestone: {
    name: 'Old-town cobblestone', category: 'residential', speed: 25, surface: 'cobble', lamps: 'both', lampSpacing: 20,
    desc: 'Historic setts with narrow granite-curbed sidewalks.',
    lanes: sym([L('drive', 3.0, 1), L('sidewalk', 2.0)]),
    noCenterLine: true,
  },
  rural2: {
    name: 'Rural two-lane', category: 'rural', speed: 80, surface: 'asphalt', lamps: 'none',
    desc: 'Passing-permitted center line, edge lines, gravel shoulders.',
    lanes: sym([L('drive', 3.5, 1), L('shoulder', 1.0), L('gravel', 1.6)]),
    centerLine: 'dashedYellow',
  },
  countryLane: {
    name: 'Country lane', category: 'rural', speed: 50, surface: 'asphaltOld', lamps: 'none',
    desc: 'Narrow unmarked lane with grass verges.',
    lanes: [L('verge', 1.0), L('drive', 4.6, 0), L('verge', 1.0)],
  },
  gravelRoad: {
    name: 'Gravel road', category: 'rural', speed: 40, surface: 'gravel', lamps: 'none',
    desc: 'Compacted aggregate track.',
    lanes: [L('verge', 0.8), L('drive', 5.0, 0), L('verge', 0.8)],
  },
  dirtRoad: {
    name: 'Dirt track', category: 'rural', speed: 30, surface: 'dirt', lamps: 'none',
    desc: 'Unsealed earth road.',
    lanes: [L('drive', 4.4, 0)],
  },
  footpath: {
    name: 'Footpath', category: 'active', speed: 5, surface: 'pavers', lamps: 'right', lampSpacing: 26, lampHeight: 4.5,
    desc: 'Paved pedestrian path.',
    lanes: [L('path', 3.0)],
  },
  gravelPath: {
    name: 'Park trail', category: 'active', speed: 5, surface: 'gravel', lamps: 'none',
    desc: 'Compacted gravel trail.',
    lanes: [L('path', 2.4)],
  },
  cycleway: {
    name: 'Cycleway', category: 'active', speed: 25, surface: 'bikeGreen', lamps: 'right', lampSpacing: 30, lampHeight: 5,
    desc: 'Bidirectional cycle path with dashed center line.',
    lanes: [L('bike', 1.6, -1), L('bike', 1.6, 1)],
  },
  promenade: {
    name: 'Promenade', category: 'active', speed: 5, surface: 'pavers', lamps: 'right', lampSpacing: 20, lampHeight: 4.5,
    desc: 'Wide riverside walk with cycle track.',
    lanes: [L('path', 4.5), L('bike', 1.5, -1), L('bike', 1.5, 1), L('path', 1.5)],
  },
};
// Expressway median: replace mirrored verge/gravel at the center with a single grass median.
PROFILES.expressway.lanes = [
  L('verge', 2.0), L('gravel', 1.4), L('shoulder', 2.5), L('drive', 3.6, -1), L('drive', 3.6, -1), L('shoulder', 1.0),
  L('verge', 8.0),
  L('shoulder', 1.0), L('drive', 3.6, 1), L('drive', 3.6, 1), L('shoulder', 2.5), L('gravel', 1.4), L('verge', 2.0),
];
// Freeway median: a single barrier between inner shoulders, outer barriers at deck edge.
PROFILES.freeway6.lanes = [
  L('barrier', 0.6), L('shoulder', 3.0), L('drive', 3.7, -1), L('drive', 3.7, -1), L('drive', 3.7, -1), L('shoulder', 1.2),
  L('barrier', 0.8),
  L('shoulder', 1.2), L('drive', 3.7, 1), L('drive', 3.7, 1), L('drive', 3.7, 1), L('shoulder', 3.0), L('barrier', 0.6),
];
PROFILES.freeway4.lanes = [
  L('barrier', 0.6), L('shoulder', 2.8), L('drive', 3.7, -1), L('drive', 3.7, -1), L('shoulder', 1.0),
  L('barrier', 0.8),
  L('shoulder', 1.0), L('drive', 3.7, 1), L('drive', 3.7, 1), L('shoulder', 2.8), L('barrier', 0.6),
];

for (const [id, p] of Object.entries(PROFILES)) p.id = id;

export function getProfile(edge) {
  if (edge.custom) return edge.custom;
  return PROFILES[edge.profile] || PROFILES.local;
}

export function laneMat(lane, profile) {
  if (lane.mat) return lane.mat;
  const t = LANE_TYPES[lane.type];
  if (t.mat === 'surface') return profile.surface || 'asphalt';
  return t.mat;
}

// Lateral layout: offsets for each lane, curb offsets for junction geometry.
const layoutCache = new WeakMap();
export function layout(profile) {
  if (layoutCache.has(profile)) return layoutCache.get(profile);
  const lanes = profile.lanes;
  const total = lanes.reduce((a, l) => a + l.width, 0);
  let o = -total / 2;
  const out = lanes.map((l, i) => {
    const t = LANE_TYPES[l.type];
    const r = { ...l, i, o0: o, o1: o + l.width, level: t.level, h: LEVEL[t.level], meta: t, mat: laneMat(l, profile) };
    o += l.width;
    return r;
  });
  // Junction "curb" lines: outermost contiguous non-raised lanes from the outside in.
  let li = 0; while (li < out.length && out[li].level === 'raised') li++;
  let ri = out.length - 1; while (ri >= 0 && out[ri].level === 'raised') ri--;
  if (li > ri) { li = 0; ri = out.length - 1; }
  const res = {
    lanes: out, total,
    left: -total / 2, right: total / 2,
    curbL: out[li].o0, curbR: out[ri].o1,           // signed offsets
    sideL: out[li].o0 - (-total / 2),                // raised zone width on left
    sideR: total / 2 - out[ri].o1,
    hasCurbL: li > 0, hasCurbR: ri < out.length - 1,
    roadFrom: li, roadTo: ri,
    oneWay: lanes.every(l => !LANE_TYPES[l.type].traffic || l.dir === 1 || l.dir === undefined) && lanes.some(l => l.dir === 1),
  };
  layoutCache.set(profile, res);
  return res;
}

export function cloneProfile(p) {
  return JSON.parse(JSON.stringify({ ...p, lanes: p.lanes }));
}

// Road hierarchy rank for junction priority / naming / LOD.
export const RANK = { freeway8: 9, rampLoop: 8, rampWide: 8, arterial6: 6, parkway: 6,  freeway4: 9, ramp1: 8, ramp2: 8, expressway: 8, boulevard: 7, avenueBike: 6, arterialBRT: 7, tramAvenue: 6, arterial4: 6, collector: 5, industrial: 5, oneway: 4, local: 3, residential: 3, cobblestone: 3, sharedStreet: 2, alley: 1, rural2: 5, countryLane: 2, gravelRoad: 2, dirtRoad: 1, footpath: 0, gravelPath: 0, cycleway: 0, promenade: 0 };
export const isPath = p => (p.category === 'active');
