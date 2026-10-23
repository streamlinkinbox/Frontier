/**
 * Exporters.
 *
 * OBJ keeps the quads as quads (n-gon faces), which is what you want when the
 * asset goes to Blender / Maya / ZBrush for detailing: the edge flow survives.
 * glTF (GLB) is triangulated on export (the format only knows triangles) and
 * carries the wind data as vertex colours + a second UV set so it can be
 * consumed directly by a game engine material.
 */

import { LeafMesh, QuadMesh } from './mesh';

export function toOBJ(mesh: QuadMesh, leaves: LeafMesh | null, name = 'tree'): string {
  const out: string[] = [];
  out.push(`# Frontier tree generator – single welded quad mesh`);
  out.push(`o ${name}`);
  const p = mesh.positions;
  for (let i = 0; i < p.length; i += 3) out.push(`v ${p[i].toFixed(5)} ${p[i + 1].toFixed(5)} ${p[i + 2].toFixed(5)}`);
  const normals = mesh.computeNormals();
  for (let i = 0; i < normals.length; i += 3) out.push(`vn ${normals[i].toFixed(4)} ${normals[i + 1].toFixed(4)} ${normals[i + 2].toFixed(4)}`);
  // Face-varying UVs.
  const uvIndex: number[] = [];
  const uvs = mesh.quadUVs;
  for (let i = 0; i < uvs.length; i += 2) {
    out.push(`vt ${uvs[i].toFixed(5)} ${uvs[i + 1].toFixed(5)}`);
    uvIndex.push(uvIndex.length + 1);
  }
  const triUVBase = uvIndex.length;
  const tuvs = mesh.triUVs;
  for (let i = 0; i < tuvs.length; i += 2) out.push(`vt ${tuvs[i].toFixed(5)} ${tuvs[i + 1].toFixed(5)}`);

  out.push(`g branches`);
  out.push(`s 1`);
  const q = mesh.quads;
  let uvi = 1;
  for (let f = 0; f < q.length; f += 4) {
    const a = q[f] + 1;
    const b = q[f + 1] + 1;
    const c = q[f + 2] + 1;
    const d = q[f + 3] + 1;
    out.push(`f ${a}/${uvi}/${a} ${b}/${uvi + 1}/${b} ${c}/${uvi + 2}/${c} ${d}/${uvi + 3}/${d}`);
    uvi += 4;
  }
  const t = mesh.tris;
  uvi = triUVBase + 1;
  for (let f = 0; f < t.length; f += 3) {
    const a = t[f] + 1;
    const b = t[f + 1] + 1;
    const c = t[f + 2] + 1;
    out.push(`f ${a}/${uvi}/${a} ${b}/${uvi + 1}/${b} ${c}/${uvi + 2}/${c}`);
    uvi += 3;
  }

  if (leaves && leaves.count > 0) {
    const vBase = mesh.vertexCount;
    const vtBase = triUVBase + tuvs.length / 2;
    const vnBase = mesh.vertexCount;
    out.push(`o ${name}_leaves`);
    const lp = leaves.positions;
    for (let i = 0; i < lp.length; i += 3) out.push(`v ${lp[i].toFixed(5)} ${lp[i + 1].toFixed(5)} ${lp[i + 2].toFixed(5)}`);
    const ln = leaves.normals;
    for (let i = 0; i < ln.length; i += 3) out.push(`vn ${ln[i].toFixed(4)} ${ln[i + 1].toFixed(4)} ${ln[i + 2].toFixed(4)}`);
    const lu = leaves.uvs;
    for (let i = 0; i < lu.length; i += 2) out.push(`vt ${lu[i].toFixed(5)} ${lu[i + 1].toFixed(5)}`);
    out.push(`g leaves`);
    const li = leaves.indices;
    for (let f = 0; f < li.length; f += 3) {
      const a = li[f];
      const b = li[f + 1];
      const c = li[f + 2];
      out.push(`f ${vBase + a + 1}/${vtBase + a + 1}/${vnBase + a + 1} ${vBase + b + 1}/${vtBase + b + 1}/${vnBase + b + 1} ${vBase + c + 1}/${vtBase + c + 1}/${vnBase + c + 1}`);
    }
  }
  return out.join('\n') + '\n';
}

/**
 * Triangulated buffers ready for a GPU. Vertices are shared (smooth normals);
 * the UV attribute is taken from the first face corner that references the
 * vertex (the seam is duplicated lazily to keep continuity).
 */
export interface GpuBuffers {
  position: Float32Array;
  normal: Float32Array;
  uv: Float32Array;
  /** height, limb, phase, detail */
  wind: Float32Array;
  pivot: Float32Array;
  level: Float32Array;
  junction: Float32Array;
  index: Uint32Array;
  /** Quad edges only (for wireframe display of the quad topology). */
  edgeIndex: Uint32Array;
}

export function toGpuBuffers(mesh: QuadMesh): GpuBuffers {
  const baseCount = mesh.vertexCount;
  const normals = mesh.computeNormals();

  // Vertex splitting for the UV seam: key = vertex -> list of (u,v) -> new index.
  const positions: number[] = Array.from(mesh.positions);
  const nrm: number[] = Array.from(normals);
  const uv: number[] = new Array(baseCount * 2).fill(NaN);
  const wind: number[] = Array.from(mesh.wind);
  const pivot: number[] = Array.from(mesh.pivots);
  const level: number[] = Array.from(mesh.levels);
  const junction: number[] = Array.from(mesh.junction);
  const alias = new Map<string, number>();

  const resolve = (v: number, u: number, w: number): number => {
    const cu = uv[v * 2];
    if (Number.isNaN(cu)) {
      uv[v * 2] = u;
      uv[v * 2 + 1] = w;
      return v;
    }
    if (Math.abs(cu - u) < 0.25) return v;
    const key = `${v}:${Math.round(u * 8)}`;
    const found = alias.get(key);
    if (found !== undefined) return found;
    const ni = positions.length / 3;
    positions.push(mesh.positions[v * 3], mesh.positions[v * 3 + 1], mesh.positions[v * 3 + 2]);
    nrm.push(normals[v * 3], normals[v * 3 + 1], normals[v * 3 + 2]);
    uv.push(u, w);
    wind.push(mesh.wind[v * 4], mesh.wind[v * 4 + 1], mesh.wind[v * 4 + 2], mesh.wind[v * 4 + 3]);
    pivot.push(mesh.pivots[v * 3], mesh.pivots[v * 3 + 1], mesh.pivots[v * 3 + 2]);
    level.push(mesh.levels[v]);
    junction.push(mesh.junction[v]);
    alias.set(key, ni);
    return ni;
  };

  const index: number[] = [];
  const edges: number[] = [];
  const q = mesh.quads;
  const quv = mesh.quadUVs;
  for (let f = 0, u = 0; f < q.length; f += 4, u += 8) {
    const a = resolve(q[f], quv[u], quv[u + 1]);
    const b = resolve(q[f + 1], quv[u + 2], quv[u + 3]);
    const c = resolve(q[f + 2], quv[u + 4], quv[u + 5]);
    const d = resolve(q[f + 3], quv[u + 6], quv[u + 7]);
    // Split along the shorter diagonal for better shading on twisted quads.
    const dac = diag(positions, a, c);
    const dbd = diag(positions, b, d);
    if (dac <= dbd) index.push(a, b, c, a, c, d);
    else index.push(a, b, d, b, c, d);
    edges.push(a, b, b, c, c, d, d, a);
  }
  const t = mesh.tris;
  const tuv = mesh.triUVs;
  for (let f = 0, u = 0; f < t.length; f += 3, u += 6) {
    const a = resolve(t[f], tuv[u], tuv[u + 1]);
    const b = resolve(t[f + 1], tuv[u + 2], tuv[u + 3]);
    const c = resolve(t[f + 2], tuv[u + 4], tuv[u + 5]);
    index.push(a, b, c);
    edges.push(a, b, b, c, c, a);
  }
  for (let i = 0; i < uv.length; i++) if (Number.isNaN(uv[i])) uv[i] = 0;

  return {
    position: new Float32Array(positions),
    normal: new Float32Array(nrm),
    uv: new Float32Array(uv),
    wind: new Float32Array(wind),
    pivot: new Float32Array(pivot),
    level: new Float32Array(level),
    junction: new Float32Array(junction),
    index: new Uint32Array(index),
    edgeIndex: new Uint32Array(edges),
  };
}

function diag(p: number[], a: number, b: number): number {
  const dx = p[a * 3] - p[b * 3];
  const dy = p[a * 3 + 1] - p[b * 3 + 1];
  const dz = p[a * 3 + 2] - p[b * 3 + 2];
  return dx * dx + dy * dy + dz * dz;
}

// -----------------------------------------------------------------------------
// GLB
// -----------------------------------------------------------------------------

interface BufferViewDesc {
  data: ArrayBuffer;
  target?: number;
  byteStride?: number;
}

export function toGLB(mesh: QuadMesh, leaves: LeafMesh | null, name = 'tree'): ArrayBuffer {
  const g = toGpuBuffers(mesh);
  const vcount = g.position.length / 3;

  const views: BufferViewDesc[] = [];
  const accessors: Record<string, unknown>[] = [];
  const addAccessor = (arr: Float32Array | Uint32Array, type: string, target: number, normalizedMinMax = false): number => {
    const comps = type === 'SCALAR' ? 1 : type === 'VEC2' ? 2 : type === 'VEC3' ? 3 : 4;
    views.push({ data: arr.buffer.slice(arr.byteOffset, arr.byteOffset + arr.byteLength) as ArrayBuffer, target });
    const acc: Record<string, unknown> = {
      bufferView: views.length - 1,
      componentType: arr instanceof Float32Array ? 5126 : 5125,
      count: arr.length / comps,
      type,
    };
    if (normalizedMinMax || type === 'VEC3') {
      const min = new Array(comps).fill(Infinity);
      const max = new Array(comps).fill(-Infinity);
      for (let i = 0; i < arr.length; i++) {
        const c = i % comps;
        if (arr[i] < min[c]) min[c] = arr[i];
        if (arr[i] > max[c]) max[c] = arr[i];
      }
      acc.min = min;
      acc.max = max;
    }
    accessors.push(acc);
    return accessors.length - 1;
  };

  // Wind as COLOR_0 (rgba = height, limb, phase, detail) + TEXCOORD_1 (level, junction).
  const color = new Float32Array(vcount * 4);
  color.set(g.wind);
  const tex1 = new Float32Array(vcount * 2);
  for (let i = 0; i < vcount; i++) {
    tex1[i * 2] = g.level[i] / 4;
    tex1[i * 2 + 1] = g.junction[i];
  }

  const primitives: Record<string, unknown>[] = [];
  primitives.push({
    attributes: {
      POSITION: addAccessor(g.position, 'VEC3', 34962),
      NORMAL: addAccessor(g.normal, 'VEC3', 34962),
      TEXCOORD_0: addAccessor(g.uv, 'VEC2', 34962),
      TEXCOORD_1: addAccessor(tex1, 'VEC2', 34962),
      COLOR_0: addAccessor(color, 'VEC4', 34962),
    },
    indices: addAccessor(g.index, 'SCALAR', 34963),
    material: 0,
    mode: 4,
  });

  const materials: Record<string, unknown>[] = [
    { name: 'bark', pbrMetallicRoughness: { baseColorFactor: [0.42, 0.36, 0.3, 1], metallicFactor: 0, roughnessFactor: 0.9 } },
  ];

  if (leaves && leaves.count > 0) {
    const lpos = new Float32Array(leaves.positions);
    const lnrm = new Float32Array(leaves.normals);
    const luv = new Float32Array(leaves.uvs);
    const lcol = new Float32Array(leaves.wind);
    const lidx = new Uint32Array(leaves.indices);
    primitives.push({
      attributes: {
        POSITION: addAccessor(lpos, 'VEC3', 34962),
        NORMAL: addAccessor(lnrm, 'VEC3', 34962),
        TEXCOORD_0: addAccessor(luv, 'VEC2', 34962),
        COLOR_0: addAccessor(lcol, 'VEC4', 34962),
      },
      indices: addAccessor(lidx, 'SCALAR', 34963),
      material: 1,
      mode: 4,
    });
    materials.push({
      name: 'leaf',
      doubleSided: true,
      pbrMetallicRoughness: { baseColorFactor: [0.3, 0.5, 0.22, 1], metallicFactor: 0, roughnessFactor: 0.7 },
    });
  }

  // Pack buffer.
  let byteLength = 0;
  const bufferViews: Record<string, unknown>[] = [];
  const chunks: ArrayBuffer[] = [];
  for (const v of views) {
    const pad = (4 - (byteLength % 4)) % 4;
    if (pad) {
      chunks.push(new ArrayBuffer(pad));
      byteLength += pad;
    }
    const bv: Record<string, unknown> = { buffer: 0, byteOffset: byteLength, byteLength: v.data.byteLength };
    if (v.target) bv.target = v.target;
    bufferViews.push(bv);
    chunks.push(v.data);
    byteLength += v.data.byteLength;
  }
  const tailPad = (4 - (byteLength % 4)) % 4;
  if (tailPad) {
    chunks.push(new ArrayBuffer(tailPad));
    byteLength += tailPad;
  }

  const gltf = {
    asset: { version: '2.0', generator: 'Frontier tree generator' },
    scene: 0,
    scenes: [{ nodes: [0] }],
    nodes: [{ mesh: 0, name }],
    meshes: [{ name, primitives }],
    materials,
    accessors,
    bufferViews,
    buffers: [{ byteLength }],
    extras: {
      wind: {
        COLOR_0: 'r = normalised height, g = limb bend weight, b = limb phase, a = detail flutter',
        TEXCOORD_1: 'u = level / 4, v = junction flag',
      },
    },
  };

  const jsonStr = JSON.stringify(gltf);
  const enc = new TextEncoder();
  let jsonBytes = enc.encode(jsonStr);
  const jsonPad = (4 - (jsonBytes.length % 4)) % 4;
  if (jsonPad) {
    const padded = new Uint8Array(jsonBytes.length + jsonPad);
    padded.set(jsonBytes);
    padded.fill(0x20, jsonBytes.length);
    jsonBytes = padded;
  }

  const total = 12 + 8 + jsonBytes.length + 8 + byteLength;
  const out = new ArrayBuffer(total);
  const dv = new DataView(out);
  const u8 = new Uint8Array(out);
  let o = 0;
  dv.setUint32(o, 0x46546c67, true);
  o += 4;
  dv.setUint32(o, 2, true);
  o += 4;
  dv.setUint32(o, total, true);
  o += 4;
  dv.setUint32(o, jsonBytes.length, true);
  o += 4;
  dv.setUint32(o, 0x4e4f534a, true);
  o += 4;
  u8.set(jsonBytes, o);
  o += jsonBytes.length;
  dv.setUint32(o, byteLength, true);
  o += 4;
  dv.setUint32(o, 0x004e4942, true);
  o += 4;
  for (const c of chunks) {
    u8.set(new Uint8Array(c), o);
    o += c.byteLength;
  }
  return out;
}
