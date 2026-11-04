/*==========================================================================================================================================
                                                   FORCE FIELD DEFINITION
==========================================================================================================================================*/
// 🧩 Analytic Ekman wind + gust fast path, divergence-free curl eddies and mesoscale drift, Burgers-Rott curved-spine tornado
//    column + cyclostrophic Δp condensation funnel, haboob gust fronts, microbursts, authored force sources and
//    multi-species particle drag.

#include "Engine/PhysicalDynamics/ForceField.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{

constexpr double π      = 3.14159265358979323846;
constexpr double Twoπ   = 6.28318530717958647692;
constexpr double ρAir0  = 1.225; // [kg/m³] sea-level standard air density
constexpr double GravG  = 9.80665;

inline double Smooth01(double Value) noexcept
{
    const double T = std::clamp(Value, 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

uint32_t HashMix(uint32_t Word) noexcept
{
    Word ^= Word >> 16u;
    Word *= 0x7FEB352Du;
    Word ^= Word >> 15u;
    Word *= 0x846CA68Bu;
    Word ^= Word >> 16u;
    return Word;
}

double HashLattice(int32_t X, int32_t Y, int32_t Z, uint32_t Seed) noexcept
{
    const uint32_t Wx = static_cast<uint32_t>(X) * 1597334677u + Seed;
    const uint32_t Wy = static_cast<uint32_t>(Y) * 3812015801u + (Seed ^ 0x9E3779B9u);
    const uint32_t Wz = static_cast<uint32_t>(Z) * 2654435761u;
    return static_cast<double>(HashMix(Wx ^ Wy ^ Wz)) * (1.0 / 4294967296.0);
}

// 📝 Level-of-detail filter: a term whose wavelength is under two cells fades out (by six cells it is whole), as a coarse
//    cascade would average it away; without it, gust bands and eddies alias into noise at 100 km. CellSpan 0 keeps all.
double ResolveResolvable(double WavelengthMeters, double CellSpanMeters) noexcept
{
    return Smooth01((WavelengthMeters / std::max(1.0e-6, CellSpanMeters) - 2.0) / 4.0);
}

double ComputeActivationEnvelope(const ActiveForceSource& Source) noexcept
{
    double Env = 1.0;
    if (Source.Timing.RampInSeconds > 1.0e-4)
    {
        Env = Smooth01(Source.AgeSeconds / Source.Timing.RampInSeconds);
    }

    if (Source.ReleaseRequested)
    {
        const double RelElapsed = std::max(0.0, Source.AgeSeconds - Source.ReleaseStartAgeS);
        const double OutScale =
            (Source.Timing.RampOutSeconds > 1.0e-4) ? (1.0 - Smooth01(RelElapsed / Source.Timing.RampOutSeconds)) : 0.0;
        Env *= OutScale;
    }
    else if (Source.Timing.HoldSeconds > 0.0)
    {
        const double DecayStart = Source.Timing.RampInSeconds + Source.Timing.HoldSeconds;
        if (Source.AgeSeconds > DecayStart)
        {
            const double Over = Source.AgeSeconds - DecayStart;
            const double OutScale =
                (Source.Timing.RampOutSeconds > 1.0e-4) ? (1.0 - Smooth01(Over / Source.Timing.RampOutSeconds)) : 0.0;
            Env *= OutScale;
        }
    }
    return std::clamp(Env, 0.0, 1.0);
}

struct SpeciesPhysicalProperties
{
    double DensityKgPerM3       = 1000.0;
    double DragCoefficient      = 0.47;
    double DefaultDiameterM     = 0.0024;
    double SaltationThresholdMs = 999.0; // Threshold air speed near ground to loft resting grains
    double BounceRestitution    = 0.0;
};

SpeciesPhysicalProperties QuerySpeciesProperties(AerodynamicParticleSpecies Species) noexcept
{
    switch (Species)
    {
        case AerodynamicParticleSpecies::RainDrop:
            return {1000.0, 0.48, 0.0024, 999.0, 0.0};
        case AerodynamicParticleSpecies::HailStone:
            return {890.0, 0.44, 0.0120, 999.0, 0.28};
        case AerodynamicParticleSpecies::SnowFlake:
            return {115.0, 1.35, 0.0040, 999.0, 0.0};
        case AerodynamicParticleSpecies::FineDust:
            return {2550.0, 1.10, 0.00012, 6.5, 0.15};
        case AerodynamicParticleSpecies::QuartzSand:
            return {2650.0, 0.65, 0.00032, 8.2, 0.42};
        case AerodynamicParticleSpecies::LoftedDebris:
            return {620.0, 0.82, 0.0350, 22.0, 0.25};
    }
    return {};
}

} // namespace

BeaufortWindClassification ClassifyBeaufortWind(double SpeedMetersPerSecond) noexcept
{
    const double Speed = std::max(0.0, SpeedMetersPerSecond);
    if (Speed < 0.5) return {0u, "Calm"};
    if (Speed < 1.6) return {1u, "Light Air"};
    if (Speed < 3.4) return {2u, "Light Breeze"};
    if (Speed < 5.5) return {3u, "Gentle Breeze"};
    if (Speed < 8.0) return {4u, "Moderate Breeze"};
    if (Speed < 10.8) return {5u, "Fresh Breeze"};
    if (Speed < 13.9) return {6u, "Strong Breeze"};
    if (Speed < 17.2) return {7u, "Near Gale"};
    if (Speed < 20.8) return {8u, "Gale"};
    if (Speed < 24.5) return {9u, "Strong Gale"};
    if (Speed < 28.5) return {10u, "Storm"};
    if (Speed < 32.7) return {11u, "Violent Storm"};
    return {12u, "Hurricane Force"};
}

ForceSourceHandle ForceFieldSpace::TriggerTornado(const TornadoColumnParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Category               = ForceSourceCategory::TornadoColumn;
    Entry.Timing             = Timing;
    Entry.Column             = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

ForceSourceHandle ForceFieldSpace::TriggerDustDevil(SpatialVector GroundAnchorMeters,
                                                    double        HeightMeters,
                                                    double        PeakSwirlMps,
                                                    double        HoldSeconds)
{
    TornadoColumnParameters Devil{};
    Devil.GroundAnchorMeters      = GroundAnchorMeters;
    Devil.TranslationVelocityMps  = {2.2, 0.0, 0.8};
    Devil.SpineLeanXMeters        = 9.0;
    Devil.SpineLeanZMeters        = -5.0;
    Devil.ColumnHeightMeters      = std::max(25.0, HeightMeters);
    Devil.GroundCoreRadiusMeters  = 4.5;
    Devil.CollarCoreRadiusMeters  = 14.0;
    Devil.TangentialPeakSpeedMps  = PeakSwirlMps;
    Devil.RadialInflowSpeedMps    = 0.38 * PeakSwirlMps;
    Devil.AxialUpdraftSpeedMps    = 0.52 * PeakSwirlMps;
    Devil.CondensationStrength    = 0.0; // Dry thermal vortex: visible via lofted dust/sand rather than cloud droplets
    Devil.DebrisSkirtHeightMeters = std::max(25.0, HeightMeters * 0.78);
    Devil.DebrisExtinctionPerM    = 0.055;
    Devil.DebrisAlbedoRgb         = {0.72, 0.58, 0.40};
    return TriggerTornado(Devil, {0.8, HoldSeconds, 1.5});
}

ForceSourceHandle ForceFieldSpace::TriggerGustFront(const GustFrontParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Category               = ForceSourceCategory::GustFront;
    Entry.Timing             = Timing;
    Entry.GustFront          = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

ForceSourceHandle ForceFieldSpace::TriggerMicroburst(const MicroburstParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Category               = ForceSourceCategory::Microburst;
    Entry.Timing             = Timing;
    Entry.Microburst         = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

ForceSourceHandle ForceFieldSpace::TriggerGustPuff(const GustPuffParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Category               = ForceSourceCategory::GustPuff;
    Entry.Timing             = Timing;
    Entry.GustPuff           = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

ForceSourceHandle ForceFieldSpace::TriggerAerodynamicWake(const AerodynamicWakeParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Category               = ForceSourceCategory::VehicleWake;
    Entry.Timing             = Timing;
    Entry.Wake               = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

ForceSourceHandle ForceFieldSpace::TriggerThermalUpdraft(const ThermalUpdraftParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Category               = ForceSourceCategory::ThermalUpdraft;
    Entry.Timing             = Timing;
    Entry.Thermal            = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

ForceSourceHandle ForceFieldSpace::TriggerDirectionalJet(const DirectionalJetParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Category               = ForceSourceCategory::JetCorridor;
    Entry.Timing             = Timing;
    Entry.Jet                = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

ForceSourceHandle ForceFieldSpace::TriggerSphericalImpulse(const SphericalImpulseParameters& Parameters, TriggerEnvelopeTiming Timing)
{
    ActiveForceSource Entry{};
    Entry.Handle             = NextHandle_++;
    Entry.Category               = ForceSourceCategory::BlastWave;
    Entry.Timing             = Timing;
    Entry.Impulse            = Parameters;
    Entry.ActivationEnvelope = (Timing.RampInSeconds <= 1.0e-4) ? 1.0 : 0.0;
    Sources_.push_back(Entry);
    return Entry.Handle;
}

bool ForceFieldSpace::ReleaseTrigger(ForceSourceHandle Handle) noexcept
{
    for (ActiveForceSource& Source : Sources_)
    {
        if (Source.Handle == Handle && !Source.ReleaseRequested)
        {
            Source.ReleaseRequested = true;
            Source.ReleaseStartAgeS = Source.AgeSeconds;
            return true;
        }
    }
    return false;
}

void ForceFieldSpace::Step(double DeltaSeconds) noexcept
{
    if (!Enabled_) return;
    const double Dt = std::max(0.0, DeltaSeconds);
    ElapsedSeconds_ += Dt;

    for (size_t Index = 0u; Index < Sources_.size();)
    {
        ActiveForceSource& Source = Sources_[Index];
        Source.AgeSeconds += Dt;
        Source.ActivationEnvelope = ComputeActivationEnvelope(Source);

        if (Source.Category == ForceSourceCategory::TornadoColumn)
        {
            Source.Column.GroundAnchorMeters.X += Source.Column.TranslationVelocityMps.X * Dt;
            Source.Column.GroundAnchorMeters.Y += Source.Column.TranslationVelocityMps.Y * Dt;
            Source.Column.GroundAnchorMeters.Z += Source.Column.TranslationVelocityMps.Z * Dt;
            const double CoreRef = std::max(2.0, Source.Column.GroundCoreRadiusMeters);
            const double ωCore   = (Source.Column.TangentialPeakSpeedMps * Source.ActivationEnvelope) / CoreRef;
            Source.SwirlPhaseRadians += ωCore * Dt;
        }
        else if (Source.Category == ForceSourceCategory::GustPuff)
        {
            // Localized wind packets automatically drift downstream with the global Ekman wind at their altitude
            const double AltKm     = std::max(0.0, Source.GustPuff.CenterMeters.Y - GroundPlaneYMeters_) * 1.0e-3;
            const double Bearing   = Wind_.BearingRadians + Wind_.VeerRadiansPerKilometer * AltKm;
            const double ShearGain = std::max(0.15, 1.0 + Wind_.ShearPerKilometer * std::min(AltKm, 12.0));
            const double DriftSpd  = Wind_.SpeedMetersPerSecond * ShearGain * Source.GustPuff.GlobalWindDriftCoupling;
            Source.GustPuff.CenterMeters.X += std::cos(Bearing) * DriftSpd * Dt;
            Source.GustPuff.CenterMeters.Z += std::sin(Bearing) * DriftSpd * Dt;
        }
        else if (Source.Category == ForceSourceCategory::VehicleWake)
        {
            Source.Wake.BodyOriginMeters.X += Source.Wake.BodyVelocityMps.X * Dt;
            Source.Wake.BodyOriginMeters.Y += Source.Wake.BodyVelocityMps.Y * Dt;
            Source.Wake.BodyOriginMeters.Z += Source.Wake.BodyVelocityMps.Z * Dt;
        }

        const bool ExpiredFromRelease =
            Source.ReleaseRequested &&
            (Source.AgeSeconds >= Source.ReleaseStartAgeS + std::max(0.0, Source.Timing.RampOutSeconds)) &&
            Source.ActivationEnvelope <= 1.0e-4;
        const bool ExpiredFromHold =
            (Source.Timing.HoldSeconds > 0.0) &&
            (Source.AgeSeconds >= Source.Timing.RampInSeconds + Source.Timing.HoldSeconds + Source.Timing.RampOutSeconds) &&
            Source.ActivationEnvelope <= 1.0e-4;

        if (ExpiredFromRelease || ExpiredFromHold)
        {
            Sources_.erase(Sources_.begin() + static_cast<std::ptrdiff_t>(Index));
        }
        else
        {
            ++Index;
        }
    }
    AuthoredSeconds_ += Dt;
    PrepareAuthoredSources();
}

void ForceFieldSpace::SetElapsedSeconds(double Seconds) noexcept
{
    ElapsedSeconds_ = Seconds;
}

void ForceFieldSpace::SetWindProfile(const PlanetaryWindProfile& Profile) noexcept
{
    Wind_ = Profile;
    PrepareAuthoredSources();
}

void ForceFieldSpace::SetGroundPlaneYMeters(double GroundYMeters) noexcept
{
    GroundPlaneYMeters_ = GroundYMeters;
    PrepareAuthoredSources();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    AUTHORED SOURCES
//------------------------------------------------------------------------------------------------------------------------

void ForceFieldSpace::ConfigureAuthoredSources(std::span<const ForceSourceConfiguration> Sources, double TimelineSeconds)
{
    AuthoredConfigurations_.assign(Sources.begin(), Sources.end());
    AuthoredSeconds_ = std::isfinite(TimelineSeconds) ? TimelineSeconds : 0.0;
    PrepareAuthoredSources();
}

// 📝 The wind as the catalogue reads it (compass bearing of travel, speed, veer, shear, ground): the profile's own terms.
ForceWindReading ForceFieldSpace::QueryAuthoredWind() const noexcept
{
    ForceWindReading Reading;
    Reading.Enabled                 = Wind_.SpeedMetersPerSecond > 0.0;
    Reading.BearingDegrees          = Wind_.BearingRadians * 180.0 / π + 90.0;
    Reading.SpeedMetresPerSecond    = Wind_.SpeedMetersPerSecond;
    Reading.VeerDegreesPerKilometre = Wind_.VeerRadiansPerKilometer * 180.0 / π;
    Reading.ShearPerKilometre       = Wind_.ShearPerKilometer;
    Reading.GroundHeightMetres      = GroundPlaneYMeters_;
    return Reading;
}

void ForceFieldSpace::PrepareAuthoredSources() noexcept
{
    Authored_.clear();
    if (AuthoredConfigurations_.empty()) return;
    const ForceWindReading Wind = QueryAuthoredWind();
    Authored_.reserve(AuthoredConfigurations_.size());
    for (const ForceSourceConfiguration& Source : AuthoredConfigurations_)
        Authored_.push_back(PrepareForceSource(Source, Wind, AuthoredSeconds_));
}

SpatialVector ForceFieldSpace::EvaluateContribution(ForceContribution Contribution,
                                                    SpatialVector     WorldPointMeters,
                                                    double            CellSpanMeters,
                                                    bool              IncludeCurlSwirl,
                                                    int32_t           Solo) const noexcept
{
    if (!Enabled_) return {};
    SpatialVector Out{};
    if (Contribution == ForceContribution::Flow)
    {
        if (Solo == kSoloEvery) return EvaluateStepSample(WorldPointMeters, IncludeCurlSwirl, CellSpanMeters).AirVelocityMps;
        if (Solo == kSoloWind) return EvaluateWindFlow(WorldPointMeters, IncludeCurlSwirl, CellSpanMeters);
    }
    if (Solo == kSoloWind) return Out;
    const ForceWindReading Wind = QueryAuthoredWind();
    for (size_t Index = 0; Index < Authored_.size(); ++Index)
    {
        if (Authored_[Index].Contribution != Contribution) continue;
        if (Solo >= 0 && static_cast<size_t>(Solo) != Index) continue;
        (void)EvaluatePreparedForceSource(Authored_[Index], Wind, WorldPointMeters, Out);
    }
    return Out;
}

SpatialVector ForceFieldSpace::SampleStep(SpatialVector WorldPointMeters) const noexcept
{
    return EvaluateStepSample(WorldPointMeters, false).AirVelocityMps;
}

// 📝 The curl of a 2D gradient-noise stream function ψ(u, v): (∂ψ/∂v, −∂ψ/∂u) is divergence-free in the horizontal and reads
//    as round eddies; a fifth of a second noise drives a small vertical part. The pattern drifts downwind at 0.35 × the wind.
SpatialVector ForceFieldSpace::SampleSwirl(SpatialVector WorldPointMeters, double CellSpanMeters) const noexcept
{
    if (!Enabled_) return {};
    if (Wind_.SwirlStrength <= 1.0e-5 || Wind_.SpeedMetersPerSecond <= 1.0e-5)
    {
        return {};
    }
    const double Visible = ResolveResolvable(Wind_.SwirlScaleMeters * 2.0, CellSpanMeters);
    if (Visible <= 0.0) return {};
    const double Scale = std::max(4.0, Wind_.SwirlScaleMeters);
    const double Drift = 0.35 * Wind_.SpeedMetersPerSecond * ElapsedSeconds_;
    const double U     = (WorldPointMeters.X - std::cos(Wind_.BearingRadians) * Drift) / Scale;
    const double V     = (WorldPointMeters.Z - std::sin(Wind_.BearingRadians) * Drift) / Scale;
    const double Gain  = Wind_.SwirlStrength * Wind_.SpeedMetersPerSecond * 0.45 * Visible;
    double       Gradient[2]{};
    (void)EvaluateGradientNoise(U, V + WorldPointMeters.Y / Scale * 0.37, Wind_.Seed, Gradient);
    SpatialVector Eddy{Gradient[1] * Gain, 0.0, -Gradient[0] * Gain};
    Eddy.Y = EvaluateGradientNoise(U + 71.3, V + 13.9, Wind_.Seed + 5u, Gradient) * Gain * 0.2;
    return Eddy;
}

// 📝 Ekman speed gain and veer with altitude, the slow mesoscale variation, and the coherent two-frequency gust wave
//    travelling downwind, each faded by the level of detail; then the eddies.
SpatialVector ForceFieldSpace::EvaluateWindFlow(SpatialVector WorldPointMeters, bool IncludeCurlSwirl, double CellSpanMeters) const noexcept
{
    if (!Enabled_) return {};
    const double AltAboveGroundM = std::max(0.0, WorldPointMeters.Y - GroundPlaneYMeters_);
    const double AltKm           = AltAboveGroundM * 1.0e-3;
    double       Bearing         = Wind_.BearingRadians + Wind_.VeerRadiansPerKilometer * AltKm;
    double       ShearGain       = std::max(0.15, 1.0 + Wind_.ShearPerKilometer * std::min(AltKm, 12.0));
    if (Wind_.MesoscaleStrength > 0.0 && Wind_.MesoscaleScaleMeters > 0.0)
    {
        const double Visible = ResolveResolvable(Wind_.MesoscaleScaleMeters * 2.0, CellSpanMeters);
        if (Visible > 0.0)
        {
            const double Drift = 0.35 * Wind_.SpeedMetersPerSecond * ElapsedSeconds_;
            const double U     = (WorldPointMeters.X - std::cos(Wind_.BearingRadians) * Drift) / Wind_.MesoscaleScaleMeters;
            const double V     = (WorldPointMeters.Z - std::sin(Wind_.BearingRadians) * Drift) / Wind_.MesoscaleScaleMeters;
            double       Gradient[2]{};
            const double SpeedShift = EvaluateGradientNoise(U, V, Wind_.Seed + 11u, Gradient);
            const double TurnShift  = EvaluateGradientNoise(U + 37.1, V - 19.7, Wind_.Seed + 23u, Gradient);
            ShearGain *= 1.0 + Wind_.MesoscaleStrength * SpeedShift * Visible;
            Bearing += Wind_.MesoscaleStrength * 55.0 * (π / 180.0) * TurnShift * Visible;
        }
    }
    const double DirX = std::cos(Bearing);
    const double DirZ = std::sin(Bearing);

    const double AlongWindCoord = WorldPointMeters.X * DirX + WorldPointMeters.Z * DirZ;
    const double Phase          = Twoπ * Wind_.GustFrequencyHz * ElapsedSeconds_ - 0.011 * AlongWindCoord;
    const double GustWave       = 0.62 * std::sin(Phase) * ResolveResolvable(Twoπ / 0.011, CellSpanMeters) +
                            0.38 * std::sin(2.17 * Phase + 1.13) * ResolveResolvable(Twoπ / (0.011 * 2.17), CellSpanMeters);
    const double LocalWindSpeed = Wind_.SpeedMetersPerSecond * ShearGain * (1.0 + Wind_.GustAmplitude * GustWave);

    SpatialVector Flow{DirX * LocalWindSpeed, 0.0, DirZ * LocalWindSpeed};
    if (IncludeCurlSwirl)
    {
        const SpatialVector Eddy = SampleSwirl(WorldPointMeters, CellSpanMeters);
        Flow.X += Eddy.X;
        Flow.Y += Eddy.Y;
        Flow.Z += Eddy.Z;
    }
    return Flow;
}

ForceFieldSample ForceFieldSpace::EvaluateStepSample(SpatialVector WorldPointMeters, bool IncludeCurlSwirl, double CellSpanMeters) const noexcept
{
    if (!Enabled_) return {};
    ForceFieldSample Out{};

    // 1. Base Ekman altitude-sheared wind + mesoscale + coherent multi-frequency gust wave (+ curl eddies)
    Out.AirVelocityMps = EvaluateWindFlow(WorldPointMeters, IncludeCurlSwirl, CellSpanMeters);

    double WeightAlbedoSum = 0.0;
    SpectrumRgb WeightedAlbedo{0.0, 0.0, 0.0};

    // 2. Superpose active coherent 3D force sources
    for (const ActiveForceSource& Source : Sources_)
    {
        const double Env = Source.ActivationEnvelope;
        if (Env <= 1.0e-5)
        {
            continue;
        }

        if (Source.Category == ForceSourceCategory::TornadoColumn)
        {
            const TornadoColumnParameters& V      = Source.Column;
            const double                  RelY   = WorldPointMeters.Y - V.GroundAnchorMeters.Y;
            const double                  SpanH  = std::max(10.0, V.ColumnHeightMeters);
            if (RelY < -15.0 || RelY > SpanH * 1.30)
            {
                continue;
            }
            const double HNorm      = std::clamp(RelY / SpanH, 0.0, 1.0);
            const double TopTaper   = (RelY <= SpanH) ? 1.0 : (1.0 - Smooth01((RelY - SpanH) / (0.30 * SpanH)));
            const double SpineX     = V.GroundAnchorMeters.X + V.SpineLeanXMeters * (HNorm * HNorm);
            const double SpineZ     = V.GroundAnchorMeters.Z + V.SpineLeanZMeters * (HNorm * HNorm);
            const double Dx         = WorldPointMeters.X - SpineX;
            const double Dz         = WorldPointMeters.Z - SpineZ;
            const double Radius     = std::sqrt(Dx * Dx + Dz * Dz);
            const double CoreRadius = V.GroundCoreRadiusMeters +
                                      (V.CollarCoreRadiusMeters - V.GroundCoreRadiusMeters) * Smooth01(HNorm);
            const double SafeCore   = std::max(1.0, CoreRadius);
            const double ρNorm      = Radius / SafeCore;

            // Burgers-Rott tangential velocity: peaks at ρ_norm = 1 with v_θ = V_max
            const double ColumnProfile = (2.0 * ρNorm) / (1.0 + ρNorm * ρNorm);
            const double vTangential   = Env * TopTaper * V.TangentialPeakSpeedMps * ColumnProfile;

            // Near-ground boundary-layer radial inflow transitioning to upper-collar divergence
            const double InflowBand    = std::exp(-RelY / std::max(12.0, 0.16 * SpanH)) -
                                         0.35 * Smooth01((HNorm - 0.72) / 0.28);
            const double vRadial       = -Env * TopTaper * V.RadialInflowSpeedMps * InflowBand * ColumnProfile;

            // Helical axial updraft concentrated inside and around the funnel wall
            const double vUpdraft      = Env * TopTaper * V.AxialUpdraftSpeedMps *
                                         std::exp(-0.42 * ρNorm * ρNorm) * Smooth01(RelY / 8.0 + 0.15);

            double UnitRadX = 1.0;
            double UnitRadZ = 0.0;
            if (Radius > 1.0e-5)
            {
                UnitRadX = Dx / Radius;
                UnitRadZ = Dz / Radius;
            }
            // Counter-clockwise cyclonic tangent in XZ: (-UnitRadZ, +UnitRadX)
            const double TangX = -UnitRadZ;
            const double TangZ = UnitRadX;

            Out.AirVelocityMps.X += TangX * vTangential + UnitRadX * vRadial + V.TranslationVelocityMps.X * Env * 0.25;
            Out.AirVelocityMps.Y += vUpdraft;
            Out.AirVelocityMps.Z += TangZ * vTangential + UnitRadZ * vRadial + V.TranslationVelocityMps.Z * Env * 0.25;

            // Cyclostrophic pressure deficit Δp(r) = ρ_air * V_max² / (1 + ρ_norm²) and radial gradient ∂p/∂r = ρ_air * v_θ² / r
            const double VMaxEff  = Env * TopTaper * V.TangentialPeakSpeedMps;
            const double ΔpLocal  = ρAir0 * (VMaxEff * VMaxEff) / (1.0 + ρNorm * ρNorm);
            Out.PressureDeficitPascals += ΔpLocal;
            if (Radius > 0.25)
            {
                const double dPdR = ρAir0 * (vTangential * vTangential) / Radius;
                Out.RadialPressureGradientPaPerM.X += UnitRadX * dPdR;
                Out.RadialPressureGradientPaPerM.Z += UnitRadZ * dPdR;
            }

            // Adiabatic condensation funnel potential & helical twist angle
            const double FunnelPotential = Env * TopTaper * V.CondensationStrength * std::exp(-0.78 * ρNorm * ρNorm);
            Out.FunnelCondensationDensity = std::max(Out.FunnelCondensationDensity, FunnelPotential);
            Out.HelicalTwistRadians +=
                (Source.SwirlPhaseRadians + 2.4 * HNorm) * Env * TopTaper / (1.0 + 0.28 * ρNorm * ρNorm);

            // Swirling surface saltation debris/dust skirt around touch-down foot
            if (V.DebrisSkirtHeightMeters > 0.5 && RelY >= 0.0 && RelY <= V.DebrisSkirtHeightMeters)
            {
                const double SkirtV   = 1.0 - Smooth01(RelY / V.DebrisSkirtHeightMeters);
                const double SheathR  = std::exp(-0.32 * (ρNorm - 0.85) * (ρNorm - 0.85));
                const double σDebris  = Env * V.DebrisExtinctionPerM * SkirtV * SheathR;
                Out.ParticulateExtinctionPerM += σDebris;
                WeightedAlbedo.R += σDebris * V.DebrisAlbedoRgb.R;
                WeightedAlbedo.G += σDebris * V.DebrisAlbedoRgb.G;
                WeightedAlbedo.B += σDebris * V.DebrisAlbedoRgb.B;
                WeightAlbedoSum += σDebris;
            }
        }
        else if (Source.Category == ForceSourceCategory::GustFront)
        {
            const GustFrontParameters& G   = Source.GustFront;
            const double DirLen            = std::hypot(G.PropagationDirectionXZ.X, G.PropagationDirectionXZ.Z);
            const double PropX             = (DirLen > 1.0e-6) ? (G.PropagationDirectionXZ.X / DirLen) : 1.0;
            const double PropZ             = (DirLen > 1.0e-6) ? (G.PropagationDirectionXZ.Z / DirLen) : 0.0;
            const double FrontTravel       = G.FrontSpeedMps * Source.AgeSeconds;
            const double NoseX             = G.OriginMeters.X + PropX * FrontTravel;
            const double NoseZ             = G.OriginMeters.Z + PropZ * FrontTravel;
            const double SignedDistBehind  = (NoseX - WorldPointMeters.X) * PropX + (NoseZ - WorldPointMeters.Z) * PropZ;
            const double CrestDepth        = std::max(5.0, G.CrestDepthMeters);
            const double RelY              = WorldPointMeters.Y - G.OriginMeters.Y;

            if (SignedDistBehind >= -0.25 * CrestDepth && SignedDistBehind <= 2.4 * CrestDepth &&
                RelY >= 0.0 && RelY <= G.VerticalExtentMeters * 1.25)
            {
                const double NoseRise  = Smooth01((SignedDistBehind + 0.25 * CrestDepth) / (0.35 * CrestDepth));
                const double WakeDecay = std::exp(-std::max(0.0, SignedDistBehind - 0.1 * CrestDepth) / CrestDepth);
                const double HeightEnv = 1.0 - Smooth01(RelY / std::max(5.0, G.VerticalExtentMeters));
                const double Surge     = Env * NoseRise * WakeDecay * HeightEnv;
                const double NoseLift  = Env * NoseRise * std::exp(-std::abs(SignedDistBehind) / (0.28 * CrestDepth)) *
                                         HeightEnv * (0.28 * G.PeakGustSpeedMps);

                Out.AirVelocityMps.X += PropX * (G.PeakGustSpeedMps * Surge);
                Out.AirVelocityMps.Y += NoseLift;
                Out.AirVelocityMps.Z += PropZ * (G.PeakGustSpeedMps * Surge);

                const double σDust = Surge * G.LoftedDustExtinctionM;
                Out.ParticulateExtinctionPerM += σDust;
                WeightedAlbedo.R += σDust * G.DustAlbedoRgb.R;
                WeightedAlbedo.G += σDust * G.DustAlbedoRgb.G;
                WeightedAlbedo.B += σDust * G.DustAlbedoRgb.B;
                WeightAlbedoSum += σDust;
            }
        }
        else if (Source.Category == ForceSourceCategory::Microburst)
        {
            const MicroburstParameters& M    = Source.Microburst;
            const double                Dx   = WorldPointMeters.X - M.CoreCenterMeters.X;
            const double                Dz   = WorldPointMeters.Z - M.CoreCenterMeters.Z;
            const double                Rad  = std::hypot(Dx, Dz);
            const double                RelY = WorldPointMeters.Y - M.GroundPlaneYMeters;
            const double                ColR = std::max(5.0, M.ColumnRadiusMeters);
            if (RelY >= 0.0 && RelY <= std::max(40.0, M.CoreCenterMeters.Y * 1.4))
            {
                const double DownCore = std::exp(-(Rad * Rad) / (ColR * ColR)) * Smooth01(RelY / 35.0);
                const double OutRing  = (Rad / std::max(10.0, M.OutflowRingRadiusMeters)) *
                                        std::exp(-0.5 * std::pow(Rad / std::max(10.0, M.OutflowRingRadiusMeters), 2.0)) *
                                        std::exp(-RelY / 42.0);
                const double InvR     = (Rad > 1.0e-4) ? (1.0 / Rad) : 0.0;
                Out.AirVelocityMps.X += Env * M.OutflowPeakSpeedMps * OutRing * (Dx * InvR);
                Out.AirVelocityMps.Y -= Env * M.DowndraftSpeedMps * DownCore;
                Out.AirVelocityMps.Z += Env * M.OutflowPeakSpeedMps * OutRing * (Dz * InvR);

                const double σDust = Env * M.LoftedDustExtinctionM * OutRing;
                Out.ParticulateExtinctionPerM += σDust;
                WeightedAlbedo.R += σDust * 0.64;
                WeightedAlbedo.G += σDust * 0.52;
                WeightedAlbedo.B += σDust * 0.38;
                WeightAlbedoSum += σDust;
            }
        }
        else if (Source.Category == ForceSourceCategory::GustPuff)
        {
            const GustPuffParameters& P    = Source.GustPuff;
            const double              Rxz  = std::max(1.5, P.RadiusXZMeters);
            const double              Ry   = std::max(1.0, P.RadiusYMeters);
            const double              Dx   = (WorldPointMeters.X - P.CenterMeters.X) / Rxz;
            const double              Dy   = (WorldPointMeters.Y - P.CenterMeters.Y) / Ry;
            const double              Dz   = (WorldPointMeters.Z - P.CenterMeters.Z) / Rxz;
            const double              Dist2 = Dx * Dx + Dy * Dy + Dz * Dz;
            if (Dist2 < 9.0)
            {
                const double Gauss = Env * std::exp(-0.5 * Dist2);
                Out.AirVelocityMps.X += P.SurgeVelocityMps.X * Gauss;
                Out.AirVelocityMps.Y += P.SurgeVelocityMps.Y * Gauss;
                Out.AirVelocityMps.Z += P.SurgeVelocityMps.Z * Gauss;

                const double σDust = Gauss * P.WispDustExtinctionM;
                Out.ParticulateExtinctionPerM += σDust;
                WeightedAlbedo.R += σDust * P.DustAlbedoRgb.R;
                WeightedAlbedo.G += σDust * P.DustAlbedoRgb.G;
                WeightedAlbedo.B += σDust * P.DustAlbedoRgb.B;
                WeightAlbedoSum += σDust;
            }
        }
        else if (Source.Category == ForceSourceCategory::VehicleWake)
        {
            const AerodynamicWakeParameters& W      = Source.Wake;
            const double                     SpdXZ  = std::hypot(W.BodyVelocityMps.X, W.BodyVelocityMps.Z);
            const double                     FwdX   = (SpdXZ > 1.0e-5) ? (W.BodyVelocityMps.X / SpdXZ) : 0.0;
            const double                     FwdZ   = (SpdXZ > 1.0e-5) ? (W.BodyVelocityMps.Z / SpdXZ) : 1.0;
            const double                     RightX = FwdZ;
            const double                     RightZ = -FwdX;
            const double                     RelX   = WorldPointMeters.X - W.BodyOriginMeters.X;
            const double                     RelY   = WorldPointMeters.Y - GroundPlaneYMeters_;
            const double                     RelZ   = WorldPointMeters.Z - W.BodyOriginMeters.Z;
            // TrailDist > 0 downstream behind the moving vehicle/obstacle
            const double                     TrailDist   = -(RelX * FwdX + RelZ * FwdZ);
            const double                     LateralDist = RelX * RightX + RelZ * RightZ;
            const double                     WakeLen     = std::max(4.0, W.WakeLengthMeters);
            const double                     HalfW       = std::max(0.4, W.HalfWidthMeters);
            const double                     WakeH       = std::max(0.8, W.WakeHeightMeters);
            if (TrailDist >= -0.5 * HalfW && TrailDist <= 2.2 * WakeLen && RelY >= 0.0 && RelY <= 2.4 * WakeH)
            {
                const double Spread     = 1.0 + 0.85 * std::max(0.0, TrailDist) / WakeLen;
                const double EffHalfW   = HalfW * Spread;
                const double EffWakeH   = WakeH * Spread;
                const double LongEnv    = Smooth01((TrailDist + 0.5 * HalfW) / HalfW) * std::exp(-std::max(0.0, TrailDist) / WakeLen);
                const double LatNorm    = LateralDist / EffHalfW;
                const double LatEnv     = std::exp(-0.5 * LatNorm * LatNorm);
                const double VertEnv    = 1.0 - Smooth01(RelY / (1.8 * EffWakeH));
                const double WakeWeight = Env * LongEnv * LatEnv * VertEnv;

                // Forward slipstream tow + counter-rotating wingtip/wheel-arch upwash & inward roll
                const double Upwash     = (1.0 - 0.8 * LatNorm * LatNorm) * W.TrailingCirculationMps;
                const double InwardRoll = -LatNorm * 0.65 * W.TrailingCirculationMps;
                Out.AirVelocityMps.X += WakeWeight * (FwdX * W.SlipstreamSuctionMps + RightX * InwardRoll);
                Out.AirVelocityMps.Y += WakeWeight * std::max(0.0, Upwash);
                Out.AirVelocityMps.Z += WakeWeight * (FwdZ * W.SlipstreamSuctionMps + RightZ * InwardRoll);

                const double σWake = WakeWeight * W.RoosterTailExtinctionM;
                Out.ParticulateExtinctionPerM += σWake;
                WeightedAlbedo.R += σWake * W.SprayDustAlbedoRgb.R;
                WeightedAlbedo.G += σWake * W.SprayDustAlbedoRgb.G;
                WeightedAlbedo.B += σWake * W.SprayDustAlbedoRgb.B;
                WeightAlbedoSum += σWake;
            }
        }
        else if (Source.Category == ForceSourceCategory::ThermalUpdraft)
        {
            const ThermalUpdraftParameters& T    = Source.Thermal;
            const double                    RelY = WorldPointMeters.Y - T.BaseAnchorMeters.Y;
            const double                    ColH = std::max(15.0, T.ColumnHeightMeters);
            if (RelY >= 0.0 && RelY <= ColH * 1.35)
            {
                const double HNorm   = std::clamp(RelY / ColH, 0.0, 1.35);
                const double Dx      = WorldPointMeters.X - T.BaseAnchorMeters.X;
                const double Dz      = WorldPointMeters.Z - T.BaseAnchorMeters.Z;
                const double Rad     = std::hypot(Dx, Dz);
                const double PlumeR  = std::max(2.0, T.CoreRadiusMeters * (1.0 + 0.45 * HNorm));
                const double ρNorm   = Rad / PlumeR;
                if (ρNorm < 3.5)
                {
                    const double CoreEnv = Env * std::exp(-0.5 * ρNorm * ρNorm) *
                                           Smooth01(RelY / 10.0 + 0.15) * (1.0 - Smooth01((HNorm - 1.0) / 0.35));
                    // Lower entrainment inflow (< 0.35 H) transitioning to upper anvil divergence (> 0.75 H)
                    const double RadialMode = -Smooth01(0.35 - HNorm) + 0.55 * Smooth01((HNorm - 0.72) / 0.28);
                    const double InvR       = (Rad > 1.0e-4) ? (1.0 / Rad) : 0.0;
                    const double vRad       = Env * T.EntrainmentInflowMps * RadialMode *
                                              (2.0 * ρNorm / (1.0 + ρNorm * ρNorm));

                    Out.AirVelocityMps.X += (Dx * InvR) * vRad;
                    Out.AirVelocityMps.Y += T.PeakUpdraftSpeedMps * CoreEnv;
                    Out.AirVelocityMps.Z += (Dz * InvR) * vRad;

                    if (HNorm > 0.65)
                    {
                        Out.FunnelCondensationDensity = std::max(
                            Out.FunnelCondensationDensity,
                            CoreEnv * T.CumulusBoostStrength * Smooth01((HNorm - 0.65) / 0.30));
                    }

                    const double σPlume = CoreEnv * T.PlumeDustExtinctionM;
                    Out.ParticulateExtinctionPerM += σPlume;
                    WeightedAlbedo.R += σPlume * T.PlumeAlbedoRgb.R;
                    WeightedAlbedo.G += σPlume * T.PlumeAlbedoRgb.G;
                    WeightedAlbedo.B += σPlume * T.PlumeAlbedoRgb.B;
                    WeightAlbedoSum += σPlume;
                }
            }
        }
        else if (Source.Category == ForceSourceCategory::JetCorridor)
        {
            const DirectionalJetParameters& J      = Source.Jet;
            const double                    AxLen  = std::sqrt(J.AxisDirectionUnit.X * J.AxisDirectionUnit.X +
                                                               J.AxisDirectionUnit.Y * J.AxisDirectionUnit.Y +
                                                               J.AxisDirectionUnit.Z * J.AxisDirectionUnit.Z);
            const SpatialVector             Axis   = (AxLen > 1.0e-6)
                                                         ? SpatialVector{J.AxisDirectionUnit.X / AxLen,
                                                                         J.AxisDirectionUnit.Y / AxLen,
                                                                         J.AxisDirectionUnit.Z / AxLen}
                                                         : SpatialVector{1.0, 0.0, 0.0};
            const SpatialVector             Rel    = {WorldPointMeters.X - J.OriginMeters.X,
                                                      WorldPointMeters.Y - J.OriginMeters.Y,
                                                      WorldPointMeters.Z - J.OriginMeters.Z};
            const double                    AxialS = Rel.X * Axis.X + Rel.Y * Axis.Y + Rel.Z * Axis.Z;
            const double                    LenM   = std::max(2.0, J.CorridorLengthMeters);
            const double                    BaseR  = std::max(0.5, J.CorridorRadiusMeters);
            if (AxialS >= -0.2 * BaseR && AxialS <= 1.25 * LenM)
            {
                const SpatialVector Perp  = {Rel.X - Axis.X * AxialS, Rel.Y - Axis.Y * AxialS, Rel.Z - Axis.Z * AxialS};
                const double        RadSq = Perp.X * Perp.X + Perp.Y * Perp.Y + Perp.Z * Perp.Z;
                const double        JetR  = BaseR * (1.0 + 0.35 * std::max(0.0, AxialS) / LenM);
                if (RadSq < 9.0 * JetR * JetR)
                {
                    const double AxialEnv  = Smooth01((AxialS + 0.2 * BaseR) / (0.4 * BaseR)) *
                                             (1.0 - Smooth01((AxialS - 0.85 * LenM) / (0.40 * LenM)));
                    const double RadialEnv = std::exp(-0.5 * RadSq / (JetR * JetR));
                    const double JetGain   = Env * AxialEnv * RadialEnv;
                    Out.AirVelocityMps.X += Axis.X * (J.CoreJetSpeedMps * JetGain);
                    Out.AirVelocityMps.Y += Axis.Y * (J.CoreJetSpeedMps * JetGain);
                    Out.AirVelocityMps.Z += Axis.Z * (J.CoreJetSpeedMps * JetGain);

                    const double σJet = JetGain * J.JetParticulateExtM;
                    Out.ParticulateExtinctionPerM += σJet;
                    WeightedAlbedo.R += σJet * J.JetAlbedoRgb.R;
                    WeightedAlbedo.G += σJet * J.JetAlbedoRgb.G;
                    WeightedAlbedo.B += σJet * J.JetAlbedoRgb.B;
                    WeightAlbedoSum += σJet;
                }
            }
        }
        else if (Source.Category == ForceSourceCategory::BlastWave)
        {
            const SphericalImpulseParameters& S      = Source.Impulse;
            const double                      R0     = std::max(0.5, S.InitialRadiusMeters);
            const double                      RFront = R0 + std::max(0.0, S.ExpansionSpeedMps) * Source.AgeSeconds;
            const double                      Thick  = std::max(0.5, S.ShellThicknessMeters);
            const double                      Dx     = WorldPointMeters.X - S.EpicenterMeters.X;
            const double                      Dy     = WorldPointMeters.Y - S.EpicenterMeters.Y;
            const double                      Dz     = WorldPointMeters.Z - S.EpicenterMeters.Z;
            const double                      Dist   = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
            const double                      ShellD = (Dist - RFront) / Thick;
            if (std::abs(ShellD) < 3.2 && Dist > 1.0e-4)
            {
                const double GeomAtten = std::pow(R0 / std::max(R0, RFront), 0.65);
                const double ShellWave = Env * GeomAtten * std::exp(-0.5 * ShellD * ShellD);
                const double InvDist   = 1.0 / Dist;
                const double vOut      = S.PeakRadialSpeedMps * ShellWave;
                Out.AirVelocityMps.X += (Dx * InvDist) * vOut;
                Out.AirVelocityMps.Y += (Dy * InvDist) * vOut;
                Out.AirVelocityMps.Z += (Dz * InvDist) * vOut;

                const double σShock = ShellWave * S.ShockDustExtinctionM;
                Out.ParticulateExtinctionPerM += σShock;
                WeightedAlbedo.R += σShock * S.ShockAlbedoRgb.R;
                WeightedAlbedo.G += σShock * S.ShockAlbedoRgb.G;
                WeightedAlbedo.B += σShock * S.ShockAlbedoRgb.B;
                WeightAlbedoSum += σShock;
            }
        }
    }

    // 3. Authored flow sources (ForceSource records)
    if (!Authored_.empty())
    {
        const ForceWindReading Wind = QueryAuthoredWind();
        for (const PreparedForceSource& Prepared : Authored_)
        {
            if (Prepared.Contribution != ForceContribution::Flow) continue;
            (void)EvaluatePreparedForceSource(Prepared, Wind, WorldPointMeters, Out.AirVelocityMps);
        }
    }

    if (WeightAlbedoSum > 1.0e-7)
    {
        Out.ParticulateScatteringAlbedo = {
            WeightedAlbedo.R / WeightAlbedoSum,
            WeightedAlbedo.G / WeightAlbedoSum,
            WeightedAlbedo.B / WeightAlbedoSum,
        };
    }

    return Out;
}

void ForceFieldSpace::StepParticles(std::span<AerodynamicParticle> Particles, double DeltaSeconds) const noexcept
{
    const double Dt = std::clamp(DeltaSeconds, 0.0, 0.1);
    if (Dt <= 0.0)
    {
        return;
    }

    for (AerodynamicParticle& Particle : Particles)
    {
        const SpeciesPhysicalProperties Props = QuerySpeciesProperties(Particle.Species);
        const double                    Diam  = (Particle.DiameterMeters > 1.0e-6f)
                                                    ? static_cast<double>(Particle.DiameterMeters)
                                                    : Props.DefaultDiameterM;
        const double Radius = 0.5 * Diam;
        const double Volume = (4.0 / 3.0) * π * Radius * Radius * Radius;
        const double Area   = π * Radius * Radius;
        const double Mass   = std::max(1.0e-12, Props.DensityKgPerM3 * Volume);

        const ForceFieldSample Field = EvaluateStepSample(Particle.PositionMeters, true);
        const double           AirSpeedHoriz = std::hypot(Field.AirVelocityMps.X, Field.AirVelocityMps.Z);

        // Bagnold surface saltation lofting: resting sand, dust, or debris lifts off when near-surface air speed exceeds threshold
        if (Particle.RestingOnGround)
        {
            if (AirSpeedHoriz > Props.SaltationThresholdMs)
            {
                Particle.RestingOnGround = false;
                const double Excess      = AirSpeedHoriz - Props.SaltationThresholdMs;
                const double HopJitter   = 0.75 + 0.5 * HashLattice(
                    static_cast<int32_t>(Particle.PositionMeters.X * 13.0),
                    static_cast<int32_t>(Particle.AgeSeconds * 60.0),
                    static_cast<int32_t>(Particle.PositionMeters.Z * 13.0),
                    Particle.Seed);
                Particle.VelocityMps.X = 0.35 * Field.AirVelocityMps.X;
                Particle.VelocityMps.Y = std::min(14.0, (0.28 * Excess + 0.45 * std::max(0.0, Field.AirVelocityMps.Y)) * HopJitter);
                Particle.VelocityMps.Z = 0.35 * Field.AirVelocityMps.Z;
                Particle.PositionMeters.Y = GroundPlaneYMeters_ + 0.02;
            }
            else
            {
                Particle.AgeSeconds += static_cast<float>(Dt);
                continue;
            }
        }

        // Sub-step stiff aerodynamic drag for small dust/snow/rain grains so explicit integration stays unconditionally stable
        const int    SubSteps = (Diam < 5.0e-4) ? 4 : 1;
        const double SubDt    = Dt / static_cast<double>(SubSteps);

        for (int Sub = 0; Sub < SubSteps; ++Sub)
        {
            SpatialVector RelVel = {
                Field.AirVelocityMps.X - Particle.VelocityMps.X,
                Field.AirVelocityMps.Y - Particle.VelocityMps.Y,
                Field.AirVelocityMps.Z - Particle.VelocityMps.Z,
            };
            if (Particle.Species == AerodynamicParticleSpecies::SnowFlake)
            {
                const double FlutterPhase = 5.5 * static_cast<double>(Particle.AgeSeconds) + 0.17 * static_cast<double>(Particle.Seed);
                RelVel.X += 0.55 * std::sin(FlutterPhase);
                RelVel.Z += 0.55 * std::cos(0.83 * FlutterPhase);
            }

            const double RelSpeed = std::sqrt(RelVel.X * RelVel.X + RelVel.Y * RelVel.Y + RelVel.Z * RelVel.Z);
            // Stokes linear + Newton quadratic drag blend so micron dust and cm hailstones both hit physical terminal velocities
            const double νKinematic = 1.5e-5;
            const double Re         = std::max(1.0e-3, RelSpeed * Diam / νKinematic);
            const double CdEff      = Props.DragCoefficient + (24.0 / Re);
            const double DragRate   = (0.5 * ρAir0 * CdEff * Area * RelSpeed) / Mass;

            // Semi-implicit velocity update: v_{n+1} = (v_n + dt * (DragRate * u_air + a_ext)) / (1 + dt * DragRate)
            const double BuoyancyG  = GravG * (1.0 - ρAir0 / Props.DensityKgPerM3);
            const double PressAccX  = -(Volume / Mass) * Field.RadialPressureGradientPaPerM.X;
            const double PressAccZ  = -(Volume / Mass) * Field.RadialPressureGradientPaPerM.Z;
            const double InvDenom   = 1.0 / (1.0 + SubDt * DragRate);

            Particle.VelocityMps.X = (Particle.VelocityMps.X + SubDt * (DragRate * (Particle.VelocityMps.X + RelVel.X) + PressAccX)) * InvDenom;
            Particle.VelocityMps.Y = (Particle.VelocityMps.Y + SubDt * (DragRate * (Particle.VelocityMps.Y + RelVel.Y) - BuoyancyG)) * InvDenom;
            Particle.VelocityMps.Z = (Particle.VelocityMps.Z + SubDt * (DragRate * (Particle.VelocityMps.Z + RelVel.Z) + PressAccZ)) * InvDenom;

            Particle.PositionMeters.X += Particle.VelocityMps.X * SubDt;
            Particle.PositionMeters.Y += Particle.VelocityMps.Y * SubDt;
            Particle.PositionMeters.Z += Particle.VelocityMps.Z * SubDt;
        }

        if (Particle.PositionMeters.Y <= GroundPlaneYMeters_)
        {
            Particle.PositionMeters.Y = GroundPlaneYMeters_;
            if (Props.BounceRestitution > 0.0 && AirSpeedHoriz > Props.SaltationThresholdMs &&
                std::abs(Particle.VelocityMps.Y) > 0.35)
            {
                Particle.VelocityMps.Y = std::abs(Particle.VelocityMps.Y) * Props.BounceRestitution;
                Particle.VelocityMps.X *= 0.72;
                Particle.VelocityMps.Z *= 0.72;
            }
            else
            {
                Particle.VelocityMps     = {};
                Particle.RestingOnGround = true;
            }
        }

        Particle.AgeSeconds += static_cast<float>(Dt);
    }
}

uint32_t ForceFieldSpace::AppendGpuForceRecords(std::vector<std::array<float, 4>>& OutFloat4Table) const
{
    if (!Enabled_) return 0;
    uint32_t Written = 0u;
    for (const ActiveForceSource& Source : Sources_)
    {
        if (Written >= kMaxGpuForceSourcesRealm)
        {
            break;
        }
        const float Env = static_cast<float>(Source.ActivationEnvelope);
        if (Env <= 1.0e-4f)
        {
            continue;
        }

        if (Source.Category == ForceSourceCategory::TornadoColumn)
        {
            const TornadoColumnParameters& V = Source.Column;
            OutFloat4Table.push_back({static_cast<float>(V.GroundAnchorMeters.X),
                                      static_cast<float>(V.GroundAnchorMeters.Y),
                                      static_cast<float>(V.GroundAnchorMeters.Z),
                                      1.0f});
            OutFloat4Table.push_back({static_cast<float>(V.ColumnHeightMeters),
                                      static_cast<float>(V.GroundCoreRadiusMeters),
                                      static_cast<float>(V.CollarCoreRadiusMeters),
                                      static_cast<float>(V.SpineLeanXMeters)});
            OutFloat4Table.push_back({static_cast<float>(V.TangentialPeakSpeedMps) * Env,
                                      static_cast<float>(V.RadialInflowSpeedMps) * Env,
                                      static_cast<float>(V.AxialUpdraftSpeedMps) * Env,
                                      static_cast<float>(V.SpineLeanZMeters)});
            OutFloat4Table.push_back({Env * static_cast<float>(V.CondensationStrength),
                                      static_cast<float>(V.DebrisSkirtHeightMeters),
                                      Env * static_cast<float>(V.DebrisExtinctionPerM),
                                      static_cast<float>(Source.SwirlPhaseRadians)});
            ++Written;
        }
        else if (Source.Category == ForceSourceCategory::GustFront)
        {
            const GustFrontParameters& G      = Source.GustFront;
            const double               DirLen = std::hypot(G.PropagationDirectionXZ.X, G.PropagationDirectionXZ.Z);
            const float                PropX  = (DirLen > 1.0e-6) ? static_cast<float>(G.PropagationDirectionXZ.X / DirLen) : 1.0f;
            const float                PropZ  = (DirLen > 1.0e-6) ? static_cast<float>(G.PropagationDirectionXZ.Z / DirLen) : 0.0f;
            const float                Travel = static_cast<float>(G.FrontSpeedMps * Source.AgeSeconds);
            const float                NoseX  = static_cast<float>(G.OriginMeters.X) + PropX * Travel;
            const float                NoseZ  = static_cast<float>(G.OriginMeters.Z) + PropZ * Travel;

            OutFloat4Table.push_back({NoseX, static_cast<float>(G.OriginMeters.Y), NoseZ, 2.0f});
            OutFloat4Table.push_back({static_cast<float>(G.VerticalExtentMeters),
                                      static_cast<float>(G.CrestDepthMeters),
                                      PropX,
                                      PropZ});
            OutFloat4Table.push_back({static_cast<float>(G.PeakGustSpeedMps) * Env,
                                      static_cast<float>(G.DustAlbedoRgb.R),
                                      static_cast<float>(G.DustAlbedoRgb.G),
                                      static_cast<float>(G.DustAlbedoRgb.B)});
            OutFloat4Table.push_back({0.0f,
                                      static_cast<float>(G.VerticalExtentMeters),
                                      Env * static_cast<float>(G.LoftedDustExtinctionM),
                                      0.0f});
            ++Written;
        }
        else if (Source.Category == ForceSourceCategory::Microburst)
        {
            const MicroburstParameters& M = Source.Microburst;
            OutFloat4Table.push_back({static_cast<float>(M.CoreCenterMeters.X),
                                      static_cast<float>(M.GroundPlaneYMeters),
                                      static_cast<float>(M.CoreCenterMeters.Z),
                                      3.0f});
            OutFloat4Table.push_back({static_cast<float>(M.CoreCenterMeters.Y),
                                      static_cast<float>(M.ColumnRadiusMeters),
                                      static_cast<float>(M.OutflowRingRadiusMeters),
                                      0.0f});
            OutFloat4Table.push_back({static_cast<float>(M.OutflowPeakSpeedMps) * Env,
                                      static_cast<float>(M.DowndraftSpeedMps) * Env,
                                      0.64f,
                                      0.52f});
            OutFloat4Table.push_back({0.0f,
                                      48.0f,
                                      Env * static_cast<float>(M.LoftedDustExtinctionM),
                                      0.38f});
            ++Written;
        }
        else if (Source.Category == ForceSourceCategory::GustPuff)
        {
            const GustPuffParameters& P = Source.GustPuff;
            OutFloat4Table.push_back({static_cast<float>(P.CenterMeters.X),
                                      static_cast<float>(P.CenterMeters.Y),
                                      static_cast<float>(P.CenterMeters.Z),
                                      4.0f});
            OutFloat4Table.push_back({static_cast<float>(P.RadiusXZMeters),
                                      static_cast<float>(P.RadiusYMeters),
                                      static_cast<float>(P.SurgeVelocityMps.X) * Env,
                                      static_cast<float>(P.SurgeVelocityMps.Z) * Env});
            OutFloat4Table.push_back({static_cast<float>(P.SurgeVelocityMps.Y) * Env,
                                      static_cast<float>(P.DustAlbedoRgb.R),
                                      static_cast<float>(P.DustAlbedoRgb.G),
                                      static_cast<float>(P.DustAlbedoRgb.B)});
            OutFloat4Table.push_back({0.0f,
                                      static_cast<float>(P.RadiusYMeters),
                                      Env * static_cast<float>(P.WispDustExtinctionM),
                                      static_cast<float>(Source.AgeSeconds)});
            ++Written;
        }
        else if (Source.Category == ForceSourceCategory::VehicleWake)
        {
            const AerodynamicWakeParameters& W     = Source.Wake;
            const double                     SpdXZ = std::hypot(W.BodyVelocityMps.X, W.BodyVelocityMps.Z);
            const float                      FwdX  = (SpdXZ > 1.0e-5) ? static_cast<float>(W.BodyVelocityMps.X / SpdXZ) : 0.0f;
            const float                      FwdZ  = (SpdXZ > 1.0e-5) ? static_cast<float>(W.BodyVelocityMps.Z / SpdXZ) : 1.0f;
            OutFloat4Table.push_back({static_cast<float>(W.BodyOriginMeters.X),
                                      static_cast<float>(GroundPlaneYMeters_),
                                      static_cast<float>(W.BodyOriginMeters.Z),
                                      5.0f});
            OutFloat4Table.push_back({FwdX,
                                      FwdZ,
                                      static_cast<float>(W.HalfWidthMeters),
                                      static_cast<float>(W.WakeLengthMeters)});
            OutFloat4Table.push_back({static_cast<float>(W.WakeHeightMeters),
                                      static_cast<float>(W.SlipstreamSuctionMps) * Env,
                                      static_cast<float>(W.SprayDustAlbedoRgb.R),
                                      static_cast<float>(W.SprayDustAlbedoRgb.G)});
            OutFloat4Table.push_back({static_cast<float>(W.TrailingCirculationMps) * Env,
                                      static_cast<float>(W.WakeHeightMeters),
                                      Env * static_cast<float>(W.RoosterTailExtinctionM),
                                      static_cast<float>(W.SprayDustAlbedoRgb.B)});
            ++Written;
        }
        else if (Source.Category == ForceSourceCategory::ThermalUpdraft)
        {
            const ThermalUpdraftParameters& T = Source.Thermal;
            OutFloat4Table.push_back({static_cast<float>(T.BaseAnchorMeters.X),
                                      static_cast<float>(T.BaseAnchorMeters.Y),
                                      static_cast<float>(T.BaseAnchorMeters.Z),
                                      6.0f});
            OutFloat4Table.push_back({static_cast<float>(T.ColumnHeightMeters),
                                      static_cast<float>(T.CoreRadiusMeters),
                                      static_cast<float>(T.PeakUpdraftSpeedMps) * Env,
                                      static_cast<float>(T.EntrainmentInflowMps) * Env});
            OutFloat4Table.push_back({Env * static_cast<float>(T.CumulusBoostStrength),
                                      static_cast<float>(T.PlumeAlbedoRgb.R),
                                      static_cast<float>(T.PlumeAlbedoRgb.G),
                                      static_cast<float>(T.PlumeAlbedoRgb.B)});
            OutFloat4Table.push_back({Env * static_cast<float>(T.CumulusBoostStrength),
                                      static_cast<float>(T.ColumnHeightMeters),
                                      Env * static_cast<float>(T.PlumeDustExtinctionM),
                                      static_cast<float>(Source.AgeSeconds)});
            ++Written;
        }
        else if (Source.Category == ForceSourceCategory::JetCorridor)
        {
            const DirectionalJetParameters& J     = Source.Jet;
            const double                    AxLen = std::sqrt(J.AxisDirectionUnit.X * J.AxisDirectionUnit.X +
                                                              J.AxisDirectionUnit.Y * J.AxisDirectionUnit.Y +
                                                              J.AxisDirectionUnit.Z * J.AxisDirectionUnit.Z);
            const float                     AxX   = (AxLen > 1.0e-6) ? static_cast<float>(J.AxisDirectionUnit.X / AxLen) : 1.0f;
            const float                     AxY   = (AxLen > 1.0e-6) ? static_cast<float>(J.AxisDirectionUnit.Y / AxLen) : 0.0f;
            const float                     AxZ   = (AxLen > 1.0e-6) ? static_cast<float>(J.AxisDirectionUnit.Z / AxLen) : 0.0f;
            OutFloat4Table.push_back({static_cast<float>(J.OriginMeters.X),
                                      static_cast<float>(J.OriginMeters.Y),
                                      static_cast<float>(J.OriginMeters.Z),
                                      7.0f});
            OutFloat4Table.push_back({AxX, AxY, AxZ, static_cast<float>(J.CorridorLengthMeters)});
            OutFloat4Table.push_back({static_cast<float>(J.CorridorRadiusMeters),
                                      static_cast<float>(J.CoreJetSpeedMps) * Env,
                                      static_cast<float>(J.JetAlbedoRgb.R),
                                      static_cast<float>(J.JetAlbedoRgb.G)});
            OutFloat4Table.push_back({0.0f,
                                      static_cast<float>(J.CorridorRadiusMeters * 2.0),
                                      Env * static_cast<float>(J.JetParticulateExtM),
                                      static_cast<float>(J.JetAlbedoRgb.B)});
            ++Written;
        }
        else if (Source.Category == ForceSourceCategory::BlastWave)
        {
            const SphericalImpulseParameters& S      = Source.Impulse;
            const double                      R0     = std::max(0.5, S.InitialRadiusMeters);
            const double                      RFront = R0 + std::max(0.0, S.ExpansionSpeedMps) * Source.AgeSeconds;
            const double                      Atten  = std::pow(R0 / std::max(R0, RFront), 0.65);
            OutFloat4Table.push_back({static_cast<float>(S.EpicenterMeters.X),
                                      static_cast<float>(S.EpicenterMeters.Y),
                                      static_cast<float>(S.EpicenterMeters.Z),
                                      8.0f});
            OutFloat4Table.push_back({static_cast<float>(RFront),
                                      static_cast<float>(S.ShellThicknessMeters),
                                      static_cast<float>(S.PeakRadialSpeedMps * Atten) * Env,
                                      0.0f});
            OutFloat4Table.push_back({static_cast<float>(S.ShockAlbedoRgb.R),
                                      static_cast<float>(S.ShockAlbedoRgb.G),
                                      static_cast<float>(S.ShockAlbedoRgb.B),
                                      0.0f});
            OutFloat4Table.push_back({0.0f,
                                      static_cast<float>(RFront + S.ShellThicknessMeters),
                                      Env * static_cast<float>(S.ShockDustExtinctionM * Atten),
                                      static_cast<float>(Source.AgeSeconds)});
            ++Written;
        }
    }
    return Written;
}

} // namespace Frontier
