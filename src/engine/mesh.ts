import { sampleField, noise } from "./field";
import { normalize, cross, sub, dot, mix, smoothstep } from "./math";
import { type Vec3, type VolumeSize } from "./types";
export interface Mesh {
  positions: Float32Array;
  normals: Float32Array;
  colors: Float32Array;
  indices: Uint32Array;
  min: Vec3;
  max: Vec3;
}
const CORNERS: Vec3[] = [
  [0, 0, 0],
  [1, 0, 0],
  [1, 1, 0],
  [0, 1, 0],
  [0, 0, 1],
  [1, 0, 1],
  [1, 1, 1],
  [0, 1, 1],
];
const TETS = [
  [0, 5, 1, 6],
  [0, 1, 2, 6],
  [0, 2, 3, 6],
  [0, 3, 7, 6],
  [0, 7, 4, 6],
  [0, 4, 5, 6],
];
export function extractMesh(
  data: Float32Array,
  size: VolumeSize,
  onProgress?: (progress: number) => void,
): Mesh {
  const positions: number[] = [],
    normals: number[] = [],
    colors: number[] = [],
    indices: number[] = [],
    cell = 96 / size.x;
  const edgeVertices = new Map<number, number>(),
    voxelCount = size.x * size.y * size.z;
  const minimum: Vec3 = [Infinity, Infinity, Infinity],
    maximum: Vec3 = [-Infinity, -Infinity, -Infinity];
  const normal = (p: Vec3): Vec3 => {
    const e = cell * 0.5;
    return normalize([
      sampleField(data, size, [p[0] + e, p[1], p[2]]) -
        sampleField(data, size, [p[0] - e, p[1], p[2]]),
      sampleField(data, size, [p[0], p[1] + e, p[2]]) -
        sampleField(data, size, [p[0], p[1] - e, p[2]]),
      sampleField(data, size, [p[0], p[1], p[2] + e]) -
        sampleField(data, size, [p[0], p[1], p[2] - e]),
    ]);
  };
  const color = (p: Vec3, n: Vec3): Vec3 => {
    const layer =
      p[1] +
      noise(p[0] * 0.035, p[1] * 0.02, p[2] * 0.035) * 1.7 +
      Math.sin(p[2] * 0.022) * 1.6;
    const bands = (Math.sin(layer * 0.92) * 0.5 + 0.5) * 0.65 + 0.15,
      pale = smoothstep(0.65, 0.92, Math.sin(layer * 0.46 + 0.9)) * 0.4;
    const base = [
      mix(0.38, 0.53, bands),
      mix(0.113, 0.24, bands),
      mix(0.043, 0.091, bands),
    ];
    const chalk = [0.61, 0.355, 0.17],
      sand = [0.5, 0.255, 0.112];
    return base.map((v, i) =>
      mix(mix(v, chalk[i], pale), sand[i], smoothstep(0.64, 0.94, n[1]) * 0.42),
    ) as Vec3;
  };
  const emit = (a: number, b: number, c: number) => {
    const pa = positions.slice(a * 3, a * 3 + 3) as Vec3,
      pb = positions.slice(b * 3, b * 3 + 3) as Vec3,
      pc = positions.slice(c * 3, c * 3 + 3) as Vec3;
    const na = normals.slice(a * 3, a * 3 + 3) as Vec3;
    if (dot(cross(sub(pb, pa), sub(pc, pa)), na) < 0) [b, c] = [c, b];
    indices.push(a, b, c);
  };
  for (let z = 0; z < size.z - 1; z++) {
    for (let y = 0; y < size.y - 1; y++)
      for (let x = 0; x < size.x - 1; x++) {
        const ids = CORNERS.map(
          (c) => ((z + c[2]) * size.y + y + c[1]) * size.x + x + c[0],
        );
        const values = ids.map((i) => data[i * 4]);
        if (values.every((v) => v >= 0) || values.every((v) => v < 0)) continue;
        const points = CORNERS.map(
          (c) =>
            [
              -48 + (x + c[0] + 0.5) * cell,
              -10 + (y + c[1] + 0.5) * cell,
              -48 + (z + c[2] + 0.5) * cell,
            ] as Vec3,
        );
        const interp = (a: number, b: number): number => {
          // A lattice edge has a stable integer key, shared by adjacent cells and
          // tetrahedra. Welding here avoids duplicate normals and huge GLB files.
          const key =
            Math.min(ids[a], ids[b]) * voxelCount + Math.max(ids[a], ids[b]);
          const existing = edgeVertices.get(key);
          if (existing !== undefined) return existing;
          const t = values[a] / (values[a] - values[b]),
            p = points[a].map((v, i) => mix(v, points[b][i], t)) as Vec3;
          const n = normal(p),
            index = positions.length / 3;
          positions.push(...p);
          normals.push(...n);
          colors.push(...color(p, n), 1);
          for (let k = 0; k < 3; k++) {
            minimum[k] = Math.min(minimum[k], p[k]);
            maximum[k] = Math.max(maximum[k], p[k]);
          }
          edgeVertices.set(key, index);
          return index;
        };
        for (const tet of TETS) {
          const inside = tet.filter((i) => values[i] < 0),
            outside = tet.filter((i) => values[i] >= 0);
          if (inside.length === 1)
            emit(
              interp(inside[0], outside[0]),
              interp(inside[0], outside[1]),
              interp(inside[0], outside[2]),
            );
          else if (inside.length === 3)
            emit(
              interp(outside[0], inside[0]),
              interp(outside[0], inside[1]),
              interp(outside[0], inside[2]),
            );
          else if (inside.length === 2) {
            const a = interp(inside[0], outside[0]),
              b = interp(inside[0], outside[1]),
              c = interp(inside[1], outside[0]),
              d = interp(inside[1], outside[1]);
            emit(a, b, c);
            emit(b, d, c);
          }
        }
      }
    if (z % 8 === 0) onProgress?.(z / (size.z - 1));
  }
  if (!positions.length)
    throw new Error("The volume has no surface to export.");
  return {
    positions: new Float32Array(positions),
    normals: new Float32Array(normals),
    colors: new Float32Array(colors),
    indices: new Uint32Array(indices),
    min: minimum,
    max: maximum,
  };
}
export function buildGLB(mesh: Mesh): ArrayBuffer {
  const { positions, normals, colors, indices } = mesh,
    count = positions.length / 3;
  const binaryLength =
    positions.byteLength +
    normals.byteLength +
    colors.byteLength +
    indices.byteLength;
  const doc = {
    asset: {
      version: "2.0",
      generator: "Frontier Terrain Lab · volumetric SDF",
    },
    scene: 0,
    scenes: [{ nodes: [0] }],
    nodes: [{ mesh: 0, name: "Frontier terrain · meters" }],
    meshes: [
      {
        primitives: [
          {
            attributes: { POSITION: 0, NORMAL: 1, COLOR_0: 2 },
            indices: 3,
            material: 0,
            mode: 4,
          },
        ],
      },
    ],
    materials: [
      {
        name: "Procedural sandstone",
        pbrMetallicRoughness: {
          baseColorFactor: [1, 1, 1, 1],
          metallicFactor: 0,
          roughnessFactor: 0.95,
        },
        doubleSided: false,
      },
    ],
    buffers: [{ byteLength: binaryLength }],
    bufferViews: [
      {
        buffer: 0,
        byteOffset: 0,
        byteLength: positions.byteLength,
        target: 34962,
      },
      {
        buffer: 0,
        byteOffset: positions.byteLength,
        byteLength: normals.byteLength,
        target: 34962,
      },
      {
        buffer: 0,
        byteOffset: positions.byteLength + normals.byteLength,
        byteLength: colors.byteLength,
        target: 34962,
      },
      {
        buffer: 0,
        byteOffset:
          positions.byteLength + normals.byteLength + colors.byteLength,
        byteLength: indices.byteLength,
        target: 34963,
      },
    ],
    accessors: [
      {
        bufferView: 0,
        componentType: 5126,
        count,
        type: "VEC3",
        min: mesh.min,
        max: mesh.max,
      },
      { bufferView: 1, componentType: 5126, count, type: "VEC3" },
      { bufferView: 2, componentType: 5126, count, type: "VEC4" },
      {
        bufferView: 3,
        componentType: 5125,
        count: indices.length,
        type: "SCALAR",
      },
    ],
    extras: {
      units: "meters",
      field: "3D signed distance, zero isosurface",
      water: "Water is a preview shader, not included in this mesh.",
    },
  };
  const json = new TextEncoder().encode(JSON.stringify(doc)),
    padded = Math.ceil(json.length / 4) * 4;
  const buffer = new ArrayBuffer(12 + 8 + padded + 8 + binaryLength),
    view = new DataView(buffer),
    bytes = new Uint8Array(buffer);
  view.setUint32(0, 0x46546c67, true);
  view.setUint32(4, 2, true);
  view.setUint32(8, buffer.byteLength, true);
  view.setUint32(12, padded, true);
  view.setUint32(16, 0x4e4f534a, true);
  bytes.fill(32, 20, 20 + padded);
  bytes.set(json, 20);
  view.setUint32(20 + padded, binaryLength, true);
  view.setUint32(24 + padded, 0x004e4942, true);
  let offset = 28 + padded;
  for (const array of [positions, normals, colors, indices]) {
    bytes.set(new Uint8Array(array.buffer), offset);
    offset += array.byteLength;
  }
  return buffer;
}
