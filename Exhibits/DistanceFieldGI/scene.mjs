const FLOAT = 5126;
const UNSIGNED_SHORT = 5123;
const UNSIGNED_INT = 5125;
const COMPONENTS = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4 };
const VEHICLE_VISIBILITY_ID = 63;

export const GLOBAL_FIELD_RESOLUTION = 64;
export const VEHICLE_SDF_DIMS = [112, 40, 48];
export const VEHICLE_SDF_PADDING = 0.18;
export const VEHICLE_VERTEX_STRIDE = 16;
export const VEHICLE_WHEEL_ORDER = ['FL', 'FR', 'RL', 'RR'];
export const WORLD_WIDTH = 10_000;
export const WORLD_LENGTH = 100_000;
export const CITY_BLOCK_SIZE = 96;
export const CITY_STREAM_RADIUS = 2;
export const CITY_SDF_RADIUS = 1;
export const MAX_SURFACE_CARDS = 32_768;
export const SURFACE_CARD_MAX_EDGE = 64;
export const SURFACE_CARD_RESOLUTION = 4;
export const SURFACE_CARD_TEXELS = SURFACE_CARD_RESOLUTION * SURFACE_CARD_RESOLUTION;
export const SURFACE_CARD_FLOATS = 24;

function identity4() {
  return new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
}

function multiply4(a, b) {
  const out = new Float32Array(16);
  for (let column = 0; column < 4; column++) {
    const b0 = b[column * 4];
    const b1 = b[column * 4 + 1];
    const b2 = b[column * 4 + 2];
    const b3 = b[column * 4 + 3];
    out[column * 4] = a[0] * b0 + a[4] * b1 + a[8] * b2 + a[12] * b3;
    out[column * 4 + 1] = a[1] * b0 + a[5] * b1 + a[9] * b2 + a[13] * b3;
    out[column * 4 + 2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14] * b3;
    out[column * 4 + 3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15] * b3;
  }
  return out;
}

function nodeMatrix(node) {
  if (node.matrix) return new Float32Array(node.matrix);
  const t = node.translation || [0, 0, 0];
  const q = node.rotation || [0, 0, 0, 1];
  const s = node.scale || [1, 1, 1];
  const [x, y, z, w] = q;
  const xx = x * x, yy = y * y, zz = z * z;
  const xy = x * y, xz = x * z, yz = y * z;
  const wx = w * x, wy = w * y, wz = w * z;
  return new Float32Array([
    (1 - 2 * (yy + zz)) * s[0], (2 * (xy + wz)) * s[0], (2 * (xz - wy)) * s[0], 0,
    (2 * (xy - wz)) * s[1], (1 - 2 * (xx + zz)) * s[1], (2 * (yz + wx)) * s[1], 0,
    (2 * (xz + wy)) * s[2], (2 * (yz - wx)) * s[2], (1 - 2 * (xx + yy)) * s[2], 0,
    t[0], t[1], t[2], 1,
  ]);
}

function transformPoint(matrix, p) {
  const x = p[0], y = p[1], z = p[2];
  const w = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15];
  const invW = Math.abs(w) > 1e-8 ? 1 / w : 1;
  return [
    (matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12]) * invW,
    (matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13]) * invW,
    (matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14]) * invW,
  ];
}

function transformNormal(matrix, n) {
  const a = matrix[0], b = matrix[4], c = matrix[8];
  const d = matrix[1], e = matrix[5], f = matrix[9];
  const g = matrix[2], h = matrix[6], i = matrix[10];
  const c00 = e * i - f * h, c01 = f * g - d * i, c02 = d * h - e * g;
  const c10 = c * h - b * i, c11 = a * i - c * g, c12 = b * g - a * h;
  const c20 = b * f - c * e, c21 = c * d - a * f, c22 = a * e - b * d;
  const determinant = a * c00 + b * c01 + c * c02;
  const inverseDeterminant = Math.abs(determinant) > 1e-10 ? 1 / determinant : 1;
  const out = [
    (c00 * n[0] + c01 * n[1] + c02 * n[2]) * inverseDeterminant,
    (c10 * n[0] + c11 * n[1] + c12 * n[2]) * inverseDeterminant,
    (c20 * n[0] + c21 * n[1] + c22 * n[2]) * inverseDeterminant,
  ];
  const length = Math.hypot(...out) || 1;
  return out.map(value => value / length);
}

function worldMatrices(document) {
  const result = new Array(document.nodes.length);
  const visited = new Set();
  const scenes = document.scenes || [];
  const roots = scenes[document.scene || 0]?.nodes || document.nodes.map((_, i) => i).filter(i =>
    !document.nodes.some(node => (node.children || []).includes(i)));
  const visit = (index, parent) => {
    if (visited.has(index)) return;
    visited.add(index);
    const matrix = multiply4(parent, nodeMatrix(document.nodes[index]));
    result[index] = matrix;
    for (const child of document.nodes[index].children || []) visit(child, matrix);
  };
  for (const root of roots) visit(root, identity4());
  // Invalid or non-scene nodes should not make an otherwise useful preview fail.
  for (let i = 0; i < document.nodes.length; i++) {
    if (!result[i]) result[i] = nodeMatrix(document.nodes[i]);
  }
  return result;
}

function decodeAccessor(document, binary, accessorIndex) {
  const accessor = document.accessors[accessorIndex];
  if (!accessor || accessor.sparse) throw new Error(`Unsupported glTF accessor ${accessorIndex}`);
  const view = document.bufferViews[accessor.bufferView];
  if (!view) throw new Error(`Missing bufferView for accessor ${accessorIndex}`);
  const componentCount = COMPONENTS[accessor.type];
  const componentBytes = accessor.componentType === FLOAT || accessor.componentType === UNSIGNED_INT ? 4 : 2;
  const elementBytes = componentCount * componentBytes;
  const stride = view.byteStride || elementBytes;
  const base = (view.byteOffset || 0) + (accessor.byteOffset || 0);
  const count = accessor.count;
  const output = new Array(count * componentCount);
  const data = new DataView(binary);
  for (let element = 0; element < count; element++) {
    const offset = base + element * stride;
    for (let component = 0; component < componentCount; component++) {
      const address = offset + component * componentBytes;
      let value;
      if (accessor.componentType === FLOAT) value = data.getFloat32(address, true);
      else if (accessor.componentType === UNSIGNED_INT) value = data.getUint32(address, true);
      else if (accessor.componentType === UNSIGNED_SHORT) value = data.getUint16(address, true);
      else throw new Error(`Unsupported glTF component type ${accessor.componentType}`);
      if (accessor.normalized && accessor.componentType === UNSIGNED_SHORT) value /= 65535;
      output[element * componentCount + component] = value;
    }
  }
  return { values: output, count, componentCount };
}

export function parseGlb(arrayBuffer) {
  if (ArrayBuffer.isView(arrayBuffer)) {
    arrayBuffer = arrayBuffer.buffer.slice(arrayBuffer.byteOffset, arrayBuffer.byteOffset + arrayBuffer.byteLength);
  }
  const data = new DataView(arrayBuffer);
  if (data.byteLength < 20 || data.getUint32(0, true) !== 0x46546c67) {
    throw new Error('ControlVehicle asset is not a valid GLB file.');
  }
  if (data.getUint32(4, true) !== 2) throw new Error('Only glTF binary version 2 is supported.');
  const jsonLength = data.getUint32(12, true);
  const jsonStart = 20;
  const document = JSON.parse(new TextDecoder().decode(new Uint8Array(arrayBuffer, jsonStart, jsonLength)));
  let binaryStart = jsonStart + jsonLength;
  let binaryLength = 0;
  while (binaryStart + 8 <= data.byteLength) {
    const length = data.getUint32(binaryStart, true);
    const type = data.getUint32(binaryStart + 4, true);
    const chunkStart = binaryStart + 8;
    if (type === 0x004e4942) {
      binaryStart = chunkStart;
      binaryLength = length;
      break;
    }
    binaryStart = chunkStart + length;
  }
  if (!binaryLength) throw new Error('ControlVehicle GLB has no binary geometry chunk.');
  const binary = arrayBuffer.slice(binaryStart, binaryStart + binaryLength);
  return { document, binary };
}

export function buildControlVehicle(arrayBuffer) {
  const { document, binary } = parseGlb(arrayBuffer);
  const matrices = worldMatrices(document);
  const vertexData = [];
  const triangles = [];
  const boundsMin = [Infinity, Infinity, Infinity];
  const boundsMax = [-Infinity, -Infinity, -Infinity];
  const groupByNode = new Map();
  const wheelBounds = VEHICLE_WHEEL_ORDER.map(() => ({
    group: -1,
    min: [Infinity, Infinity, Infinity],
    max: [-Infinity, -Infinity, -Infinity],
  }));
  let triangleCount = 0;

  for (let nodeIndex = 0; nodeIndex < document.nodes.length; nodeIndex++) {
    const node = document.nodes[nodeIndex];
    if (node.mesh === undefined) continue;
    if (!groupByNode.has(nodeIndex)) groupByNode.set(nodeIndex, groupByNode.size);
    const group = groupByNode.get(nodeIndex);
    const wheelMatch = /^Tyre\s+(FL|FR|RL|RR)$/i.exec(node.name || '');
    const wheelIndex = wheelMatch ? VEHICLE_WHEEL_ORDER.indexOf(wheelMatch[1].toUpperCase()) : -1;
    if (wheelIndex >= 0) wheelBounds[wheelIndex].group = group;
    const partCode = wheelIndex >= 0 ? wheelIndex + 2 : 1;
    const visibilityId = wheelIndex >= 0 ? 59 + wheelIndex : VEHICLE_VISIBILITY_ID;
    const matrix = matrices[nodeIndex] || identity4();
    const mesh = document.meshes[node.mesh];
    for (const primitive of mesh.primitives || []) {
      if (primitive.mode !== undefined && primitive.mode !== 4) continue;
      const positionAccessor = decodeAccessor(document, binary, primitive.attributes.POSITION);
      const normalAccessor = primitive.attributes.NORMAL === undefined
        ? null : decodeAccessor(document, binary, primitive.attributes.NORMAL);
      const indexAccessor = primitive.indices === undefined
        ? null : decodeAccessor(document, binary, primitive.indices);
      const indices = indexAccessor ? indexAccessor.values : Array.from({ length: positionAccessor.count }, (_, i) => i);
      const material = document.materials?.[primitive.material] || {};
      const pbr = material.pbrMetallicRoughness || {};
      const base = pbr.baseColorFactor || [0.72, 0.74, 0.76, 1];
      const roughness = pbr.roughnessFactor ?? 0.5;
      const metallic = pbr.metallicFactor ?? 0;
      const emissive = material.emissiveFactor || [0, 0, 0];
      const emission = Math.max(emissive[0], emissive[1], emissive[2]);
      const surfaceColor = [base[0], base[1], base[2], emission];
      const vertexCount = Math.floor(indices.length / 3) * 3;
      for (let first = 0; first < vertexCount; first += 3) {
        const points = [];
        const normals = [];
        for (let corner = 0; corner < 3; corner++) {
          const index = indices[first + corner];
          const p = positionAccessor.values.slice(index * 3, index * 3 + 3);
          points.push(transformPoint(matrix, p));
          if (normalAccessor) {
            normals.push(transformNormal(matrix, normalAccessor.values.slice(index * 3, index * 3 + 3)));
          }
        }
        let faceNormal = normals[0];
        if (!faceNormal) {
          const ab = points[1].map((value, axis) => value - points[0][axis]);
          const ac = points[2].map((value, axis) => value - points[0][axis]);
          faceNormal = normalize3([
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
          ]);
          normals.push(faceNormal, faceNormal);
        }
        const a = points[0], b = points[1], c = points[2];
        triangles.push({ a, b, c, color: surfaceColor.slice(), surface: [roughness, metallic, emission, 0], group });
        triangleCount++;
        for (let corner = 0; corner < 3; corner++) {
          const p = points[corner];
          const n = normals[corner] || faceNormal;
          for (let axis = 0; axis < 3; axis++) {
            boundsMin[axis] = Math.min(boundsMin[axis], p[axis]);
            boundsMax[axis] = Math.max(boundsMax[axis], p[axis]);
            if (wheelIndex >= 0) {
              wheelBounds[wheelIndex].min[axis] = Math.min(wheelBounds[wheelIndex].min[axis], p[axis]);
              wheelBounds[wheelIndex].max[axis] = Math.max(wheelBounds[wheelIndex].max[axis], p[axis]);
            }
          }
          vertexData.push(
            p[0], p[1], p[2], 0,
            n[0], n[1], n[2], 0,
            base[0], base[1], base[2], visibilityId,
            roughness, metallic, emission, partCode,
          );
        }
      }
    }
  }

  if (!triangleCount || !boundsMin.every(Number.isFinite)) throw new Error('ControlVehicle has no renderable triangle meshes.');
  const wheelParts = wheelBounds.map((bounds, index) => {
    if (bounds.group < 0 || !bounds.min.every(Number.isFinite) || !bounds.max.every(Number.isFinite)) {
      throw new Error(`ControlVehicle is missing the ${VEHICLE_WHEEL_ORDER[index]} wheel mesh.`);
    }
    const halfExtent = bounds.min.map((value, axis) => (bounds.max[axis] - value) * 0.5);
    return {
      name: VEHICLE_WHEEL_ORDER[index],
      group: bounds.group,
      surfaceCode: index + 2,
      visibilityId: 59 + index,
      center: bounds.min.map((value, axis) => (value + bounds.max[axis]) * 0.5),
      halfExtent,
      radius: halfExtent[1],
    };
  });
  const padding = VEHICLE_SDF_PADDING;
  const paddedMin = boundsMin.map(value => value - padding);
  const paddedMax = boundsMax.map(value => value + padding);
  return {
    document,
    triangleCount,
    groupCount: groupByNode.size,
    boundsMin: paddedMin,
    boundsMax: paddedMax,
    sourceBoundsMin: boundsMin,
    sourceBoundsMax: boundsMax,
    wheelParts,
    wheelBase: Math.abs((wheelParts[0].center[0] + wheelParts[1].center[0] - wheelParts[2].center[0] - wheelParts[3].center[0]) * 0.5),
    vertexData: new Float32Array(vertexData),
    triangles,
  };
}

function normalize3(v) {
  const length = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / length, v[1] / length, v[2] / length];
}

function triangleBounds(triangle) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (const point of [triangle.a, triangle.b, triangle.c]) {
    for (let axis = 0; axis < 3; axis++) {
      min[axis] = Math.min(min[axis], point[axis]);
      max[axis] = Math.max(max[axis], point[axis]);
    }
  }
  return { min, max, centroid: min.map((value, axis) => (value + max[axis]) * 0.5) };
}

export function buildBvh(triangles, leafSize = 8) {
  if (!triangles.length) throw new Error('Cannot build an SDF acceleration structure without triangles.');
  const records = triangles.map((triangle, index) => ({ triangle, index, ...triangleBounds(triangle) }));
  const reordered = [];
  const nodes = [];

  function emit(items) {
    const nodeIndex = nodes.length;
    const min = [Infinity, Infinity, Infinity];
    const max = [-Infinity, -Infinity, -Infinity];
    for (const item of items) {
      for (let axis = 0; axis < 3; axis++) {
        min[axis] = Math.min(min[axis], item.min[axis]);
        max[axis] = Math.max(max[axis], item.max[axis]);
      }
    }
    const node = { min, max, start: 0, count: 0, escape: 0 };
    nodes.push(node);
    if (items.length <= leafSize) {
      node.start = reordered.length;
      node.count = items.length;
      for (const item of items) reordered.push(item.triangle);
    } else {
      const extent = max.map((value, axis) => value - min[axis]);
      const axis = extent.indexOf(Math.max(...extent));
      items.sort((a, b) => a.centroid[axis] - b.centroid[axis]);
      const middle = Math.floor(items.length / 2);
      emit(items.slice(0, middle));
      emit(items.slice(middle));
    }
    node.escape = nodes.length;
    return nodeIndex;
  }

  emit(records);
  const triangleBytes = new ArrayBuffer(reordered.length * 96);
  const triangleFloats = new Float32Array(triangleBytes);
  const triangleInts = new Uint32Array(triangleBytes);
  for (let i = 0; i < reordered.length; i++) {
    const triangle = reordered[i];
    const base = i * 24;
    triangleFloats.set([
      ...triangle.a, 0,
      ...triangle.b, 0,
      ...triangle.c, 0,
      ...triangle.color,
      ...(triangle.surface || [0.5, 0, 0, 0]),
    ], base);
    triangleInts.set([
      triangle.group ?? 0,
      triangle.materialMetadata ?? triangle.cardId ?? 0,
      triangle.invertWinding ? 1 : 0,
      0,
    ], base + 20);
  }

  const nodeBytes = new ArrayBuffer(nodes.length * 48);
  const nodeFloats = new Float32Array(nodeBytes);
  const nodeInts = new Uint32Array(nodeBytes);
  for (let i = 0; i < nodes.length; i++) {
    const node = nodes[i];
    const base = i * 12;
    nodeFloats.set([...node.min, 0, ...node.max, 0], base);
    nodeInts.set([node.escape, node.start, node.count, 0], base + 8);
  }

  return {
    triangles: reordered,
    nodes,
    triangleBuffer: triangleBytes,
    nodeBuffer: nodeBytes,
  };
}

/** Build a material-carrying triangle BVH over every triangle in a static world vertex stream. */
export function buildWorldDistanceBvh(vertices) {
  if (!(vertices instanceof Float32Array) || vertices.length % (VEHICLE_VERTEX_STRIDE * 3) !== 0) {
    throw new Error('Static world geometry must be a Float32Array containing complete triangles.');
  }
  const triangles = new Array(vertices.length / (VEHICLE_VERTEX_STRIDE * 3));
  for (let triangleIndex = 0; triangleIndex < triangles.length; triangleIndex++) {
    const aOffset = triangleIndex * VEHICLE_VERTEX_STRIDE * 3;
    const bOffset = aOffset + VEHICLE_VERTEX_STRIDE;
    const cOffset = bOffset + VEHICLE_VERTEX_STRIDE;
    const visibilityId = Math.round(vertices[aOffset + 11]);
    const cardId = Math.round(vertices[aOffset + 3]);
    const emission = Math.max(vertices[aOffset + 14], 0);
    const a = [vertices[aOffset], vertices[aOffset + 1], vertices[aOffset + 2]];
    const b = [vertices[bOffset], vertices[bOffset + 1], vertices[bOffset + 2]];
    const c = [vertices[cOffset], vertices[cOffset + 1], vertices[cOffset + 2]];
    const edgeAB = b.map((value, axis) => value - a[axis]);
    const edgeAC = c.map((value, axis) => value - a[axis]);
    const faceNormal = [
      edgeAB[1] * edgeAC[2] - edgeAB[2] * edgeAC[1],
      edgeAB[2] * edgeAC[0] - edgeAB[0] * edgeAC[2],
      edgeAB[0] * edgeAC[1] - edgeAB[1] * edgeAC[0],
    ];
    const averageNormal = [0, 1, 2].map(axis =>
      vertices[aOffset + 4 + axis] + vertices[bOffset + 4 + axis] + vertices[cOffset + 4 + axis]);
    const invertWinding = faceNormal.reduce((sum, value, axis) => sum + value * averageNormal[axis], 0) < 0;
    triangles[triangleIndex] = {
      a,
      b,
      c,
      color: [vertices[aOffset + 8], vertices[aOffset + 9], vertices[aOffset + 10], emission * 8],
      surface: [vertices[aOffset + 12], vertices[aOffset + 13], emission, vertices[aOffset + 15]],
      group: visibilityId,
      materialMetadata: ((visibilityId & 0xff) << 16) | (cardId & 0xffff),
      invertWinding,
    };
  }
  const bvh = buildBvh(triangles);
  return {
    triangleBuffer: bvh.triangleBuffer,
    nodeBuffer: bvh.nodeBuffer,
    triangleCount: triangles.length,
    nodeCount: bvh.nodes.length,
  };
}

function addVertex(vertices, position, normal, color, roughness, metallic, emission, dynamic = false, visibilityId = 0, surfaceCardId = 0) {
  vertices.push(
    position[0], position[1], position[2], surfaceCardId,
    normal[0], normal[1], normal[2], 0,
    color[0], color[1], color[2], visibilityId,
    roughness, metallic, emission, dynamic ? 1 : 0,
  );
}

function addQuad(vertices, corners, normal, material, visibilityId = 0) {
  const [color, roughness = 0.8, metallic = 0, emission = 0] = material;
  const candidates = vertices.surfaceCardCandidates;
  const origin = corners[0];
  const edgeU = corners[3].map((value, axis) => value - origin[axis]);
  const edgeV = corners[1].map((value, axis) => value - origin[axis]);
  const lengthU = Math.hypot(...edgeU);
  const lengthV = Math.hypot(...edgeV);

  // Keep one radiance grid local to a useful world-space patch. A city-ground
  // quad may span hundreds of metres; assigning it one 4x4 cache would smear
  // lighting across whole blocks and bake moving-vehicle influence into far tiles.
  if (emission <= 0.001 && candidates && candidates.length < MAX_SURFACE_CARDS &&
      (lengthU > SURFACE_CARD_MAX_EDGE || lengthV > SURFACE_CARD_MAX_EDGE)) {
    const segmentsU = Math.max(1, Math.ceil(lengthU / SURFACE_CARD_MAX_EDGE));
    const segmentsV = Math.max(1, Math.ceil(lengthV / SURFACE_CARD_MAX_EDGE));
    const pointAt = (u, v) => {
      const edge0 = corners[0].map((value, axis) => value * (1 - u) + corners[3][axis] * u);
      const edge1 = corners[1].map((value, axis) => value * (1 - u) + corners[2][axis] * u);
      return edge0.map((value, axis) => value * (1 - v) + edge1[axis] * v);
    };
    for (let v = 0; v < segmentsV; v++) {
      const v0 = v / segmentsV;
      const v1 = (v + 1) / segmentsV;
      for (let u = 0; u < segmentsU; u++) {
        const u0 = u / segmentsU;
        const u1 = (u + 1) / segmentsU;
        addQuad(vertices, [pointAt(u0, v0), pointAt(u0, v1), pointAt(u1, v1), pointAt(u1, v0)],
          normal, material, visibilityId);
      }
    }
    return;
  }

  const surfaceCardId = emission <= 0.001 && candidates && candidates.length < MAX_SURFACE_CARDS
    ? candidates.length + 1
    : 0;
  if (surfaceCardId > 0) {
    const invU = 1 / Math.max(edgeU.reduce((sum, value) => sum + value * value, 0), 1e-8);
    const invV = 1 / Math.max(edgeV.reduce((sum, value) => sum + value * value, 0), 1e-8);
    const vertexStart = vertices.length / VEHICLE_VERTEX_STRIDE;
    candidates.push({
      vertexStart,
      data: [
        ...origin, 0,
        ...edgeU, invU,
        ...edgeV, invV,
        ...normal, roughness,
        ...color, metallic,
        color[0] * emission, color[1] * emission, color[2] * emission, emission,
      ],
    });
  }
  for (const index of [0, 1, 2, 0, 2, 3]) {
    addVertex(vertices, corners[index], normal, color, roughness, metallic, emission, false, visibilityId, surfaceCardId);
  }
}

function addBoxMesh(vertices, center, half, material, visibilityId = 0, yaw = 0) {
  const [x, y, z] = center;
  const [hx, hy, hz] = half;
  const c = Math.cos(yaw);
  const s = Math.sin(yaw);
  const point = ([px, py, pz]) => [x + c * px + s * pz, y + py, z - s * px + c * pz];
  const normal = ([nx, ny, nz]) => [c * nx + s * nz, ny, -s * nx + c * nz];
  const x0 = -hx, x1 = hx;
  const y0 = -hy, y1 = hy;
  const z0 = -hz, z1 = hz;
  const faces = [
    [[[x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1]], [1, 0, 0]],
    [[[x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [x0, y0, z0]], [-1, 0, 0]],
    [[[x0, y1, z0], [x0, y1, z1], [x1, y1, z1], [x1, y1, z0]], [0, 1, 0]],
    [[[x0, y0, z1], [x0, y0, z0], [x1, y0, z0], [x1, y0, z1]], [0, -1, 0]],
    [[[x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]], [0, 0, 1]],
    [[[x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0]], [0, 0, -1]],
  ];
  for (const [corners, faceNormal] of faces) {
    addQuad(vertices, corners.map(point), normal(faceNormal), material, visibilityId);
  }
}

function addGroundQuad(vertices, x0, x1, z0, z1, y, material) {
  addQuad(vertices, [[x0, y, z0], [x0, y, z1], [x1, y, z1], [x1, y, z0]], [0, 1, 0], material);
}

function cityHash(cellX, cellZ, salt = 0) {
  const value = Math.sin(cellX * 127.1 + cellZ * 311.7 + salt * 74.7) * 43758.5453123;
  return value - Math.floor(value);
}

// Cell layouts are immutable world data. Memoizing by global coordinates means
// revisiting a streamed tile recreates the exact same architecture and props.
const cityCellLayoutCache = new Map();
function getCityCellLayout(cellX, cellZ) {
  const key = `${cellX},${cellZ}`;
  const cached = cityCellLayoutCache.get(key);
  if (cached) return cached;
  const block = CITY_BLOCK_SIZE;
  const randomA = cityHash(cellX, cellZ, 1);
  const randomB = cityHash(cellX, cellZ, 2);
  const half = [
    14 + cityHash(cellX, cellZ, 3) * 7,
    7 + cityHash(cellX, cellZ, 4) * 14,
    16 + cityHash(cellX, cellZ, 5) * 6,
  ];
  const center = [
    (cellX + 0.5 + (randomA - 0.5) * 0.12) * block,
    half[1],
    (cellZ + 0.5 + (randomB - 0.5) * 0.12) * block,
  ];
  const carSide = cityHash(cellX, cellZ, 7) < 0.5 ? -1 : 1;
  const carColors = [
    [[0.58, 0.13, 0.09], 0.3, 0.34, 0],
    [[0.08, 0.26, 0.39], 0.25, 0.48, 0],
    [[0.48, 0.49, 0.43], 0.28, 0.40, 0],
    [[0.16, 0.34, 0.24], 0.32, 0.26, 0],
  ];
  const layout = Object.freeze({
    center: Object.freeze(center),
    half: Object.freeze(half),
    paletteIndex: Math.floor(cityHash(cellX, cellZ, 6) * 5),
    buildingStyle: Math.floor(cityHash(cellX, cellZ, 11) * 4),
    hasPenthouse: (Math.abs(cellX + cellZ) % 2) === 0,
    hasRobot: ((cellX + 2 * cellZ) % 5 + 5) % 5 === 0,
    robotColorIndex: Math.floor(cityHash(cellX, cellZ, 17) * 4),
    carSide,
    carCenter: Object.freeze([
      cellX * block + carSide * 10.2,
      0.72,
      center[2] + (cityHash(cellX, cellZ, 8) - 0.5) * 28,
    ]),
    carYaw: cityHash(cellX, cellZ, 9) < 0.2 ? Math.PI * 0.5 : 0,
    carColor: carColors[Math.floor(cityHash(cellX, cellZ, 10) * carColors.length)],
  });
  cityCellLayoutCache.set(key, layout);
  return layout;
}

function localPoint(center, yaw, point) {
  const c = Math.cos(yaw);
  const s = Math.sin(yaw);
  return [center[0] + c * point[0] + s * point[2], center[1] + point[1], center[2] - s * point[0] + c * point[2]];
}

function localNormal(yaw, normal) {
  const c = Math.cos(yaw);
  const s = Math.sin(yaw);
  return [c * normal[0] + s * normal[2], normal[1], -s * normal[0] + c * normal[2]];
}

function addBuildingWindows(vertices, center, half, yaw, visibilityId, paletteIndex) {
  const glass = [
    [[0.035, 0.12, 0.17], 0.24, 0.42, 0],
    [[0.07, 0.20, 0.25], 0.28, 0.36, 0],
    [[0.25, 0.20, 0.12], 0.34, 0.18, 0],
    [[0.10, 0.25, 0.26], 0.20, 0.48, 0],
  ];
  const rows = Math.max(3, Math.min(8, Math.floor((half[1] * 2 - 2.5) / 3.0)));
  const columnsX = Math.max(3, Math.floor((half[0] * 2 - 4) / 4.2));
  const columnsZ = Math.max(3, Math.floor((half[2] * 2 - 4) / 4.2));
  const windowWidth = 1.35;
  const windowHeight = 1.45;
  const startY = -half[1] + 2.2;
  const addWindow = (corners, normal, variant) => {
    addQuad(vertices, corners.map(point => localPoint(center, yaw, point)), localNormal(yaw, normal), glass[variant], visibilityId);
  };

  for (let row = 0; row < rows; row++) {
    const y = startY + row * 3.0;
    for (let column = 0; column < columnsX; column++) {
      const x = -half[0] + 2.8 + column * ((half[0] * 2 - 5.6) / Math.max(columnsX - 1, 1));
      const variant = Math.floor(cityHash(paletteIndex, row, column + 1) * glass.length);
      for (const side of [-1, 1]) {
        const z = side * (half[2] + 0.025);
        const normal = [0, 0, side];
        addWindow([
          [x - windowWidth * 0.5, y, z], [x + windowWidth * 0.5, y, z],
          [x + windowWidth * 0.5, y + windowHeight, z], [x - windowWidth * 0.5, y + windowHeight, z],
        ], normal, variant);
      }
    }
    for (let column = 0; column < columnsZ; column++) {
      const z = -half[2] + 2.8 + column * ((half[2] * 2 - 5.6) / Math.max(columnsZ - 1, 1));
      const variant = Math.floor(cityHash(paletteIndex + 11, row, column + 1) * glass.length);
      for (const side of [-1, 1]) {
        const x = side * (half[0] + 0.025);
        const normal = [side, 0, 0];
        addWindow([
          [x, y, z - windowWidth * 0.5], [x, y, z + windowWidth * 0.5],
          [x, y + windowHeight, z + windowWidth * 0.5], [x, y + windowHeight, z - windowWidth * 0.5],
        ], normal, variant);
      }
    }
  }
}

function addTireMesh(vertices, carCenter, yaw, localCenter, visibilityId) {
  const radius = 0.38;
  const width = 0.22;
  const segments = 10;
  const tire = [[0.025, 0.031, 0.034], 0.95, 0.02, 0];
  for (let segment = 0; segment < segments; segment++) {
    const a0 = segment / segments * Math.PI * 2;
    const a1 = (segment + 1) / segments * Math.PI * 2;
    const middle = (a0 + a1) * 0.5;
    const point = (x, angle) => localPoint(carCenter, yaw, [
      localCenter[0] + x,
      localCenter[1] + radius * Math.cos(angle),
      localCenter[2] + radius * Math.sin(angle),
    ]);
    const normal = localNormal(yaw, [0, Math.cos(middle), Math.sin(middle)]);
    addQuad(vertices, [
      point(-width * 0.5, a0), point(width * 0.5, a0),
      point(width * 0.5, a1), point(-width * 0.5, a1),
    ], normal, tire, visibilityId);
  }
}

function addBuildingArchitecture(vertices, center, half, visibilityId, style) {
  const trim = [
    [[0.56, 0.54, 0.46], 0.7, 0.12, 0],
    [[0.17, 0.20, 0.20], 0.72, 0.12, 0],
    [[0.43, 0.35, 0.24], 0.62, 0.22, 0],
    [[0.67, 0.68, 0.62], 0.7, 0.08, 0],
  ][style % 4];
  const dark = [[0.035, 0.055, 0.058], 0.32, 0.38, 0];
  const canopy = [
    [[0.75, 0.21, 0.10], 0.48, 0.06, 0],
    [[0.12, 0.36, 0.41], 0.38, 0.16, 0],
    [[0.75, 0.62, 0.33], 0.55, 0.10, 0],
    [[0.30, 0.45, 0.31], 0.55, 0.12, 0],
  ][style % 4];
  const [hx, hy, hz] = half;
  const addFacadeBox = (localCenter, localHalf, material, yaw = 0) => {
    addBoxMesh(vertices, localPoint(center, yaw, localCenter), localHalf, material, visibilityId, yaw);
  };

  // Deep entry portal, sign band, canopy, and storefront mullions give each
  // street-facing mass a readable ground-floor use instead of a blank box.
  const frontZ = hz + 0.16;
  addFacadeBox([0, 1.25, frontZ], [0.92, 1.25, 0.10], dark);
  addFacadeBox([0, 2.62, frontZ + 0.03], [1.24, 0.11, 0.16], trim);
  addFacadeBox([0, 3.15, hz + 0.92], [Math.min(half[0] * 0.47, 5.2), 0.16, 0.88], canopy);
  addFacadeBox([0, 3.30, hz + 0.92], [Math.min(half[0] * 0.47, 5.2), 0.045, 0.90], trim);
  for (const signX of [-1, 1]) addFacadeBox([signX * 1.1, 2.15, frontZ + 0.02], [0.10, 0.92, 0.12], trim);

  // Continuous slab bands, corner piers, and projecting balcony rails add depth
  // to multiple floors while keeping each building's analytic SDF budget compact.
  const floorCount = Math.max(3, Math.floor((hy * 2) / 3));
  for (let floor = 1; floor < floorCount; floor++) {
    const y = floor * 3.0 + 0.12;
    if (y >= hy * 2 - 0.4) continue;
    addFacadeBox([0, y, hz + 0.10], [hx, 0.075, 0.13], trim);
    addFacadeBox([0, y, -hz - 0.10], [hx, 0.075, 0.13], trim);
    addFacadeBox([hx + 0.10, y, 0], [0.13, 0.075, hz], trim);
    addFacadeBox([-hx - 0.10, y, 0], [0.13, 0.075, hz], trim);
    if (style === 2 || (style === 3 && floor % 2 === 0)) {
      for (const side of [-1, 1]) {
        const balconyZ = side * (hz + 0.56);
        addFacadeBox([0, y + 0.16, balconyZ], [1.9, 0.10, 0.53], trim);
        addFacadeBox([0, y + 0.62, side * (hz + 1.00)], [1.9, 0.055, 0.055], trim);
        for (const postX of [-1.78, -0.9, 0, 0.9, 1.78]) {
          addFacadeBox([postX, y + 0.39, side * (hz + 1.00)], [0.045, 0.24, 0.045], trim);
        }
      }
    }
  }

  const pierCount = Math.max(3, Math.floor((hx * 2) / 8));
  for (const side of [-1, 1]) {
    for (let index = 0; index <= pierCount; index++) {
      const x = -hx + index * (hx * 2 / pierCount);
      addFacadeBox([x, hy, side * (hz + 0.10)], [0.12, hy - 0.12, 0.14], trim);
    }
  }

  // Roof parapets, stepped mechanical plant, and water tank silhouettes.
  const roofY = hy * 2 + 0.28;
  addFacadeBox([0, roofY, hz - 0.18], [hx, 0.28, 0.18], trim);
  addFacadeBox([0, roofY, -hz + 0.18], [hx, 0.28, 0.18], trim);
  addFacadeBox([hx - 0.18, roofY, 0], [0.18, 0.28, hz], trim);
  addFacadeBox([-hx + 0.18, roofY, 0], [0.18, 0.28, hz], trim);
  const plantMaterial = [[0.12, 0.16, 0.16], 0.62, 0.30, 0];
  const plantScale = Math.min(1, hx / 24, hz / 24);
  addFacadeBox([-hx * 0.38, hy * 2 + 0.62, -hz * 0.26], [2.0 * plantScale, 0.62, 1.45 * plantScale], plantMaterial);
  addFacadeBox([hx * 0.28, hy * 2 + 0.48, -hz * 0.34], [1.55 * plantScale, 0.48, 1.20 * plantScale], plantMaterial);
  if (style === 1 || style === 3) {
    addFacadeBox([hx * 0.36, hy * 2 + 1.10, hz * 0.35], [0.72, 1.05, 0.72], [[0.25, 0.29, 0.27], 0.7, 0.12, 0]);
    addFacadeBox([hx * 0.36, hy * 2 + 2.18, hz * 0.35], [0.79, 0.08, 0.79], trim);
  }
  // A hanging shop sign and side-mounted service boxes create distinct silhouettes.
  addFacadeBox([hx + 0.12, 3.3, -hz * 0.32], [0.12, 0.85, 0.72], canopy);
  addFacadeBox([-hx - 0.10, 2.8, hz * 0.22], [0.10, 0.52, 0.62], dark);
}

function addBenchMeshes(vertices, center, yaw, visibilityId) {
  const wood = [[0.39, 0.22, 0.11], 0.72, 0.08, 0];
  const metal = [[0.12, 0.17, 0.17], 0.38, 0.60, 0];
  const add = (localCenter, half, material) => addBoxMesh(vertices, localPoint(center, yaw, localCenter), half, material, visibilityId, yaw);
  for (let slat = 0; slat < 4; slat++) add([0, 0.74, -0.24 + slat * 0.16], [1.0, 0.045, 0.065], wood);
  for (const x of [-0.78, 0.78]) {
    for (const z of [-0.23, 0.23]) add([x, 0.30, z], [0.075, 0.30, 0.075], metal);
    add([x, 0.93, -0.30], [0.065, 0.53, 0.065], metal);
  }
  for (let board = 0; board < 3; board++) add([0, 0.90 + board * 0.20, -0.30], [0.94, 0.075, 0.075], wood);
}

function addChairMeshes(vertices, center, yaw, visibilityId, includeSeat = true) {
  const seat = [[0.33, 0.37, 0.31], 0.66, 0.08, 0];
  const frame = [[0.10, 0.16, 0.16], 0.4, 0.56, 0];
  const add = (localCenter, half, material) => addBoxMesh(vertices, localPoint(center, yaw, localCenter), half, material, visibilityId, yaw);
  if (includeSeat) add([0, 0.67, 0], [0.42, 0.035, 0.42], seat);
  for (const x of [-0.33, 0.33]) {
    for (const z of [-0.32, 0.32]) add([x, 0.28, z], [0.055, 0.28, 0.055], frame);
    add([x, 0.94, -0.34], [0.055, 0.48, 0.055], frame);
  }
  for (let board = 0; board < 3; board++) add([0, 0.84 + board * 0.20, -0.34], [0.38, 0.075, 0.055], seat);
}

function addTrashBinMeshes(vertices, center, yaw, visibilityId) {
  const bin = [[0.08, 0.16, 0.15], 0.58, 0.42, 0];
  const trim = [[0.21, 0.27, 0.25], 0.38, 0.62, 0];
  const add = (localCenter, half, material) => addBoxMesh(vertices, localPoint(center, yaw, localCenter), half, material, visibilityId, yaw);
  add([0, 1.18, 0], [0.39, 0.08, 0.39], trim);
  add([0, 1.28, 0], [0.22, 0.045, 0.22], bin);
  add([0, 0.45, 0.352], [0.20, 0.16, 0.035], trim);
  add([0, 0.08, 0], [0.37, 0.08, 0.37], trim);
}

function addTrafficSignalLenses(vertices, headCenter, yaw, headVisibilityId) {
  const lensMaterials = [
    [[1.0, 0.025, 0.008], 0.22, 0.02, 5.0],
    [[1.0, 0.36, 0.015], 0.22, 0.02, 4.2],
    [[0.025, 0.92, 0.12], 0.22, 0.02, 3.8],
  ];
  for (let lamp = 0; lamp < lensMaterials.length; lamp++) {
    const lensCenter = localPoint(headCenter, yaw, [0, 0.44 - lamp * 0.42, 0.39]);
    addBoxMesh(vertices, lensCenter, [0.20, 0.15, 0.045], lensMaterials[lamp], headVisibilityId, yaw);
  }
}

export function buildWorldGeometry(centerX = 0, centerZ = 0) {
  const vertices = [];
  Object.defineProperty(vertices, 'surfaceCardCandidates', { value: [] });
  const shapes = [];
  const emissiveRanges = [];
  const streamCell = {
    x: Math.floor(centerX / CITY_BLOCK_SIZE),
    z: Math.floor(centerZ / CITY_BLOCK_SIZE),
  };
  const block = CITY_BLOCK_SIZE;
  const materialId = 63;
  const sdfContainsCell = (cellX, cellZ) =>
    Math.abs(cellX - streamCell.x) <= CITY_SDF_RADIUS && Math.abs(cellZ - streamCell.z) <= CITY_SDF_RADIUS;
  const sidewalk = [[0.31, 0.33, 0.31], 0.88, 0.01, 0];
  const road = [[0.075, 0.09, 0.095], 0.82, 0.02, 0];
  const curb = [[0.44, 0.46, 0.42], 0.86, 0.02, 0];
  const metal = [[0.075, 0.13, 0.15], 0.38, 0.48, 0];
  const robotMetal = [[0.035, 0.12, 0.16], 0.28, 0.68, 0];
  const warmLamp = [[1.0, 0.42, 0.12], 0.28, 0.02, 9.0];
  const coolLamp = [[0.16, 0.72, 1.0], 0.3, 0.02, 7.5];
  const glassColor = [[0.045, 0.18, 0.22], 0.24, 0.48, 0];
  const buildingColors = [
    [[0.25, 0.30, 0.29], 0.82, 0.05, 0],
    [[0.31, 0.24, 0.19], 0.84, 0.03, 0],
    [[0.18, 0.25, 0.31], 0.62, 0.16, 0],
    [[0.35, 0.33, 0.27], 0.88, 0.02, 0],
    [[0.22, 0.28, 0.25], 0.76, 0.08, 0],
  ];
  const visorColors = [
    [[0.08, 0.78, 1.0], 0.22, 0.12, 8.0],
    [[1.0, 0.38, 0.08], 0.24, 0.08, 8.0],
    [[0.25, 1.0, 0.46], 0.2, 0.1, 8.0],
    [[0.98, 0.72, 0.16], 0.24, 0.08, 8.0],
  ];

  const addSolid = (center, half, material, yaw = 0, options = {}) => {
    const firstVertex = vertices.length / VEHICLE_VERTEX_STRIDE;
    const shapeIndex = shapes.length;
    const visibilityId = shapeIndex + 1;
    addBoxMesh(vertices, center, half, material, visibilityId, yaw);
    shapes.push({
      center: center.slice(), half: half.slice(), yaw,
      color: material[0], roughness: material[1], metallic: material[2], emission: material[3],
      role: options.role || 'solid',
    });
    const vertexCount = vertices.length / VEHICLE_VERTEX_STRIDE - firstVertex;
    if (material[3] > 0.001) {
      emissiveRanges.push({
        firstVertex, vertexCount, center: center.slice(), shapeIndex,
        role: options.role || 'emissive', sampled: options.sampled !== false,
        scaleWithEmitter: options.scaleWithEmitter === true,
      });
    }
    return { shapeIndex, visibilityId };
  };
  const addCityBox = (center, half, material, yaw, inSdf, role, options = {}) => {
    if (inSdf) return addSolid(center, half, material, yaw, { ...options, role });
    const visibilityId = options.visibilityId ?? materialId;
    const firstVertex = vertices.length / VEHICLE_VERTEX_STRIDE;
    addBoxMesh(vertices, center, half, material, visibilityId, yaw);
    const vertexCount = vertices.length / VEHICLE_VERTEX_STRIDE - firstVertex;
    if (material[3] > 0.001) {
      emissiveRanges.push({
        firstVertex, vertexCount, center: center.slice(), shapeIndex: -1,
        role, sampled: false, scaleWithEmitter: false,
      });
    }
    return { shapeIndex: -1, visibilityId };
  };

  // Overlap neighboring road slabs so changing streamed cells never exposes a gap.
  const roadMinX = streamCell.x - CITY_STREAM_RADIUS - 1;
  const roadMaxX = streamCell.x + CITY_STREAM_RADIUS + 1;
  const roadMinZ = streamCell.z - CITY_STREAM_RADIUS - 1;
  const roadMaxZ = streamCell.z + CITY_STREAM_RADIUS + 1;
  const groundMinX = roadMinX * block;
  const groundMaxX = (roadMaxX + 1) * block;
  const groundMinZ = roadMinZ * block;
  const groundMaxZ = (roadMaxZ + 1) * block;
  addGroundQuad(vertices, groundMinX, groundMaxX, groundMinZ, groundMaxZ, -0.025, [[0.19, 0.23, 0.24], 0.9, 0.02, 0]);
  const roadHalf = 7.2;
  for (let index = roadMinX; index <= roadMaxX; index++) {
    const x = index * block;
    addGroundQuad(vertices, x - roadHalf, x + roadHalf, groundMinZ, groundMaxZ, -0.014, road);
    addGroundQuad(vertices, x - 11.5, x - roadHalf, groundMinZ, groundMaxZ, -0.010, sidewalk);
    addGroundQuad(vertices, x + roadHalf, x + 11.5, groundMinZ, groundMaxZ, -0.010, sidewalk);
    for (const side of [-1, 1]) {
      addBoxMesh(vertices, [x + side * 7.28, 0.075, (groundMinZ + groundMaxZ) * 0.5],
        [0.12, 0.075, (groundMaxZ - groundMinZ) * 0.5], curb, materialId);
    }
  }
  for (let index = roadMinZ; index <= roadMaxZ; index++) {
    const z = index * block;
    addGroundQuad(vertices, groundMinX, groundMaxX, z - roadHalf, z + roadHalf, -0.012, road);
    addGroundQuad(vertices, groundMinX, groundMaxX, z - 11.5, z - roadHalf, -0.008, sidewalk);
    addGroundQuad(vertices, groundMinX, groundMaxX, z + roadHalf, z + 11.5, -0.008, sidewalk);
    for (const side of [-1, 1]) {
      addBoxMesh(vertices, [(groundMinX + groundMaxX) * 0.5, 0.075, z + side * 7.28],
        [(groundMaxX - groundMinX) * 0.5, 0.075, 0.12], curb, materialId);
    }
  }

  let robotCount = 0;
  for (let cellX = streamCell.x - CITY_STREAM_RADIUS; cellX <= streamCell.x + CITY_STREAM_RADIUS; cellX++) {
    for (let cellZ = streamCell.z - CITY_STREAM_RADIUS; cellZ <= streamCell.z + CITY_STREAM_RADIUS; cellZ++) {
      const layout = getCityCellLayout(cellX, cellZ);
      const inSdf = sdfContainsCell(cellX, cellZ);
      const center = [...layout.center];
      const half = [...layout.half];
      const buildingMaterial = buildingColors[layout.paletteIndex];
      const building = addCityBox(center, half, buildingMaterial, 0, inSdf, 'building');
      addBuildingWindows(vertices, center, half, 0, building.visibilityId, layout.paletteIndex);
      addBuildingArchitecture(vertices, center, half, building.visibilityId, layout.buildingStyle);

      if (layout.hasPenthouse) {
        const penthouseHalf = [Math.max(11.5, half[0] * 0.66), 2.7, Math.max(12.5, half[2] * 0.66)];
        const penthouseCenter = [center[0], half[1] * 2 + 1.9, center[2]];
        const penthouseMaterial = buildingColors[(layout.paletteIndex + 2) % buildingColors.length];
        const penthouse = addCityBox(penthouseCenter, penthouseHalf, penthouseMaterial, 0, inSdf,
          'building-penthouse');
        addBuildingWindows(vertices, penthouseCenter, penthouseHalf, 0, penthouse.visibilityId, layout.paletteIndex + 7);
        addBoxMesh(vertices, [center[0], penthouseCenter[1] + penthouseHalf[1] + 0.20, center[2]],
          [penthouseHalf[0] + 0.25, 0.20, penthouseHalf[2] + 0.25], [metal[0], metal[1], metal[2], 0], penthouse.visibilityId);
      }

      // Each global cell owns a fixed parked vehicle and three pieces of street furniture.
      // Layouts are memoized by world coordinates, so revisiting a streamed tile restores
      // the same assets rather than rolling a new random scene.
      const carCenter = [...layout.carCenter];
      const carBody = addCityBox(carCenter, [1.02, 0.53, 2.05], layout.carColor, layout.carYaw,
        inSdf, 'parked-car');
      addBoxMesh(vertices, localPoint(carCenter, layout.carYaw, [0, 0.48, -0.08]),
        [0.79, 0.39, 1.05], glassColor, carBody.visibilityId, layout.carYaw);
      addBoxMesh(vertices, localPoint(carCenter, layout.carYaw, [0, 0.12, 1.50]),
        [0.82, 0.12, 0.34], [[0.78, 0.62, 0.29], 0.42, 0.2, 0], carBody.visibilityId, layout.carYaw);
      for (const localX of [-0.98, 0.98]) {
        for (const localZ of [-1.34, 1.34]) addTireMesh(vertices, carCenter, layout.carYaw,
          [localX, -0.27, localZ], carBody.visibilityId);
      }

      const offsetX = cellX - streamCell.x;
      const offsetZ = cellZ - streamCell.z;
      const benchSdf = inSdf && Math.abs(offsetX) === 1 && Math.abs(offsetZ) === 1;
      const chairSdf = inSdf && Math.abs(offsetX) + Math.abs(offsetZ) === 1;
      const binSdf = inSdf && ((offsetX === 0 && offsetZ === 0) ||
        (offsetX === -1 && offsetZ === 0) || (offsetX === 1 && offsetZ === 0) ||
        (offsetX === 0 && offsetZ === 1));
      const benchCenter = [cellX * block + 9.4, 0, layout.center[2] + 30];
      const bench = addCityBox([benchCenter[0], 0.58, benchCenter[2]], [0.98, 0.10, 0.34],
        [[0.22, 0.20, 0.16], 0.70, 0.18, 0], Math.PI * 0.5, benchSdf, 'bench');
      addBenchMeshes(vertices, benchCenter, Math.PI * 0.5, bench.visibilityId);

      const chairCenter = [(cellX + 1) * block - 9.4, 0, layout.center[2] - 30];
      const chair = addCityBox([chairCenter[0], 0.55, chairCenter[2]], [0.42, 0.09, 0.42],
        [[0.18, 0.25, 0.23], 0.66, 0.12, 0], Math.PI * 0.5, chairSdf, 'chair');
      addChairMeshes(vertices, chairCenter, Math.PI * 0.5, chair.visibilityId, false);

      const binCenter = [cellX * block + 9.4, 0, layout.center[2] - 30];
      const bin = addCityBox([binCenter[0], 0.58, binCenter[2]], [0.34, 0.58, 0.34],
        [[0.08, 0.16, 0.15], 0.58, 0.42, 0], 0, binSdf, 'trash-bin');
      addTrashBinMeshes(vertices, binCenter, 0, bin.visibilityId);

      if (layout.hasRobot) {
        const robotVisibilityId = 56 + robotCount;
        robotCount++;
        const robotX = cellX * block - layout.carSide * 12.0;
        const robotZ = cellZ * block + 40;
        const robotYaw = layout.carSide > 0 ? Math.PI * 0.5 : -Math.PI * 0.5;
        const robotBase = [robotX, 0, robotZ];
        const robot = addCityBox([robotX, 0.9, robotZ], [0.56, 0.88, 0.36],
          [robotMetal[0], robotMetal[1], robotMetal[2], 0], robotYaw, false, 'pedestrian-robot',
          { visibilityId: robotVisibilityId });
        addBoxMesh(vertices, localPoint(robotBase, robotYaw, [0, 0.98, 0]), [0.37, 0.43, 0.28],
          [robotMetal[0], robotMetal[1], robotMetal[2], 0], robot.visibilityId, robotYaw);
        addBoxMesh(vertices, localPoint(robotBase, robotYaw, [0, 1.58, 0]), [0.27, 0.24, 0.23],
          [metal[0], metal[1], metal[2], 0], robot.visibilityId, robotYaw);
        addBoxMesh(vertices, localPoint(robotBase, robotYaw, [-0.48, 0.98, 0]), [0.13, 0.36, 0.15],
          [robotMetal[0], robotMetal[1], robotMetal[2], 0], robot.visibilityId, robotYaw);
        addBoxMesh(vertices, localPoint(robotBase, robotYaw, [0.48, 0.98, 0]), [0.13, 0.36, 0.15],
          [robotMetal[0], robotMetal[1], robotMetal[2], 0], robot.visibilityId, robotYaw);
        addBoxMesh(vertices, localPoint(robotBase, robotYaw, [-0.17, 0.27, 0]), [0.14, 0.31, 0.16],
          [metal[0], metal[1], metal[2], 0], robot.visibilityId, robotYaw);
        addBoxMesh(vertices, localPoint(robotBase, robotYaw, [0.17, 0.27, 0]), [0.14, 0.31, 0.16],
          [metal[0], metal[1], metal[2], 0], robot.visibilityId, robotYaw);
        addCityBox(localPoint(robotBase, robotYaw, [0, 1.57, 0.41]), [0.22, 0.12, 0.07],
          visorColors[layout.robotColorIndex], robotYaw, false, 'robot-emitter',
          { visibilityId: robot.visibilityId, sampled: false, scaleWithEmitter: false });
      }
    }
  }

  // Every nearby road junction gets a four-way signal set. The center junction
  // carries SDF shapes; the surrounding fixtures remain detailed raster props.
  const intersectionMin = streamCell.x - CITY_STREAM_RADIUS;
  const intersectionMax = streamCell.x + CITY_STREAM_RADIUS;
  for (let intersectionX = intersectionMin; intersectionX <= intersectionMax; intersectionX++) {
    for (let intersectionZ = streamCell.z - CITY_STREAM_RADIUS; intersectionZ <= streamCell.z + CITY_STREAM_RADIUS; intersectionZ++) {
      const centerIntersection = intersectionX === streamCell.x && intersectionZ === streamCell.z;
      for (const signX of [-1, 1]) {
        for (const signZ of [-1, 1]) {
          const x = intersectionX * block + signX * 8.7;
          const z = intersectionZ * block + signZ * 8.7;
          addCityBox([x, 3.45, z], [0.10, 3.45, 0.10], [metal[0], metal[1], metal[2], 0],
            0, centerIntersection, 'traffic-light-pole');
          const yaw = signX === 1 ? -Math.PI * 0.5 : signX === -1 ? Math.PI * 0.5 : signZ > 0 ? Math.PI : 0;
          const headCenter = [x, 5.75, z];
          const head = addCityBox(headCenter, [0.34, 0.78, 0.37], [[0.035, 0.045, 0.045], 0.42, 0.22, 0],
            yaw, centerIntersection, 'traffic-light-head');
          addTrafficSignalLenses(vertices, headCenter, yaw, head.visibilityId);
        }
      }
    }
  }

  const sampledLampCells = new Set();
  for (let cellX = streamCell.x - 1; cellX <= streamCell.x + 1; cellX++) {
    for (let cellZ = streamCell.z - 1; cellZ <= streamCell.z; cellZ++) sampledLampCells.add(`${cellX},${cellZ}`);
  }
  for (let cellX = streamCell.x - CITY_STREAM_RADIUS; cellX <= streamCell.x + CITY_STREAM_RADIUS; cellX++) {
    for (let cellZ = streamCell.z - CITY_STREAM_RADIUS; cellZ <= streamCell.z + CITY_STREAM_RADIUS; cellZ++) {
      const sampled = sampledLampCells.has(`${cellX},${cellZ}`);
      const layout = getCityCellLayout(cellX, cellZ);
      const side = layout.carSide;
      const x = cellX * block + side * 8.5;
      const z = cellZ * block + 20;
      addCityBox([x, 3.6, z], [0.10, 3.6, 0.10], [metal[0], metal[1], metal[2], 0],
        0, sampled, 'streetlight-pole');
      const source = cityHash(cellX, cellZ, 26) < 0.5 ? coolLamp : warmLamp;
      addCityBox([x + side * 0.8, 7.2, z], [2.2, 0.08, 0.56], source,
        0, sampled, 'streetlight-emitter', { sampled, scaleWithEmitter: sampled });
    }
  }

  const surfaceCardCount = vertices.surfaceCardCandidates.length;
  const surfaceCards = new Float32Array(surfaceCardCount * SURFACE_CARD_FLOATS);
  for (let cardIndex = 0; cardIndex < surfaceCardCount; cardIndex++) {
    const card = vertices.surfaceCardCandidates[cardIndex];
    surfaceCards.set(card.data, cardIndex * SURFACE_CARD_FLOATS);
  }

  return {
    vertices: new Float32Array(vertices),
    surfaceCards,
    surfaceCardCount,
    shapes,
    emissiveRanges,
    streamCell,
    buildingCount: (CITY_STREAM_RADIUS * 2 + 1) ** 2,
    parkedCarCount: (CITY_STREAM_RADIUS * 2 + 1) ** 2,
    robotCount,
    streetlightCount: (CITY_STREAM_RADIUS * 2 + 1) ** 2,
    trafficLightCount: (CITY_STREAM_RADIUS * 2 + 1) ** 2 * 4,
    benchCount: (CITY_STREAM_RADIUS * 2 + 1) ** 2,
    chairCount: (CITY_STREAM_RADIUS * 2 + 1) ** 2,
    trashBinCount: (CITY_STREAM_RADIUS * 2 + 1) ** 2,
  };
}

export function scaleWorldEmitterGeometry(world, emitterSize) {
  const scale = Math.max(0.2, Math.min(emitterSize, 2.5));
  const vertices = world.vertices;
  for (const range of world.emissiveRanges || []) {
    if (!range.scaleWithEmitter) continue;
    const first = range.firstVertex * VEHICLE_VERTEX_STRIDE;
    const end = first + range.vertexCount * VEHICLE_VERTEX_STRIDE;
    if (!range.basePositions) {
      range.basePositions = new Float32Array(range.vertexCount * 3);
      for (let offset = first, base = 0; offset < end; offset += VEHICLE_VERTEX_STRIDE, base += 3) {
        range.basePositions.set(vertices.subarray(offset, offset + 3), base);
      }
    }
    for (let offset = first, base = 0; offset < end; offset += VEHICLE_VERTEX_STRIDE, base += 3) {
      for (let axis = 0; axis < 3; axis++) {
        vertices[offset + axis] = range.center[axis] +
          (range.basePositions[base + axis] - range.center[axis]) * scale;
      }
    }
    const shape = world.shapes[range.shapeIndex];
    if (shape) {
      if (!range.baseHalf) range.baseHalf = shape.half.slice();
      shape.half = range.baseHalf.map(value => value * scale);
    }
  }
  world.emitterSize = scale;
  return vertices;
}

export function createVehicleSdfMetadata(vehicle) {
  const min = vehicle.boundsMin;
  const max = vehicle.boundsMax;
  const dimensions = VEHICLE_SDF_DIMS;
  const buffer = new ArrayBuffer(48);
  new Float32Array(buffer).set([...min, 0, ...max, 0], 0);
  new Uint32Array(buffer).set([...dimensions, vehicle.groupCount], 8);
  return buffer;
}
