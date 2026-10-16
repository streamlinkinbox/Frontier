/**
 * Rest-state data for the co-rotational / St. Venant-Kirchhoff shell elements.
 *
 * For every triangle we store the inverse of the rest-plane edge matrix (so the deformation
 * gradient is F = [x1-x0, x2-x0] * Dm^-1 in the rest plane), the rest area and the unit belt-cord
 * direction in the same rest plane. This is pure setup: it runs once when the topology is built.
 */

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = (a) => {
  const l = Math.hypot(a[0], a[1], a[2]) || 1;
  return [a[0] / l, a[1] / l, a[2] / l];
};

/**
 * @param {object} t  topology from buildTyreTopology
 * @param {number} beltAngleRad  cord angle from the circumferential direction, in the surface
 * @returns {{ triM: Float32Array, triInfo: Float32Array }}
 *   triM[4k..4k+3]   = Dm^-1 row-major (m00, m01, m10, m11)
 *   triInfo[4k..]    = (rest area, cord dir x, cord dir y, 0) in rest-plane coordinates
 */
export function buildFemRest(t, beltAngleRad = (20 * Math.PI) / 180) {
  const T = t.T;
  const NC = t.NC;
  const N = t.N;
  const P = (v) => [t.pos[3 * v], t.pos[3 * v + 1], t.pos[3 * v + 2]];
  const triM = new Float32Array(4 * T);
  const triInfo = new Float32Array(4 * T);
  for (let k = 0; k < T; k++) {
    const a = t.tris[4 * k], b = t.tris[4 * k + 1], c = t.tris[4 * k + 2];
    const x0 = P(a), x1 = P(b), x2 = P(c);
    const e1 = norm(sub(x1, x0));
    const nrm = norm(cross(sub(x1, x0), sub(x2, x0)));
    const e2 = cross(nrm, e1);
    // rest-plane coordinates of the three corners
    const X = [[0, 0], [dot(sub(x1, x0), e1), dot(sub(x1, x0), e2)], [dot(sub(x2, x0), e1), dot(sub(x2, x0), e2)]];
    const d1 = [X[1][0] - X[0][0], X[1][1] - X[0][1]];
    const d2 = [X[2][0] - X[0][0], X[2][1] - X[0][1]];
    const det = d1[0] * d2[1] - d2[0] * d1[1];
    // Dm = [d1 d2] (columns); Dm^-1 = 1/det * [[d2y, -d2x], [-d1y, d1x]]
    triM[4 * k] = d2[1] / det;
    triM[4 * k + 1] = -d2[0] / det;
    triM[4 * k + 2] = -d1[1] / det;
    triM[4 * k + 3] = d1[0] / det;
    const area = 0.5 * Math.abs(det);
    // cord direction: circumferential (vertex i-direction) blended with the meridional (profile)
    // tangent of the first corner, then projected into the rest plane.
    const vIdx = Math.floor(a / NC), jIdx = a % NC;
    const jp = (jIdx + 1) % NC, jm = (jIdx + NC - 1) % NC;
    const merid = norm(sub(P(vIdx * NC + jp), P(vIdx * NC + jm)));
    const circ = norm(cross(nrm, merid));
    const cw = norm([Math.cos(beltAngleRad) * circ[0] + Math.sin(beltAngleRad) * merid[0],
      Math.cos(beltAngleRad) * circ[1] + Math.sin(beltAngleRad) * merid[1],
      Math.cos(beltAngleRad) * circ[2] + Math.sin(beltAngleRad) * merid[2]]);
    const cx = dot(cw, e1), cy = dot(cw, e2);
    const cl = Math.hypot(cx, cy) || 1;
    triInfo[4 * k] = area;
    triInfo[4 * k + 1] = cx / cl;
    triInfo[4 * k + 2] = cy / cl;
    triInfo[4 * k + 3] = 0;
  }
  return { triM, triInfo, N };
}
