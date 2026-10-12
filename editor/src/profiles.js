// Road cross-section profiles.
// center: lanes straddling the centreline (symmetric, left -> right).
// left / right: lanes listed from the centreline outwards.
// Lane types: car, bus, bike, parking, sidewalk, platform, median, shoulder,
//             grass, dirt, footpath, rail.

import { makeRng } from './geometry.js';

export const PROFILES = {
  avenue: {
    label: 'Avenue (bus + bike + parking)',
    center: [],
    left: [['car', 3.4], ['car', 3.4], ['bus', 3.6], ['bike', 1.8], ['parking', 2.4], ['sidewalk', 3.2]],
    right: [['car', 3.4], ['car', 3.4], ['parking', 2.4], ['bike', 1.8], ['sidewalk', 3.2]],
    markings: 'urban',
    speedBumps: 0.35,
    trees: 0.5,
  },
  boulevard: {
    label: 'Boulevard (median)',
    center: [['median', 4]],
    left: [['car', 3.4], ['car', 3.4], ['parking', 2.4], ['sidewalk', 3.5]],
    right: [['car', 3.4], ['car', 3.4], ['parking', 2.4], ['sidewalk', 3.5]],
    markings: 'urban',
    speedBumps: 0,
    trees: 0.85,
  },
  residential: {
    label: 'Residential street',
    center: [],
    left: [['car', 3.0], ['parking', 2.2], ['sidewalk', 2.4]],
    right: [['car', 3.0], ['parking', 2.2], ['sidewalk', 2.4]],
    markings: 'residential',
    speedBumps: 0.6,
    trees: 0.4,
  },
  cycle_street: {
    label: 'Cycle street',
    center: [],
    left: [['car', 3.2], ['bike', 2.2], ['sidewalk', 3.0]],
    right: [['car', 3.2], ['bike', 2.2], ['sidewalk', 3.0]],
    markings: 'urban',
    speedBumps: 0.5,
    trees: 0.6,
  },
  bus_rapid: {
    label: 'Bus rapid transit',
    center: [['median', 1.5]],
    left: [['bus', 3.6], ['bus', 3.6], ['sidewalk', 3.0]],
    right: [['bus', 3.6], ['bus', 3.6], ['sidewalk', 3.0]],
    markings: 'bus',
    speedBumps: 0,
    trees: 0.3,
  },
  highway: {
    label: 'Highway (divided)',
    center: [['median', 4]],
    left: [['car', 3.7], ['car', 3.7], ['shoulder', 2.5]],
    right: [['car', 3.7], ['car', 3.7], ['shoulder', 2.5]],
    markings: 'highway',
    speedBumps: 0,
    trees: 0,
  },
  country: {
    label: 'Country road',
    center: [],
    left: [['car', 3.0], ['shoulder', 1.0], ['grass', 2.0]],
    right: [['car', 3.0], ['shoulder', 1.0], ['grass', 2.0]],
    markings: 'country',
    speedBumps: 0,
    trees: 0.15,
  },
  offroad: {
    label: 'Offroad track',
    center: [],
    left: [['dirt', 2.4], ['grass', 1.5]],
    right: [['dirt', 2.4], ['grass', 1.5]],
    markings: 'none',
    speedBumps: 0,
    trees: 0,
    jitter: 0.6,
  },
  footpath: {
    label: 'Footpath',
    center: [],
    left: [['footpath', 2.0], ['grass', 1.0]],
    right: [['footpath', 2.0], ['grass', 1.0]],
    markings: 'none',
    speedBumps: 0,
    trees: 0.2,
  },
  railway: {
    label: 'Railway (surface)',
    center: [['rail', 3.0]],
    left: [['grass', 3.0]],
    right: [['grass', 3.0]],
    markings: 'none',
    speedBumps: 0,
    trees: 0.1,
  },
  subway: {
    label: 'Subway (surface line + platforms)',
    center: [['rail', 3.0]],
    left: [['platform', 3.0]],
    right: [['platform', 3.0]],
    markings: 'none',
    speedBumps: 0,
    trees: 0,
  },
};

export const PROFILE_IDS = Object.keys(PROFILES);

// Builds a concrete section for a road from its profile and seed.
// Each road gets its own width jitter so no two roads are identical.
export function buildSection(profileId, seed) {
  const prof = PROFILES[profileId] || PROFILES.residential;
  const rnd = makeRng(seed ^ 0x9e3779b9);
  const jit = (w, amt = 0.08) => w * (1 + (rnd() * 2 - 1) * amt);
  const lanes = [];
  const centerW = prof.center.map(([t, w]) => [t, jit(w, 0.04)]);
  const cw = centerW.reduce((s, [, w]) => s + w, 0);
  let x = -cw / 2;
  for (const [type, ww] of centerW) {
    lanes.push({ type, o0: x, o1: x + ww, side: 'c' });
    x += ww;
  }
  const cwHalf = cw / 2;
  // Side lanes are laid out outward from the centre (left: +o, right: -o).
  const sideBuild = (list, side) => {
    let cur = side === 'L' ? cwHalf : -cwHalf;
    const dir = side === 'L' ? 1 : -1;
    // Some roads drop a sidewalk on one side (replaced by grass) for variety.
    const dropSidewalk = rnd() < 0.18;
    for (const [type0, w0] of list) {
      let type = type0;
      if (dropSidewalk && type === 'sidewalk' && rnd() < 0.5) type = 'grass';
      const w = jit(w0, type === 'sidewalk' || type === 'platform' ? 0.12 : 0.08);
      const o0 = cur;
      const o1 = cur + dir * w;
      lanes.push({ type, o0: Math.min(o0, o1), o1: Math.max(o0, o1), side });
      cur = o1;
    }
    return cur;
  };
  const leftEnd = sideBuild(prof.left, 'L');
  const rightEnd = sideBuild(prof.right, 'R');
  const hwL = leftEnd;
  const hwR = -rightEnd;
  const carriage = lanes.filter((l) => ['car', 'bus', 'bike', 'parking', 'shoulder', 'dirt'].includes(l.type));
  const carMin = carriage.length ? Math.min(...carriage.map((l) => l.o0)) : 0;
  const carMax = carriage.length ? Math.max(...carriage.map((l) => l.o1)) : 0;
  return {
    profile: profileId,
    label: prof.label,
    lanes,
    hwL,
    hwR,
    cw,
    carMin,
    carMax,
    markings: prof.markings,
    speedBumps: prof.speedBumps * (0.5 + rnd()),
    trees: prof.trees * rnd(),
    bollards: rnd() < 0.3,
    asphaltTint: rnd(),
    stallSpacing: 5.2 + rnd() * 1.8,
    dashLen: 2.5 + rnd() * 2.5,
    dashGap: 6 + rnd() * 6,
    jitter: prof.jitter || 0,
    hasRail: lanes.some((l) => l.type === 'rail'),
  };
}
