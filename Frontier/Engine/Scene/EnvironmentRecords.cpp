// 🧩 Global reusable Engine records: Folders, Sun, Lens Flare, Atmosphere, Clouds, and Shared Wind Forcefield.

#include "Engine/Scene/EnvironmentRecords.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace Frontier
{

namespace
{

constexpr double PiConstant       = 3.14159265358979323846;
constexpr double DegreesToRadians = PiConstant / 180.0;
constexpr double RadiansToDegrees = 180.0 / PiConstant;

double ClampScalar(double Input, double Lower, double Upper) noexcept
{
    return std::max(Lower, std::min(Upper, Input));
}

double SmoothStepScalar(double Edge0, double Edge1, double Input) noexcept
{
    if ( std::abs(Edge1 - Edge0) < 1e-9 )
    {
        return Input >= Edge1 ? 1.0 : 0.0;
    }
    const double Normalized = ClampScalar((Input - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
    return Normalized * Normalized * (3.0 - 2.0 * Normalized);
}

double FractionalPart(double Input) noexcept
{
    return Input - std::floor(Input);
}

double HueChannel(double Hue, double Offset) noexcept
{
    const double Shifted = FractionalPart(Hue + Offset);
    return ClampScalar(std::abs(Shifted * 6.0 - 3.0) - 1.0, 0.0, 1.0);
}

double WrapDegrees360(double Degrees) noexcept
{
    double Wrapped = std::fmod(Degrees, 360.0);
    if (Wrapped < 0.0)
    {
        Wrapped += 360.0;
    }
    return Wrapped;
}

} // namespace

//========================================================================================================================
//                                                    FOLDER RECORD
//========================================================================================================================

FolderRecord::FolderRecord(std::string InitialTitle, std::string InitialTag, uint32_t PackedRgba)
    : RecordEntry(RecordClassification::Folder)
    , Title(std::move(InitialTitle))
    , Tag(std::move(InitialTag))
    , AccentRgba(PackedRgba)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "folder";
    Description.Eyebrow            = "FOLDER / HIERARCHY ENCLOSURE";
    Description.DisplayName        = Title;
    Description.Subtitle           = "Collapsible Outliner folder grouping related scene records with cascade visibility.";
    Description.InstrumentCategory = InspectorInstrumentCategory::FolderObservatory;
    Description.AccentColour       = AccentRgba;
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() { return Tag; };
    RegisterEditorDescription(std::move(Description));
#endif
}

//========================================================================================================================
//                                                     SUN RECORD
//========================================================================================================================

SunRecord::SunRecord()
    : RecordEntry(RecordClassification::Illumination)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "sun";
    Description.Eyebrow            = "ENVIRONMENT  /  02";
    Description.DisplayName        = "Sun";
    Description.Subtitle           = "Directional light / position & colour.";
    Description.InstrumentCategory = InspectorInstrumentCategory::SunObservatory;
    Description.AccentColour       = 0xFF8BC7E8u;   // Warm solar gold in ABGR (#E8C78B)
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%.1f\xC2\xB0 \xC2\xB7 %.1fx", ElevationDegrees, SolarIlluminance);
        return std::string(Buffer);
    };
    RegisterEditorDescription(std::move(Description));
#endif
}

void SunRecord::ResetToDefaults() noexcept
{
    ElevationDegrees     = 38.0;
    AzimuthDegrees       = 145.0;
    SolarIlluminance     = 12.0;
    TemperatureKelvin    = 5778.0;
    AngularRadiusDegrees = 0.27;
    OpticalGlow          = 1.0;
    DayCycleActive       = false;
    TimeLinked           = false;
    ColourSourceIndex    = 0;
    PresetIndex          = 0;
    Enabled              = true;
    DiscEnabled          = true;
}

void SunRecord::ApplyPreset(SolarLightingPreset Preset) noexcept
{
    PresetIndex    = static_cast<int32_t>(Preset);
    TimeLinked     = false;
    DayCycleActive = false;
    switch (Preset)
    {
        case SolarLightingPreset::Noon:
            ElevationDegrees  = 38.0;
            AzimuthDegrees    = 145.0;
            SolarIlluminance  = 12.0;
            TemperatureKelvin = 5778.0;
            OpticalGlow       = 1.0;
            Enabled           = true;
            break;
        case SolarLightingPreset::Golden:
            ElevationDegrees  = 8.5;
            AzimuthDegrees    = -112.0;
            SolarIlluminance  = 7.4;
            TemperatureKelvin = 3650.0;
            OpticalGlow       = 1.65;
            Enabled           = true;
            break;
        case SolarLightingPreset::Dusk:
            ElevationDegrees  = -2.4;
            AzimuthDegrees    = -98.0;
            SolarIlluminance  = 1.8;
            TemperatureKelvin = 2650.0;
            OpticalGlow       = 2.10;
            Enabled           = true;
            break;
        case SolarLightingPreset::Night:
            ElevationDegrees  = -28.0;
            AzimuthDegrees    = -35.0;
            SolarIlluminance  = 0.15;
            TemperatureKelvin = 4200.0;
            OpticalGlow       = 0.35;
            Enabled           = true;
            break;
    }
}

void SunRecord::SynchronizeFromEphemeris() noexcept
{
    const double        UtcHours = SolarTimeHours - UtcOffsetHours;
    const double        Julian   = JulianDate(CalendarYear, static_cast<unsigned>(std::clamp(CalendarMonth, 1, 12)),
                                              static_cast<unsigned>(std::clamp(CalendarDay, 1, 31)), UtcHours);
    const SolarPosition Solved   = SunPosition(Julian, LatitudeDegrees, LongitudeDegrees);
    ElevationDegrees             = ClampScalar(Solved.ElevationRad * RadiansToDegrees, -90.0, 90.0);
    double Azimuth               = Solved.AzimuthRad * RadiansToDegrees;
    if (Azimuth > 180.0)
    {
        Azimuth -= 360.0;
    }
    AzimuthDegrees = ClampScalar(Azimuth, -180.0, 180.0);
}

SpatialVector SunRecord::QuerySunDirection() const noexcept
{
    const double AzimuthRad   = AzimuthDegrees * DegreesToRadians;
    const double ElevationRad = ElevationDegrees * DegreesToRadians;
    return SpatialVector{
        std::cos(ElevationRad) * std::cos(AzimuthRad),
        std::cos(ElevationRad) * std::sin(AzimuthRad),
        std::sin(ElevationRad),
    };
}

SolarDayTrajectory SunRecord::EvaluateDayTrajectory() const noexcept
{
    SolarDayTrajectory Trajectory{};
    const unsigned     ClampedMonth = static_cast<unsigned>(std::clamp(CalendarMonth, 1, 12));
    const unsigned     ClampedDay   = static_cast<unsigned>(std::clamp(CalendarDay, 1, 31));

    for (size_t Index = 0; Index < Trajectory.ElevationDegrees.size(); ++Index)
    {
        const double        LocalHour = static_cast<double>(Index) * 0.25;
        const double        Julian    = JulianDate(CalendarYear, ClampedMonth, ClampedDay, LocalHour - UtcOffsetHours);
        const SolarPosition Position  = SunPosition(Julian, LatitudeDegrees, LongitudeDegrees);
        Trajectory.ElevationDegrees[Index] = Position.ElevationRad * RadiansToDegrees;
    }

    for (size_t Index = 1; Index < Trajectory.ElevationDegrees.size(); ++Index)
    {
        const double PreviousElevation = Trajectory.ElevationDegrees[Index - 1];
        const double CurrentElevation  = Trajectory.ElevationDegrees[Index];
        constexpr double StepHours     = 0.25;

        if (PreviousElevation >= 0.0 && CurrentElevation >= 0.0)
        {
            Trajectory.DaylightHours += StepHours;
        }
        else if ((PreviousElevation < 0.0) != (CurrentElevation < 0.0))
        {
            const double Fraction  = -PreviousElevation / (CurrentElevation - PreviousElevation);
            const double CrossHour = static_cast<double>(Index - 1) * StepHours + Fraction * StepHours;
            if (PreviousElevation < 0.0)
            {
                if (Trajectory.RiseHour < 0.0)
                {
                    Trajectory.RiseHour = CrossHour;
                }
                Trajectory.DaylightHours += (1.0 - Fraction) * StepHours;
            }
            else
            {
                if (Trajectory.SetHour < 0.0)
                {
                    Trajectory.SetHour = CrossHour;
                }
                Trajectory.DaylightHours += Fraction * StepHours;
            }
        }
    }
    return Trajectory;
}

double SunRecord::EvaluateNormalizedPlanck(double WavelengthNanometres, double Kelvin) noexcept
{
    const double ClampedKelvin = ClampScalar(Kelvin, 1000.0, 20000.0);
    auto         RawRadiance   = [ClampedKelvin](double Nanometres) noexcept -> double {
        const double WavelengthMetres = Nanometres * 1e-9;
        const double Exponent         = std::min(80.0, 0.01438776877 / (WavelengthMetres * ClampedKelvin));
        const double Denominator      = std::pow(WavelengthMetres, 5.0) * std::expm1(Exponent);
        return Denominator > 0.0 ? (1.0 / Denominator) : 0.0;
    };

    double Peak = 1e-30;
    for (int32_t Sample = 0; Sample <= 64; ++Sample)
    {
        const double Nanometres = 380.0 + (static_cast<double>(Sample) / 64.0) * 400.0;
        Peak                    = std::max(Peak, RawRadiance(Nanometres));
    }
    return ClampScalar(RawRadiance(WavelengthNanometres) / Peak, 0.0, 1.0);
}

SpectrumRgb SunRecord::EvaluateBlackbodyRgb(double Kelvin) noexcept
{
    const double Scaled = ClampScalar(Kelvin, 1000.0, 40000.0) / 100.0;
    double       Red    = 1.0;
    double       Green  = 1.0;
    double       Blue   = 1.0;

    if (Scaled <= 66.0)
    {
        Red   = 1.0;
        Green = ClampScalar((99.4708025861 * std::log(std::max(1.0, Scaled)) - 161.1195681661) / 255.0, 0.0, 1.0);
        Blue  = Scaled <= 19.0
                    ? 0.0
                    : ClampScalar((138.5177312231 * std::log(Scaled - 10.0) - 305.0447927307) / 255.0, 0.0, 1.0);
    }
    else
    {
        Red   = ClampScalar(329.698727446 * std::pow(Scaled - 60.0, -0.1332047592) / 255.0, 0.0, 1.0);
        Green = ClampScalar(288.1221695283 * std::pow(Scaled - 60.0, -0.0755148492) / 255.0, 0.0, 1.0);
        Blue  = 1.0;
    }
    return SpectrumRgb{Red, Green, Blue};
}

//========================================================================================================================
//                                                  LENS FLARE RECORD
//========================================================================================================================

LensFlareRecord::LensFlareRecord()
    : RecordEntry(RecordClassification::Illumination)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "flare";
    Description.Eyebrow            = "ENVIRONMENT / CAMERA OPTICS";
    Description.DisplayName        = "Lens Flare";
    Description.Subtitle           = "Reflections, diffraction, and light across the sensor.";
    Description.InstrumentCategory = InspectorInstrumentCategory::LensFlareObservatory;
    Description.AccentColour       = 0xFFD2AFC3u;   // Soft violet-silver optics accent (#C3AFD2)
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%d ghosts \xC2\xB7 %d rays",
                      static_cast<int>( std::lround(GhostCount) ), static_cast<int>(QueryActiveRayPairs()));
        return std::string(Buffer);
    };
    RegisterEditorDescription(std::move(Description));
#endif
}

void LensFlareRecord::ApplyPreset(LensFlarePresetCategory Preset) noexcept
{
    PresetIndex = static_cast<int32_t>(Preset);
    switch (Preset)
    {
        case LensFlarePresetCategory::Cinematic:
            AnamorphicEnabled = true;
            WarmStreakEnabled = false;
            StarburstEnabled  = true;
            RayPairs          = 6.0;
            GhostCount        = 6.0;
            GhostBrightness   = 0.30;
            GhostShapeIndex   = 1;
            GhostSpacing      = 1.0;
            HaloBrightness    = 0.20;
            StreakGain        = 0.30;
            break;
        case LensFlarePresetCategory::Clean:
            AnamorphicEnabled = false;
            WarmStreakEnabled = false;
            StarburstEnabled  = true;
            RayPairs          = 6.0;
            GhostCount        = 4.0;
            GhostBrightness   = 0.18;
            GhostShapeIndex   = 0;
            GhostSpacing      = 1.0;
            HaloBrightness    = 0.14;
            StreakGain        = 0.12;
            break;
        case LensFlarePresetCategory::Anamorphic:
            AnamorphicEnabled = true;
            WarmStreakEnabled = true;
            StarburstEnabled  = false;
            RayPairs          = 4.0;
            GhostCount        = 8.0;
            GhostBrightness   = 0.35;
            GhostShapeIndex   = 2;
            GhostSpacing      = 1.25;
            HaloBrightness    = 0.16;
            StreakGain        = 0.85;
            break;
        case LensFlarePresetCategory::Subtle:
            AnamorphicEnabled = false;
            WarmStreakEnabled = false;
            StarburstEnabled  = false;
            RayPairs          = 6.0;
            GhostCount        = 3.0;
            GhostBrightness   = 0.20;
            GhostShapeIndex   = 0;
            GhostSpacing      = 1.0;
            HaloBrightness    = 0.10;
            StreakGain        = 0.0;
            break;
        case LensFlarePresetCategory::Halo:
            AnamorphicEnabled = false;
            WarmStreakEnabled = false;
            StarburstEnabled  = false;
            RayPairs          = 6.0;
            GhostCount        = 0.0;
            GhostBrightness   = 0.0;
            GhostShapeIndex   = 0;
            GhostSpacing      = 1.0;
            HaloBrightness    = 1.00;
            StreakGain        = 0.0;
            break;
        case LensFlarePresetCategory::CustomLayers:
            break;
    }
}

int32_t LensFlareRecord::QueryGhostSides() const noexcept
{
    if (GhostShapeIndex == 1)
    {
        return 6;
    }
    if (GhostShapeIndex == 2)
    {
        return 8;
    }
    return 0;
}

int32_t LensFlareRecord::QueryActiveRayPairs() const noexcept
{
    const double RawPairs = (PresetIndex == static_cast<int32_t>(LensFlarePresetCategory::CustomLayers))
                                ? RayPairs
                                : ApertureBlades;
    return static_cast<int32_t>(std::floor(ClampScalar(RawPairs, 2.0, 12.0) + 0.5));
}

SpectrumRgb LensFlareRecord::EvaluateOpticalSample(double SampleU, double SampleV, double AspectRatio) const noexcept
{
    return EvaluateOpticalSampleAtSource(SampleU, SampleV, PreviewSourceU, PreviewSourceV, AspectRatio);
}

// 📝 Direct C++ port of NativeLensFlareKernel.js LFRender: evaluates cool anamorphic streak, rotated warm streak,
//    diffraction starburst, polygonal chromatic ghost chain, and spectral annular halo in linear HDR RGB.
SpectrumRgb LensFlareRecord::EvaluateOpticalSampleAtSource(double SampleU, double SampleV, double SourceUCoord,
                                                           double SourceVCoord, double AspectRatio) const noexcept
{
    if (Intensity <= 0.0)
    {
        return SpectrumRgb{};
    }

    const double EdgeFade = SmoothStepScalar(-0.15, 0.05, SourceUCoord) *
                            (1.0 - SmoothStepScalar(0.95, 1.15, SourceUCoord)) *
                            SmoothStepScalar(-0.15, 0.05, SourceVCoord) *
                            (1.0 - SmoothStepScalar(0.95, 1.15, SourceVCoord));

    const double SensorX      = (SampleU - 0.5) * AspectRatio;
    const double SensorY      = SampleV - 0.5;
    const double SourceX      = (SourceUCoord - 0.5) * AspectRatio;
    const double SourceY      = SourceVCoord - 0.5;
    const double DeltaX       = SensorX - SourceX;
    const double DeltaY       = SensorY - SourceY;
    const double ClampedSpread = ClampScalar(Spread, 0.25, 2.0);
    const double Distance     = std::hypot(DeltaX, DeltaY);
    const double RotationRad  = RotationDegrees * DegreesToRadians;
    const double RotatedX     = DeltaX * std::cos(RotationRad) + DeltaY * std::sin(RotationRad);
    const double RotatedY     = -DeltaX * std::sin(RotationRad) + DeltaY * std::cos(RotationRad);
    const double Chromatic    = ClampScalar(ChromaticSeparation, 0.0, 1.0);

    const double CoolStreak = std::exp(-std::abs(DeltaY) * 95.0 / ClampedSpread) *
                              std::exp(-std::abs(DeltaX) * 2.2 / ClampedSpread) * 0.55 *
                              (AnamorphicEnabled ? 1.0 : 0.0) * StreakGain;
    const double WarmStreak = std::exp(-std::abs(RotatedY) * 160.0 / ClampedSpread) *
                              std::exp(-std::abs(RotatedX) * 3.0 / ClampedSpread) * 0.65 *
                              (WarmStreakEnabled ? 1.0 : 0.0) * StreakGain;

    const double Angle     = Distance > 1e-8 ? std::atan2(RotatedY, RotatedX) : 0.0;
    const double PairCount = static_cast<double>(QueryActiveRayPairs());
    const double Burst     = std::pow(std::abs(std::sin(Angle * PairCount)), 32.0) *
                             std::exp(-Distance * 5.0 / ClampedSpread) * 0.6 * (StarburstEnabled ? 1.0 : 0.0);
    const double GlareCore = (AnamorphicEnabled || WarmStreakEnabled || StarburstEnabled)
                                 ? (std::exp(-Distance * 80.0 / ClampedSpread) * 0.8 +
                                    std::exp(-Distance * 9.0 / ClampedSpread) * 0.035)
                                 : 0.0;

    std::array<double, 3> Channels{
        CoolStreak * (1.0 - 0.55 * Chromatic) + WarmStreak + Burst + GlareCore,
        CoolStreak * (1.0 - 0.35 * Chromatic) + WarmStreak * 0.8 + Burst * 0.95 + GlareCore * 0.9,
        CoolStreak + WarmStreak * 0.55 + Burst * 0.82 + GlareCore * 0.75,
    };

    const int32_t Ghosts = static_cast<int32_t>(std::lround(ClampScalar(GhostCount, 0.0, 24.0)));
    const int32_t Sides  = QueryGhostSides();
    const std::array<double, 3> HueOffsets{0.0, 0.6666667, 0.3333333};

    for (int32_t Index = 0; Index < Ghosts; ++Index)
    {
        const double AxisScale =
            1.0 - (0.6 + (static_cast<double>(Index + 1) / static_cast<double>(Ghosts)) * (1.7 + GhostSpacing));
        const double GhostX    = SensorX - SourceX * AxisScale;
        const double GhostY    = SensorY - SourceY * AxisScale;
        double       GhostDist = std::hypot(GhostX, GhostY);
        if (Sides >= 3)
        {
            const double SectorStep = PiConstant * 2.0 / static_cast<double>(Sides);
            const double PolarAngle = GhostDist > 1e-8 ? std::atan2(GhostY, GhostX) : 0.0;
            const double Folded     = PolarAngle - SectorStep * std::floor((PolarAngle + SectorStep * 0.5) / SectorStep);
            GhostDist *= std::cos(Folded) / std::cos(PiConstant / static_cast<double>(Sides));
        }
        const double GhostRadius = (0.025 + 0.06 * FractionalPart(static_cast<double>(Index) * 0.618 + 0.31)) * ClampedSpread;
        const double SpectralHue = FractionalPart(static_cast<double>(Index) * 0.23 + 0.5);
        for (size_t Channel = 0; Channel < 3; ++Channel)
        {
            const double ShiftedRadius = GhostRadius * (1.0 + (static_cast<double>(Channel) - 1.0) * Chromatic * 0.035);
            const double Envelope      = (SmoothStepScalar(ShiftedRadius, ShiftedRadius * 0.35, GhostDist) * 0.6 +
                                          std::exp(-std::abs(GhostDist - ShiftedRadius) * 350.0)) *
                                         0.14 * GhostBrightness;
            Channels[Channel] += Envelope * (1.0 + (HueChannel(SpectralHue, HueOffsets[Channel]) - 1.0) * Chromatic);
        }
    }

    const double HaloDeltaX = SensorX - SourceX * 0.25;
    const double HaloDeltaY = SensorY - SourceY * 0.25;
    const double HaloDist   = std::hypot(HaloDeltaX, HaloDeltaY);
    const double RadialGap  = std::abs(HaloDist - HaloRadius);
    const double HaloGain   = SmoothStepScalar(ClampScalar(HaloWidth, 0.005, 0.2), 0.0, RadialGap) * 0.09 * HaloBrightness;
    const double HaloHue    = FractionalPart((HaloDist > 1e-8 ? std::atan2(HaloDeltaY, HaloDeltaX) : 0.0) / (PiConstant * 2.0) +
                                             RadialGap * 8.0);
    for (size_t Channel = 0; Channel < 3; ++Channel)
    {
        Channels[Channel] += HaloGain * (1.0 + (HueChannel(HaloHue, HueOffsets[Channel]) - 1.0) * Chromatic * 0.8);
    }

    const double Scale = Intensity * EdgeFade;
    return SpectrumRgb{Channels[0] * Scale, Channels[1] * Scale, Channels[2] * Scale};
}

//========================================================================================================================
//                                                  ATMOSPHERE RECORD
//========================================================================================================================

AtmosphereRecord::AtmosphereRecord()
    : RecordEntry(RecordClassification::Environment)
{
    SynchronizeParameters();
    RebakeLookupTable();

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "atmosphere";
    Description.Eyebrow            = "ENVIRONMENT  /  03";
    Description.DisplayName        = "Atmosphere";
    Description.Subtitle           = "Scattering, density & the visible sky.";
    Description.InstrumentCategory = InspectorInstrumentCategory::AtmosphereObservatory;
    Description.AccentColour       = 0xFFBDB3B8u;   // Atmospheric sage-cyan (#B8D3BD -> ABGR)
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "R %.1fx \xC2\xB7 M %.1fx", RayleighMultiplier, MieMultiplier);
        return std::string(Buffer);
    };
    RegisterEditorDescription(std::move(Description));
#endif
}

void AtmosphereRecord::SynchronizeParameters() noexcept
{
    Parameters.RayleighScattering    = SpectrumRgb{5.802e-3 * RayleighMultiplier, 13.558e-3 * RayleighMultiplier,
                                                   33.1e-3 * RayleighMultiplier};
    Parameters.RayleighScaleHeightKm = ClampScalar(RayleighScaleMetres / 1000.0, 0.5, 24.0);
    Parameters.MieScattering         = 3.996e-3 * MieMultiplier;
    Parameters.MieAbsorption         = 4.40e-3 * MieMultiplier;
    Parameters.MieScaleHeightKm      = ClampScalar(MieScaleMetres / 1000.0, 0.2, 12.0);
    Parameters.MiePhaseG             = ClampScalar(MieAnisotropyG, -0.95, 0.95);
    Parameters.OzoneAbsorption       = SpectrumRgb{0.65e-3 * OzoneMultiplier, 1.881e-3 * OzoneMultiplier,
                                                   0.085e-3 * OzoneMultiplier};
    Parameters.GroundAlbedo          = (GroundAlbedoRed + GroundAlbedoGreen + GroundAlbedoBlue) / 3.0;
}

void AtmosphereRecord::RebakeLookupTable()
{
    SynchronizeParameters();
    const uint32_t Steps = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(LookupSamples)), 8, 64));
    (void)LookupTable.Bake(Parameters, 128, 32, Steps);
    ++LookupRevision;
}

double AtmosphereRecord::QueryRelativeRayleighDensity(double AltitudeMetres) const noexcept
{
    return std::exp(-std::max(0.0, AltitudeMetres) / std::max(100.0, RayleighScaleMetres));
}

double AtmosphereRecord::QueryRelativeMieDensity(double AltitudeMetres) const noexcept
{
    return std::exp(-std::max(0.0, AltitudeMetres) / std::max(100.0, MieScaleMetres));
}

SpectrumRgb AtmosphereRecord::EvaluateSolarTransmission(double SunElevationDegrees) const noexcept
{
    const double CosZenith  = std::sin(SunElevationDegrees * DegreesToRadians);
    const double ObserverKm = Parameters.GroundRadiusKm + 0.1;
    if (UseBakedLookup && LookupTable.Baked())
    {
        return LookupTable.Sample(Parameters, ObserverKm, CosZenith);
    }
    const uint32_t Steps = static_cast<uint32_t>(std::clamp(static_cast<int>(std::lround(LookupSamples)), 8, 64));
    return TransmittanceToTop(Parameters, ObserverKm, CosZenith, Steps);
}

//========================================================================================================================
//                                                    CLOUD RECORD
//========================================================================================================================

CloudRecord::CloudRecord()
    : RecordEntry(RecordClassification::Environment)
{
    SynchronizeParameters();

#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "clouds";
    Description.Eyebrow            = "ENVIRONMENT  /  01";
    Description.DisplayName        = "Clouds";
    Description.Subtitle           = "Density field / shape & illumination.";
    Description.InstrumentCategory = InspectorInstrumentCategory::CloudObservatory;
    Description.AccentColour       = 0xFFA8E5D4u;   // Cloud lime-ivory (#D4E5A8)
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%d%% \xC2\xB7 %.1f km",
                      static_cast<int>(std::lround(Coverage * 100.0)), FloorAltitudeMetres / 1000.0);
        return std::string(Buffer);
    };
    RegisterEditorDescription(std::move(Description));
#endif
}

void CloudRecord::SynchronizeParameters(double WindSpeedMps, double WindBearingDegrees) noexcept
{
    FloorAltitudeMetres        = std::min(FloorAltitudeMetres, CeilingLimitMetres);
    Parameters.BaseHeightM     = FloorAltitudeMetres;
    Parameters.TopHeightM      = QueryEffectiveTopMetres();
    Parameters.Coverage        = ClampScalar(Coverage, 0.0, 1.0);
    Parameters.DensityScale    = ClampScalar(DensityMultiplier, 0.0, 4.0);
    Parameters.PhaseG          = ClampScalar(ForwardAnisotropyG, -0.8, 0.8);
    Parameters.ExtinctionPerM  = 0.012 * (0.6 + LightAbsorption * 0.8);
    const double BearingRad    = WindBearingDegrees * DegreesToRadians;
    const double EffectiveWind = FollowSharedWind ? WindSpeedMps : 0.0;
    Parameters.WindXMps        = std::sin(BearingRad) * EffectiveWind;
    Parameters.WindYMps        = std::cos(BearingRad) * EffectiveWind;
}

double CloudRecord::QueryEffectiveTopMetres() const noexcept
{
    return std::max(FloorAltitudeMetres, std::min(CeilingLimitMetres, FloorAltitudeMetres + ThicknessMetres));
}

double CloudRecord::QueryRenderedDepthMetres() const noexcept
{
    return std::max(0.0, QueryEffectiveTopMetres() - FloorAltitudeMetres);
}

bool CloudRecord::QueryCeilingClipped() const noexcept
{
    return FloorAltitudeMetres + ThicknessMetres > CeilingLimitMetres;
}

uint32_t CloudRecord::QueryCompleteLayerRaySamples() const noexcept
{
    const double DepthScale = std::max(1.0, QueryRenderedDepthMetres() / 1200.0);
    const double RawCount   = std::ceil(SkyReferenceSamples * DepthScale);
    return static_cast<uint32_t>(std::clamp(static_cast<int>(RawCount), 8, 96));
}

const char* CloudRecord::QueryFormationName() const noexcept
{
    static constexpr std::array<const char*, 6> FormationNames{
        "Stratus", "Stratocumulus", "Cumulus", "Cumulonimbus", "Altostratus", "Cirrus"};
    const size_t Index = static_cast<size_t>(std::clamp(FormationIndex, 0, 5));
    return FormationNames[Index];
}

double CloudRecord::EvaluateFormationProfile(double NormalizedHeight, int32_t Formation, double Anvil) noexcept
{
    const double Height = ClampScalar(NormalizedHeight, 0.0, 1.0);
    const double Lower  = SmoothStepScalar(0.0, 0.14, Height);
    double       Upper  = 1.0;
    switch (std::clamp(Formation, 0, 5))
    {
        case 0:   // Stratus: low flat sheet
            Upper = 1.0 - SmoothStepScalar(0.32, 0.72, Height);
            break;
        case 1:   // Stratocumulus: rounded low deck
            Upper = 1.0 - SmoothStepScalar(0.45, 0.88, Height);
            break;
        case 2:   // Cumulus: bulbous lower-mid body
            Upper = 1.0 - SmoothStepScalar(0.55, 0.96, Height);
            break;
        case 3:   // Cumulonimbus: deep tower + anvil spread near ceiling
        {
            const double Tower = 1.0 - SmoothStepScalar(0.78, 1.0, Height);
            const double Cap   = SmoothStepScalar(0.62, 0.88, Height) * (1.0 - SmoothStepScalar(0.90, 1.0, Height));
            return ClampScalar(Lower * (Tower + Cap * ClampScalar(Anvil, 0.0, 1.0) * 0.55), 0.0, 1.0);
        }
        case 4:   // Altostratus: mid-altitude Gaussian band
            return ClampScalar(std::exp(-std::pow((Height - 0.52) / 0.28, 2.0)), 0.0, 1.0);
        case 5:   // Cirrus: high wispy upper filament
            return ClampScalar(SmoothStepScalar(0.42, 0.78, Height) * (1.0 - SmoothStepScalar(0.86, 1.0, Height)), 0.0, 1.0);
    }
    const double AnvilBoost = SmoothStepScalar(0.65, 0.90, Height) * (1.0 - SmoothStepScalar(0.92, 1.0, Height)) *
                              ClampScalar(Anvil, 0.0, 1.0) * 0.28;
    return ClampScalar(Lower * Upper + AnvilBoost, 0.0, 1.0);
}

double CloudRecord::SampleTopDownDensity(double NormalizedU, double NormalizedV) const noexcept
{
    CloudParameters Local = Parameters;
    Local.BaseHeightM     = FloorAltitudeMetres;
    Local.TopHeightM      = std::max(FloorAltitudeMetres + 200.0, QueryEffectiveTopMetres());
    Local.Coverage        = ClampScalar(Coverage, 0.0, 1.0);
    Local.DensityScale    = ClampScalar(DensityMultiplier, 0.0, 4.0);
    const double SpanMetres = 9000.0 / std::max(0.2, FeatureScale);
    const double SampleX    = (NormalizedU - 0.5) * SpanMetres;
    const double SampleY    = (NormalizedV - 0.5) * SpanMetres;
    const double MidZ       = 0.5 * (Local.BaseHeightM + Local.TopHeightM);
    const double RawDensity = CloudDensity(Local, SpatialVector{SampleX, SampleY, MidZ});
    const double Eroded     = std::max(0.0, RawDensity - DetailErosion * 0.18 * SmoothStepScalar(0.0, 0.5, RawDensity));
    return ClampScalar(Eroded, 0.0, 1.0);
}

//========================================================================================================================
//                                                     WIND RECORD
//========================================================================================================================

WindRecord::WindRecord()
    : RecordEntry(RecordClassification::Environment)
{
#if defined(FRONTIER_EDITOR_PROFILE) || defined(FRONTIER_DEVELOPMENT_PROFILE)
    RecordEditorDescription Description;
    Description.Identifier         = "wind";
    Description.Eyebrow            = "FORCEFIELD / SHARED WEATHER";
    Description.DisplayName        = "Wind";
    Description.Subtitle           = "A direction, a pulse, a changing column of air.";
    Description.InstrumentCategory = InspectorInstrumentCategory::WindObservatory;
    Description.AccentColour       = 0xFFACC8C7u;   // Sage-eucalyptus wind instrument (#C7D8AC)
    Description.EnabledTarget      = &Enabled;
    Description.QueryLiveBadge     = [this]() {
        char Buffer[48] = {};
        std::snprintf(Buffer, sizeof(Buffer), "%.1f m/s \xC2\xB7 FROM %.0f\xC2\xB0",
                      ReferenceSpeedMps, QueryFromBearingDegrees());
        return std::string(Buffer);
    };
    RegisterEditorDescription(std::move(Description));
#endif
}

double WindRecord::QueryFromBearingDegrees() const noexcept
{
    return WrapDegrees360(TravelBearingDegrees + 180.0);
}

void WindRecord::ConfigureFromBearingDegrees(double FromDegrees) noexcept
{
    TravelBearingDegrees = WrapDegrees360(FromDegrees + 180.0);
}

double WindRecord::EvaluateGustFactor(double Gust, double PhaseSeconds) noexcept
{
    const double ClampedGust = ClampScalar(Gust, 0.0, 1.0);
    const double Wave = 0.62 * std::sin(PhaseSeconds * 0.75) + 0.38 * std::sin(PhaseSeconds * 1.93 + 1.1);
    return std::max(0.0, 1.0 + ClampedGust * Wave);
}

SpatialVector WindRecord::EvaluateSteadyVelocityAtAltitude(double AltitudeMetres) const noexcept
{
    const double ClampedKm  = std::max(0.0, AltitudeMetres) / 1000.0;
    const double Speed      = ReferenceSpeedMps * (1.0 + ShearPerKilometre * ClampedKm);
    const double BearingRad = (TravelBearingDegrees + VeerDegreesPerKilometre * ClampedKm) * DegreesToRadians;
    return SpatialVector{std::sin(BearingRad) * Speed, std::cos(BearingRad) * Speed, 0.0};
}

} // namespace Frontier
