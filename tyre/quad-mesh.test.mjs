import test from 'node:test';
import assert from 'node:assert/strict';
import { buildTyreQuadMesh, validateQuadMesh, quadsToTriangles, computeVertexNormals } from './quad-mesh.mjs';

// Simple 205/55-ish cross-section: bead → sidewall → shoulder → tread (mm), outward normals.
function sampleProfile(n = 40) {
  const pts = [];
  const bw = 60, halfW = 102, Rrim = 200, Rout = 330, shoulder = 20, treadHalf = 80;
  const P = [];
  for (let i = 0; i <= n; i++) {                 // tread arc from shoulder to centre, mirrored later
    const t = i / n; const x = t * treadHalf; P.push({ x, r: Rout - 2 * t * t });
  }
  const side = [];
  for (let i = 1; i <= n; i++) {                 // sidewall curve from tread edge down to bead
    const t = i / n; side.push({ x: treadHalf + (halfW - treadHalf) * Math.sin(t * Math.PI / 2), r: (Rout - 2) - (Rout - 2 - Rrim) * t });
  }
  const right = [...P, ...side].map(p => ({ ...p }));
  const left = right.slice(1).reverse().map(p => ({ x: -p.x, r: p.r }));
  const all = [...left, ...right];
  // normals from neighbours; sign chosen so they point away from the centre line of the section
  for (let i = 0; i < all.length; i++) {
    const a = all[Math.max(0, i - 1)], b = all[Math.min(all.length - 1, i + 1)];
    let tx = b.x - a.x, tr = b.r - a.r; const l = Math.hypot(tx, tr) || 1; tx /= l; tr /= l;
    let nx = tr, nr = -tx;
    const cx = 0, cr = (Rout + Rrim) / 2;
    if ((all[i].x - cx) * nx + (all[i].r - cr) * nr < 0) { nx = -nx; nr = -nr; }
    all[i].nx = nx; all[i].nr = nr;
  }
  return all;
}

test('pure quads, watertight-open tyre grid, no degenerate faces', () => {
  const profile = sampleProfile();
  const mesh = buildTyreQuadMesh({ profile, cols: 96 });
  const rep = validateQuadMesh(mesh);
  assert.equal(rep.ok, true, rep.problems.join('; '));
  assert.equal(rep.faces, (profile.length - 1) * 96);
  assert.equal(rep.nonManifoldEdges, 0);
  assert.equal(rep.orientationErrors, 0);
  assert.equal(rep.degenerateFaces, 0);
  assert.equal(rep.boundaryLoops, 2, 'one loop at each bead');
  assert.equal(rep.boundaryEdges, 2 * 96);
  assert.equal(rep.genus, null);
  // open tube (cylinder topology): V - E + F = 0
  assert.equal(rep.euler, 0);
});

test('triangulated output has exactly 2 triangles per quad', () => {
  const mesh = buildTyreQuadMesh({ profile: sampleProfile(), cols: 32 });
  assert.equal(quadsToTriangles(mesh.quads).length / 3, mesh.quads.length / 4 * 2);
});

test('normals point outward (agree with profile normal in 3D)', () => {
  const profile = sampleProfile();
  const M = 48;
  const mesh = buildTyreQuadMesh({ profile, cols: M });
  let agree = 0, n = 0;
  for (let i = 0; i < profile.length; i++) {
    const p = profile[i];
    for (let j = 0; j < M; j++) {
      const th = j / M * Math.PI * 2, k = (i * M + j) * 3;
      // profile normal mapped to 3D: axial = nx, radial direction = (cos th, sin th)
      const expect = [p.nx, p.nr * Math.cos(th), p.nr * Math.sin(th)];
      const got = [mesh.normals[k], mesh.normals[k + 1], mesh.normals[k + 2]];
      const dot = expect[0] * got[0] + expect[1] * got[1] + expect[2] * got[2];
      if (dot > 0) agree++;
      n++;
    }
  }
  assert.ok(agree / n > 0.95, `normals agree with profile on ${(100 * agree / n).toFixed(1)}%`);
});

test('tread displacement keeps topology intact and changes geometry', () => {
  const profile = sampleProfile();
  const flat = buildTyreQuadMesh({ profile, cols: 96 });
  const pattern = buildTyreQuadMesh({
    profile, cols: 96,
    displace: (u, v, i, p) => (Math.abs(p.x) < 80 ? (Math.floor(u * 12) % 2 ? 4 : 0) : 0),
  });
  const rep = validateQuadMesh(pattern);
  assert.equal(rep.ok, true, rep.problems.join('; '));
  let moved = 0;
  for (let k = 0; k < flat.positions.length; k++) if (Math.abs(flat.positions[k] - pattern.positions[k]) > 1e-4) moved++;
  assert.ok(moved > 0);
});

test('validator flags a non-manifold / broken mesh', () => {
  const mesh = buildTyreQuadMesh({ profile: sampleProfile(), cols: 12 });
  // duplicate a face so its edges are shared by 3 faces
  const q = new Uint32Array(mesh.quads.length + 4);
  q.set(mesh.quads); q.set(mesh.quads.subarray(0, 4), mesh.quads.length);
  const rep = validateQuadMesh({ ...mesh, quads: q });
  assert.equal(rep.ok, false);
  assert.ok(rep.nonManifoldEdges > 0 || rep.orientationErrors > 0);
});

test('computeVertexNormals returns unit vectors', () => {
  const mesh = buildTyreQuadMesh({ profile: sampleProfile(), cols: 24 });
  const n = computeVertexNormals(mesh.positions, mesh.quads);
  for (let i = 0; i < n.length; i += 3) assert.ok(Math.abs(Math.hypot(n[i], n[i + 1], n[i + 2]) - 1) < 1e-4);
});
