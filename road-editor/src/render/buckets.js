// Lightweight triangle buckets. Each material key owns one bucket; buckets become one
// BufferGeometry each, so the whole city costs only a few draw calls.

export class Bucket {
  constructor() {
    this.pos = [];
    this.nrm = [];
    this.col = [];
    this.idx = [];
    this.count = 0;
  }

  /** Vertex with normal and colour multiplier. */
  vert(x, y, z, nx, ny, nz, tint = 1) {
    this.pos.push(x, y, z);
    this.nrm.push(nx, ny, nz);
    this.col.push(tint, tint, tint);
    return this.count++;
  }

  tri(a, b, c, tint = 1) {
    const n = normalOf(a, b, c);
    const i0 = this.vert(a.x, a.y, a.z, n.x, n.y, n.z, tint);
    const i1 = this.vert(b.x, b.y, b.z, n.x, n.y, n.z, tint);
    const i2 = this.vert(c.x, c.y, c.z, n.x, n.y, n.z, tint);
    this.idx.push(i0, i1, i2);
  }

  quad(a, b, c, d, tint = 1) {
    this.tri(a, b, c, tint);
    this.tri(a, c, d, tint);
  }

  /** Ribbon between two polylines of equal length (L[i], R[i]). */
  strip(L, R, tint = 1) {
    for (let i = 0; i < L.length - 1; i++) this.quad(L[i], R[i], R[i + 1], L[i + 1], tint);
  }

  /** Thick line of width w in the ground plane following a polyline of {x,y,z}. */
  ribbon(pts, w, tint = 1, lift = 0) {
    const L = [], R = [];
    for (let i = 0; i < pts.length; i++) {
      const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
      let tx = b.x - a.x, tz = b.z - a.z;
      const l = Math.hypot(tx, tz) || 1;
      tx /= l; tz /= l;
      const nx = -tz * w / 2, nz = tx * w / 2;
      L.push({ x: pts[i].x + nx, y: pts[i].y + lift, z: pts[i].z + nz });
      R.push({ x: pts[i].x - nx, y: pts[i].y + lift, z: pts[i].z - nz });
    }
    this.strip(L, R, tint);
  }

  /** Vertical wall between two polylines, from base height to top height (arrays of {x,y,z}). */
  wall(base, top, tint = 1) {
    for (let i = 0; i < base.length - 1; i++) this.quad(base[i], base[i + 1], top[i + 1], top[i], tint);
  }

  /** Axis-aligned box rotated by yaw around Y. center.y is the bottom of the box. */
  box(cx, cy, cz, sx, sy, sz, yaw = 0, tint = 1) {
    const c = Math.cos(yaw), s = Math.sin(yaw);
    const P = (lx, ly, lz) => ({ x: cx + lx * c + lz * s, y: cy + ly, z: cz - lx * s + lz * c });
    const hx = sx / 2, hz = sz / 2;
    const p = [
      P(-hx, 0, -hz), P(hx, 0, -hz), P(hx, 0, hz), P(-hx, 0, hz),
      P(-hx, sy, -hz), P(hx, sy, -hz), P(hx, sy, hz), P(-hx, sy, hz),
    ];
    this.quad(p[3], p[2], p[6], p[7], tint); // +z face
    this.quad(p[1], p[0], p[4], p[5], tint); // -z
    this.quad(p[2], p[1], p[5], p[6], tint); // +x
    this.quad(p[0], p[3], p[7], p[4], tint); // -x
    this.quad(p[4], p[7], p[6], p[5], tint); // top
    this.quad(p[0], p[1], p[2], p[3], tint); // bottom
  }

  /** Vertical cylinder (open-ended sides, closed top). */
  cylinder(cx, cy, cz, radius, height, seg = 8, tint = 1) {
    const ring = (y) => Array.from({ length: seg + 1 }, (_, k) => {
      const a = (k / seg) * Math.PI * 2;
      return { x: cx + Math.cos(a) * radius, y, z: cz + Math.sin(a) * radius };
    });
    const bot = ring(cy), top = ring(cy + height);
    this.wall(bot, top, tint);
    const c = { x: cx, y: cy + height, z: cz };
    for (let k = 0; k < seg; k++) this.tri(c, top[k], top[k + 1], tint);
  }

  /** Round tube along a polyline of {x,y,z}. */
  tube(pts, radius, seg = 6, tint = 1) {
    if (pts.length < 2) return;
    const frames = pts.map((p, i) => {
      const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
      let t = norm({ x: b.x - a.x, y: b.y - a.y, z: b.z - a.z });
      let up = Math.abs(t.y) > 0.9 ? { x: 1, y: 0, z: 0 } : { x: 0, y: 1, z: 0 };
      let n = norm(cross(t, up));
      let bi = cross(n, t);
      return { p, n, bi };
    });
    const rings = frames.map((f) => Array.from({ length: seg }, (_, k) => {
      const a = (k / seg) * Math.PI * 2;
      const dx = f.n.x * Math.cos(a) + f.bi.x * Math.sin(a);
      const dy = f.n.y * Math.cos(a) + f.bi.y * Math.sin(a);
      const dz = f.n.z * Math.cos(a) + f.bi.z * Math.sin(a);
      return { v: { x: f.p.x + dx * radius, y: f.p.y + dy * radius, z: f.p.z + dz * radius }, nrm: { x: dx, y: dy, z: dz } };
    }));
    for (let i = 0; i < rings.length - 1; i++) {
      for (let k = 0; k < seg; k++) {
        const k1 = (k + 1) % seg;
        const a = rings[i][k], b = rings[i][k1], c = rings[i + 1][k1], d = rings[i + 1][k];
        const i0 = this.vert(a.v.x, a.v.y, a.v.z, a.nrm.x, a.nrm.y, a.nrm.z, tint);
        const i1 = this.vert(b.v.x, b.v.y, b.v.z, b.nrm.x, b.nrm.y, b.nrm.z, tint);
        const i2 = this.vert(c.v.x, c.v.y, c.v.z, c.nrm.x, c.nrm.y, c.nrm.z, tint);
        const i3 = this.vert(d.v.x, d.v.y, d.v.z, d.nrm.x, d.nrm.y, d.nrm.z, tint);
        this.idx.push(i0, i1, i2, i0, i2, i3);
      }
    }
  }

  isEmpty() { return this.count === 0; }
}

function cross(a, b) {
  return { x: a.y * b.z - a.z * b.y, y: a.z * b.x - a.x * b.z, z: a.x * b.y - a.y * b.x };
}
function norm(v) {
  const l = Math.hypot(v.x, v.y, v.z) || 1;
  return { x: v.x / l, y: v.y / l, z: v.z / l };
}
function normalOf(a, b, c) {
  const n = cross({ x: b.x - a.x, y: b.y - a.y, z: b.z - a.z }, { x: c.x - a.x, y: c.y - a.y, z: c.z - a.z });
  return norm(n);
}
