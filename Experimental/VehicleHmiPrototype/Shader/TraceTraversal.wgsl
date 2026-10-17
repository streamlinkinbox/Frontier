//============================================================================================================================================
//                                                           TRACETRAVERSAL.WGSL
//============================================================================================================================================
// 🧩 Two-level BVH traversal on compute: a TLAS over instances, a BLAS per facet set, Möller–Trumbore triangles.

// 📝 Rays are traced in world space through the TLAS; each instance leaf transforms the ray into the set's local
//    frame (direction left unnormalised so the parametric distance is shared) and walks the BLAS with a stack of its
//    own. Children are visited near first. Triangles are two-sided. A hit returns the interpolated shading normal
//    (world, facing the ray), the geometric normal, the material family (with the occupant's override) and the
//    occupant, which is what light sampling, reconnection and motion carrying need.

//! include "TraceRecords.wgsl"

struct TraceHit
{
    Hit       : bool,
    Travel    : f32,
    Position  : vec3f,
    Normal    : vec3f,    // [-] shading normal, facing the arriving ray
    Geometric : vec3f,    // [-] face normal, facing the arriving ray
    Family    : u32,
    Occupant  : u32,
    Triangle  : u32,
    Instance  : u32,
    Barycentric : vec2f,
}

var<private> TlasStack : array<u32, 24>;
var<private> BlasStack : array<u32, 32>;

// 📐 Slab test; returns the entry distance, or a value past Limit on a miss.
fn IntersectBounds(Node: BvhNode, Origin: vec3f, InverseDirection: vec3f, Limit: f32) -> f32
{
    let T0 = (Node.Minimum - Origin) * InverseDirection;
    let T1 = (Node.Maximum - Origin) * InverseDirection;
    let Near = min(T0, T1);
    let Far  = max(T0, T1);
    let Entry = max(max(Near.x, Near.y), max(Near.z, 0.0));
    let Exit  = min(min(Far.x, Far.y), min(Far.z, Limit));
    return select(Limit * 2.0 + 1.0, Entry, Entry <= Exit);
}

// 📐 Möller–Trumbore, two-sided: (t, u, v) with t < 0 on a miss.
fn IntersectTriangle(Record: TriangleRecord, Origin: vec3f, Direction: vec3f) -> vec3f
{
    let P = cross(Direction, Record.E2);
    let Determinant = dot(Record.E1, P);
    if (abs(Determinant) < 1.0e-12)
    {
        return vec3f(-1.0);
    }
    let Inverse = 1.0 / Determinant;
    let T = Origin - Record.V0;
    let U = dot(T, P) * Inverse;
    if (U < 0.0 || U > 1.0)
    {
        return vec3f(-1.0);
    }
    let Q = cross(T, Record.E1);
    let V = dot(Direction, Q) * Inverse;
    if (V < 0.0 || U + V > 1.0)
    {
        return vec3f(-1.0);
    }
    return vec3f(dot(Record.E2, Q) * Inverse, U, V);
}

fn SkipTriangle(Family: u32, Flags: u32) -> bool
{
    return (Family == TraceGlassFamily && (Flags & InstanceFlagVehicle) != 0u)
        || (Family == TraceTyreFamily && (Flags & InstanceFlagSkipTyre) != 0u);
}

// 📝 Walks one BLAS with a local-space ray. Closest-hit mode shrinks Limit and records the triangle; any-hit mode
//    returns on the first intersection.
fn TraverseBlas(Root: u32, Origin: vec3f, Direction: vec3f, Flags: u32, AnyHit: bool,
                Limit: ptr<function, f32>, Triangle: ptr<function, u32>, Barycentric: ptr<function, vec2f>) -> bool
{
    let InverseDirection = 1.0 / select(Direction, vec3f(1.0e-30), abs(Direction) < vec3f(1.0e-30));
    var Depth = 0u;
    BlasStack[0] = Root;
    Depth = 1u;
    var Found = false;
    var Steps = 0u;
    while (Depth > 0u && Steps < 4096u)
    {
        Steps += 1u;
        Depth -= 1u;
        let Node = Nodes[BlasStack[Depth]];
        if (Node.Count == 0u)
        {
            let Left  = Nodes[Node.LeftOrFirst];
            let Right = Nodes[Node.LeftOrFirst + 1u];
            var LeftEntry  = IntersectBounds(Left, Origin, InverseDirection, *Limit);
            var RightEntry = IntersectBounds(Right, Origin, InverseDirection, *Limit);
            var First  = Node.LeftOrFirst;
            var Second = Node.LeftOrFirst + 1u;
            if (RightEntry < LeftEntry)
            {
                let Swap = LeftEntry; LeftEntry = RightEntry; RightEntry = Swap;
                First = Node.LeftOrFirst + 1u; Second = Node.LeftOrFirst;
            }
            if (RightEntry <= *Limit && Depth < 31u)
            {
                BlasStack[Depth] = Second;
                Depth += 1u;
            }
            if (LeftEntry <= *Limit && Depth < 31u)
            {
                BlasStack[Depth] = First;
                Depth += 1u;
            }
            continue;
        }
        for (var Index = Node.LeftOrFirst; Index < Node.LeftOrFirst + Node.Count; Index++)
        {
            let Record = Triangles[Index];
            if (SkipTriangle(Record.Packed & 0xFFFFu, Flags))
            {
                continue;
            }
            let Result = IntersectTriangle(Record, Origin, Direction);
            if (Result.x > 0.0 && Result.x < *Limit)
            {
                *Limit = Result.x;
                *Triangle = Index;
                *Barycentric = Result.yz;
                Found = true;
                if (AnyHit)
                {
                    return true;
                }
            }
        }
    }
    return Found;
}

// 📝 Transforms a direction by the transpose of the instance's upper 3×3 world→local (the normal transform).
fn RotateNormalToWorld(Instance: InstanceRecord, LocalNormal: vec3f) -> vec3f
{
    // (M⁻¹)ᵀ n with M⁻¹ = WorldToLocal: component i is the dot of column i with n.
    let Column0 = Instance.WorldToLocal[0].xyz;
    let Column1 = Instance.WorldToLocal[1].xyz;
    let Column2 = Instance.WorldToLocal[2].xyz;
    return normalize(vec3f(dot(Column0, LocalNormal), dot(Column1, LocalNormal), dot(Column2, LocalNormal)));
}

fn ResolveHitFamily(Record: TriangleRecord, Occupant: OccupantRecord) -> u32
{
    let Authored = Record.Packed & 0xFFFFu;
    return select(Authored, Occupant.MaterialOverride, Occupant.MaterialOverride != NoMaterialOverride);
}

// 📝 Closest hit through the whole scene.
fn TraceClosest(Origin: vec3f, Direction: vec3f, MaximumTravel: f32) -> TraceHit
{
    var Hit: TraceHit;
    Hit.Hit = false;
    Hit.Travel = MaximumTravel;
    if (Trace.InstanceCount == 0u)
    {
        return Hit;
    }
    let InverseDirection = 1.0 / select(Direction, vec3f(1.0e-30), abs(Direction) < vec3f(1.0e-30));
    var Limit = MaximumTravel;
    var BestTriangle = 0u;
    var BestInstance = 0u;
    var BestBarycentric = vec2f(0.0);
    var Depth = 1u;
    TlasStack[0] = Trace.TlasRoot;
    var Steps = 0u;
    while (Depth > 0u && Steps < 2048u)
    {
        Steps += 1u;
        Depth -= 1u;
        let Node = Nodes[TlasStack[Depth]];
        if (Node.Count == 0u)
        {
            let Left  = Nodes[Node.LeftOrFirst];
            let Right = Nodes[Node.LeftOrFirst + 1u];
            var LeftEntry  = IntersectBounds(Left, Origin, InverseDirection, Limit);
            var RightEntry = IntersectBounds(Right, Origin, InverseDirection, Limit);
            var First  = Node.LeftOrFirst;
            var Second = Node.LeftOrFirst + 1u;
            if (RightEntry < LeftEntry)
            {
                let Swap = LeftEntry; LeftEntry = RightEntry; RightEntry = Swap;
                First = Node.LeftOrFirst + 1u; Second = Node.LeftOrFirst;
            }
            if (RightEntry <= Limit && Depth < 23u)
            {
                TlasStack[Depth] = Second;
                Depth += 1u;
            }
            if (LeftEntry <= Limit && Depth < 23u)
            {
                TlasStack[Depth] = First;
                Depth += 1u;
            }
            continue;
        }
        for (var Index = Node.LeftOrFirst; Index < Node.LeftOrFirst + Node.Count; Index++)
        {
            let Instance = Instances[Index];
            let LocalOrigin    = (Instance.WorldToLocal * vec4f(Origin, 1.0)).xyz;
            let LocalDirection = (Instance.WorldToLocal * vec4f(Direction, 0.0)).xyz;
            var Triangle = 0u;
            var Barycentric = vec2f(0.0);
            if (TraverseBlas(Instance.BlasRoot, LocalOrigin, LocalDirection, Instance.Flags, false, &Limit, &Triangle, &Barycentric))
            {
                BestTriangle = Triangle;
                BestInstance = Index;
                BestBarycentric = Barycentric;
                Hit.Hit = true;
            }
        }
    }
    if (!Hit.Hit)
    {
        return Hit;
    }
    let Instance = Instances[BestInstance];
    let Record = Triangles[BestTriangle];
    let Occupant = Occupants[Instance.Occupant];
    let Weights = vec3f(1.0 - BestBarycentric.x - BestBarycentric.y, BestBarycentric.x, BestBarycentric.y);
    let LocalShading = UnpackOctahedral(Record.N0) * Weights.x + UnpackOctahedral(Record.N1) * Weights.y + UnpackOctahedral(Record.N2) * Weights.z;
    let LocalFace = cross(Record.E1, Record.E2);
    var Geometric = RotateNormalToWorld(Instance, LocalFace);
    var Normal = RotateNormalToWorld(Instance, select(LocalShading, LocalFace, dot(LocalShading, LocalShading) < 1.0e-6));
    // 📝 Two-sided: both normals face the arriving ray; a shading normal that would still face away is bent to the face.
    if (dot(Geometric, Direction) > 0.0)
    {
        Geometric = -Geometric;
    }
    if (dot(Normal, Geometric) < 0.0)
    {
        Normal = -Normal;
    }
    if (dot(Normal, Direction) > -1.0e-3)
    {
        Normal = normalize(Normal - Direction * (dot(Normal, Direction) + 1.0e-3));
    }
    Hit.Travel = Limit;
    Hit.Position = Origin + Direction * Limit;
    Hit.Normal = Normal;
    Hit.Geometric = Geometric;
    Hit.Family = ResolveHitFamily(Record, Occupant);
    Hit.Occupant = Instance.Occupant;
    Hit.Triangle = BestTriangle;
    Hit.Instance = BestInstance;
    Hit.Barycentric = BestBarycentric;
    return Hit;
}

// 📝 Any hit before MaximumTravel: true when the segment is blocked.
fn TraceOccluded(Origin: vec3f, Direction: vec3f, MaximumTravel: f32) -> bool
{
    if (Trace.InstanceCount == 0u)
    {
        return false;
    }
    let InverseDirection = 1.0 / select(Direction, vec3f(1.0e-30), abs(Direction) < vec3f(1.0e-30));
    var Limit = MaximumTravel;
    var Depth = 1u;
    TlasStack[0] = Trace.TlasRoot;
    var Steps = 0u;
    while (Depth > 0u && Steps < 2048u)
    {
        Steps += 1u;
        Depth -= 1u;
        let Node = Nodes[TlasStack[Depth]];
        if (Node.Count == 0u)
        {
            let Left  = Nodes[Node.LeftOrFirst];
            let Right = Nodes[Node.LeftOrFirst + 1u];
            if (IntersectBounds(Left, Origin, InverseDirection, Limit) <= Limit && Depth < 23u)
            {
                TlasStack[Depth] = Node.LeftOrFirst;
                Depth += 1u;
            }
            if (IntersectBounds(Right, Origin, InverseDirection, Limit) <= Limit && Depth < 23u)
            {
                TlasStack[Depth] = Node.LeftOrFirst + 1u;
                Depth += 1u;
            }
            continue;
        }
        for (var Index = Node.LeftOrFirst; Index < Node.LeftOrFirst + Node.Count; Index++)
        {
            let Instance = Instances[Index];
            let LocalOrigin    = (Instance.WorldToLocal * vec4f(Origin, 1.0)).xyz;
            let LocalDirection = (Instance.WorldToLocal * vec4f(Direction, 0.0)).xyz;
            var Triangle = 0u;
            var Barycentric = vec2f(0.0);
            if (TraverseBlas(Instance.BlasRoot, LocalOrigin, LocalDirection, Instance.Flags, true, &Limit, &Triangle, &Barycentric))
            {
                return true;
            }
        }
    }
    return false;
}

// 📐 Ray Tracing Gems ch. 6: offsets a surface point along its geometric normal by a few ulps, scale-aware.
fn OffsetRayOrigin(Position: vec3f, Normal: vec3f) -> vec3f
{
    let IntegerOffset = vec3i(Normal * 256.0);
    let Shifted = vec3f(bitcast<f32>(bitcast<i32>(Position.x) + select(IntegerOffset.x, -IntegerOffset.x, Position.x < 0.0)),
                        bitcast<f32>(bitcast<i32>(Position.y) + select(IntegerOffset.y, -IntegerOffset.y, Position.y < 0.0)),
                        bitcast<f32>(bitcast<i32>(Position.z) + select(IntegerOffset.z, -IntegerOffset.z, Position.z < 0.0)));
    let Small = Position + Normal * (1.0 / 65536.0);
    return select(Shifted, Small, abs(Position) < vec3f(1.0 / 32.0));
}

// 📝 Visibility between two surface points, each offset off its own surface toward the other.
fn ResolveSegmentVisible(From: vec3f, FromNormal: vec3f, To: vec3f, ToNormal: vec3f) -> bool
{
    let Toward = To - From;
    let Distance = length(Toward);
    if (Distance < 1.0e-4)
    {
        return true;
    }
    let Direction = Toward / Distance;
    let Start = OffsetRayOrigin(From, select(-FromNormal, FromNormal, dot(FromNormal, Direction) >= 0.0));
    let End   = OffsetRayOrigin(To, select(-ToNormal, ToNormal, dot(ToNormal, -Direction) >= 0.0));
    let Segment = End - Start;
    let Reach = length(Segment);
    return !TraceOccluded(Start, Segment / max(Reach, 1.0e-6), Reach * (1.0 - 1.0e-4));
}

// 📝 Visibility from a surface point toward a far direction (sun, sky).
fn ResolveDirectionVisible(From: vec3f, FromNormal: vec3f, Direction: vec3f, Reach: f32) -> bool
{
    let Start = OffsetRayOrigin(From, select(-FromNormal, FromNormal, dot(FromNormal, Direction) >= 0.0));
    return !TraceOccluded(Start, Direction, Reach);
}
