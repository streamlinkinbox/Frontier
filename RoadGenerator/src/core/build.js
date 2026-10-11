// Build pipeline: network → analysed plan → merged road+junction mesh (+ detail layers).
import { analyse } from "./network.js";
import { MeshBuilder } from "./mesh.js";
import { solveTrims, sweepRoads, buildJunctions } from "./junction.js";
import { applyLevelCrossingRegrades } from "./rail.js";

export function buildRoadNetwork(net, settings = {}) {
  const t0 = typeof performance !== "undefined" ? performance.now() : Date.now();
  const plan = analyse(net, settings);
  solveTrims(plan);
  const mb = new MeshBuilder("network");
  sweepRoads(plan, mb, settings);
  if (net.rails && net.rails.size && settings.regradeCrossings !== false) applyLevelCrossingRegrades(net, plan, mb);
  buildJunctions(plan, mb, settings);
  mb.compact();
  plan.mesh = mb;
  plan.buildMs = (typeof performance !== "undefined" ? performance.now() : Date.now()) - t0;
  return plan;
}

/** Topology audit: edge manifoldness, degenerate faces, NaNs, quad ratio. */
export function auditMesh(mb) {
  const edges = new Map();
  let degenerate = 0, nan = 0, quads = 0, tris = 0;
  for (let i = 0; i < mb.positions.length; i++) if (!Number.isFinite(mb.positions[i])) nan++;
  for (const f of mb.faces) {
    if (f.i.length === 4) quads++; else tris++;
    const pts = f.i.map((v) => mb.position(v));
    // area
    let nx = 0, ny = 0, nz = 0;
    for (let k = 0; k < pts.length; k++) { const a = pts[k], b = pts[(k + 1) % pts.length]; nx += (a[1] - b[1]) * (a[2] + b[2]); ny += (a[2] - b[2]) * (a[0] + b[0]); nz += (a[0] - b[0]) * (a[1] + b[1]); }
    if (Math.hypot(nx, ny, nz) < 1e-7) degenerate++;
    for (let k = 0; k < f.i.length; k++) {
      const a = f.i[k], b = f.i[(k + 1) % f.i.length];
      const key = a < b ? a * 4294967296 + b : b * 4294967296 + a;
      edges.set(key, (edges.get(key) || 0) + 1);
    }
  }
  let boundary = 0, nonManifold = 0;
  for (const c of edges.values()) { if (c === 1) boundary++; else if (c > 2) nonManifold++; }
  return { vertices: mb.vertexCount, faces: mb.faces.length, quads, tris, quadRatio: quads / Math.max(1, quads + tris), degenerate, nan, boundaryEdges: boundary, nonManifoldEdges: nonManifold };
}
