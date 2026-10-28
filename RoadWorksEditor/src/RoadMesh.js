//============================================================================================================================================
//                                                               ROADMESH.JS
//============================================================================================================================================
// Corridor cross-sections and the swept segment mesh: carriageway with camber, a real three-face curb (gutter → face →
// top), pavement with crossfall, the pavement outer drop and lane markings.
//
// The reference implementation offset every station straight along its perpendicular, so on curves the outer edges
// fell short of the true parallel curve: curbs rippled, pavements self-intersected on tight radii and the markings
// drifted off the carriageway. Here every lateral offset is multiplied by the miter factor 1/cos(θ/2) of the turn at
// that station, and the offset is clamped against the local curvature radius so an inner curb can never fold through
// the centreline. That is what keeps pavements and curbs clean around curves.

import { add, addScaled, clamp, dist, norm, sub, vec } from './Vec.js?v=11';
import { cumulativeLengths, frameAt, miterScale, resamplePolyline, trimPolyline } from './Polyline.js?v=11';
import { MeshSpec } from './MeshSpec.js?v=11';
import { nodeGeneratesJunction, trimForNode } from './Graph.js?v=11';

const CURB_BATTER = 0.04; // m — slight slope on the visible curb face

// Signed curvature (1/R) in the XY plane at a station; used to clamp offsets on tight radii.
function curvatureAt(points, i) {
  if (i <= 0 || i >= points.length - 1) return 0;
  const a = points[i - 1];
  const b = points[i];
  const c = points[i + 1];
  const ab = Math.hypot(b.x - a.x, b.y - a.y);
  const bc = Math.hypot(c.x - b.x, c.y - b.y);
  const ca = Math.hypot(a.x - c.x, a.y - c.y);
  const area2 = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  const denom = ab * bc * ca;
  if (denom < 1e-6) return 0;
  return (2 * area2) / denom; // positive = turning left
}

export function buildCrossSections(polyline, profile, options = {}) {
  const cum = cumulativeLengths(polyline);
  const sections = [];
  const crossfall = options.pavementCrossfall ?? 0.02;

  for (let i = 0; i < polyline.length; i++) {
    const frame = frameAt(polyline, i);
    const p = polyline[i];
    const miter = miterScale(polyline, i);
    const k = curvatureAt(polyline, i);

    // Offsets on the inside of a bend must stay short of the curvature centre.
    const limit = (lateral) => {
      const signed = lateral * (k >= 0 ? 1 : -1);
      if (k !== 0 && signed > 0) {
        const radius = 1 / Math.abs(k);
        return clamp(lateral, -radius * 0.9, radius * 0.9);
      }
      return lateral;
    };

    const off = (lateral, rise) => {
      const l = limit(lateral) * (Math.abs(lateral) > 1e-6 ? miter : 1);
      return { ...addScaled(p, frame.left, l), z: p.z + rise };
    };

    const rh = profile.roadHalf;
    // A ramp leaving a mainline is lapped *inside* it: for the first stretch of a diverge most of the ramp's
    // carriageway is the mainline's carriageway, and paving both of them puts two coplanar surfaces in the same
    // place — which is the black, z-fighting wedge a fork used to show. `lap` pins the buried edge to the edge of
    // the road it is still part of, so the ramp only paves what has actually emerged. See Network.js lapForEdge().
    const lapAt = options.lap ? options.lap(cum[i]) : null;
    const edgeL = lapAt ? clamp(lapAt.left, -rh + 0.05, rh) : rh;
    const edgeR = lapAt ? clamp(lapAt.right, -rh, rh - 0.05) : -rh;
    const halfL = edgeL;
    const halfR = -edgeR; // positive distance from the centreline to the right-hand edge
    const cw = profile.curbWidth;
    // Vehicle crossovers drop the kerb almost flush over the width of the crossing — see Driveways.js. The factor
    // is per side, so a drive on one frontage never flattens the kerb opposite it.
    const drop = options.kerbDrop;
    // Inside a gore the kerb and footway have to go: the ramp and the mainline are still lapped together there and
    // a full verge on either of them would run straight through the other's carriageway. `vergeFade` closes the
    // verge down to nothing at the fork and opens it again at the nose. See Network.js forkFades().
    const fade = options.vergeFade;
    const fL = fade ? clamp(fade(cum[i], 'left'), 0, 1) : 1;
    const fR = fade ? clamp(fade(cum[i], 'right'), 0, 1) : 1;
    const chL = profile.curbHeight * (drop ? drop(cum[i], 'left') : 1) * fL;
    const chR = profile.curbHeight * (drop ? drop(cum[i], 'right') : 1) * fR;
    const cwL = cw * fL;
    const cwR = cw * fR;
    const pl = profile.pavementLeft * fL;
    const pr = profile.pavementRight * fR;
    const zpL = profile.curbHeight * fL;
    const zpR = profile.curbHeight * fR;

    sections.push({
      distance: cum[i],
      frame,
      miter,
      base: p,
      center: off(0, profile.crownRise),
      lapped: !!lapAt,
      halfLeft: halfL,
      halfRight: halfR,
      roadLeft: off(halfL, 0),
      roadRight: off(-halfR, 0),
      curbFaceLeft: off(halfL + CURB_BATTER * fL, chL),
      curbFaceRight: off(-(halfR + CURB_BATTER * fR), chR),
      curbBackLeft: off(halfL + cwL, chL),
      curbBackRight: off(-(halfR + cwR), chR),
      // The footway keeps its own level and tips down into the crossing, which is what makes the dip read.
      paveLeft: off(halfL + cwL + pl, zpL + pl * crossfall),
      paveRight: off(-(halfR + cwR + pr), zpR + pr * crossfall),
      paveLeftBase: off(halfL + cwL + pl, 0),
      paveRightBase: off(-(halfR + cwR + pr), 0),
      // Cambered carriageway samples, left → right.
      lanePoints(columns) {
        const out = [];
        for (let c = 0; c <= columns; c++) {
          // Interpolate between whatever the two edges are: on a lapped station that is a narrow sliver of the
          // full carriageway, but the camber is still sampled from the road's own crown so the two surfaces meet
          // flush instead of stepping.
          const lateral = halfL + ((-halfR - halfL) * c) / columns;
          const u = clamp(lateral / rh, -1, 1);
          out.push(off(lateral, profile.crownRise * (1 - u * u)));
        }
        return out;
      },
    });
  }
  return sections;
}

export function sectionsForEdge(graph, edge, options = {}) {
  const startTrim = trimForNode(graph, edge.startNodeId);
  const endTrim = trimForNode(graph, edge.endNodeId);
  const trimmed = trimPolyline(edge.points, startTrim, endTrim);
  if (trimmed.length < 2) return null;
  return buildCrossSections(trimmed, edge.profile, options);
}

export function buildSegmentMesh(graph, edge, out, options = {}) {
  const sections = options.sections || sectionsForEdge(graph, edge, options);
  if (!sections || sections.length < 2) return null;

  const profile = edge.profile;
  const isBridge = edge.family === 'bridge';
  const columns = clamp(Math.max(2, (profile.lanes || 2) * 2), 2, 16);

  const roadGroup = options.roadGroup || 'road';
  const road = out[roadGroup] || (out[roadGroup] = new MeshSpec(roadGroup));
  const curb = out.curb || (out.curb = new MeshSpec('curb'));
  const paveGroup = options.paveGroup || 'pavement';
  const pavement = out[paveGroup] || (out[paveGroup] = new MeshSpec(paveGroup));

  const vRoadLeft = profile.roadWidth;
  const curbDiag = Math.hypot(profile.curbWidth, profile.curbHeight);

  for (let i = 0; i < sections.length - 1; i++) {
    const a = sections[i];
    const b = sections[i + 1];
    const u0 = a.distance;
    const u1 = b.distance;

    // ── carriageway ──
    const ra = a.lanePoints(columns);
    const rb = b.lanePoints(columns);
    for (let c = 0; c < columns; c++) {
      const v0 = (c / columns) * vRoadLeft;
      const v1 = ((c + 1) / columns) * vRoadLeft;
      road.addFace(
        [ra[c], rb[c], rb[c + 1], ra[c + 1]],
        [
          { x: u0, y: v0 },
          { x: u1, y: v0 },
          { x: u1, y: v1 },
          { x: u0, y: v1 },
        ],
      );
    }

    // ── curbs: gutter face + top, both sides ──
    curb.addFace(
      [a.roadLeft, b.roadLeft, b.curbFaceLeft, a.curbFaceLeft],
      uvStrip(u0, u1, 0, profile.curbHeight),
    );
    curb.addFace(
      [a.curbFaceLeft, b.curbFaceLeft, b.curbBackLeft, a.curbBackLeft],
      uvStrip(u0, u1, profile.curbHeight, profile.curbHeight + curbDiag),
    );
    curb.addFace(
      [a.curbFaceRight, b.curbFaceRight, b.roadRight, a.roadRight],
      uvStrip(u0, u1, profile.curbHeight, 0),
    );
    curb.addFace(
      [a.curbBackRight, b.curbBackRight, b.curbFaceRight, a.curbFaceRight],
      uvStrip(u0, u1, profile.curbHeight + curbDiag, profile.curbHeight),
    );

    // ── pavement tops ──
    if (profile.pavementLeft > 1e-3) {
      pavement.addFace(
        [a.curbBackLeft, b.curbBackLeft, b.paveLeft, a.paveLeft],
        uvStrip(u0, u1, 0, profile.pavementLeft),
      );
    }
    if (profile.pavementRight > 1e-3) {
      pavement.addFace(
        [a.paveRight, b.paveRight, b.curbBackRight, a.curbBackRight],
        uvStrip(u0, u1, profile.pavementRight, 0),
      );
    }

    // ── outer drop (skipped on bridges: the deck edge replaces it) ──
    if (!isBridge) {
      pavement.addFace([a.paveLeft, b.paveLeft, b.paveLeftBase, a.paveLeftBase], uvStrip(u0, u1, 0, profile.curbHeight));
      pavement.addFace([a.paveRightBase, b.paveRightBase, b.paveRight, a.paveRight], uvStrip(u0, u1, profile.curbHeight, 0));
    }
  }

  // ── end caps for dangling corridor ends ──
  const capStart = shouldCap(graph, edge, edge.startNodeId);
  const capEnd = shouldCap(graph, edge, edge.endNodeId);
  if (capStart) capSection(road, pavement, sections[0], columns, true);
  if (capEnd) capSection(road, pavement, sections[sections.length - 1], columns, false);

  return sections;
}

function shouldCap(graph, edge, nodeId) {
  const node = graph.nodes.get(nodeId);
  if (!node) return false;
  if (edge.capMode === 'none') return false;
  if (edge.startNodeId === edge.endNodeId) return false;
  return node.degree <= 1 && !nodeGeneratesJunction(graph, node);
}

function capSection(road, pavement, s, columns, flip) {
  const ring = [s.paveRightBase, s.paveRight, s.curbBackRight, s.curbFaceRight, s.roadRight, ...s.lanePoints(columns).slice().reverse(), s.roadLeft, s.curbFaceLeft, s.curbBackLeft, s.paveLeft, s.paveLeftBase];
  const poly = flip ? ring.slice().reverse() : ring;
  road.addPolygon(poly);
  void pavement;
}

const uvStrip = (u0, u1, v0, v1) => [
  { x: u0, y: v0 },
  { x: u1, y: v0 },
  { x: u1, y: v1 },
  { x: u0, y: v1 },
];

// ── lane markings ─────────────────────────────────────────────────────────────────────────────────────────────────
// Painted as separate ribbons floating 12 mm over the carriageway so they can be toggled and exported on their own.

export function buildMarkings(sections, profile, spec, options = {}) {
  if (!sections || sections.length < 2) return;
  const lanes = Math.max(1, profile.lanes | 0);
  // One-way: no centre line to cross, so every lane boundary is a plain dashed divider.
  const oneWay = options.flow === 'one-way' || options.flow === 'one-way-reverse';
  const lift = options.lift ?? 0.012;
  const width = options.width ?? 0.14;
  const dashOn = options.dashOn ?? 3.0;
  const dashOff = options.dashOff ?? 5.0;

  // Edge lines. Where a slip road is lapped alongside (the verge has folded away for the gore) the nearside line
  // is not the edge of the road any more, it is the boundary of the exit lane — so it has to break. An unbroken
  // white line past a diverge says "you may not leave here", which is exactly the wrong instruction.
  const fade = options.vergeFade;
  const exitSide = (side) => (fade ? (d) => fade(d, side) < 0.995 : null);
  drawLine(spec, sections, profile, profile.roadHalf - 0.35, width, lift, null, { when: exitSide('left'), dash: { on: 4.0, off: 2.0 } });
  drawLine(spec, sections, profile, -(profile.roadHalf - 0.35), width, lift, null, { when: exitSide('right'), dash: { on: 4.0, off: 2.0 } });

  if (lanes >= 2) {
    const laneWidth = profile.roadWidth / lanes;
    // Centre: double line for 4+ lanes, single for 2.
    if (!oneWay && lanes % 2 === 0) {
      if (lanes >= 4) {
        drawLine(spec, sections, profile, 0.12, width, lift, null);
        drawLine(spec, sections, profile, -0.12, width, lift, null);
      } else {
        drawLine(spec, sections, profile, 0, width, lift, null);
      }
    }
    for (let i = 1; i < lanes; i++) {
      const offset = profile.roadHalf - i * laneWidth;
      if (!oneWay && Math.abs(offset) < 0.35) continue;
      drawLine(spec, sections, profile, offset, width, lift, { on: dashOn, off: dashOff });
    }
  }
}

function drawLine(spec, sections, profile, lateral, width, lift, dash, override = null) {
  const half = width * 0.5;
  const pointAt = (s, off) => {
    const scaled = off * s.miter;
    const crownU = clamp(off / Math.max(profile.roadHalf, 1e-3), -1, 1);
    const rise = profile.crownRise * (1 - crownU * crownU) + lift;
    return { ...addScaled(s.base, s.frame.left, scaled), z: s.base.z + rise };
  };

  let run = 0;
  for (let i = 0; i < sections.length - 1; i++) {
    const a = sections[i];
    const b = sections[i + 1];
    const segLen = b.distance - a.distance;
    const pattern = override?.when && override.when(a.distance) ? override.dash : dash;
    if (pattern) {
      const period = pattern.on + pattern.off;
      const phase = run % period;
      run += segLen;
      if (phase > pattern.on) continue;
    } else {
      run += segLen;
    }
    spec.addFace([
      pointAt(a, lateral + half),
      pointAt(b, lateral + half),
      pointAt(b, lateral - half),
      pointAt(a, lateral - half),
    ]);
  }
}

export { resamplePolyline, add, sub, norm, dist, vec };
