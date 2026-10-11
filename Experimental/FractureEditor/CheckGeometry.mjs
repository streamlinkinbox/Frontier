import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
const Folder = path.dirname(fileURLToPath(import.meta.url));
const Compiled = await build({
  entryPoints: [path.join(Folder, "FractureStructure.js")],
  bundle: true,
  format: "esm",
  platform: "node",
  write: false,
});
const Geometry = await import(
  "data:text/javascript;base64," +
    Buffer.from(Compiled.outputFiles[0].text).toString("base64")
);
const {
  CreateGeometry,
  Fracture,
  Bounds,
  Volume,
  Validate,
  Normal,
  Split,
  Aspect,
  EncodeParts,
  Tessellate,
  Materials,
  Random,
} = Geometry;
const Results = [],
  Errors = [];
// Independent triangle-edge receipt, including Euler characteristic and float32 upload closure.
function Inspect(Part) {
  const Surface = Tessellate(Part),
    Faces = Surface.faces.map((Face) => Face.indices),
    Used = new Set(Faces.flat()),
    Edges = new Map(),
    Points = Surface.vertices.map((Point) => Point.toArray().map(Math.fround)),
    Welded = new Set(Points.map((Point) => Point.join(",")));
  assert.equal(Welded.size, Points.length, "float32 vertex collapse");
  for (const Face of Faces)
    for (let Index = 0; Index < 3; Index++) {
      const A = Face[Index],
        B = Face[(Index + 1) % 3],
        Key = Math.min(A, B) + ":" + Math.max(A, B),
        Record = Edges.get(Key) || [];
      Record.push([A, B]);
      Edges.set(Key, Record);
    }
  for (const [Key, Incident] of Edges) {
    assert.equal(Incident.length, 2, "edge incidence " + Key);
    assert.equal(Incident[0][0], Incident[1][1], "opposite edge winding");
    assert.equal(Incident[0][1], Incident[1][0], "opposite edge winding");
  }
  assert.equal(
    Used.size - Edges.size + Faces.length,
    2,
    "closed genus-zero fragment",
  );
  const Centroid = Part.vertices
    .reduce(
      (Sum, Point) => Sum.add(Point),
      Part.vertices[0].clone().set(0, 0, 0),
    )
    .multiplyScalar(1 / Part.vertices.length);
  const Tolerance = Bounds(Part).size.length() * 1e-7;
  for (const Face of Part.faces) {
    const Direction = Normal(Part, Face),
      Distance = Direction.dot(Part.vertices[Face.indices[0]]);
    assert(Direction.dot(Centroid) < Distance + Tolerance, "outward normal");
    for (const Point of Part.vertices)
      assert(
        Direction.dot(Point) < Distance + Tolerance,
        "convex / planar partition",
      );
  }
  return Validate(Part);
}
function UploadVolume(Part) {
  const Surface = Tessellate(Part);
  return Volume({
    vertices: Surface.vertices.map((Point) =>
      Point.clone().set(...Point.toArray().map(Math.fround)),
    ),
    faces: Surface.faces,
  });
}
function Halfspaces(Part) {
  return Part.faces.map((Face) => {
    const Direction = Normal(Part, Face);
    return {
      Direction,
      Distance: Direction.dot(Part.vertices[Face.indices[0]]),
    };
  });
}
function Inside(Point, Planes) {
  return Planes.every(
    (Plane) => Plane.Direction.dot(Point) <= Plane.Distance + 1e-9,
  );
}
function Coverage(Root, Parts, Seed) {
  const Rand = Random(Seed),
    B = Bounds(Root),
    RootPlanes = Halfspaces(Root),
    FragmentPlanes = Parts.map(Halfspaces);
  let Occupied = 0;
  for (let Index = 0; Index < 450; Index++) {
    const Point = B.min
        .clone()
        .add(
          B.size.clone().multiply(B.min.clone().set(Rand(), Rand(), Rand())),
        ),
      Count = FragmentPlanes.filter((Planes) => Inside(Point, Planes)).length;
    assert.equal(
      Count,
      Inside(Point, RootPlanes) ? 1 : 0,
      "coverage / overlap mismatch",
    );
    if (Count) Occupied++;
  }
  assert(Occupied > 50);
  return Occupied;
}
try {
  for (const Primitive of [
    "cube",
    "sphere",
    "cylinder",
    "cone",
    "pane",
    "beam",
    "rock",
  ])
    for (const Material of Object.keys(Materials))
      for (const Seed of [1, 7, 42, 101]) {
        const Root = CreateGeometry(Primitive),
          Original = JSON.stringify(EncodeParts([Root])),
          Settings = {
            Material,
            Energy: 50000,
            Seed,
            Ceiling: 48,
            MinimumSize: 0.025,
            X: 0.13,
            Y: -0.12,
            Z: 0.005,
          };
        const Result = Fracture(Root, Settings);
        assert(Result.parts.length > 1);
        assert(Result.parts.length <= 48);
        Result.parts.forEach(Inspect);
        assert(Result.volumeError < 1e-8);
        assert.equal(
          JSON.stringify(EncodeParts([Root])),
          Original,
          "source mutation",
        );
        const MaximumAspect = Math.max(
          ...Result.parts.map((Part) => Aspect(Part, Bounds(Root).size)),
        );
        assert(
          MaximumAspect <= (Material === "wood" ? 10 : 6) + 1e-8,
          "fragment sliver gate",
        );
        const UploadVolumeError =
          Math.abs(
            Result.parts.reduce((Sum, Part) => Sum + UploadVolume(Part), 0) -
              UploadVolume(Root),
          ) / UploadVolume(Root);
        assert(UploadVolumeError < 2e-6, "float32 upload volume");
        const Samples = Coverage(Root, Result.parts, Seed);
        Results.push({
          Primitive,
          Material,
          Seed,
          Count: Result.parts.length,
          VolumeError: Result.volumeError,
          MinimumTriangleQuality: Result.quality,
          MaximumAspect,
          Samples,
          UploadVolumeError,
        });
      }
  // Cuts through vertices, complete edges, near faces and triangulated cap centres.
  for (const Primitive of ["cube", "sphere", "cylinder", "cone", "pane"]) {
    const Root = CreateGeometry(Primitive),
      Origin = Root.vertices[0].clone().set(0, 0, 0);
    for (const Coordinates of [
      [1, 0, 0],
      [0, 1, 0],
      [0, 0, 1],
      [1, 1, 0],
      [1, 1, 1],
    ]) {
      const Direction = Origin.clone()
        .set(...Coordinates)
        .normalize();
      for (const Distance of [
        0,
        1e-10,
        -1e-10,
        Direction.dot(Root.vertices[0]),
      ]) {
        const Pair = Split(Root, Direction, Distance);
        if (!Pair) continue;
        Pair.forEach(Inspect);
        assert(
          Math.abs(Pair.reduce((Sum, P) => Sum + Volume(P), 0) - Volume(Root)) /
            Volume(Root) <
            1e-7,
        );
      }
    }
  }
  for (const Scale of [
    [0.001, 0.001, 0.001],
    [100, 100, 100],
    [2, 0.5, 1.3],
  ]) {
    const Root = CreateGeometry("sphere", Scale),
      Result = Fracture(Root, {
        Material: "glass",
        Energy: 50000,
        Seed: 9,
        Ceiling: 32,
        MinimumSize: 0.002,
        X: 0,
        Y: 0,
        Z: 0,
      });
    Result.parts.forEach(Inspect);
    Coverage(Root, Result.parts, 9);
  }
  const Root = CreateGeometry("sphere"),
    Settings = {
      Material: "glass",
      Energy: 2500,
      Seed: 42,
      Ceiling: 48,
      MinimumSize: 0.045,
      X: 0,
      Y: 0,
      Z: 0,
    };
  assert.deepEqual(
    EncodeParts(Fracture(Root, Settings).parts),
    EncodeParts(Fracture(Root, Settings).parts),
    "deterministic seed",
  );
  assert.equal(
    Fracture(Root, { ...Settings, Material: "concrete", Energy: 0 }).parts
      .length,
    1,
    "zero energy",
  );
  assert.throws(
    () => CreateGeometry("torus"),
    /convex primitives only/,
    "no silent convex hull for concave objects",
  );
  console.log(
    "Geometry checks passed:",
    Results.length,
    "material / primitive / seed cases plus exact-plane, scaling, determinism and refusal checks.",
  );
} catch (Error) {
  Errors.push(Error.stack);
  console.error(Error);
  process.exitCode = 1;
}
fs.writeFileSync(
  path.join(Folder, "Captures/GeometryChecks.json"),
  JSON.stringify(
    {
      Revision: 2,
      Cases: Results.length,
      MinimumTriangleQuality: Math.min(
        ...Results.map((Result) => Result.MinimumTriangleQuality),
      ),
      MaximumRelativeVolumeError: Math.max(
        ...Results.map((Result) => Result.VolumeError),
      ),
      MaximumFloat32VolumeError: Math.max(
        ...Results.map((Result) => Result.UploadVolumeError),
      ),
      Results,
      Errors,
    },
    null,
    2,
  ) + "\n",
);
