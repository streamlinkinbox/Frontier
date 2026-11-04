/*==========================================================================================================================================
                                                   BAKEFIELDVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Pixel-level proof that the flattened bake field (Engine/ContentInterchange/BakeField) reproduces the
//    legacy CPU bake (RunMeshBake) map-for-map: same demo meshes, same 27 channels, same settings, compared
//    byte-by-byte with per-map difference statistics, timings and retained PNG triplets (legacy / field /
//    amplified difference). This is the correctness anchor for BakeFieldCompute: the GPU shader mirrors the
//    CPU twin evaluated here, over the identical flat buffers.
//    ⚠️ This validation is CPU-only by design (no Vulkan device is opened); GPU execution is exercised by
//    BakeRasterizationValidation where a driver is available and by the TextureStudio application itself.

#include "Engine/ContentInterchange/BakeField.h"
#include <stb_image_write.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void Require(bool Condition, const char *Description)
{
    if (!Condition) throw std::runtime_error(Description);
}

struct MapDifference
{
    std::string Key;
    uint64_t    Texels = 0;
    uint64_t    Exact = 0;
    uint64_t    WithinOne = 0;
    uint32_t    Maximum = 0; // [LSB] - Largest absolute channel difference
    double      MeanAbsolute = 0;
};

MapDifference CompareMaps(const Frontier::BakeMapImage &Legacy, const Frontier::BakeMapImage &Field)
{
    Require(Legacy.Key == Field.Key, "Map key order diverged between the two bake routes.");
    Require(Legacy.Rgba.size() == Field.Rgba.size(), "Map sizes diverged between the two bake routes.");
    MapDifference Difference;
    Difference.Key = Legacy.Key;
    Difference.Texels = Legacy.Rgba.size() / 4;
    uint64_t Sum = 0;
    for (size_t Texel = 0; Texel < Difference.Texels; ++Texel)
    {
        uint32_t TexelMaximum = 0;
        for (size_t Channel = 0; Channel < 4; ++Channel)
        {
            const int32_t Delta = int32_t(Legacy.Rgba[Texel * 4 + Channel]) - int32_t(Field.Rgba[Texel * 4 + Channel]);
            const uint32_t Absolute = uint32_t(Delta < 0 ? -Delta : Delta);
            TexelMaximum = std::max(TexelMaximum, Absolute);
            Sum += Absolute;
        }
        Difference.Maximum = std::max(Difference.Maximum, TexelMaximum);
        if (TexelMaximum == 0) ++Difference.Exact;
        if (TexelMaximum <= 1) ++Difference.WithinOne;
    }
    Difference.MeanAbsolute = Difference.Texels ? double(Sum) / double(Difference.Texels * 4) : 0.0;
    return Difference;
}

void WriteDifferencePng(const std::filesystem::path &Path, const Frontier::BakeMapImage &Legacy,
    const Frontier::BakeMapImage &Field, uint32_t Resolution)
{
    std::vector<uint8_t> Amplified(Legacy.Rgba.size());
    for (size_t Index = 0; Index < Legacy.Rgba.size(); ++Index)
    {
        const int32_t Delta = int32_t(Legacy.Rgba[Index]) - int32_t(Field.Rgba[Index]);
        Amplified[Index] = uint8_t(std::min(255, (Delta < 0 ? -Delta : Delta) * 16));
    }
    for (size_t Texel = 0; Texel + 3 < Amplified.size(); Texel += 4) Amplified[Texel + 3] = 255;
    Require(stbi_write_png(Path.string().c_str(), int(Resolution), int(Resolution), 4, Amplified.data(), int(Resolution) * 4) != 0,
        "Difference PNG write failed.");
}

struct BakeComparison
{
    std::string              Label;
    std::vector<MapDifference> Maps;
    double                   LegacyMilliseconds = 0;
    double                   FieldMilliseconds = 0;
    uint64_t                 LegacyHits = 0, FieldHits = 0, LegacyMisses = 0, FieldMisses = 0;
};

BakeComparison CompareBakes(const std::string &Label, const std::vector<const Frontier::BakePart *> &Targets,
    const Frontier::BakeMeshSet &High, const Frontier::BakeSettings &Settings, const std::filesystem::path &Output,
    bool WritePngs)
{
    using namespace Frontier;
    const auto LegacyStart = std::chrono::steady_clock::now();
    const auto Legacy = RunMeshBake(Targets, High, Settings);
    const auto LegacyEnd = std::chrono::steady_clock::now();
    const auto Field = RunBakeFieldEvaluation(Targets, High, Settings);
    const auto FieldEnd = std::chrono::steady_clock::now();
    Require(Legacy.size() == Field.size() && !Legacy.empty(), "Bake result counts diverged between the two routes.");

    BakeComparison Comparison;
    Comparison.Label = Label;
    Comparison.LegacyMilliseconds = std::chrono::duration<double, std::milli>(LegacyEnd - LegacyStart).count();
    Comparison.FieldMilliseconds = std::chrono::duration<double, std::milli>(FieldEnd - LegacyEnd).count();
    for (size_t Part = 0; Part < Legacy.size(); ++Part)
    {
        Require(Legacy[Part].Name == Field[Part].Name && Legacy[Part].Resolution == Field[Part].Resolution,
            "Bake result identity diverged between the two routes.");
        Require(Legacy[Part].Maps.size() == Field[Part].Maps.size(), "Bake map counts diverged between the two routes.");
        Comparison.LegacyHits += Legacy[Part].Stats.Hits;
        Comparison.FieldHits += Field[Part].Stats.Hits;
        Comparison.LegacyMisses += Legacy[Part].Stats.Misses;
        Comparison.FieldMisses += Field[Part].Stats.Misses;
        Require(Legacy[Part].Stats.Charts == Field[Part].Stats.Charts, "Chart counts diverged between the two routes.");
        for (size_t Map = 0; Map < Legacy[Part].Maps.size(); ++Map)
        {
            Comparison.Maps.push_back(CompareMaps(Legacy[Part].Maps[Map], Field[Part].Maps[Map]));
            const std::string &Key = Legacy[Part].Maps[Map].Key;
            if (WritePngs && (Key == "normal" || Key == "occlusion" || Key == "curvature"))
            {
                const uint32_t Resolution = Legacy[Part].Resolution;
                Require(stbi_write_png((Output / (Label + "-" + Key + "-legacy.png")).string().c_str(), int(Resolution),
                    int(Resolution), 4, Legacy[Part].Maps[Map].Rgba.data(), int(Resolution) * 4) != 0, "Legacy PNG write failed.");
                Require(stbi_write_png((Output / (Label + "-" + Key + "-field.png")).string().c_str(), int(Resolution),
                    int(Resolution), 4, Field[Part].Maps[Map].Rgba.data(), int(Resolution) * 4) != 0, "Field PNG write failed.");
                WriteDifferencePng(Output / (Label + "-" + Key + "-difference-x16.png"), Legacy[Part].Maps[Map],
                    Field[Part].Maps[Map], Resolution);
            }
        }
    }
    return Comparison;
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        Require(ArgumentCount == 2, "Expected an evidence output directory.");
        const std::filesystem::path Output(Arguments[1]);
        std::filesystem::create_directories(Output);

        std::vector<BakeComparison> Comparisons;

        // 1) Full 27-channel bakes on two built-in study meshes at 256².
        for (const char *Shape : {"Teapot", "Cube"})
        {
            const DemoBakeAssets Assets = BuildDemoBakeAssets(Shape);
            BakeSettings Settings;
            Settings.Resolution = 256;
            Settings.Channels = ImplementedMeshMapKeys();
            std::vector<const BakePart *> Targets;
            for (const auto &Part : Assets.Low.Parts) Targets.push_back(&Part);
            Comparisons.push_back(CompareBakes(Shape, Targets, Assets.High, Settings, Output, std::string(Shape) == "Teapot"));
            std::cout << "Compared all 27 maps for " << Shape << " at 256x256.\n";
        }

        // 2) Encoder branch coverage: DirectX green flip, dust axis parse, non-default sample counts, and a
        //    two-part scene whose second part is offset so the id map must resolve both high part indices.
        {
            BakeMeshSet Low, High;
            const DemoBakeAssets Cube = BuildDemoBakeAssets("Cube");
            BakePart Near = Cube.Low.Parts.front();
            BakePart Far = Near;
            Far.Name = "CubeFar";
            for (auto &Vertex : Far.Vertices) Vertex.Position[0] += 4.f;
            Low.Parts = {Near, Far};
            BakePart HighNear = Cube.High.Parts.front();
            BakePart HighFar = HighNear;
            HighFar.Name = "CubeFar";
            for (auto &Vertex : HighFar.Vertices) Vertex.Position[0] += 4.f;
            High.Parts = {HighNear, HighFar};
            BakeSettings Settings;
            Settings.Resolution = 128;
            Settings.NormalPlusY = false;
            Settings.Samples = 8;
            Settings.BevelSamples = 5;
            Settings.DustAxis = "-Z";
            Settings.Channels = ImplementedMeshMapKeys();
            std::vector<const BakePart *> Targets;
            for (const auto &Part : Low.Parts) Targets.push_back(&Part);
            Comparisons.push_back(CompareBakes("TwoCubes", Targets, High, Settings, Output, false));
            std::cout << "Compared the two-part DirectX/dust-axis variant at 128x128.\n";
        }

        // 3) Query-free channel subset: coverage/uv/wireframe family only, so the need mask stays zero and
        //    the field carries defaults end-to-end.
        {
            const DemoBakeAssets Assets = BuildDemoBakeAssets("Sphere");
            BakeSettings Settings;
            Settings.Resolution = 128;
            Settings.Channels = {"coverage", "uv", "uv-island", "wireframe", "base-color"};
            std::vector<const BakePart *> Targets;
            for (const auto &Part : Assets.Low.Parts) Targets.push_back(&Part);
            Comparisons.push_back(CompareBakes("SphereSurfaceMaps", Targets, Assets.High, Settings, Output, false));
            std::cout << "Compared the query-free channel subset on Sphere at 128x128.\n";
        }

        // 📝 Verdict thresholds: the twin shares libm and operation order with the legacy bake, so agreement
        // should be exact or within float-associativity noise (≤ 2 LSB, ≥ 99.99% of texels within 1 LSB).
        // Anything wider means the flat layout or the ported arithmetic drifted, which is what this guards.
        bool Passed = true;
        std::ofstream Report(Output / "BakeFieldChecks.json");
        Require(Report.good(), "Could not open the JSON report for writing.");
        Report << "{\n  \"validation\": \"BakeFieldValidation\",\n  \"route_legacy\": \"RunMeshBake (Engine/ContentInterchange/BakeMesh.cpp)\",\n"
               << "  \"route_field\": \"RunBakeFieldEvaluation (Engine/ContentInterchange/BakeField.cpp)\",\n"
               << "  \"thresholds\": {\"maximum_channel_difference_lsb\": 2, \"minimum_within_one_lsb_percent\": 99.99},\n  \"comparisons\": [\n";
        for (size_t Index = 0; Index < Comparisons.size(); ++Index)
        {
            const auto &Comparison = Comparisons[Index];
            Require(Comparison.LegacyHits == Comparison.FieldHits && Comparison.LegacyMisses == Comparison.FieldMisses,
                "Hit/miss statistics diverged between the two routes.");
            Report << "    {\"label\": \"" << Comparison.Label << "\", \"legacy_ms\": " << Comparison.LegacyMilliseconds
                   << ", \"field_ms\": " << Comparison.FieldMilliseconds << ", \"hits\": " << Comparison.LegacyHits
                   << ", \"misses\": " << Comparison.LegacyMisses << ", \"maps\": [\n";
            for (size_t Map = 0; Map < Comparison.Maps.size(); ++Map)
            {
                const auto &Difference = Comparison.Maps[Map];
                const double WithinOnePercent = Difference.Texels ? 100.0 * double(Difference.WithinOne) / double(Difference.Texels) : 100.0;
                const double ExactPercent = Difference.Texels ? 100.0 * double(Difference.Exact) / double(Difference.Texels) : 100.0;
                if (Difference.Maximum > 2 || WithinOnePercent < 99.99) Passed = false;
                Report << "      {\"key\": \"" << Difference.Key << "\", \"texels\": " << Difference.Texels
                       << ", \"exact_percent\": " << ExactPercent << ", \"within_one_lsb_percent\": " << WithinOnePercent
                       << ", \"maximum_difference_lsb\": " << Difference.Maximum
                       << ", \"mean_absolute_difference\": " << Difference.MeanAbsolute << "}"
                       << (Map + 1 < Comparison.Maps.size() ? "," : "") << "\n";
                std::cout << Comparison.Label << " / " << Difference.Key << ": exact " << ExactPercent << "%, within 1 LSB "
                          << WithinOnePercent << "%, maximum " << Difference.Maximum << " LSB.\n";
            }
            Report << "    ]}" << (Index + 1 < Comparisons.size() ? "," : "") << "\n";
            std::cout << Comparison.Label << " timing: legacy " << Comparison.LegacyMilliseconds << " ms, field "
                      << Comparison.FieldMilliseconds << " ms (single-threaded CPU both; GPU timing is not claimed here).\n";
        }
        Report << "  ],\n  \"passed\": " << (Passed ? "true" : "false") << "\n}\n";
        if (!Passed) throw std::runtime_error("Field bake diverged from the legacy bake beyond the agreed thresholds (see BakeFieldChecks.json).");
        std::cout << "PASS: BakeField reproduces RunMeshBake within thresholds on every compared map.\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "BakeFieldValidation failed: " << Failure.what() << '\n';
        return 1;
    }
}
