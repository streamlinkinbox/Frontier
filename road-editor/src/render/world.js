// Turns the planar road graph (core/network.js) into triangle buckets.
// Each edge is drawn lane-by-lane; each junction node gets a merged asphalt plate with
// zebra crossings; bridges and subways get their structures; props are placed last.
import { Bucket } from './buckets.js';
import { LANE_TYPES } from '../core/presets.js';
import { elevationAt, roadWidth } from '../core/model.js';
import { terrainY } from '../core/terrain.js';
import { mulberry32 } from '../core/rng.js';
import { convexHull } from '../core/geom.js';
import { buildBridge } from './bridges.js';
import { buildProps } from './props.js';

const EDGE_LINE = 0.12;

export function buildWorld(model, net) {
  const buckets = new Map();
  const get = (key) => {
    let b = buckets.get(key);
    if (!b) buckets.set(key, (b = new Bucket()));
    return b;
  };
  const ctx = { get, net };

  // Per-road decoration (patches / cracks) generated deterministically from the road seed.
  const decals = new Map();
  for (const R of net.roads) decals.set(R.id, makeDecals(R.road, R.L, roadWidth(R.road)));

  for (const e of net.edges) buildEdge(e, ctx, decals.get(e.roadId));
  for (const n of net.nodes) buildNode(n, net, ctx);
  for (const R of net.roads) {
    if (R.road.layer.mode === 'bridge') buildBridge(R, ctx);
    if (R.road.layer.mode === 'subway') buildTunnel(R, ctx);
  }
  buildProps(model, net, ctx);
  return buckets;
}

/* ------------------------------------------------------------------ edges */

function buildEdge(e, ctx, decal) {
  const get = ctx.get;
  const road = e.road;
  const L = e.roadL;
  const W = roadWidth(road);
  const look = road.look;
  const pts = e.pts;
  const tang = tangents(pts);

  // Lane offsets: positive = left of travel direction, lane 0 is the leftmost lane.
  let off = W / 2;
  const lanes = road.lanes.map((ln, i) => {
    const o1 = off, o2 = off - ln.width;
    off = o2;
    const def = LANE_TYPES[ln.type] || LANE_TYPES.drive;
    return { ...ln, def, o1, o2, index: i, tint: look.tint * (0.97 + 0.05 * ((i * 37 + road.seed) % 7) / 7) };
  });

  const at = (i, o) => {
    const p = pts[i], n = tang[i].n;
    const x = p.x + n.x * o, z = p.z + n.z * o;
    return { x, y: elevationAt(road, p.s, L, x, z), z };
  };

  // 1. Lane surfaces (+ curbs between lanes of different heights).
  for (const lane of lanes) {
    if (lane.width <= 0.01) continue;
    const Lp = [], Rp = [];
    for (let i = 0; i < pts.length; i++) {
      const a = at(i, lane.o1), b = at(i, lane.o2);
      Lp.push({ x: a.x, y: a.y + lane.def.h, z: a.z });
      Rp.push({ x: b.x, y: b.y + lane.def.h, z: b.z });
    }
    get(lane.def.surface).strip(Lp, Rp, lane.tint);

    if (lane.def.rails) drawRails(lane, pts, at, ctx);
    if (lane.def.sleepers) drawSleepers(lane, pts, at, ctx, road, L);
  }

  // 2. Curb faces where neighbouring lanes sit at different heights.
  for (let i = 0; i < lanes.length - 1; i++) {
    const a = lanes[i], b = lanes[i + 1];
    if (a.width <= 0.01 || b.width <= 0.01) continue;
    if (Math.abs(a.def.h - b.def.h) < 0.01) continue;
    const lo = a.def.h < b.def.h ? a : b, hi = lo === a ? b : a;
    const base = [], top = [];
    for (let k = 0; k < pts.length; k++) {
      const p = at(k, lo === a ? a.o2 : a.o2);
      base.push({ x: p.x, y: p.y + lo.def.h, z: p.z });
      top.push({ x: p.x, y: p.y + hi.def.h, z: p.z });
    }
    get('concrete').wall(base, top, 0.97);
  }
  // Outer kerb drop on the first and last lane if they are raised.
  for (const [lane, sideO] of [[lanes[0], lanes[0].o1], [lanes[lanes.length - 1], lanes[lanes.length - 1].o2]]) {
    if (!lane || lane.def.h < 0.05 || lane.width <= 0.01) continue;
    const top = [], bot = [];
    for (let k = 0; k < pts.length; k++) {
      const p = at(k, sideO);
      top.push({ x: p.x, y: p.y + lane.def.h, z: p.z });
      bot.push({ x: p.x, y: p.y - 0.6, z: p.z });
    }
    get('concrete').wall(bot, top, 0.95);
  }

  // 3. Lane markings.
  for (let i = 0; i < lanes.length - 1; i++) {
    const a = lanes[i], b = lanes[i + 1];
    const m = boundaryMarking(a, b);
    if (!m) continue;
    const y = Math.max(a.def.h, b.def.h) + 0.02;
    drawMarking(m, a.o2, y, pts, tang, at, get, look, road);
  }
  // Edge lines on the outside of carriageways.
  if (lanes.length) {
    const first = lanes[0], last = lanes[lanes.length - 1];
    if (carriage(first)) drawMarking({ style: 'solid', color: 'paint_white' }, first.o1 - 0.25, first.def.h + 0.02, pts, tang, at, get, look, road);
    if (carriage(last)) drawMarking({ style: 'solid', color: 'paint_white' }, last.o2 + 0.25, last.def.h + 0.02, pts, tang, at, get, look, road);
  }

  // 4. Driving arrows.
  for (const lane of lanes) {
    if (!['drive', 'turn'].includes(lane.type) || !lane.dir || lane.width < 2.4) continue;
    const every = look.arrowEvery;
    const cOff = (lane.o1 + lane.o2) / 2;
    const sign = lane.dir === 'back' ? -1 : 1;
    for (let sArrow = Math.ceil(e.s0 / every) * every + every / 2; sArrow < e.s1 - 2; sArrow += every) {
      const q = sampleAt(pts, sArrow);
      if (!q) continue;
      drawArrow(q, cOff, sign, lane.def.h, get, road, L);
    }
  }

  // 5. Patch / crack decals (darker asphalt).
  if (decal) {
    for (const d of decal) {
      if (d.s < e.s0 || d.s > e.s1) continue;
      const q = sampleAt(pts, d.s);
      if (!q) continue;
      drawDecal(d, q, get, road, L);
    }
  }
}

function carriage(lane) {
  return lane.width > 0.01 && ['drive', 'turn', 'bus', 'bike', 'parking', 'shoulder', 'cycletrack'].includes(lane.type);
}

/** Which marking goes between two neighbouring lanes (left a, right b). */
function boundaryMarking(a, b) {
  const t = [a.type, b.type];
  const has = (x) => t.includes(x);
  const both = (x, y) => (a.type === x && b.type === y) || (a.type === y && b.type === x);
  if (has('tram') || has('rail') || (has('median'))) return null;
  if (a.type === 'drive' && b.type === 'drive') {
    if (a.dir && b.dir && a.dir !== b.dir) return { style: 'double', color: 'paint_yellow' };
    return { style: 'dashed', color: 'paint_white' };
  }
  if (has('turn')) return { style: 'dashed', color: 'paint_yellow' };
  if (both('drive', 'parking')) return { style: 'dashed', color: 'paint_white' };
  if (has('bus') || has('bike') || has('cycletrack')) return { style: 'solid', color: 'paint_white' };
  if (has('shoulder') && has('drive')) return { style: 'solid', color: 'paint_white' };
  if (has('parking') && has('sidewalk')) return null;
  return null;
}

/** Paint a line parallel to the centreline at lane-boundary `offset`, lifted `lift` above the surface. */
function drawMarking(m, offset, lift, pts, tang, at, get, look, road) {
  const bucket = get(m.color || 'paint_white');
  const period = look.dashLength + look.dashGap;
  const phase = road.seed % 997;
  const h = EDGE_LINE / 2;
  const draw = (o) => {
    for (let i = 0; i < pts.length - 1; i++) {
      if (m.style === 'dashed') {
        const mid = (pts[i].s + pts[i + 1].s) / 2;
        if ((mid + phase) % period > look.dashLength) continue;
      }
      const a0 = at(i, o), a1 = at(i + 1, o);
      const n0 = tang[i].n, n1 = tang[i + 1].n;
      bucket.quad(
        { x: a0.x + n0.x * h, y: a0.y + lift, z: a0.z + n0.z * h },
        { x: a0.x - n0.x * h, y: a0.y + lift, z: a0.z - n0.z * h },
        { x: a1.x - n1.x * h, y: a1.y + lift, z: a1.z - n1.z * h },
        { x: a1.x + n1.x * h, y: a1.y + lift, z: a1.z + n1.z * h },
      );
    }
  };
  if (m.style === 'double') {
    draw(offset - 0.14);
    draw(offset + 0.14);
  } else {
    draw(offset);
  }
}

/** Arrow pointing along the lane. q = {x,y,z,tx,tz}. */
function drawArrow(q, offset, sign, h, get, road, L) {
  const n = { x: -q.tz, z: q.tx };
  const t = { x: q.tx * sign, z: q.tz * sign };
  const base = { x: q.x + n.x * offset, z: q.z + n.z * offset };
  const P = (along, across) => {
    const x = base.x + t.x * along + n.x * across;
    const z = base.z + t.z * along + n.z * across;
    return { x, y: elevationAt(road, q.s, L, x, z) + h + 0.025, z };
  };
  const b = get('paint_white');
  const stem = [[-1.6, -0.14], [0.1, -0.14], [0.1, 0.14], [-1.6, 0.14]].map(([a, c]) => P(a, c));
  b.quad(stem[0], stem[1], stem[2], stem[3]);
  b.tri(P(0.1, -0.55), P(1.5, 0), P(0.1, 0.55));
}

/** Random repair patches and cracks, deterministic for this road. */
function makeDecals(road, L, W) {
  const look = road.look;
  const r = mulberry32((road.seed ^ 0x9e3779b9) >>> 0);
  const out = [];
  for (let k = 0; k < look.patches; k++) {
    out.push({ kind: 'patch', s: r() * L, off: (r() - 0.5) * W * 0.7, len: 2 + r() * 3.5, wid: 1.4 + r() * 1.8 });
  }
  for (let k = 0; k < look.cracks; k++) {
    out.push({ kind: 'crack', s: r() * L, off: (r() - 0.5) * W * 0.8, len: 4 + r() * 10, wid: 0.12 });
  }
  return out;
}

function drawDecal(d, q, get, road, L) {
  const n = { x: -q.tz, z: q.tx };
  const tint = d.kind === 'patch' ? 0.62 : 0.55;
  const P = (a, c) => {
    const x = q.x + q.tx * a + n.x * (d.off + c), z = q.z + q.tz * a + n.z * (d.off + c);
    return { x, y: elevationAt(road, q.s, L, x, z) + 0.012, z };
  };
  const hl = d.len / 2, hw = d.wid / 2;
  get('asphalt').quad(P(-hl, -hw), P(-hl, hw), P(hl, hw), P(hl, -hw), tint);
}

function drawRails(lane, pts, at, ctx) {
  const bucket = ctx.get('steel');
  const c = (lane.o1 + lane.o2) / 2;
  const gauge = 0.7;
  for (const sgn of [-1, 1]) {
    const line = [];
    for (let i = 0; i < pts.length; i++) {
      const p = at(i, c + sgn * gauge);
      line.push({ x: p.x, y: p.y + 0.12, z: p.z });
    }
    bucket.tube(line, 0.055, 4);
  }
}

function drawSleepers(lane, pts, at, ctx, road, L) {
  const bucket = ctx.get('wood');
  const c = (lane.o1 + lane.o2) / 2;
  alongEvery(pts, 0.7, (p, t, n) => {
    const x = p.x + n.x * c, z = p.z + n.z * c;
    const y = elevationAt(road, p.s, L, x, z) + lane.def.h + 0.02;
    const yaw = Math.atan2(-n.z, n.x);
    bucket.box(x, y, z, 2.7, 0.14, 0.24, yaw, 0.95);
  });
}

/** Walk a polyline and call fn at each `step` metres of arc length. */
export function alongEvery(pts, step, fn) {
  let carried = 0;
  let next = 0;
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    const seg = Math.hypot(b.x - a.x, b.z - a.z);
    if (seg < 1e-9) continue;
    const tx = (b.x - a.x) / seg, tz = (b.z - a.z) / seg;
    while (next <= carried + seg) {
      const t = (next - carried) / seg;
      const p = { x: a.x + (b.x - a.x) * t, z: a.z + (b.z - a.z) * t, s: a.s + (b.s - a.s) * t };
      fn(p, { x: tx, z: tz }, { x: -tz, z: tx }, next);
      next += step;
    }
    carried += seg;
  }
}

/** Point + tangent on an edge polyline at road arc length s. */
function sampleAt(pts, s) {
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    if (s < a.s - 1e-6 || s > b.s + 1e-6) continue;
    const span = b.s - a.s || 1e-6;
    const t = (s - a.s) / span;
    const dx = b.x - a.x, dz = b.z - a.z, L = Math.hypot(dx, dz) || 1;
    return { x: a.x + dx * t, z: a.z + dz * t, s, tx: dx / L, tz: dz / L };
  }
  return null;
}

/** Tangent + left normal per sample. */
function tangents(pts) {
  return pts.map((p, i) => {
    const a = pts[Math.max(0, i - 1)], b = pts[Math.min(pts.length - 1, i + 1)];
    const dx = b.x - a.x, dz = b.z - a.z;
    const l = Math.hypot(dx, dz) || 1;
    const t = { x: dx / l, z: dz / l };
    return { t, n: { x: -t.z, z: t.x } };
  });
}

/* ------------------------------------------------------------------ nodes */

function buildNode(n, net, ctx) {
  if (n.degree < 1) return;
  const inc = n.edges.map(({ edge, end }) => ({ e: net.edges[edge], end }));
  const roadIds = new Set(inc.map((x) => x.e.roadId));
  const needsPlate = n.degree !== 2 || roadIds.size > 1;
  if (!needsPlate) return;

  const y = terrainY(n.x, n.z) + 0.04;
  const pts = [];
  let tint = 1;
  for (const { e, end } of inc) {
    const W = roadWidth(e.road);
    tint = e.road.look.tint;
    const p0 = e.pts[end === 'a' ? 0 : e.pts.length - 1];
    const p1 = e.pts[end === 'a' ? 1 : e.pts.length - 2];
    let dx = p1.x - p0.x, dz = p1.z - p0.z;
    const l = Math.hypot(dx, dz) || 1;
    dx /= l; dz /= l;
    const nx = -dz * W / 2, nz = dx * W / 2;
    pts.push({ x: n.x + nx, z: n.z + nz }, { x: n.x - nx, z: n.z - nz });
    if (n.degree === 1) {
      // dead end: round cap
      for (let k = 0; k <= 8; k++) {
        const a = -dx === 0 && -dz === 0 ? 0 : Math.atan2(-dz, -dx) + (-Math.PI / 2 + (k / 8) * Math.PI);
        pts.push({ x: n.x + Math.cos(a) * W / 2, z: n.z + Math.sin(a) * W / 2 });
      }
    }
  }
  const hull = convexHull(pts);
  if (hull.length < 3) return;
  const b = ctx.get('asphalt');
  const cx = hull.reduce((a, p) => a + p.x, 0) / hull.length;
  const cz = hull.reduce((a, p) => a + p.z, 0) / hull.length;
  const c = { x: cx, y, z: cz };
  for (let k = 0; k < hull.length; k++) {
    const p = hull[k], q = hull[(k + 1) % hull.length];
    b.tri(c, { x: p.x, y, z: p.z }, { x: q.x, y, z: q.z }, tint);
  }

  // Zebra crossings across each arm of a real junction (3+ arms).
  if (n.degree >= 3) {
    for (const { e, end } of inc) {
      if (e.road.kind === 'rail' || e.road.layer.mode !== 'ground') continue;
      if (e.length < 22) continue;
      const dStart = 9;
      const sC = end === 'a' ? e.s0 + dStart : e.s1 - dStart;
      const q = sampleAt(e.pts, sC);
      if (!q) continue;
      const dir = end === 'a' ? 1 : -1;
      drawZebra(q, dir, e, ctx);
    }
  }
}

function drawZebra(q, dir, e, ctx) {
  const road = e.road;
  const sidewalks = road.lanes.filter((l) => l.type === 'sidewalk' || l.type === 'pavers').reduce((a, l) => a + l.width, 0);
  const W = roadWidth(road) - sidewalks;
  if (W < 3) return;
  const tx = q.tx * dir, tz = q.tz * dir;
  const nx = -tz, nz = tx;
  const L = e.roadL;
  const b = ctx.get('paint_white');
  const count = Math.max(4, Math.floor(W / 1.0));
  for (let k = 0; k < count; k++) {
    const lat = -W / 2 + 0.5 + (k * (W - 1)) / Math.max(1, count - 1);
    const cxp = q.x + nx * lat, czp = q.z + nz * lat;
    const P = (a, c) => {
      const x = cxp + tx * a + nx * c, z = czp + tz * a + nz * c;
      return { x, y: elevationAt(road, q.s, L, x, z) + 0.03, z };
    };
    b.quad(P(-1.5, -0.3), P(-1.5, 0.3), P(1.5, 0.3), P(1.5, -0.3));
  }
}

/* --------------------------------------------------------------- subways */

function buildTunnel(R, ctx) {
  const wall = ctx.get('tunnel');
  const height = 4.4;
  for (const e of ctx.net.edges) {
    if (e.roadId !== R.id) continue;
    const W = roadWidth(e.road) + 0.8;
    const pts = e.pts;
    const tang = tangents(pts);
    const L = e.roadL;
    const leftBase = [], leftTop = [], rightBase = [], rightTop = [];
    const topRow = [], topRowR = [];
    for (let i = 0; i < pts.length; i++) {
      const p = pts[i], n = tang[i].n;
      const yBase = elevationAt(e.road, p.s, L, p.x, p.z);
      const lx = p.x + n.x * W / 2, lz = p.z + n.z * W / 2;
      const rx = p.x - n.x * W / 2, rz = p.z - n.z * W / 2;
      leftBase.push({ x: lx, y: yBase, z: lz });
      leftTop.push({ x: lx, y: yBase + height, z: lz });
      rightBase.push({ x: rx, y: yBase, z: rz });
      rightTop.push({ x: rx, y: yBase + height, z: rz });
      topRow.push(leftTop[i]);
      topRowR.push(rightTop[i]);
    }
    wall.wall(leftBase, leftTop, 0.9);
    wall.wall(rightBase, rightTop, 0.9);
    wall.strip(topRow, topRowR, 0.8);
  }
}
