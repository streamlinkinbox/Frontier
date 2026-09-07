import { DEFAULT_SETTINGS, type Settings, type VolumeSize } from "./types";
import { floatToHalf, halfToFloat } from "./math";
export interface Project {
  version: 1;
  settings: Settings;
  size: VolumeSize;
  steps: number;
  savedAt: string;
  data: Float32Array;
}
const MAGIC = "FRONTIER";
export function encodeProject(project: Project): Blob {
  const { data, ...metadata } = project;
  const text = new TextEncoder().encode(JSON.stringify(metadata));
  const headerLength = Math.ceil(text.length / 4) * 4;
  const header = new Uint8Array(12 + headerLength);
  header.set(new TextEncoder().encode(MAGIC));
  new DataView(header.buffer).setUint32(8, headerLength, true);
  header.fill(32, 12);
  header.set(text, 12);
  const half = new Uint16Array(data.length);
  for (let i = 0; i < data.length; i++) half[i] = floatToHalf(data[i]);
  return new Blob([header, half], { type: "application/octet-stream" });
}
export function validateSettings(raw: unknown): Settings {
  if (!raw || typeof raw !== "object")
    throw new Error("Missing terrain settings.");
  const input = raw as Record<string, unknown>,
    settings = { ...DEFAULT_SETTINGS };
  const ranges: Record<string, [number, number]> = {
    seed: [0, 999999],
    rainfall: [0, 1],
    erosion: [0, 1],
    sediment: [0, 1],
    evaporation: [0, 1],
    thermal: [0, 1],
    resistance: [0, 1],
    waterLevel: [-1, 8],
    wind: [0, 1],
    sunAngle: [10, 85],
    exposure: [0.5, 1.7],
    radius: [1, 12],
    strength: [0.05, 1],
    falloff: [0.05, 1],
    speed: [1, 4],
  };
  for (const [key, range] of Object.entries(ranges)) {
    if (
      typeof input[key] !== "number" ||
      !Number.isFinite(input[key]) ||
      input[key] < range[0] ||
      input[key] > range[1]
    )
      throw new Error(`Invalid project setting: ${key}.`);
    (settings as unknown as Record<string, unknown>)[key] = input[key];
  }
  // v0.1 projects did not have a micro-detail control; their voxel data remains compatible.
  if (input.detail !== undefined) {
    if (
      typeof input.detail !== "number" ||
      !Number.isFinite(input.detail) ||
      input.detail < 0 ||
      input.detail > 1.5
    )
      throw new Error("Invalid project setting: detail.");
    settings.detail = input.detail;
  }
  // Added in v0.3. Old archives retain their exact voxel data and get defaults.
  for (const [key, lo, hi] of [
    ["cohesion", 0, 1],
    ["settling", 0, 1],
    ["talusAngle", 20, 55],
    ["waterClarity", 0, 1],
  ] as const) {
    const value = input[key];
    if (value === undefined) continue;
    if (
      typeof value !== "number" ||
      !Number.isFinite(value) ||
      value < lo ||
      value > hi
    )
      throw new Error(`Invalid project setting: ${key}.`);
    settings[key] = value;
  }
  if (!Number.isInteger(settings.seed) || !Number.isInteger(settings.speed))
    throw new Error("Seed and speed must be integers.");
  for (const key of [
    "water",
    "grid",
    "autoPreview",
    "terrainVisible",
  ] as const) {
    if (typeof input[key] !== "boolean")
      throw new Error(`Invalid ${key} setting.`);
    settings[key] = input[key];
  }
  if (
    !["canyon", "arches", "badlands"].includes(String(input.preset)) ||
    !["lit", "clay", "flow"].includes(String(input.view))
  )
    throw new Error("Unknown terrain preset or view.");
  settings.preset = input.preset as Settings["preset"];
  settings.view = input.view as Settings["view"];
  return settings;
}
export function decodeProject(buffer: ArrayBuffer): Project {
  if (buffer.byteLength < 16 || buffer.byteLength > 64 * 1024 * 1024)
    throw new Error("Not a valid Frontier project (maximum 64 MB).");
  if (new TextDecoder().decode(new Uint8Array(buffer, 0, 8)) !== MAGIC)
    throw new Error("This is not a .frontier terrain file.");
  const headerLength = new DataView(buffer).getUint32(8, true);
  if (
    headerLength > 16000 ||
    headerLength % 4 !== 0 ||
    headerLength + 12 >= buffer.byteLength
  )
    throw new Error("Invalid project header.");
  const metadata = JSON.parse(
    new TextDecoder().decode(new Uint8Array(buffer, 12, headerLength)),
  );
  if (metadata.version !== 1) throw new Error("Unsupported project version.");
  const size = metadata.size as VolumeSize;
  if (
    !size ||
    ![size.x, size.y, size.z].every(
      (n) => Number.isInteger(n) && n >= 8 && n <= 256,
    ) ||
    size.x !== size.z ||
    size.x !== size.y * 2
  )
    throw new Error("Invalid volume dimensions.");
  const count = size.x * size.y * size.z * 4;
  if (buffer.byteLength !== 12 + headerLength + count * 2)
    throw new Error("Incomplete or oversized volume data.");
  const settings = validateSettings(metadata.settings);
  if (
    !Number.isInteger(metadata.steps) ||
    metadata.steps < 0 ||
    metadata.steps > 8000
  )
    throw new Error("Invalid iteration count.");
  const half = new Uint16Array(buffer, 12 + headerLength),
    data = new Float32Array(count);
  for (let i = 0; i < count; i++) {
    data[i] = halfToFloat(half[i]);
    if (!Number.isFinite(data[i]) || Math.abs(data[i]) > 64)
      throw new Error("Project contains invalid voxel data.");
  }
  return {
    version: 1,
    settings,
    size,
    steps: metadata.steps,
    savedAt: typeof metadata.savedAt === "string" ? metadata.savedAt : "",
    data,
  };
}
function database(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const r = indexedDB.open("frontier-terrain-lab", 1);
    r.onupgradeneeded = () => r.result.createObjectStore("projects");
    r.onsuccess = () => resolve(r.result);
    r.onerror = () =>
      reject(
        new Error(
          "Local storage is unavailable. Export a .frontier file instead.",
        ),
      );
  });
}
export async function saveLocal(project: Project): Promise<void> {
  const db = await database();
  try {
    await new Promise<void>((resolve, reject) => {
      const t = db.transaction("projects", "readwrite");
      t.objectStore("projects").put(project, "active");
      t.oncomplete = () => resolve();
      t.onerror = () =>
        reject(
          new Error(
            "Local save failed. Your browser may be out of storage; export a project file instead.",
          ),
        );
    });
  } finally {
    db.close();
  }
}
export async function loadLocal(): Promise<Project | null> {
  const db = await database();
  try {
    return await new Promise((resolve, reject) => {
      const r = db
        .transaction("projects")
        .objectStore("projects")
        .get("active");
      r.onsuccess = () => {
        const saved = r.result as Project | undefined;
        if (!saved) {
          resolve(null);
          return;
        }
        try {
          resolve({ ...saved, settings: validateSettings(saved.settings) });
        } catch (error) {
          reject(error);
        }
      };
      r.onerror = () => reject(new Error("Could not open the local project."));
    });
  } finally {
    db.close();
  }
}
export function download(blob: Blob, name: string) {
  const url = URL.createObjectURL(blob),
    a = document.createElement("a");
  a.href = url;
  a.download = name;
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
}
