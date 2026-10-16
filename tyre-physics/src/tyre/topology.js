/**
 * Tyre membrane topology and rest geometry.
 *
 * Pure JavaScript (no DOM, no GPU) so it can be unit-tested under Node.
 *
 * The tyre is a closed, torus-shaped membrane made of NP x NC particles:
 *   - i in [0, NP)  : angle around the wheel axle (psi)
 *   - j in [0, NC)  : position around the closed cross-section profile
 * The profile loop runs from the bead corner over the sidewall, across the tread
 * and back down the other sidewall; the bottom of the loop is a straight "bead seat"
 * that is pinned to the rim. The enclosed volume is the air cavity, which is what the
 * inflation (volume) constraint acts on.
 *
 * Units: metres, kilograms, seconds.
 */

/** Rim and tyre dimensions for a 205/55 R16-class tyre on a 6.5J rim. */
export const TYRE_GEOMETRY = Object.freeze({
  rimRadius: 0.2032, // bead seat radius (16 in rim) [m]
  outerRadius: 0.316, // hub centre to crown [m]
  rimHalfWidth: 0.09, // rim half width used for the rim collision band [m]
});

/** Right-hand half profile as [radius, z] from the bead corner (z > 0) to the crown (z = 0). */
const RIGHT_HALF_PROFILE = Object.freeze([
  [0.2032, 0.075],
  [0.225, 0.088],
  [0.255, 0.099],
  [0.29, 0.098],
  [0.3085, 0.085],
  [0.316, 0.05],
  [0.317, 0.0],
]);

/** Edge classes. Stretch and shear keep the membrane shape, bend resists folding. */
export const EDGE_TYPE = Object.freeze({
  STRETCH_U: 0, // along psi (neighbouring rings)
  STRETCH_V: 1, // along the profile
  SHEAR_D: 2, // quad diagonal (i,j)-(i+1,j+1)
  SHEAR_A: 3, // quad anti-diagonal (i+1,j)-(i,j+1)
  BEND_U: 4, // two rings apart along psi
  BEND_V: 5, // two steps apart along the profile
});

/** Catmull-Rom spline through an open chain of [r, z] control points, densely sampled. */
function sampleCatmullRom(chain, samplesPerSegment) {
  const out = [];
  const n = chain.length;
  const at = (k) => chain[Math.min(n - 1, Math.max(0, k))];
  for (let s = 0; s < n - 1; s++) {
    const p0 = at(s - 1);
    const p1 = at(s);
    const p2 = at(s + 1);
    const p3 = at(s + 2);
    for (let k = 0; k < samplesPerSegment; k++) {
      const t = k / samplesPerSegment;
      const t2 = t * t;
      const t3 = t2 * t;
      const f = (a, b, c, d) =>
        0.5 * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3);
      out.push({ r: f(p0[0], p1[0], p2[0], p3[0]), z: f(p0[1], p1[1], p2[1], p3[1]) });
    }
  }
  return out;
}

/**
 * Closed cross-section loop, resampled to `NC` points with uniform arc length.
 * Index 0 is the right bead corner. Points lying on the bead seat (radius == rimRadius)
 * are flagged as seat points; they are attached to the rim.
 */
export function buildProfileLoop(NC, samplesPerSegment = 24) {
  const rr = TYRE_GEOMETRY.rimRadius;
  const chain = [...RIGHT_HALF_PROFILE, ...RIGHT_HALF_PROFILE.slice(0, -1).reverse().map(([r, z]) => [r, -z])];
  const n = chain.length;
  const bL = chain[n - 1];
  const bR = chain[0];

  const dense = sampleCatmullRom(chain, samplesPerSegment);
  dense.push({ r: bL[0], z: bL[1] }); // left bead corner
  for (let k = 0; k < samplesPerSegment; k++) {
    // straight bead seat from the left corner back to the right corner (exclusive of the right corner)
    const t = k / samplesPerSegment;
    dense.push({ r: bL[0] + (bR[0] - bL[0]) * t, z: bL[1] + (bR[1] - bL[1]) * t });
  }

  // cumulative arc length around the closed loop
  const cum = new Float64Array(dense.length + 1);
  for (let k = 0; k < dense.length; k++) {
    const a = dense[k];
    const b = dense[(k + 1) % dense.length];
    cum[k + 1] = cum[k] + Math.hypot(b.r - a.r, b.z - a.z);
  }
  const total = cum[dense.length];

  const profile = [];
  let m = 0;
  for (let j = 0; j < NC; j++) {
    const s = (j * total) / NC;
    while (m < dense.length - 1 && cum[m + 1] < s) m++;
    const a = dense[m];
    const b = dense[(m + 1) % dense.length];
    const segLen = cum[m + 1] - cum[m];
    const t = segLen > 0 ? (s - cum[m]) / segLen : 0;
    const r = a.r + (b.r - a.r) * t;
    const z = a.z + (b.z - a.z) * t;
    profile.push({ r, z, seat: Math.abs(r - rr) < 1e-7 });
  }
  return { profile, perimeter: total };
}

/**
 * Build the complete topology: particles, triangles, constraint edges (colour-sorted),
 * adjacency and rim attachments. All arrays are typed and ready for upload.
 */
export function buildTyreTopology({ NP, NC, tyreMass = 10.0, samplesPerSegment = 24 } = {}) {
  if (!Number.isInteger(NP) || NP < 6 || NP % 2 !== 0) throw new Error('NP must be an even integer >= 6');
  if (!Number.isInteger(NC) || NC < 8 || NC % 2 !== 0) throw new Error('NC must be an even integer >= 8');

  const N = NP * NC;
  const T = 2 * N;
  const idx = (i, j) => ((((i % NP) + NP) % NP) * NC + (((j % NC) + NC) % NC));

  const { profile, perimeter } = buildProfileLoop(NC, samplesPerSegment);
  const vertexMass = tyreMass / N;

  // ---- particle rest positions (local to the hub centre, axle along +Z) ----
  const pos = new Float32Array(N * 3);
  const x0 = new Float32Array(N * 4);
  const seatFlag = new Uint8Array(N);
  const psiOf = new Float32Array(N);
  for (let i = 0; i < NP; i++) {
    const psi = (2 * Math.PI * i) / NP;
    for (let j = 0; j < NC; j++) {
      const v = idx(i, j);
      const { r, z, seat } = profile[j];
      const x = r * Math.cos(psi);
      const y = r * Math.sin(psi);
      pos[3 * v] = x;
      pos[3 * v + 1] = y;
      pos[3 * v + 2] = z;
      x0[4 * v] = x;
      x0[4 * v + 1] = y;
      x0[4 * v + 2] = z;
      x0[4 * v + 3] = 1 / vertexMass;
      seatFlag[v] = seat ? 1 : 0;
      psiOf[v] = psi;
    }
  }

  // ---- neighbours (12 per vertex, all edge classes) ----
  const vnbr = new Uint32Array(N * 12);
  for (let i = 0; i < NP; i++) {
    for (let j = 0; j < NC; j++) {
      const v = idx(i, j);
      const nb = [
        idx(i + 1, j), idx(i - 1, j),
        idx(i, j + 1), idx(i, j - 1),
        idx(i + 1, j + 1), idx(i - 1, j - 1),
        idx(i + 1, j - 1), idx(i - 1, j + 1),
        idx(i + 2, j), idx(i - 2, j),
        idx(i, j + 2), idx(i, j - 2),
      ];
      for (let k = 0; k < 12; k++) vnbr[12 * v + k] = nb[k];
    }
  }

  // ---- triangles (outward orientation, verified by signed volume) ----
  const tris = new Uint32Array(T * 4);
  let t = 0;
  for (let i = 0; i < NP; i++) {
    for (let j = 0; j < NC; j++) {
      const a = idx(i, j);
      const b = idx(i + 1, j);
      const c = idx(i + 1, j + 1);
      const d = idx(i, j + 1);
      tris.set([a, b, c, 0], 4 * t++);
      tris.set([a, c, d, 0], 4 * t++);
    }
  }
  const signedVolume = (tr) => {
    let vol = 0;
    for (let k = 0; k < T; k++) {
      const a = tr[4 * k], b = tr[4 * k + 1], c = tr[4 * k + 2];
      const ax = pos[3 * a], ay = pos[3 * a + 1], az = pos[3 * a + 2];
      const bx = pos[3 * b], by = pos[3 * b + 1], bz = pos[3 * b + 2];
      const cx = pos[3 * c], cy = pos[3 * c + 1], cz = pos[3 * c + 2];
      vol += (ax * (by * cz - bz * cy) - ay * (bx * cz - bz * cx) + az * (bx * cy - by * cx)) / 6;
    }
    return vol;
  };
  if (signedVolume(tris) < 0) {
    for (let k = 0; k < T; k++) {
      const tmp = tris[4 * k + 1];
      tris[4 * k + 1] = tris[4 * k + 2];
      tris[4 * k + 2] = tmp;
    }
  }
  const restVolume = signedVolume(tris);

  const trisRestArea = new Float32Array(T);
  for (let k = 0; k < T; k++) {
    const a = tris[4 * k], b = tris[4 * k + 1], c = tris[4 * k + 2];
    const u = [pos[3 * b] - pos[3 * a], pos[3 * b + 1] - pos[3 * a + 1], pos[3 * b + 2] - pos[3 * a + 2]];
    const w = [pos[3 * c] - pos[3 * a], pos[3 * c + 1] - pos[3 * a + 1], pos[3 * c + 2] - pos[3 * a + 2]];
    const cx = u[1] * w[2] - u[2] * w[1];
    const cy = u[2] * w[0] - u[0] * w[2];
    const cz = u[0] * w[1] - u[1] * w[0];
    trisRestArea[k] = 0.5 * Math.hypot(cx, cy, cz);
  }

  // vertex -> incident triangles (6 each on this closed grid): slot = triIndex * 4 + localIndex
  const vtri = new Uint32Array(N * 6);
  const vcount = new Uint32Array(N);
  for (let k = 0; k < T; k++) {
    for (let l = 0; l < 3; l++) {
      const v = tris[4 * k + l];
      vtri[6 * v + vcount[v]++] = k * 4 + l;
    }
  }
  for (let v = 0; v < N; v++) {
    if (vcount[v] !== 6) throw new Error(`vertex ${v} has ${vcount[v]} triangles, expected 6`);
  }

  // ---- constraint edges ----
  const rawEdges = [];
  for (let i = 0; i < NP; i++) {
    for (let j = 0; j < NC; j++) {
      const a = idx(i, j);
      rawEdges.push([a, idx(i + 1, j), EDGE_TYPE.STRETCH_U]);
      rawEdges.push([a, idx(i, j + 1), EDGE_TYPE.STRETCH_V]);
      rawEdges.push([a, idx(i + 1, j + 1), EDGE_TYPE.SHEAR_D]);
      rawEdges.push([idx(i + 1, j), idx(i, j + 1), EDGE_TYPE.SHEAR_A]);
      rawEdges.push([a, idx(i + 2, j), EDGE_TYPE.BEND_U]);
      rawEdges.push([a, idx(i, j + 2), EDGE_TYPE.BEND_V]);
    }
  }

  // Greedy edge colouring: edges of one colour share no vertex, so a colour can be solved
  // in parallel with Gauss-Seidel semantics and no atomics.
  const MAXC = 64;
  const used = new Uint8Array(N * MAXC);
  const colour = new Uint16Array(rawEdges.length);
  let colourCount = 0;
  for (let e = 0; e < rawEdges.length; e++) {
    const [a, b] = rawEdges[e];
    let c = 0;
    while (used[a * MAXC + c] || used[b * MAXC + c]) {
      c++;
      if (c >= MAXC) throw new Error('edge colouring exceeded MAXC');
    }
    used[a * MAXC + c] = 1;
    used[b * MAXC + c] = 1;
    colour[e] = c;
    colourCount = Math.max(colourCount, c + 1);
  }
  const order = rawEdges.map((_, e) => e).sort((p, q) => colour[p] - colour[q] || p - q);
  const E = rawEdges.length;
  const edges = new Uint32Array(E * 2);
  const edgeType = new Uint8Array(E);
  const edgeRest = new Float32Array(E);
  const colorStart = new Uint32Array(colourCount + 1);
  for (let k = 0; k < E; k++) {
    const [a, b, type] = rawEdges[order[k]];
    edges[2 * k] = a;
    edges[2 * k + 1] = b;
    edgeType[k] = type;
    const dx = pos[3 * a] - pos[3 * b];
    const dy = pos[3 * a + 1] - pos[3 * b + 1];
    const dz = pos[3 * a + 2] - pos[3 * b + 2];
    edgeRest[k] = Math.hypot(dx, dy, dz);
    colorStart[colour[order[k]] + 1]++;
  }
  for (let c = 0; c < colourCount; c++) colorStart[c + 1] += colorStart[c];
  const colorRanges = [];
  for (let c = 0; c < colourCount; c++) {
    colorRanges.push({ start: colorStart[c], count: colorStart[c + 1] - colorStart[c] });
  }

  // ---- cluster rest centres (shape matching): own vertex + 12 neighbours, equal masses ----
  const cl0 = new Float32Array(N * 4);
  for (let v = 0; v < N; v++) {
    let sx = x0[4 * v], sy = x0[4 * v + 1], sz = x0[4 * v + 2];
    for (let k = 0; k < 12; k++) {
      const u = vnbr[12 * v + k];
      sx += x0[4 * u];
      sy += x0[4 * u + 1];
      sz += x0[4 * u + 2];
    }
    cl0[4 * v] = sx / 13;
    cl0[4 * v + 1] = sy / 13;
    cl0[4 * v + 2] = sz / 13;
  }

  // ---- rim attachments (bead seat particles) ----
  const attachVertices = [];
  for (let v = 0; v < N; v++) if (seatFlag[v]) attachVertices.push(v);
  const K = attachVertices.length;
  // struct Attach { p: u32, phi: f32, r: f32, z: f32 } -> 16 bytes
  const attachBuffer = new ArrayBuffer(Math.max(1, K) * 16);
  const attachU32 = new Uint32Array(attachBuffer);
  const attachF32 = new Float32Array(attachBuffer);
  attachVertices.forEach((v, k) => {
    attachU32[4 * k] = v;
    attachF32[4 * k + 1] = psiOf[v];
    attachF32[4 * k + 2] = TYRE_GEOMETRY.rimRadius;
    attachF32[4 * k + 3] = pos[3 * v + 2];
  });

  // ---- render index buffer and ring/seat bookkeeping ----
  const triIndices = new Uint32Array(T * 3);
  for (let k = 0; k < T; k++) {
    triIndices[3 * k] = tris[4 * k];
    triIndices[3 * k + 1] = tris[4 * k + 1];
    triIndices[3 * k + 2] = tris[4 * k + 2];
  }

  return {
    NP,
    NC,
    N,
    T,
    E,
    K,
    perimeter,
    colourCount,
    colorRanges,
    vertexMass,
    tyreMass,
    restVolume,
    totalRestArea: trisRestArea.reduce((s, a) => s + a, 0),
    x0,
    cl0,
    pos, // local rest positions (3 floats per vertex)
    tris,
    trisRestArea,
    vtri,
    vnbr,
    edges,
    edgeType,
    edgeRest,
    attachBuffer,
    attachVertices: Uint32Array.from(attachVertices),
    triIndices,
    seatFlag,
  };
}
