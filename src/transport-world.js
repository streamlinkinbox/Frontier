import { gutterWidth } from "./streetscape.js";
import * as THREE from "three";
import { terrainHeight, dist } from "./model.js";
import { roadY } from "./geometry.js";
import { corridorPoints, railCrossings } from "./transport-geometry.js";

// One indexed rectangular sweep: shared vertices at each bend, no overlapping beam boxes.
export function sweep(w, rows, offset, width, height, mat, layer = "rail") {
  if (rows.length < 2) return;
  const positions = [],
    indices = [];
  rows.forEach((p, i) => {
    const prev = rows[Math.max(0, i - 1)],
      next = rows[Math.min(rows.length - 1, i + 1)],
      len = Math.hypot(next.x - prev.x, next.z - prev.z) || 1,
      nx = p.tangent ? -p.tangent.dz : -(next.z - prev.z) / len,
      nz = p.tangent ? p.tangent.dx : (next.x - prev.x) / len;
    for (const [u, y] of [
      [offset - width / 2, 0],
      [offset + width / 2, 0],
      [offset + width / 2, height],
      [offset - width / 2, height],
    ])
      positions.push(p.x + nx * u, p.y + y, p.z + nz * u);
    if (i)
      for (let k = 0; k < 4; k++) {
        const a = (i - 1) * 4 + k,
          b = (i - 1) * 4 + ((k + 1) % 4),
          c = i * 4 + ((k + 1) % 4),
          d = i * 4 + k;
        indices.push(a, b, c, a, c, d);
      }
  });
  const end = (rows.length - 1) * 4;
  indices.push(0, 2, 1, 0, 3, 2, end, end + 1, end + 2, end, end + 2, end + 3);
  for (let i = 0; i < indices.length; i += 3)
    [indices[i + 1], indices[i + 2]] = [indices[i + 2], indices[i + 1]];
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
  g.setIndex(indices);
  g.setAttribute(
    "uv",
    new THREE.Float32BufferAttribute(
      new Float32Array((positions.length / 3) * 2),
      2,
    ),
  );
  g.computeVertexNormals();
  const flat = g.toNonIndexed();
  flat.computeVertexNormals();
  w.batch(flat, mat, layer);
  g.dispose();
}
function resample(points) {
  let station = 0;
  const rows = [{ ...points[0], s: 0 }];
  for (let i = 1; i < points.length; i++) {
    const a = points[i - 1],
      b = points[i],
      l = dist(a, b),
      steps = Math.max(1, Math.ceil(l / 2));
    for (let j = 1; j <= steps; j++)
      rows.push({
        x: a.x + ((b.x - a.x) * j) / steps,
        z: a.z + ((b.z - a.z) * j) / steps,
        s: station + (l * j) / steps,
      });
    station += l;
  }
  return rows;
}
export function buildTransportRail(w) {
  const p = w.p,
    crossings = railCrossings(p);
  w.railCrossings = crossings;
  for (const c of p.corridors || []) {
    const own = crossings.filter((x) => x.corridorId === c.id),
      rows = resample(corridorPoints(p, c));
    for (const v of rows) {
      v.y = terrainHeight(v.x, v.z, p.brushes) + c.elevation;
      for (const cross of own) {
        const r = w.result.roads.find((r) => r.e.id === cross.roadId),
          distance = Math.abs(v.s - cross.station),
          half = cross.roadHalf / cross.sine,
          blend = Math.max(0, Math.min(1, (half + 8 - distance) / 8));
        if (blend) {
          const t = ((v.x - r.a.x) * r.dx + (v.z - r.a.z) * r.dz) / r.length;
          v.y += (roadY(p, r.e, Math.max(0, Math.min(1, t))) - v.y) * blend;
        }
      }
    }
    const source = p.edges.find((e) => e.id === c.sourceEdge),
      port = p.nodes.find((n) => n.id === c.sourceNode);
    if (source && port) {
      const a = p.nodes.find(
          (n) => n.id === (source.a === port.id ? source.b : source.a),
        ),
        l = dist(a, port);
      rows[0].tangent = { dx: (port.x - a.x) / l, dz: (port.z - a.z) / l };
    }
    if (c.kind === "monorail") {
      sweep(
        w,
        rows.map((v) => ({ ...v, y: v.y - 0.6 })),
        0,
        0.9,
        1.3,
        "concrete",
      );
    } else {
      const nearCross = (v) =>
        own.some((c) => Math.abs(v.s - c.station) < c.roadHalf / c.sine + 1);
      let chunk = [];
      const flush = () => {
        sweep(w, chunk, 0, 5.6, 0.22, c.elevation > 2 ? "concrete" : "railbed");
        chunk = [];
      };
      for (const v of rows) {
        if (nearCross(v)) {
          flush();
          continue;
        }
        chunk.push({ ...v, y: v.y - 0.27 });
      }
      flush();
      const spacing = c.trackSpacing || 3.3,
        gauge = c.gauge || 1.435;
      for (const center of [-spacing / 2, spacing / 2])
        for (const side of [-1, 1])
          sweep(
            w,
            rows.map((v) => ({ ...v, y: v.y + 0.003 })),
            center + (side * gauge) / 2,
            0.075,
            0.012,
            "rail",
          );
      let sleeper = 0;
      for (let i = 0; i < rows.length - 1; i++) {
        const v = rows[i],
          b = rows[i + 1];
        if (v.s < sleeper || nearCross(v)) continue;
        sleeper = v.s + 1.4;
        w.box(
          v.x,
          v.y - 0.075,
          v.z,
          5.1,
          0.12,
          0.24,
          "railbed",
          "rail",
          Math.atan2(b.x - v.x, b.z - v.z),
        );
      }
    }
    if (c.elevation > 2) {
      let next = 18;
      const underside = c.kind === "monorail" ? -0.6 : -0.27;
      for (const v of rows) {
        if (v.s < next) continue;
        next += 28;
        const road = w.result.roads.find(
          (r) =>
            r.e.structure !== "bridge" &&
            Math.abs((v.x - r.a.x) * r.nx + (v.z - r.a.z) * r.nz) <
              r.w / 2 + r.e.sidewalk + 2 &&
            (v.x - r.a.x) * r.dx + (v.z - r.a.z) * r.dz > 0 &&
            (v.x - r.a.x) * r.dx + (v.z - r.a.z) * r.dz < r.length,
        );
        const pier = (x, z) => {
          const ground = terrainHeight(x, z, p.brushes),
            h = v.y + underside - 0.7 - ground;
          if (h > 0)
            w.box(x, ground + h / 2, z, 0.8, h, 0.8, "concrete", "rail");
        };
        if (road) {
          const off = road.w / 2 + road.e.sidewalk + 1.8;
          for (const side of [-1, 1])
            pier(v.x + road.nx * off * side, v.z + road.nz * off * side);
          w.strip(
            [v.x - road.nx * off, v.y + underside - 0.7, v.z - road.nz * off],
            [v.x + road.nx * off, v.y + underside - 0.7, v.z + road.nz * off],
            1,
            0.7,
            "concrete",
            "rail",
          );
        } else {
          pier(v.x, v.z);
          w.box(
            v.x,
            v.y + underside - 0.35,
            v.z,
            c.kind === "rail" ? 5.4 : 1.8,
            0.7,
            1.3,
            "concrete",
            "rail",
          );
        }
      }
    }
  }
  // Keep the original elevated railway's stationary scale-reference train.
  if (p.corridors?.some((c) => c.id === "east-rail"))
    for (let i = 0; i < 3; i++) {
      const z = -100 + i * 9,
        x = 192 + Math.sin(z / 95) * 12,
        y = terrainHeight(x, z, p.brushes) + 11.5;
      w.box(x - 1.65, y, z, 1.9, 2.5, 8.2, "building", "rail");
      w.box(x - 1.65, y + 0.25, z, 2, 0.8, 6.8, "rail", "rail");
      w.box(x - 1.65, y + 1.35, z, 1.8, 0.2, 8, "roof", "rail");
    }
  for (const c of crossings) buildCrossing(w, c);
}
function buildCrossing(w, c) {
  const r = w.result.roads.find((r) => r.e.id === c.roadId),
    at = (s, u = 0, y = 0) => [
      r.a.x + r.dx * s + r.nx * u,
      roadY(w.p, r.e, s / r.length) + y,
      r.a.z + r.dz * s + r.nz * u,
    ];
  // Rails stay flush with the road; no ballast/sleepers laid on the carriageway.
  const railEdge =
    (c.railHalf + 0.04 + (r.w / 2) * Math.sqrt(1 - c.sine * c.sine)) / c.sine;
  for (const side of [-1, 1]) {
    const stop = c.sRoad + side * (railEdge + 4.8),
      gate = c.sRoad + side * (railEdge + 2.1),
      u = side * (r.w / 2 + 1.1),
      pole = at(gate, u);
    w.strip(
      at(stop, side * 0.15, 0.027),
      at(stop, side * (r.w / 2 - 0.15), 0.027),
      0.35,
      0.008,
      "white",
      "markings",
    );
    w.box(pole[0], pole[1] + 1.4, pole[2], 0.14, 2.8, 0.14, "rail", "rail");
    w.box(
      pole[0],
      pole[1] + 1.9,
      pole[2],
      1.05,
      0.5,
      0.22,
      "tramGlass",
      "rail",
      Math.atan2(r.dx, r.dz),
    );
    for (const shift of [-0.3, 0.3]) {
      const q = at(gate + side * 0.13, u + shift, 1.9);
      w.box(
        ...q,
        0.18,
        0.18,
        0.08,
        "signalRed",
        "rail",
        Math.atan2(r.dx, r.dz),
      );
    }
    // Crossbuck above the signal head.
    for (const sign of [-1, 1]) {
      const g = new THREE.BoxGeometry(1.5, 0.15, 0.08);
      g.rotateZ((sign * Math.PI) / 4);
      g.rotateY(Math.atan2(r.dx, r.dz));
      g.translate(pole[0], pole[1] + 2.7, pole[2]);
      w.batch(g, "white", "rail");
    }
    // Static half-road barriers; alternating red/white segments.
    for (let j = 0; j < 8; j++) {
      const q = at(gate, u - (side * (j + 0.5) * (r.w / 2 + 1)) / 8, 1);
      w.box(
        ...q,
        (r.w / 2 + 1) / 8,
        0.14,
        0.14,
        j % 2 ? "signalRed" : "white",
        "rail",
        Math.atan2(r.dx, r.dz),
      );
    }
  }
}
export function buildBridge(w, r, at) {
  const { e, w: width, length, from, to } = r,
    outer =
      width / 2 + (e.sidewalk > 0 ? e.sidewalk + gutterWidth(e) : 0.9) + 0.3,
    rows = [];
  for (let t = from; t < to + 1e-8; t += Math.min(3 / length, to - from)) {
    const v = at(t);
    rows.push({ x: v[0], y: v[1], z: v[2] });
  }
  const end = at(to);
  if (Math.hypot(rows.at(-1).x - end[0], rows.at(-1).z - end[2]) > 0.0001)
    rows.push({ x: end[0], y: end[1], z: end[2] });
  sweep(
    w,
    rows.map((v) => ({ ...v, y: v.y - 0.48 })),
    0,
    outer * 2,
    0.46,
    "concrete",
    "details",
  );
  for (const side of [-1, 1]) {
    sweep(
      w,
      rows.map((v) => ({ ...v, y: v.y - 1.2 })),
      side * width * 0.28,
      0.45,
      0.72,
      "rail",
      "details",
    );
    sweep(
      w,
      rows.map((v) => ({ ...v, y: v.y + 1.15 })),
      side * (outer - 0.18),
      0.16,
      0.18,
      "rail",
      "details",
    );
    for (let s = from * length + 1; s < to * length; s += 3) {
      const v = at(s / length, side * (outer - 0.18));
      w.box(v[0], v[1] + 0.65, v[2], 0.12, 1.3, 0.12, "rail");
    }
    if (e.bridgeStyle === "arch") {
      const arch = [];
      // Bearing shoes connect the rib ends to the deck; hangers land inside it.
      for (const t of [0.06, 0.94]) {
        const v = at(t, side * (outer - 0.05));
        w.box(
          v[0],
          v[1] + 0.065,
          v[2],
          0.6,
          0.17,
          0.8,
          "bridgeSteel",
          "details",
          Math.atan2(r.dx, r.dz),
        );
      }
      for (let i = 0; i <= 80; i++) {
        const t = 0.06 + (0.88 * i) / 80,
          v = at(
            t,
            side * (outer - 0.05),
            0.15 + Math.sin((i / 80) * Math.PI) * Math.min(45, length * 0.12),
          );
        arch.push({ x: v[0], y: v[1], z: v[2] });
      }
      sweep(w, arch, 0, 0.48, 0.5, "bridgeSteel", "details");
      for (let t = 0.12; t < 0.89; t += 0.06) {
        const v = at(t, side * (outer - 0.05)),
          h =
            0.15 +
            Math.sin(((t - 0.06) / 0.88) * Math.PI) *
              Math.min(45, length * 0.12);
        w.box(v[0], v[1] + h / 2, v[2], 0.1, h, 0.1, "rail");
      }
    }
  }
  for (let t = 0.15; t < 0.9; t += 0.175) {
    const v = at(t),
      clear = w.result.roads.every(
        (o) =>
          o.e.id === e.id ||
          o.e.structure === "bridge" ||
          (() => {
            const s = Math.max(
              0,
              Math.min(o.length, (v[0] - o.a.x) * o.dx + (v[2] - o.a.z) * o.dz),
            );
            return (
              Math.hypot(v[0] - o.a.x - s * o.dx, v[2] - o.a.z - s * o.dz) >
              o.w / 2 + o.e.sidewalk + 5
            );
          })(),
      );
    if (!clear) continue;
    const bottom = terrainHeight(v[0], v[2], w.p.brushes) - 1,
      h = v[1] - 1.2 - bottom;
    if (h <= 0) continue;
    for (const side of [-1, 1]) {
      const q = at(t, side * width * 0.28);
      w.box(q[0], bottom + h / 2, q[2], 1.1, h, 1.1, "concrete");
    }
  }
}
