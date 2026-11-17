// Browser import reference: spatial vertex reduction and Morton-ordered triangle
// clusters. This is not Nanite, certified QEM error bounds or a runtime DAG.
export function GeometryBounds(Positions) {
  const Minimum = [Infinity, Infinity, Infinity],
    Maximum = [-Infinity, -Infinity, -Infinity];
  for (let I = 0; I < Positions.length; I += 3)
    for (let A = 0; A < 3; A++) {
      Minimum[A] = Math.min(Minimum[A], Positions[I + A]);
      Maximum[A] = Math.max(Maximum[A], Positions[I + A]);
    }
  if (!Positions.length)
    return {
      Minimum: [0, 0, 0],
      Maximum: [0, 0, 0],
      Size: [0, 0, 0],
      Centre: [0, 0, 0],
    };
  return {
    Minimum,
    Maximum,
    Size: Minimum.map((V, A) => Maximum[A] - V),
    Centre: Minimum.map((V, A) => (V + Maximum[A]) / 2),
  };
}
export function SpatialReduce(Source, Resolution = 32, UvAware = true) {
  const { Positions, Uvs } = Source,
    Bounds = GeometryBounds(Positions),
    Step = Math.max(...Bounds.Size, 1e-9) / Math.max(1, Resolution),
    Groups = new Map(),
    Remap = [],
    Sums = [];
  for (let I = 0; I < Positions.length / 3; I++) {
    const P = Array.from(Positions.slice(I * 3, I * 3 + 3)),
      Q = P.map((V, A) => Math.floor((V - Bounds.Minimum[A]) / Step)),
      Uv = Uvs?.slice(I * 2, I * 2 + 2);
    const Key =
      String(Source.MaterialIds?.[Math.floor(I / 3)] ?? 0) +
      "|" +
      Q.join(",") +
      (UvAware && Uv?.length === 2
        ? "|" +
          Array.from(Uv)
            .map((V) => Math.floor(V * Resolution))
            .join(",")
        : "");
    let Id = Groups.get(Key);
    if (Id === undefined) {
      Id = Sums.length;
      Groups.set(Key, Id);
      Sums.push({ P: [0, 0, 0], Uv: [0, 0], Count: 0 });
    }
    Remap.push(Id);
    const Sum = Sums[Id];
    P.forEach((V, A) => (Sum.P[A] += V));
    if (Uv?.length === 2) Uv.forEach((V, A) => (Sum.Uv[A] += V));
    Sum.Count++;
  }
  const Vertices = Sums.map((S) => S.P.map((V) => V / S.Count)),
    UvVertices = Sums.map((S) => S.Uv.map((V) => V / S.Count)),
    Output = [],
    OutputUv = [],
    OutputMaterialIds = [],
    Seen = new Set();
  let Displacement = 0;
  for (let I = 0; I < Remap.length; I++) {
    const P = Vertices[Remap[I]];
    Displacement = Math.max(
      Displacement,
      Math.hypot(...P.map((V, A) => V - Positions[I * 3 + A])),
    );
  }
  for (let I = 0; I < Remap.length; I += 3) {
    const Tri = Remap.slice(I, I + 3);
    if (new Set(Tri).size !== 3) continue;
    const [A, B, C] = Tri.map((Id) => Vertices[Id]),
      U = B.map((V, J) => V - A[J]),
      V = C.map((Q, J) => Q - A[J]);
    if (
      Math.hypot(
        U[1] * V[2] - U[2] * V[1],
        U[2] * V[0] - U[0] * V[2],
        U[0] * V[1] - U[1] * V[0],
      ) <
      Step * Step * 1e-10
    )
      continue;
    const Key = [...Tri].sort((A, B) => A - B).join(",");
    if (Seen.has(Key)) continue;
    Seen.add(Key);
    OutputMaterialIds.push(Source.MaterialIds?.[Math.floor(I / 3)] ?? 0);
    for (const Id of Tri) {
      Output.push(...Vertices[Id]);
      if (Uvs?.length) OutputUv.push(...UvVertices[Id]);
    }
  }
  return {
    ...Source,
    Normals: null,
    MaterialIds: new Uint16Array(OutputMaterialIds),
    Positions: new Float32Array(Output),
    Uvs: OutputUv.length ? new Float32Array(OutputUv) : null,
    Displacement,
  };
}
function MortonCode(X, Y, Z) {
  let Code = 0;
  for (let B = 0; B < 10; B++)
    Code |=
      (((X >> B) & 1) << (B * 3)) |
      (((Y >> B) & 1) << (B * 3 + 1)) |
      (((Z >> B) & 1) << (B * 3 + 2));
  return Code >>> 0;
}
export function TriangleClusters(Source, Capacity = 128) {
  Capacity = Math.max(16, Math.min(256, Math.round(Capacity)));
  const P = Source.Positions,
    Bounds = GeometryBounds(P),
    Rows = [];
  for (let I = 0; I < P.length; I += 9) {
    const Centre = [0, 1, 2].map(
        (A) => (P[I + A] + P[I + 3 + A] + P[I + 6 + A]) / 3,
      ),
      Q = Centre.map((V, A) =>
        Math.max(
          0,
          Math.min(
            1023,
            Math.floor(
              ((V - Bounds.Minimum[A]) / (Bounds.Size[A] || 1)) * 1023,
            ),
          ),
        ),
      );
    Rows.push({ Triangle: I / 9, Code: MortonCode(...Q) });
  }
  Rows.sort((A, B) => A.Code - B.Code || A.Triangle - B.Triangle);
  const Ids = new Uint32Array(Rows.length),
    Clusters = [];
  for (let I = 0; I < Rows.length; I += Capacity) {
    const Triangles = Rows.slice(I, I + Capacity).map((R) => R.Triangle),
      Coordinates = [];
    for (const T of Triangles) {
      Ids[T] = Clusters.length;
      for (let K = 0; K < 9; K++) Coordinates.push(P[T * 9 + K]);
    }
    Clusters.push({
      Id: Clusters.length,
      Triangles,
      Bounds: GeometryBounds(Coordinates),
    });
  }
  return { Ids, Clusters, Capacity };
}
export async function BakeDetailLevels(
  Source,
  { Levels = 4, Ratio = 0.5, Capacity = 128, UvAware = true } = {},
  Progress = () => {},
  Cancelled = () => false,
) {
  const Output = [];
  for (let I = 0; I < Levels; I++) {
    if (Cancelled()) throw new Error("Bake cancelled.");
    let Geometry = Source;
    if (I) {
      const Target = (Source.Positions.length / 9) * Math.pow(Ratio, I);
      for (let Grid = 48; Grid >= 2; Grid = Math.floor(Grid * 0.65)) {
        const Trial = SpatialReduce(Source, Grid, UvAware);
        if (
          Trial.Positions.length &&
          Trial.Positions.length <= Geometry.Positions.length
        )
          Geometry = Trial;
        if (Geometry.Positions.length / 9 <= Target) break;
      }
    }
    const Groups = TriangleClusters(Geometry, Capacity);
    Output.push({
      Level: I,
      Geometry,
      ...Groups,
      Triangles: Geometry.Positions.length / 9,
      Displacement: Geometry.Displacement || 0,
    });
    Progress((I + 1) / Levels);
    await new Promise((R) => setTimeout(R, 0));
  }
  return Output;
}
export function UvStatistics(Uvs) {
  if (!Uvs?.length) return { Triangles: 0, Outside: 0, Area: 0, Degenerate: 0 };
  let Area = 0,
    Outside = 0,
    Degenerate = 0;
  for (let I = 0; I < Uvs.length; I += 6) {
    const A = Uvs.slice(I, I + 2),
      B = Uvs.slice(I + 2, I + 4),
      C = Uvs.slice(I + 4, I + 6),
      Size =
        Math.abs(
          (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0]),
        ) / 2;
    Area += Size;
    if (Size < 1e-10) Degenerate++;
    if ([...A, ...B, ...C].some((V) => V < 0 || V > 1)) Outside++;
  }
  return { Triangles: Uvs.length / 6, Outside, Area, Degenerate };
}
