/*==========================================================================================================================================
                                                      NESTINGPARITYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Bit-exact parity of the C++ virtualized geometry build against the demo: every corpus surface is rebuilt from the demo's own
//    soup and each serialized section (summary, clusters, tiers, CPU cuts) must hash to the digest the demo builder produced.

// 📝 Usage: NestingParityValidation [--dump <folder>] [--surface <name>]. The digests come from Results/NestingParity.txt
//    (Code/NestingParityExport.mjs); --dump writes each C++ section as <surface>.<section>.bin for a byte diff against the
//    export's own --dump output.
#include "SurfaceSoupArchive.h"
#include <Rendering/VirtualizedGeometry/NestingSelection.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
using namespace Frontier::VirtualizedGeometry;

namespace
{
constexpr std::array<double, 8> CutDistances = {1.5, 3, 6, 12, 24, 48, 96, 192};   // [m] NestingParityExport.mjs
constexpr double                CutProjectionScale = 467;                          // [px]

std::vector<uint8_t> SerializeSummary(const GeometryNesting& Nesting)
{
    SectionStream Stream;
    Stream.U32(Nesting.SourceTriangles);
    Stream.U32(Nesting.WeldedVertices);
    Stream.U32(Nesting.HasUvs ? 1 : 0);
    Stream.U32(static_cast<uint32_t>(Nesting.Tiers.size()));
    Stream.U32(static_cast<uint32_t>(Nesting.Clusters.size()));
    return Stream.Bytes;
}

std::vector<uint8_t> SerializeClusters(const GeometryNesting& Nesting)
{
    SectionStream Stream;
    for (const NestedCluster& Node : Nesting.Clusters)
    {
        Stream.U32(Node.Identity);
        Stream.U32(Node.Tier);
        Stream.U32List(Node.Finer);
        Stream.U32List(Node.Coarser);
        Stream.I32(Node.GroupIdentity);
        Stream.U32(Node.FirstTriangle);
        Stream.U32List(Node.Triangles);
        Stream.F64Triple(Node.Minimum);
        Stream.F64Triple(Node.Maximum);
        Stream.F64Triple(Node.Sphere.Center);
        Stream.F64(Node.Sphere.Radius);
        Stream.F64Triple(Node.Cone.Axis);
        Stream.F64(Node.Cone.Cutoff);
        Stream.F64Triple(Node.TightMinimum);
        Stream.F64Triple(Node.TightMaximum);
        Stream.F64Triple(Node.TightSphere.Center);
        Stream.F64(Node.TightSphere.Radius);
        Stream.F64(Node.Error);
        Stream.F64(Node.CoarserError);
        Stream.F64Triple(Node.CoarserSphere.Center);
        Stream.F64(Node.CoarserSphere.Radius);
        Stream.U32(Node.ClassMask);
        Stream.U32(Node.SharpEdgesInCluster);
    }
    return Stream.Bytes;
}

std::vector<uint8_t> SerializeTier(const NestingTier& Tier)
{
    SectionStream Stream;
    Stream.U32(Tier.Tier);
    Stream.U32List(Tier.Clusters);
    Stream.U32(Tier.VertexCount());
    for (double Value : Tier.Positions) Stream.F64(Value);
    Stream.U32(Tier.TriangleCount());
    for (uint32_t Value : Tier.Triangles) Stream.U32(Value);
    for (uint32_t Value : Tier.Families) Stream.U32(Value);
    for (double Value : Tier.CornerUvs) Stream.F64(Value);
    for (uint8_t Value : Tier.SharpMask) Stream.U8(Value);
    Stream.U32(Tier.SharpEdgeCount);
    Stream.U32(Tier.UvSeamEdgeCount);
    return Stream.Bytes;
}

std::vector<uint8_t> SerializeCuts(const GeometryNesting& Nesting)
{
    SectionStream Stream;
    for (double Distance : CutDistances)
    {
        NestingCamera Camera;
        Camera.Eye             = {0, -Distance, Distance * 0.25};
        Camera.Near            = 0.1;
        Camera.ProjectionScale = CutProjectionScale;
        const NestingCut Cut = SelectNestingCut(Nesting, Camera);
        Stream.F64(Distance);
        Stream.U32List(Cut.CutClusterIdentities);
        Stream.U32(Cut.CutTriangles);
        for (uint32_t Identity : Cut.CutClusterIdentities)
        {
            const ClusterCutMetric& Metric = Cut.Metrics.at(Identity);
            Stream.F64(Metric.OwnErrorPx);
            Stream.F64(Metric.CoarserErrorPx);
        }
    }
    return Stream.Bytes;
}

struct ExpectedSection
{
    size_t      ByteCount = 0;
    std::string Digest{};
};
} // namespace

int main(int ArgumentCount, char** Arguments)
{
    std::string DumpFolder, OnlySurface;
    for (int Index = 1; Index + 1 < ArgumentCount; ++Index)
    {
        const std::string Flag = Arguments[Index];
        if (Flag == "--dump") DumpFolder = Arguments[++Index];
        else if (Flag == "--surface") OnlySurface = Arguments[++Index];
    }
    const std::filesystem::path Exhibit = LocateExhibitFolder();
    std::ifstream Digests(Exhibit / "Results" / "NestingParity.txt");
    if (!Digests)
    {
        std::fprintf(stderr, "NestingParityValidation: cannot read %s\n", (Exhibit / "Results" / "NestingParity.txt").string().c_str());
        return 2;
    }
    std::vector<std::string> SurfaceOrder;
    std::map<std::string, std::map<std::string, ExpectedSection>> Expected;
    for (std::string Line; std::getline(Digests, Line);)
    {
        if (Line.empty() || Line[0] == '#') continue;
        std::istringstream Fields(Line);
        std::string Surface, Section;
        ExpectedSection Entry;
        Fields >> Surface >> Section >> Entry.ByteCount >> Entry.Digest;
        if (!Expected.contains(Surface)) SurfaceOrder.push_back(Surface);
        Expected[Surface][Section] = Entry;
    }
    if (!DumpFolder.empty()) std::filesystem::create_directories(DumpFolder);
    uint32_t Matched = 0, Mismatched = 0;
    for (const std::string& Surface : SurfaceOrder)
    {
        if (!OnlySurface.empty() && Surface != OnlySurface) continue;
        std::string Diagnostic;
        const std::optional<SurfaceSoup> Soup = ReadSurfaceSoup(Exhibit / "Surfaces" / (Surface + ".soup"), &Diagnostic);
        if (!Soup)
        {
            std::printf("FAIL %s: %s\n", Surface.c_str(), Diagnostic.c_str());
            ++Mismatched;
            continue;
        }
        NestingOptions Options;
        if (Surface.ends_with("Glass")) Options.TransmissiveFamilies = {9};
        const auto Started = std::chrono::steady_clock::now();
        const GeometryNesting Nesting = ConstructGeometryNesting(*Soup, Options);
        const double Elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
        std::vector<std::pair<std::string, std::vector<uint8_t>>> Sections = {{"Summary", SerializeSummary(Nesting)},
                                                                              {"Clusters", SerializeClusters(Nesting)}};
        for (const NestingTier& Tier : Nesting.Tiers) Sections.emplace_back("Tier" + std::to_string(Tier.Tier), SerializeTier(Tier));
        Sections.emplace_back("Cuts", SerializeCuts(Nesting));
        const std::map<std::string, ExpectedSection>& Reference = Expected[Surface];
        uint32_t SurfaceFailures = 0;
        for (const auto& [Section, Bytes] : Sections)
        {
            if (!DumpFolder.empty())
            {
                std::ofstream Dump(std::filesystem::path(DumpFolder) / (Surface + "." + Section + ".bin"), std::ios::binary);
                Dump.write(reinterpret_cast<const char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
            }
            const auto Hit = Reference.find(Section);
            const std::string Digest = DigestSha256(Bytes);
            if (Hit == Reference.end())
            {
                std::printf("FAIL %s %s: section absent from the demo build (%zu bytes)\n", Surface.c_str(), Section.c_str(), Bytes.size());
                ++SurfaceFailures;
            }
            else if (Hit->second.Digest != Digest || Hit->second.ByteCount != Bytes.size())
            {
                std::printf("FAIL %s %s: %zu bytes %s, demo %zu bytes %s\n", Surface.c_str(), Section.c_str(), Bytes.size(), Digest.c_str(),
                            Hit->second.ByteCount, Hit->second.Digest.c_str());
                ++SurfaceFailures;
            }
        }
        for (const auto& [Section, Entry] : Reference)
        {
            bool Present = false;
            for (const auto& Produced : Sections) Present = Present || Produced.first == Section;
            if (!Present)
            {
                std::printf("FAIL %s %s: the C++ build produced no such section\n", Surface.c_str(), Section.c_str());
                ++SurfaceFailures;
            }
        }
        std::printf("%s %s: %zu facets, %zu tiers, %zu clusters, %zu sections, %.0f ms\n", SurfaceFailures ? "FAIL" : "PASS", Surface.c_str(),
                    Soup->Facets.size(), Nesting.Tiers.size(), Nesting.Clusters.size(), Sections.size(), Elapsed);
        (SurfaceFailures ? Mismatched : Matched) += 1;
    }
    std::printf("NestingParityValidation: %u surfaces bit-exact, %u differ\n", Matched, Mismatched);
    return Mismatched == 0 && Matched > 0 ? 0 : 1;
}
