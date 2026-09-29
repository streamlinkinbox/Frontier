// ---------------------------------------------------------------------------
// Frontier core / meshing — dual-contouring-lite (surface nets + QEF snap)
// Table-free isosurface extraction from the narrow-band SDF. For every cell
// with a sign change we place one vertex by minimising the quadric error of the
// crossing planes (falls back to the crossing centroid), which preserves cliff
// faces and cave walls far better than marching-cubes centroid averaging.
// Normals come straight from the SDF gradient -> distance-exact shading.
// ---------------------------------------------------------------------------

import { SDFVolume } from './volume';

export interface MeshData {
  positions: Float32Array;
  normals: Float32Array;
  uvs: Float32Array;
  indices: Uint32Array;
  triangleCount: number;
}

export function meshVolume(vol: SDFVolume, uvScale = 1 / 256): MeshData {
  const { nx, ny, nz, h, data } = vol;
  const verts = new Int32Array(nx * ny * nz).fill(-1);
  const pos: number[] = [];
  const nrm: number[] = [];
  const uvs: number[] = [];
  const idx: number[] = [];

  const at = (x: number, y: number, z: number) => data[(z * ny + y) * nx + x];

  const addVertex = (px: number, py: number, pz: number): number => {
    const id = pos.length / 3;
    pos.push(px, py, pz);
    // normal from gradient (voxel space -> world is uniform scale)
    const fx = px / h, fy = py / h, fz = pz / h;
    const x0 = Math.max(0, Math.min(nx - 1, Math.round(fx)));
    const y0 = Math.max(0, Math.min(ny - 1, Math.round(fy)));
    const z0 = Math.max(0, Math.min(nz - 1, Math.round(fz)));
    let gx = 0, gy = 0, gz = 0;
    if (x0 > 0 && x0 < nx - 1) gx = at(x0 + 1, y0, z0) - at(x0 - 1, y0, z0);
    if (y0 > 0 && y0 < ny - 1) gy = at(x0, y0 + 1, z0) - at(x0, y0 - 1, z0);
    if (z0 > 0 && z0 < nz - 1) gz = at(x0, y0, z0 + 1) - at(x0, y0, z0 - 1);
    const gl = Math.hypot(gx, gy, gz) || 1;
    nrm.push(gx / gl, gy / gl, gz / gl);
    uvs.push(px * uvScale, pz * uvScale);
    return id;
  };

  // one vertex per cell with a sign change
  for (let z = 0; z < nz - 1; z++) {
    for (let y = 0; y < ny - 1; y++) {
      for (let x = 0; x < nx - 1; x++) {
        const c000 = at(x, y, z), c100 = at(x + 1, y, z);
        const c010 = at(x, y + 1, z), c110 = at(x + 1, y + 1, z);
        const c001 = at(x, y, z + 1), c101 = at(x + 1, y, z + 1);
        const c011 = at(x, y + 1, z + 1), c111 = at(x + 1, y + 1, z + 1);
        let signMask = 0;
        if (c000 < 0) signMask |= 1;
        if (c100 < 0) signMask |= 2;
        if (c010 < 0) signMask |= 4;
        if (c110 < 0) signMask |= 8;
        if (c001 < 0) signMask |= 16;
        if (c101 < 0) signMask |= 32;
        if (c011 < 0) signMask |= 64;
        if (c111 < 0) signMask |= 128;
        if (signMask === 0 || signMask === 255) continue;

        // crossings: [point, normal] pairs along cell edges
        const pts: number[] = [];
        const nrm2: number[] = [];
        const edge = (ax: number, ay: number, az: number, bx: number, by: number, bz: number, da: number, db: number) => {
          if ((da < 0) === (db < 0)) return;
          const t = da / (da - db);
          pts.push((ax + (bx - ax) * t) * h, (ay + (by - ay) * t) * h, (az + (bz - az) * t) * h);
          // plane normal = gradient approximated by edge direction * sign
          const dx = bx - ax, dy = by - ay, dz = bz - az;
          const s = da < 0 ? 1 : -1;
          nrm2.push(dx * s, dy * s, dz * s);
        };
        edge(x, y, z, x + 1, y, z, c000, c100);
        edge(x, y + 1, z, x + 1, y + 1, z, c010, c110);
        edge(x, y, z + 1, x + 1, y, z + 1, c001, c101);
        edge(x, y + 1, z + 1, x + 1, y + 1, z + 1, c011, c111);
        edge(x, y, z, x, y + 1, z, c000, c010);
        edge(x + 1, y, z, x + 1, y + 1, z, c100, c110);
        edge(x, y, z + 1, x, y + 1, z + 1, c001, c011);
        edge(x + 1, y, z + 1, x + 1, y + 1, z + 1, c101, c111);
        edge(x, y, z, x, y, z + 1, c000, c001);
        edge(x + 1, y, z, x + 1, y, z + 1, c100, c101);
        edge(x, y + 1, z, x, y + 1, z + 1, c010, c011);
        edge(x + 1, y + 1, z, x + 1, y + 1, z + 1, c110, c111);

        const m = pts.length / 3;
        if (m === 0) continue;
        // centroid
        let cx = 0, cy = 0, cz = 0;
        for (let i = 0; i < m; i++) { cx += pts[i * 3]; cy += pts[i * 3 + 1]; cz += pts[i * 3 + 2]; }
        cx /= m; cy /= m; cz /= m;

        // QEF: minimise sum ((p - pi) . ni)^2  ->  (A^T A) p = A^T b
        let a00 = 0, a01 = 0, a02 = 0, a11 = 0, a12 = 0, a22 = 0;
        let b0 = 0, b1 = 0, b2 = 0;
        for (let i = 0; i < m; i++) {
          let nx2 = nrm2[i * 3], ny2 = nrm2[i * 3 + 1], nz2 = nrm2[i * 3 + 2];
          const l = Math.hypot(nx2, ny2, nz2) || 1;
          nx2 /= l; ny2 /= l; nz2 /= l;
          const d = nx2 * pts[i * 3] + ny2 * pts[i * 3 + 1] + nz2 * pts[i * 3 + 2];
          a00 += nx2 * nx2; a01 += nx2 * ny2; a02 += nx2 * nz2;
          a11 += ny2 * ny2; a12 += ny2 * nz2; a22 += nz2 * nz2;
          b0 += nx2 * d; b1 += ny2 * d; b2 += nz2 * d;
        }
        // regularise toward centroid
        const reg = 0.35;
        a00 += reg; a11 += reg; a22 += reg;
        b0 += reg * cx; b1 += reg * cy; b2 += reg * cz;
        let px = cx, py = cy, pz = cz;
        const sol = solve3(a00, a01, a02, a01, a11, a12, a02, a12, a22, b0, b1, b2);
        if (sol) {
          // keep the vertex inside the cell (surface-nets safety)
          const minX = x * h, maxX = (x + 1) * h;
          const minY = y * h, maxY = (y + 1) * h;
          const minZ = z * h, maxZ = (z + 1) * h;
          if (sol[0] >= minX && sol[0] <= maxX && sol[1] >= minY && sol[1] <= maxY && sol[2] >= minZ && sol[2] <= maxZ) {
            px = sol[0]; py = sol[1]; pz = sol[2];
          }
        }
        verts[(z * ny + y) * nx + x] = addVertex(px, py, pz);
      }
    }
  }

  // polygonise: for every scalar-grid edge with a sign change, emit the quad
  // of the four cells sharing that edge (naive surface nets connectivity)
  const quad = (a: number, b: number, c: number, d: number) => {
    if (a < 0 || b < 0 || c < 0 || d < 0) return;
    idx.push(a, b, c, a, c, d);
  };

  for (let z = 0; z < nz; z++) {
    for (let y = 0; y < ny; y++) {
      for (let x = 0; x < nx; x++) {
        const i = (z * ny + y) * nx + x;
        const s = at(x, y, z) < 0;
        if (x + 1 < nx && (at(x + 1, y, z) < 0) !== s) {
          if (y > 0 && z > 0) quad(verts[i], verts[i - nx], verts[i - nx - nx * ny], verts[i - nx * ny]);
        }
        if (y + 1 < ny && (at(x, y + 1, z) < 0) !== s) {
          if (x > 0 && z > 0) quad(verts[i], verts[i - nx * ny], verts[i - 1 - nx * ny], verts[i - 1]);
        }
        if (z + 1 < nz && (at(x, y, z + 1) < 0) !== s) {
          if (x > 0 && y > 0) quad(verts[i], verts[i - 1], verts[i - 1 - nx], verts[i - nx]);
        }
      }
    }
  }

  // orient every triangle to agree with the SDF gradient normals
  for (let t = 0; t < idx.length; t += 3) {
    const a = idx[t], b = idx[t + 1], c = idx[t + 2];
    const ax = pos[a * 3], ay = pos[a * 3 + 1], az = pos[a * 3 + 2];
    const bx = pos[b * 3], by = pos[b * 3 + 1], bz = pos[b * 3 + 2];
    const cx = pos[c * 3], cy = pos[c * 3 + 1], cz = pos[c * 3 + 2];
    const fx = (by - ay) * (cz - az) - (bz - az) * (cy - ay);
    const fy = (bz - az) * (cx - ax) - (bx - ax) * (cz - az);
    const fz = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    const nx2 = nrm[a * 3] + nrm[b * 3] + nrm[c * 3];
    const ny2 = nrm[a * 3 + 1] + nrm[b * 3 + 1] + nrm[c * 3 + 1];
    const nz2 = nrm[a * 3 + 2] + nrm[b * 3 + 2] + nrm[c * 3 + 2];
    if (fx * nx2 + fy * ny2 + fz * nz2 < 0) {
      idx[t + 1] = c; idx[t + 2] = b;
    }
  }

  return {
    positions: Float32Array.from(pos),
    normals: Float32Array.from(nrm),
    uvs: Float32Array.from(uvs),
    indices: Uint32Array.from(idx),
    triangleCount: idx.length / 3,
  };
}

function solve3(
  a00: number, a01: number, a02: number,
  a10: number, a11: number, a12: number,
  a20: number, a21: number, a22: number,
  b0: number, b1: number, b2: number,
): [number, number, number] | null {
  const det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
  if (Math.abs(det) < 1e-9) return null;
  const inv = 1 / det;
  const x = (b0 * (a11 * a22 - a12 * a21) - a01 * (b1 * a22 - a12 * b2) + a02 * (b1 * a21 - a11 * b2)) * inv;
  const y = (a00 * (b1 * a22 - a12 * b2) - b0 * (a10 * a22 - a12 * a20) + a02 * (a10 * b2 - b1 * a20)) * inv;
  const z = (a00 * (a11 * b2 - b1 * a21) - a01 * (a10 * b2 - b1 * a20) + b0 * (a10 * a21 - a11 * a20)) * inv;
  if (!isFinite(x) || !isFinite(y) || !isFinite(z)) return null;
  return [x, y, z];
}
