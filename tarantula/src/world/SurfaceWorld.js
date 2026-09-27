import * as THREE from 'three';
import { computeBoundsTree, disposeBoundsTree, acceleratedRaycast } from 'three-mesh-bvh';

THREE.BufferGeometry.prototype.computeBoundsTree = computeBoundsTree;
THREE.BufferGeometry.prototype.disposeBoundsTree = disposeBoundsTree;
THREE.Mesh.prototype.raycast = acceleratedRaycast;

const _tri = new THREE.Triangle();
const _a = new THREE.Vector3(), _b = new THREE.Vector3(), _c = new THREE.Vector3();
const _na = new THREE.Vector3(), _nb = new THREE.Vector3(), _nc = new THREE.Vector3();

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
}
