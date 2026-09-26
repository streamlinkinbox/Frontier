// Potholes: real dents in the road surface (not a texture).
//
// Each pothole lives in tunnel coordinates (s along the spline, n lateral) and
// displaces the road vertices downward with an irregular bowl + a slightly
// raised, broken lip. The tunnel builder clusters extra edge loops around
// every pothole (adaptive ring spacing), so even small holes get a proper
// dent while the surface stays one continuous all-quad mesh. The collision
// mesh contains the dents too -> the car's suspension feels them.
import { mulberry32 } from './noise.js';

// potholes for one tunnel. layout(s) -> {l, r, hwL, hwR}; prof gives bed geometry
export function generatePotholes(edgeId, seed, s0, s1, layout, prof, density) {
  const rnd = mulberry32(seed * 977 + edgeId * 7919 + 3);
  const len = s1 - s0;
  const want = Math.round((len / 100) * density * (0.6 + rnd() * 0.8));
  const out = [];
  const inTaper = (s) => layout.tapers.some((t) => Math.abs(s - t.c) < t.L / 2 + 2);
  for (let tries = 0; out.length < want && tries < want * 12; tries++) {
    const s = s0 + 7.5 + rnd() * (len - 15); // clear of the flat mouth plates (groove weight ramps over 6 m)
    if (s <= s0 + 7.5 || s >= s1 - 7.5 || inTaper(s)) continue;
    const L = layout(s);
    // candidate zones (lateral): outer road strips of either side + the centre strip
    const zones = [];
    const edgeGap = 0.35; // keep clear of the curb
    const addZone = (lo, hi, maxR) => { if (hi - lo > 0.3) zones.push({ lo, hi, maxR }); };
    const outerL = [prof.E + 0.12, L.hwL - edgeGap], outerR = [-(L.hwR - edgeGap), -(prof.E + 0.12)];
    const bigL = L.l > 1.5, bigR = L.r > 1.5; // a full outer lane -> room for big holes
    addZone(outerL[0], outerL[1], bigL ? 1.25 : 0.55);
    addZone(outerR[0], outerR[1], bigR ? 1.25 : 0.55);
    addZone(-prof.I + 0.08, prof.I - 0.08, 0.55);
    const z = zones[Math.floor(rnd() * zones.length)];
    const R = Math.min(z.maxR, (z.hi - z.lo) / 2) * (0.55 + rnd() * 0.45);
    if (R < 0.28) continue;
    const n = z.lo + R * 0.85 + rnd() * Math.max(0, z.hi - z.lo - 1.7 * R);
    // no overlaps
    if (out.some((p) => Math.hypot(p.s - s, p.n - n) < p.R + R + 0.6)) continue;
    out.push({
      s, n, R,
      depth: 0.05 + R * 0.07 + rnd() * 0.04,          // 7 - 18 cm
      ph: [rnd() * 6.283, rnd() * 6.283, rnd() * 6.283],
      stretch: 1 + rnd() * 0.5,                       // potholes are usually longer along the wheel paths
    });
  }
  return out;
}

// displacement (negative = down) and wear (0..1) at (s, n)
export function dentAt(holes, s, n) {
  let dz = 0, wear = 0;
  for (const h of holes) {
    const ds = (s - h.s) / h.stretch, dn = n - h.n;
    const d2 = ds * ds + dn * dn;
    const lim = h.R * 1.35;
    if (d2 > lim * lim) continue;
    const th = Math.atan2(dn, ds);
    // irregular outline
    const Rt = h.R * (1 + 0.2 * Math.sin(3 * th + h.ph[0]) + 0.12 * Math.sin(5 * th + h.ph[1]) + 0.06 * Math.sin(9 * th + h.ph[2]));
    const x = Math.sqrt(d2) / Rt;
    if (x < 1) {
      const bowl = (1 - x * x);
      const rough = 1 + 0.18 * Math.sin(ds * 9 + h.ph[1]) * Math.sin(dn * 11 + h.ph[2]); // broken, uneven bottom
      dz = Math.min(dz, -h.depth * Math.pow(bowl, 0.7) * rough);
      wear = Math.max(wear, Math.min(1, 1.25 - x * 0.6));
    } else {
      // pushed-up, crumbled lip just outside the edge
      const lip = h.depth * 0.14 * Math.exp(-(((x - 1.08) / 0.12) ** 2)) * (0.6 + 0.4 * Math.sin(th * 7 + h.ph[0]));
      if (dz >= 0) dz = Math.max(dz, lip);
      wear = Math.max(wear, Math.max(0, 1 - (x - 1) * 3) * 0.45);
    }
  }
  return { dz, wear };
}

// extra ring positions (s) clustered around the potholes
export function denseIntervals(holes) {
  return holes.map((h) => {
    const half = h.R * h.stretch * 1.4;
    return { a: h.s - half, b: h.s + half, step: Math.max(0.12, Math.min(0.3, (h.R * h.stretch) / 3.2)) };
  });
}

// adaptive edge-loop positions between s0 and s1
export function ringPositions(s0, s1, base, dense) {
  const out = [s0];
  let s = s0;
  const iv = dense.slice().sort((x, y) => x.a - y.a);
  while (s < s1 - 1e-6) {
    let h = base;
    for (const d of iv) if (s >= d.a - 1e-6 && s < d.b) h = Math.min(h, d.step);
    let next = s + h;
    // don't jump over the start of a dense interval
    for (const d of iv) if (d.a > s + 1e-6 && d.a < next) next = d.a;
    if (next > s1 - h * 0.35) next = s1;
    out.push(next);
    s = next;
  }
  return out;
}

