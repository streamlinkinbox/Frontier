// Road lane layout per tunnel.
//
// Each side of a tunnel (left / right of the edge direction a -> b) carries
// 1 or 2 traffic lanes:   2 lanes = 1+1,  3 lanes = 2+1 or 1+2,  4 lanes = 2+2.
// The cart tracks always run in the INNER lane of each side, so trains,
// track beds and junction routes are independent of the lane count.
//
// edge.lanes = { keys: [[l, r], ...], at: [t, ...] }
//   keys : lane counts per side at the start, (optional middle,) and end
//   at   : taper centres as a fraction of the tunnel length (one per change)
// The layout is stored on the edge (saved with the splines JSON), generated
// procedurally from the seed, and can be edited per tunnel in the editor.
import { mulberry32 } from './noise.js';

export const LANE_W = 3.5, SHOULDER = 0.5, TAPER = 32; // m

export const sideHalfWidth = (n) => SHOULDER + LANE_W * n; // 1 lane: 4.0 m, 2 lanes: 7.5 m

const CONFIG = { 2: [[1, 1]], 3: [[2, 1], [1, 2]], 4: [[2, 2]] };
const pick = (rnd, total) => { const c = CONFIG[total]; return c[Math.floor(rnd() * c.length)].slice(); };

// procedural lane plan for every edge (deterministic per seed)
export function generateLanes(net, seed) {
  const rnd = mulberry32(seed * 131 + 17);
  const patterns = [
    [0.22, [2]], [0.12, [3]], [0.12, [4]],
    [0.12, [4, 2]], [0.08, [4, 3]], [0.1, [3, 2]],
    [0.08, [2, 4, 2]], [0.06, [4, 2, 4]], [0.05, [2, 3]], [0.05, [3, 4]],
  ];
  for (const e of net.edges) {
    if (e.overpass) { e.lanes = { keys: [[1, 1]], at: [] }; continue; } // keep the rock between the stacked tunnels thick
    let u = rnd(), pat = patterns[0][1];
    for (const [w, p] of patterns) { if (u < w) { pat = p; break; } u -= w; }
    if (rnd() < 0.5) pat = pat.slice().reverse();
    const keys = pat.map((n) => pick(rnd, n));
    const at = keys.length === 2 ? [0.35 + rnd() * 0.3] : keys.length === 3 ? [0.27 + rnd() * 0.08, 0.65 + rnd() * 0.08] : [];
    e.lanes = { keys, at };
  }
  return net;
}

// uniform override from the GUI ('random' keeps the per-edge plan)
export function effectiveLanes(e, mode) {
  if (e.overpass) return { keys: [[1, 1]], at: [] }; // stacked tunnels stay narrow (rock thickness)
  if (mode === '2') return { keys: [[1, 1]], at: [] };
  if (mode === '4') return { keys: [[2, 2]], at: [] };
  if (mode === '3') return { keys: [e.id % 2 ? [2, 1] : [1, 2]], at: [] };
  return e.lanes && e.lanes.keys && e.lanes.keys.length ? e.lanes : { keys: [[1, 1]], at: [] };
}

const smooth = (x) => { const t = Math.min(1, Math.max(0, x)); return t * t * (3 - 2 * t); };

// Resolve the plan for a tunnel running from s0 to s1 (after the junction cuts).
// Returns layout(s) -> { l, r (fractional lane counts), hwL, hwR, tapers }
export function laneLayout(plan, s0, s1) {
  let { keys, at } = plan;
  const len = s1 - s0;
  const tapers = [];
  if (keys.length > 1) {
    // the mines are dense: tunnels between junctions are often only 20 - 50 m long, so the
    // taper length adapts to the room (10 - 32 m) and may start 5 m after a junction mouth
    const M = 5, GAPT = 4, nT = keys.length - 1;
    const room = (len - 2 * M - GAPT * (nT - 1)) / nT;
    const L = Math.min(TAPER, nT === 1 ? Math.max(10, room * 0.55) : room);
    // a lane DROP should come late in its traffic's direction (room for arrows + sign):
    // right side travels +s, left side travels -s
    if (nT === 1) {
      const [[l0, r0], [l1, r1]] = keys;
      const rDrop = r1 < r0, lDrop = l1 > l0;
      if (rDrop !== lDrop) at = [rDrop ? Math.max(at[0], 0.72) : Math.min(at[0], 0.28)];
    }
    const centers = at.map((t) => s0 + t * len);
    if (L >= 10) {
      // clamp into the mouth margins, then push apart so tapers never overlap
      for (let i = 0; i < nT; i++) centers[i] = Math.min(Math.max(centers[i], s0 + M + L / 2 + i * (L + GAPT)), s1 - M - L / 2 - (nT - 1 - i) * (L + GAPT));
      for (let i = 1; i < nT; i++) centers[i] = Math.max(centers[i], centers[i - 1] + L + GAPT);
      for (let i = 0; i < nT; i++) tapers.push({ c: centers[i], L, from: keys[i], to: keys[i + 1] });
    } else {
      // too short for the transitions -> the narrowest configuration throughout
      const n = keys.reduce((m, k) => (k[0] + k[1] < m[0] + m[1] ? k : m), keys[0]);
      keys = [n];
    }
  }
  const layout = (s) => {
    let l = keys[0][0], r = keys[0][1];
    for (const t of tapers) {
      const w = smooth((s - (t.c - t.L / 2)) / t.L);
      l += (t.to[0] - t.from[0]) * w;
      r += (t.to[1] - t.from[1]) * w;
    }
    return { l, r, hwL: sideHalfWidth(l), hwR: sideHalfWidth(r) };
  };
  layout.tapers = tapers;
  layout.keys = keys;
  return layout;
}

// lane count at an end (for junction sizing) without knowing the cuts yet
export function endLanes(plan, end) {
  const k = plan.keys;
  return end === 'a' ? k[0] : k[k.length - 1];
}

// ---- editor helpers: named patterns ("4>2" = 4 lanes narrowing to 2) ----
export const PATTERNS = ['2', '3', '4', '4>3', '4>2', '3>2', '2>3', '2>4', '3>4', '2>4>2', '4>2>4'];

export function describeLanes(e) {
  const L = e.lanes;
  if (!L || !L.keys || !L.keys.length) return '2';
  return L.keys.map((k) => k[0] + k[1]).join('>');
}

// set a tunnel's plan from a pattern name; 3-lane sections alternate the wide side
export function setLanePattern(e, name) {
  const totals = String(name).split('>').map(Number);
  const flip = (e.id + (e.lanes?.flip || 0)) % 2;
  const keys = totals.map((t) => (t === 2 ? [1, 1] : t === 4 ? [2, 2] : flip ? [2, 1] : [1, 2]));
  const at = keys.length === 2 ? [0.5] : keys.length === 3 ? [0.3, 0.7] : [];
  e.lanes = { keys, at, flip: e.lanes?.flip || 0 };
  return e.lanes;
}

// swap which side keeps its lanes (mirror the plan across the tunnel axis)
export function mirrorLanes(e) {
  if (!e.lanes) return;
  e.lanes = { ...e.lanes, keys: e.lanes.keys.map(([l, r]) => [r, l]), flip: 1 - (e.lanes.flip || 0) };
}
