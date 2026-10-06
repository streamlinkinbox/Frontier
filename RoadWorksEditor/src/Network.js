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

export function buildNetwork(corridors, settings = {}) {
  const t0 = now();
  const cfg = { ...GRAPH_DEFAULTS, ...settings };
  const graph = buildGraph(corridors, cfg);

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
    buildSegmentMesh(graph, edge, groups, { ...cfg, sections, paveGroup });
    if (cfg.markings !== false) buildMarkings(sections, edge.profile, groups.markings, cfg);
    if (edge.family === 'bridge') {
      buildBridgeMesh(edge, sections, groups, { groundZ: cfg.groundZ ?? 0, ...(cfg.bridgeOverrides || {}) });
      buildBridgeApproachFill(edge, sections, groups, cfg);
    } else {
      buildRoadbedMesh(edge, sections, groups, cfg);
      if (buildGuardrail(edge, sections, groups, cfg)) guardrailCount++;
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

  const stats = {
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
