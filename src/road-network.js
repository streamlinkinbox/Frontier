const EPSILON = 1e-8;
let nextId = Date.now();

export const DEFAULT_PROFILE = Object.freeze({
  laneCount: 2,
  laneWidth: 3.5,
  shoulder: 0.3,
  pavementWidth: 1.8,
  curbWidth: 0.22,
  curbHeight: 0.18,
  pavementEnabled: true,
  markings: true,
});

export function makeId(prefix = 'item') {
  nextId += 1;
  return `${prefix}-${nextId.toString(36)}`;
}

export function distance2(a, b) {
  return Math.hypot(a.x - b.x, a.z - b.z);
}

export function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

export function cubicPoint(p0, p1, p2, p3, t) {
  const mt = 1 - t;
  const mt2 = mt * mt;
  const t2 = t * t;
  return {
    x: mt2 * mt * p0.x + 3 * mt2 * t * p1.x + 3 * mt * t2 * p2.x + t2 * t * p3.x,
    z: mt2 * mt * p0.z + 3 * mt2 * t * p1.z + 3 * mt * t2 * p2.z + t2 * t * p3.z,
  };
}

export function cubicTangent(p0, p1, p2, p3, t) {
  const mt = 1 - t;
  return {
    x: 3 * mt * mt * (p1.x - p0.x) + 6 * mt * t * (p2.x - p1.x) + 3 * t * t * (p3.x - p2.x),
    z: 3 * mt * mt * (p1.z - p0.z) + 6 * mt * t * (p2.z - p1.z) + 3 * t * t * (p3.z - p2.z),
  };
}

export function bridgeHeightAt(edge, t) {
  const bridge = edge.bridge;
  if (!bridge?.enabled) return 0;
  const ramp = clamp(Number(bridge.rampFraction ?? 0.2), 0.06, 0.46);
  const smooth = (value) => {
    const x = clamp(value, 0, 1);
    return x * x * (3 - 2 * x);
  };
  if (t < ramp) return Number(bridge.height ?? 7) * smooth(t / ramp);
  if (t > 1 - ramp) return Number(bridge.height ?? 7) * smooth((1 - t) / ramp);
  return Number(bridge.height ?? 7);
}

export function edgePoints(edge, nodes) {
  const start = nodes[edge.from];
  const end = nodes[edge.to];
  if (!start || !end) return null;
  return [start, edge.c1 ?? start, edge.c2 ?? end, end];
}

export function pointOnEdge(edge, nodes, t) {
  const points = edgePoints(edge, nodes);
  if (!points) return { x: 0, z: 0 };
  return cubicPoint(points[0], points[1], points[2], points[3], clamp(t, 0, 1));
}

export function tangentOnEdge(edge, nodes, t) {
  const points = edgePoints(edge, nodes);
  if (!points) return { x: 1, z: 0 };
  const result = cubicTangent(points[0], points[1], points[2], points[3], clamp(t, 0, 1));
  const length = Math.hypot(result.x, result.z);
  if (length < EPSILON) {
    const fallback = { x: points[3].x - points[0].x, z: points[3].z - points[0].z };
    const fallbackLength = Math.hypot(fallback.x, fallback.z) || 1;
    return { x: fallback.x / fallbackLength, z: fallback.z / fallbackLength };
  }
  return { x: result.x / length, z: result.z / length };
}

export function sampleEdge(edge, nodes, steps = 48) {
  const samples = [];
  const count = Math.max(2, steps);
  let distance = 0;
  let previous = null;
  for (let i = 0; i <= count; i += 1) {
    const t = i / count;
    const point = pointOnEdge(edge, nodes, t);
    const sample = {
      x: point.x,
      z: point.z,
      y: bridgeHeightAt(edge, t),
      t,
      distance,
    };
    if (previous) {
      distance += Math.hypot(sample.x - previous.x, sample.z - previous.z);
      sample.distance = distance;
    }
    samples.push(sample);
    previous = sample;
  }
  return samples;
}

export function edgeLength(edge, nodes, steps = 48) {
  const samples = sampleEdge(edge, nodes, steps);
  return samples.at(-1)?.distance ?? 0;
}

export function pointAtDistance(samples, requestedDistance) {
  if (!samples.length) return { x: 0, y: 0, z: 0, t: 0, distance: 0 };
  const target = clamp(requestedDistance, 0, samples.at(-1).distance);
  for (let i = 1; i < samples.length; i += 1) {
    const b = samples[i];
    if (b.distance >= target) {
      const a = samples[i - 1];
      const span = b.distance - a.distance || 1;
      const f = (target - a.distance) / span;
      return {
        x: a.x + (b.x - a.x) * f,
        y: a.y + (b.y - a.y) * f,
        z: a.z + (b.z - a.z) * f,
        t: a.t + (b.t - a.t) * f,
        distance: target,
      };
    }
  }
  return { ...samples.at(-1), distance: target };
}

export function getDegrees(network) {
  const degrees = Object.fromEntries(Object.keys(network.nodes).map((id) => [id, 0]));
  for (const edge of network.edges) {
    if (degrees[edge.from] !== undefined) degrees[edge.from] += 1;
    if (degrees[edge.to] !== undefined) degrees[edge.to] += 1;
  }
  return degrees;
}

export function getNetworkMetrics(network) {
  const degrees = getDegrees(network);
  const edgeList = network.edges;
  let length = 0;
  for (const edge of edgeList) length += edgeLength(edge, network.nodes, 32);
  return {
    nodes: Object.keys(network.nodes).length,
    roads: edgeList.filter((edge) => !edge.bridge?.enabled).length,
    bridges: edgeList.filter((edge) => edge.bridge?.enabled).length,
    junctions: Object.values(degrees).filter((degree) => degree >= 3).length,
    connections: Object.values(degrees).filter((degree) => degree >= 2).length,
    length,
    degrees,
  };
}

function cloneEdge(edge) {
  return {
    ...edge,
    c1: { ...edge.c1 },
    c2: { ...edge.c2 },
    bridge: edge.bridge ? { ...edge.bridge } : null,
  };
}

function edgeDefaults(profile = DEFAULT_PROFILE) {
  return {
    ...DEFAULT_PROFILE,
    ...profile,
    name: 'New road',
    material: 'asphalt',
    bridge: null,
    autoCurve: true,
    curveTension: 1,
    visible: true,
  };
}

export function createEdge(from, to, nodes, options = {}) {
  const start = nodes[from];
  const end = nodes[to];
  if (!start || !end) throw new Error('Road endpoints must exist before an edge is created.');
  const dx = end.x - start.x;
  const dz = end.z - start.z;
  const { profile, ...properties } = options;
  return {
    id: options.id ?? makeId('road'),
    from,
    to,
    c1: options.c1 ? { ...options.c1 } : { x: start.x + dx / 3, z: start.z + dz / 3 },
    c2: options.c2 ? { ...options.c2 } : { x: start.x + (2 * dx) / 3, z: start.z + (2 * dz) / 3 },
    ...edgeDefaults(profile),
    ...properties,
  };
}

export function createInitialNetwork() {
  const nodes = {
    west: { id: 'west', x: -31, z: 0, name: 'West approach' },
    junction: { id: 'junction', x: 14, z: 0, name: 'Creekline junction' },
    east: { id: 'east', x: 31, z: 0, name: 'East approach' },
    north: { id: 'north', x: 0, z: -29, name: 'North ramp' },
    south: { id: 'south', x: 0, z: 29, name: 'South ramp' },
    spur: { id: 'spur', x: 25, z: 15, name: 'Market street' },
  };
  const profile = { ...DEFAULT_PROFILE };
  const edges = [
    createEdge('west', 'junction', nodes, {
      id: 'road-west', name: 'Creekline arterial · west', c1: { x: -16, z: -0.2 }, c2: { x: 0, z: 0.2 }, profile,
    }),
    createEdge('junction', 'east', nodes, {
      id: 'road-east', name: 'Creekline arterial · east', c1: { x: 19.5, z: -0.05 }, c2: { x: 26, z: 0.12 }, profile,
    }),
    createEdge('junction', 'spur', nodes, {
      id: 'road-spur', name: 'Market street', c1: { x: 17.5, z: 2.3 }, c2: { x: 23.5, z: 8.2 }, profile: { ...profile, laneCount: 2, laneWidth: 3.3, pavementWidth: 1.55 },
    }),
    createEdge('north', 'south', nodes, {
      id: 'bridge-creekline', name: 'Creekline flyover', c1: { x: 0.18, z: -9.8 }, c2: { x: -0.18, z: 9.8 },
      profile: { ...profile, laneCount: 2, pavementWidth: 1.5 },
      bridge: { enabled: true, height: 7.5, rampFraction: 0.2, deckType: 'box-girder', supportType: 'twin-column', supportSpacing: 12, parapet: true },
    }),
  ];
  return {
    schema: 'roadworks.network',
    version: 1,
    name: 'Creekline flyover',
    units: 'metres',
    nodes,
    edges,
    settings: {
      snap: true,
      grid: true,
      showPoints: true,
      showRoadSurface: true,
      showPavement: true,
      showCurbs: true,
      showMarkings: true,
      showBridgeStructure: true,
      gridSize: 1,
      defaultProfile: { ...DEFAULT_PROFILE },
    },
  };
}

export function nearestNode(network, point, maxDistance = Infinity, excludeId = null, groundOnly = true) {
  let found = null;
  let best = maxDistance;
  for (const node of Object.values(network.nodes)) {
    if (node.id === excludeId) continue;
    if (groundOnly && (node.elevation ?? 0) > 0.25) continue;
    const distance = distance2(node, point);
    if (distance < best) {
      best = distance;
      found = { node, distance };
    }
  }
  return found;
}

export function nearestPointOnEdge(edge, nodes, point, steps = 96) {
  const sampleCount = Math.max(8, steps);
  let best = { distance: Infinity, t: 0, point: null };
  const samples = sampleEdge(edge, nodes, sampleCount);
  for (let i = 0; i < samples.length - 1; i += 1) {
    const a = samples[i];
    const b = samples[i + 1];
    const dx = b.x - a.x;
    const dz = b.z - a.z;
    const den = dx * dx + dz * dz || 1;
    const tLine = clamp(((point.x - a.x) * dx + (point.z - a.z) * dz) / den, 0, 1);
    const x = a.x + dx * tLine;
    const z = a.z + dz * tLine;
    const distance = Math.hypot(point.x - x, point.z - z);
    if (distance < best.distance) {
      best = { distance, t: a.t + (b.t - a.t) * tLine, point: { x, z } };
    }
  }
  return best;
}

function splitCubic(p0, p1, p2, p3, t) {
  const lerp = (a, b, amount) => ({ x: a.x + (b.x - a.x) * amount, z: a.z + (b.z - a.z) * amount });
  const p01 = lerp(p0, p1, t);
  const p12 = lerp(p1, p2, t);
  const p23 = lerp(p2, p3, t);
  const p012 = lerp(p01, p12, t);
  const p123 = lerp(p12, p23, t);
  const p = lerp(p012, p123, t);
  return [
    { p0, p1: p01, p2: p012, p3: p },
    { p0: p, p1: p123, p2: p23, p3 },
  ];
}

function edgePiece(edge, from, to, curve, id) {
  return {
    ...cloneEdge(edge),
    id,
    from,
    to,
    c1: { ...curve.p1 },
    c2: { ...curve.p2 },
  };
}

export function splitEdgeAt(network, edgeId, t, point = null) {
  const index = network.edges.findIndex((edge) => edge.id === edgeId);
  if (index < 0) return null;
  const edge = network.edges[index];
  const safeT = clamp(t, 0, 1);
  if (safeT <= 1e-4) return edge.from;
  if (safeT >= 1 - 1e-4) return edge.to;
  if (edge.bridge?.enabled) return null;
  const curve = edgePoints(edge, network.nodes);
  const [left, right] = splitCubic(...curve, safeT);
  const exactPoint = cubicPoint(...curve, safeT);
  const splitPoint = point && distance2(point, exactPoint) < 0.015 ? point : exactPoint;
  const nodeId = makeId('node');
  network.nodes[nodeId] = { id: nodeId, x: splitPoint.x, z: splitPoint.z, name: 'Junction' };
  const leftEdge = edgePiece(edge, edge.from, nodeId, left, edge.id);
  const rightEdge = edgePiece(edge, nodeId, edge.to, right, makeId('road'));
  network.edges.splice(index, 1, leftEdge, rightEdge);
  return nodeId;
}

function segmentIntersection(a, b, c, d) {
  const rx = b.x - a.x;
  const rz = b.z - a.z;
  const sx = d.x - c.x;
  const sz = d.z - c.z;
  const denominator = rx * sz - rz * sx;
  if (Math.abs(denominator) < 1e-9) return null;
  const qx = c.x - a.x;
  const qz = c.z - a.z;
  const t = (qx * sz - qz * sx) / denominator;
  const u = (qx * rz - qz * rx) / denominator;
  if (t < -1e-6 || t > 1 + 1e-6 || u < -1e-6 || u > 1 + 1e-6) return null;
  return {
    point: { x: a.x + t * rx, z: a.z + t * rz },
    localA: clamp(t, 0, 1),
    localB: clamp(u, 0, 1),
  };
}

function mergeNodes(network, keepId, removeId) {
  if (keepId === removeId || !network.nodes[keepId] || !network.nodes[removeId]) return keepId;
  for (const edge of network.edges) {
    if (edge.from === removeId) edge.from = keepId;
    if (edge.to === removeId) edge.to = keepId;
  }
  delete network.nodes[removeId];
  network.edges = network.edges.filter((edge) => edge.from !== edge.to);
  return keepId;
}

function refineCurveIntersection(aEdge, bEdge, nodes, tA, tB) {
  let a = tA;
  let b = tB;
  for (let iteration = 0; iteration < 7; iteration += 1) {
    const pa = pointOnEdge(aEdge, nodes, a);
    const pb = pointOnEdge(bEdge, nodes, b);
    const da = cubicTangent(...edgePoints(aEdge, nodes), a);
    const db = cubicTangent(...edgePoints(bEdge, nodes), b);
    const fx = pa.x - pb.x;
    const fz = pa.z - pb.z;
    const determinant = db.x * da.z - da.x * db.z;
    if (Math.abs(determinant) < 1e-10) break;
    const deltaA = (fx * db.z - db.x * fz) / determinant;
    const deltaB = (-da.x * fz + fx * da.z) / determinant;
    a = clamp(a + deltaA, 0, 1);
    b = clamp(b + deltaB, 0, 1);
    if (Math.abs(deltaA) + Math.abs(deltaB) < 1e-9) break;
  }
  const pa = pointOnEdge(aEdge, nodes, a);
  const pb = pointOnEdge(bEdge, nodes, b);
  return { tA: a, tB: b, point: { x: (pa.x + pb.x) * 0.5, z: (pa.z + pb.z) * 0.5 } };
}

/**
 * Planarizes at-grade road crossings. Crossings are found on sampled cubic splines,
 * then the original Bezier curves are split with De Casteljau so the rendered shape
 * stays unchanged. Elevated bridge spans deliberately remain a separate graph layer.
 */
export function mergePlanarCrossings(network, tolerance = 0.38) {
  const atGrade = network.edges.filter((edge) => !edge.bridge?.enabled);
  const edgeCuts = new Map();
  const junctionNodes = [];
  const crossingPoints = [];
  const sampleSteps = 72;

  const getIntersectionNode = (point, endpointCandidates = []) => {
    for (const candidate of endpointCandidates) {
      if (candidate && network.nodes[candidate] && distance2(network.nodes[candidate], point) <= tolerance * 1.6) return candidate;
    }
    const existing = nearestNode(network, point, tolerance, null, false);
    if (existing) return existing.node.id;
    const id = makeId('node');
    network.nodes[id] = { id, x: point.x, z: point.z, name: 'Auto junction', generated: true };
    junctionNodes.push(id);
    return id;
  };

  for (let i = 0; i < atGrade.length; i += 1) {
    const aEdge = atGrade[i];
    const aCurve = edgePoints(aEdge, network.nodes);
    if (!aCurve) continue;
    const aSamples = sampleEdge(aEdge, network.nodes, sampleSteps);
    for (let j = i + 1; j < atGrade.length; j += 1) {
      const bEdge = atGrade[j];
      const bCurve = edgePoints(bEdge, network.nodes);
      if (!bCurve) continue;
      const bSamples = sampleEdge(bEdge, network.nodes, sampleSteps);
      const pairHits = [];
      for (let ai = 0; ai < aSamples.length - 1; ai += 1) {
        const a0 = aSamples[ai];
        const a1 = aSamples[ai + 1];
        for (let bi = 0; bi < bSamples.length - 1; bi += 1) {
          const b0 = bSamples[bi];
          const b1 = bSamples[bi + 1];
          const hit = segmentIntersection(a0, a1, b0, b1);
          if (!hit) continue;
          const tA = a0.t + (a1.t - a0.t) * hit.localA;
          const tB = b0.t + (b1.t - b0.t) * hit.localB;
          const refined = refineCurveIntersection(aEdge, bEdge, network.nodes, tA, tB);
          if (pairHits.some((entry) => Math.abs(entry.tA - refined.tA) < 0.025 && Math.abs(entry.tB - refined.tB) < 0.025)) continue;
          pairHits.push(refined);
        }
      }

      for (const hit of pairHits) {
        const endA = hit.tA < 0.012 ? aEdge.from : hit.tA > 0.988 ? aEdge.to : null;
        const endB = hit.tB < 0.012 ? bEdge.from : hit.tB > 0.988 ? bEdge.to : null;
        const requiresTopology = !(endA && endB && endA === endB);
        if (requiresTopology && !crossingPoints.some((point) => distance2(point, hit.point) < tolerance * 1.5)) crossingPoints.push(hit.point);
        const endpointCandidates = [endA, endB].filter(Boolean);
        let nodeId = getIntersectionNode(hit.point, endpointCandidates);
        if (endA && endB && endA !== endB && network.nodes[endA] && network.nodes[endB]
          && distance2(network.nodes[endA], network.nodes[endB]) <= tolerance * 1.6) {
          nodeId = mergeNodes(network, endA, endB);
        }
        for (const [edge, t] of [[aEdge, hit.tA], [bEdge, hit.tB]]) {
          const isEndpoint = t < 0.012 || t > 0.988;
          if (isEndpoint) {
            const endpoint = t < 0.012 ? edge.from : edge.to;
            if (endpoint !== nodeId && network.nodes[endpoint] && network.nodes[nodeId]
              && distance2(network.nodes[endpoint], network.nodes[nodeId]) <= tolerance * 1.6) {
              mergeNodes(network, nodeId, endpoint);
            }
            continue;
          }
          const cuts = edgeCuts.get(edge.id) ?? [];
          const already = cuts.find((cut) => Math.abs(cut.t - t) < 0.025);
          if (already) {
            if (already.nodeId !== nodeId && network.nodes[already.nodeId] && network.nodes[nodeId]
              && distance2(network.nodes[already.nodeId], network.nodes[nodeId]) <= tolerance * 1.6) {
              nodeId = mergeNodes(network, already.nodeId, nodeId);
              already.nodeId = nodeId;
            }
          } else {
            cuts.push({ t, nodeId });
          }
          edgeCuts.set(edge.id, cuts);
        }
      }
    }
  }

  if (!edgeCuts.size) return { crossings: crossingPoints.length, splits: 0, nodesAdded: junctionNodes.length };

  const originals = new Map(network.edges.map((edge) => [edge.id, edge]));
  const nextEdges = [];
  let splitCount = 0;
  for (const edge of network.edges) {
    const cuts = edgeCuts.get(edge.id);
    if (!cuts?.length) {
      nextEdges.push(edge);
      continue;
    }
    const ordered = cuts.sort((a, b) => a.t - b.t).filter((cut, index, all) => index === 0 || Math.abs(cut.t - all[index - 1].t) > 0.02);
    const original = originals.get(edge.id) ?? edge;
    let currentCurve = {
      p0: network.nodes[original.from], p1: original.c1, p2: original.c2, p3: network.nodes[original.to],
    };
    let previousT = 0;
    let currentFrom = original.from;
    for (let index = 0; index < ordered.length; index += 1) {
      const cut = ordered[index];
      const localT = clamp((cut.t - previousT) / (1 - previousT), 0.001, 0.999);
      const [left, right] = splitCubic(currentCurve.p0, currentCurve.p1, currentCurve.p2, currentCurve.p3, localT);
      const leftEdge = edgePiece(original, currentFrom, cut.nodeId, left, index === 0 ? original.id : makeId('road'));
      nextEdges.push(leftEdge);
      currentCurve = right;
      currentFrom = cut.nodeId;
      previousT = cut.t;
      splitCount += 1;
    }
    nextEdges.push(edgePiece(original, currentFrom, original.to, currentCurve, makeId('road')));
  }
  network.edges = nextEdges;
  return { crossings: crossingPoints.length, splits: splitCount, nodesAdded: junctionNodes.length };
}

export function addRoadPath(network, inputPoints, kind = 'road') {
  const points = inputPoints.filter((point) => Number.isFinite(point.x) && Number.isFinite(point.z));
  if (points.length < 2) return { added: 0, startNode: null, endNode: null };
  const pointNodeIds = [];
  const snapToNetwork = (point) => {
    if (network.settings?.snap !== false) {
      const snappedNode = nearestNode(network, point, 1.35, null, true);
      if (snappedNode) return snappedNode.node.id;
      if (kind !== 'bridge') {
        let best = null;
        for (const edge of network.edges) {
          if (edge.bridge?.enabled) continue;
          const hit = nearestPointOnEdge(edge, network.nodes, point, 80);
          if (hit.distance < 1.0 && hit.t > 0.02 && hit.t < 0.98 && (!best || hit.distance < best.distance)) {
            best = { ...hit, edgeId: edge.id };
          }
        }
        if (best) return splitEdgeAt(network, best.edgeId, best.t, best.point);
      }
    }
    const id = makeId('node');
    network.nodes[id] = { id, x: point.x, z: point.z, name: kind === 'bridge' ? 'Bridge abutment' : 'Road point' };
    return id;
  };

  const routePoints = kind === 'bridge' && points.length > 2 ? [points[0], points.at(-1)] : points;
  for (const point of routePoints) pointNodeIds.push(snapToNetwork(point));
  let added = 0;
  const profile = { ...(network.settings?.defaultProfile ?? DEFAULT_PROFILE) };

  if (kind === 'bridge') {
    const from = pointNodeIds[0];
    const to = pointNodeIds.at(-1);
    if (from === to) return { added: 0, startNode: from, endNode: to };
    const start = network.nodes[from];
    const end = network.nodes[to];
    const dx = end.x - start.x;
    const dz = end.z - start.z;
    const edge = createEdge(from, to, network.nodes, {
      name: `Bridge ${network.edges.filter((item) => item.bridge?.enabled).length + 1}`,
      profile,
      c1: { x: start.x + dx / 3, z: start.z + dz / 3 },
      c2: { x: start.x + (2 * dx) / 3, z: start.z + (2 * dz) / 3 },
      bridge: { enabled: true, height: 7, rampFraction: 0.22, deckType: 'box-girder', supportType: 'twin-column', supportSpacing: 12, parapet: true },
    });
    network.edges.push(edge);
    added = 1;
  } else {
    for (let i = 0; i < pointNodeIds.length - 1; i += 1) {
      const from = pointNodeIds[i];
      const to = pointNodeIds[i + 1];
      if (from === to || distance2(network.nodes[from], network.nodes[to]) < 0.15) continue;
      const start = network.nodes[from];
      const end = network.nodes[to];
      const previous = i > 0 ? network.nodes[pointNodeIds[i - 1]] : null;
      const next = i + 2 < pointNodeIds.length ? network.nodes[pointNodeIds[i + 2]] : null;
      const dx = end.x - start.x;
      const dz = end.z - start.z;
      const c1 = previous
        ? { x: start.x + (end.x - previous.x) / 6, z: start.z + (end.z - previous.z) / 6 }
        : { x: start.x + dx / 3, z: start.z + dz / 3 };
      const c2 = next
        ? { x: end.x - (next.x - start.x) / 6, z: end.z - (next.z - start.z) / 6 }
        : { x: start.x + (2 * dx) / 3, z: start.z + (2 * dz) / 3 };
      network.edges.push(createEdge(from, to, network.nodes, {
        name: `Road ${network.edges.filter((item) => !item.bridge?.enabled).length + 1}`,
        profile, c1, c2,
      }));
      added += 1;
    }
  }

  const result = mergePlanarCrossings(network);
  return { added, startNode: pointNodeIds[0], endNode: pointNodeIds.at(-1), ...result };
}

export function moveNode(network, nodeId, nextPosition) {
  const node = network.nodes[nodeId];
  if (!node) return;
  const dx = nextPosition.x - node.x;
  const dz = nextPosition.z - node.z;
  node.x = nextPosition.x;
  node.z = nextPosition.z;
  for (const edge of network.edges) {
    if (edge.from === nodeId) {
      edge.c1.x += dx;
      edge.c1.z += dz;
    }
    if (edge.to === nodeId) {
      edge.c2.x += dx;
      edge.c2.z += dz;
    }
  }
}

export function deleteSelection(network, selection) {
  if (!selection) return false;
  if (selection.type === 'edge') {
    const index = network.edges.findIndex((edge) => edge.id === selection.id);
    if (index < 0) return false;
    const [edge] = network.edges.splice(index, 1);
    for (const nodeId of [edge.from, edge.to]) {
      const stillUsed = network.edges.some((candidate) => candidate.from === nodeId || candidate.to === nodeId);
      if (!stillUsed) delete network.nodes[nodeId];
    }
    return true;
  }
  if (selection.type === 'node') {
    if (!network.nodes[selection.id]) return false;
    network.edges = network.edges.filter((edge) => edge.from !== selection.id && edge.to !== selection.id);
    delete network.nodes[selection.id];
    const usedNodes = new Set(network.edges.flatMap((edge) => [edge.from, edge.to]));
    for (const id of Object.keys(network.nodes)) if (!usedNodes.has(id)) delete network.nodes[id];
    return true;
  }
  return false;
}

export function validateNetwork(raw) {
  if (!raw || raw.schema !== 'roadworks.network' || !raw.nodes || !Array.isArray(raw.edges)) {
    throw new Error('This file is not a RoadWorks network project.');
  }
  const network = structuredClone(raw);
  for (const edge of network.edges) {
    if (!network.nodes[edge.from] || !network.nodes[edge.to]) throw new Error(`Road “${edge.name ?? edge.id}” has a missing endpoint.`);
    edge.c1 ??= { x: network.nodes[edge.from].x, z: network.nodes[edge.from].z };
    edge.c2 ??= { x: network.nodes[edge.to].x, z: network.nodes[edge.to].z };
    Object.assign(edge, edgeDefaults(), edge);
    edge.name ??= 'Road';
    if (edge.bridge?.enabled) edge.bridge = { deckType: 'box-girder', supportType: 'twin-column', supportSpacing: 12, height: 7, rampFraction: 0.2, parapet: true, ...edge.bridge };
    else edge.bridge = null;
  }
  const settings = network.settings ?? {};
  network.settings = {
    snap: true, grid: true, showPoints: true, showRoadSurface: true, showPavement: true, showCurbs: true, showMarkings: true,
    showBridgeStructure: true, gridSize: 1, ...settings,
    defaultProfile: { ...DEFAULT_PROFILE, ...(settings.defaultProfile ?? {}) },
  };
  return network;
}
