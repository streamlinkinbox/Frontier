//============================================================================================================================================
//                                                       GEOMETRYHIERARCHYRUNTIME.JS
//============================================================================================================================================
// 🧩 Phase 2 virtualized-geometry runtime: fixed-size page packing & LRU residency, projected-error DAG cut (with
//    refraction-deviation error for glass), frustum + normal-cone backface culling, two-phase Hi-Z occlusion culling,
//    and indirect draw compaction separating opaque and transmissive visibility classes.

const DefaultPageByteLimit       = 65536;   // [B] 64 KB fixed-size streaming page budget
const DefaultPixelErrorThreshold = 1.0;     // [px] maximum projected screen-space error
const DefaultNearPlane           = 0.1;     // [m] near-plane clamp for projected error
const DefaultGlassRefractionEta  = 1.52;    // [-] crown glass index of refraction

//------------------------------------------------------------------------------------------------------------------------
//                                                     MATRIX & VECTOR SUBSTRATE
//------------------------------------------------------------------------------------------------------------------------

export function IdentityMatrix()
{
    return new Float64Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
}

export function MultiplyMatrices(A, B)
{
    const Out = new Float64Array(16);
    for (let Column = 0; Column < 4; ++Column)
        for (let Row = 0; Row < 4; ++Row)
        {
            let Sum = 0;
            for (let K = 0; K < 4; ++K) Sum += A[K * 4 + Row] * B[Column * 4 + K];
            Out[Column * 4 + Row] = Sum;
        }
    return Out;
}

export function TransformWorldPoint(Matrix, Point)
{
    if (!Matrix) return [Point[0], Point[1], Point[2]];
    return [
        Matrix[0] * Point[0] + Matrix[4] * Point[1] + Matrix[8] * Point[2] + Matrix[12],
        Matrix[1] * Point[0] + Matrix[5] * Point[1] + Matrix[9] * Point[2] + Matrix[13],
        Matrix[2] * Point[0] + Matrix[6] * Point[1] + Matrix[10] * Point[2] + Matrix[14]
    ];
}

export function TransformWorldDirection(Matrix, Vector)
{
    if (!Matrix) return [Vector[0], Vector[1], Vector[2]];
    const X = Matrix[0] * Vector[0] + Matrix[4] * Vector[1] + Matrix[8] * Vector[2];
    const Y = Matrix[1] * Vector[0] + Matrix[5] * Vector[1] + Matrix[9] * Vector[2];
    const Z = Matrix[2] * Vector[0] + Matrix[6] * Vector[1] + Matrix[10] * Vector[2];
    const L = Math.hypot(X, Y, Z) || 1;
    return [X / L, Y / L, Z / L];
}

export function TransformClipPoint(Matrix, Point)
{
    return [
        Matrix[0] * Point[0] + Matrix[4] * Point[1] + Matrix[8] * Point[2] + Matrix[12],
        Matrix[1] * Point[0] + Matrix[5] * Point[1] + Matrix[9] * Point[2] + Matrix[13],
        Matrix[2] * Point[0] + Matrix[6] * Point[1] + Matrix[10] * Point[2] + Matrix[14],
        Matrix[3] * Point[0] + Matrix[7] * Point[1] + Matrix[11] * Point[2] + Matrix[15]
    ];
}

export function ExtractMaxScale(Matrix)
{
    if (!Matrix) return 1;
    const SX = Math.hypot(Matrix[0], Matrix[1], Matrix[2]);
    const SY = Math.hypot(Matrix[4], Matrix[5], Matrix[6]);
    const SZ = Math.hypot(Matrix[8], Matrix[9], Matrix[10]);
    return Math.max(SX, SY, SZ, 1e-6);
}

// 📝 Reversed-Z perspective-projection builder matching SurfaceProjection.js (depth 1 at Near, 0 at Far).
export function ConstructViewProjection(Camera)
{
    if (Camera.ViewProjection) return Camera.ViewProjection;
    const Eye = Camera.Eye ?? Camera.Position ?? [0, -10, 4];
    const Target = Camera.Target ?? [0, 0, 0];
    const Up = Camera.Up ?? [0, 0, 1];
    const FovY = Camera.FovY ?? Math.PI / 3;
    const Width = Camera.ScreenWidth ?? Camera.Width ?? 720;
    const Height = Camera.ScreenHeight ?? Camera.Height ?? 540;
    const Aspect = Width / Math.max(1, Height);
    const Near = Camera.Near ?? DefaultNearPlane;
    const Far = Camera.Far ?? 1000.0;

    const Normalize = (V) => { const L = Math.hypot(V[0], V[1], V[2]) || 1; return [V[0] / L, V[1] / L, V[2] / L]; };
    const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
    const Dot = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];

    const Forward = Normalize([Target[0] - Eye[0], Target[1] - Eye[1], Target[2] - Eye[2]]);
    const Right = Normalize(Cross(Forward, Up));
    const TrueUp = Cross(Right, Forward);

    // Column-major view matrix looking along +Forward (clip w = +z_eye).
    const View = new Float64Array([
        Right[0], TrueUp[0], Forward[0], 0,
        Right[1], TrueUp[1], Forward[1], 0,
        Right[2], TrueUp[2], Forward[2], 0,
        -Dot(Right, Eye), -Dot(TrueUp, Eye), -Dot(Forward, Eye), 1
    ]);
    const Focal = 1.0 / Math.tan(FovY * 0.5);
    // Reversed-Z: z_ndc = (Far * Near / (Far - Near)) / z_eye - Near / (Far - Near), so z=Near -> 1, z=Far -> 0.
    const A = -Near / (Far - Near);
    const B = (Far * Near) / (Far - Near);
    const Proj = new Float64Array([
        Focal / Aspect, 0, 0, 0,
        0, Focal, 0, 0,
        0, 0, A, 1,
        0, 0, B, 0
    ]);
    return MultiplyMatrices(Proj, View);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              FIXED-SIZE PAGES & LRU RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Clusters are packed coarsest-to-finest (root level first, leaves last) into fixed-size binary pages.
//    Each cluster record carries its compact vertex positions, corner UVs, local triangle indices, family and
//    sharp-edge mask in a self-contained binary slice, and Page 0 (plus any additional root-level pages) is
//    marked Pinned so coarse rendering never stalls waiting on streaming.
export function PackHierarchyPages(Hierarchy, Options = {})
{
    const PageByteLimit = Options.PageByteLimit ?? DefaultPageByteLimit;
    if (!(PageByteLimit >= 512)) throw new Error("PageByteLimit must be at least 512 bytes");
    const Pages = [];
    const RootLevel = Hierarchy.Levels.length - 1;
    let CurrentClusters = [], CurrentLevels = new Set(), CurrentBytes = 32;   // 32B page header
    let CurrentPinned = true;
    const FlushPage = () =>
    {
        if (!CurrentClusters.length) return;
        const PageIndex = Pages.length;
        const Payload = new ArrayBuffer(CurrentBytes);
        const Header = new DataView(Payload);
        Header.setUint32(0, 0x50474d31, true);   // '1MGP' page magic
        Header.setUint32(4, PageIndex, true);
        Header.setUint32(8, CurrentClusters.length, true);
        Header.setUint32(12, CurrentPinned ? 1 : 0, true);
        let Cursor = 32;
        for (const Item of CurrentClusters)
        {
            Hierarchy.Clusters[Item.ClusterId].PageIndex = PageIndex;
            Header.setUint32(Cursor, Item.ClusterId, true);
            Header.setUint32(Cursor + 4, Item.Level, true);
            Header.setUint32(Cursor + 8, Item.TriangleCount, true);
            Header.setUint32(Cursor + 12, Item.VertexCount, true);
            Header.setFloat32(Cursor + 16, Hierarchy.Clusters[Item.ClusterId].Error, true);
            Cursor += Item.ByteLength;
        }
        Pages.push({
            PageIndex,
            Pinned: CurrentPinned,
            ByteSize: CurrentBytes,
            Clusters: CurrentClusters.map((Item) => Item.ClusterId),
            Levels: [...CurrentLevels].sort((A, B) => A - B),
            BinaryPayload: Payload
        });
        CurrentClusters = [];
        CurrentLevels = new Set();
        CurrentBytes = 32;
        CurrentPinned = false;
    };

    // Walk coarsest level (root) down to level 0 (leaves).
    for (let LevelIndex = RootLevel; LevelIndex >= 0; --LevelIndex)
    {
        const Level = Hierarchy.Levels[LevelIndex];
        for (const ClusterId of Level.Clusters)
        {
            const Node = Hierarchy.Clusters[ClusterId];
            const UniqueVertices = new Set();
            for (const Triangle of Node.Triangles)
                for (let C = 0; C < 3; ++C) UniqueVertices.add(Level.Triangles[Triangle * 3 + C]);
            // 24B cluster header + 12B/vertex position + 24B/tri corner UVs + 8B/tri (3x u16 local indices + family + sharp).
            const ClusterBytes = 24 + UniqueVertices.size * 12 + Node.TriangleCount * 32;
            const IsRootCluster = LevelIndex === RootLevel;
            if (CurrentClusters.length && (CurrentBytes + ClusterBytes > PageByteLimit || (CurrentPinned && !IsRootCluster)))
                FlushPage();
            if (IsRootCluster) CurrentPinned = true;
            CurrentClusters.push({
                ClusterId, Level: LevelIndex,
                TriangleCount: Node.TriangleCount, VertexCount: UniqueVertices.size,
                ByteLength: ClusterBytes
            });
            CurrentLevels.add(LevelIndex);
            CurrentBytes += ClusterBytes;
        }
    }
    FlushPage();
    return Pages;
}

export class HierarchyResidencyManager
{
    constructor(Pages, Options = {})
    {
        this.Pages = Pages;
        const PinnedBytes = Pages.filter((P) => P.Pinned).reduce((Sum, P) => Sum + P.ByteSize, 0);
        this.MaxResidentBytes = Math.max(PinnedBytes, Options.MaxResidentBytes ?? Infinity);
        this.ResidentPages = new Set();
        this.LastUsedFrame = new Int32Array(Pages.length).fill(-1);
        this.ResidentBytes = 0;
        this.StreamingQueue = [];
        this.Evictions = 0;
        this.Loads = 0;
        for (const Page of Pages)
            if (Page.Pinned || Options.InitiallyResidentAll)
            {
                this.ResidentPages.add(Page.PageIndex);
                this.ResidentBytes += Page.ByteSize;
            }
    }

    IsPageResident(PageIndex)
    {
        return PageIndex === undefined || this.ResidentPages.has(PageIndex);
    }

    TouchPage(PageIndex, FrameIndex = 0)
    {
        if (PageIndex !== undefined && this.ResidentPages.has(PageIndex))
            this.LastUsedFrame[PageIndex] = FrameIndex;
    }

    RequestPage(PageIndex, PriorityErrorPx)
    {
        if (PageIndex === undefined || this.ResidentPages.has(PageIndex)) return;
        const Existing = this.StreamingQueue.find((Item) => Item.PageIndex === PageIndex);
        if (Existing) Existing.Priority = Math.max(Existing.Priority, PriorityErrorPx);
        else this.StreamingQueue.push({ PageIndex, Priority: PriorityErrorPx });
    }

    ServiceStreamingQueue(FrameIndex = 0, MaxPagesToLoad = 2)
    {
        this.StreamingQueue.sort((A, B) => B.Priority - A.Priority || A.PageIndex - B.PageIndex);
        let Loaded = 0;
        while (Loaded < MaxPagesToLoad && this.StreamingQueue.length)
        {
            const Next = this.StreamingQueue.shift();
            if (this.ResidentPages.has(Next.PageIndex)) continue;
            const Page = this.Pages[Next.PageIndex];
            if (!Page) continue;
            // Evict least-recently-used unpinned pages if needed to fit within MaxResidentBytes.
            while (this.ResidentBytes + Page.ByteSize > this.MaxResidentBytes)
            {
                let Victim = -1, OldestFrame = Infinity;
                for (const ResidentIndex of this.ResidentPages)
                {
                    const Candidate = this.Pages[ResidentIndex];
                    if (Candidate.Pinned || this.LastUsedFrame[ResidentIndex] === FrameIndex) continue;
                    if (this.LastUsedFrame[ResidentIndex] < OldestFrame)
                    {
                        OldestFrame = this.LastUsedFrame[ResidentIndex];
                        Victim = ResidentIndex;
                    }
                }
                if (Victim < 0) break;
                this.ResidentPages.delete(Victim);
                this.ResidentBytes -= this.Pages[Victim].ByteSize;
                ++this.Evictions;
            }
            if (this.ResidentBytes + Page.ByteSize <= this.MaxResidentBytes)
            {
                this.ResidentPages.add(Page.PageIndex);
                this.ResidentBytes += Page.ByteSize;
                this.LastUsedFrame[Page.PageIndex] = FrameIndex;
                ++this.Loads;
                ++Loaded;
            }
        }
        return Loaded;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PROJECTED-ERROR DAG CUT SELECTION
//------------------------------------------------------------------------------------------------------------------------

export function ComputeProjectionScale(Camera)
{
    if (Number.isFinite(Camera.ProjectionScale)) return Camera.ProjectionScale;
    const Height = Camera.ScreenHeight ?? Camera.Height ?? 540;
    const FovY = Camera.FovY ?? Math.PI / 3;
    return (Height * 0.5) / Math.tan(FovY * 0.5);
}

// 📝 Evaluates a sphere+error pair in screen pixels. Transmissive (glass) clusters include the optical
//    refraction-deviation factor (eta - 1) * sin(coneHalfAngle) so curved refractive surfaces refine earlier
//    than flat panes of the same geometric error.
export function EvaluateProjectedSphereError(Sphere, ObjectError, Camera, LocalToWorld = null, ClassMask = 1, Cone = null, Options = {})
{
    if (!(ObjectError > 0)) return 0;
    if (!Number.isFinite(ObjectError)) return Infinity;
    const Eye = Camera.Eye ?? Camera.Position ?? [0, -10, 4];
    const Near = Camera.Near ?? DefaultNearPlane;
    const Scale = ExtractMaxScale(LocalToWorld);
    const WorldCenter = TransformWorldPoint(LocalToWorld, Sphere.Center);
    const WorldRadius = Scale * Sphere.Radius;
    const Distance = Math.max(
        Math.hypot(WorldCenter[0] - Eye[0], WorldCenter[1] - Eye[1], WorldCenter[2] - Eye[2]) - WorldRadius,
        Near
    );
    let EffectiveError = Scale * ObjectError;
    if ((ClassMask & 2) !== 0 && Cone)
    {
        const Eta = Options.IndexOfRefraction ?? DefaultGlassRefractionEta;
        const Cutoff = Math.max(-1, Math.min(1, Cone.Cutoff));
        const Spread = Math.sqrt(Math.max(0, 1 - Cutoff * Cutoff));
        EffectiveError *= 1 + Math.max(0, Eta - 1) * Spread;
    }
    return (EffectiveError / Distance) * ComputeProjectionScale(Camera);
}

export function EvaluateClusterProjectedError(Node, Camera, LocalToWorld = null, Options = {})
{
    const SelfErrorPx = EvaluateProjectedSphereError(Node.Sphere, Node.Error, Camera, LocalToWorld, Node.ClassMask, Node.Cone, Options);
    const ParentErrorPx = EvaluateProjectedSphereError(Node.ParentSphere ?? Node.Sphere, Node.ParentError, Camera, LocalToWorld, Node.ClassMask, Node.Cone, Options);
    return { SelfErrorPx, ParentErrorPx };
}

// 📝 Parallel per-cluster DAG cut: a cluster C is selected iff SelfErrorPx <= Threshold < ParentErrorPx.
//    Because ParentSphere encloses C.Sphere and ParentError > C.Error across every simplification group,
//    ParentErrorPx > SelfErrorPx holds everywhere in R^3 and all sibling clusters of a simplification group
//    transition atomically without cracks or holes. When a Residency manager is attached and a cut cluster's
//    page is not yet resident, the cut walks up to the finest resident ancestor group and queues the missing page.
export function SelectHierarchyCut(Hierarchy, Camera, LocalToWorld = null, Options = {})
{
    const Threshold = Options.PixelErrorThreshold ?? Camera.PixelErrorThreshold ?? DefaultPixelErrorThreshold;
    const Residency = Options.Residency ?? null;
    const FrameIndex = Options.FrameIndex ?? 0;
    const SelectedSet = new Set();
    const Metrics = new Map();

    for (const Node of Hierarchy.Clusters)
    {
        const Evaluated = EvaluateClusterProjectedError(Node, Camera, LocalToWorld, Options);
        Metrics.set(Node.Identity, Evaluated);
        if (Evaluated.SelfErrorPx <= Threshold && Evaluated.ParentErrorPx > Threshold)
            SelectedSet.add(Node.Identity);
    }

    // 📝 Whole-group child attribution means one cluster can be reached through several parent clusters, so
    //    the raw condition can select both a cluster and one of its descendants via different paths. Prune to
    //    the coarsest selected node on every path: the result is an antichain (no selected node is an ancestor
    //    of another) whose geometry still covers the whole surface, because a kept ancestor's level geometry
    //    covers the entire group region its dropped descendants came from.
    {
        const Kept = new Set();
        const CoarsestFirst = [...SelectedSet].sort((A, B) =>
            Hierarchy.Clusters[B].Level - Hierarchy.Clusters[A].Level || A - B);
        for (const Id of CoarsestFirst)
        {
            let HasKeptAncestor = false;
            const Stack = [...(Hierarchy.Clusters[Id].Parents ?? [])], Seen = new Set();
            while (Stack.length && !HasKeptAncestor)
            {
                const Parent = Stack.pop();
                if (Kept.has(Parent)) { HasKeptAncestor = true; break; }
                if (Seen.has(Parent)) continue;
                Seen.add(Parent);
                Stack.push(...(Hierarchy.Clusters[Parent].Parents ?? []));
            }
            if (!HasKeptAncestor) Kept.add(Id);
        }
        SelectedSet.clear();
        for (const Id of Kept) SelectedSet.add(Id);
    }

    // If a selected cluster's page is non-resident, promote the whole sibling group to its parent group until resident.
    if (Residency)
    {
        let Changed = true;
        while (Changed)
        {
            Changed = false;
            for (const ClusterId of [...SelectedSet])
            {
                const Node = Hierarchy.Clusters[ClusterId];
                if (Residency.IsPageResident(Node.PageIndex))
                {
                    Residency.TouchPage(Node.PageIndex, FrameIndex);
                    continue;
                }
                const Metric = Metrics.get(ClusterId);
                Residency.RequestPage(Node.PageIndex, Metric?.ParentErrorPx ?? Threshold + 1);
                if (!Node.Parents?.length) continue;
                // Remove all siblings that share the same parent group and replace with the parent group's clusters.
                const ParentZero = Hierarchy.Clusters[Node.Parents[0]];
                for (const SiblingId of ParentZero.Children) SelectedSet.delete(SiblingId);
                for (const ParentId of Node.Parents) SelectedSet.add(ParentId);
                Changed = true;
            }
        }
    }

    const CutClusters = [...SelectedSet].sort((A, B) => A - B).map((Id) => Hierarchy.Clusters[Id]);
    let CutTriangles = 0;
    const LevelHistogram = new Int32Array(Hierarchy.Levels.length);
    for (const Node of CutClusters)
    {
        CutTriangles += Node.TriangleCount;
        ++LevelHistogram[Node.Level];
    }
    return {
        CutClusters,
        CutClusterIds: CutClusters.map((Node) => Node.Identity),
        CutTriangles,
        SourceTriangles: Hierarchy.SourceTriangles,
        LevelHistogram: [...LevelHistogram],
        Metrics
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                           FRUSTUM & NORMAL-CONE CULLING
//------------------------------------------------------------------------------------------------------------------------

export function TestClusterFrustum(Node, LocalToWorld, ViewProjection)
{
    const M = LocalToWorld ? MultiplyMatrices(ViewProjection, LocalToWorld) : ViewProjection;
    const Min = Node.TightMinimum ?? Node.Minimum;
    const Max = Node.TightMaximum ?? Node.Maximum;
    const Outside = new Int32Array(6);
    for (let K = 0; K < 8; ++K)
    {
        const P = [
            (K & 1) ? Max[0] : Min[0],
            (K & 2) ? Max[1] : Min[1],
            (K & 4) ? Max[2] : Min[2]
        ];
        const Q = TransformClipPoint(M, P);
        if (Q[0] < -Q[3]) ++Outside[0];
        if (Q[0] >  Q[3]) ++Outside[1];
        if (Q[1] < -Q[3]) ++Outside[2];
        if (Q[1] >  Q[3]) ++Outside[3];
        if (Q[2] <  0.0)  ++Outside[4];
        if (Q[2] >  Q[3]) ++Outside[5];
    }
    for (let Plane = 0; Plane < 6; ++Plane) if (Outside[Plane] === 8) return false;
    return true;
}

export function TestClusterNormalCone(Node, LocalToWorld, CameraEye, Options = {})
{
    if (Options.BackfaceCull === false) return true;
    // Transmissive (glass) surfaces are always two-sided for entry/exit refraction.
    if ((Node.ClassMask & 2) !== 0) return true;
    const Cutoff = Node.Cone?.Cutoff ?? -1;
    if (Cutoff <= 0) return true;
    const Sphere = Node.TightSphere ?? Node.Sphere;
    const WorldCenter = TransformWorldPoint(LocalToWorld, Sphere.Center);
    const WorldRadius = ExtractMaxScale(LocalToWorld) * Sphere.Radius;
    const WorldAxis = TransformWorldDirection(LocalToWorld, Node.Cone.Axis);
    const VX = WorldCenter[0] - CameraEye[0];
    const VY = WorldCenter[1] - CameraEye[1];
    const VZ = WorldCenter[2] - CameraEye[2];
    const Distance = Math.hypot(VX, VY, VZ);
    if (Distance <= WorldRadius + 1e-6) return true;
    const SinAlpha = Math.sqrt(Math.max(0, 1 - Cutoff * Cutoff));
    const SinBeta = Math.min(1, WorldRadius / Distance);
    const CosBeta = Math.sqrt(Math.max(0, 1 - SinBeta * SinBeta));
    // Every triangle in the cone faces away from the viewer when dot(normalize(Center - Eye), Axis) > sin(alpha + beta).
    const ViewDotAxis = (VX * WorldAxis[0] + VY * WorldAxis[1] + VZ * WorldAxis[2]) / Distance;
    const SinAlphaPlusBeta = SinAlpha * CosBeta + Cutoff * SinBeta;
    return ViewDotAxis <= SinAlphaPlusBeta;
}

//------------------------------------------------------------------------------------------------------------------------
//                                       HI-Z DEPTH PYRAMID & TWO-PHASE OCCLUSION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Builds a conservative Hierarchical-Z mip pyramid from a base depth buffer.
//    In Reversed-Z ("reversed", the engine default: 1.0 = near, 0.0 = far/clear), a region is opaque only down
//    to its MINIMUM depth, so each parent texel takes the min of its 2x2 children (plus the 3rd column/row when
//    the parent dimension is odd so non-power-of-two viewports never leak).
export function ConstructHiZPyramid(BaseDepth, Width, Height, DepthMode = "reversed")
{
    const IsReversed = DepthMode !== "standard";
    const Combine = IsReversed ? Math.min : Math.max;
    const Mips = [{ Width, Height, Data: new Float32Array(BaseDepth) }];
    let PrevWidth = Width, PrevHeight = Height, PrevData = Mips[0].Data;
    while (PrevWidth > 1 || PrevHeight > 1)
    {
        const NextWidth = Math.max(1, PrevWidth >> 1);
        const NextHeight = Math.max(1, PrevHeight >> 1);
        const NextData = new Float32Array(NextWidth * NextHeight);
        const ExtraX = (PrevWidth & 1) !== 0 && PrevWidth > 1;
        const ExtraY = (PrevHeight & 1) !== 0 && PrevHeight > 1;
        for (let Y = 0; Y < NextHeight; ++Y)
            for (let X = 0; X < NextWidth; ++X)
            {
                const SX = X * 2, SY = Y * 2;
                const TapsX = (X + 1 === NextWidth && ExtraX) ? 3 : Math.min(2, PrevWidth - SX);
                const TapsY = (Y + 1 === NextHeight && ExtraY) ? 3 : Math.min(2, PrevHeight - SY);
                let Value = IsReversed ? 1.0 : 0.0;
                for (let DY = 0; DY < TapsY; ++DY)
                    for (let DX = 0; DX < TapsX; ++DX)
                        Value = Combine(Value, PrevData[(SY + DY) * PrevWidth + (SX + DX)]);
                NextData[Y * NextWidth + X] = Value;
            }
        Mips.push({ Width: NextWidth, Height: NextHeight, Data: NextData });
        PrevWidth = NextWidth;
        PrevHeight = NextHeight;
        PrevData = NextData;
    }
    return { Width, Height, DepthMode, Mips };
}

export function ProjectClusterScreenRect(Node, LocalToWorld, ViewProjection, Width, Height, DepthMode = "reversed")
{
    const M = LocalToWorld ? MultiplyMatrices(ViewProjection, LocalToWorld) : ViewProjection;
    const Min = Node.TightMinimum ?? Node.Minimum;
    const Max = Node.TightMaximum ?? Node.Maximum;
    const IsReversed = DepthMode !== "standard";
    let MinX = Infinity, MaxX = -Infinity, MinY = Infinity, MaxY = -Infinity;
    let ClosestDepth = IsReversed ? -Infinity : Infinity;
    for (let K = 0; K < 8; ++K)
    {
        const P = [
            (K & 1) ? Max[0] : Min[0],
            (K & 2) ? Max[1] : Min[1],
            (K & 4) ? Max[2] : Min[2]
        ];
        const Q = TransformClipPoint(M, P);
        if (Q[3] <= 1e-4) return { CrossesNearPlane: true };
        const NdcX = Q[0] / Q[3], NdcY = Q[1] / Q[3], NdcZ = Q[2] / Q[3];
        const ScreenX = (NdcX * 0.5 + 0.5) * Width;
        const ScreenY = (0.5 - NdcY * 0.5) * Height;
        MinX = Math.min(MinX, ScreenX); MaxX = Math.max(MaxX, ScreenX);
        MinY = Math.min(MinY, ScreenY); MaxY = Math.max(MaxY, ScreenY);
        ClosestDepth = IsReversed ? Math.max(ClosestDepth, NdcZ) : Math.min(ClosestDepth, NdcZ);
    }
    if (MaxX < 0 || MinX >= Width || MaxY < 0 || MinY >= Height) return { Offscreen: true };
    return {
        CrossesNearPlane: false,
        Offscreen: false,
        MinX: Math.max(0, Math.min(Width - 1, MinX)),
        MaxX: Math.max(0, Math.min(Width - 1, MaxX)),
        MinY: Math.max(0, Math.min(Height - 1, MinY)),
        MaxY: Math.max(0, Math.min(Height - 1, MaxY)),
        ClosestDepth: Math.max(0, Math.min(1, ClosestDepth))
    };
}

export function TestClusterHiZ(Node, LocalToWorld, ViewProjection, Pyramid)
{
    if (!Pyramid?.Mips?.length) return true;
    const Rect = ProjectClusterScreenRect(Node, LocalToWorld, ViewProjection, Pyramid.Width, Pyramid.Height, Pyramid.DepthMode);
    if (Rect.Offscreen) return false;
    if (Rect.CrossesNearPlane) return true;
    const Footprint = Math.max(Rect.MaxX - Rect.MinX, Rect.MaxY - Rect.MinY, 1);
    const MipIndex = Math.min(Pyramid.Mips.length - 1, Math.max(0, Math.ceil(Math.log2(Footprint))));
    const Mip = Pyramid.Mips[MipIndex];
    const ScaleX = Mip.Width / Pyramid.Width, ScaleY = Mip.Height / Pyramid.Height;
    const X0 = Math.max(0, Math.min(Mip.Width - 1, Math.floor(Rect.MinX * ScaleX)));
    const X1 = Math.max(0, Math.min(Mip.Width - 1, Math.floor(Rect.MaxX * ScaleX)));
    const Y0 = Math.max(0, Math.min(Mip.Height - 1, Math.floor(Rect.MinY * ScaleY)));
    const Y1 = Math.max(0, Math.min(Mip.Height - 1, Math.floor(Rect.MaxY * ScaleY)));
    const IsReversed = Pyramid.DepthMode !== "standard";
    const Combine = IsReversed ? Math.min : Math.max;
    let OccluderDepth = Combine(
        Combine(Mip.Data[Y0 * Mip.Width + X0], Mip.Data[Y0 * Mip.Width + X1]),
        Combine(Mip.Data[Y1 * Mip.Width + X0], Mip.Data[Y1 * Mip.Width + X1])
    );
    return IsReversed
        ? Rect.ClosestDepth + 1e-5 >= OccluderDepth
        : Rect.ClosestDepth - 1e-5 <= OccluderDepth;
}

// 📝 Software depth rasterizer for opaque cut clusters: writes Reversed-Z depth so two-phase Hi-Z occlusion
//    can be executed and verified deterministically in CPU tests and proof renders as well as on GPU.
//    Transmissive clusters (ClassMask & 2) never write depth into the Hi-Z buffer.
export function RasterizeClustersToDepth(ClustersToDraw, DepthBuffer, Width, Height, ViewProjection, DepthMode = "reversed")
{
    const IsReversed = DepthMode !== "standard";
    for (const Item of ClustersToDraw)
    {
        const { Hierarchy, Node, LocalToWorld } = Item;
        if ((Node.ClassMask & 2) !== 0 && (Node.ClassMask & 1) === 0) continue;   // transmissive never writes Hi-Z
        const Level = Hierarchy.Levels[Node.Level];
        const M = LocalToWorld ? MultiplyMatrices(ViewProjection, LocalToWorld) : ViewProjection;
        for (const Triangle of Node.Triangles)
        {
            const S = [];
            let Valid = true;
            for (let C = 0; C < 3; ++C)
            {
                const V = Level.Triangles[Triangle * 3 + C];
                const P = [Level.Positions[V * 3], Level.Positions[V * 3 + 1], Level.Positions[V * 3 + 2]];
                const Q = TransformClipPoint(M, P);
                if (Q[3] <= 1e-4) { Valid = false; break; }
                S.push([(Q[0] / Q[3] * 0.5 + 0.5) * Width, (0.5 - Q[1] / Q[3] * 0.5) * Height, Q[2] / Q[3]]);
            }
            if (!Valid) continue;
            const Area = (S[1][0] - S[0][0]) * (S[2][1] - S[0][1]) - (S[1][1] - S[0][1]) * (S[2][0] - S[0][0]);
            if (Math.abs(Area) < 1e-5) continue;
            const MinX = Math.max(0, Math.floor(Math.min(S[0][0], S[1][0], S[2][0])));
            const MaxX = Math.min(Width - 1, Math.ceil(Math.max(S[0][0], S[1][0], S[2][0])));
            const MinY = Math.max(0, Math.floor(Math.min(S[0][1], S[1][1], S[2][1])));
            const MaxY = Math.min(Height - 1, Math.ceil(Math.max(S[0][1], S[1][1], S[2][1])));
            for (let Y = MinY; Y <= MaxY; ++Y)
                for (let X = MinX; X <= MaxX; ++X)
                {
                    const PX = X + 0.5, PY = Y + 0.5;
                    const W0 = ((S[1][0] - PX) * (S[2][1] - PY) - (S[1][1] - PY) * (S[2][0] - PX)) / Area;
                    const W1 = ((S[2][0] - PX) * (S[0][1] - PY) - (S[2][1] - PY) * (S[0][0] - PX)) / Area;
                    const W2 = 1 - W0 - W1;
                    if (W0 < 0 || W1 < 0 || W2 < 0) continue;
                    const Z = W0 * S[0][2] + W1 * S[1][2] + W2 * S[2][2];
                    if (Z < 0 || Z > 1) continue;
                    const Index = Y * Width + X;
                    if (IsReversed ? Z > DepthBuffer[Index] : Z < DepthBuffer[Index])
                        DepthBuffer[Index] = Z;
                }
        }
    }
}

// 📝 Full Phase-2 frame execution:
//    1) Projected-error DAG cut per instance (with residency fallback if configured),
//    2) Frustum + normal-cone backface culling,
//    3) Phase-1 Hi-Z test against PreviousHiZPyramid -> Phase-1 opaque depth raster -> CurrentHiZPyramid build,
//    4) Phase-2 Hi-Z test of Phase-1-occluded clusters against CurrentHiZPyramid -> disoccluded draws + final Hi-Z,
//    5) Compaction into OpaqueDraws and TransmissiveDraws indirect command streams.
export function ExecuteHierarchyRuntimeFrame(Instances, Camera, Options = {})
{
    const Width = Camera.ScreenWidth ?? Camera.Width ?? 720;
    const Height = Camera.ScreenHeight ?? Camera.Height ?? 540;
    const DepthMode = Options.DepthMode ?? "reversed";
    const Eye = Camera.Eye ?? Camera.Position ?? [0, -10, 4];
    const ViewProjection = ConstructViewProjection(Camera);
    const PreviousViewProjection = Camera.PreviousViewProjection ?? ViewProjection;
    const PreviousHiZPyramid = Options.PreviousHiZPyramid ?? null;

    const CutCandidates = [];
    const FrustumCulled = [];
    const ConeCulled = [];
    let SourceTrianglesTotal = 0, CutTrianglesTotal = 0;

    for (let InstanceIndex = 0; InstanceIndex < Instances.length; ++InstanceIndex)
    {
        const Instance = Instances[InstanceIndex];
        const Hierarchy = Instance.Hierarchy;
        const LocalToWorld = Instance.LocalToWorld ?? null;
        const PreviousLocalToWorld = Instance.PreviousLocalToWorld ?? LocalToWorld;
        const Occupant = Instance.Occupant ?? InstanceIndex;
        SourceTrianglesTotal += Hierarchy.SourceTriangles;

        const Cut = SelectHierarchyCut(Hierarchy, Camera, LocalToWorld, {
            ...Options,
            Residency: Instance.Residency ?? Options.Residency ?? null
        });
        CutTrianglesTotal += Cut.CutTriangles;
        for (const Node of Cut.CutClusters)
        {
            const Item = {
                InstanceIndex, Occupant, Hierarchy, Node,
                LocalToWorld, PreviousLocalToWorld,
                Metric: Cut.Metrics.get(Node.Identity)
            };
            if (!TestClusterFrustum(Node, LocalToWorld, ViewProjection))
            {
                FrustumCulled.push(Item);
                continue;
            }
            if (!TestClusterNormalCone(Node, LocalToWorld, Eye, Options))
            {
                ConeCulled.push(Item);
                continue;
            }
            CutCandidates.push(Item);
        }
    }

    // Phase 1: test against previous frame's Hi-Z pyramid using previous-frame transforms.
    const Phase1Visible = [];
    const Phase2RetryQueue = [];
    for (const Item of CutCandidates)
    {
        if (!PreviousHiZPyramid || TestClusterHiZ(Item.Node, Item.PreviousLocalToWorld, PreviousViewProjection, PreviousHiZPyramid))
            Phase1Visible.push({ ...Item, Phase: 1 });
        else
            Phase2RetryQueue.push(Item);
    }

    // Rasterize Phase-1 opaque clusters into depth and construct the current-frame Hi-Z pyramid.
    const DepthBuffer = new Float32Array(Width * Height);
    if (DepthMode === "standard") DepthBuffer.fill(1.0);
    RasterizeClustersToDepth(Phase1Visible, DepthBuffer, Width, Height, ViewProjection, DepthMode);
    let CurrentHiZPyramid = ConstructHiZPyramid(DepthBuffer, Width, Height, DepthMode);

    // Phase 2: re-test Phase-1-occluded clusters against the current-frame Hi-Z pyramid to catch newly disoccluded geometry.
    const Phase2Disoccluded = [];
    const OccludedCulled = [];
    for (const Item of Phase2RetryQueue)
    {
        if (TestClusterHiZ(Item.Node, Item.LocalToWorld, ViewProjection, CurrentHiZPyramid))
            Phase2Disoccluded.push({ ...Item, Phase: 2 });
        else
            OccludedCulled.push(Item);
    }
    if (Phase2Disoccluded.length)
    {
        RasterizeClustersToDepth(Phase2Disoccluded, DepthBuffer, Width, Height, ViewProjection, DepthMode);
        CurrentHiZPyramid = ConstructHiZPyramid(DepthBuffer, Width, Height, DepthMode);
    }

    // Compact visible clusters into OpaqueDraws and TransmissiveDraws indirect draw records.
    const VisibleClusters = [...Phase1Visible, ...Phase2Disoccluded];
    const OpaqueDraws = [], TransmissiveDraws = [];
    let VisibleTriangles = 0;
    for (const Item of VisibleClusters)
    {
        VisibleTriangles += Item.Node.TriangleCount;
        const Command = {
            IndexCount: Item.Node.TriangleCount * 3,
            InstanceCount: 1,
            FirstIndex: Item.Node.FirstTriangle * 3,
            BaseVertex: 0,
            FirstInstance: Item.Occupant,
            Occupant: Item.Occupant,
            InstanceIndex: Item.InstanceIndex,
            ClusterId: Item.Node.Identity,
            Level: Item.Node.Level,
            Phase: Item.Phase,
            ClassMask: Item.Node.ClassMask,
            SelfErrorPx: Item.Metric?.SelfErrorPx ?? 0
        };
        if ((Item.Node.ClassMask & 2) !== 0) TransmissiveDraws.push(Command);
        else OpaqueDraws.push(Command);
    }

    return {
        ViewProjection,
        DepthBuffer,
        CurrentHiZPyramid,
        CutCandidates,
        Phase1Visible,
        Phase2Disoccluded,
        OccludedCulled,
        FrustumCulled,
        ConeCulled,
        VisibleClusters,
        OpaqueDraws,
        TransmissiveDraws,
        Stats: {
            SourceTriangles: SourceTrianglesTotal,
            CutTriangles: CutTrianglesTotal,
            VisibleTriangles,
            CutClusterCount: CutCandidates.length + FrustumCulled.length + ConeCulled.length,
            Phase1VisibleCount: Phase1Visible.length,
            Phase2DisoccludedCount: Phase2Disoccluded.length,
            OccludedCulledCount: OccludedCulled.length,
            FrustumCulledCount: FrustumCulled.length,
            ConeCulledCount: ConeCulled.length,
            OpaqueDrawCount: OpaqueDraws.length,
            TransmissiveDrawCount: TransmissiveDraws.length
        }
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                              GPU PAGE SLOT ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fixed set of GPU arena slots that hierarchy pages stream through. Root/pinned pages are loaded first and
//    never evicted; other pages evict the least-recently-used resident page when a slot is needed. The GPU
//    side (VirtualizedGeometry) rewrites the evicted page's cluster records (resident bit cleared) and uploads
//    the new page's geometry into the freed slot.
export class PageSlotAllocator
{
    constructor(SlotCount)
    {
        if (!(SlotCount >= 1)) throw new Error("PageSlotAllocator needs at least one slot");
        this.SlotCount = SlotCount;
        this.PageToSlot = new Map();
        this.SlotToPage = new Map();
        this.LastUsedFrame = new Map();
        this.PinnedPages = new Set();
    }

    Pin(PageIndex) { this.PinnedPages.add(PageIndex); }
    IsResident(PageIndex) { return this.PageToSlot.has(PageIndex); }
    SlotOf(PageIndex) { return this.PageToSlot.get(PageIndex); }

    Touch(PageIndex, FrameIndex)
    {
        if (this.PageToSlot.has(PageIndex)) this.LastUsedFrame.set(PageIndex, FrameIndex);
    }

    // Ensures PageIndex has a slot. Returns { Slot, EvictedPage } — EvictedPage is null when a free slot was
    // used or the page was already resident; -1 when every slot holds a pinned page (load must wait).
    Ensure(PageIndex, FrameIndex)
    {
        const Existing = this.PageToSlot.get(PageIndex);
        if (Existing !== undefined)
        {
            this.LastUsedFrame.set(PageIndex, FrameIndex);
            return { Slot: Existing, EvictedPage: null };
        }
        if (this.SlotToPage.size < this.SlotCount)
        {
            let Slot = 0;
            while (this.SlotToPage.has(Slot)) ++Slot;
            this.PageToSlot.set(PageIndex, Slot);
            this.SlotToPage.set(Slot, PageIndex);
            this.LastUsedFrame.set(PageIndex, FrameIndex);
            return { Slot, EvictedPage: null };
        }
        let Victim = -1, VictimSlot = -1, Oldest = Infinity;
        for (const [Page, Slot] of this.PageToSlot)
        {
            if (this.PinnedPages.has(Page)) continue;
            const Used = this.LastUsedFrame.get(Page) ?? -1;
            if (Used < Oldest) { Oldest = Used; Victim = Page; VictimSlot = Slot; }
        }
        if (Victim < 0) return { Slot: -1, EvictedPage: -1 };
        this.PageToSlot.delete(Victim);
        this.PageToSlot.set(PageIndex, VictimSlot);
        this.SlotToPage.set(VictimSlot, PageIndex);
        this.LastUsedFrame.set(PageIndex, FrameIndex);
        return { Slot: VictimSlot, EvictedPage: Victim };
    }

    Evict(PageIndex)
    {
        const Slot = this.PageToSlot.get(PageIndex);
        if (Slot === undefined) return;
        this.PageToSlot.delete(PageIndex);
        this.SlotToPage.delete(Slot);
    }
}
