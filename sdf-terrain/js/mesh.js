// ============================================================================
// Frontier SDF Terrain — Surface-nets meshing + export (DOM-free, Node-safe).
// Extracts a mesh from the SDF for export to DCC/engines. Vertex colors reuse
// the same satellite-shading logic as the viewport (simplified, CPU-side).
// ============================================================================

import { clamp } from './noise.js';

// Naive surface nets over the sign field.
export function surfaceNets(vol, maxVerts = 900000) {
  const N = vol.N, S = vol.sdf;
  const idx = (i, j, k) => (k * N + j) * N + i;
  // cell vertex index grid (only cells with a sign change get a vertex)
  const vertOf = new Int32Array(N * N * N).fill(-1);
  const pos = [];
  const vs = vol.voxel, h = vol.worldSize / 2;
  const P = (i) => ((i + 0.5) * vs - h);

  for (let k = 0; k < N - 1; k++) {
    for (let j = 0; j < N - 1; j++) {
      for (let i = 0; i < N - 1; i++) {
        const c000 = S[idx(i, j, k)], c100 = S[idx(i + 1, j, k)];
        const c010 = S[idx(i, j + 1, k)], c001 = S[idx(i, j, k + 1)];
        const c110 = S[idx(i + 1, j + 1, k)], c101 = S[idx(i + 1, j, k + 1)];
        const c011 = S[idx(i, j + 1, k + 1)], c111 = S[idx(i + 1, j + 1, k + 1)];
        let mask = 0;
        if (c000 < 0) mask |= 1; if (c100 < 0) mask |= 2;
        if (c010 < 0) mask |= 4; if (c110 < 0) mask |= 8;
        if (c001 < 0) mask |= 16; if (c101 < 0) mask |= 32;
        if (c011 < 0) mask |= 64; if (c111 < 0) mask |= 128;
        if (mask === 0 || mask === 255) continue;
        // zero-crossing average along the 12 edges
        let sx = 0, sy = 0, sz = 0, cnt = 0;
        const edge = (ax, ay, az, da, bx, by, bz, db) => {
          if ((da < 0) === (db < 0)) return;
          const t = da / (da - db);
          sx += ax + (bx - ax) * t; sy += ay + (by - ay) * t; sz += az + (bz - az) * t;
          cnt++;
        };
        const x0 = P(i), y0 = P(j), z0 = P(k), x1 = P(i + 1), y1 = P(j + 1), z1 = P(k + 1);
        edge(x0, y0, z0, c000, x1, y0, z0, c100);
        edge(x0, y1, z0, c010, x1, y1, z0, c110);
        edge(x0, y0, z1, c001, x1, y0, z1, c101);
        edge(x0, y1, z1, c011, x1, y1, z1, c111);
        edge(x0, y0, z0, c000, x0, y1, z0, c010);
        edge(x1, y0, z0, c100, x1, y1, z0, c110);
        edge(x0, y0, z1, c001, x0, y1, z1, c011);
        edge(x1, y0, z1, c101, x1, y1, z1, c111);
        edge(x0, y0, z0, c000, x0, y0, z1, c001);
        edge(x1, y0, z0, c100, x1, y0, z1, c101);
        edge(x0, y1, z0, c010, x0, y1, z1, c011);
        edge(x1, y1, z0, c110, x1, y1, z1, c111);
        if (!cnt) continue;
        if (pos.length / 3 >= maxVerts) break;
        vertOf[idx(i, j, k)] = pos.length / 3;
        pos.push(sx / cnt, sy / cnt, sz / cnt);
      }
    }
  }

  // Standard approach: for each interior grid edge with a sign change, join the
  // 4 adjacent cell vertices into a quad.
  const quads = [];
  const S2 = (i, j, k) => S[idx(i, j, k)];
  const V = (i, j, k) => (i < 0 || j < 0 || k < 0 || i >= N - 1 || j >= N - 1 || k >= N - 1) ? -1 : vertOf[idx(i, j, k)];
  for (let k = 1; k < N - 1; k++) {
    for (let j = 1; j < N - 1; j++) {
      for (let i = 1; i < N - 1; i++) {
        // x-edge from (i-1,j,k) to (i,j,k)
        if ((S2(i - 1, j, k) < 0) !== (S2(i, j, k) < 0)) {
          const v0 = V(i - 1, j - 1, k - 1), v1 = V(i - 1, j, k - 1), v2 = V(i - 1, j, k), v3 = V(i - 1, j - 1, k);
          if (v0 >= 0 && v1 >= 0 && v2 >= 0 && v3 >= 0) {
            if (S2(i, j, k) < 0) quads.push(v0, v1, v2, v0, v2, v3);
            else quads.push(v0, v2, v1, v0, v3, v2);
          }
        }
        // y-edge
        if ((S2(i, j - 1, k) < 0) !== (S2(i, j, k) < 0)) {
          const v0 = V(i - 1, j - 1, k - 1), v1 = V(i, j - 1, k - 1), v2 = V(i, j - 1, k), v3 = V(i - 1, j - 1, k);
          if (v0 >= 0 && v1 >= 0 && v2 >= 0 && v3 >= 0) {
            if (S2(i, j, k) < 0) quads.push(v0, v1, v2, v0, v2, v3);
            else quads.push(v0, v2, v1, v0, v3, v2);
          }
        }
        // z-edge
        if ((S2(i, j, k - 1) < 0) !== (S2(i, j, k) < 0)) {
          const v0 = V(i - 1, j - 1, k - 1), v1 = V(i, j - 1, k - 1), v2 = V(i, j, k - 1), v3 = V(i - 1, j, k - 1);
          if (v0 >= 0 && v1 >= 0 && v2 >= 0 && v3 >= 0) {
            if (S2(i, j, k) < 0) quads.push(v0, v1, v2, v0, v2, v3);
            else quads.push(v0, v2, v1, v0, v3, v2);
          }
        }
      }
    }
  }

  const positions = new Float32Array(pos);
  const indices = quads.length > 0 ? new Uint32Array(quads) : new Uint32Array(0);
  // smooth normals from SDF gradient
  const normals = new Float32Array(positions.length);
  const g = { x: 0, y: 0, z: 0 };
  for (let v = 0; v < positions.length / 3; v++) {
    vol.gradient(positions[v * 3], positions[v * 3 + 1], positions[v * 3 + 2], g);
    normals[v * 3] = g.x; normals[v * 3 + 1] = g.y; normals[v * 3 + 2] = g.z;
  }
  return { positions, normals, indices };
}

// Simplified CPU twin of the viewport satellite shader (vertex colors).
export function paintVertices(vol, mesh, matParams) {
  const m = matParams || {};
  const pal = m.palette ?? 0;
  const snow = m.snow ?? 0.34, veg = m.veg ?? 0.8;
  const sea = m.seaLevel ?? -0.28;
  const PALS = [
    { rock: [0.44, 0.42, 0.40], cliff: [0.36, 0.34, 0.32], soil: [0.30, 0.26, 0.18], veg: [0.16, 0.34, 0.10], sand: [0.66, 0.58, 0.44], snow: [0.93, 0.94, 0.97] },
    { rock: [0.66, 0.38, 0.24], cliff: [0.48, 0.26, 0.17], soil: [0.62, 0.38, 0.24], veg: [0.25, 0.32, 0.14], sand: [0.80, 0.60, 0.38], snow: [0.9, 0.9, 0.9] },
    { rock: [0.48, 0.48, 0.43], cliff: [0.42, 0.42, 0.39], soil: [0.30, 0.22, 0.14], veg: [0.10, 0.38, 0.12], sand: [0.62, 0.55, 0.38], snow: [0.9, 0.9, 0.9] },
    { rock: [0.20, 0.16, 0.15], cliff: [0.22, 0.18, 0.17], soil: [0.24, 0.18, 0.15], veg: [0.16, 0.30, 0.10], sand: [0.35, 0.28, 0.24], snow: [0.85, 0.85, 0.85] },
    { rock: [0.50, 0.48, 0.40], cliff: [0.50, 0.46, 0.36], soil: [0.34, 0.30, 0.20], veg: [0.20, 0.36, 0.12], sand: [0.82, 0.74, 0.55], snow: [0.9, 0.9, 0.9] },
  ];
  const P = PALS[pal] || PALS[0];
  const n = mesh.positions.length / 3;
  const colors = new Float32Array(n * 3);
  const mix3 = (a, b, t, o) => { o[0] = a[0] + (b[0] - a[0]) * t; o[1] = a[1] + (b[1] - a[1]) * t; o[2] = a[2] + (b[2] - a[2]) * t; };
  const c = [0, 0, 0];
  for (let v = 0; v < n; v++) {
    const x = mesh.positions[v * 3], y = mesh.positions[v * 3 + 1], z = mesh.positions[v * 3 + 2];
    const ny = mesh.normals[v * 3 + 1];
    const slope = 1 - ny;
    const sed = vol.sampleAttr(vol.attrA, 0, x, y, z);
    const wet = vol.sampleAttr(vol.attrA, 1, x, y, z);
    const tal = vol.sampleAttr(vol.attrA, 2, x, y, z);
    const strata = vol.sampleAttr(vol.attrB, 1, x, y, z);
    const band = 0.5 + 0.5 * Math.sin(strata * 6.2831);
    const rock = [P.rock[0] * (0.85 + band * 0.3), P.rock[1] * (0.85 + band * 0.3), P.rock[2] * (0.85 + band * 0.3)];
    mix3(rock, P.cliff, clamp((slope - 0.25) / 0.37, 0, 1), c);
    const vegM = clamp((0.55 - slope) / 0.35, 0, 1) * clamp(wet * 1.4 + 0.25, 0, 1) * clamp(veg, 0, 1.2);
    mix3(c, P.veg, clamp(vegM, 0, 1) * 0.9, c);
    mix3(c, P.sand, clamp(sed * 1.5, 0, 0.9), c);
    mix3(c, P.sand, clamp(tal * 0.9, 0, 0.7), c);
    if (Math.abs(y - sea - 0.012) < 0.03 && slope < 0.4) mix3(c, P.sand, 0.8, c);
    if (y > snow && slope < 0.5) mix3(c, P.snow, clamp((y - snow) * 8, 0, 1), c);
    const dk = 1 - wet * 0.3;
    colors[v * 3] = clamp(c[0] * dk, 0, 1);
    colors[v * 3 + 1] = clamp(c[1] * dk, 0, 1);
    colors[v * 3 + 2] = clamp(c[2] * dk, 0, 1);
  }
  return colors;
}

export function exportOBJ(mesh, colors) {
  const { positions, normals, indices } = mesh;
  const nv = positions.length / 3;
  const lines = ['# Frontier SDF Terrain export', `# verts ${nv} tris ${indices.length / 3}`];
  for (let v = 0; v < nv; v++) {
    lines.push(`v ${positions[v * 3].toFixed(5)} ${positions[v * 3 + 1].toFixed(5)} ${positions[v * 3 + 2].toFixed(5)}` +
      (colors ? ` ${colors[v * 3].toFixed(4)} ${colors[v * 3 + 1].toFixed(4)} ${colors[v * 3 + 2].toFixed(4)}` : ''));
  }
  for (let v = 0; v < nv; v++) {
    lines.push(`vn ${normals[v * 3].toFixed(5)} ${normals[v * 3 + 1].toFixed(5)} ${normals[v * 3 + 2].toFixed(5)}`);
  }
  for (let t = 0; t < indices.length; t += 3) {
    const a = indices[t] + 1, b = indices[t + 1] + 1, c = indices[t + 2] + 1;
    lines.push(`f ${a}//${a} ${b}//${b} ${c}//${c}`);
  }
  return lines.join('\n');
}

export function exportPLY(mesh, colors) {
  const { positions, normals, indices } = mesh;
  const nv = positions.length / 3, nf = indices.length / 3;
  const head = [
    'ply', 'format ascii 1.0',
    `element vertex ${nv}`,
    'property float x', 'property float y', 'property float z',
    'property float nx', 'property float ny', 'property float nz',
    'property uchar red', 'property uchar green', 'property uchar blue',
    `element face ${nf}`, 'property list uchar int vertex_indices',
    'end_header',
  ];
  const lines = [...head];
  for (let v = 0; v < nv; v++) {
    const r = colors ? Math.round(clamp(colors[v * 3], 0, 1) * 255) : 200;
    const g = colors ? Math.round(clamp(colors[v * 3 + 1], 0, 1) * 255) : 200;
    const b = colors ? Math.round(clamp(colors[v * 3 + 2], 0, 1) * 255) : 200;
    lines.push(`${positions[v * 3].toFixed(5)} ${positions[v * 3 + 1].toFixed(5)} ${positions[v * 3 + 2].toFixed(5)} ` +
      `${normals[v * 3].toFixed(4)} ${normals[v * 3 + 1].toFixed(4)} ${normals[v * 3 + 2].toFixed(4)} ${r} ${g} ${b}`);
  }
  for (let t = 0; t < indices.length; t += 3) {
    lines.push(`3 ${indices[t]} ${indices[t + 1]} ${indices[t + 2]}`);
  }
  return lines.join('\n');
}
