/*==========================================================================================================================================
                                                    FACETCLUSTERVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Headless K1 validation: build the reference corpora as facet soup, weld them, partition scored clusters, validate the
//    partition against the build invariants, and emit both a human report and a machine-comparable cluster dump.

#include "Engine/Geometry/FacetClusterBuild.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr double CircleRatio = 3.14159265358979323846;

//--------------------------------------------------------------------------------------------------------------------------
//                                                    CORPORA (reference parity)
//--------------------------------------------------------------------------------------------------------------------------

struct Corpus
{
    std::vector<Frontier::SpatialPosition>        Corners;
    std::vector<std::array<double, 2>>            CornerUVs;
    std::vector<uint32_t>                         Family;
    std::vector<uint8_t>                          SharpMask;
    const char                                   *Name = "";
};

Frontier::SpatialPosition Normalize(const Frontier::SpatialPosition &Position)
{
    const double Magnitude = std::hypot(Position.X, Position.Y, Position.Z);
    const double Divisor   = Magnitude > 0.0 ? Magnitude : 1.0;
    return { Position.X / Divisor, Position.Y / Divisor, Position.Z / Divisor };
}

// 📝 Subdivided icosahedron projected to the unit sphere — the reference smooth closed manifold (GeometryHierarchyCorpus.mjs).
Corpus ConstructSphereFacets(uint32_t Subdivisions)
{
    const double Ratio = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<Frontier::SpatialPosition> Vertices = {
        { -1, Ratio, 0 }, { 1, Ratio, 0 }, { -1, -Ratio, 0 }, { 1, -Ratio, 0 },
        { 0, -1, Ratio }, { 0, 1, Ratio }, { 0, -1, -Ratio }, { 0, 1, -Ratio },
        { Ratio, 0, -1 }, { Ratio, 0, 1 }, { -Ratio, 0, -1 }, { -Ratio, 0, 1 }
    };
    for (auto &Vertex : Vertices) Vertex = Normalize(Vertex);
    std::vector<std::array<uint32_t, 3>> Faces = {
        { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 },
        { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 },
        { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 }
    };
    for (uint32_t Pass = 0; Pass < Subdivisions; ++Pass)
    {
        std::vector<std::array<uint32_t, 3>> Next;
        std::vector<std::array<uint32_t, 3>> MidpointKeys;
        std::vector<uint32_t> Midpoints;
        const auto Midpoint = [&](uint32_t A, uint32_t B) -> uint32_t
        {
            const uint32_t Lower = A < B ? A : B;
            const uint32_t Upper = A < B ? B : A;
            for (std::size_t Index = 0; Index < MidpointKeys.size(); ++Index)
                if (MidpointKeys[Index][0] == Lower && MidpointKeys[Index][1] == Upper) return Midpoints[Index];
            const uint32_t Identity = uint32_t(Vertices.size());
            Vertices.push_back(Normalize({ (Vertices[A].X + Vertices[B].X) / 2.0,
                                           (Vertices[A].Y + Vertices[B].Y) / 2.0,
                                           (Vertices[A].Z + Vertices[B].Z) / 2.0 }));
            MidpointKeys.push_back({ Lower, Upper });
            Midpoints.push_back(Identity);
            return Identity;
        };
        for (const auto &Face : Faces)
        {
            const uint32_t AB = Midpoint(Face[0], Face[1]);
            const uint32_t BC = Midpoint(Face[1], Face[2]);
            const uint32_t CA = Midpoint(Face[2], Face[0]);
            Next.push_back({ Face[0], AB, CA });
            Next.push_back({ Face[1], BC, AB });
            Next.push_back({ Face[2], CA, BC });
            Next.push_back({ AB, BC, CA });
        }
        Faces = Next;
    }
    Corpus Result;
    Result.Name = "sphere";
    for (const auto &Face : Faces)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            Result.Corners.push_back(Vertices[Face[Corner]]);
            Result.CornerUVs.push_back({ 0.0, 0.0 });
        }
    Result.Family.assign(Faces.size(), 0u);
    Result.SharpMask.assign(Faces.size(), 0u);
    return Result;
}

// 📝 Triangulated heightmap with one hard cliff step — the reference open-border terrain (GeometryHierarchyCorpus.mjs).
Corpus ConstructHeightfieldFacets(uint32_t Resolution, double Extent)
{
    const auto Height = [](double X, double Y)
    {
        const double Rolling = std::sin(X * 0.31) * std::cos(Y * 0.27) * 1.4 + std::sin(X * 0.09 + Y * 0.13) * 2.2;
        const double Cliff   = X > std::sin(Y * 0.21) * 4.0 ? 7.0 : 0.0;
        return Rolling + Cliff;
    };
    const auto Point = [&](uint32_t I, uint32_t J)
    {
        const double X = (double(I) / double(Resolution) - 0.5) * Extent;
        const double Y = (double(J) / double(Resolution) - 0.5) * Extent;
        return Frontier::SpatialPosition{ X, Y, Height(X, Y) };
    };
    Corpus Result;
    Result.Name = "heightfield";
    for (uint32_t J = 0; J < Resolution; ++J)
        for (uint32_t I = 0; I < Resolution; ++I)
        {
            const Frontier::SpatialPosition A = Point(I, J);
            const Frontier::SpatialPosition B = Point(I + 1, J);
            const Frontier::SpatialPosition C = Point(I + 1, J + 1);
            const Frontier::SpatialPosition D = Point(I, J + 1);
            for (const auto &Facet : { std::array<Frontier::SpatialPosition, 3>{ A, B, C },
                                       std::array<Frontier::SpatialPosition, 3>{ A, C, D } })
                for (const auto &Corner : Facet)
                {
                    Result.Corners.push_back(Corner);
                    Result.CornerUVs.push_back({ 0.0, 0.0 });
                }
            Result.Family.push_back(1u);
            Result.Family.push_back(1u);
            Result.SharpMask.push_back(0u);
            Result.SharpMask.push_back(0u);
        }
    return Result;
}

// 📝 Closed UV-mapped cylinder with a meridian seam and marked sharp rims — the seam/crease ingestion path the reference
//    exercises with its vessel. Built here so K1 has UV and crease coverage that does not depend on the vessel's cap layout.
Corpus ConstructSeamCylinderFacets(uint32_t Segments, uint32_t Rings)
{
    const auto Point = [&](uint32_t Slice, uint32_t Ring)
    {
        const double Angle = (double(Slice % Segments) / double(Segments)) * CircleRatio * 2.0;
        const double Ridge = 1.0 + 0.22 * (1.0 - std::abs(2.0 * (double(Ring) / double(Rings)) - 1.0));
        const double Z     = (double(Ring) / double(Rings) - 0.5) * 2.0;
        return Frontier::SpatialPosition{ std::cos(Angle) * Ridge, std::sin(Angle) * Ridge, Z };
    };
    const auto Uv = [&](uint32_t Slice, uint32_t Ring)
    {
        return std::array<double, 2>{ double(Slice) / double(Segments), 0.15 + 0.6 * (double(Ring) / double(Rings)) };
    };
    Corpus Result;
    Result.Name = "seam-cylinder";
    for (uint32_t Ring = 0; Ring < Rings; ++Ring)
        for (uint32_t Slice = 0; Slice < Segments; ++Slice)
        {
            const Frontier::SpatialPosition P00 = Point(Slice, Ring);
            const Frontier::SpatialPosition P10 = Point(Slice + 1, Ring);
            const Frontier::SpatialPosition P11 = Point(Slice + 1, Ring + 1);
            const Frontier::SpatialPosition P01 = Point(Slice, Ring + 1);
            const std::array<double, 2> U00 = Uv(Slice, Ring), U10 = Uv(Slice + 1, Ring);
            const std::array<double, 2> U11 = Uv(Slice + 1, Ring + 1), U01 = Uv(Slice, Ring + 1);
            const uint8_t SharpBottom = (Ring == 0 || Ring == Rings / 2) ? 1u : 0u;
            const uint8_t SharpTop    = (Ring + 1 == Rings || Ring + 1 == Rings / 2) ? 2u : 0u;
            // 📝 Two facets per quad, corners in reference winding: (P00, P10, P11) then (P00, P11, P01).
            const std::array<Frontier::SpatialPosition, 6> Corners = { P00, P10, P11, P00, P11, P01 };
            const std::array<std::array<double, 2>, 6> UVs     = { U00, U10, U11, U00, U11, U01 };
            for (const Frontier::SpatialPosition &Corner : Corners) Result.Corners.push_back(Corner);
            for (const std::array<double, 2> &Uv : UVs) Result.CornerUVs.push_back(Uv);
            Result.Family.push_back(2u);
            Result.Family.push_back(2u);
            Result.SharpMask.push_back(SharpBottom);
            Result.SharpMask.push_back(SharpTop);
        }
    return Result;
}

Frontier::FacetField QueryFacetField(const Corpus &Source, bool WithUVs)
{
    Frontier::FacetField Field;
    Field.Corners   = Source.Corners;
    Field.Family    = Source.Family;
    Field.SharpMask = Source.SharpMask;
    if (WithUVs) Field.CornerUVs = Source.CornerUVs;
    return Field;
}

//--------------------------------------------------------------------------------------------------------------------------
//                                                    REPORTING
//--------------------------------------------------------------------------------------------------------------------------

struct Summary
{
    std::string Name;
    std::size_t Facets       = 0;
    std::size_t Vertices     = 0;
    std::size_t Clusters     = 0;
    std::size_t LargestFacetCount  = 0;
    std::size_t LargestVertexCount = 0;
    uint32_t    SharpEdges   = 0;
    uint32_t    UvSeams      = 0;
    uint32_t    Boundaries   = 0;
    std::size_t Violations   = 0;
    double      BuildMilliseconds = 0.0;
};

void EmitClusterDump(std::ostream &Stream, const std::vector<Frontier::FacetCluster> &Clusters)
{
    Stream.precision(17);
    Stream << "\"" << "clusters" << "\":[";
    for (std::size_t Identity = 0; Identity < Clusters.size(); ++Identity)
    {
        const Frontier::FacetCluster &Cluster = Clusters[Identity];
        if (Identity != 0) Stream << ",";
        Stream << "{\"first\":" << Cluster.Triangles.front() << ",\"count\":" << Cluster.Triangles.size()
               << ",\"minimum\":[" << Cluster.Minimum[0] << "," << Cluster.Minimum[1] << "," << Cluster.Minimum[2] << "]"
               << ",\"maximum\":[" << Cluster.Maximum[0] << "," << Cluster.Maximum[1] << "," << Cluster.Maximum[2] << "]"
               << ",\"centre\":[" << Cluster.SphereCentre.X << "," << Cluster.SphereCentre.Y << ","
               << Cluster.SphereCentre.Z << "]"
               << ",\"radius\":" << Cluster.SphereRadius
               << ",\"axis\":[" << Cluster.ConeAxis.X << "," << Cluster.ConeAxis.Y << "," << Cluster.ConeAxis.Z << "]"
               << ",\"cutoff\":" << Cluster.ConeCutoff << "}";
    }
    Stream << "]";
}

Summary Evaluate(const Corpus &Source, bool WithUVs, const Frontier::ClusterSpecification &Specification,
                 std::ostream *DumpStream)
{
    Summary Result;
    Result.Name = Source.Name;

    const Frontier::FacetField Field = QueryFacetField(Source, WithUVs);
    Frontier::WeldSpecification WeldSpecification;
    const std::chrono::steady_clock::time_point Start = std::chrono::steady_clock::now();
    const Frontier::WeldOutcome Weld = Frontier::WeldFacetField(Field, WeldSpecification);
    const std::vector<Frontier::FacetCluster> Clusters = Frontier::PartitionFacetClusters(Weld.Complex, Specification);
    const std::chrono::steady_clock::time_point Finish = std::chrono::steady_clock::now();

    Result.Facets       = Weld.Complex.Triangles.size() / 3;
    Result.Vertices     = Weld.Complex.Positions.size();
    Result.Clusters     = Clusters.size();
    Result.SharpEdges   = Weld.SharpEdgeCount;
    Result.UvSeams      = Weld.UvSeamEdgeCount;
    Result.Boundaries   = Weld.BoundaryEdgeCount;
    Result.BuildMilliseconds =
        std::chrono::duration<double, std::milli>(Finish - Start).count();

    std::vector<bool> Covered(Result.Facets, false);
    for (const auto &Cluster : Clusters)
    {
        std::vector<uint32_t> DistinctVertices;
        for (const uint32_t Triangle : Cluster.Triangles)
        {
            if (Triangle < Covered.size()) Covered[Triangle] = true;
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
            {
                const uint32_t Vertex = Weld.Complex.Triangles[std::size_t(Triangle) * 3 + Corner];
                if (std::find(DistinctVertices.begin(), DistinctVertices.end(), Vertex) == DistinctVertices.end())
                    DistinctVertices.push_back(Vertex);
            }
        }
        Result.LargestFacetCount  = std::max(Result.LargestFacetCount, Cluster.Triangles.size());
        Result.LargestVertexCount = std::max(Result.LargestVertexCount, DistinctVertices.size());
    }
    for (const bool Claimed : Covered)
        if (!Claimed) ++Result.Violations;

    const std::vector<std::string> Violations =
        Frontier::ValidateClusterBuild(Weld.Complex, Clusters, Specification);
    Result.Violations = Violations.size();

    if (DumpStream != nullptr)
    {
        *DumpStream << "\"" << Source.Name << "\":{\"triangles\":" << Result.Facets << ",\"vertices\":" << Result.Vertices
                    << ",\"sharp\":" << Result.SharpEdges << ",\"uvseam\":" << Result.UvSeams
                    << ",\"boundary\":" << Result.Boundaries << ",";
        EmitClusterDump(*DumpStream, Clusters);
        *DumpStream << "}";
    }

    std::cout << "  corpus " << Source.Name << "\n"
              << "    facets " << Result.Facets << "  welded vertices " << Result.Vertices
              << "  clusters " << Result.Clusters << "\n"
              << "    sharp edges " << Result.SharpEdges << "  uv seams " << Result.UvSeams
              << "  open-boundary edges " << Result.Boundaries << "\n"
              << "    largest cluster " << Result.LargestFacetCount << " facets / " << Result.LargestVertexCount
              << " vertices  (caps " << Specification.TriangleLimit << " / " << Specification.VertexLimit << ")\n"
              << "    build " << Result.BuildMilliseconds << " ms  violations " << Result.Violations << "\n";
    for (std::size_t Index = 0; Index < Violations.size() && Index < 8; ++Index)
        std::cout << "      ! " << Violations[Index] << "\n";
    if (Violations.size() > 8) std::cout << "      ! ... " << (Violations.size() - 8) << " more\n";
    return Result;
}

}   // namespace

int main(int ArgumentCount, char **Arguments)
{
    const char *DumpPath = nullptr;
    for (int Index = 1; Index < ArgumentCount; ++Index)
        if (std::strcmp(Arguments[Index], "--dump") == 0 && Index + 1 < ArgumentCount) DumpPath = Arguments[Index + 1];

    Frontier::ClusterSpecification Specification;
    std::cout << "FacetClusterValidation - virtualized-geometry build K1\n"
              << "  triangle cap " << Specification.TriangleLimit << "  vertex cap " << Specification.VertexLimit
              << "  transmissive classes none (single opaque class, as the reference default)\n\n";

    // 📝 The dump is a JSON object of corpora; a separator is written before every entry after the first.
    std::ofstream Dump;
    std::size_t DumpedCorpora = 0;
    const auto DumpTarget = [&]() -> std::ostream *
    {
        if (DumpPath == nullptr) return nullptr;
        if (DumpedCorpora == 0) Dump.open(DumpPath), Dump << "{";
        else Dump << ",";
        ++DumpedCorpora;
        return &Dump;
    };

    const Summary Sphere = Evaluate(ConstructSphereFacets(4), false, Specification, DumpTarget());
    const Summary Cliff  = Evaluate(ConstructHeightfieldFacets(96, 40.0), false, Specification, DumpTarget());
    const Frontier::ClusterSpecification CylinderSpecification;
    const Summary Cylinder = Evaluate(ConstructSeamCylinderFacets(48, 20), true, CylinderSpecification, DumpTarget());
    if (DumpPath != nullptr && DumpedCorpora != 0) Dump << "}\n";

    // 📝 Reference parity expectations from the HTML builder (GeometryHierarchy.js) run on the same corpora.
    const bool SphereParity        = Sphere.Facets == 5120 && Sphere.Vertices == 2562 && Sphere.Clusters == 48;
    const bool CliffParity         = Cliff.Facets == 18432 && Cliff.Vertices == 9409;
    const bool CylinderSeamPresent = Cylinder.UvSeams > 0 && Cylinder.SharpEdges > 0;
    const bool CapsRespected       = Sphere.LargestFacetCount <= Specification.TriangleLimit &&
                                     Cliff.LargestFacetCount <= Specification.TriangleLimit &&
                                     Cylinder.LargestFacetCount <= Specification.TriangleLimit &&
                                     Sphere.LargestVertexCount <= Specification.VertexLimit &&
                                     Cliff.LargestVertexCount <= Specification.VertexLimit &&
                                     Cylinder.LargestVertexCount <= Specification.VertexLimit;
    const bool InvariantsHold = Sphere.Violations == 0 && Cliff.Violations == 0 && Cylinder.Violations == 0;

    std::cout << "\n  checks\n"
              << "    sphere matches the reference weld and cluster count (5120 / 2562 / 48)   "
              << (SphereParity ? "PASS" : "FAIL") << "\n"
              << "    heightfield matches the reference weld (18432 facets / 9409 vertices)     "
              << (CliffParity ? "PASS" : "FAIL") << "\n"
              << "    seam cylinder exposes UV seams and marked sharp edges                    "
              << (CylinderSeamPresent ? "PASS" : "FAIL") << "\n"
              << "    every cluster inside the facet and welded-vertex caps                     "
              << (CapsRespected ? "PASS" : "FAIL") << "\n"
              << "    partition invariants hold on all corpora                                 "
              << (InvariantsHold ? "PASS" : "FAIL") << "\n";

    const bool Passed = SphereParity && CliffParity && CylinderSeamPresent && CapsRespected && InvariantsHold;
    std::cout << "\n  result " << (Passed ? "PASS" : "FAIL") << "\n";
    return Passed ? 0 : 1;
}
