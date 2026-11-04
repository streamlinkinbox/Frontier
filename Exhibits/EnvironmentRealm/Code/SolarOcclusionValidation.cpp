/*==========================================================================================================================================
                                                     SOLAROCCLUSIONVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 The sun behind the PlanetaryAtmosphere clouds. A dense cloud across the solar disc must hide it (the disc no brighter than the
//    cloud around it), the same cloud beside the sun must be lit from behind (brighter than with the sun far from it), and the
//    Everest level's cumulus deck is rendered looking into the sun for inspection. Prints every figure it checks.
//    Before the disc attenuation and the silver-lining lobe (SwiftShader): ① disc 1.61x the cloud, ② 1.89x; after: 1.03x, 2.58x.

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/PlanetaryAtmosphere/CloudSpace.h"
#include "Engine/PlanetaryAtmosphere/OpticalLensFlare.h"
#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"
#include "Engine/PlanetaryAtmosphere/PlanetaryProjection.h"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

constexpr float Pi               = 3.14159265358979323846f;
constexpr float DegreesToRadians = Pi / 180.0f;

bool MeasureOnly = false;   // --measure-only: print every figure, fail no check (before/after comparisons)

void Require(bool Condition, const std::string& Message)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[SolarOcclusionValidation] FAILED: %s\n", Message.c_str());
        std::exit(1);
    }
}

// 📝 A measured check: fatal unless --measure-only.
void Expect(bool Condition, const std::string& Message)
{
    if (!Condition && MeasureOnly)
    {
        std::fprintf(stderr, "[SolarOcclusionValidation] NOT MET: %s\n", Message.c_str());
        return;
    }
    Require(Condition, Message);
}

using Direction = std::array<float, 3>;

Direction Normalized(Direction Vector)
{
    const float Length = std::sqrt(Vector[0] * Vector[0] + Vector[1] * Vector[1] + Vector[2] * Vector[2]);
    return {Vector[0] / Length, Vector[1] / Length, Vector[2] / Length};
}

Direction Crossed(Direction A, Direction B)
{
    return {A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]};
}

float Dotted(Direction A, Direction B)
{
    return A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
}

// 📝 The environment's y-up axes (x east, y up, z south) from an elevation and an azimuth measured from north towards east.
Direction SkyDirection(float ElevationDegrees, float AzimuthDegrees)
{
    const float Elevation = ElevationDegrees * DegreesToRadians;
    const float Azimuth   = AzimuthDegrees * DegreesToRadians;
    return {std::cos(Elevation) * std::sin(Azimuth), std::sin(Elevation), -std::cos(Elevation) * std::cos(Azimuth)};
}

PlanetaryCameraView LookAlong(Direction Forward, uint32_t Width, uint32_t Height, float FieldOfViewDegrees)
{
    PlanetaryCameraView View{};
    View.PositionMeters = {0.0f, 2.0f, 0.0f};
    View.ForwardUnit    = Normalized(Forward);
    View.RightUnit      = Normalized(Crossed({0.0f, 1.0f, 0.0f}, View.ForwardUnit));
    View.UpUnit         = Normalized(Crossed(View.ForwardUnit, View.RightUnit));
    View.VerticalFovRad = FieldOfViewDegrees * DegreesToRadians;
    View.ViewportWidth  = Width;
    View.ViewportHeight = Height;
    View.ExposureScale  = 4.0f;
    return View;
}

// 📝 PlanetaryRecords.slang CameraRayDirection at a pixel centre.
Direction PixelDirection(const PlanetaryCameraView& View, uint32_t X, uint32_t Y)
{
    const float TanHalfY = std::tan(0.5f * View.VerticalFovRad);
    const float Aspect   = static_cast<float>(View.ViewportWidth) / static_cast<float>(View.ViewportHeight);
    const float U        = (static_cast<float>(X) + 0.5f) / static_cast<float>(View.ViewportWidth);
    const float V        = (static_cast<float>(Y) + 0.5f) / static_cast<float>(View.ViewportHeight);
    const float NdcX     = (U * 2.0f - 1.0f) * Aspect * TanHalfY;
    const float NdcY     = (1.0f - V * 2.0f) * TanHalfY;
    return Normalized({View.ForwardUnit[0] + View.RightUnit[0] * NdcX + View.UpUnit[0] * NdcY,
                       View.ForwardUnit[1] + View.RightUnit[1] * NdcX + View.UpUnit[1] * NdcY,
                       View.ForwardUnit[2] + View.RightUnit[2] * NdcX + View.UpUnit[2] * NdcY});
}

float Luminance(const std::vector<float>& Hdr, size_t Pixel)
{
    return 0.2126f * Hdr[Pixel * 4u + 0u] + 0.7152f * Hdr[Pixel * 4u + 1u] + 0.0722f * Hdr[Pixel * 4u + 2u];
}

// 📝 The Everest sky: the level's sun (38° up, azimuth 132°) at the environment's radiometric scale, no flare, no moons.
PlanetaryRealmEnsemble MakeSky(Direction Sun, uint64_t Revision)
{
    PlanetaryRealmEnsemble Ensemble{};
    Ensemble.Realms.resize(1u);
    PlanetaryRealm& Realm     = Ensemble.Realms[0];
    Realm.Name                = "SolarOcclusion";
    Realm.SkyViewRevision     = Revision;
    Realm.StarBrightnessScale = 0.0f;
    Realm.MilkyWayIntensity   = 0.0f;
    RealmSolarSource Solar{};
    Solar.DirectionUnit         = Sun;
    Solar.Disc.AngularRadiusRad = 0.5 * 0.53 * DegreesToRadians;
    Solar.EnableLensFlare       = false;
    Realm.Suns.push_back(Solar);
    return Ensemble;
}

// 📝 A dense cumulus box centred along Centre, Distance metres out, HalfExtent metres each way.
CloudVolumeProfile MakeCloudBox(Direction Centre, float Distance, float HalfExtent)
{
    CloudVolumeProfile Box{};
    Box.Domain              = CloudVolumeDomain::BoundedLocalBox;
    Box.HeightProfile       = TroposphericCloudProfile::Cumulus;
    Box.BoxCenterM          = {Centre[0] * Distance, 2.0 + Centre[1] * Distance, Centre[2] * Distance};
    Box.BoxHalfExtentsM     = {HalfExtent, HalfExtent, HalfExtent};
    Box.BoxEdgeFalloffM     = 0.2 * HalfExtent;
    Box.Slab.Coverage       = 0.95;
    Box.Slab.DensityScale   = 1.4;
    Box.Slab.ExtinctionPerM = 0.06;
    return Box;
}

// 📝 The Everest level's cloud record (Default.levelspace: coverage 0.42, base 1750 m, 1350 m deep) as EnvironmentOverlay maps it.
CloudVolumeProfile MakeEverestDeck()
{
    CloudVolumeProfile Deck{};
    Deck.Domain            = CloudVolumeDomain::PlanetarySlab;
    Deck.HeightProfile     = TroposphericCloudProfile::Cumulus;
    Deck.Slab.LowerHeightM = 1750.0;
    Deck.Slab.TopHeightM   = 1750.0 + 1350.0;
    Deck.Slab.Coverage     = 0.42;
    Deck.Slab.DensityScale = 1.0;
    Deck.Slab.WindXMps     = 12.0;
    Deck.PowderStrength    = 0.85;
    return Deck;
}

class SolarOcclusionStage
{
  public:
    SolarOcclusionStage(const std::filesystem::path& RepositoryRoot, const std::filesystem::path& ShaderFolder,
                        std::filesystem::path OutputFolder)
        : Output(std::move(OutputFolder))
    {
        std::string Diagnostic;
        Require(Exchange.Initialize("", &Diagnostic), "Vulkan initialization failed: " + Diagnostic);
        const auto Luna      = RepositoryRoot / "EngineContent/EnvironmentArchives/CelestialMaps/MoonAlbedoSource2048.jpg";
        const auto Hipparcos = RepositoryRoot / "EngineContent/EnvironmentArchives/CelestialMaps/Hipparcos8.csv";
        Require(Sequence.Initialize(Exchange, ShaderFolder.string(), Luna.string(), Hipparcos.string(), &Diagnostic),
                "PlanetarySubmissionSequence::Initialize failed: " + Diagnostic);
        std::filesystem::create_directories(Output);
        std::printf("[SolarOcclusionValidation] device %s\n", Exchange.DeviceName().c_str());
    }

    // 📝 Eight frames, so the clouds' temporal filter settles, then the HDR frame (and a tone-mapped PNG when named).
    std::vector<float> Render(const PlanetaryRealmEnsemble& Ensemble, PlanetaryCameraView View, const char* Filename = nullptr)
    {
        for (uint32_t Frame = 0; Frame < 8u; ++Frame)
        {
            View.FrameIndex = Frame;
            Require(Sequence.ExecuteFrame(Exchange, Ensemble, View), "ExecuteFrame failed");
        }
        std::vector<float> Hdr;
        Require(Sequence.ReadbackHdrFrame(Exchange, Hdr), "HDR readback failed");
        if (Filename != nullptr)
        {
            std::vector<uint8_t> Srgb;
            TonemapHdrToSrgb8(Hdr, View.ViewportWidth, View.ViewportHeight, View.ExposureScale, Srgb);
            const auto Path   = Output / Filename;
            const int  Width  = static_cast<int>(View.ViewportWidth);
            const int  Height = static_cast<int>(View.ViewportHeight);
            Require(stbi_write_png(Path.string().c_str(), Width, Height, 4, Srgb.data(), Width * 4) != 0, "PNG write failed");
        }
        return Hdr;
    }

  private:
    std::filesystem::path       Output;
    VulkanComputeExchange       Exchange{};
    PlanetarySubmissionSequence Sequence{};
};

// 📝 Mean luminance of the pixels whose angle from the sun lies in [Inner, Outer) radians.
float MeanAround(const std::vector<float>& Hdr, const PlanetaryCameraView& View, Direction Sun, float Inner, float Outer)
{
    double Sum   = 0.0;
    size_t Count = 0;
    for (uint32_t Y = 0; Y < View.ViewportHeight; ++Y)
    {
        for (uint32_t X = 0; X < View.ViewportWidth; ++X)
        {
            const float Angle = std::acos(std::clamp(Dotted(PixelDirection(View, X, Y), Sun), -1.0f, 1.0f));
            if (Angle >= Inner && Angle < Outer)
            {
                Sum += Luminance(Hdr, static_cast<size_t>(Y) * View.ViewportWidth + X);
                ++Count;
            }
        }
    }
    return Count > 0 ? static_cast<float>(Sum / static_cast<double>(Count)) : 0.0f;
}

// 📝 Mean luminance of the pixels the cloud changed by more than 5 % against the same sky without it.
float MeanCloud(const std::vector<float>& Clouded, const std::vector<float>& Clear, size_t Pixels, size_t* OutCount = nullptr)
{
    double Sum   = 0.0;
    size_t Count = 0;
    for (size_t Pixel = 0; Pixel < Pixels; ++Pixel)
    {
        const float Sky = Luminance(Clear, Pixel);
        if (std::abs(Luminance(Clouded, Pixel) - Sky) > 0.05f * std::max(Sky, 1.0e-4f))
        {
            Sum += Luminance(Clouded, Pixel);
            ++Count;
        }
    }
    if (OutCount != nullptr) *OutCount = Count;
    return Count > 0 ? static_cast<float>(Sum / static_cast<double>(Count)) : 0.0f;
}

} // namespace

int main(int Argc, char** Argv)
{
    const std::filesystem::path RepositoryRoot = Argc > 1 ? Argv[1] : ".";
    const std::filesystem::path ShaderFolder   = Argc > 2 ? Argv[2] : "build/ShaderArchives/Environment";
    const std::filesystem::path OutputFolder   = Argc > 3 ? Argv[3] : "build/solar-occlusion-exhibits";
    MeasureOnly = Argc > 4 && std::string(Argv[4]) == "--measure-only";
    SolarOcclusionStage Stage(RepositoryRoot, ShaderFolder, OutputFolder);

    const Direction Sun       = SkyDirection(38.0f, 132.0f);
    const float     DiscAngle = 0.5f * 0.53f * DegreesToRadians;

    // ① a dense cloud across the disc: without it the disc outshines its surroundings, with it the disc must not show
    {
        const PlanetaryCameraView View    = LookAlong(Sun, 320u, 180u, 8.0f);
        PlanetaryRealmEnsemble    Clear   = MakeSky(Sun, 11u);
        PlanetaryRealmEnsemble    Clouded = MakeSky(Sun, 11u);
        Clouded.Realms[0].CloudVolumes.push_back(MakeCloudBox(Sun, 300.0f, 80.0f));
        const auto  ClearHdr  = Stage.Render(Clear, View, "DiscClear.png");
        const auto  CloudHdr  = Stage.Render(Clouded, View, "DiscBehindCloud.png");
        const float ClearDisc = MeanAround(ClearHdr, View, Sun, 0.0f, 0.7f * DiscAngle);
        const float ClearRing = MeanAround(ClearHdr, View, Sun, 1.6f * DiscAngle, 3.0f * DiscAngle);
        const float CloudDisc = MeanAround(CloudHdr, View, Sun, 0.0f, 0.7f * DiscAngle);
        const float CloudRing = MeanAround(CloudHdr, View, Sun, 1.6f * DiscAngle, 3.0f * DiscAngle);
        std::printf("① disc behind a dense cloud  clear: disc %.4f ring %.4f (x%.1f)   clouded: disc %.4f ring %.4f (x%.2f)\n",
                    ClearDisc, ClearRing, ClearDisc / std::max(ClearRing, 1.0e-6f), CloudDisc, CloudRing,
                    CloudDisc / std::max(CloudRing, 1.0e-6f));
        Expect(ClearDisc > 20.0f * ClearRing, "the clear-sky disc must outshine the sky around it");
        Expect(CloudDisc < 1.5f * CloudRing, "a dense cloud across the sun must hide the disc (disc no brighter than 1.5x the cloud)");
    }

    // ② the same cloud beside the sun and with the sun far from it: lit from behind, it must read brighter
    {
        const Direction           Beside  = SkyDirection(38.0f, 132.0f + 24.0f);
        const PlanetaryCameraView View    = LookAlong(Beside, 320u, 180u, 60.0f);
        const Direction           FarSun  = SkyDirection(38.0f, 132.0f + 24.0f + 150.0f);
        PlanetaryRealmEnsemble    NearSky = MakeSky(Sun, 12u);
        PlanetaryRealmEnsemble    FarSky  = MakeSky(FarSun, 13u);
        PlanetaryRealmEnsemble    Near    = MakeSky(Sun, 12u);
        PlanetaryRealmEnsemble    Far     = MakeSky(FarSun, 13u);
        Near.Realms[0].CloudVolumes.push_back(MakeCloudBox(Beside, 300.0f, 80.0f));
        Far.Realms[0].CloudVolumes.push_back(MakeCloudBox(Beside, 300.0f, 80.0f));
        const auto   NearClear = Stage.Render(NearSky, View);
        const auto   NearHdr   = Stage.Render(Near, View, "CloudBesideSun.png");
        const auto   FarClear  = Stage.Render(FarSky, View);
        const auto   FarHdr    = Stage.Render(Far, View, "CloudAwayFromSun.png");
        const size_t Pixels    = static_cast<size_t>(View.ViewportWidth) * View.ViewportHeight;
        size_t       NearCount = 0, FarCount = 0;
        const float  NearCloud = MeanCloud(NearHdr, NearClear, Pixels, &NearCount);
        const float  FarCloud  = MeanCloud(FarHdr, FarClear, Pixels, &FarCount);
        std::printf("② cloud lit from behind      beside the sun %.4f (%zu px)   sun behind the camera %.4f (%zu px)   (x%.2f)\n",
                    NearCloud, NearCount, FarCloud, FarCount, NearCloud / std::max(FarCloud, 1.0e-6f));
        Expect(NearCount > 500u && FarCount > 500u, "the cloud box must cover the view");
        Expect(NearCloud > 2.2f * FarCloud, "a cloud beside the sun must be lit from behind (2.2x brighter than with the sun away)");
    }

    // ③ the Everest deck looking into the sun (its wind at 80 s puts a cloud edge across the disc), for inspection
    {
        PlanetaryRealmEnsemble Deck = MakeSky(Sun, 14u);
        Deck.Realms[0].CloudVolumes.push_back(MakeEverestDeck());
        Deck.Realms[0].CloudWindSeconds        = 80.0f;
        Deck.Realms[0].Suns[0].EnableLensFlare = true;
        Deck.Realms[0].Suns[0].MasterFlareGain = 0.62f;
        const PlanetaryCameraView View = LookAlong(Sun, 480u, 270u, 60.0f);
        const auto                Hdr  = Stage.Render(Deck, View, "EverestDeckSun.png");
        std::printf("③ Everest deck into the sun  disc %.4f  ring %.4f  sky 10-20 deg %.4f\n",
                    MeanAround(Hdr, View, Sun, 0.0f, 0.7f * DiscAngle), MeanAround(Hdr, View, Sun, 1.6f * DiscAngle, 3.0f * DiscAngle),
                    MeanAround(Hdr, View, Sun, 10.0f * DegreesToRadians, 20.0f * DegreesToRadians));
    }

    std::printf("[SolarOcclusionValidation] PASSED\n");
    return 0;
}
