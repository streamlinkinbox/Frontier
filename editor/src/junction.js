// Junction paving: 2D polygon union (Clipper) of every approach footprint, then
// triangulation. This is planar polygon clipping, not 3D boolean geometry.
import ClipperLib from 'clipper-lib';
import { ShapeUtils, Vector2 } from 'three';
import { normalAt, polygonArea, sliceS, cumLen } from './geometry.js';

const SCALE = 1000;
export const JUNCTION_Y = -0.01; // sit just below road surfaces to avoid z-fighting

// Distance from a junction centre along an approach at which the road ribbon begins.
export function trimDistance(edge) {
  const hw = Math.max(edge.section.hwL, edge.section.hwR);
  return Math.min(hw + 2, edge.length * 0.5);
}

// Polyline from the node outward along an edge, up to distance d.
export function approachFromNode(edge, endKey, d) {
  const pts = edge.pts;
  const cum = cumLen(pts);
  const L = cum[cum.length - 1];
  const dist = Math.min(d, L);
  if (endKey === 'a') return sliceS(pts, cum, 0, dist);
  return sliceS(pts, cum, L - dist, L).reverse();
}

// Ribbon polyline of an edge after trimming at junction ends.
export function trimmedEdgePts(edge, nodeKinds) {
  const cum = cumLen(edge.pts);
  const L = cum[cum.length - 1];
  const D = trimDistance(edge);
  const trimA = nodeKinds[edge.a] === 'junction' ? D : 0;
  const trimB = nodeKinds[edge.b] === 'junction' ? D : 0;
  return sliceS(edge.pts, cum, trimA, L - trimB);
}

// Paved outline of a junction as triangles in the xz plane.
export function junctionFootprint(node, edgesById) {
  const polys = [];
  let radius = 0;
  for (const eid of node.edges) {
    const e = edgesById[eid];
    const endKey = e.a === node.id ? 'a' : 'b';
    const D = trimDistance(e);
    const sl = approachFromNode(e, endKey, D + 0.3);
    const { hwL, hwR } = e.section;
    radius = Math.max(radius, hwL, hwR);
    const left = [];
    const right = [];
    for (let i = 0; i < sl.length; i++) {
      const [nx, nz] = normalAt(sl, i);
      left.push([sl[i][0] + nx * hwL, sl[i][1] + nz * hwL]);
      right.push([sl[i][0] - nx * hwR, sl[i][1] - nz * hwR]);
    }
    polys.push(left.concat(right.reverse()));
  }
  // Round core so the centre never leaves a hole.
  const core = [];
  for (let k = 0; k < 24; k++) {
    const a = (k / 24) * Math.PI * 2;
    core.push([node.x + Math.cos(a) * radius, node.z + Math.sin(a) * radius]);
  }
  polys.push(core);
  return triangulatePolygons(polys);
}

// Union a list of [x,z] polygons and triangulate the result.
export function triangulatePolygons(polys) {
  const subj = polys.map((poly) => {
    let p = poly;
    if (polygonArea(p) < 0) p = p.slice().reverse(); // normalise orientation for nonzero union
    return p.map(([x, z]) => ({ X: Math.round(x * SCALE), Y: Math.round(z * SCALE) }));
  });
  const clipper = new ClipperLib.Clipper();
  clipper.AddPaths(subj, ClipperLib.PolyType.ptSubject, true);
  const solution = [];
  clipper.Execute(
    ClipperLib.ClipType.ctUnion,
    solution,
    ClipperLib.PolyFillType.pftNonZero,
    ClipperLib.PolyFillType.pftNonZero,
  );
  const outers = [];
  const holes = [];
  for (const path of solution) {
    const pts = path.map((p) => [p.X / SCALE, p.Y / SCALE]);
    if (ClipperLib.Clipper.Orientation(path)) outers.push(pts);
    else holes.push(pts);
  }
  const vertices = [];
  const indices = [];
  for (const outer of outers) {
    const myHoles = holes.filter((h) => pointInPoly(h[0], outer));
    const base = vertices.length;
    const contour = outer.map((p) => new Vector2(p[0], p[1]));
    const holeVecs = myHoles.map((h) => h.map((p) => new Vector2(p[0], p[1])));
    const verts = outer.concat(...myHoles);
    // triangulateShape wants no closing duplicate; outer/holes are already open rings.
    const tris = ShapeUtils.triangulateShape(contour, holeVecs);
    for (const p of verts) vertices.push(p);
    for (const t of tris) indices.push(t[0] + base, t[1] + base, t[2] + base);
  }
  return { vertices, indices, area: outers.reduce((a, o) => a + Math.abs(polygonArea(o)), 0) };
}

function pointInPoly([x, z], poly) {
  let inside = false;
  for (let i = 0, j = poly.length - 1; i < poly.length; j = i++) {
    const [xi, zi] = poly[i];
    const [xj, zj] = poly[j];
    if (zi > z !== zj > z && x < ((xj - xi) * (z - zi)) / (zj - zi) + xi) inside = !inside;
  }
  return inside;
}
