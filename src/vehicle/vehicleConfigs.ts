export type VehicleDesignId = 'NOMAD' | 'TITAN' | 'MANTIS' | 'IRONCLAD';

export interface VehicleDesignPreset {
  id: VehicleDesignId;
  keyNumber: number;
  name: string;
  subtitle: string;
  perkLabel: string;

  // Physics parameters (240 Hz 6-DOF Rigid Body)
  massKg: number;
  inertiaDiag: [number, number, number]; // Ixx, Iyy, Izz
  wheelbaseHalfF: number; // front Z (-ve)
  wheelbaseHalfR: number; // rear Z (+ve)
  trackHalfF: number;
  trackHalfR: number;
  suspRestLength: number;
  wheelRadius: number;
  wheelWidth: number;
  springRate: number;
  bumpDamping: number;
  reboundDamping: number;
  antiRollStiffness: number;
  peakTorqueNm: number;
  finalDrive: number;
  gripBias: number;

  // Gameplay Vine & Root Counterplay Traits
  passiveCutterMultiplier: number; // faster chafe/wrench breakout (Mantis RS blade fenders!)
  rootDragResistance: number; // 0..1 reduces speed loss from ground roots (Titan / Ironclad!)
  boostRechargeScale: number;

  // Visual Livery Palette
  primaryHex: number;
  secondaryHex: number;
  cageHex: number;
  shockHex: number;
}

export const VEHICLE_DESIGNS: ReadonlyArray<VehicleDesignPreset> = [
  {
    id: 'NOMAD',
    keyNumber: 1,
    name: 'NOMAD T1+',
    subtitle: 'Rally-Raid Exo Buggy',
    perkLabel: 'BALANCED AGILITY · LONG-TRAVEL',
    massKg: 1620,
    inertiaDiag: [1240, 2850, 2620],
    wheelbaseHalfF: -1.48,
    wheelbaseHalfR: 1.46,
    trackHalfF: 0.98,
    trackHalfR: 1.0,
    suspRestLength: 0.64,
    wheelRadius: 0.46,
    wheelWidth: 0.34,
    springRate: 42000,
    bumpDamping: 4500,
    reboundDamping: 6900,
    antiRollStiffness: 12800,
    peakTorqueNm: 600,
    finalDrive: 6.3,
    gripBias: 1.02,
    passiveCutterMultiplier: 1.15,
    rootDragResistance: 0.2,
    boostRechargeScale: 1.05,
    primaryHex: 0x4b5943, // Matte expedition olive
    secondaryHex: 0xd4832a, // Ochre-amber
    cageHex: 0x2b3033,
    shockHex: 0xc8962e,
  },
  {
    id: 'TITAN',
    keyNumber: 2,
    name: 'TITAN V8',
    subtitle: 'Widebody Baja Trophy Truck',
    perkLabel: '760 Nm BRUTE SNAP TORQUE',
    massKg: 2160,
    inertiaDiag: [1780, 4100, 3820],
    wheelbaseHalfF: -1.66,
    wheelbaseHalfR: 1.62,
    trackHalfF: 1.08,
    trackHalfR: 1.1,
    suspRestLength: 0.72,
    wheelRadius: 0.51,
    wheelWidth: 0.4,
    springRate: 54000,
    bumpDamping: 5600,
    reboundDamping: 8400,
    antiRollStiffness: 16500,
    peakTorqueNm: 765,
    finalDrive: 6.45,
    gripBias: 1.04,
    passiveCutterMultiplier: 1.1,
    rootDragResistance: 0.45,
    boostRechargeScale: 1.0,
    primaryHex: 0x8c2f23, // Weathered crimson-terracotta trophy livery
    secondaryHex: 0xe0b653, // Championship sand-gold
    cageHex: 0x232629,
    shockHex: 0x2c78b8, // King-style anodized blue shocks
  },
  {
    id: 'MANTIS',
    keyNumber: 3,
    name: 'MANTIS RS',
    subtitle: 'Blade-Fender Canopy Interceptor',
    perkLabel: 'SERRATED ROOT/VINE CUTTERS · 1.8x WRENCH',
    massKg: 1260,
    inertiaDiag: [890, 2050, 1920],
    wheelbaseHalfF: -1.42,
    wheelbaseHalfR: 1.38,
    trackHalfF: 1.04,
    trackHalfR: 1.06,
    suspRestLength: 0.58,
    wheelRadius: 0.44,
    wheelWidth: 0.32,
    springRate: 35000,
    bumpDamping: 3800,
    reboundDamping: 5900,
    antiRollStiffness: 11500,
    peakTorqueNm: 545,
    finalDrive: 6.15,
    gripBias: 1.08,
    passiveCutterMultiplier: 1.85,
    rootDragResistance: 0.15,
    boostRechargeScale: 1.35,
    primaryHex: 0xc5beb0, // Raw sand-titanium composite
    secondaryHex: 0x5aa846, // Rainforest viper green
    cageHex: 0x1e2221,
    shockHex: 0xd95a2b,
  },
  {
    id: 'IRONCLAD',
    keyNumber: 4,
    name: 'IRONCLAD 4x4',
    subtitle: 'Portal-Axle JungleTrial Crawler',
    perkLabel: 'HIGH PORTAL CLEARANCE · MUD/ROOT GRIP',
    massKg: 2380,
    inertiaDiag: [1950, 4350, 4120],
    wheelbaseHalfF: -1.56,
    wheelbaseHalfR: 1.54,
    trackHalfF: 1.04,
    trackHalfR: 1.04,
    suspRestLength: 0.76, // Extra portal-axle ride height clears low ground roots!
    wheelRadius: 0.53,
    wheelWidth: 0.42,
    springRate: 61000,
    bumpDamping: 6300,
    reboundDamping: 9200,
    antiRollStiffness: 18500,
    peakTorqueNm: 810,
    finalDrive: 6.75,
    gripBias: 1.16,
    passiveCutterMultiplier: 1.25,
    rootDragResistance: 0.58,
    boostRechargeScale: 0.95,
    primaryHex: 0x2d3b45, // Tactical slate-iron
    secondaryHex: 0xd69636, // Rescue amber-brass
    cageHex: 0x1c2024,
    shockHex: 0xd69636,
  },
];
