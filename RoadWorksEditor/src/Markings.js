//============================================================================================================================================
//                                                              MARKINGS.JS
//============================================================================================================================================
// Lane-level road markings: turn arrows, diagonal hatching and gore areas, yellow box junctions, coloured cycle and
// bus lanes, and the painted approach to a pedestrian refuge.
//
// Everything here is authored in (distance, lateral) station space — how far along the corridor, and how far across
// it — and then pushed through one sampler that knows the corridor's cross-section. That is the whole trick: a turn
// arrow is a flat polygon in station space, so when the road bends, super-elevates or narrows, the arrow bends with
// it for free and never floats off the carriageway.
//
// Lateral is positive to the LEFT of travel, matching the cross-section frames. Right-hand traffic is assumed, so
// the lanes a driver uses on the way INTO a junction are the ones at negative lateral.

import { MeshSpec } from './MeshSpec.js?v=8';

export const MARKING_DEFAULTS = {
  laneArrows: true, // turn arrows on junction approaches
  stopLines: true, // handled by Signs.js; kept here so one object describes the paint
  hatching: true, // diagonal hatching in gores and wide central reserves
  yellowBox: false, // criss-cross box at junctions this corridor dominates
  cycleLane: 'none', // none | left | right | both
  busLane: 'none', // none | left | right | both
  refuges: 0, // pedestrian refuge islands, spaced evenly along the corridor
};

export const LANE_TINTS = { cycle: 'laneTint#cycle', bus: 'laneTint#bus' };

const LIFT = 0.014;
const PAINT = 0.14; // standard line width

// ── station-space sampler ─────────────────────────────────────────────────────────────────────────────────────────

// Returns point(distance, lateral) on the running surface, including the camber and a paint lift, by interpolating
// between the two cross-sections either side of `distance`.
export function surfaceSampler(sections, profile, lift = LIFT) {
  const last = sections.length - 1;
  const total = sections[last].distance;
  let cursor = 0;
  return function point(distance, lateral) {
    const d = clamp(distance, 0, total);
    while (cursor > 0 && sections[cursor].distance > d) cursor--;
    while (cursor < last - 1 && sections[cursor + 1].distance < d) cursor++;
    const a = sections[cursor];
    const b = sections[Math.min(cursor + 1, last)];
    const span = b.distance - a.distance;
    const t = span > 1e-6 ? (d - a.distance) / span : 0;
    const bx = a.base.x + (b.base.x - a.base.x) * t;
    const by = a.base.y + (b.base.y - a.base.y) * t;
    const bz = a.base.z + (b.base.z - a.base.z) * t;
    let lx = a.frame.left.x + (b.frame.left.x - a.frame.left.x) * t;
    let ly = a.frame.left.y + (b.frame.left.y - a.frame.left.y) * t;
    const len = Math.hypot(lx, ly) || 1;
    lx /= len;
    ly /= len;
    const miter = a.miter + (b.miter - a.miter) * t;
    const off = lateral * miter;
    const crownU = clamp(lateral / Math.max(profile.roadHalf, 1e-3), -1, 1);
    const rise = (profile.crownRise || 0) * (1 - crownU * crownU) + lift;
    return { x: bx + lx * off, y: by + ly * off, z: bz + rise };
  };
}

const group = (out, name) => out[name] || (out[name] = new MeshSpec(name));

// ── primitives ────────────────────────────────────────────────────────────────────────────────────────────────────

// A quad strip running along the corridor between two lateral offsets, each of which may vary with distance.
export function paintBand(spec, point, d0, d1, leftAt, rightAt, steps = 12) {
  const n = Math.max(1, steps);
  for (let i = 0; i < n; i++) {
    const da = d0 + ((d1 - d0) * i) / n;
    const db = d0 + ((d1 - d0) * (i + 1)) / n;
    spec.addFace([
      point(da, leftAt(da)),
      point(db, leftAt(db)),
      point(db, rightAt(db)),
      point(da, rightAt(da)),
    ]);
  }
}

// A line of constant (or varying) lateral offset, optionally dashed.
export function paintLine(spec, point, d0, d1, lateralAt, width = PAINT, dash = null, step = 1.2) {
  const span = d1 - d0;
  if (span <= 0) return;
  const n = Math.max(1, Math.ceil(span / step));
  const half = width * 0.5;
  for (let i = 0; i < n; i++) {
    const da = d0 + (span * i) / n;
    const db = d0 + (span * (i + 1)) / n;
    if (dash) {
      const period = dash.on + dash.off;
      if ((da - d0) % period > dash.on) continue;
    }
    const la = lateralAt(da);
    const lb = lateralAt(db);
    spec.addFace([
      point(da, la + half),
      point(db, lb + half),
      point(db, lb - half),
      point(da, la - half),
    ]);
  }
}

// Diagonal hatching clipped to a band. Bars run at 45° so they read as "do not enter this area" from the air and
// from the driver's seat alike; the clipping is done by walking each bar across the band and dropping the samples
// that fall outside it, which handles the tapering nose of a gore without any polygon clipping maths.
export function paintHatch(spec, point, d0, d1, leftAt, rightAt, { spacing = 2.4, barWidth = 0.4, lean = 1 } = {}) {
  const span = d1 - d0;
  if (span <= 0.5) return;
  const widest = Math.max(
    Math.abs(leftAt(d0) - rightAt(d0)),
    Math.abs(leftAt(d1) - rightAt(d1)),
    Math.abs(leftAt((d0 + d1) / 2) - rightAt((d0 + d1) / 2)),
  );
  if (widest < 0.6) return;
  const start = d0 - widest * 1.2;
  for (let anchor = start; anchor < d1; anchor += spacing) {
    const steps = Math.max(2, Math.ceil(widest / 0.3));
    let run = null;
    for (let i = 0; i <= steps; i++) {
      const frac = i / steps;
      // march across the band, from the right edge to the left edge, at the mid-station width
      const probeD = clamp(anchor + frac * widest * lean, d0, d1);
      const r = rightAt(probeD);
      const l = leftAt(probeD);
      const lat = r + (l - r) * frac;
      const d = anchor + frac * widest * lean;
      const ok = d >= d0 && d <= d1 && lat <= Math.max(l, r) && lat >= Math.min(l, r) && Math.abs(l - r) > 0.4;
      if (!ok) {
        run = flushRun(spec, point, run, barWidth);
        continue;
      }
      if (!run) run = [];
      run.push([d, lat]);
    }
    flushRun(spec, point, run, barWidth);
  }
}

function flushRun(spec, point, run, barWidth) {
  if (run && run.length > 1) {
    const h = barWidth * 0.5;
    for (let i = 0; i < run.length - 1; i++) {
      const [da, la] = run[i];
      const [db, lb] = run[i + 1];
      spec.addFace([
        point(da + h, la),
        point(db + h, lb),
        point(db - h, lb),
        point(da - h, la),
      ]);
    }
  }
  return null;
}

// ── turn arrows ───────────────────────────────────────────────────────────────────────────────────────────────────

// Arrow glyphs live in a local (u, v) frame: u runs forward along travel, v to the left. Returns a list of polygons.
export function arrowGlyph(kind) {
  const shaftHalf = 0.26;
  const polys = [];
  const stemTop = kind === 'through' ? 3.3 : 2.4;
  polys.push([[0, -shaftHalf], [stemTop, -shaftHalf], [stemTop, shaftHalf], [0, shaftHalf]]);
  const head = (tipU, tipV, baseU, baseV, nx, ny) => {
    const w = 0.62;
    polys.push([
      [baseU + nx * w, baseV + ny * w],
      [tipU, tipV],
      [baseU - nx * w, baseV - ny * w],
    ]);
  };
  if (kind === 'through' || kind === 'throughleft' || kind === 'throughright') {
    polys.push([[stemTop, -shaftHalf], [3.6, -shaftHalf], [3.6, shaftHalf], [stemTop, shaftHalf]]);
    head(5.0, 0, 3.6, 0, 0, 1);
  }
  if (kind === 'left' || kind === 'throughleft') {
    // arm bending to the left, then a head pointing across the road
    polys.push([[2.0, shaftHalf], [2.0 + 2 * shaftHalf, shaftHalf], [2.0 + 2 * shaftHalf, 1.5], [2.0, 1.5]]);
    head(2.0 + shaftHalf, 2.5, 2.0 + shaftHalf, 1.5, 1, 0);
  }
  if (kind === 'right' || kind === 'throughright') {
    polys.push([[2.0, -1.5], [2.0 + 2 * shaftHalf, -1.5], [2.0 + 2 * shaftHalf, -shaftHalf], [2.0, -shaftHalf]]);
    head(2.0 + shaftHalf, -2.5, 2.0 + shaftHalf, -1.5, 1, 0);
  }
  if (kind === 'left') head(2.0 + shaftHalf, 2.5, 2.0 + shaftHalf, 1.5, 1, 0);
  return polys;
}

// Places a glyph at distance `d0`, centred on lane offset `lateral`, pointing in the direction of increasing
// distance when `forward`, otherwise mirrored so it points back down the corridor.
export function paintGlyph(spec, point, polys, d0, lateral, forward = true) {
  for (const poly of polys) {
    spec.addPolygon(
      poly.map(([u, v]) => (forward ? point(d0 + u, lateral + v) : point(d0 - u, lateral - v))),
    );
  }
}

// Which movements can a driver make from this approach? Compares the approach bearing against every other arm.
export function turnsFromNode(graph, node, approachBearing) {
  const turns = { left: false, right: false, through: false };
  for (const edgeId of node.edgeIds) {
    const edge = graph.edges.get(edgeId);
    if (!edge) continue;
    for (const end of ['start', 'end']) {
      const nodeId = end === 'start' ? edge.startNodeId : edge.endNodeId;
      if (nodeId !== node.id) continue;
      const pts = edge.points;
      if (!pts || pts.length < 2) continue;
      const a = end === 'start' ? pts[0] : pts[pts.length - 1];
      const b = end === 'start' ? pts[1] : pts[pts.length - 2];
      const out = Math.atan2(b.y - a.y, b.x - a.x); // bearing leaving the node
      let rel = out - approachBearing;
      while (rel > Math.PI) rel -= Math.PI * 2;
      while (rel < -Math.PI) rel += Math.PI * 2;
      const deg = (rel * 180) / Math.PI;
      if (Math.abs(deg) < 35) turns.through = true;
      else if (deg >= 35 && deg < 150) turns.left = true;
      else if (deg <= -35 && deg > -150) turns.right = true;
    }
  }
  if (!turns.left && !turns.right && !turns.through) turns.through = true;
  return turns;
}

// One glyph per approach lane. The outside lane takes the right turn, the inside lane the left, the rest go through
// — the usual allocation, collapsed onto a single combined glyph when there is only one lane each way.
export function allocateArrows(laneCount, turns) {
  const out = [];
  for (let i = 0; i < laneCount; i++) {
    const outermost = i === 0;
    const innermost = i === laneCount - 1;
    let kind = 'through';
    if (laneCount === 1) {
      if (turns.left && turns.right && turns.through) kind = 'throughleft';
      else if (turns.left && turns.through) kind = 'throughleft';
      else if (turns.right && turns.through) kind = 'throughright';
      else if (turns.left) kind = 'left';
      else if (turns.right) kind = 'right';
    } else if (outermost && turns.right) kind = turns.through ? 'throughright' : 'right';
    else if (innermost && turns.left) kind = turns.through ? 'throughleft' : 'left';
    out.push(kind);
  }
  return out;
}

// ── coloured lanes ────────────────────────────────────────────────────────────────────────────────────────────────

// A cycle or bus lane: tinted surfacing inside a solid white line, with a glyph repeated along it.
export function paintSpecialLane(out, sections, profile, side, kind, options = {}) {
  const point = surfaceSampler(sections, profile, LIFT * 0.6);
  const paint = group(out, 'markings');
  const tint = group(out, LANE_TINTS[kind] || LANE_TINTS.cycle);
  const total = sections[sections.length - 1].distance;
  const d0 = options.start ?? 0;
  const d1 = options.end ?? total;
  const width = kind === 'cycle' ? 1.7 : Math.min(3.3, profile.roadWidth / Math.max(2, profile.lanes) * 1.05);
  const sign = side === 'left' ? 1 : -1;
  const outer = sign * (profile.roadHalf - 0.1);
  const inner = sign * (profile.roadHalf - 0.1 - width);
  const steps = Math.max(2, Math.round((d1 - d0) / 2));
  paintBand(tint, point, d0, d1, () => Math.max(outer, inner), () => Math.min(outer, inner), steps);
  paintLine(paint, point, d0, d1, () => inner, 0.18, null, 1.2);
  const glyphs = kind === 'cycle' ? cycleGlyph() : busGlyph();
  const spacing = 26;
  for (let d = d0 + 10; d < d1 - 6; d += spacing) {
    paintGlyph(paint, point, glyphs, d, (outer + inner) / 2, sign > 0);
  }
}

// A bicycle, drawn as two wheel rings plus a frame — legible from the air and cheap in triangles.
function cycleGlyph() {
  const polys = [];
  const ring = (cu, r) => {
    const n = 14;
    for (let i = 0; i < n; i++) {
      const a0 = (i / n) * Math.PI * 2;
      const a1 = ((i + 1) / n) * Math.PI * 2;
      const t = 0.11;
      polys.push([
        [cu + Math.cos(a0) * (r + t), Math.sin(a0) * (r + t)],
        [cu + Math.cos(a1) * (r + t), Math.sin(a1) * (r + t)],
        [cu + Math.cos(a1) * (r - t), Math.sin(a1) * (r - t)],
        [cu + Math.cos(a0) * (r - t), Math.sin(a0) * (r - t)],
      ]);
    }
  };
  ring(0.55, 0.42);
  ring(2.0, 0.42);
  polys.push([[0.55, 0.05], [1.5, 0.62], [1.6, 0.48], [0.68, -0.08]]);
  polys.push([[1.5, 0.55], [2.0, 0.05], [2.0, -0.09], [1.42, 0.45]]);
  polys.push([[0.9, -0.06], [2.0, -0.06], [2.0, 0.06], [0.9, 0.06]]);
  polys.push([[1.42, 0.5], [1.9, 0.62], [1.9, 0.74], [1.42, 0.62]]);
  return polys;
}

// Bus lanes get the diamond used across much of the world, plus a blocky B so it is unmistakable in plan.
function busGlyph() {
  const polys = [];
  polys.push([[0, 0], [1.1, 0.62], [2.2, 0], [1.1, -0.62]]);
  polys.push([[0.3, 0], [1.1, 0.44], [1.9, 0], [1.1, -0.44]].reverse());
  return polys;
}

// ── pedestrian refuge ─────────────────────────────────────────────────────────────────────────────────────────────

// A kerbed island in the middle of the carriageway with a ghost-island approach painted each side of it. The island
// itself is real geometry (kerb face + top), because a refuge that is only paint is not a refuge.
export function buildRefuge(out, sections, profile, centreDistance, options = {}) {
  const length = options.length ?? 9;
  const half = (options.width ?? 2.0) * 0.5;
  const kerb = options.height ?? 0.14;
  const taper = options.taper ?? 14;
  const total = sections[sections.length - 1].distance;
  const d0 = centreDistance - length / 2;
  const d1 = centreDistance + length / 2;
  if (d0 < 6 || d1 > total - 6 || profile.roadWidth < 7) return false;

  const deck = surfaceSampler(sections, profile, kerb);
  const road = surfaceSampler(sections, profile, 0.004);
  const curb = group(out, 'curb');
  const top = group(out, options.topGroup || 'pavement');
  const paint = group(out, 'markings');

  const steps = Math.max(6, Math.round(length / 0.8));
  // Nose radius: the island ends in a semicircle so it is not a kerb edge pointing at traffic.
  const latAt = (d) => {
    const ends = Math.min(d - d0, d1 - d);
    const nose = Math.min(half, Math.max(0, ends));
    return half * Math.sin((Math.PI / 2) * Math.min(1, nose / half));
  };
  for (let i = 0; i < steps; i++) {
    const da = d0 + (length * i) / steps;
    const db = d0 + (length * (i + 1)) / steps;
    const la = latAt(da);
    const lb = latAt(db);
    top.addFace([deck(da, la), deck(db, lb), deck(db, -lb), deck(da, -la)]);
    for (const s of [1, -1]) {
      curb.addFace([road(da, s * la), road(db, s * lb), deck(db, s * lb), deck(da, s * la)]);
    }
  }
  // Ghost island: hatching that widens from a point into the island, so traffic is steered around it.
  const paintPoint = surfaceSampler(sections, profile, LIFT);
  for (const s of [1, -1]) {
    const ramp = (d) => {
      const t = clamp((d - (d0 - taper)) / taper, 0, 1);
      return s * (0.2 + (half + 0.35) * t);
    };
    const edge = (d) => ramp(d) - s * 0.2;
    paintLine(paint, paintPoint, d0 - taper, d0, ramp, 0.18, null, 1.0);
    paintHatch(paint, paintPoint, d0 - taper + 2, d0, s > 0 ? ramp : edge, s > 0 ? edge : ramp, { spacing: 2.0, barWidth: 0.3, lean: s });
    const rampBack = (d) => {
      const t = clamp((d1 + taper - d) / taper, 0, 1);
      return s * (0.2 + (half + 0.35) * t);
    };
    paintLine(paint, paintPoint, d1, Math.min(total, d1 + taper), rampBack, 0.18, null, 1.0);
  }
  return true;
}

// ── yellow box ────────────────────────────────────────────────────────────────────────────────────────────────────

// The criss-cross box is painted on the junction apron rather than on a corridor, so it works in world space: the
// apron outline is the convex hull of the arms' carriageway corners, and the box is that hull shrunk a little.
export function paintYellowBox(out, built, options = {}) {
  const pts = [];
  for (const a of built.approaches) {
    pts.push(a.roadLeft, a.roadRight);
  }
  if (pts.length < 6) return false;
  const hull = convexHull(pts);
  if (hull.length < 3) return false;
  const centre = built.centre;
  const shrink = options.inset ?? 0.9;
  const box = hull.map((p) => {
    const dx = p.x - centre.x;
    const dy = p.y - centre.y;
    const len = Math.hypot(dx, dy) || 1;
    const k = Math.max(0, (len - shrink) / len);
    return { x: centre.x + dx * k, y: centre.y + dy * k, z: centre.z + 0.016 };
  });
  const spec = group(out, 'markingsYellow');
  // outline
  for (let i = 0; i < box.length; i++) {
    const a = box[i];
    const b = box[(i + 1) % box.length];
    strokeWorld(spec, a, b, 0.2);
  }
  // two diagonal families, clipped to the box by walking each line and keeping the inside runs
  const bounds = hullBounds(box);
  const step = 2.2;
  const diag = Math.hypot(bounds.w, bounds.h);
  for (const dir of [{ x: Math.SQRT1_2, y: Math.SQRT1_2 }, { x: Math.SQRT1_2, y: -Math.SQRT1_2 }]) {
    const nx = -dir.y;
    const ny = dir.x;
    for (let off = -diag; off <= diag; off += step) {
      const ox = centre.x + nx * off;
      const oy = centre.y + ny * off;
      let run = null;
      const samples = Math.ceil(diag / 0.5);
      for (let i = -samples; i <= samples; i++) {
        const t = i * 0.5;
        const p = { x: ox + dir.x * t, y: oy + dir.y * t, z: centre.z + 0.016 };
        if (pointInPolygon(p, box)) {
          if (!run) run = [];
          run.push(p);
        } else if (run) {
          if (run.length > 1) strokeWorld(spec, run[0], run[run.length - 1], 0.16);
          run = null;
        }
      }
      if (run && run.length > 1) strokeWorld(spec, run[0], run[run.length - 1], 0.16);
    }
  }
  return true;
}

function strokeWorld(spec, a, b, width) {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const len = Math.hypot(dx, dy);
  if (len < 1e-4) return;
  const nx = (-dy / len) * width * 0.5;
  const ny = (dx / len) * width * 0.5;
  spec.addFace([
    { x: a.x + nx, y: a.y + ny, z: a.z },
    { x: b.x + nx, y: b.y + ny, z: b.z },
    { x: b.x - nx, y: b.y - ny, z: b.z },
    { x: a.x - nx, y: a.y - ny, z: a.z },
  ]);
}

export function convexHull(points) {
  const pts = points.slice().sort((a, b) => a.x - b.x || a.y - b.y);
  if (pts.length < 3) return pts;
  const cross = (o, a, b) => (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  const lower = [];
  for (const p of pts) {
    while (lower.length >= 2 && cross(lower[lower.length - 2], lower[lower.length - 1], p) <= 0) lower.pop();
    lower.push(p);
  }
  const upper = [];
  for (let i = pts.length - 1; i >= 0; i--) {
    const p = pts[i];
    while (upper.length >= 2 && cross(upper[upper.length - 2], upper[upper.length - 1], p) <= 0) upper.pop();
    upper.push(p);
  }
  lower.pop();
  upper.pop();
  return lower.concat(upper);
}

function hullBounds(poly) {
  let minX = Infinity;
  let maxX = -Infinity;
  let minY = Infinity;
  let maxY = -Infinity;
  for (const p of poly) {
    minX = Math.min(minX, p.x);
    maxX = Math.max(maxX, p.x);
    minY = Math.min(minY, p.y);
    maxY = Math.max(maxY, p.y);
  }
  return { w: maxX - minX, h: maxY - minY };
}

export function pointInPolygon(p, poly) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const a = poly[i];
    const b = poly[j];
    if ((a.y > p.y) !== (b.y > p.y) && p.x < ((b.x - a.x) * (p.y - a.y)) / (b.y - a.y) + a.x) inside = !inside;
  }
  return inside;
}

function clamp(v, lo, hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

// ── per-edge driver ───────────────────────────────────────────────────────────────────────────────────────────────

// Everything a single corridor edge needs: approach arrows at each junction it meets, coloured lanes, refuges and
// central hatching. Called once per edge by Network.js, after the lane lines are down.
export function buildLaneDetail(graph, edge, sections, out, cfg = {}) {
  const profile = edge.profile;
  if (!profile || profile.markings === false || cfg.markings === false) return;
  const opts = { ...MARKING_DEFAULTS, ...(edge.markings || {}) };
  const total = sections[sections.length - 1].distance;
  const lanes = Math.max(1, profile.lanes | 0);
  const laneWidth = profile.roadWidth / lanes;
  const point = surfaceSampler(sections, profile);
  const paint = group(out, 'markings');

  if (opts.laneArrows && total > 26) {
    for (const end of ['start', 'end']) {
      const node = graph.nodes.get(end === 'start' ? edge.startNodeId : edge.endNodeId);
      if (!node || node.degree < 3) continue;
      // A roundabout entry gets give-way teeth instead of lane arrows.
      if (node.style === 'roundabout' || node.fork) continue;
      const pts = edge.points;
      const a = end === 'start' ? pts[0] : pts[pts.length - 1];
      const b = end === 'start' ? pts[1] : pts[pts.length - 2];
      // bearing of the arm leaving the junction, reversed to get the approach direction
      const leaving = Math.atan2(b.y - a.y, b.x - a.x);
      const turns = turnsFromNode(graph, node, leaving + Math.PI);
      const approachLanes = lanes >= 2 ? Math.max(1, Math.floor(lanes / 2)) : 1;
      const kinds = allocateArrows(approachLanes, turns);
      const forward = end === 'end';
      const tail = forward ? total - 13 : 13;
      for (let i = 0; i < approachLanes; i++) {
        const fromEdge = (i + 0.5) * laneWidth;
        const lateral = (forward ? -1 : 1) * (profile.roadHalf - fromEdge);
        paintGlyph(paint, point, arrowGlyph(kinds[i]), tail, lateral, forward);
      }
    }
  }

  if (opts.cycleLane && opts.cycleLane !== 'none') {
    for (const side of opts.cycleLane === 'both' ? ['left', 'right'] : [opts.cycleLane]) {
      paintSpecialLane(out, sections, profile, side, 'cycle');
    }
  }
  if (opts.busLane && opts.busLane !== 'none') {
    for (const side of opts.busLane === 'both' ? ['left', 'right'] : [opts.busLane]) {
      paintSpecialLane(out, sections, profile, side, 'bus');
    }
  }

  const refuges = Math.max(0, Math.min(6, opts.refuges | 0));
  for (let i = 0; i < refuges; i++) {
    buildRefuge(out, sections, profile, (total * (i + 1)) / (refuges + 1), { topGroup: cfg.paveGroup || 'pavement' });
  }

  // A dual carriageway with six or more lanes gets a hatched central reserve rather than a bare double line.
  if (opts.hatching && lanes >= 6 && total > 30) {
    paintHatch(paint, point, 4, total - 4, () => 0.55, () => -0.55, { spacing: 2.6, barWidth: 0.32 });
  }
}
