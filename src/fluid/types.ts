// Frontier Fluid — public settings/types for the WebGPU shallow-water solver.
//
// The simulation solves the 2D shallow-water equations (SWE) on a heightfield:
//
//    ∂d/∂t + ∇·(d·v) = sources          (mass conservation)
//    ∂v/∂t + (v·∇)v = -g·∇(b + d) + ν∇²v + friction + forcing   (momentum)
//
// where  d = water depth,  b = terrain height,  v = horizontal velocity.
// Wave speed is physically c = √(g·d): waves genuinely slow down, steepen and
// break as water gets shallow; they reflect off banks, refract over the bed,
// pool in puddles, and flow downhill along a river. Nothing is faked with
// normal-map scrolling — the rendered surface IS the simulated surface.

export interface FluidSettings {
  /** Simulation grid resolution (square). 256 = fast, 512 = high quality. */
  resolution: number;
  /** World size of the simulated square domain, in metres. */
  worldSize: number;
  /** World-space centre of the domain (x, z). */
  center: [number, number];
  /** Solver substeps per rendered frame. More = more stable, faster waves. */
  substeps: number;
  /** Fixed physics timestep (s). CFL requires dt <= dx / sqrt(g * depthMax). */
  timeStep: number;
  gravity: number;
  /** Kinematic viscosity (velocity diffusion). Water ~0.5–2, mud ~40–120. */
  viscosity: number;
  /** Global velocity damping per second (0 = none). */
  damping: number;
  /** Manning-style bed friction coefficient — stronger in shallow water. */
  bedFriction: number;
  /** Rest water level (world Y) used for initial fill + ocean boundary. */
  waterLevel: number;
  /** Wind stress direction (x,z) and strength packed into vector magnitude. */
  wind: [number, number];
  /** Rain rate: depth added per second, with ripple impulses. */
  rainRate: number;
  /** Evaporation: depth removed per second (puddles dry out). */
  evaporation: number;

  // --- particles ---
  maxSplashParticles: number;
  maxFoamParticles: number;
  /** How many splash particles one unit of splash energy emits. */
  splashRate: number;
  foamRate: number;
  /** Foam particle lifetime range in seconds. */
  foamLife: [number, number];

  // --- look ---
  /** RGB absorption (Beer-Lambert) per metre. Mud = huge => opaque. */
  absorption: [number, number, number];
  /** Scatter colour (shallow tint / subsurface glow). */
  scatterColor: [number, number, number];
  /** Deep colour. */
  deepColor: [number, number, number];
  /** Surface roughness for sun glints (water ~0.06, mud ~0.5). */
  roughness: number;
  /** Index of refraction (water 1.333). */
  ior: number;
  /** Density kg/m^3 — used by buoyancy. Mud ~1400–1800. */
  density: number;
  /** Enable the flow-advected micro-detail normal layer (visual only). */
  detailNormals: boolean;
  /** Brand colour for splash particles (mud = brown). */
  splashColor: [number, number, number];
  foamColor: [number, number, number];
}

export const WATER_PRESET: FluidSettings = {
  resolution: 256,
  worldSize: 120,
  center: [0, 0],
  substeps: 3,
  timeStep: 1 / 180,
  gravity: 9.81,
  viscosity: 1.2,
  damping: 0.02,
  bedFriction: 0.15,
  waterLevel: 0,
  wind: [0.6, 0.25],
  rainRate: 0,
  evaporation: 0,
  maxSplashParticles: 48000,
  maxFoamParticles: 64000,
  splashRate: 900,
  foamRate: 1600,
  foamLife: [2.5, 7],
  absorption: [0.32, 0.14, 0.09],
  scatterColor: [0.10, 0.42, 0.42],
  deepColor: [0.015, 0.11, 0.16],
  roughness: 0.07,
  ior: 1.333,
  density: 1000,
  detailNormals: true,
  splashColor: [0.85, 0.93, 0.97],
  foamColor: [0.96, 0.99, 1.0],
};

export const MUD_PRESET: FluidSettings = {
  ...WATER_PRESET,
  viscosity: 65,
  damping: 0.55,
  bedFriction: 1.4,
  wind: [0, 0],
  splashRate: 260,
  foamRate: 90,
  foamLife: [1.0, 2.5],
  absorption: [22, 20, 18], // effectively opaque within centimetres
  scatterColor: [0.16, 0.11, 0.07],
  deepColor: [0.10, 0.07, 0.045],
  roughness: 0.42,
  ior: 1.38,
  density: 1550,
  splashColor: [0.30, 0.22, 0.15],
  foamColor: [0.45, 0.38, 0.29],
};

export const OCEAN_PRESET: FluidSettings = {
  ...WATER_PRESET,
  resolution: 512,
  worldSize: 420,
  substeps: 2,
  timeStep: 1 / 120,
  viscosity: 2.0,
  damping: 0.005,
  wind: [2.2, 0.4],
  absorption: [0.22, 0.09, 0.06],
  scatterColor: [0.08, 0.38, 0.40],
  deepColor: [0.004, 0.07, 0.12],
};

/** One rigid interaction volume pushed into the fluid each frame. */
export interface FluidInteractor {
  /** 0 = sphere, 1 = vertical capsule-ish box column (uses radius as half-extent x/z). */
  kind: 0 | 1;
  position: [number, number, number];
  velocity: [number, number, number];
  radius: number;
  /** Half height for kind=1. */
  halfHeight: number;
  /** Strength multiplier (tyre churning mud = higher). */
  strength: number;
}

/** A continuous inflow/outflow. flow > 0 adds water, flow < 0 drains. */
export interface FluidSource {
  position: [number, number, number];
  radius: number;
  flow: number; // metres of depth per second at centre
  velocity: [number, number]; // horizontal injection velocity
}

export interface WaterSample {
  /** Free-surface height in world Y, or -Infinity when dry. */
  height: number;
  /** Water depth (m). */
  depth: number;
  /** Horizontal surface velocity (m/s). */
  velocity: [number, number];
  /** Surface normal (for orienting floating props). */
  normal: [number, number, number];
}
