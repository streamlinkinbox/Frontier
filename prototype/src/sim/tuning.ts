/**
 * Authoritative tuning values. These mirror the tables in docs/01-vine-system.md
 * and docs/02-vehicle-and-track.md — if you change a number here, change it there.
 */

export type SpeciesId = 'tendril' | 'anchor' | 'snare' | 'claw' | 'dragline' | 'liana';

export interface SpeciesSpec {
  id: SpeciesId;
  label: string;
  /** Mass of the whole vine (kg). */
  mass: number;
  /** Natural length grown from the canopy root (m). */
  length: number;
  /** Spring stiffness of the stem (N/m). */
  stiffness: number;
  /** Axial damping (N*s/m). */
  damping: number;
  /** Tension at which the vine fails (N). */
  maxTension: number;
  /** Friction coefficient of the pad against the body (grip mode). */
  grip: number;
  /** How fast the tip extends toward its target (m/s). */
  extensionSpeed: number;
  /** How far ahead the vine aims (s). */
  leadTime: number;
  /** Radius of the tip sphere used for contact detection (m). */
  tipRadius: number;
  /** How fast the contact point migrates toward the body centre (m/s). */
  creep: number;
  /** Base radius of the rendered tube (m). */
  tubeRadius: number;
  /** Seconds of blade contact needed for the cutter to sever it. */
  cutTime: number;
  /** Failure behaviour. */
  breakMode: 'snap' | 'shear' | 'release';
  /** Tractive force scale applied while latched (fraction of nominal grip). */
  tractionLoss: number;
  /** Downforce scale applied while latched. */
  downforceLoss: number;
  /** Spawn weights at pressure 0 and 1 (interpolated in between). */
  weight0: number;
  weight1: number;
}

export const SPECIES: Record<SpeciesId, SpeciesSpec> = {
  tendril: {
    id: 'tendril', label: 'Tender Tendril',
    mass: 1.2, length: 3.0, stiffness: 4500, damping: 22, maxTension: 2600,
    grip: 0.55, extensionSpeed: 7.0, leadTime: 0.75, tipRadius: 0.10,
    creep: 0.10, tubeRadius: 0.035, cutTime: 0.30, breakMode: 'snap',
    tractionLoss: 0, downforceLoss: 0, weight0: 1.0, weight1: 0.34,
  },
  anchor: {
    id: 'anchor', label: 'Strangler Anchor',
    mass: 18, length: 6.2, stiffness: 26000, damping: 260, maxTension: 34000,
    grip: 0.85, extensionSpeed: 3.2, leadTime: 0.9, tipRadius: 0.16,
    creep: 0.22, tubeRadius: 0.11, cutTime: 2.4, breakMode: 'shear',
    tractionLoss: 0.15, downforceLoss: 0, weight0: 0.0, weight1: 0.22,
  },
  snare: {
    id: 'snare', label: 'Snare Net',
    mass: 26, length: 4.5, stiffness: 2200, damping: 12, maxTension: 850,
    grip: 0.35, extensionSpeed: 4.8, leadTime: 0.6, tipRadius: 0.10,
    creep: 0.04, tubeRadius: 0.03, cutTime: 0.45, breakMode: 'snap',
    tractionLoss: 0.5, downforceLoss: 0.1, weight0: 0.0, weight1: 0.18,
  },
  claw: {
    id: 'claw', label: "Devil's Claw",
    mass: 0.85, length: 2.2, stiffness: 7000, damping: 45, maxTension: 18000,
    grip: 1.2, extensionSpeed: 6.2, leadTime: 0.55, tipRadius: 0.12,
    creep: 0.06, tubeRadius: 0.045, cutTime: 0.35, breakMode: 'release',
    tractionLoss: 0.05, downforceLoss: 0.25, weight0: 0.0, weight1: 0.16,
  },
  dragline: {
    id: 'dragline', label: 'Dragline',
    mass: 7, length: 12, stiffness: 45000, damping: 320, maxTension: 14000,
    grip: 0.4, extensionSpeed: 2.1, leadTime: 0.4, tipRadius: 0.08,
    creep: 0, tubeRadius: 0.05, cutTime: 0.25, breakMode: 'snap',
    tractionLoss: 0.1, downforceLoss: 0, weight0: 0.15, weight1: 0.06,
  },
  liana: {
    id: 'liana', label: 'Liana Tangle',
    mass: 11, length: 8.5, stiffness: 12000, damping: 140, maxTension: 24000,
    grip: 0.75, extensionSpeed: 2.8, leadTime: 0.8, tipRadius: 0.14,
    creep: 0.16, tubeRadius: 0.08, cutTime: 1.2, breakMode: 'snap',
    tractionLoss: 0.25, downforceLoss: 0.05, weight0: 0.0, weight1: 0.10,
  },
};

/** Vehicle constants — see docs/02-vehicle-and-track.md §1. */
export const VEHICLE = {
  mass: 1450,
  /** Body-frame inertia: x = pitch, y = roll, z = yaw. */
  inertia: { pitch: 620, roll: 560, yaw: 2180 },
  wheelbase: 2.95,
  trackF: 1.68,
  trackR: 1.66,
  cogHeight: 0.52,
  wheelRadius: 0.385,
  wheelInertia: 1.6,
  /** Suspension: rest ray length measured from the CoG. */
  restRay: 0.52,
  travel: 0.22,
  springF: 38000,
  springR: 42000,
  damperBumpF: 2600,
  damperReboundF: 4300,
  damperBumpR: 2900,
  damperReboundR: 4900,
  arbF: 18000,
  arbR: 12000,
  /** Peak steering angle at low speed, and the speed at which the rack softens. */
  maxSteer: 0.593, // 34 deg
  steerSpeedFalloff: 0.62, // rad of rack travel per (m/s) of speed loss
  ackermann: 0.14,
  engineTorquePeak: 480,
  engineTorquePeakRpm: 3400,
  redline: 7200,
  idleRpm: 950,
  gearRatios: [3.0, 1.95, 1.45, 1.14, 0.93, 0.78],
  reverseRatio: -3.1,
  finalDrive: 2.3,
  driveSplitFront: 0.4,
  shiftTime: 0.12,
  shiftUpRpm: 6900,
  shiftDownRpm: 3200,
  brakeTorqueF: 2900,
  brakeTorqueR: 1700,
  handbrakeTorqueR: 4200,
  dragArea: 1.15,
  downforceFront: 0.55,
  downforceRear: 0.95,
  boostTorque: 1.4,
  boostDuration: 1.4,
  boostCooldown: 4.0,
  cutterConeAngle: 1.22, // 70 deg total half-angle
  cutterRange: 3.2,
  cutterCooldown: 1.6,
} as const;

/** Tyre coefficients (see docs/02 §1.3). */
export const TYRE = {
  Bx: 9.4,
  Cx: 1.62,
  Ex: 0.97,
  By: 8.1,
  Cy: 1.38,
  Ey: 0.97,
  peakKappa: 0.12,
  peakAlpha: 0.16,
  loadSensitivity: 0.11,
  refLoad: 3800,
} as const;

export const SURFACES: Record<string, { mu: number; roll: number; sink: number; color: number; name: string }> = {
  dirt: { mu: 1.05, roll: 0.02, sink: 0.0, color: 0x6b5638, name: 'Packed dirt' },
  gravel: { mu: 1.25, roll: 0.028, sink: 0.005, color: 0x8a8378, name: 'Dry gravel' },
  mud: { mu: 0.72, roll: 0.045, sink: 0.018, color: 0x40301c, name: 'Wet mud' },
  leaves: { mu: 0.85, roll: 0.035, sink: 0.008, color: 0x4d5a2a, name: 'Leaf litter' },
  water: { mu: 0.55, roll: 0.09, sink: 0.04, color: 0x2c3b3a, name: 'Standing water' },
  rock: { mu: 1.4, roll: 0.012, sink: 0.0, color: 0x6d6a63, name: 'Rock slab' },
  brush: { mu: 0.55, roll: 0.12, sink: 0.05, color: 0x2f3a22, name: 'Undergrowth' },
};

/** Difficulty ramp — see docs/00 §3.1 and docs/01 §10.2. */
export function canopyPressure(distance: number): number {
  const d = Math.max(0, distance);
  if (d < 400) return lerp01(d / 400) * 0.12;
  if (d < 900) return 0.12 + lerp01((d - 400) / 500) * 0.18;
  if (d < 1600) return 0.3 + lerp01((d - 900) / 700) * 0.22;
  if (d < 2500) return 0.52 + lerp01((d - 1600) / 900) * 0.24;
  return 0.76 + lerp01((d - 2500) / 1500) * 0.24;
}

export function lerp01(t: number): number {
  return t < 0 ? 0 : t > 1 ? 1 : t;
}

/** Species mix by pressure. */
export function speciesWeights(pressure: number): { value: SpeciesId; weight: number }[] {
  const out: { value: SpeciesId; weight: number }[] = [];
  for (const key of Object.keys(SPECIES) as SpeciesId[]) {
    const s = SPECIES[key];
    const w = s.weight0 + (s.weight1 - s.weight0) * pressure;
    if (w > 0) out.push({ value: key, weight: w });
  }
  return out;
}

export const BIOME_NAMES: { at: number; name: string }[] = [
  { at: 0, name: 'CANOPY EDGE' },
  { at: 400, name: 'UNDERSTORY' },
  { at: 900, name: 'DEEP CANOPY' },
  { at: 1600, name: 'JUNGLE CORE' },
  { at: 2500, name: 'BLIGHT' },
];

export function biomeName(distance: number): string {
  let name = BIOME_NAMES[0].name;
  for (const b of BIOME_NAMES) if (distance >= b.at) name = b.name;
  return name;
}
