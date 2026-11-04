/*==========================================================================================================================================
                                                             TIERCOMPARISON.CPP
==========================================================================================================================================*/
// 🧩 Runs the branch's C++ tier build (FacetTierBuild + QuadricTierBuild through ScreenSpaceClusterRefinement) on the
//    VehicleHMI demo's four corpus surfaces and reports the metrics CorpusExport.mjs reports for the demo's builder.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Geometry/GeometryProcessing/ScreenSpaceClusterRefinement.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace Frontier;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     FACET SOUP FILES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Soup layout (little endian): u32 facet count, u32 carries-UV; per facet 9 f64 corners, 6 f64 UVs when carried,
//    u32 classification, u32 sharp mask (bit c = edge corner c → corner (c+1)%3).
bool DecodeSoup(const std::filesystem::path& Path, FacetField& Out)
{
    std::ifstream In(Path, std::ios::binary);
    if (!In) return false;
    uint32_t Count = 0, CarriesUv = 0;
    In.read(reinterpret_cast<char*>(&Count), 4);
    In.read(reinterpret_cast<char*>(&CarriesUv), 4);
    for (uint32_t Facet = 0; Facet < Count; ++Facet)
    {
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            double Point[3];
            In.read(reinterpret_cast<char*>(Point), sizeof(Point));
            Out.Corners.push_back({Point[0], Point[1], Point[2]});
        }
        if (CarriesUv)
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                double Uv[2];
                In.read(reinterpret_cast<char*>(Uv), sizeof(Uv));
                Out.CornerUVs.push_back({Uv[0], Uv[1]});
            }
        uint32_t Classification = 0, SharpMask = 0;
        In.read(reinterpret_cast<char*>(&Classification), 4);
        In.read(reinterpret_cast<char*>(&SharpMask), 4);
        Out.Classification.push_back(Classification);
        Out.SharpMask.push_back(static_cast<uint8_t>(SharpMask));
    }
    return static_cast<bool>(In);
}

void EncodeTierSoup(const std::filesystem::path& Path, const PolygonComplex& Complex)
{
    std::ofstream  Out(Path, std::ios::binary);
    const uint32_t Count     = static_cast<uint32_t>(Complex.Triangles.size() / 3);
    const uint32_t CarriesUv = Complex.CarriesUVs ? 1u : 0u;
    Out.write(reinterpret_cast<const char*>(&Count), 4);
    Out.write(reinterpret_cast<const char*>(&CarriesUv), 4);
    for (uint32_t Triangle = 0; Triangle < Count; ++Triangle)
    {
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            const FacetPoint3D& Point     = Complex.Positions[Complex.Triangles[Triangle * 3 + Corner]];
            const double        Packed[3] = {Point.X, Point.Y, Point.Z};
            Out.write(reinterpret_cast<const char*>(Packed), sizeof(Packed));
        }
        if (CarriesUv)
            for (int Corner = 0; Corner < 3; ++Corner)
                Out.write(reinterpret_cast<const char*>(Complex.CornerUVs[Triangle * 3 + Corner].data()), 16);
        const uint32_t Classification = Complex.Classification.empty() ? 0u : Complex.Classification[Triangle];
        const uint32_t SharpMask      = Complex.SharpMask.empty() ? 0u : Complex.SharpMask[Triangle];
        Out.write(reinterpret_cast<const char*>(&Classification), 4);
        Out.write(reinterpret_cast<const char*>(&SharpMask), 4);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      TIER METRICS
//------------------------------------------------------------------------------------------------------------------------

struct TierMeasurement
{
    uint32_t Triangles    = 0;     // [idx] - triangles in the tier
    uint32_t Clusters     = 0;     // [idx] - clusters partitioning the tier
    uint32_t SharpEdges   = 0;     // [idx] - undirected edges carrying a sharp bit
    uint32_t UvSeams      = 0;     // [idx] - manifold edges whose two corner UVs disagree
    uint32_t OffCrease    = 0;     // [idx] - sharp-edge endpoints that left every source crease plane
    uint32_t SeamCrossing = 0;     // [idx] - triangles whose U span exceeds 0.5 (smeared across the meridian seam)
    uint32_t Needles      = 0;     // [idx] - triangles with longest² / (2·area) > 20
    double   MaximumError = 0.0;   // [m] - the tier's own deviation bound
};

TierMeasurement MeasureTier(const ClusterTier& Tier, const std::vector<double>& CreasePlanes)
{
    TierMeasurement      Measurement;
    const PolygonComplex& Complex = Tier.Complex;
    Measurement.Triangles    = static_cast<uint32_t>(Complex.Triangles.size() / 3);
    Measurement.Clusters     = static_cast<uint32_t>(Tier.Clusters.size());
    Measurement.MaximumError = Tier.MaximumDeviation;

    struct EdgeUse
    {
        uint32_t Triangle = 0;   // [idx]
        uint32_t Corner   = 0;   // [idx] - edge runs Corner → (Corner+1)%3
    };
    std::unordered_map<uint64_t, std::vector<EdgeUse>> Edges;
    for (uint32_t Triangle = 0; Triangle < Measurement.Triangles; ++Triangle)
        for (uint32_t Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t A = Complex.Triangles[Triangle * 3 + Corner];
            const uint32_t B = Complex.Triangles[Triangle * 3 + (Corner + 1) % 3];
            Edges[A < B ? (uint64_t(A) << 32 | B) : (uint64_t(B) << 32 | A)].push_back({Triangle, Corner});
        }
    for (const auto& [Key, Uses] : Edges)
    {
        bool Sharp = false;
        for (const EdgeUse& Use : Uses)
            Sharp = Sharp || (!Complex.SharpMask.empty() && (Complex.SharpMask[Use.Triangle] & (1u << Use.Corner)));
        Measurement.SharpEdges += Sharp ? 1u : 0u;
        if (!Complex.CarriesUVs || Uses.size() != 2) continue;
        const uint32_t First = static_cast<uint32_t>(Key >> 32), Second = static_cast<uint32_t>(Key & 0xffffffffu);
        const auto UvAt = [&Complex](const EdgeUse& Use, uint32_t Vertex) {
            for (uint32_t Corner = 0; Corner < 3; ++Corner)
                if (Complex.Triangles[Use.Triangle * 3 + Corner] == Vertex)
                    return Complex.CornerUVs[Use.Triangle * 3 + Corner];
            return std::array<double, 2>{0.0, 0.0};
        };
        if (UvAt(Uses[0], First) != UvAt(Uses[1], First) || UvAt(Uses[0], Second) != UvAt(Uses[1], Second))
            ++Measurement.UvSeams;
    }
    for (uint32_t Triangle = 0; Triangle < Measurement.Triangles; ++Triangle)
    {
        FacetPoint3D Corners[3];
        for (int Corner = 0; Corner < 3; ++Corner)
            Corners[Corner] = Complex.Positions[Complex.Triangles[Triangle * 3 + Corner]];
        if (!CreasePlanes.empty() && !Complex.SharpMask.empty())
            for (uint32_t Edge = 0; Edge < 3; ++Edge)
            {
                if (!(Complex.SharpMask[Triangle] & (1u << Edge))) continue;
                for (const uint32_t Corner : {Edge, (Edge + 1) % 3})
                {
                    bool OnPlane = false;
                    for (const double Plane : CreasePlanes)
                        OnPlane = OnPlane || std::abs(Plane - Corners[Corner].Z) < 1e-9;
                    Measurement.OffCrease += OnPlane ? 0u : 1u;
                }
            }
        if (Complex.CarriesUVs)
        {
            double Lowest = 1e30, Highest = -1e30;
            for (int Corner = 0; Corner < 3; ++Corner)
            {
                Lowest  = std::min(Lowest, Complex.CornerUVs[Triangle * 3 + Corner][0]);
                Highest = std::max(Highest, Complex.CornerUVs[Triangle * 3 + Corner][0]);
            }
            Measurement.SeamCrossing += (Highest - Lowest > 0.5) ? 1u : 0u;
        }
        double Longest = 0.0;
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            const FacetPoint3D& From = Corners[Corner];
            const FacetPoint3D& To   = Corners[(Corner + 1) % 3];
            Longest = std::max(Longest, std::hypot(To.X - From.X, To.Y - From.Y, To.Z - From.Z));
        }
        const double AX = Corners[1].X - Corners[0].X, AY = Corners[1].Y - Corners[0].Y;
        const double AZ = Corners[1].Z - Corners[0].Z;
        const double BX = Corners[2].X - Corners[0].X, BY = Corners[2].Y - Corners[0].Y;
        const double BZ = Corners[2].Z - Corners[0].Z;
        const double DoubleArea = std::hypot(AY * BZ - AZ * BY, AZ * BX - AX * BZ, AX * BY - AY * BX);
        Measurement.Needles += (DoubleArea > 0.0 && Longest * Longest / DoubleArea > 20.0) ? 1u : 0u;
    }
    return Measurement;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 PER-CLUSTER (GPU-STYLE) CUT
//------------------------------------------------------------------------------------------------------------------------

// 📝 The demo's GPU cut decides every cluster alone: selected iff its error ≤ τ < its enclosing group's error.
//    Applied to these cluster entries with a view-independent τ [m], a crack-free set needs no leaf without a selected
//    self-or-enclosing cluster (Gaps) and no selected cluster enclosing another selected one (Overlaps).
struct CutDefects
{
    uint32_t Gaps     = 0;   // [idx] - leaf clusters nothing covers (holes)
    uint32_t Overlaps = 0;   // [idx] - selected clusters with a selected finer cluster below them (double cover)
};

CutDefects MeasurePerClusterCut(const ScreenSpaceClusterRefinement& Refinement, double τ)
{
    const std::vector<ClusterRefinementRecord>& Entries = Refinement.ClusterRecords();
    std::vector<uint8_t> Selected(Entries.size(), 0);
    for (size_t Index = 0; Index < Entries.size(); ++Index)
        Selected[Index] = Entries[Index].SelfDeviationMeters <= τ && τ < Entries[Index].CoarserDeviationMeters;
    std::vector<std::vector<uint32_t>> Enclosing(Entries.size());
    for (size_t Index = 0; Index < Entries.size(); ++Index)
        for (const uint32_t Finer : Entries[Index].EnclosedFinerGlobals)
            Enclosing[Finer].push_back(static_cast<uint32_t>(Index));
    CutDefects Defects;
    for (size_t Leaf = 0; Leaf < Entries.size(); ++Leaf)
    {
        if (Entries[Leaf].TierIndex != 0) continue;
        std::vector<uint32_t> Pending{static_cast<uint32_t>(Leaf)};
        std::set<uint32_t>    Visited;
        bool                  Covered = false;
        while (!Pending.empty() && !Covered)
        {
            const uint32_t Cluster = Pending.back();
            Pending.pop_back();
            if (!Visited.insert(Cluster).second) continue;
            Covered = Selected[Cluster] != 0;
            for (const uint32_t Upward : Enclosing[Cluster]) Pending.push_back(Upward);
        }
        Defects.Gaps += Covered ? 0u : 1u;
    }
    for (size_t Index = 0; Index < Entries.size(); ++Index)
    {
        if (!Selected[Index]) continue;
        const std::vector<uint32_t>& Finer = Entries[Index].EnclosedFinerGlobals;
        std::vector<uint32_t>        Pending(Finer.begin(), Finer.end());
        std::set<uint32_t>    Visited;
        bool                  Doubled = false;
        while (!Pending.empty() && !Doubled)
        {
            const uint32_t Cluster = Pending.back();
            Pending.pop_back();
            if (!Visited.insert(Cluster).second) continue;
            Doubled = Selected[Cluster] != 0;
            for (const uint32_t Finer : Entries[Cluster].EnclosedFinerGlobals) Pending.push_back(Finer);
        }
        Defects.Overlaps += Doubled ? 1u : 0u;
    }
    return Defects;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

// Usage: TierComparison <corpus directory> <crease|marked>. "marked" keeps only authored sharp edges (the demo's input
// semantics); "crease" is the C++ default (45° dihedral re-detection on every tier). Tier soups land in
// <corpus directory>/<mode>/; the report prints to stdout as JSON.
int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 3)
    {
        std::fprintf(stderr, "usage: TierComparison <corpus directory> <crease|marked>\n");
        return 2;
    }
    const std::filesystem::path Corpus      = Arguments[1];
    const std::string           Mode        = Arguments[2];
    const std::filesystem::path TierFolder  = Corpus / Mode;
    std::filesystem::create_directories(TierFolder);
    const char* const Surfaces[] = {"sphere", "glass-sphere", "heightfield-cliff", "uv-creased-vessel"};

    std::printf("{\n");
    for (int Surface = 0; Surface < 4; ++Surface)
    {
        FacetField Field;
        if (!DecodeSoup(Corpus / (std::string(Surfaces[Surface]) + ".soup"), Field)) return 3;
        TierBuildSpecification Specification;
        Specification.UseCreaseAngle = Mode == "crease";
        ScreenSpaceClusterRefinement Refinement;
        if (Surface == 1)
        {
            ClusterMaterialProfile Glass;   // 📝 the demo declares classification 9 transmissive for this surface
            Glass.ClassificationId  = 9;
            Glass.TransmissiveGlass = true;
            Refinement.RegisterMaterialProfile(Glass);
        }
        std::vector<double> Durations;   // [ms]
        for (int Repeat = 0; Repeat < 3; ++Repeat)
        {
            const auto Start = std::chrono::steady_clock::now();
            if (!Refinement.BuildFromFacetField(Field, Specification)) return 4;
            const auto Elapsed = std::chrono::steady_clock::now() - Start;
            Durations.push_back(std::chrono::duration<double, std::milli>(Elapsed).count());
        }
        std::sort(Durations.begin(), Durations.end());
        const ClusterTierSequence&     Sequence   = Refinement.TierSequence();
        const std::vector<std::string> Violations = ValidateTierSequence(Sequence, Specification);

        // 📝 Crease planes: z of every authored sharp-edge endpoint on the vessel's leaf tier (z ∈ {−1, 0, +1}).
        std::vector<double> CreasePlanes;
        if (Surface == 3)
        {
            const PolygonComplex& Leaf = Sequence.Tiers.front().Complex;
            std::set<double>      Planes;
            for (size_t Triangle = 0; Triangle < Leaf.Triangles.size() / 3; ++Triangle)
                for (uint32_t Edge = 0; Edge < 3; ++Edge)
                    if (Leaf.SharpMask[Triangle] & (1u << Edge))
                        for (const uint32_t Corner : {Edge, (Edge + 1) % 3})
                            Planes.insert(Leaf.Positions[Leaf.Triangles[Triangle * 3 + Corner]].Z);
            CreasePlanes.assign(Planes.begin(), Planes.end());
        }

        std::printf(" \"%s\": {\"BuildMsMedian\": %.1f, \"Violations\": %zu, \"Levels\": [\n", Surfaces[Surface],
                    Durations[1], Violations.size());
        double LargestError = 0.0;   // [m]
        for (size_t Tier = 0; Tier < Sequence.Tiers.size(); ++Tier)
        {
            const TierMeasurement Measurement = MeasureTier(Sequence.Tiers[Tier], CreasePlanes);
            const std::string TierName = "cpp-" + std::string(Surfaces[Surface]) + "-tier" + std::to_string(Tier);
            EncodeTierSoup(TierFolder / (TierName + ".soup"), Sequence.Tiers[Tier].Complex);
            LargestError = std::max(LargestError, Measurement.MaximumError);
            std::printf("  {\"Level\": %zu, \"Triangles\": %u, \"Clusters\": %u, \"MaxError\": %.5f, "
                        "\"SharpEdges\": %u, \"UvSeams\": %u, \"OffCrease\": %u, \"SeamCrossing\": %u, "
                        "\"Needles\": %u}%s\n",
                        Tier, Measurement.Triangles, Measurement.Clusters, Measurement.MaximumError,
                        Measurement.SharpEdges, Measurement.UvSeams, Measurement.OffCrease, Measurement.SeamCrossing,
                        Measurement.Needles, Tier + 1 < Sequence.Tiers.size() ? "," : "");
        }
        std::printf(" ], \"PerClusterCut\": [");
        constexpr int Thresholds = 12;   // [-] - τ from 10⁻³ to 10^0.2 of the largest error, logarithmic
        for (int Step = 0; Step < Thresholds; ++Step)
        {
            const double     τ       = LargestError * std::pow(10.0, -3.0 + 3.2 * Step / (Thresholds - 1));
            const CutDefects Defects = MeasurePerClusterCut(Refinement, τ);
            std::printf("{\"Tau\": %.5f, \"Gaps\": %u, \"Overlaps\": %u}%s", τ, Defects.Gaps, Defects.Overlaps,
                        Step + 1 < Thresholds ? ", " : "");
        }
        std::printf("]}%s\n", Surface < 3 ? "," : "");
    }
    std::printf("}\n");
    return 0;
}
