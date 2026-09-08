export type Vec3 = [number, number, number];
export type Preset = "canyon" | "arches" | "badlands";
export const TOOL_IDS = [
  "orbit",
  "add",
  "carve",
  "smooth",
  "ridges",
  "flatten",
  "crack",
  "crevice",
  "boulder",
] as const;
export type Tool = (typeof TOOL_IDS)[number];
export interface BrushHit {
  position: Vec3;
  normal: Vec3;
}
export interface BrushStamp {
  origin: Vec3;
  normal: Vec3;
  tangent: Vec3;
  previous: Vec3;
  seed: number;
}
export type SurfaceMaterial = "sandstone" | "limestone" | "granite" | "basalt";
export type ViewMode = "lit" | "clay" | "flow";
export type SatmapId =
  "namib" | "canyonlands" | "iceland" | "white-sands" | "custom";
export const SATMAP_VIEWS = [
  "beauty",
  "albedo",
  "texture",
  "height",
  "slope",
  "curvature",
  "ao",
  "normals",
  "flow",
  "sediment",
  "detail",
] as const;
export type SatmapView = (typeof SATMAP_VIEWS)[number];
export interface Settings {
  textureMode: "satmap" | "legacy";
  satmap: SatmapId;
  satmapPalette: string;
  satmapDetailMap: string;
  satmapName: string;
  satmapHeight: number;
  satmapSlope: number;
  satmapCurvature: number;
  satmapAO: number;
  satmapFlow: number;
  satmapSediment: number;
  satmapDetail: number;
  satmapScale: number;
  satmapRelief: number;
  satmapBias: number;
  satmapContrast: number;
  satmapLow: number;
  satmapHigh: number;
  satmapReverse: boolean;
  satmapSaturation: number;
  satmapPreview: SatmapView;
  seed: number;
  preset: Preset;
  rainfall: number;
  erosion: number;
  sediment: number;
  evaporation: number;
  thermal: number;
  resistance: number;
  cohesion: number;
  settling: number;
  talusAngle: number;
  waterClarity: number;
  water: boolean;
  waterLevel: number;
  wind: number;
  sunAngle: number;
  exposure: number;
  detail: number;
  material: SurfaceMaterial;
  materialColor: string;
  materialRoughness: number;
  materialGrain: number;
  materialRelief: number;
  materialScale: number;
  materialBedding: number;
  materialPorosity: number;
  materialWeathering: number;
  materialIOR: number;
  materialMoisture: number;
  waterAbsorption: number;
  waterFoam: number;
  waterCurrent: number;
  waterDirection: number;
  waterRippleScale: number;
  waterFlowMode: "channel" | "directional";
  waterReverse: boolean;
  waterStreaks: number;
  rockRelief: number;
  rockNoiseScale: number;
  rockOctaves: number;
  rockRidges: number;
  rockLayerSpacing: number;
  rockLayerRelief: number;
  rockLayerWarp: number;
  grid: boolean;
  view: ViewMode;
  radius: number;
  strength: number;
  brushDepth: number;
  brushWidth: number;
  flattenPlane: "surface" | "horizontal";
  channeling: number;
  windErosion: number;
  windDirection: number;
  falloff: number;
  speed: number;
  autoPreview: boolean;
  terrainVisible: boolean;
}
export const DEFAULT_SETTINGS: Settings = {
  textureMode: "satmap",
  satmap: "namib",
  satmapPalette: "",
  satmapDetailMap: "",
  satmapName: "",
  satmapHeight: 0.35,
  satmapSlope: 0.6,
  satmapCurvature: 0.65,
  satmapAO: 0.3,
  satmapFlow: 0.55,
  satmapSediment: 0.7,
  satmapDetail: 0.8,
  satmapScale: 6,
  satmapRelief: 18,
  satmapBias: 0,
  satmapContrast: 1.7,
  satmapLow: 0.03,
  satmapHigh: 0.98,
  satmapReverse: false,
  satmapSaturation: 0.82,
  satmapPreview: "beauty",
  seed: 47021,
  preset: "canyon",
  rainfall: 0.45,
  erosion: 0.55,
  sediment: 0.6,
  evaporation: 0.18,
  thermal: 0.03,
  resistance: 0.65,
  cohesion: 0.28,
  settling: 0.35,
  talusAngle: 34,
  waterClarity: 0.72,
  water: true,
  waterLevel: 0.65,
  wind: 0.28,
  sunAngle: 38,
  exposure: 1.08,
  detail: 0.85,
  material: "sandstone",
  materialColor: "#B19167",
  materialRoughness: 0.82,
  materialGrain: 0.6,
  materialRelief: 1.6,
  materialScale: 1,
  materialBedding: 0.55,
  materialPorosity: 0.32,
  materialWeathering: 0.28,
  materialIOR: 1.5,
  materialMoisture: 0,
  waterAbsorption: 8,
  waterFoam: 0.35,
  waterCurrent: 0.8,
  waterDirection: 270,
  waterRippleScale: 1.8,
  waterFlowMode: "channel",
  waterReverse: false,
  waterStreaks: 0.45,
  rockRelief: 5.5,
  rockNoiseScale: 0.55,
  rockOctaves: 4,
  rockRidges: 0.6,
  rockLayerSpacing: 0.42,
  rockLayerRelief: 2.8,
  rockLayerWarp: 0.65,
  grid: true,
  view: "lit",
  radius: 4,
  strength: 0.55,
  brushDepth: 0.7,
  brushWidth: 0.18,
  flattenPlane: "surface",
  channeling: 0.85,
  windErosion: 0,
  windDirection: 135,
  falloff: 0.6,
  speed: 2,
  autoPreview: true,
  terrainVisible: true,
};
export const PRESETS: {
  id: Preset;
  name: string;
  subtitle: string;
  label: string;
}[] = [
  {
    id: "canyon",
    name: "Desert canyon",
    subtitle: "Layered sandstone · river-cut",
    label: "Canyon 01",
  },
  {
    id: "arches",
    name: "Sandstone arches",
    subtitle: "Natural bridges · wind-worn",
    label: "Arches 01",
  },
  {
    id: "badlands",
    name: "Desert spires",
    subtitle: "Fractured hoodoos · caprock and fins",
    label: "Spires 01",
  },
];
export interface VolumeSize {
  x: number;
  y: number;
  z: number;
}
export const GPU_SIZE: VolumeSize = { x: 144, y: 72, z: 144 };
export const CPU_SIZE: VolumeSize = { x: 96, y: 48, z: 96 };
export const BOUNDS_MIN: Vec3 = [-48, -10, -48];
export const BOUNDS_MAX: Vec3 = [48, 38, 48];
export const WORLD_SIZE: Vec3 = [96, 48, 96];
export interface FrameState {
  eye: Vec3;
  forward: Vec3;
  right: Vec3;
  up: Vec3;
  width: number;
  height: number;
  time: number;
  settings: Settings;
  brush: Vec3 | null;
  tool: Tool;
  stamp?: BrushStamp;
  compare: boolean;
}
export interface Backend {
  name: "WebGPU" | "WebGL2";
  size: VolumeSize;
  readonly displayMode?: "native" | "safe";
  refreshDisplaySurface?(): void;
  initialize(settings: Settings): Promise<void>;
  regenerate(settings: Settings): Promise<void>;
  ready(): boolean;
  getDiagnostics(): Record<string, unknown>;
  sync(): Promise<void>;
  render(frame: FrameState): void;
  verifyFrame(
    frame: FrameState,
    target?: "presentation" | "offscreen",
  ): Promise<boolean>;
  capture(frame: FrameState): Promise<Blob>;
  step(settings: Settings, count: number): void;
  sculpt(
    center: Vec3,
    tool: Tool,
    settings: Settings,
    stamp?: BrushStamp,
  ): void;
  pick(frame: FrameState, x: number, y: number): Promise<BrushHit | null>;
  refreshTerrainMaps(): Promise<void>;
  readVolume(): Promise<Float32Array>;
  writeVolume(data: Float32Array): void;
  reset(): void;
  dispose(): void;
}
export interface EngineStats {
  fps: number;
  steps: number;
  backend: string;
  displayMode?: "native" | "safe";
  size: VolumeSize;
  running: boolean;
  scalePixels?: number;
  scaleMeters?: number;
  navigationMode?: "orbit" | "fly";
  cameraSpeed?: number;
  cameraPosition?: Vec3;
  quality?: "adaptive" | "native";
}
