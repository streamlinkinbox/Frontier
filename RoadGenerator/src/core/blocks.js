// City blocks: the enclosed faces of the planar road graph, filled as plots (plaza paving, concrete slab,
// gravel lot) bounded exactly by the back of the footway kits and the junction corner outlines.
// Buildings are deliberately not generated.
import { MeshBuilder } from "./mesh.js";
import { roadPoint, footprint, stationRange } from "./query.js";
import { earClip } from "./junction.js";
import { madd, makeRng } from "./math.js";

export const PLOT_STYLES = ["paving", "concrete", "gravel", "grass"];
export function defaultBlockSettings() { return { enabled: true, inset: 0.3, seed: 7, styles: [...PLOT_STYLES], overrides: {} }; }

/** Trace every bounded face of the road graph as a list of half-edges [{R, dir, node}] (face on the left of travel). */
export function traceFaces(plan) {
  const faces = [];
  const visited = new Set();
  const jByNode = new Map((plan.junctions || []).map((J) => [J.node.id, J]));
  const key = (R, dir) => R.road.id + (dir > 0 ? "+" : "-");
  for (const R0 of plan.roads.values()) for (const dir0 of [1, -1]) {
    if (visited.has(key(R0, dir0))) continue;
    const loop = [];
    let R = R0, dir = dir0, guard = 0;
    while (guard++ < 5000) {
      const k = key(R, dir);
      if (visited.has(k)) break;
      visited.add(k);
      const endId = dir > 0 ? R.road.b : R.road.a;
      const N = plan.nodes.get(endId);
      const arms = N.arms || [];
      const i = arms.findIndex((a) => a.R === R && a.isStart === (dir < 0));
      if (i < 0) break;
      const n = arms.length;
      const j = (i + 1) % n;
      const J = jByNode.get(endId);
      const corner = n >= 2 && !N.weld && J && J.corners && J.corners.length === n ? J.corners[i] : null;
      loop.push({ R, dir, node: N, armIn: arms[i], armOut: arms[j], corner });
      R = arms[j].R; dir = arms[j].isStart ? 1 : -1;
      if (R === R0 && dir === dir0) break;
    }
    if (loop.length) faces.push(loop);
  }
  return faces;
}

/** Boundary polygon (XZ + height) of a face at `inset` metres behind the kit back. */
export function facePolygon(face, inset = 0.3, step = 2.5) {
  const pts = [];
  for (const h of face) {
    const { R, dir, node, armIn, corner } = h;
    const fp = footprint(R), rng = stationRange(R);
    const x = dir > 0 ? fp.left - inset : fp.right + inset;
    const L = rng.s1 - rng.s0; if (L <= 0) continue;
    const n = Math.max(1, Math.ceil(L / step));
    for (let k = 0; k <= n; k++) {
      const s = dir > 0 ? rng.s0 + (L * k) / n : rng.s1 - (L * k) / n;
      pts.push(roadPoint(R, s, x, 0));
    }
    if (corner && corner.path) {
      // mirror buildCornerKit: kit width blends linearly from arm A's kit to arm B's; arc samples offset radially
      const wA = armIn.kitRight.width, wB = h.armOut.kitLeft.width;
      const P = corner.path.pts, Nn = corner.path.normals, arc = corner.path.arc, m = P.length;
      const hBack = roadPoint(R, dir > 0 ? rng.s1 : rng.s0, x, 0)[1];
      for (let k = 1; k < m - 1; k++) {
        const d = wA + (wB - wA) * (k / (m - 1)) + inset;
        let q;
        if (arc && arc[k] && corner.C) { const C = corner.C, rr = Math.max(0.05, corner.R - d) / corner.R; q = [C[0] + (P[k][0] - C[0]) * rr, hBack, C[2] + (P[k][2] - C[2]) * rr]; }
        else q = madd(P[k], Nn[k], d);
        q[1] = hBack; pts.push(q);
      }
    } else if (!corner && (node.arms || []).length === 1) {
      // dead end: walk straight across the end row
      const other = dir > 0 ? fp.right + inset : fp.left - inset;
      const s = dir > 0 ? rng.s1 : rng.s0;
      pts.push(roadPoint(R, s + (dir > 0 ? inset : -inset), x, 0), roadPoint(R, s + (dir > 0 ? inset : -inset), other, 0));
    }
  }
  // drop near-duplicates
  const out = [];
  for (const q of pts) if (!out.length || Math.hypot(q[0] - out[out.length - 1][0], q[2] - out[out.length - 1][2]) > 0.08) out.push(q);
  while (out.length > 2 && Math.hypot(out[0][0] - out[out.length - 1][0], out[0][2] - out[out.length - 1][2]) < 0.08) out.pop();
  return out;
}

function signedArea(poly) { let a = 0; for (let i = 0; i < poly.length; i++) { const p = poly[i], q = poly[(i + 1) % poly.length]; a += p[0] * q[2] - q[0] * p[2]; } return a / 2; }

/**
 * Build block plots. Returns { mesh, plots:[{id, area, style, centre}] }.
 * S: { enabled, inset, seed, maxArea, styles: [..] }
 */
export function buildBlocks(plan, S = {}) {
  const mb = new MeshBuilder("blocks");
  const inset = S.inset ?? 0.3, rng = makeRng((S.seed ?? 7) + 101);
  const styles = S.styles || PLOT_STYLES;
  const plots = [];
  const faces = traceFaces(plan);
  // With the "next arm in ascending angle" traversal every bounded face comes out with negative signed
  // area in XZ; the unbounded outer face of each component is positive and is skipped.
  const polys = faces.map((f) => facePolygon(f, inset)).filter((p) => p.length >= 3);
  const areas = polys.map(signedArea);
  polys.forEach((poly, i) => {
    const area = -areas[i];
    if (area <= 20) return;
    if (S.maxArea && area > S.maxArea) return;
    const ys = poly.map((p) => p[1]);
    if (Math.max(...ys) - Math.min(...ys) > 0.9 || Math.max(...ys) > 1.2) return; // not a street-level block (viaduct / cutting)
    const id = "blk" + i;
    const style = (S.overrides && S.overrides[id]) || styles[Math.floor(rng() * styles.length)];
    const cx = poly.reduce((a, p) => a + p[0], 0) / poly.length, cz = poly.reduce((a, p) => a + p[2], 0) / poly.length;
    plots.push({ id, area, style, centre: [cx, 0, cz] });
    if (style === "grass") return; // park: the ground shows through
    const mat = mb.material(style);
    const top = poly.map((p) => [p[0], p[1] + 0.04, p[2]]); // a 4 cm upstand above the kit back keeps the slab clear of the terrain
    const ids = top.map((q) => mb.vertex(q, [q[0], q[2]], [0, 1, 0.5, 0]));
    for (const [a, b, c] of earClip(top)) mb.tri(mat, ids[a], ids[b], ids[c], id);
    // skirt down to the grass so the slab reads as a raised plot
    const bot = top.map((q) => [q[0], q[1] - 0.45, q[2]]);
    const bids = bot.map((q) => mb.vertex(q, [q[0], q[2]], [0, 1, 0.5, 0]));
    const rowT = [...ids, ids[0]], rowB = [...bids, bids[0]];
    mb.gridFacing(mb.material("concrete"), [rowT, rowB], id, (c) => [c[0] - cx, 0, c[2] - cz]);
  });
  mb.compact?.();
  return { mesh: mb, plots };
}
