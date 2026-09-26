// Hand-designed battlefield layout. +Z = inland (towards the Atlantic Wall), -Z = sea.
// Everything here is authored data; props.js turns it into geometry/colliders.

export const WALL_Z = 1600;          // sea-facing foot of the Atlantic Wall
export const FIELD_HALF = 176;       // hard lateral boundary
export const ROAD_HW = 3.6;          // road half width
export const START = { x: 0, z: 6, yaw: 0 };

// Ground profile along Z: seabed -> beach -> shingle -> bluffs -> plateau -> wall
export const PROFILE = [
  [-600, -16], [-160, -6.5], [-50, -1.9], [0, -0.7], [100, 0.9], [200, 2.3], [290, 3.7],
  [335, 5.0], [365, 8.5], [420, 12.8], [560, 14.6], [760, 17.0], [960, 19.4], [1160, 21.8],
  [1360, 24.6], [1560, 27.4], [1640, 28.0], [1900, 28.5],
];

// Beach exits through the bluff ("draws"). The third one is sealed.
export const DRAWS = [{ x: -74 }, { x: 104 }, { x: -150, sealed: true }];

// Road network (control points, smoothed with Catmull-Rom)
export const ROADS = [
  { name: 'West Draw', pts: [[-70, 282], [-72, 335], [-80, 392], [-62, 450], [-22, 500], [20, 540]] },
  { name: 'East Draw', pts: [[102, 282], [104, 335], [111, 392], [96, 450], [60, 505], [20, 540]] },
  { name: 'Route 1', pts: [[20, 540], [64, 594], [62, 654], [0, 700]] },
  { name: 'West Loop', pts: [[0, 700], [-64, 724], [-114, 780], [-128, 850], [-96, 914], [-26, 948], [34, 972], [60, 1005]] },
  { name: 'Mine Alley', pts: [[0, 700], [48, 744], [92, 794], [110, 864], [96, 934], [60, 1005]] },
  {
    name: 'Wall Road', pts: [[60, 1005], [92, 1060], [80, 1118], [20, 1150], [-50, 1172], [-100, 1222], [-95, 1290],
      [-40, 1322], [40, 1342], [100, 1382], [112, 1440], [62, 1480], [0, 1508], [-32, 1545], [-6, 1578], [0, WALL_Z - 1]],
  },
];

// Huge earth mounds [x, z, radiusX, radiusZ, height, rot]
export const MOUNDS = [
  [0, 800, 26, 22, 10, 0.3], [-32, 862, 20, 24, 13, 0], [36, 846, 22, 18, 9, 0.8], [-4, 916, 18, 14, 7, 0],
  [-150, 560, 26, 20, 10, 0.4], [150, 690, 30, 22, 12, -0.3], [-12, 1212, 26, 20, 11, 0.2], [158, 1182, 30, 26, 12, 0],
  [-150, 1400, 26, 20, 9, 0.5], [-62, 1392, 18, 14, 8, 0], [140, 930, 18, 24, 9, 0.2], [-150, 1060, 22, 30, 11, 0],
  [165, 460, 24, 20, 8, 0], [-10, 610, 14, 12, 5, 0.6], [150, 1300, 20, 16, 8, 0], [-140, 660, 18, 22, 8, 0.1],
  [30, 1450, 14, 10, 5, 0], [-120, 1510, 18, 12, 6, 0.4],
];

// Trench lines (zig-zag generated along these base lines)
export const TRENCHES = [
  { z: 398, x0: -186, x1: 186, amp: 3.2 },
  { z: 612, x0: -186, x1: 186, amp: 3.0 },
  { z: 1076, x0: -186, x1: 186, amp: 3.0 },
  { z: 1252, x0: -186, x1: 186, amp: 3.0 },
  { z: 1466, x0: -186, x1: 186, amp: 2.6 },
  // short communication trenches
  { z: 760, x0: -186, x1: -140, amp: 2 }, { z: 820, x0: 140, x1: 186, amp: 2 },
];

// Bocage hedgerows [[x,z],[x,z]...]
export const HEDGES = [
  [[-172, 480], [-120, 468], [-100, 500]], [[128, 470], [172, 500]], [[-176, 700], [-150, 640]],
  [[170, 620], [120, 650]], [[-60, 1100], [-150, 1120]], [[170, 1080], [130, 1130]], [[-170, 1320], [-120, 1290]],
];

// MG bunkers [x, z]; they auto-face the nearest road / sea
export const BUNKERS = [
  [-112, 374], [-26, 380], [44, 376], [152, 372], [-40, 424], [138, 424],
  [-24, 590], [112, 560], [28, 664],
  [-74, 862], [-162, 902], [142, 828], [-32, 862], [58, 890],
  [30, 1098], [-142, 1180], [-40, 1262], [62, 1300], [152, 1400], [-62, 1446], [44, 1452],
];

// Static tanks [x, z, yaw, type, state]  type: 'sherman' | 'panzer' | 'tiger'; state: 'wreck' | 'dug' | 'intact'
export const TANKS = [
  [36, 140, 0.4, 'sherman', 'wreck'], [-118, 232, -0.7, 'sherman', 'wreck'], [148, 62, 2.6, 'sherman', 'wreck'], [-42, 74, 0.2, 'sherman', 'intact'],
  [-26, 468, 2.8, 'panzer', 'dug'], [96, 636, 3.4, 'panzer', 'dug'], [-150, 770, 2.4, 'panzer', 'intact'], [44, 906, 3.0, 'panzer', 'wreck'],
  [132, 1004, 3.6, 'panzer', 'dug'], [-62, 1206, 2.9, 'tiger', 'intact'], [-88, 1290, 1.2, 'panzer', 'wreck'],
  [72, 1538, 3.1, 'tiger', 'dug'], [-92, 1522, 3.2, 'tiger', 'dug'], [-150, 900, 2.0, 'panzer', 'wreck'],
];

// Wall-top turret emplacements (x positions)
export const WALL_TURRETS = [-150, -92, -40, 40, 92, 150];
export const GATE_HW = 9;

// Checkpoints along the route [x, z, label]
export const CHECKPOINTS = [
  [0, 6, 'Omaha Beach'], [20, 540, 'Crossroads'], [0, 700, 'The Fork'], [60, 1005, 'Junction'], [-40, 1322, 'The Climb'],
];
