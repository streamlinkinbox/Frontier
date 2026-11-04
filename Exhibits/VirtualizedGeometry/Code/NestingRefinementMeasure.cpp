/*==========================================================================================================================================
                                                     NESTINGREFINEMENTMEASURE.CPP
==========================================================================================================================================*/
// 🧩 Before and after for every refinement of the ported builder (I2 threads, I3 measured deviation, I4 needle guard, I5 class
//    borders): per tier triangles, needles, claimed error and the measured two-sided deviation from the source, the cut sizes at the
//    parity distances, validation, and build times — the demo's own build is the "Demo" row of every table.

// 📝 Usage: NestingRefinementMeasure [threads for the parallel row, default 2]. Writes the tables to stdout
//    (Results/NestingRefinement.txt is this output).
#include "SurfaceSoupArchive.h"
#include <Rendering/VirtualizedGeometry/NestingSelection.h>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
using namespace Frontier::VirtualizedGeometry;

namespace
{
using Facet = std::array<Point3, 3>;

double SquaredTriangleDistance(const Point3& P, const Facet& T) noexcept
{
    const Point3 AB = Subtract(T[1], T[0]), AC = Subtract(T[2], T[0]), AP = Subtract(P, T[0]);
    const double D1 = Dot(AB, AP), D2 = Dot(AC, AP);
    auto Along = [](const Point3& O, const Point3& S, double K) { return Point3{O[0] + S[0] * K, O[1] + S[1] * K, O[2] + S[2] * K}; };
    Point3 Q = T[0];
    if (!(D1 <= 0 && D2 <= 0))
    {
        const Point3 BP = Subtract(P, T[1]), CP = Subtract(P, T[2]);
        const double D3 = Dot(AB, BP), D4 = Dot(AC, BP), D5 = Dot(AB, CP), D6 = Dot(AC, CP);
        const double VC = D1 * D4 - D3 * D2, VB = D5 * D2 - D1 * D6, VA = D3 * D6 - D5 * D4;
        if (D3 >= 0 && D4 <= D3) Q = T[1];
        else if (VC <= 0 && D1 >= 0 && D3 <= 0) Q = Along(T[0], AB, D1 / (D1 - D3));
        else if (D6 >= 0 && D5 <= D6) Q = T[2];
        else if (VB <= 0 && D2 >= 0 && D6 <= 0) Q = Along(T[0], AC, D2 / (D2 - D6));
        else if (VA <= 0 && (D4 - D3) >= 0 && (D5 - D6) >= 0) Q = Along(T[1], Subtract(T[2], T[1]), (D4 - D3) / ((D4 - D3) + (D5 - D6)));
        else
        {
            const double Scale = 1 / (VA + VB + VC);
            Q = Along(Along(T[0], AB, VB * Scale), AC, VC * Scale);
        }
    }
    const Point3 Offset = Subtract(P, Q);
    return Dot(Offset, Offset);
}

// 📝 Uniform grid over triangle boxes; a query widens ring by ring until no unvisited cell can hold a nearer triangle.
class TriangleProximity
{
  public:
    explicit TriangleProximity(std::vector<Facet> Facets) : Facets_(std::move(Facets))
    {
        Minimum_ = {Unbounded, Unbounded, Unbounded};
        Point3 Maximum{-Unbounded, -Unbounded, -Unbounded};
        for (const Facet& T : Facets_)
            for (const Point3& P : T)
                for (int A = 0; A < 3; ++A)
                {
                    Minimum_[A] = std::min(Minimum_[A], P[A]);
                    Maximum[A]  = std::max(Maximum[A], P[A]);
                }
        const double Extent = std::max({Maximum[0] - Minimum_[0], Maximum[1] - Minimum_[1], Maximum[2] - Minimum_[2], 1e-9});
        const double Cells  = std::cbrt(static_cast<double>(Facets_.size())) * 1.5 + 1;
        Cell_ = Extent / Cells;
        for (int A = 0; A < 3; ++A) Count_[A] = std::max(1, static_cast<int>(std::ceil((Maximum[A] - Minimum_[A]) / Cell_)) + 1);
        Bins_.assign(static_cast<size_t>(Count_[0]) * Count_[1] * Count_[2], {});
        for (uint32_t Index = 0; Index < Facets_.size(); ++Index)
        {
            std::array<int, 3> Low{}, High{};
            for (int A = 0; A < 3; ++A)
            {
                const double Lo = std::min({Facets_[Index][0][A], Facets_[Index][1][A], Facets_[Index][2][A]});
                const double Hi = std::max({Facets_[Index][0][A], Facets_[Index][1][A], Facets_[Index][2][A]});
                Low[A]  = Clamp(static_cast<int>((Lo - Minimum_[A]) / Cell_), A);
                High[A] = Clamp(static_cast<int>((Hi - Minimum_[A]) / Cell_), A);
            }
            for (int X = Low[0]; X <= High[0]; ++X)
                for (int Y = Low[1]; Y <= High[1]; ++Y)
                    for (int Z = Low[2]; Z <= High[2]; ++Z) Bins_[Bin(X, Y, Z)].push_back(Index);
        }
    }
    [[nodiscard]] double Nearest(const Point3& P) const
    {
        std::array<int, 3> Home{};
        double Outside = 0.0;
        for (int A = 0; A < 3; ++A)
        {
            const double Coordinate = (P[A] - Minimum_[A]) / Cell_;
            Home[A] = Clamp(static_cast<int>(std::floor(Coordinate)), A);
            const double Below = Minimum_[A] - P[A], Above = P[A] - (Minimum_[A] + Count_[A] * Cell_);
            Outside = std::max({Outside, Below, Above});
        }
        double Best = Unbounded;
        const int Rings = std::max({Count_[0], Count_[1], Count_[2]});
        for (int Ring = 0; Ring <= Rings; ++Ring)
        {
            const double Reach = std::max(0.0, (Ring - 1) * Cell_ + Outside);
            if (Reach * Reach > Best) break;
            for (int X = Home[0] - Ring; X <= Home[0] + Ring; ++X)
                for (int Y = Home[1] - Ring; Y <= Home[1] + Ring; ++Y)
                    for (int Z = Home[2] - Ring; Z <= Home[2] + Ring; ++Z)
                    {
                        if (std::max({std::abs(X - Home[0]), std::abs(Y - Home[1]), std::abs(Z - Home[2])}) != Ring) continue;
                        if (X < 0 || Y < 0 || Z < 0 || X >= Count_[0] || Y >= Count_[1] || Z >= Count_[2]) continue;
                        for (uint32_t Index : Bins_[Bin(X, Y, Z)]) Best = std::min(Best, SquaredTriangleDistance(P, Facets_[Index]));
                    }
        }
        return std::sqrt(Best);
    }
  private:
    [[nodiscard]] int Clamp(int Value, int Axis) const noexcept { return std::clamp(Value, 0, Count_[Axis] - 1); }
    [[nodiscard]] size_t Bin(int X, int Y, int Z) const noexcept { return (static_cast<size_t>(X) * Count_[1] + Y) * Count_[2] + Z; }
    std::vector<Facet>                 Facets_;
    Point3                             Minimum_{};
    double                             Cell_ = 1.0;
    std::array<int, 3>                 Count_{1, 1, 1};
    std::vector<std::vector<uint32_t>> Bins_{};
};

std::vector<Facet> TierFacets(const TierGeometry& Tier)
{
    std::vector<Facet> Facets;
    for (uint32_t T = 0; T < Tier.TriangleCount(); ++T)
        Facets.push_back({Tier.ReadPosition(Tier.Triangles[T * 3]), Tier.ReadPosition(Tier.Triangles[T * 3 + 1]), Tier.ReadPosition(Tier.Triangles[T * 3 + 2])});
    return Facets;
}

// 📝 Two-sided deviation of a tier from the source, sampled at vertices, edge midpoints and centroids of both surfaces.
double MeasureDeviation(const std::vector<Facet>& Tier, const TriangleProximity& SourceProximity, const std::vector<Facet>& Source)
{
    const TriangleProximity TierProximity(Tier);
    auto Samples = [](const std::vector<Facet>& Facets) {
        std::vector<Point3> Points;
        for (const Facet& T : Facets)
            for (int C = 0; C < 3; ++C)
            {
                const Point3& A = T[C];
                const Point3& B = T[(C + 1) % 3];
                Points.push_back(A);
                Points.push_back({(A[0] + B[0]) / 2, (A[1] + B[1]) / 2, (A[2] + B[2]) / 2});
            }
        return Points;
    };
    double Worst = 0.0;
    for (const Point3& P : Samples(Tier)) Worst = std::max(Worst, SourceProximity.Nearest(P));
    for (const Point3& P : Samples(Source)) Worst = std::max(Worst, TierProximity.Nearest(P));
    return Worst;
}

uint32_t CountNeedles(const std::vector<Facet>& Facets)
{
    uint32_t Needles = 0;
    for (const Facet& T : Facets)
    {
        const double Longest    = std::max({Length(Subtract(T[1], T[0])), Length(Subtract(T[2], T[1])), Length(Subtract(T[0], T[2]))});
        const double DoubleArea = Length(Cross(Subtract(T[1], T[0]), Subtract(T[2], T[0])));
        Needles += (DoubleArea > 0 && Longest * Longest / DoubleArea > 20) ? 1u : 0u;
    }
    return Needles;
}

std::string Digest(const GeometryNesting& Nesting)
{
    SectionStream Stream;
    for (const NestedCluster& Node : Nesting.Clusters)
    {
        Stream.U32List(Node.Triangles);
        Stream.U32List(Node.Finer);
        Stream.F64(Node.Error);
        Stream.F64(Node.CoarserError);
        Stream.F64Triple(Node.Sphere.Center);
        Stream.F64(Node.Sphere.Radius);
    }
    for (const NestingTier& Tier : Nesting.Tiers)
    {
        for (double Value : Tier.Positions) Stream.F64(Value);
        Stream.U32List(Tier.Triangles);
    }
    return DigestSha256(Stream.Bytes);
}
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    const uint32_t ParallelThreads = ArgumentCount > 1 ? static_cast<uint32_t>(std::atoi(Arguments[1])) : 2u;
    struct Profile
    {
        const char*                           Name;
        std::function<void(NestingOptions&)> Apply;
    };
    const std::vector<Profile> Profiles = {
        {"Demo", [](NestingOptions&) {}},
        {"I3 measured deviation", [](NestingOptions& O) { O.MeasuredDeviation = true; }},
        {"I4 needle guard", [](NestingOptions& O) { O.NeedleGuard = true; }},
        {"I5 class borders", [](NestingOptions& O) { O.ClassBorderCollapse = true; }},
        {"Refined (I3+I4+I5)", [](NestingOptions& O) { O.MeasuredDeviation = O.NeedleGuard = O.ClassBorderCollapse = true; }}};
    const std::vector<std::string> Surfaces = {"SphereSubdivision4", "SphereSubdivision4Glass", "HeightfieldCliff64", "CreasedVessel48"};
    constexpr std::array<double, 8> CutDistances = {1.5, 3, 6, 12, 24, 48, 96, 192};
    int Failures = 0;
    for (const std::string& Name : Surfaces)
    {
        std::string Diagnostic;
        const std::optional<SurfaceSoup> Soup = ReadSurfaceSoup(LocateExhibitFolder() / "Surfaces" / (Name + ".soup"), &Diagnostic);
        if (!Soup)
        {
            std::printf("%s\n", Diagnostic.c_str());
            return 2;
        }
        const bool Glass = Name.ends_with("Glass");
        std::printf("\n## %s (%zu facets%s)\n", Name.c_str(), Soup->Facets.size(), Glass ? ", family 9 transmissive" : "");
        std::vector<Facet> Source;
        for (const SoupFacet& F : Soup->Facets) Source.push_back(F.Corners);
        Point3 Low{Unbounded, Unbounded, Unbounded}, High{-Unbounded, -Unbounded, -Unbounded};
        for (const Facet& T : Source)
            for (const Point3& P : T)
                for (int A = 0; A < 3; ++A)
                {
                    Low[A]  = std::min(Low[A], P[A]);
                    High[A] = std::max(High[A], P[A]);
                }
        const double Diagonal = Length(Subtract(High, Low));
        const TriangleProximity SourceProximity(Source);
        for (const Profile& Entry : Profiles)
        {
            NestingOptions Options;
            if (Glass) Options.TransmissiveFamilies = {9};
            Entry.Apply(Options);
            const auto Start = std::chrono::steady_clock::now();
            GeometryNesting Nesting = ConstructGeometryNesting(*Soup, Options);
            const double Serial = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
            NestingOptions Parallel = Options;
            Parallel.Threads = ParallelThreads;
            const auto ParallelStart = std::chrono::steady_clock::now();
            const GeometryNesting Threaded = ConstructGeometryNesting(*Soup, Parallel);
            const double Concurrent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ParallelStart).count();
            const bool Identical = Digest(Nesting) == Digest(Threaded);
            const NestingValidation Checks = ValidateGeometryNesting(*Soup, Nesting, Options);
            uint32_t Mixed = 0;
            for (const NestedCluster& Node : Nesting.Clusters) Mixed += Node.ClassMask == 3 ? 1u : 0u;
            const bool Valid = Checks.Passed && Mixed == 0 && Identical;
            Failures += Valid ? 0 : 1;
            std::printf("\n### %s — %zu tiers, coarsest %u triangles in %zu clusters; build %.0f ms serial, %.0f ms on %u threads (%s); "
                        "validation %s\n",
                        Entry.Name, Nesting.Tiers.size(), Nesting.Tiers.back().TriangleCount(), Nesting.Tiers.back().Clusters.size(), Serial, Concurrent,
                        ParallelThreads, Identical ? "bit-identical" : "DIFFERENT", Valid ? "passed" : (Checks.Failures.empty() ? "class mixing" : Checks.Failures[0].c_str()));
            std::printf("| Tier | Triangles | Needles | Claimed error %% | Measured deviation %% | Claimed / measured |\n");
            std::printf("|---:|---:|---:|---:|---:|---:|\n");
            for (const NestingTier& Tier : Nesting.Tiers)
            {
                const std::vector<Facet> Facets = TierFacets(Tier);
                double Claimed = 0.0;
                for (uint32_t Identity : Tier.Clusters) Claimed = std::max(Claimed, Nesting.Clusters[Identity].Error);
                const double Measured = Tier.Tier == 0 ? 0.0 : MeasureDeviation(Facets, SourceProximity, Source);
                std::printf("| %u | %u | %u | %.3f | %.3f | %s |\n", Tier.Tier, Tier.TriangleCount(), CountNeedles(Facets), 100 * Claimed / Diagonal,
                            100 * Measured / Diagonal, Measured > 0 ? (std::to_string(Claimed / Measured).substr(0, 5) + "x").c_str() : "-");
            }
            std::printf("Cut triangles at 1 px (eye distance m → triangles):");
            for (double Distance : CutDistances)
            {
                NestingCamera Camera;
                Camera.Eye             = {0, -Distance, Distance * 0.25};
                Camera.ProjectionScale = 467;
                std::printf(" %.1f→%u", Distance, SelectNestingCut(Nesting, Camera).CutTriangles);
            }
            std::printf("\n");
        }
    }
    std::printf("\nNestingRefinementMeasure: %d profile builds failed validation or thread determinism\n", Failures);
    return Failures == 0 ? 0 : 1;
}
