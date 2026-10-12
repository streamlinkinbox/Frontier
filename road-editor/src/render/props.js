// Props: street lamps, bollard rows, speed bumps, railway level crossings.
// Level crossings are generated automatically wherever a railway meets a ground road.
import { elevationAt, roadWidth } from '../core/model.js';
import { closestOnPolyline } from '../core/geom.js';
import { terrainY } from '../core/terrain.js';

export function buildProps(model, net, ctx) {
  const roadSamples = net.roads.map((R) => ({ R, W: roadWidth(R.road) }));

  for (const prop of model.props) {
    const hit = nearestRoad(roadSamples, prop.x, prop.z);
    let y = terrainY(prop.x, prop.z);
    let tx = Math.cos(prop.angle || 0), tz = Math.sin(prop.angle || 0);
    let W = prop.width || 0;
    if (hit) {
      y = elevationAt(hit.R.road, hit.c.s, hit.R.L, prop.x, prop.z);
      if (!prop.angle && prop.kind !== 'streetlamp') { tx = hit.c.tangent.x; tz = hit.c.tangent.z; }
      W = W || hit.W;
    }
    const at = { x: prop.x, y, z: prop.z, tx, tz, W, toward: hit ? { x: hit.c.x - prop.x, z: hit.c.z - prop.z } : null };
    switch (prop.kind) {
      case 'streetlamp': streetlamp(at, ctx); break;
      case 'bollard': bollardRow(at, prop.length || 6, ctx); break;
      case 'speedbump': speedBump(at, prop.width || W || 6, ctx); break;
      case 'railcross': levelCrossing(at, ctx); break;
    }
  }

  // Level crossings from the topology (railway x ground road).
  for (const c of net.crossings) {
    if (c.kind !== 'level-crossing') continue;
    const road = net.roads.find((R) => R.id === c.a && R.road.kind !== 'rail') || net.roads.find((R) => R.id === c.b && R.road.kind !== 'rail');
    if (!road) continue;
    const W = roadWidth(road.road);
    const tt = road.id === c.a ? c.tA : c.tB;
    levelCrossing({ x: c.x, y: elevationAt(road.road, c.sA, road.L, c.x, c.z), z: c.z, tx: tt.x, tz: tt.z, W }, ctx);
  }
}

function nearestRoad(list, x, z) {
  let best = null;
  for (const item of list) {
    const pad = item.W / 2 + 6;
    const bb = item.R.bbox || (item.R.bbox = bboxOf(item.R.pts));
    if (x < bb.minX - pad || x > bb.maxX + pad || z < bb.minZ - pad || z > bb.maxZ + pad) continue;
    const c = closestOnPolyline(item.R.pts, item.R.s, { x, z });
    if (c.dist > item.W / 2 + 6) continue;
    if (!best || c.dist < best.c.dist) best = { ...item, c };
  }
  return best;
}

function bboxOf(pts) {
  let minX = Infinity, minZ = Infinity, maxX = -Infinity, maxZ = -Infinity;
  for (const p of pts) { minX = Math.min(minX, p.x); maxX = Math.max(maxX, p.x); minZ = Math.min(minZ, p.z); maxZ = Math.max(maxZ, p.z); }
  return { minX, minZ, maxX, maxZ };
}

/* helpers in the prop's own frame: forward = (tx,tz), lateral = (-tz,tx) */
const lat = (at, along, across, up = 0) => ({
  x: at.x + at.tx * along - at.tz * across,
  y: at.y + up,
  z: at.z + at.tz * along + at.tx * across,
});
const yawAlong = (tx, tz) => Math.atan2(-tz, tx);

function streetlamp(at, ctx) {
  const pole = ctx.get('lamp_pole');
  const glow = ctx.get('lamp_glow');
  pole.cylinder(at.x, at.y, at.z, 0.11, 7.6, 7);
  // arm reaches toward the road centre
  const tw = at.toward ? Math.hypot(at.toward.x, at.toward.z) || 1 : 1;
  const dx = at.toward ? at.toward.x / tw : 1, dz = at.toward ? at.toward.z / tw : 0;
  const armLen = 1.6;
  const head = { x: at.x + dx * armLen, y: at.y + 7.4, z: at.z + dz * armLen };
  pole.box(at.x + dx * armLen / 2, at.y + 7.45, at.z + dz * armLen / 2, armLen, 0.12, 0.12, Math.atan2(-dz, dx), 0.95);
  glow.box(head.x, head.y - 0.25, head.z, 0.6, 0.2, 0.36, Math.atan2(-dz, dx), 1);
}

function bollardRow(at, length, ctx) {
  const b = ctx.get('bollard');
  const red = ctx.get('red');
  const count = Math.max(1, Math.floor(length / 1.4));
  for (let k = 0; k < count; k++) {
    const a = -length / 2 + (k + 0.5) * (length / count);
    const p = lat(at, a, 0);
    b.cylinder(p.x, at.y, p.z, 0.14, 0.8, 8);
    red.cylinder(p.x, at.y + 0.72, p.z, 0.15, 0.12, 8);
  }
}

function speedBump(at, width, ctx) {
  const body = ctx.get('rubber');
  const yel = ctx.get('paint_yellow');
  const profile = (a) => (Math.abs(a) <= 0.6 ? 0.12 * Math.cos((Math.PI * a) / 1.2) ** 2 : 0);
  const A = 8, U = 10;
  for (let i = 0; i < U; i++) {
    const u0 = -width / 2 + (i * width) / U, u1 = -width / 2 + ((i + 1) * width) / U;
    for (let j = 0; j < A; j++) {
      const a0 = -0.6 + (j * 1.2) / A, a1 = -0.6 + ((j + 1) * 1.2) / A;
      const P = (a, u) => {
        const p = lat(at, a, u, profile(a) + 0.02);
        return p;
      };
      body.quad(P(a0, u0), P(a0, u1), P(a1, u1), P(a1, u0));
    }
  }
  for (const [a0, a1] of [[-0.6, -0.46], [0.46, 0.6]]) {
    for (let i = 0; i < U; i++) {
      const u0 = -width / 2 + (i * width) / U, u1 = -width / 2 + ((i + 1) * width) / U;
      if (i % 2) continue;
      const P = (a, u) => lat(at, a, u, profile(a) + 0.035);
      yel.quad(P(a0, u0), P(a0, u1), P(a1, u1), P(a1, u0));
    }
  }
}

function levelCrossing(at, ctx) {
  const W = at.W || 7;
  const pole = ctx.get('lamp_pole');
  const white = ctx.get('bollard');
  const red = ctx.get('red');
  const paint = ctx.get('paint_white');
  const lampRed = ctx.get('lamp_red');
  const yaw = yawAlong(at.tx, at.tz);
  const yawAcross = Math.atan2(-at.tx, -at.tz); // long box axis across the road
  const side = W / 2 + 1.3;
  const segs = 6;
  const armLen = W + 0.8;
  const seg = armLen / segs;

  for (const sgn of [-1, 1]) {
    // Crossbuck sign on a post.
    const post = lat(at, 0, sgn * side);
    pole.cylinder(post.x, at.y, post.z, 0.07, 2.6, 6);
    paint.tube([lat(at, -0.5, sgn * side, 2.2), lat(at, 0.5, sgn * side, 2.9)], 0.05, 4, 1);
    paint.tube([lat(at, 0.5, sgn * side, 2.2), lat(at, -0.5, sgn * side, 2.9)], 0.05, 4, 1);

    // Flashing light head.
    const lp = lat(at, 0, sgn * (side + 0.7));
    pole.cylinder(lp.x, at.y, lp.z, 0.08, 3.1, 6);
    lampRed.box(lp.x, at.y + 3.0, lp.z, 0.5, 0.45, 0.3, yaw, 1);

    // Barrier arm pivoting at this kerb, lowered across the carriageway (alternating bands).
    const pivotAcross = sgn * (W / 2 + 0.6);
    pole.cylinder(lat(at, 0, pivotAcross).x, at.y, lat(at, 0, pivotAcross).z, 0.09, 1.2, 6);
    for (let k = 0; k < segs; k++) {
      const c = lat(at, 0, pivotAcross - sgn * seg * (k + 0.5), 1.05);
      (k % 2 ? white : red).box(c.x, c.y - 0.06, c.z, seg + 0.02, 0.12, 0.12, yawAcross, 1);
    }
  }
}
