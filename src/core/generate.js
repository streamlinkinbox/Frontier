// Mesh assembly: sweeps each arm as a quad tube (flat road + tiered walls +
// arched ceiling) and stitches junction chambers (X / Y / T) so every piece
// shares vertices -> ONE mesh, ONE topology, all quads.
//
// Winding convention: face normals point INTO the tunnel void
// (road/floor +Y, ceiling -Y, walls toward the passage centre).

import { norm, v3 } from './vec.js';
import { analyzeNetwork } from './network.js';
import { buildStationProfile, buildWallColumn, defaultProfileOpts } from './profile.js';

export class MeshBuilder {
  constructor() {
    this.positions = [];
    this.colors = [];
    this.quads = [];
  }

  vertex(p, c) {
    this.positions.push(p.x, p.y, p.z);
    this.colors.push(c[0], c[1], c[2]);
    return this.positions.length / 3 - 1;
  }

  pos(id) {
    return v3(this.positions[id * 3], this.positions[id * 3 + 1], this.positions[id * 3 + 2]);
  }

  quad(a, b, c, d, tag = 'wall') {
    this.quads.push({ v: [a, b, c, d], tag });
  }
}

const reverse = (arr) => [...arr].reverse();

// --- Coons patch (exact boundary, smooth interior) ---------------------------

function coonsPoint(edges, u, v) {
  const A = edges.bottom.length;
  const at = (pts, t) => {
    const x = Math.min(Math.max(t, 0), 1) * (A - 1);
    const i = Math.min(Math.floor(x), A - 2);
    const f = x - i;
    const a = pts[i];
    const b = pts[i + 1];
    return {
      x: a.x + (b.x - a.x) * f,
      y: a.y + (b.y - a.y) * f,
      z: a.z + (b.z - a.z) * f,
    };
  };
  const bu = at(edges.bottom, u);
  const tu = at(edges.top, u);
  const lv = at(edges.left, v);
  const rv = at(edges.right, v);
  const p00 = edges.bottom[0];
  const p10 = edges.bottom[A - 1];
  const p01 = edges.top[0];
  const p11 = edges.top[A - 1];
  return {
    x: (1 - v) * bu.x + v * tu.x + (1 - u) * lv.x + u * rv.x -
      ((1 - u) * (1 - v) * p00.x + u * (1 - v) * p10.x + (1 - u) * v * p01.x + u * v * p11.x),
    y: (1 - v) * bu.y + v * tu.y + (1 - u) * lv.y + u * rv.y -
      ((1 - u) * (1 - v) * p00.y + u * (1 - v) * p10.y + (1 - u) * v * p01.y + u * v * p11.y),
    z: (1 - v) * bu.z + v * tu.z + (1 - u) * lv.z + u * rv.z -
      ((1 - u) * (1 - v) * p00.z + u * (1 - v) * p10.z + (1 - u) * v * p01.z + u * v * p11.z),
  };
}

/**
 * Square grid patch (A x A verts) with 4 given boundary sides.
 * sides: [s0: K0->K1, s1: K1->K2, s2: K2->K3, s3: K3->K0], each A points.
 * sideIds: per side, an array of A existing vertex ids, or null to create new.
 * Returns the id grid g[i][j]: g[i][0]=s0[i], g[i][A-1]=s2[A-1-i],
 * g[0][j]=s3[A-1-j], g[A-1][j]=s1[j].
 */
function buildGridPatch(b, sides, sideIds, colorFn, domeH = 0, tag = 'floor', flipQuad = false) {
  const A = sides[0].length;
  const edges = {
    top: reverse(sides[2]),   // P(u,1): K3 -> K2
    right: sides[1],          // P(1,v): K1 -> K2
    bottom: sides[0],         // P(u,0): K0 -> K1
    left: reverse(sides[3]),  // P(0,v): K0 -> K3
  };
  const g = Array.from({ length: A }, () => new Array(A));

  const pick = (arr, i) => {
    if (!arr) return null;
    const id = arr[i];
    return id === null || id === undefined ? null : id;
  };
  const boundaryId = (i, j) => {
    if (j === 0) return pick(sideIds[0], i);
    if (j === A - 1) return pick(sideIds[2], A - 1 - i);
    if (i === 0) return pick(sideIds[3], A - 1 - j);
    if (i === A - 1) return pick(sideIds[1], j);
    return null;
  };

  for (let j = 0; j < A; j++) {
    for (let i = 0; i < A; i++) {
      const existing = boundaryId(i, j);
      if (existing !== null) {
        g[i][j] = existing;
        continue;
      }
      const u = i / (A - 1);
      const v = j / (A - 1);
      let p;
      if (j === 0) p = sides[0][i];
      else if (j === A - 1) p = sides[2][A - 1 - i];
      else if (i === 0) p = sides[3][A - 1 - j];
      else if (i === A - 1) p = sides[1][j];
      else p = coonsPoint(edges, u, v);
      if (domeH !== 0 && i > 0 && i < A - 1 && j > 0 && j < A - 1) {
        p = v3(p.x, p.y + domeH * Math.sin(Math.PI * u) * Math.sin(Math.PI * v), p.z);
      }
      g[i][j] = b.vertex(p, colorFn(i, j, p));
    }
  }

  for (let i = 0; i < A - 1; i++) {
    for (let j = 0; j < A - 1; j++) {
      if (flipQuad) b.quad(g[i][j], g[i + 1][j], g[i + 1][j + 1], g[i][j + 1], tag);
      else b.quad(g[i][j], g[i][j + 1], g[i + 1][j + 1], g[i + 1][j], tag);
    }
  }
  return g;
}

/** Side id list of a grid patch in K_j -> K_{j+1} order. */
function sideIdsOfGrid(g, j, A) {
  if (j === 0) return Array.from({ length: A }, (_, i) => g[i][0]);
  if (j === 1) return Array.from({ length: A }, (_, i) => g[A - 1][i]);
  if (j === 2) return Array.from({ length: A }, (_, i) => g[A - 1 - i][A - 1]);
  return Array.from({ length: A }, (_, i) => g[0][A - 1 - i]);
}

/** Strip between two A-point id rows. `up` = true -> faces +Y (floor),
 * false -> faces -Y (ceiling). Rows run in matching directions. */
function buildStrip(b, outer, inner, tag, up) {
  const A = outer.length;
  for (let i = 0; i < A - 1; i++) {
    if (up) b.quad(outer[i], inner[i], inner[i + 1], outer[i + 1], tag);
    else b.quad(outer[i], outer[i + 1], inner[i + 1], inner[i], tag);
  }
}

function angDiff(a, b) {
  let d = a - b;
  while (d > Math.PI) d -= 2 * Math.PI;
  while (d < -Math.PI) d += 2 * Math.PI;
  return d;
}

// --- junction chamber --------------------------------------------------------
//
// Floor  : centre grid patch + one strip per mouth (shared road seam at mouth)
// Walls  : walk of columns around the chamber; mouth columns reuse the arm's
//          wall seams, corner/back columns are new (shared base with the floor
//          patch, shared top with the ceiling patch)
// Ceiling: centre grid patch + one strip per mouth (shared arc seam at mouth)

function buildJunction(b, jn, armById, profOpts) {
  const A = profOpts.roadPts;
  const k = jn.arms.length;
  if (k < 2 || k > 4) return { built: false, reason: `unsupported k=${k}` };

  // --- mouth records. Chamber walk order (CCW in the XZ plane) visits the
  // K_j-side column first: [K_j, colA, colB(gap), K_{j+1}].
  const mouths = jn.arms.map((m) => {
    const arm = armById.get(m.armId);
    const ring = m.atStart ? arm.rings[0] : arm.rings[arm.rings.length - 1];
    const s = ring.seams;
    const flip = ring.tan.x * m.dir.x + ring.tan.z * m.dir.z < 0;
    return {
      m,
      ring,
      flip,
      colA: flip ? s.lwallBottomUp : s.rwallBottomUp, // K_j side (Rm when !flip)
      colB: flip ? s.rwallBottomUp : s.lwallBottomUp,
      topA: flip ? s.LTop : s.RTop,
      topB: flip ? s.RTop : s.LTop,
      roadWalk: flip ? s.roadIds : reverse(s.roadIds), // colA point -> colB point
      arcIds: s.arcIds, // RTop -> LTop
    };
  });

  // --- square rotation + side assignment (minimise angular mismatch)
  const angles = mouths.map((mo) => Math.atan2(mo.m.dir.z, mo.m.dir.x));
  let best = null;
  for (let mi = 0; mi < k; mi++) {
    for (let t = 0; t < 4; t++) {
      const theta = angles[mi] - (t * Math.PI) / 2;
      const sideAngles = [0, 1, 2, 3].map((j) => theta + (j * Math.PI) / 2);
      const assign = new Array(k).fill(-1);
      const used = new Array(4).fill(false);
      const order = mouths
        .map((_, i) => i)
        .sort((a, z) =>
          Math.min(...sideAngles.map((sa) => Math.abs(angDiff(angles[a], sa)))) -
          Math.min(...sideAngles.map((sa) => Math.abs(angDiff(angles[z], sa))))
        );
      let collisions = 0;
      let cost = 0;
      for (const i of order) {
        let bj = -1;
        let bc = Infinity;
        for (let j = 0; j < 4; j++) {
          const c = Math.abs(angDiff(angles[i], sideAngles[j]));
          if (!used[j] && c < bc) {
            bc = c;
            bj = j;
          }
        }
        if (bj < 0) collisions += 10;
        else {
          used[bj] = true;
          assign[i] = bj;
          cost += bc;
        }
      }
      const score = collisions * 100 + cost;
      if (!best || score < best.score) best = { score, theta, assign };
    }
  }
  const { theta, assign } = best;

  // --- square geometry
  const minD = Math.min(...mouths.map((mo) => mo.m.mouthDist));
  const s = Math.min(Math.max(profOpts.halfW * 1.15, minD * 0.5), minD * 0.85);
  const C = v3(jn.x, jn.y, jn.z);
  const nrm = (j) => {
    const phi = theta + (j * Math.PI) / 2;
    return { x: Math.cos(phi), z: Math.sin(phi) };
  };
  const tanv = (j) => {
    const n = nrm(j);
    return { x: -n.z, z: n.x };
  };
  const K = [0, 1, 2, 3].map((j) => {
    const n = nrm(j);
    const t = tanv(j);
    return v3(C.x + s * (n.x - t.x), C.y, C.z + s * (n.z - t.z));
  });

  const sidePts = [0, 1, 2, 3].map((j) => {
    const a = K[j];
    const z = K[(j + 1) % 4];
    return Array.from({ length: A }, (_, i) =>
      v3(a.x + (z.x - a.x) * (i / (A - 1)), C.y, a.z + (z.z - a.z) * (i / (A - 1)))
    );
  });

  const mouthOfSide = [-1, -1, -1, -1];
  mouths.forEach((mo, i) => {
    mouthOfSide[assign[i]] = i;
  });

  // --- floor centre patch (owns its vertices; shared as wall bases)
  const floorGrid = buildGridPatch(
    b, sidePts, [null, null, null, null],
    () => [0.17, 0.18, 0.2], 0, 'floor', false
  );

  // --- floor strips: mouth road seam -> floor patch side
  for (let j = 0; j < 4; j++) {
    const mi = mouthOfSide[j];
    if (mi < 0) continue;
    const mo = mouths[mi];
    const outer = mo.roadWalk.map((id) => mo.ring.ids[id]);
    buildStrip(b, outer, sideIdsOfGrid(floorGrid, j, A), 'floor', true);
  }

  // --- wall columns
  const walk = [];
  const cornerCols = new Array(4).fill(null);
  const backCols = {}; // `s${j}-${i}` -> {ids, topId}

  const makeCol = (baseId, basePos, insetDir) => {
    const col = buildWallColumn(basePos, insetDir, profOpts);
    const ids = col.points.map((p, i) => (i === 0 ? baseId : b.vertex(p, col.colors[i])));
    return { ids, topId: ids[ids.length - 1] };
  };

  const insetAt = (p) => norm(v3(C.x - p.x, 0, C.z - p.z));

  for (let j = 0; j < 4; j++) {
    const mi = mouthOfSide[j];
    if (mi >= 0) {
      const mo = mouths[mi];
      walk.push({
        ids: mo.colA.map((id) => mo.ring.ids[id]),
        topId: mo.ring.ids[mo.topA],
        gapWithPrev: false,
      });
      walk.push({
        ids: mo.colB.map((id) => mo.ring.ids[id]),
        topId: mo.ring.ids[mo.topB],
        gapWithPrev: true, // mouth opening
      });
    } else {
      for (let i = 1; i < A - 1; i++) {
        const baseId = floorGrid[i][0] ? sideIdsOfGrid(floorGrid, j, A)[i] : 0;
        const p = sidePts[j][i];
        const col = makeCol(baseId, p, insetAt(p));
        backCols[`s${j}-${i}`] = col;
        walk.push({ ids: col.ids, topId: col.topId, gapWithPrev: false });
      }
    }
    // corner column at K[j+1], base shared with the floor patch corner
    const cj = (j + 1) % 4;
    const baseId = sideIdsOfGrid(floorGrid, cj, A)[0]; // K_{cj} == start of side cj
    const col = makeCol(baseId, K[cj], insetAt(K[cj]));
    cornerCols[cj] = col;
    walk.push({ ids: col.ids, topId: col.topId, gapWithPrev: false });
  }

  // wall quads (skip the mouth gaps)
  const W = profOpts.tiers * 2 + 1;
  for (let i = 0; i < walk.length; i++) {
    const nxt = walk[(i + 1) % walk.length];
    if (nxt.gapWithPrev) continue;
    const c0 = walk[i].ids;
    const c1 = nxt.ids;
    for (let r = 0; r < W - 1; r++) {
      b.quad(c0[r], c1[r], c1[r + 1], c0[r + 1], 'jwall');
    }
  }

  // --- ceiling centre patch (shares wall tops at corners + back sides)
  const Ktop = cornerCols.map((col) => b.pos(col.topId));
  const sideTopPts = [0, 1, 2, 3].map((j) => {
    const a = Ktop[j];
    const z = Ktop[(j + 1) % 4];
    if (mouthOfSide[j] < 0) {
      const pts = [a];
      for (let i = 1; i < A - 1; i++) pts.push(b.pos(backCols[`s${j}-${i}`].topId));
      pts.push(z);
      return pts;
    }
    return Array.from({ length: A }, (_, i) =>
      v3(a.x + (z.x - a.x) * (i / (A - 1)), a.y + (z.y - a.y) * (i / (A - 1)), a.z + (z.z - a.z) * (i / (A - 1)))
    );
  });
  const sideTopIds = [0, 1, 2, 3].map((j) => {
    const start = cornerCols[j].topId;
    const end = cornerCols[(j + 1) % 4].topId;
    if (mouthOfSide[j] >= 0) {
      // occupied side: corners shared, interiors created by the patch
      return [start, ...new Array(A - 2).fill(null), end];
    }
    const ids = [start];
    for (let i = 1; i < A - 1; i++) ids.push(backCols[`s${j}-${i}`].topId);
    ids.push(end);
    return ids;
  });

  const ceilGrid = buildGridPatch(
    b, sideTopPts, sideTopIds,
    () => [0.46, 0.42, 0.38],
    profOpts.archRise * 0.22, 'ceil', true
  );

  // --- ceiling strips: mouth arc seam -> ceiling patch side
  for (let j = 0; j < 4; j++) {
    const mi = mouthOfSide[j];
    if (mi < 0) continue;
    const mo = mouths[mi];
    const outer = mo.arcIds.map((id) => mo.ring.ids[id]); // RTop -> LTop
    const sideIds = sideIdsOfGrid(ceilGrid, j, A); // K_top_j -> K_top_{j+1}
    // colA's top (RTop when !flip) pairs with K_top_j; arcIds start at RTop
    const inner = mo.flip ? reverse(sideIds) : sideIds;
    buildStrip(b, outer, inner, 'ceil', false);
  }

  return { built: true, k, theta, assign };
}

// --- main entry --------------------------------------------------------------

export function buildNetworkMesh(splines, params = {}) {
  const profOpts = { ...defaultProfileOpts, ...params };
  const netParams = {
    stationSpacing: params.stationSpacing ?? 3,
    junctionDist: params.junctionDist ?? 11,
    detectRadius: params.detectRadius ?? 5.5,
    heightTolerance: params.heightTolerance ?? 6.5,
    minArmLen: params.minArmLen ?? 26,
    junctionMergeDist: params.junctionMergeDist ?? 9,
  };

  const { arms, junctions, overpasses } = analyzeNetwork(splines, netParams);
  const b = new MeshBuilder();
  const A = profOpts.roadPts;
  const W = 2 * profOpts.tiers + 1;
  const P = 2 * A + 2 * W - 4;

  const armById = new Map();

  // --- arm tubes
  for (const arm of arms) {
    const rings = [];
    for (const st of arm.stations) {
      const prof = buildStationProfile(st.pos, st.tan, profOpts);
      const ids = prof.points.map((p, i) => b.vertex(p, prof.colors[i]));
      rings.push({ ids, seams: prof.seams, prof, tan: st.tan, pos: st.pos });
    }
    for (let i = 0; i < rings.length - 1; i++) {
      const r0 = rings[i].ids;
      const r1 = rings[i + 1].ids;
      for (let j = 0; j < P; j++) {
        const j1 = (j + 1) % P;
        let tag = 'wall';
        if (j < A - 1) tag = 'road';
        else if (j >= A + W - 2 && j < 2 * A + W - 3) tag = 'ceil';
        b.quad(r0[j], r1[j], r1[j1], r0[j1], tag);
      }
    }
    armById.set(arm.id, { arm, rings });
  }

  // --- junction chambers
  const jStats = [];
  for (const jn of junctions) {
    const r = buildJunction(b, jn, armById, profOpts);
    jStats.push({ id: jn.id, ...r, k: jn.arms.length });
  }

  return {
    positions: new Float32Array(b.positions),
    colors: new Float32Array(b.colors),
    quads: b.quads.map((q) => q.v),
    quadTags: b.quads.map((q) => q.tag),
    stats: {
      vertices: b.positions.length / 3,
      quads: b.quads.length,
      arms: arms.length,
      junctions: jStats.filter((s) => s.built).length,
      junctionsTotal: junctions.length,
      overpasses: overpasses.length,
      jStats,
    },
    debug: { arms, junctions, overpasses },
  };
}
