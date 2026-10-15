const VERTEX_STRIDE = 16;
const EMITTER_FLOATS = 28;
const EMITTER_BYTES = EMITTER_FLOATS * Float32Array.BYTES_PER_ELEMENT;

function triangleCross(a, b, c) {
  const ab = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
  const ac = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
  return [
    ab[1] * ac[2] - ab[2] * ac[1],
    ab[2] * ac[0] - ab[0] * ac[2],
    ab[0] * ac[1] - ab[1] * ac[0],
  ];
}

function triangleArea(a, b, c) {
  return Math.hypot(...triangleCross(a, b, c)) * 0.5;
}

function facesUpward(a, b, c) {
  const cross = triangleCross(a, b, c);
  const magnitude = Math.hypot(...cross);
  // Ceiling-panel backfaces do not light the room below. Keep downward and
  // lateral faces, while dropping only surfaces whose normals point upward.
  return magnitude > 1e-10 && cross[1] / magnitude > 0.2;
}

function luminance(color) {
  return Math.max(0, color[0] * 0.2126 + color[1] * 0.7152 + color[2] * 0.0722);
}

function aliasTable(weights) {
  const count = weights.length;
  const total = weights.reduce((sum, weight) => sum + weight, 0);
  const probabilities = weights.map(weight => weight / Math.max(total, 1e-20));
  const thresholds = new Array(count).fill(1);
  const aliases = Array.from({ length: count }, (_, index) => index);
  const scaled = probabilities.map(probability => probability * count);
  const small = [];
  const large = [];

  for (let index = 0; index < count; index++) {
    (scaled[index] < 1 ? small : large).push(index);
  }
  while (small.length && large.length) {
    const low = small.pop();
    const high = large.pop();
    thresholds[low] = scaled[low];
    aliases[low] = high;
    scaled[high] = scaled[high] + scaled[low] - 1;
    (scaled[high] < 1 ? small : large).push(high);
  }
  for (const index of [...small, ...large]) {
    thresholds[index] = 1;
    aliases[index] = index;
  }
  return { probabilities, thresholds, aliases, total };
}

function pushSource(sources, a, b, c, radiance, center, isVehicle, group) {
  const area = triangleArea(a, b, c);
  const power = area * luminance(radiance);
  if (!(area > 1e-10) || !(power > 1e-10)) return;
  sources.push({ a, b, c, radiance, center, isVehicle, group, area, power });
}

/**
 * Builds a Walker alias table over sampled room-facing emissive triangles in the
 * static scene and all emissive ControlVehicle triangles. Upward-facing panel backs
 * are excluded because they cannot illuminate the space below. Each source is selected
 * in O(1) by the shader; selection weights are emitted power (area × luminance),
 * and the stored PDF keeps next-event estimates unbiased.
 */
export function buildEmissiveSourceBuffer(world, vehicle) {
  const sources = [];
  const worldVertices = world.vertices;

  for (const range of world.emissiveRanges || []) {
    if (range.sampled === false) continue;
    const first = range.firstVertex * VERTEX_STRIDE;
    const end = first + range.vertexCount * VERTEX_STRIDE;
    for (let offset = first; offset + VERTEX_STRIDE * 3 <= end; offset += VERTEX_STRIDE * 3) {
      const bOffset = offset + VERTEX_STRIDE;
      const cOffset = offset + VERTEX_STRIDE * 2;
      const aBase = ((offset - first) / VERTEX_STRIDE) * 3;
      const bBase = aBase + 3;
      const cBase = aBase + 6;
      const basePositions = range.basePositions;
      const a = basePositions
        ? Array.from(basePositions.subarray(aBase, aBase + 3))
        : [worldVertices[offset], worldVertices[offset + 1], worldVertices[offset + 2]];
      const b = basePositions
        ? Array.from(basePositions.subarray(bBase, bBase + 3))
        : [worldVertices[bOffset], worldVertices[bOffset + 1], worldVertices[bOffset + 2]];
      const c = basePositions
        ? Array.from(basePositions.subarray(cBase, cBase + 3))
        : [worldVertices[cOffset], worldVertices[cOffset + 1], worldVertices[cOffset + 2]];
      if (facesUpward(a, b, c)) continue;
      const radiance = [0, 1, 2].map(channel => {
        const albedo = (worldVertices[offset + 8 + channel] + worldVertices[bOffset + 8 + channel] + worldVertices[cOffset + 8 + channel]) / 3;
        const emission = (worldVertices[offset + 14] + worldVertices[bOffset + 14] + worldVertices[cOffset + 14]) / 3;
        return albedo * emission;
      });
      pushSource(sources, a, b, c, radiance, range.center, false, range.shapeIndex);
    }
  }

  if (vehicle?.triangles?.length) {
    const groupBounds = new Map();
    for (const triangle of vehicle.triangles) {
      const group = triangle.group ?? 0;
      let bounds = groupBounds.get(group);
      if (!bounds) {
        bounds = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
        groupBounds.set(group, bounds);
      }
      for (const point of [triangle.a, triangle.b, triangle.c]) {
        for (let axis = 0; axis < 3; axis++) {
          bounds.min[axis] = Math.min(bounds.min[axis], point[axis]);
          bounds.max[axis] = Math.max(bounds.max[axis], point[axis]);
        }
      }
    }
    for (const triangle of vehicle.triangles) {
      const emission = triangle.surface?.[2] || 0;
      if (emission <= 1e-3) continue;
      const group = triangle.group ?? 0;
      const bounds = groupBounds.get(group);
      const center = bounds.min.map((value, axis) => (value + bounds.max[axis]) * 0.5);
      const radiance = triangle.color.slice(0, 3).map(value => value * emission);
      pushSource(sources, triangle.a, triangle.b, triangle.c, radiance, center, true, group);
    }
  }

  if (!sources.length) {
    return { buffer: new ArrayBuffer(EMITTER_BYTES), count: 0, totalPower: 0 };
  }

  const table = aliasTable(sources.map(source => source.power));
  const buffer = new ArrayBuffer(sources.length * EMITTER_BYTES);
  const floats = new Float32Array(buffer);
  const uints = new Uint32Array(buffer);
  for (let index = 0; index < sources.length; index++) {
    const source = sources[index];
    const base = index * EMITTER_FLOATS;
    floats.set([
      ...source.a, 0,
      ...source.b, 0,
      ...source.c, 0,
      ...source.radiance, 0,
      ...source.center, 0,
      source.area, table.thresholds[index], table.probabilities[index], 0,
    ], base);
    uints.set([table.aliases[index], source.isVehicle ? 1 : 0, source.group, 0], base + 24);
  }
  return { buffer, count: sources.length, totalPower: table.total };
}

export const EMISSIVE_SOURCE_BYTES = EMITTER_BYTES;
