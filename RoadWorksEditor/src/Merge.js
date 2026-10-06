//============================================================================================================================================
//                                                                MERGE.JS
//============================================================================================================================================
// Slip roads: diverges, merges and the gore between them.
//
// A junction fillet is the wrong tool for two arms that meet at a shallow angle. The corner radius needed to round
// off a 20° fork runs away to infinity, so the stock apron leaves a pinched notch and the mainline looks like it
// simply stops. A slip road is a different piece of geometry altogether: the two carriageways stay paved together
// until they have drifted far enough apart to be separate roads, and the wedge between them — the gore — is paved,
// edged with solid lines and filled with chevron hatching, ending in a painted nose.
//
// This module walks both arms outwards from the junction in step, laps the strip between their inner edges, and
// stops when the gap reaches the nose width. Everything comes from the arms' own cross-sections, so a gore on a
// curving ramp curves with it.

import { MeshSpec } from './MeshSpec.js?v=8';

export const MERGE_DEFAULTS = {
  enabled: true,
  maxAngle: 46, // ° between arms for the pair to be treated as a fork rather than a corner
  goreWidth: 7.0, // m of separation at which the gore ends and the two roads are independent
  noseWidth: 1.2, // m of painted nose at the tip
  maxLength: 160, // m — never run a gore further than this
  hatch: true,
};

const group = (out, name) => out[name] || (out[name] = new MeshSpec(name));

export function resolveMerge(settings = {}) {
  return { ...MERGE_DEFAULTS, ...(settings.slip || {}) };
}

// Angular gap from `a` to `b`, normalised to [0, 2π).
function gapBetween(a, b) {
  let delta = b.angle - a.angle;
  while (delta < 0) delta += Math.PI * 2;
  while (delta >= Math.PI * 2) delta -= Math.PI * 2;
  return delta;
}

// Walks an arm away from the junction, returning the inner-edge point and the carriageway centre at a list of
// distances. `side` is 'right' or 'left' in junction-relative terms, matching the fillet convention.
function walkArm(sections, atStart, side, distances) {
  const last = sections.length - 1;
  const total = sections[last].distance;
  const out = [];
  const pick = (s) => {
    // `atStart` arms are traversed backwards, which also swaps left and right.
    const wantRight = side === 'right';
    const useRight = atStart ? !wantRight : wantRight;
    return useRight ? s.roadRight : s.roadLeft;
  };
  let cursor = 0;
  for (const d of distances) {
    const along = atStart ? Math.min(total, d) : Math.max(0, total - d);
    while (cursor > 0 && sections[cursor].distance > along) cursor--;
    while (cursor < last - 1 && sections[cursor + 1].distance < along) cursor++;
    const a = sections[cursor];
    const b = sections[Math.min(cursor + 1, last)];
    const span = b.distance - a.distance;
    const t = span > 1e-6 ? (along - a.distance) / span : 0;
    const mix = (pa, pb) => ({ x: pa.x + (pb.x - pa.x) * t, y: pa.y + (pb.y - pa.y) * t, z: pa.z + (pb.z - pa.z) * t });
    out.push({
      edge: mix(pick(a), pick(b)),
      centre: mix(a.center, b.center),
      past: d > total,
    });
  }
  return out;
}

const dist2 = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);

// Builds every gore at one junction. Returns the number of slip roads treated.
export function buildMerges(graph, node, built, sectionsByEdge, out, settings = {}) {
  const cfg = resolveMerge(settings);
  if (!cfg.enabled) return 0;
  const approaches = built.approaches;
  const n = approaches.length;
  if (n < 2) return 0;

  const road = group(out, settings.roadGroup || 'road');
  const paint = group(out, 'markings');
  let built_ = 0;

  for (let i = 0; i < n; i++) {
    const cur = approaches[i];
    const nxt = approaches[(i + 1) % n];
    const gap = gapBetween(cur, nxt);
    if (gap > (cfg.maxAngle * Math.PI) / 180) continue;
    if (gap < 0.02) continue;
    const secA = sectionsByEdge.get(cur.edgeId);
    const secB = sectionsByEdge.get(nxt.edgeId);
    if (!secA || !secB || secA.length < 2 || secB.length < 2) continue;

    // March both arms outwards together until their inner edges have separated by the gore width.
    const step = 2.0;
    const distances = [];
    for (let d = 0; d <= cfg.maxLength; d += step) distances.push(d);
    const armA = walkArm(secA, cur.atStart, 'right', distances);
    const armB = walkArm(secB, nxt.atStart, 'left', distances);

    // Separation has to be measured as a *signed* offset away from the mainline, not a plain distance: close to the
    // junction the ramp still sits inside the mainline's footprint, and that overlap is already paved by the arms
    // themselves. The gore proper begins where the ramp's inner edge emerges past the mainline's edge.
    const strip = [];
    let started = false;
    for (let k = 0; k < distances.length; k++) {
      const a = armA[k];
      const b = armB[k];
      if (a.past || b.past) break;
      const nx = a.edge.x - a.centre.x;
      const ny = a.edge.y - a.centre.y;
      const len = Math.hypot(nx, ny) || 1;
      const separation = ((b.edge.x - a.edge.x) * nx + (b.edge.y - a.edge.y) * ny) / len;
      if (!started && separation < 0.05) continue;
      started = true;
      strip.push({ a: a.edge, b: b.edge, separation, distance: distances[k] });
      if (separation >= cfg.goreWidth) break;
    }
    if (strip.length < 3) continue;

    // Carriageway between the arms: this is the paved wedge that the fillet could not make.
    for (let k = 0; k < strip.length - 1; k++) {
      const s0 = strip[k];
      const s1 = strip[k + 1];
      if (dist2(s0.a, s1.a) < 1e-5 && dist2(s0.b, s1.b) < 1e-5) continue;
      road.addFace([s0.a, s1.a, s1.b, s0.b]);
    }

    // Paint: a solid line down each side of the gore, chevron hatching between them and a nose at the tip.
    const lift = 0.016;
    const raise = (p) => ({ x: p.x, y: p.y, z: p.z + lift });
    const lineWidth = 0.18;
    for (const key of ['a', 'b']) {
      for (let k = 0; k < strip.length - 1; k++) {
        const p0 = strip[k][key];
        const p1 = strip[k + 1][key];
        const other0 = strip[k][key === 'a' ? 'b' : 'a'];
        // offset the line slightly into the gore so it sits on the carriageway, not on its edge
        const inward = towards(p0, other0, 0.35);
        const inward1 = towards(p1, strip[k + 1][key === 'a' ? 'b' : 'a'], 0.35);
        quadAlong(paint, raise(inward), raise(inward1), lineWidth);
      }
    }

    // Painted nose: the solid wedge at the upstream tip, where the two carriageways actually part company. It is
    // the narrow end of the gore, not the wide one — a slab at the downstream end is just a blob in the road.
    const noseEnd = Math.min(strip.length - 1, Math.max(2, strip.findIndex((s) => s.separation >= Math.max(1.2, cfg.noseWidth * 2))));
    const noseLeft = [];
    const noseRight = [];
    for (let k = 0; k <= noseEnd; k++) {
      const st = strip[k];
      const inset = Math.min(0.3, st.separation * 0.25);
      noseLeft.push(raise(towards(st.a, st.b, inset)));
      noseRight.push(raise(towards(st.b, st.a, inset)));
    }
    if (noseLeft.length >= 2) paint.addPolygon([...noseLeft, ...noseRight.reverse()]);

    if (cfg.hatch) {
      for (let k = noseEnd + 1; k < strip.length - 1; k += 2) {
        const s0 = strip[k];
        const s1 = strip[Math.min(k + 1, strip.length - 1)];
        if (s0.separation < 1.6) continue;
        // One chevron bar per pair of stations, leaning back towards the nose.
        const a0 = towards(s0.a, s0.b, 0.6);
        const b0 = towards(s0.b, s0.a, 0.6);
        const mid = { x: (s1.a.x + s1.b.x) / 2, y: (s1.a.y + s1.b.y) / 2, z: (s1.a.z + s1.b.z) / 2 };
        chevron(paint, raise(a0), raise(mid), raise(b0), 0.3);
      }
    }

    built_++;
  }
  return built_;
}

function towards(from, to, metres) {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const len = Math.hypot(dx, dy) || 1;
  const k = Math.min(1, metres / len);
  return { x: from.x + dx * k, y: from.y + dy * k, z: from.z + (to.z - from.z) * k };
}

function quadAlong(spec, p0, p1, width) {
  const dx = p1.x - p0.x;
  const dy = p1.y - p0.y;
  const len = Math.hypot(dx, dy);
  if (len < 1e-5) return;
  const nx = (-dy / len) * width * 0.5;
  const ny = (dx / len) * width * 0.5;
  spec.addFace([
    { x: p0.x + nx, y: p0.y + ny, z: p0.z },
    { x: p1.x + nx, y: p1.y + ny, z: p1.z },
    { x: p1.x - nx, y: p1.y - ny, z: p1.z },
    { x: p0.x - nx, y: p0.y - ny, z: p0.z },
  ]);
}

// A single chevron bar: two legs meeting at `apex`.
function chevron(spec, left, apex, right, width) {
  quadAlong(spec, left, apex, width);
  quadAlong(spec, apex, right, width);
}
