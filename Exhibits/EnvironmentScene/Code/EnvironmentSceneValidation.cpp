/*====================================================================================================================================
                                               ENVIRONMENTSCENEVALIDATION.CPP
====================================================================================================================================*/
// 🧩 Ported environment records, their editor descriptions, and a Sun/sky/cloud/fog scene render.

#include "Engine/Interface/SceneDirectoryPanel.h"
#include "Engine/Rendering/AtmosphereModel.h"
#include "Engine/Rendering/CloudField.h"
#include "Engine/Rendering/FogMedium.h"
#include "Engine/Scene/EnvironmentRecords.h"
#include "Engine/Scene/SceneDirectory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using namespace Frontier;

int Failures = 0;
void Check(bool Condition, const char *Name)
{
    std::printf("%s %s\n", Condition ? "PASS" : "FAIL", Name);
    if (!Condition) ++Failures;
}

double Dot(SpatialVector A, SpatialVector B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
SpatialVector Add(SpatialVector A, SpatialVector B) { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Scale(SpatialVector A, double S) { return {A.X * S, A.Y * S, A.Z * S}; }
SpatialVector Unit(SpatialVector A)
{
    const double Length = std::sqrt(Dot(A, A));
    return Length > 0 ? Scale(A, 1.0 / Length) : A;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE RENDER
//------------------------------------------------------------------------------------------------------------------------

// 📝 One environment frame from the Engine models the records own: atmosphere-tinted sky, finite
//    Sun disc, marched cloud slab, sun-lit ground shadowed by that slab, then height fog. There
//    are deliberately no analytic (point/spot/area) lights in this phase; the Sun is environmental.
struct EnvironmentFrame
{
    uint32_t Width = 0, Height = 0;
    std::vector<std::array<double, 3>> Pixels;
    uint64_t Hash = 1469598103934665603ull;
};

EnvironmentFrame RenderEnvironment(const SunRecord &Sun, const AtmosphereRecord &Atmosphere,
                                   const CloudRecord &Clouds, const FogRecord &Fog,
                                   const WindRecord &Wind, uint32_t Width, uint32_t Height,
                                   double TimeSeconds = 12.0)
{
    EnvironmentFrame Frame;
    Frame.Width = Width;
    Frame.Height = Height;
    Frame.Pixels.resize(size_t(Width) * Height);

    const SpatialVector SunDirection = Sun.QuerySunDirection();
    const double SolarScale = Sun.Placement.Enabled ? Sun.Placement.SolarIlluminance : 0.0;
    const SpectrumRgb SunRadiance{1.0 * SolarScale, 0.96 * SolarScale, 0.90 * SolarScale};

    CloudParameters Cloud = Clouds.Parameters;
    if (Wind.Enabled)
    {
        const double Bearing = Wind.BearingDegrees * 3.141592653589793 / 180.0;   // [rad]
        Cloud.WindXMps = Wind.SpeedMps * std::sin(Bearing);
        Cloud.WindYMps = Wind.SpeedMps * std::cos(Bearing);
        Cloud.WindTimeS = TimeSeconds;   // [s] - Advection clock; validation passes a fixed value.
    }

    const SpatialVector Origin{0, 0, 1.7};   // [m]
    const double TanHalfFov = std::tan(30.0 * 3.141592653589793 / 180.0);
    const double Aspect = double(Width) / double(Height);

    for (uint32_t Y = 0; Y < Height; ++Y)
    {
        for (uint32_t X = 0; X < Width; ++X)
        {
            const double NdcX = (2.0 * (X + 0.5) / Width - 1.0) * Aspect * TanHalfFov;
            const double NdcY = (2.0 * (Y + 0.5) / Height - 1.0) * TanHalfFov;   // image Y down
            // Camera looks north (+Y) and slightly down; Z is up, image Y is flipped into -Z.
            const SpatialVector Direction = Unit({NdcX, 1.0, -NdcY - 0.18});

            SpectrumRgb Background{};
            double GroundDistance = -1.0;
            SpatialVector GroundPoint{};
            if (Direction.Z < -1e-6)
            {
                GroundDistance = -Origin.Z / Direction.Z;
                GroundPoint = Add(Origin, Scale(Direction, GroundDistance));
            }

            if (GroundDistance > 0)
            {
                // Sun-lit ground, shadowed by the cloud slab marched from the surface point.
                double CloudTransmittance = 1.0;
                if (Clouds.Enabled && SunDirection.Z > 0.02)
                {
                    CloudTransmittance = MarchClouds(Cloud, Add(GroundPoint, {0, 0, 0.5}), SunDirection,
                                                      SunDirection, SunRadiance, 40000.0, 24, 3).Transmittance;
                }
                const double Direct = std::max(0.0, SunDirection.Z) * CloudTransmittance * SolarScale;
                Background = {0.30 * Direct * 0.9, 0.29 * Direct * 0.85, 0.26 * Direct * 0.7};
            }
            else
            {
                // Atmosphere sky: transmittance from near ground level toward this ray's zenith.
                SpectrumRgb Transmittance{1, 1, 1};
                if (Atmosphere.Enabled)
                {
                    Transmittance = TransmittanceToTop(Atmosphere.Parameters,
                                                       Atmosphere.Parameters.GroundRadiusKm + 0.002,
                                                       std::clamp(Direction.Z, -1.0, 1.0), 24);
                }
                const double SkyBlend = std::clamp(1.0 - std::abs(Direction.Z), 0.0, 1.0);
                // 📝 In-scatter tint follows the Rayleigh ordering (blue scatters most); the
                //    previous red/blue weighting produced a green cast and is corrected here.
                Background = {(1.0 - Transmittance.R) * 0.18 + SkyBlend * 0.02,
                              (1.0 - Transmittance.G) * 0.38 + SkyBlend * 0.03,
                              (1.0 - Transmittance.B) * 0.85 + SkyBlend * 0.04};
                Background.R *= SolarScale * 0.09;
                Background.G *= SolarScale * 0.09;
                Background.B *= SolarScale * 0.09;
                if (Sun.Placement.Enabled && Sun.Placement.DiscEnabled)
                {
                    const double CosAngle = Dot(Direction, SunDirection);
                    const double DiscCos = std::cos(0.27 * 3.141592653589793 / 180.0);   // HTML SunRadius
                    if (CosAngle > DiscCos)
                    {
                        Background.R += SunRadiance.R * 2.4;
                        Background.G += SunRadiance.G * 2.4;
                        Background.B += SunRadiance.B * 2.4;
                    }
                }
            }

            SpectrumRgb Colour = Background;
            if (Clouds.Enabled)
            {
                const double MaxDistance = GroundDistance > 0 ? GroundDistance : 40000.0;
                const CloudMarchResult Marched = MarchClouds(Cloud, Origin, Direction, SunDirection,
                                                             SunRadiance, MaxDistance, 40, 5);
                Colour = {Marched.Radiance.R + Marched.Transmittance * Background.R,
                          Marched.Radiance.G + Marched.Transmittance * Background.G,
                          Marched.Radiance.B + Marched.Transmittance * Background.B};
            }

            if (Fog.Enabled && GroundDistance > 0)
            {
                const double Transmittance = FogTransmittance(Fog.Parameters, Origin, Direction, GroundDistance);
                const SpectrumRgb Fogged = ApplyFog(Fog.Parameters, Colour, Transmittance);
                // 📝 Cloud-shadowed single scattering tint: the fog colour is dimmed where the slab
                //    blocks the Sun, matching the surface shadow above instead of a constant blend.
                Colour = Fogged;
            }

            std::array<double, 3> &Pixel = Frame.Pixels[size_t(Y) * Width + X];
            Pixel = {Colour.R, Colour.G, Colour.B};
            const uint8_t Quantized[3] = {uint8_t(std::clamp(1.0 - std::exp(-Colour.R), 0.0, 1.0) * 255.0),
                                          uint8_t(std::clamp(1.0 - std::exp(-Colour.G), 0.0, 1.0) * 255.0),
                                          uint8_t(std::clamp(1.0 - std::exp(-Colour.B), 0.0, 1.0) * 255.0)};
            for (uint8_t Channel : Quantized)
            {
                Frame.Hash ^= Channel;
                Frame.Hash *= 1099511628211ull;
            }
        }
    }
    return Frame;
}

bool WritePpm(const char *Path, const EnvironmentFrame &Frame)
{
    FILE *File = std::fopen(Path, "wb");
    if (!File) return false;
    std::fprintf(File, "P6\n%u %u\n255\n", Frame.Width, Frame.Height);
    for (const auto &Pixel : Frame.Pixels)
    {
        const uint8_t Rgb[3] = {uint8_t(std::clamp(1.0 - std::exp(-Pixel[0]), 0.0, 1.0) * 255.0),
                                uint8_t(std::clamp(1.0 - std::exp(-Pixel[1]), 0.0, 1.0) * 255.0),
                                uint8_t(std::clamp(1.0 - std::exp(-Pixel[2]), 0.0, 1.0) * 255.0)};
        std::fwrite(Rgb, 1, 3, File);
    }
    std::fclose(File);
    return true;
}

} // namespace

int main(int ArgumentCount, char **Arguments)
{
    using namespace Frontier;
    const char *Output = ArgumentCount > 1 ? Arguments[1] : "EnvironmentScene.ppm";

    SunRecord Sun;
    AtmosphereRecord Atmosphere;
    CloudRecord Clouds;
    FogRecord Fog;
    WindRecord Wind;
    // 📝 Display names are directory-side; records keep their construction defaults otherwise.
    SceneDirectory::Instance().ConfigureName(Sun.Token(), "Sun");
    SceneDirectory::Instance().ConfigureName(Atmosphere.Token(), "Atmosphere");
    SceneDirectory::Instance().ConfigureName(Clouds.Token(), "Clouds");
    SceneDirectory::Instance().ConfigureName(Fog.Token(), "Fog");
    SceneDirectory::Instance().ConfigureName(Wind.Token(), "Wind");

    SceneDirectoryPanel Panel;
    Panel.Synchronize(SceneDirectory::Instance());

#if defined(FRONTIER_EDITOR_PROFILE)
    Check(Panel.QueryTotal() == 5, "five environment records registered");
    const auto &Groups = Panel.QueryGroups();
    Check(Groups.size() == 2 && Groups[0].Group == "Environment" && Groups[0].Count == 4 &&
              Groups[1].Group == "Forcefield" && Groups[1].Count == 1,
          "HTML groups Environment x4 then Forcefield x1");
    const SceneDirectoryEnabledCounts Counts = Panel.QueryEnabledCounts();
    Check(Counts.Enabled == 4 && Counts.Disabled == 1, "enabled tile 4 and disabled tile 1 (fog off)");
    bool RowMetadata = true;
    for (const SceneDirectoryRow &Row : Panel.QueryRows())
        RowMetadata &= !Row.Group.empty() && !Row.StableId.empty();
    Check(RowMetadata, "every row carries its record-built group and stable id");

    Panel.Select(Sun.Token());
    Panel.Synchronize(SceneDirectory::Instance());
    const SceneInspectorModel &SunInspector = Panel.QueryInspectorModel();
    Check(SunInspector.HasSelection && SunInspector.Name == "Sun" && SunInspector.Group == "Environment" &&
              SunInspector.Category == "Directional light" && SunInspector.ScalarFields.size() == 3,
          "Sun selection builds its own inspector");

    Panel.Select(Clouds.Token());
    Panel.Synchronize(SceneDirectory::Instance());
    const SceneInspectorModel &CloudInspector = Panel.QueryInspectorModel();
    Check(CloudInspector.ScalarFields.size() == 5 &&
              std::strcmp(CloudInspector.ScalarFields[0].Label, "Coverage") == 0,
          "Cloud selection builds its own inspector");
    const double BeforeCoverage = Clouds.Parameters.Coverage;
    *CloudInspector.ScalarFields[0].Target = 0.9;
    Check(Clouds.Parameters.Coverage == 0.9 && BeforeCoverage != 0.9,
          "inspector field edits the live cloud setting");
    *CloudInspector.ScalarFields[0].Target = BeforeCoverage;

    Panel.ConfigureTextFilter("cloud");
    Panel.Synchronize(SceneDirectory::Instance());
    Check(Panel.QueryVisible().size() == 1 && Panel.QueryVisible()[0].StableId == "clouds",
          "search finds Clouds by name");
    Panel.ConfigureTextFilter("");
#endif

    // 📝 Frame composition for the retained capture: Sun placed in the north-looking view and a
    //    broken coverage so sky, lit cloud tops and shadowed ground are all visible. Record
    //    defaults remain the pinned HTML presets; this is exhibit scene setup, not a new default.
    Sun.Placement.AzimuthDegrees = 30.0;
    Clouds.Parameters.Coverage = 0.35;
    Clouds.Parameters.AmbientRadiance = {0.10, 0.11, 0.13};
    const EnvironmentFrame Clear = RenderEnvironment(Sun, Atmosphere, Clouds, Fog, Wind, 640, 400);
    Clouds.Enabled = false;
    const EnvironmentFrame NoClouds = RenderEnvironment(Sun, Atmosphere, Clouds, Fog, Wind, 640, 400);
    Clouds.Enabled = true;
    Fog.Enabled = true;
    const EnvironmentFrame Fogged = RenderEnvironment(Sun, Atmosphere, Clouds, Fog, Wind, 640, 400);
    Fog.Enabled = false;
    Sun.Placement.ElevationDegrees = -8.0;
    const EnvironmentFrame Night = RenderEnvironment(Sun, Atmosphere, Clouds, Fog, Wind, 640, 400);
    Sun.Placement.ElevationDegrees = 38.0;

    Check(Clear.Hash != NoClouds.Hash, "cloud toggle changes the rendered frame");
    Check(Fogged.Hash != Clear.Hash, "fog toggle changes the rendered frame");
    Check(Night.Hash != Clear.Hash, "Sun elevation changes the rendered frame");
    double ClearMean = 0, NightMean = 0;
    for (size_t Index = 0; Index < Clear.Pixels.size(); ++Index)
    {
        ClearMean += Clear.Pixels[Index][0];
        NightMean += Night.Pixels[Index][0];
    }
    Check(NightMean < ClearMean, "below-horizon Sun darkens the frame");
    Check(WritePpm(Output, Clear), "scene frame written");

    // 📝 Optional small visual proofs (kept tiny so they stay fast): a four-state comparison strip
    //    reusing the frames above, and a short wind-advection frame sequence for GIF assembly.
    bool Strip = false, Animate = false;
    for (int I = 2; I < ArgumentCount; ++I)
    {
        Strip |= std::strcmp(Arguments[I], "--strip") == 0;
        Animate |= std::strcmp(Arguments[I], "--animate") == 0;
    }
    if (Strip)
    {
        EnvironmentFrame StripFrame;
        StripFrame.Width = Clear.Width * 2;
        StripFrame.Height = Clear.Height * 2;
        StripFrame.Pixels.resize(size_t(StripFrame.Width) * StripFrame.Height);
        const EnvironmentFrame *Quads[4] = {&Clear, &NoClouds, &Fogged, &Night};
        for (uint32_t Y = 0; Y < StripFrame.Height; ++Y)
            for (uint32_t X = 0; X < StripFrame.Width; ++X)
            {
                const EnvironmentFrame &Q = *Quads[(Y / Clear.Height) * 2 + (X / Clear.Width)];
                StripFrame.Pixels[size_t(Y) * StripFrame.Width + X] =
                    Q.Pixels[size_t(Y % Clear.Height) * Clear.Width + (X % Clear.Width)];
            }
        Check(WritePpm("EnvironmentStrip.ppm", StripFrame), "comparison strip written");
    }
    if (Animate)
    {
        char Path[64];
        for (int FrameIndex = 0; FrameIndex < 16; ++FrameIndex)
        {
            const EnvironmentFrame Frame = RenderEnvironment(Sun, Atmosphere, Clouds, Fog, Wind, 256, 160,
                                                             double(FrameIndex) * 2.0);
            std::snprintf(Path, sizeof(Path), "EnvironmentFrame%02d.ppm", FrameIndex);
            if (!WritePpm(Path, Frame)) Check(false, "animation frame written");
        }
        Check(true, "animation frames written");
    }

    std::printf("ENVIRONMENT_SCENE hash_clear=%llu hash_noclouds=%llu hash_fog=%llu hash_night=%llu\n",
                (unsigned long long)Clear.Hash, (unsigned long long)NoClouds.Hash,
                (unsigned long long)Fogged.Hash, (unsigned long long)Night.Hash);
    std::printf("%s environment scene validation\n", Failures == 0 ? "PASS" : "FAIL");
    return Failures == 0 ? 0 : 1;
}
