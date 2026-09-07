export type Vec3 = [number, number, number];
export type Preset = "canyon" | "arches" | "badlands";
export type Tool = "orbit" | "add" | "carve" | "smooth" | "flatten";
export type ViewMode = "lit" | "clay" | "flow";
export interface Settings {
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
  grid: boolean;
  view: ViewMode;
  radius: number;
  strength: number;
  falloff: number;
  speed: number;
  autoPreview: boolean;
  terrainVisible: boolean;
}
export const DEFAULT_SETTINGS: Settings = {
  seed: 47021,
  preset: "canyon",
  rainfall: 0.45,
  erosion: 0.55,
  sediment: 0.6,
  evaporation: 0.18,
  thermal: 0.3,
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
  grid: true,
  view: "lit",
  radius: 4,
  strength: 0.55,
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
  sculpt(center: Vec3, tool: Tool, settings: Settings): void;
  pick(frame: FrameState, x: number, y: number): Promise<Vec3 | null>;
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
