//============================================================================================================================================
//                                                             JUNCTIONMESH.JS
//============================================================================================================================================
// Junction surfacing: fillet arcs between neighbouring arms, Coons patches for the apron, and offset bands that carry
// the curb and pavement continuously around every corner.
//
// Two things make the merge watertight here:
//   1. Approach frames are *taken from* the trimmed corridor cross-sections rather than recomputed, so the junction
//      boundary ring and the corridor end ring share identical vertices — no seam, no z-fighting sliver.
//   2. The curb face, curb back and pavement edges around a corner are generated as successive offsets of the road
//      fillet, each pinned to the exact arm targets at both ends, so the curb never detaches from the arc on acute
//      or uneven-width junctions.

import { clamp, dist, lerp, vec } from './Vec.js';
import { resamplePolyline } from './Polyline.js';
import { MeshSpec } from './MeshSpec.js';
import { nodeGeneratesJunction } from './Graph.js';

// ── fillet arc between two offset edges ───────────────────────────────────────────────────────────────────────────

export function filletBetweenEdges(pRight, tangentRight, pLeft, tangentLeft, radius, steps = 10) {
  const zStart = pRight.z;
  const zEnd = pLeft.z;
  const fallback = () => [
    { x: pRight.x, y: pRight.y, z: zStart },
    { x: pLeft.x, y: pLeft.y, z: zEnd },
  ];

  const pa = { x: pRight.x, y: pRight.y };
  const pb = { x: pLeft.x, y: pLeft.y };
  let da = { x: tangentRight.x, y: tangentRight.y };
  let db = { x: tangentLeft.x, y: tangentLeft.y };
  const la = Math.hypot(da.x, da.y);
  const lb = Math.hypot(db.x, db.y);
  if (la < 1e-4 || lb < 1e-4) return fallback();
  da = { x: da.x / la, y: da.y / la };
  db = { x: db.x / lb, y: db.y / lb };

  const denom = da.x * db.y - da.y * db.x;
  if (Math.abs(denom) < 1e-4) return fallback();

  const dx = pb.x - pa.x;
  const dy = pb.y - pa.y;
  const t = (dx * db.y - dy * db.x) / denom;
  const v = { x: pa.x + da.x * t, y: pa.y + da.y * t };

  if ((v.x - pa.x) * da.x + (v.y - pa.y) * da.y < -0.05) return fallback();
  if ((v.x - pb.x) * db.x + (v.y - pb.y) * db.y < -0.05) return fallback();

  const va = { x: -da.x, y: -da.y };
  const vb = { x: -db.x, y: -db.y };
  const dA = Math.hypot(pa.x - v.x, pa.y - v.y);
  const dB = Math.hypot(pb.x - v.x, pb.y - v.y);

  const theta = Math.acos(clamp(va.x * vb.x + va.y * vb.y, -1, 1));
  if (theta < 0.02 || theta > Math.PI - 0.02) return fallback();

  const alpha = theta * 0.5;
  const tanA = Math.tan(alpha);
  const sinA = Math.sin(alpha);
  if (tanA < 1e-3 || sinA < 1e-3) return fallback();

  let r = radius;
  let tLen = r / tanA;
  const tMax = Math.min(Math.max(dA, 0), Math.max(dB, 0)) * 0.95;
  if (tLen > tMax) {
    tLen = tMax;
    r = tLen * tanA;
  }

  const tpa = { x: v.x + va.x * tLen, y: v.y + va.y * tLen };
  const tpb = { x: v.x + vb.x * tLen, y: v.y + vb.y * tLen };
  let bdir = { x: va.x + vb.x, y: va.y + vb.y };
  const bl = Math.hypot(bdir.x, bdir.y);
  if (bl < 1e-4) return fallback();
  bdir = { x: bdir.x / bl, y: bdir.y / bl };

  const c = { x: v.x + bdir.x * (r / sinA), y: v.y + bdir.y * (r / sinA) };
  const aStart = Math.atan2(tpa.y - c.y, tpa.x - c.x);
  const aEnd = Math.atan2(tpb.y - c.y, tpb.x - c.x);
  let delta = aEnd - aStart;
  while (delta > Math.PI) delta -= Math.PI * 2;
  while (delta < -Math.PI) delta += Math.PI * 2;

  const pts = [{ x: pRight.x, y: pRight.y, z: zStart }];
  for (let i = 0; i <= steps; i++) {
    const frac = i / steps;
    const ang = aStart + delta * frac;
    pts.push({ x: c.x + Math.cos(ang) * r, y: c.y + Math.sin(ang) * r, z: zStart + (zEnd - zStart) * frac });
  }
  pts.push({ x: pLeft.x, y: pLeft.y, z: zEnd });
  return pts;
}

// ── offsets pinned to arm targets ─────────────────────────────────────────────────────────────────────────────────

function tangentXY(points, index) {
  let d;
  if (points.length < 2) return { x: 1, y: 0 };
  if (index <= 0) d = { x: points[1].x - points[0].x, y: points[1].y - points[0].y };
  else if (index >= points.length - 1)
    d = { x: points[points.length - 1].x - points[points.length - 2].x, y: points[points.length - 1].y - points[points.length - 2].y };
  else d = { x: points[index + 1].x - points[index - 1].x, y: points[index + 1].y - points[index - 1].y };
  const l = Math.hypot(d.x, d.y);
  if (l < 1e-6) return { x: 1, y: 0 };
  return { x: d.x / l, y: d.y / l };
}

export function offsetCurveFromTargets(baseCurve, startTarget, endTarget) {
  if (!baseCurve.length) return [];
  if (baseCurve.length === 1) return [{ ...startTarget }];

  const first = baseCurve[0];
  const last = baseCurve[baseCurve.length - 1];
  const startDelta = { x: startTarget.x - first.x, y: startTarget.y - first.y, z: startTarget.z - first.z };
  const endDelta = { x: endTarget.x - last.x, y: endTarget.y - last.y, z: endTarget.z - last.z };

  const sT = tangentXY(baseCurve, 0);
  const sNormal = { x: -sT.y, y: sT.x };
  const eT = tangentXY(baseCurve, baseCurve.length - 1);
  const eNormal = { x: -eT.y, y: eT.x };

  const posScore = sNormal.x * startDelta.x + sNormal.y * startDelta.y + eNormal.x * endDelta.x + eNormal.y * endDelta.y;
  const negScore = -posScore;
  const sign = posScore >= negScore ? 1 : -1;

  const startWidth = Math.hypot(startDelta.x, startDelta.y);
  const endWidth = Math.hypot(endDelta.x, endDelta.y);
  const total = baseCurve.length - 1;
  const out = [];
  for (let i = 0; i < baseCurve.length; i++) {
    const frac = i / total;
    const t = tangentXY(baseCurve, i);
    const n = { x: -t.y * sign, y: t.x * sign };
    const width = (1 - frac) * startWidth + frac * endWidth;
    const zOff = (1 - frac) * startDelta.z + frac * endDelta.z;
    const p = baseCurve[i];
    out.push({ x: p.x + n.x * width, y: p.y + n.y * width, z: p.z + zOff });
  }
  out[0] = { ...startTarget };
  out[out.length - 1] = { ...endTarget };
  return out;
}

// ── Coons patch ───────────────────────────────────────────────────────────────────────────────────────────────────

export function coonsPatch(bottom, top, left, right) {
  const uSteps = bottom.length - 1;
  const vSteps = left.length - 1;
  const p00 = bottom[0];
  const p10 = bottom[bottom.length - 1];
  const p01 = top[0];
  const p11 = top[top.length - 1];
  const grid = [];
  for (let i = 0; i <= uSteps; i++) {
    const u = i / uSteps;
    const row = [];
    for (let j = 0; j <= vSteps; j++) {
      const v = j / vSteps;
      const bil = {
        x: (1 - u) * (1 - v) * p00.x + u * (1 - v) * p10.x + (1 - u) * v * p01.x + u * v * p11.x,
        y: (1 - u) * (1 - v) * p00.y + u * (1 - v) * p10.y + (1 - u) * v * p01.y + u * v * p11.y,
        z: (1 - u) * (1 - v) * p00.z + u * (1 - v) * p10.z + (1 - u) * v * p01.z + u * v * p11.z,
      };
      row.push({
        x: (1 - v) * bottom[i].x + v * top[i].x + (1 - u) * left[j].x + u * right[j].x - bil.x,
        y: (1 - v) * bottom[i].y + v * top[i].y + (1 - u) * left[j].y + u * right[j].y - bil.y,
        z: (1 - v) * bottom[i].z + v * top[i].z + (1 - u) * left[j].z + u * right[j].z - bil.z,
      });
    }
    grid.push(row);
  }
  return grid;
}

export function addQuadPatch(spec, bottom, top, left, right, uSegments = -1, vSegments = -1) {
  if (bottom.length < 2 || top.length < 2 || left.length < 2 || right.length < 2) return;
  const u = Math.max(1, uSegments >= 0 ? uSegments : Math.max(bottom.length, top.length) - 1);
  const v = Math.max(1, vSegments >= 0 ? vSegments : Math.max(left.length, right.length) - 1);
  const grid = coonsPatch(resamplePolyline(bottom, u), resamplePolyline(top, u), resamplePolyline(left, v), resamplePolyline(right, v));
  for (let i = 0; i < grid.length - 1; i++) {
    for (let j = 0; j < grid[0].length - 1; j++) {
      const a = grid[i][j];
      const b = grid[i + 1][j];
      const c = grid[i + 1][j + 1];
      const d = grid[i][j + 1];
      if (dist(a, b) <= 1e-4 && dist(b, c) <= 1e-4 && dist(c, d) <= 1e-4) continue;
      spec.addFace([a, b, c, d], [uv(a), uv(b), uv(c), uv(d)]);
    }
  }
}

export function addCurveStrip(spec, startCurve, endCurve, segments = -1, rows = 1) {
  if (startCurve.length < 2 || endCurve.length < 2) return;
  const u = Math.max(1, segments >= 0 ? segments : Math.max(startCurve.length, endCurve.length) - 1);
  const a = resamplePolyline(startCurve, u);
  const b = resamplePolyline(endCurve, u);
  const r = Math.max(1, rows);
  for (let i = 0; i < u; i++) {
    for (let k = 0; k < r; k++) {
      const p1 = lerp(a[i], b[i], k / r);
      const p2 = lerp(a[i + 1], b[i + 1], k / r);
      const p3 = lerp(a[i + 1], b[i + 1], (k + 1) / r);
      const p4 = lerp(a[i], b[i], (k + 1) / r);
      if (dist(p1, p2) <= 1e-4 && dist(p2, p3) <= 1e-4) continue;
      spec.addFace([p1, p2, p3, p4], [uv(p1), uv(p2), uv(p3), uv(p4)]);
    }
  }
}

const uv = (p) => ({ x: p.x * 0.25, y: p.y * 0.25 });

// ── approach frames straight off the corridor cross-sections ──────────────────────────────────────────────────────

export function approachForEdge(graph, edge, nodeId, sections, forceEnd = false) {
  if (!sections || sections.length < 2) return null;
  const atStart = nodeId === edge.startNodeId && !forceEnd;
  const s = atStart ? sections[0] : sections[sections.length - 1];
  const tan = s.frame.tangent;
  // Tangent points *into* the junction.
  const tangent = atStart ? { x: -tan.x, y: -tan.y, z: -tan.z } : { ...tan };

  const swap = atStart;
  const frame = {
    edgeId: edge.id,
    profile: edge.profile,
    family: edge.family,
    tangent,
    angle: Math.atan2(tangent.y, tangent.x),
    point: s.center,
    base: s.base,
    roadLeft: swap ? s.roadRight : s.roadLeft,
    roadRight: swap ? s.roadLeft : s.roadRight,
    curbFaceLeft: swap ? s.curbFaceRight : s.curbFaceLeft,
    curbFaceRight: swap ? s.curbFaceLeft : s.curbFaceRight,
    curbBackLeft: swap ? s.curbBackRight : s.curbBackLeft,
    curbBackRight: swap ? s.curbBackLeft : s.curbBackRight,
    paveLeft: swap ? s.paveRight : s.paveLeft,
    paveRight: swap ? s.paveLeft : s.paveRight,
    paveLeftBase: swap ? s.paveRightBase : s.paveLeftBase,
    paveRightBase: swap ? s.paveLeftBase : s.paveRightBase,
  };
  return frame;
}

// ── junction mesh ─────────────────────────────────────────────────────────────────────────────────────────────────

export function buildJunctionMesh(graph, node, sectionsByEdge, out) {
  if (!nodeGeneratesJunction(graph, node)) return null;

  const approaches = [];
  const seen = new Set();
  for (const edgeId of node.edgeIds) {
    const edge = graph.edges.get(edgeId);
    if (!edge) continue;
    const isLoop = edge.startNodeId === edge.endNodeId;
    const forceEnd = isLoop && seen.has(edgeId);
    seen.add(edgeId);
    const frame = approachForEdge(graph, edge, node.id, sectionsByEdge.get(edgeId), forceEnd);
    if (frame) approaches.push(frame);
  }
  if (approaches.length < 2) return null;
  approaches.sort((a, b) => a.angle - b.angle);

  const road = out.road || (out.road = new MeshSpec('road'));
  const curb = out.curb || (out.curb = new MeshSpec('curb'));
  const pavement = out.pavement || (out.pavement = new MeshSpec('pavement'));

  const n = approaches.length;
  const avgZ = approaches.reduce((acc, a) => acc + a.point.z, 0) / n;
  const centre = { x: node.co.x, y: node.co.y, z: avgZ };
  const filletRadius = Math.max(node.cornerRadius * 0.55, 1.6);

  const roadArcs = [];
  const curbFaceArcs = [];
  const curbBackArcs = [];
  const paveArcs = [];
  const paveBaseArcs = [];

  for (let i = 0; i < n; i++) {
    const cur = approaches[i];
    const nxt = approaches[(i + 1) % n];
    let arc = filletBetweenEdges(cur.roadRight, cur.tangent, nxt.roadLeft, nxt.tangent, filletRadius, 10);
    arc = resamplePolyline(arc, Math.max(10, arc.length - 1));
    roadArcs.push(arc);
    const face = offsetCurveFromTargets(arc, cur.curbFaceRight, nxt.curbFaceLeft);
    const back = offsetCurveFromTargets(face, cur.curbBackRight, nxt.curbBackLeft);
    const pave = offsetCurveFromTargets(back, cur.paveRight, nxt.paveLeft);
    const paveBase = offsetCurveFromTargets(pave, cur.paveRightBase, nxt.paveLeftBase);
    curbFaceArcs.push(face);
    curbBackArcs.push(back);
    paveArcs.push(pave);
    paveBaseArcs.push(paveBase);
  }

  // Apron: two Coons patches per corner, hinged on the junction centre.
  for (let i = 0; i < n; i++) {
    const cur = approaches[i];
    const nxt = approaches[(i + 1) % n];
    const arc = roadArcs[i];
    const half = ((arc.length - 1) >> 1) + 1;
    const right1 = arc.slice(0, half);
    const right2 = arc.slice(half - 1);

    {
      const steps = Math.max(1, right1.length - 1);
      const bLeft = [];
      for (let j = 0; j <= steps; j++) {
        const frac = j / steps;
        bLeft.push(lerp(cur.point, centre, 1 - Math.pow(1 - frac, 1.5)));
      }
      addQuadPatch(road, [cur.point, cur.roadRight], [centre, right1[right1.length - 1]], bLeft, right1);
    }
    {
      const steps = Math.max(1, right2.length - 1);
      const bLeft = [];
      for (let j = 0; j <= steps; j++) {
        const frac = j / steps;
        bLeft.push(lerp(centre, nxt.point, Math.pow(frac, 1.5)));
      }
      addQuadPatch(road, [centre, right2[0]], [nxt.point, nxt.roadLeft], bLeft, right2);
    }
  }

  // Curb bands + pavement around every corner.
  for (let i = 0; i < n; i++) {
    const segs = roadArcs[i].length - 1;
    addCurveStrip(curb, roadArcs[i], curbFaceArcs[i], segs, 1);
    addCurveStrip(curb, curbFaceArcs[i], curbBackArcs[i], segs, 1);
    addCurveStrip(pavement, curbBackArcs[i], paveArcs[i], segs, 2);

    const bridgeCorner = approaches[i].family === 'bridge' && approaches[(i + 1) % n].family === 'bridge';
    if (!bridgeCorner) addCurveStrip(pavement, paveArcs[i], paveBaseArcs[i], segs, 1);
  }

  return { approaches, centre, roadArcs, paveArcs };
}
