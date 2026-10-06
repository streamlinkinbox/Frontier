//============================================================================================================================================
//                                                               MESHSPEC.JS
//============================================================================================================================================
// A tiny renderer-agnostic triangle buffer. Generators push quads / polygons into a MeshSpec; the viewport converts it
// to a THREE.BufferGeometry and the exporter writes it as Wavefront OBJ. Normals are angle-weighted and welded, so
// road surfaces shade smoothly while curb folds and parapet edges stay crisp.

import { cross, norm, sub } from './Vec.js?v=8';

export class MeshSpec {
  constructor(name = 'mesh') {
    this.name = name;
    this.positions = []; // flat xyz
    this.uvs = []; // flat uv
    this.indices = [];
    this.normals = [];
  }

  get vertexCount() {
    return this.positions.length / 3;
  }

  get triangleCount() {
    return this.indices.length / 3;
  }

  reset() {
    this.positions.length = 0;
    this.uvs.length = 0;
    this.indices.length = 0;
    this.normals = [];
  }

  addVertex(p, uv) {
    this.positions.push(p.x, p.y, p.z);
    this.uvs.push(uv ? uv.x : p.x * 0.1, uv ? uv.y : p.y * 0.1);
    return this.vertexCount - 1;
  }

  addTriangle(a, b, c, uvA, uvB, uvC) {
    if (degenerate(a, b, c)) return;
    const i0 = this.addVertex(a, uvA);
    const i1 = this.addVertex(b, uvB);
    const i2 = this.addVertex(c, uvC);
    this.indices.push(i0, i1, i2);
  }

  // Quad given in winding order; split along the shorter diagonal to avoid skinny slivers on fillets.
  addFace(points, uvs) {
    if (points.length === 3) {
      this.addTriangle(points[0], points[1], points[2], uvs?.[0], uvs?.[1], uvs?.[2]);
      return;
    }
    const [p0, p1, p2, p3] = points;
    const u = uvs || [undefined, undefined, undefined, undefined];
    const d02 = sqrDist(p0, p2);
    const d13 = sqrDist(p1, p3);
    if (d02 <= d13) {
      this.addTriangle(p0, p1, p2, u[0], u[1], u[2]);
      this.addTriangle(p0, p2, p3, u[0], u[2], u[3]);
    } else {
      this.addTriangle(p0, p1, p3, u[0], u[1], u[3]);
      this.addTriangle(p1, p2, p3, u[1], u[2], u[3]);
    }
  }

  // Fan triangulation for convex caps (junction aprons, pier tops, deck ends).
  addPolygon(points, uvs) {
    if (points.length < 3) return;
    for (let i = 1; i < points.length - 1; i++) {
      this.addTriangle(points[0], points[i], points[i + 1], uvs?.[0], uvs?.[i], uvs?.[i + 1]);
    }
  }

  // Sweeps a closed cross-section profile (array of {x,y,z} already in world space per station) into a tube.
  addLoftClosed(stations, { capStart = true, capEnd = true, uvScale = 0.25 } = {}) {
    if (stations.length < 2) return;
    const ring = stations[0].length;
    for (let s = 0; s < stations.length - 1; s++) {
      for (let i = 0; i < ring; i++) {
        const j = (i + 1) % ring;
        this.addFace(
          [stations[s][i], stations[s + 1][i], stations[s + 1][j], stations[s][j]],
          [
            { x: s * uvScale, y: i * uvScale },
            { x: (s + 1) * uvScale, y: i * uvScale },
            { x: (s + 1) * uvScale, y: (i + 1) * uvScale },
            { x: s * uvScale, y: (i + 1) * uvScale },
          ],
        );
      }
    }
    if (capStart) this.addPolygon([...stations[0]].reverse());
    if (capEnd) this.addPolygon(stations[stations.length - 1]);
  }

  // Axis-aligned-ish box from 8 corners: [b0,b1,b2,b3, t0,t1,t2,t3] bottom then top, both CCW seen from above.
  addBox(corners) {
    const [b0, b1, b2, b3, t0, t1, t2, t3] = corners;
    this.addFace([t0, t1, t2, t3]);
    this.addFace([b3, b2, b1, b0]);
    this.addFace([b0, b1, t1, t0]);
    this.addFace([b1, b2, t2, t1]);
    this.addFace([b2, b3, t3, t2]);
    this.addFace([b3, b0, t0, t3]);
  }

  // Spreading (`push(...arr)`) blows the argument limit on anything large — a single highway corridor is already
  // ~90 000 numbers — so this copies element by element.
  append(other) {
    const offset = this.vertexCount;
    for (let i = 0; i < other.positions.length; i++) this.positions.push(other.positions[i]);
    for (let i = 0; i < other.uvs.length; i++) this.uvs.push(other.uvs[i]);
    for (let i = 0; i < other.indices.length; i++) this.indices.push(other.indices[i] + offset);
  }

  translate(dx, dy, dz) {
    for (let i = 0; i < this.positions.length; i += 3) {
      this.positions[i] += dx;
      this.positions[i + 1] += dy;
      this.positions[i + 2] += dz;
    }
  }

  // Angle-limited smoothing: faces meeting at more than `creaseDeg` keep hard edges.
  // Flat typed-array maths with a spatial hash — this runs on every rebuild, including while dragging a control
  // point, so it avoids per-vertex object allocation entirely.
  recalcNormals(creaseDeg = 50) {
    const idx = this.indices;
    const pos = this.positions;
    const triCount = idx.length / 3;
    const vCount = pos.length / 3;
    const cosLimit = Math.cos((creaseDeg * Math.PI) / 180);

    const fn = new Float32Array(triCount * 3);
    for (let t = 0; t < triCount; t++) {
      const a = idx[t * 3] * 3;
      const b = idx[t * 3 + 1] * 3;
      const c = idx[t * 3 + 2] * 3;
      const ux = pos[b] - pos[a];
      const uy = pos[b + 1] - pos[a + 1];
      const uz = pos[b + 2] - pos[a + 2];
      const vx = pos[c] - pos[a];
      const vy = pos[c + 1] - pos[a + 1];
      const vz = pos[c + 2] - pos[a + 2];
      let nx = uy * vz - uz * vy;
      let ny = uz * vx - ux * vz;
      let nz = ux * vy - uy * vx;
      const l = Math.hypot(nx, ny, nz);
      if (l > 1e-12) {
        nx /= l;
        ny /= l;
        nz /= l;
      } else {
        nz = 1;
      }
      fn[t * 3] = nx;
      fn[t * 3 + 1] = ny;
      fn[t * 3 + 2] = nz;
    }

    // vertex → weld bucket (hash of the quantised position)
    const buckets = new Map();
    const vertexFace = new Int32Array(vCount).fill(-1);
    for (let t = 0; t < triCount; t++) {
      for (let k = 0; k < 3; k++) {
        const vi = idx[t * 3 + k];
        vertexFace[vi] = t;
        const h = hashPosition(pos, vi);
        let bucket = buckets.get(h);
        if (!bucket) buckets.set(h, (bucket = []));
        bucket.push(vi);
      }
    }

    const normals = new Float32Array(vCount * 3);
    for (const bucket of buckets.values()) {
      for (let i = 0; i < bucket.length; i++) {
        const vi = bucket[i];
        const own = vertexFace[vi] * 3;
        const ox = fn[own];
        const oy = fn[own + 1];
        const oz = fn[own + 2];
        let nx = 0;
        let ny = 0;
        let nz = 0;
        for (let j = 0; j < bucket.length; j++) {
          const vj = bucket[j];
          if (
            Math.abs(pos[vj * 3] - pos[vi * 3]) > 1e-3 ||
            Math.abs(pos[vj * 3 + 1] - pos[vi * 3 + 1]) > 1e-3 ||
            Math.abs(pos[vj * 3 + 2] - pos[vi * 3 + 2]) > 1e-3
          ) {
            continue; // hash collision between genuinely different positions
          }
          const f = vertexFace[vj] * 3;
          if (fn[f] * ox + fn[f + 1] * oy + fn[f + 2] * oz < cosLimit) continue;
          nx += fn[f];
          ny += fn[f + 1];
          nz += fn[f + 2];
        }
        const l = Math.hypot(nx, ny, nz) || 1;
        normals[vi * 3] = nx / l;
        normals[vi * 3 + 1] = ny / l;
        normals[vi * 3 + 2] = nz / l;
      }
    }

    this.normals = normals;
    return this;
  }

  vertexAt(i) {
    return { x: this.positions[i * 3], y: this.positions[i * 3 + 1], z: this.positions[i * 3 + 2] };
  }
}

// Integer spatial hash at 1 mm resolution; bucket members are position-verified before they are averaged, so a hash
// collision costs a few wasted comparisons and nothing else.
function hashPosition(pos, i) {
  const x = Math.round(pos[i * 3] * 1000);
  const y = Math.round(pos[i * 3 + 1] * 1000);
  const z = Math.round(pos[i * 3 + 2] * 1000);
  return (Math.imul(x, 73856093) ^ Math.imul(y, 19349663) ^ Math.imul(z, 83492791)) | 0;
}

function sqrDist(a, b) {
  const dx = a.x - b.x;
  const dy = a.y - b.y;
  const dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

function degenerate(a, b, c) {
  const n = cross(sub(b, a), sub(c, a));
  return Math.hypot(n.x, n.y, n.z) < 1e-9;
}

// Wavefront OBJ for a set of named MeshSpecs. Z-up is converted to Y-up on export, which is what DCC tools expect.
export function toObj(groups, { yUp = true } = {}) {
  const lines = ['# RoadWorks Editor export', `# ${new Date().toISOString()}`];
  let offset = 1;
  for (const { name, spec } of groups) {
    if (!spec || spec.triangleCount === 0) continue;
    lines.push(`o ${name}`);
    for (let i = 0; i < spec.positions.length; i += 3) {
      const x = spec.positions[i];
      const y = spec.positions[i + 1];
      const z = spec.positions[i + 2];
      lines.push(yUp ? `v ${f(x)} ${f(z)} ${f(-y)}` : `v ${f(x)} ${f(y)} ${f(z)}`);
    }
    for (let i = 0; i < spec.uvs.length; i += 2) lines.push(`vt ${f(spec.uvs[i])} ${f(spec.uvs[i + 1])}`);
    if (spec.normals.length) {
      for (let i = 0; i < spec.normals.length; i += 3) {
        const x = spec.normals[i];
        const y = spec.normals[i + 1];
        const z = spec.normals[i + 2];
        lines.push(yUp ? `vn ${f(x)} ${f(z)} ${f(-y)}` : `vn ${f(x)} ${f(y)} ${f(z)}`);
      }
    }
    lines.push(`g ${name}`);
    for (let i = 0; i < spec.indices.length; i += 3) {
      const a = spec.indices[i] + offset;
      const b = spec.indices[i + 1] + offset;
      const c = spec.indices[i + 2] + offset;
      lines.push(spec.normals.length ? `f ${a}/${a}/${a} ${b}/${b}/${b} ${c}/${c}/${c}` : `f ${a}/${a} ${b}/${b} ${c}/${c}`);
    }
    offset += spec.vertexCount;
  }
  return lines.join('\n');
}

const f = (n) => (Math.abs(n) < 1e-6 ? '0' : n.toFixed(5));
