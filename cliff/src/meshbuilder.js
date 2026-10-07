/*
 * Accumulates world-space triangles (position + normal + colour) so that every
 * generated piece can be merged into a single draw call.  Kept deliberately
 * tiny - it is the only "geometry plumbing" in the project.
 */
import * as THREE from '../vendor/three.module.js';

function hash01(n) {
  let h = Math.imul(n ^ 0x27d4eb2d, 0x165667b1);
  h ^= h >>> 15;
  h = Math.imul(h, 0x85ebca6b);
  h ^= h >>> 13;
  return (h >>> 0) / 4294967296;
}

export class MeshBuilder {
  constructor() {
    this.pos = [];
    this.nrm = [];
    this.col = [];
    this.tris = 0;
    this._nm = new THREE.Matrix3();
    this._v = new THREE.Vector3();
    this._n = new THREE.Vector3();
    this._c = new THREE.Color();
  }

  /** Append a (non-indexed) geometry with a transform and a base tint colour. */
  addGeometry(geo, matrix, tint, opts = {}) {
    const { jitter = 0, jitterSeed = 0, shade = 1, shadePerTri = null } = opts;
    const pos = geo.attributes.position;
    const nor = geo.attributes.normal;
    if (!pos || !nor) return this;

    this._nm.getNormalMatrix(matrix);
    const triCount = pos.count / 3;

    for (let i = 0; i < pos.count; i++) {
      const tri = (i / 3) | 0;
      this._v.fromBufferAttribute(pos, i).applyMatrix4(matrix);
      this._n.fromBufferAttribute(nor, i).applyMatrix3(this._nm).normalize();

      let s = shade;
      if (shadePerTri) s *= shadePerTri[tri];
      if (jitter > 0) s *= 1 + (hash01(tri * 9781 + jitterSeed) - 0.5) * jitter;

      this._c.copy(tint).multiplyScalar(s);
      this.pos.push(this._v.x, this._v.y, this._v.z);
      this.nrm.push(this._n.x, this._n.y, this._n.z);
      this.col.push(this._c.r, this._c.g, this._c.b);
    }
    this.tris += triCount;
    return this;
  }

  /** One triangle, three (position, normal, colour) corners. */
  addTri(p0, p1, p2, n0, n1, n2, c0, c1, c2) {
    this._push(p0, n0, c0);
    this._push(p1, n1, c1);
    this._push(p2, n2, c2);
    this.tris++;
    return this;
  }

  _push(p, n, c) {
    this.pos.push(p.x, p.y, p.z);
    this.nrm.push(n.x, n.y, n.z);
    this.col.push(c.r, c.g, c.b);
  }

  build() {
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.pos, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.nrm, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(this.col, 3));
    g.computeBoundingSphere();
    g.computeBoundingBox();
    return g;
  }
}
