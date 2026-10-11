/**
 * Mesh building blocks.
 *
 * Everything paved in the scene ends up in a single surface mesh (roads,
 * junctions, kerb runs, footways, paving, structures, ground) distinguished by a
 * per-vertex material id, so the whole world renders in one draw and the
 * procedural material shader can composite per-material layer stacks.
 *
 * Quads are the primary primitive: the builder keeps the quad list (for picking,
 * export and topology inspection) and derives triangles for the GPU.
 */

import { type V3 } from './math';
import type { SurfaceKind } from './model';

export type MatKind = SurfaceKind;

export const MAT_ORDER: SurfaceKind[] = [
  'asphalt',
  'asphalt-worn',
  'asphalt-fresh',
  'concrete',
  'concrete-panel',
  'pavers',
  'pavers-mixed',
  'slabs',
  'cobble',
  'tactile',
  'gravel',
  'soil',
  'grass',
  'ballast',
  'steel',
  'paint',
  'wood',
  'rubber',
  'water',
  'kerb',
  'road-plate',
];

export const MAT_INDEX: Record<string, number> = Object.fromEntries(MAT_ORDER.map((m, i) => [m, i]));

export interface MeshVertex {
  p: V3;
  n: V3;
  /** material-space UV in metres (station, lateral offset) */
  uv: [number, number];
  /** world-space UV in metres (x, z) for large-scale detail */
  uv2: [number, number];
  mat: number;
  /** rgba 0..1 multiplier */
  tint: [number, number, number, number];
}

function v2area(a: number[], b: number[], c: number[]): number {
  return Math.abs((b[0] - a[0]) * (c[2] - a[2]) - (c[0] - a[0]) * (b[2] - a[2]));
}

export class MeshBuilder {
  verts: MeshVertex[] = [];
  /** quad list, 4 indices per quad */
  quads: number[] = [];
  /** triangle list derived from quads (plus any explicit triangles) */
  tris: number[] = [];
  /** per-quad material id (for picking / stats) */
  quadMat: number[] = [];

  get vertexCount(): number {
    return this.verts.length;
  }
  get quadCount(): number {
    return this.quads.length / 4;
  }
  get triCount(): number {
    return this.tris.length / 3;
  }

  addVertex(v: MeshVertex): number {
    this.verts.push(v);
    return this.verts.length - 1;
  }

  /**
   * Add a quad. Vertices must be supplied in surface order (a→b→c→d); the
   * winding is corrected automatically against the supplied normals so the
   * resulting triangles always wind counter-clockwise as seen from the normal.
   */
  addQuad(a: number, b: number, c: number, d: number): void {
    // a quad that reuses a vertex has no area and would show up as a topology
    // error downstream
    if (a === b || b === c || c === d || d === a || a === c || b === d) return;
    const va = this.verts[a];
    const vb = this.verts[b];
    const vc = this.verts[c];
    // geometric normal of the first triangle
    const ux = vb.p[0] - va.p[0];
    const uy = vb.p[1] - va.p[1];
    const uz = vb.p[2] - va.p[2];
    const vx = vc.p[0] - va.p[0];
    const vy = vc.p[1] - va.p[1];
    const vz = vc.p[2] - va.p[2];
    const nx = uy * vz - uz * vy;
    const ny = uz * vx - ux * vz;
    const nz = ux * vy - uy * vx;
    const refN = va.n;
    const dot = nx * refN[0] + ny * refN[1] + nz * refN[2];
    if (Math.hypot(nx, ny, nz) < 1e-7 || v2area(va.p, vb.p, vc.p) < 1e-9) return;
    const flip = dot < 0;
    if (flip) {
      this.quads.push(a, d, c, b);
      this.tris.push(a, d, c, a, c, b);
    } else {
      this.quads.push(a, b, c, d);
      this.tris.push(a, b, c, a, c, d);
    }
    this.quadMat.push(va.mat);
  }

  /** Triangle with explicit winding control. */
  addTriangle(a: number, b: number, c: number): void {
    this.tris.push(a, b, c);
  }

  /** Grid of quads: rows×cols vertex indices supplied as a function. */
  static index(row: number, col: number, cols: number): number {
    return row * cols + col;
  }

  /** Concatenate another builder (keeps indices valid). */
  append(other: MeshBuilder): void {
    const base = this.verts.length;
    this.verts.push(...other.verts);
    for (const q of other.quads) this.quads.push(q + base);
    for (const t of other.tris) this.tris.push(t + base);
    this.quadMat.push(...other.quadMat);
  }

  /** Pack into GPU-ready typed arrays. */
  pack(): {
    positions: Float32Array;
    normals: Float32Array;
    uvs: Float32Array;
    uv2s: Float32Array;
    mats: Uint32Array;
    tints: Uint8Array;
    indices: Uint32Array;
  } {
    const n = this.verts.length;
    const positions = new Float32Array(n * 3);
    const normals = new Float32Array(n * 3);
    const uvs = new Float32Array(n * 2);
    const uv2s = new Float32Array(n * 2);
    const mats = new Uint32Array(n);
    const tints = new Uint8Array(n * 4);
    for (let i = 0; i < n; i++) {
      const v = this.verts[i];
      positions[i * 3] = v.p[0];
      positions[i * 3 + 1] = v.p[1];
      positions[i * 3 + 2] = v.p[2];
      normals[i * 3] = v.n[0];
      normals[i * 3 + 1] = v.n[1];
      normals[i * 3 + 2] = v.n[2];
      uvs[i * 2] = v.uv[0];
      uvs[i * 2 + 1] = v.uv[1];
      uv2s[i * 2] = v.uv2[0];
      uv2s[i * 2 + 1] = v.uv2[1];
      mats[i] = v.mat;
      tints[i * 4] = Math.round(Math.min(1, Math.max(0, v.tint[0])) * 255);
      tints[i * 4 + 1] = Math.round(Math.min(1, Math.max(0, v.tint[1])) * 255);
      tints[i * 4 + 2] = Math.round(Math.min(1, Math.max(0, v.tint[2])) * 255);
      tints[i * 4 + 3] = Math.round(Math.min(1, Math.max(0, v.tint[3])) * 255);
    }
    return { positions, normals, uvs, uv2s, mats, tints, indices: new Uint32Array(this.tris) };
  }
}

/** Normals for a transverse profile strip: cross(du, ds), oriented up. */
export function stripNormals(
  positions: Array<[number, number, number]>,
  cols: number,
  rows: number,
  ups: Float32Array,
): void {
  // forward differences inside the grid, one-sided at the borders
  for (let r = 0; r < rows; r++) {
    for (let c = 0; c < cols; c++) {
      const i = r * cols + c;
      const cNext = Math.min(cols - 1, c + 1);
      const cPrev = Math.max(0, c - 1);
      const rNext = Math.min(rows - 1, r + 1);
      const rPrev = Math.max(0, r - 1);
      const p = positions[i];
      const pU = positions[r * cols + cNext];
      const pU2 = positions[r * cols + cPrev];
      const pS = positions[rNext * cols + c];
      const pS2 = positions[rPrev * cols + c];
      const du: V3 = [pU[0] - pU2[0], pU[1] - pU2[1], pU[2] - pU2[2]];
      const ds: V3 = [pS[0] - pS2[0], pS[1] - pS2[1], pS[2] - pS2[2]];
      // n = normalize(cross(ds, du)) → +Y for a road laid out in XZ
      let nx = ds[1] * du[2] - ds[2] * du[1];
      let ny = ds[2] * du[0] - ds[0] * du[2];
      let nz = ds[0] * du[1] - ds[1] * du[0];
      const len = Math.hypot(nx, ny, nz) || 1;
      nx /= len;
      ny /= len;
      nz /= len;
      if (ny < 0) {
        nx = -nx;
        ny = -ny;
        nz = -nz;
      }
      ups[i * 3] = nx;
      ups[i * 3 + 1] = ny;
      ups[i * 3 + 2] = nz;
    }
  }
}

/** A single instanced prop mesh (position/normal/uv/mat + optional animation parts). */
export interface PropVertex {
  p: V3;
  n: V3;
  uv: [number, number];
  mat: number;
  color: [number, number, number, number];
  /** part flags: x = wheel index (-1 none), y = axle x offset, z = axle y, w = radius */
  part: [number, number, number, number];
}

export class PropBuilder {
  verts: PropVertex[] = [];
  tris: number[] = [];

  add(v: PropVertex): number {
    this.verts.push(v);
    return this.verts.length - 1;
  }

  quad(a: number, b: number, c: number, d: number): void {
    this.tris.push(a, b, c, a, c, d);
  }

  /** Box helper used by most furniture: centre, size, rotation, colour, material. */
  box(centre: V3, size: V3, rotY: number, mat: number, color: [number, number, number, number], part: [number, number, number, number] = [-1, 0, 0, 0]): void {
    const [cx, cy, cz] = centre;
    const [sx, sy, sz] = [size[0] / 2, size[1] / 2, size[2] / 2];
    const c = Math.cos(rotY);
    const s = Math.sin(rotY);
    const rot = (x: number, z: number): [number, number] => [cx + x * c - z * s, cz + x * s + z * c];
    const corners: Array<[number, number]> = [
      [-sx, -sz],
      [sx, -sz],
      [sx, sz],
      [-sx, sz],
    ];
    const ids: number[] = [];
    for (const [x, z] of corners) {
      const [px, pz] = rot(x, z);
      ids.push(this.add({ p: [px, cy - sy, pz], n: [0, -1, 0], uv: [x, z], mat, color, part }));
    }
    for (const [x, z] of corners) {
      const [px, pz] = rot(x, z);
      ids.push(this.add({ p: [px, cy + sy, pz], n: [0, 1, 0], uv: [x, z], mat, color, part }));
    }
    // bottom / top
    this.quad(ids[0], ids[3], ids[2], ids[1]);
    this.quad(ids[4], ids[5], ids[6], ids[7]);
    // sides
    for (let i = 0; i < 4; i++) {
      const j = (i + 1) % 4;
      this.quad(ids[i], ids[j], ids[4 + j], ids[4 + i]);
    }
  }

  /** Cylinder / tube helper (used by poles, bollards, ducts, tree trunks). */
  cylinder(base: V3, height: number, radius: number, segments: number, mat: number, color: [number, number, number, number], radiusTop = radius, capTop = true, capBottom = false): void {
    const ring: number[][] = [];
    for (let i = 0; i < segments; i++) {
      const a = (i / segments) * Math.PI * 2;
      const x = base[0] + Math.cos(a) * radius;
      const z = base[2] + Math.sin(a) * radius;
      const xt = base[0] + Math.cos(a) * radiusTop;
      const zt = base[2] + Math.sin(a) * radiusTop;
      const nx = Math.cos(a);
      const nz = Math.sin(a);
      const bi = this.add({ p: [x, base[1], z], n: [nx, 0, nz], uv: [a * radius, 0], mat, color, part: [-1, 0, 0, 0] });
      const ti = this.add({ p: [xt, base[1] + height, zt], n: [nx, 0, nz], uv: [a * radius, height], mat, color, part: [-1, 0, 0, 0] });
      ring.push([bi, ti]);
    }
    for (let i = 0; i < segments; i++) {
      const j = (i + 1) % segments;
      this.quad(ring[i][0], ring[j][0], ring[j][1], ring[i][1]);
    }
    if (capTop) {
      const centre = this.add({ p: [base[0], base[1] + height, base[2]], n: [0, 1, 0], uv: [0, 0], mat, color, part: [-1, 0, 0, 0] });
      for (let i = 0; i < segments; i++) {
        const j = (i + 1) % segments;
        this.tris.push(centre, ring[i][1], ring[j][1]);
      }
    }
    if (capBottom) {
      const centre = this.add({ p: [base[0], base[1], base[2]], n: [0, -1, 0], uv: [0, 0], mat, color, part: [-1, 0, 0, 0] });
      for (let i = 0; i < segments; i++) {
        const j = (i + 1) % segments;
        this.tris.push(centre, ring[j][0], ring[i][0]);
      }
    }
  }

  /** Extruded profile along a straight run (guardrails, kerb profiles, rails). */
  extrude(p0: V3, p1: V3, profile: Array<[number, number]>, mat: number, color: [number, number, number, number], closed = false, uScale = 1): void {
    const dx = p1[0] - p0[0];
    const dy = p1[1] - p0[1];
    const dz = p1[2] - p0[2];
    const len = Math.hypot(dx, dy, dz) || 1;
    const fx = dx / len;
    const fy = dy / len;
    const fz = dz / len;
    // lateral axis = up × forward
    let rx = 0 * fz - 1 * fy;
    let ry = 1 * fx - 0 * fz;
    let rz = 0 * fy - 0 * fx;
    const rl = Math.hypot(rx, ry, rz) || 1;
    rx /= rl;
    ry /= rl;
    rz /= rl;
    const idxs: number[] = [];
    const n = profile.length;
    for (let i = 0; i < n; i++) {
      const [lat, up] = profile[i];
      const ax = p0[0] + rx * lat;
      const ay = p0[1] + up;
      const az = p0[2] + rz * lat;
      const bx = p1[0] + rx * lat;
      const by = p1[1] + up;
      const bz = p1[2] + rz * lat;
      // simple normal: outward from the profile centroid
      const cx = profile.reduce((a, p) => a + p[0], 0) / n;
      const cu = profile.reduce((a, p) => a + p[1], 0) / n;
      let nx0 = lat - cx;
      let nu0 = up - cu;
      const nl = Math.hypot(nx0, nu0) || 1;
      nx0 /= nl;
      nu0 /= nl;
      const nwx = rx * nx0;
      const nwy = nu0;
      const nwz = rz * nx0;
      idxs.push(this.add({ p: [ax, ay, az], n: [nwx, nwy, nwz], uv: [0, lat * uScale], mat, color, part: [-1, 0, 0, 0] }));
      idxs.push(this.add({ p: [bx, by, bz], n: [nwx, nwy, nwz], uv: [len, lat * uScale], mat, color, part: [-1, 0, 0, 0] }));
    }
    const segs = closed ? n : n - 1;
    for (let i = 0; i < segs; i++) {
      const a = i * 2;
      const b = ((i + 1) % n) * 2;
      this.quad(idxs[a], idxs[a + 1], idxs[b + 1], idxs[b]);
    }
  }

  pack(): {
    positions: Float32Array;
    normals: Float32Array;
    uvs: Float32Array;
    mats: Uint32Array;
    colors: Uint8Array;
    parts: Float32Array;
    indices: Uint32Array;
  } {
    const n = this.verts.length;
    const positions = new Float32Array(n * 3);
    const normals = new Float32Array(n * 3);
    const uvs = new Float32Array(n * 2);
    const mats = new Uint32Array(n);
    const colors = new Uint8Array(n * 4);
    const parts = new Float32Array(n * 4);
    for (let i = 0; i < n; i++) {
      const v = this.verts[i];
      positions[i * 3] = v.p[0];
      positions[i * 3 + 1] = v.p[1];
      positions[i * 3 + 2] = v.p[2];
      normals[i * 3] = v.n[0];
      normals[i * 3 + 1] = v.n[1];
      normals[i * 3 + 2] = v.n[2];
      uvs[i * 2] = v.uv[0];
      uvs[i * 2 + 1] = v.uv[1];
      mats[i] = v.mat;
      colors[i * 4] = Math.round(Math.min(1, Math.max(0, v.color[0])) * 255);
      colors[i * 4 + 1] = Math.round(Math.min(1, Math.max(0, v.color[1])) * 255);
      colors[i * 4 + 2] = Math.round(Math.min(1, Math.max(0, v.color[2])) * 255);
      colors[i * 4 + 3] = Math.round(Math.min(1, Math.max(0, v.color[3])) * 255);
      parts[i * 4] = v.part[0];
      parts[i * 4 + 1] = v.part[1];
      parts[i * 4 + 2] = v.part[2];
      parts[i * 4 + 3] = v.part[3];
    }
    return { positions, normals, uvs, mats, colors, parts, indices: new Uint32Array(this.tris) };
  }
}
