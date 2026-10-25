// Demo scenes. Each builds the static bed/flow data the solver runs on:
// terrain heights, the river channel + flow field (via RiverPath),
// sources/sinks, prop placements and the material preset.
import { RiverPath } from '../fluid/RiverPath';
import { WATER_PRESET, MUD_PRESET, OCEAN_PRESET, type FluidSettings, type FluidSource } from '../fluid/types';
import { VALLEY_PALETTE, MUD_PALETTE, BEACH_PALETTE, type TerrainPalette } from '../engine/Terrain';
import { fbm, smoothstep, clamp, lerp } from '../engine/math';

export interface BedData {
  heights: Float32Array;
  flowX: Float32Array;
  flowZ: Float32Array;
  channel: Float32Array;
  sources: FluidSource[];
}

export interface PropSpawn { kind: 'crate' | 'boat'; x: number; z: number; yaw: number }

export interface SceneDef {
  id: string;
  name: string;
  blurb: string;
  preset: FluidSettings;
  palette: TerrainPalette;
  boundaryMode: 0 | 1;
  edgeWaveAmp: number;
  edgeWaveFreq: number;
  fogDensity: number;
  vehicleSpawn: { x: number; z: number; yaw: number };
  cameraStart: { x: number; z: number; yaw: number; pitch: number; dist: number };
  props: PropSpawn[];
  build: (res: number) => BedData;
}

function arrays(res: number) {
  return {
    heights: new Float32Array(res * res),
    flowX: new Float32Array(res * res),
    flowZ: new Float32Array(res * res),
    channel: new Float32Array(res * res),
  };
}

function forEachCell(
  res: number, worldSize: number,
  fn: (x: number, z: number, idx: number) => void,
) {
  for (let j = 0; j < res; j++) {
    for (let i = 0; i < res; i++) {
      const x = ((i + 0.5) / res - 0.5) * worldSize;
      const z = ((j + 0.5) / res - 0.5) * worldSize;
      fn(x, z, j * res + i);
    }
  }
}

/** Flatten a smooth dip toward `target` height (puddle/pit). */
function dip(h: Float32Array, res: number, worldSize: number, cx: number, cz: number, r: number, target: number) {
  forEachCell(res, worldSize, (x, z, idx) => {
    const d = Math.hypot(x - cx, z - cz);
    if (d >= r) return;
    const t = smoothstep(r, r * 0.3, d);
    h[idx] = lerp(h[idx], target, t);
  });
}

// ---------------------------------------------------------------- valley/river
function buildValley(res: number): BedData {
  const size = 120;
  const a = arrays(res);
  forEachCell(res, size, (x, z, idx) => {
    let h = 0.75 + fbm(x * 0.018 + 7, z * 0.018 - 3, 4) * 1.5 + fbm(x * 0.07, z * 0.07, 3) * 0.22;
    const r = Math.hypot(x, z);
    h += smoothstep(38, 60, r) * 5.0;                       // valley walls
    h += smoothstep(10, 55, -x) * 2.0;                      // high ground NW where the river starts
    a.heights[idx] = h;
  });
  // pond basin (SE) the river drains into
  dip(a.heights, res, size, 24, 19, 17, -1.25);
  // puddles along the dirt track (will hold water below level 0)
  dip(a.heights, res, size, -22, -18, 5.5, -0.42);
  dip(a.heights, res, size, -2, -21, 4.5, -0.5);
  dip(a.heights, res, size, 16, -16, 6.5, -0.38);
  dip(a.heights, res, size, 33, -5, 4.2, -0.45);
  dip(a.heights, res, size, -34, 8, 5.0, -0.4);

  const river = new RiverPath(
    [
      { x: -50, z: -44, y: 1.5 },
      { x: -34, z: -30, y: 1.0 },
      { x: -20, z: -12, y: 0.55 },
      { x: -6, z: -1, y: 0.18 },
      { x: 8, z: 7, y: -0.15 },
      { x: 19, z: 14, y: -0.7 },
      { x: 26, z: 20, y: -1.2 },
    ],
    { width: 3.4, depth: 1.15, speed: 1.7, minSlope: 0.006 },
  );
  river.bakeInto(a.heights, a.flowX, a.flowZ, a.channel, res, size, [0, 0]);
  return { ...a, sources: [river.getSource(0.5), river.getSink(-0.35)] };
}

// ------------------------------------------------------------------- mud track
function buildMud(res: number): BedData {
  const size = 120;
  const a = arrays(res);
  forEachCell(res, size, (x, z, idx) => {
    let h = 0.32 + fbm(x * 0.022 + 2, z * 0.022 + 9, 4) * 0.55 + fbm(x * 0.09, z * 0.09, 2) * 0.08;
    const r = Math.hypot(x, z);
    h += smoothstep(42, 60, r) * 3.2; // low berms around the arena
    a.heights[idx] = h;
  });
  // chained mud pits + a big central bog
  dip(a.heights, res, size, 0, 0, 13, -0.55);
  dip(a.heights, res, size, -18, -10, 8, -0.5);
  dip(a.heights, res, size, 17, -14, 9, -0.52);
  dip(a.heights, res, size, -14, 16, 7.5, -0.48);
  dip(a.heights, res, size, 20, 14, 8, -0.5);
  dip(a.heights, res, size, 2, -26, 6, -0.45);
  dip(a.heights, res, size, -30, 4, 6, -0.46);
  // a sluggish ooze channel linking pits (still solver-driven; mud barely flows)
  const ooze = new RiverPath(
    [
      { x: -18, z: -10, y: -0.5 },
      { x: -8, z: -4, y: -0.54 },
      { x: 4, z: -6, y: -0.56 },
      { x: 17, z: -14, y: -0.58 },
    ],
    { width: 2.6, depth: 0.5, speed: 0.12, minSlope: 0.001 },
  );
  ooze.bakeInto(a.heights, a.flowX, a.flowZ, a.channel, res, size, [0, 0]);
  return { ...a, sources: [] };
}

// ----------------------------------------------------------------- ocean beach
function buildOcean(res: number): BedData {
  const size = 420;
  const a = arrays(res);
  forEachCell(res, size, (x, z, idx) => {
    // shore profile along x: deep water west -> beach -> dunes east
    const t = clamp((x + 210) / 420, 0, 1);
    let h = lerp(-10, 9, Math.pow(t, 2.6));
    h += Math.sin(z * 0.016 + 1.7) * 1.1 * smoothstep(-140, 60, x);      // curving shoreline
    h += fbm(x * 0.008, z * 0.008, 4) * (x > 0 ? 2.6 : 0.8);             // dunes / seabed relief
    h += fbm(x * 0.05 + 4, z * 0.05, 3) * 0.25;
    // headland pushing into the sea (south): waves refract + break around it
    const hd = Math.hypot(x + 30, z - 150);
    h += smoothstep(70, 10, hd) * 7.0;
    // rise at the far east edge & north/south rims so the sponge stays calm
    h += smoothstep(150, 205, x) * 4;
    a.heights[idx] = h;
  });
  // tidal pool on the beach (shoreline sits near x=118) + a lagoon bay
  dip(a.heights, res, size, 132, -42, 14, -0.55);
  dip(a.heights, res, size, 95, 45, 20, -0.7);
  return { ...a, sources: [] };
}

export const SCENES: SceneDef[] = [
  {
    id: 'valley',
    name: 'River Valley',
    blurb: 'A spline river winds from the NW hills down to a pond. Puddles dot the dirt track. Drive the truck through the ford, push crates into the current.',
    preset: { ...WATER_PRESET, resolution: 256, worldSize: 120, waterLevel: 0 },
    palette: VALLEY_PALETTE,
    boundaryMode: 0, edgeWaveAmp: 0, edgeWaveFreq: 0.1, fogDensity: 0.0035,
    vehicleSpawn: { x: -30, z: -20, yaw: Math.PI / 2 },
    cameraStart: { x: -6, z: -4, yaw: 0.8, pitch: 0.42, dist: 42 },
    props: [
      { kind: 'crate', x: -16, z: -9, yaw: 0.4 },
      { kind: 'crate', x: -4, z: 0, yaw: 1.1 },
      { kind: 'crate', x: 10, z: 8, yaw: 2.0 },
      { kind: 'crate', x: 20, z: 16, yaw: 0.2 },
      { kind: 'boat', x: 26, z: 22, yaw: -0.7 },
      { kind: 'crate', x: 30, z: 15, yaw: 2.8 },
    ],
    build: buildValley,
  },
  {
    id: 'mud',
    name: 'Mud Track',
    blurb: 'Same solver, mud material: viscosity x50, density 1550 kg/m³, opaque. Wheels churn thick brown spray and the truck bogs down if you stop mid-pit. Try rain — the pits grow.',
    preset: { ...MUD_PRESET, resolution: 256, worldSize: 120, waterLevel: -0.08 },
    palette: MUD_PALETTE,
    boundaryMode: 0, edgeWaveAmp: 0, edgeWaveFreq: 0.1, fogDensity: 0.004,
    vehicleSpawn: { x: -26, z: -22, yaw: Math.PI / 2.4 },
    cameraStart: { x: 0, z: 0, yaw: 0.6, pitch: 0.45, dist: 40 },
    props: [
      { kind: 'crate', x: -4, z: 2, yaw: 0.3 },
      { kind: 'crate', x: 14, z: -12, yaw: 1.4 },
      { kind: 'crate', x: -16, z: 14, yaw: 2.2 },
    ],
    build: buildMud,
  },
  {
    id: 'ocean',
    name: 'Ocean Beach',
    blurb: '512² sim over 420 m. Swell is injected at the west boundary and the shallow-water physics does the rest: waves shoal, steepen and break on the beach and around the headland. Nothing is keyframed.',
    preset: { ...OCEAN_PRESET },
    palette: BEACH_PALETTE,
    boundaryMode: 1, edgeWaveAmp: 0.85, edgeWaveFreq: 0.085, fogDensity: 0.0016,
    vehicleSpawn: { x: 148, z: -10, yaw: -Math.PI / 2 },
    cameraStart: { x: 40, z: 0, yaw: 2.25, pitch: 0.36, dist: 90 },
    props: [
      { kind: 'boat', x: 6, z: -18, yaw: 0.5 },
      { kind: 'crate', x: 18, z: 8, yaw: 0.9 },
      { kind: 'crate', x: 30, z: -30, yaw: 1.8 },
      { kind: 'crate', x: -8, z: 30, yaw: 0.1 },
    ],
    build: buildOcean,
  },
];
