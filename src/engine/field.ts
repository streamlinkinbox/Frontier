import { clamp, mix, smoothstep, rayBox, add, scale, normalize } from "./math";
import {
  BOUNDS_MIN,
  BOUNDS_MAX,
  WORLD_SIZE,
  type Vec3,
  type Settings,
  type VolumeSize,
  type FrameState,
} from "./types";

// Matching hash/noise implementations are used by the WGSL initializer. No
// heightfield is stored: every sample is a signed distance in a 3D volume.
export function hash(x: number, y: number, z: number): number {
  let a =
    (Math.imul(x | 0, 73856093) ^
      Math.imul(y | 0, 19349663) ^
      Math.imul(z | 0, 83492791)) >>>
    0;
  a = Math.imul(a ^ (a >>> 13), 1274126177) >>> 0;
  return ((a ^ (a >>> 16)) >>> 0) / 4294967295;
}
export function noise(x: number, y: number, z: number): number {
  const ix = Math.floor(x),
    iy = Math.floor(y),
    iz = Math.floor(z);
  let fx = x - ix,
    fy = y - iy,
    fz = z - iz;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  fz = fz * fz * (3 - 2 * fz);
  return (
    mix(
      mix(
        mix(hash(ix, iy, iz), hash(ix + 1, iy, iz), fx),
        mix(hash(ix, iy + 1, iz), hash(ix + 1, iy + 1, iz), fx),
        fy,
      ),
      mix(
        mix(hash(ix, iy, iz + 1), hash(ix + 1, iy, iz + 1), fx),
        mix(hash(ix, iy + 1, iz + 1), hash(ix + 1, iy + 1, iz + 1), fx),
        fy,
      ),
      fz,
    ) *
      2 -
    1
  );
}
const smin = (a: number, b: number, k: number) => {
  const h = clamp(0.5 + (0.5 * (b - a)) / k, 0, 1);
  return mix(b, a, h) - k * h * (1 - h);
};
const ellipsoid = (
  x: number,
  y: number,
  z: number,
  rx: number,
  ry: number,
  rz: number,
) => {
  const k0 = Math.hypot(x / rx, y / ry, z / rz),
    k1 = Math.hypot(x / (rx * rx), y / (ry * ry), z / (rz * rz));
  return k0 < 0.00001
    ? -Math.min(rx, ry, rz)
    : (k0 * (k0 - 1)) / Math.max(k1, 0.00001);
};
const roundedBox = (
  x: number,
  y: number,
  z: number,
  bx: number,
  by: number,
  bz: number,
  r = 0,
) => {
  const qx = Math.abs(x) - bx,
    qy = Math.abs(y) - by,
    qz = Math.abs(z) - bz;
  return (
    Math.hypot(Math.max(qx, 0), Math.max(qy, 0), Math.max(qz, 0)) +
    Math.min(Math.max(qx, qy, qz), 0) -
    r
  );
};

/** Faceted, leaning rock stacks: broad caprock, narrow necks, fins and ledges.
 * The cross-section is intentionally non-monotonic, never a tapered cone. */
export function rockStack(
  x: number,
  y: number,
  z: number,
  h: number,
  r: number,
  index: number,
  seed: number,
): number {
  const bound = roundedBox(
    x,
    y - h * 0.5,
    z,
    r * 2.2 + 3,
    h * 0.65 + 3,
    r * 2.2 + 3,
  );
  if (bound > 3) return bound;
  const phase = hash(index, seed, 11) * 6.2831853,
    u = clamp(y / h, 0, 1);
  const px = x - u * (hash(index, seed, 12) - 0.5) * 4.6;
  const pz = z - u * (hash(index, seed, 13) - 0.5) * 4.6;
  const angle = phase * 0.55,
    c = Math.cos(angle),
    sn = Math.sin(angle);
  const rx = px * c - pz * sn,
    rz = px * sn + pz * c;
  const coarse = noise(px * 0.24 + seed, y * 0.17, pz * 0.24);
  const kind = index % 3;
  let wx = r,
    wz = r;
  if (kind === 0) {
    const waist = (u - 0.72) / 0.16;
    wx =
      r *
      (1.02 -
        0.34 * smoothstep(0.05, 0.4, u) -
        0.19 * Math.exp(-waist * waist));
    wz = wx * 0.83;
  } else if (kind === 1) {
    wx = r * 0.5;
    wz = r * 1.38;
  } else {
    wx = r * (u < 0.32 ? 1.1 : u < 0.67 ? 0.78 : 1.0);
    wz = wx * 0.84;
  }
  const ledges = Math.sin(y * 0.83 + phase) * 0.24 + coarse * 0.35;
  const qx = rx / Math.max(1.1, wx + ledges),
    qz = rz / Math.max(1.1, wz + ledges);
  const polygon = Math.max(
    Math.abs(qx * 0.94 + qz * 0.34),
    Math.max(Math.abs(-qx * 0.55 + qz * 0.84), Math.abs(qx * 0.28 + qz * 0.96)),
  );
  const joints =
    Math.pow(
      Math.abs(Math.sin(rx * 0.93 + rz * 0.62 + phase + coarse * 0.6)),
      14,
    ) * 0.4;
  const side = (polygon - 1) * Math.min(wx, wz) + joints + coarse * 0.34;
  const top =
    y -
    (h - (kind === 0 ? 2.4 : 0)) +
    coarse * 0.8 +
    Math.sin(rx * 0.5 + phase) * 0.35;
  let d =
    Math.hypot(Math.max(side, 0), Math.max(top, 0)) +
    Math.min(Math.max(side, top), 0);
  if (kind === 0) {
    const cap =
      roundedBox(
        rx - r * 0.16,
        y - (h - 1.8) + rx * 0.12,
        rz + r * 0.09,
        r * 1.17,
        1.65,
        r * 0.88,
        0.85,
      ) +
      coarse * 0.45;
    d = smin(d, cap, 0.85);
  }
  if (kind === 1) {
    const notch = ellipsoid(
      rx + r * 0.4,
      y - h * 0.61,
      rz - r * 0.68,
      r * 0.64,
      1.9,
      r * 0.72,
    );
    d = Math.max(d, -notch);
  }
  return d;
}
export function organicArch(
  x: number,
  y: number,
  z: number,
  seed: number,
): number {
  const warp = noise(x * 0.053 + seed, y * 0.052, z * 0.053);
  const wx = x + warp * 1.15 + Math.sin(y * 0.14 + seed * 0.01) * 0.42;
  const wy = y + noise(x * 0.079 + seed, y * 0.04, z * 0.078) * 0.72;
  const wz = z + noise(x * 0.041 + seed, y * 0.07, z * 0.065) * 1.2 + x * 0.054;
  const left = roundedBox(
    wx + 19 + (wy - 14) * 0.1,
    wy - 13,
    wz + 1,
    8.3,
    14,
    5.5,
    1.8,
  );
  const right = roundedBox(
    wx - 20.5 - (wy - 13) * 0.1,
    wy - 11.5,
    wz - 2.2,
    5.8,
    11.5,
    4.7,
    1.5,
  );
  const roof = roundedBox(
    wx - 1,
    wy - 27 + wx * 0.075 + noise(wx * 0.18 + seed, 0, wz * 0.18) * 0.9,
    wz,
    19,
    4.6,
    5.7,
    1.35,
  );
  let d = smin(smin(left, roof, 3.1), right, 2.1);
  const opening = smin(
    ellipsoid(wx + 2.5, wy - 10.5, wz + 1, 14.7, 13.7, 17),
    ellipsoid(wx - 6, wy - 8, wz - 2, 11.6, 11.7, 17),
    2.3,
  );
  d = Math.max(d, -opening);
  // Unequal alcoves, a small weathered window, and chipped roof break symmetry.
  const window = ellipsoid(wx + 24.5, wy - 18, wz, 2.25, 3.6, 13);
  const chip = ellipsoid(wx - 8, wy - 34, wz - 4, 3.8, 2.8, 7);
  d = Math.max(d, -Math.min(window, chip));
  const weather = noise(wx * 0.23 + seed, wy * 0.28, wz * 0.24);
  d +=
    weather * 0.58 +
    Math.pow(Math.abs(Math.sin(wx * 0.29 + wz * 0.16 + warp)), 18) * 0.3;
  const t1 = rockStack(x + 31, y, z - 22, 21, 5.5, 2, seed);
  const t2 = rockStack(x - 29, y, z + 22, 16.5, 4.8, 1, seed);
  return smin(d, Math.min(t1, t2), 1.4);
}
export function spireField(
  x: number,
  y: number,
  z: number,
  seed: number,
): number {
  let rock = 50;
  for (let i = 0; i < 9; i++) {
    const cx = ((i % 3) - 1) * 24 + (hash(i, seed, 0) - 0.5) * 10;
    const cz = (Math.floor(i / 3) - 1) * 25 + (hash(i, seed, 1) - 0.5) * 10;
    const h = 13 + hash(i, seed, 2) * 18,
      r = 4.0 + hash(i, seed, 3) * 2.5;
    rock = smin(rock, rockStack(x - cx, y, z - cz, h, r, i, seed), 1.4);
  }
  return rock;
}

export function terrainSdf(
  x: number,
  y: number,
  z: number,
  settings: Pick<Settings, "preset" | "seed">,
): number {
  const seed = settings.seed % 8192;
  const n = noise(x * 0.065 + seed, y * 0.038, z * 0.065);
  const fine = noise(x * 0.26 + seed, y * 0.3, z * 0.26);
  const bedding =
    Math.sin(y * 1.38 + n * 1.4) * 0.23 + Math.sin(y * 3.6 + n) * 0.075;
  const ground = y + 0.6 - noise(x * 0.1 + seed, 0, z * 0.1) * 0.23;
  let d = ground;
  if (settings.preset === "canyon") {
    const curve =
      x + 9.5 * Math.sin(z * 0.058) + 3.3 * Math.sin(z * 0.117 + seed * 0.01);
    const floorHeight =
      -0.8 +
      smoothstep(3, 17, Math.abs(curve)) * 4.4 +
      noise(x * 0.13 + seed, 0, z * 0.13) * 0.35;
    let rock = 50;
    for (let i = 0; i < 6; i++) {
      const cx = (i % 2 === 0 ? -25 : 24) + (hash(i, seed, 4) - 0.5) * 5;
      const cz = -28 + Math.floor(i / 2) * 28 + (hash(i, seed, 5) - 0.5) * 4;
      const height = 19 + hash(i, seed, 6) * 14;
      const rx = 14.5 + hash(i, seed, 7) * 4,
        rz = 17.5 + hash(i, seed, 8) * 3;
      const ledge =
        Math.sin(y * 0.67 + n * 1.1) * 0.72 + Math.sin(y * 1.7) * 0.24;
      const taper = Math.max(y - 2, 0) * 0.095;
      const dx = (x - cx + n * 1.8) / (rx - taper + ledge),
        dz = (z - cz + n) / (rz - taper + ledge);
      const poly = Math.max(
        Math.abs(dx * 0.94 + dz * 0.34),
        Math.max(
          Math.abs(-dx * 0.55 + dz * 0.84),
          Math.abs(dx * 0.28 + dz * 0.96),
        ),
      );
      const fractures =
        Math.pow(
          Math.abs(Math.sin((x - cx) * 0.66 + (z - cz) * 0.51 + n * 2.4)),
          16,
        ) * 0.85;
      const footprint = (poly - 1) * Math.min(rx, rz) + fractures;

      const crown =
        y -
        height +
        Math.abs(noise(x * 0.17 + seed, y * 0.018, z * 0.17)) * 0.85 -
        fine * 0.3;
      const mesa =
        Math.hypot(Math.max(footprint, 0), Math.max(crown, 0)) +
        Math.min(Math.max(footprint, crown), 0);
      rock = smin(rock, mesa, 2.2);
    }
    const undercut = Math.exp(-(((y - 6) / 2.8) ** 2)) * 1.7;
    const width =
      5.4 +
      Math.max(y, 0) * 0.24 +
      Math.sin(y * 0.66 + n * 0.9) * 0.75 +
      n * 1.3 +
      undercut;
    const flutes =
      Math.pow(
        Math.abs(Math.sin(z * 0.41 + noise(x * 0.08 + seed, 2, z * 0.12) * 2)),
        10,
      ) * 1.8;
    const cut = (Math.abs(curve) - width - flutes) * 0.78;
    rock = Math.max(rock, -cut);
    d = smin(y - floorHeight, rock, 2.5);
    const c1 = ellipsoid(x + 12, y - 6, z - 12, 8.2, 4.6, 8);
    const c2 = ellipsoid(x - 12, y - 7, z + 23, 7.7, 4.1, 6.4);
    d = Math.max(d, -Math.min(c1, c2));
    const gx = Math.floor(x / 6),
      gz = Math.floor(z / 6),
      scatter = hash(gx, seed, gz);
    if (scatter > 0.53 && Math.abs(curve) > 8) {
      const r = 0.6 + hash(gx, seed + 1, gz) * 1.2;
      const bx = (gx + 0.25 + hash(gx, seed + 2, gz) * 0.5) * 6,
        bz = (gz + 0.25 + hash(gx, seed + 3, gz) * 0.5) * 6;
      d = Math.min(
        d,
        ellipsoid(
          x - bx,
          y - floorHeight - r * 0.12,
          z - bz,
          r,
          r * 0.68,
          r * 0.8,
        ),
      );
    }
  } else if (settings.preset === "arches") {
    d = smin(ground, organicArch(x, y, z, seed), 2.2);
  } else {
    d = smin(ground, spireField(x, y, z, seed), 2.1);
  }
  // Bed joints and volumetric weathering modify all faces, including undersides.
  d += bedding * 0.58 + fine * 0.52 + n * 0.26;
  const qx = Math.abs(x) - 44.6,
    qy = Math.abs(y - 16.7) - 18.1,
    qz = Math.abs(z) - 44.6;
  const box =
    Math.hypot(Math.max(qx, 0), Math.max(qy, 0), Math.max(qz, 0)) +
    Math.min(Math.max(qx, qy, qz), 0) -
    1.1;
  return clamp(Math.max(d, box), -24, 32);
}
export function generateField(
  size: VolumeSize,
  settings: Settings,
): Float32Array {
  const field = new Float32Array(size.x * size.y * size.z * 4);
  for (let z = 0; z < size.z; z++)
    for (let y = 0; y < size.y; y++)
      for (let x = 0; x < size.x; x++) {
        const i = ((z * size.y + y) * size.x + x) * 4;
        field[i] = terrainSdf(
          -48 + ((x + 0.5) * 96) / size.x,
          -10 + ((y + 0.5) * 48) / size.y,
          -48 + ((z + 0.5) * 96) / size.z,
          settings,
        );
      }
  return field;
}
export function sampleField(
  data: Float32Array,
  size: VolumeSize,
  p: Vec3,
  component = 0,
): number {
  const v = p.map((q, i) =>
    clamp(
      ((q - BOUNDS_MIN[i]) / WORLD_SIZE[i]) * [size.x, size.y, size.z][i] - 0.5,
      0,
      [size.x, size.y, size.z][i] - 1,
    ),
  );
  const a = v.map(Math.floor),
    b = a.map((q, i) => Math.min(q + 1, [size.x, size.y, size.z][i] - 1)),
    f = v.map((q, i) => q - a[i]);
  const at = (x: number, y: number, z: number) =>
    data[((z * size.y + y) * size.x + x) * 4 + component];
  return mix(
    mix(
      mix(at(a[0], a[1], a[2]), at(b[0], a[1], a[2]), f[0]),
      mix(at(a[0], b[1], a[2]), at(b[0], b[1], a[2]), f[0]),
      f[1],
    ),
    mix(
      mix(at(a[0], a[1], b[2]), at(b[0], a[1], b[2]), f[0]),
      mix(at(a[0], b[1], b[2]), at(b[0], b[1], b[2]), f[0]),
      f[1],
    ),
    f[2],
  );
}
export function pickField(
  data: Float32Array,
  size: VolumeSize,
  f: FrameState,
  x: number,
  y: number,
): Vec3 | null {
  const rd = normalize(
    add(
      f.forward,
      add(
        scale(f.right, ((x * f.width) / f.height) * 0.414214),
        scale(f.up, y * 0.414214),
      ),
    ),
  );
  const hit = rayBox(f.eye, rd, BOUNDS_MIN, BOUNDS_MAX);
  if (!hit) return null;
  let t = hit[0],
    previousT = t,
    previousD = sampleField(data, size, add(f.eye, scale(rd, t)));
  for (let i = 0; i < 280 && t < hit[1]; i++) {
    const p = add(f.eye, scale(rd, t));
    const d = sampleField(data, size, p);
    if (Math.abs(d) < 0.018 + Math.min(t * 0.0003, 0.045)) return p;
    if (d * previousD < 0) {
      let lo = previousT,
        hi = t;
      for (let j = 0; j < 7; j++) {
        const mid = (lo + hi) * 0.5;
        if (sampleField(data, size, add(f.eye, scale(rd, mid))) * previousD > 0)
          lo = mid;
        else hi = mid;
      }
      return add(f.eye, scale(rd, (lo + hi) * 0.5));
    }
    previousT = t;
    previousD = d;
    t += Math.max(0.015, Math.abs(d) * 0.65);
  }
  return null;
}
export function sculptField(
  data: Float32Array,
  size: VolumeSize,
  center: Vec3,
  tool: string,
  s: Settings,
): void {
  const cell = 96 / size.x,
    r = s.radius;
  const ranges = center.map((c, i) => [
    Math.max(1, Math.floor((c - r - BOUNDS_MIN[i]) / cell)),
    Math.min(
      [size.x, size.y, size.z][i] - 2,
      Math.ceil((c + r - BOUNDS_MIN[i]) / cell),
    ),
  ]);
  const source = tool === "smooth" ? data.slice() : data;
  for (let z = ranges[2][0]; z <= ranges[2][1]; z++)
    for (let y = ranges[1][0]; y <= ranges[1][1]; y++)
      for (let x = ranges[0][0]; x <= ranges[0][1]; x++) {
        const px = -48 + (x + 0.5) * cell,
          py = -10 + (y + 0.5) * cell,
          pz = -48 + (z + 0.5) * cell;
        const dist = Math.hypot(px - center[0], py - center[1], pz - center[2]);
        if (dist >= r) continue;
        const i = ((z * size.y + y) * size.x + x) * 4,
          w =
            (1 - smoothstep(r * (1 - s.falloff), r, dist)) * s.strength * 0.38;
        if (tool === "add") data[i] -= w * cell * 1.5;
        if (tool === "carve") data[i] += w * cell * 1.5;
        if (tool === "flatten") data[i] = mix(data[i], py - center[1], w);
        if (tool === "smooth")
          data[i] = mix(
            data[i],
            (source[i - 4] +
              source[i + 4] +
              source[i - size.x * 4] +
              source[i + size.x * 4] +
              source[i - size.x * size.y * 4] +
              source[i + size.x * size.y * 4]) /
              6,
            w,
          );
      }
}
