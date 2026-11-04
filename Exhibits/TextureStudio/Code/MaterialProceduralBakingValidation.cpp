/*============================================================================================================================================
                                                      MATERIALPROCEDURALBAKINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Prove the CPU procedural port matches the Alloy kernels: noise parity against an independent double-precision
// reference, deterministic seeded scratch fields shared across channels, correct normal/ORM encoding, parsed
// recipe coverage for every shipped material, and byte-exact regeneration of the committed pilot textures.

#include "Engine/ContentInterchange/MaterialLibrary.h"
#include "Engine/Render/MaterialProceduralBaking.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
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

// Independent double-precision reference for the GLSL hash31/noise3 pair; the engine must track the true
// function within float rounding, not merely agree with itself.
double ReferenceHash31(double X, double Y, double Z)
{
    X = X * .1031; Y = Y * .1031; Z = Z * .1031;
    X -= std::floor(X); Y -= std::floor(Y); Z -= std::floor(Z);
    const double Dot = X * (Y + 33.33) + Y * (Z + 33.33) + Z * (X + 33.33);
    X += Dot; Y += Dot; Z += Dot;
    const double Hash = (X + Y) * Z;
    return Hash - std::floor(Hash);
}

double ReferenceNoise3(double X, double Y, double Z)
{
    const double Ix = std::floor(X), Iy = std::floor(Y), Iz = std::floor(Z);
    double Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
    Fx = Fx * Fx * (3 - 2 * Fx); Fy = Fy * Fy * (3 - 2 * Fy); Fz = Fz * Fz * (3 - 2 * Fz);
    const auto Mix = [](double A, double B, double T) { return A + (B - A) * T; };
    const double X00 = Mix(ReferenceHash31(Ix, Iy, Iz), ReferenceHash31(Ix + 1, Iy, Iz), Fx);
    const double X10 = Mix(ReferenceHash31(Ix, Iy + 1, Iz), ReferenceHash31(Ix + 1, Iy + 1, Iz), Fx);
    const double X01 = Mix(ReferenceHash31(Ix, Iy, Iz + 1), ReferenceHash31(Ix + 1, Iy, Iz + 1), Fx);
    const double X11 = Mix(ReferenceHash31(Ix, Iy + 1, Iz + 1), ReferenceHash31(Ix + 1, Iy + 1, Iz + 1), Fx);
    return Mix(Mix(X00, X10, Fy), Mix(X01, X11, Fy), Fz);
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path &Path)
{
    std::ifstream Input(Path, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes <= 0) throw std::runtime_error("Committed procedural texture is missing.");
    std::vector<uint8_t> Data(size_t(Bytes), uint8_t(0));
    Input.seekg(0);
    Input.read(reinterpret_cast<char *>(Data.data()), Bytes);
    return Data;
}

double Scalar(const MaterialRecipeParameters &Recipe, const char *Key)
{
    const auto Value = Recipe.Scalars.find(Key);
    if (Value == Recipe.Scalars.end()) throw std::runtime_error("Parsed recipe is missing a pilot uniform.");
    return Value->second;
}

double OptionalScalar(const MaterialRecipeParameters &Recipe, const char *Key, double Fallback)
{
    const auto Value = Recipe.Scalars.find(Key);
    return Value != Recipe.Scalars.end() ? Value->second : Fallback;
}

std::string RecipeText(const MaterialRecipeParameters &Recipe, const char *Key, const std::string &Fallback = "")
{
    const auto Value = Recipe.Texts.find(Key);
    return Value != Recipe.Texts.end() ? Value->second : Fallback;
}

int PatternIndex(const std::string &Pattern)
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
    throw std::runtime_error("Unknown weave pattern in validation.");
}

// 📝 Cell-center warp/weft dominance: 'R' where the baked albedo leans warp, 'B' where it leans weft.
// Patterns resolve through color selection alone, so the asserts never touch float equality.
char WeaveCell(int Weave, int I, int J)
{
    WeaveFieldParameters P;
    P.Type = 4;
    P.Weave = Weave;
    P.Scale = 38;
    P.Relief = .65f;
    P.FiberDetail = 12;
    P.Warp[0] = 1;
    P.Warp[1] = P.Warp[2] = 0;
    P.Weft[2] = 1;
    P.Weft[0] = P.Weft[1] = 0;
    const auto Sample = EvaluateWeaveField((float(I) + .5f) / 38, (float(J) + .5f) / 38, P, 38.f / 512);
    return Sample.Albedo[0] > Sample.Albedo[2] ? 'R' : 'B';
}
}

int main()
{
    try
    {
        // Noise parity: the double reference proves the formula at small magnitude; past that, hash31 chaos
        // amplifies float32 rounding on the CPU exactly as highp does on the GPU, so large coordinates assert
        // range, determinism and avalanche instead of false-precision equality with infinite precision.
        for (const auto &[X, Y, Z] : {std::array<double, 3>{0, 0, 0}, {1, 2, 3}, {.3, .7, .1}})
            Confirm(std::abs(double(ProceduralHash31(float(X), float(Y), float(Z))) - ReferenceHash31(X, Y, Z)) < 2e-4,
                "Procedural hash31 drifted from the Alloy kernel.");
        for (const auto &[X, Y, Z] :
            {std::array<double, 3>{-4.5, 17.25, 113.7}, {81.2, -31.1, 3}, {5.25, -3.75, 11.125}, {220, 220, 220}})
        {
            const float Hash = ProceduralHash31(float(X), float(Y), float(Z));
            Confirm(Hash >= 0 && Hash < 1, "Procedural hash31 escaped its unit range.");
            Confirm(Hash == ProceduralHash31(float(X), float(Y), float(Z)), "Procedural hash31 is not deterministic.");
        }
        Confirm(ProceduralHash31(220, 220, 220) != ProceduralHash31(221, 220, 220), "Procedural hash31 fails to avalanche.");
        for (const auto &[X, Y, Z] : {std::array<double, 3>{0, 0, 0}, {.3, .7, .1}, {1, 2, 3}})
            Confirm(std::abs(double(ProceduralNoise3(float(X), float(Y), float(Z))) - ReferenceNoise3(X, Y, Z)) < 2e-4,
                "Procedural noise3 drifted from the Alloy kernel.");
        for (const auto &[X, Y, Z] : {std::array<double, 3>{5.25, -3.75, 11.125}, {220, 220, 220}})
        {
            const double Engine = ProceduralNoise3(float(X), float(Y), float(Z));
            Confirm(Engine >= 0 && Engine < 1, "Procedural noise3 escaped its unit range.");
        }
        Confirm(ProceduralNoise3(1, 2, 3) == ProceduralNoise3(1, 2, 3), "Procedural noise3 is not deterministic.");
        const float Fbm = ProceduralStoneFbm(3.5f, -1.25f, 7);
        Confirm(Fbm >= 0 && Fbm < 1, "Procedural stone fbm escaped its unit range.");
        Confirm(Fbm == ProceduralStoneFbm(3.5f, -1.25f, 7), "Procedural stone fbm is not deterministic.");
        // Scratch field: empty without density, deterministic per seed, sensitive across seeds.
        ScratchFieldParameters Empty;
        const auto Bare = EvaluateScratchField(.3f, .6f, Empty, .01f);
        Confirm(Bare.Coverage == 0 && Bare.Height == 0 && Bare.Rim == 0, "Empty scratch field is not bare.");
        ScratchFieldParameters Pilot{6, 1.15f, .01f, .0018f, .8f, .42f, 6, float(25 * 3.141592653589793 / 180), 17};
        const auto First = EvaluateScratchField(.3f, .6f, Pilot, 6.f / 512);
        Confirm(First.Coverage == EvaluateScratchField(.3f, .6f, Pilot, 6.f / 512).Coverage, "Scratch field is not deterministic.");
        ScratchFieldParameters Reseeded = Pilot;
        Reseeded.Seed = 18;
        const auto Altered = BakeScratchMaps(Reseeded, 32);
        const auto Sealed = BakeScratchMaps(Pilot, 32);
        bool SeedSensitive = Altered.Height.size() == Sealed.Height.size();
        for (size_t Pixel = 0; SeedSensitive && Pixel < Sealed.Height.size(); ++Pixel)
            SeedSensitive = Altered.Height[Pixel] == Sealed.Height[Pixel] && Altered.Coverage[Pixel] == Sealed.Coverage[Pixel];
        Confirm(!SeedSensitive, "Scratch field ignores its seed.");
        // Groove assertions resolve at 256: coarser bakes anti-alias sub-texel cuts away, exactly as the
        // shader fades them with distance.
        const auto Maps = BakeScratchMaps(Pilot, 256);
        Confirm(Maps.Extent == 256 && Maps.Height.size() == size_t(256) * 256 && Maps.Coverage.size() == Maps.Height.size(),
            "Scratch bake shape changed.");
        float MinimumHeight = 0, MaximumCoverage = 0;
        bool Shared = true, Groove = false;
        for (size_t Pixel = 0; Pixel < Maps.Height.size(); ++Pixel)
        {
            MinimumHeight = std::min(MinimumHeight, Maps.Height[Pixel]);
            MaximumCoverage = std::max(MaximumCoverage, Maps.Coverage[Pixel]);
            Confirm(Maps.Coverage[Pixel] >= 0 && Maps.Coverage[Pixel] <= 1, "Scratch coverage escaped its mask range.");
            Confirm(Maps.Height[Pixel] <= Pilot.Depth * .17f, "Scratch rim exceeds the kernel lip budget.");
            // Shared-definition property: every groove pixel carries mask, so roughness and occlusion answer
            // to the same cuts as the height-derived normals.
            if (Maps.Height[Pixel] < -Pilot.Depth * .1f)
            {
                Groove = true;
                Shared = Shared && Maps.Coverage[Pixel] > 0;
            }
        }
        Confirm(Groove && MinimumHeight < -Pilot.Depth * .5f, "Scratch bake holds no groove.");
        Confirm(MaximumCoverage > .5f, "Scratch bake holds no cut core.");
        Confirm(Shared, "Scratch groove escapes its shared mask.");
        // 📝 The bake-wide instance memo must be bit-identical to resolving every instance per pixel:
        // compare BakeScratchMaps against the public per-pixel entry point over varied fields,
        // including degenerate zero-width and bend-heavy parameters.
        for (const ScratchFieldParameters Field : {Pilot,
            ScratchFieldParameters{11.7f, .4f, 0.f, .004f, 1.9f, 2.5f, 3, -.7f, 99},
            ScratchFieldParameters{1.2f, 2.5f, .05f, .0005f, 0.f, 0.f, 8, 0.f, 0}})
        {
            constexpr uint32_t ParityExtent = 128;
            const auto Memoized = BakeScratchMaps(Field, ParityExtent);
            const float ParityPixelUv = Field.Scale / float(ParityExtent);
            for (uint32_t Y = 0; Y < ParityExtent; ++Y)
                for (uint32_t X = 0; X < ParityExtent; ++X)
                {
                    const auto Reference = EvaluateScratchField((float(X) + .5f) / float(ParityExtent),
                        (float(Y) + .5f) / float(ParityExtent), Field, ParityPixelUv);
                    const size_t Pixel = size_t(Y) * ParityExtent + X;
                    Confirm(Memoized.Height[Pixel] == Reference.Height && Memoized.Coverage[Pixel] == Reference.Coverage,
                        "Memoized scratch bake diverged from the per-pixel reference.");
                }
        }
        // 📝 Performance evidence (reported, not asserted): a 512px density-6 field is the interactive
        // rebake the user waits on; keep it visible so regressions are obvious in the test log.
        {
            const auto Started = std::chrono::steady_clock::now();
            const auto Timed = BakeScratchMaps(Pilot, 512);
            const auto Elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - Started).count();
            Confirm(Timed.Extent == 512, "Timed scratch bake extent changed.");
            std::cout << "Scratch bake 512px density 6: " << Elapsed << " ms\n";
        }
        bool Rejected = false;
        try { (void)BakeScratchMaps(Pilot, 0); }
        catch (const std::exception &) { Rejected = true; }
        Confirm(Rejected, "Scratch bake accepted a zero extent.");
        // Normal encoding: flat relief points up, ramps tilt against the gradient, grooves dent inward.
        const std::vector<float> Flat(16 * 16, 0);
        const auto FlatNormal = ConvertHeightToNormalRgba(Flat, 16, 2);
        Confirm(FlatNormal[0] == 128 && FlatNormal[1] == 128 && FlatNormal[2] == 255 && FlatNormal[3] == 255,
            "Flat relief normal is not straight up.");
        std::vector<float> Ramp(16 * 16, 0);
        for (uint32_t Y = 0; Y < 16; ++Y)
            for (uint32_t X = 0; X < 16; ++X) Ramp[size_t(Y) * 16 + X] = float(X) / 15;
        const auto RampNormal = ConvertHeightToNormalRgba(Ramp, 16, 2);
        Confirm(RampNormal[(size_t(8) * 16 + 8) * 4] < 128, "Uphill ramp normal tilts the wrong way.");
        std::vector<float> Relief = Maps.Height;
        for (float &Height : Relief) Height /= Pilot.Depth;
        const auto GrooveNormal = ConvertHeightToNormalRgba(Relief, 256, 2);
        size_t Dented = 0;
        for (size_t Pixel = 2; Pixel < GrooveNormal.size(); Pixel += 4) Dented += GrooveNormal[Pixel] < 250 ? 1 : 0;
        Confirm(Dented > 100, "Baked grooves leave no normal dent.");
        // ORM encoding: field pixels preserve the base through the ceiling ratio, cuts reach the ceiling.
        const std::vector<float> BareMask(8 * 8, 0), CutMask(8 * 8, 1);
        const auto FieldOrm = ConvertScratchOrmRgba(BareMask, 8, .23f);
        Confirm(FieldOrm[0] == 255 && FieldOrm[1] == uint8_t(std::lround(.23f / .53f * 255)) && FieldOrm[2] == 255,
            "ORM field pixel does not preserve the authored base.");
        const auto CutOrm = ConvertScratchOrmRgba(CutMask, 8, .23f);
        Confirm(CutOrm[0] == uint8_t(std::lround(.89f * 255)) && CutOrm[1] == 255 && CutOrm[2] == 255,
            "ORM cut pixel misses the shader ceiling.");
        Confirm(FieldOrm[1] * .53f / 255 < .24f && FieldOrm[1] * .53f / 255 > .22f, "ORM field ratio drifts from the base.");
        // Grain ops: registry shape, Alloy formula parity, determinism, seed sensitivity, and the wear-disabled
        // passthrough that keeps unworn polymer out of the worn-roughness branch.
        // 📝 31 Alloy ops + 8 coded fields (HTML FieldSpecification registry).
        Confirm(QueryProceduralOps().size() == 39, "Procedural op registry changed.");
        Confirm(QueryProceduralOpChannels("camouflage") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpTextParams("camouflage-smooth").size() == 4 &&
                QueryProceduralOpTextParams("noise").empty(),
            "Coded-field op registration changed.");
        Confirm(QueryProceduralOpChannels("metalGrain") == std::vector<std::string>{"Albedo", "Normal"},
            "Bare-metal op channel set changed.");
        Confirm(QueryProceduralOpChannels("polymerGrain").size() == 3, "Polymer op channel set changed.");
        Confirm(QueryProceduralOpChannels("solar") == std::vector<std::string>{"Albedo", "Orm", "Normal"} &&
                QueryProceduralOpChannels("scratchedMetal") == std::vector<std::string>{"Albedo", "Orm", "Normal"} &&
                QueryProceduralOpChannels("latheMetal") == std::vector<std::string>{"Albedo", "Orm", "Normal"} &&
                QueryProceduralOpChannels("dimples") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("stucco") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("weave") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("knit") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("clay") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("led") == std::vector<std::string>{"Albedo", "Normal", "Radiance"} &&
                QueryProceduralOpChannels("fruit") == std::vector<std::string>{"Albedo", "Orm", "Normal"},
            "Disputed-family op channel set changed.");
        Confirm(QueryProceduralOpChannels("cactus") == std::vector<std::string>{"Albedo", "Orm", "Normal"} &&
                QueryProceduralOpChannels("rust") == std::vector<std::string>{"Albedo", "Orm", "Normal"} &&
                QueryProceduralOpChannels("tiles") == std::vector<std::string>{"Albedo", "Orm", "Normal"} &&
                QueryProceduralOpChannels("leaf") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("grass") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("petal") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("stem") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("leather") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("crocodile") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("wax") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("skin") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("paper") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("ceramic") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("rubber") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("granite") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("marble") == std::vector<std::string>{"Albedo", "Normal"} &&
                QueryProceduralOpChannels("concrete") == std::vector<std::string>{"Albedo", "Normal"},
            "Botanical/extended op channel set changed.");
        Confirm(QueryProceduralOpCeilingLift("scratches") == .3f && QueryProceduralOpCeilingLift("polymerGrain") == .096f &&
                QueryProceduralOpCeilingLift("metalGrain") == 0 && QueryProceduralOpCeilingLift("unknown") == 0,
            "ORM ceiling lift changed.");
        Confirm(QueryProceduralOpCeilingLift("solar") == .02f && QueryProceduralOpCeilingLift("scratchedMetal") == .3f &&
                QueryProceduralOpCeilingLift("latheMetal") == .3f && QueryProceduralOpCeilingLift("dimples") == 0 &&
                QueryProceduralOpCeilingLift("stucco") == 0 && QueryProceduralOpCeilingLift("weave") == 0 &&
                QueryProceduralOpCeilingLift("knit") == 0 && QueryProceduralOpCeilingLift("clay") == 0 &&
                QueryProceduralOpCeilingLift("led") == 0 && QueryProceduralOpCeilingLift("fruit") == .24f &&
                QueryProceduralOpCeilingLift("cactus") == .24f && QueryProceduralOpCeilingLift("rust") == .58f &&
                QueryProceduralOpCeilingLift("tiles") == .73f,
            "Disputed-family ORM ceiling lift changed.");
        Confirm(ResolveProceduralParamKey("scratchedMetal", "scratchAngle") == "angle" &&
                ResolveProceduralParamKey("latheMetal", "scratchAngle") == "angle" &&
                ResolveProceduralParamKey("scratches", "weaveAngle") == "angle" &&
                ResolveProceduralParamKey("weave", "weaveAngle") == "weaveAngle",
            "Scratch direction alias changed.");
        bool RejectedGrain = false;
        try { (void)BakeProceduralChannelRgba("metalGrain", "Orm", {}, 32); }
        catch (const std::exception &) { RejectedGrain = true; }
        Confirm(RejectedGrain, "Bare-metal op baked a channel its shader never varies.");
        const std::map<std::string, double> Metal{{"grain", .5}, {"detailScale", 8}, {"surfaceSeed", 17}};
        const auto MetalAlbedo = BakeProceduralChannelRgba("metalGrain", "Albedo", Metal, 32);
        const double MetalSpot = .92 + .1 * ReferenceNoise3(.5 / 32 * 8, 17, .5 / 32 * 8);
        Confirm(std::abs(MetalAlbedo[0] / 255. - MetalSpot) < 2. / 255, "Bare-metal albedo drifted from the Alloy type-1 term.");
        Confirm(MetalAlbedo == BakeProceduralChannelRgba("metalGrain", "Albedo", Metal, 32), "Grain bake is not deterministic.");
        const std::map<std::string, double> ReseededMetal{{"grain", .5}, {"detailScale", 8}, {"surfaceSeed", 18}};
        Confirm(MetalAlbedo != BakeProceduralChannelRgba("metalGrain", "Albedo", ReseededMetal, 32), "Grain bake ignores its seed.");
        const std::map<std::string, double> Polymer{{"grain", .6}, {"detailScale", 8}, {"wear", 0}, {"wearSoftness", .65},
            {"wornRoughness", .24}, {"scratchScale", 32}, {"surfaceSeed", 17}, {"roughness", .5}};
        const auto PolymerOrm = BakeProceduralChannelRgba("polymerGrain", "Orm", Polymer, 32);
        uint8_t MinimumGreen = 255, MaximumGreen = 0;
        double GreenSum = 0;
        for (size_t Pixel = 0; Pixel < PolymerOrm.size(); Pixel += 4)
        {
            Confirm(PolymerOrm[Pixel] == 255 && PolymerOrm[Pixel + 2] == 255 && PolymerOrm[Pixel + 3] == 255,
                "Polymer ORM carries occlusion or metal it never bakes.");
            MinimumGreen = std::min(MinimumGreen, PolymerOrm[Pixel + 1]);
            MaximumGreen = std::max(MaximumGreen, PolymerOrm[Pixel + 1]);
            GreenSum += PolymerOrm[Pixel + 1];
        }
        const double GreenMean = GreenSum / (PolymerOrm.size() / 4);
        Confirm(MinimumGreen >= 190 && MaximumGreen - MinimumGreen > 4 && GreenMean > 225 && GreenMean < 241,
            "Polymer ORM grain escapes its ceiling or drifts from the base ratio.");
        const auto PolymerAlbedo = BakeProceduralChannelRgba("polymerGrain", "Albedo", Polymer, 32);
        uint8_t MinimumGray = 255, MaximumGray = 0;
        for (size_t Pixel = 0; Pixel < PolymerAlbedo.size(); Pixel += 4)
            MinimumGray = std::min(MinimumGray, PolymerAlbedo[Pixel]), MaximumGray = std::max(MaximumGray, PolymerAlbedo[Pixel]);
        Confirm(MinimumGray >= 225 && MaximumGray == 255 && MinimumGray < MaximumGray, "Polymer albedo modulator range changed.");
        const std::map<std::string, double> BarePolymer{{"grain", 0}, {"detailScale", 8}, {"wear", 0}, {"surfaceSeed", 17}};
        const auto BarePolymerNormal = BakeProceduralChannelRgba("polymerGrain", "Normal", BarePolymer, 32);
        bool BareUp = true;
        for (size_t Pixel = 0; Pixel < BarePolymerNormal.size(); Pixel += 4)
            BareUp = BareUp && BarePolymerNormal[Pixel] == 128 && BarePolymerNormal[Pixel + 1] == 128 &&
                BarePolymerNormal[Pixel + 2] == 255;
        Confirm(BareUp, "Grain-free polymer relief tilts.");
        // Solar cells: separators, silicon, busbars and fingers resolve; two texels pin the kernel wiring
        // exactly (a busbar core reads full contact, a separator edge reads none).
        const std::map<std::string, double> MonoSolar{{"detailScale", 3}, {"roughness", .2}, {"busbarWidth", .016},
            {"surfaceSeed", 17}, {"panelMode", 0}};
        const std::map<std::string, std::string> MonoTexts{{"color", "#172742"}, {"secondaryColor", "#bec6c4"}};
        const auto SolarAlbedo = BakeProceduralChannelRgba("solar", "Albedo", MonoSolar, MonoTexts, 96);
        Confirm(SolarAlbedo == BakeProceduralChannelRgba("solar", "Albedo", MonoSolar, MonoTexts, 96),
            "Solar bake is not deterministic.");
        bool Separator = false, Silicon = false, Contact = false;
        for (size_t Pixel = 0; Pixel < SolarAlbedo.size(); Pixel += 4)
        {
            Separator = Separator || (SolarAlbedo[Pixel] < 30 && SolarAlbedo[Pixel + 1] < 30 && SolarAlbedo[Pixel + 2] < 30);
            Silicon = Silicon || (std::abs(int(SolarAlbedo[Pixel]) - 45) < 9 && std::abs(int(SolarAlbedo[Pixel + 1]) - 56) < 9 &&
                std::abs(int(SolarAlbedo[Pixel + 2]) - 76) < 9);
            Contact = Contact || (SolarAlbedo[Pixel] > 150 && SolarAlbedo[Pixel + 1] > 150 && SolarAlbedo[Pixel + 2] > 150);
        }
        Confirm(Separator && Silicon && Contact, "Solar cell palette lost a tone.");
        const auto SolarOrm = BakeProceduralChannelRgba("solar", "Orm", MonoSolar, MonoTexts, 96);
        const size_t BusPixel = (size_t(48) * 96 + 48) * 4;
        Confirm(SolarOrm[BusPixel] == 255 && SolarOrm[BusPixel + 1] == 255 && SolarOrm[BusPixel + 2] == 245,
            "Solar busbar core misses full contact.");
        const auto SolarGapOrm = BakeProceduralChannelRgba("solar", "Orm", MonoSolar, MonoTexts, 192);
        const size_t GapPixel = (size_t(96) * 192 + 63) * 4;
        Confirm(SolarGapOrm[GapPixel] == 255 && SolarGapOrm[GapPixel + 1] == 232 && SolarGapOrm[GapPixel + 2] == 64,
            "Solar separator gap misses the field ratio.");
        uint8_t SolarGreenMin = 255, SolarGreenMax = 0, SolarBlueMin = 255, SolarBlueMax = 0;
        for (size_t Pixel = 0; Pixel < SolarOrm.size(); Pixel += 4)
        {
            Confirm(SolarOrm[Pixel] == 255 && SolarOrm[Pixel + 3] == 255, "Solar ORM carries occlusion it never bakes.");
            SolarGreenMin = std::min(SolarGreenMin, SolarOrm[Pixel + 1]);
            SolarGreenMax = std::max(SolarGreenMax, SolarOrm[Pixel + 1]);
            SolarBlueMin = std::min(SolarBlueMin, SolarOrm[Pixel + 2]);
            SolarBlueMax = std::max(SolarBlueMax, SolarOrm[Pixel + 2]);
        }
        Confirm(SolarGreenMin < 236 && SolarGreenMax > 250 && SolarBlueMin < 70 && SolarBlueMax > 240,
            "Solar ORM escapes the contact mix.");
        const auto SolarNormal = BakeProceduralChannelRgba("solar", "Normal", MonoSolar, MonoTexts, 96);
        size_t SolarDented = 0;
        for (size_t Pixel = 2; Pixel < SolarNormal.size(); Pixel += 4) SolarDented += SolarNormal[Pixel] < 250 ? 1 : 0;
        Confirm(SolarDented > 50, "Solar cell steps leave no normal dent.");
        const std::map<std::string, double> PolySolar{{"detailScale", 3}, {"roughness", .2}, {"busbarWidth", .016},
            {"surfaceSeed", 17}, {"panelMode", 1}};
        const std::map<std::string, std::string> PolyTexts{{"color", "#253f83"}, {"secondaryColor", "#bec6c4"}};
        Confirm(BakeProceduralChannelRgba("solar", "Albedo", PolySolar, PolyTexts, 64) !=
                BakeProceduralChannelRgba("solar", "Albedo", MonoSolar, MonoTexts, 64),
            "Polycrystalline grain matches mono.");
        const std::map<std::string, double> BlackSolar{{"detailScale", 3}, {"roughness", .2}, {"busbarWidth", .016},
            {"surfaceSeed", 17}, {"panelMode", 2}};
        Confirm(BakeProceduralChannelRgba("solar", "Orm", BlackSolar, MonoTexts, 64) ==
                BakeProceduralChannelRgba("solar", "Orm", MonoSolar, MonoTexts, 64),
            "Black module ORM differs from mono geometry.");
        bool RejectedSolar = false;
        try { (void)BakeProceduralChannelRgba("solar", "Albedo", {}, MonoTexts, 32); }
        catch (const std::exception &) { RejectedSolar = true; }
        Confirm(RejectedSolar, "Solar bake accepted an empty scale.");
        // Dimples: staggered bowls dent the normals, the cover grain modulates the albedo, guards hold.
        const std::map<std::string, double> Tour{{"detailScale", 7}, {"dimpleDepth", .012}, {"surfaceSeed", 17}};
        const auto GolfNormal = BakeProceduralChannelRgba("dimples", "Normal", Tour, {}, 128);
        size_t Bowls = 0;
        for (size_t Pixel = 2; Pixel < GolfNormal.size(); Pixel += 4) Bowls += GolfNormal[Pixel] < 250 ? 1 : 0;
        Confirm(Bowls > 100, "Dimple bowls leave no normal dent.");
        const auto GolfAlbedo = BakeProceduralChannelRgba("dimples", "Albedo", Tour, {}, 64);
        double GolfSum = 0;
        for (size_t Pixel = 0; Pixel < GolfAlbedo.size(); Pixel += 4)
        {
            Confirm(GolfAlbedo[Pixel] == GolfAlbedo[Pixel + 1] && GolfAlbedo[Pixel + 1] == GolfAlbedo[Pixel + 2],
                "Dimple albedo modulator carries hue.");
            GolfSum += GolfAlbedo[Pixel];
        }
        const double GolfMean = GolfSum / (GolfAlbedo.size() / 4);
        Confirm(GolfMean > 248 && GolfMean < 255 && GolfAlbedo == BakeProceduralChannelRgba("dimples", "Albedo", Tour, {}, 64),
            "Dimple cover grain drifted or wobbles.");
        bool RejectedDimples = false;
        try { (void)BakeProceduralChannelRgba("dimples", "Normal", {{"detailScale", 7}}, {}, 32); }
        catch (const std::exception &) { RejectedDimples = true; }
        Confirm(RejectedDimples, "Dimple bake accepted an empty depth.");
        // Stucco: aggregate over cloud drift stays near-white, relief dents, seeds separate.
        const std::map<std::string, double> Lime{{"detailScale", 48}, {"grain", .85}, {"surfaceSeed", 17}};
        const auto StuccoAlbedo = BakeProceduralChannelRgba("stucco", "Albedo", Lime, {}, 64);
        double StuccoSum = 0;
        uint8_t StuccoMin = 255, StuccoMax = 0;
        for (size_t Pixel = 0; Pixel < StuccoAlbedo.size(); Pixel += 4)
        {
            StuccoMin = std::min(StuccoMin, StuccoAlbedo[Pixel]);
            StuccoMax = std::max(StuccoMax, StuccoAlbedo[Pixel]);
            StuccoSum += StuccoAlbedo[Pixel];
        }
        const double StuccoMean = StuccoSum / (StuccoAlbedo.size() / 4);
        Confirm(StuccoMin > 200 && StuccoMax > 250 && StuccoMean > 240 && StuccoMean < 258,
            "Stucco modulator escapes its aggregate range.");
        const auto StuccoNormal = BakeProceduralChannelRgba("stucco", "Normal", Lime, {}, 128);
        size_t StuccoDented = 0;
        for (size_t Pixel = 2; Pixel < StuccoNormal.size(); Pixel += 4) StuccoDented += StuccoNormal[Pixel] < 250 ? 1 : 0;
        Confirm(StuccoDented > 100, "Stucco aggregate leaves no normal dent.");
        const std::map<std::string, double> ReseededLime{{"detailScale", 48}, {"grain", .85}, {"surfaceSeed", 18}};
        Confirm(StuccoAlbedo != BakeProceduralChannelRgba("stucco", "Albedo", ReseededLime, {}, 64),
            "Stucco bake ignores its seed.");
        // Clay: aggregate over hand variation stays bright, relief dents, moisture damps relief and tone.
        const std::map<std::string, double> Terracotta{
            {"detailScale", 24}, {"grain", .8}, {"moisture", .04}, {"surfaceSeed", 17}};
        const auto ClayAlbedo = BakeProceduralChannelRgba("clay", "Albedo", Terracotta, {}, 64);
        double ClaySum = 0;
        uint8_t ClayMin = 255, ClayMax = 0;
        for (size_t Pixel = 0; Pixel < ClayAlbedo.size(); Pixel += 4)
        {
            ClayMin = std::min(ClayMin, ClayAlbedo[Pixel]);
            ClayMax = std::max(ClayMax, ClayAlbedo[Pixel]);
            ClaySum += ClayAlbedo[Pixel];
        }
        const double ClayMean = ClaySum / (ClayAlbedo.size() / 4);
        Confirm(ClayMin > 190 && ClayMax > 250 && ClayMean > 235 && ClayMean < 258,
            "Clay modulator escapes its aggregate range.");
        const auto ClayNormal = BakeProceduralChannelRgba("clay", "Normal", Terracotta, {}, 128);
        size_t ClayDented = 0;
        for (size_t Pixel = 2; Pixel < ClayNormal.size(); Pixel += 4) ClayDented += ClayNormal[Pixel] < 250 ? 1 : 0;
        Confirm(ClayDented > 100, "Clay aggregate leaves no normal dent.");
        const std::map<std::string, double> ReseededClay{
            {"detailScale", 24}, {"grain", .8}, {"moisture", .04}, {"surfaceSeed", 18}};
        Confirm(ClayAlbedo != BakeProceduralChannelRgba("clay", "Albedo", ReseededClay, {}, 64),
            "Clay bake ignores its seed.");
        const std::map<std::string, double> DryClay{
            {"detailScale", 24}, {"grain", .8}, {"moisture", 0}, {"surfaceSeed", 17}};
        const std::map<std::string, double> WetClay{
            {"detailScale", 24}, {"grain", .8}, {"moisture", 1}, {"surfaceSeed", 17}};
        const auto DryNormal = BakeProceduralChannelRgba("clay", "Normal", DryClay, {}, 128);
        const auto WetNormal = BakeProceduralChannelRgba("clay", "Normal", WetClay, {}, 128);
        size_t DryDented = 0, WetDented = 0;
        for (size_t Pixel = 2; Pixel < DryNormal.size(); Pixel += 4) DryDented += DryNormal[Pixel] < 250 ? 1 : 0;
        for (size_t Pixel = 2; Pixel < WetNormal.size(); Pixel += 4) WetDented += WetNormal[Pixel] < 250 ? 1 : 0;
        Confirm(DryDented > WetDented, "Clay moisture fails to damp relief.");
        const auto DryAlbedo = BakeProceduralChannelRgba("clay", "Albedo", DryClay, {}, 64);
        const auto WetAlbedo = BakeProceduralChannelRgba("clay", "Albedo", WetClay, {}, 64);
        double DrySum = 0, WetSum = 0;
        for (size_t Pixel = 0; Pixel < DryAlbedo.size(); Pixel += 4) DrySum += DryAlbedo[Pixel];
        for (size_t Pixel = 0; Pixel < WetAlbedo.size(); Pixel += 4) WetSum += WetAlbedo[Pixel];
        Confirm(DrySum > WetSum, "Clay moisture fails to darken tone.");
        // LED: dark housing against gold contacts, domed lens normals and a 0..1 emitter mask.
        const std::map<std::string, double> PixelMatrix{{"detailScale", 7}, {"pixelFill", .62}, {"lensDome", .7},
            {"packageDepth", .6}, {"surfaceSeed", 17}};
        const std::map<std::string, std::string> PixelTexts{{"color", "#111518"}};
        const auto LedAlbedo = BakeProceduralChannelRgba("led", "Albedo", PixelMatrix, PixelTexts, 64);
        uint8_t LedMin = 255, LedMax = 0;
        for (size_t Pixel = 0; Pixel < LedAlbedo.size(); Pixel += 4)
        {
            LedMin = std::min(LedMin, LedAlbedo[Pixel]);
            LedMax = std::max(LedMax, LedAlbedo[Pixel]);
        }
        Confirm(LedMax > LedMin + 40, "LED package albedo has no housing/contact spread.");
        const auto LedRadiance = BakeProceduralChannelRgba("led", "Radiance", PixelMatrix, PixelTexts, 64);
        uint8_t MaskMin = 255, MaskMax = 0;
        for (size_t Pixel = 0; Pixel < LedRadiance.size(); Pixel += 4)
        {
            MaskMin = std::min(MaskMin, LedRadiance[Pixel]);
            MaskMax = std::max(MaskMax, LedRadiance[Pixel]);
        }
        Confirm(MaskMin < 8 && MaskMax > 230, "LED emitter mask escapes its dark-housing range.");
        Confirm(LedRadiance == BakeProceduralChannelRgba("led", "Radiance", PixelMatrix, PixelTexts, 64),
            "LED bake is not deterministic.");
        const std::map<std::string, double> ReseededLed{{"detailScale", 7}, {"pixelFill", .62}, {"lensDome", .7},
            {"packageDepth", .6}, {"surfaceSeed", 18}};
        Confirm(LedAlbedo != BakeProceduralChannelRgba("led", "Albedo", ReseededLed, PixelTexts, 64),
            "LED bake ignores its seed.");
        const std::map<std::string, double> SparseLed{{"detailScale", 7}, {"pixelFill", .2}, {"lensDome", .7},
            {"packageDepth", .6}, {"surfaceSeed", 17}};
        const std::map<std::string, double> DenseLed{{"detailScale", 7}, {"pixelFill", .9}, {"lensDome", .7},
            {"packageDepth", .6}, {"surfaceSeed", 17}};
        Confirm(BakeProceduralChannelRgba("led", "Radiance", SparseLed, PixelTexts, 64) !=
                BakeProceduralChannelRgba("led", "Radiance", DenseLed, PixelTexts, 64),
            "LED pixel fill fails to resize the emitter.");
        // 📝 The dome resolves to few-pixel tilts at 64; finer extents correctly flatten the same smooth
        // dome exactly as the live shader does, so the dent check samples the resolving extent.
        const auto LedNormal = BakeProceduralChannelRgba("led", "Normal", PixelMatrix, PixelTexts, 64);
        size_t LedDented = 0;
        for (size_t Pixel = 2; Pixel < LedNormal.size(); Pixel += 4) LedDented += LedNormal[Pixel] < 250 ? 1 : 0;
        Confirm(LedDented > 100, "LED lens leaves no normal dent.");
        // Fruit: blush over stripes, seeded lenticels and skin-mode-specific relief.
        const std::map<std::string, double> Apple{{"detailScale", 72}, {"skinMode", 0}, {"spotDensity", .55},
            {"colorGradient", .85}, {"grain", .4}, {"surfaceSeed", 17}};
        const std::map<std::string, std::string> AppleTexts{
            {"color", "#8f2029"}, {"secondaryColor", "#c5ac52"}, {"tertiaryColor", "#e3c697"}};
        const auto FruitAlbedo = BakeProceduralChannelRgba("fruit", "Albedo", Apple, AppleTexts, 64);
        uint8_t FruitMin = 255, FruitMax = 0;
        for (size_t Pixel = 0; Pixel < FruitAlbedo.size(); Pixel += 4)
        {
            FruitMin = std::min(FruitMin, FruitAlbedo[Pixel]);
            FruitMax = std::max(FruitMax, FruitAlbedo[Pixel]);
        }
        Confirm(FruitMax > FruitMin + 30, "Apple blush has no pigment spread.");
        Confirm(FruitAlbedo == BakeProceduralChannelRgba("fruit", "Albedo", Apple, AppleTexts, 64),
            "Fruit bake is not deterministic.");
        const std::map<std::string, double> ReseededApple{{"detailScale", 72}, {"skinMode", 0}, {"spotDensity", .55},
            {"colorGradient", .85}, {"grain", .4}, {"surfaceSeed", 18}};
        Confirm(FruitAlbedo != BakeProceduralChannelRgba("fruit", "Albedo", ReseededApple, AppleTexts, 64),
            "Fruit bake ignores its seed.");
        const std::map<std::string, double> Citrus{{"detailScale", 72}, {"skinMode", 1}, {"spotDensity", .55},
            {"colorGradient", .85}, {"grain", .4}, {"surfaceSeed", 17}};
        Confirm(FruitAlbedo != BakeProceduralChannelRgba("fruit", "Albedo", Citrus, AppleTexts, 64),
            "Fruit bake ignores its skin mode.");
        const std::map<std::string, double> Spotless{{"detailScale", 72}, {"skinMode", 0}, {"spotDensity", 0},
            {"colorGradient", .85}, {"grain", .4}, {"surfaceSeed", 17}};
        const std::map<std::string, double> Spotted{{"detailScale", 72}, {"skinMode", 0}, {"spotDensity", 1},
            {"colorGradient", .85}, {"grain", .4}, {"surfaceSeed", 17}};
        // 📝 Lenticels resolve at 256 and above; at 64 the pore field correctly fades exactly as the live
        // shader does at distance, so the density comparison samples the resolving extent.
        Confirm(BakeProceduralChannelRgba("fruit", "Albedo", Spotless, AppleTexts, 256) !=
                BakeProceduralChannelRgba("fruit", "Albedo", Spotted, AppleTexts, 256),
            "Fruit spot density paints no lenticels.");
        const auto FruitNormal = BakeProceduralChannelRgba("fruit", "Normal", Apple, AppleTexts, 128);
        size_t FruitDented = 0;
        for (size_t Pixel = 2; Pixel < FruitNormal.size(); Pixel += 4) FruitDented += FruitNormal[Pixel] < 250 ? 1 : 0;
        Confirm(FruitDented > 100, "Apple skin leaves no normal dent.");
        // Scratched metal: uncut metal reproduces the bare-metal bytes exactly; cuts match the study ORM
        // while the albedo keeps the tooth (the study's occlusion convention carries the darkening).
        const std::map<std::string, double> CastIron{{"grain", .8}, {"detailScale", 65}, {"surfaceSeed", 17},
            {"roughness", .56}, {"scratchDensity", 0}, {"scratchLength", 1.15}, {"scratchWidth", .01},
            {"scratchDepth", .0018}, {"scratchSpread", .8}, {"scratchBend", .42}, {"scratchScale", 6},
            {"angle", 25}};
        const std::map<std::string, double> BareIron{{"grain", .8}, {"detailScale", 65}, {"surfaceSeed", 17}};
        Confirm(BakeProceduralChannelRgba("scratchedMetal", "Albedo", CastIron, {}, 64) ==
                BakeProceduralChannelRgba("metalGrain", "Albedo", BareIron, {}, 64),
            "Uncut scratched metal drifts from the bare-metal albedo.");
        Confirm(BakeProceduralChannelRgba("scratchedMetal", "Normal", CastIron, {}, 64) ==
                BakeProceduralChannelRgba("metalGrain", "Normal", BareIron, {}, 64),
            "Uncut scratched metal drifts from the bare-metal relief.");
        const std::map<std::string, double> CutIron(CastIron.begin(), CastIron.end());
        const_cast<std::map<std::string, double> &>(CutIron)["scratchDensity"] = 6;
        const_cast<std::map<std::string, double> &>(CutIron)["roughness"] = .23;
        const std::map<std::string, double> StudyCuts{{"scratchDensity", 6}, {"scratchLength", 1.15},
            {"scratchWidth", .01}, {"scratchDepth", .0018}, {"scratchSpread", .8}, {"scratchBend", .42},
            {"scratchScale", 6}, {"angle", 25}, {"surfaceSeed", 17}, {"roughness", .23}};
        Confirm(BakeProceduralChannelRgba("scratchedMetal", "Orm", CutIron, {}, 64) ==
                BakeProceduralChannelRgba("scratches", "Orm", StudyCuts, {}, 64),
            "Metal cut ORM differs from the study definition.");
        Confirm(BakeProceduralChannelRgba("scratchedMetal", "Albedo", CutIron, {}, 64) ==
                BakeProceduralChannelRgba("metalGrain", "Albedo", BareIron, {}, 64),
            "Metal cuts darken the albedo instead of the occlusion.");
        const auto CutNormal = BakeProceduralChannelRgba("scratchedMetal", "Normal", CutIron, {}, 128);
        size_t CutDented = 0;
        for (size_t Pixel = 2; Pixel < CutNormal.size(); Pixel += 4) CutDented += CutNormal[Pixel] < 250 ? 1 : 0;
        Confirm(CutDented > 20, "Metal cuts leave no normal dent.");
        // Lathe metal: recipe-scale rings fade exactly as the Alloy anti-aliasing dictates, coarser scales
        // resolve, and the uncut ORM stays flat at the base ratio.
        const std::map<std::string, double> Brake{{"depth", .2}, {"detailScale", 180}, {"roughness", .42},
            {"scratchDensity", 0}, {"scratchLength", 1.15}, {"scratchWidth", .01}, {"scratchDepth", .0018},
            {"scratchSpread", .8}, {"scratchBend", .42}, {"scratchScale", 6}, {"angle", 25}, {"surfaceSeed", 17}};
        const auto FadedRings = BakeProceduralChannelRgba("latheMetal", "Albedo", Brake, {}, 64);
        bool RingsFlat = true;
        for (size_t Pixel = 0; Pixel < FadedRings.size(); Pixel += 4) RingsFlat = RingsFlat && FadedRings[Pixel] == 232;
        Confirm(RingsFlat, "Lathe anti-aliasing fade drifted.");
        const std::map<std::string, double> CoarseBrake(Brake.begin(), Brake.end());
        const_cast<std::map<std::string, double> &>(CoarseBrake)["detailScale"] = 8;
        const auto ResolvedRings = BakeProceduralChannelRgba("latheMetal", "Albedo", CoarseBrake, {}, 64);
        uint8_t RingMin = 255, RingMax = 0;
        for (size_t Pixel = 0; Pixel < ResolvedRings.size(); Pixel += 4)
        {
            RingMin = std::min(RingMin, ResolvedRings[Pixel]);
            RingMax = std::max(RingMax, ResolvedRings[Pixel]);
        }
        Confirm(RingMax - RingMin > 8, "Resolved lathe rings leave no modulation.");
        const auto BrakeOrm = BakeProceduralChannelRgba("latheMetal", "Orm", Brake, {}, 32);
        const uint8_t BrakeGreen = uint8_t(std::lround(.42f / .72f * 255));
        bool BrakeFlat = true;
        for (size_t Pixel = 0; Pixel < BrakeOrm.size(); Pixel += 4)
            BrakeFlat = BrakeFlat && BrakeOrm[Pixel] == 255 && BrakeOrm[Pixel + 1] == BrakeGreen && BrakeOrm[Pixel + 2] == 255;
        Confirm(BrakeFlat, "Uncut brake ORM misses the flat base ratio.");
        // Weave/knit dispatch: the channel path reproduces the direct field bakes byte for byte.
        const std::map<std::string, double> Selvedge{{"detailScale", 160}, {"weaveAngle", 0}, {"weaveRelief", .65},
            {"fiberDetail", 12}, {"fuzz", .35}, {"slub", .35}, {"denimFade", .08}, {"grain", 0},
            {"fabricMode", 0}, {"surfaceSeed", 17}, {"sheen", .5}, {"sheenRoughness", .75}, {"fuzzLength", .7}};
        const std::map<std::string, std::string> SelvedgeTexts{
            {"weavePattern", "denim"}, {"warpColor", "#163353"}, {"weftColor", "#bdb8a0"}};
        WeaveFieldParameters SelvedgeField;
        SelvedgeField.Type = 4;
        SelvedgeField.Weave = 8;
        SelvedgeField.Scale = 160;
        SelvedgeField.Relief = .65f;
        SelvedgeField.FiberDetail = 12;
        SelvedgeField.Fuzz = .35f;
        SelvedgeField.Slub = .35f;
        SelvedgeField.DenimFade = .08f;
        SelvedgeField.Seed = 17;
        Confirm(DecodeHexLinearRgb("#163353", SelvedgeField.Warp) && DecodeHexLinearRgb("#bdb8a0", SelvedgeField.Weft),
            "Selvedge yarn test colours invalid.");
        auto SelvedgeMaps = BakeWeaveMaps(SelvedgeField, 32);
        const float SelvedgeAmplitude = WeaveReliefAmplitude(SelvedgeField);
        for (float &Height : SelvedgeMaps.Relief) Height /= SelvedgeAmplitude;
        Confirm(BakeProceduralChannelRgba("weave", "Albedo", Selvedge, SelvedgeTexts, 32) ==
                ConvertLinearAlbedoRgba(SelvedgeMaps.Albedo, 32),
            "Weave channel albedo differs from the field bake.");
        Confirm(BakeProceduralChannelRgba("weave", "Normal", Selvedge, SelvedgeTexts, 32) ==
                ConvertHeightToNormalRgba(SelvedgeMaps.Relief, 32, 2.f),
            "Weave channel normal differs from the field bake.");
        bool RejectedWeavePattern = false;
        try
        {
            (void)BakeProceduralChannelRgba(
                "weave", "Albedo", Selvedge, {{"weavePattern", "herringbone?"}, {"warpColor", "#163353"}, {"weftColor", "#bdb8a0"}}, 16);
        }
        catch (const std::exception &) { RejectedWeavePattern = true; }
        Confirm(RejectedWeavePattern, "Weave dispatch accepted an unknown pattern.");
        const std::map<std::string, double> JerseyScalars{{"detailScale", 22}, {"grain", .6}, {"fuzz", .35},
            {"weaveAngle", 0}, {"surfaceSeed", 17}, {"sheen", .5}};
        const std::map<std::string, std::string> JerseyTexts{{"color", "#8d9294"}, {"secondaryColor", "#e2ddd0"}};
        KnitFieldParameters JerseyParams;
        JerseyParams.Scale = 22;
        JerseyParams.Grain = .6f;
        JerseyParams.Fuzz = .35f;
        JerseyParams.Seed = 17;
        Confirm(DecodeHexLinearRgb("#8d9294", JerseyParams.Color) && DecodeHexLinearRgb("#e2ddd0", JerseyParams.Secondary),
            "Jersey test colours invalid.");
        auto JerseyDirect = BakeKnitMaps(JerseyParams, 32);
        const float JerseyAmplitude = KnitReliefAmplitude(JerseyParams);
        for (float &Height : JerseyDirect.Relief) Height /= JerseyAmplitude;
        Confirm(BakeProceduralChannelRgba("knit", "Albedo", JerseyScalars, JerseyTexts, 32) ==
                ConvertLinearAlbedoRgba(JerseyDirect.Albedo, 32),
            "Knit channel albedo differs from the field bake.");
        Confirm(BakeProceduralChannelRgba("knit", "Normal", JerseyScalars, JerseyTexts, 32) ==
                ConvertHeightToNormalRgba(JerseyDirect.Relief, 32, 2.f),
            "Knit channel normal differs from the field bake.");
        // Weave patterns: every over/under mask resolves at cell centers exactly as the Alloy twill().
        Confirm(WeaveCell(0, 0, 0) == 'R' && WeaveCell(0, 1, 0) == 'B' && WeaveCell(0, 0, 1) == 'B' &&
                WeaveCell(0, 1, 1) == 'R',
            "Plain weave checker drifted.");
        Confirm(WeaveCell(1, 0, 0) == 'R' && WeaveCell(1, 2, 0) == 'B' && WeaveCell(1, 0, 1) == 'B' &&
                WeaveCell(1, 1, 1) == 'R',
            "Twill diagonal drifted.");
        Confirm(WeaveCell(2, 4, 0) == 'B' && WeaveCell(2, 0, 0) == 'R' && WeaveCell(2, 2, 1) == 'B',
            "Satin dot drifted.");
        Confirm(WeaveCell(3, 0, 0) == 'R' && WeaveCell(3, 2, 0) == 'B' && WeaveCell(3, 0, 2) == 'B' &&
                WeaveCell(3, 2, 2) == 'R',
            "Basket blocks drifted.");
        Confirm(WeaveCell(4, 0, 0) == 'R' && WeaveCell(4, 1, 0) == 'B' && WeaveCell(4, 0, 1) == 'R' &&
                WeaveCell(4, 0, 2) == 'B',
            "Rib stripes drifted.");
        Confirm(WeaveCell(5, 0, 0) == 'R' && WeaveCell(5, 2, 0) == 'B' && WeaveCell(5, 8, 0) == 'B' &&
                WeaveCell(5, 10, 0) == 'R' && WeaveCell(5, 16, 0) == 'R',
            "Herringbone chevron drifted.");
        Confirm(WeaveCell(6, 0, 0) == 'R' && WeaveCell(6, 2, 0) == 'B' && WeaveCell(6, 0, 1) == 'B' &&
                WeaveCell(6, 2, 1) == 'R',
            "Oxford bricks drifted.");
        Confirm(WeaveCell(7, 2, 0) == 'R' && WeaveCell(7, 0, 1) == 'R' && WeaveCell(7, 2, 1) == 'B' &&
                WeaveCell(7, 0, 2) == 'B' && WeaveCell(7, 0, 3) == 'R' && WeaveCell(7, 1, 3) == 'B',
            "Houndstooth teeth drifted.");
        Confirm(WeaveCell(8, 0, 0) == 'R' && WeaveCell(8, 3, 0) == 'B' && WeaveCell(8, 0, 1) == 'B',
            "Denim steep twill drifted.");
        // Weave fields: carbon/nap modulation stays grayscale, relief keeps its analytic amplitude.
        WeaveFieldParameters Carbon;
        Carbon.Type = 7;
        Carbon.Scale = 14;
        Carbon.Relief = .7f;
        Carbon.FiberDetail = 12;
        const auto CarbonSample = EvaluateWeaveField(.3f, .6f, Carbon, 14.f / 512);
        Confirm(CarbonSample.Albedo[0] == CarbonSample.Albedo[1] && CarbonSample.Albedo[1] == CarbonSample.Albedo[2],
            "Carbon weave modulation carries hue.");
        WeaveFieldParameters Nap = Carbon;
        Nap.Type = 4;
        Nap.FabricMode = 2;
        Nap.Scale = 36;
        Nap.Fuzz = 1;
        Nap.Seed = 17;
        const auto NapSample = EvaluateWeaveField(.3f, .6f, Nap, 36.f / 512);
        Confirm(NapSample.Albedo[0] == NapSample.Albedo[1] && NapSample.Albedo[1] == NapSample.Albedo[2],
            "Nap weave modulation carries hue.");
        WeaveFieldParameters Denim;
        Denim.Type = 4;
        Denim.Weave = 8;
        Denim.Scale = 160;
        Denim.Relief = .65f;
        Denim.FiberDetail = 12;
        Denim.Fuzz = .35f;
        Denim.Slub = .35f;
        Denim.DenimFade = .08f;
        Denim.Seed = 17;
        const float DenimAmplitude = WeaveReliefAmplitude(Denim);
        Confirm(DenimAmplitude > 0, "Denim relief amplitude collapsed.");
        const auto DenimMaps = BakeWeaveMaps(Denim, 64);
        const auto DenimRepeat = BakeWeaveMaps(Denim, 64);
        Confirm(DenimMaps.Albedo == DenimRepeat.Albedo && DenimMaps.Relief == DenimRepeat.Relief,
            "Weave bake is not deterministic.");
        for (const float Height : DenimMaps.Relief)
            Confirm(std::abs(Height) <= DenimAmplitude * 1.001f, "Weave relief escapes its amplitude.");
        bool RejectedWeave = false;
        try { (void)BakeWeaveMaps(WeaveFieldParameters{}, 32); }
        catch (const std::exception &) { RejectedWeave = true; }
        Confirm(RejectedWeave, "Weave bake accepted an empty scale.");
        // Knit fields: loops modulate the albedo, relief keeps its analytic amplitude.
        KnitFieldParameters Jersey;
        Jersey.Scale = 22;
        Jersey.Grain = .6f;
        Jersey.Seed = 17;
        Jersey.Color[0] = Jersey.Color[1] = Jersey.Color[2] = .5f;
        const float KnitAmplitude = KnitReliefAmplitude(Jersey);
        Confirm(KnitAmplitude > 0, "Knit relief amplitude collapsed.");
        const auto JerseyMaps = BakeKnitMaps(Jersey, 64);
        float KnitMinimum = 1, KnitMaximum = 0;
        for (size_t Pixel = 0; Pixel < JerseyMaps.Relief.size(); ++Pixel)
        {
            Confirm(std::abs(JerseyMaps.Relief[Pixel]) <= KnitAmplitude * 1.001f, "Knit relief escapes its amplitude.");
            KnitMinimum = std::min(KnitMinimum, JerseyMaps.Albedo[Pixel * 3]);
            KnitMaximum = std::max(KnitMaximum, JerseyMaps.Albedo[Pixel * 3]);
        }
        Confirm(KnitMaximum - KnitMinimum > .05f, "Knit loops leave no albedo modulation.");
        Confirm(JerseyMaps.Albedo == BakeKnitMaps(Jersey, 64).Albedo, "Knit bake is not deterministic.");
        bool RejectedKnit = false;
        try { (void)BakeKnitMaps(KnitFieldParameters{}, 32); }
        catch (const std::exception &) { RejectedKnit = true; }
        Confirm(RejectedKnit, "Knit bake accepted an empty scale.");
        // Sand fields: zero coverage reproduces the base exactly, live beds stay in range, seeds separate.
        SandFieldParameters Dune;
        Dune.Count = 1;
        Dune.Seed = 41;
        Dune.BaseColor[0] = Dune.BaseColor[1] = Dune.BaseColor[2] = .5f;
        auto &DuneBed = Dune.Beds[0];
        DuneBed.Color[0] = DuneBed.Color[1] = DuneBed.Color[2] = .8f;
        DuneBed.Pale[0] = DuneBed.Pale[1] = DuneBed.Pale[2] = .9f;
        DuneBed.Size = .7f;
        DuneBed.SizeVariation = .55f;
        DuneBed.Coverage = 0;
        DuneBed.Thickness = 1;
        DuneBed.Roughness = .58f;
        DuneBed.RoughnessVariation = .3f;
        DuneBed.Specular = .8f;
        DuneBed.Tilt = .32f;
        DuneBed.Relief = .85f;
        DuneBed.BedSeed = SandBedSeed("sand-bed-1");
        const auto BareSand = BakeSandMaps(Dune, 16);
        for (size_t Pixel = 0; Pixel < BareSand.Relief.size(); ++Pixel)
        {
            Confirm(BareSand.Tint[Pixel * 3] == .5f && BareSand.Roughness[Pixel] == std::min(1.f, .58f + .16f) &&
                    BareSand.Relief[Pixel] == 0 && BareSand.Facet[Pixel * 2] == 0,
                "Bare sand escapes its base.");
        }
        DuneBed.Coverage = 1;
        const float SandAmplitude = SandReliefAmplitude(Dune);
        Confirm(SandAmplitude > 0, "Sand relief amplitude collapsed.");
        // 📝 Grains resolve past extent ~88 at this frequency; coarser bakes fade to the mean like the
        // shader does with distance, so seed sensitivity asserts at a resolved extent.
        const auto LiveSand = BakeSandMaps(Dune, 128);
        const auto LiveRepeat = BakeSandMaps(Dune, 128);
        Confirm(LiveSand.Tint == LiveRepeat.Tint && LiveSand.Roughness == LiveRepeat.Roughness &&
                LiveSand.Relief == LiveRepeat.Relief && LiveSand.Facet == LiveRepeat.Facet,
            "Sand bake is not deterministic.");
        SandFieldParameters ReseededDune = Dune;
        ReseededDune.Seed = 42;
        Confirm(BakeSandMaps(ReseededDune, 128).Tint != LiveSand.Tint, "Sand bake ignores its seed.");
        bool GrainDiffers = false;
        for (size_t Pixel = 0; Pixel < LiveSand.Relief.size(); ++Pixel)
        {
            Confirm(LiveSand.Tint[Pixel * 3] >= 0 && LiveSand.Roughness[Pixel] >= .04f &&
                    LiveSand.Roughness[Pixel] <= 1 && LiveSand.Relief[Pixel] >= 0 &&
                    LiveSand.Relief[Pixel] <= SandAmplitude * 1.001f &&
                    std::abs(LiveSand.Facet[Pixel * 2]) <= DuneBed.Tilt + .01f,
                "Sand grain escapes its range.");
            GrainDiffers = GrainDiffers || LiveSand.Tint[Pixel * 3] != .5f;
        }
        Confirm(GrainDiffers, "Live sand reproduces its base.");
        bool RejectedSand = false;
        try { (void)BakeSandMaps(SandFieldParameters{}, 32); }
        catch (const std::exception &) { RejectedSand = true; }
        Confirm(RejectedSand, "Sand bake accepted zero beds.");
        Confirm(SandBedSeed("sand-bed-1") != SandBedSeed("sand-bed-2"), "Sand bed seeds collide.");
        // Recipe color and converter encoding: sRGB round-trips through linear, ORM/normal stay exact.
        float Indigo[3] = {0, 0, 0};
        Confirm(DecodeHexLinearRgb("#163353", Indigo), "Recipe indigo rejected.");
        const double IndigoRed = .0862745 > .04045 ? std::pow((.0862745 + .055) / 1.055, 2.4) : .0862745 / 12.92;
        Confirm(std::abs(double(Indigo[0]) - IndigoRed) < 1e-6, "Hex decode drifts from sRGB.");
        float BadHex[3] = {0, 0, 0};
        Confirm(!DecodeHexLinearRgb("#xyz123", BadHex) && !DecodeHexLinearRgb("163353", BadHex) &&
                !DecodeHexLinearRgb("#12345", BadHex),
            "Malformed hex accepted.");
        const std::vector<float> LinearGray{0, .5f, 1};
        const auto EncodedGray = ConvertLinearAlbedoRgba(LinearGray, 1);
        Confirm(EncodedGray.size() == 4 && EncodedGray[0] == 0 && EncodedGray[2] == 255 && EncodedGray[3] == 255 &&
                std::abs(int(EncodedGray[1]) - 188) <= 1,
            "Linear albedo encoding drifted.");
        const std::vector<float> RoughField{0, .5f, 1, .25f};
        const auto SandOrm = ConvertSandOrmRgba(RoughField, 2);
        Confirm(SandOrm[0] == 255 && SandOrm[1] == 0 && SandOrm[2] == 255 &&
                SandOrm[5] >= 127 && SandOrm[5] <= 129 && SandOrm[9] == 255 && SandOrm[13] >= 63 && SandOrm[13] <= 65,
            "Sand ORM encoding drifted.");
        const std::vector<float> FlatRelief(16 * 16, 0), ZeroFacet(16 * 16 * 2, 0);
        const auto FlatFaceted = ConvertFacetNormalRgba(FlatRelief, ZeroFacet, 16, 2);
        Confirm(FlatFaceted[0] == 128 && FlatFaceted[1] == 128 && FlatFaceted[2] == 255,
            "Flat faceted normal is not straight up.");
        std::vector<float> TiltFacet(16 * 16 * 2, 0);
        for (size_t Pixel = 0; Pixel < 16 * 16; ++Pixel) TiltFacet[Pixel * 2] = 1;
        const auto Tilted = ConvertFacetNormalRgba(FlatRelief, TiltFacet, 16, 2);
        Confirm(Tilted[0] > 128 && Tilted[1] == 128, "Facet tilt leans the wrong way.");
        // Recipe coverage: every shipped material parses its SourceRecipe; the pilot resolves exactly.
        MaterialLibrary Library;
        Confirm(Library.ConfigureDirectory("EngineContent/MaterialArchives/TextureStudio") && Library.Refresh(),
            "Material library refresh failed.");
        // 📝 127 HTML presets + 4 camouflage entries + 5 sand presets + 6 layered rock/landscape presets.
        Confirm(Library.QueryEntries().size() == 142, "Shipped material count changed.");
        std::set<std::string> RecipeIds;
        const MaterialCatalogueEntry *PilotEntry = nullptr, *SandEntry = nullptr, *LayeredEntry = nullptr,
                                     *DenimEntry = nullptr;
        for (const auto &Entry : Library.QueryEntries())
        {
            if (!Entry.Recipe.RecipeId.empty()) RecipeIds.insert(Entry.Recipe.RecipeId);
            if (Entry.Recipe.RecipeId == "scratches") PilotEntry = &Entry;
            if (Entry.RelativePath == "sand.material") SandEntry = &Entry;
            if (Entry.RelativePath == "layered-sand.material") LayeredEntry = &Entry;
            if (Entry.RelativePath == "raw-selvedge-denim.material") DenimEntry = &Entry;
        }
        Confirm(RecipeIds.size() == 51, "Parsed recipe family count changed.");
        Confirm(PilotEntry && PilotEntry->RelativePath == "scratches.material", "Scratch pilot entry is missing.");
        const auto &Recipe = PilotEntry->Recipe;
        Confirm(Scalar(Recipe, "type") == 29 && Scalar(Recipe, "surfaceSeed") == 17, "Pilot integers misparsed.");
        Confirm(std::abs(Scalar(Recipe, "scratchLength") - 1.15) < 1e-9 && std::abs(Scalar(Recipe, "scratchWidth") - .01) < 1e-9 &&
                std::abs(Scalar(Recipe, "scratchDepth") - .0018) < 1e-12 && Scalar(Recipe, "scratchDensity") == 6 &&
                std::abs(Scalar(Recipe, "scratchSpread") - .8) < 1e-9 && std::abs(Scalar(Recipe, "scratchBend") - .42) < 1e-9 &&
                Scalar(Recipe, "scratchScale") == 6 && Scalar(Recipe, "weaveAngle") == 25 &&
                std::abs(Scalar(Recipe, "roughness") - .23) < 1e-9,
            "Pilot scratch uniforms misparsed.");
        Confirm(Scalar(Recipe, "metalScratches") == 0 && Scalar(Recipe, "clothMapping") == 0, "Pilot flags misparsed.");
        Confirm(Recipe.Texts.at("color") == "#b9bec0", "Pilot color text misparsed.");
        Confirm(DenimEntry && RecipeText(DenimEntry->Recipe, "weavePattern") == "denim" &&
                RecipeText(DenimEntry->Recipe, "warpColor") == "#163353",
            "Denim weave texts misparsed.");
        Confirm(SandEntry && SandEntry->Recipe.SandBeds.size() == 1, "Single-grain sand bed count changed.");
        const auto &LoneBed = SandEntry->Recipe.SandBeds.front();
        Confirm(LoneBed.Id == "sand-bed-1" && LoneBed.Color == "#c9aa79" && LoneBed.SecondaryColor == "#eee2c7" &&
                std::abs(LoneBed.Size - .7) < 1e-9 && std::abs(LoneBed.Roughness - .58) < 1e-9 &&
                std::abs(LoneBed.Coverage - 1) < 1e-9 && std::abs(LoneBed.Thickness - 1) < 1e-9,
            "Single-grain sand bed misparsed.");
        Confirm(LayeredEntry && LayeredEntry->Recipe.SandBeds.size() == 3, "Layered sand bed count changed.");
        const auto &Beds = LayeredEntry->Recipe.SandBeds;
        Confirm(Beds[0].Id == "sand-bed-1" && std::abs(Beds[0].Size - .65) < 1e-9 &&
                std::abs(Beds[0].Coverage - .85) < 1e-9 && Beds[1].Id == "sand-bed-2" &&
                std::abs(Beds[1].Size - .25) < 1e-9 && std::abs(Beds[1].Coverage - .92) < 1e-9 &&
                Beds[2].Id == "sand-bed-3" && std::abs(Beds[2].Size - .95) < 1e-9 &&
                std::abs(Beds[2].Thickness - 1.3) < 1e-9,
            "Layered sand beds misparsed.");
        // 📝 Botanical/extended ops: deterministic, seed-sensitive, and driven by their headline
        // parameters (the sliders the catalogue audit proved were locked before the port).
        {
            const std::map<std::string, double> Leaf{{"detailScale", 10}, {"cellScale", 120}, {"cellRelief", .5},
                {"cellLobing", .65}, {"stomataDensity", .22}, {"veinRelief", .65}, {"colorGradient", .18},
                {"surfaceSeed", 17}};
            const std::map<std::string, std::string> LeafTexts{{"color", "#355d28"}, {"secondaryColor", "#829548"},
                {"tertiaryColor", "#8c8a38"}};
            const auto LeafNormal = BakeProceduralChannelRgba("leaf", "Normal", Leaf, LeafTexts, 128);
            Confirm(LeafNormal == BakeProceduralChannelRgba("leaf", "Normal", Leaf, LeafTexts, 128),
                "Leaf bake is not deterministic.");
            auto FlatLeaf = Leaf;
            FlatLeaf["veinRelief"] = 0;
            FlatLeaf["cellRelief"] = 0;
            Confirm(LeafNormal != BakeProceduralChannelRgba("leaf", "Normal", FlatLeaf, LeafTexts, 128),
                "Leaf vein/cell relief sliders do not drive the normal bake.");
            const std::map<std::string, double> Cactus{{"detailScale", 15}, {"plantRibs", 10}, {"cellScale", 120},
                {"cellRelief", .5}, {"veinRelief", .55}, {"colorGradient", .35}, {"roughness", .48}, {"surfaceSeed", 17}};
            const std::map<std::string, std::string> CactusTexts{{"color", "#426d48"}, {"secondaryColor", "#8faaa0"},
                {"tertiaryColor", "#bbb095"}};
            auto RibbedCactus = Cactus;
            RibbedCactus["plantRibs"] = 6;
            Confirm(BakeProceduralChannelRgba("cactus", "Albedo", Cactus, CactusTexts, 128) !=
                    BakeProceduralChannelRgba("cactus", "Albedo", RibbedCactus, CactusTexts, 128),
                "Cactus rib count does not drive the bake.");
            const auto CactusOrm = BakeProceduralChannelRgba("cactus", "Orm", Cactus, CactusTexts, 128);
            bool PoreRoughness = false;
            for (size_t Pixel = 1; Pixel < CactusOrm.size(); Pixel += 4)
                PoreRoughness = PoreRoughness || CactusOrm[Pixel] > 200;
            Confirm(PoreRoughness, "Cactus areoles do not lift ORM roughness toward the pore ceiling.");
            const std::map<std::string, double> Hide{{"detailScale", 43}, {"grain", .75}, {"hideVariant", 1},
                {"hideVariation", .65}, {"wear", .12}, {"wearSoftness", .65}, {"wornRoughness", .24},
                {"scratchScale", 32}, {"surfaceSeed", 17}};
            auto BullHide = Hide;
            BullHide["hideVariant"] = 2;
            const auto NappaNormal = BakeProceduralChannelRgba("leather", "Normal", Hide, {}, 128);
            Confirm(NappaNormal != BakeProceduralChannelRgba("leather", "Normal", BullHide, {}, 128),
                "Hide variant does not select a different furrow field.");
            bool Furrows = false;
            for (size_t Pixel = 0; Pixel + 2 < NappaNormal.size(); Pixel += 4)
                Furrows = Furrows || NappaNormal[Pixel] != 128 || NappaNormal[Pixel + 1] != 128;
            Confirm(Furrows, "Leather bake holds no furrow relief.");
            const std::map<std::string, double> Rust{{"detailScale", 48}, {"grain", .8}, {"oxidation", .85},
                {"roughness", .36}, {"surfaceSeed", 17}};
            const std::map<std::string, std::string> RustTexts{{"color", "#6a6d70"}, {"secondaryColor", "#8a4a26"}};
            auto CleanRust = Rust;
            CleanRust["oxidation"] = 0;
            const auto RustOrm = BakeProceduralChannelRgba("rust", "Orm", Rust, RustTexts, 128);
            const auto CleanOrm = BakeProceduralChannelRgba("rust", "Orm", CleanRust, RustTexts, 128);
            bool OxideKillsMetal = false, CleanKeepsMetal = true;
            for (size_t Pixel = 2; Pixel < RustOrm.size(); Pixel += 4)
            {
                OxideKillsMetal = OxideKillsMetal || RustOrm[Pixel] < 64;
                CleanKeepsMetal = CleanKeepsMetal && CleanOrm[Pixel] == 255;
            }
            Confirm(OxideKillsMetal && CleanKeepsMetal, "Rust oxidation does not drive the ORM metalness term.");
            bool RejectedBotanical = false;
            try { (void)BakeProceduralChannelRgba("leaf", "Orm", Leaf, LeafTexts, 32); }
            catch (const std::exception &) { RejectedBotanical = true; }
            Confirm(RejectedBotanical, "Leaf op baked an ORM channel its shader never varies.");
        }
        // Fail-closed regeneration: committed pilot PNGs must equal a fresh bake from the parsed recipe.
        ScratchFieldParameters LibraryPilot{float(Scalar(Recipe, "scratchDensity")), float(Scalar(Recipe, "scratchLength")),
            float(Scalar(Recipe, "scratchWidth")), float(Scalar(Recipe, "scratchDepth")), float(Scalar(Recipe, "scratchSpread")),
            float(Scalar(Recipe, "scratchBend")), float(Scalar(Recipe, "scratchScale")),
            float(Scalar(Recipe, "weaveAngle") * 3.141592653589793 / 180), uint32_t(Scalar(Recipe, "surfaceSeed"))};
        auto LibraryMaps = BakeScratchMaps(LibraryPilot, 512);
        for (float &Height : LibraryMaps.Height) Height /= LibraryPilot.Depth;
        const auto LibraryNormal = ConvertHeightToNormalRgba(LibraryMaps.Height, 512, 2.f);
        const auto LibraryOrm = ConvertScratchOrmRgba(LibraryMaps.Coverage, 512, float(Scalar(Recipe, "roughness")));
        for (const auto &[Pixels, Stem] :
            {std::pair<const std::vector<uint8_t> &, const char *>{LibraryNormal, "scratches-normal.png"}, {LibraryOrm, "scratches-orm.png"}})
        {
            const auto Encoded = EncodeProceduralPng(Pixels, 512);
            const auto Committed = ReadBytes(std::filesystem::path("EngineContent/MaterialArchives/TextureStudio/Textures") / Stem);
            Confirm(Committed == Encoded, "Committed pilot texture drifted from its recipe bake.");
        }
        // Fail-closed regeneration across every baked family: each committed weave/knit/sand PNG must equal
        // a fresh bake from the parsed recipe. Param mapping mirrors the offline baker key for key.
        const auto BakeLibraryWeave = [&](const MaterialRecipeParameters &R, const std::string &Stem)
        {
            WeaveFieldParameters P;
            P.Type = int(OptionalScalar(R, "type", 4));
            P.Weave = PatternIndex(RecipeText(R, "weavePattern", "twill"));
            P.FabricMode = int(OptionalScalar(R, "fabricMode", 0));
            P.Scale = float(OptionalScalar(R, "detailScale", 0));
            P.AngleRadians = float(OptionalScalar(R, "weaveAngle", 0) * 3.141592653589793 / 180);
            P.Relief = float(OptionalScalar(R, "weaveRelief", 0));
            P.FiberDetail = float(OptionalScalar(R, "fiberDetail", 12));
            P.Fuzz = float(OptionalScalar(R, "fuzz", 0));
            P.Slub = float(OptionalScalar(R, "slub", 0));
            P.DenimFade = float(OptionalScalar(R, "denimFade", 0));
            P.Grain = float(OptionalScalar(R, "grain", 0));
            P.Seed = float(OptionalScalar(R, "surfaceSeed", 0));
            Confirm(DecodeHexLinearRgb(RecipeText(R, "warpColor"), P.Warp), "Library warp color invalid.");
            Confirm(DecodeHexLinearRgb(RecipeText(R, "weftColor"), P.Weft), "Library weft color invalid.");
            if (!DecodeHexLinearRgb(RecipeText(R, "secondaryColor"), P.Secondary))
                P.Secondary[0] = P.Secondary[1] = P.Secondary[2] = 0;
            auto Maps = BakeWeaveMaps(P, 512);
            const float Amplitude = WeaveReliefAmplitude(P);
            if (Amplitude > 0)
                for (float &Height : Maps.Relief) Height /= Amplitude;
            return std::vector<std::pair<std::vector<uint8_t>, std::string>>{
                {ConvertLinearAlbedoRgba(Maps.Albedo, 512), Stem + "-albedo.png"},
                {ConvertHeightToNormalRgba(Maps.Relief, 512, 2.f), Stem + "-normal.png"}};
        };
        const auto BakeLibraryKnit = [&](const MaterialRecipeParameters &R, const std::string &Stem)
        {
            KnitFieldParameters P;
            P.Scale = float(OptionalScalar(R, "detailScale", 0));
            P.Grain = float(OptionalScalar(R, "grain", 0));
            P.Fuzz = float(OptionalScalar(R, "fuzz", 0));
            P.Seed = float(OptionalScalar(R, "surfaceSeed", 0));
            P.AngleRadians = float(OptionalScalar(R, "weaveAngle", 0) * 3.141592653589793 / 180);
            Confirm(DecodeHexLinearRgb(RecipeText(R, "color"), P.Color), "Library knit color invalid.");
            if (!DecodeHexLinearRgb(RecipeText(R, "secondaryColor"), P.Secondary))
                P.Secondary[0] = P.Secondary[1] = P.Secondary[2] = 0;
            auto Maps = BakeKnitMaps(P, 512);
            const float Amplitude = KnitReliefAmplitude(P);
            if (Amplitude > 0)
                for (float &Height : Maps.Relief) Height /= Amplitude;
            return std::vector<std::pair<std::vector<uint8_t>, std::string>>{
                {ConvertLinearAlbedoRgba(Maps.Albedo, 512), Stem + "-albedo.png"},
                {ConvertHeightToNormalRgba(Maps.Relief, 512, 2.f), Stem + "-normal.png"}};
        };
        const auto BakeLibrarySand = [&](const MaterialRecipeParameters &R, const std::string &Stem)
        {
            Confirm(!R.SandBeds.empty() && R.SandBeds.size() <= 4, "Library sand beds missing.");
            SandFieldParameters P;
            P.Count = int(R.SandBeds.size());
            for (size_t Bed = 0; Bed < R.SandBeds.size(); ++Bed)
            {
                const auto &Source = R.SandBeds[Bed];
                auto &Target = P.Beds[Bed];
                Confirm(DecodeHexLinearRgb(Source.Color, Target.Color) &&
                        DecodeHexLinearRgb(Source.SecondaryColor, Target.Pale),
                    "Library sand bed colors invalid.");
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
            P.BandsMode = RecipeText(R, "sandArrangement") == "bands" ? 1 : 0;
            P.Scale = float(OptionalScalar(R, "sandScale", 1));
            P.Seed = float(OptionalScalar(R, "surfaceSeed", 0));
            P.BandFrequency = float(OptionalScalar(R, "sandBandFrequency", 1.5));
            P.BandWarp = float(OptionalScalar(R, "sandBandWarp", .2));
            P.BandAngleRadians = float(OptionalScalar(R, "sandBandAngle", 0) * 3.141592653589793 / 180);
            Confirm(DecodeHexLinearRgb(RecipeText(R, "color"), P.BaseColor), "Library sand base color invalid.");
            auto Maps = BakeSandMaps(P, 512);
            const float Amplitude = SandReliefAmplitude(P);
            if (Amplitude > 0)
                for (float &Height : Maps.Relief) Height /= Amplitude;
            return std::vector<std::pair<std::vector<uint8_t>, std::string>>{
                {ConvertLinearAlbedoRgba(Maps.Tint, 512), Stem + "-albedo.png"},
                {ConvertSandOrmRgba(Maps.Roughness, 512), Stem + "-orm.png"},
                {ConvertFacetNormalRgba(Maps.Relief, Maps.Facet, 512, 2.f), Stem + "-normal.png"}};
        };
        static const std::set<std::string> LibraryWeaveIds{"woven", "carbon", "suede", "velvet", "cotton", "denim",
            "silk", "linen", "wool"};
        // 📝 Channel-dispatched families pin through the same BakeProceduralChannelRgba path the offline
        // baker and the workspace rebake use; the key lists mirror the baker entry for entry.
        const auto BakeLibraryChannels = [&](const MaterialRecipeParameters &R, const std::string &Stem,
            std::string_view Op, std::initializer_list<const char *> Channels)
        {
            std::map<std::string, double> Params;
            std::map<std::string, std::string> Texts;
            if (Op == "solar")
            {
                for (const char *Key : {"detailScale", "roughness", "busbarWidth", "surfaceSeed", "panelMode"})
                    Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
            }
            else if (Op == "dimples")
                for (const char *Key : {"detailScale", "dimpleDepth", "surfaceSeed"}) Params[Key] = Scalar(R, Key);
            else if (Op == "stucco")
                for (const char *Key : {"detailScale", "grain", "surfaceSeed"}) Params[Key] = Scalar(R, Key);
            else if (Op == "clay")
                for (const char *Key : {"detailScale", "grain", "moisture", "surfaceSeed"}) Params[Key] = Scalar(R, Key);
            else if (Op == "led")
            {
                for (const char *Key : {"detailScale", "pixelFill", "lensDome", "packageDepth", "surfaceSeed"})
                    Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
            }
            else if (Op == "fruit")
            {
                for (const char *Key : {"detailScale", "skinMode", "spotDensity", "colorGradient", "grain", "surfaceSeed", "roughness"})
                    Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
                Texts["tertiaryColor"] = RecipeText(R, "tertiaryColor");
            }
            else if (Op == "metalGrain")
                for (const char *Key : {"grain", "detailScale", "surfaceSeed"}) Params[Key] = Scalar(R, Key);
            else if (Op == "polymerGrain")
                for (const char *Key : {"grain", "detailScale", "wear", "wearSoftness", "wornRoughness",
                         "scratchScale", "surfaceSeed", "roughness"})
                    Params[Key] = Scalar(R, Key);
            else if (Op == "leaf" || Op == "grass" || Op == "petal" || Op == "stem" || Op == "cactus")
            {
                for (const char *Key : {"detailScale", "plantRibs", "cellScale", "cellRelief", "cellLobing",
                         "stomataDensity", "veinRelief", "colorGradient", "spotDensity", "roughness", "surfaceSeed"})
                    Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
                Texts["tertiaryColor"] = RecipeText(R, "tertiaryColor");
            }
            else if (Op == "leather")
                for (const char *Key : {"detailScale", "grain", "hideVariant", "hideVariation", "wear",
                         "wearSoftness", "wornRoughness", "scratchScale", "surfaceSeed"})
                    Params[Key] = Scalar(R, Key);
            else if (Op == "crocodile")
            {
                for (const char *Key : {"detailScale", "grain", "groutWidth", "poreDensity", "hideVariation", "surfaceSeed"})
                    Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
                Texts["tertiaryColor"] = RecipeText(R, "tertiaryColor");
            }
            else if (Op == "wax")
                for (const char *Key : {"detailScale", "grain", "surfaceSeed"}) Params[Key] = Scalar(R, Key);
            else if (Op == "skin")
            {
                for (const char *Key : {"detailScale", "grain", "freckles", "surfaceSeed"}) Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
            }
            else if (Op == "paper")
            {
                for (const char *Key : {"detailScale", "grain", "fiberContrast", "paperRibs", "surfaceSeed"})
                    Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
            }
            else if (Op == "rust")
            {
                for (const char *Key : {"detailScale", "grain", "oxidation", "roughness", "surfaceSeed"})
                    Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
            }
            else if (Op == "ceramic" || Op == "rubber")
                for (const char *Key : {"detailScale", "depth", "surfaceSeed"}) Params[Key] = Scalar(R, Key);
            else if (Op == "tiles")
            {
                for (const char *Key : {"detailScale", "grain", "groutWidth", "tileVariation", "tileStagger",
                         "roughness", "surfaceSeed"})
                    Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
            }
            else if (Op == "granite")
            {
                for (const char *Key : {"detailScale", "grain", "surfaceSeed"}) Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
                Texts["tertiaryColor"] = RecipeText(R, "tertiaryColor");
            }
            else if (Op == "marble")
            {
                for (const char *Key : {"detailScale", "grain", "veinWidth", "surfaceSeed"}) Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
            }
            else if (Op == "concrete")
            {
                for (const char *Key : {"detailScale", "grain", "poreDensity", "wallMode", "surfaceSeed"})
                    Params[Key] = Scalar(R, Key);
                Texts["color"] = RecipeText(R, "color");
                Texts["secondaryColor"] = RecipeText(R, "secondaryColor");
            }
            else if (Op == "scratchedMetal" || Op == "latheMetal")
            {
                for (const char *Key : {Op == "scratchedMetal" ? "grain" : "depth", "detailScale", "roughness",
                         "scratchLength", "scratchWidth", "scratchDepth", "scratchSpread", "scratchBend",
                         "scratchScale", "surfaceSeed"})
                    Params[Key] = Scalar(R, Key);
                Params["scratchDensity"] = 0;
                Params["angle"] = Scalar(R, "scratchAngle");
            }
            else throw std::runtime_error("Pinned family has no bake mapping.");
            std::vector<std::pair<std::vector<uint8_t>, std::string>> Fresh;
            for (const char *Channel : Channels)
            {
                std::string Lower(Channel);
                for (char &Byte : Lower) Byte = char(std::tolower((unsigned char)Byte));
                Fresh.push_back({BakeProceduralChannelRgba(Op, Channel, Params, Texts, 512),
                    Stem + "-" + Lower + ".png"});
            }
            return Fresh;
        };
        // 📝 Pure conductors are value-only: no baked channels, so only the machined/polished families
        // carry a scratch-field bake. Their optional overlay attaches at authoring time.
        static const std::set<std::string> LibraryMetalIds{"polishedMetal", "brushedMetal"};
        size_t PinnedTextures = 0, SolarEntries = 0, GolfEntries = 0, StuccoEntries = 0, MetalEntries = 0,
               BrakeEntries = 0, ClayEntries = 0, LedEntries = 0, FruitEntries = 0, BotanicalEntries = 0,
               LeatherEntries = 0, ExtendedEntries = 0, PolymerEntries = 0, BareMetalEntries = 0;
        for (const auto &Entry : Library.QueryEntries())
        {
            const auto &Id = Entry.Recipe.RecipeId;
            const std::string Stem = Entry.RelativePath.stem().string();
            // 📝 Woodland camouflage presets carry the woven recipe but their authored albedo is the coded
            // camouflage field (MaterialAuthoringValidation pins that bake); weave relief is not yet
            // composited under the field, so no committed weave pair exists for these stems.
            if (Stem.starts_with("woodland-camouflage-fabric")) continue;
            if (Stem.starts_with("camouflage-ballistic-polymer")) continue;
            std::vector<std::pair<std::vector<uint8_t>, std::string>> Fresh;
            if (LibraryWeaveIds.contains(Id)) Fresh = BakeLibraryWeave(Entry.Recipe, Stem);
            else if (Id == "jersey") Fresh = BakeLibraryKnit(Entry.Recipe, Stem);
            else if (Id == "sand" || Id == "layeredSand") Fresh = BakeLibrarySand(Entry.Recipe, Stem);
            else if (Id == "solar")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "solar", {"Albedo", "Orm", "Normal"});
                ++SolarEntries;
            }
            else if (Id == "golf")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "dimples", {"Albedo", "Normal"});
                ++GolfEntries;
            }
            else if (Id == "stucco")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "stucco", {"Albedo", "Normal"});
                ++StuccoEntries;
            }
            else if (Id == "clay")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "clay", {"Albedo", "Normal"});
                ++ClayEntries;
            }
            else if (Id == "led")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "led", {"Albedo", "Normal", "Radiance"});
                ++LedEntries;
            }
            else if (Id == "fruitSurface" || Id == "citrusSurface" || Id == "berrySurface")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "fruit", {"Albedo", "Orm", "Normal"});
                ++FruitEntries;
            }
            else if (Id == "leafSurface")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "leaf", {"Albedo", "Normal"});
                ++BotanicalEntries;
            }
            else if (Id == "grassSurface")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "grass", {"Albedo", "Normal"});
                ++BotanicalEntries;
            }
            else if (Id == "petalSurface")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "petal", {"Albedo", "Normal"});
                ++BotanicalEntries;
            }
            else if (Id == "stemSurface")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "stem", {"Albedo", "Normal"});
                ++BotanicalEntries;
            }
            else if (Id == "cactusSurface")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "cactus", {"Albedo", "Orm", "Normal"});
                ++BotanicalEntries;
            }
            else if (Id == "leather")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "leather", {"Albedo", "Normal"});
                ++LeatherEntries;
            }
            else if (Id == "crocodile")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "crocodile", {"Albedo", "Normal"});
                ++LeatherEntries;
            }
            else if (Id == "wax")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "wax", {"Albedo", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "skin")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "skin", {"Albedo", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "paper")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "paper", {"Albedo", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "rust")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "rust", {"Albedo", "Orm", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "ceramic")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "ceramic", {"Albedo", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "rubber")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "rubber", {"Albedo", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "tiles")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "tiles", {"Albedo", "Orm", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "granite")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "granite", {"Albedo", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "marble")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "marble", {"Albedo", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "concrete")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "concrete", {"Albedo", "Normal"});
                ++ExtendedEntries;
            }
            else if (Id == "glossPlastic" || Id == "grainPlastic" || Id == "engineering" || Id == "wornPlastic")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "polymerGrain", {"Albedo", "Orm", "Normal"});
                ++PolymerEntries;
            }
            else if (Id == "bareMetal")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "metalGrain", {"Albedo", "Normal"});
                ++BareMetalEntries;
            }
            else if (LibraryMetalIds.contains(Id))
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "scratchedMetal", {"Albedo", "Orm", "Normal"});
                ++MetalEntries;
            }
            else if (Id == "brake")
            {
                Fresh = BakeLibraryChannels(Entry.Recipe, Stem, "latheMetal", {"Albedo", "Orm", "Normal"});
                ++BrakeEntries;
            }
            else continue;
            for (const auto &[Pixels, File] : Fresh)
            {
                const auto Encoded = EncodeProceduralPng(Pixels, 512);
                const auto Committed =
                    ReadBytes(std::filesystem::path("EngineContent/MaterialArchives/TextureStudio/Textures") / File);
                Confirm(Committed == Encoded, "Committed procedural texture drifted from its recipe bake.");
                ++PinnedTextures;
            }
        }
        Confirm(SolarEntries == 3 && GolfEntries == 2 && StuccoEntries == 2 && MetalEntries == 2 && BrakeEntries == 1 &&
                ClayEntries == 5 && LedEntries == 2 && FruitEntries == 3 && BotanicalEntries == 8 &&
                LeatherEntries == 5 && ExtendedEntries == 31 && PolymerEntries == 17 && BareMetalEntries == 19,
            "Disputed-family entry count changed.");
        // 📝 Every converted family pins its committed maps: 292 recipe-baked textures in total.
        Confirm(PinnedTextures == 292, "Baked texture family count changed.");
        std::cout << "{\"result\":\"passed\",\"checks\":" << Assertions << "}\n";
        return 0;
    }
    catch (const std::exception &Failure)
    {
        std::cout << "{\"result\":\"failed\",\"error\":\"" << Failure.what() << "\"}\n";
        return 1;
    }
}
