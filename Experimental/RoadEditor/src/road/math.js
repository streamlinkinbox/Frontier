// Small, allocation-light vector helpers shared by generator and mesh builders.
// World convention: X east, Z south, Y up. Right normal of a ground tangent (tx,tz) is (-tz, tx).

export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smooth = t => t * t * (3 - 2 * t);
export const smoother = t => t * t * t * (t * (t * 6 - 15) + 10);
export const TAU = Math.PI * 2;

export function len2(x, z) { return Math.hypot(x, z); }
export function norm2(x, z) { const l = Math.hypot(x, z) || 1; return [x / l, z / l]; }
export function angleOf(x, z) { return Math.atan2(z, x); }
export function wrapAngle(a) { while (a < 0) a += TAU; while (a >= TAU) a -= TAU; return a; }

// Seeded PRNG (mulberry32) so generated cities are reproducible from a seed.
export function rng(seed) {
  let a = (seed >>> 0) || 1;
  const f = () => {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  f.range = (lo, hi) => lo + (hi - lo) * f();
  f.int = (lo, hi) => Math.floor(lo + (hi - lo + 1) * f());
  f.pick = arr => arr[Math.floor(f() * arr.length)];
  f.chance = p => f() < p;
  return f;
}

// Intersection of two infinite 2D lines p + d*t and q + e*s. Returns [t, s] or null if parallel.
export function lineIntersect(px, pz, dx, dz, qx, qz, ex, ez) {
  const den = dx * ez - dz * ex;
  if (Math.abs(den) < 1e-9) return null;
  const wx = qx - px, wz = qz - pz;
  return [(wx * ez - wz * ex) / den, (wx * dz - wz * dx) / den];
}

// Segment/segment intersection with parameters; tolerant endpoint handling via eps.
export function segIntersect(ax, az, bx, bz, cx, cz, dx, dz, eps = 1e-6) {
  const rx = bx - ax, rz = bz - az, sx = dx - cx, sz = dz - cz;
  const den = rx * sz - rz * sx;
  if (Math.abs(den) < 1e-10) return null;
  const qx = cx - ax, qz = cz - az;
  const t = (qx * sz - qz * sx) / den, u = (qx * rz - qz * rx) / den;
  if (t < -eps || t > 1 + eps || u < -eps || u > 1 + eps) return null;
  return [t, u];
}

export function pointSegDist(px, pz, ax, az, bx, bz) {
  const dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
  let t = l2 > 0 ? ((px - ax) * dx + (pz - az) * dz) / l2 : 0;
  t = clamp(t, 0, 1);
  const x = ax + dx * t, z = az + dz * t;
  return [Math.hypot(px - x, pz - z), t, x, z];
}

// Distance from a point to a polyline [[x,z]...]; returns {d, i, t, x, z, s} (s = arclength).
export function pointPolylineDist(px, pz, pts) {
  let best = { d: Infinity }, acc = 0;
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    const [d, t, x, z] = pointSegDist(px, pz, a[0], a[1], b[0], b[1]);
    const l = Math.hypot(b[0] - a[0], b[1] - a[1]);
    if (d < best.d) best = { d, i, t, x, z, s: acc + l * t };
    acc += l;
  }
  return best;
}

export function polylineLength(pts) {
  let l = 0;
  for (let i = 1; i < pts.length; i++) l += Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]);
  return l;
}

// Uniformly resample a polyline at spacing (keeps endpoints).
export function resample(pts, spacing) {
  const L = polylineLength(pts);
  if (L === 0) return [pts[0].slice()];
  const n = Math.max(1, Math.round(L / spacing));
  const out = [];
  let seg = 0, segStart = 0, segLen = Math.hypot(pts[1][0] - pts[0][0], pts[1][1] - pts[0][1]);
  for (let k = 0; k <= n; k++) {
    const s = (k / n) * L;
    while (s > segStart + segLen && seg < pts.length - 2) { segStart += segLen; seg++; segLen = Math.hypot(pts[seg + 1][0] - pts[seg][0], pts[seg + 1][1] - pts[seg][1]); }
    const t = segLen > 0 ? clamp((s - segStart) / segLen, 0, 1) : 0;
    out.push([lerp(pts[seg][0], pts[seg + 1][0], t), lerp(pts[seg][1], pts[seg + 1][1], t)]);
  }
  return out;
}

// Value noise (2D) for warps and variation.
export function makeNoise(seed) {
  const r = rng(seed);
  const perm = new Uint8Array(512), grad = new Float32Array(256);
  for (let i = 0; i < 256; i++) { perm[i] = i; grad[i] = r() * 2 - 1; }
  for (let i = 255; i > 0; i--) { const j = Math.floor(r() * (i + 1)); [perm[i], perm[j]] = [perm[j], perm[i]]; }
  for (let i = 0; i < 256; i++) perm[i + 256] = perm[i];
  const h = (x, z) => grad[perm[(perm[x & 255] + z) & 255]];
  return (x, z) => {
    const xi = Math.floor(x), zi = Math.floor(z), xf = x - xi, zf = z - zi;
    const u = smooth(xf), v = smooth(zf);
    return lerp(lerp(h(xi, zi), h(xi + 1, zi), u), lerp(h(xi, zi + 1), h(xi + 1, zi + 1), u), v);
  };
}
