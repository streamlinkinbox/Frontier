/**
 * tileKit.ts — the individual tile solids.
 *
 * The East-Asian tile roof (本瓦葺 hongawara-gae) is a two-layer system
 * (see docs/roof-research.md §6):
 *
 *   板瓦 bǎnwǎ / 平瓦   — the concave pan tile: the water channel, laid on the
 *                         sheathing edge to edge. Its raised edges are what the
 *                         barrel tiles stand on.
 *   筒瓦 tǒngwǎ / 丸瓦  — the half-round barrel tile: caps the seam between two
 *                         pan tiles.
 *   瓦當 wǎdāng / 軒丸瓦 — the round eave disc closing each barrel row.
 *   滴水 dīshuǐ / 軒平瓦 — the pan tile at the eave with a hanging apron that
 *                         throws water clear of the timber.
 *
 * Tiles taper from the tail (down-slope, wide) to the head (up-slope, narrow):
 * the narrow head slides under the course above. That is what makes the
 * 壓六露四 overlap nest correctly instead of butt-jointing.
 *
 * Every tile is a SOLID (outer + inner surface + closed edges) so it reads
 * correctly from below and never shows a paper-thin back face.
 *
 * Local frame for every tile: X across the slope, Z along the slope (tail/low
 * end at -L/2, head/high end at +L/2), Y up with the arc's crown at y = 0.
 */

import { BufferGeometry, Float32BufferAttribute } from 'three';

export interface TileShape {
  /** Chord width across the tile. */
  width: number;
  /** Length along the slope. */
  length: number;
  /** Depth of the cross-section arc. */
  sagitta: number;
  thickness: number;
  /** true = barrel (convex, a hill) · false = pan (concave, a valley). */
  convex: boolean;
  /** Width reduction at the up-slope head, 0..0.25. */
  taper: number;
  /** Longitudinal arch (a tile is never perfectly straight). */
  bow: number;
  arcSegments: number;
  lenSegments: number;
}

export function tileShape(over: Partial<TileShape> = {}): TileShape {
  return {
    width: 0.2,
    length: 0.22,
    sagitta: 0.032,
    thickness: 0.018,
    convex: false,
    taper: 0.12,
    bow: 0.01,
    arcSegments: 7,
    lenSegments: 3,
    ...over,
  };
}

/**
 * A tile body: a cross-section arc swept along the slope, tapered and bowed,
 * given thickness, with both ends and both side edges closed.
 *
 * The arc's centre sits at (0, sign·R) in the local XZ-section, so the inner
 * surface is the outer one scaled radially about that centre — constant
 * thickness all the way across, including at the crown.
 */
export function makeTileSolid(shape: TileShape): BufferGeometry {
  const { width, length, sagitta, thickness, convex, taper, bow } = shape;
  const nu = Math.max(3, Math.round(shape.arcSegments));
  const nv = Math.max(2, Math.round(shape.lenSegments));

  const half = Math.max(1e-4, width / 2);
  const s = Math.max(1e-4, sagitta);
  const R = (half * half + s * s) / (2 * s);
  const phi = Math.asin(Math.min(0.999, half / R));
  const sign = convex ? -1 : 1;
  const innerK = Math.max(0.02, (R - Math.max(1e-4, thickness)) / R);

  const outer: number[][] = [];
  const inner: number[][] = [];
  for (let j = 0; j <= nv; j++) {
    const v = j / nv;
    const z = (v - 0.5) * length;
    const w = 1 - taper * v;
    const arch = sign * bow * Math.sin(Math.PI * v);
    const ro: number[] = [];
    const ri: number[] = [];
    for (let i = 0; i <= nu; i++) {
      const a = -phi + (i / nu) * 2 * phi;
      const x = R * Math.sin(a) * w;
      const y = sign * R * (1 - Math.cos(a)) * w + arch;
      ro.push(x, y, z);
      ri.push(x * innerK, (y - sign * R) * innerK + sign * R, z);
    }
    outer.push(ro);
    inner.push(ri);
  }

  const pos: number[] = [];
  const V = (arr: number[][], i: number, j: number): [number, number, number] => [
    arr[j][i * 3],
    arr[j][i * 3 + 1],
    arr[j][i * 3 + 2],
  ];
  const tri = (
    a: [number, number, number],
    b: [number, number, number],
    c: [number, number, number],
  ) => pos.push(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);

  // outer surface
  for (let j = 0; j < nv; j++) {
    for (let i = 0; i < nu; i++) {
      tri(V(outer, i, j), V(outer, i + 1, j), V(outer, i + 1, j + 1));
      tri(V(outer, i, j), V(outer, i + 1, j + 1), V(outer, i, j + 1));
    }
  }
  // inner surface (reversed)
  for (let j = 0; j < nv; j++) {
    for (let i = 0; i < nu; i++) {
      tri(V(inner, i, j), V(inner, i + 1, j + 1), V(inner, i + 1, j));
      tri(V(inner, i, j), V(inner, i, j + 1), V(inner, i + 1, j + 1));
    }
  }
  // side edges
  for (let j = 0; j < nv; j++) {
    tri(V(outer, 0, j), V(inner, 0, j + 1), V(outer, 0, j + 1));
    tri(V(outer, 0, j), V(inner, 0, j), V(inner, 0, j + 1));
    tri(V(outer, nu, j), V(outer, nu, j + 1), V(inner, nu, j + 1));
    tri(V(outer, nu, j), V(inner, nu, j + 1), V(inner, nu, j));
  }
  // end walls
  for (let i = 0; i < nu; i++) {
    tri(V(outer, i, 0), V(outer, i + 1, 0), V(inner, i + 1, 0));
    tri(V(outer, i, 0), V(inner, i + 1, 0), V(inner, i, 0));
    tri(V(outer, i, nv), V(inner, i + 1, nv), V(outer, i + 1, nv));
    tri(V(outer, i, nv), V(inner, i, nv), V(inner, i + 1, nv));
  }

  return finish(pos);
}

/** 瓦當 wǎdāng / 軒丸瓦 — barrel tile closed by a moulded disc at the eave. */
export function makeEaveRound(shape: TileShape): BufferGeometry {
  const parts: BufferGeometry[] = [
    makeTileSolid({ ...shape, convex: true, taper: 0, bow: 0 }),
  ];
  const R = shape.width / 2;
  const discR = R * 1.1;
  const discT = shape.thickness * 1.8;
  const cy = -R - shape.sagitta * 0.2;

  const disc = cylinder(discR, discT, 18);
  disc.rotateZ(Math.PI / 2);
  disc.translate(0, cy, -shape.length / 2 - discT / 2);
  parts.push(disc);

  const ring = cylinder(discR * 0.6, discT * 0.75, 14);
  ring.rotateZ(Math.PI / 2);
  ring.translate(0, cy, -shape.length / 2 - discT * 1.15);
  parts.push(ring);

  return mergeGeoms(parts);
}

/**
 * 滴水 dīshuǐ / 軒平瓦 — pan tile with a hanging apron at the eave end, its
 * lower edge scalloped, angled back under the tile.
 */
export function makeDripTile(shape: TileShape): BufferGeometry {
  const parts: BufferGeometry[] = [makeTileSolid({ ...shape, convex: false })];
  const w = shape.width * 1.04;
  const apronH = shape.width * 0.46;
  const th = shape.thickness * 0.8;
  const n = 10;
  const zTop = -shape.length / 2;
  const yTop = shape.sagitta * 0.15;
  const zBot = zTop - apronH * 0.5;
  const yBot = -apronH;

  const pos: number[] = [];
  const tri = (
    a: [number, number, number],
    b: [number, number, number],
    c: [number, number, number],
  ) => pos.push(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
  const col = (i: number) => -w / 2 + (i / n) * w;
  const dip = (i: number) => (i % 2 === 0 ? 0 : -apronH * 0.3);
  const front = (i: number, bot: boolean): [number, number, number] => [
    col(i),
    bot ? yBot + dip(i) : yTop,
    bot ? zBot : zTop,
  ];
  const back = (p: [number, number, number]): [number, number, number] => [
    p[0],
    p[1] - th * 0.4,
    p[2] - th,
  ];
  for (let i = 0; i < n; i++) {
    const a = front(i, false);
    const b = front(i + 1, false);
    const c = front(i + 1, true);
    const d = front(i, true);
    tri(a, b, c);
    tri(a, c, d);
    const A = back(a);
    const B = back(b);
    const C = back(c);
    const D = back(d);
    tri(A, C, B);
    tri(A, D, C);
  }
  parts.push(finish(pos));
  return mergeGeoms(parts);
}

/** 脊瓦 — a ridge / hip cap: a wide barrel with almost no taper (they butt up). */
export function makeRidgeCap(shape: TileShape): BufferGeometry {
  return makeTileSolid({ ...shape, convex: true, taper: 0.03, bow: 0 });
}

/**
 * 鬼瓦 onigawara / 鴟吻 chiwen — the moulded ornament closing a ridge end,
 * built from stacked plates so it reads as a block from any angle.
 */
export function makeOgreTile(width: number, height: number): BufferGeometry {
  const t = width * 0.24;
  const parts: BufferGeometry[] = [];
  const add = (w: number, h: number, d: number, x: number, y: number, z = 0) => {
    const g = box(w, h, d);
    g.translate(x, y, z);
    parts.push(g);
  };
  add(width, height * 0.6, t, 0, height * 0.3);
  add(width * 0.56, height * 0.52, t * 1.2, 0, height * 0.74);
  add(width * 0.15, height * 0.36, t * 0.65, -width * 0.3, height * 1.0);
  add(width * 0.15, height * 0.36, t * 0.65, width * 0.3, height * 1.0);
  add(width * 1.08, height * 0.12, t * 1.45, 0, height * 0.06);
  add(width * 0.34, height * 0.22, t * 0.8, 0, height * 1.16);
  return mergeGeoms(parts);
}

/** 寶頂 finial for a pyramidal roof: a post with stacked rings and a jewel. */
export function makeFinial(width: number, height: number): BufferGeometry {
  const parts: BufferGeometry[] = [];
  const add = (g: BufferGeometry, y: number) => {
    g.translate(0, y, 0);
    parts.push(g);
  };
  add(cylinder(width * 0.44, height * 0.1, 18), height * 0.05);
  add(cylinder(width * 0.18, height * 0.5, 12), height * 0.25);
  for (let i = 0; i < 4; i++) {
    add(cylinder(width * (0.33 - i * 0.055), height * 0.055, 16), height * (0.36 + i * 0.1));
  }
  add(sphere(width * 0.19, 16, 10), height * 0.9);
  add(cylinder(width * 0.045, height * 0.24, 8), height * 1.02);
  return mergeGeoms(parts);
}

/* ------------------------------------------------------------------ helpers */

function finish(pos: number[]): BufferGeometry {
  const g = new BufferGeometry();
  g.setAttribute('position', new Float32BufferAttribute(pos, 3));
  g.computeVertexNormals();
  return g;
}

/** Cylinder with its axis along +Y. */
export function cylinder(radius: number, height: number, seg: number): BufferGeometry {
  const pos: number[] = [];
  const push = (x: number, y: number, z: number) => pos.push(x, y, z);
  const half = height / 2;
  for (let i = 0; i < seg; i++) {
    const a0 = (i / seg) * Math.PI * 2;
    const a1 = ((i + 1) / seg) * Math.PI * 2;
    const x0 = Math.cos(a0) * radius;
    const z0 = Math.sin(a0) * radius;
    const x1 = Math.cos(a1) * radius;
    const z1 = Math.sin(a1) * radius;
    push(x0, -half, z0);
    push(x1, -half, z1);
    push(x1, half, z1);
    push(x0, -half, z0);
    push(x1, half, z1);
    push(x0, half, z0);
    push(0, half, 0);
    push(x0, half, z0);
    push(x1, half, z1);
    push(0, -half, 0);
    push(x1, -half, z1);
    push(x0, -half, z0);
  }
  return finish(pos);
}

export function sphere(radius: number, seg: number, rings: number): BufferGeometry {
  const pos: number[] = [];
  const push = (p: [number, number, number]) => pos.push(p[0], p[1], p[2]);
  for (let j = 0; j < rings; j++) {
    const p0 = (j / rings) * Math.PI;
    const p1 = ((j + 1) / rings) * Math.PI;
    for (let i = 0; i < seg; i++) {
      const t0 = (i / seg) * Math.PI * 2;
      const t1 = ((i + 1) / seg) * Math.PI * 2;
      const P = (p: number, t: number): [number, number, number] => [
        radius * Math.sin(p) * Math.cos(t),
        radius * Math.cos(p),
        radius * Math.sin(p) * Math.sin(t),
      ];
      const a = P(p0, t0);
      const b = P(p1, t0);
      const c = P(p1, t1);
      const d = P(p0, t1);
      push(a);
      push(b);
      push(c);
      push(a);
      push(c);
      push(d);
    }
  }
  return finish(pos);
}

export function box(w: number, h: number, d: number): BufferGeometry {
  const x = w / 2;
  const y = h / 2;
  const z = d / 2;
  const v: [number, number, number][] = [
    [-x, -y, -z],
    [x, -y, -z],
    [x, y, -z],
    [-x, y, -z],
    [-x, -y, z],
    [x, -y, z],
    [x, y, z],
    [-x, y, z],
  ];
  const faces = [
    [0, 1, 2, 3],
    [5, 4, 7, 6],
    [4, 0, 3, 7],
    [1, 5, 6, 2],
    [3, 2, 6, 7],
    [4, 5, 1, 0],
  ];
  const pos: number[] = [];
  for (const f of faces) {
    const [a, b, c, d] = f.map((i) => v[i]) as [
      [number, number, number],
      [number, number, number],
      [number, number, number],
      [number, number, number],
    ];
    pos.push(...a, ...b, ...c, ...a, ...c, ...d);
  }
  return finish(pos);
}

/** Merge geometries (positions only; normals recomputed). */
export function mergeGeoms(parts: BufferGeometry[]): BufferGeometry {
  const pos: number[] = [];
  for (const p of parts) {
    const arr = p.getAttribute('position');
    const idx = p.getIndex();
    if (idx) {
      for (let i = 0; i < idx.count; i++) {
        const v = idx.getX(i);
        pos.push(arr.getX(v), arr.getY(v), arr.getZ(v));
      }
    } else {
      for (let i = 0; i < arr.count; i++) pos.push(arr.getX(i), arr.getY(i), arr.getZ(i));
    }
  }
  return finish(pos);
}

/**
 * Rescale a geometry so its bounding box is exactly 1×1×1, centred on X and Y,
 * with the BASE at z = 0. Ridge ornaments are authored with a sensible
 * silhouette and then placed by giving an exact (width, depth, height) box.
 * Also rotates the "+Y up" authoring convention into the ridge convention
 * (+Z up, +Y along the run) used by ridges.ts.
 */
export function unitizeBox(g: BufferGeometry): BufferGeometry {
  g.rotateX(Math.PI / 2);
  g.computeBoundingBox();
  const bb = g.boundingBox!;
  const sx = bb.max.x - bb.min.x || 1;
  const sy = bb.max.y - bb.min.y || 1;
  const sz = bb.max.z - bb.min.z || 1;
  g.translate(-(bb.min.x + bb.max.x) / 2, -(bb.min.y + bb.max.y) / 2, -bb.min.z);
  g.scale(1 / sx, 1 / sy, 1 / sz);
  g.computeVertexNormals();
  return g;
}

/**
 * 正吻 / 鴟吻 / 鬼瓦 — a ridge-end ornament. Built as a stepped stack so it
 * reads as moulded clay from any angle: a plinth, a swelling body, two horns,
 * and a curled tail. Authored centred on the origin, base at y = 0.
 */
export function makeOrnament(width: number, height: number, horns = true): BufferGeometry {
  const t = width * 0.75;
  const parts: BufferGeometry[] = [];
  const add = (
    w: number,
    h: number,
    d: number,
    x: number,
    y: number,
    z: number,
    rotZ = 0,
  ) => {
    const g = box(w, h, d);
    if (rotZ !== 0) g.rotateZ(rotZ);
    g.translate(x, y, z);
    parts.push(g);
  };
  add(width * 1.15, height * 0.14, t * 1.15, 0, height * 0.07, 0);
  add(width * 0.8, height * 0.42, t, 0, height * 0.35, 0);
  add(width * 0.62, height * 0.34, t * 0.85, 0, height * 0.7, t * 0.06);
  if (horns) {
    for (const sx of [-1, 1]) {
      add(width * 0.16, height * 0.34, t * 0.5, sx * width * 0.32, height * 0.98, 0, sx * 0.32);
    }
    add(width * 0.34, height * 0.3, t * 0.6, 0, height * 1.14, -t * 0.18);
  }
  add(width * 0.3, height * 0.16, t * 0.4, 0, height * 1.3, -t * 0.3);
  return mergeGeoms(parts);
}
