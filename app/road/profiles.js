/* ============================================================
   FRONTIER · ROAD EDITOR — cross-section assembly library
   A "profile" is an ordered list of section elements from the left
   edge of the corridor to the right edge, plus design rules that drive
   surfaces, curbs, markings, street furniture and traffic lanes.
   ============================================================ */

export const SURFACES = {
  'asphalt':      { label: 'Asphalt (worn)',        tex: 'asphalt',  scale: 4.0, rough: .93, tint: [0.40, 0.41, 0.43], group: 'Carriageway' },
  'asphalt-new':  { label: 'Asphalt (fresh)',       tex: 'asphalt',  scale: 4.0, rough: .85, tint: [0.22, 0.225, 0.24], group: 'Carriageway' },
  'asphalt-red':  { label: 'Red oxidised surfacing',tex: 'asphalt',  scale: 4.0, rough: .9,  tint: [0.55, 0.20, 0.15], group: 'Carriageway' },
  'asphalt-blue': { label: 'Blue bus priority',     tex: 'asphalt',  scale: 4.0, rough: .9,  tint: [0.16, 0.26, 0.48], group: 'Carriageway' },
  'asphalt-dark': { label: 'Blacktop / dense',      tex: 'asphalt',  scale: 3.0, rough: .88, tint: [0.16, 0.165, 0.18], group: 'Carriageway' },
  'concrete':     { label: 'Concrete slabs',        tex: 'concrete', scale: 6.0, rough: .78, tint: [0.66, 0.66, 0.64], group: 'Paving' },
  'concrete-raw': { label: 'Concrete (in-situ)',    tex: 'concrete', scale: 8.0, rough: .84, tint: [0.52, 0.52, 0.51], group: 'Structures' },
  'brick':        { label: 'Clay brick',            tex: 'brick',    scale: 2.0, rough: .86, tint: [0.58, 0.31, 0.23], group: 'Paving' },
  'cobble':       { label: 'Granite setts',         tex: 'cobble',   scale: 1.6, rough: .72, tint: [0.48, 0.46, 0.43], group: 'Paving' },
  'paver':        { label: 'Concrete paver block',  tex: 'paver',    scale: 2.4, rough: .8,  tint: [0.60, 0.57, 0.53], group: 'Paving' },
  'paver-herring':{ label: 'Herringbone pavers',    tex: 'herring',  scale: 2.6, rough: .8,  tint: [0.62, 0.55, 0.47], group: 'Paving' },
  'granite-sm':   { label: 'Granite kerb stone',    tex: 'granite',  scale: 1.0, rough: .7,  tint: [0.55, 0.55, 0.54], group: 'Structures' },
  'gravel':       { label: 'Compact gravel',        tex: 'gravel',   scale: 1.6, rough: .95, tint: [0.52, 0.48, 0.42], group: 'Vernacular' },
  'dirt':         { label: 'Unmade track',          tex: 'gravel',   scale: 1.4, rough: .98, tint: [0.42, 0.35, 0.26], group: 'Vernacular' },
  'grass':        { label: 'Grass verge',           tex: 'grass',    scale: 2.0, rough: .95, tint: [0.30, 0.40, 0.22], group: 'Landscape' },
  'grass-mown':   { label: 'Mown median grass',     tex: 'grass',    scale: 1.6, rough: .95, tint: [0.35, 0.47, 0.26], group: 'Landscape' },
  'ballast':      { label: 'Track ballast',         tex: 'gravel',   scale: 0.8, rough: .96, tint: [0.40, 0.39, 0.38], group: 'Rail' },
  'metal':        { label: 'Weathering steel',      tex: 'metal',    scale: 1.0, rough: .45, tint: [0.42, 0.42, 0.44], group: 'Structures' },
  'steel-rail':   { label: 'Rail head (polished)',  tex: 'metal',    scale: 1.0, rough: .22, tint: [0.72, 0.73, 0.75], group: 'Rail' },
  'tarmac-patch': { label: 'Patched / cold lay',    tex: 'asphalt',  scale: 3.0, rough: .8,  tint: [0.30, 0.29, 0.28], group: 'Carriageway' },
  'waterproof':   { label: 'Bridge waterproofing',  tex: 'asphalt',  scale: 5.0, rough: .7,  tint: [0.25, 0.26, 0.30], group: 'Structures' },
  'tile-white':   { label: 'Glazed tunnel tile',    tex: 'tile',     scale: 1.2, rough: .35, tint: [0.78, 0.78, 0.75], group: 'Structures' },
  'timber':       { label: 'Timber decking',        tex: 'timber',   scale: 1.2, rough: .82, tint: [0.46, 0.33, 0.22], group: 'Vernacular' },
  'soil':         { label: 'Mulch / soil',          tex: 'grass',    scale: 2.2, rough: .98, tint: [0.24, 0.21, 0.15], group: 'Landscape' },
  'water':        { label: 'Water surface',         tex: 'water',    scale: 6.0, rough: .12, tint: [0.11, 0.19, 0.24], group: 'Landscape' },
};

/* ------------------------------- section element constructors ------------------------------- */
const E = (k, w, o = {}) => ({ k, w, ...o });
export const lane = (w = 3.4, o = {}) => E('lane', w, { surf: 'asphalt', ...o });
export const laneL = (w = 3.4, o = {}) => lane(w, { dir: 'rev', ...o });
export const turn = (w = 3.3, o = {}) => E('turn', w, { surf: 'asphalt', ...o });
export const bus = (w = 3.3, o = {}) => E('bus', w, { surf: 'asphalt-red', ...o });
export const cycle = (w = 1.8, o = {}) => E('cycle', w, { surf: 'asphalt-red', dir: undefined, ...o });
export const parking = (w = 2.4, o = {}) => E('parking', w, { surf: 'asphalt', ...o });
export const loading = (w = 3.0, o = {}) => E('loading', w, { surf: 'asphalt', ...o });
export const shoulder = (w = 1.0, o = {}) => E('shoulder', w, { surf: 'asphalt-dark', ...o });
export const gutter = (w = 0.6, o = {}) => E('gutter', w, { surf: 'concrete', ...o});
export const curb = (o = {}) => E('curb', o.bullnose ? 0.30 : 0.18, { h: 0.15, surf: 'granite-sm', r: o.bullnose ? 0.16 : 0.05, ...o });
export const median = (w = 2.0, o = {}) => E('median', w, { surf: 'grass-mown', ...o });
export const jersey = (w = 0.6, o = {}) => E('barrier', w, { surf: 'concrete-raw', h: 0.84, ...o });
export const sidewalk = (w = 2.4, o = {}) => E('sidewalk', w, { surf: 'paver', h: 0.15, ...o });
export const paving = (w = 3.0, o = {}) => E('plaza', w, { surf: 'paver-herring', h: 0.06, ...o });
export const verge = (w = 1.5, o = {}) => E('verge', w, { surf: 'grass', h: 0.06, ...o });
export const fence = (w = 0.3, o = {}) => E('fence', w, { surf: 'metal', ...o });
export const wall = (w = 0.5, o = {}) => E('wall', w, { surf: 'concrete-raw', h: 2.4, ...o });
export const serviceFront = (w = 3.2, o = {}) => E('lane', w, { surf: 'asphalt', service: true, ...o });
export const guardBlock = (w = 0.5, o = {}) => E('guard', w, { surf: 'concrete-raw', h: 0.55, ...o });
export const path = (w = 3.0, o = {}) => E('path', w, { surf: 'gravel', share: true, ...o });
export const railTrack = (w = 4.2, o = {}) => E('rail', w, { gauge: 1.435, surf: 'ballast', ...o });
export const tramTrack = (w = 2.6, o = {}) => E('tram', w, { gauge: 1.435, surf: 'paver', ...o });
export const tunnelWall = (w = 0.8, o = {}) => E('tunnel', w, { surf: 'tile-white', ...o });
export const water = (w = 6, o = {}) => E('water', w, { surf: 'water', ...o });

/** mirror a carriageway list to build a symmetric two-way assembly */
export const mirror = list => list.slice().reverse().map(e => ({
  ...e,
  dir: e.dir === 'fwd' ? 'rev' : e.dir === 'rev' ? 'fwd' : e.dir,
  crown: -e.crown
}));

/* ------------------------------- the catalogue ------------------------------- */
const P = (id, label, group, o = {}) => ({
  id, label, group,
  cls: o.cls || 'street',                 // family key (drives generator + colours)
  speed: o.speed ?? 50,                    // design speed km/h
  crown: o.crown ?? 2.0,                   // % crossfall each side of crown line
  crownAt: o.crownAt ?? 'center',          // 'center' | 'left' | 'right' | 'median'
  elements: o.elements ?? [],
  markings: Object.assign({ center: 'none', lane: 'none', edge: 'none', symbols: false, words: false, arrows: false, crosswalk: 'none', stop: false, rumble: false, parkingStalls: 'none', tactile: false }, o.markings),
  props: Object.assign({ lights: null, bollards: null, trees: null, guards: null, drains: null, meters: null, benches: null, bins: null, signs: null, busStops: false, hydrants: false, crossings: [] }, o.props),
  tags: o.tags ?? [],
  anchor: o.anchor ?? 'center',
  twoWay: o.twoWay ?? null, oneWay: o.oneWay ?? false,
  grade: o.grade ?? 1,                     // tolerance to slope (%)
  minRadius: o.minRadius ?? 60,
  elevation: o.elevation ?? 0,             // deck height (bridges / viaducts)
  structure: o.structure ?? null,          // 'bridge' | 'tunnel' | 'culvert' | 'subway'
});

export const PROFILES = [
  /* ---- Freeway & highway ---- */
  P('fwy-us-3', 'Interstate · 3+3 with shoulder', 'Highway & freeway', {
    cls: 'freeway', speed: 120, minRadius: 230, crown: 2,
    elements: [
      shoulder(1.0, { rumble: true }), laneL(3.7), laneL(3.7), laneL(3.7), jersey(0.9, { h: 1.07 }),
      lane(3.7), lane(3.7), lane(3.7), shoulder(1.0, { rumble: true }), fence(0.3)
    ],
    markings: { center: 'none', lane: 'broken-white', edge: 'solid-white', rumble: true },
    props: { lights: { type: 'twin', spacing: 45 }, guards: { side: 'both', type: 'wire' }, signs: true },
    tags: ['grade-separated', 'access-controlled']
  }),
  P('fwy-2', 'Expressway · 2+2', 'Highway & freeway', {
    cls: 'freeway', speed: 110, minRadius: 180,
    elements: [shoulder(0.9, { rumble: true }), laneL(3.6), laneL(3.6), median(2.4, { barrier: true }), lane(3.6), lane(3.6), shoulder(0.9, { rumble: true })],
    markings: { lane: 'broken-white', edge: 'solid-white', rumble: true },
    props: { guards: { side: 'both', type: 'wire' }, lights: { type: 'twin', spacing: 60 } },
  }),
  P('ramp', 'Connector ramp · single lane', 'Highway & freeway', {
    cls: 'ramp', speed: 60, anchor: 'center', minRadius: 45, oneWay: true,
    elements: [shoulder(0.8), lane(4.0, { surf: 'asphalt-new' }), shoulder(0.8)],
    markings: { edge: 'solid-white', chevron: true },
    props: { guards: { side: 'both', type: 'beam' } },
  }),
  P('uk-motorway', 'UK motorway · 4+4 + hard shoulder', 'Highway & freeway', {
    cls: 'freeway', speed: 112,
    elements: [shoulder(1.0), laneL(3.65), laneL(3.65), laneL(3.65), laneL(3.65), median(3.0, { surf: 'grass' }), lane(3.65), lane(3.65), lane(3.65), lane(3.65), shoulder(1.0)],
    markings: { lane: 'broken-white', edge: 'solid-white', words: true, redRoute: true },
    props: { signs: true, cats: true },
    tags: ['hard shoulder', 'cat\'s eyes']
  }),

  /* ---- Arterials & boulevards ---- */
  P('art-ave-4', 'Avenue · 2+2 with bus & cycle lanes', 'Arterial & boulevard', {
    cls: 'arterial', speed: 60,
    elements: [
      sidewalk(2.6, { trees: true }), curb(), parking(2.3), cycle(1.9), bus(3.2),
      lane(3.4), lane(3.4), median(2.2, { surf: 'grass-mown', curbL: true, curbR: true }),
      lane(3.4), lane(3.4), bus(3.2, { dir: 'rev' }), cycle(1.9), parking(2.3), curb(), sidewalk(2.6, { trees: true })
    ],
    markings: { center: 'double-yellow', lane: 'broken-white', edge: 'solid-white', words: true, arrows: true, crosswalk: 'zebra', stop: true, tactile: true, busLane: true, cycleSymbols: true },
    props: { lights: { type: 'modern', spacing: 34 }, trees: { spacing: 9, side: 'verge' }, benches: true, busStops: true, drains: { spacing: 40 }, meters: true, hydrants: true, bollards: { spacing: 14 } },
  }),
  P('art-boulevard', 'European boulevard · tram + planted median', 'Arterial & boulevard', {
    cls: 'boulevard', speed: 50,
    elements: [
      sidewalk(3.2, { surf: 'paver' }), curb(), parking(2.3), lane(3.2, { surf: 'cobble' }), tramTrack(2.6, { catenary: true }), median(3.6, { surf: 'grass', trees: true, curbL: true, curbR: true }),
      tramTrack(2.6), lane(3.2, { surf: 'cobble' }), parking(2.3), curb(), sidewalk(3.2, { surf: 'paver' })
    ],
    markings: { center: 'none', lane: 'solid-white', edge: 'solid-white', crosswalk: 'ladder', tactile: true, tramHorn: true },
    props: { lights: { type: 'heritage', spacing: 28 }, trees: { spacing: 8, side: 'median', species: 'plane' }, bins: true, benches: true, signs: true },
    tags: ['transit mall', 'cobble', 'catenary masts']
  }),
  P('art-main', 'Downtown main street · 2+2 + turn lane', 'Arterial & boulevard', {
    cls: 'arterial', speed: 50,
    elements: [
      sidewalk(3.4, { surf: 'concrete' }), gutter(0.5), parking(2.4), lane(3.3), lane(3.3), turn(3.3, { turnLane: true }), lane(3.3), lane(3.3), parking(2.4), gutter(0.5), sidewalk(3.4, { surf: 'concrete' })
    ],
    markings: { center: 'double-yellow', lane: 'broken-white', edge: 'solid-white', arrows: true, words: true, crosswalk: 'continental', stop: true, tactile: true },
    props: { lights: { type: 'modern', spacing: 26 }, meters: true, drains: { spacing: 30 }, signals: true, benches: true, trash: true },
  }),
  P('art-highstreet-uk', 'UK high street · two-stage with cycle lane', 'Arterial & boulevard', {
    cls: 'arterial', speed: 48, crownAt: 'right',
    elements: [
      sidewalk(2.2, { surf: 'paver' }), curb(), parking(2.2), lane(3.3), cycle(1.5, { surf: 'asphalt', buffer: 0.5 }),
      lane(3.3, { dir: 'rev' }), curb(0.16), sidewalk(2.0, { surf: 'paver' })
    ],
    markings: { center: 'broken-white', edge: 'solid-yellow', cycleEdge: 'solid-yellow', crosswalk: 'zebra', giveWay: true, tactile: true },
    props: { lights: { type: 'heritage', spacing: 32 }, bollards: { spacing: 10 }, signs: true, busStops: true },
    tags: ['advisory cycle lane', 'double yellow']
  }),
  P('art-arabian', 'Gulf avenue · service roads + planted median', 'Arterial & boulevard', {
    cls: 'arterial', speed: 80, crownAt: 'median',
    elements: [
      ...mirror([verge(2.0), sidewalk(3.0, { surf: 'paver' }), curb(), parking(2.4), lane(3.6), lane(3.6), serviceFront(3.2), wall(0.4, { h: 1.8 })]),
      median(8, { surf: 'paver-herring', trees: true, irrigation: true }),
      verge(2.0), sidewalk(3.0, { surf: 'paver' }), curb(), parking(2.4), lane(3.6), lane(3.6), serviceFront(3.2), wall(0.4, { h: 1.8 })
    ],
    markings: { lane: 'broken-white', edge: 'solid-white', arrows: true, words: true, crosswalk: 'zebra', tactile: true },
    props: { lights: { type: 'twin', spacing: 30 }, trees: { spacing: 7, side: 'median', species: 'palm' }, signs: true },
    tags: ['frontage road', 'palms']
  }),
  P('art-superblock', 'Barcelona superblock · filtered permeability', 'Arterial & boulevard', {
    cls: 'collector', speed: 30,
    elements: [paving(4.0, { surf: 'paver' }), lane(3.3), lane(3.3, { dir: 'rev' }), paving(4.0, { surf: 'paver' })],
    markings: { center: 'broken-white', filter: true, crosswalk: 'zebra' },
    props: { bollards: { spacing: 6, type: 'removable' }, benches: true, trees: { spacing: 12, side: 'both' }, play: true },
    tags: ['shared space', 'modal filter']
  }),

  /* ---- Urban streets ---- */
  P('str-30', 'Residential collector · 2 lanes + parking', 'Urban street', {
    cls: 'collector', speed: 40,
    elements: [sidewalk(2.0, { surf: 'concrete' }), curb(), parking(2.2), lane(3.2), lane(3.2, { dir: 'rev' }), curb(), sidewalk(2.0, { surf: 'concrete' })],
    markings: { center: 'none', edge: 'none', crosswalk: 'zebra', tactile: true, speedCushion: true },
    props: { lights: { type: 'modern', spacing: 34 }, trees: { spacing: 11, side: 'verge' }, drains: { spacing: 55 }, signs: true },
  }),
  P('str-local', 'Residential local · undivided 9.4 m', 'Urban street', {
    cls: 'local', speed: 40,
    elements: [sidewalk(1.8), curb(), lane(4.7), lane(4.7, { dir: 'rev', surf: 'asphalt' }), curb(), sidewalk(1.8)],
    markings: { center: 'none', crosswalk: 'none', speedCushion: true },
    props: { lights: { type: 'modern', spacing: 40 }, trees: { spacing: 12, side: 'verge' } },
  }),
  P('str-curb-parking', 'Local with angled parking & cycle lane', 'Urban street', {
    cls: 'local', speed: 40, oneWay: true,
    elements: [sidewalk(2.2), curb(), parking(2.6, { angle: 45 }), lane(3.3), cycle(1.6, { surf: 'asphalt' }), curb(), sidewalk(1.6)],
    markings: { center: 'broken-white', edge: 'solid-white', cycleEdge: 'solid-white', crosswalk: 'zebra', parkingStalls: 'angled', tactile: true },
    props: { lights: { type: 'modern', spacing: 36 }, meters: true, trees: { spacing: 14, side: 'verge' }, bins: true },
  }),
  P('str-woonerf', 'Dutch woonerf · shared surface', 'Urban street', {
    cls: 'shared', speed: 15,
    elements: [paving(5.2, { surf: 'brick' }), lane(3.4, { surf: 'brick', dir: 'both' }), paving(5.2, { surf: 'brick' })],
    markings: { center: 'none', SharedZone: true, crosswalk: 'none', speedTable: true },
    props: { trees: { spacing: 8, side: 'both', species: 'cherry' }, benches: true, planters: true, tableMarkings: true },
    tags: ['living street', 'raised tables', 'parking in bays']
  }),
  P('str-japanese', 'Japanese residential 4 m · no kerb', 'Urban street', {
    cls: 'local', speed: 30, crownAt: 'center',
    elements: [lane(2.0, { surf: 'asphalt-dark' }), lane(2.0, { dir: 'rev', surf: 'asphalt-dark' })],
    markings: { center: 'none', edge: 'white-line', noParking: 'red', crosswalk: 'zebra' },
    props: { mirrors: true, drains: { spacing: 25 }, signs: true },
    tags: ['no curb', 'hazard-avoid', 'manhole covers']
  }),
  P('str-alley', 'Mews / alley · brick paving', 'Urban street', {
    cls: 'service', speed: 20,
    elements: [paving(2.0, { surf: 'brick' }), lane(3.6, { surf: 'brick', dir: 'both' }), paving(2.0, { surf: 'brick' })],
    markings: { center: 'none' },
    props: { lights: { type: 'wall', spacing: 18 }, bins: true, bollards: { spacing: 8 } },
  }),
  P('str-industrial', 'Industrial estate road · wide + HGV', 'Urban street', {
    cls: 'industrial', speed: 50,
    elements: [verge(1.4), curb(), lane(3.7), lane(3.7, { dir: 'rev' }), curb(), verge(1.4)],
    markings: { center: 'broken-yellow', edge: 'solid-white', crosswalk: 'zebra', hgv: true },
    props: { lights: { type: 'twin', spacing: 45 }, guards: { side: 'both', type: 'beam' }, signs: true },
  }),
  P('str-campus', 'Campus / parkway · stone edging', 'Urban street', {
    cls: 'park', speed: 30,
    elements: [verge(3.0, { trees: true }), curb({ bullnose: true, surf: 'granite-sm' }), lane(3.2), lane(3.2, { dir: 'rev' }), curb({ bullnose: true }), verge(3.0, { trees: true })],
    markings: { center: 'double-yellow', crosswalk: 'zebra' },
    props: { lights: { type: 'heritage', spacing: 22 }, trees: { spacing: 8, side: 'verge', species: 'oak' }, benches: true, trash: true },
  }),

  /* ---- Cycle & bus priority ---- */
  P('cyc-superhighway', 'Cycle superhighway · 2-way red asphalt', 'Cycle & bus', {
    cls: 'cycle', speed: 25, crownAt: 'center',
    elements: [gutter(0.4), cycle(2.0, { surf: 'asphalt-red' }), cycle(2.0, { surf: 'asphalt-red', dir: 'rev' }), gutter(0.4)],
    markings: { center: 'broken-white', edge: 'solid-white', cycleSymbols: true, numbers: true },
    props: { lights: { type: 'cycle', spacing: 28 }, bollards: { spacing: 18, type: 'post' }, signage: true, sensors: true },
    tags: ['grade-separated at junctions']
  }),
  P('cyc-path', 'Shared-use path · asphalt', 'Cycle & bus', {
    cls: 'path', speed: 20,
    elements: [cycle(1.6), cycle(1.6, { dir: 'rev' })],
    markings: { center: 'broken-white', pedestrianSymbol: true, cycleSymbols: true },
    props: { trees: { spacing: 16, side: 'both' }, bollards: { spacing: 22, type: 'post' } },
  }),
  P('cyc-bridleway', 'Bridleway / rural permissive path', 'Cycle & bus', {
    cls: 'path', speed: 12,
    elements: [{ ...E('path', 3.0), surf: 'dirt' }],
    markings: { center: 'none' },
    props: { signs: true, gates: true, hoof: true },
  }),
  P('bus-brt', 'BRT corridor · busway + centre platforms', 'Cycle & bus', {
    cls: 'bus', speed: 60, crownAt: 'median',
    elements: [
      sidewalk(2.2), curb(), parking(2.2), lane(3.3), bus(3.6, { surf: 'asphalt-blue' }),
      median(3.4, { surf: 'concrete-raw', platform: true }),
      bus(3.6, { surf: 'asphalt-blue' }), lane(3.3), parking(2.2), curb(), sidewalk(2.2)
    ],
    markings: { edge: 'solid-white', busLane: true, crosswalk: 'zebra', tactile: true },
    props: { busStops: { type: 'island', spacing: 700 }, lights: { type: 'modern', spacing: 30 }, platformScreen: true },
    tags: ['off-board fare', 'level boarding']
  }),
  P('bus-lane-only', 'Bus lane · peak hours', 'Cycle & bus', {
    cls: 'bus', speed: 50, oneWay: true,
    elements: [sidewalk(2.2), curb(), parking(2.2), bus(3.4), lane(3.35), lane(3.35), curb(), sidewalk(2.2)],
    markings: { busLane: true, words: true, center: 'broken-white', edge: 'solid-white' },
    props: { busStops: { spacing: 400 }, cameras: true },
  }),

  /* ---- Pedestrian & paving ---- */
  P('ped-street', 'Pedestrian street · paved shared space', 'Pedestrian & paving', {
    cls: 'pedestrian', speed: 10,
    elements: [paving(6.0, { surf: 'paver-herring' }), paving(5.0, { surf: 'paver', inlay: true }), paving(6.0, { surf: 'paver-herring' })],
    markings: { none: true, inlayPattern: true },
    props: { bollards: { spacing: 9, type: 'retractable' }, trees: { spacing: 10, side: 'both', species: 'olive' }, benches: true, bins: true, lights: { type: 'sphere', spacing: 16 }, kiosks: true, paving: true },
    tags: ['vehicle barrier', 'street furniture']
  }),
  P('ped-plaza', 'Civic plaza · radial paving', 'Pedestrian & paving', {
    cls: 'plaza', speed: 5,
    elements: [paving(14.0, { surf: 'paver-herring', radial: true }), paving(6.0, { surf: 'granite-sm', waterFeature: true }), paving(14.0, { surf: 'paver-herring', radial: true })],
    markings: { none: true },
    props: { trees: { spacing: 14, side: 'both' }, benches: true, lights: { type: 'bollard', spacing: 8 }, flags: true, steps: true },
  }),
  P('ped-boardwalk', 'Timber boardwalk over dunes', 'Pedestrian & paving', {
    cls: 'path', speed: 8,
    elements: [cycle(3.0, { surf: 'metal' })],
    markings: { center: 'none' },
    props: { guards: { side: 'both', type: 'timber' }, lights: { type: 'bollard', spacing: 6 }, benches: true },
    tags: ['timber decking', 'handrail']
  }),
  P('park-path', 'Park path · gravel with kerb pins', 'Pedestrian & paving', {
    cls: 'path', speed: 15,
    elements: [{ ...E('path', 3.4), surf: 'gravel' }],
    markings: { center: 'none' },
    props: { bollards: { spacing: 26, type: 'pin' }, trees: { spacing: 12, side: 'both' }, benches: true, lights: { type: 'bollard', spacing: 20 } },
  }),
  P('lot-surface', 'Surface car park · 90° stalls', 'Pedestrian & paving', {
    cls: 'parking-lot', speed: 15,
    elements: [shoulder(0.6), parking(5.5, { angle: 90, bay: true }), lane(6.0, { dir: 'both' }), parking(5.5, { angle: 90, bay: true }), shoulder(0.6)],
    markings: { parkingStalls: 'perpendicular', disabled: true, arrows: true, edge: 'none', centre: 'none', accessAisle: true },
    props: { lights: { type: 'modern', spacing: 30 }, bumpers: true, bins: true, trees: { spacing: 16, side: 'island' } },
  }),
  P('lot-angled', 'Pay car park · angled bays + barrier', 'Pedestrian & paving', {
    cls: 'parking-lot', speed: 15,
    elements: [paving(1.2, { surf: 'concrete' }), parking(5.2, { angle: 60, payBays: true }), lane(6.4), curb(), sidewalk(1.4)],
    markings: { parkingStalls: 'angled', disabled: true, hatch: true, arrows: true },
    props: { meters: true, barrier: true, lights: { type: 'modern', spacing: 22 }, signage: true },
  }),

  /* ---- Rail & transit ---- */
  P('rail-main', 'Mainline railway · ballasted, 2 tracks', 'Rail & transit', {
    cls: 'rail', speed: 160, structure: null,
    elements: [fence(0.3), verge(1.6), railTrack(4.4), median(1.6, { surf: 'ballast' }), railTrack(4.4), verge(1.6), fence(0.3)],
    markings: { none: true },
    props: { catenary: true, crossingProtection: 'half-barrier', telegraph: true, mileposts: true },
    tags: ['level crossings', '454 mm ballast depth']
  }),
  P('rail-light', 'Light rail in street · grooved rails in paving', 'Rail & transit', {
    cls: 'tram', speed: 50,
    elements: [sidewalk(2.0), curb(), lane(3.2, { surf: 'paver' }), tramTrack(2.6, { greenTrack: true }), median(1.0, { surf: 'grass-mown' }), tramTrack(2.6, { greenTrack: true }), lane(3.2, { surf: 'paver', dir: 'rev' }), curb(), sidewalk(2.0)],
    markings: { edge: 'solid-white', crosswalk: 'ladder', tramHorn: true },
    props: { catenary: true, platforms: true, lights: { type: 'modern', spacing: 30 } },
    tags: ['embedded track', 'platform level boarding']
  }),
  P('subway-line', 'Metro · twin-bore tunnel + stations', 'Rail & transit', {
    cls: 'subway', speed: 80, elevation: -16, structure: 'subway',
    elements: [E('tunnel-bore', 12, { surf: 'tile-white', gauge: 1.435 })],
    markings: { none: true },
    props: { stations: { spacing: 750 }, entrances: true, vents: true, tunnels: true },
    tags: ['underground', 'station boxes', 'street entrances']
  }),
  P('rail-single', 'Branch line · single track + footpath', 'Rail & transit', {
    cls: 'rail', speed: 90,
    elements: [verge(1.8), railTrack(4.2), fence(0.3), cycle(2.0, { surf: 'gravel' })],
    props: { crossingProtection: 'gates', telegraph: true },
    markings: { none: true },
  }),

  /* ---- Structures ---- */
  P('bridge-road', 'Road bridge · steel parapet deck', 'Structures', {
    cls: 'bridge', speed: 60, structure: 'bridge', elevation: 6.5,
    elements: [paving(1.0, { surf: 'metal' }), lane(3.5, { surf: 'asphalt-dark' }), lane(3.5, { surf: 'asphalt-dark', dir: 'rev' }), paving(1.0, { surf: 'metal' })],
    markings: { center: 'double-yellow', edge: 'solid-white', expansion: true },
    props: { lights: { type: 'bridge', spacing: 20 }, parapet: true, expansionJoints: true, drains: { spacing: 12 }, bearings: true },
    tags: ['piers', 'deck waterproofing', 'expansion joints']
  }),
  P('bridge-viaduct', 'Viaduct / elevated roadway', 'Structures', {
    cls: 'viaduct', speed: 80, structure: 'viaduct', elevation: 12,
    elements: [shoulder(0.8), lane(3.7), lane(3.7), median(1.2, { barrier: true }), lane(3.7), lane(3.7), shoulder(0.8)],
    markings: { lane: 'broken-white', edge: 'solid-white' },
    props: { guards: { side: 'both', type: 'parapet' }, lights: { type: 'twin', spacing: 34 }, piers: { spacing: 34 } },
    tags: ['piers', 'segmental box girder']
  }),
  P('tunnel', 'Road tunnel · tiled bore', 'Structures', {
    cls: 'tunnel', speed: 70, structure: 'tunnel',
    elements: [tunnelWall(0.8), lane(3.6), lane(3.6, { dir: 'rev' }), tunnelWall(0.8)],
    markings: { center: 'double-white', edge: 'solid-white', cats: true },
    props: { lights: { type: 'tunnel', spacing: 12 }, sos: true, signage: true, jetFans: true },
    tags: ['arch lining', 'pedestrian niches']
  }),
  P('underpass', 'Pedestrian underpass · subways', 'Structures', {
    cls: 'underpass', speed: 8, structure: 'underpass', elevation: -3.2,
    elements: [cycle(3.6, { surf: 'tile-white' }), cycle(3.6, { surf: 'concrete' })],
    markings: { center: 'none' },
    props: { lights: { type: 'tunnel', spacing: 8 }, guards: { side: 'both', type: 'rail' }, cctv: true, artPanels: true },
    tags: ['ramps', 'tactile paving']
  }),
  P('cul-de-sac', 'Cul-de-sac · hammerhead', 'Urban street', {
    cls: 'cul-de-sac', speed: 20,
    elements: [sidewalk(2.0), curb(), lane(3.4), lane(3.4, { dir: 'rev' }), curb(), sidewalk(2.0)],
    markings: { none: true, apron: true, hatched: true },
    props: { trees: { spacing: 12, side: 'both' }, lights: { type: 'modern', spacing: 30 } },
    tags: ['turning head', 'restricted parking']
  }),
  P('roundabout', 'Mini-roundabout approach', 'Structures', {
    cls: 'roundabout', speed: 30,
    elements: [sidewalk(1.8), curb(), lane(3.6), lane(3.6, { dir: 'rev' }), curb(), sidewalk(1.8)],
    markings: { center: 'none', giveWay: true, chevrons: true, crosswalk: 'zebra' },
    props: { island: { radius: 3.2, surf: 'granite-sm', dome: true }, yield: true, tactile: true },
    tags: ['truck apron', 'checker markings']
  }),

  /* ---- Rural ---- */
  P('rural-2', 'Rural two-lane · edge lines + guardrail', 'Rural', {
    cls: 'rural', speed: 90, minRadius: 130, crown: 2.5,
    elements: [verge(1.2), lane(3.6), lane(3.6, { dir: 'rev' }), verge(1.2)],
    markings: { center: 'broken-yellow', edge: 'solid-white', rumble: true, noOvertaking: true },
    props: { guards: { side: 'both', type: 'beam' }, reflectorPosts: true, cattle: true, drains: { spacing: 70 } },
    tags: ['7.3 m carriageway', 'farm access']
  }),
  P('rural-lane', 'Country lane · single track + verges', 'Rural', {
    cls: 'rural', speed: 40, minRadius: 40,
    elements: [{ ...E('path', 3.6), surf: 'asphalt-dark' }],
    markings: { none: true, stoneLines: true },
    props: { guards: { side: 'hazard', type: 'stone' }, passingPlaces: true, signs: true },
    tags: ['no centre line', 'stone kerbs', 'passing places']
  }),
  P('rural-mountain', 'Mountain pass · hairpins + drain guards', 'Rural', {
    cls: 'rural', speed: 40, minRadius: 12, grade: 9, crownAt: 'inside',
    elements: [wall(0.4, { h: 0.6, side: 'outside' }), lane(3.4), lane(3.4, { dir: 'rev' }), guardBlock(0.5)],
    markings: { center: 'solid-white', edge: 'none', chevrons: true },
    props: { guards: { side: 'outside', type: 'concrete' }, snowPoles: true, rockfallNetting: true },
    tags: ['superelevation', 'avalanche gallery']
  }),
  P('rural-unsealed', 'Unsealed grid road · gravel', 'Rural', {
    cls: 'gravel', speed: 60,
    elements: [{ ...E('path', 6.0), surf: 'gravel' }],
    markings: { none: true },
    props: { drains: { spacing: 120, culvert: true }, stock: true },
    tags: ['crown + rolling grade', 'slatted cattle grid']
  }),
];

/* ------------------------------- section solving ------------------------------- */
/**
 * Resolve an assembly into laid-out elements with lateral offsets from the
 * road's reference centreline. `anchor:'center'` puts the corridor centre on
 * the line; 'left'/'right' hug the line (used for one-ways and paths).
 */
export function solveSection(profile) {
  const els = profile.elements.filter(e => e.w > 0);
  const total = els.reduce((s, e) => s + e.w, 0);
  const start = profile.anchor === 'left' ? 0 : profile.anchor === 'right' ? -total : -total / 2;
  let x = start;
  const out = [];
  for (const e of els) {
    const surf = SURFACES[e.surf] ? e.surf : (e.k === 'path' ? 'gravel' : 'asphalt');
    out.push({
      ...e, key: e.k, x0: x, x1: x + e.w, cx: x + e.w / 2, w: e.w,
      surf, level: e.h ?? 0,
      crownSign: e.k === 'lane' || e.k === 'bus' || e.k === 'cycle' || e.k === 'parking' || e.k === 'shoulder' || e.k === 'turn' ? 1 : 0,
    });
    x += e.w;
  }
  // crown line: middle of carriageway by default
  const carriage = out.filter(e => ['lane', 'bus', 'turn', 'tram', 'parking'].includes(e.k));
  let crownX = start + total / 2;
  if (profile.crownAt === 'left') crownX = start;
  else if (profile.crownAt === 'right') crownX = start + total;
  else if (profile.crownAt === 'median') { const m = out.find(e => e.k === 'median'); if (m) crownX = m.cx; }
  else if (carriage.length) {
    if (profile.crownAt === 'inside') crownX = start;
    else crownX = (Math.min(...carriage.map(e => e.x0)) + Math.max(...carriage.map(e => e.x1))) / 2;
  }
  // ---- direction of travel: two-way assemblies split at the median / crown line ----
  const hasMed = out.some(e => e.k === 'median' || e.k === 'barrier');
  const travelCount = out.filter(e => ['lane', 'turn', 'bus', 'cycle'].includes(e.k)).length;
  const centreMarked = !!(profile.markings && profile.markings.center && profile.markings.center !== 'none');
  const twoWay = profile.oneWay !== true && (profile.twoWay === true || hasMed || travelCount >= 2 || centreMarked);
  const med = out.find(e => e.k === 'median' || e.k === 'barrier');
  const trav = out.filter(e => ['lane', 'turn', 'bus', 'cycle', 'parking', 'loading'].includes(e.k));
  const carriageMid = trav.length ? (Math.min(...trav.map(e => e.x0)) + Math.max(...trav.map(e => e.x1))) / 2 : start + total / 2;
  const center = twoWay ? (med ? med.cx : carriageMid) : crownX;
  const TRAVEL = ['lane', 'turn', 'bus', 'cycle', 'loading', 'parking', 'path', 'tram'];
  for (const e of out) {
    if (e.dir === 'both') { e.dirNum = 0; continue; }
    if (!TRAVEL.includes(e.k)) { e.dir = undefined; e.dirNum = 0; continue; }
    if (!twoWay) { e.dir = e.dir || 'fwd'; }
    else e.dir = e.cx < center - 1e-6 ? 'rev' : 'fwd';
    e.dirNum = e.dir === 'rev' ? -1 : 1;
  }
  return { elements: out, total, start, end: start + total, crownX, center, twoWay, corridor: total };
}

/** level (metres above carriageway datum) at a lateral offset — crown drives drainage */
export function findElement(section, off) {
  const els = section.elements;
  for (let i = 0; i < els.length; i++) if (off >= els[i].x0 - 1e-6 && off <= els[i].x1 + 1e-6) return els[i];
  return off < els[0].x0 ? els[0] : els[els.length - 1];
}
export const CARRIAGEWAY = ['lane', 'turn', 'bus', 'parking', 'shoulder', 'gutter', 'tram', 'path', 'loading'];
export function levelAt(section, profile, off) {
  const e = findElement(section, off);
  const base = e ? (e.level || 0) : 0;
  const crownPct = (profile.crown ?? 2) / 100;
  const fall = -Math.abs(off - section.crownX) * crownPct;
  return base + (e && CARRIAGEWAY.includes(e.k) ? fall : fall * 0.3);
}

/** Which side a travel lane is driven on — for traffic simulation. */
export function laneDirections(section) {
  return section.elements
    .filter(e => ['lane', 'bus', 'cycle', 'turn', 'tram'].includes(e.k))
    .map(e => ({ x0: e.x0, x1: e.x1, dir: e.dirNum ?? 0, kind: e.k, w: e.w, service: !!e.service }));
}

/** Rules-driven line markings between adjacent elements. */
export function markingBoundaries(section, profile) {
  const out = [];
  const els = section.elements;
  const m = profile.markings || {};
  for (let i = 0; i < els.length - 1; i++) {
    const a = els[i], b = els[i + 1];
    const road = ['lane', 'turn', 'bus', 'parking', 'shoulder', 'tram', 'cycle'];
    if (!road.includes(a.k) || !road.includes(b.k)) continue;
    const sameDir = (a.dirNum ?? 1) === (b.dirNum ?? 1) || a.dir === 'both' || b.dir === 'both';
    const bothTravel = ['lane', 'turn'].includes(a.k) && ['lane', 'turn'].includes(b.k);
    let type = 'none';
    if (bothTravel) {
      if (!sameDir) type = m.center === 'none' ? (m.lane !== 'none' ? m.lane : 'none') : m.center;
      else type = m.lane;
    } else if ((a.k === 'lane' || a.k === 'turn') && (b.k === 'parking' || b.k === 'shoulder' || b.k === 'cycle' || b.k === 'bus' || b.k === 'tram')) type = m.edge === 'none' ? 'solid-white' : m.edge;
    else if ((b.k === 'lane' || b.k === 'turn') && (a.k === 'parking' || a.k === 'shoulder' || a.k === 'cycle' || a.k === 'bus' || a.k === 'tram')) type = m.edge === 'none' ? 'solid-white' : m.edge;
    else if (a.k === 'cycle' && b.k === 'lane') type = m.cycleEdge || 'solid-white';
    else if (a.k === 'lane' && b.k === 'cycle') type = m.cycleEdge || 'solid-white';
    else if (a.k === 'median' || b.k === 'median') type = m.center === 'double-yellow' ? 'double-yellow' : 'double-white';
    if (type && type !== 'none') out.push({ x: b.x0, type });
  }
  // outer edges of the carriageway against kerb/verge/plaza
  const first = els[0], last = els[els.length - 1];
  if (m.edge && m.edge !== 'none') {
    const li = els.findIndex(e => ['lane', 'cycle', 'bus', 'parking', 'tram'].includes(e.k));
    if (li > 0 && els[li - 1] && ['curb', 'verge', 'gutter', 'sidewalk', 'shoulder', 'wall'].includes(els[li - 1].k)) out.push({ x: els[li].x0, type: m.edge });
    const ri = els.length - 1 - els.slice().reverse().findIndex(e => ['lane', 'cycle', 'bus', 'parking', 'tram'].includes(e.k));
    if (ri < els.length - 1 && els[ri] && els[ri + 1] && ['curb', 'verge', 'gutter', 'sidewalk', 'shoulder', 'wall'].includes(els[ri + 1].k)) out.push({ x: els[ri].x1, type: m.edge });
  }
  return out;
}

export const profileById = id => PROFILES.find(p => p.id === id) || PROFILES[0];

export const PROFILE_GROUPS = [...new Set(PROFILES.map(p => p.group))];

/** class → colour used by the "material ID / class" view mode and the minimap */
export const CLASS_COLOR = {
  freeway: '#e8b65f', arterial: '#f0a26a', boulevard: '#d6a078', collector: '#9ecbff', local: '#7fb3a6',
  shared: '#c2b783', service: '#8a8f98', industrial: '#b18f6d', park: '#93c779', cycle: '#74c69d',
  path: '#a3d977', bus: '#ef7390', pedestrian: '#b4a4e1', plaza: '#8fc2c6', 'parking-lot': '#7d8ba0',
  rail: '#8ebce8', tram: '#6fb3c9', subway: '#8487e8', bridge: '#cab281', viaduct: '#d6c07a',
  tunnel: '#a1b3e2', underpass: '#7dc3bf', 'cul-de-sac': '#7fb3a6', roundabout: '#e2b58a',
  rural: '#c9b48a', gravel: '#b7a488', ramp: '#f2c46d'
};

/** quick search for the Construct palette */
export function searchProfiles(q) {
  q = (q || '').trim().toLowerCase();
  if (!q) return PROFILES;
  return PROFILES.filter(p => (p.label + ' ' + p.group + ' ' + p.id + ' ' + p.tags.join(' ') + ' ' + p.cls).toLowerCase().includes(q));
}
