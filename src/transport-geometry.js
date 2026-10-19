import { dist, segmentIntersection, roadWidth } from "./model.js";

// Tangent-continuous quarter-circle departure from an open street end.
export function exitPoints(a, b, side = 1, radius = 80, run = 100, lead = 0) {
  const len = dist(a, b),
    dx = (b.x - a.x) / len,
    dz = (b.z - a.z) / len,
    nx = -dz * side,
    nz = dx * side;
  const pts = lead ? [{ x: b.x - dx * lead, z: b.z - dz * lead }] : [];
  for (let i = 0; i <= 64; i++) {
    const t = ((i / 64) * Math.PI) / 2;
    pts.push({
      x: b.x + dx * radius * Math.sin(t) + nx * radius * (1 - Math.cos(t)),
      z: b.z + dz * radius * Math.sin(t) + nz * radius * (1 - Math.cos(t)),
    });
  }
  const end = pts.at(-1);
  pts.push({ x: end.x + nx * run, z: end.z + nz * run });
  return pts;
}
export function addRailExit(
  p,
  edgeId,
  { kind = "rail", side = -1, radius = 80, run = 100, id } = {},
) {
  const e = p.edges.find((e) => e.id === edgeId);
  if (
    !e ||
    e.landscape !== "tram" ||
    !["rail", "monorail"].includes(kind) ||
    ![-1, 1].includes(side) ||
    !Number.isFinite(radius) ||
    radius < 30 ||
    radius > 200 ||
    !Number.isFinite(run) ||
    run < 20 ||
    run > 300
  )
    throw Error(
      "Select a tram street; use a 30–200 m turn radius and 20–300 m exit run.",
    );
  const port = [e.b, e.a].find(
    (id) => p.edges.filter((e) => e.a === id || e.b === id).length === 1,
  );
  if (!port)
    throw Error(
      "A rail exit needs an open street end. Try the Eastbank Tram departure street sample.",
    );
  const b = p.nodes.find((n) => n.id === port),
    a = p.nodes.find((n) => n.id === (port === e.b ? e.a : e.b));
  if (p.corridors.some((c) => c.sourceEdge === edgeId && c.kind === kind))
    throw Error("This street already has that rail exit.");
  const corridor = {
    id: id || `rail-exit-${p.nextId++}`,
    district: e.district,
    name:
      kind === "rail"
        ? "Curved street-to-rail departure"
        : "Monorail street-to-off-road curve",
    kind,
    elevation: kind === "rail" ? 0.12 : 8,
    gauge: 1.435,
    trackSpacing: 3.3,
    sourceEdge: e.id,
    sourceNode: port,
    exitSide: side,
    turnRadius: radius,
    exitRun: run,
    lead: kind === "monorail" ? Math.min(70, dist(a, b) / 2) : 0,
  };
  corridor.points = exitPoints(a, b, side, radius, run, corridor.lead);
  p.corridors.push(corridor);
  return corridor;
}
export function corridorPoints(p, c) {
  const e = p.edges.find((e) => e.id === c.sourceEdge),
    b = p.nodes.find((n) => n.id === c.sourceNode);
  if (!e || !b || ![e.a, e.b].includes(b.id)) return c.points;
  const a = p.nodes.find((n) => n.id === (e.a === b.id ? e.b : e.a));
  return exitPoints(a, b, c.exitSide, c.turnRadius, c.exitRun, c.lead || 0);
}
export function railCrossings(p) {
  const out = [];
  for (const c of p.corridors || []) {
    if (c.kind !== "rail" || c.elevation > 1) continue;
    const points = corridorPoints(p, c);
    let station = 0;
    for (let i = 1; i < points.length; i++) {
      const a = points[i - 1],
        b = points[i],
        length = dist(a, b);
      if (length < 1e-6) continue;
      for (const e of p.edges) {
        if (e.structure === "bridge" || e.id === c.sourceEdge) continue;
        const u = p.nodes.find((n) => n.id === e.a),
          v = p.nodes.find((n) => n.id === e.b),
          hit = segmentIntersection(a, b, u, v);
        if (!hit) continue;
        const roadLength = dist(u, v),
          rx = (v.x - u.x) / roadLength,
          rz = (v.z - u.z) / roadLength,
          dx = (b.x - a.x) / length,
          dz = (b.z - a.z) / length,
          sine = Math.abs(dx * rz - dz * rx);
        if (sine < 0.001) continue;
        const point = {
          x: a.x + (b.x - a.x) * hit.t,
          z: a.z + (b.z - a.z) * hit.t,
        };
        if (
          out.some(
            (x) =>
              x.corridorId === c.id &&
              x.roadId === e.id &&
              dist(point, x.point) < 1,
          )
        )
          continue;
        out.push({
          corridorId: c.id,
          roadId: e.id,
          point,
          station: station + hit.t * length,
          sRoad: hit.u * roadLength,
          roadLength,
          rx,
          rz,
          dx,
          dz,
          sine,
          angle: (Math.asin(sine) * 180) / Math.PI,
          railHalf: (c.trackSpacing || 3.3) / 2 + (c.gauge || 1.435) / 2,
          roadHalf: roadWidth(e) / 2 + e.sidewalk + 0.7,
        });
      }
      station += length;
    }
  }
  return out;
}
// Explicit limits: do not silently run a railway through raised medians or curbs.
export function crossingIssues(p) {
  return railCrossings(p).flatMap((c) => {
    const e = p.edges.find((e) => e.id === c.roadId);
    return Math.min(c.sRoad, c.roadLength - c.sRoad) < 18
      ? [
          `Railway crossing on ${e.name} is too close to a road end or junction (18 m minimum).`,
        ]
      : c.angle < 60
        ? [`Railway crossing on ${e.name} is too oblique (minimum 60°).`]
        : e.sidewalk > 0 || (e.landscape && e.landscape !== "standard")
          ? [
              `Railway crossing on ${e.name} needs a lowered footway/median design. Use an unkerbed country road for this crossing prototype.`,
            ]
          : [];
  });
}
