// 2D (x,z) geometry helpers. All road topology is computed in the ground plane;
// heights are resolved later from the terrain / layer rules.

export const dist2 = (a, b) => Math.hypot(a.x - b.x, a.z - b.z);

/** Remove consecutive duplicate points (closer than eps). */
export function dedupe(points, eps = 0.05) {
  const out = [];
  for (const p of points) {
    if (!out.length || dist2(out[out.length - 1], p) > eps) out.push({ x: p.x, z: p.z });
  }
  return out;
}

/**
 * Centripetal Catmull-Rom spline through the control points (alpha = 0.5).
 * Centripetal parametrisation never overshoots, so roads don't loop back on themselves
 * around closely spaced control points. Returns densely sampled points (~spacing apart).
 */
export function catmullRom(controls, spacing = 2) {
  const P = dedupe(controls);
  if (P.length === 0) return [];
  if (P.length === 1) return [{ x: P[0].x, z: P[0].z }];
  if (P.length === 2) {
    const n = Math.max(1, Math.ceil(dist2(P[0], P[1]) / spacing));
    const out = [];
    for (let k = 0; k <= n; k++) {
      const t = k / n;
      out.push({ x: P[0].x + (P[1].x - P[0].x) * t, z: P[0].z + (P[1].z - P[0].z) * t });
    }
    return out;
  }
  // Mirror the end points so the spline passes through the first and last control point.
  const ext = [
    { x: 2 * P[0].x - P[1].x, z: 2 * P[0].z - P[1].z },
    ...P,
    { x: 2 * P[P.length - 1].x - P[P.length - 2].x, z: 2 * P[P.length - 1].z - P[P.length - 2].z },
  ];
  const out = [{ x: P[0].x, z: P[0].z }];
  for (let i = 1; i < ext.length - 2; i++) {
    const seg = crSegment(ext[i - 1], ext[i], ext[i + 1], ext[i + 2], spacing);
    for (let k = 1; k < seg.length; k++) out.push(seg[k]);
  }
  return dedupe(out, 0.02);
}

function crSegment(p0, p1, p2, p3, spacing) {
  const alpha = 0.5;
  const tj = (ti, a, b) => ti + Math.pow(Math.max(dist2(a, b), 1e-6), alpha);
  const t0 = 0;
  const t1 = tj(t0, p0, p1);
  const t2 = tj(t1, p1, p2);
  const t3 = tj(t2, p2, p3);
  const n = Math.max(2, Math.ceil(dist2(p1, p2) / spacing));
  const out = [];
  for (let k = 0; k <= n; k++) {
    const t = t1 + ((t2 - t1) * k) / n;
    const lerp = (a, b, ta, tb) => ({
      x: ((tb - t) / (tb - ta)) * a.x + ((t - ta) / (tb - ta)) * b.x,
      z: ((tb - t) / (tb - ta)) * a.z + ((t - ta) / (tb - ta)) * b.z,
    });
    const A1 = lerp(p0, p1, t0, t1);
    const A2 = lerp(p1, p2, t1, t2);
    const A3 = lerp(p2, p3, t2, t3);
    const B1 = lerp(A1, A2, t0, t2);
    const B2 = lerp(A2, A3, t1, t3);
    out.push(lerp(B1, B2, t1, t2));
  }
  return out;
}

/** Cumulative arc length, one value per point. */
export function cumulative(pts) {
  const s = new Float64Array(pts.length);
  for (let i = 1; i < pts.length; i++) s[i] = s[i - 1] + dist2(pts[i - 1], pts[i]);
  return s;
}

export function bbox(pts) {
  let minX = Infinity, minZ = Infinity, maxX = -Infinity, maxZ = -Infinity;
  for (const p of pts) {
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.z < minZ) minZ = p.z;
    if (p.z > maxZ) maxZ = p.z;
  }
  return { minX, minZ, maxX, maxZ };
}

export function bboxOverlap(a, b, pad = 0) {
  return a.minX - pad <= b.maxX && a.maxX + pad >= b.minX && a.minZ - pad <= b.maxZ && a.maxZ + pad >= b.minZ;
}

/** Proper segment intersection. Returns {t, u, x, z} with t,u in [0,1] or null. */
export function segmentIntersection(a, b, c, d) {
  const rX = b.x - a.x, rZ = b.z - a.z;
  const sX = d.x - c.x, sZ = d.z - c.z;
  const den = rX * sZ - rZ * sX;
  if (Math.abs(den) < 1e-12) return null; // parallel / collinear: handled as overlap elsewhere
  const qpX = c.x - a.x, qpZ = c.z - a.z;
  const t = (qpX * sZ - qpZ * sX) / den;
  const u = (qpX * rZ - qpZ * rX) / den;
  if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u > 1 + 1e-9) return null;
  return { t, u, x: a.x + rX * t, z: a.z + rZ * t };
}

/** All crossings between two polylines. Returns [{sA, sB, x, z, ia, ib}] (s = arc length). */
export function polylineCrossings(A, sA, B, sB) {
  const out = [];
  const bA = bbox(A), bB = bbox(B);
  if (!bboxOverlap(bA, bB, 0.01)) return out;
  // Spatial hash of B's segments so each A segment only tests nearby B segments.
  const CELL = 40;
  const grid = new Map();
  for (let j = 0; j < B.length - 1; j++) {
    const b0 = B[j], b1 = B[j + 1];
    const x0 = Math.floor(Math.min(b0.x, b1.x) / CELL), x1 = Math.floor(Math.max(b0.x, b1.x) / CELL);
    const z0 = Math.floor(Math.min(b0.z, b1.z) / CELL), z1 = Math.floor(Math.max(b0.z, b1.z) / CELL);
    for (let cx = x0; cx <= x1; cx++) for (let cz = z0; cz <= z1; cz++) {
      const k = cx * 100003 + cz;
      let list = grid.get(k);
      if (!list) grid.set(k, (list = []));
      list.push(j);
    }
  }
  const seen = new Set();
  for (let i = 0; i < A.length - 1; i++) {
    const a0 = A[i], a1 = A[i + 1];
    const x0 = Math.floor(Math.min(a0.x, a1.x) / CELL), x1 = Math.floor(Math.max(a0.x, a1.x) / CELL);
    const z0 = Math.floor(Math.min(a0.z, a1.z) / CELL), z1 = Math.floor(Math.max(a0.z, a1.z) / CELL);
    for (let cx = x0; cx <= x1; cx++) for (let cz = z0; cz <= z1; cz++) {
      const list = grid.get(cx * 100003 + cz);
      if (!list) continue;
      for (const j of list) {
        const key = i * 1048576 + j;
        if (seen.has(key)) continue;
        seen.add(key);
        const b0 = B[j], b1 = B[j + 1];
        const hit = segmentIntersection(a0, a1, b0, b1);
        if (!hit) continue;
        // A crossing exactly on a shared sample vertex is reported by both adjacent segments.
        if (out.some((o) => Math.hypot(o.x - hit.x, o.z - hit.z) < 0.05)) continue;
        out.push({
          sA: sA[i] + (sA[i + 1] - sA[i]) * hit.t,
          sB: sB[j] + (sB[j + 1] - sB[j]) * hit.u,
          x: hit.x,
          z: hit.z,
          ia: i,
          ib: j,
        });
      }
    }
  }
  return out;
}

/** Closest point on a polyline to p. */
export function closestOnPolyline(pts, s, p) {
  let best = { d2: Infinity, x: 0, z: 0, s: 0, seg: 0, tangent: { x: 1, z: 0 } };
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    const dx = b.x - a.x, dz = b.z - a.z;
    const L2 = dx * dx + dz * dz || 1e-9;
    let t = ((p.x - a.x) * dx + (p.z - a.z) * dz) / L2;
    t = Math.max(0, Math.min(1, t));
    const x = a.x + dx * t, z = a.z + dz * t;
    const d2 = (p.x - x) ** 2 + (p.z - z) ** 2;
    if (d2 < best.d2) {
      const L = Math.sqrt(L2);
      best = { d2, x, z, s: s[i] + (s[i + 1] - s[i]) * t, seg: i, tangent: { x: dx / L, z: dz / L } };
    }
  }
  best.dist = Math.sqrt(best.d2);
  return best;
}

export function normalize2(v) {
  const l = Math.hypot(v.x, v.z) || 1;
  return { x: v.x / l, z: v.z / l };
}

/** Left-hand normal of a planar tangent (rotated +90 degrees). */
export const leftNormal = (t) => ({ x: -t.z, z: t.x });

/** Monotone-chain convex hull (CCW). Input: [{x,z}]. */
export function convexHull(points) {
  const P = points.slice().sort((a, b) => a.x - b.x || a.z - b.z);
  if (P.length < 3) return P;
  const cross = (o, a, b) => (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
  const lower = [];
  for (const p of P) {
    while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 1e-9) lower.pop();
    lower.push(p);
  }
  const upper = [];
  for (let i = P.length - 1; i >= 0; i--) {
    const p = P[i];
    while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 1e-9) upper.pop();
    upper.push(p);
  }
  upper.pop();
  lower.pop();
  return lower.concat(upper);
}
