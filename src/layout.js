// ---------------------------------------------------------------------------
// D-DAY "Breakwater" — shared level layout.
// Convention: x runs along the coast (east/west), z runs sea -> wall (south/north).
// Sea is at negative z, the giant Wall sits at z ≈ 150.
// ---------------------------------------------------------------------------

export const WORLD = {
  seaLevel: -1.1,          // starting water height
  seaLevelMax: 1.95,       // water height at full tide (shoreline creeps up the beach)
  tideDuration: 360,       // seconds for the tide to fully rise
  shoreZ: -18,             // approximate shoreline
  wallZ: 150,              // front face of the great wall
  wallHeight: 18,
  wallHalfWidth: 140,
  gateHalfWidth: 8,
  groundMaxX: 150,
  groundMinZ: -60,
  groundMaxZ: 220,
};

export const PLAYER = {
  spawn: { x: 4, z: -6, yaw: Math.PI }, // yaw π = facing the wall (+z)
  eye: 1.68,
  radius: 0.42,
  walk: 4.6,
  sprint: 7.4,
  jump: 5.2,
  gravity: 14.5,
};

export const CAR = {
  spawn: { x: 14, z: -12, yaw: 0.25 },
  maxSpeed: 16.5,
  accel: 9.5,
  brake: 18,
  drag: 1.35,
  steer: 1.55,
  hp: 100,
};

// --- terrain features -------------------------------------------------------

export const MOUNDS = [
  { x: -95, z: 25, r: 12, h: 5.5 },
  { x: -55, z: 45, r: 10, h: 6.0 },
  { x: -112, z: 70, r: 14, h: 7.0 },
  { x: -70, z: 95, r: 11, h: 6.5 },
  { x: -95, z: 130, r: 13, h: 7.0 },
  { x: -35, z: 60, r: 9, h: 5.0 },
  { x: -45, z: 115, r: 10, h: 6.0 },
  { x: -20, z: 38, r: 8, h: 4.4 },
  { x: 95, z: 30, r: 12, h: 5.5 },
  { x: 55, z: 50, r: 10, h: 6.0 },
  { x: 112, z: 75, r: 13, h: 7.0 },
  { x: 75, z: 100, r: 11, h: 6.0 },
  { x: 100, z: 130, r: 12, h: 6.5 },
  { x: 40, z: 120, r: 10, h: 6.0 },
  { x: 15, z: 85, r: 8, h: 4.5 },
  { x: -15, z: 100, r: 8, h: 4.5 },
  { x: 60, z: 22, r: 9, h: 5.0 },
  { x: -62, z: 15, r: 9, h: 5.0 },
  { x: 125, z: 45, r: 10, h: 5.5 },
  { x: -125, z: 48, r: 10, h: 5.5 },
];

// Zigzag trench center-lines (a trench is carved into the terrain along these).
export const TRENCHES = [
  // front fire trench
  {
    width: 2.3, depth: 1.2,
    points: [
      [-108, 122], [-90, 118], [-73, 124], [-56, 118], [-39, 124],
      [-22, 118], [-12, 122],
    ],
  },
  {
    width: 2.3, depth: 1.2,
    points: [
      [12, 122], [22, 118], [39, 124], [56, 118], [73, 124], [90, 118], [108, 122],
    ],
  },
  // support trench
  {
    width: 2.1, depth: 1.1,
    points: [
      [-104, 97], [-86, 93], [-68, 99], [-50, 93], [-32, 99], [-16, 95], [-10, 97],
    ],
  },
  {
    width: 2.1, depth: 1.1,
    points: [
      [10, 97], [18, 93], [36, 99], [54, 93], [72, 99], [90, 95], [104, 99],
    ],
  },
  // communication trenches linking front & support lines
  { width: 1.9, depth: 1.05, points: [[-62, 99], [-58, 110], [-63, 120]] },
  { width: 1.9, depth: 1.05, points: [[62, 99], [58, 110], [63, 120]] },
  { width: 1.9, depth: 1.05, points: [[-24, 96], [-20, 108], [-25, 119]] },
  { width: 1.9, depth: 1.05, points: [[24, 96], [20, 108], [25, 119]] },
];

// Roads / pathways (center-lines with half widths). Main road hits the gate.
export const ROADS = [
  {
    halfWidth: 4.6, main: true,
    points: [[0, -22], [0, 30], [-2, 80], [0, 120], [0, 152]],
  },
  {
    halfWidth: 2.4,
    points: [[-30, -18], [-36, 35], [-52, 90], [-58, 145]],
  },
  {
    halfWidth: 2.4,
    points: [[35, -18], [42, 35], [58, 90], [64, 145]],
  },
  {
    halfWidth: 1.9,
    points: [[-52, 88], [-20, 84], [16, 86], [52, 88]], // lateral path
  },
];

// --- defenses ---------------------------------------------------------------

// Czech hedgehogs (anti-tank barricades) — {x, z, yaw}
export const HEDGEHOGS = (() => {
  const out = [];
  const rows = [
    { z: 32, xs: [-72, -58, -44, 44, 58, 72, 86] },
    { z: 48, xs: [-96, -82, -68, -54, 54, 68, 82, 96] },
    { z: 64, xs: [-88, -74, -60, -46, 46, 60, 74, 88] },
  ];
  for (const row of rows) {
    for (const x of row.xs) {
      out.push({ x: x + (Math.sin(x * 12.9898) * 1.6), z: row.z + (Math.sin(x * 78.233) * 1.8), yaw: Math.sin(x * 3.7) * Math.PI });
    }
  }
  return out;
})();

// Barbed wire entanglement lines (segments along x at fixed z).
export const WIRE_LINES = [
  { z: 70, spans: [[-112, -14], [14, 112]] },
  { z: 88, spans: [[-118, -12], [12, 118]] },
  { z: 112, spans: [[-120, -13], [13, 120]] },
  { z: 138, spans: [[-125, -11], [11, 125]] },
];

// Anti-vehicle "hedge stakes" near the shoreline (tilted logs).
export const STAKE_ROWS = [
  { z: 8, spans: [[-100, -20], [20, 100]] },
  { z: 22, spans: [[-110, -16], [16, 110]] },
];

// Sandbag walls: segments of stacked sandbags {from:[x,z], to:[x,z], rows}
export const SANDBAG_WALLS = [
  { from: [-18, 30], to: [-18, 46], rows: 3 },
  { from: [18, 30], to: [18, 46], rows: 3 },
  { from: [-46, 76], to: [-30, 78], rows: 3 },
  { from: [30, 78], to: [46, 76], rows: 3 },
  { from: [-70, 112], to: [-52, 114], rows: 2 },
  { from: [52, 114], to: [70, 112], rows: 2 },
  { from: [-12, 142], to: [-12, 150], rows: 3 },
  { from: [12, 142], to: [12, 150], rows: 3 },
  { from: [-90, 100], to: [-82, 118], rows: 2 },
  { from: [82, 100], to: [90, 118], rows: 2 },
];

// Tank mine fields (rectangles, mines seeded deterministically).
export const TANK_MINE_FIELDS = [
  { x0: -88, x1: -38, z0: 34, z1: 62, step: 5.2 },
  { x0: 38, x1: 88, z0: 38, z1: 66, step: 5.2 },
  { x0: -26, x1: -12, z0: 100, z1: 132, step: 4.6 },
  { x0: 12, x1: 26, z0: 100, z1: 132, step: 4.6 },
];

// Scatter of anti-personnel mines (these detonate when stepped on).
export const AP_MINE_FIELDS = [
  { x0: -105, x1: -16, z0: 18, z1: 132, count: 26 },
  { x0: 16, x1: 105, z0: 18, z1: 132, count: 26 },
];

// Static tanks (never move): {x, z, yaw, variant: 'intact' | 'wreck'}
export const TANKS = [
  { x: -17, z: 74, yaw: 0.42, variant: 'intact' },
  { x: 27, z: 57, yaw: -0.55, variant: 'wreck' },
  { x: -56, z: 106, yaw: 1.25, variant: 'intact' },
  { x: 66, z: 128, yaw: -2.2, variant: 'wreck' },
  { x: -34, z: 24, yaw: 0.9, variant: 'wreck' },
];

// Defensive bunkers (pillboxes) facing the beach.
// NOTE: bunker geometry faces local -z, so yaw 0 already looks at the sea.
export const BUNKERS = [
  { x: -72, z: 132, yaw: 0, sentry: true },
  { x: -26, z: 134, yaw: 0, sentry: true },
  { x: 26, z: 134, yaw: 0, sentry: true },
  { x: 72, z: 132, yaw: 0, sentry: true },
  // casemates flanking the gate, embedded in the wall
  { x: -34, z: 152, yaw: 0, sentry: true },
  { x: 34, z: 152, yaw: 0, sentry: true },
];

// Sentry towers / lookout posts on top of the wall.
export const TOWERS = [
  { x: -52, z: 152 },
  { x: 0, z: 152 },
  { x: 52, z: 152 },
  { x: -104, z: 152 },
  { x: 104, z: 152 },
];
