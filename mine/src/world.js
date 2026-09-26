// Collision world backed by a BVH of the merged mine mesh.
import * as THREE from 'three';
import { MeshBVH } from 'three-mesh-bvh';

const _ray = new THREE.Ray();
const _cp = new THREE.Vector3();
const _n = new THREE.Vector3();
const _d = new THREE.Vector3();
const _box = new THREE.Box3();

export class World {
  constructor() { this.bvh = null; }
  setGeometry(geometry) {
    this.bvh = new MeshBVH(geometry, { targetLeafSize: 8 });
    this.geometry = geometry;
  }
  // first hit, returns {distance, point, normal} or null
  raycast(origin, dir, far) {
    if (!this.bvh) return null;
    _ray.origin.copy(origin); _ray.direction.copy(dir);
    const hit = this.bvh.raycastFirst(_ray, THREE.DoubleSide, 0, far);
    if (!hit || hit.distance > far) return null;
    const n = hit.face.normal.clone();
    return { distance: hit.distance, point: hit.point.clone(), normal: n };
  }
  // deepest penetration of a sphere against walls/roof (floor-like triangles ignored)
  // returns {depth, normal, point} or null
  sphereContact(center, radius, floorLimit = 0.55) {
    if (!this.bvh) return null;
    let best = null;
    this.bvh.shapecast({
      intersectsBounds: (box) => {
        _box.copy(box);
        return _box.distanceToPoint(center) < radius;
      },
      intersectsTriangle: (tri) => {
        tri.getNormal(_n);
        if (_n.y > floorLimit) return false; // floor: handled by the wheels
        tri.closestPointToPoint(center, _cp);
        _d.subVectors(center, _cp);
        const dist = _d.length();
        if (dist > radius) return false;
        const side = _d.dot(_n);
        if (side < 0) return false; // back side (e.g. far wall of a groove) -> ignore
        const depth = radius - dist;
        const nrm = dist > 1e-5 ? _d.clone().divideScalar(dist) : _n.clone();
        if (!best || depth > best.depth) best = { depth, normal: nrm, point: _cp.clone() };
        return false;
      },
    });
    return best;
  }
}
