/*==========================================================================================================================================
                                                 GEOMETRY PIPELINE VALIDATION (EXHIBIT)
                                     ------------------------------------------------------------------
                 Headless proof for ClusterNesting + VisibilityCull: procedural meshes, exact invariants.
==========================================================================================================================================*/

// Proves the portable geometry core without any demo corpus (the demo assets stay on their own branch):
//   - builds refuse garbage (empty mesh, dangling index) and are deterministic (structure hash);
//   - every leaf bound encloses its vertices, every parent encloses its children, error is monotone;
//   - a cut partitions the mesh exactly once at every threshold and refines monotonically;
//   - frustum tests agree with point tests and never reject a cluster holding an inside triangle;
//   - the HiZ pyramid propagates maxima exactly and occludes only what is truly behind the occluder.

#include "Engine/Rendering/ClusterNesting.h"
#include "Engine/Rendering/VisibilityCull.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace Frontier;

namespace
{

int  Failures = 0;
int  Total    = 0;

void Check(bool Condition, const char* Label)
{
    ++Total;
    if (!Condition)
    {
        ++Failures;
        std::printf("FAIL  %s\n", Label);
    }
}

SpatialVector Normalised(const SpatialVector& V)
{
    const double Length = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
    return {V.X / Length, V.Y / Length, V.Z / Length};
}

double Distance(const SpatialVector& A, const SpatialVector& B)
{
    const double DX = A.X - B.X, DY = A.Y - B.Y, DZ = A.Z - B.Z;
    return std::sqrt(DX * DX + DY * DY + DZ * DZ);
}

TriangleMesh MakeIcosphere(double Radius, int Subdivisions)
{
    const double T = (1.0 + std::sqrt(5.0)) * 0.5;
    TriangleMesh Mesh;
    const SpatialVector Seed[12] = {{-1, T, 0}, {1, T, 0}, {-1, -T, 0}, {1, -T, 0}, {0, -1, T}, {0, 1, T},
                                    {0, -1, -T}, {0, 1, -T}, {T, 0, -1}, {T, 0, 1}, {-T, 0, -1}, {-T, 0, 1}};
    for (const SpatialVector& V : Seed)
    {
        const SpatialVector N = Normalised(V);
        Mesh.Vertices.push_back({N.X * Radius, N.Y * Radius, N.Z * Radius});
    }
    const uint32_t Faces[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9},
                                   {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},   {3, 9, 4},  {3, 4, 2},
                                   {3, 2, 6},  {3, 6, 8}, {3, 8, 9},  {4, 9, 5},   {2, 4, 11}, {6, 2, 10},
                                   {8, 6, 7},  {9, 8, 1}};
    for (const auto& Face : Faces)
    {
        Mesh.Indices.insert(Mesh.Indices.end(), {Face[0], Face[1], Face[2]});
    }
    for (int Pass = 0; Pass < Subdivisions; ++Pass)
    {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> Midpoints;
        const auto Midpoint = [&](uint32_t A, uint32_t B) {
            const auto Key = std::make_pair(std::min(A, B), std::max(A, B));
            const auto Found = Midpoints.find(Key);
            if (Found != Midpoints.end())
            {
                return Found->second;
            }
            const SpatialVector& PA = Mesh.Vertices[A];
            const SpatialVector& PB = Mesh.Vertices[B];
            const SpatialVector  N  = Normalised({(PA.X + PB.X) * 0.5, (PA.Y + PB.Y) * 0.5, (PA.Z + PB.Z) * 0.5});
            const uint32_t       Index = static_cast<uint32_t>(Mesh.Vertices.size());
            Mesh.Vertices.push_back({N.X * Radius, N.Y * Radius, N.Z * Radius});
            Midpoints.emplace(Key, Index);
            return Index;
        };
        std::vector<uint32_t> Next;
        for (size_t Tri = 0; Tri + 2 < Mesh.Indices.size(); Tri += 3)
        {
            const uint32_t A = Mesh.Indices[Tri], B = Mesh.Indices[Tri + 1], C = Mesh.Indices[Tri + 2];
            const uint32_t AB = Midpoint(A, B), BC = Midpoint(B, C), CA = Midpoint(C, A);
            Next.insert(Next.end(), {A, AB, CA, B, BC, AB, C, CA, BC, AB, BC, CA});
        }
        Mesh.Indices = std::move(Next);
    }
    return Mesh;
}

TriangleMesh MakeHeightfield(uint32_t Cells, double Extent)
{
    TriangleMesh Mesh;
    for (uint32_t Y = 0; Y <= Cells; ++Y)
    {
        for (uint32_t X = 0; X <= Cells; ++X)
        {
            const double U = static_cast<double>(X) / Cells, V = static_cast<double>(Y) / Cells;
            Mesh.Vertices.push_back({(U - 0.5) * Extent, std::sin(U * 9.0) * std::cos(V * 7.0) * 0.4,
                                     (V - 0.5) * Extent});
        }
    }
    const uint32_t Row = Cells + 1;
    for (uint32_t Y = 0; Y < Cells; ++Y)
    {
        for (uint32_t X = 0; X < Cells; ++X)
        {
            const uint32_t A = Y * Row + X, B = A + 1, C = A + Row, D = C + 1;
            Mesh.Indices.insert(Mesh.Indices.end(), {A, C, B, B, C, D});
        }
    }
    return Mesh;
}

uint32_t SubtreeMark(const ClusterNesting& Nesting, uint32_t NodeIndex, std::vector<uint32_t>& LeafMarks,
                     uint32_t& LeafCount)
{
    const ClusterNode& Node = Nesting.Nodes()[NodeIndex];
    if (Node.ChildCount == 0)
    {
        LeafMarks[NodeIndex] += 1;
        ++LeafCount;
        return Node.TriangleCount;
    }
    uint32_t Sum = 0;
    for (uint32_t Slot = 0; Slot < Node.ChildCount; ++Slot)
    {
        Sum += SubtreeMark(Nesting, Nesting.ChildIndices()[Node.FirstChild + Slot], LeafMarks, LeafCount);
    }
    return Sum;
}

void ValidateNesting(const TriangleMesh& Mesh, const char* Name)
{
    ClusterNesting Nesting;
    Check(Nesting.Build(Mesh, 64), (std::string(Name) + ": build succeeds").c_str());
    Check(Nesting.TriangleTotal() == Mesh.Indices.size() / 3, (std::string(Name) + ": triangle total").c_str());

    ClusterNesting Again;
    (void)Again.Build(Mesh, 64);
    Check(Again.StructureHash() == Nesting.StructureHash(), (std::string(Name) + ": deterministic build").c_str());

    // Bounds enclosure + monotone error + subtree triangle sums, over every node.
    bool EnclosureOk = true, ErrorOk = true, SumOk = true;
    const std::vector<ClusterNode>& Nodes = Nesting.Nodes();
    for (uint32_t Index = 0; Index < Nodes.size(); ++Index)
    {
        const ClusterNode& Node = Nodes[Index];
        if (Node.ChildCount == 0)
        {
            for (uint32_t Slot = 0; Slot < Node.TriangleCount; ++Slot)
            {
                const uint32_t Triangle = Nesting.TriangleOrder()[Node.FirstTriangle + Slot];
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                {
                    if (Distance(Mesh.Vertices[Mesh.Indices[Triangle * 3 + Corner]], Node.Bounds.Centre) >
                        Node.Bounds.Radius + 1e-9)
                    {
                        EnclosureOk = false;
                    }
                }
            }
        }
        else
        {
            uint32_t Sum = 0;
            for (uint32_t Slot = 0; Slot < Node.ChildCount; ++Slot)
            {
                const ClusterNode& Child = Nodes[Nesting.ChildIndices()[Node.FirstChild + Slot]];
                Sum += Child.TriangleCount;
                if (Distance(Child.Bounds.Centre, Node.Bounds.Centre) + Child.Bounds.Radius >
                    Node.Bounds.Radius + 1e-9)
                {
                    EnclosureOk = false;
                }
                if (Child.GeometricError > Node.GeometricError + 1e-12)
                {
                    ErrorOk = false;
                }
            }
            if (Sum != Node.TriangleCount)
            {
                SumOk = false;
            }
        }
    }
    Check(EnclosureOk, (std::string(Name) + ": bounds enclose vertices and children").c_str());
    Check(ErrorOk, (std::string(Name) + ": geometric error monotone to root").c_str());
    Check(SumOk, (std::string(Name) + ": subtree triangle sums").c_str());
    Check(Nodes[Nesting.RootIndex()].TriangleCount == Nesting.TriangleTotal(),
          (std::string(Name) + ": root covers all triangles").c_str());

    // Cuts partition exactly once at several thresholds and refine monotonically as it shrinks.
    const double            Factor = ClusterNesting::ProjectionFactor(1080.0, 60.0 * 3.141592653589793 / 180.0);
    const SpatialVector     Camera{3.0, 1.5, 4.0};
    size_t                  PreviousCut = 0;
    bool                    Refines     = true;
    const double            Thresholds[5] = {1e9, 32.0, 8.0, 2.0, 0.0};
    for (const double Threshold : Thresholds)
    {
        std::vector<uint32_t> Cut;
        Nesting.SelectCut(Camera, Factor, Threshold, Cut);
        std::vector<uint32_t> LeafMarks(Nodes.size(), 0);
        uint32_t              LeafHits = 0, TriSum = 0;
        for (const uint32_t CutNode : Cut)
        {
            TriSum += SubtreeMark(Nesting, CutNode, LeafMarks, LeafHits);
        }
        bool ExactlyOnce = TriSum == Nesting.TriangleTotal();
        for (uint32_t Index = 0; Index < Nodes.size(); ++Index)
        {
            if (Nodes[Index].ChildCount == 0 && LeafMarks[Index] != 1)
            {
                ExactlyOnce = false;
            }
        }
        Check(ExactlyOnce, (std::string(Name) + ": cut partitions mesh exactly once").c_str());
        if (Cut.size() < PreviousCut)
        {
            Refines = false;
        }
        PreviousCut = Cut.size();
    }
    Check(Refines, (std::string(Name) + ": cut refines monotonically with threshold").c_str());

    std::vector<uint32_t> Cut;
    Nesting.SelectCut(Camera, Factor, 1e18, Cut);
    Check(Cut.size() == 1 && Cut[0] == Nesting.RootIndex(), (std::string(Name) + ": huge budget -> root only").c_str());
    Nesting.SelectCut(Camera, Factor, 0.0, Cut);
    bool AllLeaves = !Cut.empty();
    for (const uint32_t CutNode : Cut)
    {
        AllLeaves = AllLeaves && Nodes[CutNode].ChildCount == 0;
    }
    Check(AllLeaves, (std::string(Name) + ": zero budget -> all leaves").c_str());

    // Approaching the mesh refines the cut at a fixed budget.
    std::vector<uint32_t> FarCut, NearCut;
    Nesting.SelectCut({40.0, 20.0, 50.0}, Factor, 4.0, FarCut);
    Nesting.SelectCut({1.2, 0.4, 1.4}, Factor, 4.0, NearCut);
    Check(NearCut.size() >= FarCut.size(), (std::string(Name) + ": nearer camera refines").c_str());

    std::printf("%s: %zu nodes, root error %.4f m, hash %llu\n", Name, Nodes.size(),
                Nodes[Nesting.RootIndex()].GeometricError,
                static_cast<unsigned long long>(Nesting.StructureHash()));
}

} // namespace

int main()
{
    // -- Refusals -------------------------------------------------------------------------------------
    {
        ClusterNesting Nesting;
        TriangleMesh   Empty;
        Check(!Nesting.Build(Empty) && Nesting.Empty(), "empty mesh refused");
        TriangleMesh Broken;
        Broken.Vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        Broken.Indices  = {0, 1, 7};
        Check(!Nesting.Build(Broken) && Nesting.Empty(), "dangling index refused");
        Broken.Indices = {0, 1, 2};
        Check(Nesting.Build(Broken) && Nesting.TriangleTotal() == 1, "single triangle builds");
    }

    // -- Nesting invariants on two procedural corpora --------------------------------------------------
    ValidateNesting(MakeIcosphere(1.0, 3), "icosphere(1280 tri)");
    ValidateNesting(MakeHeightfield(24, 8.0), "heightfield(1152 tri)");

    // -- Frustum --------------------------------------------------------------------------------------
    CullCamera Camera;
    Camera.Position  = {0.0, 0.0, 5.0};
    Camera.Forward   = {0.0, 0.0, -1.0};
    Camera.Right     = {1.0, 0.0, 0.0};
    Camera.Up        = {0.0, 1.0, 0.0};
    Camera.NearPlane = 0.1;
    Camera.FarPlane  = 100.0;
    CameraFrustum Frustum;
    Check(Frustum.Build(Camera), "frustum builds");
    Check(Frustum.Planes().size() == 6, "frustum has six planes");
    Check(Frustum.Contains({0.0, 0.0, 0.0}), "centre point inside");
    Check(!Frustum.Contains({0.0, 0.0, 6.0}), "point behind camera outside");
    Check(!Frustum.Contains({50.0, 0.0, -40.0}), "point far right outside");
    Check(!Frustum.Contains({0.0, 0.0, -200.0}), "point beyond far plane outside");
    Check(Frustum.Intersects({{0.0, 0.0, 0.0}, 1.0}), "centred sphere intersects");
    Check(!Frustum.Intersects({{30.0, 0.0, -10.0}, 0.5}), "off-axis sphere rejected");
    Check(Frustum.Intersects({{30.0, 0.0, -10.0}, 40.0}), "large sphere still intersects");

    // No false negatives: any leaf holding an inside vertex must pass the sphere test.
    {
        const TriangleMesh Mesh = MakeIcosphere(2.0, 2);
        ClusterNesting     Nesting;
        (void)Nesting.Build(Mesh, 32);
        bool Conservative = true;
        for (const ClusterNode& Node : Nesting.Nodes())
        {
            if (Node.ChildCount != 0)
            {
                continue;
            }
            bool AnyInside = false;
            for (uint32_t Slot = 0; Slot < Node.TriangleCount; ++Slot)
            {
                const uint32_t Triangle = Nesting.TriangleOrder()[Node.FirstTriangle + Slot];
                for (uint32_t Corner = 0; Corner < 3; ++Corner)
                {
                    AnyInside = AnyInside || Frustum.Contains(Mesh.Vertices[Mesh.Indices[Triangle * 3 + Corner]]);
                }
            }
            if (AnyInside && !Frustum.Intersects(Node.Bounds))
            {
                Conservative = false;
            }
        }
        Check(Conservative, "frustum sphere test has no false negatives");
    }

    // -- HiZ pyramid ----------------------------------------------------------------------------------
    {
        // Handcrafted 4x4: left half wall at 0.5, right half open (far = 1.0), one near texel 0.1.
        double Depth[16];
        for (int Y = 0; Y < 4; ++Y)
        {
            for (int X = 0; X < 4; ++X)
            {
                Depth[Y * 4 + X] = X < 2 ? 0.5 : 1.0;
            }
        }
        Depth[3 * 4 + 3] = 0.1;
        HiZPyramid Pyramid;
        Check(Pyramid.BuildFromDepth(Depth, 4, 4), "HiZ builds");
        Check(Pyramid.Levels() == 3, "HiZ level count (4x4 -> 3)");
        Check(std::fabs(Pyramid.Sample(1, 0, 0) - 0.5) < 1e-12, "HiZ level1 left = 0.5");
        Check(std::fabs(Pyramid.Sample(1, 1, 1) - 1.0) < 1e-12, "HiZ level1 max propagates over near texel");
        Check(std::fabs(Pyramid.Sample(2, 0, 0) - 1.0) < 1e-12, "HiZ top = global max");
        Check(!Pyramid.BuildFromDepth(nullptr, 4, 4), "HiZ refuses null buffer");
        (void)Pyramid.BuildFromDepth(Depth, 4, 4);

        // Occlusion against a full-screen wall at NDC 0.5 (separate pyramid so mip choice is unambiguous).
        std::vector<double> Wall(64 * 64, 0.5);
        HiZPyramid          WallPyramid;
        (void)WallPyramid.BuildFromDepth(Wall.data(), 64, 64);
        // Camera looks down -Z from origin; NDC z 0.5 ~ view z (0.1 + 0.5 * 99.9) = 50.05.
        CullCamera OccCam;
        OccCam.Position  = {0.0, 0.0, 0.0};
        OccCam.NearPlane = 0.1;
        OccCam.FarPlane  = 100.0;
        Check(WallPyramid.IsOccluded(OccCam, {{0.0, 0.0, -80.0}, 1.0}), "sphere behind wall occluded");
        Check(!WallPyramid.IsOccluded(OccCam, {{0.0, 0.0, -20.0}, 1.0}), "sphere in front of wall visible");
        Check(!WallPyramid.IsOccluded(OccCam, {{0.0, 0.0, -50.05}, 2.0}), "sphere straddling wall visible");
    }

    // -- Cull driver ----------------------------------------------------------------------------------
    {
        const TriangleMesh Mesh = MakeHeightfield(16, 20.0);
        ClusterNesting     Nesting;
        (void)Nesting.Build(Mesh, 32);
        const double          Factor = ClusterNesting::ProjectionFactor(1080.0, Camera.FovYRadians);
        std::vector<uint32_t> Cut, Visible;
        Nesting.SelectCut(Camera.Position, Factor, 2.0, Cut);
        CullCut(Nesting, Cut, Frustum, Camera, nullptr, Visible);
        Check(!Visible.empty() && Visible.size() <= Cut.size(), "cull keeps a non-empty subset");
        bool SubsetInOrder = true;
        size_t Scan = 0;
        for (const uint32_t Kept : Visible)
        {
            while (Scan < Cut.size() && Cut[Scan] != Kept)
            {
                ++Scan;
            }
            if (Scan >= Cut.size())
            {
                SubsetInOrder = false;
            }
        }
        Check(SubsetInOrder, "cull preserves cut order");

        // Camera facing away: everything must go.
        CullCamera Away = Camera;
        Away.Position   = {0.0, 0.0, 60.0};   // Beyond the 20 m field, looking further +Z: all behind
        Away.Forward    = {0.0, 0.0, 1.0};
        CameraFrustum AwayFrustum;
        (void)AwayFrustum.Build(Away);
        CullCut(Nesting, Cut, AwayFrustum, Away, nullptr, Visible);
        Check(Visible.empty(), "camera facing away culls all");
    }

    std::printf("Geometry pipeline validation: %d/%d checks passed\n", Total - Failures, Total);
    return Failures == 0 ? 0 : 1;
}
