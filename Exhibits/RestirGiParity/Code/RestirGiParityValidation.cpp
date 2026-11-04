/*==========================================================================================================================================
                                                      RESTIRGIPARITYVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 Runs the C++/Vulkan/Slang illumination port against a reference capture of the original demo and reports parity.

#include "ReferenceCapture.h"

#include "Frontier/Engine/Rendering/GlobalIllumination/BoundingStructure.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/IlluminationConstants.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/IlluminationHost.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/OccupantDistanceSpace.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/RayTracingSpace.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/SceneDistanceSpace.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/SurfaceCardDepot.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/SceneStructure.h"
#include "Frontier/Engine/Rendering/GlobalIllumination/ShaderInterchange.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <tuple>

#include "stb_image_write.h"
#include <cstring>
#include <filesystem>
#include <string>

using namespace Frontier;
using namespace Frontier::GlobalIllumination;
using namespace Frontier::Parity;

namespace
{

int Failures = 0;

void ReportCheck(bool Passed, const std::string& Name, const std::string& Detail = "")
{
    std::printf("%s %s%s%s\n", Passed ? "[PASS]" : "[FAIL]", Name.c_str(), Detail.empty() ? "" : " — ", Detail.c_str());
    if (!Passed) ++Failures;
}

// 📝 Byte comparison of a host encoding against the demo's captured upload of the same label.
void CompareUpload(const ReferenceCapture& Capture, const std::string& Label, const std::vector<uint8_t>& Port, int32_t Refresh = -1)
{
    const auto Writes = Capture.QueryWrites(Refresh, Label);
    if (Writes.empty())
    {
        ReportCheck(false, "upload '" + Label + "'", "absent from the capture");
        return;
    }
    const std::vector<uint8_t>& Reference = Writes.front()->Bytes;
    size_t Differing = 0;
    const size_t Count = std::min(Reference.size(), Port.size());
    for (size_t Index = 0; Index < Count; ++Index)
    {
        if (Reference[Index] != Port[Index]) ++Differing;
    }
    char Detail[160];
    std::snprintf(Detail, sizeof(Detail), "%zu B port, %zu B demo, %zu differing bytes", Port.size(), Reference.size(), Differing);
    ReportCheck(Differing == 0 && Reference.size() == Port.size(), "upload '" + Label + "' byte-identical", Detail);
}

void CompareHalfTarget(const ReferenceCapture& Capture, DeviceExchange& Exchange, const std::shared_ptr<DeviceTexture>& Texture,
                       const std::string& Tag, const std::string& Label, double PsnrFloor)
{
    std::vector<uint8_t> Reference;
    if (!Capture.ReadTarget(Tag, Label, Reference))
    {
        std::printf("[SKIP] %s '%s' — not dumped by this capture\n", Tag.c_str(), Label.c_str());
        return;
    }
    std::vector<uint8_t> Port;
    const auto& Specification = Texture->Specification;
    const bool  Volume        = Specification.Dimension == TextureDimension::Volumetric;
    const uint32_t Layers     = Volume ? 1u : Specification.DepthOrLayers;
    for (uint32_t Layer = 0; Layer < Layers; ++Layer)
    {
        std::vector<uint8_t> Slice;
        Exchange.ReadTexture(Texture, Layer, 0, Slice);
        Port.insert(Port.end(), Slice.begin(), Slice.end());
    }
    TexelComparison Comparison;
    switch (Specification.Format)
    {
        case TextureFormat::Rgba16Float:
        case TextureFormat::Rg16Float:
        case TextureFormat::R16Float:     Comparison = CompareHalfTexels(Port, Reference); break;
        case TextureFormat::R32Float:
        case TextureFormat::Rgba32Float:
        case TextureFormat::Depth32Float: Comparison = CompareFloatTexels(Port, Reference); break;
        default:                          Comparison = CompareByteTexels(Port, Reference); break;
    }
    const bool Sized = Port.size() == Reference.size();
    ReportCheck(Sized && Comparison.PeakSignalNoise >= PsnrFloor && Comparison.NonFinite == 0, Tag + " '" + Label + "'",
                (Sized ? "" : "SIZE MISMATCH ") + DescribeComparison(Comparison));
}

// 📝 Replays the demo's writes of one label and refresh into a port region (pure device parity, host inputs identical).
void ReplayWrites(const ReferenceCapture& Capture, DeviceExchange& Exchange, int32_t Refresh, const std::string& Label,
                  const std::shared_ptr<DeviceRegion>& Region)
{
    for (const CapturedWrite* Write : Capture.QueryWrites(Refresh, Label))
    {
        if (Write->Category <= 1) Exchange.WriteRegion(Region, Write->Offset, Write->Bytes.data(), Write->Bytes.size());
    }
}

std::shared_ptr<DeviceRegion> RegionFromCapture(const ReferenceCapture& Capture, DeviceExchange& Exchange, const std::string& Label)
{
    const CapturedWrite* Write = Capture.QueryFirstWrite(Label);
    if (Write == nullptr) return Exchange.ConstructRegion(16, Label);
    return Exchange.ConstructRegionFrom(Write->Bytes.data(), Write->Bytes.size(), Label);
}

// 📝 Minimal JSON reader for one Refreshes.jsonl line: follows a key path, then reads a number or a [number, ...] list.
std::vector<double> ExtractNumbers(const std::string& Line, std::initializer_list<const char*> Path)
{
    size_t Cursor = 0;
    for (const char* Key : Path)
    {
        const std::string Quoted = std::string("\"") + Key + "\":";
        Cursor = Line.find(Quoted, Cursor);
        if (Cursor == std::string::npos) return {};
        Cursor += Quoted.size();
    }
    std::vector<double> Values;
    if (Line[Cursor] == '[')
    {
        ++Cursor;
        while (Cursor < Line.size() && Line[Cursor] != ']')
        {
            char* End = nullptr;
            Values.push_back(std::strtod(Line.c_str() + Cursor, &End));
            Cursor = static_cast<size_t>(End - Line.c_str());
            if (Line[Cursor] == ',') ++Cursor;
        }
        return Values;
    }
    if (Line.compare(Cursor, 4, "true") == 0) return {1.0};
    if (Line.compare(Cursor, 5, "false") == 0) return {0.0};
    Values.push_back(std::strtod(Line.c_str() + Cursor, nullptr));
    return Values;
}

// 📝 Flat lookups in the demo's pretty-printed Settings.json ("Key": value): numbers, booleans and strings.
std::string FindSettingToken(const std::string& Text, const std::string& Key)
{
    const std::string Quoted = "\"" + Key + "\":";
    size_t Cursor = Text.find(Quoted);
    if (Cursor == std::string::npos) return {};
    Cursor += Quoted.size();
    while (Cursor < Text.size() && (Text[Cursor] == ' ' || Text[Cursor] == '\n')) ++Cursor;
    size_t End = Cursor;
    if (End < Text.size() && Text[End] == '"')
    {
        End = Text.find('"', Cursor + 1);
        return End == std::string::npos ? std::string{} : Text.substr(Cursor + 1, End - Cursor - 1);
    }
    while (End < Text.size() && Text[End] != ',' && Text[End] != '\n' && Text[End] != '}') ++End;
    return Text.substr(Cursor, End - Cursor);
}

void ReadSetting(const std::string& Text, const std::string& Key, double& Target)
{
    const std::string Token = FindSettingToken(Text, Key);
    if (!Token.empty()) Target = std::strtod(Token.c_str(), nullptr);
}

void ReadSetting(const std::string& Text, const std::string& Key, bool& Target)
{
    const std::string Token = FindSettingToken(Text, Key);
    if (Token == "true") Target = true;
    if (Token == "false") Target = false;
}

// 📝 The demo's settings at bring-up become the native EnvironmentSettings the host encodes from.
EnvironmentSettings ReadEnvironmentSettings(const std::filesystem::path& Path)
{
    EnvironmentSettings S;
    std::ifstream Stream(Path);
    const std::string Text((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
    if (Text.empty()) return S;
    for (const auto& [Key, Target] : std::initializer_list<std::pair<const char*, double*>>{
             {"SunElevation", &S.SunElevation}, {"SunAzimuth", &S.SunAzimuth}, {"SolarIlluminance", &S.SolarIlluminance},
             {"SunRadius", &S.SunRadius}, {"SunTemperature", &S.SunTemperature}, {"SunGlow", &S.SunGlow}, {"SunTime", &S.SunTime},
             {"DayDuration", &S.DayDuration}, {"Latitude", &S.Latitude}, {"Longitude", &S.Longitude}, {"TimeZone", &S.TimeZone},
             {"StarBrightness", &S.StarBrightness}, {"StarLimit", &S.StarLimit}, {"StarSize", &S.StarSize}, {"StarRotation", &S.StarRotation},
             {"TwinkleDepth", &S.TwinkleDepth}, {"TwinkleRate", &S.TwinkleRate}, {"MilkyWay", &S.MilkyWay}, {"StarHalo", &S.StarHalo},
             {"PlanetBrightness", &S.PlanetBrightness}, {"MoonAzimuth", &S.PrimaryMoon.MoonAzimuth}, {"MoonElevation", &S.PrimaryMoon.MoonElevation},
             {"MoonRoll", &S.PrimaryMoon.MoonRoll}, {"MoonPitch", &S.PrimaryMoon.MoonPitch}, {"MoonSpin", &S.PrimaryMoon.MoonSpin},
             {"MoonSize", &S.PrimaryMoon.MoonSize}, {"MoonBrightness", &S.PrimaryMoon.MoonBrightness}, {"MoonPhase", &S.PrimaryMoon.MoonPhase},
             {"MoonGlow", &S.PrimaryMoon.MoonGlow}, {"FogDensity", &S.FogDensity}, {"FogHeight", &S.FogHeight}, {"FogScatter", &S.FogScatter},
             {"AerialDensity", &S.AerialDensity}, {"AerialStart", &S.AerialStart}, {"AerialMie", &S.AerialMie},
             {"FlareIntensity", &S.FlareIntensity}, {"FlareSpread", &S.FlareSpread}, {"FlareGhosts", &S.FlareGhosts},
             {"FlareGhostBrightness", &S.FlareGhostBrightness}, {"FlareHaloRadius", &S.FlareHaloRadius}, {"FlareHaloWidth", &S.FlareHaloWidth},
             {"FlareHaloBrightness", &S.FlareHaloBrightness}, {"FlareStreak", &S.FlareStreak}, {"FlareRotation", &S.FlareRotation},
             {"FlareRays", &S.FlareRays}, {"FlareChromatic", &S.FlareChromatic}, {"FlareGhostSpacing", &S.FlareGhostSpacing},
             {"FlareGhostShape", &S.FlareGhostShape}, {"FlareAperture", &S.FlareAperture}, {"HorizonGlow", &S.HorizonGlow},
             {"WhiteLine", &S.WhiteLine}, {"Rayleigh", &S.Atmosphere.Rayleigh}, {"Mie", &S.Atmosphere.Mie}, {"Ozone", &S.Atmosphere.Ozone},
             {"MieG", &S.Atmosphere.MieG}, {"RayleighHeight", &S.Atmosphere.RayleighHeight}, {"MieHeight", &S.Atmosphere.MieHeight}})
        ReadSetting(Text, Key, *Target);
    double MoonBody = 0.0;
    ReadSetting(Text, "MoonBody", MoonBody);
    S.PrimaryMoon.MoonBody = static_cast<uint32_t>(MoonBody);
    for (const auto& [Key, Target] : std::initializer_list<std::pair<const char*, bool*>>{
             {"SunEnabled", &S.SunEnabled}, {"SunDiscEnabled", &S.SunDiscEnabled}, {"SunAutoPosition", &S.SunAutoPosition},
             {"DayAnimate", &S.DayAnimate}, {"StarsEnabled", &S.StarsEnabled}, {"TwinkleEnabled", &S.TwinkleEnabled},
             {"PlanetsEnabled", &S.PlanetsEnabled}, {"MoonEnabled", &S.PrimaryMoon.MoonEnabled}, {"MoonFollowSky", &S.PrimaryMoon.MoonFollowSky},
             {"FogEnabled", &S.FogEnabled}, {"AerialEnabled", &S.AerialEnabled}, {"FlareEnabled", &S.FlareEnabled},
             {"FlareAnamorphic", &S.FlareAnamorphic}, {"FlareWarmStreak", &S.FlareWarmStreak}, {"FlareBurst", &S.FlareBurst},
             {"LineAtCivilOnly", &S.LineAtCivilOnly}, {"AtmosphereEnabled", &S.AtmosphereEnabled}, {"AirMass", &S.Atmosphere.AirMass},
             {"MoonLightEnabled", &S.Atmosphere.MoonLightEnabled}, {"MultipleScattering", &S.Atmosphere.MultipleScattering}})
        ReadSetting(Text, Key, *Target);
    ReadSetting(Text, "MoonLightIntensity", S.Atmosphere.MoonLightIntensity);
    S.FlareCustomLayers = FindSettingToken(Text, "FlarePreset") == "Custom layers";
    const std::string Date = FindSettingToken(Text, "SkyDate");
    if (!Date.empty()) S.SkyDate = Date;
    return S;
}

} // namespace

int main(int ArgumentCount, char** Arguments)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // 🔍 Demo-versus-demo noise floor: the same targets from two independent runs of the original demo.
    if (ArgumentCount >= 4 && std::string(Arguments[1]) == "--compare-runs")
    {
        const std::filesystem::path First = Arguments[2], Second = Arguments[3];
        for (const auto& Entry : std::filesystem::directory_iterator(First))
        {
            std::vector<uint8_t> Left, Right;
            std::ifstream LeftStream(Entry.path(), std::ios::binary), RightStream(Second / Entry.path().filename(), std::ios::binary);
            if (!RightStream) continue;
            Left.assign(std::istreambuf_iterator<char>(LeftStream), std::istreambuf_iterator<char>());
            Right.assign(std::istreambuf_iterator<char>(RightStream), std::istreambuf_iterator<char>());
            const std::string Name = Entry.path().filename().string();
            if (Name.find("Reservoirs") != std::string::npos)
            {
                size_t Identical = 0;
                for (size_t Index = 0; Index + 32 <= Left.size(); Index += 32) Identical += std::memcmp(Left.data() + Index, Right.data() + Index, 32) == 0;
                std::printf("%s — %.4f %% of reservoirs bit-identical between demo runs\n", Name.c_str(), 100.0 * Identical / std::max<size_t>(1, Left.size() / 32));
                continue;
            }
            const bool Bytes = Name.find("Capture") != std::string::npos;
            const bool Floats = Name.find("depth") != std::string::npos || Name.find("Shadow") != std::string::npos || Name.find("sites") != std::string::npos;
            const TexelComparison Comparison = Bytes ? CompareByteTexels(Left, Right) : (Floats ? CompareFloatTexels(Left, Right) : CompareHalfTexels(Left, Right));
            std::printf("%s — demo A vs demo B: %s\n", Name.c_str(), DescribeComparison(Comparison).c_str());
        }
        return 0;
    }
    if (ArgumentCount < 4)
    {
        std::printf("usage: RestirGiParityValidation <SpirvFolder> <CaptureFolder> <OutFolder> [Quality] [Refreshes] [Width] [Height]\n");
        return 2;
    }
    const std::filesystem::path SpirvFolder   = Arguments[1];
    const std::filesystem::path CaptureFolder = Arguments[2];
    const std::filesystem::path OutFolder     = Arguments[3];
    const std::string           Quality       = ArgumentCount > 4 ? Arguments[4] : "low";
    const uint32_t              Refreshes     = ArgumentCount > 5 ? static_cast<uint32_t>(std::atoi(Arguments[5])) : 12u;
    const uint32_t              Width         = ArgumentCount > 6 ? static_cast<uint32_t>(std::atoi(Arguments[6])) : 288u;
    const uint32_t              Height        = ArgumentCount > 7 ? static_cast<uint32_t>(std::atoi(Arguments[7])) : 162u;
    const bool                  Live          = ArgumentCount > 8 && std::string(Arguments[8]) == "live";
    std::vector<std::string>    RefreshLines;
    if (Live)
    {
        std::ifstream Stream(CaptureFolder / "Refreshes.jsonl");
        for (std::string Line; std::getline(Stream, Line);) RefreshLines.push_back(Line);
        std::printf("live host encoders: %zu captured camera/light refreshes\n", RefreshLines.size());
    }
    std::filesystem::create_directories(OutFolder);

    std::string      Diagnostic;
    ReferenceCapture Capture;
    if (!Capture.Load(CaptureFolder, &Diagnostic))
    {
        std::printf("capture: %s\n", Diagnostic.c_str());
        return 2;
    }
    std::printf("capture: %zu host → device writes\n", Capture.Writes.size());
    DeviceExchange Exchange;
    if (!Exchange.Initialize("", &Diagnostic))
    {
        std::printf("device: %s\n", Diagnostic.c_str());
        return 2;
    }
    std::printf("device: %s\n", Exchange.AdapterDescription().c_str());

    // ① assets the demo loads from files (materials, moon atlas, star catalogue) come from the captured uploads
    IlluminationAssets Assets;
    Assets.FacetSpace = CaptureFolder / "Scene.facetspace";
    for (const CapturedWrite& Write : Capture.Writes)
    {
        if (Write.Refresh != -1) continue;
        if (Write.Label == "Materials" && Write.Category == 1) Assets.MaterialProfiles = Write.Bytes;
        if (Write.Label == "Materials" && Write.Category == 0) Assets.MaterialRewrites.push_back({static_cast<uint32_t>(Write.Offset), Write.Bytes});
        if (Write.Label == "Native six-body lunar atlas · 2K sRGB" && Write.Category == 2) Assets.MoonAtlas = Write.Bytes;
        if (Write.Label == "Native star catalogue cells + records" && Write.Category == 2)
        {
            Assets.StarCatalogue     = Write.Bytes;
            Assets.StarCatalogueRows = static_cast<uint32_t>(Write.Bytes.size() / (1024 * 16));
        }
    }
    IlluminationSettings Settings;
    Settings.Quality = Quality;
    EnvironmentSettings Environment = ReadEnvironmentSettings(CaptureFolder / "Settings.json");

    // 🔍 every host write is observed and compared with the demo's write of the same refresh and label
    std::map<std::string, std::string> LabelMap = {
        {"Ray tracing entries (TLAS + BLAS)", "RayTracing BVH nodes (TLAS + BLAS)"}, {"Ray tracing BLAS triangles", "RayTracing BLAS triangles"},
        {"Card profiles", "Card records"}, {"Occupant profiles", "Occupant records"}, {"Atmosphere constants", "Atmosphere record"},
        {"Refresh constants", "Frame record"}, {"Distance constants", "Distance record"}, {"Clustered luminaire profiles", "Clustered light records"},
        {"Cascade profiles", "Cascade records"}};
    std::vector<std::tuple<std::string, uint64_t, std::vector<uint8_t>>> Observed;
    bool Observing = true;
    Exchange.WriteObserver = [&](const std::string& Label, VkDeviceSize Offset, const void* Bytes, VkDeviceSize Count) {
        if (!Observing) return;
        const auto Mapped = LabelMap.find(Label);
        Observed.emplace_back(Mapped == LabelMap.end() ? Label : Mapped->second, Offset,
                              std::vector<uint8_t>(static_cast<const uint8_t*>(Bytes), static_cast<const uint8_t*>(Bytes) + Count));
    };
    auto CompareObserved = [&](int32_t Refresh, const std::set<std::string>& Labels) {
        for (const std::string& Label : Labels)
        {
            const auto Reference = Capture.QueryWrites(Refresh, Label);
            std::vector<const std::tuple<std::string, uint64_t, std::vector<uint8_t>>*> Port;
            for (const auto& Entry : Observed) if (std::get<0>(Entry) == Label) Port.push_back(&Entry);
            // 📝 Creation uploads (category 1) of the demo correspond to the port's first write of a fresh region.
            std::vector<const CapturedWrite*> Demo;
            for (const CapturedWrite* Write : Reference) Demo.push_back(Write);
            // 📝 Fresh regions are zero-filled by the port first (WebGPU zero initialisation): all-zero writes are not inputs.
            auto Meaningful = [](const std::vector<uint8_t>& Bytes) { return std::any_of(Bytes.begin(), Bytes.end(), [](uint8_t B) { return B != 0; }); };
            std::vector<const std::tuple<std::string, uint64_t, std::vector<uint8_t>>*> PortInputs;
            for (const auto* Entry : Port) if (Meaningful(std::get<2>(*Entry))) PortInputs.push_back(Entry);
            std::vector<const CapturedWrite*> DemoInputs;
            for (const CapturedWrite* Write : Demo) if (Meaningful(Write->Bytes)) DemoInputs.push_back(Write);
            bool Same = !DemoInputs.empty() && DemoInputs.size() == PortInputs.size();
            size_t Differing = 0, Bytes = 0;
            for (size_t Index = 0; Same && Index < DemoInputs.size(); ++Index)
            {
                const auto& PortBytes = std::get<2>(*PortInputs[Index]);
                const auto& DemoBytes = DemoInputs[Index]->Bytes;
                Bytes += DemoBytes.size();
                if (PortBytes.size() != DemoBytes.size() || std::get<1>(*PortInputs[Index]) != DemoInputs[Index]->Offset) { Same = false; break; }
                for (size_t Byte = 0; Byte < DemoBytes.size(); ++Byte) if (PortBytes[Byte] != DemoBytes[Byte]) ++Differing;
            }
            Same = Same && Differing == 0;
            if (!Same && Label == "Frame record" && !DemoInputs.empty() && !PortInputs.empty())
            {
                const auto& PortBytes = std::get<2>(*PortInputs.back());
                const auto& DemoBytes = DemoInputs.back()->Bytes;
                std::string Scalars;
                for (size_t Scalar = 0; Scalar * 4 + 4 <= std::min(PortBytes.size(), DemoBytes.size()); ++Scalar)
                {
                    if (std::memcmp(PortBytes.data() + Scalar * 4, DemoBytes.data() + Scalar * 4, 4) != 0)
                    {
                        float PortValue, DemoValue;
                        std::memcpy(&PortValue, PortBytes.data() + Scalar * 4, 4);
                        std::memcpy(&DemoValue, DemoBytes.data() + Scalar * 4, 4);
                        char Item[96];
                        std::snprintf(Item, sizeof(Item), " [%zu] %.9g vs %.9g", Scalar, PortValue, DemoValue);
                        Scalars += Item;
                    }
                }
                std::printf("    differing scalars:%s\n", Scalars.c_str());
            }
            char Detail[160];
            std::snprintf(Detail, sizeof(Detail), "%zu demo / %zu port writes, %zu B, %zu differing bytes", DemoInputs.size(), PortInputs.size(), Bytes, Differing);
            ReportCheck(Same, "refresh " + std::to_string(Refresh) + " host write '" + Label + "'", Detail);
        }
    };

    IlluminationHost Host(Exchange, SpirvFolder);
    if (!Host.Construct(Assets, Settings, {Width, Height}, &Diagnostic))
    {
        std::printf("bring-up: %s\n", Diagnostic.c_str());
        return 2;
    }
    std::printf("bring-up: %zu occupants, %zu cards, %u TLAS entries, trace %u × %u\n", Host.Scene.Occupants.size(), Host.Cards->Cards.size(),
                Host.RayTracing->UsedTlasEntries, Host.TraceExtent[0], Host.TraceExtent[1]);
    CompareObserved(-1, {"Facet vertices", "RayTracing BLAS triangles", "Card records", "Distance list", "Brick stamps"});
    {
        // 📝 Bring-up occupant profiles and TLAS: the demo's queue writes after the creation uploads.
        const auto DemoOccupants = Capture.QueryWrites(-1, "Occupant records");
        const auto DemoTlas      = Capture.QueryWrites(-1, "RayTracing BVH nodes (TLAS + BLAS)");
        std::vector<uint8_t> PortOccupants, PortTlas;
        for (const auto& Entry : Observed)
        {
            if (std::get<0>(Entry) == "Occupant records" && std::get<2>(Entry).size() > 16 && std::any_of(std::get<2>(Entry).begin(), std::get<2>(Entry).end(), [](uint8_t B) { return B != 0; }))
                PortOccupants = std::get<2>(Entry);
            if (std::get<0>(Entry) == "RayTracing BVH nodes (TLAS + BLAS)" && std::get<1>(Entry) == 0 && std::get<2>(Entry).size() < 100000) PortTlas = std::get<2>(Entry);
        }
        ReportCheck(!DemoOccupants.empty() && DemoOccupants.back()->Bytes == PortOccupants, "bring-up occupant profiles byte-identical",
                    std::to_string(PortOccupants.size()) + " B");
        ReportCheck(!DemoTlas.empty() && DemoTlas.back()->Bytes == PortTlas, "bring-up TLAS byte-identical", std::to_string(PortTlas.size()) + " B");
    }
    CompareHalfTarget(Capture, Exchange, Host.Volumes->Texture, "BringUp", "Occupant distance space", 60.0);
    CompareHalfTarget(Capture, Exchange, Host.Cards->AlbedoTexture, "BringUp", "Card albedo", 40.0);
    CompareHalfTarget(Capture, Exchange, Host.Cards->NormalTexture, "BringUp", "Card normal", 40.0);
    CompareHalfTarget(Capture, Exchange, Host.Cards->IncandescenceTexture, "BringUp", "Card emission", 40.0);
    CompareHalfTarget(Capture, Exchange, Host.Cards->DepthTexture, "BringUp", "Card depth", 40.0);

    // ② refreshes: scene motion and the camera-/physics-/effect-driven buffers replayed from the capture
    for (uint32_t Refresh = 0; Refresh < Refreshes; ++Refresh)
    {
        Observed.clear();
        const int32_t Index = static_cast<int32_t>(Refresh);
        auto Prepare = [&](uint32_t) {
            for (const CapturedWrite* Write : Capture.QueryWrites(Index, "Occupant records"))
            {
                const uint32_t First = static_cast<uint32_t>(Write->Offset / OccupantStride);
                const uint32_t Count = static_cast<uint32_t>(Write->Bytes.size() / OccupantStride);
                for (uint32_t Local = 0; Local < Count; ++Local)
                {
                    float Scalars[84];
                    std::memcpy(Scalars, Write->Bytes.data() + static_cast<size_t>(Local) * OccupantStride, sizeof(Scalars));
                    OccupantEntry& Occupant = Host.Scene.Occupants[First + Local];
                    for (int Element = 0; Element < 16; ++Element)
                    {
                        Occupant.Current[Element]  = Scalars[Element];
                        Occupant.Previous[Element] = Scalars[32 + Element];
                    }
                }
            }
        };
        RefreshInputs Inputs;
        const bool    LiveRefresh = Live && Refresh < RefreshLines.size();
        if (LiveRefresh)
        {
            const std::string& Line = RefreshLines[Refresh];
            const auto Position = ExtractNumbers(Line, {"Camera", "Position"});
            const auto Target   = ExtractNumbers(Line, {"Camera", "Target"});
            const auto Forward  = ExtractNumbers(Line, {"Camera", "Forward"});
            const auto Up       = ExtractNumbers(Line, {"Camera", "Up"});
            const auto Focus    = ExtractNumbers(Line, {"Focus"});
            Inputs.Camera.Position = {Position[0], Position[1], Position[2]};
            Inputs.Camera.Target   = {Target[0], Target[1], Target[2]};
            Inputs.Camera.Forward  = {Forward[0], Forward[1], Forward[2]};
            Inputs.Camera.Up       = {Up[0], Up[1], Up[2]};
            Inputs.Camera.VerticalFieldOfView = ExtractNumbers(Line, {"Camera", "VerticalFieldOfView"})[0];
            Inputs.Camera.Aspect   = ExtractNumbers(Line, {"Camera", "Aspect"})[0];
            Inputs.Camera.Near     = ExtractNumbers(Line, {"Camera", "Near"})[0];
            Inputs.Focus           = {Focus[0], Focus[1], Focus[2]};
            // 📝 The environment block and the luminaire table come from the native ports (EncodeEnvironment,
            //    LightingSystem.Update); the comparison below checks them byte for byte against the demo's writes.
            Inputs.Environment    = &Environment;
            Inputs.LuminairesLive = true;
            const auto Body = ExtractNumbers(Line, {"VehicleBody"});
            if (Body.size() == 16)
            {
                Transform VehicleBody{};
                std::copy(Body.begin(), Body.end(), VehicleBody.begin());
                Inputs.VehicleTransform = VehicleBody;
            }
            // 🔍 the orbital solver reproduces the demo's planets and sidereal angle
            const CelestialSolution Solved = SolveCelestial(Environment);
            double PlanetDeviation = 0.0;
            for (int Planet = 0; Planet < 4; ++Planet)
            {
                const std::string Named = std::string("\"Name\":\"") + std::array<const char*, 4>{"Venus", "Jupiter", "Mars", "Saturn"}[Planet] + "\"";
                const size_t      Entry = Line.find(Named);
                const auto Logged = Entry == std::string::npos ? std::vector<double>{} : ExtractNumbers(Line.substr(Entry), {"Direction"});
                for (int Axis = 0; Axis < 3 && Logged.size() == 3; ++Axis)
                    PlanetDeviation = std::max(PlanetDeviation, std::abs(Logged[Axis] - Solved.Planets[Planet].Direction[Axis]));
            }
            // 📝 Doubles may differ in the last bits (V8's fdlibm vs this libm); the float32 'Frame record' bytes below are the
            //    1:1 criterion, this check bounds the double-precision drift of the solver itself.
            const auto   Logged   = ExtractNumbers(Line, {"Light", "Sidereal"});
            const double Sidereal = Logged.empty() ? 0.0 : std::abs(Logged[0] - ResolveSiderealAngle(Solved.JulianDay, Environment.Longitude));
            char Detail[96];
            std::snprintf(Detail, sizeof(Detail), "max planet Δ %.3g, sidereal Δ %.3g", PlanetDeviation, Sidereal);
            ReportCheck(PlanetDeviation < 1.0e-12 && Sidereal < 1.0e-12, "refresh " + std::to_string(Refresh) + " celestial solver", Detail);
            // 📝 Scalars owned by systems outside the port come from the demo's own refresh constants.
            const auto Constants = Capture.QueryWrites(Index, "Frame record");
            if (!Constants.empty())
            {
                const auto& Bytes = Constants.back()->Bytes;
                float Elapsed;
                std::memcpy(&Elapsed, Bytes.data() + 87 * 4, 4);
                Inputs.External.ElapsedSeconds = Elapsed;
                std::memcpy(&Inputs.External.PresentationControl, Bytes.data() + 119 * 4, 4);
                std::memcpy(Inputs.External.FireBlock.data(), Bytes.data() + 120 * 4, 32);
            }
        }
        auto Supply = [&](uint32_t) {
            Observing = false;
            if (!LiveRefresh)
            {
                ReplayWrites(Capture, Exchange, Index, "Occupant records", Host.OccupantRegion);
                ReplayWrites(Capture, Exchange, Index, "Frame record", Host.RefreshRegion);
                ReplayWrites(Capture, Exchange, Index, "Cascade records", Host.Shadow->CascadeRegion);
                ReplayWrites(Capture, Exchange, Index, "Cascade receiver", Host.Shadow->ReceiverRegion);
                ReplayWrites(Capture, Exchange, Index, "Distance record", Host.Distance->UniformRegion);
                Host.Distance->RefreshBricks.clear();
                for (const CapturedWrite* Write : Capture.QueryWrites(Index, "Invalidated bricks"))
                {
                    Host.Distance->RefreshBricks.resize(Write->Bytes.size() / 4);
                    std::memcpy(Host.Distance->RefreshBricks.data(), Write->Bytes.data(), Write->Bytes.size());
                    Exchange.WriteRegion(Host.Distance->BrickRegion, 0, Write->Bytes.data(), Write->Bytes.size());
                }
            }
            else
            {
                // 📝 Occupants moved by systems outside the port (cones settling) keep the demo's profiles.
                for (const CapturedWrite* Write : Capture.QueryWrites(Index, "Occupant records"))
                    if (Write->Offset != 0) Exchange.WriteRegion(Host.OccupantRegion, Write->Offset, Write->Bytes.data(), Write->Bytes.size());
            }
            if (!LiveRefresh) ReplayWrites(Capture, Exchange, Index, "Clustered light records", Host.LuminaireRegion);
            ReplayWrites(Capture, Exchange, Index, "Vehicle record", Host.VehicleRegion);
            ReplayWrites(Capture, Exchange, Index, "Effect record", Host.EffectRegion);
            ReplayWrites(Capture, Exchange, Index, "Projected cloud shadows", Host.CloudShadowParams);
            ReplayWrites(Capture, Exchange, Index, "Cloud renderer v2", Host.CloudParams);
            Observing = true;
        };
        if (!Host.AdvanceRefresh(LiveRefresh ? &Inputs : nullptr, Prepare, Supply, &Diagnostic))
        {
            ReportCheck(false, "refresh " + std::to_string(Refresh), Diagnostic);
            break;
        }
        std::set<std::string> HostLabels = {"RayTracing BVH nodes (TLAS + BLAS)", "Card tiles", "Card work"};
        if (Refresh == 0) HostLabels.insert({"Atmosphere record", "Cluster triangle indices", "Cluster instance identities", "Geometry cluster bounds"});
        if (LiveRefresh)
        {
            HostLabels.insert({"Frame record", "Cascade records", "Cascade receiver", "Distance record", "Clustered light records"});
            if (!Capture.QueryWrites(Index, "Invalidated bricks").empty()) HostLabels.insert("Invalidated bricks");
        }
        CompareObserved(Index, HostLabels);
        const std::string Tag = "F" + std::to_string(Refresh);
        std::vector<uint8_t> Probe;
        if (!Capture.ReadTarget(Tag, "Sky view", Probe) && !Capture.ReadTarget(Tag, "Surface depth", Probe) && !Capture.ReadTarget(Tag, "Capture", Probe)) continue;
        std::printf("--- refresh %u targets\n", Refresh);
        CompareHalfTarget(Capture, Exchange, Host.Atmosphere->SkyTexture, Tag, "Sky view", 50.0);
        CompareHalfTarget(Capture, Exchange, Host.Atmosphere->TransmittanceTexture, Tag, "Atmosphere transmittance 256×64", 50.0);
        CompareHalfTarget(Capture, Exchange, Host.Atmosphere->MultipleTexture, Tag, "Atmosphere multiple scattering 32×32", 50.0);
        for (uint32_t Level = 0; Level < 4; ++Level)
            CompareHalfTarget(Capture, Exchange, Host.Distance->Textures[Level], Tag, "Scene distance cascade " + std::to_string(Level), 60.0);
        CompareHalfTarget(Capture, Exchange, Host.Shadow->DepthTexture, Tag, "Shadow cascades", 50.0);
        CompareHalfTarget(Capture, Exchange, Host.Cards->DirectTexture, Tag, "Card direct irradiance", 40.0);
        CompareHalfTarget(Capture, Exchange, Host.Cards->FinalTexture, Tag, "Card final radiance", 40.0);
        CompareHalfTarget(Capture, Exchange, Host.Surface->DepthTexture, Tag, "Surface depth", 50.0);
        CompareHalfTarget(Capture, Exchange, Host.Surface->AttributeTexture, Tag, "Surface attributes", 40.0);
        CompareHalfTarget(Capture, Exchange, Host.Surface->MotionTexture, Tag, "Surface motion", 40.0);
        CompareHalfTarget(Capture, Exchange, Host.Surface->DirectTexture, Tag, "Direct radiance", 35.0);
        CompareHalfTarget(Capture, Exchange, Host.Resampling->GuideTextures[0], Tag, "Resampling guide A", 35.0);
        CompareHalfTarget(Capture, Exchange, Host.Resampling->GuideTextures[1], Tag, "Resampling guide B", 35.0);
        CompareHalfTarget(Capture, Exchange, Host.Resampling->IrradianceTexture, Tag, "Resampled irradiance", 25.0);
        CompareHalfTarget(Capture, Exchange, Host.Resampling->ReflectionTexture, Tag, "Reflection radiance", 25.0);
        CompareHalfTarget(Capture, Exchange, Host.Diffuse->IntegratedTexture, Tag, "Diffuse integrated", 25.0);
        CompareHalfTarget(Capture, Exchange, Host.Diffuse->ScreenTexture, Tag, "Diffuse screen", 25.0);
        CompareHalfTarget(Capture, Exchange, Host.Glossy->IntegratedTexture, Tag, "Glossy integrated", 25.0);
        CompareHalfTarget(Capture, Exchange, Host.Glossy->ScreenTexture, Tag, "Glossy screen", 25.0);
        CompareHalfTarget(Capture, Exchange, Host.Display->RecombinedTexture, Tag, "Recombined radiance", 25.0);
        CompareHalfTarget(Capture, Exchange, Host.Display->ResolvedTextures[0], Tag, "Resolved radiance A", 25.0);
        CompareHalfTarget(Capture, Exchange, Host.Display->ResolvedTextures[1], Tag, "Resolved radiance B", 25.0);
        CompareHalfTarget(Capture, Exchange, Host.CaptureTexture, Tag, "Capture", 25.0);
        for (uint32_t Slot = 0; Slot < 2; ++Slot)
        {
            std::vector<uint8_t> Reference, Port;
            if (Capture.ReadTarget(Tag, "Reservoirs " + std::to_string(Slot), Reference) && Exchange.ReadRegion(Host.Resampling->Reservoirs[Slot], 0, Reference.size(), Port))
            {
                size_t Identical = 0;
                for (size_t Index = 0; Index + 32 <= Reference.size(); Index += 32) Identical += std::memcmp(Reference.data() + Index, Port.data() + Index, 32) == 0;
                const double Fraction = 100.0 * static_cast<double>(Identical) / static_cast<double>(std::max<size_t>(1, Reference.size() / 32));
                // 📝 Two runs of the demo itself agree on ~94 % of reservoirs (card radiosity race, README §Noise floor).
                ReportCheck(Fraction > 85.0, Tag + " 'Reservoirs " + std::to_string(Slot) + "'", std::to_string(Fraction) + " % of reservoirs bit-identical");
            }
        }
        // 📝 Demo | port | difference (×8) strip of the presented image, 8-bit sRGB as presented.
        std::vector<uint8_t> Present, Reference;
        Exchange.ReadTexture(Host.CaptureTexture, 0, 0, Present);
        if (Capture.ReadTarget(Tag, "Capture", Reference) && Reference.size() == Present.size())
        {
            std::vector<uint8_t> Strip(static_cast<size_t>(Width) * 3 * Height * 4, 255u);
            for (uint32_t Y = 0; Y < Height; ++Y)
            {
                for (uint32_t X = 0; X < Width; ++X)
                {
                    const size_t Source = (static_cast<size_t>(Y) * Width + X) * 4;
                    for (int Channel = 0; Channel < 3; ++Channel)
                    {
                        Strip[(static_cast<size_t>(Y) * Width * 3 + X) * 4 + Channel]             = Reference[Source + Channel];
                        Strip[(static_cast<size_t>(Y) * Width * 3 + Width + X) * 4 + Channel]     = Present[Source + Channel];
                        const int Difference = std::abs(static_cast<int>(Present[Source + Channel]) - static_cast<int>(Reference[Source + Channel])) * 8;
                        Strip[(static_cast<size_t>(Y) * Width * 3 + 2 * Width + X) * 4 + Channel] = static_cast<uint8_t>(std::min(255, Difference));
                    }
                }
            }
            const std::string StripPath = (OutFolder / ("Parity_" + Tag + ".png")).string();
            stbi_write_png(StripPath.c_str(), static_cast<int>(Width * 3), static_cast<int>(Height), 4, Strip.data(), static_cast<int>(Width * 3 * 4));
        }
        const std::string PresentPath = (OutFolder / ("Port_" + Tag + ".png")).string();
        stbi_write_png(PresentPath.c_str(), static_cast<int>(Width), static_cast<int>(Height), 4, Present.data(), static_cast<int>(Width * 4));
    }

    std::printf("\n=== parity: %d failing checks\n", Failures);
    return Failures == 0 ? 0 : 1;
}
