// Lane vocabulary, road presets, bridge types and prop kinds.
// Everything that is "a road type" lives here so the editor, the city generator and the
// renderer all agree on one source of truth.

/**
 * Lane types. `h` = height above the carriageway (curbs are a height step),
 * `surface` = material bucket, `dir` is used for drive lanes to decide the centre marking.
 */
export const LANE_TYPES = {
  drive: { label: 'Driving lane', surface: 'asphalt', h: 0, color: '#2b2e33' },
  turn: { label: 'Turn / centre lane', surface: 'asphalt', h: 0, color: '#2b2e33' },
  bus: { label: 'Bus lane', surface: 'asphalt_red', h: 0, color: '#8a2a2a' },
  bike: { label: 'Bike lane', surface: 'bike', h: 0, color: '#2f7a4a' },
  parking: { label: 'Parking lane', surface: 'asphalt', h: 0, color: '#33363b', parking: true },
  sidewalk: { label: 'Sidewalk', surface: 'concrete', h: 0.15, color: '#b7b5ad' },
  pavers: { label: 'Footpath (pavers)', surface: 'pavers', h: 0.0, color: '#9c8f80' },
  median: { label: 'Median (kerbed)', surface: 'concrete', h: 0.3, color: '#9d9d98', barrier: true },
  shoulder: { label: 'Shoulder (asphalt)', surface: 'asphalt', h: 0, color: '#3a3d42' },
  grass: { label: 'Verge (grass)', surface: 'grass', h: 0.02, color: '#3d6b2c' },
  gravel: { label: 'Gravel', surface: 'gravel', h: 0.0, color: '#8a7c65' },
  dirt: { label: 'Off-road dirt', surface: 'dirt', h: 0.0, color: '#6b5233' },
  tram: { label: 'Tram track bed', surface: 'tram', h: 0.02, color: '#6d5e55', rails: true },
  rail: { label: 'Railway ballast', surface: 'ballast', h: 0.03, color: '#4a4640', rails: true, sleepers: true },
  cycletrack: { label: 'Cycle track (kerbed)', surface: 'bike', h: 0.12, color: '#2f7a4a' },
};

export const LANE_TYPE_KEYS = Object.keys(LANE_TYPES);

/**
 * Road presets. `lanes` is listed left -> right. Each entry: [type, width, dir?].
 * Presets only give the starting point; every lane can be edited per road afterwards.
 */
export const PRESETS = {
  residential: {
    label: 'Residential street',
    lanes: [['sidewalk', 2.6], ['parking', 2.3], ['drive', 3.1, 'fwd'], ['drive', 3.1, 'back'], ['parking', 2.3], ['sidewalk', 2.6]],
  },
  arterial_bus: {
    label: 'Arterial with bus & bike lanes',
    lanes: [['sidewalk', 3.2], ['bike', 1.8], ['bus', 3.4], ['drive', 3.3, 'fwd'], ['turn', 1.8], ['drive', 3.3, 'back'], ['bus', 3.4], ['bike', 1.8], ['sidewalk', 3.2]],
  },
  avenue_tram: {
    label: 'Avenue with tram',
    lanes: [['sidewalk', 3.0], ['parking', 2.2], ['drive', 3.2, 'fwd'], ['tram', 3.0], ['drive', 3.2, 'back'], ['parking', 2.2], ['sidewalk', 3.0]],
  },
  boulevard_median: {
    label: 'Boulevard with median',
    lanes: [['sidewalk', 3.0], ['parking', 2.2], ['drive', 3.4, 'fwd'], ['drive', 3.4, 'fwd'], ['median', 2.6], ['drive', 3.4, 'back'], ['drive', 3.4, 'back'], ['parking', 2.2], ['sidewalk', 3.0]],
  },
  cycle_boulevard: {
    label: 'Cycle boulevard (segregated)',
    lanes: [['sidewalk', 2.8], ['cycletrack', 2.4], ['drive', 3.2, 'fwd'], ['drive', 3.2, 'back'], ['cycletrack', 2.4], ['sidewalk', 2.8]],
  },
  highway: {
    label: 'Highway (dual carriageway)',
    lanes: [['shoulder', 3.0], ['drive', 3.7, 'fwd'], ['drive', 3.7, 'fwd'], ['drive', 3.7, 'fwd'], ['median', 4.0], ['drive', 3.7, 'back'], ['drive', 3.7, 'back'], ['drive', 3.7, 'back'], ['shoulder', 3.0]],
  },
  country_road: {
    label: 'Country road',
    lanes: [['grass', 1.2], ['shoulder', 0.8], ['drive', 3.0, 'fwd'], ['drive', 3.0, 'back'], ['shoulder', 0.8], ['grass', 1.2]],
  },
  gravel_track: {
    label: 'Gravel / farm track',
    lanes: [['grass', 0.8], ['gravel', 3.4], ['grass', 0.8]],
  },
  offroad: {
    label: 'Off-road trail',
    lanes: [['dirt', 2.6], ['grass', 0.6]],
  },
  footpath: {
    label: 'Footpath / pathway',
    lanes: [['pavers', 2.6]],
  },
  cycle_path: {
    label: 'Shared cycle path',
    lanes: [['cycletrack', 3.0]],
  },
  railway: {
    label: 'Railway line (double track)',
    kind: 'rail',
    lanes: [['rail', 1.2], ['rail', 1.2], ['rail', 1.2]],
  },
};

export const PRESET_KEYS = Object.keys(PRESETS);

/** Bridge types. Each one is drawn by a different model in render/bridges.js. */
export const BRIDGE_TYPES = {
  girder: { label: 'Girder / beam bridge' },
  arch: { label: 'Tied arch bridge' },
  suspension: { label: 'Suspension bridge' },
  cable: { label: 'Cable-stayed bridge' },
};

/** Prop kinds that the editor can drop onto the network. */
export const PROP_KINDS = {
  bollard: { label: 'Bollard row' },
  speedbump: { label: 'Speed bump' },
  railcross: { label: 'Railway crossing' },
  streetlamp: { label: 'Street lamp' },
};

export const DEFAULTS = {
  clearance: 9,      // bridge deck height above water / ground
  depth: 8,          // subway depth below ground
  rampLength: 32,    // how far bridge / subway ramps take to reach full height
  spacing: 2,        // centreline sample spacing in metres
  snap: 3,           // merge radius for endpoints / crossings
};
