import {
  addRoad,
  terrainHeight,
  segmentIntersection,
  roadWidth,
  validateProject,
} from "./model.js";
import { corridorPoints } from "./transport-geometry.js";
import { roadY, generateRoadMesh, auditMesh } from "./geometry.js";
import { generateStreetscape, auditStreetscape } from "./streetscape.js";

export const BRIDGE_NETWORKS = [
  {
    id: "loop",
    name: "01 · Pearl loop interchange",
    description:
      "Elevated oval distributor, two approach ramps and an uninterrupted boulevard below.",
  },
  {
    id: "roundabout",
    name: "02 · Lantern roundabout interchange",
    description:
      "Four elevated approaches around a compact circular distributor over a through-road.",
  },
  {
    id: "branch",
    name: "03 · Dragonfly branching viaduct",
    description:
      "Curving Y-shaped flyovers, shared elevated junction and two ground-level crossing streets.",
  },
];
const smooth = (t) => t * t * (3 - 2 * t);

// Add editable graph edges, never a disconnected decorative bridge object.
export function buildBridgeNetwork(p, kind, origin) {
  if (!BRIDGE_NETWORKS.some((n) => n.id === kind))
    throw Error("Unknown bridge network.");
  const ground = ([x, z]) => ({ x: origin.x + x, z: origin.z + z });
  const datum = terrainHeight(origin.x, origin.z, p.brushes) + 9.2;
  const pos = (v) => ({
    ...ground(v),
    ...(v.length > 2 ? { elevation: v[2] } : {}),
  });
  const roads = [];
  const road = (a, b, elevated = false, extra = {}) => {
    const ids = addRoad(p, pos(a), pos(b), "local", {
      bridgeNetwork: kind,
      transportSample: true,
      name: `${BRIDGE_NETWORKS.find((n) => n.id === kind).name} · ${elevated ? "viaduct" : "connecting street"}`,
      lanes: 2,
      laneWidth: 3.3,
      sidewalk: 0,
      bike: false,
      landscape: "standard",
      speed: 40,
      structure: elevated ? "bridge" : "surface",
      ...(elevated ? { elevationMode: "linear", bridgeStyle: "girder" } : {}),
      drainage: "none",
      rampStyle: "none",
      tactile: "none",
      ...extra,
    });
    if (!ids.length)
      throw Error(`Bridge network segment conflicts at ${a}/${b}`);
    roads.push(...ids);
    return ids;
  };
  const path = (points, elevated = false, extra = {}) =>
    points.slice(1).forEach((b, i) => road(points[i], b, elevated, extra));
  const ramp = (a, b) => {
    const A = pos(a),
      B = pos(b),
      ha = A.elevation ?? terrainHeight(A.x, A.z, p.brushes) + 0.12,
      hb = B.elevation ?? terrainHeight(B.x, B.z, p.brushes) + 0.12;
    const points = Array.from({ length: 7 }, (_, i) => {
      const t = i / 6;
      return [
        a[0] + (b[0] - a[0]) * t,
        a[1] + (b[1] - a[1]) * t,
        ha + (hb - ha) * smooth(t),
      ];
    });
    path(points, true);
  };
  const circle = (rx, rz, count) => {
    const points = Array.from({ length: count }, (_, i) => {
      const a = (2 * Math.PI * i) / count;
      return [rx * Math.cos(a), rz * Math.sin(a), datum];
    });
    points.forEach((a, i) =>
      road(a, points[(i + 1) % count], true, {
        networkFlow: "forward",
        networkArrow: i % 3 === 0,
        networkCurve: kind + "-ring",
        networkCurveRadius:
          Math.pow(
            (rx * Math.sin((2 * Math.PI * (i + 0.5)) / count)) ** 2 +
              (rz * Math.cos((2 * Math.PI * (i + 0.5)) / count)) ** 2,
            1.5,
          ) /
          (rx * rz),
        name: `${BRIDGE_NETWORKS.find((n) => n.id === kind).name} · circulating deck`,
      }),
    );
    return points;
  };
  const through = (a, b) =>
    road(a, b, false, {
      lanes: 4,
      laneWidth: 3.2,
      networkDivided: true,
      speed: 60,
    });
  if (kind === "loop") {
    const ring = circle(100, 155, 24);
    ramp(ring[6], [-180, 405]);
    ramp(ring[18], [180, -405]);
    through([-420, 0], [420, 0]);
    path([
      [-180, 405],
      [-180, 500],
    ]);
    path([
      [180, -405],
      [420, -405],
      [420, 0],
      [420, 500],
    ]);
    path([
      [-420, 0],
      [-420, 500],
    ]);
  } else if (kind === "roundabout") {
    const ring = circle(78, 78, 24);
    ramp(ring[0], [338, 0]);
    ramp(ring[12], [-338, 0]);
    path([ring[18], [-140, -218, datum]], true);
    ramp([-140, -218, datum], [-140, -458]);
    path([ring[6], [140, 218, datum]], true);
    ramp([140, 218, datum], [140, 458]);
    through([0, -458], [0, 500]);
    path([
      [-338, 0],
      [-420, 0],
      [-420, 500],
    ]);
    path([
      [338, 0],
      [420, 0],
      [420, 500],
    ]);
    path([
      [-140, -458],
      [0, -458],
    ]);
    path([
      [140, 458],
      [140, 500],
    ]);
  } else {
    // Cubic curves have nonzero endpoint tangents; long enough edges for filleted junctions.
    const bezier = (a, b, c, d, n = 7) =>
      Array.from({ length: n + 1 }, (_, i) => {
        const t = i / n,
          s = 1 - t;
        return [0, 1]
          .map(
            (k) =>
              s * s * s * a[k] +
              3 * s * s * t * b[k] +
              3 * s * t * t * c[k] +
              t * t * t * d[k],
          )
          .concat(datum);
      });
    ramp([-200, 440], [-100, 220, datum]);
    path(bezier([-100, 220], [-25, 180], [0, 95], [0, 0]), true);
    path(bezier([0, 0], [-70, -45], [-170, -150], [-210, -210]), true);
    path(bezier([0, 0], [70, -45], [170, -150], [210, -210]), true);
    ramp([-210, -210, datum], [-350, -430]);
    ramp([210, -210, datum], [350, -430]);
    through([-430, -100], [430, -100]);
    through([-430, 100], [430, 100]);
    path([
      [-350, -430],
      [-430, -430],
      [-430, -100],
      [-430, 100],
      [-430, 500],
    ]);
    path([
      [350, -430],
      [430, -430],
      [430, -100],
      [430, 100],
      [430, 500],
    ]);
    path([
      [-200, 440],
      [-200, 500],
    ]);
  }
  return { id: kind, origin: { ...origin }, datum, edgeIds: roads };
}

export function auditBridgeNetworks(p) {
  const nodes = new Map(p.nodes.map((n) => [n.id, n])),
    issues = [],
    crossings = [];
  let maxGrade = 0;
  const elevated = p.edges.filter(
    (e) => e.bridgeNetwork && e.structure === "bridge",
  );
  for (const e of elevated) {
    const a = nodes.get(e.a),
      b = nodes.get(e.b),
      length = Math.hypot(b.x - a.x, b.z - a.z);
    const grade = Math.abs(roadY(p, e, 1) - roadY(p, e, 0)) / length;
    maxGrade = Math.max(maxGrade, grade);
    if (grade > 0.065) issues.push(`${e.id}: ramp grade exceeds 6.5%`);
    for (const f of p.edges) {
      if (f.id === e.id || [f.a, f.b].some((id) => id === e.a || id === e.b))
        continue;
      const c = nodes.get(f.a),
        d = nodes.get(f.b),
        hit = segmentIntersection(a, b, c, d);
      if (!hit) continue;
      const gap = Math.abs(roadY(p, e, hit.t) - roadY(p, f, hit.u));
      // Full width / slope envelope, not just the centreline clearance.
      const otherGrade =
        Math.abs(roadY(p, f, 1) - roadY(p, f, 0)) /
        Math.hypot(d.x - c.x, d.z - c.z);
      const sine =
        Math.abs((b.x - a.x) * (d.z - c.z) - (b.z - a.z) * (d.x - c.x)) /
        (length * Math.hypot(d.x - c.x, d.z - c.z));
      const margin =
        (grade * (roadWidth(f) / 2 + 0.9) +
          otherGrade * (roadWidth(e) / 2 + 0.9)) /
        Math.max(0.1, sine);
      const clearance = gap - 1.2 - margin;
      crossings.push({
        upper: e.id,
        other: f.id,
        clearance,
        x: hit.x,
        z: hit.z,
      });
      if (clearance < 5.5)
        issues.push(
          `${e.id}/${f.id}: insufficient deck clearance (${clearance.toFixed(2)}m)`,
        );
    }
  }
  return { ok: !issues.length, issues, crossings, maxGrade };
}

export function installBridgeNetworks(project) {
  if (project.bridgeNetworkVersion === 1) return { added: false };
  const p = structuredClone(project);
  const all = [
    ...p.nodes,
    ...(p.corridors || []).flatMap((c) => corridorPoints(p, c)),
  ];
  const baseX = Math.max(0, ...all.map((n) => n.x)) + 520,
    south = Math.max(0, ...all.map((n) => n.z)) + 240;
  if (baseX + 2500 > 9800 || south > 9800)
    throw Error(
      "Not enough room for the bridge district inside the map limits.",
    );
  const used = new Set(
    p.edges.filter((e) => e.structure === "surface").flatMap((e) => [e.a, e.b]),
  );
  const anchor = p.nodes
    .filter((n) => used.has(n.id))
    .sort((a, b) => b.z - a.z)[0];
  const link = (a, b) => {
    if (
      !addRoad(p, a, b, "country", {
        bridgeNetwork: "link",
        transportSample: true,
        name: "Bridge district · connected approach",
        sidewalk: 0,
      }).length
    )
      throw Error("Bridge district connector could not be placed.");
  };
  if (anchor) {
    link(anchor, { x: anchor.x, z: south });
    link({ x: anchor.x, z: south }, { x: baseX - 520, z: south });
  }
  const networks = BRIDGE_NETWORKS.map((n, i) =>
    buildBridgeNetwork(p, n.id, { x: baseX + i * 1000, z: south - 500 }),
  );
  // One shared access street joins every ground-level ramp and underpass endpoint.
  const ports = p.nodes
    .filter((n) => Math.abs(n.z - south) < 0.01 && n.x >= baseX - 521)
    .sort((a, b) => a.x - b.x);
  for (let i = 1; i < ports.length; i++)
    if (ports[i].x - ports[i - 1].x > 1) link(ports[i - 1], ports[i]);
  p.bridgeNetworks = networks.map(({ edgeIds, ...n }) => n);
  p.bridgeNetworkVersion = 1;
  validateProject(p);
  const networkAudit = auditBridgeNetworks(p);
  if (!networkAudit.ok) throw Error(networkAudit.issues.join("; "));
  const result = generateRoadMesh(p),
    roadAudit = auditMesh(result, p);
  if (roadAudit.issues.length) throw Error(roadAudit.issues.join("; "));
  const streetscape = generateStreetscape(p, result),
    streetAudit = auditStreetscape(streetscape);
  if (!streetAudit.ok)
    throw Error(
      `Bridge-network paving: ${JSON.stringify(streetAudit.issues.slice(0, 3))}`,
    );
  Object.assign(project, p);
  return { added: true, result, streetscape, streetAudit, networkAudit };
}
