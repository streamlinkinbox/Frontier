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

import { add, addScaled, clamp, dist, norm, sub, vec } from './Vec.js?v=6';
import { cumulativeLengths, frameAt, miterScale, resamplePolyline, trimPolyline } from './Polyline.js?v=6';
import { MeshSpec } from './MeshSpec.js?v=6';
import { nodeGeneratesJunction, trimForNode } from './Graph.js?v=6';

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
    const cw = profile.curbWidth;
    // Vehicle crossovers drop the kerb almost flush over the width of the crossing — see Driveways.js. The factor
    // is per side, so a drive on one frontage never flattens the kerb opposite it.
    const drop = options.kerbDrop;
    const chL = profile.curbHeight * (drop ? drop(cum[i], 'left') : 1);
    const chR = profile.curbHeight * (drop ? drop(cum[i], 'right') : 1);
    const ch = profile.curbHeight;
    const pl = profile.pavementLeft;
    const pr = profile.pavementRight;

    sections.push({
      distance: cum[i],
      frame,
      miter,
      base: p,
      center: off(0, profile.crownRise),
      roadLeft: off(rh, 0),
      roadRight: off(-rh, 0),
      curbFaceLeft: off(rh + CURB_BATTER, chL),
      curbFaceRight: off(-(rh + CURB_BATTER), chR),
      curbBackLeft: off(rh + cw, chL),
      curbBackRight: off(-(rh + cw), chR),
      // The footway keeps its own level and tips down into the crossing, which is what makes the dip read.
      paveLeft: off(rh + cw + pl, ch + pl * crossfall),
      paveRight: off(-(rh + cw + pr), ch + pr * crossfall),
      paveLeftBase: off(rh + cw + pl, 0),
      paveRightBase: off(-(rh + cw + pr), 0),
      // Cambered carriageway samples, left → right.
      lanePoints(columns) {
        const out = [];
        for (let c = 0; c <= columns; c++) {
          const u = 1 - (2 * c) / columns; // +1 left edge → -1 right edge
          out.push(off(u * rh, profile.crownRise * (1 - u * u)));
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
  const lift = options.lift ?? 0.012;
  const width = options.width ?? 0.14;
  const dashOn = options.dashOn ?? 3.0;
  const dashOff = options.dashOff ?? 5.0;

  // Edge lines
  drawLine(spec, sections, profile, profile.roadHalf - 0.35, width, lift, null);
  drawLine(spec, sections, profile, -(profile.roadHalf - 0.35), width, lift, null);

  if (lanes >= 2) {
    const laneWidth = profile.roadWidth / lanes;
    // Centre: double line for 4+ lanes, single for 2.
    if (lanes % 2 === 0) {
      if (lanes >= 4) {
        drawLine(spec, sections, profile, 0.12, width, lift, null);
        drawLine(spec, sections, profile, -0.12, width, lift, null);
      } else {
        drawLine(spec, sections, profile, 0, width, lift, null);
      }
    }
    for (let i = 1; i < lanes; i++) {
      const offset = profile.roadHalf - i * laneWidth;
      if (Math.abs(offset) < 0.35) continue;
      drawLine(spec, sections, profile, offset, width, lift, { on: dashOn, off: dashOff });
    }
  }
}

function drawLine(spec, sections, profile, lateral, width, lift, dash) {
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
    if (dash) {
      const period = dash.on + dash.off;
      const phase = run % period;
      run += segLen;
      if (phase > dash.on) continue;
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
