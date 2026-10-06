//============================================================================================================================================
//                                                               NETWORK.JS
//============================================================================================================================================
// Orchestration: corridors → graph → meshes. One call rebuilds the whole network into material groups (road, curb,
// paving variants, markings, roadbed/earth, deck, structure, piers, railing, cables, signs) plus diagnostics.
//
// Pavement is split into one group per paving pattern in use — `pavement#brick@1` and friends — so a single draw
// call still covers every corridor laid in the same material while each pattern keeps its own texture.

import { MeshSpec } from './MeshSpec.js?v=8';
import { buildGraph, nodeGeneratesJunction, GRAPH_DEFAULTS } from './Graph.js?v=8';
import { buildMarkings, buildSegmentMesh, sectionsForEdge } from './RoadMesh.js?v=8';
import { buildJunctionMesh } from './JunctionMesh.js?v=8';
import { buildBridgeMesh } from './BridgeMesh.js?v=8';
import { buildRoadbedMesh, buildApronSkirt, buildBridgeApproachFill } from './Roadbed.js?v=8';
import { buildJunctionFurniture } from './Signs.js?v=8';
import { buildGuardrail } from './Guardrail.js?v=8';
import { surfaceGroup } from './Surfaces.js?v=8';
import { buildLaneDetail, paintYellowBox } from './Markings.js?v=8';
import { buildRoundabout, buildSplitterIsland } from './Roundabout.js?v=8';
import { buildMerges, MERGE_DEFAULTS } from './Merge.js?v=8';
import { buildDriveways, drivewayWindows, kerbDropFn } from './Driveways.js?v=8';
import { buildDrainage } from './Drainage.js?v=8';

export const GROUP_NAMES = [
  'road', 'curb', 'pavement', 'markings', 'markingsYellow', 'driveway',
  'drainGrate', 'drainCover', 'drainPipe',
  'earth', 'roadbed',
  'deck', 'structure', 'piers', 'railing', 'barrier', 'cables',
  'signFace', 'signPost',
];

// Groups whose shading should stay smooth across shallow folds; everything else creases earlier.
const SMOOTH = new Set(['road', 'pavement', 'deck', 'earth']);

export function groupBase(name) {
  return name.split('#')[0];
}

export function pavingGroup(profile) {
  return `pavement#${profile.paving || 'concrete'}@${(profile.pavingScale || 1).toFixed(2)}`;
}

// Per-edge geometry cache. Dragging one street re-solves the whole network, but only the corridors whose trimmed
// cross-sections actually changed need their meshes rebuilt — everything else is appended straight from the cache.
// Keys are a hash of the edge's own geometry and settings, so a stale entry simply never matches.
function edgeKey(edge, sections, cfg, paveGroup, ends) {
  let h = 2166136261;
  const mix = (v) => {
    h ^= Math.round(v * 1000) | 0;
    h = Math.imul(h, 16777619);
  };
  for (const s of sections) {
    mix(s.base.x);
    mix(s.base.y);
    mix(s.base.z);
    mix(s.miter);
    mix(s.frame.left.x);
    mix(s.frame.left.y);
  }
  const tail = JSON.stringify([edge.family, edge.profile, edge.bridge, edge.guardrail, edge.roadbed, edge.markings, edge.driveways, edge.drainage, paveGroup, cfg.markings !== false, cfg.groundZ ?? 0, ends]);
  for (let i = 0; i < tail.length; i++) {
    h ^= tail.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return `${h >>> 0}:${sections.length}:${tail.length}`;
}

function mergeInto(groups, parts) {
  for (const [name, spec] of parts) {
    const target = groups[name] || (groups[name] = new MeshSpec(name));
    target.append(spec);
  }
}


// ── gores: folding the verge away between two lapped arms ─────────────────────────────────────────────────────────
//
// Where a ramp forks off a mainline the two carriageways are still lapped together: for the first stretch the ramp
// sits inside the motorway's footprint. Giving either of them a kerb and footway there drags a verge straight
// across the other's running lanes, which is exactly what a diverge is not. So each arm of a fork has its verge
// closed down to nothing at the node and opened again at the nose — the paved gore covers the gap in between.

const MERGE_MAX_ANGLE = (MERGE_DEFAULTS.maxAngle * Math.PI) / 180;

function outgoingDir(edge, atStart) {
  const pts = edge.points;
  const a = atStart ? pts[0] : pts[pts.length - 1];
  const b = atStart ? pts[1] : pts[pts.length - 2];
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const len = Math.hypot(dx, dy) || 1;
  return { x: dx / len, y: dy / len };
}

// The windows on one edge where its verge has to stay shut, one per fork it ends at.
function forkWindows(graph, edge, cfg) {
  const windows = [];
  const goreWidth = cfg.slip?.goreWidth ?? MERGE_DEFAULTS.goreWidth;
  const maxLength = cfg.slip?.maxLength ?? MERGE_DEFAULTS.maxLength;
  for (const atStart of [true, false]) {
    const node = graph.nodes.get(atStart ? edge.startNodeId : edge.endNodeId);
    if (!node || !node.fork || node.style === 'roundabout') continue;
    const dir = outgoingDir(edge, atStart);
    let best = null;
    for (const otherId of node.edgeIds) {
      if (otherId === edge.id) continue;
      const other = graph.edges.get(otherId);
      if (!other) continue;
      const otherAtStart = other.startNodeId === node.id;
      const odir = outgoingDir(other, otherAtStart);
      const dot = clampNum(dir.x * odir.x + dir.y * odir.y, -1, 1);
      const gap = Math.acos(dot);
      if (gap < 0.02 || gap > MERGE_MAX_ANGLE) continue;
      if (!best || gap < best.gap) best = { gap, cross: dir.x * odir.y - dir.y * odir.x, profile: other.profile, other };
    }
    if (!best) continue;
    // Separation grows as 2·sin(gap/2) per metre, so this is the run needed for the two kerbs to clear each other
    // plus the width of the gore itself.
    const need = edge.profile.roadHalf + (best.profile?.roadHalf || 4) + goreWidth;
    const until = clampNum(need / Math.max(0.02, 2 * Math.sin(best.gap / 2)), 12, maxLength);
    const gore = (best.cross > 0) === atStart ? 'left' : 'right';
    windows.push({ atStart, side: gore, until });
    // The minor arm of a fork — the ramp — is lapped *inside* the road it is leaving, so its outer verge is buried
    // in the other carriageway too. It gets folded away as well, and opens again a little sooner.
    if (isMinorArm(graph, node, edge, best.other)) {
      windows.push({ atStart, side: gore === 'left' ? 'right' : 'left', until: until * 0.75 });
    }
  }
  return windows;
}

// Of the two arms that form a fork, the minor one is the ramp: the through road keeps two arms at the node, the
// ramp only one. Equal counts fall back to the narrower carriageway.
function isMinorArm(graph, node, edge, other) {
  const armsOf = (sourceId) => node.edgeIds.filter((id) => graph.edges.get(id)?.sourceId === sourceId).length;
  const mine = armsOf(edge.sourceId);
  const theirs = armsOf(other.sourceId);
  if (mine !== theirs) return mine < theirs;
  return (edge.profile?.roadWidth || 0) <= (other.profile?.roadWidth || 0);
}

function clampNum(v, lo, hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

// 0 at the fork, 1 from the nose onwards, eased over the last quarter so the kerb grows in rather than popping up.
function vergeFadeFn(windows, total) {
  if (!windows.length) return null;
  return (distance, side) => {
    let f = 1;
    for (const w of windows) {
      if (w.side !== side) continue;
      const d = w.atStart ? distance : total - distance;
      if (d >= w.until) continue;
      const t = clampNum((d - w.until * 0.72) / (w.until * 0.28), 0, 1);
      f = Math.min(f, t * t * (3 - 2 * t));
    }
    return f;
  };
}

export function buildNetwork(corridors, settings = {}, cache = null) {
  const t0 = now();
  const cfg = { ...GRAPH_DEFAULTS, ...settings };
  const graph = buildGraph(corridors, cfg);
  // A fresh map each solve: anything not hit this time around is dropped, so the cache cannot grow without bound.
  const nextCache = cache ? new Map() : null;
  let cacheHits = 0;

  const groups = {};
  for (const name of GROUP_NAMES) groups[name] = new MeshSpec(name);

  const sectionsByEdge = new Map();
  const warnings = [...graph.warnings];
  let guardrailCount = 0;
  let drivewayCount = 0;
  let drainageCount = 0;

  // Corridors first: every edge contributes its trimmed cross-sections, which the junctions then reuse verbatim.
  for (const edge of graph.edges.values()) {
    let sections = sectionsForEdge(graph, edge, cfg);
    // Vehicle crossovers change the cross-section itself (the kerb drops), and so does a gore (the verge closes),
    // so once both are known the sections are re-solved with them applied rather than patched afterwards.
    let crossings = [];
    let drop = null;
    if (sections && edge.driveways?.enabled && edge.family !== 'bridge') {
      crossings = drivewayWindows(edge.driveways, sections[sections.length - 1].distance, edge.profile);
      drop = kerbDropFn(crossings, edge.driveways.drop);
    }
    const windows = sections && edge.family !== 'bridge' ? forkWindows(graph, edge, cfg) : [];
    const vergeFade = sections ? vergeFadeFn(windows, sections[sections.length - 1].distance) : null;
    if (sections && (drop || vergeFade)) {
      sections = sectionsForEdge(graph, edge, { ...cfg, kerbDrop: drop || undefined, vergeFade: vergeFade || undefined }) || sections;
    }
    if (!sections) {
      warnings.push(`${edge.name || edge.sourceId}: segment too short between junctions`);
      continue;
    }
    sectionsByEdge.set(edge.id, sections);
    const paveGroup = pavingGroup(edge.profile);
    const roadGroup = surfaceGroup(edge.profile);

    // Splitter islands depend on what the arm runs into, so the junction styles at both ends are part of the key.
    const ends = [graph.nodes.get(edge.startNodeId)?.style || '', graph.nodes.get(edge.endNodeId)?.style || ''];
    const key = cache ? edgeKey(edge, sections, cfg, paveGroup, ends) : null;
    const cached = key ? cache.get(key) : null;
    if (cached) {
      mergeInto(groups, cached.parts);
      if (cached.guardrail) guardrailCount++;
      drivewayCount += cached.drives || 0;
      drainageCount += cached.drains || 0;
      nextCache.set(key, cached);
      cacheHits++;
      continue;
    }

    // Build into a private set of specs so the result can be cached and appended as a unit.
    const local = cache ? {} : groups;
    buildSegmentMesh(graph, edge, local, { ...cfg, sections, paveGroup, roadGroup });
    if (cfg.markings !== false && edge.profile.markings !== false) {
      const markings = local.markings || (local.markings = new MeshSpec('markings'));
      buildMarkings(sections, edge.profile, markings, cfg);
      buildLaneDetail(graph, edge, sections, local, { ...cfg, paveGroup });
    }
    // Splitter islands on any arm that meets a roundabout.
    for (const atEnd of [false, true]) {
      const node = graph.nodes.get(atEnd ? edge.endNodeId : edge.startNodeId);
      if (!node || node.style !== 'roundabout') continue;
      const splitter = node.roundabout?.splitter ?? 16;
      if (splitter > 2) buildSplitterIsland(local, sections, edge.profile, atEnd, { length: splitter, topGroup: paveGroup });
    }

    let drives = 0;
    let drains = 0;
    if (crossings.length) drives = buildDriveways(edge, sections, crossings, local);
    if (edge.family !== 'bridge') drains = buildDrainage(edge, sections, local, cfg);
    drivewayCount += drives;
    drainageCount += drains;

    let hasGuardrail = false;
    if (edge.family === 'bridge') {
      buildBridgeMesh(edge, sections, local, { groundZ: cfg.groundZ ?? 0, ...(cfg.bridgeOverrides || {}) });
      buildBridgeApproachFill(edge, sections, local, cfg);
    } else {
      buildRoadbedMesh(edge, sections, local, cfg);
      hasGuardrail = !!buildGuardrail(edge, sections, local, { ...cfg, vergeFade });
    }
    if (hasGuardrail) guardrailCount++;
    if (cache) {
      const parts = Object.entries(local);
      nextCache.set(key, { parts, guardrail: hasGuardrail, drives, drains });
      mergeInto(groups, parts);
    }
  }

  let junctionCount = 0;
  let signCount = 0;
  let roundaboutCount = 0;
  let slipCount = 0;
  for (const node of graph.nodes.values()) {
    if (!nodeGeneratesJunction(graph, node)) continue;
    const dominant = widestApproachProfile(graph, node);
    const paveGroup = pavingGroup(dominant);
    const built = buildJunctionMesh(graph, node, sectionsByEdge, groups, { paveGroup, roadGroup: surfaceGroup(dominant) });
    if (!built) continue;
    junctionCount++;
    buildApronSkirt(built, groups, cfg);
    if (node.style !== 'roundabout') {
      slipCount += buildMerges(graph, node, built, sectionsByEdge, groups, { ...cfg, roadGroup: surfaceGroup(dominant) });
    }
    if (node.style === 'roundabout') {
      buildRoundabout(groups, node, built, { apronGroup: paveGroup });
      roundaboutCount++;
    } else if (junctionWantsYellowBox(graph, node)) {
      paintYellowBox(groups, built);
    }
    // A roundabout signs itself with give-way markings, so the stop furniture stays away.
    // Roundabouts sign themselves with give-way markings; forks are merges, where a stop line would be nonsense.
    if (node.style !== 'roundabout' && !node.fork) signCount += buildJunctionFurniture(built, node, groups, cfg);
  }

  let triangles = 0;
  for (const name of Object.keys(groups)) {
    groups[name].recalcNormals(SMOOTH.has(groupBase(name)) ? 55 : 40);
    triangles += groups[name].triangleCount;
  }

  if (cache) {
    cache.clear();
    for (const [k, v] of nextCache) cache.set(k, v);
  }

  const stats = {
    cacheHits,
    corridors: corridors.length,
    nodes: graph.nodes.size,
    edges: graph.edges.size,
    junctions: junctionCount,
    roundabouts: roundaboutCount,
    slipRoads: slipCount,
    signs: signCount,
    guardrails: guardrailCount,
    driveways: drivewayCount,
    drainage: drainageCount,
    gradeSeparations: graph.crossings.filter((c) => c.separated).length,
    triangles,
    buildMs: Math.round(now() - t0),
    warnings,
  };

  return { graph, groups, stats, sectionsByEdge };
}

// A yellow box is painted when any arm meeting here asks for one.
function junctionWantsYellowBox(graph, node) {
  for (const edgeId of node.edgeIds) {
    const edge = graph.edges.get(edgeId);
    if (edge && edge.markings && edge.markings.yellowBox) return true;
  }
  return false;
}

// The apron takes the paving of the widest arm meeting there — the dominant street reads as continuing through.
function widestApproachProfile(graph, node) {
  let best = null;
  for (const edgeId of node.edgeIds) {
    const edge = graph.edges.get(edgeId);
    if (!edge) continue;
    if (!best || edge.profile.roadWidth > best.roadWidth) best = edge.profile;
  }
  return best || { paving: 'concrete', pavingScale: 1 };
}

function now() {
  return typeof performance !== 'undefined' ? performance.now() : Date.now();
}
