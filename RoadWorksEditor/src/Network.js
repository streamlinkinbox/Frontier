//============================================================================================================================================
//                                                               NETWORK.JS
//============================================================================================================================================
// Orchestration: corridors → graph → meshes. One call rebuilds the whole network into a handful of material groups
// (road, curb, pavement, markings, deck, structure, piers, railing, cables) plus diagnostics for the inspector.

import { MeshSpec } from './MeshSpec.js';
import { buildGraph, nodeGeneratesJunction, GRAPH_DEFAULTS } from './Graph.js';
import { buildMarkings, buildSegmentMesh, sectionsForEdge } from './RoadMesh.js';
import { buildJunctionMesh } from './JunctionMesh.js';
import { buildBridgeMesh } from './BridgeMesh.js';

export const GROUP_NAMES = ['road', 'curb', 'pavement', 'markings', 'deck', 'structure', 'piers', 'railing', 'cables'];

export function buildNetwork(corridors, settings = {}) {
  const t0 = now();
  const cfg = { ...GRAPH_DEFAULTS, ...settings };
  const graph = buildGraph(corridors, cfg);

  const groups = {};
  for (const name of GROUP_NAMES) groups[name] = new MeshSpec(name);

  const sectionsByEdge = new Map();
  const warnings = [...graph.warnings];

  // Corridors first: every edge contributes its trimmed cross-sections, which the junctions then reuse verbatim.
  for (const edge of graph.edges.values()) {
    const sections = sectionsForEdge(graph, edge, cfg);
    if (!sections) {
      warnings.push(`${edge.name || edge.sourceId}: segment too short between junctions`);
      continue;
    }
    sectionsByEdge.set(edge.id, sections);
    buildSegmentMesh(graph, edge, groups, { ...cfg, sections });
    if (cfg.markings !== false) buildMarkings(sections, edge.profile, groups.markings, cfg);
    if (edge.family === 'bridge') {
      buildBridgeMesh(edge, sections, groups, { groundZ: cfg.groundZ ?? 0, ...(cfg.bridgeOverrides || {}) });
    }
  }

  let junctionCount = 0;
  for (const node of graph.nodes.values()) {
    if (!nodeGeneratesJunction(graph, node)) continue;
    const built = buildJunctionMesh(graph, node, sectionsByEdge, groups);
    if (built) junctionCount++;
  }

  let triangles = 0;
  for (const name of GROUP_NAMES) {
    groups[name].recalcNormals(name === 'road' || name === 'pavement' || name === 'deck' ? 55 : 40);
    triangles += groups[name].triangleCount;
  }

  const stats = {
    corridors: corridors.length,
    nodes: graph.nodes.size,
    edges: graph.edges.size,
    junctions: junctionCount,
    gradeSeparations: graph.crossings.filter((c) => c.separated).length,
    triangles,
    buildMs: Math.round(now() - t0),
    warnings,
  };

  return { graph, groups, stats, sectionsByEdge };
}

function now() {
  return typeof performance !== 'undefined' ? performance.now() : Date.now();
}
