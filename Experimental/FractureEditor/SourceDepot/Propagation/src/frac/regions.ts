/**
 * Turning a live crack network into fragments.
 *
 * The crack tips rasterise into an occupancy grid. Whatever is still connected
 * through un-cracked cells is still one piece of plate. So:
 *
 *   flood fill -> connected components -> boundary edge walk -> simplify ->
 *   ear-clip triangulate -> extrude through the thickness -> rigid body.
 *
 * A component is *released* the moment it is completely surrounded by cracks
 * (nothing left holding it). Components that still touch the frame stay in the
 * window, hanging, exactly like a real broken pane -- which is why this looks
 * right and a "spawn 200 voronoi shards" approach never does.
 *
 * This runs incrementally while the cracks are still propagating, so pieces
 * start falling out of a window that is still visibly cracking.
 */

import { V3, v3 } from '../core/math';
import { MeshData } from '../geom/convex';
import { CrackNetwork } from './crack2d';

export interface ShellFragment {
  poly: number[];        // closed polygon, flat x,y in plate space [m]
  cx: number; cy: number;
  area: number;
  mesh: MeshData;        // local space, centred on (cx,cy,0)
  contacts: V3[];        // local-space contact points for the rigid solver
  radius: number;
  touchedBorder: boolean;
  cells: number;
}

export class RegionExtractor {
  private released: Uint8Array;
  private labels: Int32Array;
  private stack: Int32Array;
  readonly mask: Uint8Array;      // GPU texture payload: 255 = gone
  maskDirty = true;
  /** regions that could not be turned into geometry (diagnostics) */
  skipped = 0;

  constructor(private net: CrackNetwork) {
    const n = net.nx * net.ny;
    this.released = new Uint8Array(n);
    this.labels = new Int32Array(n);
    this.stack = new Int32Array(n);
    this.mask = new Uint8Array(n);
  }

  /**
   * Find connected components that are ready to fall out.
   * @param releaseBorder also release pieces still touching the frame
   * @param minCells ignore specks
   */
  harvest(releaseBorder: boolean, minCells = 4, limit = 400): ShellFragment[] {
    const { net } = this;
    const { nx, ny, grid, h, W, H } = net;
    const labels = this.labels;
    labels.fill(0);
    const out: ShellFragment[] = [];
    let label = 0;

    for (let start = 0; start < grid.length; start++) {
      if (grid[start] !== 0 || labels[start] !== 0 || this.released[start]) continue;
      label++;
      let sp = 0;
      this.stack[sp++] = start;
      labels[start] = label;
      let count = 0;
      let touchesBorder = false;
      let borderCells = 0;
      let minI = nx, maxI = -1, minJ = ny, maxJ = -1;
      const cellList: number[] = [];

      while (sp > 0) {
        const c = this.stack[--sp];
        const i = c % nx, j = (c / nx) | 0;
        count++;
        cellList.push(c);
        if (i < minI) minI = i; if (i > maxI) maxI = i;
        if (j < minJ) minJ = j; if (j > maxJ) maxJ = j;
        if (i === 0 || j === 0 || i === nx - 1 || j === ny - 1) { touchesBorder = true; borderCells++; }
        // 4-connected
        if (i > 0) { const n2 = c - 1; if (grid[n2] === 0 && !labels[n2] && !this.released[n2]) { labels[n2] = label; this.stack[sp++] = n2; } }
        if (i < nx - 1) { const n2 = c + 1; if (grid[n2] === 0 && !labels[n2] && !this.released[n2]) { labels[n2] = label; this.stack[sp++] = n2; } }
        if (j > 0) { const n2 = c - nx; if (grid[n2] === 0 && !labels[n2] && !this.released[n2]) { labels[n2] = label; this.stack[sp++] = n2; } }
        if (j < ny - 1) { const n2 = c + nx; if (grid[n2] === 0 && !labels[n2] && !this.released[n2]) { labels[n2] = label; this.stack[sp++] = n2; } }
      }

      if (count < minCells) continue;
      // A component covering most of the plate is still "the plate", not a shard.
      if (count > grid.length * 0.4) continue;
      // Hanging by a thread? A piece whose only remaining support is a short
      // stretch of frame will tear free on its own.
      const perim = 2 * ((maxI - minI) + (maxJ - minJ) + 2);
      const barelyHeld = touchesBorder && borderCells < perim * 0.12;
      const ready = !touchesBorder || barelyHeld || releaseBorder;
      if (!ready) continue;

      const loop = traceBoundary(labels, nx, ny, label, minI, minJ, maxI, maxJ);
      if (!loop || loop.length < 8) { this.skipped++; continue; }

      // grid corner coords -> metres, then push outward by ~1 cell so the
      // pieces touch again (the crack stroke ate a cell of material).
      const pts: number[] = [];
      for (let k = 0; k < loop.length; k += 2) {
        pts.push(loop[k] * h - W * 0.5, loop[k + 1] * h - H * 0.5);
      }
      // Simplify hard first (ear clipping is superlinear in vertex count),
      // and only fall back to a finer tolerance for very small pieces.
      let simp = rdp(pts, h * 1.1);
      if (simp.length < 8) simp = rdp(pts, h * 0.4);
      if (simp.length < 6) simp = pts;          // tiny piece: keep every corner
      if (simp.length < 6) { this.skipped++; continue; }
      if (simp.length > 96) simp = rdp(simp, h * 2.2);
      const poly = offsetPolygon(ensureCCW(simp), h * 0.75);
      const frag = buildShellFragment(poly, net.t, touchesBorder);
      if (!frag || frag.area < 1e-6) continue;
      frag.cells = count;
      out.push(frag);

      for (const c of cellList) { this.released[c] = 1; this.mask[c] = 255; }
      this.maskDirty = true;
      if (out.length >= limit) break;
    }
    return out;
  }

  /** Mark every remaining cell as gone (used when the whole pane lets go). */
  releaseAll(): void {
    this.released.fill(1);
    this.mask.fill(255);
    this.maskDirty = true;
  }
}

// ------------------------------------------------------------ boundary walk

/**
 * Walk the boundary of a labelled component as closed loops of grid corner
 * vertices (CCW, region on the left) and return the largest loop.
 *
 * Every cell of the region contributes its outward-facing sides as directed
 * edges. A vertex can carry several outgoing edges where the region pinches to
 * a point, so the walk picks the most clockwise continuation ("right hand
 * first"), which is the standard way to enumerate one face of a planar graph
 * without cutting corners.
 */
function traceBoundary(
  labels: Int32Array, nx: number, ny: number, label: number,
  minI: number, minJ: number, maxI: number, maxJ: number,
): number[] | null {
  const vw = nx + 1;
  // directions: 0:+x 1:+y 2:-x 3:-y
  const edges = new Set<number>();
  const key = (v: number, d: number) => v * 4 + d;
  const inR = (i: number, j: number) =>
    i >= 0 && j >= 0 && i < nx && j < ny && labels[j * nx + i] === label;

  for (let j = minJ; j <= maxJ; j++) {
    for (let i = minI; i <= maxI; i++) {
      if (!inR(i, j)) continue;
      if (!inR(i, j - 1)) edges.add(key(j * vw + i, 0));               // bottom, +x
      if (!inR(i + 1, j)) edges.add(key(j * vw + (i + 1), 1));         // right, +y
      if (!inR(i, j + 1)) edges.add(key((j + 1) * vw + (i + 1), 2));   // top, -x
      if (!inR(i - 1, j)) edges.add(key((j + 1) * vw + i, 3));         // left, -y
    }
  }
  if (!edges.size) return null;

  const DX = [1, 0, -1, 0], DY = [0, 1, 0, -1];
  let best: number[] | null = null;
  let bestArea = 0;

  while (edges.size) {
    const first = edges.values().next().value as number;
    let v = (first / 4) | 0;
    let d = first % 4;
    const start = v;
    const loop: number[] = [];
    let guard = 0;
    while (guard++ < edges.size + 8) {
      edges.delete(key(v, d));
      loop.push(v % vw, (v / vw) | 0);
      const nv = (((v / vw) | 0) + DY[d]) * vw + ((v % vw) + DX[d]);
      if (nv === start) break;
      // most clockwise continuation first
      let nd = -1;
      for (const t of [(d + 3) % 4, d, (d + 1) % 4, (d + 2) % 4]) {
        if (edges.has(key(nv, t))) { nd = t; break; }
      }
      if (nd < 0) break;
      v = nv; d = nd;
    }
    if (loop.length >= 6) {
      const a = Math.abs(polyArea(loop));
      if (a > bestArea) { bestArea = a; best = loop; }
    }
  }
  return best;
}

// -------------------------------------------------------------- polygon ops

/** Ramer-Douglas-Peucker on a closed polygon. */
export function rdp(pts: number[], eps: number): number[] {
  const n = pts.length / 2;
  if (n < 4) return pts.slice();
  const keep = new Uint8Array(n);
  keep[0] = 1; keep[n - 1] = 1;
  const stack: [number, number][] = [[0, n - 1]];
  while (stack.length) {
    const [a, b] = stack.pop()!;
    if (b <= a + 1) continue;
    const ax = pts[a * 2], ay = pts[a * 2 + 1];
    const bx = pts[b * 2], by = pts[b * 2 + 1];
    let dx = bx - ax, dy = by - ay;
    const L = Math.hypot(dx, dy) || 1e-9;
    dx /= L; dy /= L;
    let worst = -1, wi = -1;
    for (let i = a + 1; i < b; i++) {
      const px = pts[i * 2] - ax, py = pts[i * 2 + 1] - ay;
      const d = Math.abs(px * dy - py * dx);
      if (d > worst) { worst = d; wi = i; }
    }
    if (worst > eps && wi > 0) {
      keep[wi] = 1;
      stack.push([a, wi], [wi, b]);
    }
  }
  const out: number[] = [];
  for (let i = 0; i < n; i++) if (keep[i]) out.push(pts[i * 2], pts[i * 2 + 1]);
  return out;
}

export function polyArea(p: number[]): number {
  let a = 0;
  for (let i = 0, n = p.length / 2; i < n; i++) {
    const j = (i + 1) % n;
    a += p[i * 2] * p[j * 2 + 1] - p[j * 2] * p[i * 2 + 1];
  }
  return a * 0.5;
}

function ensureCCW(p: number[]): number[] {
  if (polyArea(p) >= 0) return p;
  const out: number[] = [];
  for (let i = p.length / 2 - 1; i >= 0; i--) out.push(p[i * 2], p[i * 2 + 1]);
  return out;
}

/** Push every vertex outward along the average of its adjacent edge normals. */
function offsetPolygon(p: number[], d: number): number[] {
  const n = p.length / 2;
  const out = new Array(n * 2);
  for (let i = 0; i < n; i++) {
    const pv = ((i - 1) + n) % n, nx2 = (i + 1) % n;
    const e1x = p[i * 2] - p[pv * 2], e1y = p[i * 2 + 1] - p[pv * 2 + 1];
    const e2x = p[nx2 * 2] - p[i * 2], e2y = p[nx2 * 2 + 1] - p[i * 2 + 1];
    const l1 = Math.hypot(e1x, e1y) || 1e-9, l2 = Math.hypot(e2x, e2y) || 1e-9;
    // outward normal of a CCW edge (dx,dy) is (dy,-dx)
    let nxs = e1y / l1 + e2y / l2;
    let nys = -e1x / l1 - e2x / l2;
    const l = Math.hypot(nxs, nys) || 1e-9;
    nxs /= l; nys /= l;
    out[i * 2] = p[i * 2] + nxs * d;
    out[i * 2 + 1] = p[i * 2 + 1] + nys * d;
  }
  return out;
}

/** Ear clipping for a simple CCW polygon. Returns index triples. */
export function earClip(p: number[]): number[] {
  const n = p.length / 2;
  const idx: number[] = [];
  for (let i = 0; i < n; i++) idx.push(i);
  const tris: number[] = [];
  const cross2 = (ax: number, ay: number, bx: number, by: number, cx: number, cy: number) =>
    (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);

  let guard = 0;
  while (idx.length > 3 && guard++ < n * n + 64) {
    let clipped = false;
    for (let k = 0; k < idx.length; k++) {
      const i0 = idx[(k + idx.length - 1) % idx.length];
      const i1 = idx[k];
      const i2 = idx[(k + 1) % idx.length];
      const ax = p[i0 * 2], ay = p[i0 * 2 + 1];
      const bx = p[i1 * 2], by = p[i1 * 2 + 1];
      const cx = p[i2 * 2], cy = p[i2 * 2 + 1];
      if (cross2(ax, ay, bx, by, cx, cy) <= 0) continue; // reflex
      let ok = true;
      for (const m of idx) {
        if (m === i0 || m === i1 || m === i2) continue;
        const px = p[m * 2], py = p[m * 2 + 1];
        if (cross2(ax, ay, bx, by, px, py) >= 0 &&
            cross2(bx, by, cx, cy, px, py) >= 0 &&
            cross2(cx, cy, ax, ay, px, py) >= 0) { ok = false; break; }
      }
      if (!ok) continue;
      tris.push(i0, i1, i2);
      idx.splice(k, 1);
      clipped = true;
      break;
    }
    if (!clipped) break;
  }
  if (idx.length === 3) tris.push(idx[0], idx[1], idx[2]);
  return tris;
}

// ------------------------------------------------------------------ extrude

export function buildShellFragment(
  poly: number[], thickness: number, touchedBorder: boolean,
): ShellFragment | null {
  const n = poly.length / 2;
  if (n < 3) return null;
  const area = polyArea(poly);
  if (area <= 0) return null;

  // centroid
  let cx = 0, cy = 0;
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    const c = poly[i * 2] * poly[j * 2 + 1] - poly[j * 2] * poly[i * 2 + 1];
    cx += (poly[i * 2] + poly[j * 2]) * c;
    cy += (poly[i * 2 + 1] + poly[j * 2 + 1]) * c;
  }
  cx /= 6 * area; cy /= 6 * area;

  const local = new Float64Array(n * 2);
  for (let i = 0; i < n; i++) { local[i * 2] = poly[i * 2] - cx; local[i * 2 + 1] = poly[i * 2 + 1] - cy; }

  const tris = earClip(Array.from(local));
  if (!tris.length) return null;

  const hz = thickness * 0.5;
  const pos: number[] = [], nrm: number[] = [], attr: number[] = [];
  const push = (x: number, y: number, z: number, nx2: number, ny2: number, nz: number, fresh: number, j: number) => {
    pos.push(x, y, z); nrm.push(nx2, ny2, nz); attr.push(fresh, j, 0);
  };
  // front / back
  for (let k = 0; k < tris.length; k += 3) {
    const [a, b, c] = [tris[k], tris[k + 1], tris[k + 2]];
    push(local[a * 2], local[a * 2 + 1], hz, 0, 0, 1, 0, 0);
    push(local[b * 2], local[b * 2 + 1], hz, 0, 0, 1, 0, 0);
    push(local[c * 2], local[c * 2 + 1], hz, 0, 0, 1, 0, 0);
    push(local[c * 2], local[c * 2 + 1], -hz, 0, 0, -1, 0, 0);
    push(local[b * 2], local[b * 2 + 1], -hz, 0, 0, -1, 0, 0);
    push(local[a * 2], local[a * 2 + 1], -hz, 0, 0, -1, 0, 0);
  }
  // sides = the fracture surface (mirror / mist / hackle band on glass)
  for (let i = 0; i < n; i++) {
    const j = (i + 1) % n;
    const ax = local[i * 2], ay = local[i * 2 + 1];
    const bx = local[j * 2], by = local[j * 2 + 1];
    let ex = bx - ax, ey = by - ay;
    const l = Math.hypot(ex, ey) || 1e-9;
    const nx2 = ey / l, ny2 = -ex / l;
    void ex; void ey;
    push(ax, ay, -hz, nx2, ny2, 0, 1, 0);
    push(bx, by, -hz, nx2, ny2, 0, 1, 0);
    push(bx, by, hz, nx2, ny2, 0, 1, 0);
    push(ax, ay, -hz, nx2, ny2, 0, 1, 0);
    push(bx, by, hz, nx2, ny2, 0, 1, 0);
    push(ax, ay, hz, nx2, ny2, 0, 1, 0);
  }

  const contacts: V3[] = [];
  let radius = 0;
  for (let i = 0; i < n; i++) {
    const x = local[i * 2], y = local[i * 2 + 1];
    contacts.push(v3(x, y, hz), v3(x, y, -hz));
    radius = Math.max(radius, Math.hypot(x, y, hz));
  }

  const mesh: MeshData = {
    pos: new Float32Array(pos),
    nrm: new Float32Array(nrm),
    attr: new Float32Array(attr),
    count: pos.length / 3,
  };
  return { poly, cx, cy, area, mesh, contacts, radius, touchedBorder, cells: 0 };
}
