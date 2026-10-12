import { Out, ribbon, wall, sweep, quad, box, strip, polyWall } from '../src/road/geom/out.js';
const frames = [0, 5, 10].map(s => ({ s, x: s, y: 0, z: 0, tx: 1, tz: 0, nx: -0, nz: 1 })); // along +x, right = +z
function normals(out) {
  const r = [];
  for (const k in out.mesh) { const { p, i } = out.mesh[k];
    for (let t = 0; t < i.length; t += 3) {
      const A = i[t] * 3, B = i[t + 1] * 3, C = i[t + 2] * 3;
      const cb = [p[C] - p[B], p[C + 1] - p[B + 1], p[C + 2] - p[B + 2]], ab = [p[A] - p[B], p[A + 1] - p[B + 1], p[A + 2] - p[B + 2]];
      r.push([cb[1] * ab[2] - cb[2] * ab[1], cb[2] * ab[0] - cb[0] * ab[2], cb[0] * ab[1] - cb[1] * ab[0]].map(v => Math.sign(Math.round(v * 1000))));
    } }
  return JSON.stringify([...new Set(r.map(String))]);
}
let o = new Out(); ribbon(o, 'm', frames, -1, 1, 0); console.log('ribbon (want 0,1,0)', normals(o));
o = new Out(); wall(o, 'm', frames, 1, 0, 1, 1); console.log('wall +1 (want 0,0,1)', normals(o));
o = new Out(); wall(o, 'm', frames, 1, 0, 1, -1); console.log('wall -1 (want 0,0,-1)', normals(o));
o = new Out(); sweep(o, 'm', frames, [[0, 0], [0, 1]]); console.log('sweep up-trace (outward right of travel in o,h → ?)', normals(o));
o = new Out(); quad(o, 'm', 0, 0, 0, 1, 0, 1, 1); console.log('quad (want 0,1,0)', normals(o));
o = new Out(); box(o, 'm', 0, 0, 0, 1, 0, 1, 1, 1); console.log('box', normals(o));
// strip: CCW around center at origin; inner curb A at radius 5, outer B radius 8, angles increasing (x->z)
const A = [], B = []; for (let k = 0; k <= 4; k++) { const a = k * 0.3; A.push([5 * Math.cos(a), 0, 5 * Math.sin(a)]); B.push([8 * Math.cos(a), 0, 8 * Math.sin(a)]); }
o = new Out(); strip(o, 'm', A, B); console.log('strip ccw (want 0,1,0)', normals(o));
o = new Out(); polyWall(o, 'm', A, 0, 1, 1); console.log('polyWall side+1 (want facing right of travel: outward = away from center?)', normals(o), 'first pt dir right-of-travel = ', [-(A[1][2]-A[0][2]), (A[1][0]-A[0][0])]);
{ const o = new Out(); box(o, 'm', 0, 0, 0, 1, 0, 1, 1, 1); const { p, i } = o.mesh.m; let bad = 0;
  for (let t = 0; t < i.length; t += 3) { const A = i[t] * 3, B = i[t + 1] * 3, C = i[t + 2] * 3;
    const cb = [p[C] - p[B], p[C + 1] - p[B + 1], p[C + 2] - p[B + 2]], ab = [p[A] - p[B], p[A + 1] - p[B + 1], p[A + 2] - p[B + 2]];
    const n = [cb[1] * ab[2] - cb[2] * ab[1], cb[2] * ab[0] - cb[0] * ab[2], cb[0] * ab[1] - cb[1] * ab[0]];
    const c = [(p[A] + p[B] + p[C]) / 3, (p[A + 1] + p[B + 1] + p[C + 1]) / 3, (p[A + 2] + p[B + 2] + p[C + 2]) / 3];
    if (n[0] * c[0] + n[1] * c[1] + n[2] * c[2] <= 0) bad++; }
  console.log('box inward faces:', bad); }
