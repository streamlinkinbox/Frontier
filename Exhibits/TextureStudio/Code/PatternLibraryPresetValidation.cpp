/*============================================================================================================================================
                                                      PATTERNLIBRARYPRESETVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Prove every Pattern library preset builds bounded, GPU-valid vector artwork without authoring a pixel.

#include "Engine/ContentInterchange/PatternLibraryPresets.h"
#include "Engine/Render/VectorCoverage.h"
#include <toml++/toml.hpp>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

using namespace Frontier;

namespace
{
uint32_t Assertions = 0;

void Confirm(bool Condition, const char *Caption)
{
    if (!Condition) throw std::runtime_error(Caption);
    ++Assertions;
}
}

int main()
{
    try
    {
        Confirm(QueryPatternLibraryPresetCount() == 8, "Pattern library preset count changed.");
        for (size_t Index = 0; Index < QueryPatternLibraryPresetCount(); ++Index)
        {
            const auto *Name = QueryPatternLibraryPresetName(Index);
            Confirm(Name && Name[0], "Pattern preset is unnamed.");
            const auto Document = ConstructPatternLibraryPreset(Index);
            Confirm(!Document.Layers.empty(), "Pattern preset has no motifs.");
            Confirm(Document.Name == Name, "Pattern preset document lost its gallery name.");
            Confirm(Document.Layers.size() == Document.Instructions.size(), "Preset layers and sources diverged.");
            for (const auto &Source : Document.Instructions)
            {
                for (float Coordinate : Source.Bounds)
                    Confirm(std::isfinite(Coordinate), "Preset motif is non-finite.");
                Confirm(Source.Bounds[0] >= 0 && Source.Bounds[0] <= 512 && Source.Bounds[1] >= 0 && Source.Bounds[1] <= 512,
                    "Preset motif centre escaped the 512 tile.");
                Confirm(Source.Bounds[2] >= 1 && Source.Bounds[3] >= 1 && Source.Bounds[2] <= 512 && Source.Bounds[3] <= 512,
                    "Preset motif extent is degenerate or larger than the tile.");
            }
            const auto Program = ConstructVectorAuthoringCoverage(Document);
            ValidateVectorCoverage(Program);
            Confirm(DecodeVectorAuthoring(EncodeVectorAuthoring(Document)) == Document, "Preset TOML round-trip changed artwork.");
        }
        // Diamond Weave: the user's own procedural worked example (array/inset, mirror, align-to-
        // edge). Confirm the composable modifier stack actually produced the structure it claims to
        // -- 8 diamonds per edge (4 ring sizes mirrored top/bottom), two edges with distinct accents,
        // and every ring size appearing exactly twice (top + mirrored bottom) per edge.
        {
            const auto Weave = ConstructPatternLibraryPreset(6);
            Confirm(Weave.Name == std::string("Diamond Weave"), "Diamond Weave preset lost its name.");
            size_t DiamondCount = 0;
            std::set<float> LeftWidths, RightWidths;
            std::set<std::string> AccentColours;
            for (const auto &Source : Weave.Instructions)
            {
                if (Source.Topology[0] != 2) continue; // Background field is Topology 0.
                ++DiamondCount;
                char Key[64];
                std::snprintf(Key, sizeof(Key), "%.4f,%.4f,%.4f", Source.Colour[0], Source.Colour[1], Source.Colour[2]);
                const bool IsGround = std::abs(Source.Colour[0] - .1490f) < .01f && std::abs(Source.Colour[1] - .2353f) < .01f;
                if (!IsGround) AccentColours.insert(Key);
                (Source.Bounds[0] < 256 ? LeftWidths : RightWidths).insert(Source.Bounds[2]);
            }
            Confirm(DiamondCount == 16, "Diamond Weave ring/mirror/edge count changed.");
            Confirm(LeftWidths.size() == 4 && RightWidths.size() == 4, "Diamond Weave inset ring sizes changed.");
            Confirm(AccentColours.size() == 2, "Diamond Weave per-edge accent recolouring changed.");
        }
        // Grid array / Scatter placement / Fibonacci square spiral: the three C++ tools added for
        // full parity with the HTML pattern studio toolset. Confirm each independently of any preset.
        {
            VectorCoverageInstruction Base;
            Base.Bounds = {64, 64, 40, 40};
            const auto Grid = ConstructGridMotif(Base, 3, 2, 100, 150,
                [](uint32_t Row) { return (Row % 2) ? 50.f : 0.f; },
                [](uint32_t Row, uint32_t) { return Row ? 20.f : 40.f; });
            Confirm(Grid.size() == 6, "ConstructGridMotif cell count changed.");
            Confirm(Grid[0].Bounds[0] == 64 && Grid[0].Bounds[1] == 64, "ConstructGridMotif row 0 origin changed.");
            Confirm(Grid[3].Bounds[0] == 64 + 50 && Grid[3].Bounds[1] == 64 + 150, "ConstructGridMotif row offset changed.");
            Confirm(Grid[0].Bounds[2] == 40 && Grid[3].Bounds[2] == 20, "ConstructGridMotif size-at-row gradient changed.");

            const auto PlacementA = QueryScatterPlacement(0, 197, 113, 37, 64);
            const auto PlacementB = QueryScatterPlacement(3, 197, 113, 37, 64);
            Confirm(PlacementA[0] == 37 && PlacementA[1] == 64, "QueryScatterPlacement index 0 changed.");
            Confirm(PlacementB[0] == float((3 * 197 + 37) % 512) && PlacementB[1] == float((3 * 113 + 64) % 512),
                "QueryScatterPlacement modulo formula changed.");

            const auto Spiral = ConstructFibonacciSquareSpiral(6);
            Confirm(Spiral.size() == 6, "ConstructFibonacciSquareSpiral count changed.");
            const std::array<std::array<float, 3>, 6> Expected{{{0, 0, 1}, {1, 0, 1}, {0, -2, 2}, {-3, -2, 3}, {-3, 1, 5}, {2, -2, 8}}};
            for (size_t Index = 0; Index < 6; ++Index)
                Confirm(Spiral[Index].X == Expected[Index][0] && Spiral[Index].Y == Expected[Index][1] && Spiral[Index].Side == Expected[Index][2],
                    "ConstructFibonacciSquareSpiral square changed.");
        }
        // Pebble Path: confirm the brick-offset grid (16 pebbles, alternating row shift) plus 5
        // scattered accent pebbles actually landed in the document.
        {
            const auto Pebbles = ConstructPatternLibraryPreset(7);
            Confirm(Pebbles.Name == std::string("Pebble Path"), "Pebble Path preset lost its name.");
            size_t SmallCount = 0, AccentCount = 0;
            std::set<float> RowX;
            for (const auto &Source : Pebbles.Instructions)
            {
                if (Source.Topology[0] != 1) continue;
                if (Source.Bounds[2] == 56) { ++SmallCount; RowX.insert(Source.Bounds[0]); }
                else if (Source.Bounds[2] == 72) ++AccentCount;
            }
            Confirm(SmallCount == 16, "Pebble Path grid pebble count changed.");
            Confirm(AccentCount == 5, "Pebble Path scattered accent count changed.");
            Confirm(RowX.size() >= 4, "Pebble Path brick row offset stopped varying column positions.");
        }
        // Shipped Alloy starters: the Library.toml sidecar enumerates every family and
        // palette file (Automation/ExportPatternLibrary.mjs generates both). Each file
        // must decode and coverage-validate; the counts pin the catalog so families
        // can only change alongside this test.
        const std::filesystem::path LibraryRoot("EngineContent/PatternArchives/TextureStudio/starters");
        const auto Library = toml::parse_file((LibraryRoot / "Library.toml").string());
        Confirm(std::string(Library["format"].value_or("")) == "frontier.pattern_library", "Pattern library sidecar format changed.");
        Confirm(Library["version"].value_or(0) == 1, "Pattern library sidecar version changed.");
        Confirm(std::string(Library["default_palette"].value_or("")) == "Indigo", "Pattern library default palette changed.");
        const auto *Palettes = Library["palettes"].as_array();
        const auto *Groups = Library["groups"].as_array();
        const auto *Families = Library["families"].as_array();
        Confirm(Palettes && Palettes->size() == 4, "Pattern library palette list changed.");
        Confirm(Groups && Groups->size() == 8, "Pattern library collection list changed.");
        Confirm(Families && Families->size() == 103, "Pattern library family count changed.");
        std::set<std::string> ShippedNames;
        size_t ShippedFiles = 0;
        for (const auto &Item : *Families)
        {
            const auto *Family = Item.as_table();
            Confirm(Family != nullptr, "Pattern library family is malformed.");
            const std::string Name = (*Family)["name"].value_or("");
            const std::string Group = (*Family)["group"].value_or("");
            Confirm(!Name.empty() && !Group.empty(), "Pattern library family lost its name or group.");
            bool GroupListed = false;
            for (const auto &Listed : *Groups)
                if (Listed.value_or("") == Group) GroupListed = true;
            Confirm(GroupListed, "Pattern library family references an unlisted collection.");
            const auto *Files = (*Family)["files"].as_table();
            Confirm(Files && !Files->empty(), "Pattern library family holds no palette files.");
            for (const auto &[Palette, File] : *Files)
            {
                const std::string Filename = File.value_or("");
                Confirm(!Filename.empty(), "Pattern library palette file is unnamed.");
                std::ifstream Input(LibraryRoot / Filename, std::ios::binary | std::ios::ate);
                Confirm(bool(Input), "Shipped preset file unreadable.");
                Confirm(Input.tellg() > 0 && Input.tellg() <= 4000000, "Shipped preset file size out of bounds.");
                std::string Source(size_t(Input.tellg()), '\0');
                Input.seekg(0);
                Confirm(bool(Input.read(Source.data(), Source.size())), "Shipped preset file truncated.");
                const auto Document = DecodeVectorAuthoring(Source);
                Confirm(!Document.Layers.empty() && !Document.Name.empty(), "Shipped preset file holds no named artwork.");
                ValidateVectorCoverage(ConstructVectorAuthoringCoverage(Document));
                ShippedNames.insert(Document.Name);
                ++ShippedFiles;
            }
        }
        Confirm(ShippedFiles == 178, "Shipped preset palette file count changed.");
        Confirm(ShippedNames.size() == ShippedFiles, "Shipped preset artwork names collided.");
        bool Refused = false;
        try { (void)ConstructPatternLibraryPreset(QueryPatternLibraryPresetCount()); }
        catch (const std::exception &) { Refused = true; }
        Confirm(Refused, "Out-of-range preset index accepted.");
        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
