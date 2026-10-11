/**
 * Frontier Road Studio — document model.
 *
 * The document is a plain serialisable graph:
 *   nodes  — control points (splines/knots); degree>=3 nodes become junctions
 *   edges  — cubic-Bezier road segments between two nodes
 *   props  — street furniture, signals, signage, billboards, ...
 * Surfaces, markings, junctions, structures, lane graphs and GPU meshes are all
 * *derived* from this data, never authored directly, so editing stays lossless.
 */

import type { V2 } from './math';

export type Id = string;

let idCounter = 0;
export const uid = (prefix: string): string => {
  idCounter += 1;
  return `${prefix}_${Date.now().toString(36).slice(-4)}${(idCounter % 100000).toString(36)}${Math.floor(Math.random() * 1296).toString(36)}`;
};

// ---------------------------------------------------------------- surfaces & materials

export type SurfaceKind =
  | 'asphalt'
  | 'asphalt-worn'
  | 'concrete'
  | 'concrete-panel'
  | 'pavers'
  | 'pavers-mixed'
  | 'slabs'
  | 'cobble'
  | 'tactile'
  | 'gravel'
  | 'soil'
  | 'grass'
  | 'ballast'
  | 'steel'
  | 'paint'
  | 'wood'
  | 'rubber'
  | 'water'
  | 'kerb'
  | 'asphalt-fresh'
  | 'road-plate';

export type PatternKind =
  | 'aggregate'
  | 'asphalt-worn'
  | 'patch'
  | 'crack'
  | 'grime'
  | 'oil'
  | 'tyre-polish'
  | 'paint-wear'
  | 'joints'
  | 'brick-running'
  | 'brick-herringbone'
  | 'brick-basket'
  | 'brick-stretcher'
  | 'slabs'
  | 'cobble'
  | 'tactile-dots'
  | 'tactile-bars'
  | 'gravel-scatter'
  | 'soil-detail'
  | 'grass-tufts'
  | 'ballast-stones'
  | 'concrete-panels'
  | 'rust'
  | 'steel-brushed'
  | 'wood-grain'
  | 'puddles'
  | 'moss'
  | 'snow'
  | 'leaves'
  | 'chalk'
  | 'speed-bump-stripe'
  | 'manhole'
  | 'gutter-stain';

export type BlendMode = 'normal' | 'multiply' | 'add' | 'overlay' | 'replace' | 'subtract';

export interface TextureLayer {
  id: Id;
  name: string;
  pattern: PatternKind;
  blend: BlendMode;
  visible: boolean;
  /** 0..1 */
  opacity: number;
  /** repeats per metre */
  scale: number;
  rotation: number; // radians
  offsetU: number;
  offsetV: number;
  /** height contribution for the derived normal map */
  relief: number;
  contrast: number;
  tintHex: string;
  seed: number;
  /** restrict influence, e.g. by height (patches only in dips) */
  lowCut: number;
  highCut: number;
}

export interface MaterialDef {
  id: Id;
  name: string;
  surface: SurfaceKind;
  layers: TextureLayer[];
  baseColorHex: string;
  roughness: number;
  metalness: number;
  /** world scale, repeats per metre for the base layer */
  texScale: number;
  texRotation: number;
  normalStrength: number;
  /** global extras consumed by the surface shader */
  wetness: number; // 0..1 local extra wetness
  reflectance: number;
  age: number; // 0..1 weathering
  edgeWear: number;
  builtin?: boolean;
}

export const hexToRgb = (hex: string): [number, number, number] => {
  const h = hex.replace('#', '');
  const n = parseInt(h.length === 3 ? h.split('').map((c) => c + c).join('') : h, 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
};

// ---------------------------------------------------------------- road specification

export type RoadClass =
  | 'motorway'
  | 'expressway'
  | 'arterial'
  | 'boulevard'
  | 'avenue'
  | 'street'
  | 'local'
  | 'lane'
  | 'alley'
  | 'service'
  | 'ramp'
  | 'roundabout'
  | 'parking-aisle'
  | 'cycleway'
  | 'footpath'
  | 'rail'
  | 'tram';

export type KerbType = 'none' | 'flush' | 'mountable' | 'low' | 'standard' | 'tall' | 'stone' | 'rolled' | 'timber';
export type MedianType = 'none' | 'painted' | 'raised' | 'grass' | 'barrier';
export type ParkingKind = 'none' | 'parallel' | 'angle-45' | 'angle-60' | 'perpendicular' | 'loading' | 'disabled';

export interface FootwaySpec {
  width: number;
  surface: SurfaceKind;
  materialId?: Id;
  crossfall: number; // % toward kerb
  /** extra strip of different paving next to the kerb (edge course) */
  edgeCourse: boolean;
  plantingWidth: number;
  plantingSurface: SurfaceKind;
}

export interface LaneSideSpec {
  footway: FootwaySpec | null;
  verge: number;
  vergeSurface: SurfaceKind;
  parking: ParkingKind;
  parkingWidth: number;
  bikeLane: number; // width of a marked cycle lane on this side
  bikeLaneSurface: 'paint' | 'raised';
  busLane: number; // width reserved for buses (lane-grade)
  taperOut: number; // extra widening (bus bay, turn pocket) — total added width
}

export interface RoadSpec {
  class: RoadClass;
  /** lanes per direction (direction A = from node a to b) */
  lanesA: number;
  lanesB: number;
  laneWidth: number;
  /** outer lane widening on curves (auto) */
  shoulderA: number;
  shoulderB: number;
  shoulderSurface: SurfaceKind;
  median: MedianType;
  medianWidth: number;
  medianSurface: SurfaceKind;
  kerb: KerbType;
  kerbHeight: number;
  gutterWidth: number;
  crown: number; // crown rise in metres at the centre
  crossfall: number; // % fall from crown to kerb
  superElevationMax: number; // % max banking in curves
  left: LaneSideSpec;
  right: LaneSideSpec;
  carriagewayMaterialId?: Id;
  kerbMaterialId?: Id;
  footwayMaterialId?: Id;
  vergeMaterialId?: Id;
  medianMaterialId?: Id;
  markings: MarkingSpec;
  speedLimit: number; // km/h
  oneWay: boolean;
  /** junction priority: 1 low, 5 motorway — used for give-way rules */
  priority: number;
  /** shape of the pavement edge when the road is elevated (mountain road) */
  retainingWall: boolean;
}

export interface MarkingSpec {
  centreLine: 'none' | 'single' | 'double' | 'dashed' | 'single-yellow' | 'double-yellow';
  laneLines: boolean;
  edgeLines: boolean;
  edgeLineInset: number;
  /** stagger along an edge (lane-change dashes) */
  laneDash: [number, number]; // [paint, gap] metres
  stopBars: boolean;
  giveWay: boolean;
  zebra: boolean; // at authored crossings
  crossDashes: boolean;
  arrows: boolean;
  text: boolean;
  bayLines: boolean;
  hatching: boolean;
  yellowBox: boolean;
  cycleSymbols: boolean;
  railCrossing: boolean;
  reflectors: boolean;
  lineWidth: number;
  markingColor: 'white' | 'yellow';
}

// ---------------------------------------------------------------- graph

export type NodeKind = 'auto' | 'junction' | 'crossing' | 'dead-end' | 'waypoint' | 'roundabout' | 'transition';

export interface RoadNode {
  id: Id;
  name: string;
  x: number;
  z: number;
  /** elevation of the finished surface centre at this node (metres) */
  y: number;
  kind: NodeKind;
  /** force a hard corner (no tangent smoothing across this node) */
  corner: boolean;
  /** tangent smoothing tension 0..1 (0 = straight, 1 = Catmull-Rom-ish) */
  tension: number;
  /** junction overrides */
  junction: JunctionOverride;
  layerId: Id | null;
  locked: boolean;
  tags?: Record<string, string>;
}

export interface JunctionOverride {
  /** corner radius scale, 1 = auto from road class */
  radiusScale: number;
  /** explicit corner radius, -1 = auto */
  radius: number;
  /** minimum angle between arms in degrees (plan constraint) */
  minAngle: number;
  /** corner treatment: kerb return, sharp, chamfer, painted apron */
  corner: 'return' | 'sharp' | 'chamfer' | 'apron';
  chamfer: number;
  /** raised junction table (continuous footway / speed table) */
  table: boolean;
  tableHeight: number;
  /** surface override for the junction area */
  surfaceMaterialId?: Id;
  /** painted apron / yellow box override */
  markingsOverride?: Partial<MarkingSpec>;
  /** turn restrictions: pairs of arm indices that cannot connect [from, to] */
  turnRestrictions: Array<[number, number]>;
  signalised: boolean;
}

export type EdgeFeatureKind =
  | 'crossing-zebra'
  | 'crossing-signal'
  | 'crossing-unsignalised'
  | 'crossing-raised'
  | 'speed-hump'
  | 'speed-cushion'
  | 'speed-table'
  | 'construction-resurface'
  | 'construction-excavation'
  | 'construction-utility'
  | 'construction-closure'
  | 'pothole'
  | 'patch'
  | 'gutter-trench'
  | 'bus-stop'
  | 'bus-bay'
  | 'parking-bay'
  | 'loading-bay'
  | 'driveway'
  | 'field-entry'
  | 'culvert'
  | 'bridge'
  | 'tunnel'
  | 'underpass'
  | 'viaduct'
  | 'retaining-wall'
  | 'guardrail'
  | 'railing'
  | 'fence'
  | 'noise-barrier'
  | 'kerb-extension'
  | 'chicane'
  | 'cattle-grid'
  | 'toll'
  | 'weigh-station'
  | 'gantry'
  | 'tunnel-portal'
  | 'embankment'
  | 'cutting'
  | 'tram-track'
  | 'paint-marking';

export interface EdgeFeature {
  id: Id;
  kind: EdgeFeatureKind;
  /** distance from node a along the edge, metres */
  station: number;
  length: number;
  /** for side features */
  side: 'both' | 'left' | 'right' | 'centre';
  /** lateral offset override (metres, from centreline; NaN = auto) */
  offset: number;
  width: number;
  depth: number;
  height: number;
  variant: number;
  severity: number;
  params: Record<string, number | string | boolean>;
  label: string;
}

export interface Edge {
  id: Id;
  name: string;
  a: Id;
  b: Id;
  spec: RoadSpec;
  features: EdgeFeature[];
  /** manual control-point offset at the `a` end (vector from node a into the edge) */
  ha: V2 | null;
  /** manual control-point offset at the `b` end (vector from node b into the edge) */
  hb: V2 | null;
  /** elevation override per station used by bridges/underpasses (metres above node interpolation) */
  vertical: VerticalProfile | null;
  layerId: Id | null;
  hidden: boolean;
  locked: boolean;
  /** rail metadata used when spec.class === 'rail' */
  rail: RailSpec | null;
  tags?: Record<string, string>;
}

export interface VerticalProfile {
  /** station offsets [s0,s1] and elevation deltas relative to the linear node interpolation */
  keys: Array<{ s: number; dy: number }>;
  /** extra: grade continuity is solved at rebuild */
}

export interface RailSpec {
  gauge: number;
  ballastWidth: number;
  ballastDepth: number;
  sleeperSpacing: number;
  sleeperLength: number;
  electrified: boolean;
  railHeight: number;
  railWeight: number; // kg/m, 54..60
  slabTrack: boolean;
  crossovers: boolean;
  speed: number;
}

// ---------------------------------------------------------------- objects placed in the world

export type PropKind =
  | 'streetlight'
  | 'streetlight-twin'
  | 'streetlight-historic'
  | 'streetlight-catenary'
  | 'signal'
  | 'signal-mast'
  | 'signal-head'
  | 'ped-signal'
  | 'beacon'
  | 'billboard'
  | 'billboard-wall'
  | 'billboard-digital'
  | 'bollard'
  | 'bollard-flexible'
  | 'bollard-illuminated'
  | 'bus-stop'
  | 'bus-shelter'
  | 'bench'
  | 'bin'
  | 'hydrant'
  | 'utility-box'
  | 'cabinet'
  | 'gantry'
  | 'sign'
  | 'sign-chevron'
  | 'camera'
  | 'tree'
  | 'shrub'
  | 'planter'
  | 'fence-panel'
  | 'gate'
  | 'barrier-arm'
  | 'speed-camera'
  | 'sensor'
  | 'mailbox'
  | 'bike-rack'
  | 'phone-box'
  | 'pillar'
  | 'transformer'
  | 'pump';

export interface PropObject {
  id: Id;
  name: string;
  kind: PropKind;
  x: number;
  z: number;
  y: number | null; // null = snap to surface
  rotation: number; // radians about Y
  scale: number;
  variant: number;
  /** attachment that keeps the prop glued to a road edge */
  attach: PropAttachment | null;
  params: Record<string, number | string | boolean>;
  layerId: Id | null;
  hidden: boolean;
  locked: boolean;
  /** custom text for signs/billboards */
  text?: string;
  /** image data URL for billboards (procedural when empty) */
  imageData?: string;
}

export interface PropAttachment {
  edgeId: Id;
  station: number;
  side: 'left' | 'right';
  offset: number;
  followTangent: boolean;
}

export interface PavedArea {
  id: Id;
  name: string;
  kind: 'plaza' | 'parking-lot' | 'yard' | 'driveway' | 'forecourt' | 'median-island' | 'bus-interchange' | 'market' | 'sport-court' | 'plot';
  polygon: V2[];
  surface: SurfaceKind;
  materialId?: Id;
  edgeCourse: boolean;
  kerb: KerbType;
  kerbHeight: number;
  elevation: number;
  markings: 'none' | 'parking-bays' | 'parking-45' | 'sport' | 'hatch';
  baySize: number;
  slope: number;
  layerId: Id | null;
  hidden: boolean;
  locked: boolean;
}

export interface District {
  id: Id;
  name: string;
  /** street grid definition */
  origin: V2;
  columns: number;
  rows: number;
  blockSize: V2;
  streetClass: RoadClass;
  crossClass: RoadClass;
  avenueEvery: number;
  boulevardEvery: number;
  /** plot/apron width around blocks */
  apronWidth: number;
  plotDepth: number;
  plotCountPerBlock: number;
  interiorSurface: SurfaceKind;
  interiorCover: number; // 0..1 how much of the interior is paved
  courtyards: boolean;
  parkingCourts: number;
  culdesacs: boolean;
  layerId: Id | null;
  hidden: boolean;
  locked: boolean;
  seed: number;
}

// ---------------------------------------------------------------- rail traffic

export interface TrainService {
  id: Id;
  name: string;
  railEdgeIds: Id[];
  cars: number;
  carLength: number;
  carWidth: number;
  height: number;
  speed: number; // m/s
  spacing: number; // headway metres between consecutive services on the loop
  colorHex: string;
  accentHex: string;
  livery: 'modern' | 'regional' | 'freight' | 'tram' | 'metro';
  layerId: Id | null;
  hidden: boolean;
  locked: boolean;
}

// ---------------------------------------------------------------- layers

export interface Layer {
  id: Id;
  name: string;
  visible: boolean;
  locked: boolean;
  solo: boolean;
  colorHex: string;
  kind: 'roads' | 'junctions' | 'markings' | 'props' | 'structures' | 'ground' | 'traffic' | 'rail' | 'group';
  parentId: Id | null;
  collapsed: boolean;
  opacity: number;
}

// ---------------------------------------------------------------- environment / render / traffic

export type WeatherKind = 'clear' | 'sunny-haze' | 'cloudy' | 'overcast' | 'rain-light' | 'rain-heavy' | 'storm' | 'fog' | 'snow' | 'dust';

export interface EnvConfig {
  timeOfDay: number; // 0..24 hours
  dayOfYear: number;
  latitude: number;
  timeScale: number; // 0 = static
  weather: WeatherKind;
  cloudCover: number; // 0..1
  cloudHeight: number;
  windSpeed: number;
  windDirection: number; // degrees
  wetness: number; // 0..1 override (auto from weather)
  puddles: number; // 0..1
  fogDensity: number;
  fogHeight: number;
  snowCover: number;
  leavesAmount: number;
  sunIntensity: number;
  ambientIntensity: number;
  exposure: number;
  contrast: number;
  saturation: number;
  shadowStrength: number;
  streetlightsOn: number; // 0 auto, 1 on, 2 off
  nightGlow: number;
  rainOnLens: boolean;
}

export type QualityProfile = 'draft' | 'standard' | 'high' | 'cinematic';

export interface RenderConfig {
  quality: QualityProfile;
  resolutionScale: number;
  msaaSamples: 1 | 2 | 4;
  shadows: boolean;
  shadowMapSize: number;
  cascadeCount: number;
  shadowDistance: number;
  ssao: boolean;
  bloom: boolean;
  bloomStrength: number;
  fxaa: boolean;
  grain: number;
  vignette: number;
  chromatic: number;
  depthOfField: boolean;
  focalDistance: number;
  fStop: number;
  motionBlur: boolean;
  showGizmos: boolean;
  showRoadPlan: boolean;
  showSurfaceFlow: boolean;
  wireframe: boolean;
  bakes: boolean;
  instancedDensity: number;
  drawDistance: number;
  lodBias: number;
}

export interface TrafficConfig {
  enabled: boolean;
  maxVehicles: number; // capped at 25
  density: number; // 0..1 of the cap
  mixCars: number;
  mixVans: number;
  mixTrucks: number;
  mixBuses: number;
  mixEmergency: number;
  speedFactor: number;
  obedience: number; // 0..1 stop at signals / give way
  aggressiveness: number; // headway factor
  rightHandTraffic: boolean;
  headlightsAuto: boolean;
  horns: boolean;
  despawnRadius: number;
  spawnRadius: number;
  pedestrians: number; // count
  cyclists: number;
}

export interface GroundConfig {
  surface: SurfaceKind;
  materialId?: Id;
  elevation: number;
  undulation: number;
  plotSurface: SurfaceKind;
  showGrid: boolean;
  size: number;
}

export interface DocSettings {
  /** road-building defaults used by the drawing tools */
  drawClass: RoadClass;
  snapAngle: number;
  snapGrid: number;
  snapNodeDistance: number;
  mergeNodes: boolean;
  autoSplitCrossings: boolean;
  autoMinAngle: boolean;
  defaultCrossing: EdgeFeatureKind;
  angleConstraint: number; // minimum junction angle in degrees
  unitSystem: 'metric' | 'imperial';
}

export interface RoadDoc {
  version: number;
  name: string;
  nodes: RoadNode[];
  edges: Edge[];
  props: PropObject[];
  areas: PavedArea[];
  districts: District[];
  trains: TrainService[];
  materials: MaterialDef[];
  layers: Layer[];
  env: EnvConfig;
  render: RenderConfig;
  traffic: TrafficConfig;
  ground: GroundConfig;
  settings: DocSettings;
}

// ---------------------------------------------------------------- defaults

export const SURFACE_BASE: Record<SurfaceKind, { color: string; roughness: number; metalness: number; scale: number }> = {
  asphalt: { color: '#3a3b3d', roughness: 0.72, metalness: 0.0, scale: 0.55 },
  'asphalt-worn': { color: '#4a4a4a', roughness: 0.82, metalness: 0.0, scale: 0.45 },
  'asphalt-fresh': { color: '#2b2c2e', roughness: 0.62, metalness: 0.0, scale: 0.6 },
  concrete: { color: '#9a9992', roughness: 0.78, metalness: 0.0, scale: 0.35 },
  'concrete-panel': { color: '#a3a29b', roughness: 0.7, metalness: 0.0, scale: 0.25 },
  pavers: { color: '#7d766c', roughness: 0.72, metalness: 0.0, scale: 0.5 },
  'pavers-mixed': { color: '#8a8074', roughness: 0.74, metalness: 0.0, scale: 0.45 },
  slabs: { color: '#a9a49a', roughness: 0.66, metalness: 0.0, scale: 0.3 },
  cobble: { color: '#6f6a63', roughness: 0.8, metalness: 0.0, scale: 0.7 },
  tactile: { color: '#c9a13a', roughness: 0.62, metalness: 0.0, scale: 1.0 },
  gravel: { color: '#8d8577', roughness: 0.9, metalness: 0.0, scale: 0.4 },
  soil: { color: '#5b4a38', roughness: 0.95, metalness: 0.0, scale: 0.3 },
  grass: { color: '#4a6b32', roughness: 0.9, metalness: 0.0, scale: 0.35 },
  ballast: { color: '#5c584f', roughness: 0.95, metalness: 0.0, scale: 0.8 },
  steel: { color: '#8d9095', roughness: 0.42, metalness: 0.85, scale: 0.5 },
  paint: { color: '#e8e6df', roughness: 0.55, metalness: 0.0, scale: 1.4 },
  wood: { color: '#8a6a45', roughness: 0.8, metalness: 0.0, scale: 0.6 },
  rubber: { color: '#2a2a2c', roughness: 0.85, metalness: 0.0, scale: 1.0 },
  water: { color: '#243038', roughness: 0.06, metalness: 0.0, scale: 0.15 },
  kerb: { color: '#a8a49b', roughness: 0.7, metalness: 0.0, scale: 0.4 },
  'road-plate': { color: '#6b6f74', roughness: 0.35, metalness: 0.9, scale: 0.4 },
};

export const defaultMarkings = (): MarkingSpec => ({
  centreLine: 'double-yellow',
  laneLines: true,
  edgeLines: true,
  edgeLineInset: 0.25,
  laneDash: [3, 6],
  stopBars: true,
  giveWay: true,
  zebra: true,
  crossDashes: true,
  arrows: true,
  text: true,
  bayLines: true,
  hatching: true,
  yellowBox: true,
  cycleSymbols: true,
  railCrossing: true,
  reflectors: true,
  lineWidth: 0.12,
  markingColor: 'white',
});

const sideDefaults = (over: Partial<LaneSideSpec> = {}): LaneSideSpec => ({
  footway: {
    width: 2.4,
    surface: 'concrete',
    crossfall: 2,
    edgeCourse: true,
    plantingWidth: 0,
    plantingSurface: 'soil',
  },
  verge: 0,
  vergeSurface: 'grass',
  parking: 'none',
  parkingWidth: 2.2,
  bikeLane: 0,
  bikeLaneSurface: 'paint',
  busLane: 0,
  taperOut: 0,
  ...over,
});

export const ROAD_CLASS_DEFAULTS: Record<RoadClass, () => RoadSpec> = {
  motorway: () => ({
    class: 'motorway',
    lanesA: 3,
    lanesB: 3,
    laneWidth: 3.65,
    shoulderA: 2.5,
    shoulderB: 2.5,
    shoulderSurface: 'asphalt',
    median: 'barrier',
    medianWidth: 3.2,
    medianSurface: 'concrete',
    kerb: 'flush',
    kerbHeight: 0,
    gutterWidth: 0,
    crown: 0.06,
    crossfall: 2.5,
    superElevationMax: 6,
    left: sideDefaults({
      footway: null,
      verge: 6,
      vergeSurface: 'grass',
    }),
    right: sideDefaults({ footway: null, verge: 6, vergeSurface: 'grass' }),
    markings: { ...defaultMarkings(), centreLine: 'none', zebra: false, crossDashes: false, edgeLines: true, edgeLineInset: 0.4, markingColor: 'white' },
    speedLimit: 110,
    oneWay: false,
    priority: 5,
    retainingWall: true,
  }),
  expressway: () => ({
    class: 'expressway',
    lanesA: 2,
    lanesB: 2,
    laneWidth: 3.5,
    shoulderA: 1.8,
    shoulderB: 1.8,
    shoulderSurface: 'asphalt',
    median: 'barrier',
    medianWidth: 2.4,
    medianSurface: 'concrete',
    kerb: 'flush',
    kerbHeight: 0,
    gutterWidth: 0,
    crown: 0.06,
    crossfall: 2.5,
    superElevationMax: 5,
    left: sideDefaults({ footway: null, verge: 10, vergeSurface: 'grass' }),
    right: sideDefaults({ footway: null, verge: 10, vergeSurface: 'grass' }),
    markings: { ...defaultMarkings(), centreLine: 'none', zebra: false, markingColor: 'white', edgeLineInset: 0.3 },
    speedLimit: 90,
    oneWay: false,
    priority: 5,
    retainingWall: true,
  }),
  arterial: () => ({
    class: 'arterial',
    lanesA: 2,
    lanesB: 2,
    laneWidth: 3.5,
    shoulderA: 0.4,
    shoulderB: 0.4,
    shoulderSurface: 'asphalt',
    median: 'raised',
    medianWidth: 1.8,
    medianSurface: 'concrete',
    kerb: 'standard',
    kerbHeight: 0.15,
    gutterWidth: 0.35,
    crown: 0.08,
    crossfall: 2.2,
    superElevationMax: 4,
    left: sideDefaults({ footway: { width: 3.0, surface: 'concrete', crossfall: 2, edgeCourse: true, plantingWidth: 0, plantingSurface: 'soil' } }),
    right: sideDefaults({ footway: { width: 3.0, surface: 'concrete', crossfall: 2, edgeCourse: true, plantingWidth: 0, plantingSurface: 'soil' } }),
    markings: { ...defaultMarkings(), centreLine: 'double', markingColor: 'white', yellowBox: false },
    speedLimit: 60,
    oneWay: false,
    priority: 4,
    retainingWall: false,
  }),
  boulevard: () => ({
    class: 'boulevard',
    lanesA: 2,
    lanesB: 2,
    laneWidth: 3.35,
    shoulderA: 0.3,
    shoulderB: 0.3,
    shoulderSurface: 'asphalt',
    median: 'grass',
    medianWidth: 5,
    medianSurface: 'grass',
    kerb: 'standard',
    kerbHeight: 0.14,
    gutterWidth: 0.35,
    crown: 0.08,
    crossfall: 2,
    superElevationMax: 3,
    left: sideDefaults({
      footway: { width: 4.5, surface: 'pavers', crossfall: 2, edgeCourse: true, plantingWidth: 1.6, plantingSurface: 'soil' },
      bikeLane: 1.8,
      bikeLaneSurface: 'raised',
    }),
    right: sideDefaults({
      footway: { width: 4.5, surface: 'pavers', crossfall: 2, edgeCourse: true, plantingWidth: 1.6, plantingSurface: 'soil' },
      bikeLane: 1.8,
      bikeLaneSurface: 'raised',
    }),
    markings: { ...defaultMarkings(), centreLine: 'none', markingColor: 'white' },
    speedLimit: 50,
    oneWay: false,
    priority: 4,
    retainingWall: false,
  }),
  avenue: () => ({
    class: 'avenue',
    lanesA: 2,
    lanesB: 2,
    laneWidth: 3.3,
    shoulderA: 0.2,
    shoulderB: 0.2,
    shoulderSurface: 'asphalt',
    median: 'raised',
    medianWidth: 2.4,
    medianSurface: 'concrete',
    kerb: 'standard',
    kerbHeight: 0.15,
    gutterWidth: 0.35,
    crown: 0.08,
    crossfall: 2,
    superElevationMax: 3,
    left: sideDefaults({ footway: { width: 3.6, surface: 'pavers', crossfall: 2, edgeCourse: true, plantingWidth: 1.2, plantingSurface: 'soil' } }),
    right: sideDefaults({ footway: { width: 3.6, surface: 'pavers', crossfall: 2, edgeCourse: true, plantingWidth: 1.2, plantingSurface: 'soil' } }),
    markings: { ...defaultMarkings(), centreLine: 'double', markingColor: 'yellow' },
    speedLimit: 50,
    oneWay: false,
    priority: 4,
    retainingWall: false,
  }),
  street: () => ({
    class: 'street',
    lanesA: 1,
    lanesB: 1,
    laneWidth: 3.2,
    shoulderA: 0,
    shoulderB: 0,
    shoulderSurface: 'asphalt',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'standard',
    kerbHeight: 0.15,
    gutterWidth: 0.3,
    crown: 0.07,
    crossfall: 2.2,
    superElevationMax: 3,
    left: sideDefaults({ parking: 'parallel', parkingWidth: 2.3 }),
    right: sideDefaults({ parking: 'parallel', parkingWidth: 2.3 }),
    markings: { ...defaultMarkings(), centreLine: 'dashed', markingColor: 'white', yellowBox: false },
    speedLimit: 40,
    oneWay: false,
    priority: 3,
    retainingWall: false,
  }),
  local: () => ({
    class: 'local',
    lanesA: 1,
    lanesB: 1,
    laneWidth: 2.9,
    shoulderA: 0,
    shoulderB: 0,
    shoulderSurface: 'asphalt',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'mountable',
    kerbHeight: 0.12,
    gutterWidth: 0.25,
    crown: 0.06,
    crossfall: 2.2,
    superElevationMax: 3,
    left: sideDefaults({ parking: 'parallel', parkingWidth: 2.1, verge: 1.5, vergeSurface: 'grass' }),
    right: sideDefaults({ parking: 'parallel', parkingWidth: 2.1, verge: 1.5, vergeSurface: 'grass' }),
    markings: { ...defaultMarkings(), centreLine: 'none', laneLines: false, markingColor: 'white', yellowBox: false, arrows: false },
    speedLimit: 30,
    oneWay: false,
    priority: 2,
    retainingWall: false,
  }),
  lane: () => ({
    class: 'lane',
    lanesA: 1,
    lanesB: 1,
    laneWidth: 2.7,
    shoulderA: 0.6,
    shoulderB: 0.6,
    shoulderSurface: 'gravel',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'none',
    kerbHeight: 0,
    gutterWidth: 0,
    crown: 0.05,
    crossfall: 3,
    superElevationMax: 2,
    left: sideDefaults({ footway: null, verge: 1.2, vergeSurface: 'grass' }),
    right: sideDefaults({ footway: null, verge: 1.2, vergeSurface: 'grass' }),
    markings: { ...defaultMarkings(), centreLine: 'none', laneLines: false, edgeLines: false, stopBars: false, giveWay: false, zebra: false, crossDashes: false, arrows: false, text: false, bayLines: false, hatching: false, yellowBox: false, cycleSymbols: false, lineWidth: 0.1 },
    speedLimit: 30,
    oneWay: false,
    priority: 1,
    retainingWall: false,
  }),
  alley: () => ({
    class: 'alley',
    lanesA: 1,
    lanesB: 0,
    laneWidth: 3.0,
    shoulderA: 0.2,
    shoulderB: 0.2,
    shoulderSurface: 'concrete',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'flush',
    kerbHeight: 0.05,
    gutterWidth: 0.2,
    crown: 0.04,
    crossfall: 2,
    superElevationMax: 0,
    left: sideDefaults({ footway: { width: 1.0, surface: 'concrete', crossfall: 2, edgeCourse: false, plantingWidth: 0, plantingSurface: 'soil' } }),
    right: sideDefaults({ footway: { width: 1.0, surface: 'concrete', crossfall: 2, edgeCourse: false, plantingWidth: 0, plantingSurface: 'soil' } }),
    markings: { ...defaultMarkings(), centreLine: 'none', laneLines: false, edgeLines: false, stopBars: false, giveWay: false, zebra: false, crossDashes: false, arrows: false, text: false, bayLines: false, hatching: false, yellowBox: false, cycleSymbols: false },
    speedLimit: 15,
    oneWay: true,
    priority: 1,
    retainingWall: false,
  }),
  service: () => ({
    class: 'service',
    lanesA: 1,
    lanesB: 1,
    laneWidth: 3.0,
    shoulderA: 0.5,
    shoulderB: 0.5,
    shoulderSurface: 'asphalt',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'flush',
    kerbHeight: 0.08,
    gutterWidth: 0.25,
    crown: 0.05,
    crossfall: 2,
    superElevationMax: 2,
    left: sideDefaults({ footway: null, verge: 2, vergeSurface: 'gravel' }),
    right: sideDefaults({ footway: null, verge: 2, vergeSurface: 'gravel' }),
    markings: { ...defaultMarkings(), centreLine: 'none', laneLines: false, arrows: false, text: false, yellowBox: false, zebra: false },
    speedLimit: 20,
    oneWay: false,
    priority: 2,
    retainingWall: false,
  }),
  ramp: () => ({
    class: 'ramp',
    lanesA: 1,
    lanesB: 0,
    laneWidth: 3.7,
    shoulderA: 1.5,
    shoulderB: 0.6,
    shoulderSurface: 'asphalt',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'flush',
    kerbHeight: 0,
    gutterWidth: 0,
    crown: 0.06,
    crossfall: 3,
    superElevationMax: 7,
    left: sideDefaults({ footway: null, verge: 4, vergeSurface: 'grass' }),
    right: sideDefaults({ footway: null, verge: 4, vergeSurface: 'grass' }),
    markings: { ...defaultMarkings(), centreLine: 'none', laneLines: false, zebra: false, crossDashes: false, text: false, bayLines: false, hatching: false, yellowBox: false, cycleSymbols: false, stopBars: false, markingColor: 'white' },
    speedLimit: 60,
    oneWay: true,
    priority: 4,
    retainingWall: true,
  }),
  roundabout: () => ({
    class: 'roundabout',
    lanesA: 1,
    lanesB: 1,
    laneWidth: 4.0,
    shoulderA: 0.3,
    shoulderB: 0.3,
    shoulderSurface: 'asphalt',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'standard',
    kerbHeight: 0.14,
    gutterWidth: 0.3,
    crown: 0.05,
    crossfall: 2,
    superElevationMax: 2,
    left: sideDefaults({ footway: null, verge: 1.5, vergeSurface: 'grass' }),
    right: sideDefaults({ footway: null, verge: 1.5, vergeSurface: 'grass' }),
    markings: { ...defaultMarkings(), centreLine: 'none', laneLines: false, zebra: false, arrows: true, crossDashes: false, yellowBox: false },
    speedLimit: 30,
    oneWay: true,
    priority: 3,
    retainingWall: false,
  }),
  'parking-aisle': () => ({
    class: 'parking-aisle',
    lanesA: 1,
    lanesB: 1,
    laneWidth: 5.5,
    shoulderA: 0,
    shoulderB: 0,
    shoulderSurface: 'asphalt',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'flush',
    kerbHeight: 0.05,
    gutterWidth: 0,
    crown: 0.04,
    crossfall: 2,
    superElevationMax: 0,
    left: sideDefaults({ footway: null, verge: 0 }),
    right: sideDefaults({ footway: null, verge: 0 }),
    markings: { ...defaultMarkings(), centreLine: 'none', laneLines: false, stopBars: false, giveWay: false, zebra: false, crossDashes: false, arrows: true, hatching: false, yellowBox: false, bayLines: true },
    speedLimit: 15,
    oneWay: false,
    priority: 1,
    retainingWall: false,
  }),
  cycleway: () => ({
    class: 'cycleway',
    lanesA: 1,
    lanesB: 1,
    laneWidth: 2.0,
    shoulderA: 0.15,
    shoulderB: 0.15,
    shoulderSurface: 'asphalt',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'flush',
    kerbHeight: 0.1,
    gutterWidth: 0.2,
    crown: 0.04,
    crossfall: 2,
    superElevationMax: 2,
    left: sideDefaults({ footway: null, verge: 1.0, vergeSurface: 'grass' }),
    right: sideDefaults({ footway: null, verge: 1.0, vergeSurface: 'grass' }),
    markings: { ...defaultMarkings(), centreLine: 'dashed', laneLines: false, stopBars: false, giveWay: false, zebra: false, crossDashes: false, arrows: false, text: false, hatching: false, yellowBox: false, cycleSymbols: true },
    speedLimit: 25,
    oneWay: false,
    priority: 1,
    retainingWall: false,
  }),
  footpath: () => ({
    class: 'footpath',
    lanesA: 1,
    lanesB: 0,
    laneWidth: 2.2,
    shoulderA: 0.1,
    shoulderB: 0.1,
    shoulderSurface: 'concrete',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'none',
    kerbHeight: 0,
    gutterWidth: 0,
    crown: 0.03,
    crossfall: 2,
    superElevationMax: 0,
    left: sideDefaults({ footway: null, verge: 0.5, vergeSurface: 'soil' }),
    right: sideDefaults({ footway: null, verge: 0.5, vergeSurface: 'soil' }),
    markings: { ...defaultMarkings(), centreLine: 'none', laneLines: false, edgeLines: false, stopBars: false, giveWay: false, zebra: false, crossDashes: false, arrows: false, text: false, bayLines: false, hatching: false, yellowBox: false, cycleSymbols: false },
    speedLimit: 8,
    oneWay: true,
    priority: 1,
    retainingWall: false,
  }),
  rail: () => ({
    class: 'rail',
    lanesA: 1,
    lanesB: 0,
    laneWidth: 1.435,
    shoulderA: 1.6,
    shoulderB: 1.6,
    shoulderSurface: 'ballast',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'gravel',
    kerb: 'none',
    kerbHeight: 0,
    gutterWidth: 0,
    crown: 0.02,
    crossfall: 1,
    superElevationMax: 4,
    left: sideDefaults({ footway: null, verge: 2.5, vergeSurface: 'gravel' }),
    right: sideDefaults({ footway: null, verge: 2.5, vergeSurface: 'gravel' }),
    markings: { ...defaultMarkings(), centreLine: 'none', laneLines: false, edgeLines: false, stopBars: false, giveWay: false, zebra: false, crossDashes: false, arrows: false, text: false, bayLines: false, hatching: false, yellowBox: false, cycleSymbols: false },
    speedLimit: 100,
    oneWay: true,
    priority: 6,
    retainingWall: false,
  }),
  tram: () => ({
    class: 'tram',
    lanesA: 1,
    lanesB: 1,
    laneWidth: 3.2,
    shoulderA: 0.2,
    shoulderB: 0.2,
    shoulderSurface: 'concrete',
    median: 'none',
    medianWidth: 0,
    medianSurface: 'concrete',
    kerb: 'standard',
    kerbHeight: 0.14,
    gutterWidth: 0.3,
    crown: 0.06,
    crossfall: 2,
    superElevationMax: 2,
    left: sideDefaults({}),
    right: sideDefaults({}),
    markings: { ...defaultMarkings(), centreLine: 'none', markingColor: 'white', laneLines: true },
    speedLimit: 40,
    oneWay: false,
    priority: 4,
    retainingWall: false,
  }),
};

export const makeRoadSpec = (cls: RoadClass): RoadSpec => ROAD_CLASS_DEFAULTS[cls]();

export function makeNode(x: number, z: number, y = 0, kind: NodeKind = 'auto'): RoadNode {
  return {
    id: uid('n'),
    name: '',
    x,
    z,
    y,
    kind,
    corner: false,
    tension: 0.45,
    junction: makeJunctionOverride(),
    layerId: null,
    locked: false,
  };
}

export function makeJunctionOverride(): JunctionOverride {
  return {
    radiusScale: 1,
    radius: -1,
    minAngle: 40,
    corner: 'return',
    chamfer: 3,
    table: false,
    tableHeight: 0.1,
    turnRestrictions: [],
    signalised: false,
  };
}

export function makeEdge(a: Id, b: Id, cls: RoadClass): Edge {
  return {
    id: uid('e'),
    name: '',
    a,
    b,
    spec: makeRoadSpec(cls),
    features: [],
    ha: null,
    hb: null,
    vertical: null,
    layerId: null,
    hidden: false,
    locked: false,
    rail: cls === 'rail' ? makeRailSpec() : null,
  };
}

export function makeRailSpec(): RailSpec {
  return {
    gauge: 1.435,
    ballastWidth: 3.6,
    ballastDepth: 0.55,
    sleeperSpacing: 0.6,
    sleeperLength: 2.6,
    electrified: true,
    railHeight: 0.172,
    railWeight: 60,
    slabTrack: false,
    crossovers: false,
    speed: 27,
  };
}

export function makeFeature(kind: EdgeFeatureKind, station: number, length = 3, over: Partial<EdgeFeature> = {}): EdgeFeature {
  return {
    id: uid('f'),
    kind,
    station,
    length,
    side: 'centre',
    offset: NaN,
    width: 0,
    depth: 0,
    height: 0,
    variant: 0,
    severity: 0.5,
    params: {},
    label: '',
    ...over,
  };
}

export function makeProp(kind: PropKind, x: number, z: number, over: Partial<PropObject> = {}): PropObject {
  return {
    id: uid('p'),
    name: '',
    kind,
    x,
    z,
    y: null,
    rotation: 0,
    scale: 1,
    variant: 0,
    attach: null,
    params: {},
    layerId: null,
    hidden: false,
    locked: false,
    ...over,
  };
}

export function makeArea(kind: PavedArea['kind'], polygon: V2[], over: Partial<PavedArea> = {}): PavedArea {
  return {
    id: uid('a'),
    name: '',
    kind,
    polygon,
    surface: kind === 'parking-lot' ? 'asphalt' : 'pavers',
    edgeCourse: true,
    kerb: 'standard',
    kerbHeight: 0.14,
    elevation: 0.14,
    markings: kind === 'parking-lot' ? 'parking-bays' : 'none',
    baySize: 2.5,
    slope: 1.5,
    layerId: null,
    hidden: false,
    locked: false,
    ...over,
  };
}

export function makeDistrict(origin: V2, over: Partial<District> = {}): District {
  return {
    id: uid('d'),
    name: '',
    origin,
    columns: 5,
    rows: 4,
    blockSize: [110, 90],
    streetClass: 'street',
    crossClass: 'local',
    avenueEvery: 3,
    boulevardEvery: 0,
    apronWidth: 8,
    plotDepth: 22,
    plotCountPerBlock: 6,
    interiorSurface: 'soil',
    interiorCover: 0.35,
    courtyards: true,
    parkingCourts: 2,
    culdesacs: false,
    layerId: null,
    hidden: false,
    locked: false,
    seed: 1234,
    ...over,
  };
}

// ---------------------------------------------------------------- materials

export function makeTextureLayer(pattern: PatternKind, over: Partial<TextureLayer> = {}): TextureLayer {
  return {
    id: uid('tl'),
    name: pattern,
    pattern,
    blend: 'normal',
    visible: true,
    opacity: 1,
    scale: 1,
    rotation: 0,
    offsetU: 0,
    offsetV: 0,
    relief: 0.5,
    contrast: 1,
    tintHex: '#ffffff',
    seed: Math.floor(Math.random() * 65535),
    lowCut: 0,
    highCut: 1,
    ...over,
  };
}

export function makeMaterial(surface: SurfaceKind, over: Partial<MaterialDef> = {}): MaterialDef {
  const base = SURFACE_BASE[surface];
  const layerFor = (): TextureLayer[] => {
    switch (surface) {
      case 'asphalt':
        return [
          makeTextureLayer('aggregate', { scale: 1.4, relief: 0.55, contrast: 1.05 }),
          makeTextureLayer('asphalt-worn', { scale: 0.12, opacity: 0.85, relief: 0.05, blend: 'multiply' }),
          makeTextureLayer('patch', { scale: 0.05, opacity: 0.7, blend: 'multiply', relief: 0.2 }),
          makeTextureLayer('grime', { scale: 0.03, opacity: 0.45, blend: 'multiply', relief: 0.05 }),
        ];
      case 'asphalt-worn':
        return [
          makeTextureLayer('aggregate', { scale: 1.2, relief: 0.5 }),
          makeTextureLayer('patch', { scale: 0.08, opacity: 0.85, blend: 'multiply', relief: 0.25 }),
          makeTextureLayer('crack', { scale: 0.06, opacity: 0.55, blend: 'multiply', relief: 0.6 }),
          makeTextureLayer('oil', { scale: 0.05, opacity: 0.3, blend: 'multiply' }),
          makeTextureLayer('grime', { scale: 0.02, opacity: 0.5, blend: 'multiply' }),
        ];
      case 'asphalt-fresh':
        return [
          makeTextureLayer('aggregate', { scale: 1.8, relief: 0.35, contrast: 0.9 }),
          makeTextureLayer('tyre-polish', { scale: 0.04, opacity: 0.35, blend: 'multiply' }),
        ];
      case 'concrete':
      case 'concrete-panel':
        return [
          makeTextureLayer('joints', { scale: 0.25, relief: 0.85 }),
          makeTextureLayer('concrete-panels', { scale: 0.1, opacity: 0.5, blend: 'overlay', relief: 0.2 }),
          makeTextureLayer('grime', { scale: 0.05, opacity: 0.35, blend: 'multiply' }),
          makeTextureLayer('moss', { scale: 0.03, opacity: 0.2, blend: 'overlay' }),
        ];
      case 'pavers':
        return [
          makeTextureLayer('brick-running', { scale: 0.5, relief: 1.0 }),
          makeTextureLayer('grime', { scale: 0.04, opacity: 0.4, blend: 'multiply' }),
          makeTextureLayer('moss', { scale: 0.03, opacity: 0.22, blend: 'overlay' }),
        ];
      case 'pavers-mixed':
        return [
          makeTextureLayer('brick-herringbone', { scale: 0.45, relief: 1.0 }),
          makeTextureLayer('grime', { scale: 0.04, opacity: 0.4, blend: 'multiply' }),
        ];
      case 'slabs':
        return [
          makeTextureLayer('slabs', { scale: 0.28, relief: 0.9 }),
          makeTextureLayer('grime', { scale: 0.04, opacity: 0.35, blend: 'multiply' }),
          makeTextureLayer('moss', { scale: 0.03, opacity: 0.18, blend: 'overlay' }),
        ];
      case 'cobble':
        return [
          makeTextureLayer('cobble', { scale: 0.55, relief: 1.2 }),
          makeTextureLayer('grime', { scale: 0.05, opacity: 0.45, blend: 'multiply' }),
        ];
      case 'tactile':
        return [makeTextureLayer('tactile-dots', { scale: 1.0, relief: 0.7 }), makeTextureLayer('grime', { scale: 0.1, opacity: 0.25, blend: 'multiply' })];
      case 'gravel':
        return [makeTextureLayer('gravel-scatter', { scale: 0.9, relief: 0.9 }), makeTextureLayer('grime', { scale: 0.06, opacity: 0.3, blend: 'multiply' })];
      case 'soil':
        return [makeTextureLayer('soil-detail', { scale: 0.5, relief: 0.8 }), makeTextureLayer('grime', { scale: 0.08, opacity: 0.3, blend: 'multiply' })];
      case 'grass':
        return [makeTextureLayer('grass-tufts', { scale: 0.45, relief: 0.9 }), makeTextureLayer('leaves', { scale: 0.15, opacity: 0.25, blend: 'overlay' })];
      case 'ballast':
        return [makeTextureLayer('ballast-stones', { scale: 0.9, relief: 1.1 }), makeTextureLayer('oil', { scale: 0.05, opacity: 0.4, blend: 'multiply' })];
      case 'steel':
        return [makeTextureLayer('steel-brushed', { scale: 0.6, relief: 0.35 }), makeTextureLayer('rust', { scale: 0.08, opacity: 0.35, blend: 'overlay' })];
      case 'paint':
        return [makeTextureLayer('paint-wear', { scale: 0.5, relief: 0.1 }), makeTextureLayer('grime', { scale: 0.05, opacity: 0.25, blend: 'multiply' })];
      case 'wood':
        return [makeTextureLayer('wood-grain', { scale: 0.5, relief: 0.6 })];
      case 'kerb':
        return [makeTextureLayer('joints', { scale: 0.35, relief: 0.7 }), makeTextureLayer('grime', { scale: 0.08, opacity: 0.4, blend: 'multiply' })];
      case 'road-plate':
        return [makeTextureLayer('steel-brushed', { scale: 1.2, relief: 0.3 })];
      case 'water':
        return [makeTextureLayer('puddles', { scale: 0.06, relief: 0.05 })];
      case 'rubber':
        return [makeTextureLayer('aggregate', { scale: 3.0, relief: 0.4 })];
    }
    return [makeTextureLayer('aggregate', { scale: 1 })];
  };
  return {
    id: uid('m'),
    name: surface,
    surface,
    layers: layerFor(),
    baseColorHex: base.color,
    roughness: base.roughness,
    metalness: base.metalness,
    texScale: base.scale,
    texRotation: 0,
    normalStrength: 1,
    wetness: 0,
    reflectance: surface === 'steel' || surface === 'road-plate' ? 0.6 : 0.04,
    age: 0.35,
    edgeWear: 0.5,
    ...over,
  };
}

export function defaultMaterials(): MaterialDef[] {
  const surfaces: SurfaceKind[] = [
    'asphalt',
    'asphalt-worn',
    'asphalt-fresh',
    'concrete',
    'pavers',
    'pavers-mixed',
    'slabs',
    'cobble',
    'tactile',
    'gravel',
    'soil',
    'grass',
    'ballast',
    'steel',
    'paint',
    'wood',
    'kerb',
    'road-plate',
  ];
  const out = surfaces.map((s) => {
    const m = makeMaterial(s);
    const names: Record<string, string> = {
      asphalt: 'Asphalt — standard',
      'asphalt-worn': 'Asphalt — worn',
      'asphalt-fresh': 'Asphalt — fresh',
      concrete: 'Concrete — pavement',
      pavers: 'Pavers — grey running bond',
      'pavers-mixed': 'Pavers — herringbone',
      slabs: 'Slabs — 600 flagstones',
      cobble: 'Cobbles — granite setts',
      tactile: 'Tactile — warning',
      gravel: 'Gravel — compacted',
      soil: 'Soil — bare earth',
      grass: 'Grass — amenity',
      ballast: 'Ballast — railway',
      steel: 'Steel — galvanised',
      paint: 'Road paint — white',
      wood: 'Timber',
      kerb: 'Kerb — precast concrete',
      'road-plate': 'Road plate — steel',
    };
    m.name = names[s] ?? s;
    m.builtin = true;
    return m;
  });
  // a couple of styled variants users pick from often
  const yellowPaint = makeMaterial('paint', { name: 'Road paint — yellow', baseColorHex: '#d8b13a' });
  yellowPaint.builtin = true;
  const concreteCharcoal = makeMaterial('pavers', {
    name: 'Pavers — charcoal',
    baseColorHex: '#55565a',
    layers: [
      makeTextureLayer('brick-running', { scale: 0.55, relief: 1.0, tintHex: '#c9c9c9' }),
      makeTextureLayer('grime', { scale: 0.05, opacity: 0.4, blend: 'multiply' }),
    ],
  });
  concreteCharcoal.builtin = true;
  const paverSand = makeMaterial('pavers', {
    name: 'Pavers — buff',
    baseColorHex: '#9c8a6a',
    layers: [makeTextureLayer('brick-stretcher', { scale: 0.5, relief: 1.0, tintHex: '#ffeeda' }), makeTextureLayer('grime', { scale: 0.05, opacity: 0.35, blend: 'multiply' })],
  });
  paverSand.builtin = true;
  const redAsphalt = makeMaterial('asphalt', {
    name: 'Asphalt — reddish (bus/cycle)',
    baseColorHex: '#5c3a34',
    layers: [
      makeTextureLayer('aggregate', { scale: 1.5, relief: 0.5, tintHex: '#d9b0a0' }),
      makeTextureLayer('tyre-polish', { scale: 0.04, opacity: 0.4, blend: 'multiply' }),
      makeTextureLayer('grime', { scale: 0.03, opacity: 0.4, blend: 'multiply' }),
    ],
  });
  redAsphalt.builtin = true;
  const freshTarmac = makeMaterial('asphalt-fresh', { name: 'Asphalt — new overlay', baseColorHex: '#232426' });
  freshTarmac.builtin = true;
  const expansion = makeMaterial('concrete-panel', { name: 'Concrete — expansion jointed', baseColorHex: '#b0aea6' });
  expansion.builtin = true;
  out.push(yellowPaint, concreteCharcoal, paverSand, redAsphalt, freshTarmac, expansion);
  return out;
}

// ---------------------------------------------------------------- layers

export function defaultLayers(): Layer[] {
  const mk = (name: string, kind: Layer['kind'], colorHex: string, over: Partial<Layer> = {}): Layer => ({
    id: uid('l'),
    name,
    visible: true,
    locked: false,
    solo: false,
    colorHex,
    kind,
    parentId: null,
    collapsed: false,
    opacity: 1,
    ...over,
  });
  return [
    mk('Ground & paving', 'ground', '#8a8f7a'),
    mk('Roads', 'roads', '#9aa3b0'),
    mk('Junctions', 'junctions', '#b0a48a'),
    mk('Road markings', 'markings', '#d8d3c4'),
    mk('Structures', 'structures', '#9d8f86'),
    mk('Rail', 'rail', '#8f9aa5'),
    mk('Street furniture', 'props', '#b3b6bd'),
    mk('Traffic', 'traffic', '#8d9db3'),
  ];
}

// ---------------------------------------------------------------- doc

export function makeEnv(): EnvConfig {
  return {
    timeOfDay: 15.4,
    dayOfYear: 172,
    latitude: 51.5,
    timeScale: 0,
    weather: 'sunny-haze',
    cloudCover: 0.28,
    cloudHeight: 900,
    windSpeed: 3.2,
    windDirection: 240,
    wetness: 0,
    puddles: 0.2,
    fogDensity: 0.15,
    fogHeight: 60,
    snowCover: 0,
    leavesAmount: 0.1,
    sunIntensity: 1,
    ambientIntensity: 1,
    exposure: 0,
    contrast: 1,
    saturation: 1,
    shadowStrength: 1,
    streetlightsOn: 0,
    nightGlow: 1,
    rainOnLens: true,
  };
}

export function makeRender(): RenderConfig {
  return {
    quality: 'high',
    resolutionScale: 1,
    msaaSamples: 4,
    shadows: true,
    shadowMapSize: 2048,
    cascadeCount: 3,
    shadowDistance: 220,
    ssao: true,
    bloom: true,
    bloomStrength: 0.55,
    fxaa: true,
    grain: 0.04,
    vignette: 0.35,
    chromatic: 0.12,
    depthOfField: false,
    focalDistance: 40,
    fStop: 4,
    motionBlur: false,
    showGizmos: true,
    showRoadPlan: false,
    showSurfaceFlow: false,
    wireframe: false,
    bakes: false,
    instancedDensity: 1,
    drawDistance: 900,
    lodBias: 0,
  };
}

export function makeTraffic(): TrafficConfig {
  return {
    enabled: true,
    maxVehicles: 25,
    density: 0.6,
    mixCars: 0.62,
    mixVans: 0.14,
    mixTrucks: 0.1,
    mixBuses: 0.08,
    mixEmergency: 0.06,
    speedFactor: 1,
    obedience: 0.9,
    aggressiveness: 1,
    rightHandTraffic: true,
    headlightsAuto: true,
    horns: false,
    despawnRadius: 620,
    spawnRadius: 420,
    pedestrians: 0,
    cyclists: 0,
  };
}

export function makeGround(): GroundConfig {
  return {
    surface: 'grass',
    elevation: 0,
    undulation: 0.35,
    plotSurface: 'soil',
    showGrid: false,
    size: 1600,
  };
}

export function makeSettings(): DocSettings {
  return {
    drawClass: 'street',
    snapAngle: 15,
    snapGrid: 1,
    snapNodeDistance: 6,
    mergeNodes: true,
    autoSplitCrossings: true,
    autoMinAngle: true,
    defaultCrossing: 'crossing-zebra',
    angleConstraint: 40,
    unitSystem: 'metric',
  };
}

export function makeDoc(name = 'Untitled network'): RoadDoc {
  return {
    version: 1,
    name,
    nodes: [],
    edges: [],
    props: [],
    areas: [],
    districts: [],
    trains: [],
    materials: defaultMaterials(),
    layers: defaultLayers(),
    env: makeEnv(),
    render: makeRender(),
    traffic: makeTraffic(),
    ground: makeGround(),
    settings: makeSettings(),
  };
}

// ---------------------------------------------------------------- helpers

export const layerByKind = (doc: RoadDoc, kind: Layer['kind']): Layer | undefined =>
  doc.layers.find((l) => l.kind === kind);

export const layerIdForKind = (doc: RoadDoc, kind: Layer['kind']): Id | null => layerByKind(doc, kind)?.id ?? null;

export function materialFor(doc: RoadDoc, surface: SurfaceKind, preferredId?: Id): MaterialDef {
  if (preferredId) {
    const m = doc.materials.find((x) => x.id === preferredId);
    if (m) return m;
  }
  return doc.materials.find((m) => m.surface === surface) ?? doc.materials[0];
}

export const KERB_PROFILES: Record<KerbType, { upstand: number; chamfer: number; faceSlope: number; topWidth: number }> = {
  none: { upstand: 0, chamfer: 0, faceSlope: 0, topWidth: 0 },
  flush: { upstand: 0.02, chamfer: 0.02, faceSlope: 0.1, topWidth: 0.2 },
  mountable: { upstand: 0.07, chamfer: 0.05, faceSlope: 0.45, topWidth: 0.2 },
  low: { upstand: 0.1, chamfer: 0.04, faceSlope: 0.15, topWidth: 0.2 },
  standard: { upstand: 0.15, chamfer: 0.02, faceSlope: 0.03, topWidth: 0.2 },
  tall: { upstand: 0.22, chamfer: 0.02, faceSlope: 0.02, topWidth: 0.22 },
  stone: { upstand: 0.16, chamfer: 0.03, faceSlope: 0.05, topWidth: 0.3 },
  rolled: { upstand: 0.12, chamfer: 0.06, faceSlope: 0.25, topWidth: 0.25 },
  timber: { upstand: 0.14, chamfer: 0.0, faceSlope: 0.02, topWidth: 0.24 },
};

export function clone<T>(v: T): T {
  return JSON.parse(JSON.stringify(v)) as T;
}
