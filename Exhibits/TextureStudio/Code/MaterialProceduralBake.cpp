/*============================================================================================================================================
                                                          MATERIALPROCEDURALBAKE.CPP
============================================================================================================================================*/
// 🧩 Offline baker for procedural material pilots: evaluate the CPU kernels from each entry's parsed SourceRecipe
// and write the shared ORM/normal PNGs the preview samples. Regenerate with:
// MaterialProceduralBake [ContentDirectory]; the validation test fails closed when committed bytes drift.

#include "Engine/ContentInterchange/MaterialLibrary.h"
#include "Engine/Render/MaterialProceduralBaking.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <set>

using namespace Frontier;

namespace
{
double Scalar(const MaterialRecipeParameters &Recipe, const char *Key, double Fallback = 0)
{
    const auto Value = Recipe.Scalars.find(Key);
    return Value != Recipe.Scalars.end() ? Value->second : Fallback;
}

void WritePng(const std::filesystem::path &Path, const std::vector<uint8_t> &Rgba, uint32_t Extent)
{
    const auto Encoded = EncodeProceduralPng(Rgba, Extent);
    std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
    Output.write(reinterpret_cast<const char *>(Encoded.data()), Encoded.size());
    if (!Output) throw std::runtime_error("Procedural PNG write failed.");
}

std::string Text(const MaterialRecipeParameters &Recipe, const char *Key, const std::string &Fallback = "")
{
    const auto Value = Recipe.Texts.find(Key);
    return Value != Recipe.Texts.end() ? Value->second : Fallback;
}

void DecodeRecipeColor(const MaterialRecipeParameters &Recipe, const char *Key, float Rgb[3], const std::string &Stem)
{
    if (!DecodeHexLinearRgb(Text(Recipe, Key), Rgb))
        throw std::runtime_error("Procedural bake needs a valid " + std::string(Key) + " for " + Stem + ".");
}

int WeavePatternIndex(const std::string &Pattern, const std::string &Stem)
{
    if (Pattern == "plain") return 0;
    if (Pattern == "twill") return 1;
    if (Pattern == "satin") return 2;
    if (Pattern == "basket") return 3;
    if (Pattern == "rib") return 4;
    if (Pattern == "herringbone") return 5;
    if (Pattern == "oxford") return 6;
    if (Pattern == "houndstooth") return 7;
    if (Pattern == "denim") return 8;
    throw std::runtime_error("Unknown weave pattern for " + Stem + ".");
}

bool BakeWeave(const MaterialCatalogueEntry &Entry, const std::filesystem::path &Textures)
{
    const auto &Recipe = Entry.Recipe;
    const auto Stem = Entry.RelativePath.stem().string();
    WeaveFieldParameters Parameters;
    Parameters.Type = int(Scalar(Recipe, "type", 4));
    Parameters.Weave = WeavePatternIndex(Text(Recipe, "weavePattern", "twill"), Stem);
    Parameters.FabricMode = int(Scalar(Recipe, "fabricMode"));
    Parameters.Scale = float(Scalar(Recipe, "detailScale"));
    Parameters.AngleRadians = float(Scalar(Recipe, "weaveAngle") * 3.141592653589793 / 180);
    Parameters.Relief = float(Scalar(Recipe, "weaveRelief"));
    Parameters.FiberDetail = float(Scalar(Recipe, "fiberDetail", 12));
    Parameters.Fuzz = float(Scalar(Recipe, "fuzz"));
    Parameters.Slub = float(Scalar(Recipe, "slub"));
    Parameters.DenimFade = float(Scalar(Recipe, "denimFade"));
    Parameters.Grain = float(Scalar(Recipe, "grain"));
    Parameters.Seed = float(Scalar(Recipe, "surfaceSeed"));
    DecodeRecipeColor(Recipe, "warpColor", Parameters.Warp, Stem);
    DecodeRecipeColor(Recipe, "weftColor", Parameters.Weft, Stem);
    if (!DecodeHexLinearRgb(Text(Recipe, "secondaryColor"), Parameters.Secondary))
        Parameters.Secondary[0] = Parameters.Secondary[1] = Parameters.Secondary[2] = 0;
    if (!(Parameters.Scale > 0)) return false;
    constexpr uint32_t Extent = 512;
    auto Maps = BakeWeaveMaps(Parameters, Extent);
    const float Amplitude = WeaveReliefAmplitude(Parameters);
    if (Amplitude > 0)
        for (float &Height : Maps.Relief) Height /= Amplitude;
    WritePng(Textures / (Stem + "-albedo.png"), ConvertLinearAlbedoRgba(Maps.Albedo, Extent), Extent);
    WritePng(Textures / (Stem + "-normal.png"), ConvertHeightToNormalRgba(Maps.Relief, Extent, 2.f), Extent);
    std::cout << "baked " << Stem << " (weave, seed " << Parameters.Seed << ")\n";
    return true;
}

bool BakeKnit(const MaterialCatalogueEntry &Entry, const std::filesystem::path &Textures)
{
    const auto &Recipe = Entry.Recipe;
    const auto Stem = Entry.RelativePath.stem().string();
    KnitFieldParameters Parameters;
    Parameters.Scale = float(Scalar(Recipe, "detailScale"));
    Parameters.Grain = float(Scalar(Recipe, "grain"));
    Parameters.Fuzz = float(Scalar(Recipe, "fuzz"));
    Parameters.Seed = float(Scalar(Recipe, "surfaceSeed"));
    Parameters.AngleRadians = float(Scalar(Recipe, "weaveAngle") * 3.141592653589793 / 180);
    DecodeRecipeColor(Recipe, "color", Parameters.Color, Stem);
    if (!DecodeHexLinearRgb(Text(Recipe, "secondaryColor"), Parameters.Secondary))
        Parameters.Secondary[0] = Parameters.Secondary[1] = Parameters.Secondary[2] = 0;
    if (!(Parameters.Scale > 0)) return false;
    constexpr uint32_t Extent = 512;
    auto Maps = BakeKnitMaps(Parameters, Extent);
    const float Amplitude = KnitReliefAmplitude(Parameters);
    if (Amplitude > 0)
        for (float &Height : Maps.Relief) Height /= Amplitude;
    WritePng(Textures / (Stem + "-albedo.png"), ConvertLinearAlbedoRgba(Maps.Albedo, Extent), Extent);
    WritePng(Textures / (Stem + "-normal.png"), ConvertHeightToNormalRgba(Maps.Relief, Extent, 2.f), Extent);
    std::cout << "baked " << Stem << " (knit, seed " << Parameters.Seed << ")\n";
    return true;
}

bool BakeSand(const MaterialCatalogueEntry &Entry, const std::filesystem::path &Textures)
{
    const auto &Recipe = Entry.Recipe;
    const auto Stem = Entry.RelativePath.stem().string();
    if (Recipe.SandBeds.empty() || Recipe.SandBeds.size() > 4)
        throw std::runtime_error("Sand bake needs one to four parsed beds for " + Stem + ".");
    SandFieldParameters Parameters;
    Parameters.Count = int(Recipe.SandBeds.size());
    for (size_t Bed = 0; Bed < Recipe.SandBeds.size(); ++Bed)
    {
        const auto &Source = Recipe.SandBeds[Bed];
        auto &Target = Parameters.Beds[Bed];
        if (!DecodeHexLinearRgb(Source.Color, Target.Color) || !DecodeHexLinearRgb(Source.SecondaryColor, Target.Pale))
            throw std::runtime_error("Sand bake needs valid bed colors for " + Stem + ".");
        Target.Size = float(Source.Size);
        Target.SizeVariation = float(Source.SizeVariation);
        Target.Coverage = float(Source.Coverage);
        Target.Thickness = float(Source.Thickness);
        Target.Roughness = float(Source.Roughness);
        Target.RoughnessVariation = float(Source.RoughnessVariation);
        Target.Specular = float(Source.Specular);
        Target.SpecularVariation = float(Source.SpecularVariation);
        Target.Ior = float(Source.Ior);
        Target.Roundness = float(Source.Roundness);
        Target.Tilt = float(Source.Tilt);
        Target.Relief = float(Source.Relief);
        Target.BedSeed = SandBedSeed(Source.Id);
    }
    Parameters.BandsMode = Text(Recipe, "sandArrangement") == "bands" ? 1 : 0;
    Parameters.Scale = float(Scalar(Recipe, "sandScale", 1));
    Parameters.Seed = float(Scalar(Recipe, "surfaceSeed"));
    Parameters.BandFrequency = float(Scalar(Recipe, "sandBandFrequency", 1.5));
    Parameters.BandWarp = float(Scalar(Recipe, "sandBandWarp", .2));
    Parameters.BandAngleRadians = float(Scalar(Recipe, "sandBandAngle") * 3.141592653589793 / 180);
    DecodeRecipeColor(Recipe, "color", Parameters.BaseColor, Stem);
    if (!(Parameters.Scale > 0)) return false;
    constexpr uint32_t Extent = 512;
    auto Maps = BakeSandMaps(Parameters, Extent);
    const float Amplitude = SandReliefAmplitude(Parameters);
    if (Amplitude > 0)
        for (float &Height : Maps.Relief) Height /= Amplitude;
    WritePng(Textures / (Stem + "-albedo.png"), ConvertLinearAlbedoRgba(Maps.Tint, Extent), Extent);
    WritePng(Textures / (Stem + "-orm.png"), ConvertSandOrmRgba(Maps.Roughness, Extent), Extent);
    WritePng(Textures / (Stem + "-normal.png"), ConvertFacetNormalRgba(Maps.Relief, Maps.Facet, Extent, 2.f), Extent);
    std::cout << "baked " << Stem << " (sand, seed " << Parameters.Seed << ")\n";
    return true;
}

std::map<std::string, double> BakeScalars(const MaterialRecipeParameters &Recipe,
    std::initializer_list<const char *> Keys)
{
    std::map<std::string, double> Params;
    for (const char *Key : Keys) Params[Key] = Scalar(Recipe, Key);
    return Params;
}

std::map<std::string, std::string> BakeTexts(const MaterialRecipeParameters &Recipe,
    std::initializer_list<const char *> Keys, const std::string &Stem)
{
    std::map<std::string, std::string> Texts;
    for (const char *Key : Keys)
    {
        const std::string Value = Text(Recipe, Key);
        if (Value.empty()) throw std::runtime_error("Procedural bake needs " + std::string(Key) + " for " + Stem + ".");
        Texts[Key] = Value;
    }
    return Texts;
}

// 📝 Channel-dispatch bake: the same BakeProceduralChannelRgba path the workspace rebakes through, fed
// from the SourceRecipe exactly as the recipe channels are. Metals stage density 0 (polished default)
// with the Alloy scratch defaults retained for the toggle; the study keeps its direct pilot path below
// so its committed bytes prove the dispatch reproduces the field bake.
bool BakeChannels(const MaterialCatalogueEntry &Entry, const std::filesystem::path &Textures, std::string_view Op,
    std::initializer_list<const char *> Channels)
{
    const auto &Recipe = Entry.Recipe;
    const auto Stem = Entry.RelativePath.stem().string();
    std::map<std::string, double> Params;
    std::map<std::string, std::string> Texts;
    if (Op == "solar")
    {
        Params = BakeScalars(Recipe, {"detailScale", "roughness", "busbarWidth", "surfaceSeed", "panelMode"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor"}, Stem);
    }
    else if (Op == "dimples") Params = BakeScalars(Recipe, {"detailScale", "dimpleDepth", "surfaceSeed"});
    else if (Op == "stucco") Params = BakeScalars(Recipe, {"detailScale", "grain", "surfaceSeed"});
    else if (Op == "clay") Params = BakeScalars(Recipe, {"detailScale", "grain", "moisture", "surfaceSeed"});
    else if (Op == "led")
    {
        Params = BakeScalars(Recipe, {"detailScale", "pixelFill", "lensDome", "packageDepth", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color"}, Stem);
    }
    else if (Op == "fruit")
    {
        Params = BakeScalars(Recipe, {"detailScale", "skinMode", "spotDensity", "colorGradient", "grain", "surfaceSeed", "roughness"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor", "tertiaryColor"}, Stem);
    }
    else if (Op == "metalGrain") Params = BakeScalars(Recipe, {"grain", "detailScale", "surfaceSeed"});
    else if (Op == "polymerGrain")
        Params = BakeScalars(Recipe, {"grain", "detailScale", "wear", "wearSoftness", "wornRoughness",
            "scratchScale", "surfaceSeed", "roughness"});
    else if (Op == "leaf")
    {
        Params = BakeScalars(Recipe, {"detailScale", "cellScale", "cellRelief", "cellLobing", "stomataDensity",
            "veinRelief", "colorGradient", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor", "tertiaryColor"}, Stem);
    }
    else if (Op == "grass")
    {
        Params = BakeScalars(Recipe, {"plantRibs", "cellScale", "cellRelief", "veinRelief", "colorGradient", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor"}, Stem);
    }
    else if (Op == "petal")
    {
        Params = BakeScalars(Recipe, {"cellScale", "cellRelief", "veinRelief", "colorGradient", "spotDensity", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor", "tertiaryColor"}, Stem);
    }
    else if (Op == "stem")
    {
        Params = BakeScalars(Recipe, {"detailScale", "cellScale", "cellRelief", "veinRelief", "colorGradient",
            "spotDensity", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor", "tertiaryColor"}, Stem);
    }
    else if (Op == "cactus")
    {
        Params = BakeScalars(Recipe, {"detailScale", "plantRibs", "cellScale", "cellRelief", "veinRelief",
            "colorGradient", "roughness", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor", "tertiaryColor"}, Stem);
    }
    else if (Op == "leather")
        Params = BakeScalars(Recipe, {"detailScale", "grain", "hideVariant", "hideVariation", "wear",
            "wearSoftness", "wornRoughness", "scratchScale", "surfaceSeed"});
    else if (Op == "crocodile")
    {
        Params = BakeScalars(Recipe, {"detailScale", "grain", "groutWidth", "poreDensity", "hideVariation", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor", "tertiaryColor"}, Stem);
    }
    else if (Op == "wax") Params = BakeScalars(Recipe, {"detailScale", "grain", "surfaceSeed"});
    else if (Op == "skin")
    {
        Params = BakeScalars(Recipe, {"detailScale", "grain", "freckles", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor"}, Stem);
    }
    else if (Op == "paper")
    {
        Params = BakeScalars(Recipe, {"detailScale", "grain", "fiberContrast", "paperRibs", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor"}, Stem);
    }
    else if (Op == "rust")
    {
        Params = BakeScalars(Recipe, {"detailScale", "grain", "oxidation", "roughness", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor"}, Stem);
    }
    else if (Op == "ceramic" || Op == "rubber")
        Params = BakeScalars(Recipe, {"detailScale", "depth", "surfaceSeed"});
    else if (Op == "tiles")
    {
        Params = BakeScalars(Recipe, {"detailScale", "grain", "groutWidth", "tileVariation", "tileStagger",
            "roughness", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor"}, Stem);
    }
    else if (Op == "granite")
    {
        Params = BakeScalars(Recipe, {"detailScale", "grain", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor", "tertiaryColor"}, Stem);
    }
    else if (Op == "marble")
    {
        Params = BakeScalars(Recipe, {"detailScale", "grain", "veinWidth", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor"}, Stem);
    }
    else if (Op == "concrete")
    {
        Params = BakeScalars(Recipe, {"detailScale", "grain", "poreDensity", "wallMode", "surfaceSeed"});
        Texts = BakeTexts(Recipe, {"color", "secondaryColor"}, Stem);
    }
    else if (Op == "scratchedMetal" || Op == "latheMetal")
    {
        Params = BakeScalars(Recipe,
            Op == "scratchedMetal"
                ? std::initializer_list<const char *>{"grain", "detailScale", "roughness", "scratchLength",
                      "scratchWidth", "scratchDepth", "scratchSpread", "scratchBend", "scratchScale", "surfaceSeed"}
                : std::initializer_list<const char *>{"depth", "detailScale", "roughness", "scratchLength",
                      "scratchWidth", "scratchDepth", "scratchSpread", "scratchBend", "scratchScale", "surfaceSeed"});
        Params["scratchDensity"] = 0;
        Params["angle"] = Scalar(Recipe, "scratchAngle");
    }
    else return false;
    constexpr uint32_t Extent = 512;
    for (const char *Channel : Channels)
    {
        const auto Rgba = BakeProceduralChannelRgba(Op, Channel, Params, Texts, Extent);
        std::string Lower(Channel);
        for (char &Byte : Lower) Byte = char(std::tolower((unsigned char)Byte));
        WritePng(Textures / (Stem + "-" + Lower + ".png"), Rgba, Extent);
    }
    std::cout << "baked " << Stem << " (" << Op << ", seed " << Scalar(Recipe, "surfaceSeed") << ")\n";
    return true;
}

bool BakeScratches(const MaterialCatalogueEntry &Entry, const std::filesystem::path &Textures)
{
    // Shared-field pilot: one scratch evaluation feeds height-derived normals plus roughness/occlusion,
    // so every channel answers to the same density/length/width/depth/spread/bend/scale/seed definition.
    ScratchFieldParameters Parameters;
    Parameters.Density = float(Scalar(Entry.Recipe, "scratchDensity"));
    Parameters.Length = float(Scalar(Entry.Recipe, "scratchLength"));
    Parameters.Width = float(Scalar(Entry.Recipe, "scratchWidth"));
    Parameters.Depth = float(Scalar(Entry.Recipe, "scratchDepth"));
    Parameters.Spread = float(Scalar(Entry.Recipe, "scratchSpread"));
    Parameters.Bend = float(Scalar(Entry.Recipe, "scratchBend"));
    Parameters.Scale = float(Scalar(Entry.Recipe, "scratchScale", 1));
    const double Type = Scalar(Entry.Recipe, "type", 29);
    const double Degrees = Type == 29 ? Scalar(Entry.Recipe, "weaveAngle") : Scalar(Entry.Recipe, "scratchAngle");
    Parameters.AngleRadians = float(Degrees * 3.141592653589793 / 180);
    Parameters.Seed = uint32_t(Scalar(Entry.Recipe, "surfaceSeed"));
    if (!(Parameters.Density > 0) || !(Parameters.Depth > 0) || !(Parameters.Scale > 0)) return false;
    constexpr uint32_t Extent = 512;
    auto Maps = BakeScratchMaps(Parameters, Extent);
    for (float &Height : Maps.Height) Height /= Parameters.Depth;
    const auto Normal = ConvertHeightToNormalRgba(Maps.Height, Extent, 2.f);
    const auto Orm = ConvertScratchOrmRgba(Maps.Coverage, Extent, float(Scalar(Entry.Recipe, "roughness", .2)));
    const auto Stem = Entry.RelativePath.stem().string();
    WritePng(Textures / (Stem + "-normal.png"), Normal, Extent);
    WritePng(Textures / (Stem + "-orm.png"), Orm, Extent);
    std::cout << "baked " << Stem << " (scratches, seed " << Parameters.Seed << ")\n";
    return true;
}
}

int main(int ArgumentCount, char **Arguments)
{
    try
    {
        const std::filesystem::path Content = ArgumentCount > 1 ? Arguments[1] : "EngineContent/MaterialArchives/TextureStudio";
        MaterialLibrary Library;
        if (!Library.ConfigureDirectory(Content) || !Library.Refresh()) throw std::runtime_error("Material library refresh failed.");
        const auto Textures = Content / "Textures";
        std::filesystem::create_directories(Textures);
        static const std::set<std::string> WeaveIds{"woven", "carbon", "suede", "velvet", "cotton", "denim",
            "silk", "linen", "wool"};
        size_t Baked = 0;
        for (const auto &Entry : Library.QueryEntries())
        {
            // 📝 Woodland camouflage albedo is the coded field channel, rebaked live by the workspace;
            // baking the weave pair here would ship textures its .material never references.
            if (Entry.RelativePath.stem().string().starts_with("woodland-camouflage-fabric")) continue;
            // 📝 Camouflage ballistic polymer keeps its coded-field albedo; the polymer grain under it
            // is not yet composited, so no polymer bake is shipped for those stems either.
            if (Entry.RelativePath.stem().string().starts_with("camouflage-ballistic-polymer")) continue;
            const auto &RecipeId = Entry.Recipe.RecipeId;
            if (RecipeId == "scratches" && BakeScratches(Entry, Textures)) ++Baked;
            else if (RecipeId == "solar" && BakeChannels(Entry, Textures, "solar", {"Albedo", "Orm", "Normal"})) ++Baked;
            else if (RecipeId == "golf" && BakeChannels(Entry, Textures, "dimples", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "stucco" && BakeChannels(Entry, Textures, "stucco", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "clay" && BakeChannels(Entry, Textures, "clay", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "led" && BakeChannels(Entry, Textures, "led", {"Albedo", "Normal", "Radiance"})) ++Baked;
            else if ((RecipeId == "fruitSurface" || RecipeId == "citrusSurface" || RecipeId == "berrySurface") &&
                BakeChannels(Entry, Textures, "fruit", {"Albedo", "Orm", "Normal"})) ++Baked;
            else if (RecipeId == "leafSurface" && BakeChannels(Entry, Textures, "leaf", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "grassSurface" && BakeChannels(Entry, Textures, "grass", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "petalSurface" && BakeChannels(Entry, Textures, "petal", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "stemSurface" && BakeChannels(Entry, Textures, "stem", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "cactusSurface" && BakeChannels(Entry, Textures, "cactus", {"Albedo", "Orm", "Normal"})) ++Baked;
            else if (RecipeId == "leather" && BakeChannels(Entry, Textures, "leather", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "crocodile" && BakeChannels(Entry, Textures, "crocodile", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "wax" && BakeChannels(Entry, Textures, "wax", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "skin" && BakeChannels(Entry, Textures, "skin", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "paper" && BakeChannels(Entry, Textures, "paper", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "rust" && BakeChannels(Entry, Textures, "rust", {"Albedo", "Orm", "Normal"})) ++Baked;
            else if (RecipeId == "ceramic" && BakeChannels(Entry, Textures, "ceramic", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "rubber" && BakeChannels(Entry, Textures, "rubber", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "tiles" && BakeChannels(Entry, Textures, "tiles", {"Albedo", "Orm", "Normal"})) ++Baked;
            else if (RecipeId == "granite" && BakeChannels(Entry, Textures, "granite", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "marble" && BakeChannels(Entry, Textures, "marble", {"Albedo", "Normal"})) ++Baked;
            else if (RecipeId == "concrete" && BakeChannels(Entry, Textures, "concrete", {"Albedo", "Normal"})) ++Baked;
            else if ((RecipeId == "glossPlastic" || RecipeId == "grainPlastic" || RecipeId == "engineering" ||
                RecipeId == "wornPlastic") && BakeChannels(Entry, Textures, "polymerGrain", {"Albedo", "Orm", "Normal"})) ++Baked;
            // 📝 Bare metals bake the Alloy type-1 tooth; the optional scratch overlay attaches later.
            else if (RecipeId == "bareMetal" && BakeChannels(Entry, Textures, "metalGrain", {"Albedo", "Normal"})) ++Baked;
            else if ((RecipeId == "polishedMetal" || RecipeId == "brushedMetal") &&
                BakeChannels(Entry, Textures, "scratchedMetal", {"Albedo", "Orm", "Normal"}))
                ++Baked;
            else if (RecipeId == "brake" && BakeChannels(Entry, Textures, "latheMetal", {"Albedo", "Orm", "Normal"})) ++Baked;
            else if (WeaveIds.contains(Entry.Recipe.RecipeId) && BakeWeave(Entry, Textures)) ++Baked;
            else if (Entry.Recipe.RecipeId == "jersey" && BakeKnit(Entry, Textures)) ++Baked;
            else if ((Entry.Recipe.RecipeId == "sand" || Entry.Recipe.RecipeId == "layeredSand") &&
                BakeSand(Entry, Textures))
                ++Baked;
        }
        std::cout << "{\"result\":\"passed\",\"baked\":" << Baked << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
