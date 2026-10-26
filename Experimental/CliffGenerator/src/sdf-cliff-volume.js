// Dense local SDF volume used only inside the steep-slope chunks selected by sdf-chunks.js.
//
// This is the salvageable core of the rejected standalone SDF editor, adapted to CliffGenerator's
// hybrid representation: the heightfield remains authoritative everywhere else, while one chunk
// volume carries the SDF plus the geology fields that weathering acts on (bed, thickness,
// hardness, joints, seepage, susceptibility and loose sediment). The volume is built with a
// halo around the chunk footprint and only the interior is polygonised, so the narrow-band
// updates below cannot see an artificial boundary at a face shared with a neighbouring chunk.
//
// Two deliberate differences from the standalone whole-terrain volume:
//   * no global redistancing — a fast-sweep pass would move values on W = 0 columns, where the
//     field must stay exactly y - h for the watertight heightfield seam. The weathering update
//     is Hamilton-Jacobi (delta scaled by |grad sdf|), so the front speed stays meaningful
//     without reinitialisation over the few relaxation passes a chunk gets.
//   * block fall only *detaches* blocks. In the standalone editor the fallen block was landed
//     in the same volume; here the foot of the face is usually heightfield again, and the
//     editor's rock/talus placement already owns fallen material. Landing boxes inside a
//     cropped chunk would sever them at chunk seams.

const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
const clamp01 = (x) => clamp(x, 0, 1);
const lerp = (a, b, t) => a + (b - a) * t;
const smin = (a, b, k) => {
  if (k <= 1e-6) return Math.min(a, b);
  const h = clamp01(0.5 + 0.5 * (b - a) / k);
  return lerp(b, a, h) - k * h * (1 - h);
};
const smax = (a, b, k) => -smin(-a, -b, k);

function hash01(x, y, z, seed = 0) {
  let h = (Math.imul(x | 0, 374761393) + Math.imul(y | 0, 668265263)
    + Math.imul(z | 0, 1274126177) + Math.imul(seed | 0, 2246822519)) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  h ^= h >>> 16;
  return (h >>> 0) / 4294967296;
}

export class CliffVolume {
  constructor(nx, ny, nz, spacing, origin) {
    this.nx = nx; this.ny = ny; this.nz = nz;
    this.spacing = spacing;
    this.origin = origin.slice();
    const n = nx * ny * nz;
    this.sdf = new Float32Array(n).fill(10);
    this.hardness = new Uint8Array(n).fill(180);
    this.joint = new Uint8Array(n);
    this.seep = new Uint8Array(n).fill(127);
    this.susceptibility = new Uint8Array(n);
    this.bed = new Uint16Array(n);
    this.bedThick = new Float32Array(n).fill(10);
    this.sediment = new Float32Array(n);
    // per-column (x,z) data carried over from the heightfield maps
    this.weight = new Float32Array(nx * nz);   // chunk blend weight W (0 = plain heightfield)
    this.height = new Float32Array(nx * nz);   // unsheared heightfield height
    this.steep = new Float32Array(nx * nz);    // heightfield steepness factor
    this.outcrop = new Float32Array(nx * nz);  // embedded-boulder mask
    this.bandLo = new Int32Array(nx * nz);
    this.bandHi = new Int32Array(nx * nz).fill(Math.max(0, ny - 1));
  }

  index(ix, iy, iz) { return (iz * this.ny + iy) * this.nx + ix; }
  columnIndex(ix, iz) { return iz * this.nx + ix; }
  nodeX(ix) { return this.origin[0] + ix * this.spacing; }
  nodeY(iy) { return this.origin[1] + iy * this.spacing; }
  nodeZ(iz) { return this.origin[2] + iz * this.spacing; }

  sampleScalar(arr, x, y, z) {
    const u = clamp((x - this.origin[0]) / this.spacing, 0, this.nx - 1.00001);
    const v = clamp((y - this.origin[1]) / this.spacing, 0, this.ny - 1.00001);
    const w = clamp((z - this.origin[2]) / this.spacing, 0, this.nz - 1.00001);
    const ix = u | 0, iy = v | 0, iz = w | 0;
    const fx = u - ix, fy = v - iy, fz = w - iz;
    const { nx, ny } = this;
    const b = (iz * ny + iy) * nx + ix;
    const b2 = b + nx * ny;
    return lerp(
      lerp(lerp(arr[b], arr[b + 1], fx), lerp(arr[b + nx], arr[b + nx + 1], fx), fy),
      lerp(lerp(arr[b2], arr[b2 + 1], fx), lerp(arr[b2 + nx], arr[b2 + nx + 1], fx), fy), fz);
  }
  sampleSdf(x, y, z) { return this.sampleScalar(this.sdf, x, y, z); }

  nodeGradient(ix, iy, iz, out) {
    const { sdf, nx, ny, nz, spacing } = this;
    const x0 = Math.max(0, ix - 1), x1 = Math.min(nx - 1, ix + 1);
    const y0 = Math.max(0, iy - 1), y1 = Math.min(ny - 1, iy + 1);
    const z0 = Math.max(0, iz - 1), z1 = Math.min(nz - 1, iz + 1);
    const b = (iz * ny + iy) * nx + ix;
    out[0] = (sdf[b + (x1 - ix)] - sdf[b - (ix - x0)]) / ((x1 - x0) * spacing);
    out[1] = (sdf[b + (y1 - iy) * nx] - sdf[b - (iy - y0) * nx]) / ((y1 - y0) * spacing);
    out[2] = (sdf[b + (z1 - iz) * nx * ny] - sdf[b - (iz - z0) * nx * ny]) / ((z1 - z0) * spacing);
    return out;
  }

  /** Smooth subtraction of a rounded box in an orthonormal frame (joint-bounded block scars). */
  removeRoundedBox(center, half, axes, smooth = 0.2) {
    const sp = this.spacing;
    const rounding = Math.min(smooth + sp * 0.1, Math.min(half[0], half[1], half[2]) * 0.6);
    const bound = Math.hypot(half[0], half[1], half[2]) + smooth + sp;
    const x0 = clamp(Math.floor((center[0] - bound - this.origin[0]) / sp), 0, this.nx - 1);
    const x1 = clamp(Math.ceil((center[0] + bound - this.origin[0]) / sp), 0, this.nx - 1);
    const y0 = clamp(Math.floor((center[1] - bound - this.origin[1]) / sp), 0, this.ny - 1);
    const y1 = clamp(Math.ceil((center[1] + bound - this.origin[1]) / sp), 0, this.ny - 1);
    const z0 = clamp(Math.floor((center[2] - bound - this.origin[2]) / sp), 0, this.nz - 1);
    const z1 = clamp(Math.ceil((center[2] + bound - this.origin[2]) / sp), 0, this.nz - 1);
    const [ax, ay, az] = axes;
    for (let iz = z0; iz <= z1; iz++) for (let iy = y0; iy <= y1; iy++) for (let ix = x0; ix <= x1; ix++) {
      const px = this.nodeX(ix), py = this.nodeY(iy), pz = this.nodeZ(iz);
      if (Math.hypot(px - center[0], py - center[1], pz - center[2]) > bound) continue;
      const dx = px - center[0], dy = py - center[1], dz = pz - center[2];
      const lx = dx * ax[0] + dy * ax[1] + dz * ax[2];
      const ly = dx * ay[0] + dy * ay[1] + dz * ay[2];
      const lz = dx * az[0] + dy * az[1] + dz * az[2];
      const qx = Math.abs(lx) - (half[0] - rounding);
      const qy = Math.abs(ly) - (half[1] - rounding);
      const qz = Math.abs(lz) - (half[2] - rounding);
      const boxD = Math.hypot(Math.max(qx, 0), Math.max(qy, 0), Math.max(qz, 0))
        + Math.min(Math.max(qx, Math.max(qy, qz)), 0) - rounding;
      const idx = this.index(ix, iy, iz);
      this.sdf[idx] = smax(this.sdf[idx], -boxD, smooth);
      if (this.sdf[idx] > 0) this.sediment[idx] = 0;
    }
  }
}

/**
 * Narrow-band differential weathering on the chunk volume ( synchronous; the builder runs in a
 * worker). The per-node susceptibility — bed softness, notch profile, joint proximity, seepage —
 * is filled by sdf-chunks.js from the editor's own strata model; this pass supplies the feedback
 * a one-shot carve cannot: the exposure is read from the *current* field gradient, so the
 * underside of a fresh undercut and the walls of a widening joint slot keep weathering while
 * flat, sediment-mantled tops barely move. Nodes on W = 0 columns and on the volume shell are
 * never updated, which keeps both the heightfield seam and the halo bookkeeping exact.
 */
export function weatherCliffVolume(volume, opts = {}) {
  const iterations = Math.max(0, Math.round(opts.iterations ?? 4));
  const step = opts.step ?? volume.spacing * 0.25;
  const band = opts.band ?? Math.max(2.2, volume.spacing * 2.4);
  const topExposure = opts.topExposure ?? 0.1;
  const sedimentProtect = opts.sedimentProtect ?? 2.0;
  const { nx, ny, nz, sdf } = volume;
  if (!iterations || !(step > 0)) return { iterations: 0, totalDelta: 0, active: 0 };
  const delta = new Float32Array(sdf.length);
  const g = [0, 0, 0];
  let totalDelta = 0, active = 0;

  for (let iter = 0; iter < iterations; iter++) {
    delta.fill(0);
    for (let iz = 1; iz < nz - 1; iz++) for (let ix = 1; ix < nx - 1; ix++) {
      const c = iz * nx + ix;
      const w = volume.weight[c];
      if (w <= 1e-6) continue;
      const lo = Math.max(1, volume.bandLo[c]), hi = Math.min(ny - 2, volume.bandHi[c]);
      for (let iy = lo; iy <= hi; iy++) {
        const idx = (iz * ny + iy) * nx + ix;
        if (Math.abs(sdf[idx]) > band) continue;
        const susc = volume.susceptibility[idx] / 255;
        if (susc <= 0) continue;
        volume.nodeGradient(ix, iy, iz, g);
        const gl = Math.hypot(g[0], g[1], g[2]);
        if (gl < 1e-5) continue;
        const nX = g[0] / gl, nY = g[1] / gl, nZ = g[2] / gl;
        const face = Math.hypot(nX, nZ);                 // 1 on a vertical face, 0 on flat ground
        const under = clamp(-nY, 0, 1);                  // overhang undersides / shelter roofs
        const exposure = clamp(Math.max(face, under * 0.85, topExposure), 0, 1);
        const protect = 1 / (1 + volume.sediment[idx] * sedimentProtect);
        // Hamilton-Jacobi form: scaling by |grad| keeps the iso-surface speed at step*susc
        // even though the field is only an approximate distance after the bulk retreat.
        delta[idx] = step * susc * exposure * protect * w * clamp(gl, 0.3, 1.8);
      }
    }
    for (let iz = 1; iz < nz - 1; iz++) for (let ix = 1; ix < nx - 1; ix++) {
      const c = iz * nx + ix;
      if (volume.weight[c] <= 1e-6) continue;
      const lo = Math.max(1, volume.bandLo[c]), hi = Math.min(ny - 2, volume.bandHi[c]);
      for (let iy = lo; iy <= hi; iy++) {
        const idx = (iz * ny + iy) * nx + ix;
        const d = delta[idx];
        if (!d) continue;
        sdf[idx] += d;
        totalDelta += d;
        active++;
      }
    }
  }
  return { iterations, totalDelta, active };
}

/**
 * Detach joint-bounded hard blocks that weathering has left unsupported. Adapted from the
 * standalone editor's block fall: a candidate must be hard rock on a fracture (joint proximity
 * high), at the surface, not on an upward face, with air below/outboard — then a rounded box
 * (bed-thickness tall, joint-spacing wide, flat bedding/joint faces — never a sphere) is
 * subtracted. Selection is hashed on world-space cells, and a box whose bound would cross the
 * chunk's interior boundary is skipped rather than clipped, so no scar is ever severed at a
 * chunk seam. Fallen material itself is left to the editor's talus rock placement.
 */
export function detachJointBlocks(volume, opts = {}) {
  const { nx, ny, nz, sdf, spacing: sp } = volume;
  const seed = opts.seed | 0;
  const strength = opts.strength ?? 0.5;
  const loss = clamp01(opts.loss ?? 0.12);
  const blockSize = Math.max(1, opts.blockSize ?? 8);
  const maxBlocks = Math.max(0, Math.round(opts.maxBlocks ?? 24));
  const nb = opts.nodeBounds || { ix0: 1, ix1: nx - 2, iz0: 1, iz1: nz - 2 };
  const interior = opts.interior || null; // world-space chunk footprint {minX,maxX,minZ,maxZ}
  if (!maxBlocks || strength <= 0) return { candidates: 0, detached: 0 };

  const cellW = clamp(blockSize * 0.85, sp * 2.5, Math.max(sp * 2.5, 60));
  const cellH = clamp(blockSize * 0.6, sp * 2, Math.max(sp * 2, 40));
  const layer = nx * ny;
  const g = [0, 0, 0];
  const occupied = new Set();
  const candidates = [];

  for (let iz = Math.max(1, nb.iz0); iz <= Math.min(nz - 2, nb.iz1); iz++) {
    for (let ix = Math.max(1, nb.ix0); ix <= Math.min(nx - 2, nb.ix1); ix++) {
      const c = iz * nx + ix;
      if (volume.weight[c] < 0.55 || volume.steep[c] < 0.35) continue;
      const lo = Math.max(2, volume.bandLo[c]), hi = Math.min(ny - 2, volume.bandHi[c]);
      for (let iy = lo; iy <= hi; iy++) {
        const idx = (iz * ny + iy) * nx + ix;
        const s = sdf[idx];
        if (s >= 0 || s < -Math.max(1.3, sp * 1.25)) continue;
        const airNeighbour = sdf[idx - 1] > 0 || sdf[idx + 1] > 0 || sdf[idx - nx] > 0
          || sdf[idx + nx] > 0 || sdf[idx - layer] > 0 || sdf[idx + layer] > 0;
        if (!airNeighbour) continue;
        const hard = volume.hardness[idx] / 255;
        const joint = volume.joint[idx] / 255;
        if (hard < 0.56 || joint < 0.30) continue;
        volume.nodeGradient(ix, iy, iz, g);
        const gl = Math.hypot(g[0], g[1], g[2]) || 1;
        const nX = g[0] / gl, nY = g[1] / gl, nZ = g[2] / gl;
        if (nY > 0.25) continue; // upward-facing surfaces do not shed blocks
        const px = volume.nodeX(ix), py = volume.nodeY(iy), pz = volume.nodeZ(iz);
        const hl = Math.hypot(nX, nZ) || 1;
        const out = Math.max(1.3, sp * 1.1), down = Math.max(2.3, sp * 1.9);
        const below = volume.sampleSdf(px + (nX / hl) * out, py - down, pz + (nZ / hl) * out);
        const belowCentre = volume.sampleSdf(px, py - down * 1.15, pz);
        if (below < 0.7 && belowCentre < 0.7) continue; // still supported by the bed below
        const cx = Math.floor(px / cellW), cy = Math.floor(py / cellH), cz = Math.floor(pz / cellW);
        const key = cx + ':' + cy + ':' + cz;
        if (occupied.has(key)) continue;
        const score = hash01(cx, cy, cz, seed + 733);
        const prob = clamp01(strength * (0.18 + 1.65 * loss) * joint * (nY < -0.15 ? 1 : 0.55));
        if (score > prob) continue;
        occupied.add(key);
        candidates.push({ p: [px, py, pz], n: [nX, nY, nZ], thick: volume.bedThick[idx], cell: [cx, cy, cz], score });
      }
    }
  }

  candidates.sort((a, b) => a.score - b.score);
  const vnorm = (v) => { const l = Math.hypot(v[0], v[1], v[2]) || 1; return [v[0] / l, v[1] / l, v[2] / l]; };
  const vcross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  let detached = 0;

  for (const c of candidates) {
    if (detached >= maxBlocks) break;
    const h1 = hash01(c.cell[0], c.cell[1], c.cell[2], seed + 911);
    const width = clamp(blockSize * (0.8 + 0.55 * h1), sp * 2.2, Math.max(sp * 2.2, Math.min(70, sp * 9)));
    const height = clamp(Math.min(Math.max(c.thick, sp * 1.5), width * 0.8), sp * 1.5, Math.max(sp * 1.5, sp * 6));
    let depth = clamp(Math.min(Math.max(c.thick * 0.9, sp * 1.4), Math.max(sp * 1.5, blockSize * 0.55)),
      sp * 1.2, Math.max(sp * 1.2, sp * 5));
    if (opts.depth > 0) depth = clamp(depth + opts.depth * 0.45, sp * 1.2, Math.max(sp * 1.2, sp * 6));
    const half = [width / 2, height / 2, depth / 2];
    const centre = [
      c.p[0] - c.n[0] * half[2] * 0.35,
      c.p[1] - c.n[1] * half[2] * 0.35,
      c.p[2] - c.n[2] * half[2] * 0.35,
    ];
    if (interior) {
      // The guard matters: it is not enough for the box to stay on its own side of the boundary
      // plane — a box that merely *reaches* the plane would move the boundary nodes in this
      // chunk while the neighbouring chunk, which never scanned this candidate, kept the old
      // values. Keep the whole smooth-CSG support (bound + smoothing) plus a voxel away from
      // every boundary node, so shared-face values are untouched by detachment in either chunk.
      const bound = Math.hypot(half[0], half[1], half[2]) + sp * 2.25;
      if (centre[0] - bound < interior.minX || centre[0] + bound > interior.maxX
        || centre[2] - bound < interior.minZ || centre[2] + bound > interior.maxZ) continue;
    }
    let tangent = vcross([0, 1, 0], c.n);
    tangent = Math.hypot(tangent[0], tangent[1], tangent[2]) < 0.15 ? [1, 0, 0] : vnorm(tangent);
    const upish = vnorm(vcross(c.n, tangent));
    volume.removeRoundedBox(centre, half, [tangent, upish, c.n], Math.max(0.1, sp * 0.16));
    detached++;
  }
  return { candidates: candidates.length, detached };
}
