// CPU reference of the shell FEM step.
// TEST ORACLE ONLY: the demo never runs this path. It measures stability and calibration quickly
// and provides expected values for the WGSL kernels.
import { buildTyreTopology } from '../src/tyre/topology.js';
import { buildFemRest } from '../src/tyre/femRest.js';

export function makeModel({ NP = 32, NC = 24, E = 3e6, nu = 0.45, thick = 0.01, Ec = 3e6, cordDeg = 20, tyreMass = 10 } = {}) {
  const t = buildTyreTopology({ NP, NC, tyreMass, samplesPerSegment: 1 });
  const { triM, triInfo } = buildFemRest(t, (cordDeg * Math.PI) / 180);
  const nuPS = nu;
  const lam = (E * nuPS) / (1 - nuPS * nuPS);
  const mu = E / (2 * (1 + nuPS));
  const mass = new Float64Array(t.N).fill(t.vertexMass); // scalar in the topology: tyreMass / N
  return { t, mass, triM, triInfo, N: t.N, T: t.T, lam, mu, Ec, thick, V0: t.restVolume };
}

const cross = (u, w) => [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];

/**
 * Forces on every vertex at the current positions x.
 * Elastic: St. Venant-Kirchhoff membrane (plane stress) plus tension-only belt cords.
 * Gas: adiabatic pressure p_abs = pAbs * (V0 / V)^gamma acting on dV/dx.
 * Returns { F, V, pGauge }.
 */
export function forces(m, x, { pAbs, patm, gamma }) {
  const { t, triM, triInfo, N, T, lam, mu, Ec, thick } = m;
  let V = 0;
  for (let k = 0; k < T; k++) {
    const a = 3 * t.tris[4 * k], b = 3 * t.tris[4 * k + 1], c = 3 * t.tris[4 * k + 2];
    const pa = [x[a], x[a + 1], x[a + 2]], pb = [x[b], x[b + 1], x[b + 2]], pc = [x[c], x[c + 1], x[c + 2]];
    V += (pa[0] * (pb[1] * pc[2] - pb[2] * pc[1]) - pa[1] * (pb[0] * pc[2] - pb[2] * pc[0]) + pa[2] * (pb[0] * pc[1] - pb[1] * pc[0])) / 6;
  }
  const pGauge = pAbs * Math.pow(m.V0 / V, gamma) - patm;
  const F = new Float64Array(3 * N);
  for (let k = 0; k < T; k++) {
    const i0 = t.tris[4 * k], i1 = t.tris[4 * k + 1], i2 = t.tris[4 * k + 2];
    const p0 = [x[3 * i0], x[3 * i0 + 1], x[3 * i0 + 2]];
    const p1 = [x[3 * i1], x[3 * i1 + 1], x[3 * i1 + 2]];
    const p2 = [x[3 * i2], x[3 * i2 + 1], x[3 * i2 + 2]];
    const e1 = [p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]];
    const e2 = [p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]];
    const M00 = triM[4 * k], M01 = triM[4 * k + 1], M10 = triM[4 * k + 2], M11 = triM[4 * k + 3];
    // F = [e1 e2] M  (3x2), columns Fc0, Fc1
    const Fc0 = [0, 0, 0], Fc1 = [0, 0, 0];
    for (let d = 0; d < 3; d++) {
      Fc0[d] = e1[d] * M00 + e2[d] * M10;
      Fc1[d] = e1[d] * M01 + e2[d] * M11;
    }
    const E00 = 0.5 * (Fc0[0] ** 2 + Fc0[1] ** 2 + Fc0[2] ** 2 - 1);
    const E11 = 0.5 * (Fc1[0] ** 2 + Fc1[1] ** 2 + Fc1[2] ** 2 - 1);
    const E01 = 0.5 * (Fc0[0] * Fc1[0] + Fc0[1] * Fc1[1] + Fc0[2] * Fc1[2]);
    const tr = E00 + E11;
    let S00 = 2 * mu * E00 + lam * tr;
    let S11 = 2 * mu * E11 + lam * tr;
    let S01 = 2 * mu * E01;
    const dx = triInfo[4 * k + 1], dy = triInfo[4 * k + 2];
    const dEd = dx * dx * E00 + 2 * dx * dy * E01 + dy * dy * E11;
    if (dEd > 0) {
      S00 += Ec * dEd * dx * dx;
      S11 += Ec * dEd * dy * dy;
      S01 += Ec * dEd * dx * dy;
    }
    const A = triInfo[4 * k] * thick;
    // P = F S ; nodal gradients g1 = A (P0 M00 + P1 M01), g2 = A (P0 M10 + P1 M11)
    const P0 = [0, 0, 0], P1 = [0, 0, 0];
    for (let d = 0; d < 3; d++) {
      P0[d] = Fc0[d] * S00 + Fc1[d] * S01;
      P1[d] = Fc0[d] * S01 + Fc1[d] * S11;
    }
    const g1 = [0, 0, 0], g2 = [0, 0, 0];
    for (let d = 0; d < 3; d++) {
      g1[d] = A * (P0[d] * M00 + P1[d] * M01);
      g2[d] = A * (P0[d] * M10 + P1[d] * M11);
    }
    // gas: dV/dx_a = (1/6) x_b x x_c (cyclic)
    const ga = cross(p1, p2), gb = cross(p2, p0), gc = cross(p0, p1);
    for (let d = 0; d < 3; d++) {
      F[3 * i0 + d] += g1[d] + g2[d] + (pGauge * ga[d]) / 6;
      F[3 * i1 + d] += -g1[d] + (pGauge * gb[d]) / 6;
      F[3 * i2 + d] += -g2[d] + (pGauge * gc[d]) / 6;
    }
  }
  return { F, V, pGauge };
}

/**
 * Explicit substepped run with a 1-DOF hub (vertical) and kinematic seat vertices.
 * Ground is a one-way inelastic contact at y = contactRadius (friction omitted in this oracle).
 */
export function run(m, { dt, seconds, payloadKg = 400, bar = 2.3, massDamp = 4, hubDamp = 2000, gamma = 1.4, rampSec = 1.0, log = 0, h0 = null }) {
  const { t, N, mass } = m;
  const g = 9.81;
  const patm = 1e5;
  const pAbsFull = patm + bar * 1e5;
  const rc = 0.004;
  const hubM = 30 + payloadKg;
  const totalM = hubM + t.tyreMass;
  const x = new Float64Array(3 * N), v = new Float64Array(3 * N);
  let hubY = h0 ?? 0.316 + rc + 0.002;
  let hubV = 0;
  for (let i = 0; i < N; i++) {
    x[3 * i] = t.pos[3 * i];
    x[3 * i + 1] = t.pos[3 * i + 1] + hubY;
    x[3 * i + 2] = t.pos[3 * i + 2];
  }
  const steps = Math.round(seconds / dt);
  const hist = [];
  let maxSpeed = 0;
  let nanAt = -1;
  let fzAcc = 0, fzCount = 0;
  for (let s = 0; s < steps; s++) {
    const ramp = Math.min(1, (s * dt) / 0.5);
    const pAbs = patm + (pAbsFull - patm) * ramp;
    const { F, V, pGauge } = forces(m, x, { pAbs, patm, gamma });
    // predicted hub velocity (gravity, payload-included mass, hub damper)
    // payload weight ramps in through gravity (settle-in); the mass is constant
    const tRamp = Math.min(1, (s * dt) / rampSec);
    const gEff = g * (1 - (payloadKg / hubM) * (1 - tRamp));
    const vhStar = hubV - dt * gEff - (dt * hubDamp * hubV) / hubM;
    let pinReaction = 0; // momentum delivered to the seat vertices by the pins
    for (let i = 0; i < N; i++) {
      const mi = mass[i];
      for (let d = 0; d < 3; d++) {
        const j = 3 * i + d;
        const grav = d === 1 ? -gEff : 0;
        const vStar = v[j] + dt * (F[j] / mi - massDamp * v[j]) + dt * grav;
        if (t.seatFlag[i]) {
          // pinned: the seat moves with the hub. Vertical velocity is the hub velocity.
          pinReaction += d === 1 ? mi * (vStar - vhStar) : 0;
          v[j] = d === 1 ? vhStar : 0;
          x[j] = d === 1 ? hubY + t.pos[j] : t.pos[j];
        } else {
          v[j] = vStar;
          x[j] = x[j] + dt * vStar;
        }
      }
    }
    // hub: receives the reaction of the seat momentum (gas pushes the hub up)
    hubV = vhStar + pinReaction / hubM;
    hubY += dt * hubV;
    // ground contact: one-way, inelastic; impulses give the ground reaction
    let impulse = 0;
    for (let i = 0; i < N; i++) {
      const j = 3 * i + 1;
      if (x[j] < rc) {
        const dyv = rc - x[j];
        impulse += (mass[i] * dyv) / dt; // momentum pushed back up [kg m/s]
        x[j] = rc;
        if (v[j] < 0) v[j] = 0;
      }
    }
    // ground reaction force = momentum rate = impulse / dt
    const Fz = impulse / dt;
    fzAcc += Fz;
    fzCount++;
    let mx = 0;
    for (let i = 0; i < N; i++) mx = Math.max(mx, Math.abs(v[3 * i]), Math.abs(v[3 * i + 1]), Math.abs(v[3 * i + 2]));
    maxSpeed = Math.max(maxSpeed, mx);
    if (!Number.isFinite(mx) || !Number.isFinite(hubY)) {
      nanAt = s;
      break;
    }
    if (log && s % log === 0) hist.push({ t: s * dt, hubY, hubV, V, pGauge, Fz, maxSpeed: mx });
  }
  return { hist, maxSpeed, nanAt, hubY, hubV, meanFz: fzCount ? fzAcc / fzCount : NaN, totalM };
}
