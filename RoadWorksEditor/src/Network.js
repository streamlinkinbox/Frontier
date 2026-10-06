//============================================================================================================================================
//                                                               NETWORK.JS
//============================================================================================================================================
// Orchestration: corridors → graph → meshes. One call rebuilds the whole network into material groups (road, curb,
// paving variants, markings, roadbed/earth, deck, structure, piers, railing, cables, signs) plus diagnostics.
//
// Pavement is split into one group per paving pattern in use — `pavement#brick@1` and friends — so a single draw
// call still covers every corridor laid in the same material while each pattern keeps its own texture.

import { MeshSpec } from './MeshSpec.js?v=4';
import { buildGraph, nodeGeneratesJunction, GRAPH_DEFAULTS } from './Graph.js?v=4';
import { buildMarkings, buildSegmentMesh, sectionsForEdge } from './RoadMesh.js?v=4';
import { buildJunctionMesh } from './JunctionMesh.js?v=4';
import { buildBridgeMesh } from './BridgeMesh.js?v=4';
import { buildRoadbedMesh, buildApronSkirt, buildBridgeApproachFill } from './Roadbed.js?v=4';
import { buildJunctionFurniture } from './Signs.js?v=4';
import { buildGuardrail } from './Guardrail.js?v=4';

export const GROUP_NAMES = [
  'road', 'curb', 'pavement', 'markings',
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
function edgeKey(edge, sections, cfg, paveGroup) {
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
  const tail = JSON.stringify([edge.family, edge.profile, edge.bridge, edge.guardrail, edge.roadbed, paveGroup, cfg.markings !== false, cfg.groundZ ?? 0]);
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

  // Corridors first: every edge contributes its trimmed cross-sections, which the junctions then reuse verbatim.
  for (const edge of graph.edges.values()) {
    const sections = sectionsForEdge(graph, edge, cfg);
    if (!sections) {
      warnings.push(`${edge.name || edge.sourceId}: segment too short between junctions`);
      continue;
    }
    sectionsByEdge.set(edge.id, sections);
    const paveGroup = pavingGroup(edge.profile);

    const key = cache ? edgeKey(edge, sections, cfg, paveGroup) : null;
    const cached = key ? cache.get(key) : null;
    if (cached) {
      mergeInto(groups, cached.parts);
      if (cached.guardrail) guardrailCount++;
      nextCache.set(key, cached);
      cacheHits++;
      continue;
    }

    // Build into a private set of specs so the result can be cached and appended as a unit.
    const local = cache ? {} : groups;
    buildSegmentMesh(graph, edge, local, { ...cfg, sections, paveGroup });
    if (cfg.markings !== false) {
      const markings = local.markings || (local.markings = new MeshSpec('markings'));
      buildMarkings(sections, edge.profile, markings, cfg);
    }
    let hasGuardrail = false;
    if (edge.family === 'bridge') {
      buildBridgeMesh(edge, sections, local, { groundZ: cfg.groundZ ?? 0, ...(cfg.bridgeOverrides || {}) });
      buildBridgeApproachFill(edge, sections, local, cfg);
    } else {
      buildRoadbedMesh(edge, sections, local, cfg);
      hasGuardrail = !!buildGuardrail(edge, sections, local, cfg);
    }
    if (hasGuardrail) guardrailCount++;
    if (cache) {
      const parts = Object.entries(local);
      nextCache.set(key, { parts, guardrail: hasGuardrail });
      mergeInto(groups, parts);
    }
  }

  let junctionCount = 0;
  let signCount = 0;
  for (const node of graph.nodes.values()) {
    if (!nodeGeneratesJunction(graph, node)) continue;
    const paveGroup = pavingGroup(widestApproachProfile(graph, node));
    const built = buildJunctionMesh(graph, node, sectionsByEdge, groups, { paveGroup });
    if (!built) continue;
    junctionCount++;
    buildApronSkirt(built, groups, cfg);
    signCount += buildJunctionFurniture(built, node, groups, cfg);
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
    signs: signCount,
    guardrails: guardrailCount,
    gradeSeparations: graph.crossings.filter((c) => c.separated).length,
    triangles,
    buildMs: Math.round(now() - t0),
    warnings,
  };

  return { graph, groups, stats, sectionsByEdge };
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
