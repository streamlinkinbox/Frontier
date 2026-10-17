// =====================================================================================
//  FRAGMENTED TOWN — route module (pure, tested by route.test.mjs)
//  The path is ONE designed centreline per district, sampled densely. The renderer
//  draws exactly these segments; the car is located against exactly these segments.
//  There is no random slab placement anywhere near the drivable corridor.
// =====================================================================================

// District I is deliberately ≈500 m: at a moving average of ~16 m/s that is 30+ s
// from the start stones to Portal 1, as briefed — checkpoint to checkpoint, no laps.
export const DISTRICTS = [
  { name: "District I — The Fragmented Town", width: 8.5, kind: "town",
    points: [[0,10,0],[45,10,0],[80,12,-18],[115,15,-48],[155,18,-58],[195,21,-35],
             [225,25,-5],[250,28,18],[290,30,38],[330,33,22],[365,36,-5],[405,38,-18],[450,40,0],[492,42,10]] },
  { name: "District II — The Roofline", width: 7.5, kind: "roofs",
    points: [[800,48,0],[845,45,18],[885,42,8],[925,38,30],[965,35,18],[1005,31,36],[1045,28,20],[1088,25,34]] },
  { name: "District III — The Shattered Causeway", width: 8.0, kind: "causeway",
    points: [[1400,30,0],[1460,31,-14],[1520,33,6],[1580,34,-10],[1640,36,8],[1702,37,0]] },
];

// Gentle, shared drift. Neighbouring pieces get near-identical phase because the
// offset is a smooth function of route distance s (not a per-piece random value),
// so the path breathes as one piece of wreckage and never tears open.
export function motionOffset(s, t) {
  return 0.20 * Math.sin(t * 0.22 + s * 0.011) + 0.07 * Math.sin(t * 0.37 + s * 0.023);
}

// Catmull-Rom sampling of a district centreline into ~2.5 m samples with heading.
export function sampleDistrict(district) {
  const P = district.points, samples = [];
  const cr = (p0, p1, p2, p3, f) => {
    const f2 = f * f, f3 = f2 * f;
    return p1.map((v, i) => 0.5 * ((2 * v) + (-p0[i] + p2[i]) * f +
      (2 * p0[i] - 5 * v + 4 * p2[i] - p3[i]) * f2 + (-p0[i] + 3 * v - 3 * p2[i] + p3[i]) * f3));
  };
  for (let i = 0; i + 1 < P.length; i++) {
    const p0 = P[Math.max(0, i - 1)], p1 = P[i], p2 = P[i + 1], p3 = P[Math.min(P.length - 1, i + 2)];
    const span = Math.hypot(p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]);
    const n = Math.max(4, Math.round(span / 2.5));
    for (let j = (i === 0 ? 0 : 1); j <= n; j++) {
      if (i + 1 < P.length - 1 || j < n) samples.push(cr(p0, p1, p2, p3, j / n));
    }
  }
  samples.push([...P[P.length - 1]]);
  let s = 0;
  const out = samples.map((p, i) => {
    if (i > 0) s += Math.hypot(p[0] - samples[i - 1][0], p[2] - samples[i - 1][2]);
    const q = samples[Math.min(samples.length - 1, i + 1)], r = samples[Math.max(0, i - 1)];
    return { p, s, yaw: Math.atan2(q[2] - r[2], q[0] - r[0]) };
  });
  return { samples: out, length: s, width: district.width, name: district.name, kind: district.kind };
}

// Locate a world position against the sampled path, searching a window around the
// previous sample index so the car can never "find" a shortcut segment far away.
// Returns route distance, lateral offset (signed), deck height (incl. drift) and heading.
export function locate(samples, x, z, hint = 0, t = 0) {
  let best = null;
  const lo = Math.max(0, hint - 6), hi = Math.min(samples.length - 2, hint + 30);
  for (let i = lo; i <= hi; i++) {
    const a = samples[i], b = samples[i + 1];
    const ux = b.p[0] - a.p[0], uz = b.p[2] - a.p[2];
    const len2 = ux * ux + uz * uz || 1, f = Math.max(0, Math.min(1, ((x - a.p[0]) * ux + (z - a.p[2]) * uz) / len2));
    const px = a.p[0] + ux * f, pz = a.p[2] + uz * f;
    const d2 = (x - px) ** 2 + (z - pz) ** 2;
    if (!best || d2 < best.d2) {
      const inv = 1 / Math.sqrt(len2), nx = -uz * inv, nz = ux * inv; // left normal
      const sHere = a.s + Math.sqrt(len2) * f;
      best = { index: i, s: sHere, lateral: (x - px) * nx + (z - pz) * nz, d2, px, pz, nx, nz,
               height: a.p[1] + (b.p[1] - a.p[1]) * f + motionOffset(sHere, t),
               yaw: Math.atan2(uz, ux) };
    }
  }
  return best;
}
