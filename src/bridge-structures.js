import * as THREE from "three";
import { QuadMesh, roadY } from "./geometry.js";
import { terrainHeight } from "./model.js";
import { sweep } from "./transport-world.js";

const isDeck = (e) => e.bridgeNetwork && e.structure === "bridge";
const mid = (a, b) => a.map((v, i) => (v + b[i]) / 2);

// One welded deck skin across straight spans, curved returns and branch junctions.
// The source road trim/mouth coordinates are reused, not independently guessed.
export function networkDeck(result) {
  const mesh = new QuadMesh(),
    boundaries = [];
  const add = (points, owner) =>
    mesh.quad(
      points.map((v) => [v[0], v[1] - 0.05, v[2]]),
      owner,
    );
  for (const r of result.roads.filter((r) => isDeck(r.e))) {
    const rows = r.sections.map((row) => {
      const a = row[0],
        b = row.at(-1);
      return [
        [a[0] - r.nx * 0.9, a[1], a[2] - r.nz * 0.9],
        mid(a, b),
        [b[0] + r.nx * 0.9, b[1], b[2] + r.nz * 0.9],
      ];
    });
    for (let i = 1; i < rows.length; i++)
      for (let k = 0; k < 2; k++)
        add(
          [rows[i - 1][k], rows[i][k], rows[i][k + 1], rows[i - 1][k + 1]],
          r.e.id,
        );
    for (const k of [0, 2])
      boundaries.push({
        owner: r.e.id,
        points: rows.map((row) => row[k]),
        island: r.e.networkCurve === "roundabout-ring" && k === 2,
      });
  }
  for (const j of result.junctions.filter((j) =>
    j.arms.some((a) => isDeck(a.e)),
  )) {
    const boundary = [];
    j.arms.forEach((a, i) => {
      boundary.push(
        [a.right[0] + a.dz * 0.9, a.right[1], a.right[2] - a.dx * 0.9],
        [a.left[0] - a.dz * 0.9, a.left[1], a.left[2] + a.dx * 0.9],
      );
      const c = j.corners[i],
        outer = c.points.map((v, k) => [
          v[0] + c.normals[k][0] * 0.9,
          v[1],
          v[2] + c.normals[k][1] * 0.9,
        ]);
      boundary.push(...outer.slice(1, -1));
      boundaries.push({
        owner: `junction:${j.id}`,
        points: outer,
        island:
          a.e.networkCurve === "roundabout-ring" &&
          j.arms[(i + 1) % j.arms.length].e.networkCurve ===
            "roundabout-ring" &&
          a.corner?.turn < 0,
      });
    });
    const triangles = THREE.ShapeUtils.triangulateShape(
      boundary.map((v) => new THREE.Vector2(v[0], v[2])),
      [],
    );
    for (const ids of triangles) {
      const [a, b, c] = ids.map((i) => boundary[i]);
      if (
        Math.abs(
          (b[0] - a[0]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[0] - a[0]),
        ) < 1e-9
      )
        continue;
      const ab = mid(a, b),
        bc = mid(b, c),
        ca = mid(c, a),
        center = a.map((v, k) => (v + b[k] + c[k]) / 3);
      add([a, ab, center, ca], `junction:${j.id}`);
      add([b, bc, center, ab], `junction:${j.id}`);
      add([c, ca, center, bc], `junction:${j.id}`);
    }
  }
  return { mesh, boundaries };
}
export function deckVolume(mesh) {
  const n = mesh.vertices.length,
    positions = [
      ...mesh.vertices.flat(),
      ...mesh.vertices.flatMap((v) => [v[0], v[1] - 1.1, v[2]]),
    ],
    indices = [],
    edges = new Map();
  for (const f of mesh.faces) {
    indices.push(
      f[0],
      f[1],
      f[2],
      f[0],
      f[2],
      f[3],
      f[2] + n,
      f[1] + n,
      f[0] + n,
      f[3] + n,
      f[2] + n,
      f[0] + n,
    );
    for (let i = 0; i < 4; i++) {
      const a = f[i],
        b = f[(i + 1) % 4],
        key = [a, b].sort((x, y) => x - y).join(":");
      if (edges.has(key)) edges.get(key).count++;
      else edges.set(key, { a, b, count: 1 });
    }
  }
  for (const { a, b, count } of edges.values())
    if (count === 1) indices.push(b, a, a + n, b, a + n, b + n);
  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute(
    "position",
    new THREE.Float32BufferAttribute(positions, 3),
  );
  geometry.setAttribute(
    "uv",
    new THREE.Float32BufferAttribute(
      Array.from({ length: positions.length / 3 }, (_, i) => [
        positions[i * 3] / 1.2,
        positions[i * 3 + 2] / 1.2,
      ]).flat(),
      2,
    ),
  );
  geometry.setIndex(indices);
  geometry.computeVertexNormals();
  return geometry;
}
export function networkPiers(p, result) {
  const piers = [];
  for (const r of result.roads.filter((r) => isDeck(r.e))) {
    const count = Math.max(1, Math.ceil(r.length / 38));
    for (let i = 0; i < count; i++) {
      const t = (i + 0.5) / count,
        x = r.a.x + (r.b.x - r.a.x) * t,
        z = r.a.z + (r.b.z - r.a.z) * t,
        top = roadY(p, r.e, t) - 1.15,
        ground = terrainHeight(x, z, p.brushes);
      if (top - ground < 1.8) continue;
      const clear = result.roads.every((o) => {
        const s = Math.max(
          0,
          Math.min(o.length, (x - o.a.x) * o.dx + (z - o.a.z) * o.dz),
        );
        if (roadY(p, o.e, s / o.length) >= top - 0.5) return true;
        return (
          Math.hypot(x - o.a.x - s * o.dx, z - o.a.z - s * o.dz) >
          o.w / 2 + o.e.sidewalk + 4
        );
      });
      if (clear)
        piers.push({
          x,
          z,
          top,
          ground,
          angle: Math.atan2(r.dx, r.dz),
          owner: r.e.id,
        });
    }
  }
  return piers;
}
export function buildBridgeNetworks(w) {
  w.networkDeck = null;
  w.networkPiers = [];
  w.networkIslandDeck = null;
  w.networkIslandPiers = [];
  if (!w.p.edges.some(isDeck)) return;
  const { mesh, boundaries } = networkDeck(w.result);
  w.networkDeck = mesh;
  w.networkPiers = networkPiers(w.p, w.result);
  w.batch(deckVolume(mesh), "concrete", "details");
  for (const boundary of boundaries) {
    // No transverse barriers across the connecting mouths. Clip low ramp portions.
    let rows = [];
    const height = boundary.island ? 0.22 : 0.9;
    const flush = () => {
      if (rows.length > 1)
        sweep(w, rows, 0, 0.3, height, "concrete", "details");
      rows = [];
    };
    for (const v of boundary.points) {
      if (v[1] - terrainHeight(v[0], v[2], w.p.brushes) < 0.8) {
        flush();
        continue;
      }
      rows.push({ x: v[0], y: v[1] + height / 2, z: v[2] });
    }
    flush();
  }
  buildRoundaboutIsland(w, boundaries);
  for (const p of w.networkPiers) {
    const h = p.top - p.ground - 0.45;
    w.box(p.x, p.ground - 0.2, p.z, 3, 0.6, 3, "concrete");
    w.box(p.x, p.ground + h / 2, p.z, 1.4, h, 1.4, "concrete");
    w.box(
      p.x,
      p.top - 0.225,
      p.z,
      6,
      0.45,
      1.5,
      "concrete",
      "details",
      p.angle,
    );
  }
  for (const [i, r] of w.result.roads.filter((r) => isDeck(r.e)).entries()) {
    if (i % 3) continue;
    const t = (r.from + r.to) / 2,
      offset = r.w / 2 + 1.06,
      x = r.a.x + (r.b.x - r.a.x) * t + r.nx * offset,
      z = r.a.z + (r.b.z - r.a.z) * t + r.nz * offset,
      y = roadY(w.p, r.e, t);
    w.box(x, y + 3, z, 0.12, 6, 0.12, "rail");
    w.box(
      x - r.nx * 0.65,
      y + 6,
      z - r.nz * 0.65,
      1.4,
      0.15,
      0.3,
      "white",
      "details",
      Math.atan2(r.dx, r.dz),
    );
  }
  // Edge lines follow the exact junction boundaries, including merge noses.
  for (const j of w.result.junctions.filter((j) =>
    j.arms.some((a) => a.e.bridgeNetwork && a.e.markings),
  )) {
    for (const c of j.corners)
      for (let i = 1; i < c.points.length; i++) {
        const point = (k) => [
          c.points[k][0] - c.normals[k][0] * 0.25,
          c.points[k][1] + 0.045,
          c.points[k][2] - c.normals[k][1] * 0.25,
        ];
        w.strip(point(i - 1), point(i), 0.14, 0.012, "white", "markings");
      }
    if (j.degree !== 2 || !j.arms.every((a) => a.e.bridgeNetwork)) continue;
    const [a, b] = j.arms,
      P = mid(a.left, a.right),
      Q = mid(b.left, b.right),
      c1 = [P[0] - a.dx * a.trim * 0.7, P[1], P[2] - a.dz * a.trim * 0.7],
      c2 = [Q[0] - b.dx * b.trim * 0.7, Q[1], Q[2] - b.dz * b.trim * 0.7];
    const at = (t) => {
      const s = 1 - t;
      return P.map(
        (v, k) =>
          s * s * s * v +
          3 * s * s * t * c1[k] +
          3 * s * t * t * c2[k] +
          t * t * t * Q[k] +
          (k === 1 ? 0.045 : 0),
      );
    };
    if (
      j.arms.every((a) => a.e.networkFlow === "forward") &&
      j.arms.some((a) => !a.start && a.e.networkArrow)
    ) {
      const left = at(0.49),
        right = at(0.51),
        length = Math.hypot(right[0] - left[0], right[2] - left[2]),
        dx = (right[0] - left[0]) / length,
        dz = (right[2] - left[2]) / length,
        m = at(0.5),
        dir = a.start ? -1 : 1;
      for (const side of [-1, 1])
        w.arrow(
          [
            m[0] - (dz * side * a.width) / 4,
            m[1] + 0.015,
            m[2] + (dx * side * a.width) / 4,
          ],
          dx * dir,
          dz * dir,
        );
    }
    const length = Math.hypot(P[0] - Q[0], P[2] - Q[2]),
      steps = Math.max(2, Math.ceil(length / 2));
    for (let i = 1; i <= steps; i++)
      if (i % 3 !== 0)
        w.strip(
          at((i - 1) / steps),
          at(i / steps),
          0.15,
          0.012,
          "white",
          "markings",
        );
  }
}

// A covered, landscaped central deck distinguishes the roundabout from an open loop.
// Its perimeter follows the live ring boundaries; a 60 mm expansion joint avoids
// overlapping slab shells. Supports flank (never stand in) the through-road.
function buildRoundaboutIsland(w, boundaries) {
  const points = boundaries.filter((b) => b.island).flatMap((b) => b.points);
  if (points.length < 12) return;
  const ring = w.result.roads.filter(
    (r) => r.e.networkCurve === "roundabout-ring",
  );
  const x = ring.reduce((s, r) => s + r.a.x, 0) / ring.length,
    z = ring.reduce((s, r) => s + r.a.z, 0) / ring.length,
    y = points.reduce((s, v) => s + v[1], 0) / points.length;
  const sorted = points
    .map((v) => {
      const d = Math.hypot(v[0] - x, v[2] - z);
      return [
        v[0] - ((v[0] - x) * 0.06) / d,
        v[1] - 0.05,
        v[2] - ((v[2] - z) * 0.06) / d,
      ];
    })
    .sort(
      (a, b) => Math.atan2(a[2] - z, a[0] - x) - Math.atan2(b[2] - z, b[0] - x),
    );
  const outline = sorted.filter(
    (v, i) =>
      !i || Math.hypot(v[0] - sorted[i - 1][0], v[2] - sorted[i - 1][2]) > 1e-5,
  );
  const mesh = new QuadMesh(),
    center = [x, y - 0.05, z];
  for (let i = 0; i < outline.length; i++) {
    const a = outline[i],
      b = outline[(i + 1) % outline.length],
      ab = mid(a, b),
      bc = mid(b, center),
      ca = mid(center, a),
      m = a.map((v, k) => (v + b[k] + center[k]) / 3);
    mesh.quad([a, ab, m, ca], "island");
    mesh.quad([b, bc, m, ab], "island");
    mesh.quad([center, ca, m, bc], "island");
  }
  w.networkIslandDeck = mesh;
  w.networkIslandPiers = [];
  w.batch(deckVolume(mesh), "concrete", "details");
  const radius =
    Math.min(...outline.map((v) => Math.hypot(v[0] - x, v[2] - z))) - 7;
  if (radius < 12) return;
  const grass = new THREE.CylinderGeometry(radius, radius, 0.24, 80);
  grass.translate(x, y + 0.1, z);
  w.batch(grass, "tree2", "vegetation");
  w.strip(
    [x - radius, y + 0.23, z],
    [x + radius, y + 0.23, z],
    3.2,
    0.04,
    "concrete",
  );
  w.strip(
    [x, y + 0.23, z - radius],
    [x, y + 0.23, z - 1.6],
    3.2,
    0.04,
    "concrete",
  );
  w.strip(
    [x, y + 0.23, z + 1.6],
    [x, y + 0.23, z + radius],
    3.2,
    0.04,
    "concrete",
  );
  const plaza = new THREE.CylinderGeometry(9, 9, 0.32, 48);
  plaza.translate(x, y + 0.35, z);
  w.batch(plaza, "concrete", "details");
  w.box(x, y + 1.5, z, 3, 2.4, 3, "bridgeSteel");
  for (let i = 0; i < 12; i++) {
    const a = (2 * Math.PI * (i + 0.5)) / 12;
    w.tree(
      x + Math.cos(a) * radius * 0.72,
      y + 0.24,
      z + Math.sin(a) * radius * 0.72,
      3,
    );
  }
  for (const dx of [-50, -25, 25, 50])
    for (const dz of [-40, 0, 40]) {
      if (Math.hypot(dx, dz) > radius) continue;
      const px = x + dx,
        pz = z + dz,
        ground = terrainHeight(px, pz, w.p.brushes),
        top = y - 1.15;
      const clear = w.result.roads.every((r) => {
        const t = Math.max(
          0,
          Math.min(1, ((px - r.a.x) * r.dx + (pz - r.a.z) * r.dz) / r.length),
        );
        return (
          roadY(w.p, r.e, t) >= top - 0.5 ||
          Math.hypot(
            px - r.a.x - t * r.length * r.dx,
            pz - r.a.z - t * r.length * r.dz,
          ) >
            r.w / 2 + 4
        );
      });
      if (clear && top > ground + 1.8) {
        w.networkIslandPiers.push({ x: px, z: pz, ground, top });
        const h = top - ground;
        w.box(px, ground + h / 2, pz, 1.6, h, 1.6, "concrete");
      }
    }
}
