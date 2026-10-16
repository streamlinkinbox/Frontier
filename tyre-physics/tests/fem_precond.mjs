// Element-level stiffness helpers for the block-Jacobi preconditioner (CPU oracle).

/** Element-local K product. pn[a][d] = nodal displacement of local node a in direction d.
 *  Writes the nodal force response (negative of the force derivative) into out3[a][d]. */
export function elemK(m, el, k, pn, out3) {
  const { triM, lam, mu, Ec } = m;
  const o = 16 * k;
  const de1 = [pn[1][0] - pn[0][0], pn[1][1] - pn[0][1], pn[1][2] - pn[0][2]];
  const de2 = [pn[2][0] - pn[0][0], pn[2][1] - pn[0][1], pn[2][2] - pn[0][2]];
  const M00 = triM[4 * k], M01 = triM[4 * k + 1], M10 = triM[4 * k + 2], M11 = triM[4 * k + 3];
  const R0 = [el[o], el[o + 1], el[o + 2]], R1 = [el[o + 3], el[o + 4], el[o + 5]];
  const dF0 = [0, 0, 0], dF1 = [0, 0, 0];
  for (let d = 0; d < 3; d++) {
    dF0[d] = de1[d] * M00 + de2[d] * M10;
    dF1[d] = de1[d] * M01 + de2[d] * M11;
  }
  const q00 = R0[0] * dF0[0] + R0[1] * dF0[1] + R0[2] * dF0[2];
  const q01 = R0[0] * dF1[0] + R0[1] * dF1[1] + R0[2] * dF1[2];
  const q10 = R1[0] * dF0[0] + R1[1] * dF0[1] + R1[2] * dF0[2];
  const q11 = R1[0] * dF1[0] + R1[1] * dF1[1] + R1[2] * dF1[2];
  const d00 = q00, d11 = q11, d01 = 0.5 * (q01 + q10);
  const tr = d00 + d11;
  let dS00 = 2 * mu * d00 + lam * tr, dS01 = 2 * mu * d01, dS11 = 2 * mu * d11 + lam * tr;
  if (el[o + 9]) {
    const dx = el[o + 10], dy = el[o + 11];
    const dd = dx * dx * d00 + 2 * dx * dy * d01 + dy * dy * d11;
    dS00 += Ec * dd * dx * dx;
    dS01 += Ec * dd * dx * dy;
    dS11 += Ec * dd * dy * dy;
  }
  const A = el[o + 12];
  const dP0 = [0, 0, 0], dP1 = [0, 0, 0];
  for (let d = 0; d < 3; d++) {
    dP0[d] = R0[d] * dS00 + R1[d] * dS01;
    dP1[d] = R0[d] * dS01 + R1[d] * dS11;
  }
  for (let d = 0; d < 3; d++) {
    const g1 = A * (dP0[d] * M00 + dP1[d] * M01);
    const g2 = A * (dP0[d] * M10 + dP1[d] * M11);
    out3[0][d] = -(g1 + g2);
    out3[1][d] = g1;
    out3[2][d] = g2;
  }
  return out3;
}

/** Per-vertex 3x3 diagonal blocks of K. Returns Float64Array(9N), row-major blocks. */
export function diagBlocks(m, el) {
  const { t, T, N } = m;
  const D = new Float64Array(9 * N);
  const pn = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
  const out3 = [[0, 0, 0], [0, 0, 0], [0, 0, 0]];
  for (let k = 0; k < T; k++) {
    const vid = [t.tris[4 * k], t.tris[4 * k + 1], t.tris[4 * k + 2]];
    for (let a = 0; a < 3; a++) {
      for (let dc = 0; dc < 3; dc++) {
        for (let r = 0; r < 3; r++) pn[r].fill(0);
        pn[a][dc] = 1;
        elemK(m, el, k, pn, out3);
        for (let dr = 0; dr < 3; dr++) D[9 * vid[a] + 3 * dr + dc] += out3[a][dr];
      }
    }
  }
  return D;
}

/** Invert symmetric 3x3 blocks; falls back to a floored diagonal when a block is not positive. */
export function invertBlocks(D, N, floor) {
  const inv = new Float64Array(9 * N);
  for (let i = 0; i < N; i++) {
    const o = 9 * i;
    const A00 = D[o], A11 = D[o + 4], A22 = D[o + 8];
    const A01 = 0.5 * (D[o + 1] + D[o + 3]), A02 = 0.5 * (D[o + 2] + D[o + 6]), A12 = 0.5 * (D[o + 5] + D[o + 7]);
    const c00 = A11 * A22 - A12 * A12, c01 = A02 * A12 - A01 * A22, c02 = A01 * A12 - A02 * A11;
    const det = A00 * c00 + A01 * c01 + A02 * c02;
    const scale = Math.max(Math.abs(A00) + Math.abs(A11) + Math.abs(A22), 1e-30);
    if (!(A00 > 0 && A11 > 0 && A22 > 0 && det > floor * scale * scale * scale)) {
      inv[o] = 1 / Math.max(A00, floor * scale);
      inv[o + 4] = 1 / Math.max(A11, floor * scale);
      inv[o + 8] = 1 / Math.max(A22, floor * scale);
      continue;
    }
    const id = 1 / det;
    const c11 = A00 * A22 - A02 * A02, c12 = A02 * A01 - A00 * A12, c22 = A00 * A11 - A01 * A01;
    inv[o] = c00 * id; inv[o + 1] = c01 * id; inv[o + 2] = c02 * id;
    inv[o + 3] = c01 * id; inv[o + 4] = c11 * id; inv[o + 5] = c12 * id;
    inv[o + 6] = c02 * id; inv[o + 7] = c12 * id; inv[o + 8] = c22 * id;
  }
  return inv;
}
