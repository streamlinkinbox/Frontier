// CPU ray casts for the editor camera: screen rays, the terrain / water heightfield and the
// car + props as oriented boxes. Used for click-to-select, the orbit pivot and focus (F).
import { v3 } from '../src/math.js';

/** Pick box of the demo car in body space (covers body, cabin and wheels). */
export const VEHICLE_BOX = { center: [0, 0.1, 0], half: [1.0, 0.75, 2.35] };

/**
 * World-space ray through normalised device coordinates (ndcX right, ndcY up, both -1..1).
 * It matches the demo's view: lookAt(pos, target, +Y) with a vertical FOV.
 */
export function screenRay(pos, target, fov, aspect, ndcX, ndcY) {
  const f = v3.norm(v3.sub(target, pos));
  let r = v3.cross(f, [0, 1, 0]);
  r = v3.len(r) < 1e-6 ? [1, 0, 0] : v3.norm(r);
  const u = v3.cross(r, f);
  const t = Math.tan(fov / 2);
  return { origin: pos.slice(), dir: v3.norm(v3.add(f, v3.add(v3.scale(r, ndcX * t * aspect), v3.scale(u, ndcY * t)))) };
}

/** Project a world point to NDC (x right, y up), or null when it is behind the camera. */
export function projectToNdc(pos, target, fov, aspect, p) {
  const f = v3.norm(v3.sub(target, pos));
  let r = v3.cross(f, [0, 1, 0]);
  r = v3.len(r) < 1e-6 ? [1, 0, 0] : v3.norm(r);
  const u = v3.cross(r, f);
  const d = v3.sub(p, pos), z = v3.dot(d, f);
  if (z <= 1e-6) return null;
  const t = Math.tan(fov / 2);
  return [v3.dot(d, r) / (z * t * aspect), v3.dot(d, u) / (z * t)];
}

/**
 * Ray vs oriented box. `toLocal(v)` rotates a world vector into box space.
 * @returns {number|null} entry distance (0 if the origin is inside), or null
 */
export function rayOBB(origin, dir, center, toLocal, half) {
  const o = toLocal(v3.sub(origin, center)), d = toLocal(dir);
  let t0 = 0, t1 = Infinity;
  for (let k = 0; k < 3; k++) {
    if (Math.abs(d[k]) < 1e-12) {
      if (Math.abs(o[k]) > half[k]) return null;
      continue;
    }
    let a = (-half[k] - o[k]) / d[k], b = (half[k] - o[k]) / d[k];
    if (a > b) [a, b] = [b, a];
    if (a > t0) t0 = a;
    if (b < t1) t1 = b;
    if (t0 > t1) return null;
  }
  return t0;
}

/**
 * March a ray against a height function (y = heightAt(x, z)) and refine the crossing by bisection.
 * @returns {number|null} hit distance, or null
 */
export function rayHeightfield(origin, dir, heightAt, maxDist = 800, ceiling = 300) {
  const above = (t) => origin[1] + dir[1] * t - heightAt(origin[0] + dir[0] * t, origin[2] + dir[2] * t);
  if (above(0) < 0) return 0;
  let t = 0, tPrev = 0;
  while (t < maxDist) {
    t = Math.min(maxDist, t + Math.min(4, 0.25 + t * 0.015));
    if (above(t) < 0) {
      let a = tPrev, b = t;
      for (let k = 0; k < 20; k++) {
        const m = (a + b) / 2;
        if (above(m) < 0) b = m; else a = m;
      }
      return (a + b) / 2;
    }
    tPrev = t;
    if (dir[1] >= 0 && origin[1] + dir[1] * t > ceiling) return null;
  }
  return null;
}

/**
 * Closest hit along a ray.
 * @param {{vehicle?: object, props?: {items: object[]}, surfaceAt: (x:number, z:number) => number}} world
 *   surfaceAt is the top of terrain or water, whichever is higher
 * @returns {{dist:number, point:number[], object: null|{kind:'vehicle'}|{kind:'prop', item:object}}|null}
 *   object is null when the ground / water was hit; the result is null when nothing is hit
 */
export function pickScene(world, origin, dir, { maxDist = 800, objects = true } = {}) {
  let best = null;
  const g = rayHeightfield(origin, dir, world.surfaceAt, maxDist);
  if (g !== null) best = { dist: g, object: null };
  if (objects) {
    const consider = (t, object) => { if (t !== null && t <= maxDist && (!best || t < best.dist)) best = { dist: t, object }; };
    if (world.vehicle) {
      const b = world.vehicle.body;
      consider(rayOBB(origin, dir, b.toWorld(VEHICLE_BOX.center), (v) => b.dirToLocal(v), VEHICLE_BOX.half), { kind: 'vehicle' });
    }
    for (const item of world.props?.items ?? []) {
      const b = item.body;
      consider(rayOBB(origin, dir, b.position, (v) => b.dirToLocal(v), item.T.half), { kind: 'prop', item });
    }
  }
  if (best) best.point = v3.madd(origin, dir, best.dist);
  return best;
}

/** Bounding sphere of a pickable object (car when obj is null / vehicle). */
export function objectBounds(obj, vehicle) {
  if (obj?.kind === 'prop') return { center: obj.item.body.position.slice(), radius: obj.item.radius };
  const h = VEHICLE_BOX.half;
  return { center: vehicle.body.toWorld(VEHICLE_BOX.center), radius: Math.hypot(h[0], h[1], h[2]) };
}
