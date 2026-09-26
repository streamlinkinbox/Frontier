// Tunnel cross-section. One closed edge ring:
//
//   index 0 ............ a   : floor (curb, outer road, track bed, centre, bed, outer road, curb), left -> right
//   a+1 ....... a+w          : right wall going up (a+w = right spring point)
//   a+w+1 ... a+w+r-1        : roof arch interior
//   a+w+r                    : left spring point
//   a+w+r+1 ... a+2w+r-1     : left wall going down (wraps to index 0)
//
// VARIABLE WIDTH, FIXED TOPOLOGY: the ring always has the same vertex count
// and the same structure; only the positions depend on the half widths of
// the left / right side (1 lane = 4.0 m, 2 lanes = 7.5 m). The outer road
// strips stretch, the track beds and the centre strip are fixed (the cart
// tracks run in the inner lanes). So lane changes are smooth deformations of
// ONE continuous quad surface, and every junction arm has the same counts.
//
// The floor carries the recessed track beds, so the cart track channels are
// part of the same surface as the road and the cave (rails + sleepers are
// laid into them as separate swept / instanced geometry).
// "n" = lateral offset (+ = left of travel direction), "b" = height.
import { sideHalfWidth } from './lanes.js';

export const MAT = { ROCK: 0, ROAD: 1, GROOVE: 2, RAIL: 3, CURB: 4, PLATE: 5 };

export function buildProfile(P) {
  const D = P.grooveDepth;
  const bedHalf = P.gauge / 2 + 0.42;
  const c = P.laneOffset;
  const E = c + bedHalf + 0.05;            // outer edge of a track bed
  const I = c - bedHalf - 0.05;            // inner edge of a track bed
  const hwMax = sideHalfWidth(2);
  const outerSegs = Math.max(4, Math.ceil((hwMax - 0.3 - E) / 0.45)); // sized for the widest road
  let centerSegs = Math.max(2, Math.ceil((2 * I) / 0.42));
  if (centerSegs % 2) centerSegs++;        // symmetric -> total floor segment count is even

  // one track bed, left side, from outer edge to inner edge (n relative to lane centre c)
  const bed = [];
  bed.push({ d: bedHalf + 0.05, b: 0, m: MAT.ROAD });
  bed.push({ d: bedHalf, b: -D * 0.35, m: MAT.CURB });
  bed.push({ d: bedHalf - 0.05, b: -D, m: MAT.GROOVE });
  const kb = 6;
  for (let i = 1; i < kb; i++) bed.push({ d: bedHalf - 0.05 - (2 * (bedHalf - 0.05) * i) / kb, b: -D - 0.012 * Math.sin((i / kb) * Math.PI), m: MAT.GROOVE });
  bed.push({ d: -(bedHalf - 0.05), b: -D, m: MAT.GROOVE });
  bed.push({ d: -bedHalf, b: -D * 0.35, m: MAT.CURB });
  bed.push({ d: -(bedHalf + 0.05), b: 0, m: MAT.ROAD });

  // floor for given side half widths (left = +n, right = -n)
  const floorAt = (hwL, hwR) => {
    const f = [];
    // left curb
    f.push({ n: hwL, b: 0.12, m: MAT.CURB }, { n: hwL - 0.22, b: 0.12, m: MAT.CURB });
    // left outer road: hwL-0.3 -> E (outerSegs segments), road strip stretches with the width
    for (let i = 0; i < outerSegs; i++) f.push({ n: hwL - 0.3 + (E - (hwL - 0.3)) * (i / outerSegs), b: 0, m: MAT.ROAD, outer: 1 });
    // left bed (first point = E)
    for (const q of bed) f.push({ n: c + q.d, b: q.b, m: q.m });
    // centre strip interior
    for (let i = 1; i < centerSegs; i++) f.push({ n: I - (2 * I * i) / centerSegs, b: 0, m: MAT.ROAD });
    // right bed (mirror)
    for (let k = bed.length - 1; k >= 0; k--) f.push({ n: -(c + bed[k].d), b: bed[k].b, m: bed[k].m });
    // right outer road: -E -> -(hwR-0.3)
    for (let i = 1; i <= outerSegs; i++) f.push({ n: -E + (-(hwR - 0.3) + E) * (i / outerSegs), b: 0, m: MAT.ROAD, outer: 1 });
    f.push({ n: -(hwR - 0.22), b: 0.12, m: MAT.CURB }, { n: -hwR, b: 0.12, m: MAT.CURB });
    return f;
  };
  const probe = floorAt(4, 4);
  const a = probe.length - 1;
  if (a % 2) throw new Error('floor segment count must be even');

  const w = P.wallSegs, r = P.roofSegs;
  const bulge = P.wallBulge, sh = P.springHeight;

  // full ring for given half widths; the arch spans the (possibly asymmetric) width
  const ringAt = (hwL, hwR) => {
    const ring = floorAt(hwL, hwR).map((q) => ({ ...q, rock: 0 }));
    for (let j = 1; j <= w; j++) {
      const t = j / w;
      ring.push({ n: -(hwR + bulge * Math.sin(t * Math.PI / 2)), b: 0.12 + (sh - 0.12) * t, m: MAT.ROCK, rock: smooth(t) });
    }
    const R0 = (hwL + hwR) / 2 + bulge, cn = (hwL - hwR) / 2;
    const rh = P.roofHeight + 0.22 * (R0 - (4 + bulge)); // wider span -> slightly higher crown
    for (let j = 1; j <= r; j++) {
      const th = (j / r) * Math.PI;
      ring.push({ n: cn - R0 * Math.cos(th), b: sh + (rh - sh) * Math.sin(th), m: MAT.ROCK, rock: 1 });
    }
    for (let j = 1; j < w; j++) {
      const t = (w - j) / w;
      ring.push({ n: hwL + bulge * Math.sin(t * Math.PI / 2), b: 0.12 + (sh - 0.12) * t, m: MAT.ROCK, rock: smooth(t) });
    }
    return ring;
  };
  // flat, evenly spaced floor used at the tunnel mouths (beds fade into the junction plate)
  const uniformAt = (hwL, hwR) => { const o = []; for (let i = 0; i <= a; i++) o.push({ n: hwL - ((hwL + hwR) * i) / a, b: 0 }); return o; };
  // arc length along the ring (UVs)
  const arcOf = (ring) => {
    const arc = [0];
    for (let i = 1; i <= ring.length; i++) {
      const p0 = ring[i - 1], p1 = ring[i % ring.length];
      arc.push(arc[i - 1] + Math.hypot(p1.n - p0.n, p1.b - p0.b));
    }
    return arc;
  };
  const N = ringAt(4, 4).length;
  return { a, w, r, N, ringAt, uniformAt, arcOf, bedHalf, E, I };
}

function smooth(t) { return t * t * (3 - 2 * t); }
