/*==========================================================================================================================================
                                                      MATERIALLIBRARYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Exercise real library IO, failed-apply retention, source conflicts and native ImGui edit actions without claiming a window launch.

#include "UI/TextureStudio/MaterialLibraryPanel.h"
#include "RasterizeLibraryEvidence.h"
#include <imgui_internal.h>
#include "Engine/Interface/ContentDirectory.h"
#include "Engine/Render/VectorCoverageRasterization.h"
#include "Engine/Render/VectorSurfaceMaterialBridge.h"
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <tuple>
#include <vector>
#include <imgui.h>

namespace
{
std::string ExtractText(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary);
    return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
}

void EncodeText(const std::filesystem::path &Path, const std::string &Text)
{
    std::ofstream Output(Path, std::ios::binary);
    Output << Text;
    if (!Output) throw std::runtime_error("Fixture could not be written.");
}
} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    try
    {
        uint32_t Checks = 0;
        const auto Confirm = [&](bool Condition, const char *Description)
        {
            if (!Condition) throw std::runtime_error(Description);
            ++Checks;
        };
        Confirm(ArgumentCount == 3, "Expected material specimens and scratch output directory.");
        // 🧩 Adaptive bake buckets keep dense weaves under the OS GPU
        // watchdog: the largest square extent fitting ~2e9 curve·pixel·
        // channels, six channels for the bridge, one for the canvas. The
        // empty/simple single-channel ceiling is 4096 (not 2048): both canvas
        // panels already clamp their own zoomed, device-pixel display size to
        // 4096, and VectorCoverageRasterization::Initialize's own image-size
        // validation already allows up to 4096x4096 — a 2048 ceiling here left
        // a sufficiently zoomed-in (e.g. 800%) canvas baking at a frozen 2048
        // while the displayed quad kept growing, a stretched raster visibly
        // pixelating past that zoom level.
        for (const auto &[Segments, Channels, Extent] : {std::tuple<size_t, uint32_t, uint32_t>{0, 6, 512}, {0, 1, 4096},
                 {1271, 6, 512}, {1272, 6, 256}, {5086, 6, 256}, {5087, 6, 128}, {18954, 6, 128}, {32768, 6, 128},
                 {1907, 1, 1024}, {1908, 1, 512}, {7629, 1, 512}, {7630, 1, 256}, {30517, 1, 256}, {30518, 1, 128}, {32768, 1, 128}})
            Confirm(VectorCoverageRasterization::SelectBakeExtent(Segments, Channels) == Extent, "Bake bucket boundary moved.");
        bool RejectedChannels = false;
        try { (void)VectorCoverageRasterization::SelectBakeExtent(100, 0); }
        catch (const std::exception &) { RejectedChannels = true; }
        Confirm(RejectedChannels, "Channel-free bake budget accepted.");
        const std::vector<float> Constant(4, .25f);
        const auto Spread = VectorSurfaceMaterialBridge::UpsampleFloatRgba(Constant, 1, 1, 4, false);
        Confirm(Spread.size() == 64, "Upsampled map has the wrong pixel population.");
        Confirm(std::all_of(Spread.begin(), Spread.end(), [](float Sample) { return Sample == .25f; }),
            "Constant map did not survive upsampling.");
        const std::vector<float> Corners{0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1};
        const auto Restored = VectorSurfaceMaterialBridge::UpsampleFloatRgba(Corners, 2, 2, 4, false);
        Confirm(std::abs(Restored[(1 * 4 + 1) * 4] - 1 / 3.f) < 1e-5f && std::abs(Restored[(1 * 4 + 1) * 4 + 1] - 1 / 3.f) < 1e-5f,
            "Bilinear upsample missed its interior blend.");
        const auto Levelled = VectorSurfaceMaterialBridge::UpsampleFloatRgba(
            std::vector<float>{.5f, .5f, 1, 1, .5f, .5f, 1, 1, .5f, .5f, 1, 1, .5f, .5f, 1, 1}, 2, 2, 4, true);
        Confirm(std::abs(Levelled[2] - 1) < 1e-5f && std::abs(Levelled[0] - .5f) < 1e-5f, "Flat normal tilted during upsampling.");
        bool RejectedUpsample = false;
        const std::vector<float> Ragged{1, 2, 3};
        try { (void)VectorSurfaceMaterialBridge::UpsampleFloatRgba(Ragged, 1, 1, 4, false); }
        catch (const std::exception &) { RejectedUpsample = true; }
        Confirm(RejectedUpsample, "Ragged upsample source accepted.");
        const auto Output = std::filesystem::path(Arguments[2]) / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto Content = Output/"Library";
        std::filesystem::create_directories(Content/"Nested");
        std::filesystem::copy(Arguments[1], Content, std::filesystem::copy_options::recursive);
        const std::string Minimal = "# Retain this authored comment\nFormat=1\nName='Nested reference'\n[[Strata]]\nCategory='Opaque'\n";
        EncodeText(Content/"Nested/Reference.material", Minimal);
        EncodeText(Content/"Broken.material", "Not valid material TOML!");
        EncodeText(Content/"Ignore.texture.json", "{}");
        EncodeText(Content/"Oversized.material", std::string(1048577, 'x'));
        MaterialLibrary Library;
        Confirm(!Library.Refresh(), "Unconfigured library refreshed.");
        Confirm(!Library.ConfigureDirectory(Output/"Missing"), "Missing library was silently created.");
        Confirm(Library.ConfigureDirectory(Content), "Library did not open.");
        Confirm(Library.QueryEntries().size() == 7, "Recursive catalogue missed material files or included unrelated formats.");
        size_t Invalid = 0;
        for (const auto &Entry : Library.QueryEntries())
        {
            Invalid += !Entry.Diagnostic.empty();
            Confirm(EncodeContentPath(Entry.RelativePath).find('\\') == std::string::npos,
                "Stored material selection uses platform-native separators.");
        }
        Confirm(Invalid == 2, "Invalid and oversized files did not remain discoverable with diagnostics.");
        const auto Locations = Library.QueryEntries();
        Confirm(Library.Refresh(), "Catalogue refresh failed.");
        for (size_t Index = 0; Index < Locations.size(); ++Index)
            Confirm(Locations[Index].RelativePath == Library.QueryEntries()[Index].RelativePath, "Catalogue ordering changed.");
        Library.ConfigureFilter("NESTED REFERENCE");
        Confirm(Library.QueryVisible().size() == 1, "ASCII case-insensitive name search failed.");
        Library.ConfigureFilter("Nested/");
        Confirm(Library.QueryVisible().size() == 1, "Relative path search failed.");
        Library.ConfigureFilter("Nested\\");
        Confirm(Library.QueryVisible().size() == 1, "Windows separator path search failed.");
        Library.ConfigureFilter("");
        Confirm(Library.OpenMaterial("Nested/Reference.material"), "Nested material did not open.");
        Confirm(Library.QuerySource() == Minimal && !Library.QueryModified() && !Library.QueryUnapplied(), "Opening changed authored source or dirty status.");
        const auto Revision = Library.QueryRevision();
        const auto *Applied = Library.QueryApplied();
        Library.ReplaceSource(Minimal+"Roughness=-1\n");
        Confirm(Library.QueryModified() && Library.QueryUnapplied(), "Draft state was not separated from applied/disk source.");
        Confirm(!Library.ApplySource() && Library.QueryApplied() == Applied && Library.QueryRevision() == Revision, "Failed apply replaced the last valid material.");
        Confirm(!Library.SaveMaterial() && ExtractText(Content/"Nested/Reference.material") == Minimal, "Invalid draft was saved.");
        Confirm(Library.RevertSource() && Library.QuerySource() == Minimal && !Library.QueryModified(), "Revert did not restore saved source.");
        Confirm(Library.ConfigureStratumCoefficient(0, "Roughness", .6f), "Missing structured coefficient could not be inserted.");
        Confirm(Library.QuerySource().find("# Retain this authored comment") != std::string::npos &&
                ExtractText(Content/"Nested/Reference.material") == Minimal, "Structured edit changed comments or wrote to disk.");
        Confirm(!Library.ConfigureStratumCoefficient(0, "Roughness", -1), "Invalid structured coefficient accepted.");
        Confirm(Library.ConfigureStratumCoefficient(0, "Anisotropy", .35f), "Missing anisotropy could not be inserted.");
        // 🧩 Fuzz/sheen contract: the fabric rim lobe is new ABI surface (PR #7's dormant-contract freeze
        // taught us uniform-meaning changes must be asserted at the CPU boundary), so exercise the full
        // coefficient + colour + compile round trip here rather than trusting the TOML plumbing alone.
        Confirm(Library.ConfigureStratumCoefficient(0, "SheenIntensity", .4f), "Missing sheen intensity could not be inserted.");
        Confirm(Library.ConfigureStratumCoefficient(0, "SheenRoughness", .5f), "Missing sheen roughness could not be inserted.");
        Confirm(!Library.ConfigureStratumCoefficient(0, "SheenIntensity", -1), "Invalid sheen intensity accepted.");
        Confirm(Library.ConfigureStratumColour(0, std::array<float,3>{.1f,.2f,.3f}, "SheenColor"), "Sheen colour could not be inserted.");
        Confirm(Library.QuerySource().find("SheenColor") != std::string::npos &&
                Library.QuerySource().find("SheenIntensity") != std::string::npos &&
                Library.QuerySource().find("SheenRoughness") != std::string::npos, "Sheen fields missing from structured draft.");
        Confirm(Library.ApplySource() &&
                Library.QueryApplied()->Scattering.Illumination.Strata[0].Sheen[3] > 0 &&
                Library.QueryApplied()->Scattering.Illumination.Strata[0].SheenFinish[0] > 0,
                "Sheen coefficients did not compile into the scattering contract.");
        Confirm(Library.RevertSource(), "Structured draft did not revert.");
        const auto Edited = Minimal+"Roughness=0.75\n# User-authored footer\n";
        Library.ReplaceSource(Edited);
        Confirm(Library.ApplySource(), "Valid edited material failed to compile.");
        Confirm(Library.QueryModified() && !Library.QueryUnapplied(), "Apply incorrectly saved the file.");
        Confirm(ExtractText(Content/"Nested/Reference.material") == Minimal, "Apply wrote to disk.");
        const bool Saved = Library.SaveMaterial();
        Confirm(Saved, Library.QueryDiagnostic().c_str());
        Confirm(ExtractText(Content/"Nested/Reference.material") == Edited, "Save changed comments/source bytes.");
        Confirm(!Library.QueryModified(), "Save left material dirty.");
        EncodeText(Content/"Nested/Reference.material", Minimal+"# External edit\n");
        Library.ReplaceSource(Edited+"# Local edit\n");
        Confirm(!Library.SaveMaterial() && ExtractText(Content/"Nested/Reference.material") == Minimal+"# External edit\n", "Save overwrote external changes.");
        Confirm(Library.OpenMaterial("Nested/Reference.material"), "Explicit reopen after conflict failed.");
        const auto BeforeFailedOpen = Library.QuerySource();
        Confirm(!Library.OpenMaterial("Broken.material") && Library.QuerySource() == BeforeFailedOpen, "Invalid open discarded the current document.");
        Confirm(!Library.ConfigureDirectory(Output/"Missing") && Library.QuerySource() == BeforeFailedOpen, "Failed library switch discarded the current document.");
        for (const auto &Path : {"../Escape.material", "/Outside.material", "C:/Outside.material", "Bad\\Path.material", "Wrong.json"})
            Confirm(!Library.CreateMaterial(DecodeContentPath(Path), "Refused"), "Unsafe/wrong-extension destination accepted.");
        const auto Unicode = DecodeContentPath("Nested/Brass-\xc3\xa9.material");
        Confirm(Library.CreateMaterial(Unicode, "Brass \xc3\xa9"), "Unicode material creation failed.");
        const auto Created = ExtractText(Content/Unicode);
        Confirm(!Library.CreateMaterial(Unicode, "Overwrite") && ExtractText(Content/Unicode) == Created, "Create overwrote existing material.");
        Confirm(Library.DuplicateMaterial("Copy.material") && ExtractText(Content/"Copy.material") == Created, "Duplicate changed authored source.");
        Confirm(!Library.DuplicateMaterial("Copy.material") && ExtractText(Content/"Copy.material") == Created, "Duplicate overwrote destination.");
        Library.ReplaceSource("invalid draft");
        Confirm(!Library.DuplicateMaterial("InvalidCopy.material") && !std::filesystem::exists(Content/"InvalidCopy.material"), "Invalid draft duplicated.");
        Confirm(Library.OpenMaterial("CoatedCeramic.material") && Library.QueryApplied()->Textures[1].Layers == 2, "Independent layer textures did not compile through library.");
        auto Definition = DecodeSurfaceMaterial(Minimal);
        Definition.TextureSources[0] = "Textures/Albedo.png";
        EncodeText(Content/"Nested/RootTexture.material", EncodeSurfaceMaterial(Definition));
        Confirm(Library.OpenMaterial("Nested/RootTexture.material") && Library.QueryApplied()->Textures[0].Levels[0].Width == 128,
                "Nested document changed ContentDirectory-relative texture meaning.");
        auto InvalidTexture = DecodeSurfaceMaterial(Minimal);
        InvalidTexture.TextureSources[0] = "../Escape.png";
        Library.ReplaceSource(EncodeSurfaceMaterial(InvalidTexture));
        const auto *BeforeTextureFailure = Library.QueryApplied();
        Confirm(!Library.ApplySource() && Library.QueryApplied() == BeforeTextureFailure, "Failed texture compilation replaced applied material.");
#ifndef _WIN32
        std::filesystem::create_directories(Output/"Outside");
        EncodeText(Output/"Outside/Escape.material", Minimal);
        std::filesystem::create_directory_symlink(Output/"Outside", Content/"Linked");
        Confirm(!Library.OpenMaterial("Linked/Escape.material"), "Symlink escaped ContentDirectory.");
        Confirm(!Library.CreateMaterial("Linked/New.material", "Escape"), "Create escaped via symlink.");
        Confirm(!std::filesystem::exists(Output/"Outside/New.material"), "Rejected create wrote outside root.");
#endif
        EncodeText(Content/"ProceduralPilot.material", Minimal);
        MaterialLibrary Pilot;
        Confirm(Pilot.ConfigureDirectory(Content), "Procedural scratch library did not open.");
        Confirm(Pilot.OpenMaterial("ProceduralPilot.material"), "Procedural pilot did not open.");
        Confirm(!Pilot.InsertStratum(9) && !Pilot.RemoveStratum(0), "Stratum bounds accepted an invalid edit.");
        Confirm(Pilot.InsertStratum(1), "Stratum append failed.");
        Confirm(DecodeSurfaceMaterial(Pilot.QuerySource()).Scattering.Illumination.Configuration[0] == 2, "Appended stratum did not decode.");
        Confirm(Pilot.ConfigureStratumChannel(0, "Normal", "scratches"), "Procedural channel switch failed.");
        {
            const auto PilotDefinition = DecodeSurfaceMaterial(Pilot.QuerySource());
            Confirm(PilotDefinition.StratumChannels[0].size() == 1 && PilotDefinition.StratumChannels[0][0].Op == "scratches",
                "Procedural channel did not decode with its op.");
            Confirm(PilotDefinition.StratumTextureSources[0][2] == "procedural/0/normal", "Derived procedural source differs.");
        }
        Confirm(Pilot.ConfigureStratumChannel(1, "Normal", "scratches"), "Second procedural channel failed.");
        Confirm(Pilot.ConfigureProceduralParam(0, "Normal", "density", 9), "Shared pool param edit failed.");
        {
            const auto PilotDefinition = DecodeSurfaceMaterial(Pilot.QuerySource());
            const auto Pool = QueryProceduralPool(PilotDefinition, "density");
            Confirm(Pool && Pool->Value == 9 && Pool->Stratum == 0 && Pool->Channel == "Normal", "Shared pool lookup differs.");
            const auto Fanout = QueryProceduralPool(PilotDefinition, "density");
            Confirm(Fanout && PilotDefinition.StratumChannels[1][0].Params.at("density") == 9, "Pool edit did not fan out to the linked channel.");
        }
        Confirm(Pilot.ConfigureProceduralParam(0, "Normal", "scratchAngle", 30), "Direction alias edit failed.");
        {
            const auto PilotDefinition = DecodeSurfaceMaterial(Pilot.QuerySource());
            const auto Pool = QueryProceduralPool(PilotDefinition, "angle");
            Confirm(Pool && Pool->Value == 30, "Direction alias did not collapse onto the stored angle.");
        }
        Confirm(Pilot.ForkProceduralChannel(1, "Normal"), "Pool fork failed.");
        {
            const auto PilotDefinition = DecodeSurfaceMaterial(Pilot.QuerySource());
            Confirm(PilotDefinition.StratumChannels[0][0].Link != PilotDefinition.StratumChannels[1][0].Link, "Fork kept the shared pool link.");
        }
        Confirm(Pilot.MergeProceduralChannel(1, "Normal", 0, "Normal"), "Pool merge failed.");
        {
            const auto PilotDefinition = DecodeSurfaceMaterial(Pilot.QuerySource());
            Confirm(PilotDefinition.StratumChannels[0][0].Link == PilotDefinition.StratumChannels[1][0].Link, "Merge did not reunite the pool link.");
        }
        Confirm(Pilot.RebakeProcedurals(64), "Proxy procedural rebake failed.");
        Confirm(Pilot.QueryApplied()->Textures[2].Levels[0].Width == 64, "Rebaked normal did not compile at proxy resolution.");
        Confirm((Pilot.QueryApplied()->Scattering.TextureUse[0][0] & 4) != 0, "Rebaked normal mask bit missing.");
        Confirm(Pilot.RevertSource(), "Procedural revert failed.");
        Confirm(DecodeSurfaceMaterial(Pilot.QuerySource()).StratumChannels[0].empty(), "Revert kept procedural channels.");
        Confirm(Pilot.ConfigureStratumChannel(0, "Normal", "scratches"), "Channel switch after revert failed.");
        Confirm(Pilot.SaveMaterial(), "Procedural save failed.");
        Confirm(std::filesystem::is_regular_file(Content/"Textures"/"ProceduralPilot-normal.png"), "Baked procedural PNG missing after save.");
        Confirm(Pilot.QuerySource().find("ProceduralPilot-normal.png") != std::string::npos, "Saved source does not reference the baked PNG.");
        Confirm(Pilot.QuerySource().find("procedural/") == std::string::npos, "Derived source leaked into the saved material.");
        Confirm(Pilot.QueryApplied()->Textures[2].Levels[0].Width == 512, "Saved material did not compile full-resolution baked maps.");
        Confirm(Pilot.InsertStratum(1) && Pilot.RemoveStratum(1), "Stratum removal failed.");
        Confirm(DecodeSurfaceMaterial(Pilot.QuerySource()).Scattering.Illumination.Configuration[0] == 1, "Removed stratum still decodes.");
        MaterialLibrary ReferenceLibrary;
        Confirm(ReferenceLibrary.ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio"), "HTML library did not open.");
        // 📝 127 HTML presets + 4 camouflage entries + 5 sand presets + 6 layered rock/landscape presets.
        Confirm(ReferenceLibrary.QueryEntries().size() == 142, "HTML library count differs.");
        Confirm(ReferenceLibrary.QueryEntries().front().Name == "Racing Green" && ReferenceLibrary.QueryEntries()[1].Name == "Midnight Amethyst", "HTML catalogue order differs.");
        for (const auto &Entry : ReferenceLibrary.QueryEntries())
            Confirm(Entry.Diagnostic.empty() && !Entry.Category.empty() && !Entry.RecipeControls.empty(), "HTML preset metadata/physical definition is invalid.");
        for (const auto *SandPath : {"sand.material", "layered-sand.material", "sahara-erg-sand.material",
                 "icelandic-black-sand.material", "coral-atoll-white-sand.material",
                 "wadi-red-desert-sand.material", "wet-tidal-beach-sand.material"})
        {
            Confirm(ReferenceLibrary.OpenMaterial(SandPath), "Sand preset did not compile.");
            const auto *Compiled = ReferenceLibrary.QueryApplied();
            Confirm(Compiled && Compiled->Scattering.Paint.Configuration[0] == 1 &&
                        Compiled->Scattering.Paint.Distribution[0] > 0.f &&
                        Compiled->Textures[0].Levels[0].Width == 512,
                    "Sand preset is missing active mineral flakes or 512px textures.");
        }
        for (const auto *RockPath : {"alpine-mica-schist.material", "columnar-olivine-basalt.material",
                 "canyon-banded-sandstone.material", "weathered-karst-limestone.material",
                 "obsidian-rhyolite-scree.material", "badlands-laterite-outcrop.material"})
        {
            Confirm(ReferenceLibrary.OpenMaterial(RockPath), "Layered rock preset did not compile.");
            const auto *Compiled = ReferenceLibrary.QueryApplied();
            Confirm(Compiled && Compiled->Scattering.Paint.Configuration[0] == 1 &&
                        Compiled->Scattering.Paint.Distribution[0] > 0.f &&
                        Compiled->AuthoringMaps[0].Levels[0].Width == 256 &&
                        Compiled->Textures[0].Levels[0].Width == 512,
                    "Layered rock preset is missing mineral flakes, spatial Fill layers, or 512px textures.");
        }
        Confirm(ReferenceLibrary.OpenMaterial("racing-green.material"), "Racing Green did not compile.");
        for (const auto *Path : {"tour-golf-ball.material", "practice-golf-ball.material"})
        {
            Confirm(ReferenceLibrary.OpenMaterial(Path), "Golf preset did not compile.");
            const auto *Compiled = ReferenceLibrary.QueryApplied();
            Confirm(Compiled->Textures[0].Levels[0].Width == 512 && Compiled->Textures[2].Levels[0].Width == 512,
                    "Golf preset albedo/normal detail textures did not compile.");
            Confirm(Compiled->Scattering.TextureUse[0][0] == 5, "Golf preset albedo/normal mask differs.");
        }
        for (const auto *Path : {"monocrystalline-solar.material", "polycrystalline-solar.material", "black-solar-module.material"})
        {
            Confirm(ReferenceLibrary.OpenMaterial(Path), "Solar preset did not compile.");
            const auto *Compiled = ReferenceLibrary.QueryApplied();
            Confirm(Compiled->Textures[0].Levels[0].Width == 512 && Compiled->Textures[1].Levels[0].Width == 512 &&
                        Compiled->Textures[2].Levels[0].Width == 512,
                    "Solar preset albedo/ORM/normal detail textures did not compile.");
            Confirm(Compiled->Scattering.TextureUse[0][0] == 7, "Solar preset texture mask differs.");
        }
        for (const auto *Path : {"lime-stucco.material"})
        {
            Confirm(ReferenceLibrary.OpenMaterial(Path), "Stucco preset did not compile.");
            const auto *Compiled = ReferenceLibrary.QueryApplied();
            Confirm(Compiled->Textures[0].Levels[0].Width == 512 && Compiled->Textures[2].Levels[0].Width == 512,
                    "Stucco preset albedo/normal detail textures did not compile.");
            Confirm(Compiled->Scattering.TextureUse[0][0] == 5, "Stucco preset albedo/normal mask differs.");
        }
        // 📝 24k-gold is a pure conductor and ships values only, so the machined/polished families are
        // the textured specimens here.
        for (const auto *Path : {"liquid-silver.material", "brushed-titanium.material", "brake-disc.material"})
        {
            Confirm(ReferenceLibrary.OpenMaterial(Path), "Metal preset did not compile.");
            const auto *Compiled = ReferenceLibrary.QueryApplied();
            Confirm(Compiled->Textures[0].Levels[0].Width == 512 && Compiled->Textures[1].Levels[0].Width == 512 &&
                        Compiled->Textures[2].Levels[0].Width == 512,
                    "Metal preset albedo/ORM/normal detail textures did not compile.");
            Confirm(Compiled->Scattering.TextureUse[0][0] == 7, "Metal preset texture mask differs.");
        }
        // 📝 Pure conductors bake the Alloy type-1 tooth: an albedo modulator under the reflectance
        // scalar plus grain relief, matching the HTML kernel instead of shipping flat values.
        for (const auto *Path : {"24k-gold.material", "pure-copper.material", "chromium.material"})
        {
            Confirm(ReferenceLibrary.OpenMaterial(Path), "Pure conductor preset did not compile.");
            const auto *Compiled = ReferenceLibrary.QueryApplied();
            Confirm(Compiled->Scattering.TextureUse[0][0] == 5, "Pure conductor preset tooth mask differs.");
            Confirm(Compiled->Textures[0].Levels[0].Width == 512 && Compiled->Textures[1].Levels[0].Width == 1 &&
                        Compiled->Textures[2].Levels[0].Width == 512,
                "Pure conductor preset did not compile its baked tooth maps.");
            Confirm(Compiled->Scattering.Illumination.Strata[0].Optics[0] > .99f,
                "Pure conductor preset is not metallic.");
        }
        // 📝 The optional scratch overlay attaches over the conductor tooth: relief and ORM slots gain
        // the staged cut field, and the field must still bake.
        {
            MaterialLibrary ScratchLibrary;
            Confirm(ScratchLibrary.ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio") &&
                        ScratchLibrary.OpenMaterial("pure-copper.material"),
                "Scratch overlay library did not open a pure conductor.");
            Confirm(ScratchLibrary.ConfigureStratumChannel(0, "Normal", "scratchedMetal") &&
                        ScratchLibrary.ConfigureStratumChannel(0, "Orm", "scratchedMetal"),
                "Optional scratch overlay did not attach to a pure conductor.");
            const auto ScratchDefinition = DecodeSurfaceMaterial(ScratchLibrary.QuerySource());
            const auto Pool = std::find_if(ScratchDefinition.StratumChannels[0].begin(), ScratchDefinition.StratumChannels[0].end(),
                [](const auto &Channel) { return !Channel.Op.empty() && Channel.Params.contains("scratchDensity"); });
            Confirm(Pool != ScratchDefinition.StratumChannels[0].end(), "Attached scratch overlay carries no density.");
            Confirm(ScratchLibrary.ConfigureProceduralParam(0, Pool->Name, "scratchDensity", 6.0) &&
                        ScratchLibrary.RebakeProcedurals(512),
                "Optional scratch overlay on a pure conductor did not bake.");
            const auto *ScratchCompiled = ScratchLibrary.QueryApplied();
            // 📝 The overlay replaces the tooth relief/ORM slots; the baked tooth albedo stays bound.
            Confirm(ScratchCompiled->Scattering.TextureUse[0][0] == 7, "Scratch overlay bound the wrong slots.");
            Confirm(ScratchCompiled->Textures[0].Levels[0].Width == 512, "Scratch overlay lost the conductor tooth albedo.");
            Confirm(ScratchCompiled->Textures[2].Levels[0].Width == 512, "Scratch overlay did not bake relief.");
        }
        Confirm(ReferenceLibrary.OpenMaterial("sand.material"), "Sand preset did not compile.");
        Confirm(ReferenceLibrary.QueryApplied()->Scattering.TextureUse[0][0] == 7, "Sand preset texture mask differs.");
        for (const auto *Path : {"camouflage-ballistic-polymer.material", "camouflage-ballistic-polymer-smooth.material",
                                 "woodland-camouflage-fabric.material", "woodland-camouflage-fabric-smooth.material"})
        {
            Confirm(ReferenceLibrary.OpenMaterial(Path), "Camo preset did not compile.");
            const auto *Compiled = ReferenceLibrary.QueryApplied();
            Confirm(Compiled->Scattering.TextureUse[0][0] != 0, "Camo preset selection did not bake procedural channels.");
            Confirm(Compiled->Textures[0].Levels[0].Width == 256, "Camo preset selection baked maps at the wrong resolution.");
        }
        Confirm(ReferenceLibrary.OpenMaterial("black-solar-module.material"), "Solar preset did not recompile after camo parity.");
        Confirm(ReferenceLibrary.ConfigureStratumColour(0, {.1f, .2f, .3f}), "Structured albedo failed.");
        Confirm(ReferenceLibrary.ConfigureStratumCoefficient(1, "Coverage", .8f), "Structured coat coverage failed.");
        Confirm(!ReferenceLibrary.ConfigureStratumColour(0, {-1, 0, 0}), "Invalid albedo accepted.");
        ImGui::CreateContext();
        auto &IO = ImGui::GetIO();
        IO.IniFilename = nullptr;
        if (std::filesystem::is_regular_file("EngineContent/FontArchives/Inter/Inter-Regular.otf"))
            IO.Fonts->AddFontFromFileTTF("EngineContent/FontArchives/Inter/Inter-Regular.otf", 14);
        else IO.Fonts->AddFontDefault();
        unsigned char *Pixels = nullptr;
        int AtlasWidth = 0, AtlasHeight = 0;
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        ActivationOwnership PanelOwnership(new MaterialLibraryPanel());
        auto *Panel = static_cast<MaterialLibraryPanel *>(PanelOwnership.get());
        Confirm(Panel->Initialize() && Panel->Activate(), "Library panel lifecycle failed.");
        Confirm(Panel->ConfigureDirectory(Content) && Panel->SelectMaterial(Unicode), "Native panel did not open material.");
        const auto IntegrateInterface = [&](float Width, float Density)
        {
            IO.DisplaySize = ImVec2(Width, 900);
            IO.DeltaTime = 1.f/60;
            ImGui::GetStyle().FontScaleDpi = Density;
            ImGui::NewFrame();
            Panel->ConstructInterface();
            ImGui::Render();
            Confirm(ImGui::GetDrawData()->TotalVtxCount > 0, "Native library produced no ImGui draw commands.");
        };
        IntegrateInterface(1280, 1);
        IntegrateInterface(1280, 1);
        const auto Click = [&](PlaneExtent Extent)
        {
            Confirm(Extent.Width() > 0 && Extent.Height() > 0, "Material operation has no interactive extent.");
            IO.AddMousePosEvent((Extent.MinimumX+Extent.MaximumX)/2, (Extent.MinimumY+Extent.MaximumY)/2);
            IO.AddMouseButtonEvent(0, true);
            IntegrateInterface(1280, 1);
            IO.AddMouseButtonEvent(0, false);
            IntegrateInterface(1280, 1);
        };
        Panel->ReplaceSource(Minimal+"Roughness=0.65\n");
        IntegrateInterface(1280, 1);
        const auto ParkedDraft = Panel->QueryLibrary().QuerySource();
        Confirm(Panel->SelectMaterial("CoatedCeramic.material"), "Native selection refused to park unsaved edits.");
        Confirm(Panel->QueryLibrary().QueryParkedCount() == 1, "Parked draft was not retained across selection.");
        Confirm(Panel->SelectMaterial(Unicode), "Native selection did not return to the parked draft.");
        Confirm(Panel->QueryLibrary().QuerySource() == ParkedDraft && Panel->QueryLibrary().QueryModified(),
            "Parked draft was not restored with unsaved status.");
        Click(Panel->QueryApplyExtent());
        Confirm(!Panel->QueryLibrary().QueryUnapplied() && Panel->QueryLibrary().QueryModified(), "Native Apply failed or silently saved.");
        Click(Panel->QuerySaveExtent());
        Confirm(!Panel->QueryLibrary().QueryModified(), "Native Save click failed.");
        Panel->ReplaceSource("Broken draft");
        IntegrateInterface(1280, 1);
        Click(Panel->QueryApplyExtent());
        Confirm(!Panel->QueryLibrary().QueryDiagnostic().empty(), "Native Apply error absent.");
        Click(Panel->QueryRevertExtent());
        Confirm(!Panel->QueryLibrary().QueryModified(), "Native Revert click failed.");
        Confirm(Panel->SelectMaterial("BrushedCopper.material"), "Native selection after save failed.");
        IntegrateInterface(1280, 1);
        IntegrateInterface(1280, 1);
        auto *LibraryWindow = ImGui::FindWindowByName("Material library###FrontierPanel-MaterialLibrary");
        auto *ViewportWindow = ImGui::FindWindowByName("Viewport###FrontierPanel-MaterialViewport");
        auto *InspectorWindow = ImGui::FindWindowByName("Inspector###FrontierPanel-MaterialInspector");
        Confirm(LibraryWindow && ViewportWindow && InspectorWindow, "Registered material dock panels are absent.");
        Confirm(LibraryWindow->DockId && ViewportWindow->DockId && InspectorWindow->DockId, "Material panels are not docked.");
        Confirm(LibraryWindow->DockId != ViewportWindow->DockId && ViewportWindow->DockId != InspectorWindow->DockId,
                "Material panels share one tab region instead of three columns.");
        Confirm(LibraryWindow->Pos.x < ViewportWindow->Pos.x && ViewportWindow->Pos.x < InspectorWindow->Pos.x,
                "Default material columns differ from HTML ordering.");
        Confirm(!Panel->QueryPanels().QueryDetachedWindows(), "Detached OS windows enabled by default.");
        const float OriginalRoughness = Panel->QueryLibrary().QueryApplied()->Scattering.Illumination.Strata[0].AlbedoRoughness[3];
        Click(Panel->QueryRoughnessExtent());
        Confirm(Panel->QueryLibrary().QueryModified() && !Panel->QueryLibrary().QueryUnapplied(), "Released structured slider did not live-apply its draft.");
        Confirm(Panel->QueryLibrary().QueryApplied()->Scattering.Illumination.Strata[0].AlbedoRoughness[3] != OriginalRoughness,
                "Released structured slider did not update the applied material used by the renderer.");
        Click(Panel->QueryRevertExtent());
        Confirm(!Panel->QueryLibrary().QueryModified(), "Structured property edit did not revert.");
        IntegrateInterface(1280, 1);
        IO.Fonts->GetTexDataAsRGBA32(&Pixels, &AtlasWidth, &AtlasHeight);
        EncodeLibraryDrawCapture(Output/"MaterialLibrary.ppm", *ImGui::GetDrawData(), Pixels, AtlasWidth, AtlasHeight);
        Confirm(Panel->SelectMaterial("ProceduralPilot.material"), "Native selection did not open the procedural pilot.");
        IntegrateInterface(1280, 1);
        IntegrateInterface(1280, 1);
        ImGuiWindow *Properties = nullptr;
        for (auto *Candidate : ImGui::GetCurrentContext()->Windows)
        {
            // 📝 Nested property cards qualify under the same parent name; the scroll region itself carries
            // no further child qualifier past its own identifier.
            const std::string Name(Candidate->Name);
            const size_t Child = Name.rfind("/Material properties_");
            if (Child != std::string::npos && Name.find('/', Child + 1) == std::string::npos) Properties = Candidate;
        }
        Confirm(Properties != nullptr, "Material properties scroll region is missing.");
        // 📝 The properties panel replaces the layer stack: the recipe cards render inside this scroll
        // region, so the headless gesture scrolls it and confirms a designer control still live-applies.
        Confirm(Properties->ScrollMax.y > 0, "Material properties scroll region carries no recipe content.");
        Confirm(Panel->QueryLibrary().RevertSource(), "Pilot property edit did not revert.");
        Confirm(Panel->ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio") && Panel->SelectMaterial("racing-green.material"),
                "Native automotive inspector did not open the actual preset.");
        IntegrateInterface(1280, 1);
        IntegrateInterface(1280, 1);
        // 📝 Layer categorization moved clearcoat gloss into the coat layer's finish card, below
        // the fold: scroll the properties region until the recorded coat slider extent is
        // visible, then click its endpoint as before.
        auto Gloss = Panel->QueryCoatRoughnessExtent();
        for (int Attempt = 0; Attempt < 40; ++Attempt)
        {
            ImGuiWindow *PropertiesScroll = nullptr;
            for (auto *Candidate : ImGui::GetCurrentContext()->Windows)
            {
                const std::string Name(Candidate->Name);
                const size_t Child = Name.rfind("/Material properties_");
                if (Child != std::string::npos && Name.find('/', Child + 1) == std::string::npos) PropertiesScroll = Candidate;
            }
            Confirm(PropertiesScroll != nullptr, "Material properties scroll region vanished during gloss gesture.");
            const float CentreY = (Gloss.MinimumY + Gloss.MaximumY) * .5f;
            if (Gloss.Width() > 0 && CentreY > PropertiesScroll->Pos.y + 40 &&
                CentreY < PropertiesScroll->Pos.y + PropertiesScroll->Size.y - 40) break;
            PropertiesScroll->Scroll.y = std::min(PropertiesScroll->Scroll.y + 240.f, PropertiesScroll->ScrollMax.y);
            IntegrateInterface(1280, 1);
            Gloss = Panel->QueryCoatRoughnessExtent();
        }
        Confirm(Gloss.Width() > 0, "Coat layer recorded no designer slider extent.");
        Gloss.MinimumX = Gloss.MaximumX - 2;
        Click(Gloss);
        const auto GlossDefinition = DecodeSurfaceMaterial(Panel->QueryLibrary().QuerySource());
        Confirm(Panel->QueryLibrary().QueryDiagnostic().empty() && GlossDefinition.Scattering.Illumination.Strata[1].AlbedoRoughness[3] >= .045f,
                "Clearcoat gloss endpoint generated an invalid native roughness.");
        Confirm(Panel->QueryLibrary().QueryModified() && !Panel->QueryLibrary().QueryUnapplied() &&
                Panel->QueryLibrary().QueryApplied()->Scattering.Illumination.Strata[1].Optics[2] > .97f,
                "Clearcoat gloss endpoint did not live-update the applied coating.");
        Confirm(Panel->QueryLibrary().RevertSource(), "Automotive inspector edit did not revert.");
        Panel->ReplaceSource(Panel->QueryLibrary().QuerySource());
        Panel->ReplaceSource("Unsaved closing draft");
        Confirm(!Panel->RequestClose(), "Closing discarded unsaved material without confirmation.");
        IntegrateInterface(1280, 1);
        Confirm(!Panel->ConsumeClose(), "Unsaved close automatically authorized.");
        (void)Panel->QueryLibrary().RevertSource();
        Confirm(Panel->RequestClose(), "Clean library refused closing.");
        IntegrateInterface(760, 1);
        IntegrateInterface(1280, 1.5f);
        PanelOwnership.reset();
        ImGui::DestroyContext();
        std::ofstream Report(Output/"MaterialLibraryChecks.json");
        Report << "{\"result\":\"passed\",\"checks\":" << Checks << ",\"native_window_execution\":false,\"windows_execution\":false}\n";
        std::cout << "PASS: " << Checks << " material catalogue, source transaction and native ImGui checks\n";
        std::cout << "REPORT " << (Output/"MaterialLibraryChecks.json").string() << '\n';
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "FAIL: " << Failure.what() << '\n';
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        return 1;
    }
}
