/* RoadForge — spline sampling, polyline utilities, offsets, intersections. */
(function (RF) {
  'use strict';
  const V = RF.V;

  /** Centripetal Catmull-Rom evaluation over control points with explicit phantom end tangents. */
  function catmullRomPoint(p0, p1, p2, p3, t, alpha = 0.5) {
    const d = (a, b) => Math.pow(Math.max(V.dist(a, b), 1e-6), alpha);
    const t0 = 0, t1 = t0 + d(p0, p1), t2 = t1 + d(p1, p2), t3 = t2 + d(p2, p3);
    const tt = t1 + (t2 - t1) * t;
    const A1 = V.add(V.mul(p0, (t1 - tt) / (t1 - t0)), V.mul(p1, (tt - t0) / (t1 - t0)));
    const A2 = V.add(V.mul(p1, (t2 - tt) / (t2 - t1)), V.mul(p2, (tt - t1) / (t2 - t1)));
    const A3 = V.add(V.mul(p2, (t3 - tt) / (t3 - t2)), V.mul(p3, (tt - t2) / (t3 - t2)));
    const B1 = V.add(V.mul(A1, (t2 - tt) / (t2 - t0)), V.mul(A2, (tt - t0) / (t2 - t0)));
    const B2 = V.add(V.mul(A2, (t3 - tt) / (t3 - t1)), V.mul(A3, (tt - t1) / (t3 - t1)));
    return V.add(V.mul(B1, (t2 - tt) / (t2 - t1)), V.mul(B2, (tt - t1) / (t2 - t1)));
  }

  /**
   * Densely sample a Catmull-Rom chain. pts: control points; tanA/tanB optional unit tangents at the ends
   * (used to build phantom points so neighbouring roads join with tangent continuity).
   */
  function samplePolyline(pts, tanA, tanB, maxStep = 0.5) {
    if (pts.length < 2) return pts.map(V.clone);
    if (pts.length === 2 && !tanA && !tanB) {
      // straight line, subdivide for elevation/stations later
      return [V.clone(pts[0]), V.clone(pts[1])];
    }
    const n = pts.length;
    const first = pts[0], last = pts[n - 1];
    const lenA = V.dist(pts[0], pts[1]), lenB = V.dist(pts[n - 2], pts[n - 1]);
    const phantomA = tanA ? V.madd(first, tanA, -Math.max(lenA, 1)) : V.madd(first, V.sub(first, pts[1]), 1);
    const phantomB = tanB ? V.madd(last, tanB, Math.max(lenB, 1)) : V.madd(last, V.sub(last, pts[n - 2]), 1);
    const cp = [phantomA, ...pts, phantomB];
    const out = [V.clone(pts[0])];
    for (let i = 1; i < cp.length - 2; i++) {
      const segLen = V.dist(cp[i], cp[i + 1]);
      const steps = Math.max(2, Math.ceil(segLen / maxStep));
      for (let k = 1; k <= steps; k++) out.push(catmullRomPoint(cp[i - 1], cp[i], cp[i + 1], cp[i + 2], k / steps));
    }
    return out;
  }

  function cumulative(poly) { const s = [0]; for (let i = 1; i < poly.length; i++) s.push(s[i - 1] + V.dist(poly[i - 1], poly[i])); return s; }
  function cumulativeXZ(poly) { const s = [0]; for (let i = 1; i < poly.length; i++) s.push(s[i - 1] + V.distXZ(poly[i - 1], poly[i])); return s; }

  /** Point & tangent at arc-length station s on a polyline with cumulative lengths cum. */
  function evalAt(poly, cum, s) {
    const n = poly.length; if (n === 1) return { p: V.clone(poly[0]), d: [1, 0, 0] };
    if (s <= 0) return { p: V.clone(poly[0]), d: V.norm(V.sub(poly[1], poly[0])) };
    if (s >= cum[n - 1]) return { p: V.clone(poly[n - 1]), d: V.norm(V.sub(poly[n - 1], poly[n - 2])) };
    let lo = 0, hi = n - 1;
    while (hi - lo > 1) { const m = (lo + hi) >> 1; if (cum[m] <= s) lo = m; else hi = m; }
    const t = (s - cum[lo]) / Math.max(cum[hi] - cum[lo], 1e-9);
    return { p: V.lerp(poly[lo], poly[hi], t), d: V.norm(V.sub(poly[hi], poly[lo])), i: lo, t };
  }

  /** Curvature (1/R) in plan at index i of a polyline. */
  function curvatureAt(poly, i) {
    if (i <= 0 || i >= poly.length - 1) return 0;
    const a = poly[i - 1], b = poly[i], c = poly[i + 1];
    const ab = [b[0] - a[0], b[2] - a[2]], bc = [c[0] - b[0], c[2] - b[2]];
    const l1 = Math.hypot(ab[0], ab[1]), l2 = Math.hypot(bc[0], bc[1]); if (l1 < 1e-9 || l2 < 1e-9) return 0;
    const cross = ab[0] * bc[1] - ab[1] * bc[0];
    const dot = ab[0] * bc[0] + ab[1] * bc[1];
    const ang = Math.atan2(cross, dot);
    return ang / ((l1 + l2) * 0.5);
  }

  /**
   * Build curvature-adaptive stations along a dense polyline. Returns samples [{p,d,n,s,k}].
   * extraStations: array of station values that must appear exactly (features, trims).
   * minStep/maxStep in metres. Dense zones: [{s0,s1,step}] to refine (potholes, bumps).
   */
  function stationize(dense, opts = {}) {
    const minStep = opts.minStep ?? 0.5, maxStep = opts.maxStep ?? 3.0, tol = opts.tol ?? 0.01;
    const cum = cumulativeXZ(dense), L = cum[cum.length - 1];
    if (L < 1e-6) return { samples: [], length: 0, dense, cum };
    // curvature per dense sample
    const curv = dense.map((_, i) => Math.abs(curvatureAt(dense, i)));
    const stations = [0];
    let s = 0;
    const zones = opts.zones || [];
    while (s < L - 1e-6) {
      const e = evalAt(dense, cum, s);
      const i = e.i ?? 0;
      const k = Math.max(curv[i], curv[Math.min(i + 1, curv.length - 1)], curv[Math.max(i - 1, 0)]);
      // chord error tol: step = 2*sqrt(2*R*tol - tol^2)
      let step = k > 1e-6 ? 2 * Math.sqrt(Math.max(2 * tol / k - tol * tol, 1e-6)) : maxStep;
      step = RF.clamp(step, minStep, maxStep);
      for (const z of zones) { if (s + step > z.s0 - 1e-6 && s < z.s1 + 1e-6) step = Math.min(step, z.step); }
      // snap to required stations
      if (opts.required) for (const r of opts.required) { if (r > s + 1e-4 && r < s + step - 1e-4) step = r - s; }
      if (s + step > L - minStep * 0.4) step = L - s;
      s += step;
      stations.push(Math.min(s, L));
    }
    const samples = stations.map((st) => {
      const e = evalAt(dense, cum, st);
      const d = V.normXZ(e.d);
      return { p: e.p, d, n: V.perp(d), s: st, k: 0, slope: e.d[1] };
    });
    // smooth plan-tangent using neighbours & curvature
    for (let i = 0; i < samples.length; i++) {
      const a = samples[Math.max(0, i - 1)].p, b = samples[Math.min(samples.length - 1, i + 1)].p;
      const d = V.normXZ(V.sub(b, a));
      if (i > 0 && i < samples.length - 1) { samples[i].d = d; samples[i].n = V.perp(d); }
      samples[i].k = curvatureAt(samples.map((q) => q.p), i);
    }
    return { samples, length: L, dense, cum };
  }

  /* ---------- Segment intersection (XZ) ---------- */
  function segIntersect(a, b, c, d) {
    const r0 = b[0] - a[0], r1 = b[2] - a[2], s0 = d[0] - c[0], s1 = d[2] - c[2];
    const den = r0 * s1 - r1 * s0; if (Math.abs(den) < 1e-12) return null;
    const q0 = c[0] - a[0], q1 = c[2] - a[2];
    const t = (q0 * s1 - q1 * s0) / den, u = (q0 * r1 - q1 * r0) / den;
    if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u > 1 + 1e-9) return null;
    return { t, u, p: [a[0] + r0 * t, a[1] + (b[1] - a[1]) * t, a[2] + r1 * t] };
  }
  /** First intersection between two polylines starting from their index 0 (minimising combined index). */
  function polyIntersect(A, B, maxA = A.length, maxB = B.length) {
    let best = null;
    const na = Math.min(A.length - 1, maxA), nb = Math.min(B.length - 1, maxB);
    for (let i = 0; i < na; i++) {
      for (let j = 0; j < nb; j++) {
        const h = segIntersect(A[i], A[i + 1], B[j], B[j + 1]);
        if (h) { const score = i + h.t + j + h.u; if (!best || score < best.score) best = { i, j, t: h.t, u: h.u, p: h.p, score }; }
      }
      if (best && i > best.i + 2) break;
    }
    return best;
  }
  /** All XZ intersections between two polylines (with cumulative arrays) → [{p, sA, sB, i, j}] sorted along A. */
  function polyIntersectAll(A, cumA, B, cumB) {
    const out = [];
    const bb = (P, i) => [Math.min(P[i][0], P[i + 1][0]), Math.max(P[i][0], P[i + 1][0]), Math.min(P[i][2], P[i + 1][2]), Math.max(P[i][2], P[i + 1][2])];
    const bA = []; for (let i = 0; i < A.length - 1; i++) bA.push(bb(A, i));
    const bB = []; for (let j = 0; j < B.length - 1; j++) bB.push(bb(B, j));
    for (let i = 0; i < A.length - 1; i++) {
      const a = bA[i];
      for (let j = 0; j < B.length - 1; j++) {
        const b = bB[j]; if (a[1] < b[0] || b[1] < a[0] || a[3] < b[2] || b[3] < a[2]) continue;
        const h = segIntersect(A[i], A[i + 1], B[j], B[j + 1]);
        if (h) out.push({ p: h.p, sA: cumA[i] + (cumA[i + 1] - cumA[i]) * h.t, sB: cumB[j] + (cumB[j + 1] - cumB[j]) * h.u, i, j });
      }
    }
    // merge near-duplicate hits (shared vertices)
    out.sort((x, y) => x.sA - y.sA);
    const clean = []; for (const h of out) if (!clean.length || Math.abs(h.sA - clean[clean.length - 1].sA) > 0.5) clean.push(h);
    return clean;
  }
  /** Fractional index of a point at arc distance `dist` from fractional index f0 along polyline (XZ lengths). */
  function walk(poly, f0, dist) {
    let i = Math.floor(f0), t = f0 - i;
    if (i >= poly.length - 1) { i = poly.length - 2; t = 1; }
    let remaining = dist;
    while (true) {
      const segLen = V.distXZ(poly[i], poly[i + 1]);
      const avail = segLen * (1 - t);
      if (remaining <= avail || i >= poly.length - 2) { const tt = segLen > 1e-9 ? t + remaining / segLen : 1; return i + Math.min(tt, 1); }
      remaining -= avail; i++; t = 0;
    }
  }
  function atIndex(poly, f) { const i = Math.min(Math.floor(f), poly.length - 2), t = f - i; return V.lerp(poly[i], poly[i + 1], t); }
  function dirAtIndex(poly, f) { const i = Math.min(Math.max(Math.floor(f), 0), poly.length - 2); return V.normXZ(V.sub(poly[i + 1], poly[i])); }
  function interpArr(arr, f) { const i = Math.min(Math.floor(f), arr.length - 2), t = f - i; return arr[i] + (arr[i + 1] - arr[i]) * t; }

  /** Offset a polyline laterally (XZ) by signed distance with mitered joins (limited). */
  function offsetPolyline(poly, off, limit = 3) {
    const n = poly.length, out = new Array(n);
    for (let i = 0; i < n; i++) {
      const a = poly[Math.max(0, i - 1)], b = poly[Math.min(n - 1, i + 1)];
      let d = V.normXZ(V.sub(b, a));
      let nrm = V.perp(d);
      // miter scale
      let scale = 1;
      if (i > 0 && i < n - 1) {
        const d0 = V.normXZ(V.sub(poly[i], poly[i - 1])), d1 = V.normXZ(V.sub(poly[i + 1], poly[i]));
        const cosHalf = Math.sqrt(Math.max(0, (1 + V.dot(d0, d1)) / 2));
        scale = Math.min(limit, 1 / Math.max(cosHalf, 1e-3));
      }
      out[i] = V.madd(poly[i], nrm, off * scale);
    }
    return out;
  }
  /** Resample a polyline into exactly `segments` equal-length (XZ) segments. Keeps endpoints exactly. */
  function resampleEqual(poly, segments) {
    if (poly.length === 1) return Array.from({ length: segments + 1 }, () => V.clone(poly[0]));
    const cum = cumulativeXZ(poly), L = cum[cum.length - 1];
    const out = [];
    for (let k = 0; k <= segments; k++) {
      if (L < 1e-9) { out.push(V.lerp(poly[0], poly[poly.length - 1], k / segments)); continue; }
      out.push(evalAt(poly, cum, (L * k) / segments).p);
    }
    out[0] = V.clone(poly[0]); out[segments] = V.clone(poly[poly.length - 1]);
    return out;
  }
  function arcPoints(center, r, a0, a1, segments, y0, y1) {
    const out = [];
    for (let k = 0; k <= segments; k++) { const t = k / segments, a = a0 + (a1 - a0) * t; out.push([center[0] + Math.cos(a) * r, y0 + (y1 - y0) * t, center[2] + Math.sin(a) * r]); }
    return out;
  }
  function polygonArea(poly) { let a = 0; for (let i = 0; i < poly.length; i++) { const p = poly[i], q = poly[(i + 1) % poly.length]; a += p[0] * q[2] - q[0] * p[2]; } return a / 2; }
  function pointInPoly(pt, poly) { let c = false; for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) { const pi = poly[i], pj = poly[j]; if ((pi[2] > pt[2]) !== (pj[2] > pt[2]) && pt[0] < ((pj[0] - pi[0]) * (pt[2] - pi[2])) / (pj[2] - pi[2]) + pi[0]) c = !c; } return c; }
  /** Closest point on polyline (XZ). Returns {dist, s, p, i, t} */
  function closestOnPolyline(poly, cum, pt) {
    let best = { dist: Infinity };
    for (let i = 0; i < poly.length - 1; i++) {
      const a = poly[i], b = poly[i + 1];
      const abx = b[0] - a[0], abz = b[2] - a[2], l2 = abx * abx + abz * abz || 1e-12;
      let t = ((pt[0] - a[0]) * abx + (pt[2] - a[2]) * abz) / l2; t = RF.clamp(t, 0, 1);
      const px = a[0] + abx * t, pz = a[2] + abz * t, d = Math.hypot(pt[0] - px, pt[2] - pz);
      if (d < best.dist) best = { dist: d, s: cum[i] + (cum[i + 1] - cum[i]) * t, p: [px, a[1] + (b[1] - a[1]) * t, pz], i, t };
    }
    return best;
  }

  RF.spline = { samplePolyline, cumulative, cumulativeXZ, evalAt, stationize, segIntersect, polyIntersect, polyIntersectAll, walk, atIndex, dirAtIndex, interpArr, offsetPolyline, resampleEqual, arcPoints, polygonArea, pointInPoly, closestOnPolyline, curvatureAt, catmullRomPoint };
})(globalThis.RF || (globalThis.RF = {}));
