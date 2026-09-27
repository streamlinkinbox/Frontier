/**
 * MESH BUILDER
 *
 * A small, allocation-conscious geometry kernel. Everything the mosquito is
 * made of — sclerites, setae, claws, wing membrane — is emitted through these
 * primitives, so a single material can be assembled from many disjoint parts
 * without extra draw calls.
 */

import * as THREE from 'three';

const V = new THREE.Vector3();

/* ------------------------------------------------------------- frames */

/**
 * Parallel-transport frames along a polyline. Avoids the flipping you get from
 * naive Frenet frames, which matters for the labium sheath and the antennae
 * where the path curves tightly.
 */
export function transportFrames(points, up = new THREE.Vector3(0, 1, 0)) {
  const n = points.length;
  const tangents = [], normals = [], binormals = [];
  for (let i = 0; i < n; i++) {
    const a = points[Math.max(0, i - 1)], b = points[Math.min(n - 1, i + 1)];
    tangents.push(new THREE.Vector3().subVectors(b, a).normalize());
  }
  let ref = up.clone();
  if (Math.abs(ref.dot(tangents[0])) > 0.95) ref.set(1, 0, 0);
  let nrm = new THREE.Vector3().crossVectors(tangents[0], ref).normalize();
  for (let i = 0; i < n; i++) {
    if (i > 0) {
      // Rotate the previous normal by the minimal rotation taking t[i-1]→t[i]
      const q = new THREE.Quaternion().setFromUnitVectors(tangents[i - 1], tangents[i]);
      nrm.applyQuaternion(q);
    }
    // Re-orthogonalise
    nrm.sub(tangents[i].clone().multiplyScalar(nrm.dot(tangents[i]))).normalize();
    normals.push(nrm.clone());
    binormals.push(new THREE.Vector3().crossVectors(tangents[i], nrm).normalize());
  }
  return { tangents, normals, binormals };
}

/* ------------------------------------------------------- MeshBuilder */

export class MeshBuilder {
  constructor() {
    this.pos = []; this.nor = []; this.uv = []; this.idx = [];
  }

  get vertexCount() { return this.pos.length / 3; }

  pushVertex(p, n, u, v) {
    this.pos.push(p.x, p.y, p.z);
    this.nor.push(n.x, n.y, n.z);
    this.uv.push(u, v);
    return this.vertexCount - 1;
  }

  /**
   * Swept tube.
   * @param {THREE.Vector3[]} points spine
   * @param {(i:number,t:number)=>number} radiusFn radius at station i
   * @param {object} opts
   *   radial   - tube resolution
   *   capStart / capEnd - close the ends
   *   uRepeat  - texture repeats along the length
   *   vOffset  - texture offset around the circumference
   *   scale    - elliptical cross-section (1, e) — mosquito legs are flattened
   *   frames   - pre-computed transport frames
   */
  tube(points, radiusFn, opts = {}) {
    const {
      radial = 12, capStart = false, capEnd = true,
      uRepeat = 1, vOffset = 0, flatten = 1, frames = null, uStart = 0, uEnd = 1,
    } = opts;
    const f = frames || transportFrames(points);
    const n = points.length;
    const start = this.vertexCount;
    const scaleFn = typeof flatten === 'function' ? flatten : () => flatten;

    for (let i = 0; i < n; i++) {
      const t = n === 1 ? 0 : i / (n - 1);
      const r = radiusFn(i, t);
      const sc = scaleFn(i, t);
      const P = points[i], N = f.normals[i], B = f.binormals[i];
      for (let j = 0; j <= radial; j++) {
        const a = (j / radial) * Math.PI * 2;
        const cx = Math.cos(a) * r, cy = Math.sin(a) * r * sc;
        const p = new THREE.Vector3(
          P.x + N.x * cx + B.x * cy,
          P.y + N.y * cx + B.y * cy,
          P.z + N.z * cx + B.z * cy);
        // Analytic-ish normal: radial direction corrected by the taper slope
        const dt = 1 / (n - 1 || 1);
        const rPrev = radiusFn(Math.max(0, i - 1), Math.max(0, t - dt));
        const rNext = radiusFn(Math.min(n - 1, i + 1), Math.min(1, t + dt));
        const slope = (rNext - rPrev) / (2 * dt * (points[Math.min(n - 1, i + 1)].distanceTo(points[Math.max(0, i - 1)]) || 1));
        const nr = new THREE.Vector3(
          N.x * Math.cos(a) + B.x * Math.sin(a) * sc,
          N.y * Math.cos(a) + B.y * Math.sin(a) * sc,
          N.z * Math.cos(a) + B.z * Math.sin(a) * sc).normalize();
        nr.addScaledVector(f.tangents[i], -slope).normalize();
        this.pushVertex(p, nr, uStart + (uEnd - uStart) * t * uRepeat, vOffset + j / radial);
      }
    }
    for (let i = 0; i < n - 1; i++) {
      for (let j = 0; j < radial; j++) {
        const a = start + i * (radial + 1) + j;
        const b = a + 1;
        const c = a + radial + 1;
        const d = c + 1;
        this.idx.push(a, c, b, b, c, d);
      }
    }
    if (capEnd) this._cap(points[n - 1], f.tangents[n - 1], radiusFn(n - 1, 1), radial, scaleFn(n - 1, 1), 1, uEnd);
    if (capStart) this._cap(points[0], f.tangents[0].clone().negate(), radiusFn(0, 0), radial, scaleFn(0, 0), -1, uStart);
    return start;
  }

  _cap(center, dir, r, radial, sc, sign, u) {
    const base = this.vertexCount;
    const nrm = dir.clone().multiplyScalar(sign);
    this.pushVertex(center, nrm, u, 0.5);
    const up = Math.abs(nrm.y) > 0.9 ? new THREE.Vector3(1, 0, 0) : new THREE.Vector3(0, 1, 0);
    const N = new THREE.Vector3().crossVectors(up, nrm).normalize();
    const B = new THREE.Vector3().crossVectors(nrm, N).normalize();
    for (let j = 0; j <= radial; j++) {
      const a = (j / radial) * Math.PI * 2;
      const p = new THREE.Vector3(
        center.x + (N.x * Math.cos(a) + B.x * Math.sin(a) * sc) * r,
        center.y + (N.y * Math.cos(a) + B.y * Math.sin(a) * sc) * r,
        center.z + (N.z * Math.cos(a) + B.z * Math.sin(a) * sc) * r);
      this.pushVertex(p, nrm, u, 0.5 + Math.cos(a) * 0.5);
    }
    for (let j = 0; j < radial; j++) {
      if (sign > 0) this.idx.push(base, base + 1 + j, base + 2 + j);
      else this.idx.push(base, base + 2 + j, base + 1 + j);
    }
  }

  /**
   * Parametric patch. `fn(u, v, out)` writes a position; normals are derived
   * from finite differences unless `fnN` is supplied.
   */
  surface(fn, nu, nv, opts = {}) {
    const { flip = false, uWrap = false, vWrap = false, fnN = null, uv = null } = opts;
    const start = this.vertexCount;
    const eps = 1e-3;
    const p = new THREE.Vector3();
    const pU0 = new THREE.Vector3(), pU1 = new THREE.Vector3();
    const pV0 = new THREE.Vector3(), pV1 = new THREE.Vector3();
    const du = new THREE.Vector3(), dv = new THREE.Vector3(), n = new THREE.Vector3();
    // A wrapped parameter wraps instead of clamping, so the normal is
    // continuous across the seam of a closed surface (the wing membrane).
    const su = (t) => (uWrap ? ((t % 1) + 1) % 1 : Math.min(1, Math.max(0, t)));
    const sv = (t) => (vWrap ? ((t % 1) + 1) % 1 : Math.min(1, Math.max(0, t)));
    for (let i = 0; i <= nu; i++) {
      for (let j = 0; j <= nv; j++) {
        const u = i / nu, v = j / nv;
        fn(u, v, p);
        if (fnN) {
          fnN(u, v, n);
        } else {
          fn(su(u + eps), v, pU1); fn(su(u - eps), v, pU0);
          fn(u, sv(v + eps), pV1); fn(u, sv(v - eps), pV0);
          du.subVectors(pU1, pU0);
          dv.subVectors(pV1, pV0);
          n.crossVectors(du, dv);
          if (n.lengthSq() < 1e-16) n.set(0, 1, 0);
          n.normalize();
        }
        if (flip) n.negate();
        const t = uv ? uv(u, v) : [u, v];
        this.pushVertex(p, n, t[0], t[1]);
      }
    }
    const stride = nv + 1;
    for (let i = 0; i < nu; i++) {
      for (let j = 0; j < nv; j++) {
        const p0 = start + i * stride + j;
        const p1 = p0 + 1, p2 = p0 + stride, p3 = p2 + 1;
        if (flip) this.idx.push(p0, p1, p2, p1, p3, p2);
        else this.idx.push(p0, p2, p1, p1, p2, p3);
      }
    }
    return start;
  }

  /**
   * Hair / seta / spine. A 3-segment tapered cone. These are the micro-setae
   * that give an insect its silhouette; instancing thousands of them as real
   * geometry is affordable here because the whole insect is < 40k triangles.
   */
  hair(origin, dir, length, baseRadius, opts = {}) {
    const { seg = 3, tipRatio = 0.06, bend = 0, radial = 4, tipRatioY = 1 } = opts;
    const d = dir.clone().normalize();
    const side = new THREE.Vector3().crossVectors(d, Math.abs(d.y) > 0.9 ? new THREE.Vector3(1, 0, 0) : new THREE.Vector3(0, 1, 0)).normalize();
    const up = new THREE.Vector3().crossVectors(side, d).normalize();
    const pts = [];
    for (let i = 0; i <= seg; i++) {
      const t = i / seg;
      const bow = bend * Math.sin(t * Math.PI) * length * 0.35;
      pts.push(new THREE.Vector3(
        origin.x + d.x * length * t + up.x * bow,
        origin.y + d.y * length * t + up.y * bow,
        origin.z + d.z * length * t + up.z * bow));
    }
    const f = transportFrames(pts, up);
    const start = this.vertexCount;
    for (let i = 0; i <= seg; i++) {
      const t = i / seg;
      const r = baseRadius * (1 - t * (1 - tipRatio));
      for (let j = 0; j < radial; j++) {
        const ang = (j / radial) * Math.PI * 2;
        const nx = f.normals[i].x * Math.cos(ang) + f.binormals[i].x * Math.sin(ang);
        const ny = f.normals[i].y * Math.cos(ang) + f.binormals[i].y * Math.sin(ang);
        const nz = f.normals[i].z * Math.cos(ang) + f.binormals[i].z * Math.sin(ang);
        const p = new THREE.Vector3(pts[i].x + nx * r, pts[i].y + ny * r, pts[i].z + nz * r);
        const nn = new THREE.Vector3(nx, ny, nz);
        this.pushVertex(p, nn, t, j / radial);
      }
    }
    for (let i = 0; i < seg; i++) {
      for (let j = 0; j < radial; j++) {
        const a0 = start + i * radial + j;
        const a1 = start + i * radial + ((j + 1) % radial);
        const b0 = a0 + radial, b1 = a1 + radial;
        this.idx.push(a0, b0, a1, a1, b0, b1);
      }
    }
    return start;
  }

  /** Append another builder's geometry, optionally transformed. */
  append(other, matrix = null) {
    const base = this.vertexCount;
    if (matrix) {
      const nm = new THREE.Matrix3().getNormalMatrix(matrix);
      const p = new THREE.Vector3(), n = new THREE.Vector3();
      for (let i = 0; i < other.pos.length; i += 3) {
        p.set(other.pos[i], other.pos[i + 1], other.pos[i + 2]).applyMatrix4(matrix);
        n.set(other.nor[i], other.nor[i + 1], other.nor[i + 2]).applyMatrix3(nm).normalize();
        this.pos.push(p.x, p.y, p.z); this.nor.push(n.x, n.y, n.z);
      }
    } else {
      for (let i = 0; i < other.pos.length; i++) { this.pos.push(other.pos[i]); this.nor.push(other.nor[i]); }
    }
    for (let i = 0; i < other.uv.length; i++) this.uv.push(other.uv[i]);
    for (let i = 0; i < other.idx.length; i++) this.idx.push(other.idx[i] + base);
    return base;
  }

  build(computeNormals = false) {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.nor, 3));
    g.setAttribute('uv', new THREE.Float32BufferAttribute(this.uv, 2));
    g.setIndex(this.idx);
    if (computeNormals) { g.deleteAttribute('normal'); g.computeVertexNormals(); }
    g.computeBoundingSphere();
    g.computeBoundingBox();
    return g;
  }
}

/* --------------------------------------------------------- utilities */

/** Catmull-Rom resample of a polyline into `count` evenly-parameterised points. */
export function resample(points, count) {
  const curve = new THREE.CatmullRomCurve3(points, false, 'centripetal', 0.5);
  return curve.getSpacedPoints(count - 1);
}

export function spherePoints(radius, nu = 16, nv = 24) {
  const pts = [];
  for (let i = 0; i < nu; i++) {
    const phi = (i / (nu - 1)) * Math.PI;
    for (let j = 0; j < nv; j++) {
      const th = (j / nv) * Math.PI * 2;
      pts.push(new THREE.Vector3(
        radius.x * Math.sin(phi) * Math.cos(th),
        radius.y * Math.cos(phi),
        radius.z * Math.sin(phi) * Math.sin(th)));
    }
  }
  return pts;
}

/**
 * Super-ellipsoid blob. The head and thorax are not spheres; they are
 * sclerotised boxes with rounded edges, so we use a super-ellipsoid
 * (exponent controls "boxiness") modulated by a per-direction radius function.
 */
export function blobGeometry({
  size = [1, 1, 1], exponent = 0.78, nu = 40, nv = 56,
  centre = [0, 0, 0], radiusMod = null, uScale = 1, vScale = 1,
} = {}) {
  const b = new MeshBuilder();
  const sgnPow = (v, e) => Math.sign(v) * Math.pow(Math.abs(v), e);
  const centreV = new THREE.Vector3(...centre);
  b.surface((u, v, out) => {
    const phi = u * Math.PI;          // 0..PI  (pole to pole)
    const th = v * Math.PI * 2;       // around
    const sp = sgnPow(Math.sin(phi), exponent);
    const cp = sgnPow(Math.cos(phi), exponent);
    const st = sgnPow(Math.sin(th), exponent);
    const ct = sgnPow(Math.cos(th), exponent);
    let m = 1;
    if (radiusMod) m = radiusMod(u, v);
    out.set(
      centreV.x + size[0] * sp * ct * m,
      centreV.y + size[1] * cp * m,
      centreV.z + size[2] * sp * st * m);
  }, nu, nv, { uv: (u, v) => [v * uScale, u * vScale] });
  return b;
}

/** Simple lathe of a 2-D profile (array of [radius, y]). */
export function latheGeometry(profile, segments = 32, uvScale = [1, 1]) {
  const b = new MeshBuilder();
  const pts = profile.map(([r, y]) => new THREE.Vector3(0, y, 0));
  b.surface((u, v, out) => {
    const t = u * (profile.length - 1);
    const i0 = Math.min(profile.length - 2, Math.floor(t));
    const f = t - i0;
    const r = profile[i0][0] * (1 - f) + profile[i0 + 1][0] * f;
    const y = profile[i0][1] * (1 - f) + profile[i0 + 1][1] * f;
    const a = v * Math.PI * 2;
    out.set(Math.cos(a) * r, y, Math.sin(a) * r);
  }, profile.length * 2, segments, { uWrap: false, uv: (u, v) => [u * uvScale[0], v * uvScale[1]] });
  return b;
}
