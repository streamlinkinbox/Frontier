// Quad-based mesh primitives: lathe (revolve), sweep (extrude a 2D section along a path), lofts, boxes.
import { add, sub, mul, madd, norm, cross, dot, lerp } from "./math.js";

/** Revolve a profile [{r, y}] around the Y axis at `origin`. Quads everywhere; apex rows (r=0) become a fan. */
export function lathe(mb, mat, profile, segments = 16, origin = [0, 0, 0], opts = {}) {
  const rows = [];
  const a0 = opts.angle0 || 0, a1 = opts.angle1 ?? Math.PI * 2;
  const closed = (a1 - a0) >= Math.PI * 2 - 1e-6;
  for (const p of profile) {
    const row = [];
    const n = closed ? segments : segments + 1;
    for (let i = 0; i < n; i++) {
      const a = a0 + ((a1 - a0) * i) / segments;
      const r = Math.max(p.r, 1e-4);
      row.push(mb.vertex([origin[0] + Math.cos(a) * r, origin[1] + p.y, origin[2] + Math.sin(a) * r], [a * r, p.y], opts.color || [0, 1, 0.5, 0]));
    }
    if (closed) row.push(row[0]);
    rows.push(row);
  }
  // profile ordered bottom→top with angle increasing gives outward normals by construction
  if (opts.facing) mb.gridFacing(mat, rows, opts.owner, opts.facing); else mb.grid(mat, rows, opts.owner, false);
  return rows;
}
/** Parallel-transport frames along a polyline (array of points). Returns [{p, t, n, b}]. */
export function frames(path, up = [0, 1, 0]) {
  const out = [];
  let prevN = null;
  for (let i = 0; i < path.length; i++) {
    const t = norm(sub(path[Math.min(i + 1, path.length - 1)], path[Math.max(i - 1, 0)]));
    let n;
    if (!prevN) { n = norm(cross(up, t)); if (Math.hypot(...n) < 1e-6) n = norm(cross([1, 0, 0], t)); }
    else { n = norm(sub(prevN, mul(t, dot(prevN, t)))); }
    const b = cross(t, n);
    out.push({ p: path[i], t, n, b });
    prevN = n;
  }
  return out;
}
/** Sweep a closed/open 2D section [{x, y}] (x along frame normal, y along binormal) along frames. */
export function sweep(mb, mat, frs, section, opts = {}) {
  const rows = [];
  const closed = opts.closed !== false;
  let s = 0;
  for (let i = 0; i < frs.length; i++) {
    const f = frs[i];
    if (i > 0) s += Math.hypot(...sub(f.p, frs[i - 1].p));
    const row = section.map((q) => mb.vertex(add(add(f.p, mul(f.n, q.x)), mul(f.b, q.y)), [s, q.u ?? 0], opts.color || [0, 1, 0.5, 0]));
    if (closed) row.push(row[0]);
    rows.push(row);
  }
  const centre = frs.map((f) => f.p);
  mb.gridFacing(mat, rows, opts.owner, opts.facing || ((c) => { let best = centre[0], bd = Infinity; for (const p of centre) { const d = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2; if (d < bd) { bd = d; best = p; } } return sub(c, best); }));
  if (opts.caps && closed) { cap(mb, mat, rows[0].slice(0, -1), sub(frs[0].p, frs[1].p), opts.owner); cap(mb, mat, rows[rows.length - 1].slice(0, -1), sub(frs[frs.length - 1].p, frs[frs.length - 2].p), opts.owner); }
  return rows;
}
/** Cap a ring with a centre vertex fan, oriented so the cap normal points along `want`. */
export function cap(mb, mat, ring, want, owner) {
  const pts = ring.map((i) => mb.position(i));
  const c = pts.reduce((a, p) => add(a, mul(p, 1 / pts.length)), [0, 0, 0]);
  const ci = mb.vertex(c, [0, 0], [0, 1, 0.5, 0]);
  // Newell normal of the ring polygon
  let nx = 0, ny = 0, nz = 0;
  for (let i = 0; i < pts.length; i++) { const a = pts[i], b = pts[(i + 1) % pts.length]; nx += (a[1] - b[1]) * (a[2] + b[2]); ny += (a[2] - b[2]) * (a[0] + b[0]); nz += (a[0] - b[0]) * (a[1] + b[1]); }
  const w = Array.isArray(want) ? want : [0, 1, 0];
  const reverse = nx * w[0] + ny * w[1] + nz * w[2] < 0;
  for (let i = 0; i < ring.length; i++) { const a = ring[i], b = ring[(i + 1) % ring.length]; if (reverse) mb.tri(mat, ci, b, a, owner); else mb.tri(mat, ci, a, b, owner); }
}
/** Loft a list of rings (arrays of 3D points, same length) into quads; optional caps. */
export function loft(mb, mat, rings, opts = {}) {
  const rows = rings.map((ring, i) => { const row = ring.map((p, j) => mb.vertex(p, [i, j], opts.color || [0, 1, 0.5, 0])); if (opts.closed !== false) row.push(row[0]); return row; });
  const centroids = rings.map((ring) => ring.reduce((a, p) => add(a, mul(p, 1 / ring.length)), [0, 0, 0]));
  const all = centroids.reduce((a, p) => add(a, mul(p, 1 / centroids.length)), [0, 0, 0]);
  mb.gridFacing(mat, rows, opts.owner, opts.facing || ((c) => { let best = centroids[0], bd = Infinity; for (const p of centroids) { const d = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2; if (d < bd) { bd = d; best = p; } } const v = sub(c, best); return Math.hypot(...v) < 1e-6 ? sub(c, all) : v; }));
  if (opts.caps) { cap(mb, mat, rows[0].slice(0, -1), sub(centroids[0], centroids[1]), opts.owner); cap(mb, mat, rows[rows.length - 1].slice(0, -1), sub(centroids[centroids.length - 1], centroids[centroids.length - 2]), opts.owner); }
  return rows;
}
/** Axis-aligned box (optionally chamfered) centred at c with size [sx, sy, sz]. */
export function box(mb, mat, c, size, opts = {}) {
  const [sx, sy, sz] = size, h = [sx / 2, sy / 2, sz / 2];
  const bev = Math.min(opts.bevel || 0, h[0], h[1], h[2]);
  const col = opts.color || [0, 1, 0.5, 0];
  if (bev <= 0) {
    const P = (x, y, z) => mb.vertex([c[0] + x * h[0], c[1] + y * h[1], c[2] + z * h[2]], [x, z], col);
    const faces = [
      [[-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]], [[1, -1, -1], [-1, -1, -1], [-1, 1, -1], [1, 1, -1]],
      [[1, -1, 1], [1, -1, -1], [1, 1, -1], [1, 1, 1]], [[-1, -1, -1], [-1, -1, 1], [-1, 1, 1], [-1, 1, -1]],
      [[-1, 1, 1], [1, 1, 1], [1, 1, -1], [-1, 1, -1]], [[-1, -1, -1], [1, -1, -1], [1, -1, 1], [-1, -1, 1]]];
    for (const f of faces) { const ids = f.map((v) => P(...v)); mb.quad(mat, ids[0], ids[1], ids[2], ids[3], opts.owner); }
    return;
  }
  // chamfered box as a loft of 4 rounded rings (bottom bevel, bottom, top, top bevel) — all quads + caps
  const ringAt = (y, inset) => { const pts = []; const k = 4; const hx = h[0] - bev, hz = h[2] - bev;
    for (const [cx, cz, a0] of [[hx, hz, 0], [-hx, hz, Math.PI / 2], [-hx, -hz, Math.PI], [hx, -hz, Math.PI * 1.5]])
      for (let i = 0; i <= k; i++) { const a = a0 + (Math.PI / 2) * (i / k); pts.push([c[0] + cx + Math.cos(a) * (bev - inset), c[1] + y, c[2] + cz + Math.sin(a) * (bev - inset)]); }
    return pts; };
  const rings = [ringAt(-h[1], bev * 0.999), ringAt(-h[1] + bev, 0), ringAt(h[1] - bev, 0), ringAt(h[1], bev * 0.999)];
  loft(mb, mat, rings, { caps: true, owner: opts.owner, color: col });
}
/** Vertical cylinder/cone between y0..y1 with radii r0,r1 at [x,z]. */
export function cylinder(mb, mat, x, z, y0, y1, r0, r1 = r0, seg = 12, opts = {}) {
  const prof = [];
  if (opts.capBottom) prof.push({ r: 0, y: y0 });
  prof.push({ r: r0, y: y0 }, { r: r1, y: y1 });
  if (opts.capTop) prof.push({ r: 0, y: y1 });
  return lathe(mb, mat, prof, seg, [x, 0, z], opts);
}
/** Circle ring of points in the XZ plane. */
export const circle = (c, r, n, y = 0) => Array.from({ length: n }, (_, i) => { const a = (i / n) * Math.PI * 2; return [c[0] + Math.cos(a) * r, y + (c[1] || 0), c[2] + Math.sin(a) * r]; });
/** Rounded rectangle ring in the local (x,y) plane, returned as 2D section points {x,y}. */
export function roundedRect(w, h, r, k = 3) {
  const pts = []; const hx = w / 2 - r, hy = h / 2 - r;
  for (const [cx, cy, a0] of [[hx, hy, 0], [-hx, hy, Math.PI / 2], [-hx, -hy, Math.PI], [hx, -hy, Math.PI * 1.5]])
    for (let i = 0; i <= k; i++) { const a = a0 + (Math.PI / 2) * (i / k); pts.push({ x: cx + Math.cos(a) * r, y: cy + Math.sin(a) * r }); }
  return pts;
}
export function transformPoints(pts, m) { return pts.map((p) => [m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12], m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13], m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]]); }
