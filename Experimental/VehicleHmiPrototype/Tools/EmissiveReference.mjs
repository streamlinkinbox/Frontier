//============================================================================================================================================
//                                                          EMISSIVEREFERENCE.MJS
//============================================================================================================================================
// 🧩 The CPU twin of RayTraversal.wgsl's SampleEmissiveLight / EvaluateEmissiveBsdfWeight, written against the very records EmissiveLightSpace.js produces. The unit tests use it to
//    show the algorithm is unbiased; EmissiveParityGpu.mjs runs the real WGSL beside it to show the shader is this algorithm.

export const Slot = (Words, Index) => { const Pair = Words[Index >> 1]; return (Index & 1) ? (Pair >>> 16) & 0x7FFF : Pair & 0x7FFF; };
export const EmptySlot = 0x7FFF;
export const Luminance = (E) => 0.2126 * E[0] + 0.7152 * E[1] + 0.0722 * E[2];
const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Dot = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Length = (A) => Math.hypot(A[0], A[1], A[2]);

// 📐 Mirror of EvaluateEmissiveBsdfWeight.
export function BsdfWeight(BsdfPdf, Distance, CosineAtEmitter)
{
    const DistSq = Math.max(Distance * Distance, 0.04), Geom = Math.max(CosineAtEmitter, 0.05) / DistSq;
    return Math.min(1.0, Math.max(0.15, (BsdfPdf * BsdfPdf) / Math.max(BsdfPdf * BsdfPdf + Geom * Geom * 0.01, 1.0e-5)));
}

// 📐 Mirror of SampleEmissiveLight. Space: an EmissiveLightSpace after Update; Words: the cell's four u32; R: four uniforms; Occupants[i].Transform: column-major;
//    Materials[family].Emission: rgb.
export function SampleEmissiveLight(Space, Occupants, Materials, EmissionScale, Position, Words, R)
{
    const Result = { Valid: false };
    const Weights = []; let Total = 0.0;
    for (let Index = 0; Index < 8; ++Index)
    {
        const Id = Slot(Words, Index);
        if (Id === EmptySlot) break;
        const O = Id * 8, F = Space.ClusterFloats;
        const To = [F[O] - Position[0], F[O + 1] - Position[1], F[O + 2] - Position[2]];
        const Weight = Math.max(F[O + 7], 0.0) / (Dot(To, To) + F[O + 3] * F[O + 3] + 1.0e-4);
        Weights.push(Weight); Total += Weight;
    }
    if (Weights.length === 0 || !(Total > 0.0)) return Result;
    let Chosen = Weights.length - 1, Running = 0.0;
    const Pick = R[0] * Total;
    for (let Index = 0; Index < Weights.length; ++Index) { Running += Weights[Index]; if (Pick < Running) { Chosen = Index; break; } }
    const ClusterProbability = Weights[Chosen] / Total;
    const Id = Slot(Words, Chosen), O = Id * 8;
    const OccupantIndex = Space.ClusterWords[O + 4], First = Space.ClusterWords[O + 5], Number = Space.ClusterWords[O + 6];
    const Triangle = (Index) =>
    {
        const T = (First + Index) * 12, F = Space.TriangleFloats;
        return { A: [F[T], F[T + 1], F[T + 2]], E1: [F[T + 4], F[T + 5], F[T + 6]], Area: F[T + 7], E2: [F[T + 8], F[T + 9], F[T + 10]], Family: Space.TriangleWords[T + 11] };
    };
    const WeightOf = (T) => T.Area * Luminance(Materials[T.Family].Emission);
    let TriangleTotal = 0.0;
    for (let Index = 0; Index < Number; ++Index) TriangleTotal += WeightOf(Triangle(Index));
    if (!(TriangleTotal > 0.0)) return Result;
    let Selected = Triangle(Number - 1), SelectedWeight = WeightOf(Selected), Cumulative = 0.0;
    const TrianglePick = R[1] * TriangleTotal;
    for (let Index = 0; Index < Number; ++Index)
    {
        const Candidate = Triangle(Index), Weight = WeightOf(Candidate);
        Cumulative += Weight;
        if (TrianglePick < Cumulative) { Selected = Candidate; SelectedWeight = Weight; break; }
    }
    const Root = Math.sqrt(R[2]), B1 = Root * (1.0 - R[3]), B2 = Root * R[3];
    const M = Occupants[OccupantIndex].Transform;
    const Apply = (V, W) => [M[0] * V[0] + M[4] * V[1] + M[8] * V[2] + W * M[12], M[1] * V[0] + M[5] * V[1] + M[9] * V[2] + W * M[13], M[2] * V[0] + M[6] * V[1] + M[10] * V[2] + W * M[14]];
    const Point = Apply([Selected.A[0] + Selected.E1[0] * B1 + Selected.E2[0] * B2, Selected.A[1] + Selected.E1[1] * B1 + Selected.E2[1] * B2, Selected.A[2] + Selected.E1[2] * B1 + Selected.E2[2] * B2], 1);
    const Edge1 = Apply(Selected.E1, 0), Edge2 = Apply(Selected.E2, 0), Normal2 = Cross(Edge1, Edge2), Area = 0.5 * Length(Normal2);
    if (!(Area > 1.0e-9)) return Result;
    const Emission = Materials[Selected.Family].Emission;
    return { Valid: true, Position: Point, Normal: Normal2.map((V) => V / (2.0 * Area)), Radiance: Emission.map((V) => V * EmissionScale),
             Pdf: ClusterProbability * (SelectedWeight / TriangleTotal) / Area, Cluster: Id, Occupant: OccupantIndex };
}

// 📝 The cell's words for a world position from the space's grid (a mirror of ResolveEmissiveCell, without the feature gates), or null outside the grid.
export function CellWords(Space, Bounds, Position)
{
    const [Min, Ext, Res] = [Bounds.Minimum, Bounds.Extent, Bounds.Resolution];
    const Local = [0, 1, 2].map((Axis) => (Position[Axis] - Min[Axis]) / Ext[Axis]);
    if (Local.some((V) => V < 0 || V >= 1)) return null;
    const Cell = Local.map((V, Axis) => Math.min(Math.floor(V * Res[Axis]), Res[Axis] - 1));
    const Index = Cell[0] + Res[0] * (Cell[1] + Res[1] * Cell[2]);
    const Words = [0, 1, 2, 3].map((K) => Space.Cells[Index * 4 + K]);
    return Words;
}

// 📐 Mirror of IsListedEmitter: is the BVH triangle `Triangle` of occupant `Identity` in a cluster the cell lists?
export function IsListedEmitter(Space, Words, Identity, Triangle)
{
    if (!Words) return false;
    for (let Index = 0; Index < 8; ++Index)
    {
        const Id = Slot(Words, Index);
        if (Id === EmptySlot) break;
        if (Space.ClusterWords[Id * 8 + 4] !== Identity) continue;
        const First = Space.ClusterWords[Id * 8 + 5], Count = Space.ClusterWords[Id * 8 + 6];
        for (let K = 0; K < Count; ++K) if (Space.TriangleWords[(First + K) * 12 + 3] === Triangle) return true;
    }
    return false;
}
