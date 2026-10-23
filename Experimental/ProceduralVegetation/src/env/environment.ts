/**
 * Environment: the ground and the objects roots grow against.
 *
 * Every object is a signed distance field with a gradient, which is exactly
 * what the root tropism needs: `d < 0` is inside, the gradient is the surface
 * normal, and the same field is ray-marched to produce the preview mesh, so
 * what the roots feel is what the user sees.
 *
 *  - 'rock'  : fBm-displaced ellipsoid (boulders, stones);
 *  - 'block' : rounded box with light surface noise (walls, foundations,
 *              ruins, kerb stones – anything man-made the roots should climb).
 *
 * Placement is explicit (`ObstacleSpec[]`) so the user can drag objects around
 * in the viewport; `scatterObstacles` only produces an initial layout. The
 * ground is the plane y = 0; `groundHeight` is the hook for a future terrain.
 */

import { V3, add, addScaled, clamp, length, normalize, rotateAxis, scale, sub, v3 } from '../core/math';
import { Random } from '../core/random';
import { fbm3 } from './noise';

export type ObstacleKind = 'rock' | 'block';

export interface ObstacleSpec {
  kind: ObstacleKind;
  x: number;
  z: number;
  /** Mean radius (rock) or mean half-extent (block), metres. */
  size: number;
  /** Relative extents along local x, y, z. */
  aspect: [number, number, number];
  /** Rotation about the world Y axis, radians. */
  yaw: number;
  /** Small tilt about a horizontal axis, radians. */
  tilt: number;
  /** 0 = resting on the ground, 1 = half buried. */
  burial: number;
  /** Surface noise amplitude as a fraction of `size`. */
  roughness: number;
  noiseSeed: number;
}

export interface ScatterParams {
  count: number;
  seed: number;
  /** Extra distance from the trunk surface, metres. */
  minDistance: number;
  maxDistance: number;
  minSize: number;
  maxSize: number;
  burial: number;
  roughness: number;
  /** Fraction of scattered objects that are blocks instead of rocks. */
  blocks: number;
}

export interface EnvironmentParams {
  enabled: boolean;
  scatter: ScatterParams;
  obstacles: ObstacleSpec[];
}

export const DEFAULT_SCATTER: ScatterParams = {
  count: 4,
  seed: 11,
  minDistance: 0.3,
  maxDistance: 2.2,
  minSize: 0.35,
  maxSize: 0.9,
  burial: 0.4,
  roughness: 0.18,
  blocks: 0,
};

export const DEFAULT_ENVIRONMENT: EnvironmentParams = {
  enabled: true,
  scatter: { ...DEFAULT_SCATTER },
  obstacles: [],
};

export function makeObstacle(kind: ObstacleKind, x: number, z: number, size: number, rng: Random, burial = 0.4, roughness = 0.18): ObstacleSpec {
  const aspect: [number, number, number] =
    kind === 'rock' ? [rng.range(0.85, 1.4), rng.range(0.55, 0.9), rng.range(0.75, 1.2)] : [rng.range(1.2, 2.2), rng.range(0.5, 0.9), rng.range(0.6, 1.0)];
  return {
    kind,
    x,
    z,
    size,
    aspect,
    yaw: rng.range(0, Math.PI * 2),
    tilt: kind === 'rock' ? rng.range(-0.25, 0.25) : rng.range(-0.06, 0.06),
    burial: clamp(burial, 0, 0.95),
    roughness: kind === 'rock' ? roughness : roughness * 0.25,
    noiseSeed: rng.int(1 << 20),
  };
}

/** Initial layout around a trunk of (flared) base radius `trunkRadius`, metres. */
export function scatterObstacles(sp: ScatterParams, trunkRadius: number): ObstacleSpec[] {
  const rng = new Random(sp.seed * 7919 + 13);
  const n = Math.max(0, Math.round(sp.count));
  const out: ObstacleSpec[] = [];
  let guard = 0;
  while (out.length < n && guard++ < n * 80) {
    const size = rng.range(sp.minSize, sp.maxSize);
    const dist = trunkRadius + size * 0.9 + rng.range(sp.minDistance, sp.maxDistance);
    const theta = rng.range(0, Math.PI * 2);
    const x = Math.cos(theta) * dist;
    const z = Math.sin(theta) * dist;
    let ok = true;
    for (const o of out) if (Math.hypot(o.x - x, o.z - z) < (o.size + size) * 1.6) ok = false;
    if (!ok) continue;
    const kind: ObstacleKind = rng.next() < sp.blocks ? 'block' : 'rock';
    out.push(makeObstacle(kind, x, z, size, rng, sp.burial * rng.range(0.75, 1.15), sp.roughness * rng.range(0.7, 1.3)));
  }
  return out;
}

export interface SdfSample {
  d: number;
  /** Outward unit normal (gradient of the field). */
  n: V3;
  obstacle: number;
}

interface Body {
  spec: ObstacleSpec;
  center: V3;
  radii: V3;
  tiltAxis: V3;
  cosYaw: number;
  sinYaw: number;
  frequency: number;
  noiseAmp: number;
  /** Bounding sphere radius. */
  bound: number;
  /** Highest point, for climb decisions. */
  top: number;
}

export interface ObstacleMeshData {
  kind: ObstacleKind;
  positions: Float32Array;
  normals: Float32Array;
  indices: Uint32Array;
}

export class Environment {
  readonly bodies: Body[] = [];

  constructor(readonly params: EnvironmentParams) {
    if (!params.enabled) return;
    for (const spec of params.obstacles) {
      const radii = v3(spec.size * spec.aspect[0], spec.size * spec.aspect[1], spec.size * spec.aspect[2]);
      const center = v3(spec.x, radii.y * (1 - 2 * spec.burial), spec.z);
      const tiltAxis = normalize(v3(Math.cos(spec.yaw + 1.3), 0, Math.sin(spec.yaw + 1.3)));
      const noiseAmp = spec.roughness * spec.size;
      const bound = (spec.kind === 'rock' ? Math.max(radii.x, radii.y, radii.z) : length(radii)) + noiseAmp * 1.3;
      this.bodies.push({
        spec,
        center,
        radii,
        tiltAxis,
        cosYaw: Math.cos(spec.yaw),
        sinYaw: Math.sin(spec.yaw),
        frequency: 2.1 / Math.max(0.05, spec.size),
        noiseAmp,
        bound,
        top: center.y + radii.y + noiseAmp,
      });
    }
  }

  get count(): number {
    return this.bodies.length;
  }

  /** Terrain hook: height of the ground at (x, z). Flat for now. */
  groundHeight(_x: number, _z: number): number {
    return 0;
  }

  /**
   * Height of the surface a root can rest on at (x, z): the ground, or the
   * upper surface of an object standing there if that object is no taller
   * than `maxClimb` above the ground (taller objects are skirted instead).
   * Returns the body index (-1 for the ground).
   */
  surfaceHeight(x: number, z: number, maxClimb: number): { h: number; body: number } {
    let h = this.groundHeight(x, z);
    let body = -1;
    for (let i = 0; i < this.bodies.length; i++) {
      const b = this.bodies[i];
      const dx = x - b.center.x;
      const dz = z - b.center.z;
      if (dx * dx + dz * dz > b.bound * b.bound) continue;
      // Sphere-trace straight down from above the object to its upper surface.
      let y = b.top + 0.02;
      let hit = -Infinity;
      const floor = b.center.y - b.bound;
      for (let it = 0; it < 48 && y > floor; it++) {
        const d = this.distance(i, v3(x, y, z));
        if (d <= 2e-3) {
          hit = y;
          break;
        }
        y -= Math.max(d, 4e-3);
      }
      // Only objects low enough to be crossed count as "surface" for a root;
      // taller ones are walls it has to go around.
      if (hit > h && b.top - h <= maxClimb) {
        h = hit;
        body = i;
      }
    }
    return { h, body };
  }

  private toLocal(b: Body, p: V3): V3 {
    const q = rotateAxis(sub(p, b.center), b.tiltAxis, -b.spec.tilt);
    return v3(q.x * b.cosYaw + q.z * b.sinYaw, q.y, -q.x * b.sinYaw + q.z * b.cosYaw);
  }

  private toWorld(b: Body, l: V3): V3 {
    const q = v3(l.x * b.cosYaw - l.z * b.sinYaw, l.y, l.x * b.sinYaw + l.z * b.cosYaw);
    return add(b.center, rotateAxis(q, b.tiltAxis, b.spec.tilt));
  }

  /** Signed distance to one object. Exact enough near the surface, monotone far away. */
  distance(i: number, p: V3): number {
    const b = this.bodies[i];
    const l = this.toLocal(b, p);
    let base: number;
    if (b.spec.kind === 'rock') {
      // Ellipsoid distance bound (Inigo Quilez).
      const k0 = length(v3(l.x / b.radii.x, l.y / b.radii.y, l.z / b.radii.z));
      const k1 = length(v3(l.x / (b.radii.x * b.radii.x), l.y / (b.radii.y * b.radii.y), l.z / (b.radii.z * b.radii.z)));
      base = (k0 * (k0 - 1)) / Math.max(1e-6, k1);
    } else {
      // Rounded box.
      const round = Math.min(b.radii.x, b.radii.y, b.radii.z) * 0.12;
      const qx = Math.abs(l.x) - b.radii.x + round;
      const qy = Math.abs(l.y) - b.radii.y + round;
      const qz = Math.abs(l.z) - b.radii.z + round;
      const outside = Math.hypot(Math.max(qx, 0), Math.max(qy, 0), Math.max(qz, 0));
      const inside = Math.min(Math.max(qx, qy, qz), 0);
      base = outside + inside - round;
    }
    if (b.noiseAmp <= 0) return base;
    const nz = fbm3(l.x * b.frequency, l.y * b.frequency, l.z * b.frequency, b.spec.noiseSeed, 3);
    // The displacement is faded out away from the surface, so the field stays
    // a usable distance estimate (the displacement's gradient would otherwise
    // dominate far from the object, where the true distance is smooth).
    const fade = Math.max(0, 1 - Math.abs(base) / (2.5 * b.noiseAmp + 0.05));
    return base - nz * b.noiseAmp * fade;
  }

  /**
   * Distance and normal of one object. `eps` is the finite-difference scale of
   * the normal: the roots use a scale of their own radius so the fBm detail of
   * the surface is filtered out of the steering, the preview mesh uses a small one.
   */
  sample(i: number, p: V3, eps = 0.006): SdfSample {
    const d = this.distance(i, p);
    const e = eps;
    const n = normalize(
      v3(
        this.distance(i, v3(p.x + e, p.y, p.z)) - this.distance(i, v3(p.x - e, p.y, p.z)),
        this.distance(i, v3(p.x, p.y + e, p.z)) - this.distance(i, v3(p.x, p.y - e, p.z)),
        this.distance(i, v3(p.x, p.y, p.z + e)) - this.distance(i, v3(p.x, p.y, p.z - e)),
      ),
    );
    return { d, n, obstacle: i };
  }

  /** Nearest object sample within `maxDist`, or null. */
  nearest(p: V3, maxDist = Infinity, eps = 0.006): SdfSample | null {
    let best: SdfSample | null = null;
    for (let i = 0; i < this.bodies.length; i++) {
      const b = this.bodies[i];
      const lower = length(sub(p, b.center)) - b.bound;
      if (lower > maxDist) continue;
      if (best && lower > best.d) continue;
      const s = this.sample(i, p, eps);
      if (s.d <= maxDist && (!best || s.d < best.d)) best = s;
    }
    return best;
  }

  center(i: number): V3 {
    return this.bodies[i].center;
  }
  bound(i: number): number {
    return this.bodies[i].bound;
  }
  top(i: number): number {
    return this.bodies[i].top;
  }

  /** True when `p` is at least `margin` away from every object surface. */
  isFree(p: V3, margin = 0): boolean {
    for (let i = 0; i < this.bodies.length; i++) {
      const b = this.bodies[i];
      if (length(sub(p, b.center)) - b.bound > margin) continue;
      if (this.distance(i, p) < margin) return false;
    }
    return true;
  }

  /** Move a point out of any object it penetrates until it is `margin` away. */
  pushOut(p: V3, margin: number, eps = 0.006): V3 {
    let q = p;
    for (let iter = 0; iter < 8; iter++) {
      const s = this.nearest(q, margin, eps);
      if (!s || s.d >= margin - 1e-5) break;
      q = addScaled(q, s.n, margin - s.d + 1e-4);
    }
    return q;
  }

  /** Preview mesh of one object, ray-marched from the same field the roots use. */
  meshObstacle(i: number, segments = 40, rings = 26): ObstacleMeshData {
    const b = this.bodies[i];
    const rMean = b.spec.size;
    if (b.spec.kind === 'block') {
      segments = 56;
      rings = 36;
    }
    const pos: number[] = [];
    const idx: number[] = [];
    for (let ri = 0; ri <= rings; ri++) {
      const phi = (ri / rings) * Math.PI;
      for (let sg = 0; sg <= segments; sg++) {
        const th = (sg / segments) * Math.PI * 2;
        const dir = v3(Math.sin(phi) * Math.cos(th), Math.cos(phi), Math.sin(phi) * Math.sin(th));
        // Radial march to the zero level; both shapes are star-shaped about their centre.
        let t = rMean * 0.6;
        for (let it = 0; it < 40; it++) {
          const d = this.distance(i, this.toWorld(b, scale(dir, t)));
          if (Math.abs(d) < 3e-4) break;
          t = clamp(t - d * 0.7, 0.05 * rMean, 4 * rMean);
        }
        const wp = this.toWorld(b, scale(dir, t));
        pos.push(wp.x, wp.y, wp.z);
      }
    }
    for (let ri = 0; ri < rings; ri++) {
      for (let sg = 0; sg < segments; sg++) {
        const a = ri * (segments + 1) + sg;
        const c = a + segments + 1;
        idx.push(a, a + 1, c, a + 1, c + 1, c);
      }
    }
    const nrm: number[] = [];
    for (let k = 0; k < pos.length; k += 3) {
      const s = this.sample(i, v3(pos[k], pos[k + 1], pos[k + 2]));
      nrm.push(s.n.x, s.n.y, s.n.z);
    }
    return { kind: b.spec.kind, positions: new Float32Array(pos), normals: new Float32Array(nrm), indices: new Uint32Array(idx) };
  }

  meshAll(): ObstacleMeshData[] {
    const out: ObstacleMeshData[] = [];
    for (let i = 0; i < this.bodies.length; i++) out.push(this.meshObstacle(i));
    return out;
  }
}
