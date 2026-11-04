/*==========================================================================================================================================
                                                         NESTINGSELECTION.CPP
==========================================================================================================================================*/
// 🧩 GeometryHierarchyRuntime.js statement for statement: matrices, pages, residency, cut, culling, Hi-Z, depth raster, refresh
//    execution and page slot allocation.
#include "NestingSelection.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace Frontier::VirtualizedGeometry
{
namespace
{
constexpr double DefaultPixelErrorThreshold = 1.0;    // [px]
constexpr double DefaultGlassRefractionEta  = 1.52;   // [-]

// 📝 JavaScript `Math.hypot(...) || 1`: zero and NaN both fall back to 1.
[[nodiscard]] double HypotOrUnit(double X, double Y, double Z) noexcept
{
    const double Magnitude = JsHypot(X, Y, Z);
    return (Magnitude == 0.0 || std::isnan(Magnitude)) ? 1.0 : Magnitude;
}
[[nodiscard]] ColumnMatrix ClipFromLocal(const ColumnMatrix* LocalToWorld, const ColumnMatrix& ViewProjection) noexcept
{
    return LocalToWorld ? MultiplyMatrices(ViewProjection, *LocalToWorld) : ViewProjection;
}
[[nodiscard]] Point3 BoxCorner(const Point3& Minimum, const Point3& Maximum, uint32_t K) noexcept
{
    return {(K & 1u) ? Maximum[0] : Minimum[0], (K & 2u) ? Maximum[1] : Minimum[1], (K & 4u) ? Maximum[2] : Minimum[2]};
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     MATRIX & VECTOR SUBSTRATE
//------------------------------------------------------------------------------------------------------------------------

ColumnMatrix IdentityMatrix() noexcept { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

ColumnMatrix MultiplyMatrices(const ColumnMatrix& A, const ColumnMatrix& B) noexcept
{
    ColumnMatrix Out{};
    for (int Column = 0; Column < 4; ++Column)
        for (int Row = 0; Row < 4; ++Row)
        {
            double Sum = 0;
            for (int K = 0; K < 4; ++K) Sum += A[K * 4 + Row] * B[Column * 4 + K];
            Out[Column * 4 + Row] = Sum;
        }
    return Out;
}

Point3 TransformWorldPoint(const ColumnMatrix* Matrix, const Point3& Point) noexcept
{
    if (!Matrix) return Point;
    const ColumnMatrix& M = *Matrix;
    return {M[0] * Point[0] + M[4] * Point[1] + M[8] * Point[2] + M[12], M[1] * Point[0] + M[5] * Point[1] + M[9] * Point[2] + M[13],
            M[2] * Point[0] + M[6] * Point[1] + M[10] * Point[2] + M[14]};
}

Point3 TransformWorldDirection(const ColumnMatrix* Matrix, const Point3& Vector) noexcept
{
    if (!Matrix) return Vector;
    const ColumnMatrix& M = *Matrix;
    const double X = M[0] * Vector[0] + M[4] * Vector[1] + M[8] * Vector[2];
    const double Y = M[1] * Vector[0] + M[5] * Vector[1] + M[9] * Vector[2];
    const double Z = M[2] * Vector[0] + M[6] * Vector[1] + M[10] * Vector[2];
    const double L = HypotOrUnit(X, Y, Z);
    return {X / L, Y / L, Z / L};
}

std::array<double, 4> TransformClipPoint(const ColumnMatrix& M, const Point3& Point) noexcept
{
    return {M[0] * Point[0] + M[4] * Point[1] + M[8] * Point[2] + M[12], M[1] * Point[0] + M[5] * Point[1] + M[9] * Point[2] + M[13],
            M[2] * Point[0] + M[6] * Point[1] + M[10] * Point[2] + M[14], M[3] * Point[0] + M[7] * Point[1] + M[11] * Point[2] + M[15]};
}

double ExtractMaxScale(const ColumnMatrix* Matrix) noexcept
{
    if (!Matrix) return 1;
    const ColumnMatrix& M = *Matrix;
    const double SX = JsHypot(M[0], M[1], M[2]);
    const double SY = JsHypot(M[4], M[5], M[6]);
    const double SZ = JsHypot(M[8], M[9], M[10]);
    return JsMax(JsMax(JsMax(SX, SY), SZ), 1e-6);
}

ColumnMatrix ConstructViewProjection(const NestingCamera& Camera)
{
    if (Camera.ViewProjection) return *Camera.ViewProjection;
    const Point3& Eye = Camera.Eye;
    const Point3& Target = Camera.Target;
    const Point3& Up = Camera.Up;
    const double Aspect = Camera.ScreenWidth / JsMax(1.0, Camera.ScreenHeight);
    const double Near = Camera.Near, Far = Camera.Far;
    auto Unit = [](const Point3& V) {
        const double L = HypotOrUnit(V[0], V[1], V[2]);
        return Point3{V[0] / L, V[1] / L, V[2] / L};
    };
    const Point3 Forward = Unit({Target[0] - Eye[0], Target[1] - Eye[1], Target[2] - Eye[2]});
    const Point3 Right   = Unit(Cross(Forward, Up));
    const Point3 TrueUp  = Cross(Right, Forward);
    // Column-major view matrix looking along +Forward (clip w = +z_eye).
    const ColumnMatrix View = {Right[0], TrueUp[0], Forward[0], 0, Right[1], TrueUp[1], Forward[1], 0, Right[2], TrueUp[2], Forward[2], 0,
                               -Dot(Right, Eye), -Dot(TrueUp, Eye), -Dot(Forward, Eye), 1};
    const double Focal = 1.0 / std::tan(Camera.FovY * 0.5);
    // Reversed-Z: z_ndc = (Far * Near / (Far - Near)) / z_eye - Near / (Far - Near), so z = Near -> 1, z = Far -> 0.
    const double A = -Near / (Far - Near);
    const double B = (Far * Near) / (Far - Near);
    const ColumnMatrix Projection = {Focal / Aspect, 0, 0, 0, 0, Focal, 0, 0, 0, 0, A, 1, 0, 0, B, 0};
    return MultiplyMatrices(Projection, View);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              FIXED-SIZE PAGES & LRU RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

std::vector<NestingPage> PackNestingPages(GeometryNesting& Nesting, uint32_t PageByteLimit)
{
    if (!(PageByteLimit >= 512)) throw std::invalid_argument("PageByteLimit must be at least 512 bytes");
    struct PageItem
    {
        uint32_t ClusterIdentity = 0, Tier = 0, TriangleCount = 0, VertexCount = 0, ByteLength = 0;
    };
    std::vector<NestingPage> Pages;
    const uint32_t CoarsestTier = static_cast<uint32_t>(Nesting.Tiers.size() - 1);
    std::vector<PageItem> CurrentClusters;
    std::vector<uint32_t> CurrentTiers;
    uint32_t CurrentBytes  = 32;   // 32 B page header
    bool     CurrentPinned = true;
    auto PutU32 = [](std::vector<uint8_t>& Bytes, size_t Offset, uint32_t Value) {
        for (int Shift = 0; Shift < 32; Shift += 8) Bytes[Offset + Shift / 8] = static_cast<uint8_t>(Value >> Shift);
    };
    auto FlushPage = [&]() {
        if (CurrentClusters.empty()) return;
        NestingPage Page;
        Page.PageIndex = static_cast<uint32_t>(Pages.size());
        Page.Pinned    = CurrentPinned;
        Page.ByteSize  = CurrentBytes;
        Page.BinaryPayload.assign(CurrentBytes, 0);
        PutU32(Page.BinaryPayload, 0, 0x50474d31u);   // '1MGP' page magic
        PutU32(Page.BinaryPayload, 4, Page.PageIndex);
        PutU32(Page.BinaryPayload, 8, static_cast<uint32_t>(CurrentClusters.size()));
        PutU32(Page.BinaryPayload, 12, CurrentPinned ? 1u : 0u);
        size_t Cursor = 32;
        for (const PageItem& Item : CurrentClusters)
        {
            Nesting.Clusters[Item.ClusterIdentity].PageIndex = static_cast<int32_t>(Page.PageIndex);
            PutU32(Page.BinaryPayload, Cursor, Item.ClusterIdentity);
            PutU32(Page.BinaryPayload, Cursor + 4, Item.Tier);
            PutU32(Page.BinaryPayload, Cursor + 8, Item.TriangleCount);
            PutU32(Page.BinaryPayload, Cursor + 12, Item.VertexCount);
            const float Error = static_cast<float>(Nesting.Clusters[Item.ClusterIdentity].Error);
            uint32_t    ErrorBits = 0;
            std::memcpy(&ErrorBits, &Error, 4);
            PutU32(Page.BinaryPayload, Cursor + 16, ErrorBits);
            Cursor += Item.ByteLength;
            Page.Clusters.push_back(Item.ClusterIdentity);
        }
        Page.Tiers = CurrentTiers;
        std::sort(Page.Tiers.begin(), Page.Tiers.end());
        Pages.push_back(std::move(Page));
        CurrentClusters.clear();
        CurrentTiers.clear();
        CurrentBytes  = 32;
        CurrentPinned = false;
    };
    // Walk the coarsest tier down to tier 0 (the leaves).
    for (int64_t TierIndex = CoarsestTier; TierIndex >= 0; --TierIndex)
    {
        const NestingTier& Tier = Nesting.Tiers[static_cast<size_t>(TierIndex)];
        for (uint32_t ClusterIdentity : Tier.Clusters)
        {
            const NestedCluster& Node = Nesting.Clusters[ClusterIdentity];
            std::unordered_set<uint32_t> UniqueVertices;
            for (uint32_t Triangle : Node.Triangles)
                for (uint32_t C = 0; C < 3; ++C) UniqueVertices.insert(Tier.Triangles[Triangle * 3 + C]);
            // 24 B cluster header + 12 B per vertex position + 24 B per triangle corner UVs + 8 B per triangle indices, family, sharp.
            const uint32_t ClusterBytes = 24 + static_cast<uint32_t>(UniqueVertices.size()) * 12 + Node.TriangleCount * 32;
            const bool IsCoarsestCluster = TierIndex == CoarsestTier;
            if (!CurrentClusters.empty() && (CurrentBytes + ClusterBytes > PageByteLimit || (CurrentPinned && !IsCoarsestCluster))) FlushPage();
            if (IsCoarsestCluster) CurrentPinned = true;
            CurrentClusters.push_back({ClusterIdentity, static_cast<uint32_t>(TierIndex), Node.TriangleCount,
                                       static_cast<uint32_t>(UniqueVertices.size()), ClusterBytes});
            if (std::find(CurrentTiers.begin(), CurrentTiers.end(), static_cast<uint32_t>(TierIndex)) == CurrentTiers.end())
                CurrentTiers.push_back(static_cast<uint32_t>(TierIndex));
            CurrentBytes += ClusterBytes;
        }
    }
    FlushPage();
    return Pages;
}

NestingResidency::NestingResidency(const std::vector<NestingPage>& PageList, std::optional<double> MaximumBytes, bool InitiallyResidentAll)
    : Pages(PageList)
{
    double PinnedBytes = 0;
    for (const NestingPage& Page : Pages)
        if (Page.Pinned) PinnedBytes += Page.ByteSize;
    MaxResidentBytes = JsMax(PinnedBytes, MaximumBytes.value_or(Unbounded));
    LastUsedFrame.assign(Pages.size(), -1);
    for (const NestingPage& Page : Pages)
        if (Page.Pinned || InitiallyResidentAll)
        {
            ResidentPages.push_back(Page.PageIndex);
            ResidentBytes += Page.ByteSize;
        }
}

bool NestingResidency::Holds(uint32_t PageIndex) const
{
    return std::find(ResidentPages.begin(), ResidentPages.end(), PageIndex) != ResidentPages.end();
}

bool NestingResidency::IsPageResident(int32_t PageIndex) const { return PageIndex < 0 || Holds(static_cast<uint32_t>(PageIndex)); }

void NestingResidency::TouchPage(int32_t PageIndex, int32_t FrameIndex)
{
    if (PageIndex >= 0 && Holds(static_cast<uint32_t>(PageIndex))) LastUsedFrame[PageIndex] = FrameIndex;
}

void NestingResidency::RequestPage(int32_t PageIndex, double PriorityErrorPx)
{
    if (PageIndex < 0 || Holds(static_cast<uint32_t>(PageIndex))) return;
    for (StreamingRequest& Existing : StreamingQueue)
        if (Existing.PageIndex == PageIndex)
        {
            Existing.Priority = JsMax(Existing.Priority, PriorityErrorPx);
            return;
        }
    StreamingQueue.push_back({PageIndex, PriorityErrorPx});
}

uint32_t NestingResidency::ServiceStreamingQueue(int32_t FrameIndex, uint32_t MaxPagesToLoad)
{
    // 📝 B.Priority - A.Priority || A.PageIndex - B.PageIndex: a NaN or zero difference falls through to the page order.
    std::stable_sort(StreamingQueue.begin(), StreamingQueue.end(), [](const StreamingRequest& A, const StreamingRequest& B) {
        const double Difference = B.Priority - A.Priority;
        if (Difference != 0 && !std::isnan(Difference)) return Difference < 0;
        return A.PageIndex < B.PageIndex;
    });
    uint32_t Loaded = 0;
    while (Loaded < MaxPagesToLoad && !StreamingQueue.empty())
    {
        const StreamingRequest Next = StreamingQueue.front();
        StreamingQueue.erase(StreamingQueue.begin());
        if (Holds(static_cast<uint32_t>(Next.PageIndex))) continue;
        if (Next.PageIndex < 0 || static_cast<size_t>(Next.PageIndex) >= Pages.size()) continue;
        const NestingPage& Page = Pages[Next.PageIndex];
        // Evict least-recently-used unpinned pages if needed to fit within MaxResidentBytes.
        while (ResidentBytes + Page.ByteSize > MaxResidentBytes)
        {
            int64_t Victim = -1;
            double  OldestFrame = Unbounded;
            for (uint32_t ResidentIndex : ResidentPages)
            {
                const NestingPage& Candidate = Pages[ResidentIndex];
                if (Candidate.Pinned || LastUsedFrame[ResidentIndex] == FrameIndex) continue;
                if (LastUsedFrame[ResidentIndex] < OldestFrame)
                {
                    OldestFrame = LastUsedFrame[ResidentIndex];
                    Victim      = ResidentIndex;
                }
            }
            if (Victim < 0) break;
            ResidentPages.erase(std::find(ResidentPages.begin(), ResidentPages.end(), static_cast<uint32_t>(Victim)));
            ResidentBytes -= Pages[Victim].ByteSize;
            ++Evictions;
        }
        if (ResidentBytes + Page.ByteSize <= MaxResidentBytes)
        {
            ResidentPages.push_back(Page.PageIndex);
            ResidentBytes += Page.ByteSize;
            LastUsedFrame[Page.PageIndex] = FrameIndex;
            ++Loads;
            ++Loaded;
        }
    }
    return Loaded;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              PROJECTED-ERROR CUT
//------------------------------------------------------------------------------------------------------------------------

double ComputeProjectionScale(const NestingCamera& Camera)
{
    if (Camera.ProjectionScale && std::isfinite(*Camera.ProjectionScale)) return *Camera.ProjectionScale;
    return (Camera.ScreenHeight * 0.5) / std::tan(Camera.FovY * 0.5);
}

// 📝 Transmissive clusters include the refraction deviation factor (η - 1) · sin(cone half angle), so curved refractive surfaces
//    refine earlier than flat panes of the same geometric error.
double EvaluateProjectedSphereError(const NestingSphere& Sphere, double ObjectError, const NestingCamera& Camera, const ColumnMatrix* LocalToWorld,
                                    uint32_t ClassMask, const NestingCone* Cone, const RefreshOptions& Options)
{
    if (!(ObjectError > 0)) return 0;
    if (!std::isfinite(ObjectError)) return Unbounded;
    const Point3& Eye   = Camera.Eye;
    const double  Scale = ExtractMaxScale(LocalToWorld);
    const Point3  WorldCenter = TransformWorldPoint(LocalToWorld, Sphere.Center);
    const double  WorldRadius = Scale * Sphere.Radius;
    const double  Distance    = JsMax(JsHypot(WorldCenter[0] - Eye[0], WorldCenter[1] - Eye[1], WorldCenter[2] - Eye[2]) - WorldRadius, Camera.Near);
    double EffectiveError = Scale * ObjectError;
    if ((ClassMask & 2u) != 0 && Cone)
    {
        const double Eta    = Options.IndexOfRefraction.value_or(DefaultGlassRefractionEta);
        const double Cutoff = JsMax(-1.0, JsMin(1.0, Cone->Cutoff));
        const double Spread = std::sqrt(JsMax(0.0, 1 - Cutoff * Cutoff));
        EffectiveError *= 1 + JsMax(0.0, Eta - 1) * Spread;
    }
    return (EffectiveError / Distance) * ComputeProjectionScale(Camera);
}

ClusterCutMetric EvaluateClusterProjectedError(const NestedCluster& Node, const NestingCamera& Camera, const ColumnMatrix* LocalToWorld,
                                               const RefreshOptions& Options)
{
    return {EvaluateProjectedSphereError(Node.Sphere, Node.Error, Camera, LocalToWorld, Node.ClassMask, &Node.Cone, Options),
            EvaluateProjectedSphereError(Node.CoarserSphere, Node.CoarserError, Camera, LocalToWorld, Node.ClassMask, &Node.Cone, Options)};
}

NestingCut SelectNestingCut(const GeometryNesting& Nesting, const NestingCamera& Camera, const ColumnMatrix* LocalToWorld, const RefreshOptions& Options)
{
    const double Threshold = Options.PixelErrorThreshold ? *Options.PixelErrorThreshold
                                                         : Camera.PixelErrorThreshold.value_or(DefaultPixelErrorThreshold);
    NestingResidency* Residency = Options.Residency;
    const int32_t FrameIndex = Options.FrameIndex;
    // 📝 Insertion-ordered selection, as the demo's Set.
    std::vector<uint32_t> Selected;
    std::vector<uint8_t>  IsSelected(Nesting.Clusters.size(), 0);
    auto Add = [&](uint32_t Identity) {
        if (IsSelected[Identity]) return;
        IsSelected[Identity] = 1;
        Selected.push_back(Identity);
    };
    auto Remove = [&](uint32_t Identity) {
        if (!IsSelected[Identity]) return;
        IsSelected[Identity] = 0;
        Selected.erase(std::find(Selected.begin(), Selected.end(), Identity));
    };
    NestingCut Cut;
    Cut.Metrics.resize(Nesting.Clusters.size());
    for (const NestedCluster& Node : Nesting.Clusters)
    {
        const ClusterCutMetric Evaluated = EvaluateClusterProjectedError(Node, Camera, LocalToWorld, Options);
        Cut.Metrics[Node.Identity] = Evaluated;
        if (Evaluated.OwnErrorPx <= Threshold && Evaluated.CoarserErrorPx > Threshold) Add(Node.Identity);
    }
    // 📝 Whole-group finer attribution means one cluster can be reached through several coarser clusters, so the raw condition can
    //    select both a cluster and one of its finer clusters along different paths. Prune to the coarsest selected cluster on every
    //    path: the result is an antichain whose geometry still covers the whole surface.
    {
        std::vector<uint32_t> CoarsestFirst = Selected;
        std::stable_sort(CoarsestFirst.begin(), CoarsestFirst.end(), [&](uint32_t A, uint32_t B) {
            if (Nesting.Clusters[A].Tier != Nesting.Clusters[B].Tier) return Nesting.Clusters[A].Tier > Nesting.Clusters[B].Tier;
            return A < B;
        });
        std::vector<uint32_t> Kept;
        std::vector<uint8_t>  IsKept(Nesting.Clusters.size(), 0);
        for (uint32_t Identity : CoarsestFirst)
        {
            bool HasKeptCoarser = false;
            std::vector<uint32_t> Stack = Nesting.Clusters[Identity].Coarser;
            std::unordered_set<uint32_t> Seen;
            while (!Stack.empty() && !HasKeptCoarser)
            {
                const uint32_t Coarser = Stack.back();
                Stack.pop_back();
                if (IsKept[Coarser])
                {
                    HasKeptCoarser = true;
                    break;
                }
                if (!Seen.insert(Coarser).second) continue;
                const std::vector<uint32_t>& Next = Nesting.Clusters[Coarser].Coarser;
                Stack.insert(Stack.end(), Next.begin(), Next.end());
            }
            if (!HasKeptCoarser)
            {
                IsKept[Identity] = 1;
                Kept.push_back(Identity);
            }
        }
        for (uint32_t Identity : Selected) IsSelected[Identity] = 0;
        Selected.clear();
        for (uint32_t Identity : Kept) Add(Identity);
    }
    // If a selected cluster's page is non-resident, promote the whole group to its coarser group until resident.
    if (Residency)
    {
        bool Changed = true;
        while (Changed)
        {
            Changed = false;
            const std::vector<uint32_t> Snapshot = Selected;
            for (uint32_t ClusterIdentity : Snapshot)
            {
                const NestedCluster& Node = Nesting.Clusters[ClusterIdentity];
                if (Residency->IsPageResident(Node.PageIndex))
                {
                    Residency->TouchPage(Node.PageIndex, FrameIndex);
                    continue;
                }
                Residency->RequestPage(Node.PageIndex, Cut.Metrics[ClusterIdentity].CoarserErrorPx);
                if (Node.Coarser.empty()) continue;
                const NestedCluster& FirstCoarser = Nesting.Clusters[Node.Coarser[0]];
                for (uint32_t Sibling : FirstCoarser.Finer) Remove(Sibling);
                for (uint32_t Coarser : Node.Coarser) Add(Coarser);
                Changed = true;
            }
        }
    }
    Cut.CutClusterIdentities = Selected;
    std::sort(Cut.CutClusterIdentities.begin(), Cut.CutClusterIdentities.end());
    Cut.TierHistogram.assign(Nesting.Tiers.size(), 0);
    for (uint32_t Identity : Cut.CutClusterIdentities)
    {
        Cut.CutTriangles += Nesting.Clusters[Identity].TriangleCount;
        ++Cut.TierHistogram[Nesting.Clusters[Identity].Tier];
    }
    Cut.SourceTriangles = Nesting.SourceTriangles;
    return Cut;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CULLING, HI-Z AND DEPTH RASTER
//------------------------------------------------------------------------------------------------------------------------

bool TestClusterFrustum(const NestedCluster& Node, const ColumnMatrix* LocalToWorld, const ColumnMatrix& ViewProjection)
{
    const ColumnMatrix M = ClipFromLocal(LocalToWorld, ViewProjection);
    std::array<int32_t, 6> Outside{};
    for (uint32_t K = 0; K < 8; ++K)
    {
        const std::array<double, 4> Q = TransformClipPoint(M, BoxCorner(Node.TightMinimum, Node.TightMaximum, K));
        if (Q[0] < -Q[3]) ++Outside[0];
        if (Q[0] > Q[3]) ++Outside[1];
        if (Q[1] < -Q[3]) ++Outside[2];
        if (Q[1] > Q[3]) ++Outside[3];
        if (Q[2] < 0.0) ++Outside[4];
        if (Q[2] > Q[3]) ++Outside[5];
    }
    for (int Plane = 0; Plane < 6; ++Plane)
        if (Outside[Plane] == 8) return false;
    return true;
}

bool TestClusterNormalCone(const NestedCluster& Node, const ColumnMatrix* LocalToWorld, const Point3& CameraEye, const RefreshOptions& Options)
{
    if (!Options.BackfaceCull) return true;
    // Transmissive (glass) surfaces are always two-sided for entry and exit refraction.
    if ((Node.ClassMask & 2u) != 0) return true;
    const double Cutoff = Node.Cone.Cutoff;
    if (Cutoff <= 0) return true;
    const NestingSphere& Sphere = Node.TightSphere;
    const Point3 WorldCenter = TransformWorldPoint(LocalToWorld, Sphere.Center);
    const double WorldRadius = ExtractMaxScale(LocalToWorld) * Sphere.Radius;
    const Point3 WorldAxis   = TransformWorldDirection(LocalToWorld, Node.Cone.Axis);
    const double VX = WorldCenter[0] - CameraEye[0];
    const double VY = WorldCenter[1] - CameraEye[1];
    const double VZ = WorldCenter[2] - CameraEye[2];
    const double Distance = JsHypot(VX, VY, VZ);
    if (Distance <= WorldRadius + 1e-6) return true;
    const double SinAlpha = std::sqrt(JsMax(0.0, 1 - Cutoff * Cutoff));
    const double SinBeta  = JsMin(1.0, WorldRadius / Distance);
    const double CosBeta  = std::sqrt(JsMax(0.0, 1 - SinBeta * SinBeta));
    // Every triangle in the cone faces away when dot(normalize(Center - Eye), Axis) > sin(α + β).
    const double ViewDotAxis      = (VX * WorldAxis[0] + VY * WorldAxis[1] + VZ * WorldAxis[2]) / Distance;
    const double SinAlphaPlusBeta = SinAlpha * CosBeta + Cutoff * SinBeta;
    return ViewDotAxis <= SinAlphaPlusBeta;
}

DepthPyramid ConstructDepthPyramid(const std::vector<float>& BaseDepth, uint32_t Width, uint32_t Height, bool StandardDepth)
{
    const bool IsReversed = !StandardDepth;
    auto Combine = [IsReversed](double A, double B) { return IsReversed ? JsMin(A, B) : JsMax(A, B); };
    DepthPyramid Pyramid;
    Pyramid.Width         = Width;
    Pyramid.Height        = Height;
    Pyramid.StandardDepth = StandardDepth;
    Pyramid.Mips.push_back({Width, Height, BaseDepth});
    uint32_t PrevWidth = Width, PrevHeight = Height;
    while (PrevWidth > 1 || PrevHeight > 1)
    {
        const uint32_t NextWidth  = std::max<uint32_t>(1, PrevWidth >> 1);
        const uint32_t NextHeight = std::max<uint32_t>(1, PrevHeight >> 1);
        std::vector<float> NextData(static_cast<size_t>(NextWidth) * NextHeight);
        const std::vector<float>& PrevData = Pyramid.Mips.back().Depth;
        const bool ExtraX = (PrevWidth & 1u) != 0 && PrevWidth > 1;
        const bool ExtraY = (PrevHeight & 1u) != 0 && PrevHeight > 1;
        for (uint32_t Y = 0; Y < NextHeight; ++Y)
            for (uint32_t X = 0; X < NextWidth; ++X)
            {
                const uint32_t SX = X * 2, SY = Y * 2;
                const uint32_t TapsX = (X + 1 == NextWidth && ExtraX) ? 3 : std::min<uint32_t>(2, PrevWidth - SX);
                const uint32_t TapsY = (Y + 1 == NextHeight && ExtraY) ? 3 : std::min<uint32_t>(2, PrevHeight - SY);
                double Value = IsReversed ? 1.0 : 0.0;
                for (uint32_t DY = 0; DY < TapsY; ++DY)
                    for (uint32_t DX = 0; DX < TapsX; ++DX) Value = Combine(Value, PrevData[static_cast<size_t>(SY + DY) * PrevWidth + (SX + DX)]);
                NextData[static_cast<size_t>(Y) * NextWidth + X] = static_cast<float>(Value);
            }
        Pyramid.Mips.push_back({NextWidth, NextHeight, std::move(NextData)});
        PrevWidth  = NextWidth;
        PrevHeight = NextHeight;
    }
    return Pyramid;
}

ClusterScreenRect ProjectClusterScreenRect(const NestedCluster& Node, const ColumnMatrix* LocalToWorld, const ColumnMatrix& ViewProjection, double Width,
                                           double Height, bool StandardDepth)
{
    const ColumnMatrix M = ClipFromLocal(LocalToWorld, ViewProjection);
    const bool IsReversed = !StandardDepth;
    double MinX = Unbounded, MaxX = -Unbounded, MinY = Unbounded, MaxY = -Unbounded;
    double ClosestDepth = IsReversed ? -Unbounded : Unbounded;
    ClusterScreenRect Rect;
    for (uint32_t K = 0; K < 8; ++K)
    {
        const std::array<double, 4> Q = TransformClipPoint(M, BoxCorner(Node.TightMinimum, Node.TightMaximum, K));
        if (Q[3] <= 1e-4)
        {
            Rect.CrossesNearPlane = true;
            return Rect;
        }
        const double NdcX = Q[0] / Q[3], NdcY = Q[1] / Q[3], NdcZ = Q[2] / Q[3];
        const double ScreenX = (NdcX * 0.5 + 0.5) * Width;
        const double ScreenY = (0.5 - NdcY * 0.5) * Height;
        MinX = JsMin(MinX, ScreenX);
        MaxX = JsMax(MaxX, ScreenX);
        MinY = JsMin(MinY, ScreenY);
        MaxY = JsMax(MaxY, ScreenY);
        ClosestDepth = IsReversed ? JsMax(ClosestDepth, NdcZ) : JsMin(ClosestDepth, NdcZ);
    }
    if (MaxX < 0 || MinX >= Width || MaxY < 0 || MinY >= Height)
    {
        Rect.Offscreen = true;
        return Rect;
    }
    Rect.MinX         = JsMax(0.0, JsMin(Width - 1, MinX));
    Rect.MaxX         = JsMax(0.0, JsMin(Width - 1, MaxX));
    Rect.MinY         = JsMax(0.0, JsMin(Height - 1, MinY));
    Rect.MaxY         = JsMax(0.0, JsMin(Height - 1, MaxY));
    Rect.ClosestDepth = JsMax(0.0, JsMin(1.0, ClosestDepth));
    return Rect;
}

bool TestClusterHiZ(const NestedCluster& Node, const ColumnMatrix* LocalToWorld, const ColumnMatrix& ViewProjection, const DepthPyramid* Pyramid)
{
    if (!Pyramid || Pyramid->Mips.empty()) return true;
    const ClusterScreenRect Rect = ProjectClusterScreenRect(Node, LocalToWorld, ViewProjection, Pyramid->Width, Pyramid->Height, Pyramid->StandardDepth);
    if (Rect.Offscreen) return false;
    if (Rect.CrossesNearPlane) return true;
    const double Footprint = JsMax(JsMax(Rect.MaxX - Rect.MinX, Rect.MaxY - Rect.MinY), 1.0);
    const double MipChoice = JsMin(static_cast<double>(Pyramid->Mips.size() - 1), JsMax(0.0, std::ceil(std::log2(Footprint))));
    const DepthPyramid::MipLevel& Mip = Pyramid->Mips[static_cast<size_t>(MipChoice)];
    const double ScaleX = static_cast<double>(Mip.Width) / Pyramid->Width, ScaleY = static_cast<double>(Mip.Height) / Pyramid->Height;
    const auto Clamp = [](double Value, double Upper) { return static_cast<size_t>(JsMax(0.0, JsMin(Upper, std::floor(Value)))); };
    const size_t X0 = Clamp(Rect.MinX * ScaleX, Mip.Width - 1.0), X1 = Clamp(Rect.MaxX * ScaleX, Mip.Width - 1.0);
    const size_t Y0 = Clamp(Rect.MinY * ScaleY, Mip.Height - 1.0), Y1 = Clamp(Rect.MaxY * ScaleY, Mip.Height - 1.0);
    const bool IsReversed = !Pyramid->StandardDepth;
    auto Combine = [IsReversed](double A, double B) { return IsReversed ? JsMin(A, B) : JsMax(A, B); };
    const double OccluderDepth = Combine(Combine(Mip.Depth[Y0 * Mip.Width + X0], Mip.Depth[Y0 * Mip.Width + X1]),
                                         Combine(Mip.Depth[Y1 * Mip.Width + X0], Mip.Depth[Y1 * Mip.Width + X1]));
    return IsReversed ? Rect.ClosestDepth + 1e-5 >= OccluderDepth : Rect.ClosestDepth - 1e-5 <= OccluderDepth;
}

void RasterizeClustersToDepth(const std::vector<RefreshCluster>& Clusters, std::vector<float>& DepthBuffer, uint32_t Width, uint32_t Height,
                              const ColumnMatrix& ViewProjection, bool StandardDepth)
{
    const bool IsReversed = !StandardDepth;
    for (const RefreshCluster& Item : Clusters)
    {
        const NestedCluster& Node = *Item.Node;
        if ((Node.ClassMask & 2u) != 0 && (Node.ClassMask & 1u) == 0) continue;   // transmissive never writes Hi-Z
        const NestingTier& Tier = Item.Nesting->Tiers[Node.Tier];
        const ColumnMatrix M = ClipFromLocal(Item.LocalToWorld, ViewProjection);
        for (uint32_t Triangle : Node.Triangles)
        {
            std::array<std::array<double, 3>, 3> S{};
            bool Valid = true;
            for (uint32_t C = 0; C < 3; ++C)
            {
                const std::array<double, 4> Q = TransformClipPoint(M, Tier.ReadPosition(Tier.Triangles[Triangle * 3 + C]));
                if (Q[3] <= 1e-4)
                {
                    Valid = false;
                    break;
                }
                S[C] = {(Q[0] / Q[3] * 0.5 + 0.5) * Width, (0.5 - Q[1] / Q[3] * 0.5) * Height, Q[2] / Q[3]};
            }
            if (!Valid) continue;
            const double Area = (S[1][0] - S[0][0]) * (S[2][1] - S[0][1]) - (S[1][1] - S[0][1]) * (S[2][0] - S[0][0]);
            if (std::fabs(Area) < 1e-5) continue;
            const double MinX = JsMax(0.0, std::floor(JsMin(JsMin(S[0][0], S[1][0]), S[2][0])));
            const double MaxX = JsMin(Width - 1.0, std::ceil(JsMax(JsMax(S[0][0], S[1][0]), S[2][0])));
            const double MinY = JsMax(0.0, std::floor(JsMin(JsMin(S[0][1], S[1][1]), S[2][1])));
            const double MaxY = JsMin(Height - 1.0, std::ceil(JsMax(JsMax(S[0][1], S[1][1]), S[2][1])));
            for (double Y = MinY; Y <= MaxY; ++Y)
                for (double X = MinX; X <= MaxX; ++X)
                {
                    const double PX = X + 0.5, PY = Y + 0.5;
                    const double W0 = ((S[1][0] - PX) * (S[2][1] - PY) - (S[1][1] - PY) * (S[2][0] - PX)) / Area;
                    const double W1 = ((S[2][0] - PX) * (S[0][1] - PY) - (S[2][1] - PY) * (S[0][0] - PX)) / Area;
                    const double W2 = 1 - W0 - W1;
                    if (W0 < 0 || W1 < 0 || W2 < 0) continue;
                    const double Z = W0 * S[0][2] + W1 * S[1][2] + W2 * S[2][2];
                    if (Z < 0 || Z > 1) continue;
                    const size_t Index = static_cast<size_t>(Y) * Width + static_cast<size_t>(X);
                    if (IsReversed ? Z > DepthBuffer[Index] : Z < DepthBuffer[Index]) DepthBuffer[Index] = static_cast<float>(Z);
                }
        }
    }
}

NestingRefresh ExecuteNestingRefresh(const std::vector<RefreshInstance>& Instances, const NestingCamera& Camera, const RefreshOptions& Options)
{
    const uint32_t Width  = static_cast<uint32_t>(Camera.ScreenWidth);
    const uint32_t Height = static_cast<uint32_t>(Camera.ScreenHeight);
    const bool StandardDepth = Options.StandardDepth;
    const Point3& Eye = Camera.Eye;
    NestingRefresh Refresh;
    Refresh.ViewProjection = ConstructViewProjection(Camera);
    const ColumnMatrix PreviousViewProjection = Camera.PreviousViewProjection.value_or(Refresh.ViewProjection);
    const DepthPyramid* PreviousPyramid = Options.PreviousDepthPyramid;

    for (uint32_t InstanceIndex = 0; InstanceIndex < Instances.size(); ++InstanceIndex)
    {
        const RefreshInstance& Instance = Instances[InstanceIndex];
        const GeometryNesting& Nesting  = *Instance.Nesting;
        const ColumnMatrix* LocalToWorld = Instance.LocalToWorld ? &*Instance.LocalToWorld : nullptr;
        const ColumnMatrix* PreviousLocalToWorld = Instance.PreviousLocalToWorld ? &*Instance.PreviousLocalToWorld : LocalToWorld;
        const uint32_t Occupant = Instance.Occupant.value_or(InstanceIndex);
        Refresh.Stats.SourceTriangles += Nesting.SourceTriangles;
        RefreshOptions CutOptions = Options;
        CutOptions.Residency = Instance.Residency ? Instance.Residency : Options.Residency;
        const NestingCut Cut = SelectNestingCut(Nesting, Camera, LocalToWorld, CutOptions);
        Refresh.Stats.CutTriangles += Cut.CutTriangles;
        for (uint32_t Identity : Cut.CutClusterIdentities)
        {
            const NestedCluster& Node = Nesting.Clusters[Identity];
            RefreshCluster Item{InstanceIndex, Occupant, &Nesting, &Node, LocalToWorld, PreviousLocalToWorld, Cut.Metrics[Identity], 0};
            if (!TestClusterFrustum(Node, LocalToWorld, Refresh.ViewProjection))
            {
                Refresh.FrustumCulled.push_back(Item);
                continue;
            }
            if (!TestClusterNormalCone(Node, LocalToWorld, Eye, Options))
            {
                Refresh.ConeCulled.push_back(Item);
                continue;
            }
            Refresh.CutCandidates.push_back(Item);
        }
    }
    // Phase 1: test against the previous refresh's pyramid with previous transforms.
    std::vector<RefreshCluster> Phase2RetryQueue;
    for (const RefreshCluster& Item : Refresh.CutCandidates)
    {
        if (!PreviousPyramid || TestClusterHiZ(*Item.Node, Item.PreviousLocalToWorld, PreviousViewProjection, PreviousPyramid))
        {
            RefreshCluster Visible = Item;
            Visible.Phase = 1;
            Refresh.Phase1Visible.push_back(Visible);
        }
        else
            Phase2RetryQueue.push_back(Item);
    }
    // Rasterize the phase-1 opaque clusters into depth and construct the current pyramid.
    Refresh.DepthBuffer.assign(static_cast<size_t>(Width) * Height, StandardDepth ? 1.0f : 0.0f);
    RasterizeClustersToDepth(Refresh.Phase1Visible, Refresh.DepthBuffer, Width, Height, Refresh.ViewProjection, StandardDepth);
    Refresh.CurrentDepthPyramid = ConstructDepthPyramid(Refresh.DepthBuffer, Width, Height, StandardDepth);
    // Phase 2: re-test phase-1-occluded clusters against the current pyramid to catch newly disoccluded geometry.
    for (const RefreshCluster& Item : Phase2RetryQueue)
    {
        if (TestClusterHiZ(*Item.Node, Item.LocalToWorld, Refresh.ViewProjection, &Refresh.CurrentDepthPyramid))
        {
            RefreshCluster Visible = Item;
            Visible.Phase = 2;
            Refresh.Phase2Disoccluded.push_back(Visible);
        }
        else
            Refresh.OccludedCulled.push_back(Item);
    }
    if (!Refresh.Phase2Disoccluded.empty())
    {
        RasterizeClustersToDepth(Refresh.Phase2Disoccluded, Refresh.DepthBuffer, Width, Height, Refresh.ViewProjection, StandardDepth);
        Refresh.CurrentDepthPyramid = ConstructDepthPyramid(Refresh.DepthBuffer, Width, Height, StandardDepth);
    }
    // Compact visible clusters into opaque and transmissive indirect draw commands.
    Refresh.VisibleClusters = Refresh.Phase1Visible;
    Refresh.VisibleClusters.insert(Refresh.VisibleClusters.end(), Refresh.Phase2Disoccluded.begin(), Refresh.Phase2Disoccluded.end());
    for (const RefreshCluster& Item : Refresh.VisibleClusters)
    {
        Refresh.Stats.VisibleTriangles += Item.Node->TriangleCount;
        ClusterDrawCommand Command;
        Command.IndexCount      = Item.Node->TriangleCount * 3;
        Command.FirstIndex      = Item.Node->FirstTriangle * 3;
        Command.FirstInstance   = Item.Occupant;
        Command.Occupant        = Item.Occupant;
        Command.InstanceIndex   = Item.InstanceIndex;
        Command.ClusterIdentity = Item.Node->Identity;
        Command.Tier            = Item.Node->Tier;
        Command.Phase           = Item.Phase;
        Command.ClassMask       = Item.Node->ClassMask;
        Command.OwnErrorPx      = Item.Metric.OwnErrorPx;
        ((Item.Node->ClassMask & 2u) != 0 ? Refresh.TransmissiveDraws : Refresh.OpaqueDraws).push_back(Command);
    }
    NestingRefresh::Statistics& Stats = Refresh.Stats;
    Stats.CutClusterCount        = static_cast<uint32_t>(Refresh.CutCandidates.size() + Refresh.FrustumCulled.size() + Refresh.ConeCulled.size());
    Stats.Phase1VisibleCount     = static_cast<uint32_t>(Refresh.Phase1Visible.size());
    Stats.Phase2DisoccludedCount = static_cast<uint32_t>(Refresh.Phase2Disoccluded.size());
    Stats.OccludedCulledCount    = static_cast<uint32_t>(Refresh.OccludedCulled.size());
    Stats.FrustumCulledCount     = static_cast<uint32_t>(Refresh.FrustumCulled.size());
    Stats.ConeCulledCount        = static_cast<uint32_t>(Refresh.ConeCulled.size());
    Stats.OpaqueDrawCount        = static_cast<uint32_t>(Refresh.OpaqueDraws.size());
    Stats.TransmissiveDrawCount  = static_cast<uint32_t>(Refresh.TransmissiveDraws.size());
    return Refresh;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              GPU PAGE SLOT ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

PageSlotAllocator::PageSlotAllocator(uint32_t Slots) : SlotCount(Slots)
{
    if (!(Slots >= 1)) throw std::invalid_argument("PageSlotAllocator needs at least one slot");
}

bool PageSlotAllocator::IsResident(uint32_t PageIndex) const { return SlotOf(PageIndex).has_value(); }

std::optional<uint32_t> PageSlotAllocator::SlotOf(uint32_t PageIndex) const
{
    for (const auto& [Page, Slot] : PageToSlot_)
        if (Page == PageIndex) return Slot;
    return std::nullopt;
}

void PageSlotAllocator::Touch(uint32_t PageIndex, int32_t FrameIndex)
{
    if (IsResident(PageIndex)) LastUsedFrame_[PageIndex] = FrameIndex;
}

PageSlotAllocator::Placement PageSlotAllocator::Ensure(uint32_t PageIndex, int32_t FrameIndex)
{
    if (const std::optional<uint32_t> Existing = SlotOf(PageIndex))
    {
        LastUsedFrame_[PageIndex] = FrameIndex;
        return {static_cast<int32_t>(*Existing), std::nullopt};
    }
    if (SlotToPage_.size() < SlotCount)
    {
        uint32_t Slot = 0;
        while (SlotToPage_.contains(Slot)) ++Slot;
        PageToSlot_.push_back({PageIndex, Slot});
        SlotToPage_[Slot]         = PageIndex;
        LastUsedFrame_[PageIndex] = FrameIndex;
        return {static_cast<int32_t>(Slot), std::nullopt};
    }
    int64_t Victim = -1, VictimSlot = -1;
    double  Oldest = Unbounded;
    for (const auto& [Page, Slot] : PageToSlot_)
    {
        if (PinnedPages_.contains(Page)) continue;
        const auto   Hit  = LastUsedFrame_.find(Page);
        const double Used = Hit == LastUsedFrame_.end() ? -1 : Hit->second;
        if (Used < Oldest)
        {
            Oldest     = Used;
            Victim     = Page;
            VictimSlot = Slot;
        }
    }
    if (Victim < 0) return {-1, -1};
    PageToSlot_.erase(std::find_if(PageToSlot_.begin(), PageToSlot_.end(), [&](const auto& Entry) { return Entry.first == Victim; }));
    PageToSlot_.push_back({PageIndex, static_cast<uint32_t>(VictimSlot)});
    SlotToPage_[static_cast<uint32_t>(VictimSlot)] = PageIndex;
    LastUsedFrame_[PageIndex] = FrameIndex;
    return {static_cast<int32_t>(VictimSlot), static_cast<int32_t>(Victim)};
}

void PageSlotAllocator::Evict(uint32_t PageIndex)
{
    const auto Hit = std::find_if(PageToSlot_.begin(), PageToSlot_.end(), [&](const auto& Entry) { return Entry.first == PageIndex; });
    if (Hit == PageToSlot_.end()) return;
    SlotToPage_.erase(Hit->second);
    PageToSlot_.erase(Hit);
}
} // namespace Frontier::VirtualizedGeometry
