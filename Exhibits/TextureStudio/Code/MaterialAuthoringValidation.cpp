/*============================================================================================================================================
                                                      MATERIALAUTHORINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Prove the native layer-stack port matches the HTML specification: coded-field samples against recorded
// JS goldens, the strict normalize gate, link/designer projection, TOML round trip, and the shipped
// camouflage catalogue entries decoding with a live coded colour source that bakes seamlessly.

#include "Engine/ContentInterchange/MaterialAuthoring.h"
#include "Engine/ContentInterchange/MaterialLibrary.h"
#include "Engine/Render/MaterialProceduralBaking.h"
#include <cmath>
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

bool Near(double Actual, double Expected, double Tolerance = 1e-6) { return std::abs(Actual - Expected) <= Tolerance; }

std::string ReadText(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary);
    if (!Input) throw std::runtime_error("Catalogue material is missing.");
    return {std::istreambuf_iterator<char>(Input), std::istreambuf_iterator<char>()};
}
} // namespace

int main()
{
    try
    {
        // Coded-field goldens recorded from FieldSpecification.js SampleGenerator (centred coordinates,
        // camouflage source {scale 8, seed 12, softness .012, pixelSize 4, colourCount 4}; others
        // {scale 6, seed 3}). Tolerance covers libm sin ulp drift only.
        constexpr std::array<std::pair<double, double>, 7> Points{
            {{-.5, -.5}, {-.31, .17}, {0, 0}, {.123, -.456}, {.499, .499}, {.25, .25}, {-.05, .42}}};
        struct GoldenRow
        {
            const char *Identity;
            std::array<double, 7> Samples;
        };
        constexpr std::array<GoldenRow, 8> Goldens{{
            {"camouflage-smooth", {.6666666667, 0, .3333333333, .3333333333, .6666666667, 0, .6666666667}},
            {"camouflage", {.6666666667, 0, .3333333333, .3333333333, .6666666667, 0, 1}},
            {"noise", {.7703528562, .2544963357, .6203949063, .5099946453, .3632752912, .4521477193, .5169604390}},
            {"stripes", {.5705600040, .5079641369, .5705600040, .0007188198857, .5891663682, .4294399960, .4920358631}},
            {"checker", {1, 0, 1, 0, 1, 1, 0}},
            {"cells", {.5362749359, .3644850572, .6402928873, .5793583388, .4645750117, .3159485151, .4816498185}},
            {"ridged", {.4592942876, .5089926714, .7592101875, .9800107093, .7265505824, .9042954385, .9660791220}},
            {"rings", {.3918987976, .04786257445, .05457854635, .5825996850, .05775659730, .0005246695994, .9521627639}},
        }};
        for (const auto &Row : Goldens)
        {
            CodedFieldParameters Parameters;
            if (std::string_view(Row.Identity).starts_with("camouflage"))
            {
                Parameters.Scale = 8;
                Parameters.Seed = 12;
            }
            else
            {
                Parameters.Scale = 6;
                Parameters.Seed = 3;
            }
            for (size_t Index = 0; Index < Points.size(); ++Index)
                Confirm(Near(EvaluateCodedField(Row.Identity, Parameters, Points[Index].first, Points[Index].second),
                            Row.Samples[Index], 1e-5),
                    "Coded field sample drifts from the JS golden.");
            // Periodic camouflage: opposite tile edges agree.
            if (std::string_view(Row.Identity).starts_with("camouflage"))
                Confirm(Near(EvaluateCodedField(Row.Identity, Parameters, -.5, .123),
                            EvaluateCodedField(Row.Identity, Parameters, .5, .123), 1e-9),
                    "Camouflage tile seam is visible.");
        }
        Confirm(!QueryCodedField("does-not-exist") && EvaluateCodedField("does-not-exist", {}, 0, 0) == 0,
            "Unknown coded field must sample zero.");

        // Declaration shapes mirror DeclareMaterialAuthoring: paint stacks coat over flakes over surface.
        MaterialAuthoringDeclaration Paint;
        Paint.Type = 0;
        Paint.RecipeIdentity = "paint";
        Paint.RecipeCaption = "Automotive paint";
        const auto PaintDocument = DeclareMaterialAuthoring(Paint);
        Confirm(PaintDocument.Layers.size() == 3 && PaintDocument.Layers[0].Model == "coat" &&
                PaintDocument.Layers[1].Model == "flakes" && PaintDocument.Layers[2].Model == "surface",
            "Paint layer declaration changed.");
        MaterialAuthoringDeclaration Plain;
        Plain.Type = 3;
        Plain.RecipeIdentity = "metal";
        Plain.RecipeCaption = "Metal";
        auto Document = DeclareMaterialAuthoring(Plain);
        Confirm(Document.Layers.size() == 1 && Document.Layers[0].Model == "surface", "Plain declaration changed.");

        // Coded camouflage instance: palette seeded, colour source references it, normalize accepts.
        const std::string FieldIdentity = DeclareCodedField(Document, "camouflage", "Woodland camouflage", "Camouflage");
        MaterialAuthoringSource ColourSource;
        ColourSource.Mode = "field";
        ColourSource.Direct = true;
        ColourSource.Generator = FieldIdentity;
        Document.Sources["color"] = ColourSource;
        NormalizeMaterialAuthoring(Document);
        MaterialAuthoringStore Store;
        const auto Palette = ReadMaterialParameter(Document, Store, "field:" + FieldIdentity + ":colour1");
        Confirm(Palette && std::get<std::string>(*Palette) == "#566044", "Camouflage palette seeding changed.");

        // Normalize rejects: duplicate identity, unknown channel, bad version, dangling field source.
        const auto Rejected = [](MaterialAuthoringDocument Candidate) {
            try { NormalizeMaterialAuthoring(Candidate); }
            catch (const std::exception &) { return true; }
            return false;
        };
        auto Duplicate = Document;
        Duplicate.Layers.push_back(Duplicate.Layers.front());
        Confirm(Rejected(Duplicate), "Duplicate layer identity accepted.");
        auto BadChannel = Document;
        BadChannel.Layers[0].Channels.emplace_back("bogus");
        Confirm(Rejected(BadChannel), "Unknown PBR channel accepted.");
        auto BadVersion = Document;
        BadVersion.Version = 1;
        Confirm(Rejected(BadVersion), "Legacy authoring version accepted.");
        auto Dangling = Document;
        Dangling.Sources["color"].Generator = IssueMaterialAuthoringIdentity();
        Confirm(Rejected(Dangling), "Dangling coded field source accepted.");

        // Link chain: a shared variable redirects reads/writes and cycles fail closed.
        MaterialAuthoringVariable Shared;
        Shared.Identity = IssueMaterialAuthoringIdentity();
        Shared.Name = "Shared roughness";
        Shared.Address = "roughness";
        Shared.Category = "scalar";
        Shared.Minimum = 0;
        Shared.Maximum = 1;
        Shared.Bounded = true;
        Document.Variables.push_back(Shared);
        Document.Links["coatRoughness"] = Shared.Identity;
        NormalizeMaterialAuthoring(Document);
        Store["roughness"] = .42;
        const auto Linked = ReadMaterialParameter(Document, Store, "coatRoughness");
        Confirm(Linked && Near(std::get<double>(*Linked), .42), "Linked read misses the shared address.");
        WriteMaterialParameter(Document, Store, "coatRoughness", .25);
        Confirm(Near(std::get<double>(Store["roughness"]), .25), "Linked write misses the shared address.");

        // Designer projection: one normalized control drives every target (RecipeInspector parity).
        MaterialAuthoringControl Control;
        Control.Direct = false;
        Control.Targets = {{"roughness", .12, .42, false}, {"depth", .15, 1.8, false}};
        ProjectDesignerControl(Document, Store, Control, .5);
        Confirm(Near(std::get<double>(Store["roughness"]), .27) && Near(std::get<double>(Store["depth"]), .975),
            "Designer control projection changed.");
        Confirm(Near(DesignerControlQuantity(Document, Store, Control), .5), "Designer quantity readback changed.");

        // Resolve projection: retiring the surface's roughness channel restores the channel default.
        auto Retired = Document;
        auto &SurfaceChannels = Retired.Layers.back().Channels;
        SurfaceChannels.erase(std::remove(SurfaceChannels.begin(), SurfaceChannels.end(), "roughness"),
            SurfaceChannels.end());
        MaterialAuthoringStore Resolved = Store;
        Resolved["roughness"] = .9;
        (void)ResolveAuthoringParameters(Retired, Resolved);
        Confirm(Near(std::get<double>(Resolved["roughness"]), .5), "Retired channel keeps its authored value.");

        // TOML round trip through a full .material document keeps the stack intact.
        MaterialDefinition Definition;
        Definition.Name = "Authoring round trip";
        Definition.Scattering.Illumination.Configuration[0] = 1;
        Definition.Authoring = Document;
        const std::string Encoded = EncodeSurfaceMaterial(Definition);
        const auto Decoded = DecodeSurfaceMaterial(Encoded);
        Confirm(Decoded.Authoring && Decoded.Authoring->Layers.size() == Document.Layers.size() &&
                Decoded.Authoring->Generators.size() == Document.Generators.size() &&
                Decoded.Authoring->Sources.at("color").Generator == FieldIdentity &&
                Decoded.Authoring->Links.at("coatRoughness") == Shared.Identity,
            "Authoring TOML round trip lost the layer stack.");
        Confirm(EncodeSurfaceMaterial(Decoded) == Encoded, "Authoring TOML encoding is not stable.");

        // Shipped catalogue: four camouflage presets decode with a coded colour field and bake a
        // non-uniform, seam-free albedo whose texels come from the authored palette.
        MaterialLibrary Library;
        Confirm(Library.ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio"), "Catalogue missing.");
        Confirm(Library.Refresh(), "Catalogue refresh failed.");
        Confirm(Library.QueryEntries().size() == 142, "Catalogue no longer holds the shipped preset count.");
        const std::set<std::string> CamouflageIds{"woodland-camouflage-fabric", "woodland-camouflage-fabric-smooth",
            "camouflage-ballistic-polymer", "camouflage-ballistic-polymer-smooth"};
        size_t CamouflageFound = 0;
        for (const auto &Entry : Library.QueryEntries())
        {
            const std::string Stem = Entry.RelativePath.stem().string();
            if (!CamouflageIds.count(Stem)) continue;
            ++CamouflageFound;
            const auto Material = DecodeSurfaceMaterial(
                ReadText(std::filesystem::path("EngineContent/MaterialArchives/TextureStudio") / Entry.RelativePath));
            Confirm(Material.Authoring.has_value(), "Camouflage preset lost its authoring document.");
            const auto &Source = Material.Authoring->Sources.at("color");
            Confirm(Source.Mode == "field" && Source.Direct.value_or(false), "Camouflage colour is not a direct field.");
            bool AlbedoCoded = false;
            for (const auto &Channels : Material.StratumChannels)
                for (const auto &Channel : Channels)
                    if (Channel.Name == "Albedo" && QueryCodedField(Channel.Op))
                    {
                        AlbedoCoded = true;
                        const auto Baked = BakeProceduralChannelRgba(Channel.Op, Channel.Name, Channel.Params,
                            Channel.Texts, 64);
                        Confirm(Baked.size() == 64u * 64 * 4, "Camouflage bake extent changed.");
                        std::set<uint32_t> Texels;
                        for (size_t Pixel = 0; Pixel < 64u * 64; ++Pixel)
                            Texels.insert(uint32_t(Baked[Pixel * 4]) << 16 | uint32_t(Baked[Pixel * 4 + 1]) << 8 |
                                          uint32_t(Baked[Pixel * 4 + 2]));
                        Confirm(Texels.size() >= 3, "Camouflage bake collapsed to a flat colour.");
                        if (Channel.Op == "camouflage")
                            Confirm(Texels.size() <= 4, "Pixelated camouflage blends outside its palette.");
                    }
            Confirm(AlbedoCoded, "Camouflage preset has no coded albedo channel.");
        }
        Confirm(CamouflageFound == CamouflageIds.size(), "Camouflage catalogue entries are missing.");
        Confirm(Library.OpenMaterial("alpine-mica-schist.material"), "Alpine Mica Schist failed to open.");
        {
            const auto Active = DecodeSurfaceMaterial(Library.QuerySource());
            Confirm(Active.Authoring.has_value(), "Alpine Mica Schist authoring document is missing.");
            const auto Before = Library.QueryApplied()->AuthoringMaps[0].FloatRgba;
            auto Toggled = *Active.Authoring;
            bool DisabledFill = false;
            for (auto &Layer : Toggled.Layers)
                if (Layer.Model == "detail" && Layer.Enabled) { Layer.Enabled = false; DisabledFill = true; break; }
            Confirm(DisabledFill && Library.ConfigureAuthoring(Toggled), "Disabling rock Fill layer failed.");
            const auto &After = Library.QueryApplied()->AuthoringMaps[0].FloatRgba;
            Confirm(Before != After, "Toggling a Fill layer did not update compiled AuthoringMaps.");
        }

        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
