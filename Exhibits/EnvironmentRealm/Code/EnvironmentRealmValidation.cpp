//==========================================================================================================================================
//                                             ENVIRONMENT REALM VALIDATION & EXHIBITS
//==========================================================================================================================================
// End-to-end CPU + Vulkan 1.3 Slang GPU verification of PlanetaryAtmosphere, GeometricRaster, and DeviceExchange
// across multi-instance / cross-realm Day-Night Portal worlds, non-tiled 3D clouds + cloud shadows, and all 6 real 2K moons.

#include "Engine/DeviceExchange/VulkanComputeExchange.h"
#include "Engine/GeometricRaster/VisibilityProjection.h"
#include "Engine/PhysicalDynamics/ForceField.h"
#include "Engine/PlanetaryAtmosphere/AstronomicalEphemeris.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereProjection.h"
#include "Engine/PlanetaryAtmosphere/AtmosphereScattering.h"
#include "Engine/PlanetaryAtmosphere/CloudSpace.h"
#include "Engine/PlanetaryAtmosphere/FogSpace.h"
#include "Engine/PlanetaryAtmosphere/OpticalLensFlare.h"
#include "Engine/PlanetaryAtmosphere/PlanetaryEnsemble.h"
#include "Engine/PlanetaryAtmosphere/PlanetaryProjection.h"
#include "Engine/PlanetaryAtmosphere/VolumetricClouds.h"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;

void RequireCheck(bool Condition, const char* Expression, const char* Detail, int Line)
{
    if (!Condition)
    {
        std::cerr << "[EnvironmentRealmValidation] FAILED at line " << Line << ": " << Expression << " (" << Detail << ")\n";
        std::exit(1);
    }
}

#define REQUIRE_ENV(Cond, Msg) RequireCheck((Cond), #Cond, (Msg), __LINE__)

std::array<float, 3> Norm3(float X, float Y, float Z)
{
    const float Len = std::sqrt(X * X + Y * Y + Z * Z);
    return (Len > 1.0e-12f) ? std::array<float, 3>{X / Len, Y / Len, Z / Len} : std::array<float, 3>{0.0f, 1.0f, 0.0f};
}

Vec3 NormVec3(float X, float Y, float Z)
{
    const auto A = Norm3(X, Y, Z);
    return {A[0], A[1], A[2]};
}

SpatialVector NormSpatial(double X, double Y, double Z)
{
    const double Len = std::sqrt(X * X + Y * Y + Z * Z);
    return (Len > 1.0e-12) ? SpatialVector{X / Len, Y / Len, Z / Len} : SpatialVector{0.0, 0.0, 1.0};
}

std::array<float, 3> Cross3(std::array<float, 3> A, std::array<float, 3> B)
{
    return {
        A[1] * B[2] - A[2] * B[1],
        A[2] * B[0] - A[0] * B[2],
        A[0] * B[1] - A[1] * B[0],
    };
}

PlanetaryCameraView MakeLookAtCamera(uint32_t             RealmIndex,
                                     std::array<float, 3> Eye,
                                     std::array<float, 3> Target,
                                     uint32_t             Width,
                                     uint32_t             Height,
                                     float                FovDeg   = 60.0f,
                                     float                Exposure = 1.0f)
{
    PlanetaryCameraView Cam{};
    Cam.ActiveRealmIndex = RealmIndex;
    Cam.PositionMeters   = Eye;
    Cam.ForwardUnit      = Norm3(Target[0] - Eye[0], Target[1] - Eye[1], Target[2] - Eye[2]);
    const auto WorldUp   = std::array<float, 3>{0.0f, 1.0f, 0.0f};
    const auto RightRaw  = Cross3(WorldUp, Cam.ForwardUnit);
    Cam.RightUnit        = Norm3(RightRaw[0], RightRaw[1], RightRaw[2]);
    const auto UpRaw     = Cross3(Cam.ForwardUnit, Cam.RightUnit);
    Cam.UpUnit           = Norm3(UpRaw[0], UpRaw[1], UpRaw[2]);
    Cam.VerticalFovRad   = FovDeg * 3.14159265358979323846f / 180.0f;
    Cam.ViewportWidth    = Width;
    Cam.ViewportHeight   = Height;
    Cam.ExposureScale    = Exposure;
    return Cam;
}

bool WritePngRgba(const std::filesystem::path& FilePath, uint32_t Width, uint32_t Height, const std::vector<uint8_t>& Rgba)
{
    std::error_code Ec;
    std::filesystem::create_directories(FilePath.parent_path(), Ec);
    return stbi_write_png(FilePath.string().c_str(),
                          static_cast<int>(Width),
                          static_cast<int>(Height),
                          4,
                          Rgba.data(),
                          static_cast<int>(Width * 4u)) != 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                      1. CPU ORACLE VERIFICATION OF ALL 5 COUPLED PLANETARY UNITS
//------------------------------------------------------------------------------------------------------------------------

void VerifyPhysicalOracles(const std::filesystem::path& RepoRoot)
{
    std::cout << "[1/4] Verifying 6-pt Gauss-Legendre Solar/Sky NEE (< 2.85% vs FP64 Oracle)...\n";
    {
        SolarSkySpecification Spec{};
        Spec.Atmosphere         = MakeEarthAtmosphere();
        Spec.LocalUpDirection   = {0.0, 0.0, 1.0};
        Spec.ObserverAltitudeKm = 0.05;

        const std::array<double, 4> SunElevationsDeg  = {54.0, 16.0, 3.2, -2.5};
        const std::array<double, 4> ViewElevationsDeg = {68.0, 30.0, 9.5, 24.0};
        double                      MaxRelErr         = 0.0;

        for (double θSunDeg : SunElevationsDeg)
        {
            const double          θSunRad = θSunDeg * 3.14159265358979323846 / 180.0;
            SolarSkySourceProfile Source{};
            Source.SolarDirection  = NormSpatial(0.18 * std::cos(θSunRad), 0.9836 * std::cos(θSunRad), std::sin(θSunRad));
            Source.SolarIrradiance = {18.5, 18.0, 17.2};
            Spec.SolarSources      = {Source};

            for (double θViewDeg : ViewElevationsDeg)
            {
                const double        θViewRad = θViewDeg * 3.14159265358979323846 / 180.0;
                const SpatialVector ViewDir  = NormSpatial(0.32 * std::cos(θViewRad), 0.9474 * std::cos(θViewRad), std::sin(θViewRad));
                const SpectrumRgb   Fast6Pt  = EvaluateSolarSkyGaussLegendre6Pt(Spec, {0.0, 0.0, 50.0}, ViewDir);
                const SpectrumRgb   Oracle   = EvaluateSolarSkyOracleContinuum(Spec, {0.0, 0.0, 50.0}, ViewDir, 256u);

                const double FastLum   = 0.2126 * Fast6Pt.R + 0.7152 * Fast6Pt.G + 0.0722 * Fast6Pt.B;
                const double OracleLum = 0.2126 * Oracle.R + 0.7152 * Oracle.G + 0.0722 * Oracle.B;
                if (OracleLum > 1.0e-4)
                {
                    const double RelErr = std::abs(FastLum - OracleLum) / OracleLum;
                    MaxRelErr           = std::max(MaxRelErr, RelErr);
                    REQUIRE_ENV(RelErr < 0.0285, "6-pt Gauss-Legendre sky quadrature must stay within 2.85% of FP64 oracle");
                }
            }
        }
        std::cout << "      Max 6-pt GL sky relative error across day/golden-hour/twilight = " << (MaxRelErr * 100.0) << "% (< 2.85%)\n";
    }

    std::cout << "[2/4] Verifying Meeus Ephemeris, All 6 Real 2K Moon Maps, Scintillating Stars & Ringed Planets...\n";
    {
        const double Jd       = JulianDate(2026, 10, 31, 20.0);
        const auto   SunPos   = SunPosition(Jd, -26.188, 28.320);
        const auto   SunVec   = SunDirectionLocal(SunPos);
        const auto   MoonGeo  = MoonGeocentric(Jd);
        const auto   MoonTopo = MoonTopocentric(MoonGeo, Jd, -26.188, 28.320, 1650.0);
        const double LenSq    = SunVec.X * SunVec.X + SunVec.Y * SunVec.Y + SunVec.Z * SunVec.Z;
        REQUIRE_ENV(std::abs(LenSq - 1.0) < 1.0e-6, "Solar ephemeris vector must be unit length");
        REQUIRE_ENV(MoonGeo.DistanceKm > 350000.0 && MoonGeo.DistanceKm < 410000.0 && MoonTopo.AngularRadiusRad > 0.004,
                    "Meeus Ch.47 lunar perigee/apogee distance and topocentric radius must be physical");

        const SunDiscProfile       Disc{};
        const SolarDiscPixelFilter Filter(Disc);
        const SpectrumRgb          CentIn = Filter.Evaluate(0.0, 0.0, 0.0006, 0.0006, {100.0, 100.0, 100.0});
        const SpectrumRgb          OutIn  = Filter.Evaluate(0.04, 0.0, 0.0006, 0.0006, {100.0, 100.0, 100.0});
        REQUIRE_ENV(CentIn.R > 50.0 && OutIn.R < 1.0e-4, "SolarDiscPixelFilter must preserve core energy and vanish outside limb");

        // PlanetaryDomeProjection: verify all 6 real 2048x1024 moon JPEGs + Hipparcos8.csv + Ringed Planets
        PlanetaryDomeProjection Dome{};
        const auto              MapDir  = RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps";
        const auto              JpgPath = MapDir / "MoonAlbedoSource2048.jpg";
        const auto              CsvPath = MapDir / "Hipparcos8.csv";
        REQUIRE_ENV(Dome.LoadLunarAtlas(JpgPath.string(), 2048u, 1024u), "All 6 real 2K moon maps must load into LunarSixBodyAtlas");
        REQUIRE_ENV(Dome.LunarAtlas().Width() == 2048u && Dome.LunarAtlas().Height() == 1024u, "Lunar atlas must be 2048x1024");
        REQUIRE_ENV(Dome.LunarAtlas().Rgba8SrgbBytes().size() == size_t(2048u) * 1024u * 6u * 4u,
                    "6-layer RGBA8 sRGB lunar byte size must equal 2048*1024*6*4");

        // Verify all 6 layers contain distinct real surface data (mean RGB differs across Luna, Ember, Glacier, Sulfur, Shroud, Shard)
        std::array<double, 6> LayerMeans{};
        for (uint32_t Layer = 0u; Layer < 6u; ++Layer)
        {
            const auto Preset = static_cast<LunarBodyPreset>(Layer);
            double     Sum    = 0.0;
            for (int Sample = 0; Sample < 64; ++Sample)
            {
                const float λLon = ((static_cast<float>(Sample % 8) + 0.5f) / 8.0f - 0.5f) * 6.2831853f;
                const float φLat = (0.5f - (static_cast<float>(Sample / 8) + 0.5f) / 8.0f) * 3.1415926f;
                const Vec3  C    = Dome.LunarAtlas().SampleLinearAlbedo(Preset, λLon, φLat);
                Sum += static_cast<double>(C.X) * 1.7 + static_cast<double>(C.Y) * 1.1 + static_cast<double>(C.Z) * 0.6;
            }
            LayerMeans[Layer] = Sum / 64.0;
            REQUIRE_ENV(LayerMeans[Layer] > 0.02 && LayerMeans[Layer] < 3.5, "Each real 2K moon map must carry valid linear albedo");
        }
        for (uint32_t A = 0u; A < 6u; ++A)
        {
            for (uint32_t B = A + 1u; B < 6u; ++B)
            {
                REQUIRE_ENV(std::abs(LayerMeans[A] - LayerMeans[B]) > 1.0e-4, "Each of the 6 real moon maps must be distinct");
            }
        }

        REQUIRE_ENV(Dome.LoadHipparcosCatalogueCsv(CsvPath.string()), "Hipparcos8.csv must decode into 32x32 octahedral index");
        uint32_t NonEmptyCells = 0u;
        for (const auto& Cell : Dome.StarCells())
        {
            if (Cell.Count > 0u)
            {
                ++NonEmptyCells;
            }
        }
        REQUIRE_ENV(Dome.SourceStarCount() > 30000u && NonEmptyCells > 400u,
                    "Octahedral star index must populate > 30k Hipparcos stars across sphere");

        LunarBodyDescriptor Moon{};
        Moon.Preset           = LunarBodyPreset::Luna;
        Moon.DirectionUnit    = NormVec3(0.0f, 0.8f, 0.6f);
        Moon.AngularRadiusRad = 0.03f;

        float      FullCov = 0.0f, QuarterCov = 0.0f, NewCov = 0.0f;
        const Vec3 ProbeView  = NormVec3(0.015f, 0.8f, 0.6f);
        const Vec3 FullRad    = Dome.EvaluateLunarBody(Moon, ProbeView, NormVec3(0.0f, -0.8f, -0.6f), {18.0f, 18.0f, 18.0f}, 0.001f, FullCov);
        const Vec3 QuarterRad = Dome.EvaluateLunarBody(Moon, ProbeView, NormVec3(1.0f, 0.0f, 0.0f), {18.0f, 18.0f, 18.0f}, 0.001f, QuarterCov);
        const Vec3 NewRad     = Dome.EvaluateLunarBody(Moon, ProbeView, NormVec3(0.0f, 0.8f, 0.6f), {18.0f, 18.0f, 18.0f}, 0.001f, NewCov);
        REQUIRE_ENV(FullCov > 0.99f && FullRad.Y > QuarterRad.Y && QuarterRad.Y > NewRad.Y && NewRad.Z > 0.0f,
                    "Lunar Lommel-Seeliger phase curve and earthshine must satisfy Full > Quarter > New > 0");

        // Verify Hazy Lunar Atmospheres (limb optical depth + off-limb atmospheric corona scattering)
        LunarBodyDescriptor ShroudMoon{};
        ShroudMoon.Preset           = LunarBodyPreset::Shroud;
        ShroudMoon.DirectionUnit    = NormVec3(0.0f, 1.0f, 0.0f);
        ShroudMoon.AngularRadiusRad = 0.05f;

        LunarBodyDescriptor AirlessLuna = ShroudMoon;
        AirlessLuna.Preset              = LunarBodyPreset::Luna;

        REQUIRE_ENV(ResolveLunarAtmosphereHaze(ShroudMoon) > 0.60f &&
                        ResolveLunarAtmosphereHaze(AirlessLuna) < 0.08f,
                    "Shroud must resolve dense atmospheric haze (>0.60) while Luna remains near-vacuum (<0.08)");

        float      ShroudOffLimbCov = 0.0f, LunaOffLimbCov = 0.0f;
        const Vec3 OffLimbDir = NormVec3(0.0535f, 1.0f, 0.0f); // 1.07x AngularRadius (outside solid disc, inside atmospheric shell)
        const Vec3 ShroudHalo = Dome.EvaluateLunarBody(ShroudMoon, OffLimbDir, NormVec3(0.2f, -0.8f, -0.5f), {18.0f, 18.0f, 18.0f}, 0.001f, ShroudOffLimbCov);
        const Vec3 LunaHalo   = Dome.EvaluateLunarBody(AirlessLuna, OffLimbDir, NormVec3(0.2f, -0.8f, -0.5f), {18.0f, 18.0f, 18.0f}, 0.001f, LunaOffLimbCov);
        REQUIRE_ENV(ShroudOffLimbCov == 0.0f && LunaOffLimbCov == 0.0f &&
                        (ShroudHalo.X + ShroudHalo.Y + ShroudHalo.Z) > (LunaHalo.X + LunaHalo.Y + LunaHalo.Z) * 8.0f,
                    "Hazy moon (Shroud) must emit glowing off-limb atmospheric corona without occluding background stars");

        // Verify Unified 3D ForceField: Beaufort classification, Ekman shear, Tornado Vortex trigger, Gust Front, & Particle Solver
        REQUIRE_ENV(ClassifyBeaufortWind(0.1).ForceNumber == 0u &&
                        ClassifyBeaufortWind(12.0).ForceNumber == 6u &&
                        ClassifyBeaufortWind(35.0).ForceNumber == 12u,
                    "Beaufort wind scale must classify calm (0), strong breeze (6), and hurricane/tornado-inflow (12)");

        ForceFieldSpace      ForceTest{};
        PlanetaryWindProfile WindProf{};
        WindProf.SpeedMetersPerSecond = 9.5;
        WindProf.BearingRadians       = 0.35;
        WindProf.GustAmplitude        = 0.0; // Fast-path zero-gust check
        WindProf.ShearPerKilometer    = 0.45;
        ForceTest.SetWindProfile(WindProf);

        const SpatialVector ZeroGustLow  = ForceTest.SampleStep({10.0, 10.0, 0.0});
        const SpatialVector ZeroGustHigh = ForceTest.SampleStep({10.0, 800.0, 0.0});
        REQUIRE_ENV(std::hypot(ZeroGustHigh.X, ZeroGustHigh.Z) > std::hypot(ZeroGustLow.X, ZeroGustLow.Z),
                    "Ekman vertical wind profile must shear/accelerate with altitude");

        TornadoColumnParameters TornadoSpec{};
        TornadoSpec.GroundAnchorMeters      = {0.0, 0.0, 0.0};
        TornadoSpec.TranslationVelocityMps  = {0.0, 0.0, 0.0};
        TornadoSpec.ColumnHeightMeters      = 1100.0;
        TornadoSpec.GroundCoreRadiusMeters  = 24.0;
        TornadoSpec.CollarCoreRadiusMeters  = 88.0;
        TornadoSpec.TangentialPeakSpeedMps  = 72.0;
        TornadoSpec.RadialInflowSpeedMps    = 26.0;
        TornadoSpec.AxialUpdraftSpeedMps    = 40.0;
        TornadoSpec.SpineLeanXMeters        = 28.0;
        TornadoSpec.SpineLeanZMeters        = 0.0;
        TornadoSpec.CondensationStrength    = 0.94;
        TornadoSpec.DebrisSkirtHeightMeters = 96.0;
        TornadoSpec.DebrisExtinctionPerM    = 0.095;

        const ForceSourceHandle VortexHandle = ForceTest.TriggerTornado(TornadoSpec, {0.5, 45.0, 2.0});
        const ForceSourceHandle GustHandle   = ForceTest.TriggerGustFront(GustFrontParameters{}, {0.5, 30.0, 1.5});
        const ForceSourceHandle BurstHandle  = ForceTest.TriggerMicroburst(MicroburstParameters{}, {0.5, 18.0, 1.5});
        ForceTest.Step(2.0); // Advance past RampInSeconds so ActivationEnvelope == 1.0
        REQUIRE_ENV(VortexHandle != 0u && GustHandle != 0u && BurstHandle != 0u && ForceTest.ActiveSources().size() == 3u,
                    "ForceFieldSpace must register tornado vortex, traveling gust front, and radial microburst triggers");

        const ForceFieldSample CoreWall  = ForceTest.EvaluateStepSample({34.0, 140.0, 0.0}, false);
        const ForceFieldSample CoreAxis  = ForceTest.EvaluateStepSample({6.0, 180.0, 0.0}, false);
        const ForceFieldSample CoreBase  = ForceTest.EvaluateStepSample({26.0, 18.0, 0.0}, false);
        const SpatialVector    SwirlEddy = ForceTest.SampleSwirl({18.0, 90.0, 12.0});
        REQUIRE_ENV(std::hypot(CoreWall.AirVelocityMps.X, CoreWall.AirVelocityMps.Z) > 36.0 &&
                        CoreAxis.AirVelocityMps.Y > 18.0 &&
                        CoreAxis.PressureDeficitPascals > 1800.0 &&
                        CoreAxis.FunnelCondensationDensity > 0.35 &&
                        CoreBase.ParticulateExtinctionPerM > 0.02 &&
                        (std::abs(SwirlEddy.X) + std::abs(SwirlEddy.Y) + std::abs(SwirlEddy.Z)) > 0.05,
                    "Triggered Burgers-Rott tornado must produce strong tangential swirl, central updraft, cyclostrophic Δp funnel, curl swirl, and ground debris skirt");

        std::array<AerodynamicParticle, 6> Particles{{
            {{280.0, 45.0, 0.0}, {0.0, 0.0, 0.0}, AerodynamicParticleSpecies::RainDrop,   0.0024f, 0.0f, 0x11u, false},
            {{280.0, 45.0, 4.0}, {0.0, 0.0, 0.0}, AerodynamicParticleSpecies::SnowFlake,  0.0040f, 0.0f, 0x22u, false},
            {{280.0, 45.0, 8.0}, {0.0, 0.0, 0.0}, AerodynamicParticleSpecies::HailStone,  0.0120f, 0.0f, 0x33u, false},
            {{16.0,  1.2,  0.0}, {0.0, 0.0, 0.0}, AerodynamicParticleSpecies::FineDust,   0.00004f, 0.0f, 0x44u, false},
            {{22.0,  0.0,  0.0}, {0.0, 0.0, 0.0}, AerodynamicParticleSpecies::QuartzSand, 0.00028f, 0.0f, 0x55u, true},
            {{20.0,  2.0,  0.0}, {0.0, 0.0, 0.0}, AerodynamicParticleSpecies::LoftedDebris, 0.035f, 0.0f, 0x66u, false},
        }};
        for (int Step = 0; Step < 30; ++Step)
        {
            ForceTest.StepParticles(Particles, 0.04);
        }
        REQUIRE_ENV(Particles[3].PositionMeters.Y > 6.0 &&
                        Particles[4].PositionMeters.Y > 0.05 &&
                        std::hypot(Particles[5].VelocityMps.X, Particles[5].VelocityMps.Z) > 10.0 &&
                        Particles[2].PositionMeters.Y < Particles[1].PositionMeters.Y,
                    "Aerodynamic + Bagnold saltation solver must loft dust/sand/debris in vortex inflow and separate hail vs snowflake terminal fall");

        PlanetaryBodyDescriptor Giant{};
        Giant.Archetype              = PlanetarySurfaceArchetype::BandedGasGiant;
        Giant.DirectionUnit          = NormVec3(0.0f, 1.0f, 0.0f);
        Giant.AngularRadiusRad       = 0.06f;
        Giant.SpinAxisUnit           = NormVec3(0.0f, -0.35f, 0.9367f);
        Giant.Rings.Enabled          = true;
        Giant.Rings.InnerRadiusRatio = 1.35f;
        Giant.Rings.OuterRadiusRatio = 2.25f;
        Giant.Rings.PeakOpticalDepth = 1.8f;

        float      LitRingCov = 0.0f, ShadowRingCov = 0.0f;
        const Vec3 SideSunDir = NormVec3(1.0f, -0.25f, 0.12f);
        const Vec3 LitFlank   = Dome.EvaluatePlanetaryBody(Giant, NormVec3(0.095f, 1.0f, 0.01f), SideSunDir, {20.0f, 20.0f, 20.0f}, 0.001f, LitRingCov);
        const Vec3 ShadFlank  = Dome.EvaluatePlanetaryBody(Giant, NormVec3(-0.095f, 1.0f, -0.01f), SideSunDir, {20.0f, 20.0f, 20.0f}, 0.001f, ShadowRingCov);
        REQUIRE_ENV(LitRingCov > 0.2f && ShadowRingCov > 0.2f && LitFlank.X > ShadFlank.X * 2.5f,
                    "Planetary body must cast geometric shadow across anti-solar ring sector");
    }

    std::cout << "[3/4] Verifying Non-Tiled 3D CloudSpace, Wide Ortho CloudShadowProjection & Unified FogSpace...\n";
    {
        CloudVolumeProfile LocalBox{};
        LocalBox.Domain              = CloudVolumeDomain::BoundedLocalBox;
        LocalBox.BoxCenterM          = {0.0, 120.0, 80.0};
        LocalBox.BoxHalfExtentsM     = {45.0, 45.0, 25.0};
        LocalBox.BoxEdgeFalloffM     = 6.0;
        LocalBox.Slab.Coverage       = 0.88;
        LocalBox.Slab.DensityScale   = 2.2;
        LocalBox.Slab.ExtinctionPerM = 0.06;

        const double InsideDens  = EvaluateCloudVolumeDensity(LocalBox, {2.0, 120.0, 80.0});
        const double OutsideDens = EvaluateCloudVolumeDensity(LocalBox, {95.0, 120.0, 80.0});
        REQUIRE_ENV(InsideDens > 0.0 && OutsideDens == 0.0, "BoundedLocalBox cloud volume must have zero leakage outside bounds");

        CloudShadowProjection                   ShadowMap{};
        const std::array<CloudVolumeProfile, 1> Vols{{LocalBox}};
        REQUIRE_ENV(ShadowMap.Bake(Vols, {0.0, 120.0, 0.0}, {0.0, 0.0, 1.0}, 140.0, 64u, 20u), "CloudShadowProjection::Bake failed");
        const double CenterTrans = ShadowMap.SampleTransmittance({0.0, 120.0, 0.0});
        const double ClearTrans  = ShadowMap.SampleTransmittance({120.0, 230.0, 0.0});
        REQUIRE_ENV(CenterTrans < 0.85 && ClearTrans > 0.99, "Orthographic CloudShadowProjection must attenuate beneath cloud box");

        FogParameters Hf{};
        Hf.ReferenceDensity = 0.01;
        Hf.FalloffHeightM   = 20.0;
        const double HorizTrans = FogTransmittance(Hf, {0.0, 0.0, 2.0}, {0.0, 1.0, 0.0}, 40.0);
        REQUIRE_ENV(std::isfinite(HorizTrans) && HorizTrans > 0.60 && HorizTrans < 0.72,
                    "FogTransmittance must remain exact and finite on horizontal rays (dz = 0)");

        LocalFogVolumeProfile FogBox{};
        FogBox.BoxCenterM      = {0.0, 15.0, 3.0};
        FogBox.BoxHalfExtentsM = {8.0, 8.0, 4.0};
        FogBox.ExtinctionPerM  = 0.06;

        PhotometricFogLuminaire Spot{};
        Spot.PositionM     = {0.0, 15.0, 5.5};
        Spot.AxisUnit      = {0.0, 0.0, -1.0};
        Spot.PeakIntensity = {140.0, 115.0, 80.0};

        LightningFogChannel Bolt{};
        Bolt.SegmentStartM = {-2.0, 16.0, 8.0};
        Bolt.SegmentEndM   = {-1.5, 16.0, 0.2};
        Bolt.LineRadiance  = {90.0, 130.0, 210.0};

        RadiantFogFacet Fire{};
        Fire.CornerA         = {1.5, 14.0, 0.2};
        Fire.CornerB         = {2.5, 14.0, 0.2};
        Fire.CornerC         = {2.0, 14.0, 1.4};
        Fire.SurfaceRadiance = {30.0, 11.0, 2.5};

        UnifiedVolumetricFogSpecification UnlitSpec{};
        UnlitSpec.HeightFog    = Hf;
        UnlitSpec.LocalVolumes = std::span<const LocalFogVolumeProfile>{&FogBox, 1};
        const auto UnlitRes    = EvaluateUnifiedVolumetricFog(UnlitSpec, {0.0, 0.0, 2.0}, {0.0, 1.0, 0.0}, 25.0);

        UnifiedVolumetricFogSpecification LitSpec = UnlitSpec;
        LitSpec.Luminaires        = std::span<const PhotometricFogLuminaire>{&Spot, 1};
        LitSpec.LightningChannels = std::span<const LightningFogChannel>{&Bolt, 1};
        LitSpec.RadiantFacets     = std::span<const RadiantFogFacet>{&Fire, 1};
        const auto LitRes         = EvaluateUnifiedVolumetricFog(LitSpec, {0.0, 0.0, 2.0}, {0.0, 1.0, 0.0}, 25.0);

        REQUIRE_ENV(LitRes.Radiance.R > UnlitRes.Radiance.R * 3.0 && LitRes.Transmittance < 0.85,
                    "Unified volumetric fog must scatter IES luminaire, lightning channel, and thermal fire facet radiance");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                  2. BUILD THE MULTI-REALM & DAY/NIGHT PORTAL ENSEMBLE SCENES
//------------------------------------------------------------------------------------------------------------------------

PlanetaryRealmEnsemble BuildThreeRealmPortalWorld()
{
    PlanetaryRealmEnsemble World{};
    World.Realms.resize(3u);

    // ============================================================================================================
    // REALM 0: Sunny Daytime World (3D Non-Tiled Cumulus Clouds, Wide Cloud Shadows, Daytime Luna, Sun Flare)
    // ============================================================================================================
    {
        PlanetaryRealm& R    = World.Realms[0];
        R.Atmosphere          = MakeEarthAtmosphere();
        R.ObserverAltitudeKm  = 0.08f;
        R.GroundPlaneY        = 0.0f;
        R.GroundAlbedoRgb     = {0.28f, 0.30f, 0.24f};
        R.StarBrightnessScale = 0.0f;
        R.MilkyWayIntensity   = 0.0f;
        R.AtmosphereRevision  = 101u;
        R.SkyViewRevision     = 201u;

        RealmSolarSource Sun{};
        Sun.DirectionUnit            = Norm3(-0.48f, 0.40f, 0.78f);
        Sun.DiscIrradiance           = {1.34f, 1.24f, 1.08f};
        Sun.MasterFlareGain          = 1.10f;
        Sun.ChromaticShift           = 0.008f;
        Sun.Flare.ApertureBlades     = 6u;
        Sun.Flare.GhostStrength      = 0.95;
        Sun.Flare.HaloStrength       = 0.55;
        Sun.Flare.StreakStrength     = 0.72;
        Sun.Flare.AnamorphicStrength = 0.52;
        Sun.Flare.StarburstStrength  = 1.08;
        R.Suns.push_back(Sun);

        // Daytime waxing Luna clearly framed in a clear blue-sky window on the upper right
        LunarBodyDescriptor DayMoon{};
        DayMoon.Preset             = LunarBodyPreset::Luna;
        DayMoon.DirectionUnit      = NormVec3(0.52f, 0.33f, 0.788f);
        DayMoon.AngularRadiusRad   = 0.052f;
        DayMoon.SurfaceBrightness  = 3.8f;
        DayMoon.EarthshineStrength = 0.020f;
        R.Moons.push_back(DayMoon);

        // Non-tiled 3D procedural fair-weather cumulus (~870 m cells -> sculpted puffs, open blue sky + dappled shadows)
        CloudVolumeProfile GlobalSlab{};
        GlobalSlab.Domain              = CloudVolumeDomain::PlanetarySlab;
        GlobalSlab.HeightProfile       = TroposphericCloudProfile::Cumulus;
        GlobalSlab.Slab.LowerHeightM   = 1050.0;
        GlobalSlab.Slab.TopHeightM     = 1780.0;
        GlobalSlab.Slab.Coverage       = 0.39;
        GlobalSlab.Slab.DensityScale   = 1.45;
        GlobalSlab.Slab.ExtinctionPerM = 0.046;
        GlobalSlab.PowderStrength      = 0.82;
        R.CloudVolumes.push_back(GlobalSlab);

        CloudVolumeProfile LocalPlume{};
        LocalPlume.Domain              = CloudVolumeDomain::BoundedLocalBox;
        LocalPlume.BoxCenterM          = {-16.0, 22.0, 44.0};
        LocalPlume.BoxHalfExtentsM     = {13.0, 5.5, 11.0};
        LocalPlume.BoxEdgeFalloffM     = 4.0;
        LocalPlume.Slab.Coverage       = 0.78;
        LocalPlume.Slab.DensityScale   = 1.85;
        LocalPlume.Slab.ExtinctionPerM = 0.095;
        R.CloudVolumes.push_back(LocalPlume);
        R.CloudShadowHalfSpanM = 1200.0f;
        R.CloudWindSeconds     = 95.0f;

        R.HeightFog.ReferenceDensity = 0.0009;
        R.HeightFog.FalloffHeightM   = 14.0;
        R.HeightFog.ReferenceHeightM = 0.0;

        LocalFogVolumeProfile ShaftBox{};
        ShaftBox.BoxCenterM      = {-7.0, 3.2, 16.0};
        ShaftBox.BoxHalfExtentsM = {6.0, 3.5, 8.0};
        ShaftBox.ExtinctionPerM  = 0.028;
        ShaftBox.NoiseModulation = 0.45;
        ShaftBox.NoiseScaleM     = 4.5;
        ShaftBox.PhaseAsymmetryG = 0.68;
        R.LocalFogVolumes.push_back(ShaftBox);

        R.SolidOccluders.push_back({{-6.2f, 2.0f, 14.5f}, 2.0f, {0.78f, 0.74f, 0.68f}, 0.4f});
        R.SolidOccluders.push_back({{7.2f, 1.5f, 18.0f}, 1.5f, {0.68f, 0.36f, 0.28f}, 0.35f});

        RealmGlassPane Glass{};
        Glass.CenterMeters      = {5.4f, 2.2f, 9.5f};
        Glass.NormalUnit        = {0.0f, 0.0f, -1.0f};
        Glass.HalfWidthMeters   = 1.4f;
        Glass.HalfHeightMeters  = 1.8f;
        Glass.TintTransmittance = {0.72f, 0.90f, 0.98f};
        Glass.FresnelF0         = 0.05f;
        R.GlassPanes.push_back(Glass);
    }

    // ============================================================================================================
    // REALM 1: Deep Starry Night World (Framed Moons Luna & Ember + Ringed Gas Giant + Stars + Nocturnal Fog)
    // ============================================================================================================
    {
        PlanetaryRealm& R       = World.Realms[1];
        R.Atmosphere            = MakeEarthAtmosphere();
        R.ObserverAltitudeKm    = 0.20f;
        R.GroundPlaneY          = 0.0f;
        R.GroundAlbedoRgb       = {0.15f, 0.16f, 0.20f};
        R.StarBrightnessScale   = 2.3f;
        R.StarLimitingMagnitude = 6.6f;
        R.ScintillationStrength = 1.25f;
        R.MilkyWayIntensity     = 2.5f;
        R.StarBasisRow0         = Norm3(0.82f, 0.18f, -0.54f);
        R.StarBasisRow1         = Norm3(-0.22f, 0.97f, -0.06f);
        R.AtmosphereRevision    = 102u;
        R.SkyViewRevision       = 202u;

        RealmSolarSource NightSun{};
        NightSun.DirectionUnit   = Norm3(-0.74f, -0.28f, -0.61f);
        NightSun.DiscIrradiance  = {1.38f, 1.32f, 1.24f};
        NightSun.EnableLensFlare = false;
        R.Suns.push_back(NightSun);

        // Framed squarely inside the cross-realm portal window (Y/Z in [0.16..0.22])!
        LunarBodyDescriptor Luna{};
        Luna.Preset             = LunarBodyPreset::Luna;
        Luna.DirectionUnit      = NormVec3(-0.11f, 0.19f, 0.975f);
        Luna.AngularRadiusRad   = 0.048f;
        Luna.SurfaceBrightness  = 1.9f;
        Luna.EarthshineStrength = 0.015f;
        Luna.RollAngleRad       = 0.14f;
        R.Moons.push_back(Luna);

        LunarBodyDescriptor VolcanicMoon{};
        VolcanicMoon.Preset             = LunarBodyPreset::Ember;
        VolcanicMoon.DirectionUnit      = NormVec3(0.22f, 0.20f, 0.955f);
        VolcanicMoon.AngularRadiusRad   = 0.032f;
        VolcanicMoon.SurfaceBrightness  = 2.1f;
        VolcanicMoon.EarthshineStrength = 0.018f;
        VolcanicMoon.RollAngleRad       = -0.22f;
        R.Moons.push_back(VolcanicMoon);

        PlanetaryBodyDescriptor SaturnLike{};
        SaturnLike.Archetype                      = PlanetarySurfaceArchetype::BandedGasGiant;
        SaturnLike.DirectionUnit                  = NormVec3(0.06f, 0.10f, 0.993f);
        SaturnLike.AngularRadiusRad               = 0.056f;
        SaturnLike.SpinAxisUnit                   = NormVec3(0.32f, 0.89f, -0.32f);
        SaturnLike.SurfaceAlbedoScale             = 1.25f;
        SaturnLike.Atmosphere.Enabled             = true;
        SaturnLike.Atmosphere.ShellHeightFraction = 0.09f;
        SaturnLike.Rings.Enabled                  = true;
        SaturnLike.Rings.InnerRadiusRatio         = 1.32f;
        SaturnLike.Rings.OuterRadiusRatio         = 2.28f;
        SaturnLike.Rings.PeakOpticalDepth         = 2.1f;
        SaturnLike.Rings.OuterTintRgb             = {0.90f, 0.83f, 0.70f};
        R.Planets.push_back(SaturnLike);

        PlanetaryBodyDescriptor OceanWorld{};
        OceanWorld.Archetype                      = PlanetarySurfaceArchetype::OceanicTerrestrial;
        OceanWorld.DirectionUnit                  = NormVec3(-0.34f, 0.24f, 0.91f);
        OceanWorld.AngularRadiusRad               = 0.036f;
        OceanWorld.SurfaceAlbedoScale             = 1.15f;
        OceanWorld.Atmosphere.Enabled             = true;
        OceanWorld.Atmosphere.ShellHeightFraction = 0.11f;
        OceanWorld.Rings.Enabled                  = false;
        R.Planets.push_back(OceanWorld);

        CustomStarDescriptor BrightCustom{};
        BrightCustom.DirectionUnit     = NormVec3(-0.02f, 0.23f, 0.97f);
        BrightCustom.TemperatureKelvin = 10500.0f;
        BrightCustom.RadianceGain      = 28.0f;
        R.CustomStars.push_back(BrightCustom);

        R.HeightFog.ReferenceDensity = 0.0025;
        R.HeightFog.FalloffHeightM   = 10.0;

        LocalFogVolumeProfile NightFog{};
        NightFog.BoxCenterM      = {0.0, 1.8, 17.0};
        NightFog.BoxHalfExtentsM = {9.0, 2.4, 8.0};
        NightFog.ExtinctionPerM  = 0.036;
        NightFog.NoiseModulation = 0.55;
        NightFog.NoiseScaleM     = 4.0;
        R.LocalFogVolumes.push_back(NightFog);

        PhotometricFogLuminaire Spot{};
        Spot.PositionM     = {-2.6, 4.4, 15.0};
        Spot.AxisUnit      = {0.18, -0.96, 0.20};
        Spot.PeakIntensity = {95.0, 74.0, 42.0};
        Spot.OuterConeCos  = 0.62;
        Spot.RangeMeters   = 14.0;
        R.Luminaires.push_back(Spot);

        LightningFogChannel Bolt{};
        Bolt.SegmentStartM = {3.2, 6.8, 20.0};
        Bolt.SegmentEndM   = {2.6, 0.2, 19.5};
        Bolt.LineRadiance  = {16.0, 20.0, 28.0};
        Bolt.CoreRadiusM   = 0.15;
        R.LightningChannels.push_back(Bolt);

        RadiantFogFacet Fire{};
        Fire.CornerA         = {0.2, 0.1, 14.0};
        Fire.CornerB         = {1.2, 0.1, 14.0};
        Fire.CornerC         = {0.7, 1.2, 14.0};
        Fire.SurfaceRadiance = {22.0, 7.5, 1.6};
        R.RadiantFacets.push_back(Fire);

        R.SolidOccluders.push_back({{-2.4f, 1.1f, 17.5f}, 1.1f, {0.55f, 0.58f, 0.66f}, 0.5f});
    }

    // ============================================================================================================
    // REALM 2: Binary-Sun Alien Exoplanet World (Distinct Rust-Violet Atmosphere LUT + 2 Suns + Real Glacier/Sulfur Moons)
    // ============================================================================================================
    {
        PlanetaryRealm& R               = World.Realms[2];
        R.Atmosphere                    = MakeEarthAtmosphere();
        R.Atmosphere.RayleighScattering = {14.5e-3, 9.2e-3, 18.8e-3};
        R.Atmosphere.MieScattering      = 7.5e-3;
        R.ObserverAltitudeKm            = 0.06f;
        R.GroundAlbedoRgb               = {0.34f, 0.19f, 0.14f};
        R.StarBrightnessScale           = 0.55f;
        R.MilkyWayIntensity             = 0.7f;
        R.AtmosphereRevision            = 103u;
        R.SkyViewRevision               = 203u;

        RealmSolarSource WarmStar{};
        WarmStar.DirectionUnit           = Norm3(-0.30f, 0.18f, 0.937f);
        WarmStar.DiscIrradiance          = {1.35f, 0.82f, 0.46f};
        WarmStar.Disc.AngularRadiusRad   = 0.0085;
        WarmStar.MasterFlareGain         = 0.78f;
        WarmStar.Flare.HaloStrength      = 0.36;
        WarmStar.Flare.StarburstStrength = 1.0;
        WarmStar.Flare.ApertureBlades    = 8u;
        R.Suns.push_back(WarmStar);

        RealmSolarSource BlueStar{};
        BlueStar.DirectionUnit            = Norm3(0.26f, 0.30f, 0.917f);
        BlueStar.DiscIrradiance           = {0.65f, 0.92f, 1.45f};
        BlueStar.Disc.AngularRadiusRad    = 0.0042;
        BlueStar.MasterFlareGain          = 0.72f;
        BlueStar.Flare.HaloStrength       = 0.34;
        BlueStar.Flare.AnamorphicStrength = 0.90;
        BlueStar.Flare.ApertureBlades     = 5u;
        R.Suns.push_back(BlueStar);

        LunarBodyDescriptor GlacierMoon{};
        GlacierMoon.Preset            = LunarBodyPreset::Glacier;
        GlacierMoon.DirectionUnit     = NormVec3(-0.04f, 0.36f, 0.93f);
        GlacierMoon.AngularRadiusRad  = 0.046f;
        GlacierMoon.SurfaceBrightness = 2.6f;
        R.Moons.push_back(GlacierMoon);

        LunarBodyDescriptor SulfurMoon{};
        SulfurMoon.Preset            = LunarBodyPreset::Sulfur;
        SulfurMoon.DirectionUnit     = NormVec3(0.42f, 0.34f, 0.84f);
        SulfurMoon.AngularRadiusRad  = 0.032f;
        SulfurMoon.SurfaceBrightness = 2.5f;
        R.Moons.push_back(SulfurMoon);

        PlanetaryBodyDescriptor IceGiant{};
        IceGiant.Archetype              = PlanetarySurfaceArchetype::MethaneIceGiant;
        IceGiant.DirectionUnit          = NormVec3(-0.46f, 0.30f, 0.835f);
        IceGiant.AngularRadiusRad       = 0.055f;
        IceGiant.Rings.Enabled          = true;
        IceGiant.Rings.InnerRadiusRatio = 1.45f;
        IceGiant.Rings.OuterRadiusRatio = 1.95f;
        IceGiant.Rings.PeakOpticalDepth = 0.9f;
        IceGiant.Rings.OuterTintRgb     = {0.72f, 0.88f, 0.98f};
        R.Planets.push_back(IceGiant);

        CloudVolumeProfile AlienClouds{};
        AlienClouds.Domain              = CloudVolumeDomain::PlanetarySlab;
        AlienClouds.HeightProfile       = TroposphericCloudProfile::Altostratus;
        AlienClouds.Slab.LowerHeightM   = 1300.0;
        AlienClouds.Slab.TopHeightM     = 1950.0;
        AlienClouds.Slab.Coverage       = 0.46;
        AlienClouds.Slab.DensityScale   = 1.45;
        AlienClouds.Slab.ExtinctionPerM = 0.045;
        R.CloudVolumes.push_back(AlienClouds);

        R.HeightFog.ReferenceDensity = 0.0035;
        R.HeightFog.FalloffHeightM   = 15.0;
        R.SolidOccluders.push_back({{0.0f, 1.6f, 14.0f}, 1.6f, {0.68f, 0.56f, 0.50f}, 0.4f});
    }

    // ============================================================================================================
    // CROSS-REALM PORTAL APERTURE: Connects Daytime (Realm 0) -> Starry Moonlit Ringed-Planet Night (Realm 1)!
    // ============================================================================================================
    {
        PortalAperture Portal{};
        Portal.SourceRealmIndex   = 0u;
        Portal.TargetRealmIndex   = 1u;
        Portal.CenterMeters       = {0.1f, 2.45f, 8.0f};
        Portal.NormalUnit         = {0.0f, 0.0f, -1.0f};
        Portal.RightUnit          = {1.0f, 0.0f, 0.0f};
        Portal.UpUnit             = {0.0f, 1.0f, 0.0f};
        Portal.HalfWidthMeters    = 2.65f;
        Portal.HalfHeightMeters   = 2.25f;
        Portal.FrameWidthMeters   = 0.13f;
        Portal.FrameAlbedoRgb     = {0.20f, 0.22f, 0.26f};
        Portal.TargetOriginMeters = {0.0f, 2.35f, 2.0f};
        Portal.TargetRightUnit    = {1.0f, 0.0f, 0.0f};
        Portal.TargetUpUnit       = {0.0f, 1.0f, 0.0f};
        Portal.TargetForwardUnit  = {0.0f, 0.0f, 1.0f};
        World.Portals.push_back(Portal);
    }

    return World;
}

//------------------------------------------------------------------------------------------------------------------------
//                  3. GPU VULKAN 1.3 EXECUTION, READBACK VERIFICATION & EXHIBIT RENDERING
//------------------------------------------------------------------------------------------------------------------------

void VerifyGpuPipelineAndRenderExhibits(const std::filesystem::path& RepoRoot,
                                        const std::filesystem::path& ShaderDir,
                                        const std::filesystem::path& OutputDir,
                                        bool                         MirrorToTrackedShowcase)
{
    std::cout << "[4/4] Initializing VulkanComputeExchange & Compiling 11 SPIR-V Compute Pipelines...\n";
    VulkanComputeExchange Exchange{};
    std::string           Diag;
    REQUIRE_ENV(Exchange.Initialize("", &Diag), ("Vulkan initialization failed: " + Diag).c_str());
    std::cout << "      Active Vulkan Device: " << Exchange.DeviceName() << "\n";

    PlanetarySubmissionSequence Sequence{};
    const auto JpgPath = (RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps/MoonAlbedoSource2048.jpg").string();
    const auto CsvPath = (RepoRoot / "EngineContent/EnvironmentArchives/CelestialMaps/Hipparcos8.csv").string();
    REQUIRE_ENV(Sequence.Initialize(Exchange, ShaderDir.string(), JpgPath, CsvPath, &Diag),
                ("PlanetarySubmissionSequence::Initialize failed: " + Diag).c_str());
    std::cout << "      Uploaded 2048x1024x6 real sRGB Lunar Atlas + " << Sequence.PlanetaryDome().IndexedStarCount()
              << " cell-indexed Hipparcos stars\n";
    REQUIRE_ENV(Sequence.PlanetaryDome().IndexedStarCount() > 30000u, "Hipparcos8.csv must index > 30,000 stars on GPU");

    // 3A. Verify Multi-Realm Dirty-Gated AtmosphereProjection Bakes + Numerical Readback
    PlanetaryRealmEnsemble World    = BuildThreeRealmPortalWorld();
    PlanetaryCameraView    ProbeCam = MakeLookAtCamera(0u, {0.0f, 2.2f, 0.0f}, {0.0f, 2.5f, 10.0f}, 320u, 180u);

    REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, World, ProbeCam), "Initial 3-realm GPU frame failed");
    REQUIRE_ENV(Sequence.AtmosphereLuts().LastAtmosphereBakedCount() == 3u &&
                    Sequence.AtmosphereLuts().LastSkyViewBakedCount() == 3u,
                "First frame must bake all 3 realm LUT slices");

    ProbeCam.FrameIndex = 1u;
    REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, World, ProbeCam), "Steady GPU frame failed");
    REQUIRE_ENV(Sequence.AtmosphereLuts().LastAtmosphereBakedCount() == 0u &&
                    Sequence.AtmosphereLuts().LastSkyViewBakedCount() == 0u,
                "Unchanged revisions must trigger zero AtmosphereProjection dispatches");

    World.Realms[0].SkyViewRevision++;
    ProbeCam.FrameIndex = 2u;
    REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, World, ProbeCam), "SkyView-dirty GPU frame failed");
    REQUIRE_ENV(Sequence.AtmosphereLuts().LastAtmosphereBakedCount() == 0u &&
                    Sequence.AtmosphereLuts().LastSkyViewBakedCount() == 1u,
                "Per-realm SkyView dirty gate must rebake only the single modified realm slice");

    std::vector<float> GpuTrans0;
    REQUIRE_ENV(Sequence.AtmosphereLuts().ReadbackTransmittanceLayer(Exchange, 0u, GpuTrans0),
                "GPU Transmittance LUT readback failed");
    const size_t ZenithIdx = (2u * kTransmittanceLutWidth + 12u) * 4u;
    const size_t LimbIdx   = (2u * kTransmittanceLutWidth + 240u) * 4u;
    REQUIRE_ENV(GpuTrans0[ZenithIdx + 1u] > GpuTrans0[LimbIdx + 1u] &&
                    GpuTrans0[LimbIdx + 0u] > GpuTrans0[LimbIdx + 2u],
                "GPU Transmittance LUT must exhibit higher zenith transmittance and redder limb optical path");

    std::vector<float> Shadow0, Shadow1;
    REQUIRE_ENV(Sequence.Clouds().ReadbackShadowLayer(Exchange, 0u, Shadow0) &&
                    Sequence.Clouds().ReadbackShadowLayer(Exchange, 1u, Shadow1),
                "GPU CloudShadowArray readback failed");
    float MinTr0 = 1.0f, MaxTr0 = 0.0f, MinTr1 = 1.0f;
    for (size_t Idx = 1u; Idx < Shadow0.size(); Idx += 4u)
    {
        MinTr0 = std::min(MinTr0, Shadow0[Idx]);
        MaxTr0 = std::max(MaxTr0, Shadow0[Idx]);
        MinTr1 = std::min(MinTr1, Shadow1[Idx]);
    }
    REQUIRE_ENV(MinTr0 < 0.50f && MaxTr0 > 0.95f && MinTr1 > 0.999f,
                "GPU CloudShadowArray must contain both sunlit gaps (>0.95) and dense cloud shadows (<0.50) across ±1200m");

    // 3B. Verify GPU 1x1-Per-Source OpticalLensFlare Visibility Across 5 Physical Occlusion Scenarios!
    std::cout << "      Verifying GPU 1x1 OpticalLensFlare visibility (open sky / sphere / cloud / glass / portal)...\n";
    {
        PlanetaryRealmEnsemble FlareTest{};
        FlareTest.Realms.resize(2u);
        PlanetaryRealm& R    = FlareTest.Realms[0];
        R.ObserverAltitudeKm = 0.05f;
        RealmSolarSource Sun{};
        Sun.DirectionUnit   = Norm3(0.0f, 0.35f, 0.9367f);
        Sun.DiscIrradiance  = {1.0f, 1.0f, 1.0f};
        Sun.MasterFlareGain = 1.0f;
        R.Suns.push_back(Sun);

        PlanetaryCameraView               Cam = MakeLookAtCamera(0u, {0.0f, 2.0f, 0.0f}, {0.0f, 5.5f, 10.0f}, 320u, 180u);
        std::vector<std::array<float, 4>> Vis;

        // (1) Open sky
        REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Exchange, 1u, Vis),
                    "Open-sky flare visibility execution failed");
        const float OpenVis = Vis[0][3];
        REQUIRE_ENV(OpenVis > 0.92f, "Unoccluded solar disc must produce > 0.92 GPU flare visibility");

        // (2) Solid reversed-Z sphere directly along Sun.DirectionUnit
        R.SolidOccluders.push_back({{0.0f, 2.0f + 0.35f * 15.0f, 0.9367f * 15.0f}, 1.5f, {0.5f, 0.5f, 0.5f}, 0.5f});
        REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Exchange, 1u, Vis),
                    "Sphere-occluded flare visibility execution failed");
        REQUIRE_ENV(Vis[0][3] == 0.0f, "Reversed-Z solid sphere covering solar disc must yield 0.0 flare visibility");
        R.SolidOccluders.clear();

        // (3) Tinted dielectric glass pane ({0.28, 0.72, 0.96}) in front of the sun
        RealmGlassPane BlueGlass{};
        BlueGlass.CenterMeters      = {0.0f, 2.0f + 0.35f * 8.0f, 0.9367f * 8.0f};
        BlueGlass.NormalUnit        = {0.0f, 0.0f, -1.0f};
        BlueGlass.HalfWidthMeters   = 3.0f;
        BlueGlass.HalfHeightMeters  = 3.0f;
        BlueGlass.TintTransmittance = {0.28f, 0.72f, 0.96f};
        BlueGlass.FresnelF0         = 0.04f;
        R.GlassPanes.push_back(BlueGlass);
        REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Exchange, 1u, Vis),
                    "Glass-tinted flare visibility execution failed");
        REQUIRE_ENV(Vis[0][2] > Vis[0][1] && Vis[0][1] > Vis[0][0] && Vis[0][0] > 0.15f,
                    "Dielectric glass mask must spectrally tint GPU solar flare visibility");
        R.GlassPanes.clear();

        // (4) Volumetric cloud box partially attenuating the solar disc
        CloudVolumeProfile SunCloud{};
        SunCloud.Domain              = CloudVolumeDomain::BoundedLocalBox;
        SunCloud.BoxCenterM          = {0.0, 2.0 + 0.35 * 30.0, 0.9367 * 30.0};
        SunCloud.BoxHalfExtentsM     = {12.0, 12.0, 12.0};
        SunCloud.BoxEdgeFalloffM     = 2.0;
        SunCloud.Slab.Coverage       = 0.95;
        SunCloud.Slab.DensityScale   = 1.4;
        // 📝 A quarter of the former 0.06: the flare dims as the disc does, by the cloud transmittance to the fourth power
        //    (ResolveSolarDiscTransmittance), so the same partial attenuation takes a quarter of the optical depth.
        SunCloud.Slab.ExtinctionPerM = 0.015;
        R.CloudVolumes.push_back(SunCloud);
        Cam.FrameIndex = 0u;
        REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Exchange, 1u, Vis),
                    "Cloud-attenuated flare visibility execution failed");
        REQUIRE_ENV(Vis[0][3] > 0.05f && Vis[0][3] < OpenVis * 0.90f,
                    "Volumetric cloud transmittance must partially attenuate GPU solar flare visibility");
        R.CloudVolumes.clear();

        // (5) Cross-realm sun inside Portal Target Realm 1
        R.Suns.clear();
        FlareTest.Realms[1].Suns.push_back(Sun);
        PortalAperture Port{};
        Port.SourceRealmIndex   = 0u;
        Port.TargetRealmIndex   = 1u;
        Port.CenterMeters       = {0.0f, 2.0f + 0.35f * 8.0f, 0.9367f * 8.0f};
        Port.HalfWidthMeters    = 2.5f;
        Port.HalfHeightMeters   = 2.5f;
        Port.TargetOriginMeters = {0.0f, 2.0f, 0.0f};
        FlareTest.Portals.push_back(Port);

        REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Exchange, 1u, Vis),
                    "Portal-open flare visibility execution failed");
        const float PortalOpenVis = Vis[0][3];

        FlareTest.Portals[0].CenterMeters[0] = 6.5f;
        REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, FlareTest, Cam) &&
                        Sequence.LensFlare().ReadbackVisibilityValues(Exchange, 1u, Vis),
                    "Portal-clipped flare visibility execution failed");
        const float PortalClippedVis = Vis[0][3];
        REQUIRE_ENV(PortalOpenVis > 0.85f && PortalClippedVis == 0.0f,
                    "Cross-realm sun viewed through PortalAperture must clip cleanly against portal window bounds");
    }

    // 3C. Render 7 Showcase GPU Exhibits (800 x 450 crisp widescreen) & Verify Day/Night Portal + Cloud Shadow Contrast!
    constexpr uint32_t kExhibitW  = 800u;
    constexpr uint32_t kExhibitH  = 450u;
    const auto         TrackedDir = RepoRoot / "Exhibits/EnvironmentRealm/Images";

    auto SaveExhibit = [&](const char* FileName, const PlanetaryCameraView& Cam, const PlanetaryRealmEnsemble& Scene) {
        PlanetaryCameraView View = Cam;
        View.ViewportWidth       = kExhibitW;
        View.ViewportHeight      = kExhibitH;
        View.FrameIndex          = 0u;
        REQUIRE_ENV(Sequence.ExecuteFrame(Exchange, Scene, View), FileName);

        std::vector<float>   Hdr;
        std::vector<uint8_t> Srgb;
        REQUIRE_ENV(Sequence.ReadbackHdrFrame(Exchange, Hdr), "HDR frame readback failed");
        TonemapHdrToSrgb8(Hdr, kExhibitW, kExhibitH, View.ExposureScale, Srgb);

        REQUIRE_ENV(WritePngRgba(OutputDir / FileName, kExhibitW, kExhibitH, Srgb), "Failed writing PNG exhibit");
        if (MirrorToTrackedShowcase)
        {
            REQUIRE_ENV(WritePngRgba(TrackedDir / FileName, kExhibitW, kExhibitH, Srgb), "Failed mirroring PNG exhibit");
        }
        return Hdr;
    };

    std::cout << "      Rendering 7 GPU Vulkan PlanetaryAtmosphere & Multi-Realm Portal Showcase Exhibits (800x450)...\n";

    // Exhibit 1: Flagship Simultaneous Day/Night Cross-Realm Portal Frame (Daytime Luna + Night Portal Luna/Ember/Saturn)!
    {
        const auto Cam = MakeLookAtCamera(0u, {-0.3f, 2.20f, -0.8f}, {0.0f, 2.50f, 9.0f}, kExhibitW, kExhibitH, 64.0f, 1.50f);
        const auto Hdr = SaveExhibit("DayNightPortalEnsemble.png", Cam, World);

        std::vector<float> VisRealm;
        REQUIRE_ENV(Sequence.ReadbackVisibilityLayer(Exchange, 1u, VisRealm), "Visibility Layer 1 readback failed");
        const size_t   LeftExteriorPx = (115u * kExhibitW + 75u) * 4u;
        const size_t   PortalCenterPx = (200u * kExhibitW + 410u) * 4u;
        const uint32_t ExteriorRealm  = static_cast<uint32_t>(VisRealm[LeftExteriorPx + 3u] + 0.5f);
        const uint32_t InteriorRealm  = static_cast<uint32_t>(VisRealm[PortalCenterPx + 3u] + 0.5f);
        REQUIRE_ENV(ExteriorRealm == 0u && InteriorRealm == 1u,
                    "DayNightPortalEnsemble must route exterior rays to Daytime Realm 0 and interior portal rays to Night Realm 1");
        REQUIRE_ENV(Hdr[LeftExteriorPx + 2u] > 0.08f, "Daytime exterior sky must carry scattered blue atmospheric radiance");
    }

    // Exhibit 2: Golden-Hour / Twilight Atmosphere, Optical Lens Flare & Crescent Moon
    {
        PlanetaryRealmEnsemble TwilightScene                  = World;
        TwilightScene.Portals.clear();
        TwilightScene.Realms[0].Suns[0].DirectionUnit         = Norm3(0.12f, 0.135f, 0.984f);
        TwilightScene.Realms[0].Suns[0].DiscIrradiance        = {1.52f, 0.92f, 0.56f};
        TwilightScene.Realms[0].Suns[0].MasterFlareGain       = 1.30f;
        TwilightScene.Realms[0].Moons[0].DirectionUnit        = NormVec3(-0.36f, 0.28f, 0.89f);
        TwilightScene.Realms[0].Moons[0].AngularRadiusRad     = 0.046f;
        TwilightScene.Realms[0].Moons[0].SurfaceBrightness    = 2.6f;
        TwilightScene.Realms[0].CloudVolumes[0].Slab.Coverage = 0.36;
        TwilightScene.Realms[0].HeightFog.ReferenceDensity    = 0.0007;
        TwilightScene.Realms[0].TwilightStrength              = 1.5f;
        TwilightScene.Realms[0].SkyViewRevision++;
        const auto Cam = MakeLookAtCamera(0u, {0.0f, 2.1f, 0.0f}, {0.0f, 2.9f, 12.0f}, kExhibitW, kExhibitH, 60.0f, 1.55f);
        (void)SaveExhibit("TwilightAtmosphereAndLensFlare.png", Cam, TwilightScene);
    }

    // Exhibit 3: Celestial Dome Night Sky (Real 2K Moons Luna/Ember/Glacier + Ringed Gas Giant + Oceanic Planet + Milky Way)
    {
        PlanetaryRealmEnsemble NightDome = World;
        NightDome.Realms[1].LocalFogVolumes.clear();
        NightDome.Realms[1].HeightFog.ReferenceDensity = 0.0004;
        NightDome.Realms[1].Moons[0].DirectionUnit     = NormVec3(-0.22f, 0.42f, 0.88f);
        NightDome.Realms[1].Moons[0].AngularRadiusRad  = 0.062f;
        NightDome.Realms[1].Moons[1].DirectionUnit     = NormVec3(0.30f, 0.48f, 0.82f);
        NightDome.Realms[1].Moons[1].AngularRadiusRad  = 0.040f;
        NightDome.Realms[1].Planets[0].DirectionUnit   = NormVec3(0.06f, 0.30f, 0.95f);
        NightDome.Realms[1].Planets[0].AngularRadiusRad = 0.072f;

        LunarBodyDescriptor Glacier{};
        Glacier.Preset            = LunarBodyPreset::Glacier;
        Glacier.DirectionUnit     = NormVec3(-0.48f, 0.34f, 0.81f);
        Glacier.AngularRadiusRad  = 0.044f;
        Glacier.SurfaceBrightness = 2.1f;
        NightDome.Realms[1].Moons.push_back(Glacier);

        const auto Cam = MakeLookAtCamera(1u, {0.0f, 2.0f, 0.0f}, {-0.3f, 4.8f, 12.0f}, kExhibitW, kExhibitH, 58.0f, 2.9f);
        (void)SaveExhibit("CelestialDomeMoonsPlanetsStars.png", Cam, NightDome);
    }

    // Exhibit 4: Non-Tiled 3D Volumetric Clouds, Front-Lit Daytime Gibbous Luna & Crisp Dappled Ground Shadows!
    {
        PlanetaryRealmEnsemble CloudScene = World;
        CloudScene.Portals.clear();
        CloudScene.Realms[0].GlassPanes.clear();
        CloudScene.Realms[0].LocalFogVolumes.clear();
        CloudScene.Realms[0].GroundAlbedoRgb                     = {0.38f, 0.41f, 0.32f};
        // Sun behind-upper-left illuminates cumulus billows in front-lit 3D relief AND lights daytime Luna as a gibbous Moon!
        CloudScene.Realms[0].Suns[0].DirectionUnit               = Norm3(-0.62f, 0.58f, -0.528f);
        CloudScene.Realms[0].Suns[0].DiscIrradiance              = {1.45f, 1.36f, 1.22f};
        CloudScene.Realms[0].Suns[0].EnableLensFlare             = false;
        CloudScene.Realms[0].Moons[0].DirectionUnit              = NormVec3(0.44f, 0.30f, 0.846f);
        CloudScene.Realms[0].Moons[0].AngularRadiusRad           = 0.054f;
        CloudScene.Realms[0].Moons[0].SurfaceBrightness          = 3.2f;
        CloudScene.Realms[0].CloudVolumes[0].Slab.LowerHeightM   = 420.0;
        CloudScene.Realms[0].CloudVolumes[0].Slab.TopHeightM     = 960.0;
        CloudScene.Realms[0].CloudVolumes[0].Slab.Coverage       = 0.45;
        CloudScene.Realms[0].CloudVolumes[0].Slab.DensityScale   = 1.85;
        CloudScene.Realms[0].CloudVolumes[0].Slab.ExtinctionPerM = 0.068;
        // Two sculpted foreground 3D cumulus puffs casting crisp dappled shadows across the foreground checker plane & spheres!
        CloudScene.Realms[0].CloudVolumes[1].BoxCenterM          = {-28.0, 26.0, 4.0};
        CloudScene.Realms[0].CloudVolumes[1].BoxHalfExtentsM     = {18.0, 8.0, 16.0};
        CloudScene.Realms[0].CloudVolumes[1].BoxEdgeFalloffM     = 4.5;
        CloudScene.Realms[0].CloudVolumes[1].Slab.Coverage       = 0.86;
        CloudScene.Realms[0].CloudVolumes[1].Slab.DensityScale   = 2.2;
        CloudScene.Realms[0].CloudVolumes[1].Slab.ExtinctionPerM = 0.14;

        CloudVolumeProfile SecondPuff                            = CloudScene.Realms[0].CloudVolumes[1];
        SecondPuff.BoxCenterM                                    = {-14.0, 38.0, 36.0};
        SecondPuff.BoxHalfExtentsM                               = {24.0, 9.5, 20.0};
        SecondPuff.BoxEdgeFalloffM                               = 5.5;
        CloudScene.Realms[0].CloudVolumes.push_back(SecondPuff);

        CloudVolumeProfile ThirdPuff                             = SecondPuff;
        ThirdPuff.BoxCenterM                                     = {-52.0, 34.0, 28.0};
        ThirdPuff.BoxHalfExtentsM                                = {22.0, 8.5, 18.0};
        CloudScene.Realms[0].CloudVolumes.push_back(ThirdPuff);
        CloudScene.Realms[0].CloudShadowHalfSpanM                = 900.0f;
        CloudScene.Realms[0].SkyViewRevision++;

        const auto Cam = MakeLookAtCamera(0u, {0.0f, 7.5f, -10.0f}, {0.0f, 4.2f, 26.0f}, kExhibitW, kExhibitH, 64.0f, 1.50f);
        const auto Hdr = SaveExhibit("VolumetricCloudsAndOrthoShadows.png", Cam, CloudScene);

        // Verify sunlit vs cloud-shadowed ground contrast directly on the rendered HDR ground rows!
        float MinGroundLum = 1.0e9f, MaxGroundLum = 0.0f;
        for (uint32_t Py = 245u; Py < 435u; Py += 3u)
        {
            for (uint32_t Px = 60u; Px < 740u; Px += 4u)
            {
                const size_t Off = (size_t(Py) * kExhibitW + size_t(Px)) * 4u;
                const float  Lum = 0.2126f * Hdr[Off + 0u] + 0.7152f * Hdr[Off + 1u] + 0.0722f * Hdr[Off + 2u];
                MinGroundLum     = std::min(MinGroundLum, Lum);
                MaxGroundLum     = std::max(MaxGroundLum, Lum);
            }
        }
        REQUIRE_ENV(MaxGroundLum > MinGroundLum * 2.5f,
                    "Ground plane in VolumetricCloudsAndOrthoShadows must exhibit strong sunlit-to-cloud-shadow contrast");
    }

    // Exhibit 5: Coupled Volumetric Fog (IES Spotlight + Lightning Channel + Radiant Fire Facets + Moonlit Haze)
    {
        const auto Cam = MakeLookAtCamera(1u, {0.0f, 2.2f, 2.5f}, {0.2f, 2.5f, 16.0f}, kExhibitW, kExhibitH, 60.0f, 2.5f);
        (void)SaveExhibit("VolumetricFogLuminairesAndShafts.png", Cam, World);
    }

    // Exhibit 6: Binary-Sun Alien Exoplanet Ensemble (2 Suns + Dual Lens Flares + Methane Ringed Giant + Glacier & Sulfur Moons)
    {
        const auto Cam = MakeLookAtCamera(2u, {0.0f, 2.2f, 0.0f}, {0.0f, 3.6f, 12.0f}, kExhibitW, kExhibitH, 62.0f, 1.60f);
        (void)SaveExhibit("BinarySunExoplanetEnsemble.png", Cam, World);
    }

    // Exhibit 7: Complete 6-Body Real 2K Lunar Roster (Luna, Ember, Glacier, Sulfur, Shroud, Shard)
    {
        PlanetaryRealmEnsemble RosterWorld{};
        RosterWorld.Realms.resize(1u);
        PlanetaryRealm& R       = RosterWorld.Realms[0];
        R.Atmosphere            = MakeEarthAtmosphere();
        R.ObserverAltitudeKm    = 0.35f;
        R.GroundPlaneY          = 0.0f;
        R.GroundAlbedoRgb       = {0.18f, 0.20f, 0.25f};
        R.StarBrightnessScale   = 2.1f;
        R.StarLimitingMagnitude = 6.5f;
        R.ScintillationStrength = 0.8f;
        R.MilkyWayIntensity     = 2.0f;
        R.StarBasisRow0         = Norm3(0.86f, 0.14f, -0.49f);
        R.StarBasisRow1         = Norm3(-0.18f, 0.98f, -0.05f);
        R.AtmosphereRevision    = 401u;
        R.SkyViewRevision       = 501u;

        // Oblique anti-view solar illumination (-Z/+X/+Y) so every moon displays crisp Lommel-Seeliger phase relief & craters
        RealmSolarSource NightSun{};
        NightSun.DirectionUnit   = Norm3(0.42f, -0.22f, -0.88f);
        NightSun.DiscIrradiance  = {1.42f, 1.38f, 1.34f};
        NightSun.EnableLensFlare = false;
        R.Suns.push_back(NightSun);

        const std::array<LunarBodyPreset, 6> Presets = {
            LunarBodyPreset::Luna,
            LunarBodyPreset::Ember,
            LunarBodyPreset::Glacier,
            LunarBodyPreset::Sulfur,
            LunarBodyPreset::Shroud,
            LunarBodyPreset::Shard,
        };
        const std::array<float, 6> AzimuthX = {-0.54f, -0.325f, -0.11f, 0.11f, 0.325f, 0.54f};
        const std::array<float, 6> ElevY    = { 0.25f,  0.31f,   0.26f, 0.31f, 0.25f,  0.30f};
        const std::array<float, 6> Radii    = { 0.068f, 0.064f,  0.064f, 0.064f, 0.066f, 0.062f};
        const std::array<float, 6> Rolls    = { 0.00f,  0.22f,  -0.18f, 0.12f, -0.08f, 0.35f};

        for (size_t Idx = 0u; Idx < 6u; ++Idx)
        {
            LunarBodyDescriptor Body{};
            Body.Preset                = Presets[Idx];
            Body.DirectionUnit         = NormVec3(AzimuthX[Idx], ElevY[Idx], 0.92f);
            Body.AngularRadiusRad      = Radii[Idx];
            Body.SurfaceBrightness     = 1.85f;
            Body.EarthshineStrength    = 0.018f;
            Body.RollAngleRad          = Rolls[Idx];
            Body.SpinLongitudeRad      = 0.35f * static_cast<float>(Idx);
            Body.LibrationLatitudeRad  = 0.08f * ((Idx % 2u == 0u) ? 1.0f : -1.0f);
            R.Moons.push_back(Body);
        }

        R.HeightFog.ReferenceDensity = 0.0006;
        R.HeightFog.FalloffHeightM   = 12.0;

        const auto Cam = MakeLookAtCamera(0u, {0.0f, 2.2f, 0.0f}, {0.0f, 3.3f, 14.0f}, kExhibitW, kExhibitH, 68.0f, 2.35f);
        (void)SaveExhibit("LunarSixBodyRoster.png", Cam, RosterWorld);
    }

    // Exhibit 8: Triggered Tornado Supercell Vortex + Rotating Wall Cloud + Lofted Dust/Debris Skirt (ForceField GPU Coupling)
    {
        PlanetaryRealmEnsemble StormWorld{};
        StormWorld.Realms.resize(1u);
        PlanetaryRealm& R        = StormWorld.Realms[0];
        R.Atmosphere             = MakeEarthAtmosphere();
        R.Atmosphere.MieScattering *= 1.55;
        R.ObserverAltitudeKm     = 0.014f;
        R.GroundPlaneY           = 0.0f;
        R.GroundAlbedoRgb        = {0.28f, 0.25f, 0.20f};
        R.CloudShadowHalfSpanM   = 1800.0f;
        R.AtmosphereRevision     = 601u;
        R.SkyViewRevision        = 701u;

        // Low dramatic side-oblique sun illuminating the funnel cylinder, rotating wall cloud, and ochre dust collar
        RealmSolarSource StormSun{};
        StormSun.DirectionUnit   = Norm3(-0.76f, 0.32f, -0.56f);
        StormSun.DiscIrradiance  = {1.48f, 1.34f, 1.18f};
        StormSun.EnableLensFlare = false;
        R.Suns.push_back(StormSun);

        // Supercell tropospheric cloud deck: the triggered ForceField vortex twists the wall cloud AND
        // condenses the curved-spine tornado funnel all the way from cloud base (340 m) down to ground touchdown!
        CloudVolumeProfile SupercellDeck{};
        SupercellDeck.Domain              = CloudVolumeDomain::PlanetarySlab;
        SupercellDeck.Slab.LowerHeightM   = 340.0;
        SupercellDeck.Slab.TopHeightM     = 1350.0;
        SupercellDeck.Slab.Coverage       = 0.66;
        SupercellDeck.Slab.DensityScale   = 1.85;
        SupercellDeck.Slab.ExtinctionPerM = 0.072;
        R.CloudVolumes.push_back(SupercellDeck);

        // Configure ambient sheared storm inflow + trigger Burgers-Rott Tornado Vortex + Haboob Gust Front
        PlanetaryWindProfile StormWind{};
        StormWind.SpeedMetersPerSecond = 16.5;
        StormWind.BearingRadians       = 0.42;
        StormWind.ShearPerKilometer    = 0.38;
        StormWind.GustAmplitude        = 0.28;
        R.ForceField.SetWindProfile(StormWind);

        TornadoColumnParameters Funnel{};
        Funnel.GroundAnchorMeters      = {8.0, 0.0, 320.0};
        Funnel.TranslationVelocityMps  = {0.0, 0.0, 0.0};
        Funnel.SpineLeanXMeters        = 46.0;
        Funnel.SpineLeanZMeters        = -18.0;
        Funnel.ColumnHeightMeters      = 420.0;
        Funnel.GroundCoreRadiusMeters  = 16.0;
        Funnel.CollarCoreRadiusMeters  = 78.0;
        Funnel.TangentialPeakSpeedMps  = 78.0;
        Funnel.RadialInflowSpeedMps    = 28.0;
        Funnel.AxialUpdraftSpeedMps    = 42.0;
        Funnel.CondensationStrength    = 0.98;
        Funnel.DebrisSkirtHeightMeters = 92.0;
        Funnel.DebrisExtinctionPerM    = 0.048;
        Funnel.DebrisAlbedoRgb         = {0.68, 0.50, 0.32};
        (void)R.ForceField.TriggerTornado(Funnel, {0.5, 60.0, 2.0});

        GustFrontParameters OutflowGust{};
        OutflowGust.OriginMeters           = {-145.0, 0.0, 360.0};
        OutflowGust.PropagationDirectionXZ = {0.95, 0.0, 0.31};
        OutflowGust.FrontSpeedMps          = 0.0;
        OutflowGust.PeakGustSpeedMps       = 26.0;
        OutflowGust.CrestDepthMeters       = 95.0;
        OutflowGust.VerticalExtentMeters   = 96.0;
        OutflowGust.LoftedDustExtinctionM  = 0.022;
        OutflowGust.DustAlbedoRgb          = {0.72, 0.55, 0.36};
        (void)R.ForceField.TriggerGustFront(OutflowGust, {0.5, 60.0, 2.0});
        R.ForceField.Step(4.5); // Fully ramped tornado + gust swirl!

        R.HeightFog.ReferenceDensity = 0.00028;
        R.HeightFog.FalloffHeightM   = 55.0;

        const auto StormCam = MakeLookAtCamera(0u, {0.0f, 11.0f, -55.0f}, {16.0f, 128.0f, 320.0f}, kExhibitW, kExhibitH, 58.0f, 1.48f);
        const auto StormHdr = SaveExhibit("TornadoSupercellAndDustVortex.png", StormCam, StormWorld);

        // Verify funnel column + dust collar produce strong horizontal HDR contrast against surrounding storm sky
        const auto HdrLumAt = [&](uint32_t Px, uint32_t Py) -> float {
            const size_t Off = (static_cast<size_t>(Py) * kExhibitW + Px) * 4u;
            return 0.2126f * StormHdr[Off + 0u] + 0.7152f * StormHdr[Off + 1u] + 0.0722f * StormHdr[Off + 2u];
        };
        float MinMidRowLum = 1.0e9f, MaxMidRowLum = 0.0f;
        for (uint32_t X = kExhibitW / 5u; X < (kExhibitW * 4u) / 5u; X += 4u)
        {
            const float L = HdrLumAt(X, (kExhibitH * 56u) / 100u);
            MinMidRowLum  = std::min(MinMidRowLum, L);
            MaxMidRowLum  = std::max(MaxMidRowLum, L);
        }
        REQUIRE_ENV(MaxMidRowLum > MinMidRowLum * 1.35f,
                    "Triggered tornado condensation funnel and dust skirt must create clear horizontal storm contrast on GPU");
    }

    Sequence.Shutdown(Exchange);
    Exchange.Shutdown();
}

} // namespace

int main(int Argc, char** Argv)
{
    const std::filesystem::path RepoRoot  = (Argc > 1) ? Argv[1] : ".";
    const std::filesystem::path ShaderDir = (Argc > 2) ? Argv[2] : "build/ShaderArchives/Environment";
    const std::filesystem::path OutputDir = (Argc > 3) ? Argv[3] : "build/environment-realm-exhibits";

    bool MirrorShowcase = false;
    for (int Idx = 1; Idx < Argc; ++Idx)
    {
        if (std::strcmp(Argv[Idx], "--write-showcase") == 0)
        {
            MirrorShowcase = true;
        }
    }

    VerifyPhysicalOracles(RepoRoot);
    VerifyGpuPipelineAndRenderExhibits(RepoRoot, ShaderDir, OutputDir, MirrorShowcase);

    std::cout << "[EnvironmentRealmValidation] ALL CPU ORACLE & VULKAN 1.3 GPU PIPELINE CHECKS PASSED.\n";
    return 0;
}
