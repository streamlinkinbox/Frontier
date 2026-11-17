import test from "node:test";
import assert from "node:assert/strict";
import * as THREE from "three";
import {
  TriangleDistanceSquared,
  BuildDistanceTree,
  SampleDistance,
  BakeDistanceField,
} from "./SdfBake.mjs";
import { HousingGeometry } from "./ImportSources.js";
import { SpatialReduce } from "./GeometryCompile.mjs";
const Box = new THREE.BoxGeometry(2, 2, 2).toNonIndexed(),
  Geometry = { Positions: Box.attributes.position.array };
test("closest triangle distance covers face, edge, vertex and collapsed triangles", () => {
  const A = [0, 0, 0],
    B = [1, 0, 0],
    C = [0, 1, 0];
  assert.equal(TriangleDistanceSquared([0.2, 0.2, 2], A, B, C), 4);
  assert.equal(TriangleDistanceSquared([1, 1, 0], A, B, C), 0.5);
  assert.equal(TriangleDistanceSquared([-1, -1, 0], A, B, C), 2);
  assert.equal(TriangleDistanceSquared([0.5, 1, 0], A, A, B), 1);
  assert.equal(TriangleDistanceSquared([1, 0, 0], A, A, A), 1);
});
test("closed box ray parity has negative inside, positive outside, zero surface", () => {
  const Tree = BuildDistanceTree(Geometry.Positions);
  assert.equal(SampleDistance(Tree, [0, 0, 0], true), -1);
  assert.equal(SampleDistance(Tree, [0, 0, 0]), 1);
  assert.equal(SampleDistance(Tree, [2, 0, 0], true), 1);
  assert.equal(SampleDistance(Tree, [1, 0, 0], true), 0);
  assert(
    Math.abs(SampleDistance(Tree, [2, 2, 2], true) - Math.sqrt(3)) < 1e-10,
  );
});
test("distance tree validates missing/incomplete/nonfinite triangles", () => {
  assert.throws(() => BuildDistanceTree([]), /triangle/);
  assert.throws(() => BuildDistanceTree([0]), /triangle/);
  assert.throws(
    () => BuildDistanceTree([NaN, 0, 0, 0, 1, 0, 1, 0, 0]),
    /Non-finite/,
  );
});
test("BVH nearest distances agree with brute force over many triangles", () => {
  const P = HousingGeometry().Positions,
    Tree = BuildDistanceTree(P);
  for (const Q of [
    [0, 0, 0],
    [3, 2, 1],
    [-0.4, 0.15, 0.03],
  ]) {
    let Expected = Infinity;
    for (let I = 0; I < P.length; I += 9)
      Expected = Math.min(
        Expected,
        TriangleDistanceSquared(
          Q,
          Array.from(P.slice(I, I + 3)),
          Array.from(P.slice(I + 3, I + 6)),
          Array.from(P.slice(I + 6, I + 9)),
        ),
      );
    assert(Math.abs(SampleDistance(Tree, Q) - Math.sqrt(Expected)) < 1e-9);
  }
});
test("volume writes voxel-centred finite float32 samples, cubic bounds and progress", async () => {
  const Progress = [],
    V = await BakeDistanceField(
      Geometry,
      { Resolution: 8, Signed: true, Padding: 0.1 },
      (P) => Progress.push(P),
    );
  assert(V.Values instanceof Float32Array);
  assert.equal(V.Values.length, 512);
  assert.equal(V.Spacing, 0.3);
  assert.deepEqual(V.Bounds.Minimum, [-1.2, -1.2, -1.2]);
  assert.equal(Progress.length, 8);
  assert.equal(Progress.at(-1), 1);
  assert(V.MinimumDistance < 0);
  assert(V.MaximumDistance > 0);
  assert(Array.from(V.Values).every(Number.isFinite));
  const At = (X, Y, Z) => V.Values[(Z * 8 + Y) * 8 + X];
  assert(Math.abs(At(3, 3, 3) + 0.85) < 1e-6);
  const Unsigned = await BakeDistanceField(Geometry, { Resolution: 8 });
  assert(Unsigned.MinimumDistance >= 0);
});
test("volume cancellation stops before publishing a complete field", async () => {
  let Cancel = false,
    Calls = 0;
  await assert.rejects(
    BakeDistanceField(
      Geometry,
      { Resolution: 8 },
      () => {
        Calls++;
        Cancel = true;
      },
      () => Cancel,
    ),
    /cancelled/,
  );
  assert.equal(Calls, 1);
});
test("material assignments remain aligned through geometry reduction", () => {
  const Source = HousingGeometry();
  assert.equal(Source.Materials.length, 3);
  assert.equal(Source.MaterialIds.length, Source.Positions.length / 9);
  assert.deepEqual([...new Set(Source.MaterialIds)].sort(), [0, 1, 2]);
  for (const Resolution of [8, 16]) {
    const Reduced = SpatialReduce(Source, Resolution, false);
    assert.equal(Reduced.MaterialIds.length, Reduced.Positions.length / 9);
    assert.equal(Reduced.Materials, Source.Materials);
    assert.equal(Reduced.Normals, null);
    assert(Array.from(Reduced.MaterialIds).every((I) => I < 3));
  }
});
