// Frontier Fluid — river paths.
//
// A RiverPath is a Catmull-Rom spline through control points with a bed
// elevation at each point. Baking it into the sim's bed/flow data:
//   * carves a U-shaped channel into the terrain heights (monotonically
//     descending, so water always flows the right way),
//   * writes a flow field (target velocity, parabolic across the channel —
//     fastest mid-stream) that the solver relaxes toward inside the channel,
//   * writes a channel mask controlling how strongly that forcing applies.
// Pair it with a source at the head and a sink at the mouth (getSource /
// getSink) and you have a living river: block it, splash in it, drive
// through it — the water obeys the solver, the spline only guides it.

import type { FluidSource } from './types';

export interface RiverPoint { x: number; z: number; /** bed elevation (world Y) */ y: number }

export interface RiverOptions {
  /** Half-width of the water channel (m). */
  width: number;
  /** How deep below the surrounding bank the bed sits (m). */
  depth: number;
  /** Mid-stream target flow speed (m/s). */
  speed: number;
  /** Extra metres of drop forced per metre travelled (keeps flow honest). */
  minSlope?: number;
}

interface Sample { x: number; z: number; y: number; tx: number; tz: number; arc: number }

export class RiverPath {
  readonly points: RiverPoint[];
  readonly opts: Required<RiverOptions>;
  private samples: Sample[] = [];

  constructor(points: RiverPoint[], opts: RiverOptions) {
    this.points = points;
    this.opts = { minSlope: 0.004, ...opts };
    this.buildSamples();
  }

  private catmull(p0: number, p1: number, p2: number, p3: number, t: number): number {
    const t2 = t * t, t3 = t2 * t;
    return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
  }

  private buildSamples(): void {
    const pts = this.points;
    const ext = [pts[0], ...pts, pts[pts.length - 1]];
    const per = 24;
    const raw: Sample[] = [];
    for (let s = 0; s < ext.length - 3; s++) {
      for (let i = 0; i < per; i++) {
        const t = i / per;
        const x = this.catmull(ext[s].x, ext[s + 1].x, ext[s + 2].x, ext[s + 3].x, t);
        const z = this.catmull(ext[s].z, ext[s + 1].z, ext[s + 2].z, ext[s + 3].z, t);
        const y = this.catmull(ext[s].y, ext[s + 1].y, ext[s + 2].y, ext[s + 3].y, t);
        raw.push({ x, z, y, tx: 0, tz: 1, arc: 0 });
      }
    }
    const last = pts[pts.length - 1];
    raw.push({ x: last.x, z: last.z, y: last.y, tx: 0, tz: 1, arc: 0 });
    // tangents, arc length, and a monotonic descent so the river can't dam itself
    let arc = 0;
    for (let i = 0; i < raw.length; i++) {
      const a = raw[Math.max(0, i - 1)], b = raw[Math.min(raw.length - 1, i + 1)];
      const dx = b.x - a.x, dz = b.z - a.z;
      const l = Math.hypot(dx, dz) || 1;
      raw[i].tx = dx / l; raw[i].tz = dz / l;
      if (i > 0) {
        const seg = Math.hypot(raw[i].x - raw[i - 1].x, raw[i].z - raw[i - 1].z);
        arc += seg;
        raw[i].y = Math.min(raw[i].y, raw[i - 1].y - this.opts.minSlope * seg);
      }
      raw[i].arc = arc;
    }
    this.samples = raw;
  }

  get length(): number { return this.samples.length ? this.samples[this.samples.length - 1].arc : 0; }
  get head(): Sample { return this.samples[0]; }
  get mouth(): Sample { return this.samples[this.samples.length - 1]; }

  /** Inflow at the river head. flow = m/s of depth added at centre. */
  getSource(flow = 0.55): FluidSource {
    const h = this.head;
    return {
      position: [h.x, h.y + 1, h.z],
      radius: this.opts.width * 0.95,
      flow,
      velocity: [h.tx * this.opts.speed, h.tz * this.opts.speed],
    };
  }

  /** Drain at the river mouth. */
  getSink(flow = -0.5): FluidSource {
    const m = this.mouth;
    return {
      position: [m.x, m.y + 1, m.z],
      radius: this.opts.width * 1.15,
      flow,
      velocity: [0, 0],
    };
  }

  /**
   * Carve the channel and write flow data into sim arrays (res*res, row-major,
   * index = iz*res + ix, mapping the worldSize square around center).
   * Call before FluidSim.setBed().
   */
  bakeInto(
    bed: Float32Array, flowX: Float32Array, flowZ: Float32Array, channel: Float32Array,
    res: number, worldSize: number, center: [number, number],
  ): void {
    const { width, depth, speed } = this.opts;
    const dx = worldSize / res;
    const influence = width * 2.6;
    const radCells = Math.ceil(influence / dx);
    const toCell = (x: number, z: number) => [
      Math.floor(((x - center[0]) / worldSize + 0.5) * res),
      Math.floor(((z - center[1]) / worldSize + 0.5) * res),
    ];
    const smooth = (a: number, b: number, x: number) => {
      const t = Math.max(0, Math.min(1, (x - a) / (b - a)));
      return t * t * (3 - 2 * t);
    };

    // Per-cell nearest-sample info, gathered by stamping around each sample.
    const bestDist = new Float32Array(res * res).fill(Infinity);
    const bestY = new Float32Array(res * res);
    const bestTx = new Float32Array(res * res);
    const bestTz = new Float32Array(res * res);

    for (const sm of this.samples) {
      const [cx, cz] = toCell(sm.x, sm.z);
      for (let j = cz - radCells; j <= cz + radCells; j++) {
        if (j < 0 || j >= res) continue;
        for (let i = cx - radCells; i <= cx + radCells; i++) {
          if (i < 0 || i >= res) continue;
          const wx = center[0] + ((i + 0.5) / res - 0.5) * worldSize;
          const wz = center[1] + ((j + 0.5) / res - 0.5) * worldSize;
          const d = Math.hypot(wx - sm.x, wz - sm.z);
          const idx = j * res + i;
          if (d < bestDist[idx]) {
            bestDist[idx] = d;
            bestY[idx] = sm.y;
            bestTx[idx] = sm.tx;
            bestTz[idx] = sm.tz;
          }
        }
      }
    }

    for (let idx = 0; idx < res * res; idx++) {
      const d = bestDist[idx];
      if (!isFinite(d) || d > influence) continue;
      const bedY = bestY[idx];
      // U-profile: bedY at centre rising to bank level (bedY + depth) at |d| = width,
      // then feathered into the existing terrain out to the influence radius.
      const prof = smooth(0, 1, Math.min(d / width, 1));
      const channelBed = bedY + prof * depth;
      const feather = 1 - smooth(width * 0.9, influence, d);
      bed[idx] = bed[idx] * (1 - feather) + channelBed * feather;
      if (d < width) {
        const mask = 1 - smooth(width * 0.45, width, d);
        if (mask > channel[idx]) {
          channel[idx] = mask;
          const parabolic = Math.max(0, 1 - (d / width) * (d / width));
          flowX[idx] = bestTx[idx] * speed * (0.25 + 0.75 * parabolic);
          flowZ[idx] = bestTz[idx] * speed * (0.25 + 0.75 * parabolic);
        }
      }
    }
  }
}
