// Convex polyhedron with exact half-space clipping.
// This is the workhorse: every crack is applied as a clip, so fragments are
// guaranteed watertight, convex (great for physics) and gap-free by construction.
import { BufferAttribute, BufferGeometry, Vector3 } from 'three';

export interface Plane { n: Vector3; d: number } // n·p = d, n unit
export const plane = (n: Vector3, p: Vector3): Plane => ({ n: n.clone().normalize(), d: n.dot(p) / n.length() });

export interface Face {
  idx: number[];
  /** true when the face was created by a crack (interior material). */
  interior: boolean;
  /** the crack plane normal in object space, identical on both sides of the
   *  crack, so displacement noise matches and fragments still mate. */
  crackN?: Vector3;
  crackSeed?: number;
}

const KEY = (v: Vector3) => `${Math.round(v.x * 1e5)},${Math.round(v.y * 1e5)},${Math.round(v.z * 1e5)}`;

export class Piece {
  constructor(public verts: Vector3[], public faces: Face[]) {}

  clone() {
    return new Piece(this.verts.map(v => v.clone()),
      this.faces.map(f => ({ ...f, idx: f.idx.slice(), crackN: f.crackN?.clone() })));
  }

  centroid(): Vector3 {
    // volume-weighted centroid via tetra decomposition
    const c = new Vector3(); let vol = 0;
    const o = this.verts[0];
    for (const f of this.faces) {
      for (let i = 1; i < f.idx.length - 1; i++) {
        const a = this.verts[f.idx[0]], b = this.verts[f.idx[i]], d = this.verts[f.idx[i + 1]];
        const ab = b.clone().sub(o), ac = d.clone().sub(o), aa = a.clone().sub(o);
        const v = aa.dot(ab.clone().cross(ac)) / 6;
        vol += v;
        c.addScaledVector(a.clone().add(b).add(d).add(o).multiplyScalar(0.25), v);
      }
    }
    return Math.abs(vol) < 1e-12 ? this.verts[0].clone() : c.multiplyScalar(1 / vol);
  }

  volume(): number {
    let vol = 0; const o = this.verts[0];
    for (const f of this.faces)
      for (let i = 1; i < f.idx.length - 1; i++) {
        const a = this.verts[f.idx[0]].clone().sub(o), b = this.verts[f.idx[i]].clone().sub(o), c = this.verts[f.idx[i + 1]].clone().sub(o);
        vol += a.dot(b.clone().cross(c)) / 6;
      }
    return Math.abs(vol);
  }

  /** Radius of the bounding sphere around the centroid. */
  radius(c = this.centroid()) {
    let r = 0; for (const v of this.verts) r = Math.max(r, v.distanceTo(c));
    return r;
  }

  bounds() {
    const mn = new Vector3(Infinity, Infinity, Infinity), mx = new Vector3(-Infinity, -Infinity, -Infinity);
    for (const v of this.verts) { mn.min(v); mx.max(v); }
    return { mn, mx };
  }

  translate(t: Vector3) { for (const v of this.verts) v.add(t); return this; }

  faceNormal(f: Face): Vector3 {
    const n = new Vector3(); const V = this.verts;
    for (let i = 0; i < f.idx.length; i++) {
      const a = V[f.idx[i]], b = V[f.idx[(i + 1) % f.idx.length]];
      n.x += (a.y - b.y) * (a.z + b.z); n.y += (a.z - b.z) * (a.x + b.x); n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n.normalize();
  }
}

/**
 * Split a convex piece by a plane. Returns the piece on the +n side and the
 * piece on the -n side (either may be null). The new cap faces are tagged as
 * crack surfaces.
 */
export function clip(piece: Piece, pl: Plane, crackSeed = 0): { front: Piece | null; back: Piece | null } {
  const eps = 1e-7;
  const V = piece.verts;
  const dist = V.map(v => pl.n.dot(v) - pl.d);
  let pos = 0, neg = 0;
  for (const d of dist) { if (d > eps) pos++; else if (d < -eps) neg++; }
  if (neg === 0) return { front: piece, back: null };
  if (pos === 0) return { front: null, back: piece };

  const fV: Vector3[] = [], bV: Vector3[] = [];
  const fMap = new Map<string, number>(), bMap = new Map<string, number>();
  const push = (arr: Vector3[], map: Map<string, number>, v: Vector3) => {
    const k = KEY(v); const e = map.get(k); if (e !== undefined) return e;
    map.set(k, arr.length); arr.push(v.clone()); return arr.length - 1;
  };

  const fFaces: Face[] = [], bFaces: Face[] = [];
  const cut: Vector3[] = [];

  for (const f of piece.faces) {
    const fp: number[] = [], bp: number[] = [];
    const n = f.idx.length;
    for (let i = 0; i < n; i++) {
      const ia = f.idx[i], ib = f.idx[(i + 1) % n];
      const da = dist[ia], db = dist[ib];
      const a = V[ia], b = V[ib];
      if (da >= -eps) fp.push(push(fV, fMap, a));
      if (da <= eps) bp.push(push(bV, bMap, a));
      if ((da > eps && db < -eps) || (da < -eps && db > eps)) {
        const t = da / (da - db);
        const x = a.clone().lerp(b, t);
        fp.push(push(fV, fMap, x)); bp.push(push(bV, bMap, x)); cut.push(x);
      }
    }
    if (fp.length >= 3) fFaces.push({ ...f, idx: fp, crackN: f.crackN?.clone() });
    if (bp.length >= 3) bFaces.push({ ...f, idx: bp, crackN: f.crackN?.clone() });
  }

  // Build the cap polygon: order the cut points around the plane normal.
  const uniq: Vector3[] = [];
  const seen = new Set<string>();
  for (const c of cut) { const k = KEY(c); if (!seen.has(k)) { seen.add(k); uniq.push(c); } }
  if (uniq.length >= 3) {
    const c = new Vector3();
    for (const p of uniq) c.add(p);
    c.multiplyScalar(1 / uniq.length);
    const ax = uniq[0].clone().sub(c).normalize();
    const ay = pl.n.clone().cross(ax).normalize();
    const ordered = uniq.map(p => {
      const d = p.clone().sub(c);
      return { p, a: Math.atan2(d.dot(ay), d.dot(ax)) };
    }).sort((x, y) => x.a - y.a).map(o => o.p);

    // front piece lies on +n, so its cap faces -n  -> clockwise around n
    const fi = ordered.map(p => push(fV, fMap, p)).reverse();
    const bi = ordered.map(p => push(bV, bMap, p));
    const cn = pl.n.clone();
    fFaces.push({ idx: fi, interior: true, crackN: cn, crackSeed });
    bFaces.push({ idx: bi, interior: true, crackN: cn.clone(), crackSeed });
  }

  const front = fV.length >= 4 ? new Piece(fV, fFaces) : null;
  const back = bV.length >= 4 ? new Piece(bV, bFaces) : null;
  return { front, back };
}

/* -------------------------- primitives -------------------------- */

export function box(sx: number, sy: number, sz: number): Piece {
  const x = sx / 2, y = sy / 2, z = sz / 2;
  const v = [
    new Vector3(-x, -y, -z), new Vector3(x, -y, -z), new Vector3(x, y, -z), new Vector3(-x, y, -z),
    new Vector3(-x, -y, z), new Vector3(x, -y, z), new Vector3(x, y, z), new Vector3(-x, y, z)];
  const f = (idx: number[]): Face => ({ idx, interior: false });
  return new Piece(v, [
    f([4, 5, 6, 7]), f([1, 0, 3, 2]), f([0, 4, 7, 3]), f([5, 1, 2, 6]), f([3, 7, 6, 2]), f([0, 1, 5, 4])]);
}

/** Convex prism / n-gon cylinder along +Y. Also used for boulders (then clipped). */
export function prism(radius: number, height: number, sides: number, jitter = 0, rand = () => 0.5): Piece {
  const v: Vector3[] = [], top: number[] = [], bot: number[] = [];
  const h = height / 2;
  for (let i = 0; i < sides; i++) {
    const a = (i / sides) * Math.PI * 2;
    const r = radius * (1 + (rand() - 0.5) * 2 * jitter);
    v.push(new Vector3(Math.cos(a) * r, -h, Math.sin(a) * r));
    v.push(new Vector3(Math.cos(a) * r, h, Math.sin(a) * r));
    bot.push(i * 2); top.push(i * 2 + 1);
  }
  const faces: Face[] = [{ idx: top, interior: false }, { idx: bot.slice().reverse(), interior: false }];
  for (let i = 0; i < sides; i++) {
    const j = (i + 1) % sides;
    faces.push({ idx: [i * 2, j * 2, j * 2 + 1, i * 2 + 1], interior: false });
  }
  return new Piece(v, faces);
}

/** Intersection of random half-spaces around a sphere: irregular rock blank. */
export function roughBlank(radius: number, cuts: number, rand: () => number, flatten = 1): Piece {
  let p: Piece | null = box(radius * 2.2, radius * 2.2 * flatten, radius * 2.2);
  for (let i = 0; i < cuts; i++) {
    const z = rand() * 2 - 1, a = rand() * Math.PI * 2, s = Math.sqrt(1 - z * z);
    const n = new Vector3(s * Math.cos(a), z * flatten, s * Math.sin(a)).normalize();
    const d = radius * (0.72 + rand() * 0.3);
    const r = clip(p!, { n, d }, -1);
    if (r.back) p = r.back;
  }
  for (const f of p!.faces) { f.interior = false; f.crackN = undefined; }
  return p!;
}

/* ------------------------ mesh generation ------------------------ */

export interface SurfaceOpts {
  /** displacement amplitude of crack surfaces (metres) */
  amp: number;
  /** spatial frequency of the crack roughness */
  freq: number;
  octaves: number;
  /** anisotropic stretch of the noise domain (e.g. wood grain) */
  stretch?: Vector3;
  /** subdivision level of interior faces (0 = flat) */
  subdiv: number;
  noise: (x: number, y: number, z: number) => number;
}

/**
 * Triangulate a piece. Interior (crack) faces are subdivided and displaced by
 * the shared noise field -> conchoidal / fibrous / aggregate micro relief
 * without any gaps between neighbouring fragments.
 * group 0 = exterior surface, group 1 = fracture surface.
 */
export function buildGeometry(piece: Piece, o: SurfaceOpts, origin = new Vector3()) {
  const pos: number[] = [], nrm: number[] = [], uv: number[] = [];
  const ext: number[] = [], inn: number[] = [];
  const V = piece.verts;

  const disp = (p: Vector3, n: Vector3, w: number, seed: number) => {
    if (o.amp <= 0 || w <= 0) return p;
    const s = o.stretch ?? new Vector3(1, 1, 1);
    const q = p.clone().add(origin);
    const f = o.freq;
    const nz = o.noise(q.x * f * s.x + seed * 13.7, q.y * f * s.y + seed * 7.1, q.z * f * s.z + seed * 3.3);
    return p.clone().addScaledVector(n, nz * o.amp * w);
  };

  for (const f of piece.faces) {
    const N = piece.faceNormal(f);
    const poly = f.idx.map(i => V[i]);
    const c = new Vector3(); poly.forEach(p => c.add(p)); c.multiplyScalar(1 / poly.length);
    let rad = 0; poly.forEach(p => (rad = Math.max(rad, p.distanceTo(c))));

    // tangent frame for UVs
    const t = Math.abs(N.y) < 0.9 ? new Vector3(0, 1, 0).cross(N).normalize() : new Vector3(1, 0, 0).cross(N).normalize();
    const b = N.clone().cross(t);
    const tris: Vector3[][] = [];

    if (f.interior && o.subdiv > 0) {
      // fan then midpoint-subdivide, so the crack face can carry relief
      for (let i = 0; i < poly.length; i++) {
        let sub = [[c, poly[i], poly[(i + 1) % poly.length]]];
        for (let s = 0; s < o.subdiv; s++) {
          const next: Vector3[][] = [];
          for (const [A, B, C] of sub) {
            const AB = A.clone().add(B).multiplyScalar(0.5);
            const BC = B.clone().add(C).multiplyScalar(0.5);
            const CA = C.clone().add(A).multiplyScalar(0.5);
            next.push([A, AB, CA], [AB, B, BC], [CA, BC, C], [AB, BC, CA]);
          }
          sub = next;
        }
        tris.push(...sub);
      }
    } else {
      for (let i = 1; i < poly.length - 1; i++) tris.push([poly[0], poly[i], poly[i + 1]]);
    }

    const cn = f.crackN ?? N;
    const seed = f.crackSeed ?? 0;
    const cache = new Map<string, Vector3>();
    const warp = (p: Vector3) => {
      if (!f.interior) return p;
      const k = KEY(p); const hit = cache.get(k); if (hit) return hit;
      // taper to zero at the face rim so neighbouring fragments + the outer
      // silhouette stay perfectly welded
      const dr = p.distanceTo(c) / (rad + 1e-6);
      const w = Math.min(1, 1.35 * (1 - dr * dr));
      const r = disp(p, cn, w, seed);
      cache.set(k, r); return r;
    };

    const target = f.interior ? inn : ext;
    for (const tri of tris) {
      const a = warp(tri[0]), bb = warp(tri[1]), cc = warp(tri[2]);
      const fn = bb.clone().sub(a).cross(cc.clone().sub(a)).normalize();
      if (fn.lengthSq() < 0.5) fn.copy(N);
      for (const p of [a, bb, cc]) {
        target.push(pos.length / 3);
        pos.push(p.x, p.y, p.z);
        nrm.push(fn.x, fn.y, fn.z);
        uv.push(p.dot(t), p.dot(b));
      }
    }
  }

  const g = new BufferGeometry();
  g.setAttribute('position', new BufferAttribute(new Float32Array(pos), 3));
  g.setAttribute('normal', new BufferAttribute(new Float32Array(nrm), 3));
  g.setAttribute('uv', new BufferAttribute(new Float32Array(uv), 2));
  g.setIndex([...ext, ...inn]);
  g.addGroup(0, ext.length, 0);
  g.addGroup(ext.length, inn.length, 1);
  return g;
}
