// CPU oracle for the co-rotational shell FEM with implicit (backward-Euler) time stepping.
// TEST ORACLE ONLY. The demo runs the same algorithm on the GPU (src/shaders/tyre_sim.wgsl).
//
// Element: linear-elastic membrane in the rest plane, evaluated on the rotation-removed strain
//   F = R U (polar), eps = U - I,  S = C eps (plane stress) + tension-only cord term,  P = R S.
// Tangent (matrix-free): dP = R C sym(R^T dF). This is positive semi-definite for every
// deformation, which the St. Venant-Kirchhoff tangent is not under compression.
import { makeModel } from './fem_reference.mjs';
import { diagBlocks, invertBlocks } from './fem_precond.mjs';

const cross = (u, w) => [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];

/** Element state for all triangles at positions x: R (3x2 cols), S (3), cord flag, dx, dy, area. */
function elementStates(m, x) {
  const { t, triM, triInfo, T, lam, mu, Ec, thick } = m;
  const el = new Float64Array(T * 16);
  // el: R col0 (0..2), R col1 (3..5), S00 S01 S11 (6..8), cordOn (9), dx dy (10,11), A (12), spare (13..15)
  for (let k = 0; k < T; k++) {
    const i0 = t.tris[4 * k], i1 = t.tris[4 * k + 1], i2 = t.tris[4 * k + 2];
    const e1 = [x[3 * i1] - x[3 * i0], x[3 * i1 + 1] - x[3 * i0 + 1], x[3 * i1 + 2] - x[3 * i0 + 2]];
    const e2 = [x[3 * i2] - x[3 * i0], x[3 * i2 + 1] - x[3 * i0 + 1], x[3 * i2 + 2] - x[3 * i0 + 2]];
    const M00 = triM[4 * k], M01 = triM[4 * k + 1], M10 = triM[4 * k + 2], M11 = triM[4 * k + 3];
    const Fc0 = [0, 0, 0], Fc1 = [0, 0, 0];
    for (let d = 0; d < 3; d++) {
      Fc0[d] = e1[d] * M00 + e2[d] * M10;
      Fc1[d] = e1[d] * M01 + e2[d] * M11;
    }
    // C = F^T F (2x2), U = sqrt(C) = (C + sqrt(det C) I) / sqrt(tr C + 2 sqrt(det C))
    const c00 = Fc0[0] ** 2 + Fc0[1] ** 2 + Fc0[2] ** 2;
    const c11 = Fc1[0] ** 2 + Fc1[1] ** 2 + Fc1[2] ** 2;
    const c01 = Fc0[0] * Fc1[0] + Fc0[1] * Fc1[1] + Fc0[2] * Fc1[2];
    const detC = Math.max(c00 * c11 - c01 * c01, 1e-30);
    const sq = Math.sqrt(detC);
    const tq = Math.sqrt(c00 + c11 + 2 * sq);
    const u00 = (c00 + sq) / tq, u01 = c01 / tq, u11 = (c11 + sq) / tq;
    const du = u00 * u11 - u01 * u01;
    // U^-1
    const i00 = u11 / du, i01 = -u01 / du, i11 = u00 / du;
    // R = F U^-1 (3x2)
    const R0 = [0, 0, 0], R1 = [0, 0, 0];
    for (let d = 0; d < 3; d++) {
      R0[d] = Fc0[d] * i00 + Fc1[d] * i01;
      R1[d] = Fc0[d] * i01 + Fc1[d] * i11;
    }
    // rotation-removed strain eps = U - I
    const e00 = u00 - 1, e01 = u01, e11 = u11 - 1;
    const tr = e00 + e11;
    let S00 = 2 * mu * e00 + lam * tr, S01 = 2 * mu * e01, S11 = 2 * mu * e11 + lam * tr;
    const dx = triInfo[4 * k + 1], dy = triInfo[4 * k + 2];
    const dEd = dx * dx * e00 + 2 * dx * dy * e01 + dy * dy * e11;
    const cordOn = dEd > 0 ? 1 : 0;
    if (cordOn) {
      S00 += Ec * dEd * dx * dx;
      S01 += Ec * dEd * dx * dy;
      S11 += Ec * dEd * dy * dy;
    }
    const o = 16 * k;
    for (let d = 0; d < 3; d++) {
      el[o + d] = R0[d];
      el[o + 3 + d] = R1[d];
    }
    el[o + 6] = S00; el[o + 7] = S01; el[o + 8] = S11;
    el[o + 9] = cordOn; el[o + 10] = dx; el[o + 11] = dy; el[o + 12] = triInfo[4 * k] * thick;
  }
  return el;
}

/** Elastic + gas forces. Returns { F, V, pGauge }. */
export function forcesCorot(m, x, { pAbs, patm, gamma }) {
  const { t, triM, N, T } = m;
  let V = 0;
  for (let k = 0; k < T; k++) {
    const a = 3 * t.tris[4 * k], b = 3 * t.tris[4 * k + 1], c = 3 * t.tris[4 * k + 2];
    const pa = [x[a], x[a + 1], x[a + 2]], pb = [x[b], x[b + 1], x[b + 2]], pc = [x[c], x[c + 1], x[c + 2]];
    V += (pa[0] * (pb[1] * pc[2] - pb[2] * pc[1]) - pa[1] * (pb[0] * pc[2] - pb[2] * pc[0]) + pa[2] * (pb[0] * pc[1] - pb[1] * pc[0])) / 6;
  }
  const pGauge = pAbs * Math.pow(m.V0 / V, gamma) - patm;
  const el = elementStates(m, x);
  const F = new Float64Array(3 * N);
  for (let k = 0; k < T; k++) {
    const o = 16 * k;
    const i0 = t.tris[4 * k], i1 = t.tris[4 * k + 1], i2 = t.tris[4 * k + 2];
    const M00 = triM[4 * k], M01 = triM[4 * k + 1], M10 = triM[4 * k + 2], M11 = triM[4 * k + 3];
    const R0 = [el[o], el[o + 1], el[o + 2]], R1 = [el[o + 3], el[o + 4], el[o + 5]];
    const S00 = el[o + 6], S01 = el[o + 7], S11 = el[o + 8], A = el[o + 12];
    const P0 = [0, 0, 0], P1 = [0, 0, 0];
    for (let d = 0; d < 3; d++) {
      P0[d] = R0[d] * S00 + R1[d] * S01;
      P1[d] = R0[d] * S01 + R1[d] * S11;
    }
    const g1 = [0, 0, 0], g2 = [0, 0, 0];
    for (let d = 0; d < 3; d++) {
      g1[d] = A * (P0[d] * M00 + P1[d] * M01);
      g2[d] = A * (P0[d] * M10 + P1[d] * M11);
    }
    const p0 = [x[3 * i0], x[3 * i0 + 1], x[3 * i0 + 2]];
    const p1 = [x[3 * i1], x[3 * i1 + 1], x[3 * i1 + 2]];
    const p2 = [x[3 * i2], x[3 * i2 + 1], x[3 * i2 + 2]];
    const ga = cross(p1, p2), gb = cross(p2, p0), gc = cross(p0, p1);
    for (let d = 0; d < 3; d++) {
      F[3 * i0 + d] += g1[d] + g2[d] + (pGauge * ga[d]) / 6;
      F[3 * i1 + d] += -g1[d] + (pGauge * gb[d]) / 6;
      F[3 * i2 + d] += -g2[d] + (pGauge * gc[d]) / 6;
    }
  }
  return { F, V, pGauge, el };
}

/** K p = -d(elastic force)/dx p, co-rotational tangent: dP = R C sym(R^T dF). */
export function kProduct(m, el, p, out) {
  const { t, triM, T, lam, mu, Ec } = m;
  out.fill(0);
  for (let k = 0; k < T; k++) {
    const o = 16 * k;
    const i0 = t.tris[4 * k], i1 = t.tris[4 * k + 1], i2 = t.tris[4 * k + 2];
    const de1 = [p[3 * i1] - p[3 * i0], p[3 * i1 + 1] - p[3 * i0 + 1], p[3 * i1 + 2] - p[3 * i0 + 2]];
    const de2 = [p[3 * i2] - p[3 * i0], p[3 * i2 + 1] - p[3 * i0 + 1], p[3 * i2 + 2] - p[3 * i0 + 2]];
    const M00 = triM[4 * k], M01 = triM[4 * k + 1], M10 = triM[4 * k + 2], M11 = triM[4 * k + 3];
    const R0 = [el[o], el[o + 1], el[o + 2]], R1 = [el[o + 3], el[o + 4], el[o + 5]];
    const dF0 = [0, 0, 0], dF1 = [0, 0, 0];
    for (let d = 0; d < 3; d++) {
      dF0[d] = de1[d] * M00 + de2[d] * M10;
      dF1[d] = de1[d] * M01 + de2[d] * M11;
    }
    // Q = R^T dF (2x2), dEps = sym(Q)
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
      out[3 * i0 + d] += -(g1 + g2);
      out[3 * i1 + d] += g1;
      out[3 * i2 + d] += g2;
    }
  }
  return out;
}

/**
 * Backward-Euler step: (M(1+h c) + h^2 K) v = M v0 + h f0 for the free DOFs. Seat DOFs are
 * Dirichlet with velocity (0, vSeatY, 0). Returns the new velocity and the seat impulse (y).
 */
export function implicitStep(m, st, { h, c, pAbs, patm, gamma, vSeatY, gEff, maxIt = 120, tol = 1e-4, blockPrec = true }) {
  const { t, N, mass } = m;
  const { x, v } = st;
  const n3 = 3 * N;
  const { F: f0, V, pGauge, el } = forcesCorot(m, x, { pAbs, patm, gamma });
  const seat = new Uint8Array(n3);
  for (let i = 0; i < N; i++) if (t.seatFlag[i]) { seat[3 * i] = 1; seat[3 * i + 1] = 1; seat[3 * i + 2] = 1; }
  const freeM = (j) => (seat[j] ? 0 : 1);
  const massOf = (j) => mass[(j / 3) | 0];
  // right-hand side: b = M v0 + h (f0 - m g e_y)
  const b = new Float64Array(n3);
  for (let j = 0; j < n3; j++) {
    b[j] = massOf(j) * v[j] + h * f0[j] - (j % 3 === 1 ? h * massOf(j) * gEff : 0);
  }
  const vs = new Float64Array(n3);
  for (let i = 0; i < N; i++) if (t.seatFlag[i]) vs[3 * i + 1] = vSeatY;
  // lift the seat coupling: rhs_free = b_free - h^2 K vs (free rows)
  const kvs = kProduct(m, el, vs, new Float64Array(n3));
  const rhs = new Float64Array(n3);
  for (let j = 0; j < n3; j++) rhs[j] = freeM(j) * (b[j] - h * h * kvs[j]);
  const Aop = (p, out) => {
    const pm = new Float64Array(n3);
    for (let j = 0; j < n3; j++) pm[j] = freeM(j) * p[j];
    const kp = kProduct(m, el, pm, new Float64Array(n3));
    for (let j = 0; j < n3; j++) out[j] = freeM(j) * (massOf(j) * (1 + h * c) * pm[j] + h * h * kp[j]);
    return out;
  };
  // preconditioner: per-vertex 3x3 blocks of M(1+hc) + h^2 K (seat rows are identity)
  const Dk = blockPrec ? diagBlocks(m, el) : null;
  const blk = new Float64Array(9 * N);
  for (let i = 0; i < N; i++) {
    for (let q = 0; q < 9; q++) blk[9 * i + q] = h * h * (Dk ? Dk[9 * i + q] : 0);
    for (let dd = 0; dd < 3; dd++) blk[9 * i + 4 * dd] += mass[i] * (1 + h * c);
  }
  const invB = invertBlocks(blk, N, 1e-9);
  const applyPrec = (rr, zz) => {
    for (let i = 0; i < N; i++) {
      const o = 9 * i;
      const isSeat = t.seatFlag[i];
      for (let dd = 0; dd < 3; dd++) {
        if (isSeat) { zz[3 * i + dd] = 0; continue; }
        zz[3 * i + dd] = invB[o + 3 * dd] * rr[3 * i] + invB[o + 3 * dd + 1] * rr[3 * i + 1] + invB[o + 3 * dd + 2] * rr[3 * i + 2];
      }
    }
  };
  // Jacobi-preconditioned conjugate gradients
  const xv = new Float64Array(n3), r = Float64Array.from(rhs), z = new Float64Array(n3), p = new Float64Array(n3), Ap = new Float64Array(n3);
  let rz = 0;
  applyPrec(r, z);
  for (let j = 0; j < n3; j++) { p[j] = z[j]; rz += r[j] * z[j]; }
  let bn = 0;
  for (let j = 0; j < n3; j++) bn += rhs[j] * rhs[j];
  bn = Math.sqrt(bn) || 1;
  let it = 0, relRes = 0;
  for (; it < maxIt; it++) {
    Aop(p, Ap);
    let pAp = 0;
    for (let j = 0; j < n3; j++) pAp += p[j] * Ap[j];
    const alpha = rz / pAp;
    let rr = 0;
    for (let j = 0; j < n3; j++) { xv[j] += alpha * p[j]; r[j] -= alpha * Ap[j]; rr += r[j] * r[j]; }
    relRes = Math.sqrt(rr) / bn;
    if (relRes < tol) { it++; break; }
    let rzNew = 0;
    applyPrec(r, z);
    for (let j = 0; j < n3; j++) rzNew += r[j] * z[j];
    const beta = rzNew / rz;
    rz = rzNew;
    for (let j = 0; j < n3; j++) p[j] = z[j] + beta * p[j];
  }
  const vNew = new Float64Array(n3);
  for (let j = 0; j < n3; j++) vNew[j] = seat[j] ? vs[j] : xv[j];
  // seat impulse: (A v - b) on seat rows, full coupling
  const kv = kProduct(m, el, vNew, new Float64Array(n3));
  let Jy = 0;
  for (let i = 0; i < N; i++) {
    if (!t.seatFlag[i]) continue;
    const j = 3 * i + 1;
    Jy += massOf(j) * (1 + h * c) * vNew[j] + h * h * kv[j] - b[j];
  }
  return { vNew, V, pGauge, cgIters: it, relRes, Jy, el };
}

/** Full protocol run with a 1-DOF vertical hub, payload ramp, pressure ramp, one-way ground. */
export function runShell(m, { h, seconds, payloadKg = 400, bar = 2.3, c = 4, hubDamp = 2000, gamma = 1.4, rampSec = 1.0, log = 0, maxIt = 80 }) {
  const { t, N, mass } = m;
  const g = 9.81, patm = 1e5, pAbsFull = patm + bar * 1e5, rc = 0.004;
  const hubM = 30 + payloadKg;
  const x = new Float64Array(3 * N), v = new Float64Array(3 * N);
  let hubY = 0.316 + rc + 0.002, hubV = 0;
  for (let i = 0; i < N; i++) {
    x[3 * i] = t.pos[3 * i];
    x[3 * i + 1] = t.pos[3 * i + 1] + hubY;
    x[3 * i + 2] = t.pos[3 * i + 2];
  }
  const steps = Math.round(seconds / h);
  const hist = [];
  let cgSum = 0, cgMax = 0, nanAt = -1, maxSpeed = 0;
  for (let s = 0; s < steps; s++) {
    const tRamp = Math.min(1, (s * h) / rampSec);
    const gEff = g * (1 - (payloadKg / hubM) * (1 - tRamp));
    const pAbs = patm + (pAbsFull - patm) * Math.min(1, (s * h) / 0.5);
    const vhStar = hubV - h * gEff - (h * hubDamp * hubV) / hubM;
    const res = implicitStep(m, { x, v }, { h, c, pAbs, patm, gamma, vSeatY: vhStar, gEff, maxIt });
    cgSum += res.cgIters; cgMax = Math.max(cgMax, res.cgIters);
    v.set(res.vNew);
    for (let i = 0; i < N; i++) {
      if (t.seatFlag[i]) {
        x[3 * i] = t.pos[3 * i];
        x[3 * i + 1] = hubY + t.pos[3 * i + 1];
        x[3 * i + 2] = t.pos[3 * i + 2];
      } else {
        for (let d = 0; d < 3; d++) x[3 * i + d] += h * v[3 * i + d];
      }
    }
    hubV = vhStar - res.Jy / hubM;
    hubY += h * hubV;
    let impulse = 0;
    for (let i = 0; i < N; i++) {
      const j = 3 * i + 1;
      if (x[j] < rc) {
        impulse += (mass[i] * (rc - x[j])) / h;
        x[j] = rc;
        if (v[j] < 0) v[j] = 0;
      }
    }
    const Fz = impulse / h;
    let mx = 0;
    for (let j = 0; j < 3 * N; j++) mx = Math.max(mx, Math.abs(v[j]));
    maxSpeed = Math.max(maxSpeed, mx);
    if (!Number.isFinite(mx) || !Number.isFinite(hubY)) { nanAt = s; break; }
    if (log && s % log === 0) hist.push({ t: s * h, hubY, hubV, V: res.V, pGauge: res.pGauge, Fz, cg: res.cgIters, relRes: res.relRes });
  }
  return { hist, nanAt, maxSpeed, hubY, hubV, cgMean: cgSum / Math.max(1, steps), cgMax };
}

export { makeModel };
