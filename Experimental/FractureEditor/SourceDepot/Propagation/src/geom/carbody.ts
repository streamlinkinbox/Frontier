/**
 * Test vehicle: a "box car" built as ONE welded, closed, quad-gridded shell.
 *
 * Deformation bakes need shared vertices — a triangle soup would tear apart at
 * every seam the moment you dent it — so the body is generated as a subdivided
 * cube whose surface is pushed onto a rounded superquadric and then shaped by a
 * car profile (hood / cabin / boot heights, width taper, tumblehome). Because
 * the whole thing comes from one parametric grid, it is automatically welded:
 * one position per grid corner, no duplicates, no seams.
 *
 * Everything else on the car (lights, glass, trim) is a separate mesh that is
 * bound to the same deformation cage, which is exactly how you would ship this:
 * one bake drives every part attached to the panel.
 */

import { V3, v3, norm, sub, cross, add, mul, clamp } from '../core/math';
import { MeshData } from './convex';

export interface IndexedShell {
  /** unique welded positions, length n*3 */
  pos: Float32Array;
  /** triangle list into `pos` */
  tris: Uint32Array;
  n: number;
  min: V3;
  max: V3;
}

export const CAR = {
  L: 4.15,     // length  (x, +x = front)
  W: 1.82,     // width   (z)
  H: 1.44,     // height  (y)
  /** target quad edge length [m]; dents need ~5 cm to resolve a crease */
  cell: 0.040,
};

/** Roofline as a fraction of full height, front (u=1) to rear (u=-1). */
function roofProfile(u: number): number {
  const smooth = (a: number, b: number, x: number) => {
    const t = clamp((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
  };
  // nose -> hood -> windscreen -> cabin -> backlight -> boot -> tail
  const hood = 0.50 + 0.06 * (1 - smooth(0.45, 1.0, u));
  const cabin = 1.0;
  const boot = 0.60;
  const frontBlend = smooth(0.30, 0.52, u);              // 0 cabin .. 1 hood
  const rearBlend = smooth(-0.50, -0.26, u);             // 0 boot .. 1 cabin
  const h = cabin * rearBlend + boot * (1 - rearBlend);
  return h * (1 - frontBlend) + hood * frontBlend;
}

/**
 * Build the welded body shell on an anisotropic cube grid (so quads stay
 * roughly square in world space along every axis).
 */
export function buildCarShell(cell = CAR.cell): IndexedShell {
  const Nx = Math.max(6, Math.round(CAR.L / cell));
  const Ny = Math.max(6, Math.round(CAR.H / cell));
  const Nz = Math.max(6, Math.round(CAR.W / cell));
  const index = new Map<number, number>();
  const pts: number[] = [];
  const tris: number[] = [];

  const id = (i: number, j: number, k: number): number => {
    const key = (i * (Ny + 1) + j) * (Nz + 1) + k;
    let v = index.get(key);
    if (v === undefined) {
      v = pts.length / 3;
      index.set(key, v);
      const a = (i / Nx) * 2 - 1, b = (j / Ny) * 2 - 1, c = (k / Nz) * 2 - 1;
      // round the cube: project onto a p-norm superquadric (soft edges)
      const p = 7;
      const m = Math.pow(Math.pow(Math.abs(a), p) + Math.pow(Math.abs(b), p) + Math.pow(Math.abs(c), p), 1 / p) || 1;
      let x = a / m, y = b / m, z = c / m;

      // ---- car shaping
      const u = x;
      const yn = y;                                    // -1 floor .. +1 roof
      if (yn > 0) y = yn * roofProfile(u);
      else y = yn * 0.92;
      const taper = 1 - 0.13 * Math.pow(Math.abs(u), 3);
      const tumble = 1 - 0.23 * Math.max(0, yn) * Math.max(0, yn);
      const sill = 1 - 0.06 * Math.max(0, -yn);
      z *= taper * tumble * sill;

      pts.push(x * CAR.L * 0.5, y * CAR.H * 0.5, z * CAR.W * 0.5);
    }
    return v;
  };

  const quad = (a: number, b: number, c: number, d: number) => {
    tris.push(a, b, c, a, c, d);
  };

  for (let j = 0; j < Ny; j++) for (let k = 0; k < Nz; k++) {
    quad(id(Nx, j, k), id(Nx, j + 1, k), id(Nx, j + 1, k + 1), id(Nx, j, k + 1));
    quad(id(0, j, k), id(0, j, k + 1), id(0, j + 1, k + 1), id(0, j + 1, k));
  }
  for (let i = 0; i < Nx; i++) for (let k = 0; k < Nz; k++) {
    quad(id(i, Ny, k), id(i, Ny, k + 1), id(i + 1, Ny, k + 1), id(i + 1, Ny, k));
    quad(id(i, 0, k), id(i + 1, 0, k), id(i + 1, 0, k + 1), id(i, 0, k + 1));
  }
  for (let i = 0; i < Nx; i++) for (let j = 0; j < Ny; j++) {
    quad(id(i, j, Nz), id(i + 1, j, Nz), id(i + 1, j + 1, Nz), id(i, j + 1, Nz));
    quad(id(i, j, 0), id(i, j + 1, 0), id(i + 1, j + 1, 0), id(i + 1, j, 0));
  }

  const pos = new Float32Array(pts);
  const mn = v3(1e9, 1e9, 1e9), mx = v3(-1e9, -1e9, -1e9);
  for (let i = 0; i < pos.length; i += 3) {
    mn.x = Math.min(mn.x, pos[i]); mx.x = Math.max(mx.x, pos[i]);
    mn.y = Math.min(mn.y, pos[i + 1]); mx.y = Math.max(mx.y, pos[i + 1]);
    mn.z = Math.min(mn.z, pos[i + 2]); mx.z = Math.max(mx.z, pos[i + 2]);
  }
  return { pos, tris: new Uint32Array(tris), n: pos.length / 3, min: mn, max: mx };
}

/**
 * Plain welded box grid - the flat steel sheet used to show the portable dent
 * bake on something that is nothing but panel.
 */
export function buildBoxShell(sx: number, sy: number, sz: number, cell: number): IndexedShell {
  const Nx = Math.max(1, Math.round(sx / cell));
  const Ny = Math.max(1, Math.round(sy / cell));
  const Nz = Math.max(1, Math.round(sz / cell));
  const index = new Map<number, number>();
  const pts: number[] = [];
  const tris: number[] = [];
  const id = (i: number, j: number, k: number): number => {
    const key = (i * (Ny + 1) + j) * (Nz + 1) + k;
    let v = index.get(key);
    if (v === undefined) {
      v = pts.length / 3;
      index.set(key, v);
      pts.push((i / Nx - 0.5) * sx, (j / Ny - 0.5) * sy, (k / Nz - 0.5) * sz);
    }
    return v;
  };
  const quad = (a: number, b: number, c: number, d: number) => tris.push(a, b, c, a, c, d);
  for (let j = 0; j < Ny; j++) for (let k = 0; k < Nz; k++) {
    quad(id(Nx, j, k), id(Nx, j + 1, k), id(Nx, j + 1, k + 1), id(Nx, j, k + 1));
    quad(id(0, j, k), id(0, j, k + 1), id(0, j + 1, k + 1), id(0, j + 1, k));
  }
  for (let i = 0; i < Nx; i++) for (let k = 0; k < Nz; k++) {
    quad(id(i, Ny, k), id(i, Ny, k + 1), id(i + 1, Ny, k + 1), id(i + 1, Ny, k));
    quad(id(i, 0, k), id(i + 1, 0, k), id(i + 1, 0, k + 1), id(i, 0, k + 1));
  }
  for (let i = 0; i < Nx; i++) for (let j = 0; j < Ny; j++) {
    quad(id(i, j, Nz), id(i + 1, j, Nz), id(i + 1, j + 1, Nz), id(i, j + 1, Nz));
    quad(id(i, j, 0), id(i, j + 1, 0), id(i + 1, j + 1, 0), id(i + 1, j, 0));
  }
  const pos = new Float32Array(pts);
  const mn = v3(1e9, 1e9, 1e9), mx = v3(-1e9, -1e9, -1e9);
  for (let i = 0; i < pos.length; i += 3) {
    mn.x = Math.min(mn.x, pos[i]); mx.x = Math.max(mx.x, pos[i]);
    mn.y = Math.min(mn.y, pos[i + 1]); mx.y = Math.max(mx.y, pos[i + 1]);
    mn.z = Math.min(mn.z, pos[i + 2]); mx.z = Math.max(mx.z, pos[i + 2]);
  }
  return { pos, tris: new Uint32Array(tris), n: pos.length / 3, min: mn, max: mx };
}

/** Area-weighted smooth normals for an indexed shell. */
export function shellNormals(pos: Float32Array, tris: Uint32Array, out?: Float32Array): Float32Array {
  const nrm = out ?? new Float32Array(pos.length);
  nrm.fill(0);
  for (let t = 0; t < tris.length; t += 3) {
    const a = tris[t] * 3, b = tris[t + 1] * 3, c = tris[t + 2] * 3;
    const ux = pos[b] - pos[a], uy = pos[b + 1] - pos[a + 1], uz = pos[b + 2] - pos[a + 2];
    const vx = pos[c] - pos[a], vy = pos[c + 1] - pos[a + 1], vz = pos[c + 2] - pos[a + 2];
    const nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    for (const o of [a, b, c]) { nrm[o] += nx; nrm[o + 1] += ny; nrm[o + 2] += nz; }
  }
  for (let i = 0; i < nrm.length; i += 3) {
    const l = Math.hypot(nrm[i], nrm[i + 1], nrm[i + 2]) || 1;
    nrm[i] /= l; nrm[i + 1] /= l; nrm[i + 2] /= l;
  }
  return nrm;
}

/**
 * Expand an indexed shell into the renderer's soup layout, carrying the unique
 * vertex id in a 4th stream so the vertex shader can look the vertex up in the
 * vertex-animation texture.
 */
export function shellToMesh(shell: IndexedShell): MeshData & { vid: Float32Array; idx: Uint32Array } {
  const { pos, tris, n } = shell;
  const nrm = shellNormals(pos, tris);
  const attr = new Float32Array(n * 3);
  const vid = new Float32Array(n);
  for (let i = 0; i < n; i++) vid[i] = i;
  return { pos: new Float32Array(pos), nrm, attr, count: tris.length, vid, idx: tris };
}

// --------------------------------------------------------------------- parts

export interface CarPart {
  mesh: MeshData & { vid?: Float32Array };
  kind: 'paint' | 'glass' | 'lamp' | 'rubber' | 'trim';
  /** local centre, used for hit tests and for binding to the cage */
  centre: V3;
  half: V3;
  /** lamps can be shattered */
  broken?: boolean;
}

/** Axis-aligned rounded slab as a soup mesh, optionally cage-bound. */
export function slabMesh(c: V3, h: V3, bevel = 0.012): MeshData & { vid: Float32Array } {
  const pos: number[] = [], nrm: number[] = [], attr: number[] = [];
  const b = bevel;
  const corner = (sx: number, sy: number, sz: number): V3 => v3(
    c.x + sx * (h.x - b) + sx * b * 0.72,
    c.y + sy * (h.y - b) + sy * b * 0.72,
    c.z + sz * (h.z - b) + sz * b * 0.72,
  );
  const faces: [V3, V3, V3, V3, V3][] = [];
  const push = (n: V3, a: V3, bb: V3, cc: V3, d: V3) => faces.push([n, a, bb, cc, d]);
  const P = (sx: number, sy: number, sz: number) => corner(sx, sy, sz);
  push(v3(1, 0, 0), P(1, -1, -1), P(1, -1, 1), P(1, 1, 1), P(1, 1, -1));
  push(v3(-1, 0, 0), P(-1, -1, 1), P(-1, -1, -1), P(-1, 1, -1), P(-1, 1, 1));
  push(v3(0, 1, 0), P(-1, 1, -1), P(1, 1, -1), P(1, 1, 1), P(-1, 1, 1));
  push(v3(0, -1, 0), P(-1, -1, 1), P(1, -1, 1), P(1, -1, -1), P(-1, -1, -1));
  push(v3(0, 0, 1), P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1));
  push(v3(0, 0, -1), P(1, -1, -1), P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1));
  for (const [n, a, bb, cc, d] of faces) {
    for (const q of [a, bb, cc, a, cc, d]) {
      pos.push(q.x, q.y, q.z); nrm.push(n.x, n.y, n.z); attr.push(0, 0, 0);
    }
  }
  const count = pos.length / 3;
  return {
    pos: new Float32Array(pos), nrm: new Float32Array(nrm), attr: new Float32Array(attr),
    count, vid: new Float32Array(count),
  };
}

/** Wheel: a low-poly tyre + rim, not cage-bound (it is not sheet metal). */
export function wheelMesh(r: number, w: number, seg = 18): MeshData {
  const pos: number[] = [], nrm: number[] = [], attr: number[] = [];
  const ring = (a: number, rr: number, z: number): V3 =>
    v3(Math.cos(a) * rr, Math.sin(a) * rr, z);
  for (let i = 0; i < seg; i++) {
    const a0 = (i / seg) * Math.PI * 2, a1 = ((i + 1) / seg) * Math.PI * 2;
    const o0 = ring(a0, r, w / 2), o1 = ring(a1, r, w / 2);
    const i0 = ring(a0, r, -w / 2), i1 = ring(a1, r, -w / 2);
    const n0 = v3(Math.cos(a0), Math.sin(a0), 0), n1 = v3(Math.cos(a1), Math.sin(a1), 0);
    const tri = (p: V3, q: V3, s: V3, np: V3, nq: V3, ns: V3, fresh: number) => {
      for (const [pp, nn] of [[p, np], [q, nq], [s, ns]] as [V3, V3][]) {
        pos.push(pp.x, pp.y, pp.z); nrm.push(nn.x, nn.y, nn.z); attr.push(fresh, 0, 0);
      }
    };
    tri(o0, i0, i1, n0, n0, n1, 0);
    tri(o0, i1, o1, n0, n1, n1, 0);
    // side discs (rim face)
    const cO = v3(0, 0, w / 2), cI = v3(0, 0, -w / 2);
    const rimA = ring(a0, r * 0.62, w / 2), rimB = ring(a1, r * 0.62, w / 2);
    tri(cO, rimA, rimB, v3(0, 0, 1), v3(0, 0, 1), v3(0, 0, 1), 1);
    tri(rimA, o0, o1, v3(0, 0, 1), v3(0, 0, 1), v3(0, 0, 1), 0);
    tri(rimA, o1, rimB, v3(0, 0, 1), v3(0, 0, 1), v3(0, 0, 1), 0);
    tri(cI, ring(a1, r * 0.82, -w / 2), ring(a0, r * 0.82, -w / 2), v3(0, 0, -1), v3(0, 0, -1), v3(0, 0, -1), 0);
    tri(ring(a0, r * 0.82, -w / 2), ring(a1, r * 0.82, -w / 2), i1, v3(0, 0, -1), v3(0, 0, -1), v3(0, 0, -1), 0);
    tri(ring(a0, r * 0.82, -w / 2), i1, i0, v3(0, 0, -1), v3(0, 0, -1), v3(0, 0, -1), 0);
  }
  return {
    pos: new Float32Array(pos), nrm: new Float32Array(nrm), attr: new Float32Array(attr),
    count: pos.length / 3,
  };
}

/**
 * Window glass that hugs the body.
 *
 * A flat quad laid over a curved shell half-sinks and half-floats, so each
 * grid point of the pane is snapped onto the nearest point of the actual body
 * surface and lifted a couple of millimetres along its normal. The pane is
 * cage-bound, so when the pillar behind it dents, the glass goes with it.
 */
export function conformPane(
  shell: IndexedShell, nrm: Float32Array, c: V3, u: V3, v: V3, res = 6, lift = 0.006,
): MeshData & { vid: Float32Array } {
  const gp: V3[] = [], gn: V3[] = [];
  for (let j = 0; j <= res; j++) {
    for (let i = 0; i <= res; i++) {
      const a = (i / res) * 2 - 1, b = (j / res) * 2 - 1;
      const p = add(c, add(mul(u, a), mul(v, b)));
      let best = 0, bd = 1e9;
      for (let k = 0; k < shell.n; k++) {
        const d = (shell.pos[k * 3] - p.x) ** 2 + (shell.pos[k * 3 + 1] - p.y) ** 2
          + (shell.pos[k * 3 + 2] - p.z) ** 2;
        if (d < bd) { bd = d; best = k; }
      }
      const n = v3(nrm[best * 3], nrm[best * 3 + 1], nrm[best * 3 + 2]);
      gp.push(add(v3(shell.pos[best * 3], shell.pos[best * 3 + 1], shell.pos[best * 3 + 2]), mul(n, lift)));
      gn.push(n);
    }
  }
  const pos: number[] = [], nn: number[] = [], attr: number[] = [];
  const at = (i: number, j: number) => j * (res + 1) + i;
  for (let j = 0; j < res; j++) {
    for (let i = 0; i < res; i++) {
      const q = [at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)];
      for (const k of [q[0], q[1], q[2], q[0], q[2], q[3]]) {
        pos.push(gp[k].x, gp[k].y, gp[k].z);
        nn.push(gn[k].x, gn[k].y, gn[k].z);
        attr.push(0, 0, 0);
      }
    }
  }
  const count = pos.length / 3;
  return {
    pos: new Float32Array(pos), nrm: new Float32Array(nn), attr: new Float32Array(attr),
    count, vid: new Float32Array(count),
  };
}

/** Flat inset window pane (cage-bound, so it dents with the pillar it sits in). */
export function paneMesh(c: V3, u: V3, v: V3, n: V3): MeshData & { vid: Float32Array } {
  const pos: number[] = [], nrm: number[] = [], attr: number[] = [];
  const P = [sub(sub(c, u), v), add(sub(c, v), u), add(add(c, u), v), sub(add(c, v), u)];
  for (const q of [P[0], P[1], P[2], P[0], P[2], P[3]]) {
    pos.push(q.x, q.y, q.z); nrm.push(n.x, n.y, n.z); attr.push(0, 0, 0);
  }
  const count = pos.length / 3;
  return {
    pos: new Float32Array(pos), nrm: new Float32Array(nrm), attr: new Float32Array(attr),
    count, vid: new Float32Array(count),
  };
}

export { norm, cross, mul };
