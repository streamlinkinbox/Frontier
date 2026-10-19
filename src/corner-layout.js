// Shared 2D layout for asphalt fillets AND pavement offsets. No renderer state.
const TAU = Math.PI * 2,
  EPS = 1e-8;
const dot = (a, b) => a.x * b.x + a.z * b.z;
import { channelWidth } from "./drainage.js";
import { SHOULDER_WIDTH } from "./edge-transitions.js";
export const pavementExtent = (e) =>
  e.sidewalk > 0 ? e.sidewalk + channelWidth(e) : SHOULDER_WIDTH;
const vec = (a) => ({ x: a.dx, z: a.dz });
export function describeCorner(a, b, node) {
  const gap = (b.angle - a.angle + TAU) % TAU,
    turn = gap - Math.PI,
    envelope = Math.max(pavementExtent(a.e), pavementExtent(b.e));
  const nearStraight = Math.abs(turn) < 0.22;
  const start = {
      x: node.x - (a.dz * a.width) / 2,
      z: node.z + (a.dx * a.width) / 2,
    },
    end = {
      x: node.x + (b.dz * b.width) / 2,
      z: node.z - (b.dx * b.width) / 2,
    };
  const den = a.dx * b.dz - a.dz * b.dx;
  let center = null,
    ca = 0,
    cb = 0;
  if (Math.abs(den) > EPS) {
    const t = ((end.x - start.x) * b.dz - (end.z - start.z) * b.dx) / den;
    center = { x: start.x + a.dx * t, z: start.z + a.dz * t };
    ca = dot({ x: center.x - node.x, z: center.z - node.z }, vec(a));
    cb = dot({ x: center.x - node.x, z: center.z - node.z }, vec(b));
  }
  const factor = Math.tan(Math.abs(turn) / 2),
    radius =
      turn < 0 ? envelope + 1.5 : Math.max(2, Math.max(a.width, b.width) * 0.5);
  const sameRing =
    a.e.roundabout &&
    a.e.roundabout === b.e.roundabout &&
    a.e.roundaboutRole === "ring" &&
    b.e.roundaboutRole === "ring";
  const sameCurve = a.e.networkCurve && a.e.networkCurve === b.e.networkCurve;
  const curveRadius = sameCurve
    ? (a.e.networkCurveRadius + b.e.networkCurveRadius) / 2
    : a.e.roundaboutRadius;
  const roundRadius =
    sameRing || sameCurve
      ? Math.max(
          radius,
          curveRadius * Math.cos(Math.abs(turn) / 2) +
            ((turn < 0 ? -1 : 1) * Math.max(a.width, b.width)) / 2,
        )
      : radius;
  return {
    gap,
    turn,
    envelope,
    nearStraight,
    center,
    ca,
    cb,
    factor,
    radius: roundRadius,
  };
}
export function allocateJunctionTrims(joins, nodes) {
  const ends = new Map();
  for (const [id, arms] of joins) {
    arms.sort((a, b) => a.angle - b.angle);
    for (const arm of arms) {
      arm.trim = arms.length > 1 ? Math.max(1, arm.width * 0.55) : 0;
      if (!ends.has(arm.e.id)) ends.set(arm.e.id, []);
      ends.get(arm.e.id).push(arm);
    }
    if (arms.length < 2) continue;
    for (let i = 0; i < arms.length; i++) {
      const a = arms[i],
        b = arms[(i + 1) % arms.length],
        c = describeCorner(a, b, nodes.get(id));
      a.corner = c;
      if (c.nearStraight || !c.center) {
        const transition =
          Math.max(a.width, b.width) * 0.65 + Math.abs(a.width - b.width) * 0.5;
        a.trim = Math.max(a.trim, transition);
        b.trim = Math.max(b.trim, transition);
      } else {
        a.trim = Math.max(a.trim, c.ca + c.radius * c.factor + 0.25);
        b.trim = Math.max(b.trim, c.cb + c.radius * c.factor + 0.25);
      }
    }
  }
  // Share each edge's available length between its two ends. Do not force both
  // junctions to the same half-length when only one needs a large acute fillet.
  for (const arms of ends.values()) {
    const total = arms.reduce((s, a) => s + a.trim, 0),
      budget = Math.max(0.1, arms[0].length - 0.8);
    if (total > budget) for (const a of arms) a.trim *= budget / total;
  }
}
export function buildCorner(a, b, node) {
  const c = a.corner ?? describeCorner(a, b, node),
    start = a.left,
    end = b.right;
  const points = [],
    normals = [];
  let minRadius = Infinity,
    usedRadius = Infinity;
  const add = (x, z, nx, nz) => {
    const prev = points.at(-1);
    if (prev && Math.hypot(prev[0] - x, prev[2] - z) < 1e-7) return;
    points.push([x, 0, z]);
    normals.push([nx, nz]);
  };
  add(start[0], start[2], -a.dz, a.dx);
  if (c.nearStraight || !c.center) {
    const chord = Math.hypot(end[0] - start[0], end[2] - start[2]),
      handle = chord / 3;
    const p0 = [start[0], start[2]],
      p1 = [start[0] - a.dx * handle, start[2] - a.dz * handle],
      p2 = [end[0] - b.dx * handle, end[2] - b.dz * handle],
      p3 = [end[0], end[2]];
    const steps = Math.max(4, Math.ceil(chord / 0.65));
    for (let i = 1; i < steps; i++) {
      const t = i / steps,
        s = 1 - t;
      const p = p0.map(
        (v, k) =>
          s * s * s * v +
          3 * s * s * t * p1[k] +
          3 * s * t * t * p2[k] +
          t * t * t * p3[k],
      );
      const d = p0.map(
        (_, k) =>
          3 * s * s * (p1[k] - p0[k]) +
          6 * s * t * (p2[k] - p1[k]) +
          3 * t * t * (p3[k] - p2[k]),
      );
      const dd = p0.map(
        (_, k) =>
          6 * s * (p2[k] - 2 * p1[k] + p0[k]) +
          6 * t * (p3[k] - 2 * p2[k] + p1[k]),
      );
      const l = Math.hypot(...d),
        curvature = (d[0] * dd[1] - d[1] * dd[0]) / (l * l * l);
      if (curvature < -EPS) minRadius = Math.min(minRadius, -1 / curvature);
      add(p[0], p[1], d[1] / l, -d[0] / l);
    }
  } else {
    usedRadius = Math.max(
      0.025,
      Math.min(
        c.radius,
        (a.trim - c.ca) / c.factor,
        (b.trim - c.cb) / c.factor,
      ),
    );
    if (c.turn < 0) minRadius = usedRadius;
    const lead = usedRadius * c.factor,
      ts = { x: c.center.x + a.dx * lead, z: c.center.z + a.dz * lead };
    const sign = Math.sign(c.turn),
      cx = ts.x + sign * a.dz * usedRadius,
      cz = ts.z - sign * a.dx * usedRadius;
    const angle = Math.atan2(ts.z - cz, ts.x - cx),
      steps = Math.max(
        3,
        Math.ceil(
          Math.max(
            Math.abs(c.turn) / 0.075,
            (Math.abs(c.turn) * usedRadius) / 0.65,
          ),
        ),
      );
    add(ts.x, ts.z, -a.dz, a.dx);
    for (let i = 1; i <= steps; i++) {
      const theta = angle + (c.turn * i) / steps;
      add(
        cx + Math.cos(theta) * usedRadius,
        cz + Math.sin(theta) * usedRadius,
        Math.cos(theta) * sign,
        Math.sin(theta) * sign,
      );
    }
  }
  add(end[0], end[2], b.dz, -b.dx);
  // The exact mouth endpoints and normals are shared with the road ribbons.
  points[0] = [...start];
  points[points.length - 1] = [...end];
  normals[0] = [-a.dz, a.dx];
  normals[normals.length - 1] = [b.dz, -b.dx];
  // Earcut must not receive numerically collinear samples on straight tails.
  for (let i = points.length - 2; i > 0; i--) {
    const p = points[i - 1],
      q = points[i],
      r = points[i + 1];
    if (
      Math.abs((q[0] - p[0]) * (r[2] - q[2]) - (q[2] - p[2]) * (r[0] - q[0])) <
      1e-9
    ) {
      points.splice(i, 1);
      normals.splice(i, 1);
    }
  }
  const lengths = [0];
  for (let i = 1; i < points.length; i++)
    lengths.push(
      lengths[i - 1] +
        Math.hypot(
          points[i][0] - points[i - 1][0],
          points[i][2] - points[i - 1][2],
        ),
    );
  const total = lengths.at(-1) || 1;
  points.forEach(
    (p, i) => (p[1] = start[1] + ((end[1] - start[1]) * lengths[i]) / total),
  );
  const clearance = minRadius - c.envelope;
  return {
    points,
    normals,
    fractions: lengths.map((l) => l / total),
    radius: usedRadius,
    minRadius,
    clearance,
    envelope: c.envelope,
    valid: clearance > 0.12,
    sidewalk: Math.min(a.e.sidewalk, b.e.sidewalk),
    node,
  };
}
