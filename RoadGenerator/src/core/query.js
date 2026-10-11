// Queries against a built plan: surface points at (road, station, lateral), lane layouts, kerb lines.
import { lerp, madd, mul, add, sub, norm, cross, rightXZ } from "./math.js";

/** Bank (superelevation slope) at station s, interpolated from the sweep stations. */
export function bankAt(R, s) {
  const st = R.stations, b = R.bank;
  if (!st || !b || st.length < 2) return 0;
  if (s <= st[0]) return b[0];
  if (s >= st[st.length - 1]) return b[b.length - 1];
  let lo = 0, hi = st.length - 1;
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (st[m] <= s) lo = m; else hi = m; }
  const t = (s - st[lo]) / Math.max(1e-9, st[hi] - st[lo]);
  return lerp(b[lo], b[hi], t);
}
/** Carriageway height offset (relative to the centreline datum) at lateral x, from the section columns. */
export function sectionY(section, x) {
  const c = section.cols;
  if (x <= c[0].x) return c[0].y;
  if (x >= c[c.length - 1].x) return c[c.length - 1].y;
  for (let i = 0; i + 1 < c.length; i++) if (x >= c[i].x && x <= c[i + 1].x) { const t = (x - c[i].x) / Math.max(1e-9, c[i + 1].x - c[i].x); return lerp(c[i].y, c[i + 1].y, t); }
  return 0;
}
/** Kerb/footway kit height at lateral distance d beyond the kerb line (side: -1 left, +1 right). */
export function kitY(kit, d) {
  let last = 0;
  for (const strip of kit.strips) { if (strip.kind === "skirt") break; for (const v of strip.verts) { if (d <= v.d) return v.h; last = v.h; } }
  return last;
}
/** World point on/above the road surface. x: lateral (right positive). lift: metres above the surface. */
export function roadPoint(R, s, x, lift = 0) {
  const F = R.frameAt(s);
  const sec = R.section, bank = bankAt(R, s);
  let base, extra = 0;
  if (x < -sec.widthL) { base = sectionY(sec, -sec.widthL) - bank * sec.widthL; extra = kitY(R.kitL, -sec.widthL - x); }
  else if (x > sec.widthR) { base = sectionY(sec, sec.widthR) + bank * sec.widthR; extra = kitY(R.kitR, x - sec.widthR); }
  else base = sectionY(sec, x) + bank * x;
  const w = regradeWeight(R, s);
  if (w > 0) base = lerp(base, regradeTarget(R, s) - F.p[1], w);
  return [F.p[0] + F.r[0] * x, F.p[1] + base + extra + lift, F.p[2] + F.r[2] * x];
}
/**
 * Regrades: zones where the carriageway is flattened to a fixed level (level crossings: the road is regraded to the
 * rail plane). R.regrades = [{ s0, s1, y, T }] → weight 1 inside [s0, s1], smoothstep to 0 over T outside.
 */
export function regradeWeight(R, s) {
  let w = 0;
  for (const z of R.regrades || []) {
    const d = s < z.s0 ? z.s0 - s : s > z.s1 ? s - z.s1 : 0;
    if (d >= z.T) continue;
    const t = 1 - d / z.T; w = Math.max(w, t * t * (3 - 2 * t));
  }
  return w;
}
export function regradeTarget(R, s) {
  let best = null, bw = -1;
  for (const z of R.regrades || []) { const d = s < z.s0 ? z.s0 - s : s > z.s1 ? s - z.s1 : 0; const w = Math.max(0, 1 - d / z.T); if (w > bw) { bw = w; best = z; } }
  return best ? best.y : 0;
}
/** Full frame: point, forward (dxz), right, up-ish normal. */
export function roadFrame(R, s, x, lift = 0) {
  const F = R.frameAt(s);
  const p = roadPoint(R, s, x, lift);
  const d = F.d, r = F.r;
  const n = norm(cross(r, d));
  return { p, d, r, n: n[1] < 0 ? mul(n, -1) : n, dxz: F.dxz };
}
/** Lane records in road coordinates: { x0, x1, xc, dir, kind, index } across the whole section. */
export function lanes(R) {
  const sec = R.section, out = [];
  let x = sec.halfMedian;
  sec.layoutR.forEach((e, i) => { out.push({ x0: x, x1: x + e.width, xc: x + e.width / 2, dir: e.dir || 0, kind: e.kind, side: 1, index: i, width: e.width }); x += e.width; });
  x = -sec.halfMedian;
  sec.layoutL.forEach((e, i) => { out.push({ x0: -(x + e.width), x1: -x, xc: -(x + e.width / 2), dir: e.dir || 0, kind: e.kind, side: -1, index: i, width: e.width }); x += e.width; });
  return out;
}
export const trafficLanes = (R) => lanes(R).filter((l) => l.kind === "lane");
/** Outer edge of the footway kit on each side (lateral x), i.e. the road's total footprint. */
export function footprint(R) { return { left: -(R.section.widthL + R.kitL.width), right: R.section.widthR + R.kitR.width }; }
/** The usable (swept) station range of a road. */
export function stationRange(R) { return { s0: R.cutA, s1: R.length - R.cutB }; }
/** Which side of the road approaches node `nodeId` (lanes travelling towards that node). */
export function approachSide(R, nodeId) { return R.road.a === nodeId ? -1 : 1; }
