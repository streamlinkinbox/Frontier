/**
 * Convex polyhedron kernel.
 *
 * Fragments produced by the solid fracture solver are convex cells carved out
 * of the body by successive crack surfaces. Each cell knows which of its faces
 * are *original* surface and which are *fresh fracture surface* — that
 * distinction drives both the shading (fresh rock is bright and rough, fresh
 * glass is a mirror/mist/hackle band, fresh wood is fibrous) and the
 * displacement applied at mesh build time.
 */

import {
  V3, add, cross, dot, len, mul, norm, sub, v3, fbm3, clone,
} from '../core/math';

export interface Plane { n: V3; d: number; } // { x : dot(n,x) = d }

export interface Face {
  v: number[];      // indices into Convex.verts, CCW seen from outside
  fresh: number;    // 0 = original surface, 1 = fracture surface
  gen: number;      // which crack generation created it (for shading variety)
}

export interface Convex {
  verts: V3[];
  faces: Face[];
}

export const planeFrom = (n: V3, p: V3): Plane => {
  const nn = norm(n);
  return { n: nn, d: dot(nn, p) };
};

export function boxConvex(hx: number, hy: number, hz: number): Convex {
  const verts: V3[] = [
    v3(-hx, -hy, -hz), v3(hx, -hy, -hz), v3(hx, hy, -hz), v3(-hx, hy, -hz),
    v3(-hx, -hy, hz), v3(hx, -hy, hz), v3(hx, hy, hz), v3(-hx, hy, hz),
  ];
  const faces: Face[] = [
    { v: [4, 5, 6, 7], fresh: 0, gen: 0 }, // +z
    { v: [1, 0, 3, 2], fresh: 0, gen: 0 }, // -z
    { v: [5, 1, 2, 6], fresh: 0, gen: 0 }, // +x
    { v: [0, 4, 7, 3], fresh: 0, gen: 0 }, // -x
    { v: [3, 7, 6, 2], fresh: 0, gen: 0 }, // +y
    { v: [0, 1, 5, 4], fresh: 0, gen: 0 }, // -y
  ];
  return { verts, faces };
}

/** Rough sphere-ish convex body (icosphere-like hull), used for boulders. */
export function blobConvex(radius: number, rand: () => number, lumpiness = 0.25): Convex {
  // Start from a box and shave it with random planes -> irregular convex hull.
  let c = boxConvex(radius, radius, radius);
  const cuts = 22;
  for (let i = 0; i < cuts; i++) {
    const z = rand() * 2 - 1;
    const t = rand() * Math.PI * 2;
    const r = Math.sqrt(Math.max(0, 1 - z * z));
    const n = v3(r * Math.cos(t), z, r * Math.sin(t));
    const d = radius * (0.72 + lumpiness * rand());
    const res = clipConvex(c, { n, d }, 0);
    if (res) c = res;
  }
  for (const f of c.faces) f.fresh = 0;
  return c;
}

const EPS = 1e-9;

/**
 * Clip a convex polyhedron by a plane, keeping the half-space dot(n,x) <= d.
 * Returns null if nothing survives. New faces are tagged fresh=1.
 */
export function clipConvex(poly: Convex, plane: Plane, gen: number): Convex | null {
  const n = poly.verts.length;
  const s = new Float64Array(n);
  let minS = Infinity, maxS = -Infinity;
  for (let i = 0; i < n; i++) {
    s[i] = dot(plane.n, poly.verts[i]) - plane.d;
    if (s[i] < minS) minS = s[i];
    if (s[i] > maxS) maxS = s[i];
  }
  const scale = Math.max(1, Math.abs(plane.d), maxS - minS);
  const eps = EPS * scale * 1e3;
  if (maxS <= eps) return { verts: poly.verts.map(clone), faces: poly.faces.map((f) => ({ ...f, v: f.v.slice() })) };
  if (minS >= -eps) return null;

  const verts: V3[] = [];
  const remap = new Int32Array(n).fill(-1);
  const edgeCache = new Map<number, number>();

  const keepVert = (i: number) => {
    if (remap[i] < 0) { remap[i] = verts.length; verts.push(clone(poly.verts[i])); }
    return remap[i];
  };
  const cutVert = (i: number, j: number) => {
    const key = i < j ? i * n + j : j * n + i;
    const hit = edgeCache.get(key);
    if (hit !== undefined) return hit;
    const t = s[i] / (s[i] - s[j]);
    const p = add(poly.verts[i], mul(sub(poly.verts[j], poly.verts[i]), t));
    const idx = verts.length;
    verts.push(p);
    edgeCache.set(key, idx);
    return idx;
  };

  const faces: Face[] = [];
  const capEdges: [number, number][] = [];

  for (const f of poly.faces) {
    const out: number[] = [];
    const onPlane: number[] = [];
    const m = f.v.length;
    for (let k = 0; k < m; k++) {
      const i = f.v[k], j = f.v[(k + 1) % m];
      const si = s[i], sj = s[j];
      if (si <= eps) {
        const vi = keepVert(i);
        if (out[out.length - 1] !== vi) out.push(vi);
        if (si >= -eps) onPlane.push(vi);
      }
      if ((si > eps && sj < -eps) || (si < -eps && sj > eps)) {
        const vc = cutVert(i, j);
        if (out[out.length - 1] !== vc) out.push(vc);
        onPlane.push(vc);
      }
    }
    while (out.length > 1 && out[0] === out[out.length - 1]) out.pop();
    if (out.length >= 3) faces.push({ v: out, fresh: f.fresh, gen: f.gen });
    if (onPlane.length === 2 && onPlane[0] !== onPlane[1]) capEdges.push([onPlane[0], onPlane[1]]);
  }

  // Stitch the cap polygon from the collected boundary edges.
  if (capEdges.length >= 3) {
    const adj = new Map<number, number[]>();
    for (const [a, b] of capEdges) {
      (adj.get(a) ?? adj.set(a, []).get(a)!).push(b);
      (adj.get(b) ?? adj.set(b, []).get(b)!).push(a);
    }
    const start = capEdges[0][0];
    const loop: number[] = [start];
    let prev = -1, cur = start;
    for (let guard = 0; guard < capEdges.length + 2; guard++) {
      const nb = adj.get(cur);
      if (!nb) break;
      const nxt = nb.find((x) => x !== prev);
      if (nxt === undefined || nxt === start) break;
      loop.push(nxt);
      prev = cur; cur = nxt;
    }
    if (loop.length >= 3) {
      // Orient CCW as seen from outside (+plane.n).
      let nrm = v3();
      for (let k = 0; k < loop.length; k++) {
        const a = verts[loop[k]], b = verts[loop[(k + 1) % loop.length]];
        nrm = add(nrm, cross(a, b));
      }
      if (dot(nrm, plane.n) < 0) loop.reverse();
      faces.push({ v: loop, fresh: 1, gen });
    }
  }

  if (verts.length < 4 || faces.length < 4) return null;
  return { verts, faces };
}

/** Split into both halves. Either side may be null. */
export function splitConvex(poly: Convex, plane: Plane, gen: number): [Convex | null, Convex | null] {
  const a = clipConvex(poly, plane, gen);
  const b = clipConvex(poly, { n: mul(plane.n, -1), d: -plane.d }, gen);
  return [a, b];
}

export interface MassProps { volume: number; centroid: V3; inertia: Float32Array; }

/** Volume, centroid and inertia tensor (about centroid, unit density). */
export function massProperties(poly: Convex): MassProps {
  let vol = 0;
  const c = v3();
  // Covariance accumulation over tetrahedra (origin, a, b, c).
  let xx = 0, yy = 0, zz = 0, xy = 0, xz = 0, yz = 0;
  for (const f of poly.faces) {
    for (let k = 1; k + 1 < f.v.length; k++) {
      const a = poly.verts[f.v[0]], b = poly.verts[f.v[k]], d = poly.verts[f.v[k + 1]];
      const v = dot(a, cross(b, d)) / 6;
      vol += v;
      c.x += v * (a.x + b.x + d.x) / 4;
      c.y += v * (a.y + b.y + d.y) / 4;
      c.z += v * (a.z + b.z + d.z) / 4;
      const det = v * 6;
      const f1 = (p: 'x' | 'y' | 'z') => a[p] + b[p] + d[p];
      const f2 = (p: 'x' | 'y' | 'z') =>
        a[p] * a[p] + b[p] * b[p] + d[p] * d[p] + a[p] * b[p] + a[p] * d[p] + b[p] * d[p];
      const f3 = (p: 'x' | 'y' | 'z', q: 'x' | 'y' | 'z') =>
        2 * (a[p] * a[q] + b[p] * b[q] + d[p] * d[q]) +
        a[p] * b[q] + b[p] * a[q] + a[p] * d[q] + d[p] * a[q] + b[p] * d[q] + d[p] * b[q];
      xx += det * f2('x') / 60;
      yy += det * f2('y') / 60;
      zz += det * f2('z') / 60;
      xy += det * f3('x', 'y') / 120;
      xz += det * f3('x', 'z') / 120;
      yz += det * f3('y', 'z') / 120;
      void f1;
    }
  }
  if (Math.abs(vol) < 1e-12) {
    return { volume: 0, centroid: v3(), inertia: new Float32Array([1, 0, 0, 0, 1, 0, 0, 0, 1]) };
  }
  c.x /= vol; c.y /= vol; c.z /= vol;
  // Shift to centroid (parallel axis).
  xx -= vol * c.x * c.x; yy -= vol * c.y * c.y; zz -= vol * c.z * c.z;
  xy -= vol * c.x * c.y; xz -= vol * c.x * c.z; yz -= vol * c.y * c.z;
  const I = new Float32Array([
    yy + zz, -xy, -xz,
    -xy, xx + zz, -yz,
    -xz, -yz, xx + yy,
  ]);
  return { volume: Math.abs(vol), centroid: c, inertia: I };
}

export function translateConvex(poly: Convex, t: V3): void {
  for (const v of poly.verts) { v.x += t.x; v.y += t.y; v.z += t.z; }
}

export function faceNormal(poly: Convex, f: Face): V3 {
  let n = v3();
  for (let k = 0; k < f.v.length; k++) {
    const a = poly.verts[f.v[k]], b = poly.verts[f.v[(k + 1) % f.v.length]];
    n = add(n, cross(a, b));
  }
  return norm(n);
}

export function faceArea(poly: Convex, f: Face): number {
  let n = v3();
  for (let k = 0; k < f.v.length; k++) {
    const a = poly.verts[f.v[k]], b = poly.verts[f.v[(k + 1) % f.v.length]];
    n = add(n, cross(a, b));
  }
  return len(n) * 0.5;
}

export function boundingRadius(poly: Convex, c: V3): number {
  let r = 0;
  for (const v of poly.verts) r = Math.max(r, len(sub(v, c)));
  return r;
}

export interface MeshData {
  pos: Float32Array;
  nrm: Float32Array;
  /** per-vertex: x = fresh(0/1), y = roughness jitter, z = local coord for grain */
  attr: Float32Array;
  count: number;
  /** welded vertex id, for meshes driven by a vertex-animation texture */
  vid?: Float32Array;
  /** present for indexed meshes */
  idx?: Uint32Array;
}

/**
 * Triangulate a convex cell into a render mesh.
 *
 * Fresh fracture faces get tessellated and displaced by fractal noise scaled by
 * the material roughness — that is what makes a concrete break look like
 * concrete (aggregate pull-out), wood look fibrous, and glass stay near-mirror.
 * The displacement is visual only; the physics proxy stays convex.
 */
export function buildMesh(poly: Convex, roughness: number, grainDir: V3, seed = 0): MeshData {
  const pos: number[] = [], nrm: number[] = [], attr: number[] = [];
  const amp = roughness * 0.012;

  const push = (p: V3, n: V3, fresh: number, jitter: number, g: number) => {
    pos.push(p.x, p.y, p.z); nrm.push(n.x, n.y, n.z); attr.push(fresh, jitter, g);
  };

  for (const f of poly.faces) {
    const fn = faceNormal(poly, f);
    const area = faceArea(poly, f);
    const grain = dot(fn, grainDir);
    // Anisotropic displacement: wood tears along fibres, so stretch the noise
    // along the grain axis; concrete/rock use isotropic lumps.
    const disp = (p: V3): V3 => {
      if (amp <= 0) return p;
      const s = 34 + seed * 0.7;
      const gx = p.x + grainDir.x * 0.0, gy = p.y, gz = p.z;
      const nz = fbm3(gx * s, gy * s, gz * s, 3) - 0.5;
      const nz2 = fbm3(gz * s * 2.1 + 11, gx * s * 2.1, gy * s * 2.1, 2) - 0.5;
      return add(p, mul(fn, (nz * 1.4 + nz2 * 0.6) * amp * Math.min(1, Math.sqrt(area) * 6)));
    };

    const m = f.v.length;
    const subdiv = f.fresh && amp > 0 && area > 2e-4 ? 2 : 0;
    if (!subdiv) {
      for (let k = 1; k + 1 < m; k++) {
        const a = poly.verts[f.v[0]], b = poly.verts[f.v[k]], c = poly.verts[f.v[k + 1]];
        push(a, fn, f.fresh, f.gen, grain);
        push(b, fn, f.fresh, f.gen, grain);
        push(c, fn, f.fresh, f.gen, grain);
      }
    } else {
      // Fan from the face centroid, each triangle split once, then displaced.
      let ctr = v3();
      for (const i of f.v) ctr = add(ctr, poly.verts[i]);
      ctr = mul(ctr, 1 / m);
      const dc = disp(ctr);
      for (let k = 0; k < m; k++) {
        const a = poly.verts[f.v[k]], b = poly.verts[f.v[(k + 1) % m]];
        const mid = disp(mul(add(a, b), 0.5));
        const da = disp(a), db = disp(b);
        for (const [p, q, r] of [[da, mid, dc], [mid, db, dc]] as [V3, V3, V3][]) {
          const tn = norm(cross(sub(q, p), sub(r, p)));
          const nn = dot(tn, fn) < 0 ? mul(tn, -1) : tn;
          push(p, nn, f.fresh, f.gen, grain);
          push(q, nn, f.fresh, f.gen, grain);
          push(r, nn, f.fresh, f.gen, grain);
        }
      }
    }
  }
  return {
    pos: new Float32Array(pos),
    nrm: new Float32Array(nrm),
    attr: new Float32Array(attr),
    count: pos.length / 3,
  };
}
