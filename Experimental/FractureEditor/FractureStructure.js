//============================================================================================================================================
//                                                            FRACTURESTRUCTURE.JS
//============================================================================================================================================
// 📦 Closed convex partitioning, boundary-preserving triangulation and geometric verification.

import {
  Vector3,
  BufferGeometry,
  Float32BufferAttribute,
  IcosahedronGeometry,
  CylinderGeometry,
  ConeGeometry,
} from "three";
import {
  box,
  roughBlank,
} from "./SourceDepot/Fragmentation/src/core/convex.ts";
import { MATERIALS } from "./SourceDepot/Fragmentation/src/fracture/materials.ts";

export const Materials = MATERIALS;
export const Supported = [
  "cube",
  "sphere",
  "cylinder",
  "cone",
  "pane",
  "beam",
  "rock",
];
const Cross = (FirstPoint, SecondPoint, ThirdPoint) =>
  SecondPoint.clone().sub(FirstPoint).cross(ThirdPoint.clone().sub(FirstPoint));
export function Random(Seed) {
  let Accumulator = Seed >>> 0;
  return () => {
    Accumulator = (Math.imul(1664525, Accumulator) + 1013904223) >>> 0;
    return Accumulator / 4294967296;
  };
}
export function Bounds(Part) {
  const Minimum = new Vector3(Infinity, Infinity, Infinity),
    Maximum = Minimum.clone().negate();
  Part.vertices.forEach((Point) => {
    Minimum.min(Point);
    Maximum.max(Point);
  });
  return {
    min: Minimum,
    max: Maximum,
    size: Maximum.clone().sub(Minimum),
    center: Minimum.clone().add(Maximum).multiplyScalar(0.5),
  };
}
export function Normal(Part, Face) {
  const Result = new Vector3();
  for (let Slot = 0; Slot < Face.indices.length; Slot++) {
    const FirstPoint = Part.vertices[Face.indices[Slot]],
      SecondPoint =
        Part.vertices[Face.indices[(Slot + 1) % Face.indices.length]];
    Result.x += (FirstPoint.y - SecondPoint.y) * (FirstPoint.z + SecondPoint.z);
    Result.y += (FirstPoint.z - SecondPoint.z) * (FirstPoint.x + SecondPoint.x);
    Result.z += (FirstPoint.x - SecondPoint.x) * (FirstPoint.y + SecondPoint.y);
  }
  return Result.normalize();
}
export function Volume(Part) {
  const Origin = Part.vertices[0];
  let Result = 0;
  for (const Face of Part.faces)
    for (let Slot = 1; Slot + 1 < Face.indices.length; Slot++) {
      const [FirstPoint, SecondPoint, ThirdPoint] = [
        Face.indices[0],
        Face.indices[Slot],
        Face.indices[Slot + 1],
      ].map((Index) => Part.vertices[Index].clone().sub(Origin));
      Result += FirstPoint.dot(SecondPoint.cross(ThirdPoint)) / 6;
    }
  return Result;
}
function Convert(Part) {
  return {
    vertices: Part.verts.map((Point) => Point.clone()),
    faces: Part.faces.map((Polygon) => ({
      indices: [...Polygon.idx],
      interior: !!Polygon.interior,
    })),
  };
}
function FromGeometry(Geometry) {
  const Positions = Geometry.getAttribute("position"),
    Points = [],
    Lookup = new Map(),
    Remap = [];
  for (let Slot = 0; Slot < Positions.count; Slot++) {
    const Position = new Vector3().fromBufferAttribute(Positions, Slot),
      Key = Position.toArray()
        .map((Value) => Math.round(Value * 1e8))
        .join(",");
    if (!Lookup.has(Key)) {
      Lookup.set(Key, Points.length);
      Points.push(Position);
    }
    Remap.push(Lookup.get(Key));
  }
  const Indices = Geometry.index
      ? Array.from(Geometry.index.array)
      : Array.from({ length: Positions.count }, (_, Slot) => Slot),
    Faces = [];
  for (let Slot = 0; Slot < Indices.length; Slot += 3) {
    const Triangle = Indices.slice(Slot, Slot + 3).map((Index) => Remap[Index]);
    if (new Set(Triangle).size === 3)
      Faces.push({ indices: Triangle, interior: false });
  }
  Geometry.dispose();
  return { vertices: Points, faces: Faces };
}
export function CreateGeometry(Primitive, Scale = [1, 1, 1]) {
  let Part;
  if (Primitive === "cube") Part = Convert(box(1, 1, 1));
  else if (Primitive === "pane") Part = Convert(box(1.7, 1.15, 0.014));
  else if (Primitive === "beam") Part = Convert(box(1.6, 0.26, 0.12));
  else if (Primitive === "rock")
    Part = Convert(roughBlank(0.6, 13, Random(7), 0.85));
  else if (Primitive === "sphere")
    Part = FromGeometry(new IcosahedronGeometry(0.5, 3));
  else if (Primitive === "cylinder")
    Part = FromGeometry(new CylinderGeometry(0.5, 0.5, 1, 24, 1));
  else if (Primitive === "cone")
    Part = FromGeometry(new ConeGeometry(0.5, 1, 24, 1));
  else
    throw new Error(
      "This preview supports convex primitives only. Concave geometry such as a torus needs decomposition; its hole will not be filled with a convex proxy.",
    );
  Part.vertices.forEach((Point) => Point.multiply(new Vector3(...Scale)));
  const Center = Bounds(Part).center;
  for (const Face of Part.faces) {
    const FaceCenter = Face.indices
      .reduce((Sum, Slot) => Sum.add(Part.vertices[Slot]), new Vector3())
      .multiplyScalar(1 / Face.indices.length);
    if (Normal(Part, Face).dot(FaceCenter.sub(Center)) < 0)
      Face.indices.reverse();
  }
  // Rejoin coplanar triangle patches before clipping to avoid gratuitous cap-edge intersections.
  Part = MergeCoplanar(Part);
  Validate(Part);
  return Part;
}
function MergeCoplanar(Part) {
  const Groups = [];
  for (const Face of Part.faces) {
    const Direction = Normal(Part, Face),
      Distance = Direction.dot(Part.vertices[Face.indices[0]]);
    let Group = Groups.find(
      (PlaneGroup) =>
        PlaneGroup.normal.dot(Direction) > 1 - 1e-10 &&
        Math.abs(PlaneGroup.distance - Distance) < 1e-8,
    );
    if (!Group) {
      Group = { normal: Direction, distance: Distance, faces: [] };
      Groups.push(Group);
    }
    Group.faces.push(Face);
  }
  const Faces = [];
  for (const Group of Groups) {
    if (Group.faces.length === 1) {
      Faces.push(Group.faces[0]);
      continue;
    }
    const Edges = new Map();
    for (const Polygon of Group.faces)
      for (let Slot = 0; Slot < Polygon.indices.length; Slot++) {
        const StartIndex = Polygon.indices[Slot],
          EndIndex = Polygon.indices[(Slot + 1) % Polygon.indices.length];
        if (Edges.has(EndIndex + ":" + StartIndex))
          Edges.delete(EndIndex + ":" + StartIndex);
        else Edges.set(StartIndex + ":" + EndIndex, [StartIndex, EndIndex]);
      }
    const Next = new Map([...Edges.values()]),
      Start = Next.keys().next().value,
      Loop = [];
    let Current = Start;
    while (!Loop.includes(Current) && Next.has(Current)) {
      Loop.push(Current);
      Current = Next.get(Current);
    }
    if (Current === Start && Loop.length === Edges.size)
      Faces.push({ indices: Loop, interior: false });
    else Faces.push(...Group.faces);
  }
  const Used = [...new Set(Faces.flatMap((Polygon) => Polygon.indices))],
    Reindex = new Map(Used.map((Index, Slot) => [Index, Slot]));
  return {
    vertices: Used.map((Slot) => Part.vertices[Slot]),
    faces: Faces.map((Polygon) => ({
      ...Polygon,
      indices: Polygon.indices.map((Slot) => Reindex.get(Slot)),
    })),
  };
}
export function Split(Part, Direction, Distance) {
  const Tolerance = Bounds(Part).size.length() * 1e-9,
    Points = Part.vertices.map((Point) => Point.clone()),
    Distances = Points.map((Point) => Direction.dot(Point) - Distance);
  if (
    !Distances.some((Offset) => Offset > Tolerance) ||
    !Distances.some((Offset) => Offset < -Tolerance)
  )
    return null;
  const Crossings = new Map(),
    Rim = new Set(),
    Sides = [[], []];
  Distances.forEach((Offset, Slot) => {
    if (Math.abs(Offset) <= Tolerance) {
      Points[Slot].addScaledVector(Direction, -Offset);
      Distances[Slot] = 0;
      Rim.add(Slot);
    }
  });
  for (const Face of Part.faces) {
    const Polygons = [[], []];
    for (let Slot = 0; Slot < Face.indices.length; Slot++) {
      const StartIndex = Face.indices[Slot],
        EndIndex = Face.indices[(Slot + 1) % Face.indices.length],
        StartDistance = Distances[StartIndex],
        EndDistance = Distances[EndIndex];
      if (StartDistance >= 0) Polygons[0].push(StartIndex);
      if (StartDistance <= 0) Polygons[1].push(StartIndex);
      if (StartDistance * EndDistance < 0) {
        const Key =
          Math.min(StartIndex, EndIndex) + ":" + Math.max(StartIndex, EndIndex);
        let Index = Crossings.get(Key);
        if (Index === undefined) {
          Index = Points.length;
          const Point = Points[StartIndex].clone().lerp(
            Points[EndIndex],
            StartDistance / (StartDistance - EndDistance),
          );
          Point.addScaledVector(Direction, Distance - Direction.dot(Point));
          Points.push(Point);
          Crossings.set(Key, Index);
          Rim.add(Index);
        }
        Polygons.forEach((Polygon) => Polygon.push(Index));
      }
    }
    Polygons.forEach((Indices, Side) => {
      const Clean = Indices.filter(
        (Candidate, Slot) =>
          Candidate !== Indices[(Slot + Indices.length - 1) % Indices.length],
      );
      if (Clean.length >= 3) Sides[Side].push({ ...Face, indices: Clean });
    });
  }
  const Center = [...Rim]
      .reduce((Sum, Slot) => Sum.add(Points[Slot]), new Vector3())
      .multiplyScalar(1 / Rim.size),
    Tangent = new Vector3(
      Math.abs(Direction.x) < 0.8 ? 1 : 0,
      Math.abs(Direction.x) < 0.8 ? 0 : 1,
      0,
    )
      .cross(Direction)
      .normalize(),
    Bitangent = Direction.clone().cross(Tangent);
  const Ordered = [...Rim].sort((StartIndex, EndIndex) => {
    const StartPoint = Points[StartIndex].clone().sub(Center),
      EndPoint = Points[EndIndex].clone().sub(Center);
    return (
      Math.atan2(StartPoint.dot(Bitangent), StartPoint.dot(Tangent)) -
      Math.atan2(EndPoint.dot(Bitangent), EndPoint.dot(Tangent))
    );
  });
  if (Ordered.length < 3) return null;
  Sides[0].push({ indices: [...Ordered].reverse(), interior: true });
  Sides[1].push({ indices: Ordered, interior: true });
  return Sides.map((Faces) => {
    const Indices = [...new Set(Faces.flatMap((Polygon) => Polygon.indices))],
      MapIndex = new Map(Indices.map((Index, Slot) => [Index, Slot]));
    return {
      vertices: Indices.map((Slot) => Points[Slot].clone()),
      faces: Faces.map((Polygon) => ({
        ...Polygon,
        indices: Polygon.indices.map((Slot) => MapIndex.get(Slot)),
      })),
    };
  });
}
// Maximise the weakest triangle rather than fanning every face from one corner.
// All boundary vertices, including collinear seam vertices, remain in the triangulation.
export function Triangulate(Part, Face) {
  const Vertices = Face.indices.map((Begin) => Part.vertices[Begin]),
    Count = Vertices.length,
    Score = Array.from({ length: Count }, () => Array(Count).fill(-1)),
    Pivot = Array.from({ length: Count }, () => Array(Count).fill(-1));
  for (let Begin = 0; Begin + 1 < Count; Begin++) Score[Begin][Begin + 1] = 1;
  const Quality = (FirstPoint, SecondPoint, ThirdPoint) => {
    const Area = Cross(FirstPoint, SecondPoint, ThirdPoint).length();
    return (
      (2 * Math.sqrt(3) * Area) /
      (FirstPoint.distanceToSquared(SecondPoint) +
        SecondPoint.distanceToSquared(ThirdPoint) +
        ThirdPoint.distanceToSquared(FirstPoint))
    );
  };
  for (let Span = 2; Span < Count; Span++)
    for (let Begin = 0; Begin + Span < Count; Begin++) {
      const End = Begin + Span;
      for (let Middle = Begin + 1; Middle < End; Middle++) {
        const TriangleQuality = Quality(
          Vertices[Begin],
          Vertices[Middle],
          Vertices[End],
        );
        if (!Number.isFinite(TriangleQuality) || TriangleQuality < 1e-10)
          continue;
        const Candidate = Math.min(
          Score[Begin][Middle],
          Score[Middle][End],
          TriangleQuality,
        );
        if (Candidate > Score[Begin][End]) {
          Score[Begin][End] = Candidate;
          Pivot[Begin][End] = Middle;
        }
      }
    }
  const Triangles = [];
  function Emit(Begin, End) {
    if (End - Begin < 2) return;
    const Middle = Pivot[Begin][End];
    if (Middle < 0)
      throw new Error("Face cannot be triangulated without degeneracy");
    Triangles.push([
      Face.indices[Begin],
      Face.indices[Middle],
      Face.indices[End],
    ]);
    Emit(Begin, Middle);
    Emit(Middle, End);
  }
  Emit(0, Count - 1);
  let Center = new Vector3(),
    Area = 0;
  for (let Index = 1; Index + 1 < Count; Index++) {
    const Weight = Cross(
      Vertices[0],
      Vertices[Index],
      Vertices[Index + 1],
    ).length();
    Center.addScaledVector(
      Vertices[0]
        .clone()
        .add(Vertices[Index])
        .add(Vertices[Index + 1]),
      Weight / 3,
    );
    Area += Weight;
  }
  Center.multiplyScalar(1 / Area);
  const FanQuality = Math.min(
    ...Vertices.map((Point, Index) =>
      Quality(Point, Vertices[(Index + 1) % Count], Center),
    ),
  );
  if (FanQuality > Score[0][Count - 1] + 1e-8)
    return {
      triangles: Face.indices.map((Index, Slot) => [
        Index,
        Face.indices[(Slot + 1) % Count],
        Part.vertices.length,
      ]),
      quality: FanQuality,
      center: Center,
    };
  return { triangles: Triangles, quality: Score[0][Count - 1] };
}
export function Tessellate(Part) {
  const Points = [...Part.vertices],
    Faces = [];
  let Quality = 1;
  for (const Face of Part.faces) {
    const Result = Triangulate(Part, Face);
    Quality = Math.min(Quality, Result.quality);
    const Added = Points.length;
    if (Result.center) Points.push(Result.center);
    for (const Triangle of Result.triangles)
      Faces.push({
        indices: Triangle.map((Index) =>
          Result.center && Index === Part.vertices.length ? Added : Index,
        ),
        interior: !!Face.interior,
      });
  }
  return { vertices: Points, faces: Faces, quality: Quality };
}
export function Validate(Part) {
  const Surface = Tessellate(Part),
    Edges = new Map();
  for (const Face of Surface.faces)
    for (let Index = 0; Index < 3; Index++) {
      const StartIndex = Face.indices[Index],
        EndIndex = Face.indices[(Index + 1) % 3],
        Key =
          Math.min(StartIndex, EndIndex) + ":" + Math.max(StartIndex, EndIndex),
        Edge = Edges.get(Key) || { count: 0, balance: 0 };
      Edge.count++;
      Edge.balance += StartIndex < EndIndex ? 1 : -1;
      Edges.set(Key, Edge);
    }
  if (Surface.vertices.some((Point) => !Point.toArray().every(Number.isFinite)))
    throw new Error("Nonfinite geometry");
  if (
    [...Edges.values()].some((Edge) => Edge.count !== 2 || Edge.balance !== 0)
  )
    throw new Error("Open or inconsistently oriented fragment");
  const Size = Bounds(Part).size.length();
  if (!(Volume(Part) > Size ** 3 * 1e-13))
    throw new Error("Collapsed or reversed fragment");
  return {
    quality: Surface.quality,
    triangles: Surface.faces.length,
    volume: Volume(Part),
  };
}
export function Aspect(Part, SourceSize) {
  const Points = Part.vertices.map((Point) => Point.clone().divide(SourceSize));
  const Minimum = new Vector3(Infinity, Infinity, Infinity),
    Maximum = Minimum.clone().negate();
  Points.forEach((Point) => {
    Minimum.min(Point);
    Maximum.max(Point);
  });
  let Width = Infinity;
  for (const Face of Part.faces) {
    const Direction = Normal(Part, Face).multiply(SourceSize).normalize();
    const Projections = Points.map((Point) => Direction.dot(Point));
    Width = Math.min(
      Width,
      Math.max(...Projections) - Math.min(...Projections),
    );
  }
  return Maximum.sub(Minimum).length() / Width;
}
export function RenderGeometry(Part) {
  const Surface = Tessellate(Part),
    Geometry = new BufferGeometry(),
    Indices = [];
  Geometry.setAttribute(
    "position",
    new Float32BufferAttribute(
      Surface.vertices.flatMap((Point) => Point.toArray()),
      3,
    ),
  );
  for (const Interior of [false, true]) {
    const Start = Indices.length;
    for (const Face of Surface.faces)
      if (Face.interior === Interior) Indices.push(...Face.indices);
    Geometry.addGroup(Start, Indices.length - Start, Interior ? 1 : 0);
  }
  Geometry.setIndex(Indices);
  Geometry.computeVertexNormals();
  return Geometry;
}
export function Fracture(Root, Settings) {
  const Start = performance.now(),
    DrawRandom = Random(Settings.Seed),
    Material = MATERIALS[Settings.Material] || MATERIALS.concrete,
    Original = Volume(Root),
    QualityFloor = Math.min(0.018, Validate(Root).quality * 0.4),
    Size = Bounds(Root).size,
    Radius = Size.length(),
    Parts = [Root],
    Hit = new Vector3(Settings.X, Settings.Y, Settings.Z),
    Thin = Size.z < Math.min(Size.x, Size.y) * 0.1;
  let Energy = Settings.Energy + Material.storedEnergy * Original,
    Rejected = 0,
    Spent = 0;
  const Stopped = new Set();
  while (Parts.length < Settings.Ceiling) {
    let Best = -1,
      Priority = -1;
    Parts.forEach((Part, PieceSlot) => {
      if (Stopped.has(Part)) return;
      const WeightedVolume =
        Volume(Part) *
        Math.exp(-Bounds(Part).center.distanceTo(Hit) / (Radius * 0.65));
      if (WeightedVolume > Priority) {
        Priority = WeightedVolume;
        Best = PieceSlot;
      }
    });
    if (Best < 0 || Energy <= 0) break;
    const Part = Parts[Best],
      Extent = Bounds(Part),
      UncutVolume = Volume(Part);
    let Choice = null;
    for (let Trial = 0; Trial < 8; Trial++) {
      let Direction = new Vector3(
        DrawRandom() * 2 - 1,
        DrawRandom() * 2 - 1,
        Thin ? 0 : DrawRandom() * 2 - 1,
      ).normalize();
      if (Settings.Material === "wood") Direction.x *= 0.14;
      if (Trial === 7) {
        const Axes = (Thin ? ["x", "y"] : ["x", "y", "z"]).sort(
          (Axis, OtherAxis) =>
            (Extent.size[OtherAxis] / Size[OtherAxis]) *
              (Settings.Material === "wood" && OtherAxis === "x" ? 0.15 : 1) -
            (Extent.size[Axis] / Size[Axis]) *
              (Settings.Material === "wood" && Axis === "x" ? 0.15 : 1),
        );
        Direction.set(0, 0, 0);
        Direction[Axes[0]] = 1;
      }
      if (Thin && Settings.Material === "glass") {
        const Radial = Extent.center.clone().sub(Hit);
        Direction = new Vector3(-Radial.y, Radial.x, 0)
          .normalize()
          .lerp(Direction, 0.35)
          .normalize();
      }
      if (Direction.lengthSq() < 0.5) continue;
      Direction.normalize();
      const Projections = Part.vertices.map((Point) => Direction.dot(Point)),
        Low = Math.min(...Projections),
        High = Math.max(...Projections);
      const Offset =
        Direction.dot(Extent.center) +
        (DrawRandom() - 0.5) * (High - Low) * 0.18;
      const Halves = Split(Part, Direction, Offset);
      if (!Halves) continue;
      try {
        const Metrics = Halves.map(Validate),
          Volumes = Metrics.map((Measurement) => Measurement.volume),
          Ratio = Math.min(...Volumes) / UncutVolume;
        if (
          Ratio < 0.18 ||
          Math.abs(Volumes[0] + Volumes[1] - UncutVolume) >
            UncutVolume * 1e-7 ||
          Metrics.some((Measurement) => Measurement.quality < QualityFloor)
        ) {
          Rejected++;
          continue;
        }
        if (
          Halves.some((Half) => {
            const Span = Bounds(Half).size;
            return ["x", "y", "z"].some(
              (Axis) =>
                Span[Axis] < Math.min(Settings.MinimumSize, Size[Axis] * 0.3),
            );
          })
        ) {
          Rejected++;
          continue;
        }
        const Cap = Halves[0].faces.at(-1),
          CapOrigin = Halves[0].vertices[Cap.indices[0]];
        let Area = 0;
        for (let PieceSlot = 1; PieceSlot + 1 < Cap.indices.length; PieceSlot++)
          Area +=
            Cross(
              CapOrigin,
              Halves[0].vertices[Cap.indices[PieceSlot]],
              Halves[0].vertices[Cap.indices[PieceSlot + 1]],
            ).length() * 0.5;
        const Cost =
          2 *
          Material.Gc *
          Area *
          (Settings.Material === "wood" ? 1 + Math.abs(Direction.x) * 10 : 1);
        if (Cost > Energy) continue;
        const Slenderness = Math.max(
          ...Halves.map((Half) => Aspect(Half, Size)),
        );
        if (Slenderness > (Settings.Material === "wood" ? 10 : 6)) {
          Rejected++;
          continue;
        }
        const Score =
          (Math.sqrt(
            Math.min(...Metrics.map((Measurement) => Measurement.quality)) /
              Slenderness,
          ) *
            Math.min(...Volumes)) /
          Math.max(...Volumes);
        if (!Choice || Score > Choice.score)
          Choice = { halves: Halves, cost: Cost, score: Score };
      } catch {
        Rejected++;
      }
    }
    if (!Choice) {
      Stopped.add(Part);
      continue;
    }
    Energy -= Choice.cost;
    Spent += Choice.cost;
    Parts.splice(Best, 1, ...Choice.halves);
  }
  const Metrics = Parts.map(Validate),
    Total = Metrics.reduce((Sum, Measurement) => Sum + Measurement.volume, 0),
    Error = Math.abs(Total - Original) / Original;
  if (Error > 1e-7)
    throw new Error("Volume conservation failed; source geometry preserved");
  return {
    parts: Parts,
    milliseconds: performance.now() - Start,
    volumeError: Error,
    quality: Math.min(...Metrics.map((Measurement) => Measurement.quality)),
    triangles: Metrics.reduce(
      (Sum, Measurement) => Sum + Measurement.triangles,
      0,
    ),
    rejected: Rejected,
    spent: Spent,
  };
}
export function EncodeParts(Parts) {
  return Parts.map((Part) => {
    const Surface = Tessellate(Part);
    return {
      vertices: Surface.vertices.map((Point) => Point.toArray()),
      faces: Surface.faces,
    };
  });
}
export function DecodeParts(Parts) {
  return Parts.map((Part) => ({
    vertices: Part.vertices.map((Position) => new Vector3(...Position)),
    faces: Part.faces,
  }));
}
