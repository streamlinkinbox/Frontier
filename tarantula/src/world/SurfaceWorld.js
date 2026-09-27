import * as THREE from 'three';
import { computeBoundsTree, disposeBoundsTree, acceleratedRaycast } from 'three-mesh-bvh';

THREE.BufferGeometry.prototype.computeBoundsTree = computeBoundsTree;
THREE.BufferGeometry.prototype.disposeBoundsTree = disposeBoundsTree;
THREE.Mesh.prototype.raycast = acceleratedRaycast;

const _tri = new THREE.Triangle();
const _a = new THREE.Vector3(), _b = new THREE.Vector3(), _c = new THREE.Vector3();
const _I = new THREE.Matrix4();
const _na = new THREE.Vector3(), _nb = new THREE.Vector3(), _nc = new THREE.Vector3();
const _fn = new THREE.Vector3(), _fn2 = new THREE.Vector3(), _dv = new THREE.Vector3();
const _ray = new THREE.Ray(), _inv = new THREE.Matrix4();
const _DIRS = [new THREE.Vector3(0.31, 0.9, 0.3).normalize(), new THREE.Vector3(-0.7, -0.2, 0.68).normalize(), new THREE.Vector3(0.6, -0.5, -0.62).normalize()];

// Collision/adhesion surface queries (BVH accelerated). Returns smooth (interpolated) normals so
// the spider's body frame transitions continuously between floor, walls and ceiling.
export class SurfaceWorld {
  constructor() {
    this.meshes = [];
    this.raycaster = new THREE.Raycaster();
    this.raycaster.firstHitOnly = true;
  }

  add(mesh) {
    if (!mesh.geometry.boundsTree) mesh.geometry.computeBoundsTree({ targetLeafSize: 12 });
    this.meshes.push(mesh);
  }

  raycast(origin, dir, far) {
    const rc = this.raycaster;
    rc.set(origin, dir);
    rc.near = 0; rc.far = far;
    let best = null;
    for (const m of this.meshes) {
      const hits = [];
      m.raycast(rc, hits);
      if (hits.length && (!best || hits[0].distance < best.distance)) { best = hits[0]; best.object = m; }
    }
    if (!best) return null;
    const g = best.object.geometry, face = best.face;
    const nrm = g.attributes.normal, pos = g.attributes.position;
    let normal;
    if (nrm && face) {
      _a.fromBufferAttribute(pos, face.a); _b.fromBufferAttribute(pos, face.b); _c.fromBufferAttribute(pos, face.c);
      _na.fromBufferAttribute(nrm, face.a); _nb.fromBufferAttribute(nrm, face.b); _nc.fromBufferAttribute(nrm, face.c);
      const local = best.object.worldToLocal(best.point.clone());
      _tri.set(_a, _b, _c);
      normal = new THREE.Vector3();
      THREE.Triangle.getInterpolation(local, _a, _b, _c, _na, _nb, _nc, normal);
      normal.transformDirection(best.object.matrixWorld).normalize();
      // guard against back-face or degenerate interpolation
      if (normal.dot(dir) > 0 && face.normal.dot(dir) < 0) normal.copy(face.normal).transformDirection(best.object.matrixWorld);
    } else normal = face.normal.clone().transformDirection(best.object.matrixWorld);
    return { point: best.point, normal, distance: best.distance, object: best.object };
  }

  /**
   * Signed distance from `p` to the nearest collision surface (positive = in open air, negative =
   * inside rock), plus the closest point and smooth surface normal. null if nothing within maxDist.
   */
  closest(p, maxDist = 3) {
    let best = null, bestMesh = null;
    const t = this._cpt || (this._cpt = {});
    for (const m of this.meshes) {
      const lp = _a.copy(p); if (!m.matrixWorld.equals(_I)) m.worldToLocal(lp);
      const r = m.geometry.boundsTree.closestPointToPoint(lp, t, 0, maxDist);
      if (r && (!best || r.distance < best.distance)) { best = { point: r.point.clone(), distance: r.distance, faceIndex: r.faceIndex }; bestMesh = m; }
    }
    if (!best) return null;
    const g = bestMesh.geometry, idx = g.index, pos = g.attributes.position, nrm = g.attributes.normal;
    const ia = idx ? idx.getX(best.faceIndex * 3) : best.faceIndex * 3, ib = idx ? idx.getX(best.faceIndex * 3 + 1) : best.faceIndex * 3 + 1, ic = idx ? idx.getX(best.faceIndex * 3 + 2) : best.faceIndex * 3 + 2;
    _a.fromBufferAttribute(pos, ia); _b.fromBufferAttribute(pos, ib); _c.fromBufferAttribute(pos, ic);
    const normal = new THREE.Vector3();
    _na.fromBufferAttribute(nrm, ia); _nb.fromBufferAttribute(nrm, ib); _nc.fromBufferAttribute(nrm, ic);
    THREE.Triangle.getInterpolation(best.point, _a, _b, _c, _na, _nb, _nc, normal);
    if (normal.lengthSq() < 1e-8) _tri.set(_a, _b, _c).getNormal(normal);
    normal.transformDirection(bestMesh.matrixWorld).normalize();
    const point = best.point.applyMatrix4(bestMesh.matrixWorld);
    // Inside/outside. The smooth normal alone is unreliable next to creases and edges (the nearest
    // point lies on an edge/vertex and the interpolated normal can face away), which used to report
    // open air as deep inside rock. Trust it only when it agrees with the flat face normal and the
    // offset is clearly along it; otherwise decide with double-sided ray parity (majority of 3).
    let inside = false;
    if (best.distance > 1e-5) {
      _tri.set(_a, _b, _c).getNormal(_fn).transformDirection(bestMesh.matrixWorld);
      const d = _dv.copy(p).sub(point).divideScalar(best.distance);
      const cF = d.dot(_fn), cN = d.dot(normal);
      if (Math.abs(cF) > 0.5 && (cF > 0) === (cN > 0)) inside = cF < 0;
      else {
        let votes = 0;
        for (let i = 0; i < 3; i++) { const v = this._rayInside(p, _DIRS[i]); votes += v; if (votes >= 2 || votes <= i - 1) break; }
        inside = votes >= 2;
      }
    }
    return { point, normal, distance: inside ? -best.distance : best.distance };
  }

  // 1 if the first surface hit along `dir` is seen from behind (p is inside rock), else 0.
  _rayInside(p, dir) {
    let bestD = Infinity, inside = 0;
    for (const m of this.meshes) {
      _ray.origin.copy(p); _ray.direction.copy(dir);
      if (!m.matrixWorld.equals(_I)) { _inv.copy(m.matrixWorld).invert(); _ray.applyMatrix4(_inv); }
      const h = m.geometry.boundsTree.raycastFirst(_ray, THREE.DoubleSide);
      if (!h) continue;
      const hp = h.point.applyMatrix4(m.matrixWorld), dd = hp.distanceTo(p);
      if (dd < bestD) { bestD = dd; inside = _fn2.copy(h.face.normal).transformDirection(m.matrixWorld).dot(dir) > 0 ? 1 : 0; }
    }
    return inside;
  }
}
