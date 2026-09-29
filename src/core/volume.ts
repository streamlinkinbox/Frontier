// ---------------------------------------------------------------------------
// Frontier core / SDF volume
// Narrow-band signed distance volume. Distances beyond +/- band are saturated,
// which bounds memory and keeps every operator O(band) instead of O(volume).
// Convention: d < 0 inside solid, d > 0 in air, d in metres.
// ---------------------------------------------------------------------------

export interface Domain {
  /** world size in metres */
  size: [number, number, number];
  /** grid resolution along X (Y/Z derived from aspect) */
  res: number;
  seed: number;
}

export const BAND_VOXELS = 6;

export class SDFVolume {
  nx: number; ny: number; nz: number;
  h: number;            // voxel size (metres, isotropic)
  band: number;         // band distance in metres (BAND_VOXELS * h)
  data: Float32Array;

  constructor(nx: number, ny: number, nz: number, h: number) {
    this.nx = nx; this.ny = ny; this.nz = nz; this.h = h;
    this.band = BAND_VOXELS * h;
    this.data = new Float32Array(nx * ny * nz);
  }

  static fromDomain(d: Domain, aspectY = 0.32): SDFVolume {
    const nx = d.res | 0;
    const nz = d.res | 0;
    const ny = Math.max(32, Math.round(d.res * aspectY));
    const h = d.size[0] / nx;
    return new SDFVolume(nx, ny, nz, h);
  }

  idx(x: number, y: number, z: number): number {
    return (z * this.ny + y) * this.nx + x;
  }

  get(x: number, y: number, z: number): number {
    return this.data[this.idx(x, y, z)];
  }

  set(x: number, y: number, z: number, v: number): void {
    this.data[this.idx(x, y, z)] = v;
  }

  /** trilinear sample in voxel space (clamped) */
  sampleVox(fx: number, fy: number, fz: number): number {
    const x0 = Math.max(0, Math.min(this.nx - 1, Math.floor(fx)));
    const y0 = Math.max(0, Math.min(this.ny - 1, Math.floor(fy)));
    const z0 = Math.max(0, Math.min(this.nz - 1, Math.floor(fz)));
    const x1 = Math.min(this.nx - 1, x0 + 1);
    const y1 = Math.min(this.ny - 1, y0 + 1);
    const z1 = Math.min(this.nz - 1, z0 + 1);
    const tx = fx - x0, ty = fy - y0, tz = fz - z0;
    const c000 = this.get(x0, y0, z0), c100 = this.get(x1, y0, z0);
    const c010 = this.get(x0, y1, z0), c110 = this.get(x1, y1, z0);
    const c001 = this.get(x0, y0, z1), c101 = this.get(x1, y0, z1);
    const c011 = this.get(x0, y1, z1), c111 = this.get(x1, y1, z1);
    const l = (a: number, b: number, t: number) => a + (b - a) * t;
    return l(
      l(l(c000, c100, tx), l(c010, c110, tx), ty),
      l(l(c001, c101, tx), l(c011, c111, tx), ty),
      tz,
    );
  }

  /** central-difference gradient in voxel space */
  gradient(x: number, y: number, z: number): [number, number, number] {
    const gx = this.get(Math.min(this.nx - 1, x + 1), y, z) - this.get(Math.max(0, x - 1), y, z);
    const gy = this.get(x, Math.min(this.ny - 1, y + 1), z) - this.get(x, Math.max(0, y - 1), z);
    const gz = this.get(x, y, Math.min(this.nz - 1, z + 1)) - this.get(x, y, Math.max(0, z - 1));
    return [gx * 0.5, gy * 0.5, gz * 0.5];
  }

  saturate(): void {
    const s = this.band, d = this.data;
    for (let i = 0; i < d.length; i++) {
      if (d[i] > s) d[i] = s;
      else if (d[i] < -s) d[i] = -s;
    }
  }

  fillSolid(): void { this.data.fill(-this.band); }
  fillEmpty(): void { this.data.fill(this.band); }

  clone(): SDFVolume {
    const v = new SDFVolume(this.nx, this.ny, this.nz, this.h);
    v.data.set(this.data);
    return v;
  }

  /** count of voxels inside the narrow band (diagnostics / cost estimate) */
  bandVoxelCount(): number {
    const s = this.band; let n = 0;
    for (let i = 0; i < this.data.length; i++) if (this.data[i] < s && this.data[i] > -s) n++;
    return n;
  }
}

// ---------------------------------------------------------------------------
// SDF algebra — sharp by default. Smooth blends are opt-in per operation and
// always clamped so the result stays a valid (Lipschitz <= 1) distance field.
// ---------------------------------------------------------------------------

export function opUnion(a: number, b: number): number { return Math.min(a, b); }
export function opSubtract(a: number, b: number): number { return Math.max(a, -b); }
export function opIntersect(a: number, b: number): number { return Math.max(a, b); }

export function opSmoothUnion(a: number, b: number, k: number): number {
  if (k <= 1e-6) return Math.min(a, b);
  const h = Math.max(0, Math.min(1, 0.5 + 0.5 * (b - a) / k));
  return b + (a - b) * h - k * h * (1 - h);
}

export function opSmoothSubtract(a: number, b: number, k: number): number {
  if (k <= 1e-6) return Math.max(a, -b);
  const h = Math.max(0, Math.min(1, 0.5 - 0.5 * (a + b) / k));
  return b - (a + b) * h + k * h * (1 - h);
}

/** analytic primitives (metres, voxel-space inputs converted by caller) */
export function sdfSphere(px: number, py: number, pz: number, cx: number, cy: number, cz: number, r: number): number {
  return Math.sqrt((px - cx) ** 2 + (py - cy) ** 2 + (pz - cz) ** 2) - r;
}

export function sdfBox(px: number, py: number, pz: number, cx: number, cy: number, cz: number, hx: number, hy: number, hz: number): number {
  const qx = Math.abs(px - cx) - hx, qy = Math.abs(py - cy) - hy, qz = Math.abs(pz - cz) - hz;
  const ax = Math.max(qx, 0), ay = Math.max(qy, 0), az = Math.max(qz, 0);
  return Math.sqrt(ax * ax + ay * ay + az * az) + Math.min(Math.max(qx, Math.max(qy, qz)), 0);
}

export function sdfCapsule(px: number, py: number, pz: number, ax: number, ay: number, az: number, bx: number, by: number, bz: number, r: number): number {
  const pax = px - ax, pay = py - ay, paz = pz - az;
  const bax = bx - ax, bay = by - ay, baz = bz - az;
  const dot = Math.max(0, Math.min(1, (pax * bax + pay * bay + paz * baz) / (bax * bax + bay * bay + baz * baz || 1)));
  const dx = pax - bax * dot, dy = pay - bay * dot, dz = paz - baz * dot;
  return Math.sqrt(dx * dx + dy * dy + dz * dz) - r;
}
