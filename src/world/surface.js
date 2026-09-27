/**
 * SURFACE FIELD
 *
 * An analytic, ray-queryable description of the world. Because the mosquito has
 * to attach to *any* uneven or smooth terrain — ground, a vertical wall, the
 * curved hull of a fuel tank, a crate — the locomotion code cannot assume a
 * height field. Everything is expressed as primitives that can be hit by a ray
 * from any direction and that return an exact surface normal.
 *
 * Analytic rather than mesh raycasting: exact, allocation-free, and fast enough
 * to run six foot queries per frame.
 */

import * as THREE from 'three';

const _p = new THREE.Vector3();
const _d = new THREE.Vector3();
const _n = new THREE.Vector3();

/* --------------------------------------------------------- primitives */

class Heightfield {
  constructor({ fn, gradient, extent, name = 'Terrain' }) {
    this.kind = 'field';
    this.fn = fn; this.gradient = gradient;
    this.extent = extent; this.name = name;
  }

  /** March + bisect. Cheaper and more robust than solving a polynomial. */
  hit(origin, dir, maxT, out) {
    const step = 0.55;
    let tPrev = 0;
    let dPrev = origin.y - this.fn(origin.x, origin.z);
    if (dPrev < 0) return false;           // started below the surface
    for (let t = step; t < maxT; t += step) {
      const x = origin.x + dir.x * t, y = origin.y + dir.y * t, z = origin.z + dir.z * t;
      if (Math.abs(x) > this.extent || Math.abs(z) > this.extent) return false;
      const d = y - this.fn(x, z);
      if (d <= 0) {
        // Bisect between tPrev and t
        let lo = tPrev, hi = t;
        for (let k = 0; k < 18; k++) {
          const mid = (lo + hi) * 0.5;
          const mx = origin.x + dir.x * mid, my = origin.y + dir.y * mid, mz = origin.z + dir.z * mid;
          if (my - this.fn(mx, mz) > 0) lo = mid; else hi = mid;
        }
        const tt = (lo + hi) * 0.5;
        out.point.set(origin.x + dir.x * tt, origin.y + dir.y * tt, origin.z + dir.z * tt);
        const g = this.gradient(out.point.x, out.point.z, _n);
        out.normal.copy(g);
        out.t = tt;
        return true;
      }
      tPrev = t; dPrev = d;
    }
    return false;
  }
}

class CylinderShell {
  /** Vertical cylinder, we stand on the OUTSIDE of it. */
  constructor({ x = 0, z = 0, y0 = 0, y1 = 100, r = 10, name = 'Cylinder' }) {
    this.kind = 'cyl';
    this.x = x; this.z = z; this.y0 = y0; this.y1 = y1; this.r = r; this.name = name;
  }
  hit(origin, dir, maxT, out) {
    const ox = origin.x - this.x, oz = origin.z - this.z;
    const a = dir.x * dir.x + dir.z * dir.z;
    if (a < 1e-8) return false;
    const b = 2 * (ox * dir.x + oz * dir.z);
    const c = ox * ox + oz * oz - this.r * this.r;
    const disc = b * b - 4 * a * c;
    if (disc < 0) return false;
    const sq = Math.sqrt(disc);
    for (const t of [(-b - sq) / (2 * a), (-b + sq) / (2 * a)]) {
      if (t < 1e-4 || t > maxT) continue;
      const y = origin.y + dir.y * t;
      if (y < this.y0 || y > this.y1) continue;
      out.point.set(origin.x + dir.x * t, y, origin.z + dir.z * t);
      out.normal.set((out.point.x - this.x) / this.r, 0, (out.point.z - this.z) / this.r);
      out.t = t;
      return true;
    }
    return false;
  }
}

class BoxSurface {
  constructor({ min, max, name = 'Box' }) {
    this.kind = 'box';
    this.min = new THREE.Vector3(...min);
    this.max = new THREE.Vector3(...max);
    this.name = name;
  }
  hit(origin, dir, maxT, out) {
    let tmin = 0, tmax = maxT, axis = -1, sign = 1;
    const o = [origin.x, origin.y, origin.z], d = [dir.x, dir.y, dir.z];
    const mn = [this.min.x, this.min.y, this.min.z], mx = [this.max.x, this.max.y, this.max.z];
    // Require the origin outside the box (we only ever stand on exteriors)
    let inside = true;
    for (let i = 0; i < 3; i++) if (o[i] < mn[i] || o[i] > mx[i]) inside = false;
    if (inside) return false;
    for (let i = 0; i < 3; i++) {
      if (Math.abs(d[i]) < 1e-8) {
        if (o[i] < mn[i] || o[i] > mx[i]) return false;
        continue;
      }
      const inv = 1 / d[i];
      let t1 = (mn[i] - o[i]) * inv, t2 = (mx[i] - o[i]) * inv;
      let s = -1;
      if (t1 > t2) { const t = t1; t1 = t2; t2 = t; s = 1; }
      if (t1 > tmin) { tmin = t1; axis = i; sign = s; }
      if (t2 < tmax) tmax = t2;
      if (tmin > tmax) return false;
    }
    if (tmin < 1e-4 || tmin > maxT || axis < 0) return false;
    out.point.set(origin.x + dir.x * tmin, origin.y + dir.y * tmin, origin.z + dir.z * tmin);
    out.normal.set(0, 0, 0);
    out.normal.setComponent(axis, sign);
    out.t = tmin;
    return true;
  }
}

class DiscSurface {
  /** Horizontal disc walked on from above — tank lids, plate glass, tables. */
  constructor({ x = 0, y = 0, z = 0, r = 10, name = 'Disc' }) {
    this.kind = 'disc';
    this.c = new THREE.Vector3(x, y, z); this.r = r; this.name = name;
  }
  hit(origin, dir, maxT, out) {
    if (Math.abs(dir.y) < 1e-8) return false;
    const t = (this.c.y - origin.y) / dir.y;
    if (t < 1e-4 || t > maxT) return false;
    const x = origin.x + dir.x * t, z = origin.z + dir.z * t;
    const dx = x - this.c.x, dz = z - this.c.z;
    if (dx * dx + dz * dz > this.r * this.r) return false;
    out.point.set(x, this.c.y, z);
    out.normal.set(0, 1, 0);
    out.t = t;
    return true;
  }
}

/**
 * A finite cylinder of arbitrary orientation — a pipe, a tank filler neck, a
 * railing. This is the primitive the fuel vent needs: its axis points radially
 * out of the hull, so a mosquito has to climb the hull, round the re-entrant
 * 90° corner where the stub meets it, and stand on the cap.
 *
 * `c0` and `c1` are the two end centres. The shell is open at both ends; pair
 * it with a `CapDisc` at `c1` to make the top walkable.
 */
class TubeSurface {
  constructor({ c0, c1, r = 5, name = 'Tube', segments = 16 }) {
    this.kind = 'tube';
    this.name = name;
    this.r = r;
    this.segments = segments;
    this.a = new THREE.Vector3().fromArray(c0);
    this.b = new THREE.Vector3().fromArray(c1);
    this.axis = new THREE.Vector3().subVectors(this.b, this.a);
    this.len = this.axis.length();
    this.axis.divideScalar(this.len || 1);
    // An orthonormal frame around the axis, for the inside/outside test.
    const seed = Math.abs(this.axis.y) > 0.9 ? new THREE.Vector3(1, 0, 0) : new THREE.Vector3(0, 1, 0);
    this.u = new THREE.Vector3().crossVectors(this.axis, seed).normalize();
    this.v = new THREE.Vector3().crossVectors(this.axis, this.u).normalize();
  }

  hit(origin, dir, maxT, out) {
    const A = this.a, U = this.u, V = this.v, W = this.axis;
    // Work in the frame where the tube axis is +Z.
    const to = origin.clone().sub(A);
    const ox = to.dot(U), oy = to.dot(V), oz = to.dot(W);
    const dx = dir.dot(U), dy = dir.dot(V), dz = dir.dot(W);
    const a = dx * dx + dy * dy;
    if (a < 1e-9) return false;                 // ray parallel to the axis
    const b = 2 * (ox * dx + oy * dy);
    const c = ox * ox + oy * oy - this.r * this.r;
    const disc = b * b - 4 * a * c;
    if (disc < 0) return false;
    const sq = Math.sqrt(disc);
    for (const t of [(-b - sq) / (2 * a), (-b + sq) / (2 * a)]) {
      if (t < 1e-4 || t > maxT) continue;
      const z = oz + dz * t;
      if (z < -1e-4 || z > this.len + 1e-4) continue;
      out.point.copy(origin).addScaledVector(dir, t);
      const rx = out.point.dot(U) - A.dot(U), ry = out.point.dot(V) - A.dot(V);
      const rl = Math.hypot(rx, ry) || 1;
      out.normal.copy(U).multiplyScalar(rx / rl).addScaledVector(V, ry / rl);
      out.t = t;
      return true;
    }
    return false;
  }

  /** Is `p` (given relative to centre `c`) inside the tube's end cap? */
  coversCap(c, p) {
    const d = p.clone().sub(c);
    return d.dot(this.axis) * d.dot(this.axis) + 0 >= 0
      && Math.hypot(d.dot(this.u), d.dot(this.v)) <= this.r;
  }
}

/** One flat disc at the end of a tube, facing along the tube's axis. */
class CapDisc {
  constructor({ c, axis, r = 5, name = 'Cap', hole = 0 }) {
    this.kind = 'disc';
    this.name = name;
    this.c = new THREE.Vector3().fromArray(c);
    this.n = new THREE.Vector3().fromArray(axis).normalize();
    this.r = r;
    this.hole = hole;                          // open breather hole at the centre
  }

  hit(origin, dir, maxT, out) {
    const denom = dir.dot(this.n);
    if (Math.abs(denom) < 1e-8) return false;
    const t = _d.subVectors(this.c, origin).dot(this.n) / denom;
    if (t < 1e-4 || t > maxT) return false;
    out.point.copy(origin).addScaledVector(dir, t);
    const radial = out.point.distanceTo(this.c);
    if (radial > this.r || radial < this.hole) return false;
    out.normal.copy(this.n);
    out.t = t;
    return true;
  }
}

class SphereSurface {
  constructor({ x = 0, y = 0, z = 0, r = 5, name = 'Sphere' }) {
    this.kind = 'sphere'; this.c = new THREE.Vector3(x, y, z); this.r = r; this.name = name;
  }
  hit(origin, dir, maxT, out) {
    const oc = _d.subVectors(origin, this.c);
    const b = 2 * oc.dot(dir);
    const c = oc.lengthSq() - this.r * this.r;
    const disc = b * b - 4 * c;
    if (disc < 0) return false;
    const sq = Math.sqrt(disc);
    for (const t of [(-b - sq) / 2, (-b + sq) / 2]) {
      if (t < 1e-4 || t > maxT) continue;
      out.point.copy(origin).addScaledVector(dir, t);
      out.normal.subVectors(out.point, this.c).normalize();
      out.t = t;
      return true;
    }
    return false;
  }
}

/* ------------------------------------------------------------- the field */

export class SurfaceField {
  constructor() {
    this.primitives = [];
    this._hit = { point: new THREE.Vector3(), normal: new THREE.Vector3(), t: 0, name: '' };
  }

  add(p) { this.primitives.push(p); return p; }

  /**
   * Cast a ray from `origin` along `dir` (need not be normalised).
   * Returns { point, normal, name, t } of the nearest surface, or null.
   */
  raycast(origin, dir, maxT = 200) {
    let best = null;
    for (const p of this.primitives) {
      const h = this._hit;
      if (p.hit(origin, dir, maxT, h)) {
        if (!best || h.t < best.t) {
          best = {
            point: h.point.clone(), normal: h.normal.clone(), name: p.name, t: h.t, prim: p,
          };
        }
      }
    }
    return best;
  }

  /**
   * Find the surface directly beneath `p` when falling along `down`.
   * Used by the foot-planting pass: the mosquito casts from a probe point
   * above the nominal foothold toward the body-down direction.
   */
  probe(probePoint, down, maxT = 60) {
    return this.raycast(probePoint, down, maxT);
  }

  /**
   * March along a direction and return the point offset `dist` from the
   * surface along the surface normal — used to lift the swing arc clear of
   * bumps without the foot tunnelling through the ground.
   */
  clearanceAbove(point, normal, dir, dist) {
    return this.raycast(point.clone().addScaledVector(dir, dist), normal.clone().negate(), dist * 2 + 1);
  }

  /** Surface roughness 0..1 at a point, used to decide claw vs pulvillus grip. */
  roughnessAt(point, normal) {
    const h = this.raycast(point.clone().addScaledVector(normal, 0.35), normal.clone().negate(), 1.2);
    if (!h) return 0.0;
    // Compare the true normal to the geometric normal
    return 1.0 - Math.max(0, h.normal.dot(normal));
  }
}

export { Heightfield, CylinderShell, BoxSurface, SphereSurface, DiscSurface, TubeSurface, CapDisc };
