//============================================================================================================================================
//                                                          GEOMETRYHIERARCHY.JS
//============================================================================================================================================
// 🧩 Virtualized geometry build: UV- and sharp-edge-aware weld, adjacency meshlets, boundary/seam/crease-locked QEM into a cluster DAG.

// 📝 Anything arriving as a triangle soup is accepted — authored meshes with UV charts and marked sharp edges,
//    heightmap-generated terrain, and voxelized-SDF-converted cliff meshes alike. Positions weld by exact 3D
//    coordinates so the full manifold adjacency is visible across UV seams and hard creases, while per-corner
//    UV wedges (CornerUVs) and per-triangle sharp-edge masks (SharpMask) travel with the triangles through
//    every level. During QEM simplification:
//      • Group borders, open borders and material-family borders lock 3D position and corner UVs bit-exact.
//      • UV seam edges and marked sharp edges add 3D line-alignment quadrics; degree-≠2 feature corners pin,
//        while degree-2 seam/sharp vertices may only collapse ALONG their feature curve, interpolating both
//        charts' UVs at the shared parameter t and rejecting any collapse that would fold a 2D UV triangle.
//    Every cluster node also records its group-coherent ParentError and ParentSphere so Phase 2's runtime cut
//    (SelfProjectedError <= Threshold < ParentProjectedError) can be evaluated independently per cluster.

const DefaultTriangleLimit     = 128;   // [-] triangles per meshlet
const DefaultVertexLimit       = 256;   // [-] unique vertices per meshlet
const DefaultGroupLimit        = 8;     // [-] child clusters per simplification group
const DefaultTargetRatio       = 0.5;   // [-] triangles kept per simplification pass
const DefaultMaxLevels         = 32;    // [-]
const DefaultSharpNormalCosine = Math.cos((45 * Math.PI) / 180);   // [-] corner-normal crease threshold
const UvMatchTolerance         = 1e-5;  // [-] UV wedge identity tolerance

//------------------------------------------------------------------------------------------------------------------------
//                                                     SMALL VECTOR SUBSTRATE
//------------------------------------------------------------------------------------------------------------------------

function Subtract(A, B) { return [A[0] - B[0], A[1] - B[1], A[2] - B[2]]; }
function Cross(A, B) { return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]]; }
function Dot(A, B) { return A[0] * B[0] + A[1] * B[1] + A[2] * B[2]; }
function Length(A) { return Math.hypot(A[0], A[1], A[2]); }
function Normalize(A) { const L = Length(A); return L > 0 ? [A[0] / L, A[1] / L, A[2] / L] : [0, 0, 1]; }
function ReadPosition(Positions, Vertex) { return [Positions[Vertex * 3], Positions[Vertex * 3 + 1], Positions[Vertex * 3 + 2]]; }
function ReadCornerUv(CornerUVs, Triangle, Corner)
{
    const Base = Triangle * 6 + Corner * 2;
    return [CornerUVs[Base], CornerUVs[Base + 1]];
}
function UvDistance(A, B) { return Math.hypot(A[0] - B[0], A[1] - B[1]); }
function UvSignedArea2(U0, U1, U2)
{
    return (U1[0] - U0[0]) * (U2[1] - U0[1]) - (U1[1] - U0[1]) * (U2[0] - U0[0]);
}

function TriangleNormal(Positions, Triangles, Triangle)
{
    const A = ReadPosition(Positions, Triangles[Triangle * 3]);
    const B = ReadPosition(Positions, Triangles[Triangle * 3 + 1]);
    const C = ReadPosition(Positions, Triangles[Triangle * 3 + 2]);
    return Cross(Subtract(B, A), Subtract(C, A));   // 📝 unnormalized: length is twice the area
}

function OrthogonalBasis(Tangent)
{
    const Axis = Math.abs(Tangent[0]) < 0.8 ? [1, 0, 0] : [0, 1, 0];
    const U = Normalize(Cross(Tangent, Axis));
    const V = Normalize(Cross(Tangent, U));
    return [U, V];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WELD AND ADJACENCY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Soup corners weld on exact 3D position bits so geometric connectivity across UV seams and hard edges is
//    preserved. Per-corner UVs and marked sharp edges are extracted into parallel per-triangle arrays.
export function WeldFacetSoup(Source, Options = {})
{
    const Positions = [], Triangles = [], Families = [], CornerUVs = [], SharpMask = [], CornerNormals = [];
    const Welded = new Map();
    let HasUVs = false, HasCornerNormals = false;
    const PositionKey = (P) => (P[0] + 0) + "," + (P[1] + 0) + "," + (P[2] + 0);
    const Weld = (P) =>
    {
        const Key = PositionKey(P);
        let Vertex = Welded.get(Key);
        if (Vertex === undefined)
        {
            Vertex = Positions.length / 3;
            Welded.set(Key, Vertex);
            Positions.push(P[0], P[1], P[2]);
        }
        return Vertex;
    };
    for (const Facet of Source.Facets)
    {
        const UVs = Facet.UVs ?? Facet.Coordinates ?? null;
        if (UVs) HasUVs = true;
        if (Facet.Normals) HasCornerNormals = true;
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            Triangles.push(Weld(Facet.Corners[Corner]));
            const Uv = UVs?.[Corner];
            CornerUVs.push(Uv ? Uv[0] : 0, Uv ? Uv[1] : 0);
            const N = Facet.Normals?.[Corner];
            if (N) CornerNormals.push(N[0], N[1], N[2]);
            else CornerNormals.push(0, 0, 0);
        }
        let Mask = 0;
        if (typeof Facet.SharpEdges === "number") Mask = Facet.SharpEdges & 7;
        else if (Array.isArray(Facet.SharpEdges))
            for (let Edge = 0; Edge < 3; ++Edge) if (Facet.SharpEdges[Edge]) Mask |= 1 << Edge;
        SharpMask.push(Mask);
        Families.push(Facet.Family ?? 0);
    }
    const VertexCount = Positions.length / 3;
    const TriangleArray = new Uint32Array(Triangles);
    const SharpArray = new Uint8Array(SharpMask);
    const UvArray = new Float64Array(CornerUVs);
    const PositionArray = new Float64Array(Positions);

    // Explicit mesh-level SharpEdges: entries may be [vA, vB] vertex indices or [[x,y,z], [x,y,z]] point pairs.
    const ExplicitSharp = new Set();
    const EdgeIdentity = (A, B) => (A < B ? A * VertexCount + B : B * VertexCount + A);
    if (Array.isArray(Source.SharpEdges))
        for (const Pair of Source.SharpEdges)
        {
            if (!Array.isArray(Pair) || Pair.length < 2) continue;
            const A = Array.isArray(Pair[0]) ? Welded.get(PositionKey(Pair[0])) : Pair[0];
            const B = Array.isArray(Pair[1]) ? Welded.get(PositionKey(Pair[1])) : Pair[1];
            if (Number.isInteger(A) && Number.isInteger(B) && A !== B) ExplicitSharp.add(EdgeIdentity(A, B));
        }

    // Propagate explicit sharp edges, authored normal discontinuities, and optional dihedral crease angles.
    const CreaseCosine = Number.isFinite(Options.SharpAngleDegrees)
        ? Math.cos((Options.SharpAngleDegrees * Math.PI) / 180) : null;
    const NormalCosine = Options.SharpNormalCosine ?? DefaultSharpNormalCosine;
    const Incidence = BuildEdgeIncidence(TriangleArray, VertexCount, UvArray, SharpArray);
    for (const Entry of Incidence.Edges.values())
    {
        let IsSharp = Entry.Sharp || ExplicitSharp.has(EdgeIdentity(Entry.A, Entry.B));
        if (!IsSharp && CreaseCosine !== null && Entry.Triangles.length === 2)
        {
            const N0 = Normalize(TriangleNormal(PositionArray, TriangleArray, Entry.Triangles[0]));
            const N1 = Normalize(TriangleNormal(PositionArray, TriangleArray, Entry.Triangles[1]));
            if (Dot(N0, N1) < CreaseCosine) IsSharp = true;
        }
        if (!IsSharp && HasCornerNormals && Entry.Triangles.length === 2)
        {
            const [T0, T1] = Entry.Triangles;
            for (const SharedVertex of [Entry.A, Entry.B])
            {
                let C0 = -1, C1 = -1;
                for (let C = 0; C < 3; ++C)
                {
                    if (TriangleArray[T0 * 3 + C] === SharedVertex) C0 = C;
                    if (TriangleArray[T1 * 3 + C] === SharedVertex) C1 = C;
                }
                if (C0 >= 0 && C1 >= 0)
                {
                    const N0 = [CornerNormals[(T0 * 3 + C0) * 3], CornerNormals[(T0 * 3 + C0) * 3 + 1], CornerNormals[(T0 * 3 + C0) * 3 + 2]];
                    const N1 = [CornerNormals[(T1 * 3 + C1) * 3], CornerNormals[(T1 * 3 + C1) * 3 + 1], CornerNormals[(T1 * 3 + C1) * 3 + 2]];
                    if (Length(N0) > 1e-6 && Length(N1) > 1e-6 && Dot(Normalize(N0), Normalize(N1)) < NormalCosine)
                    { IsSharp = true; break; }
                }
            }
        }
        if (IsSharp)
        {
            Entry.Sharp = true;
            for (const Triangle of Entry.Triangles)
                for (let Corner = 0; Corner < 3; ++Corner)
                {
                    const V0 = TriangleArray[Triangle * 3 + Corner], V1 = TriangleArray[Triangle * 3 + (Corner + 1) % 3];
                    if ((V0 === Entry.A && V1 === Entry.B) || (V0 === Entry.B && V1 === Entry.A))
                        SharpArray[Triangle] |= 1 << Corner;
                }
        }
    }
    return {
        Positions: PositionArray,
        Triangles: TriangleArray,
        Families: new Uint32Array(Families),
        CornerUVs: UvArray,
        SharpMask: SharpArray,
        HasUVs
    };
}

// 📝 Edge incidence over an indexed geometry: every undirected edge lists its triangles once and classifies
//    whether the edge is a UV seam (corner UVs disagree across incident triangles) or a marked sharp edge.
function BuildEdgeIncidence(Triangles, VertexCount, CornerUVs = null, SharpMask = null)
{
    const Edges = new Map();
    const Key = (A, B) => (A < B ? A * VertexCount + B : B * VertexCount + A);
    const TriangleCount = Triangles.length / 3;
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            const V0 = Triangles[Triangle * 3 + Corner], V1 = Triangles[Triangle * 3 + (Corner + 1) % 3];
            const A = Math.min(V0, V1), B = Math.max(V0, V1);
            const Identity = Key(A, B);
            let Entry = Edges.get(Identity);
            if (!Entry)
            {
                Entry = { A, B, Triangles: [], Sharp: false, UvSeam: false, UvA: null, UvB: null };
                Edges.set(Identity, Entry);
            }
            Entry.Triangles.push(Triangle);
            if (SharpMask && (SharpMask[Triangle] & (1 << Corner)) !== 0) Entry.Sharp = true;
            if (CornerUVs)
            {
                const CornerA = V0 === A ? Corner : (Corner + 1) % 3;
                const CornerB = V0 === A ? (Corner + 1) % 3 : Corner;
                const UvA = ReadCornerUv(CornerUVs, Triangle, CornerA);
                const UvB = ReadCornerUv(CornerUVs, Triangle, CornerB);
                if (!Entry.UvA) { Entry.UvA = UvA; Entry.UvB = UvB; }
                else if (UvDistance(UvA, Entry.UvA) > UvMatchTolerance || UvDistance(UvB, Entry.UvB) > UvMatchTolerance)
                    Entry.UvSeam = true;
            }
        }
    const Adjacent = Array.from({ length: TriangleCount }, () => []);
    const FeatureSeamBetween = new Set();
    const PairKey = (T0, T1) => (T0 < T1 ? T0 * TriangleCount + T1 : T1 * TriangleCount + T0);
    let SharpEdgeCount = 0, UvSeamEdgeCount = 0;
    for (const Entry of Edges.values())
    {
        if (Entry.Sharp) ++SharpEdgeCount;
        if (Entry.UvSeam) ++UvSeamEdgeCount;
        for (const Triangle of Entry.Triangles)
            for (const Other of Entry.Triangles)
                if (Other !== Triangle)
                {
                    Adjacent[Triangle].push(Other);
                    if (Entry.Sharp || Entry.UvSeam) FeatureSeamBetween.add(PairKey(Triangle, Other));
                }
    }
    for (const List of Adjacent) List.sort((A, B) => A - B);
    return { Edges, Adjacent, FeatureSeamBetween, PairKey, SharpEdgeCount, UvSeamEdgeCount };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     MESHLET PARTITION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Greedy adjacency expansion: a seed grows across shared edges, scoring candidates by normal coherence with
//    the running cone, scale-normalised centroid distance, and a soft penalty for crossing UV seams or marked
//    sharp edges so meshlets naturally align with UV chart boundaries and hard creases when possible.
function BuildMeshlets(Geometry, PartitionKeys, TriangleLimit, VertexLimit)
{
    const { Positions, Triangles, CornerUVs, SharpMask } = Geometry;
    const TriangleCount = Triangles.length / 3;
    const { Adjacent, FeatureSeamBetween, PairKey } = BuildEdgeIncidence(Triangles, Positions.length / 3, CornerUVs, SharpMask);
    const Normals = [], Centroids = [];
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        Normals.push(Normalize(TriangleNormal(Positions, Triangles, Triangle)));
        const A = ReadPosition(Positions, Triangles[Triangle * 3]);
        const B = ReadPosition(Positions, Triangles[Triangle * 3 + 1]);
        const C = ReadPosition(Positions, Triangles[Triangle * 3 + 2]);
        Centroids.push([(A[0] + B[0] + C[0]) / 3, (A[1] + B[1] + C[1]) / 3, (A[2] + B[2] + C[2]) / 3]);
    }
    const Assigned = new Int32Array(TriangleCount).fill(-1);
    const Meshlets = [];
    let SpacingTotal = 0, SpacingPairs = 0;
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (const Other of Adjacent[Triangle])
            if (Other > Triangle) { SpacingTotal += Length(Subtract(Centroids[Triangle], Centroids[Other])); ++SpacingPairs; }
    const MeanSpacing = SpacingPairs ? SpacingTotal / SpacingPairs : 1;
    const ExpectedRadius = Math.max(MeanSpacing * Math.sqrt(TriangleLimit / Math.PI), 1e-12);
    for (let Seed = 0; Seed < TriangleCount; ++Seed)
    {
        if (Assigned[Seed] >= 0) continue;
        const Identity = Meshlets.length, Key = PartitionKeys[Seed];
        const Members = [], Vertices = new Set();
        const AddVertex = (Triangle) =>
        {
            let Added = 0;
            for (let Corner = 0; Corner < 3; ++Corner) if (!Vertices.has(Triangles[Triangle * 3 + Corner])) ++Added;
            return Added;
        };
        const Admit = (Triangle) =>
        {
            Assigned[Triangle] = Identity;
            Members.push(Triangle);
            for (let Corner = 0; Corner < 3; ++Corner) Vertices.add(Triangles[Triangle * 3 + Corner]);
        };
        Admit(Seed);
        let Cone = [...Normals[Seed]];
        const Frontier = new Set(Adjacent[Seed].filter((Other) => Assigned[Other] < 0 && PartitionKeys[Other] === Key));
        while (Members.length < TriangleLimit && Frontier.size)
        {
            let Best = -1, BestScore = -Infinity;
            for (const Candidate of Frontier)
            {
                if (Assigned[Candidate] >= 0) continue;
                if (Vertices.size + AddVertex(Candidate) > VertexLimit) continue;
                const Alignment = Dot(Normals[Candidate], Cone);
                const Distance = Length(Subtract(Centroids[Candidate], Centroids[Seed]));
                // Check whether Candidate connects to the meshlet across a smooth edge or only across a UV seam / sharp edge.
                let SmoothConnection = false;
                for (const Neighbour of Adjacent[Candidate])
                    if (Assigned[Neighbour] === Identity && !FeatureSeamBetween.has(PairKey(Candidate, Neighbour)))
                    { SmoothConnection = true; break; }
                const SeamPenalty = SmoothConnection ? 0 : 0.35;
                const Score = Alignment - 0.3 * (Distance / ExpectedRadius) - SeamPenalty;
                if (Score > BestScore || (Score === BestScore && Candidate < Best)) { BestScore = Score; Best = Candidate; }
            }
            if (Best < 0) break;
            Frontier.delete(Best);
            Admit(Best);
            Cone = Normalize([Cone[0] + Normals[Best][0], Cone[1] + Normals[Best][1], Cone[2] + Normals[Best][2]]);
            for (const Other of Adjacent[Best])
                if (Assigned[Other] < 0 && PartitionKeys[Other] === Key) Frontier.add(Other);
        }
        Members.sort((A, B) => A - B);
        Meshlets.push(Members);
    }
    return Meshlets;
}

function MeasureCluster(Geometry, Members, Error)
{
    const { Positions, Triangles } = Geometry;
    const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
    const Center = [0, 0, 0];
    let Samples = 0;
    let Axis = [0, 0, 0];
    const Normals = [];
    for (const Triangle of Members)
    {
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            const P = ReadPosition(Positions, Triangles[Triangle * 3 + Corner]);
            for (let A = 0; A < 3; ++A) { Minimum[A] = Math.min(Minimum[A], P[A]); Maximum[A] = Math.max(Maximum[A], P[A]); Center[A] += P[A]; }
            ++Samples;
        }
        const Normal = TriangleNormal(Positions, Triangles, Triangle);
        Normals.push(Normal);
        Axis = [Axis[0] + Normal[0], Axis[1] + Normal[1], Axis[2] + Normal[2]];
    }
    for (let A = 0; A < 3; ++A) Center[A] /= Math.max(1, Samples);
    let Radius = 0;
    for (const Triangle of Members)
        for (let Corner = 0; Corner < 3; ++Corner)
            Radius = Math.max(Radius, Length(Subtract(ReadPosition(Positions, Triangles[Triangle * 3 + Corner]), Center)));
    Axis = Normalize(Axis);
    let Cutoff = 1;
    for (const Normal of Normals) Cutoff = Math.min(Cutoff, Dot(Axis, Normalize(Normal)));
    return {
        Minimum: Minimum.map((Value) => Value - Error),
        Maximum: Maximum.map((Value) => Value + Error),
        Sphere: { Center, Radius: Radius + Error },
        Cone: { Axis, Cutoff }
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     QEM SIMPLIFICATION
//------------------------------------------------------------------------------------------------------------------------

function QuadricFromPlane(Normal, D, Weight)
{
    const [A, B, C] = Normal;
    return new Float64Array([
        Weight * A * A, Weight * A * B, Weight * A * C, Weight * A * D,
        Weight * B * B, Weight * B * C, Weight * B * D,
        Weight * C * C, Weight * C * D, Weight * D * D]);
}

function QuadricError(Quadric, P)
{
    const [X, Y, Z] = P, Q = Quadric;
    return Q[0] * X * X + 2 * Q[1] * X * Y + 2 * Q[2] * X * Z + 2 * Q[3] * X +
           Q[4] * Y * Y + 2 * Q[5] * Y * Z + 2 * Q[6] * Y + Q[7] * Z * Z + 2 * Q[8] * Z + Q[9];
}

function QuadricOptimum(Quadric, Fallbacks)
{
    const Q = Quadric;
    const M = [[Q[0], Q[1], Q[2]], [Q[1], Q[4], Q[5]], [Q[2], Q[5], Q[7]]];
    const V = [-Q[3], -Q[6], -Q[8]];
    const Det = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) -
                M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
    if (Math.abs(Det) > 1e-12)
    {
        const Replace = (Column) =>
        {
            const R = M.map((Row, Index) => Row.map((Value, Axis) => (Axis === Column ? V[Index] : Value)));
            return R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) -
                   R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) +
                   R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
        };
        const Optimum = [Replace(0) / Det, Replace(1) / Det, Replace(2) / Det];
        if (Optimum.every(Number.isFinite) && Fallbacks.length >= 2)
        {
            const EdgeLength = Length(Subtract(Fallbacks[0], Fallbacks[1]));
            const Midpoint = [0, 1, 2].map((Axis) => (Fallbacks[0][Axis] + Fallbacks[1][Axis]) / 2);
            if (Length(Subtract(Optimum, Midpoint)) <= EdgeLength * 0.5) Fallbacks.push(Optimum);
        }
    }
    let Best = Fallbacks[0], BestError = Infinity;
    for (const Candidate of Fallbacks)
    {
        const Error = QuadricError(Quadric, Candidate);
        if (Error < BestError) { BestError = Error; Best = Candidate; }
    }
    return { Position: Best, Error: Math.max(0, BestError) };
}

// 📝 Simplifies one group's geometry in place-local terms while respecting:
//    1) Locked border vertices (group boundary, open border, non-manifold edge, material-family border),
//    2) Marked sharp edges and UV chart seams (line-alignment quadrics, feature-corner pinning, collapse-along-feature
//       restriction, synchronized multi-chart UV interpolation, and 2D UV triangle flip rejection).
function SimplifyGroup(Positions, Triangles, Families, TargetTriangles, CornerUVs = null, SharpMask = null)
{
    const VertexCount = Positions.length / 3, TriangleCount = Triangles.length / 3;
    const LivePositions = new Float64Array(Positions);
    const LiveTriangles = new Int32Array(Triangles);
    const LiveCornerUVs = CornerUVs ? new Float64Array(CornerUVs) : new Float64Array(TriangleCount * 6);
    const LiveSharpMask = SharpMask ? new Uint8Array(SharpMask) : new Uint8Array(TriangleCount);
    const { Edges } = BuildEdgeIncidence(Triangles, VertexCount, LiveCornerUVs, LiveSharpMask);
    const Locked = new Uint8Array(VertexCount);
    for (const Entry of Edges.values())
    {
        const BorderEdge = Entry.Triangles.length !== 2;
        const FamilyBorder = Entry.Triangles.length >= 2 &&
            Entry.Triangles.some((Triangle) => Families[Triangle] !== Families[Entry.Triangles[0]]);
        if (BorderEdge || FamilyBorder) { Locked[Entry.A] = 1; Locked[Entry.B] = 1; }
        if (Entry.Sharp)
            for (const Triangle of Entry.Triangles)
                for (let Corner = 0; Corner < 3; ++Corner)
                {
                    const V0 = LiveTriangles[Triangle * 3 + Corner], V1 = LiveTriangles[Triangle * 3 + (Corner + 1) % 3];
                    if ((V0 === Entry.A && V1 === Entry.B) || (V0 === Entry.B && V1 === Entry.A))
                        LiveSharpMask[Triangle] |= 1 << Corner;
                }
    }

    const Quadrics = Array.from({ length: VertexCount }, () => new Float64Array(10));
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        const A = ReadPosition(LivePositions, LiveTriangles[Triangle * 3]);
        const Raw = TriangleNormal(LivePositions, LiveTriangles, Triangle);
        const Area = Length(Raw) / 2;
        if (!(Area > 0)) continue;
        const Normal = Normalize(Raw);
        // Weight surface plane quadric slightly higher where UV area is dense relative to 3D area.
        const U0 = ReadCornerUv(LiveCornerUVs, Triangle, 0);
        const U1 = ReadCornerUv(LiveCornerUVs, Triangle, 1);
        const U2 = ReadCornerUv(LiveCornerUVs, Triangle, 2);
        const UvArea = Math.abs(UvSignedArea2(U0, U1, U2)) * 0.5;
        const Weight = Area * (1 + Math.min(2, UvArea / Math.max(Area, 1e-8)));
        const Plane = QuadricFromPlane(Normal, -Dot(Normal, A), Weight);
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            const Target = Quadrics[LiveTriangles[Triangle * 3 + Corner]];
            for (let C = 0; C < 10; ++C) Target[C] += Plane[C];
        }
    }
    // Feature-line alignment quadrics for marked sharp edges and UV seams: two orthogonal planes intersecting
    // along the 3D edge line penalize bending a crease or seam while letting collinear runs collapse freely.
    for (const Entry of Edges.values())
    {
        if (!Entry.Sharp && !Entry.UvSeam) continue;
        const PA = ReadPosition(LivePositions, Entry.A), PB = ReadPosition(LivePositions, Entry.B);
        const Span = Subtract(PB, PA), EdgeLen = Length(Span);
        if (!(EdgeLen > 1e-12)) continue;
        const Tangent = [Span[0] / EdgeLen, Span[1] / EdgeLen, Span[2] / EdgeLen];
        const [U, V] = OrthogonalBasis(Tangent);
        const FeatureWeight = (Entry.Sharp ? 6.0 : 4.0) * EdgeLen * EdgeLen;
        const Plane1 = QuadricFromPlane(U, -Dot(U, PA), FeatureWeight);
        const Plane2 = QuadricFromPlane(V, -Dot(V, PA), FeatureWeight);
        for (const Endpoint of [Entry.A, Entry.B])
            for (let C = 0; C < 10; ++C) Quadrics[Endpoint][C] += Plane1[C] + Plane2[C];
    }

    const Alive = new Uint8Array(TriangleCount).fill(1);
    const VertexTriangles = Array.from({ length: VertexCount }, () => new Set());
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
        for (let Corner = 0; Corner < 3; ++Corner) VertexTriangles[LiveTriangles[Triangle * 3 + Corner]].add(Triangle);
    const Remap = new Int32Array(VertexCount);
    for (let Vertex = 0; Vertex < VertexCount; ++Vertex) Remap[Vertex] = Vertex;
    const Resolve = (Vertex) => { while (Remap[Vertex] !== Vertex) Vertex = Remap[Vertex]; return Vertex; };

    // Live feature topology analyzer for a resolved vertex and an undirected edge (A, B).
    const AnalyzeVertexFeatures = (Vertex) =>
    {
        const NeighbourMap = new Map();
        const Wedges = [];
        for (const Triangle of VertexTriangles[Vertex])
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((C) => Resolve(LiveTriangles[Triangle * 3 + C]));
            const LocalCorner = Corners.indexOf(Vertex);
            if (LocalCorner < 0) continue;
            const Uv = ReadCornerUv(LiveCornerUVs, Triangle, LocalCorner);
            if (!Wedges.some((W) => UvDistance(W, Uv) <= UvMatchTolerance)) Wedges.push(Uv);
            for (let Edge = 0; Edge < 3; ++Edge)
            {
                const V0 = Corners[Edge], V1 = Corners[(Edge + 1) % 3];
                if (V0 !== Vertex && V1 !== Vertex) continue;
                const Other = V0 === Vertex ? V1 : V0;
                const CornerV = V0 === Vertex ? Edge : (Edge + 1) % 3;
                const CornerO = V0 === Vertex ? (Edge + 1) % 3 : Edge;
                const UvV = ReadCornerUv(LiveCornerUVs, Triangle, CornerV);
                const UvO = ReadCornerUv(LiveCornerUVs, Triangle, CornerO);
                let Record = NeighbourMap.get(Other);
                if (!Record)
                {
                    Record = { Sharp: false, UvSeam: false, UvV, UvO };
                    NeighbourMap.set(Other, Record);
                }
                else if (UvDistance(UvV, Record.UvV) > UvMatchTolerance || UvDistance(UvO, Record.UvO) > UvMatchTolerance)
                    Record.UvSeam = true;
                if ((LiveSharpMask[Triangle] & (1 << Edge)) !== 0) Record.Sharp = true;
            }
        }
        let SharpDegree = 0, SeamDegree = 0, FeatureDegree = 0;
        for (const Record of NeighbourMap.values())
        {
            if (Record.Sharp) ++SharpDegree;
            if (Record.UvSeam) ++SeamDegree;
            if (Record.Sharp || Record.UvSeam) ++FeatureDegree;
        }
        const IsFeature = FeatureDegree > 0 || Wedges.length > 1;
        const IsCollapsibleFeature = IsFeature && FeatureDegree === 2 &&
            (SharpDegree === 0 || SharpDegree === 2) &&
            (SeamDegree === 0 || SeamDegree === 2) &&
            Wedges.length <= 2;
        return { IsFeature, IsCollapsibleFeature, IsFeatureCorner: IsFeature && !IsCollapsibleFeature,
                 SharpDegree, SeamDegree, FeatureDegree, Wedges, NeighbourMap };
    };

    const AnalyzeEdgeFeatures = (A, B, FeatureA) =>
    {
        const Info = FeatureA.NeighbourMap.get(B);
        if (!Info) return null;
        return { Sharp: Info.Sharp, UvSeam: Info.UvSeam, IsFeature: Info.Sharp || Info.UvSeam };
    };

    const Candidates = [];
    for (const Entry of Edges.values()) Candidates.push([Entry.A, Entry.B]);
    Candidates.sort((A, B) => A[0] - B[0] || A[1] - B[1]);
    const Versions = new Int32Array(VertexCount);
    const Heap = [];
    const HeapPush = (Item) =>
    {
        Heap.push(Item);
        let Index = Heap.length - 1;
        while (Index > 0)
        {
            const Parent = (Index - 1) >> 1;
            if (Heap[Parent].Cost <= Heap[Index].Cost) break;
            [Heap[Parent], Heap[Index]] = [Heap[Index], Heap[Parent]];
            Index = Parent;
        }
    };
    const HeapPop = () =>
    {
        const Top = Heap[0], Last = Heap.pop();
        if (Heap.length)
        {
            Heap[0] = Last;
            let Index = 0;
            while (true)
            {
                let Smallest = Index;
                const Left = Index * 2 + 1, Right = Left + 1;
                if (Left < Heap.length && Heap[Left].Cost < Heap[Smallest].Cost) Smallest = Left;
                if (Right < Heap.length && Heap[Right].Cost < Heap[Smallest].Cost) Smallest = Right;
                if (Smallest === Index) break;
                [Heap[Smallest], Heap[Index]] = [Heap[Index], Heap[Smallest]];
                Index = Smallest;
            }
        }
        return Top;
    };

    const Evaluate = (A, B) =>
    {
        A = Resolve(A); B = Resolve(B);
        if (A === B) return null;
        if (Locked[A] && Locked[B]) return null;
        const FA = AnalyzeVertexFeatures(A), FB = AnalyzeVertexFeatures(B);
        const EdgeFeat = AnalyzeEdgeFeatures(A, B, FA);
        if (!EdgeFeat) return null;

        // Feature-preservation rules:
        // 1) Never collapse a non-feature chord between two feature vertices.
        if (FA.IsFeature && FB.IsFeature && !EdgeFeat.IsFeature) return null;
        const PinA = Boolean(Locked[A] || FA.IsFeatureCorner || (FA.IsFeature && !FB.IsFeature));
        const PinB = Boolean(Locked[B] || FB.IsFeatureCorner || (FB.IsFeature && !FA.IsFeature));
        if (PinA && PinB) return null;
        // 2) A smooth interior vertex may only collapse into a UV-seam vertex if the seam vertex uses a single
        //    UV wedge on their shared triangles (so the smooth chart stays on its own side of the seam).
        if (!EdgeFeat.IsFeature && (FA.SeamDegree > 0 || FB.SeamDegree > 0)) return null;

        const Sum = new Float64Array(10);
        for (let C = 0; C < 10; ++C) Sum[C] = Quadrics[A][C] + Quadrics[B][C];
        const PA = ReadPosition(LivePositions, A), PB = ReadPosition(LivePositions, B);
        const Midpoint = [(PA[0] + PB[0]) / 2, (PA[1] + PB[1]) / 2, (PA[2] + PB[2]) / 2];
        const Pinned = PinA ? PA : PinB ? PB : null;
        let Position, Error;
        if (Pinned)
        {
            Position = Pinned;
            Error = Math.max(0, QuadricError(Sum, Pinned));
        }
        else if (EdgeFeat.IsFeature)
        {
            // Along a feature edge, restrict candidates to the segment [PA, PB] so parameter t in [0, 1] is exact.
            let Best = Midpoint, BestError = Infinity;
            for (const Candidate of [PA, PB, Midpoint])
            {
                const E = QuadricError(Sum, Candidate);
                if (E < BestError) { BestError = E; Best = Candidate; }
            }
            Position = Best;
            Error = Math.max(0, BestError);
        }
        else
        {
            const Result = QuadricOptimum(Sum, [PA, PB, Midpoint]);
            Position = Result.Position;
            Error = Result.Error;
        }
        const Deviation = Math.max(Length(Subtract(Position, PA)), Length(Subtract(Position, PB)));
        // 📝 Edge-length regularization: on ruled or planar patches (cylinders, caps, flat terrain benches) the
        //    plane quadric cost is near zero along straight generators, so without a length tie-breaker the heap
        //    collapses one entire column into long needle triangles before touching short adjacent edges.
        const Trace = Math.max(Sum[0] + Sum[4] + Sum[7], 1e-6);
        const Cost = Error + 0.02 * Deviation * Deviation * Trace;
        return { A, B, Position, PinA, PinB, Cost, Deviation, VersionA: Versions[A], VersionB: Versions[B] };
    };
    for (const [A, B] of Candidates) { const Item = Evaluate(A, B); if (Item) HeapPush(Item); }

    let Remaining = TriangleCount, MaxDeviation = 0;
    const Flips = (A, B, Position) =>
    {
        for (const Triangle of VertexTriangles[A])
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((Corner) => Resolve(LiveTriangles[Triangle * 3 + Corner]));
            if (Corners.includes(B)) continue;
            const Points = Corners.map((Vertex) => (Vertex === A ? Position : ReadPosition(LivePositions, Vertex)));
            const Current = Corners.map((Vertex) => ReadPosition(LivePositions, Vertex));
            const Before = Cross(Subtract(Current[1], Current[0]), Subtract(Current[2], Current[0]));
            const After = Cross(Subtract(Points[1], Points[0]), Subtract(Points[2], Points[0]));
            if (Dot(Before, After) <= 0) return true;
        }
        return false;
    };

    // Computes chart-coherent updated corner UVs for every surviving triangle in Affected and rejects any
    // collapse that would mix UV charts or invert a 2D UV triangle.
    const ComputeCornerUvUpdates = (Keeper, Removed, Position) =>
    {
        const PK = ReadPosition(LivePositions, Keeper), PR = ReadPosition(LivePositions, Removed);
        const Span = Subtract(PR, PK), SpanSq = Dot(Span, Span);
        const T = SpanSq > 1e-20 ? Math.max(0, Math.min(1, Dot(Subtract(Position, PK), Span) / SpanSq)) : 0;
        const Charts = [];
        for (const Triangle of VertexTriangles[Removed])
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((C) => Resolve(LiveTriangles[Triangle * 3 + C]));
            const CK = Corners.indexOf(Keeper), CR = Corners.indexOf(Removed);
            if (CK < 0 || CR < 0) continue;
            const UvK = ReadCornerUv(LiveCornerUVs, Triangle, CK);
            const UvR = ReadCornerUv(LiveCornerUVs, Triangle, CR);
            const UvNew = [UvK[0] * (1 - T) + UvR[0] * T, UvK[1] * (1 - T) + UvR[1] * T];
            Charts.push({ UvK, UvR, UvNew });
        }
        if (!Charts.length) return null;
        const Updates = [];
        const Affected = new Set([...VertexTriangles[Removed], ...VertexTriangles[Keeper]]);
        for (const Triangle of Affected)
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((C) => Resolve(LiveTriangles[Triangle * 3 + C]));
            if (Corners.includes(Keeper) && Corners.includes(Removed)) continue;   // dies in collapse
            const CK = Corners.indexOf(Keeper), CR = Corners.indexOf(Removed);
            const TargetCorner = CK >= 0 ? CK : CR;
            if (TargetCorner < 0) continue;
            const CurrentUv = ReadCornerUv(LiveCornerUVs, Triangle, TargetCorner);
            let ReplacementUv = null;
            if (CK >= 0 && T === 0)
            {
                ReplacementUv = CurrentUv;
            }
            else
            {
                for (const Chart of Charts)
                {
                    const Ref = CK >= 0 ? Chart.UvK : Chart.UvR;
                    if (UvDistance(CurrentUv, Ref) <= UvMatchTolerance) { ReplacementUv = Chart.UvNew; break; }
                }
                if (!ReplacementUv && Charts.length === 1) ReplacementUv = Charts[0].UvNew;
            }
            if (!ReplacementUv) return null;
            // Guard against 2D UV triangle inversion inside a chart.
            const BeforeUVs = [0, 1, 2].map((C) => ReadCornerUv(LiveCornerUVs, Triangle, C));
            const AfterUVs = BeforeUVs.map((Uv, C) => (C === TargetCorner ? ReplacementUv : Uv));
            const AreaBefore = UvSignedArea2(BeforeUVs[0], BeforeUVs[1], BeforeUVs[2]);
            const AreaAfter = UvSignedArea2(AfterUVs[0], AfterUVs[1], AfterUVs[2]);
            if (Math.abs(AreaBefore) > 1e-10 && AreaBefore * AreaAfter <= 1e-12) return null;
            Updates.push({ Triangle, Corner: TargetCorner, Uv: ReplacementUv });
        }
        return Updates;
    };

    while (Remaining > TargetTriangles && Heap.length)
    {
        const Item = HeapPop();
        const A = Resolve(Item.A), B = Resolve(Item.B);
        if (A === B) continue;
        if (A !== Item.A || B !== Item.B || Versions[A] !== Item.VersionA || Versions[B] !== Item.VersionB)
        {
            const Fresh = Evaluate(A, B);
            if (Fresh) HeapPush(Fresh);
            continue;
        }
        const FA = AnalyzeVertexFeatures(A), FB = AnalyzeVertexFeatures(B);
        const EdgeFeat = AnalyzeEdgeFeatures(A, B, FA);
        if (!EdgeFeat || (FA.IsFeature && FB.IsFeature && !EdgeFeat.IsFeature)) continue;
        const PinA = Boolean(Locked[A] || FA.IsFeatureCorner || (FA.IsFeature && !FB.IsFeature));
        const PinB = Boolean(Locked[B] || FB.IsFeatureCorner || (FB.IsFeature && !FA.IsFeature));
        if (PinA && PinB) continue;
        if (PinA !== Item.PinA || PinB !== Item.PinB)
        {
            const Fresh = Evaluate(A, B);
            if (Fresh) HeapPush(Fresh);
            continue;
        }
        const Keeper = PinA ? A : PinB ? B : A;
        const Removed = Keeper === A ? B : A;
        if (Flips(Removed, Keeper, Item.Position) || Flips(Keeper, Removed, Item.Position)) continue;
        const UvUpdates = ComputeCornerUvUpdates(Keeper, Removed, Item.Position);
        if (!UvUpdates) continue;

        const DyingVertices = new Set([Keeper, Removed]);
        let Orphans = false;
        for (const Triangle of VertexTriangles[Removed])
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((Corner) => Resolve(LiveTriangles[Triangle * 3 + Corner]));
            if (!Corners.includes(Keeper)) continue;
            for (const Vertex of Corners) DyingVertices.add(Vertex);
        }
        let DyingDeviation = 0;
        for (const Vertex of DyingVertices)
        {
            if (Vertex === Removed) continue;
            let Survives = false;
            for (const Triangle of VertexTriangles[Vertex])
            {
                if (!Alive[Triangle]) continue;
                const Corners = [0, 1, 2].map((Corner) => Resolve(LiveTriangles[Triangle * 3 + Corner]));
                if (!(Corners.includes(Keeper) && Corners.includes(Removed))) { Survives = true; break; }
            }
            if (Survives) continue;
            if (Vertex === Keeper || Locked[Vertex]) { Orphans = true; break; }
            DyingDeviation = Math.max(DyingDeviation,
                Length(Subtract(ReadPosition(LivePositions, Vertex), Item.Position)));
        }
        if (Orphans) continue;

        // Record sharp edges among Affected triangles before remappingRemoved -> Keeper.
        const Affected = new Set([...VertexTriangles[Removed], ...VertexTriangles[Keeper]]);
        const SharpPairs = new Set();
        const SharpKey = (U, V) => (U < V ? U * VertexCount + V : V * VertexCount + U);
        for (const Triangle of Affected)
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((C) => Resolve(LiveTriangles[Triangle * 3 + C]));
            for (let Edge = 0; Edge < 3; ++Edge)
                if ((LiveSharpMask[Triangle] & (1 << Edge)) !== 0)
                {
                    const U = Corners[Edge] === Removed ? Keeper : Corners[Edge];
                    const V = Corners[(Edge + 1) % 3] === Removed ? Keeper : Corners[(Edge + 1) % 3];
                    if (U !== V) SharpPairs.add(SharpKey(U, V));
                }
        }

        // Collapse Removed into Keeper at the evaluated position and apply UV updates.
        for (let Axis = 0; Axis < 3; ++Axis) LivePositions[Keeper * 3 + Axis] = Item.Position[Axis];
        for (let C = 0; C < 10; ++C) Quadrics[Keeper][C] += Quadrics[Removed][C];
        for (const Update of UvUpdates)
        {
            const Base = Update.Triangle * 6 + Update.Corner * 2;
            LiveCornerUVs[Base] = Update.Uv[0];
            LiveCornerUVs[Base + 1] = Update.Uv[1];
        }
        Remap[Removed] = Keeper;
        MaxDeviation = Math.max(MaxDeviation, Item.Deviation, DyingDeviation);
        for (const Triangle of Affected)
        {
            if (!Alive[Triangle]) continue;
            const Corners = [0, 1, 2].map((Corner) => Resolve(LiveTriangles[Triangle * 3 + Corner]));
            if (new Set(Corners).size < 3) { Alive[Triangle] = 0; --Remaining; continue; }
            let Mask = 0;
            for (let Corner = 0; Corner < 3; ++Corner)
            {
                LiveTriangles[Triangle * 3 + Corner] = Corners[Corner];
                VertexTriangles[Corners[Corner]].add(Triangle);
                if (SharpPairs.has(SharpKey(Corners[Corner], Corners[(Corner + 1) % 3]))) Mask |= 1 << Corner;
            }
            LiveSharpMask[Triangle] = Mask;
        }
        VertexTriangles[Removed].clear();
        ++Versions[Keeper];
        const Neighbours = new Set();
        for (const Triangle of VertexTriangles[Keeper])
            for (let Corner = 0; Corner < 3; ++Corner)
            {
                const Vertex = Resolve(LiveTriangles[Triangle * 3 + Corner]);
                if (Vertex !== Keeper) Neighbours.add(Vertex);
            }
        for (const Vertex of Neighbours) { const Fresh = Evaluate(Keeper, Vertex); if (Fresh) HeapPush(Fresh); }
    }

    // Compact surviving geometry with per-corner UVs and sharp-edge masks.
    const OutPositions = [], OutTriangles = [], OutFamilies = [], OutCornerUVs = [], OutSharpMask = [];
    const Compact = new Map();
    const EmitVertex = (Vertex) =>
    {
        Vertex = Resolve(Vertex);
        let Index = Compact.get(Vertex);
        if (Index === undefined)
        {
            Index = OutPositions.length / 3;
            Compact.set(Vertex, Index);
            OutPositions.push(LivePositions[Vertex * 3], LivePositions[Vertex * 3 + 1], LivePositions[Vertex * 3 + 2]);
        }
        return Index;
    };
    for (let Triangle = 0; Triangle < TriangleCount; ++Triangle)
    {
        if (!Alive[Triangle]) continue;
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            OutTriangles.push(EmitVertex(LiveTriangles[Triangle * 3 + Corner]));
            OutCornerUVs.push(LiveCornerUVs[Triangle * 6 + Corner * 2], LiveCornerUVs[Triangle * 6 + Corner * 2 + 1]);
        }
        OutFamilies.push(Families[Triangle]);
        OutSharpMask.push(LiveSharpMask[Triangle]);
    }
    return {
        Positions: new Float64Array(OutPositions),
        Triangles: new Uint32Array(OutTriangles),
        Families: new Uint32Array(OutFamilies),
        CornerUVs: new Float64Array(OutCornerUVs),
        SharpMask: new Uint8Array(OutSharpMask),
        Deviation: MaxDeviation,
        LockedCount: Locked.reduce((Sum, Value) => Sum + Value, 0)
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     HIERARCHY CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

function GroupClusters(Geometry, Meshlets, GroupLimit)
{
    const { Triangles } = Geometry;
    const ClusterOf = new Int32Array(Triangles.length / 3).fill(-1);
    Meshlets.forEach((Members, Identity) => { for (const Triangle of Members) ClusterOf[Triangle] = Identity; });
    const { Adjacent } = BuildEdgeIncidence(Triangles, Geometry.Positions.length / 3);
    const ClusterAdjacent = Meshlets.map(() => new Set());
    for (let Triangle = 0; Triangle < ClusterOf.length; ++Triangle)
        for (const Other of Adjacent[Triangle])
            if (ClusterOf[Other] !== ClusterOf[Triangle]) ClusterAdjacent[ClusterOf[Triangle]].add(ClusterOf[Other]);
    const Assigned = new Int32Array(Meshlets.length).fill(-1);
    const Groups = [];
    for (let Seed = 0; Seed < Meshlets.length; ++Seed)
    {
        if (Assigned[Seed] >= 0) continue;
        const Identity = Groups.length, Members = [Seed];
        Assigned[Seed] = Identity;
        const Frontier = [...ClusterAdjacent[Seed]].sort((A, B) => A - B);
        while (Members.length < GroupLimit && Frontier.length)
        {
            const Next = Frontier.shift();
            if (Assigned[Next] >= 0) continue;
            Assigned[Next] = Identity;
            Members.push(Next);
            for (const Other of [...ClusterAdjacent[Next]].sort((A, B) => A - B))
                if (Assigned[Other] < 0 && !Frontier.includes(Other)) Frontier.push(Other);
            Frontier.sort((A, B) => A - B);
        }
        Members.sort((A, B) => A - B);
        Groups.push(Members);
    }
    const MinimumGroup = Math.max(2, GroupLimit >> 2);
    let Repaired = true;
    while (Repaired)
    {
        Repaired = false;
        for (let Group = 0; Group < Groups.length; ++Group)
        {
            if (!Groups[Group].length || Groups[Group].length >= MinimumGroup) continue;
            const Neighbours = new Map();
            for (const Member of Groups[Group])
                for (const Other of ClusterAdjacent[Member])
                {
                    const OtherGroup = Assigned[Other];
                    if (OtherGroup !== Group && Groups[OtherGroup].length)
                        Neighbours.set(OtherGroup, (Neighbours.get(OtherGroup) ?? 0) + 1);
                }
            let Target = -1, TargetSize = Infinity, TargetShared = -1;
            for (const [OtherGroup, Shared] of [...Neighbours.entries()].sort((A, B) => A[0] - B[0]))
            {
                const Size = Groups[OtherGroup].length;
                if (Size + Groups[Group].length > GroupLimit * 2) continue;
                if (Size < TargetSize || (Size === TargetSize && Shared > TargetShared))
                { Target = OtherGroup; TargetSize = Size; TargetShared = Shared; }
            }
            if (Target < 0) continue;
            for (const Member of Groups[Group]) { Assigned[Member] = Target; Groups[Target].push(Member); }
            Groups[Group] = [];
            Repaired = true;
        }
    }
    return Groups.filter((Members) => Members.length).map((Members) => Members.sort((A, B) => A - B));
}

function ExtractGroupGeometry(Geometry, TriangleList)
{
    const Positions = [], Triangles = [], Families = [], CornerUVs = [], SharpMask = [], Local = new Map();
    const Emit = (Vertex) =>
    {
        let Index = Local.get(Vertex);
        if (Index === undefined)
        {
            Index = Positions.length / 3;
            Local.set(Vertex, Index);
            Positions.push(Geometry.Positions[Vertex * 3], Geometry.Positions[Vertex * 3 + 1], Geometry.Positions[Vertex * 3 + 2]);
        }
        return Index;
    };
    for (const Triangle of TriangleList)
    {
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            Triangles.push(Emit(Geometry.Triangles[Triangle * 3 + Corner]));
            if (Geometry.CornerUVs)
                CornerUVs.push(Geometry.CornerUVs[Triangle * 6 + Corner * 2], Geometry.CornerUVs[Triangle * 6 + Corner * 2 + 1]);
            else CornerUVs.push(0, 0);
        }
        Families.push(Geometry.Families[Triangle]);
        SharpMask.push(Geometry.SharpMask ? Geometry.SharpMask[Triangle] : 0);
    }
    return {
        Positions: new Float64Array(Positions),
        Triangles: new Uint32Array(Triangles),
        Families: new Uint32Array(Families),
        CornerUVs: new Float64Array(CornerUVs),
        SharpMask: new Uint8Array(SharpMask)
    };
}

// 📝 Exact-position weld for concatenated group output. Triangle order is preserved (child attribution via
//    GroupOf stays aligned), and per-corner UVs and sharp-edge masks travel with their surviving triangles.
function WeldLevelGeometry(Positions, Triangles, Families, PartitionKeys, GroupOf, CornerUVs = null, SharpMask = null)
{
    const Welded = new Map(), OutPositions = [], Remap = new Uint32Array(Positions.length / 3);
    for (let Vertex = 0; Vertex < Positions.length / 3; ++Vertex)
    {
        const Key = [0, 1, 2].map((A) => Positions[Vertex * 3 + A] + 0).join(",");
        let Index = Welded.get(Key);
        if (Index === undefined)
        {
            Index = OutPositions.length / 3;
            Welded.set(Key, Index);
            OutPositions.push(Positions[Vertex * 3], Positions[Vertex * 3 + 1], Positions[Vertex * 3 + 2]);
        }
        Remap[Vertex] = Index;
    }
    const OutTriangles = [], OutFamilies = [], OutKeys = [], OutGroupOf = [], OutCornerUVs = [], OutSharpMask = [];
    for (let Triangle = 0; Triangle < Triangles.length / 3; ++Triangle)
    {
        const Corners = [0, 1, 2].map((Corner) => Remap[Triangles[Triangle * 3 + Corner]]);
        if (new Set(Corners).size < 3) continue;
        OutTriangles.push(...Corners);
        OutFamilies.push(Families[Triangle]);
        OutKeys.push(PartitionKeys[Triangle]);
        OutGroupOf.push(GroupOf[Triangle]);
        for (let Corner = 0; Corner < 3; ++Corner)
            if (CornerUVs) OutCornerUVs.push(CornerUVs[Triangle * 6 + Corner * 2], CornerUVs[Triangle * 6 + Corner * 2 + 1]);
            else OutCornerUVs.push(0, 0);
        OutSharpMask.push(SharpMask ? SharpMask[Triangle] : 0);
    }
    return {
        Positions: new Float64Array(OutPositions), Triangles: new Uint32Array(OutTriangles),
        Families: new Uint32Array(OutFamilies), PartitionKeys: OutKeys, GroupOf: OutGroupOf,
        CornerUVs: new Float64Array(OutCornerUVs), SharpMask: new Uint8Array(OutSharpMask)
    };
}

export function BuildGeometryHierarchy(Source, Options = {})
{
    const TriangleLimit = Options.TriangleLimit ?? DefaultTriangleLimit;
    const VertexLimit = Options.VertexLimit ?? DefaultVertexLimit;
    const GroupLimit = Options.GroupLimit ?? DefaultGroupLimit;
    const TargetRatio = Options.TargetRatio ?? DefaultTargetRatio;
    const MaxLevels = Options.MaxLevels ?? DefaultMaxLevels;
    const Transmissive = new Set(Options.TransmissiveFamilies ?? []);
    if (!(TriangleLimit >= 1) || !(VertexLimit >= 3) || !(GroupLimit >= 1) || !(TargetRatio > 0 && TargetRatio < 1))
        throw new Error("Geometry hierarchy limits are invalid");
    const ClassOf = (Family) => (Transmissive.has(Family) ? 1 : 0);

    let Geometry = WeldFacetSoup(Source, Options);
    const SourceTriangles = Geometry.Triangles.length / 3;
    const Clusters = [], Levels = [];
    let LevelGeometry = Geometry, ChildClusters = null;
    for (let Level = 0; Level < MaxLevels; ++Level)
    {
        const PartitionKeys = LevelGeometry.PartitionKeys ??
            Array.from(LevelGeometry.Families, (Family) => "class:" + ClassOf(Family));
        const Meshlets = BuildMeshlets(LevelGeometry, PartitionKeys, TriangleLimit, VertexLimit);
        const LevelIncidence = BuildEdgeIncidence(LevelGeometry.Triangles, LevelGeometry.Positions.length / 3,
            LevelGeometry.CornerUVs, LevelGeometry.SharpMask);
        const FirstCluster = Clusters.length;
        const LevelClusters = [];
        Meshlets.forEach((Members, LocalIdentity) =>
        {
            const Children = ChildClusters ? [...new Set(Members.map((Triangle) => ChildClusters.GroupOf[Triangle]))]
                .flatMap((Group) => ChildClusters.Groups[Group]) : [];
            const ChildError = Children.reduce((Max, Child) => Math.max(Max, Clusters[Child].Error), 0);
            const GroupStep = ChildClusters ? Math.max(ChildClusters.GroupDeviation[ChildClusters.GroupOf[Members[0]]] ?? 0, 1e-5) : 0;
            const Error = Level === 0 ? 0 : ChildError + GroupStep;
            const Measured = MeasureCluster(LevelGeometry, Members, Error);
            const TightMinimum = [...Measured.Minimum];
            const TightMaximum = [...Measured.Maximum];
            const TightSphere = { Center: [...Measured.Sphere.Center], Radius: Measured.Sphere.Radius };
            // Union over children so parent bounds conservatively contain every child cluster they stand in for.
            for (const Child of Children)
                for (let Axis = 0; Axis < 3; ++Axis)
                {
                    Measured.Minimum[Axis] = Math.min(Measured.Minimum[Axis], Clusters[Child].Minimum[Axis]);
                    Measured.Maximum[Axis] = Math.max(Measured.Maximum[Axis], Clusters[Child].Maximum[Axis]);
                }
            if (Children.length)
            {
                const Center = [0, 1, 2].map((Axis) => (Measured.Minimum[Axis] + Measured.Maximum[Axis]) / 2);
                let Radius = Length(Subtract(Measured.Sphere.Center, Center)) + Measured.Sphere.Radius;
                let Cutoff = Measured.Cone.Cutoff;
                for (const Child of Children)
                {
                    const Node = Clusters[Child];
                    Radius = Math.max(Radius, Length(Subtract(Node.Sphere.Center, Center)) + Node.Sphere.Radius);
                    const Alignment = Math.max(-1, Math.min(1, Dot(Measured.Cone.Axis, Node.Cone.Axis)));
                    const Worst = Alignment * Node.Cone.Cutoff -
                        Math.sqrt(Math.max(0, 1 - Node.Cone.Cutoff * Node.Cone.Cutoff)) * Math.sqrt(Math.max(0, 1 - Alignment * Alignment));
                    Cutoff = Math.min(Cutoff, Worst);
                }
                Measured.Sphere = { Center, Radius };
                Measured.Cone.Cutoff = Cutoff;
            }
            let ClassMask = 0, SharpEdgesInCluster = 0;
            for (const Triangle of Members)
            {
                ClassMask |= 1 << ClassOf(LevelGeometry.Families[Triangle]);
                const Mask = LevelGeometry.SharpMask ? LevelGeometry.SharpMask[Triangle] : 0;
                SharpEdgesInCluster += (Mask & 1) + ((Mask >> 1) & 1) + ((Mask >> 2) & 1);
            }
            const Node = {
                Identity: Clusters.length, Level, Children: Children.sort((A, B) => A - B),
                Parents: [], GroupId: -1,
                FirstTriangle: Members[0] ?? 0, Triangles: Members, TriangleCount: Members.length,
                ...Measured, TightMinimum, TightMaximum, TightSphere,
                Error, ParentError: Infinity, ParentSphere: { Center: [...Measured.Sphere.Center], Radius: Measured.Sphere.Radius },
                ClassMask, SharpEdgesInCluster
            };
            Clusters.push(Node);
            LevelClusters.push(Node.Identity);
            void LocalIdentity;
        });
        // Link parent clusters back to their child simplification groups so every child in a group shares the
        // exact same ParentError and enclosing ParentSphere for crack-free runtime DAG cuts.
        if (ChildClusters)
        {
            const ParentsByGroup = ChildClusters.Groups.map(() => []);
            for (const ParentIdentity of LevelClusters)
            {
                const ParentNode = Clusters[ParentIdentity];
                const SourceGroup = ChildClusters.GroupOf[ParentNode.FirstTriangle];
                if (SourceGroup !== undefined && ParentsByGroup[SourceGroup])
                    ParentsByGroup[SourceGroup].push(ParentIdentity);
            }
            ChildClusters.Groups.forEach((GroupMembers, GroupIndex) =>
            {
                const GroupParents = ParentsByGroup[GroupIndex];
                if (!GroupParents.length) return;
                // Compute a single enclosing sphere and error across the whole group and its parent clusters.
                const GMin = [Infinity, Infinity, Infinity], GMax = [-Infinity, -Infinity, -Infinity];
                let GroupParentError = 0;
                for (const Id of [...GroupMembers, ...GroupParents])
                {
                    const Node = Clusters[Id];
                    for (let A = 0; A < 3; ++A)
                    {
                        GMin[A] = Math.min(GMin[A], Node.Minimum[A]);
                        GMax[A] = Math.max(GMax[A], Node.Maximum[A]);
                    }
                }
                for (const ParentId of GroupParents) GroupParentError = Math.max(GroupParentError, Clusters[ParentId].Error);
                const Center = [0, 1, 2].map((A) => (GMin[A] + GMax[A]) / 2);
                let Radius = 0;
                for (const Id of [...GroupMembers, ...GroupParents])
                {
                    const Node = Clusters[Id];
                    Radius = Math.max(Radius, Length(Subtract(Node.Sphere.Center, Center)) + Node.Sphere.Radius);
                }
                const SharedSphere = { Center, Radius };
                for (const ParentId of GroupParents) Clusters[ParentId].Sphere = { Center: [...Center], Radius };
                for (const ChildId of GroupMembers)
                {
                    Clusters[ChildId].GroupId = GroupIndex;
                    Clusters[ChildId].Parents = [...GroupParents];
                    Clusters[ChildId].ParentError = Math.max(GroupParentError, Clusters[ChildId].Error + 1e-5);
                    Clusters[ChildId].ParentSphere = { Center: [...SharedSphere.Center], Radius: SharedSphere.Radius };
                }
            });
        }
        Levels.push({
            Level, Clusters: LevelClusters,
            Positions: LevelGeometry.Positions, Triangles: LevelGeometry.Triangles, Families: LevelGeometry.Families,
            CornerUVs: LevelGeometry.CornerUVs, SharpMask: LevelGeometry.SharpMask,
            SharpEdgeCount: LevelIncidence.SharpEdgeCount, UvSeamEdgeCount: LevelIncidence.UvSeamEdgeCount,
            TriangleCount: LevelGeometry.Triangles.length / 3, VertexCount: LevelGeometry.Positions.length / 3
        });
        if (LevelGeometry.Triangles.length / 3 <= TriangleLimit) break;
        const EffectiveGroupLimit = Math.min(GroupLimit * 2 ** Level, 32);
        const Groups = GroupClusters(LevelGeometry, Meshlets, EffectiveGroupLimit);
        const ParentPositions = [], ParentTriangles = [], ParentFamilies = [], ParentKeys = [];
        const ParentCornerUVs = [], ParentSharpMask = [];
        const GroupOfTriangle = [], GroupDeviation = [], GroupMembers = [];
        let Progress = false;
        Groups.forEach((Members, GroupIdentity) =>
        {
            const TriangleList = Members.flatMap((Cluster) => Meshlets[Cluster]).sort((A, B) => A - B);
            const Local = ExtractGroupGeometry(LevelGeometry, TriangleList);
            const Target = Math.max(1, Math.ceil((Local.Triangles.length / 3) * TargetRatio));
            const Simplified = SimplifyGroup(Local.Positions, Local.Triangles, Local.Families, Target,
                Local.CornerUVs, Local.SharpMask);
            if (Simplified.Triangles.length < Local.Triangles.length) Progress = true;
            const Base = ParentPositions.length / 3;
            for (const Value of Simplified.Positions) ParentPositions.push(Value);
            for (const Index of Simplified.Triangles) ParentTriangles.push(Index + Base);
            for (const Value of Simplified.CornerUVs) ParentCornerUVs.push(Value);
            for (const Mask of Simplified.SharpMask) ParentSharpMask.push(Mask);
            for (const Family of Simplified.Families)
            {
                ParentFamilies.push(Family);
                ParentKeys.push("group:" + GroupIdentity + ":class:" + ClassOf(Family));
                GroupOfTriangle.push(GroupIdentity);
            }
            GroupDeviation.push(Simplified.Deviation);
            GroupMembers.push(Members.map((Cluster) => FirstCluster + Cluster));
        });
        if (!Progress) break;
        ChildClusters = { Groups: GroupMembers, GroupOf: GroupOfTriangle, GroupDeviation };
        LevelGeometry = WeldLevelGeometry(new Float64Array(ParentPositions), new Uint32Array(ParentTriangles),
            new Uint32Array(ParentFamilies), ParentKeys, GroupOfTriangle,
            new Float64Array(ParentCornerUVs), new Uint8Array(ParentSharpMask));
        ChildClusters.GroupOf = LevelGeometry.GroupOf;
        LevelGeometry.GroupOf = undefined;
    }
    return {
        Clusters, Levels, SourceTriangles,
        WeldedVertices: Geometry.Positions.length / 3,
        HasUVs: Geometry.HasUVs
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VALIDATION SUBSTRATE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Independent re-checks used by the proof tool and tests; nothing here trusts builder-internal bookkeeping.
export function ValidateGeometryHierarchy(Source, Hierarchy, Options = {})
{
    const TriangleLimit = Options.TriangleLimit ?? DefaultTriangleLimit;
    const VertexLimit = Options.VertexLimit ?? DefaultVertexLimit;
    const Checks = { Passed: true, Failures: [] };
    const Confirm = (Condition, Caption) => { if (!Condition) { Checks.Passed = false; Checks.Failures.push(Caption); } };
    Confirm(Hierarchy.SourceTriangles === Source.Facets.length, "source triangle count changed during weld");
    const Leaf = Hierarchy.Levels[0];
    const Covered = new Int32Array(Leaf.TriangleCount);
    for (const Identity of Leaf.Clusters)
    {
        const Node = Hierarchy.Clusters[Identity];
        Confirm(Node.TriangleCount <= TriangleLimit, "leaf meshlet exceeds the triangle limit");
        const Vertices = new Set();
        for (const Triangle of Node.Triangles)
        {
            ++Covered[Triangle];
            for (let Corner = 0; Corner < 3; ++Corner) Vertices.add(Leaf.Triangles[Triangle * 3 + Corner]);
        }
        Confirm(Vertices.size <= VertexLimit, "leaf meshlet exceeds the vertex limit");
        Confirm(Node.Error === 0, "leaf cluster carries simplification error");
    }
    Confirm([...Covered].every((Count) => Count === 1), "leaf clusters do not cover every source triangle exactly once");

    // Compute source UV bounds when UVs are present so we can verify every level stays inside the source UV domain.
    let UvMin = [Infinity, Infinity], UvMax = [-Infinity, -Infinity];
    if (Leaf.CornerUVs?.length)
        for (let I = 0; I < Leaf.CornerUVs.length; I += 2)
        {
            UvMin[0] = Math.min(UvMin[0], Leaf.CornerUVs[I]);
            UvMin[1] = Math.min(UvMin[1], Leaf.CornerUVs[I + 1]);
            UvMax[0] = Math.max(UvMax[0], Leaf.CornerUVs[I]);
            UvMax[1] = Math.max(UvMax[1], Leaf.CornerUVs[I + 1]);
        }

    for (const Level of Hierarchy.Levels)
    {
        if (Level.CornerUVs)
        {
            Confirm(Level.CornerUVs.length === Level.TriangleCount * 6, "level CornerUVs length disagrees with triangle count");
            for (let I = 0; I < Level.CornerUVs.length; I += 2)
            {
                const U = Level.CornerUVs[I], V = Level.CornerUVs[I + 1];
                Confirm(Number.isFinite(U) && Number.isFinite(V), "non-finite UV coordinate in hierarchy level");
                Confirm(U >= UvMin[0] - 1e-5 && U <= UvMax[0] + 1e-5 && V >= UvMin[1] - 1e-5 && V <= UvMax[1] + 1e-5,
                    "interpolated UV escaped the source UV domain");
            }
        }
        if (Level.SharpMask)
            Confirm(Level.SharpMask.length === Level.TriangleCount, "level SharpMask length disagrees with triangle count");
        if (Level.Level === 0) continue;
        const ParentPositions = new Set();
        for (let Vertex = 0; Vertex < Level.VertexCount; ++Vertex)
            ParentPositions.add([0, 1, 2].map((A) => Level.Positions[Vertex * 3 + A] + 0).join(","));
        const ChildLevel = Hierarchy.Levels[Level.Level - 1];
        const ChildLevelEdges = BuildEdgeIncidence(ChildLevel.Triangles, ChildLevel.VertexCount,
            ChildLevel.CornerUVs, ChildLevel.SharpMask).Edges;
        for (const Identity of Level.Clusters)
        {
            const Node = Hierarchy.Clusters[Identity];
            Confirm(Node.Children.length > 0, "parent cluster has no children");
            const ChildError = Math.max(...Node.Children.map((Child) => Hierarchy.Clusters[Child].Error));
            Confirm(Node.Error >= ChildError, "cluster error is not monotonic toward the root");
            for (const Child of Node.Children)
            {
                const ChildNode = Hierarchy.Clusters[Child];
                Confirm(ChildNode.ParentError >= ChildNode.Error, "child ParentError is below its own Error");
                for (let A = 0; A < 3; ++A)
                {
                    Confirm(Node.Minimum[A] <= ChildNode.Minimum[A] + 1e-9, "parent bounds do not contain child bounds");
                    Confirm(Node.Maximum[A] >= ChildNode.Maximum[A] - 1e-9, "parent bounds do not contain child bounds");
                }
            }
            const UnionTriangles = new Set(Node.Children.flatMap((Child) => Hierarchy.Clusters[Child].Triangles));
            for (const Entry of ChildLevelEdges.values())
            {
                const Inside = Entry.Triangles.filter((Triangle) => UnionTriangles.has(Triangle));
                if (!Inside.length || Inside.length === Entry.Triangles.length && Entry.Triangles.length === 2) continue;
                for (const Vertex of [Entry.A, Entry.B])
                {
                    const Key = [0, 1, 2].map((A) => ChildLevel.Positions[Vertex * 3 + A] + 0).join(",");
                    Confirm(ParentPositions.has(Key), "shared border vertex moved during simplification (crack)");
                }
            }
        }
    }
    Checks.Levels = Hierarchy.Levels.map((Level) => ({
        Level: Level.Level, Clusters: Level.Clusters.length, Triangles: Level.TriangleCount, Vertices: Level.VertexCount,
        SharpEdges: Level.SharpEdgeCount ?? 0, UvSeams: Level.UvSeamEdgeCount ?? 0,
        MaxError: Math.max(0, ...Level.Clusters.map((Identity) => Hierarchy.Clusters[Identity].Error))
    }));
    return Checks;
}

export const HierarchyInternals = { BuildEdgeIncidence, BuildMeshlets, GroupClusters, ExtractGroupGeometry,
    SimplifyGroup, WeldLevelGeometry, MeasureCluster };

export function CollectHierarchyLevel(Hierarchy, Level)
{
    const Entry = Hierarchy.Levels[Level];
    if (!Entry) throw new Error("Hierarchy level does not exist: " + Level);
    const Incidence = BuildEdgeIncidence(Entry.Triangles, Entry.VertexCount, Entry.CornerUVs, Entry.SharpMask);
    const EdgeKey = (A, B) => (A < B ? A * Entry.VertexCount + B : B * Entry.VertexCount + A);
    const Positions = [], CornerUVs = [], SharpMask = [], UvSeamMask = [], Families = [], Owners = [], Errors = [];
    for (const Identity of Entry.Clusters)
    {
        const Node = Hierarchy.Clusters[Identity];
        for (const Triangle of Node.Triangles)
        {
            let SeamBits = 0;
            for (let Corner = 0; Corner < 3; ++Corner)
            {
                const Vertex = Entry.Triangles[Triangle * 3 + Corner];
                const NextVertex = Entry.Triangles[Triangle * 3 + (Corner + 1) % 3];
                Positions.push(Entry.Positions[Vertex * 3], Entry.Positions[Vertex * 3 + 1], Entry.Positions[Vertex * 3 + 2]);
                if (Entry.CornerUVs)
                    CornerUVs.push(Entry.CornerUVs[Triangle * 6 + Corner * 2], Entry.CornerUVs[Triangle * 6 + Corner * 2 + 1]);
                else CornerUVs.push(0, 0);
                if (Incidence.Edges.get(EdgeKey(Vertex, NextVertex))?.UvSeam) SeamBits |= 1 << Corner;
            }
            SharpMask.push(Entry.SharpMask ? Entry.SharpMask[Triangle] : 0);
            UvSeamMask.push(SeamBits);
            Families.push(Entry.Families[Triangle]);
            Owners.push(Identity);
            Errors.push(Node.Error);
        }
    }
    return {
        Positions: new Float64Array(Positions),
        CornerUVs: new Float64Array(CornerUVs),
        SharpMask: new Uint8Array(SharpMask),
        UvSeamMask: new Uint8Array(UvSeamMask),
        Families: new Uint32Array(Families),
        Owners: new Uint32Array(Owners),
        Errors: new Float64Array(Errors),
        TriangleCount: Families.length
    };
}
