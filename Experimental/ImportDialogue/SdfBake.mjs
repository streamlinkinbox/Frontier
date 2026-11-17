import { GeometryBounds } from "./GeometryCompile.mjs";
const Dot = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Sub = (A, B) => A.map((V, I) => V - B[I]);
const LengthSquared = (A) => Dot(A, A);
export function TriangleDistanceSquared(P, A, B, C) {
  const EdgeAB = Sub(B, A),
    EdgeAC = Sub(C, A),
    Cross = [
      EdgeAB[1] * EdgeAC[2] - EdgeAB[2] * EdgeAC[1],
      EdgeAB[2] * EdgeAC[0] - EdgeAB[0] * EdgeAC[2],
      EdgeAB[0] * EdgeAC[1] - EdgeAB[1] * EdgeAC[0],
    ];
  if (
    LengthSquared(Cross) <=
    1e-28 * Math.max(LengthSquared(EdgeAB) * LengthSquared(EdgeAC), 1e-28)
  ) {
    const Segment = (Start, End) => {
      const Edge = Sub(End, Start),
        Delta = Sub(P, Start),
        Length = LengthSquared(Edge),
        T = Length ? Math.max(0, Math.min(1, Dot(Delta, Edge) / Length)) : 0;
      return LengthSquared(Delta.map((V, I) => V - T * Edge[I]));
    };
    return Math.min(Segment(A, B), Segment(B, C), Segment(C, A));
  }
  const AB = EdgeAB,
    AC = EdgeAC,
    AP = Sub(P, A),
    D1 = Dot(AB, AP),
    D2 = Dot(AC, AP);
  if (D1 <= 0 && D2 <= 0) return LengthSquared(AP);
  const BP = Sub(P, B),
    D3 = Dot(AB, BP),
    D4 = Dot(AC, BP);
  if (D3 >= 0 && D4 <= D3) return LengthSquared(BP);
  const VC = D1 * D4 - D3 * D2;
  if (VC <= 0 && D1 >= 0 && D3 <= 0) {
    const V = D1 / (D1 - D3);
    return LengthSquared(AP.map((Q, I) => Q - V * AB[I]));
  }
  const CP = Sub(P, C),
    D5 = Dot(AB, CP),
    D6 = Dot(AC, CP);
  if (D6 >= 0 && D5 <= D6) return LengthSquared(CP);
  const VB = D5 * D2 - D1 * D6;
  if (VB <= 0 && D2 >= 0 && D6 <= 0) {
    const W = D2 / (D2 - D6);
    return LengthSquared(AP.map((Q, I) => Q - W * AC[I]));
  }
  const VA = D3 * D6 - D5 * D4;
  if (VA <= 0 && D4 - D3 >= 0 && D5 - D6 >= 0) {
    const W = (D4 - D3) / (D4 - D3 + D5 - D6);
    return LengthSquared(BP.map((Q, I) => Q - W * (C[I] - B[I])));
  }
  const Denominator = VA + VB + VC;
  if (Math.abs(Denominator) < 1e-24)
    return Math.min(LengthSquared(AP), LengthSquared(BP), LengthSquared(CP));
  const V = VB / Denominator,
    W = VC / Denominator;
  return LengthSquared(AP.map((Q, I) => Q - V * AB[I] - W * AC[I]));
}
function BoundsDistanceSquared(P, Bounds) {
  let D = 0;
  for (let A = 0; A < 3; A++) {
    const V = Math.max(Bounds.Minimum[A] - P[A], 0, P[A] - Bounds.Maximum[A]);
    D += V * V;
  }
  return D;
}
export function BuildDistanceTree(Positions) {
  if (!Positions?.length || Positions.length % 9)
    throw new Error("An SDF bake requires triangle geometry.");
  const Triangles = [];
  for (let I = 0; I < Positions.length; I += 9) {
    const A = Array.from(Positions.slice(I, I + 3)),
      B = Array.from(Positions.slice(I + 3, I + 6)),
      C = Array.from(Positions.slice(I + 6, I + 9));
    if ([...A, ...B, ...C].some((V) => !Number.isFinite(V)))
      throw new Error("Non-finite distance-field geometry.");
    Triangles.push({ A, B, C, Bounds: GeometryBounds([...A, ...B, ...C]) });
  }
  function Build(Ids) {
    const Minimum = [Infinity, Infinity, Infinity],
      Maximum = [-Infinity, -Infinity, -Infinity];
    for (const I of Ids)
      for (let A = 0; A < 3; A++) {
        Minimum[A] = Math.min(Minimum[A], Triangles[I].Bounds.Minimum[A]);
        Maximum[A] = Math.max(Maximum[A], Triangles[I].Bounds.Maximum[A]);
      }
    const Bounds = { Minimum, Maximum };
    if (Ids.length <= 12) return { Bounds, Ids };
    const Size = Minimum.map((V, A) => Maximum[A] - V),
      Axis = Size.indexOf(Math.max(...Size));
    Ids.sort(
      (A, B) =>
        Triangles[A].Bounds.Centre[Axis] - Triangles[B].Bounds.Centre[Axis],
    );
    const Mid = Math.floor(Ids.length / 2);
    return {
      Bounds,
      Left: Build(Ids.slice(0, Mid)),
      Right: Build(Ids.slice(Mid)),
    };
  }
  const Root = Build(Triangles.map((_, I) => I));
  return { Root, Triangles };
}
export function SampleDistance(Tree, P, Signed = false) {
  let Best = Infinity;
  function Search(Node) {
    if (BoundsDistanceSquared(P, Node.Bounds) > Best) return;
    if (Node.Ids) {
      for (const Id of Node.Ids) {
        const T = Tree.Triangles[Id];
        Best = Math.min(Best, TriangleDistanceSquared(P, T.A, T.B, T.C));
      }
      return;
    }
    const L = BoundsDistanceSquared(P, Node.Left.Bounds),
      R = BoundsDistanceSquared(P, Node.Right.Bounds);
    if (L < R) {
      Search(Node.Left);
      Search(Node.Right);
    } else {
      Search(Node.Right);
      Search(Node.Left);
    }
  }
  Search(Tree.Root);
  const Distance = Math.sqrt(Math.max(0, Best));
  if (!Signed || Distance === 0) return Distance;
  const Span = Math.max(
      ...Tree.Root.Bounds.Maximum.map(
        (V, A) => V - Tree.Root.Bounds.Minimum[A],
      ),
      1e-8,
    ),
    Epsilon = Span * 1e-8,
    Hits = [];
  // +X parity. A deterministic sub-voxel perturbation avoids rays through shared
  // edges. Coincident hits are deduplicated; overlapping solids still use parity,
  // not a boolean union. Open/non-manifold surfaces do not have a reliable sign.
  const Y = P[1] + Epsilon * 0.371,
    Z = P[2] + Epsilon * 0.613;
  function Ray(Node) {
    const B = Node.Bounds;
    if (
      B.Maximum[0] < P[0] ||
      Y < B.Minimum[1] ||
      Y > B.Maximum[1] ||
      Z < B.Minimum[2] ||
      Z > B.Maximum[2]
    )
      return;
    if (!Node.Ids) {
      Ray(Node.Left);
      Ray(Node.Right);
      return;
    }
    for (const Id of Node.Ids) {
      const { A, B, C } = Tree.Triangles[Id],
        By = B[1] - A[1],
        Bz = B[2] - A[2],
        Cy = C[1] - A[1],
        Cz = C[2] - A[2],
        Den = By * Cz - Bz * Cy;
      if (Math.abs(Den) < 1e-20) continue;
      const Py = Y - A[1],
        Pz = Z - A[2],
        U = (Py * Cz - Pz * Cy) / Den,
        V = (By * Pz - Bz * Py) / Den;
      if (U >= 0 && V >= 0 && U + V <= 1) {
        const X = A[0] + U * (B[0] - A[0]) + V * (C[0] - A[0]);
        if (X > P[0] + Epsilon) Hits.push(X);
      }
    }
  }
  Ray(Tree.Root);
  Hits.sort((A, B) => A - B);
  let Count = 0,
    Previous = -Infinity;
  for (const X of Hits) {
    if (X - Previous > Epsilon) {
      Count++;
      Previous = X;
    }
  }
  return Count % 2 ? -Distance : Distance;
}
export async function BakeDistanceField(
  Geometry,
  { Resolution = 24, Padding = 0.12, Signed = false } = {},
  Progress = () => {},
  Cancelled = () => false,
) {
  if (Cancelled()) throw new Error("SDF bake cancelled.");
  const Tree = BuildDistanceTree(Geometry.Positions),
    SourceBounds = GeometryBounds(Geometry.Positions),
    N = Math.max(8, Math.min(64, Math.round(Resolution))),
    Span =
      Math.max(...SourceBounds.Size, 1e-5) *
      (1 + 2 * Math.max(0.02, Math.min(0.5, Padding))),
    Minimum = SourceBounds.Centre.map((V) => V - Span / 2),
    Maximum = Minimum.map((V) => V + Span),
    Spacing = Span / N,
    Values = new Float32Array(N * N * N);
  let MinimumDistance = Infinity,
    MaximumDistance = -Infinity;
  for (let Z = 0; Z < N; Z++) {
    if (Cancelled()) throw new Error("SDF bake cancelled.");
    for (let Y = 0; Y < N; Y++)
      for (let X = 0; X < N; X++) {
        const P = [X, Y, Z].map((V, A) => Minimum[A] + (V + 0.5) * Spacing),
          D = SampleDistance(Tree, P, Signed);
        Values[(Z * N + Y) * N + X] = D;
        MinimumDistance = Math.min(MinimumDistance, D);
        MaximumDistance = Math.max(MaximumDistance, D);
      }
    Progress((Z + 1) / N);
    await new Promise((R) => setTimeout(R, 0));
  }
  return {
    Units: "metres",
    Layout: "X fastest, then Y, then Z; voxel-centred samples",
    Resolution: N,
    Bounds: {
      Minimum,
      Maximum,
      Size: [Span, Span, Span],
      Centre: SourceBounds.Centre,
    },
    Spacing,
    Values,
    Signed,
    MinimumDistance,
    MaximumDistance,
    Method: Signed
      ? "Closest triangle + deduplicated +X ray parity"
      : "Closest triangle, unsigned",
    SourceTriangles: Geometry.Positions.length / 9,
  };
}
