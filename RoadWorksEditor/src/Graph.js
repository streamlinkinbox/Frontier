//============================================================================================================================================
//                                                                GRAPH.JS
//============================================================================================================================================
// Network topology: corridors in, a merged junction graph out.
//
// This is the part of the problem the Unreal TransitArchitect plugin gets right and the browser reference got wrong,
// so the *algorithm* is reproduced here (not the code): sample every corridor to a polyline, find all planar segment
// crossings with a uniform grid broad-phase, record split markers per segment, force every corridor that passes near a
// detected node to split there as well, then weld markers inside a merge radius into shared junction nodes. Because the
// corridors end up sharing one node position — and each junction's corner radius is derived from the widths and the
// angular gaps of everything meeting there — the approach trims always line up and the intersection actually merges
// instead of leaving T-junction overlaps.
//
// Crossings are only merged when the two corridors are at a similar elevation; anything separated vertically becomes a
// grade separation (an overpass) and is left for the bridge generator.

import { clamp, dist, distXY, lerp, norm, sub, vec } from './Vec.js';
import { dedupe, polylineLength } from './Polyline.js';
import { sampleSpline } from './Spline.js';
import { resolveProfile } from './Profiles.js';

export const GRAPH_DEFAULTS = {
  sampleStep: 2.0, // m between polyline samples
  gridCell: 24.0, // m broad-phase cell
  nodeMergeXY: 2.0, // m — markers closer than this weld into one node
  zMerge: 1.5, // m — above this a crossing is a grade separation, not a junction
  minEdgeLength: 1.5, // m
  minCornerRadius: 2.0,
  maxCornerRadius: 26.0,
  cornerScale: 1.0, // inspector multiplier on every computed junction radius
};

export function sampleCorridors(corridors, settings = {}) {
  const cfg = { ...GRAPH_DEFAULTS, ...settings };
  const out = [];
  for (const corridor of corridors) {
    if (!corridor.visible && corridor.visible !== undefined) continue;
    if (corridor.points.length < 2) continue;
    let points = sampleSpline(corridor.points, {
      closed: !!corridor.closed,
      tension: corridor.tension || 0,
      step: cfg.sampleStep,
    });
    points = dedupe(points, 1e-3);
    if (points.length < 2) continue;
    if (polylineLength(points) < cfg.minEdgeLength) continue;

    out.push({
      sourceId: corridor.id,
      key: corridor.id,
      name: corridor.name,
      profile: resolveProfile(corridor),
      family: corridor.family || 'road',
      capMode: corridor.capMode || 'flat',
      radiusBias: corridor.radiusBias || 0,
      cyclic: !!corridor.closed,
      bridge: corridor.bridge,
      points,
    });
  }
  return out;
}

// ── planar segment intersection ───────────────────────────────────────────────────────────────────────────────────

export function segmentIntersection2D(a0, a1, b0, b1) {
  const EPS = 1e-6;
  const rx = a1.x - a0.x;
  const ry = a1.y - a0.y;
  const sx = b1.x - b0.x;
  const sy = b1.y - b0.y;
  const denom = rx * sy - ry * sx;
  const dx = b0.x - a0.x;
  const dy = b0.y - a0.y;
  if (Math.abs(denom) <= EPS) return null;

  const t = (dx * sy - dy * sx) / denom;
  const u = (dx * ry - dy * rx) / denom;
  if (t < -EPS || t > 1 + EPS || u < -EPS || u > 1 + EPS) return null;

  const tc = clamp(t, 0, 1);
  const uc = clamp(u, 0, 1);
  return {
    t: tc,
    u: uc,
    point: lerp(a0, a1, tc),
    zA: a0.z + (a1.z - a0.z) * tc,
    zB: b0.z + (b1.z - b0.z) * uc,
  };
}

// ── split markers ─────────────────────────────────────────────────────────────────────────────────────────────────

function addMarker(splitMap, key, segIndex, t, isNode) {
  let perSpline = splitMap.get(key);
  if (!perSpline) splitMap.set(key, (perSpline = new Map()));
  let markers = perSpline.get(segIndex);
  if (!markers) perSpline.set(segIndex, (markers = []));
  for (const m of markers) {
    if (Math.abs(m.t - t) <= 1e-6) {
      m.isNode = m.isNode || isNode;
      return;
    }
  }
  markers.push({ t, isNode });
}

export function buildSplitRecords(samples, cfg) {
  const splitMap = new Map();

  for (const s of samples) {
    const segCount = s.points.length - 1;
    for (let i = 0; i < segCount; i++) {
      addMarker(splitMap, s.key, i, 0, i === 0 && !s.cyclic);
      addMarker(splitMap, s.key, i, 1, i === segCount - 1 && !s.cyclic);
    }
  }

  // flat segment list + uniform grid broad phase
  const segments = [];
  for (let si = 0; si < samples.length; si++) {
    const pts = samples[si].points;
    for (let i = 0; i < pts.length - 1; i++) segments.push({ sampleIndex: si, segIndex: i, p0: pts[i], p1: pts[i + 1] });
  }

  const grid = new Map();
  const cellKey = (x, y) => `${Math.floor(x / cfg.gridCell)}:${Math.floor(y / cfg.gridCell)}`;
  for (let i = 0; i < segments.length; i++) {
    const { p0, p1 } = segments[i];
    const x0 = Math.floor(Math.min(p0.x, p1.x) / cfg.gridCell);
    const x1 = Math.floor(Math.max(p0.x, p1.x) / cfg.gridCell);
    const y0 = Math.floor(Math.min(p0.y, p1.y) / cfg.gridCell);
    const y1 = Math.floor(Math.max(p0.y, p1.y) / cfg.gridCell);
    for (let cx = x0; cx <= x1; cx++) {
      for (let cy = y0; cy <= y1; cy++) {
        const k = `${cx}:${cy}`;
        let bucket = grid.get(k);
        if (!bucket) grid.set(k, (bucket = []));
        bucket.push(i);
      }
    }
  }
  void cellKey;

  const tested = new Set();
  const crossings = [];
  for (const bucket of grid.values()) {
    for (let a = 0; a < bucket.length; a++) {
      for (let b = a + 1; b < bucket.length; b++) {
        const li = Math.min(bucket[a], bucket[b]);
        const ri = Math.max(bucket[a], bucket[b]);
        const pairKey = li * 1e7 + ri;
        if (tested.has(pairKey)) continue;
        tested.add(pairKey);

        const L = segments[li];
        const R = segments[ri];
        const sameSpline = samples[L.sampleIndex].key === samples[R.sampleIndex].key;
        if (sameSpline && Math.abs(L.segIndex - R.segIndex) <= 3) continue;

        const hit = segmentIntersection2D(L.p0, L.p1, R.p0, R.p1);
        if (!hit) continue;
        if (Math.abs(hit.zA - hit.zB) > cfg.zMerge) {
          crossings.push({ point: hit.point, zA: hit.zA, zB: hit.zB, separated: true });
          continue; // grade separation
        }
        if (sameSpline && Math.abs(L.segIndex - R.segIndex) <= 8) {
          if (hit.t < 0.1 || hit.t > 0.9 || hit.u < 0.1 || hit.u > 0.9) continue;
        }

        addMarker(splitMap, samples[L.sampleIndex].key, L.segIndex, hit.t, true);
        addMarker(splitMap, samples[R.sampleIndex].key, R.segIndex, hit.u, true);
        crossings.push({ point: hit.point, separated: false });
      }
    }
  }

  ensureSplitAtNodes(samples, splitMap, cfg);
  return { splitMap, crossings };
}

// Corridors that merely *graze* a detected node (shared endpoints, near-misses from sampling) must also be split
// there, otherwise one arm of a T-junction keeps running through the intersection and the merge visibly fails.
function ensureSplitAtNodes(samples, splitMap, cfg) {
  const nodeLocations = [];
  const seen = new Set();
  for (const s of samples) {
    const perSpline = splitMap.get(s.key);
    if (!perSpline) continue;
    for (const [segIndex, markers] of perSpline) {
      for (const m of markers) {
        if (!m.isNode) continue;
        const pt = lerp(s.points[segIndex], s.points[segIndex + 1], m.t);
        const qk = `${Math.round(pt.x * 2)}|${Math.round(pt.y * 2)}|${Math.round(pt.z * 2)}`;
        if (seen.has(qk)) continue;
        seen.add(qk);
        nodeLocations.push(pt);
      }
    }
  }
  if (!nodeLocations.length) return;

  for (const s of samples) {
    const segCount = s.points.length - 1;
    for (const nodeCo of nodeLocations) {
      let already = false;
      const perSpline = splitMap.get(s.key);
      if (perSpline) {
        for (const [segIndex, markers] of perSpline) {
          for (const m of markers) {
            if (!m.isNode) continue;
            const pt = lerp(s.points[segIndex], s.points[segIndex + 1], m.t);
            if (distXY(pt, nodeCo) <= cfg.nodeMergeXY && Math.abs(pt.z - nodeCo.z) <= cfg.zMerge) {
              already = true;
              break;
            }
          }
          if (already) break;
        }
      }
      if (already) continue;

      let bestSeg = -1;
      let bestT = 0;
      let bestDist = Infinity;
      for (let i = 0; i < segCount; i++) {
        const p0 = s.points[i];
        const p1 = s.points[i + 1];
        const d = sub(p1, p0);
        const lenSq = d.x * d.x + d.y * d.y + d.z * d.z;
        if (lenSq < 1e-6) continue;
        const t = clamp(((nodeCo.x - p0.x) * d.x + (nodeCo.y - p0.y) * d.y + (nodeCo.z - p0.z) * d.z) / lenSq, 0, 1);
        const proj = lerp(p0, p1, t);
        const dd = distXY(proj, nodeCo);
        if (dd < bestDist) {
          bestDist = dd;
          bestSeg = i;
          bestT = t;
        }
      }
      if (bestSeg >= 0 && bestDist < cfg.nodeMergeXY) {
        const p0 = s.points[bestSeg];
        const p1 = s.points[bestSeg + 1];
        const projZ = p0.z + (p1.z - p0.z) * bestT;
        if (Math.abs(projZ - nodeCo.z) <= cfg.zMerge) addMarker(splitMap, s.key, bestSeg, bestT, true);
      }
    }
  }
}

function recordsFromSplits(sample, splitMap) {
  const records = [];
  const perSpline = splitMap.get(sample.key);
  if (!perSpline) return records;

  for (let segIndex = 0; segIndex < sample.points.length - 1; segIndex++) {
    const markers = perSpline.get(segIndex);
    if (!markers) continue;
    const sorted = [...markers].sort((a, b) => a.t - b.t);
    const pa = sample.points[segIndex];
    const pb = sample.points[segIndex + 1];
    for (const m of sorted) {
      const pt = lerp(pa, pb, m.t);
      const last = records[records.length - 1];
      if (last && dist(last.co, pt) <= 1e-3) {
        last.isNode = last.isNode || m.isNode;
        continue;
      }
      records.push({ co: pt, isNode: m.isNode });
    }
  }

  if (records.length) {
    if (!sample.cyclic) {
      records[0].isNode = true;
      records[records.length - 1].isNode = true;
    } else if (!records.some((r) => r.isNode)) {
      records[0].isNode = true;
      if (dist(records[0].co, records[records.length - 1].co) <= 0.01) records[records.length - 1].isNode = true;
      else records.push({ co: { ...records[0].co }, isNode: true });
    }
  }
  return records;
}

// ── node welding + edge extraction ────────────────────────────────────────────────────────────────────────────────

function findOrCreateNode(graph, co, cfg) {
  for (const node of graph.nodes.values()) {
    if (Math.abs(node.co.z - co.z) > cfg.zMerge) continue;
    if (distXY(node.co, co) <= cfg.nodeMergeXY) return node.id;
  }
  const id = `N${graph.nodes.size.toString().padStart(3, '0')}_${Math.round(co.x)}_${Math.round(co.y)}`;
  graph.nodes.set(id, {
    id,
    co: { ...co },
    edgeIds: [],
    sources: new Set(),
    degree: 0,
    cornerRadius: cfg.minCornerRadius,
    enabled: true,
  });
  return id;
}

export function buildGraph(corridors, settings = {}) {
  const cfg = { ...GRAPH_DEFAULTS, ...settings };
  const samples = sampleCorridors(corridors, cfg);
  const graph = { nodes: new Map(), edges: new Map(), crossings: [], warnings: [] };
  if (!samples.length) return graph;

  const { splitMap, crossings } = buildSplitRecords(samples, cfg);
  graph.crossings = crossings;

  for (const sample of samples) {
    const records = recordsFromSplits(sample, splitMap);
    if (records.length < 2) continue;

    let edgeCounter = 0;
    let current = 0;
    while (current < records.length) {
      if (!records[current].isNode) {
        current++;
        continue;
      }
      const startNodeId = findOrCreateNode(graph, records[current].co, cfg);
      const polyline = [records[current].co];
      let search = current + 1;
      let closedEdge = false;

      while (search < records.length) {
        polyline.push(records[search].co);
        if (records[search].isNode) {
          const endNodeId = findOrCreateNode(graph, records[search].co, cfg);
          const deduped = dedupe(polyline, 1e-3);
          if (deduped.length >= 2 && polylineLength(deduped) > cfg.minEdgeLength) {
            const edgeId = `${sample.key}:E${edgeCounter.toString().padStart(2, '0')}`;
            graph.edges.set(edgeId, {
              id: edgeId,
              sourceId: sample.sourceId,
              name: sample.name,
              points: deduped,
              startNodeId,
              endNodeId,
              profile: sample.profile,
              family: sample.family,
              capMode: sample.capMode,
              radiusBias: sample.radiusBias,
              bridge: sample.bridge,
            });
            const sn = graph.nodes.get(startNodeId);
            const en = graph.nodes.get(endNodeId);
            sn.edgeIds.push(edgeId);
            sn.sources.add(sample.sourceId);
            en.edgeIds.push(edgeId);
            en.sources.add(sample.sourceId);
            edgeCounter++;
          }
          current = search;
          closedEdge = true;
          break;
        }
        search++;
      }
      if (!closedEdge) break;
    }
  }

  for (const node of graph.nodes.values()) node.degree = node.edgeIds.length;
  for (const node of graph.nodes.values()) node.cornerRadius = defaultCornerRadius(graph, node, cfg);

  return graph;
}

// ── junction sizing ───────────────────────────────────────────────────────────────────────────────────────────────

export function edgeTangentAwayFromNode(edge, nodeId, forceEnd = false) {
  const pts = edge.points;
  const t = nodeId === edge.startNodeId && !forceEnd ? sub(pts[1], pts[0]) : sub(pts[pts.length - 2], pts[pts.length - 1]);
  return norm(t, vec(1, 0, 0));
}

// The corner radius is the distance each arm is trimmed back from the node. It must be large enough that the widest
// pair of neighbouring arms can be joined by a fillet without the trimmed stubs overlapping — that requirement is what
// makes acute Y-junctions merge cleanly.
export function defaultCornerRadius(graph, node, cfg = GRAPH_DEFAULTS) {
  if (node.degree < 2) return cfg.minCornerRadius;

  const approaches = [];
  const seen = new Set();
  for (const edgeId of node.edgeIds) {
    const edge = graph.edges.get(edgeId);
    if (!edge) continue;
    const isLoop = edge.startNodeId === edge.endNodeId;
    const forceEnd = isLoop && seen.has(edgeId);
    seen.add(edgeId);
    const tangent = edgeTangentAwayFromNode(edge, node.id, forceEnd);
    const p = edge.profile;
    approaches.push({
      angle: Math.atan2(tangent.y, tangent.x),
      halfWidth: p.roadHalf + p.curbWidth + Math.max(p.pavementLeft, p.pavementRight),
      bias: edge.radiusBias || 0,
      length: polylineLength(edge.points),
    });
  }
  if (!approaches.length) return cfg.minCornerRadius;
  approaches.sort((a, b) => a.angle - b.angle);

  let radius = cfg.minCornerRadius;
  for (let i = 0; i < approaches.length; i++) {
    const cur = approaches[i];
    const nxt = approaches[(i + 1) % approaches.length];
    let delta = nxt.angle - cur.angle;
    while (delta < 0) delta += Math.PI * 2;
    if (delta < 0.1) continue;
    const wMax = Math.max(cur.halfWidth, nxt.halfWidth);
    const fillet = Math.max(cur.bias, nxt.bias) + 1.5;
    radius = Math.max(radius, wMax / Math.max(0.1, Math.sin(delta * 0.5)) + fillet);
  }

  radius *= cfg.cornerScale ?? 1;

  // Never eat more than 45% of the shortest arm, otherwise short blocks vanish.
  const shortest = Math.min(...approaches.map((a) => a.length));
  radius = Math.min(radius, Math.max(cfg.minCornerRadius, shortest * 0.45));
  return clamp(radius, cfg.minCornerRadius, cfg.maxCornerRadius);
}

export function nodeGeneratesJunction(graph, node) {
  if (!node.enabled) return false;
  if (node.degree < 2) return false;
  if (node.degree >= 3) return true;
  const a = graph.edges.get(node.edgeIds[0]);
  const b = graph.edges.get(node.edgeIds[1]);
  if (!a || !b) return false;
  const ta = edgeTangentAwayFromNode(a, node.id);
  const forceEnd = b.startNodeId === b.endNodeId && node.edgeIds[0] === node.edgeIds[1];
  const tb = edgeTangentAwayFromNode(b, node.id, forceEnd);
  const angle = (Math.acos(clamp(ta.x * tb.x + ta.y * tb.y + ta.z * tb.z, -1, 1)) * 180) / Math.PI;
  // Two arms meeting almost head-on are a continuation, not an intersection.
  return angle < 150 && a.sourceId !== b.sourceId;
}

export function trimForNode(graph, nodeId) {
  const node = graph.nodes.get(nodeId);
  if (!node) return 0;
  return nodeGeneratesJunction(graph, node) ? node.cornerRadius : 0;
}
