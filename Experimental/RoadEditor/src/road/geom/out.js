// Element geometry output. Builders write indexed triangle pieces per material plus prop
// instances. Pieces stay plain arrays so builders run in Node tests and in the browser.
// Winding: every primitive emits counter-clockwise triangles seen from its outward side.

export class Out {
  constructor(id) {
    this.id = id;
    this.mesh = Object.create(null);
    this.props = Object.create(null);
    this.min = [Infinity, Infinity, Infinity];
    this.max = [-Infinity, -Infinity, -Infinity];
  }
  piece(mat) {
    return this.mesh[mat] || (this.mesh[mat] = { p: [], uv: [], i: [] });
  }
  grow(x, y, z) {
    if (x < this.min[0]) this.min[0] = x; if (y < this.min[1]) this.min[1] = y; if (z < this.min[2]) this.min[2] = z;
    if (x > this.max[0]) this.max[0] = x; if (y > this.max[1]) this.max[1] = y; if (z > this.max[2]) this.max[2] = z;
  }
  vert(pc, x, y, z, u, v) {
    pc.p.push(x, y, z); pc.uv.push(u, v); this.grow(x, y, z);
    return pc.p.length / 3 - 1;
  }
  // Instance record: x,y,z,yaw,pitch,sx,sy,sz
  prop(type, x, y, z, yaw = 0, sx = 1, sy = 1, sz = 1, pitch = 0) {
    (this.props[type] || (this.props[type] = [])).push(x, y, z, yaw, pitch, sx, sy, sz);
    this.grow(x, y, z);
  }
  get triangles() { let n = 0; for (const k in this.mesh) n += this.mesh[k].i.length / 3; return n; }
  get center() { return [(this.min[0] + this.max[0]) / 2, (this.min[2] + this.max[2]) / 2]; }
}

export const yawFor = (dx, dz) => Math.atan2(-dz, dx); // local +X → world (dx, dz)

// Ribbon over frames between lateral offsets (o0 left, o1 right) — upward facing.
// h0/h1 heights above frame.y; offsets may be functions of the frame index for tapers.
export function ribbon(out, mat, frames, o0, o1, h0, h1 = h0, uvScale = 1) {
  if (frames.length < 2) return;
  const pc = out.piece(mat), base = pc.p.length / 3;
  for (let k = 0; k < frames.length; k++) {
    const f = frames[k];
    const a = typeof o0 === 'function' ? o0(k, f) : o0, b = typeof o1 === 'function' ? o1(k, f) : o1;
    out.vert(pc, f.x + f.nx * a, f.y + h0, f.z + f.nz * a, a * uvScale, f.s * uvScale);
    out.vert(pc, f.x + f.nx * b, f.y + h1, f.z + f.nz * b, b * uvScale, f.s * uvScale);
  }
  for (let k = 0; k < frames.length - 1; k++) {
    const l0 = base + k * 2, r0 = l0 + 1, l1 = l0 + 2, r1 = l0 + 3;
    pc.i.push(l0, r0, l1, r0, r1, l1);
  }
}

// Vertical wall strip at lateral offset o, from hb to ht; side = +1 faces right (+n), -1 faces left.
export function wall(out, mat, frames, o, hb, ht, side) {
  if (frames.length < 2 || ht - hb < 1e-4) return;
  const pc = out.piece(mat), base = pc.p.length / 3;
  for (const f of frames) {
    const a = typeof o === 'function' ? o(f) : o;
    const x = f.x + f.nx * a, z = f.z + f.nz * a;
    const yb = typeof hb === 'function' ? hb(f) : f.y + hb;
    const yt = typeof ht === 'function' ? ht(f) : f.y + ht;
    out.vert(pc, x, yb, z, f.s, yb);
    out.vert(pc, x, yt, z, f.s, yt);
  }
  for (let k = 0; k < frames.length - 1; k++) {
    const b0 = base + k * 2, t0 = b0 + 1, b1 = b0 + 2, t1 = b0 + 3;
    if (side > 0) pc.i.push(b0, b1, t0, t0, b1, t1);
    else pc.i.push(b0, t0, b1, b1, t0, t1);
  }
}

// Sweep an open 2D profile [[o, h], ...] along frames; profile traced so that its outward
// side is on the right of the direction of travel in the (o, h) plane looking along +s.
export function sweep(out, mat, frames, shape, oBase = 0, flat = true) {
  if (frames.length < 2) return;
  const pc = out.piece(mat);
  for (let j = 0; j < shape.length - 1; j++) {
    const [oa, ha] = shape[j], [ob, hb] = shape[j + 1];
    const seg = Math.hypot(ob - oa, hb - ha);
    const base = pc.p.length / 3;
    for (const f of frames) {
      const a = oBase + oa, b = oBase + ob;
      out.vert(pc, f.x + f.nx * a, f.y + ha, f.z + f.nz * a, f.s, 0);
      out.vert(pc, f.x + f.nx * b, f.y + hb, f.z + f.nz * b, f.s, seg);
    }
    for (let k = 0; k < frames.length - 1; k++) {
      const a0 = base + k * 2, b0 = a0 + 1, a1 = a0 + 2, b1 = a0 + 3;
      pc.i.push(a0, a1, b0, b0, a1, b1);
    }
  }
}

// Strip between two equal-length polylines of [x,y,z] (inner → outer), upward facing when
// polylines are ordered counter-clockwise around the junction center (as built by junction.js).
export function strip(out, mat, A, B, flip = false) {
  const n = Math.min(A.length, B.length);
  if (n < 2) return;
  const pc = out.piece(mat), base = pc.p.length / 3;
  for (let k = 0; k < n; k++) {
    out.vert(pc, A[k][0], A[k][1], A[k][2], A[k][0], A[k][2]);
    out.vert(pc, B[k][0], B[k][1], B[k][2], B[k][0], B[k][2]);
  }
  for (let k = 0; k < n - 1; k++) {
    const a0 = base + k * 2, b0 = a0 + 1, a1 = a0 + 2, b1 = a0 + 3;
    if (flip) pc.i.push(a0, b0, a1, a1, b0, b1);
    else pc.i.push(a0, a1, b0, b0, a1, b1);
  }
}

// Vertical wall along a polyline [[x,y,z]] from y+hb to y+ht; side +1 = faces right of travel.
export function polyWall(out, mat, P, hb, ht, side) {
  if (P.length < 2) return;
  const pc = out.piece(mat), base = pc.p.length / 3;
  let u = 0;
  for (let k = 0; k < P.length; k++) {
    if (k) u += Math.hypot(P[k][0] - P[k - 1][0], P[k][2] - P[k - 1][2]);
    out.vert(pc, P[k][0], P[k][1] + hb, P[k][2], u, hb);
    out.vert(pc, P[k][0], P[k][1] + ht, P[k][2], u, ht);
  }
  for (let k = 0; k < P.length - 1; k++) {
    const b0 = base + k * 2, t0 = b0 + 1, b1 = b0 + 2, t1 = b0 + 3;
    if (side < 0) pc.i.push(b0, t0, b1, b1, t0, t1);
    else pc.i.push(b0, b1, t0, t0, b1, t1);
  }
}

// Horizontal polygon (already triangulated indices) with world-space UVs.
export function polygon(out, mat, pts3, tris, flip = false) {
  const pc = out.piece(mat), base = pc.p.length / 3;
  for (const p of pts3) out.vert(pc, p[0], p[1], p[2], p[0], p[2]);
  for (let k = 0; k < tris.length; k += 3) {
    if (!flip) pc.i.push(base + tris[k], base + tris[k + 1], base + tris[k + 2]);
    else pc.i.push(base + tris[k], base + tris[k + 2], base + tris[k + 1]);
  }
}

// Oriented quad on the ground: center (x,y,z), along-direction (tx,tz), half sizes, uv rect.
export function quad(out, mat, x, y, z, tx, tz, halfL, halfW, uv = [0, 0, 1, 1]) {
  const pc = out.piece(mat), base = pc.p.length / 3;
  const nx = -tz, nz = tx;
  const [u0, v0, u1, v1] = uv;
  out.vert(pc, x - tx * halfL - nx * halfW, y, z - tz * halfL - nz * halfW, u0, v0);
  out.vert(pc, x - tx * halfL + nx * halfW, y, z - tz * halfL + nz * halfW, u1, v0);
  out.vert(pc, x + tx * halfL - nx * halfW, y, z + tz * halfL - nz * halfW, u0, v1);
  out.vert(pc, x + tx * halfL + nx * halfW, y, z + tz * halfL + nz * halfW, u1, v1);
  pc.i.push(base, base + 1, base + 2, base + 1, base + 3, base + 2);
}

// Box (axis = frame), used for deck end caps etc.
export function box(out, mat, cx, cy, cz, tx, tz, hl, hw, hh) {
  const nx = -tz, nz = tx;
  const P = [];
  for (const sy of [-1, 1]) for (const sl of [-1, 1]) for (const sw of [-1, 1]) {
    P.push([cx + tx * hl * sl + nx * hw * sw, cy + hh * sy, cz + tz * hl * sl + nz * hw * sw]);
  }
  // faces as quads of indices into P (outward CCW)
  const F = [[4, 5, 6, 7], [0, 2, 1, 3], [0, 1, 4, 5], [2, 6, 3, 7], [0, 4, 2, 6], [1, 3, 5, 7]];
  const pc = out.piece(mat);
  for (const [a, b, c, d] of F) {
    const base = pc.p.length / 3;
    for (const k of [a, b, c, d]) out.vert(pc, P[k][0], P[k][1], P[k][2], P[k][0] + P[k][2], P[k][1]);
    pc.i.push(base, base + 1, base + 2, base + 1, base + 3, base + 2);
  }
}
