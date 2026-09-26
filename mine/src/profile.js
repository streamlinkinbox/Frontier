// Tunnel cross-section. One closed edge ring:
//
//   index 0 ............ a   : floor (road + grooves + rails), arm-left -> arm-right
//   a+1 ....... a+w          : right wall going up (a+w = right spring point)
//   a+w+1 ... a+w+r-1        : roof arch interior
//   a+w+r                    : left spring point
//   a+w+r+1 ... a+2w+r-1     : left wall going down (wraps to index 0)
//
// The floor carries the grooves + rail heads, so the cart TRACKS are part
// of the same continuous surface as the road and the cave.
// "n" = lateral offset (+ = left of travel direction), "b" = height.

export const MAT = { ROCK: 0, ROAD: 1, GROOVE: 2, RAIL: 3, CURB: 4, PLATE: 5 };

export function buildProfile(P) {
  const hw = P.roadHalfWidth;
  const D = P.grooveDepth;
  const key = [];
  // curb, left side
  key.push({ n: hw, b: 0.12, m: MAT.CURB });
  key.push({ n: hw - 0.22, b: 0.12, m: MAT.CURB });
  key.push({ n: hw - 0.3, b: 0.0, m: MAT.ROAD });
  const rails = [P.laneOffset + P.gauge / 2, P.laneOffset - P.gauge / 2, -(P.laneOffset - P.gauge / 2), -(P.laneOffset + P.gauge / 2)];
  for (const r of rails) {
    key.push({ n: r + 0.13, b: 0, m: MAT.ROAD });
    key.push({ n: r + 0.09, b: -D, m: MAT.GROOVE });
    key.push({ n: r + 0.035, b: -D, m: MAT.GROOVE });
    key.push({ n: r + 0.035, b: -0.025, m: MAT.RAIL });
    key.push({ n: r - 0.035, b: -0.025, m: MAT.RAIL });
    key.push({ n: r - 0.035, b: -D, m: MAT.GROOVE });
    key.push({ n: r - 0.09, b: -D, m: MAT.GROOVE });
    key.push({ n: r - 0.13, b: 0, m: MAT.ROAD });
  }
  key.push({ n: -(hw - 0.3), b: 0.0, m: MAT.ROAD });
  key.push({ n: -(hw - 0.22), b: 0.12, m: MAT.CURB });
  key.push({ n: -hw, b: 0.12, m: MAT.CURB });

  // fill large gaps so the road has even-ish spacing (<= 0.45 m)
  let floor = [key[0]];
  for (let i = 1; i < key.length; i++) {
    const p0 = key[i - 1], p1 = key[i];
    const gap = p0.n - p1.n;
    const c = Math.ceil(gap / 0.45);
    if (gap > 0.46) {
      for (let j = 1; j < c; j++) {
        const t = j / c;
        floor.push({ n: p0.n + (p1.n - p0.n) * t, b: p0.b + (p1.b - p0.b) * t, m: MAT.ROAD });
      }
    }
    floor.push(p1);
  }
  // floor segment count must be EVEN so each junction cap splits cleanly at the middle vertex
  if ((floor.length - 1) % 2 === 1) {
    // split the widest road gap
    let best = -1, bestGap = 0;
    for (let i = 1; i < floor.length; i++) {
      const g = floor[i - 1].n - floor[i].n;
      if (g > bestGap && floor[i - 1].m === MAT.ROAD && floor[i].m === MAT.ROAD) { bestGap = g; best = i; }
    }
    const p0 = floor[best - 1], p1 = floor[best];
    floor.splice(best, 0, { n: (p0.n + p1.n) / 2, b: (p0.b + p1.b) / 2, m: MAT.ROAD });
  }
  const a = floor.length - 1;
  // uniform, flat layout used at the tunnel mouths (grooves fade into the junction plate)
  const uniform = floor.map((_, i) => ({ n: hw - (2 * hw * i) / a, b: 0 }));

  const w = P.wallSegs, r = P.roofSegs;
  const bulge = P.wallBulge, sh = P.springHeight, rh = P.roofHeight;
  const ring = [];
  for (let i = 0; i <= a; i++) ring.push({ ...floor[i], rock: 0, part: 'floor' });
  for (let j = 1; j <= w; j++) {
    const t = j / w;
    ring.push({ n: -(hw + bulge * Math.sin(t * Math.PI / 2)), b: 0.12 + (sh - 0.12) * t, m: MAT.ROCK, rock: smooth(t), part: 'wallR' });
  }
  const R0 = hw + bulge;
  for (let j = 1; j <= r; j++) {
    const th = (j / r) * Math.PI;
    ring.push({ n: -R0 * Math.cos(th), b: sh + (rh - sh) * Math.sin(th), m: MAT.ROCK, rock: 1, part: 'roof' });
  }
  for (let j = 1; j < w; j++) {
    const t = (w - j) / w;
    ring.push({ n: hw + bulge * Math.sin(t * Math.PI / 2), b: 0.12 + (sh - 0.12) * t, m: MAT.ROCK, rock: smooth(t), part: 'wallL' });
  }
  // arc-length (for UVs)
  const arc = [0];
  for (let i = 1; i <= ring.length; i++) {
    const p0 = ring[i - 1], p1 = ring[i % ring.length];
    arc.push(arc[i - 1] + Math.hypot(p1.n - p0.n, p1.b - p0.b));
  }
  return { a, w, r, N: ring.length, ring, floor, uniform, arc };
}

function smooth(t) { return t * t * (3 - 2 * t); }
