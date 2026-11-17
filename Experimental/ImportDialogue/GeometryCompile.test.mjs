import test from "node:test";
import assert from "node:assert/strict";
import {
  GeometryBounds,
  SpatialReduce,
  TriangleClusters,
  BakeDetailLevels,
  UvStatistics,
} from "./GeometryCompile.mjs";
import {
  HousingGeometry,
  ReadSource,
  FileKind,
  AudioSample,
} from "./ImportSources.js";
const Sample = HousingGeometry();
test("built-in mechanical specimen has finite geometry and packed UVs", () => {
  assert(Sample.Positions.length / 9 > 5000);
  assert.equal(Sample.Positions.length, Sample.Normals.length);
  assert.equal(Sample.Uvs.length, (Sample.Positions.length / 3) * 2);
  assert([...Sample.Positions, ...Sample.Uvs].every(Number.isFinite));
  assert(UvStatistics(Sample.Uvs).Area < 1);
  assert.equal(UvStatistics(Sample.Uvs).Outside, 0);
});
test("bounds compute axis extents and the geometric centre", () => {
  assert.deepEqual(GeometryBounds([1, 2, 3, -1, 4, 0]), {
    Minimum: [-1, 2, 0],
    Maximum: [1, 4, 3],
    Size: [2, 2, 3],
    Centre: [0, 3, 1.5],
  });
  assert.deepEqual(GeometryBounds([]).Size, [0, 0, 0]);
});
test("cluster partition includes every triangle once and respects capacity", () => {
  const R = TriangleClusters(Sample, 64),
    Flat = R.Clusters.flatMap((C) => C.Triangles);
  assert.equal(Flat.length, Sample.Positions.length / 9);
  assert.equal(new Set(Flat).size, Flat.length);
  assert(R.Clusters.every((C) => C.Triangles.length <= 64));
  for (const C of R.Clusters)
    for (const T of C.Triangles) assert.equal(R.Ids[T], C.Id);
});
test("spatial reduction produces smaller finite geometry with measured displacement", () => {
  const R = SpatialReduce(Sample, 8, false);
  assert(R.Positions.length < Sample.Positions.length);
  assert(R.Positions.length > 0);
  assert.equal(R.Positions.length % 9, 0);
  assert.equal(R.Uvs.length, (R.Positions.length / 3) * 2);
  assert(R.Displacement > 0);
  assert([...R.Positions].every(Number.isFinite));
});
test("local bake yields real inspectable detail levels and actual progress", async () => {
  const Progress = [];
  const R = await BakeDetailLevels(
    Sample,
    { Levels: 4, Ratio: 0.5, Capacity: 128, UvAware: false },
    (V) => Progress.push(V),
  );
  assert.equal(R.length, 4);
  assert.equal(R[0].Triangles, Sample.Positions.length / 9);
  assert(R[3].Triangles < R[0].Triangles);
  assert(R.every((L) => L.Clusters.length === Math.ceil(L.Triangles / 128)));
  assert.deepEqual(Progress, [0.25, 0.5, 0.75, 1]);
});
test("bake cancellation aborts before publishing later levels", async () => {
  let Cancel = false;
  await assert.rejects(
    BakeDetailLevels(
      Sample,
      { Levels: 4 },
      () => {
        Cancel = true;
      },
      () => Cancel,
    ),
    /cancelled/,
  );
});
test("UV statistics distinguish missing, outside and degenerate coordinates", () => {
  assert.equal(UvStatistics(null).Triangles, 0);
  assert.equal(UvStatistics([0, 0, 1, 0, 0, 1]).Area, 0.5);
  assert.equal(UvStatistics([0, 0, 2, 0, 0, 1]).Outside, 1);
  assert.equal(UvStatistics([0, 0, 0, 0, 0, 0]).Degenerate, 1);
});
test("OBJ import decodes actual positions, normals and UVs", async () => {
  const F = new File(
    ["v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n"],
    "Triangle.obj",
  );
  const R = await ReadSource([F]);
  assert.equal(R.Kind, "Geometry");
  assert.equal(R.Geometry.Positions.length, 9);
  assert.equal(R.Geometry.Uvs.length, 6);
  assert.deepEqual(GeometryBounds(R.Geometry.Positions).Size, [1, 1, 0]);
});
test("arbitrary files are retained without pretending they have geometry", async () => {
  const R = await ReadSource([
    new File([new Uint8Array([1, 2, 3, 255])], "Capture.unknown"),
  ]);
  assert.equal(R.Kind, "Binary");
  assert.equal(R.Geometry, null);
  assert.equal(R.Hex, "01 02 03 ff");
  assert.equal(R.Files[0].size, 4);
});
test("document sources are inert text; type dispatch covers nongeometry formats", async () => {
  const R = await ReadSource([
    new File(["<script>throw 1</script>"], "Note.txt"),
  ]);
  assert.equal(R.Text, "<script>throw 1</script>");
  assert.equal(FileKind("Sky.EXR"), "Image");
  assert.equal(FileKind("Music.flac"), "Audio");
  assert.equal(FileKind("Inter.woff2"), "Font");
  assert.equal(FileKind("Clip.webm"), "Video");
  assert.equal(AudioSample().size, 144044);
});

test("undecodable geometry can be explicitly retained as a source-only import", async () => {
  const Source = new File(["v 0 0 0"], "Incomplete.obj");
  await assert.rejects(ReadSource([Source]), /No triangle geometry/);
  const Kept = await ReadSource([Source], true);
  assert.equal(Kept.Kind, "Binary");
  assert.equal(Kept.Geometry, null);
  assert.equal(Kept.Files[0], Source);
  assert.equal(Kept.Bytes, Source.size);
});
