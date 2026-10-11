/**
 * Road surface builder.
 *
 * Every corridor becomes one regular quad grid: rows are stations, columns are
 * profile samples. Because the profile sampler emits a constant number of samples
 * in a constant order, bays, tapers, dropped kerbs and median noses reshape the
 * section without disturbing the grid — the topology never changes.
 *
 * Normals are computed per *zone* and vertices are welded only when position and
 * normal agree, which keeps kerb faces genuinely crisp while asphalt and
 * footways stay smooth across zone boundaries that share a slope.
 */

import { clamp, lerp, v2norm, type V2, type V3 } from './math';
import { MeshBuilder, MAT_INDEX, type MeshVertex } from './mesh';
import { profileHeightAt, type CompiledProfile, type ProfileSample } from './profile';
import { edgeAtStation, profileAtCorridorStation, type Corridor, type EndState } from './spline';
import { computeSmoothNormals, mouthVertex, type JunctionArmSolved, type JunctionMeshContext } from './junction';

export interface RowData {
  s: number;
  cs: number[];
  samples: ProfileSample[];
  pos: Array<[number, number, number]>;
  mouth: boolean;
}

export interface CorridorMeshStats {
  rows: number;
  cols: number;
  quads: number;
}

const CARRIAGEWAY_ZONES = new Set([
  'gutter',
  'lane',
  'shoulder',
  'parking',
  'bike',
  'bus',
  'median-flush',
  'median-top',
  'barrier',
  'tram-track',
]);

/** Stations along a corridor between its two mouths, denser near each mouth. */
export function corridorStations(corridor: Corridor, startMouth: number, endMouth: number, quality: string): number[] {
  const a = Math.min(startMouth, endMouth);
  const b = Math.max(startMouth, endMouth);
  const base = quality === 'cinematic' ? 0.6 : quality === 'high' ? 1.1 : quality === 'standard' ? 2 : 3.5;
  const span = Math.max(0.001, b - a);
  const n = Math.max(2, Math.ceil(span / base));
  const out: number[] = [];
  const ramp = [0, 0.7, 1.6, 3, 5, 8, 12, 18];
  for (const r of ramp) if (a + r < b - 1e-3) out.push(a + r);
  for (let i = 1; i < n; i++) out.push(a + (span * i) / n);
  for (let i = ramp.length - 1; i >= 0; i--) if (b - ramp[i] > a + 1e-3) out.push(b - ramp[i]);
  out.push(b);
  const merged = Array.from(new Set(out.map((v) => Math.round(v * 1000) / 1000))).sort((x, y) => x - y);
  return merged.filter((v) => v >= a - 1e-6 && v <= b + 1e-6);
}

/** Build one corridor: carriageway, kerb runs, footways and verges as a single quad grid. */
export function buildCorridorMesh(
  corridor: Corridor,
  cp: CompiledProfile,
  startArm: JunctionArmSolved | null,
  endArm: JunctionArmSolved | null,
  ends: EndState[],
  ctx: JunctionMeshContext,
  cpByEdge: Map<string, CompiledProfile>,
): CorridorMeshStats {
  const quality = ctx.quality as string;
  const startStation = startArm ? startArm.corridorStation((startArm.stationLeft + startArm.stationRight) / 2) : 0;
  const endStation = endArm ? endArm.corridorStation((endArm.stationLeft + endArm.stationRight) / 2) : corridor.length;
  const lo = Math.min(startStation, endStation);
  const hi = Math.max(startStation, endStation);
  if (hi - lo < 0.5) return { rows: 0, cols: 0, quads: 0 };

  const stations = corridorStations(corridor, lo, hi, quality);
  const rows: RowData[] = [];
  const cpForEdge = (edgeId: string) => cpByEdge.get(edgeId) ?? cp;

  const makeRow = (s: number, mouthArm: JunctionArmSolved | null): RowData => {
    if (mouthArm) {
      const out: number[] = [];
      const pos: Array<[number, number, number]> = [];
      const samples: ProfileSample[] = [];
      const cs: number[] = [];
      const midSamples = profileAtCorridorStation(corridor, cpForEdge(mouthArm.arm.edge.id), s, ends, quality);
      for (const smp of midSamples) {
        const outU = mouthArm.flipOffset(smp.u);
        const t = clamp((outU - mouthArm.kerb.left) / Math.max(1e-6, mouthArm.kerb.right - mouthArm.kerb.left), 0, 1);
        const outward = lerp(mouthArm.stationLeft, mouthArm.stationRight, t);
        const mv = mouthVertex(mouthArm, outU, outward, cpByEdge, ctx.quality, ends);
        out.push(outU);
        pos.push([mv.p[0], mv.y, mv.p[1]]);
        samples.push(smp);
        cs.push(mouthArm.corridorStation(outward));
      }
      return { s, cs, samples, pos, mouth: true };
    }
    const samples = profileAtCorridorStation(corridor, cpForEdge(edgeAtStation(corridor, s).edge.id), s, ends, quality);
    const p = corridor.path.positionAt(s);
    const sA = clamp(s - 0.6, 0, corridor.path.length);
    const sB = clamp(s + 0.6, 0, corridor.path.length);
    const pa = corridor.path.positionAt(sA);
    const pb = corridor.path.positionAt(sB);
    const dir = v2norm([pb[0] - pa[0], pb[2] - pa[2]]);
    const right: V2 = [dir[1], -dir[0]];
    const bank = Math.tan(corridor.bankAt(s));
    const elev = corridor.elevationAt(s);
    const pos: Array<[number, number, number]> = samples.map((smp) => [
      p[0] + right[0] * smp.u,
      elev + smp.y + bank * smp.u,
      p[2] + right[1] * smp.u,
    ]);
    return { s, cs: samples.map(() => s), samples, pos, mouth: false };
  };

  if (startArm) rows.push(makeRow(lo, startArm));
  for (const s of stations) {
    if (startArm && Math.abs(s - lo) < 1e-6) continue;
    if (endArm && Math.abs(s - hi) < 1e-6) continue;
    rows.push(makeRow(s, null));
  }
  if (endArm) rows.push(makeRow(hi, endArm));
  if (!rows.length) return { rows: 0, cols: 0, quads: 0 };

  const cols = rows[0].samples.length;
  if (cols < 2) return { rows: rows.length, cols, quads: 0 };

  for (const row of rows) {
    if (row.samples.length !== cols) return { rows: rows.length, cols, quads: 0 };
  }

  const zoneSamples = new Map<number, number[]>();
  for (let c = 0; c < cols; c++) {
    for (const z of rows[0].samples[c].zones) {
      if (!zoneSamples.has(z)) zoneSamples.set(z, []);
      zoneSamples.get(z)!.push(c);
    }
  }

  // ---- normals per (row, col, zone) ---------------------------------------
  const normals = new Float32Array(rows.length * cols * 3);
  for (let r = 0; r < rows.length; r++) {
    const row = rows[r];
    const rPrev = rows[Math.max(0, r - 1)];
    const rNext = rows[Math.min(rows.length - 1, r + 1)];
    for (let c = 0; c < cols; c++) {
      const zone = row.samples[c].zones[0];
      const list = zoneSamples.get(zone) ?? [c];
      const idx = list.indexOf(c);
      const cPrev = idx > 0 ? list[idx - 1] : c;
      const cNext = idx >= 0 && idx < list.length - 1 ? list[idx + 1] : c;
      const du: V3 = [
        row.pos[cNext][0] - row.pos[cPrev][0],
        row.pos[cNext][1] - row.pos[cPrev][1],
        row.pos[cNext][2] - row.pos[cPrev][2],
      ];
      const ds: V3 = [
        rNext.pos[c][0] - rPrev.pos[c][0],
        rNext.pos[c][1] - rPrev.pos[c][1],
        rNext.pos[c][2] - rPrev.pos[c][2],
      ];
      let nx = ds[1] * du[2] - ds[2] * du[1];
      let ny = ds[2] * du[0] - ds[0] * du[2];
      let nz = ds[0] * du[1] - ds[1] * du[0];
      const l = Math.hypot(nx, ny, nz);
      if (l < 1e-9) {
        nx = 0;
        ny = 1;
        nz = 0;
      } else {
        nx /= l;
        ny /= l;
        nz /= l;
      }
      if (ny < 0) {
        nx = -nx;
        ny = -ny;
        nz = -nz;
      }
      const o = (r * cols + c) * 3;
      normals[o] = nx;
      normals[o + 1] = ny;
      normals[o + 2] = nz;
    }
  }

  // ---- vertices (welded unless the normal differs = hard edge) ------------
  const vertexGrid: number[][] = [];
  const keyMap = new Map<string, number>();
  for (let r = 0; r < rows.length; r++) {
    const row = rows[r];
    const rowIdx: number[] = [];
    for (let c = 0; c < cols; c++) {
      const smp = row.samples[c];
      const zone = cp.zones[smp.zones[0]];
      const matId = ctx.matOf(zone?.material ?? 'asphalt');
      const o = (r * cols + c) * 3;
      const n: V3 = [normals[o], normals[o + 1], normals[o + 2]];
      const p = row.pos[c];
      const key = `${Math.round(p[0] * 256)},${Math.round(p[1] * 256)},${Math.round(p[2] * 256)}|${Math.round(n[0] * 20)},${Math.round(n[1] * 20)},${Math.round(n[2] * 20)}`;
      let idx = keyMap.get(key);
      if (idx === undefined) {
        const vertical = zone?.vertical ?? false;
        const v: MeshVertex = {
          p,
          n,
          uv: vertical ? [row.cs[c], p[1]] : [row.cs[c], smp.u],
          uv2: [p[0], p[2]],
          mat: matId,
          tint: [1, 1, 1, 1],
        };
        idx = ctx.builder.addVertex(v);
        keyMap.set(key, idx);
      }
      rowIdx.push(idx);
    }
    vertexGrid.push(rowIdx);
  }

  // ---- quads --------------------------------------------------------------
  let quads = 0;
  for (let r = 0; r + 1 < rows.length; r++) {
    for (let c = 0; c + 1 < cols; c++) {
      const kind = cp.zones[rows[0].samples[c].zones[0]]?.kind ?? 'lane';
      const a = vertexGrid[r][c];
      const b = vertexGrid[r][c + 1];
      const cc = vertexGrid[r + 1][c + 1];
      const d = vertexGrid[r + 1][c];
      ctx.builder.addQuad(a, b, cc, d);
      if (CARRIAGEWAY_ZONES.has(kind)) pushSheet(ctx, a, b, cc, d);
      quads += 1;
    }
  }

  // ---- free ends: cap the cut so nothing is see-through -------------------
  const capEnd = (rowIndex: number, outward: V2) => {
    for (let c = 0; c + 1 < cols; c++) {
      const a = vertexGrid[rowIndex][c];
      const b = vertexGrid[rowIndex][c + 1];
      const va = ctx.builder.verts[a];
      const vb = ctx.builder.verts[b];
      const nn: V3 = [outward[0], 0, outward[1]];
      const a2 = ctx.builder.addVertex({ p: [va.p[0], va.p[1] - 0.05, va.p[2]], n: nn, uv: va.uv, uv2: va.uv2, mat: va.mat, tint: [1, 1, 1, 1] });
      const b2 = ctx.builder.addVertex({ p: [vb.p[0], vb.p[1] - 0.05, vb.p[2]], n: nn, uv: vb.uv, uv2: vb.uv2, mat: vb.mat, tint: [1, 1, 1, 1] });
      ctx.builder.addQuad(a, a2, b2, b);
    }
  };
  if (!startArm) {
    const t = corridor.path.tangentAt(0);
    capEnd(0, [-t[0], -t[2]]);
  }
  if (!endArm) {
    const t = corridor.path.tangentAt(corridor.length);
    capEnd(rows.length - 1, [t[0], t[2]]);
  }

  return { rows: rows.length, cols, quads };
}

function pushSheet(ctx: JunctionMeshContext, a: number, b: number, c: number, d: number): void {
  const va = ctx.builder.verts[a];
  const vb = ctx.builder.verts[b];
  const vc = ctx.builder.verts[c];
  const vd = ctx.builder.verts[d];
  ctx.sheetQuads.push([va.p, vb.p, vc.p, vd.p]);
}

/**
 * Cul-de-sac turning head: a paved apron with a raised centre island so dead ends
 * read as finished carriageway instead of an open cut.
 */
export function buildTurningHead(corridor: Corridor, cp: CompiledProfile, atEnd: 'start' | 'end', ctx: JunctionMeshContext): number {
  const s = atEnd === 'start' ? 0 : corridor.length;
  const sign = atEnd === 'start' ? -1 : 1;
  const p = corridor.path.positionAt(s);
  const t = corridor.path.tangentAt(s);
  const dir: V2 = [t[0] * sign, t[2] * sign];
  const radius = Math.max(4.5, cp.envelope + 1.6);
  const centre: V2 = [p[0] + dir[0] * (radius - 1), p[2] + dir[1] * (radius - 1)];
  const y = corridor.elevationAt(s);
  const mat = ctx.matOf('asphalt');
  const quads0 = ctx.builder.quadCount;
  const segs = 28;
  const rInner = radius * 0.52;
  const quadHalf = rInner * 0.5;
  const axis = dir;
  const side: V2 = [axis[1], -axis[0]];
  const quadCorners: Array<{ p: V2; y: number }> = [
    { p: [centre[0] + axis[0] * quadHalf + side[0] * quadHalf, centre[1] + axis[1] * quadHalf + side[1] * quadHalf], y },
    { p: [centre[0] + axis[0] * quadHalf - side[0] * quadHalf, centre[1] + axis[1] * quadHalf - side[1] * quadHalf], y },
    { p: [centre[0] - axis[0] * quadHalf - side[0] * quadHalf, centre[1] - axis[1] * quadHalf - side[1] * quadHalf], y },
    { p: [centre[0] - axis[0] * quadHalf + side[0] * quadHalf, centre[1] - axis[1] * quadHalf + side[1] * quadHalf], y },
  ];
  const add = (v: V2): number =>
    ctx.builder.addVertex({ p: [v[0], y, v[1]], n: [0, 1, 0], uv: [v[0] * 0.25, v[1] * 0.25], uv2: [v[0], v[1]], mat, tint: [1, 1, 1, 1] });
  const ringIdx: number[] = [];
  const ringInnerIdx: number[] = [];
  for (let i = 0; i <= segs; i++) {
    const a = (i / segs) * Math.PI * 2;
    ringInnerIdx.push(add([centre[0] + Math.cos(a) * rInner, centre[1] + Math.sin(a) * rInner]));
    ringIdx.push(add([centre[0] + Math.cos(a) * radius, centre[1] + Math.sin(a) * radius]));
  }
  for (let i = 0; i < segs; i++) {
    ctx.builder.addQuad(ringInnerIdx[i], ringInnerIdx[i + 1], ringIdx[i + 1], ringIdx[i]);
    pushSheet(ctx, ringInnerIdx[i], ringInnerIdx[i + 1], ringIdx[i + 1], ringIdx[i]);
  }
  // fan from the inner ring into a centre quad (same construction as junctions)
  const quadIdx = quadCorners.map((c) => add(c.p));
  const baseAngle = Math.atan2(quadCorners[0].p[1] - centre[1], quadCorners[0].p[0] - centre[0]);
  const norm = (a: number): number => {
    let v = a - baseAngle;
    while (v < 0) v += Math.PI * 2;
    while (v >= Math.PI * 2) v -= Math.PI * 2;
    return v;
  };
  const quadAng = quadCorners.map((c) => norm(Math.atan2(c.p[1] - centre[1], c.p[0] - centre[0])));
  const innerAng: number[] = [];
  for (let i = 0; i <= segs; i++) innerAng.push(norm((i / segs) * Math.PI * 2));
  for (let sideIdx = 0; sideIdx < 4; sideIdx++) {
    const a0 = quadAng[sideIdx];
    const a1 = sideIdx === 3 ? Math.PI * 2 : quadAng[sideIdx + 1];
    const span = Math.max(1e-4, a1 - a0);
    const cols: number[] = [-1];
    for (let i = 0; i <= segs; i++) if (innerAng[i] >= a0 - 1e-9 && innerAng[i] < a1 - 1e-9) cols.push(i);
    cols.push(-2);
    for (let c = 0; c + 1 < cols.length; c++) {
      const m = cols[c];
      const m2 = cols[c + 1];
      const angOf = (m: number): number => (m === -1 ? a0 : m === -2 ? a1 : innerAng[m]);
      const t0 = clamp((angOf(m) - a0) / span, 0, 1);
      const t1 = clamp((angOf(m2) - a0) / span, 0, 1);
      const qa = quadCorners[sideIdx];
      const qb = quadCorners[(sideIdx + 1) % 4];
      const e0 = add([lerp(qa.p[0], qb.p[0], t0), lerp(qa.p[1], qb.p[1], t0)]);
      const e1 = add([lerp(qa.p[0], qb.p[0], t1), lerp(qa.p[1], qb.p[1], t1)]);
      const i0 = m === -1 ? quadIdx[sideIdx] : ringInnerIdx[m];
      const i1 = m2 === -2 ? quadIdx[(sideIdx + 1) % 4] : ringInnerIdx[m2];
      ctx.builder.addQuad(e0, e1, i1, i0);
    }
  }
  ctx.builder.addQuad(quadIdx[0], quadIdx[1], quadIdx[2], quadIdx[3]);
  computeSmoothNormals(ctx.builder, ctx.builder.vertexCount - (segs + 1) * 2 - 4);
  void rInner;
  return ctx.builder.quadCount - quads0;
}

export { MAT_INDEX, profileHeightAt };
