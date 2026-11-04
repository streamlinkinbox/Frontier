/*==========================================================================================================================================
                                                   VIRTUALIZEDGEOMETRYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The demo's 22 virtualized geometry tests (GeometryHierarchy.test.mjs, GeometryHierarchyRuntime.test.mjs,
//    VisibilityResolve.test.mjs) assertion for assertion against the C++ port, on the demo's own corpus surfaces.
#include "SurfaceSoupArchive.h"
#include <Rendering/VirtualizedGeometry/VisibilityResolve.h>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
using namespace Frontier::VirtualizedGeometry;

namespace
{
constexpr double CirclePi = 3.141592653589793;

struct CheckFailure
{
    std::string Caption;
};
void Check(bool Condition, const std::string& Caption)
{
    if (!Condition) throw CheckFailure{Caption};
}

// 📝 Demo generator call → committed corpus soup (Exhibits/VirtualizedGeometry/Surfaces).
const SurfaceSoup& Surface(const std::string& Name)
{
    static std::map<std::string, SurfaceSoup> Loaded;
    auto Hit = Loaded.find(Name);
    if (Hit != Loaded.end()) return Hit->second;
    std::string Diagnostic;
    std::optional<SurfaceSoup> Soup = ReadSurfaceSoup(LocateExhibitFolder() / "Surfaces" / (Name + ".soup"), &Diagnostic);
    if (!Soup) throw CheckFailure{"corpus surface unavailable: " + Diagnostic};
    return Loaded.emplace(Name, std::move(*Soup)).first->second;
}
NestingOptions GlassOptions()
{
    NestingOptions Options;
    Options.TransmissiveFamilies = {9};
    return Options;
}
ColumnMatrix Translation(double X, double Y, double Z)
{
    ColumnMatrix M = IdentityMatrix();
    M[12] = X;
    M[13] = Y;
    M[14] = Z;
    return M;
}
NestingCamera RuntimeCamera()
{
    NestingCamera Camera;
    Camera.Eye          = {0, -8, 0};
    Camera.Target       = {0, 0, 0};
    Camera.Up           = {0, 0, 1};
    Camera.FovY         = CirclePi / 3;
    Camera.ScreenWidth  = 256;
    Camera.ScreenHeight = 256;
    Camera.Near         = 0.1;
    Camera.Far          = 200;
    return Camera;
}
NestingCamera ResolveCamera()
{
    NestingCamera Camera;
    Camera.Eye          = {2.0, -6.8, 2.2};
    Camera.ScreenWidth  = 192;
    Camera.ScreenHeight = 192;
    Camera.Near         = 0.1;
    Camera.Far          = 200;
    return Camera;
}
RefreshInstance Instance(const GeometryNesting& Nesting, std::optional<ColumnMatrix> LocalToWorld = std::nullopt, uint32_t Occupant = 0)
{
    RefreshInstance Entry;
    Entry.Nesting      = &Nesting;
    Entry.LocalToWorld = LocalToWorld;
    Entry.Occupant     = Occupant;
    return Entry;
}
RefreshOptions Threshold(double Pixels)
{
    RefreshOptions Options;
    Options.PixelErrorThreshold = Pixels;
    return Options;
}

// 📝 A valid cut is an antichain that covers every leaf: walking coarser from any leaf must reach a selected cluster.
void AssertCutIsCompleteFrontier(const GeometryNesting& Nesting, const std::vector<uint32_t>& CutIdentities)
{
    const std::set<uint32_t> Selected(CutIdentities.begin(), CutIdentities.end());
    auto CoarserOf = [&](uint32_t Identity) {
        std::set<uint32_t> Found;
        std::vector<uint32_t> Stack = Nesting.Clusters[Identity].Coarser;
        while (!Stack.empty())
        {
            const uint32_t Coarser = Stack.back();
            Stack.pop_back();
            if (!Found.insert(Coarser).second) continue;
            Stack.insert(Stack.end(), Nesting.Clusters[Coarser].Coarser.begin(), Nesting.Clusters[Coarser].Coarser.end());
        }
        return Found;
    };
    for (uint32_t Identity : CutIdentities)
        for (uint32_t Coarser : CoarserOf(Identity))
            Check(!Selected.contains(Coarser), "selected cluster " + std::to_string(Identity) + " has selected coarser cluster " + std::to_string(Coarser));
    for (uint32_t Leaf : Nesting.Tiers[0].Clusters)
    {
        if (Selected.contains(Leaf)) continue;
        bool Reached = false;
        for (uint32_t Coarser : CoarserOf(Leaf)) Reached = Reached || Selected.contains(Coarser);
        Check(Reached, "leaf " + std::to_string(Leaf) + " reaches no selected coarser cluster: cut has a hole");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     GEOMETRYHIERARCHY.TEST.MJS
//------------------------------------------------------------------------------------------------------------------------

void WeldingRemovesDuplication()
{
    const SurfaceSoup& Set = Surface("SphereSubdivision2");
    const WeldedSoup Welded = WeldFacetSoup(Set);
    Check(Welded.TriangleCount() == Set.Facets.size(), "weld changed the triangle count");
    Check(Welded.VertexCount() < Set.Facets.size(), "closed sphere must weld below one vertex per facet");
}

void SphereBuildsCrackFreeNesting()
{
    const SurfaceSoup& Set = Surface("SphereSubdivision4");
    const GeometryNesting Nesting = ConstructGeometryNesting(Set);
    const NestingValidation Checks = ValidateGeometryNesting(Set, Nesting);
    Check(Checks.Failures.empty(), Checks.Failures.empty() ? "" : Checks.Failures[0]);
    Check(Checks.Passed, "validation did not pass");
    Check(Nesting.Tiers.size() >= 3, "expected at least three tiers, got " + std::to_string(Nesting.Tiers.size()));
    const NestingTier& Leaf = Nesting.Tiers.front();
    const NestingTier& Coarsest = Nesting.Tiers.back();
    Check(Coarsest.Clusters.size() == 1, "closed manifold sphere should simplify to a single coarsest cluster");
    Check(Coarsest.TriangleCount() <= Leaf.TriangleCount() / 4.0, "coarsest tier kept too many triangles");
    for (size_t Tier = 1; Tier < Checks.Tiers.size(); ++Tier)
        Check(Checks.Tiers[Tier].Triangles < Checks.Tiers[Tier - 1].Triangles, "tiers must shrink toward the coarsest");
    for (const NestedCluster& Node : Nesting.Clusters)
        Check(Node.Sphere.Radius >= 0 && Node.Cone.Cutoff <= 1 && Node.Cone.Cutoff >= -1, "sphere or cone out of range");
}

void HeightfieldWithoutCracks()
{
    const SurfaceSoup& Set = Surface("HeightfieldCliff64");
    const GeometryNesting Nesting = ConstructGeometryNesting(Set);
    const NestingValidation Checks = ValidateGeometryNesting(Set, Nesting);
    Check(Checks.Failures.empty(), Checks.Failures.empty() ? "" : Checks.Failures[0]);
    Check(Nesting.Tiers.size() >= 3, "heightfield should build at least three tiers");
}

void ClassesNeverShareCluster()
{
    const SurfaceSoup& Set = Surface("SphereSubdivision3Glass");
    Check(std::any_of(Set.Facets.begin(), Set.Facets.end(), [](const SoupFacet& Facet) { return Facet.Family == 9; }), "corpus lacks the glass band");
    const GeometryNesting Nesting = ConstructGeometryNesting(Set, GlassOptions());
    const NestingValidation Checks = ValidateGeometryNesting(Set, Nesting, GlassOptions());
    Check(Checks.Failures.empty(), Checks.Failures.empty() ? "" : Checks.Failures[0]);
    bool Transmissive = false;
    for (const NestedCluster& Node : Nesting.Clusters)
    {
        Check(Node.ClassMask != 3, "cluster " + std::to_string(Node.Identity) + " mixes opaque and transmissive triangles");
        Transmissive = Transmissive || Node.ClassMask == 2;
    }
    Check(Transmissive, "glass band must produce transmissive clusters");
}

void BuildsAreDeterministic()
{
    const SurfaceSoup& Set = Surface("SphereSubdivision3");
    const GeometryNesting First = ConstructGeometryNesting(Set), Second = ConstructGeometryNesting(Set);
    Check(First.Tiers.size() == Second.Tiers.size(), "tier counts differ");
    for (size_t Tier = 0; Tier < First.Tiers.size(); ++Tier)
        Check(First.Tiers[Tier].Clusters.size() == Second.Tiers[Tier].Clusters.size() &&
                  First.Tiers[Tier].TriangleCount() == Second.Tiers[Tier].TriangleCount() &&
                  First.Tiers[Tier].VertexCount() == Second.Tiers[Tier].VertexCount(),
              "tier summaries differ");
    for (size_t Index = 0; Index < First.Clusters.size(); ++Index)
    {
        Check(First.Clusters[Index].TriangleCount == Second.Clusters[Index].TriangleCount, "cluster triangle counts differ");
        Check(First.Clusters[Index].Error == Second.Clusters[Index].Error, "cluster errors differ");
        Check(First.Clusters[Index].CoarserError == Second.Clusters[Index].CoarserError, "cluster coarser errors differ");
    }
    Check(First.Tiers[0].Triangles == Second.Tiers[0].Triangles, "leaf triangles differ");
    Check(First.Tiers[0].CornerUvs == Second.Tiers[0].CornerUvs, "leaf corner UVs differ");
    Check(First.Tiers.back().CornerUvs == Second.Tiers.back().CornerUvs, "coarsest corner UVs differ");
}

void TinySurfacesStopAtOneLeafTier()
{
    const SurfaceSoup& Set = Surface("Tetrahedron");
    const GeometryNesting Nesting = ConstructGeometryNesting(Set);
    Check(Nesting.Tiers.size() == 1, "tetrahedron should stay a single tier");
    Check(Nesting.Clusters.size() == 1, "tetrahedron should stay a single cluster");
    Check(ValidateGeometryNesting(Set, Nesting).Failures.empty(), "tetrahedron validation failed");
}

void TierCollectionIsRenderable()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision3"));
    const TierSoup Soup = CollectNestingTier(Nesting, 1);
    Check(Soup.TriangleCount == Nesting.Tiers[1].TriangleCount(), "collected triangle count differs from the tier");
    Check(Soup.Positions.size() == Soup.TriangleCount * 9u, "positions are not nine per triangle");
    Check(Soup.CornerUvs.size() == Soup.TriangleCount * 6u, "corner UVs are not six per triangle");
    Check(Soup.SharpMask.size() == Soup.TriangleCount, "sharp mask length differs");
    Check(Soup.UvSeamMask.size() == Soup.TriangleCount, "UV seam mask length differs");
    Check(std::all_of(Soup.Errors.begin(), Soup.Errors.end(), [](double Error) { return Error >= 0; }), "negative cluster error");
}

void SeamsAndCreasesSurviveEveryTier()
{
    const SurfaceSoup& Mesh = Surface("CreasedVessel48");
    const GeometryNesting Nesting = ConstructGeometryNesting(Mesh);
    const NestingValidation Checks = ValidateGeometryNesting(Mesh, Nesting);
    Check(Checks.Failures.empty(), Checks.Failures.empty() ? "" : Checks.Failures[0]);
    Check(Nesting.Tiers.size() >= 4, "vessel should build a deep nesting, got " + std::to_string(Nesting.Tiers.size()));
    Check(Nesting.HasUvs, "vessel UVs must be recognised");
    Check(Nesting.Tiers[0].SharpEdgeCount == 144, "three 48-segment marked sharp rings expected");
    Check(Nesting.Tiers[0].UvSeamEdgeCount >= 100, "meridian and cap-rim UV seams expected");
    for (const NestingTier& Tier : Nesting.Tiers)
    {
        Check(Tier.SharpEdgeCount > 0, "tier " + std::to_string(Tier.Tier) + " lost every marked sharp edge");
        Check(Tier.UvSeamEdgeCount > 0, "tier " + std::to_string(Tier.Tier) + " lost every UV seam");
        std::set<double> SharpZ;
        for (uint32_t T = 0; T < Tier.TriangleCount(); ++T)
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
                if ((Tier.SharpMask[T] & (1u << Corner)) != 0)
                    for (uint32_t Vertex : {Tier.Triangles[T * 3 + Corner], Tier.Triangles[T * 3 + (Corner + 1) % 3]})
                        SharpZ.insert(std::round(Tier.Positions[Vertex * 3 + 2] * 1000) / 1000);
        for (double Z : SharpZ)
            Check(std::fabs(Z) < 1e-9 || std::fabs(std::fabs(Z) - 1) < 1e-9, "tier " + std::to_string(Tier.Tier) + " sharp vertex drifted off its crease plane");
        for (uint32_t T = 0; T < Tier.TriangleCount(); ++T)
        {
            std::array<double, 3> Us, Vs;
            for (uint32_t C = 0; C < 3; ++C)
            {
                Us[C] = Tier.CornerUvs[T * 6 + C * 2];
                Vs[C] = Tier.CornerUvs[T * 6 + C * 2 + 1];
            }
            Check(*std::max_element(Us.begin(), Us.end()) - *std::min_element(Us.begin(), Us.end()) <= 0.6,
                  "tier " + std::to_string(Tier.Tier) + " triangle crosses the meridian UV seam");
            const bool Low = std::any_of(Vs.begin(), Vs.end(), [](double V) { return V <= 0.755; });
            const bool High = std::any_of(Vs.begin(), Vs.end(), [](double V) { return V >= 0.775; });
            Check(!(Low && High), "tier " + std::to_string(Tier.Tier) + " triangle crosses between UV islands");
            for (uint32_t I = 0; I < 6; ++I) Check(std::isfinite(Tier.CornerUvs[T * 6 + I]), "non-finite UV");
        }
    }
}

void GroupsShareCoarserErrorAndSphere()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision4"));
    std::map<std::string, const NestedCluster*> Groups;
    for (const NestedCluster& Node : Nesting.Clusters)
    {
        if (Node.Coarser.empty()) continue;
        Check(Node.CoarserError >= Node.Error, "coarser error must dominate the cluster's own error");
        Check(std::isfinite(Node.CoarserError), "clusters with coarser clusters need a finite coarser error");
        const std::string Key = std::to_string(Node.Tier) + ":" + std::to_string(Node.GroupIdentity);
        auto [Hit, Fresh] = Groups.emplace(Key, &Node);
        if (!Fresh)
        {
            const NestedCluster& First = *Hit->second;
            Check(Node.CoarserError == First.CoarserError, "group members must share the coarser error exactly");
            Check(Node.CoarserSphere == First.CoarserSphere, "group members must share the coarser sphere exactly");
            std::vector<uint32_t> A = Node.Coarser, B = First.Coarser;
            std::sort(A.begin(), A.end());
            std::sort(B.begin(), B.end());
            Check(A == B, "group members must share the same coarser clusters");
        }
        const Point3& C = Node.CoarserSphere.Center;
        const Point3& S = Node.Sphere.Center;
        const double D = JsHypot(C[0] - S[0], C[1] - S[1], C[2] - S[2]);
        Check(D + Node.Sphere.Radius <= Node.CoarserSphere.Radius + 1e-9, "coarser sphere does not enclose the cluster sphere");
    }
    Check(Groups.size() > 10, "expected many simplification groups, got " + std::to_string(Groups.size()));
    for (const NestedCluster& Node : Nesting.Clusters)
        if (Node.Tier == Nesting.Tiers.size() - 1) Check(Node.CoarserError == Unbounded, "coarsest clusters keep an infinite coarser error");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     GEOMETRYHIERARCHYRUNTIME.TEST.MJS
//------------------------------------------------------------------------------------------------------------------------

void CutIsCompleteFrontierAtEveryThreshold()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision3"));
    int64_t PreviousTriangles = -1;
    for (double Pixels : {1000.0, 50.0, 5.0, 1.0, 0.1, 0.001})
    {
        const NestingCut Cut = SelectNestingCut(Nesting, RuntimeCamera(), nullptr, Threshold(Pixels));
        Check(!Cut.CutClusterIdentities.empty(), "cut is empty");
        AssertCutIsCompleteFrontier(Nesting, Cut.CutClusterIdentities);
        Check(static_cast<int64_t>(Cut.CutTriangles) >= PreviousTriangles, "smaller thresholds must not coarsen the cut");
        PreviousTriangles = Cut.CutTriangles;
        if (Pixels >= 1000) Check(Cut.CutClusterIdentities.size() == 1, "huge threshold selects only the coarsest cluster");
        if (Pixels <= 0.001) Check(Cut.CutTriangles == Nesting.SourceTriangles, "tiny threshold selects all leaves");
    }
}

void FrustumAndConeCulling()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision3"));
    const NestingRefresh Offscreen = ExecuteNestingRefresh({Instance(Nesting, Translation(500, 0, 0), 0)}, RuntimeCamera());
    Check(Offscreen.Stats.VisibleTriangles == 0, "offscreen instance drew triangles");
    Check(Offscreen.Stats.FrustumCulledCount > 0, "offscreen instance was not frustum culled");
    NestedCluster Node;
    Node.ClassMask   = 1;
    Node.Cone        = {{0, 0, 1}, 0.9};
    Node.Sphere      = {{0, 0, 0}, 0.1};
    Node.TightSphere = {{0, 0, 0}, 0.1};
    Check(!TestClusterNormalCone(Node, nullptr, {0, 0, -5}), "cone facing +Z seen from -Z must cull");
    Check(TestClusterNormalCone(Node, nullptr, {0, 0, 5}), "cone facing the viewer must stay");
    Node.ClassMask = 2;
    Check(TestClusterNormalCone(Node, nullptr, {0, 0, -5}), "transmissive clusters are two-sided and never cone-culled");
}

void TwoPhaseOcclusion()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision3"));
    const std::vector<RefreshInstance> Instances = {Instance(Nesting, Translation(0, 0, 0), 0), Instance(Nesting, Translation(0, 7, 0), 1)};
    const NestingRefresh Refresh1 = ExecuteNestingRefresh(Instances, RuntimeCamera());
    Check(Refresh1.Stats.VisibleTriangles > 0, "first refresh drew nothing");
    auto CountOccupant = [](const NestingRefresh& Refresh, uint32_t Occupant) {
        return std::count_if(Refresh.VisibleClusters.begin(), Refresh.VisibleClusters.end(), [&](const RefreshCluster& Item) { return Item.Occupant == Occupant; });
    };
    const auto FarVisible1 = CountOccupant(Refresh1, 1);
    Check(FarVisible1 > 0, "without a previous pyramid nothing is occlusion-culled yet");
    RefreshOptions Second;
    Second.PreviousDepthPyramid = &Refresh1.CurrentDepthPyramid;
    const NestingRefresh Refresh2 = ExecuteNestingRefresh(Instances, RuntimeCamera(), Second);
    const auto FarVisible2 = CountOccupant(Refresh2, 1);
    Check(FarVisible2 < FarVisible1, "second refresh must occlusion-cull some far clusters");
    Check(Refresh2.Stats.OccludedCulledCount > 0, "far clusters end in the occluded bin");
    Check(CountOccupant(Refresh2, 0) > 0, "near sphere still draws");

    NestingCamera WideCamera = RuntimeCamera();
    WideCamera.ScreenWidth  = 64;
    WideCamera.ScreenHeight = 64;
    std::vector<float> Depth(64 * 64, 0.0f);   // 0 = far/clear (reversed)
    const double OccluderDepth = -0.1 / (200 - 0.1) + (200 * 0.1) / (200 - 0.1) / 7.0;
    for (int Y = 16; Y < 48; ++Y)
        for (int X = 16; X < 48; ++X) Depth[Y * 64 + X] = static_cast<float>(OccluderDepth);
    const DepthPyramid Pyramid = ConstructDepthPyramid(Depth, 64, 64);
    Check(Pyramid.Mips[1].Depth.size() == 32 * 32, "mip 1 is not 32x32");
    Check(Pyramid.Mips.back().Depth.size() == 1, "pyramid reduces to a single texel");
    const ColumnMatrix ViewProjection = ExecuteNestingRefresh({}, WideCamera).ViewProjection;
    auto Box = [](const Point3& Minimum, const Point3& Maximum) {
        NestedCluster Node;
        Node.ClassMask    = 1;
        Node.TightMinimum = Minimum;
        Node.TightMaximum = Maximum;
        Node.Minimum      = Minimum;
        Node.Maximum      = Maximum;
        Node.TriangleCount = 1;
        return Node;
    };
    Check(!TestClusterHiZ(Box({-0.2, 6.8, -0.2}, {0.2, 7.2, 0.2}), nullptr, ViewProjection, &Pyramid), "box behind the occluder block must be Hi-Z culled");
    Check(TestClusterHiZ(Box({-0.2, -4.2, -0.2}, {0.2, -3.8, 0.2}), nullptr, ViewProjection, &Pyramid), "box in front of the occluder block must stay visible");
}

void GlassDrawsTransmissive()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision3Glass"), GlassOptions());
    const NestingRefresh Refresh = ExecuteNestingRefresh({Instance(Nesting)}, RuntimeCamera());
    Check(Refresh.Stats.OpaqueDrawCount > 0, "opaque shell draws");
    Check(Refresh.Stats.TransmissiveDrawCount > 0, "glass band draws in the transmissive stream");
    for (const ClusterDrawCommand& Draw : Refresh.OpaqueDraws) Check((Draw.ClassMask & 2u) == 0, "opaque stream holds a transmissive draw");
    for (const ClusterDrawCommand& Draw : Refresh.TransmissiveDraws) Check((Draw.ClassMask & 2u) != 0, "transmissive stream holds an opaque draw");
    const NestingRefresh GlassOnly = ExecuteNestingRefresh({Instance(Nesting)}, RuntimeCamera(), Threshold(1e9));
    Check(*std::max_element(GlassOnly.DepthBuffer.begin(), GlassOnly.DepthBuffer.end()) <= 1, "depth buffer exists");
    const NestedCluster* GlassCluster = nullptr;
    for (const NestedCluster& Node : Nesting.Clusters)
        if ((Node.ClassMask & 2u) && Node.Error > 0)
        {
            GlassCluster = &Node;
            break;
        }
    Check(GlassCluster, "need a glass cluster with error");
    const double OpaquePx = EvaluateProjectedSphereError(GlassCluster->Sphere, GlassCluster->Error, RuntimeCamera(), nullptr, 1, &GlassCluster->Cone);
    const double GlassPx  = EvaluateProjectedSphereError(GlassCluster->Sphere, GlassCluster->Error, RuntimeCamera(), nullptr, 2, &GlassCluster->Cone);
    Check(GlassPx >= OpaquePx, "refraction deviation must not shrink the projected error");
}

void PagesAndResidency()
{
    GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision3"));
    const std::vector<NestingPage> Pages = PackNestingPages(Nesting, 16384);
    Check(Pages.size() >= 2, "1280-triangle nesting should span several 16 KB pages");
    std::vector<uint32_t> Packed;
    for (const NestingPage& Page : Pages) Packed.insert(Packed.end(), Page.Clusters.begin(), Page.Clusters.end());
    Check(Packed.size() == Nesting.Clusters.size(), "every cluster packed exactly once");
    Check(std::set<uint32_t>(Packed.begin(), Packed.end()).size() == Packed.size(), "no cluster packed twice");
    for (const NestingPage& Page : Pages)
    {
        Check(Page.BinaryPayload.size() == Page.ByteSize, "payload size differs from the page size");
        Check(Page.ByteSize <= 16384 || Page.Clusters.size() == 1, "page exceeds the fixed size budget");
        for (uint32_t Identity : Page.Clusters) Check(Nesting.Clusters[Identity].PageIndex == static_cast<int32_t>(Page.PageIndex), "cluster page index wrong");
    }
    const uint32_t CoarsestTier = static_cast<uint32_t>(Nesting.Tiers.size() - 1);
    Check(Pages[0].Pinned && std::find(Pages[0].Tiers.begin(), Pages[0].Tiers.end(), CoarsestTier) != Pages[0].Tiers.end(),
          "first page is pinned and holds the coarsest tier");
    double PinnedBytes = 0;
    for (const NestingPage& Page : Pages)
        if (Page.Pinned) PinnedBytes += Page.ByteSize;
    NestingResidency Residency(Pages, PinnedBytes + 20000);
    for (const NestingPage& Page : Pages)
        if (Page.Pinned) Check(Residency.IsPageResident(static_cast<int32_t>(Page.PageIndex)), "pinned page not resident");
    std::vector<uint32_t> NonPinned;
    for (const NestingPage& Page : Pages)
        if (!Page.Pinned) NonPinned.push_back(Page.PageIndex);
    for (uint32_t Index : NonPinned) Residency.RequestPage(static_cast<int32_t>(Index), 10);
    Residency.ServiceStreamingQueue(0, static_cast<uint32_t>(NonPinned.size()));
    Check(Residency.ResidentBytes <= Residency.MaxResidentBytes, "residency stays within its byte budget");
    for (const NestingPage& Page : Pages)
        if (Page.Pinned) Check(Residency.IsPageResident(static_cast<int32_t>(Page.PageIndex)), "pinned pages are never evicted");
    NestingResidency ColdResidency(Pages);
    RefreshOptions Options = Threshold(0.001);
    Options.Residency  = &ColdResidency;
    Options.FrameIndex = 0;
    const NestingCut Cut = SelectNestingCut(Nesting, RuntimeCamera(), nullptr, Options);
    Check(!Cut.CutClusterIdentities.empty(), "cold cut is empty");
    for (uint32_t Identity : Cut.CutClusterIdentities)
        Check(ColdResidency.IsPageResident(Nesting.Clusters[Identity].PageIndex), "cold cut may only select resident clusters");
    Check(!ColdResidency.StreamingQueue.empty(), "cold cut queues the pages it wanted");
}

void HeightfieldCutRespectsThreshold()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("HeightfieldCliff48"));
    NestingCamera TerrainCamera;
    TerrainCamera.Eye          = {0, -30, 18};
    TerrainCamera.ScreenWidth  = 256;
    TerrainCamera.ScreenHeight = 256;
    TerrainCamera.Near         = 0.1;
    TerrainCamera.Far          = 300;
    const NestingCut Coarse = SelectNestingCut(Nesting, TerrainCamera, nullptr, Threshold(8));
    const NestingCut Fine   = SelectNestingCut(Nesting, TerrainCamera, nullptr, Threshold(0.25));
    AssertCutIsCompleteFrontier(Nesting, Coarse.CutClusterIdentities);
    AssertCutIsCompleteFrontier(Nesting, Fine.CutClusterIdentities);
    Check(Coarse.CutTriangles < Fine.CutTriangles, "coarser threshold must draw fewer triangles");
    Check(Fine.CutTriangles <= Nesting.SourceTriangles, "fine cut exceeds the source");
}

void PageSlotsStreamLru()
{
    PageSlotAllocator Allocator(3);
    Allocator.Pin(0);
    Check(Allocator.Ensure(0, 0).Slot == 0, "page 0 should take slot 0");
    Check(!Allocator.Ensure(1, 0).EvictedPage.has_value(), "free slot must not evict");
    Check(!Allocator.Ensure(2, 0).EvictedPage.has_value(), "free slot must not evict");
    Allocator.Touch(1, 5);   // page 1 recently used; page 2 is now the LRU victim
    const PageSlotAllocator::Placement Loaded = Allocator.Ensure(3, 6);
    Check(Loaded.EvictedPage == 2, "page 2 should be the LRU victim");
    Check(Allocator.SlotOf(3) == static_cast<uint32_t>(Loaded.Slot), "page 3 should take the freed slot");
    Check(Allocator.IsResident(0), "pinned page survives eviction pressure");
    Check(!Allocator.IsResident(2), "evicted page still resident");
    PageSlotAllocator Blocked(1);
    Blocked.Pin(0);
    (void)Blocked.Ensure(0, 0);
    Check(Blocked.Ensure(1, 1).Slot == -1, "all-pinned slots refuse new pages instead of evicting");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     VISIBILITYRESOLVE.TEST.MJS
//------------------------------------------------------------------------------------------------------------------------

std::vector<RefreshCluster> VisibleItems(const GeometryNesting& Nesting, double Pixels, NestingRefresh& Refresh)
{
    Refresh = ExecuteNestingRefresh({Instance(Nesting)}, ResolveCamera(), Threshold(Pixels));
    return Refresh.VisibleClusters;
}

void IdentitiesPackLosslessly()
{
    for (const auto& [Slot, Triangle] : std::vector<std::pair<uint32_t, uint32_t>>{{0, 0}, {5, 127}, {1234, 42}, {65535, 255}})
    {
        const std::optional<VisibilityIdentity> Decoded = UnpackVisibilityIdentity(PackVisibilityIdentity(Slot, Triangle));
        Check(Decoded && Decoded->RecordSlot == Slot && Decoded->LocalTriangle == Triangle, "identity did not round-trip");
    }
    Check(!UnpackVisibilityIdentity(0).has_value(), "identity 0 must be the background");
}

void OpaqueSphereFillsVisibility()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision3"));
    NestingRefresh Refresh;
    const std::vector<RefreshCluster> Items = VisibleItems(Nesting, 2, Refresh);
    const VisibilityRaster Raster = RasterizeVisibility(Items, 192, 192, Refresh.ViewProjection);
    Check(Raster.OpaqueVis[96 * 192 + 96] > 0, "screen centre must hit the sphere");
    uint32_t Covered = 0;
    for (size_t Pixel = 0; Pixel < 192 * 192; ++Pixel)
    {
        if (!Raster.OpaqueVis[Pixel]) continue;
        ++Covered;
        const std::optional<VisibilityIdentity> Decoded = UnpackVisibilityIdentity(Raster.OpaqueVis[Pixel]);
        Check(Decoded->RecordSlot < Items.size(), "decoded record slot must exist");
        Check(Decoded->LocalTriangle < Items[Decoded->RecordSlot].Node->TriangleCount, "decoded triangle must belong to the cluster");
    }
    Check(Covered > 1000, "sphere should cover a good part of the image, got " + std::to_string(Covered));
    Check(std::none_of(Raster.GlassVis[0].begin(), Raster.GlassVis[0].end(), [](uint32_t Id) { return Id > 0; }), "opaque sphere produces no glass pixels");
}

void GlassPeelsIntoTwoLayers()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision3Glass"), GlassOptions());
    NestingRefresh Refresh;
    const std::vector<RefreshCluster> Items = VisibleItems(Nesting, 2, Refresh);
    const VisibilityRaster Raster = RasterizeVisibility(Items, 192, 192, Refresh.ViewProjection);
    uint32_t Front = 0, Back = 0;
    for (size_t Pixel = 0; Pixel < 192 * 192; ++Pixel)
    {
        if (Raster.GlassVis[0][Pixel])
        {
            ++Front;
            const std::optional<VisibilityIdentity> Decoded = UnpackVisibilityIdentity(Raster.GlassVis[0][Pixel]);
            Check((Items[Decoded->RecordSlot].Node->ClassMask & 2u) != 0, "glass buffer holds only transmissive clusters");
        }
        if (Raster.GlassVis[1][Pixel]) ++Back;
    }
    Check(Front > 100, "glass band front layer visible, got " + std::to_string(Front));
    Check(Back > 10, "looking through the band must peel a back layer, got " + std::to_string(Back));
}

void ResolveShadesAndRefracts()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision3Glass"), GlassOptions());
    NestingRefresh Refresh;
    const std::vector<RefreshCluster> Items = VisibleItems(Nesting, 2, Refresh);
    const VisibilityRaster Raster = RasterizeVisibility(Items, 192, 192, Refresh.ViewProjection);
    NestingCamera Camera = ResolveCamera();
    Camera.ViewProjection = Refresh.ViewProjection;
    const VisibilityResolution Resolved = ResolveVisibility(Raster, Camera);
    Check(Resolved.GlassPixels > 100, "too few glass pixels");
    Check(Resolved.TwoLayerPixels > 10, "two-layer refraction engaged, got " + std::to_string(Resolved.TwoLayerPixels));
    uint32_t Differing = 0, GlassChecked = 0;
    for (size_t Pixel = 0; Pixel < 192 * 192; ++Pixel)
    {
        if (!Raster.GlassVis[0][Pixel]) continue;
        ++GlassChecked;
        double Delta = 0;
        for (int C = 0; C < 3; ++C) Delta += std::fabs(Resolved.FinalColor[Pixel * 3 + C] - Resolved.OpaqueColor[Pixel * 3 + C]);
        if (Delta > 0.02) ++Differing;
    }
    Check(Differing > GlassChecked * 0.5, "most glass pixels should refract or tint");
    for (float Value : Resolved.FinalColor) Check(std::isfinite(Value) && Value >= 0, "resolved colours stay finite");
}

void VesselResolveFollowsUvs()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("CreasedVessel32"));
    NestingRefresh Refresh;
    const std::vector<RefreshCluster> Items = VisibleItems(Nesting, 4, Refresh);
    const VisibilityRaster Raster = RasterizeVisibility(Items, 192, 192, Refresh.ViewProjection);
    NestingCamera Camera = ResolveCamera();
    Camera.ViewProjection = Refresh.ViewProjection;
    const VisibilityResolution Resolved = ResolveVisibility(Raster, Camera);
    std::set<double> Luminances;
    for (size_t Pixel = 0; Pixel < 192 * 192; ++Pixel)
        if (Raster.OpaqueVis[Pixel]) Luminances.insert(std::round(Resolved.OpaqueColor[Pixel * 3] * 20));
    Check(Luminances.size() >= 4, "UV checker and lighting should produce varied shades, got " + std::to_string(Luminances.size()));
}

void RasterAndResolveAreDeterministic()
{
    const GeometryNesting Nesting = ConstructGeometryNesting(Surface("SphereSubdivision2"));
    NestingRefresh Refresh;
    const std::vector<RefreshCluster> Items = VisibleItems(Nesting, 2, Refresh);
    NestingCamera Camera = ResolveCamera();
    Camera.ViewProjection = Refresh.ViewProjection;
    const VisibilityResolution A = ResolveVisibility(RasterizeVisibility(Items, 128, 128, Refresh.ViewProjection), Camera);
    const VisibilityResolution B = ResolveVisibility(RasterizeVisibility(Items, 128, 128, Refresh.ViewProjection), Camera);
    Check(A.FinalColor == B.FinalColor, "resolve output differs between identical runs");
}
} // namespace

int main()
{
    const std::vector<std::pair<const char*, std::function<void()>>> Tests = {
        {"welding removes soup duplication without touching triangle coverage", WeldingRemovesDuplication},
        {"sphere builds a multi-tier crack-free nesting with monotonic error", SphereBuildsCrackFreeNesting},
        {"heightmap cliff surface (open border, hard crease) nests without cracks", HeightfieldWithoutCracks},
        {"opaque and transmissive families never share a cluster", ClassesNeverShareCluster},
        {"builds are deterministic", BuildsAreDeterministic},
        {"tiny surfaces stop at one honest leaf tier", TinySurfacesStopAtOneLeafTier},
        {"tier collection returns a renderable soup owned by clusters", TierCollectionIsRenderable},
        {"UV seams and marked sharp edges survive every tier", SeamsAndCreasesSurviveEveryTier},
        {"every simplification group shares one coarser error and sphere (runtime cut contract)", GroupsShareCoarserErrorAndSphere},
        {"projected-error cut is a complete frontier at every threshold and refines monotonically", CutIsCompleteFrontierAtEveryThreshold},
        {"frustum culling rejects off-screen instances; normal cone culls back-facing clusters but never glass", FrustumAndConeCulling},
        {"two-phase Hi-Z occlusion culls the hidden sphere on the second refresh", TwoPhaseOcclusion},
        {"glass clusters draw transmissive, never write Hi-Z depth, and refine earlier than opaque", GlassDrawsTransmissive},
        {"fixed-size pages pack every cluster once, coarsest pages are pinned, residency is LRU-bounded", PagesAndResidency},
        {"heightfield cut draws real triangles and respects the threshold direction", HeightfieldCutRespectsThreshold},
        {"page slot allocator streams LRU with pinned pages protected", PageSlotsStreamLru},
        {"visibility identities pack and unpack losslessly", IdentitiesPackLosslessly},
        {"opaque sphere fills the visibility centre and every identity decodes to a drawn triangle", OpaqueSphereFillsVisibility},
        {"glass band depth-peels into two layers and never enters the opaque visibility", GlassPeelsIntoTwoLayers},
        {"deferred resolve shades from family and UV, and refraction bends the gathered colour", ResolveShadesAndRefracts},
        {"vessel resolve is driven by interpolated UVs (both checker parities appear)", VesselResolveFollowsUvs},
        {"visibility raster and resolve are deterministic", RasterAndResolveAreDeterministic}};
    uint32_t Passed = 0, Failed = 0;
    for (const auto& [Caption, Run] : Tests)
    {
        try
        {
            Run();
            std::printf("PASS %s\n", Caption);
            ++Passed;
        }
        catch (const CheckFailure& Failure)
        {
            std::printf("FAIL %s: %s\n", Caption, Failure.Caption.c_str());
            ++Failed;
        }
        catch (const std::exception& Error)
        {
            std::printf("FAIL %s: %s\n", Caption, Error.what());
            ++Failed;
        }
    }
    std::printf("VirtualizedGeometryValidation: %u passed, %u failed\n", Passed, Failed);
    return Failed == 0 ? 0 : 1;
}
