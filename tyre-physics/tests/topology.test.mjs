// Unit tests for the tyre topology (pure JS). Run with: node --test tyre-physics/tests
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { buildTyreTopology, buildProfileLoop, TYRE_GEOMETRY } from '../src/tyre/topology.js';

const topo = buildTyreTopology({ NP: 48, NC: 32 });

test('vertex and triangle counts', () => {
  assert.equal(topo.N, 48 * 32);
  assert.equal(topo.T, 2 * topo.N);
  assert.equal(topo.E, 6 * topo.N);
});

test('closed torus: Euler characteristic V - E + F = 0 on the triangulation', () => {
  // triangulation edges are the U, V and diagonal classes: 3 per vertex
  const V = topo.N;
  const Ef = 3 * topo.N;
  const F = topo.T;
  assert.equal(V - Ef + F, 0);
});

test('every vertex touches exactly six triangles, and slots point back to it', () => {
  for (let v = 0; v < topo.N; v++) {
    for (let s = 0; s < 6; s++) {
      const slot = topo.vtri[6 * v + s];
      const tri = slot >>> 2;
      const loc = slot & 3;
      assert.equal(topo.tris[4 * tri + loc], v, `vertex ${v} slot ${s}`);
    }
  }
});

test('all triangle edges are present among constraint edges (no missing shear/stretch)', () => {
  const set = new Set();
  for (let e = 0; e < topo.E; e++) {
    const a = topo.edges[2 * e], b = topo.edges[2 * e + 1];
    set.add(a < b ? `${a}_${b}` : `${b}_${a}`);
  }
  for (let k = 0; k < topo.T; k++) {
    const t = [topo.tris[4 * k], topo.tris[4 * k + 1], topo.tris[4 * k + 2]];
    for (let l = 0; l < 3; l++) {
      const a = t[l], b = t[(l + 1) % 3];
      assert.ok(set.has(a < b ? `${a}_${b}` : `${b}_${a}`));
    }
  }
});

test('edge colouring is valid: edges of one colour never share a vertex', () => {
  for (const { start, count } of topo.colorRanges) {
    const seen = new Set();
    for (let e = start; e < start + count; e++) {
      const a = topo.edges[2 * e], b = topo.edges[2 * e + 1];
      assert.ok(!seen.has(a) && !seen.has(b), `colour clash at edge ${e}`);
      seen.add(a);
      seen.add(b);
    }
  }
  const covered = topo.colorRanges.reduce((s, r) => s + r.count, 0);
  assert.equal(covered, topo.E);
  assert.ok(topo.colourCount <= 24, `colour count ${topo.colourCount} unexpectedly large`);
});

test('rest edge lengths are positive and of tyre-scale size', () => {
  let min = Infinity, max = 0;
  for (let e = 0; e < topo.E; e++) {
    min = Math.min(min, topo.edgeRest[e]);
    max = Math.max(max, topo.edgeRest[e]);
  }
  assert.ok(min > 0.001, `min rest length ${min}`);
  assert.ok(max < 0.12, `max rest length ${max}`);
});

test('triangle orientation is outward: signed volume is positive', () => {
  assert.ok(topo.restVolume > 0);
});

test('rest volume matches Pappus (2*pi*centroid_r*area) for the profile loop', () => {
  // Shoelace area and centroid of the closed profile (r, z) polygon, using the dense loop.
  const { profile } = buildProfileLoop(256, 48);
  let A = 0, Cr = 0;
  for (let k = 0; k < profile.length; k++) {
    const p = profile[k], q = profile[(k + 1) % profile.length];
    const cross = p.r * q.z - q.r * p.z;
    A += cross;
    Cr += (p.r + q.r) * cross;
  }
  A *= 0.5;
  const centroidR = Cr / (6 * A);
  const pappus = 2 * Math.PI * centroidR * Math.abs(A);
  const dense = buildTyreTopology({ NP: 96, NC: 64 });
  const rel = Math.abs(dense.restVolume - pappus) / pappus;
  assert.ok(rel < 0.02, `mesh volume ${dense.restVolume} vs Pappus ${pappus} (rel ${rel})`);
});

test('bead seat particles lie exactly on the rim radius and form a full ring', () => {
  const rr = TYRE_GEOMETRY.rimRadius;
  assert.ok(topo.K > 0);
  for (let k = 0; k < topo.K; k++) {
    const v = topo.attachVertices[k];
    const x = topo.pos[3 * v], y = topo.pos[3 * v + 1];
    assert.ok(Math.abs(Math.hypot(x, y) - rr) < 1e-6);
  }
  // every ring (psi index) carries the same number of seat particles
  const perRing = topo.K / topo.NP;
  assert.equal(Number.isInteger(perRing), true);
});

test('bead seat spans both bead seats (within one arc-length sample of the corners)', () => {
  const zs = [];
  for (let k = 0; k < topo.K; k++) zs.push(topo.pos[3 * topo.attachVertices[k] + 2]);
  const sample = topo.perimeter / topo.NC; // arc-length spacing bound
  assert.ok(Math.min(...zs) < -0.075 + sample + 1e-6, `left seat edge ${Math.min(...zs)}`);
  assert.ok(Math.max(...zs) > 0.075 - 1e-6, `right seat edge ${Math.max(...zs)}`);
});
