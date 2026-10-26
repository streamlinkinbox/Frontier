// Chunked marching-tetrahedra polygonisation of the SDF volume.
// Same construction as SdfCliffLab/hybrid.js (one consistent tet split, edge
// crossings from shared node values), so neighbouring chunks produce
// coincident border vertices — watertight with no stitching. Normals are the
// trilinearly interpolated SDF gradient, which is what keeps cliff faces
// smooth instead of terraced; the banding this editor avoids comes from
// geology-free displacement, not from here.
//
// Vertex layout (interleaved, stride 14 floats):
//   pos xyz | normal xyz | bed colour rgb | data: hardness, joint, sediment, baked SDF AO | moisture

const CORNER = [[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0], [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]];
const TETS = [[0, 5, 1, 6], [0, 1, 2, 6], [0, 2, 3, 6], [0, 3, 7, 6], [0, 7, 4, 6], [0, 4, 5, 6]];
export const VERT_STRIDE = 14;

export function buildChunkMesh(volume, cx, cy, cz) {
  const { sdf, nx, ny, spacing, origin, chunk } = volume;
  const x0 = cx * chunk, y0 = cy * chunk, z0 = cz * chunk;
  const x1 = Math.min(x0 + chunk, volume.nx - 1);
  const y1 = Math.min(y0 + chunk, volume.ny - 1);
  const z1 = Math.min(z0 + chunk, volume.nz - 1);
  const pos = [];
  const cv = new Float64Array(8), cp = new Float64Array(24);
  const tv = new Float64Array(4), tp = new Float64Array(12);
  const cross = new Float64Array(12);
  const inside = [], outside = [];

  function edgePoint(a, b, out, o) {
    const va = tv[a], vb = tv[b];
    const t = va / (va - vb);
    out[o] = tp[a * 3] + (tp[b * 3] - tp[a * 3]) * t;
    out[o + 1] = tp[a * 3 + 1] + (tp[b * 3 + 1] - tp[a * 3 + 1]) * t;
    out[o + 2] = tp[a * 3 + 2] + (tp[b * 3 + 2] - tp[a * 3 + 2]) * t;
  }
  function emitTri(a, b, c) {
    let ix = 0, iy = 0, iz = 0, ox = 0, oy = 0, oz = 0;
    for (const q of inside) { ix += tp[q * 3]; iy += tp[q * 3 + 1]; iz += tp[q * 3 + 2]; }
    for (const q of outside) { ox += tp[q * 3]; oy += tp[q * 3 + 1]; oz += tp[q * 3 + 2]; }
    const dx = ox / outside.length - ix / inside.length, dy = oy / outside.length - iy / inside.length, dz = oz / outside.length - iz / inside.length;
    const ux = cross[b] - cross[a], uy = cross[b + 1] - cross[a + 1], uz = cross[b + 2] - cross[a + 2];
    const vx = cross[c] - cross[a], vy = cross[c + 1] - cross[a + 1], vz = cross[c + 2] - cross[a + 2];
    const cnx = uy * vz - uz * vy, cny = uz * vx - ux * vz, cnz = ux * vy - uy * vx;
    if (cnx * dx + cny * dy + cnz * dz < 0) { const t = b; b = c; c = t; }
    pos.push(cross[a], cross[a + 1], cross[a + 2], cross[b], cross[b + 1], cross[b + 2], cross[c], cross[c + 1], cross[c + 2]);
  }

  for (let iz = z0; iz < z1; iz++) for (let iy = y0; iy < y1; iy++) for (let ix = x0; ix < x1; ix++) {
    let neg = 0, posC = 0;
    for (let c = 0; c < 8; c++) {
      const jx = ix + CORNER[c][0], jy = iy + CORNER[c][1], jz = iz + CORNER[c][2];
      const v = sdf[(jz * ny + jy) * nx + jx];
      cv[c] = v; if (v < 0) neg++; else posC++;
      cp[c * 3] = origin[0] + jx * spacing; cp[c * 3 + 1] = origin[1] + jy * spacing; cp[c * 3 + 2] = origin[2] + jz * spacing;
    }
    if (neg === 0 || posC === 0) continue;
    for (const tet of TETS) {
      inside.length = 0; outside.length = 0;
      for (let q = 0; q < 4; q++) {
        tv[q] = cv[tet[q]];
        tp[q * 3] = cp[tet[q] * 3]; tp[q * 3 + 1] = cp[tet[q] * 3 + 1]; tp[q * 3 + 2] = cp[tet[q] * 3 + 2];
        (tv[q] < 0 ? inside : outside).push(q);
      }
      if (inside.length === 0 || inside.length === 4) continue;
      if (inside.length === 1 || inside.length === 3) {
        const one = inside.length === 1 ? inside[0] : outside[0];
        const others = inside.length === 1 ? outside : inside;
        edgePoint(one, others[0], cross, 0); edgePoint(one, others[1], cross, 3); edgePoint(one, others[2], cross, 6);
        emitTri(0, 3, 6);
      } else {
        const [a, b] = inside, [c, d] = outside;
        edgePoint(a, c, cross, 0); edgePoint(a, d, cross, 3); edgePoint(b, d, cross, 6); edgePoint(b, c, cross, 9);
        emitTri(0, 3, 6); emitTri(0, 6, 9);
      }
    }
  }

  const vertCount = pos.length / 3;
  const data = new Float32Array(vertCount * VERT_STRIDE);
  const beds = volume.geology ? volume.geology.beds : null;
  const g = [0, 0, 0];
  for (let vtx = 0; vtx < vertCount; vtx++) {
    const px = pos[vtx * 3], py = pos[vtx * 3 + 1], pz = pos[vtx * 3 + 2];
    const o = vtx * VERT_STRIDE;
    data[o] = px; data[o + 1] = py; data[o + 2] = pz;
    volume.gradientAt(px, py, pz, g);
    const gl = Math.hypot(g[0], g[1], g[2]) || 1;
    data[o + 3] = g[0] / gl; data[o + 4] = g[1] / gl; data[o + 5] = g[2] / gl;
    let r = 0.6, gg = 0.58, b = 0.52;
    if (beds) {
      const bed = beds[volume.nearestBed(px, py, pz)] || beds[0];
      if (bed) { r = bed.color[0]; gg = bed.color[1]; b = bed.color[2]; }
    }
    // slight per-vertex tone from hardness heterogeneity — baked geology, not a repeating texture
    const hard = volume.sampleHardness(px, py, pz);
    const tone = 0.94 + 0.10 * hard;
    data[o + 6] = r * tone; data[o + 7] = gg * tone; data[o + 8] = b * tone;
    data[o + 9] = hard;
    data[o + 10] = volume.sampleJoint(px, py, pz);
    data[o + 11] = volume.sampleSediment(px, py, pz);
    // Baked SDF ambient occlusion: probe the signed-distance field in a small
    // hemisphere around the vertex. For an unoccluded plane SDF(offset) is the
    // normal component of the offset; any deficit means rock is closing in
    // (cornice underside, joint slot, cave wall). This is geometric occlusion
    // from the actual volume, not a screen-space or texture effect.
    {
      const nxx = data[o + 3], nyy = data[o + 4], nzz = data[o + 5];
      const hx = Math.abs(nyy) < 0.9 ? 0 : 1, hy = Math.abs(nyy) < 0.9 ? 1 : 0;
      // tangent = helper × normal (helper has z = 0)
      let tx = hy * nzz, ty = -hx * nzz, tz = hx * nyy - hy * nxx;
      const tl = Math.hypot(tx, ty, tz) || 1; tx /= tl; ty /= tl; tz /= tl;
      const bx = nyy * tz - nzz * ty, by = nzz * tx - nxx * tz, bz = nxx * ty - nyy * tx;
      const probes = spacing < 0.6
        ? [[0, 0, 1.25], [0.8, 0, 0.62], [-0.8, 0, 0.62], [0, 0.8, 0.62], [0, -0.8, 0.62]]
        : [[0, 0, 1.3], [0.8, 0, 0.62], [-0.8, 0, 0.62], [0, 0.8, 0.62], [0, -0.8, 0.62],
           [0.58, 0.58, 0.55], [-0.58, 0.58, 0.55], [0.58, -0.58, 0.55], [-0.58, -0.58, 0.55], [0, 0, 1.0]];
      let occ = 0, wsum = 0;
      for (let pi = 0; pi < probes.length; pi++) {
        const [ta, ba, na] = probes[pi];
        const dist = (pi === probes.length - 1 && probes.length > 5 ? 3.1 : 1.65) * spacing;
        const ox = (tx * ta + bx * ba + nxx * na), oy = (ty * ta + by * ba + nyy * na), oz = (tz * ta + bz * ba + nzz * na);
        const ol = Math.hypot(ox, oy, oz) || 1;
        const expected = dist * na / ol;
        const sample = volume.sampleSdf(px + ox / ol * dist, py + oy / ol * dist, pz + oz / ol * dist);
        const weight = pi === 0 ? 1.35 : (Math.abs(ta) > 0 && Math.abs(ba) > 0 ? 0.7 : 1.0);
        occ += weight * Math.max(0, Math.min(1, (expected - sample) / (1.5 * spacing)));
        wsum += weight;
      }
      data[o + 12] = Math.max(0.16, Math.min(1, 1 - 0.84 * (occ / Math.max(1e-6, wsum))));
    }
    const ix = Math.min(volume.nx - 1, Math.max(0, Math.round((px - origin[0]) / spacing)));
    const iz = Math.min(volume.nz - 1, Math.max(0, Math.round((pz - origin[2]) / spacing)));
    data[o + 13] = volume.moisture[iz * volume.nx + ix];
  }
  return { data, vertexCount: vertCount, triangles: vertCount / 3 };
}

export function buildAllChunks(volume) {
  const out = new Map();
  let triangles = 0;
  for (let cz = 0; cz < volume.chunksZ; cz++) for (let cy = 0; cy < volume.chunksY; cy++) for (let cx = 0; cx < volume.chunksX; cx++) {
    const key = volume.chunkKey(cx, cy, cz);
    const mesh = buildChunkMesh(volume, cx, cy, cz);
    out.set(key, mesh);
    triangles += mesh.triangles;
  }
  return { meshes: out, triangles };
}

/** Wavefront OBJ from the CPU chunk meshes (non-indexed soup → sequential faces). */
export function exportOBJ(meshes) {
  const lines = ['# Frontier Environment Editor — SDF terrain export'];
  let base = 1;
  const faces = [];
  for (const mesh of meshes.values()) {
    const d = mesh.data;
    for (let v = 0; v < mesh.vertexCount; v++) {
      const o = v * VERT_STRIDE;
      lines.push(`v ${d[o].toFixed(3)} ${d[o + 1].toFixed(3)} ${d[o + 2].toFixed(3)}`);
    }
    for (let v = 0; v < mesh.vertexCount; v++) {
      const o = v * VERT_STRIDE;
      lines.push(`vn ${d[o + 3].toFixed(4)} ${d[o + 4].toFixed(4)} ${d[o + 5].toFixed(4)}`);
    }
    for (let f = 0; f < mesh.vertexCount; f += 3) {
      faces.push(`f ${base + f}//${base + f} ${base + f + 1}//${base + f + 1} ${base + f + 2}//${base + f + 2}`);
    }
    base += mesh.vertexCount;
  }
  return lines.concat(faces).join('\n');
}
