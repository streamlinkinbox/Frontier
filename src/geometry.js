import { railCrossings } from "./transport-geometry.js";
import {
  modern,
  streetLayout,
  landscapePlan,
  surfaceAt,
} from "./landscape-layout.js";
import * as THREE from "three";
import { normalizedFaceUV, objTextureCoordinates } from "./surface-uv.js";
import { allocateJunctionTrims, buildCorner } from "./corner-layout.js";
import {
  dist,
  roadWidth,
  terrainHeight,
  segmentIntersection,
} from "./model.js";
const lerp = (a, b, t) => a + (b - a) * t;
const mid = (a, b) => a.map((v, i) => (v + b[i]) / 2);
export class QuadMesh {
  constructor() {
    this.vertices = [];
    this.faces = [];
    this.groups = [];
    this.uvs = [];
    this.surfaceKinds = [];
    this.map = new Map();
  }
  vertex(v) {
    const key = v.map((n) => Math.round(n * 1e5)).join(":");
    if (this.map.has(key)) return this.map.get(key);
    const i = this.vertices.length;
    this.vertices.push(v);
    this.map.set(key, i);
    return i;
  }
  quad(points, owner, uvs = null) {
    let ids = points.map((v) => this.vertex(v));
    const a = points[0],
      b = points[1],
      c = points[2];
    if ((b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]) > 0) {
      ids.reverse();
      if (uvs) uvs = [...uvs].reverse();
    }
    this.faces.push(ids);
    this.groups.push(owner);
    this.uvs.push(uvs);
    this.surfaceKinds[this.faces.length - 1] = "asphalt";
  }
  geometry(owner = null, surface = null) {
    const indices = [],
      uv = [],
      positions = [],
      remap = new Map();
    const local = (id) => {
      if (remap.has(id)) return remap.get(id);
      const index = positions.length / 3;
      positions.push(...this.vertices[id]);
      remap.set(id, index);
      return index;
    };
    this.faces.forEach((f, i) => {
      if (owner !== null && this.groups[i] !== owner) return;
      if (surface !== null && this.surfaceKinds[i] !== surface) return;
      const ids = f.map(local);
      indices.push(ids[0], ids[1], ids[2], ids[0], ids[2], ids[3]);
      const coords = normalizedFaceUV(this, i);
      for (const k of [0, 1, 2, 0, 2, 3]) uv.push(...coords[k]);
    });
    const geo = new THREE.BufferGeometry();
    geo.setAttribute(
      "position",
      new THREE.Float32BufferAttribute(positions, 3),
    );
    geo.setIndex(indices);
    geo.computeVertexNormals();
    const flat = geo.toNonIndexed();
    geo.dispose();
    flat.setAttribute("uv", new THREE.Float32BufferAttribute(uv, 2));
    return flat;
  }
}
export function roadY(p, e, t) {
  const a = p.nodes.find((n) => n.id === e.a),
    b = p.nodes.find((n) => n.id === e.b);
  const x = lerp(a.x, b.x, t),
    z = lerp(a.z, b.z, t);
  if (e.elevationMode === "linear")
    return lerp(
      a.elevation ?? terrainHeight(a.x, a.z, p.brushes) + 0.12,
      b.elevation ?? terrainHeight(b.x, b.z, p.brushes) + 0.12,
      t,
    );
  return e.structure === "bridge"
    ? lerp(
        a.elevation ?? terrainHeight(a.x, a.z, p.brushes) + 0.12,
        b.elevation ?? terrainHeight(b.x, b.z, p.brushes) + 0.12,
        t,
      ) +
        Math.sin(t * Math.PI) * (e.bridgeRise ?? 3.6)
    : terrainHeight(x, z, p.brushes) + 0.12;
}
export function generateRoadMesh(p) {
  const crossings = railCrossings(p);
  const mesh = new QuadMesh(),
    nodes = new Map(p.nodes.map((n) => [n.id, n])),
    joins = new Map(p.nodes.map((n) => [n.id, []]));
  for (const e of p.edges) {
    const a = nodes.get(e.a),
      b = nodes.get(e.b),
      length = dist(a, b);
    if (length < 0.01) continue;
    for (const [n, other, start] of [
      [a, b, true],
      [b, a, false],
    ]) {
      const dx = (other.x - n.x) / length,
        dz = (other.z - n.z) / length;
      joins.get(n.id).push({
        e,
        dx,
        dz,
        length,
        start,
        angle: Math.atan2(dz, dx),
        width: roadWidth(e),
      });
    }
  }
  allocateJunctionTrims(joins, nodes);
  const trims = new Map(),
    junctions = [];
  for (const [id, arms] of joins) {
    arms.sort((a, b) => a.angle - b.angle);
    if (arms.length < 2) {
      if (arms.length) trims.set(`${arms[0].e.id}:${id}`, 0);
      continue;
    }
    const node = nodes.get(id);
    const radius = Math.max(...arms.map((a) => a.trim));
    const boundary = [],
      corners = [];
    for (const arm of arms) {
      trims.set(`${arm.e.id}:${id}`, arm.trim);
      const t = arm.start ? arm.trim / arm.length : 1 - arm.trim / arm.length,
        y = roadY(p, arm.e, t);
      const cx = node.x + arm.dx * arm.trim,
        cz = node.z + arm.dz * arm.trim,
        nx = -arm.dz,
        nz = arm.dx;
      arm.right = [cx - (nx * arm.width) / 2, y, cz - (nz * arm.width) / 2];
      arm.left = [cx + (nx * arm.width) / 2, y, cz + (nz * arm.width) / 2];
      const offsets = modern(arm.e)
        ? streetLayout(arm.e).base
        : [-arm.width / 2, arm.width / 2];
      arm.mouthBase = offsets.map((o) => [cx + nx * o, y, cz + nz * o]);
      arm.mouthBase[0] = arm.right;
      arm.mouthBase[arm.mouthBase.length - 1] = arm.left;
      arm.mouth = arm.mouthBase.flatMap((v, i) =>
        i ? [mid(arm.mouthBase[i - 1], v), v] : [v],
      );
    }
    for (let i = 0; i < arms.length; i++) {
      const arm = arms[i],
        next = arms[(i + 1) % arms.length];
      boundary.push(arm.right, arm.left);
      const corner = buildCorner(arm, next, node);
      boundary.push(...corner.points.slice(1, -1));
      corners.push(corner);
    }
    const contour = boundary.map((v) => new THREE.Vector2(v[0], v[2]));
    const tris = THREE.ShapeUtils.triangulateShape(contour, []);
    const triangles = [];
    for (const tri of tris) {
      const points = tri.map((i) => boundary[i]);
      const split = arms.find(
        (a) =>
          modern(a.e) && points.includes(a.right) && points.includes(a.left),
      );
      if (!split) {
        triangles.push(points);
        continue;
      }
      const tip = points.find((v) => v !== split.right && v !== split.left);
      for (let k = 1; k < split.mouthBase.length; k++)
        triangles.push([split.mouthBase[k - 1], split.mouthBase[k], tip]);
    }
    for (const [a, b, c] of triangles) {
      const ab = mid(a, b),
        bc = mid(b, c),
        ca = mid(c, a),
        center = a.map((v, k) => (v + b[k] + c[k]) / 3);
      mesh.quad([a, ab, center, ca], `junction:${id}`);
      mesh.quad([b, bc, center, ab], `junction:${id}`);
      mesh.quad([c, ca, center, bc], `junction:${id}`);
    }
    junctions.push({
      id,
      node,
      arms,
      boundary,
      corners,
      radius,
      degree: arms.length,
    });
  }
  const roads = [];
  for (const e of p.edges) {
    const a = nodes.get(e.a),
      b = nodes.get(e.b),
      length = dist(a, b);
    if (length < 0.01) continue;
    const dx = (b.x - a.x) / length,
      dz = (b.z - a.z) / length,
      nx = -dz,
      nz = dx,
      w = roadWidth(e),
      from = (trims.get(`${e.id}:${a.id}`) || 0) / length,
      to = 1 - (trims.get(`${e.id}:${b.id}`) || 0) / length;
    const layout = streetLayout(e),
      isModern = modern(e),
      degree = (id) => joins.get(id)?.length ?? 1;
    const plan = isModern
      ? landscapePlan(e, from, to, length, degree(e.a), degree(e.b))
      : null;
    const steps = Math.max(1, Math.ceil(((to - from) * length) / 6));
    const samples = isModern
      ? plan.stations.map((s) => s / length)
      : Array.from({ length: steps + 1 }, (_, i) => lerp(from, to, i / steps));
    const decks = crossings
      .filter((c) => c.roadId === e.id && e.sidewalk === 0 && !isModern)
      .map((c) => ({
        ...c,
        half:
          (c.railHalf + 0.65 + (w / 2) * Math.sqrt(1 - c.sine * c.sine)) /
          c.sine,
      }));
    for (const c of decks)
      for (const s of [c.sRoad - c.half, c.sRoad + c.half])
        if (s / length > from + 1e-5 && s / length < to - 1e-5)
          samples.push(s / length);
    samples.sort((a, b) => a - b);
    for (let i = samples.length - 1; i > 0; i--)
      if (samples[i] - samples[i - 1] < 1e-6) samples.splice(i, 1);
    const offsets = isModern ? layout.offsets : [-w / 2, 0, w / 2],
      sections = [];
    for (let i = 0; i < samples.length; i++) {
      const t = samples[i],
        x = lerp(a.x, b.x, t),
        z = lerp(a.z, b.z, t),
        y = roadY(p, e, t);
      let row = offsets.map((o) => [
        x + nx * o,
        y + (isModern ? surfaceAt(layout, plan, t * length, o).height : 0),
        z + nz * o,
      ]);
      if (isModern && (i === 0 || i === samples.length - 1)) {
        const nodeId = i === 0 ? e.a : e.b,
          arm = joins.get(nodeId)?.find((arm) => arm.e.id === e.id);
        if (arm?.mouth) row = i === 0 ? arm.mouth : [...arm.mouth].reverse();
      }
      sections.push(row);
      if (!i) continue;
      for (let k = 0; k < offsets.length - 1; k++) {
        const prev = sections[i - 1];
        mesh.quad([prev[k], row[k], row[k + 1], prev[k + 1]], e.id, [
          [samples[i - 1] * length, offsets[k]],
          [t * length, offsets[k]],
          [t * length, offsets[k + 1]],
          [samples[i - 1] * length, offsets[k + 1]],
        ]);
        if (
          decks.some(
            (c) =>
              Math.abs(((samples[i - 1] + t) * length) / 2 - c.sRoad) < c.half,
          )
        )
          mesh.surfaceKinds[mesh.faces.length - 1] = "crossingDeck";
        if (isModern)
          mesh.surfaceKinds[mesh.faces.length - 1] = surfaceAt(
            layout,
            plan,
            ((samples[i - 1] + t) * length) / 2,
            (offsets[k] + offsets[k + 1]) / 2,
          ).kind;
      }
    }
    roads.push({
      e,
      a,
      b,
      length,
      dx,
      dz,
      nx,
      nz,
      w,
      from,
      to,
      sections,
      samples,
      layout,
      landscape: plan,
    });
  }
  return { mesh, roads, junctions, trims };
}
export function auditMesh(result, p) {
  const { mesh } = result,
    issues = [],
    edges = new Map(),
    dupes = new Set();
  let degenerate = 0,
    duplicate = 0,
    nonmanifold = 0,
    nonConvex = 0,
    selfIntersections = 0;
  for (const f of mesh.faces) {
    const key = [...f].sort((a, b) => a - b).join(",");
    if (dupes.has(key)) duplicate++;
    dupes.add(key);
    if (new Set(f).size !== 4) {
      degenerate++;
      continue;
    }
    let area = 0;
    for (let i = 0; i < 4; i++) {
      const a = mesh.vertices[f[i]],
        b = mesh.vertices[f[(i + 1) % 4]];
      area += a[0] * b[2] - b[0] * a[2];
      const edge = [f[i], f[(i + 1) % 4]].sort((a, b) => a - b).join(":");
      edges.set(edge, (edges.get(edge) || 0) + 1);
    }
    if (Math.abs(area) < 1e-6) degenerate++;
    const signs = f.map((_, i) => {
      const a = mesh.vertices[f[i]],
        b = mesh.vertices[f[(i + 1) % 4]],
        c = mesh.vertices[f[(i + 2) % 4]];
      return (b[0] - a[0]) * (c[2] - b[2]) - (b[2] - a[2]) * (c[0] - b[0]);
    });
    if (signs.some((v) => v > 1e-7) && signs.some((v) => v < -1e-7))
      nonConvex++;
  }
  for (const count of edges.values()) if (count > 2) nonmanifold++;
  if (degenerate) issues.push(`${degenerate} degenerate faces`);
  if (duplicate) issues.push(`${duplicate} duplicate faces`);
  if (nonmanifold) issues.push(`${nonmanifold} non-manifold edges`);
  for (const j of result.junctions) {
    const points = j.boundary.map((v) => ({ x: v[0], z: v[2] })),
      n = points.length;
    for (let a = 0; a < n; a++)
      for (let b = a + 2; b < n; b++) {
        if (a === 0 && b === n - 1) continue;
        const hit = segmentIntersection(
          points[a],
          points[(a + 1) % n],
          points[b],
          points[(b + 1) % n],
        );
        if (
          hit &&
          hit.t > 1e-6 &&
          hit.t < 1 - 1e-6 &&
          hit.u > 1e-6 &&
          hit.u < 1 - 1e-6
        )
          selfIntersections++;
      }
  }
  if (nonConvex) issues.push(`${nonConvex} non-convex quad faces`);
  if (selfIntersections)
    issues.push(`${selfIntersections} self-intersecting junction boundaries`);
  const nodeMap = new Map(p.nodes.map((n) => [n.id, n]));
  let crossings = 0,
    short = 0;
  for (let i = 0; i < p.edges.length; i++) {
    const e = p.edges[i],
      a = nodeMap.get(e.a),
      b = nodeMap.get(e.b);
    if (dist(a, b) < roadWidth(e) * 1.5) short++;
    if (e.structure !== "surface") continue;
    for (let j = i + 1; j < p.edges.length; j++) {
      const f = p.edges[j];
      if (
        f.structure !== "surface" ||
        [f.a, f.b].some((id) => id === e.a || id === e.b)
      )
        continue;
      const hit = segmentIntersection(a, b, nodeMap.get(f.a), nodeMap.get(f.b));
      if (hit) crossings++;
    }
  }
  let seamErrors = 0;
  for (const j of result.junctions)
    for (const arm of j.arms) {
      const ids = arm.mouth.map((v) =>
        mesh.map.get(v.map((n) => Math.round(n * 1e5)).join(":")),
      );
      for (let i = 0; i < ids.length - 1; i++) {
        const key = [ids[i], ids[i + 1]].sort((a, b) => a - b).join(":");
        if (
          ids[i] === undefined ||
          ids[i + 1] === undefined ||
          edges.get(key) !== 2
        )
          seamErrors++;
      }
    }
  if (seamErrors) issues.push(`${seamErrors} unwelded junction mouth edges`);
  if (crossings) issues.push(`${crossings} unjoined centerline crossings`);
  if (short)
    issues.push(`${short} very short segments: inspect junction clearance`);
  return {
    issues,
    degenerate,
    duplicate,
    nonmanifold,
    crossings,
    short,
    seamErrors,
    nonConvex,
    selfIntersections,
    quads: mesh.faces.length,
    vertices: mesh.vertices.length,
    boundaries: [...edges.values()].filter((n) => n === 1).length,
  };
}
export function exportOBJ(result, p) {
  let text = `# Frontier | ${p.name}\n# Road surface only; meters; Y up. Quad faces retained.\no RoadNetwork\n`;
  for (const v of result.mesh.vertices)
    text += `v ${v.map((n) => n.toFixed(5)).join(" ")}\n`;
  text += objTextureCoordinates(result.mesh);
  let last = "",
    lastSurface = "";
  result.mesh.faces.forEach((f, i) => {
    const surface = result.mesh.surfaceKinds[i];
    if (surface !== lastSurface) {
      text += `usemtl ${surface}\n`;
      lastSurface = surface;
    }
    const group = result.mesh.groups[i];
    if (group !== last) {
      text += `g ${group.replace(":", "_")}\n`;
      last = group;
    }
    text += `f ${f.map((n, k) => `${n + 1}/${i * 4 + k + 1}`).join(" ")}\n`;
  });
  return text;
}
