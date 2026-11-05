/*==========================================================================================================================================
                                                      PLUMESOURCEPROJECTION.CPP
==========================================================================================================================================*/
// 🧩 See PlumeSourceProjection.h. Doubles stay doubles; every store into Records is the JavaScript's Float32Array store (static_cast<float>).

#include "PlumeSourceProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier::FluidDynamics
{

using GlobalIllumination::AddVector;
using GlobalIllumination::MultiplyAddVector;
using GlobalIllumination::ScaleVector;

namespace
{

constexpr double WarmBase           = 333.15;   // [K]   below this a sliding tread hardly smokes
constexpr double WarmSpan           = 190.0;    // [K]   from WarmBase to full vapourisation
constexpr double FlingShare         = 0.35;     // [-]   of the surface slip speed carried by the thrown smoke
constexpr double FlingCeiling       = 9.0;      // [m/s]
constexpr double ArchShare          = 0.35;     // [-]   of the patch smoke shed from the arch
constexpr Vector3 FireColour        = {1.0, 0.46, 0.14};   // [-] a sooty rubber flame, linear
constexpr double FireLightGain      = 0.00042;  // [lx·m²/W] combustion power to fire-light intensity
constexpr double FlameSidewallReach = 0.24;     // [m]   hub to the flame seat outside the sidewall
constexpr double FlameRise          = 2.6;      // [m/s] momentum given to a flame seat — burning rubber jets its vapour upward

// 📝 Math.sign.
double SignOf(double Value) noexcept
{
    if (Value > 0.0) { return 1.0; }
    if (Value < 0.0) { return -1.0; }
    return Value;
}

} // namespace

PlumeSourceProjection::PlumeSourceProjection() = default;

void PlumeSourceProjection::Reset()
{
    for (SmokeOnset& Onset : Onsets_)
    {
        Onset = SmokeOnset{0.0, 0.0, false};
    }
    PopSeconds_ = {0.0, 0.0};
    Bursts_.clear();
}

double PlumeSourceProjection::ProjectUniform()
{
    Seed_ ^= Seed_ << 13;
    Seed_ ^= Seed_ >> 17;
    Seed_ ^= Seed_ << 5;
    return static_cast<double>(Seed_) / 4294967296.0;
}

void PlumeSourceProjection::QueueBurst(const PlumeBurst& Burst)
{
    PlumeBurst Queued = Burst;
    Queued.Remaining  = Burst.Seconds;
    Bursts_.push_back(Queued);
    if (Bursts_.size() > 4)
    {
        Bursts_.erase(Bursts_.begin());
    }
}

bool PlumeSourceProjection::AdvanceOnset(SmokeOnset& Onset, double Power, double SlipSpeed, bool Contact, double Seconds)
{
    if (!Contact || !(Seconds > 0.0))
    {
        Onset = SmokeOnset{0.0, 0.0, false};
        return false;
    }
    const bool AboveEntry   = Power > SmokeSpecification::SourcePowerOnset && SlipSpeed >= SmokeSpecification::MinimumSlipSpeed;
    const bool AboveRelease = Power > SmokeSpecification::SourcePowerOnset * SmokeSpecification::SourceReleaseRatio
                           && SlipSpeed >= SmokeSpecification::MinimumSlipSpeed;
    if (AboveEntry)
    {
        Onset.RequestSeconds += Seconds;
        Onset.QuietSeconds = 0.0;
        if (Onset.RequestSeconds + 1.0e-6 >= SmokeSpecification::SourceOnsetSeconds)
        {
            Onset.Active = true;
        }
    }
    else if (Onset.Active && AboveRelease)
    {
        Onset.QuietSeconds = 0.0;
    }
    else
    {
        Onset.RequestSeconds = 0.0;
        Onset.QuietSeconds += Seconds;
        if (Onset.QuietSeconds >= SmokeSpecification::SourceReleaseSeconds)
        {
            Onset.Active = false;
        }
    }
    return Onset.Active;
}

void PlumeSourceProjection::AppendRecord(const std::array<float, PlumeSourceStride>& Record)
{
    std::copy(Record.begin(), Record.end(), Records_.begin() + static_cast<std::ptrdiff_t>(Count_ * PlumeSourceStride));
    ++Count_;
}

uint32_t PlumeSourceProjection::Project(const PlumeTelemetry& Telemetry, double Seconds, const PlumeProjectionOptions& Options)
{
    const double Rate       = Options.EmissionRate;
    const double SmokeYield = Options.SmokeYield * Rate;
    const double HeatYield  = Options.HeatYield * Rate;
    const double SootYield  = Options.SootYield * Rate;
    Count_       = 0;
    ActiveTyres_ = 0;
    Lights_.clear();
    Embers_.clear();
    Elapsed_ += Seconds;

    const auto Emit = [this](const Vector3& Position, double Radius, const Vector3& Velocity, double Smoke, double Heat, double Flame, double Soot)
    {
        if (Count_ >= PlumeSourceCapacity || (Smoke <= 0.0 && Heat <= 0.0 && Flame <= 0.0 && Soot <= 0.0))
        {
            return;
        }
        const std::array<double, PlumeSourceStride> Values = {Position[0], Position[1], Position[2], Radius, Velocity[0], Velocity[1], Velocity[2], 0.0,
                                                              Smoke, Heat, Flame, Soot};
        for (uint32_t Index = 0; Index < PlumeSourceStride; ++Index)
        {
            Records_[Count_ * PlumeSourceStride + Index] = static_cast<float>(Values[Index]);
        }
        ++Count_;
    };

    // ① tyres — ResolveWheelSmoke(), thrown along the tread's slip, shed again from the arch
    for (size_t Index = 0; Index < Telemetry.Wheels.size(); ++Index)
    {
        const PlumeWheel&     Wheel   = Telemetry.Wheels[Index];
        const PlumeWheelHeat& Thermal = Wheel.Thermal;
        const bool            Burning = Thermal.Ignited && Thermal.CombustionPower > 0.0;
        const double          Power   = Wheel.SlidingPower;
        const bool            Sliding = AdvanceOnset(Onsets_.at(Index), Power, Wheel.SlipSpeed, Wheel.InContact, Seconds);
        if (!Sliding && !Burning)
        {
            continue;
        }
        ++ActiveTyres_;
        const Vector3& Anchor   = (Burning && !Wheel.InContact) ? Wheel.HubPosition : Wheel.ContactPoint;
        const Vector3  Position = {Anchor[0], Anchor[1], Anchor[2] + 0.04};
        const double   Strength = Sliding ? std::min(4.0, std::max(0.0, (Power - SmokeSpecification::SourcePowerOnset)
                                                                         / std::max(1.0, SmokeSpecification::SourcePowerFull - SmokeSpecification::SourcePowerOnset)))
                                          : 0.0;
        const double Warm  = std::min(1.0, std::max(0.0, (Thermal.SurfaceTemperature - WarmBase) / WarmSpan));
        const double Smoke = SmokeSpecification::DensityRate * Strength * (0.35 + 0.65 * Warm) * (Wheel.Burst ? 0.25 : 1.0);
        // 📝 The tread at the patch moves at (surface speed − ground speed) along the wheel's heading; smoke leaves with a share of that,
        //    rearward of a tyre spinning forward, plus a little lift.
        const double SurfaceSlip = Wheel.SurfaceSpeed
            - (Wheel.Longitudinal.has_value() ? (Wheel.ContactVelocity[0] * (*Wheel.Longitudinal)[0] + Wheel.ContactVelocity[1] * (*Wheel.Longitudinal)[1]
                                                 + Wheel.ContactVelocity[2] * (*Wheel.Longitudinal)[2])
                                              : 0.0);
        const double  Fling   = std::max(-FlingCeiling, std::min(FlingCeiling, FlingShare * SurfaceSlip));
        const Vector3 Heading = Wheel.Longitudinal.value_or(Vector3{1.0, 0.0, 0.0});
        const Vector3 Thrown  = MultiplyAddVector(ScaleVector(Wheel.ContactVelocity, SmokeSpecification::CarrierVelocityShare), Heading, -Fling);
        Emit(Position, SmokeSpecification::PatchRadius * 1.4, AddVector(Thrown, {0.0, 0.0, 0.35}), Smoke * SmokeYield,
             SmokeSpecification::SmokeHeatRate * Strength * HeatYield, 0.0, 0.0);
        if (Sliding && std::abs(Fling) > 1.0 && Wheel.Radius != 0.0)
        {
            const Vector3 Back = ScaleVector(Heading, -SignOf(Fling));
            const Vector3 Arch = AddVector(AddVector(Wheel.HubPosition, ScaleVector(Back, Wheel.Radius * 0.85)), {0.0, 0.0, Wheel.Radius * 0.35});
            Emit(Arch, 0.28, AddVector(ScaleVector(Back, 0.45 * std::abs(Fling)), {0.0, 0.0, 1.0}), ArchShare * Smoke * SmokeYield, 0.0, 0.0, 0.0);
        }
        // 📝 Burning rubber burns where air reaches it — the outer sidewall and the back of the tread — and climbs past the arch. Two flame seats
        //    per tyre, both in open air beside the body, so the lattice's velocity cells there are fluid and the column rises instead of pooling
        //    under the arch. Together they shed Frontier's 12/s of incandescence and black soot in place of the white rubber smoke.
        if (Burning)
        {
            const Vector3 FlameHeading = Wheel.Longitudinal.value_or(Vector3{1.0, 0.0, 0.0});
            const Vector3 Outward      = ScaleVector({-FlameHeading[1], FlameHeading[0], 0.0}, (Index & 1) ? -1.0 : 1.0);
            const double  Radius       = Wheel.Radius != 0.0 ? Wheel.Radius : 0.5;
            const Vector3 Sidewall     = AddVector(MultiplyAddVector(Wheel.HubPosition, Outward, FlameSidewallReach), {0.0, 0.0, -0.3 * Radius});
            const Vector3 Trailing     = AddVector(MultiplyAddVector(Wheel.HubPosition, FlameHeading, -(Radius + 0.12)), {0.0, 0.0, -0.15 * Radius});
            const Vector3 Carried      = ScaleVector(Wheel.ContactVelocity, SmokeSpecification::CarrierVelocityShare);
            const double  Heat         = Thermal.CombustionPower * SmokeSpecification::FireHeatPowerScale * HeatYield;
            Emit(Sidewall, 0.3, AddVector(MultiplyAddVector(Carried, Outward, 0.4), {0.0, 0.0, FlameRise}), 0.6 * SmokeYield, 0.6 * Heat,
                 7.0 * Rate, 8.0 * SootYield);
            Emit(Trailing, 0.28, AddVector(Carried, {0.0, 0.0, FlameRise}), 0.4 * SmokeYield, 0.4 * Heat, 5.0 * Rate, 6.0 * SootYield);
            const Vector3& Flank     = Sidewall;
            const double   Flicker   = 0.78 + 0.22 * std::sin(Elapsed_ * 17.0 + static_cast<double>(Index) * 2.1) * std::sin(Elapsed_ * 6.3 + static_cast<double>(Index));
            const double   Intensity = Thermal.CombustionPower * FireLightGain * Options.FireLight * Flicker;
            Lights_.push_back({AddVector(Flank, {0.0, 0.0, 0.45}), 0.4, ScaleVector(FireColour, Intensity)});
            Embers_.push_back({Flank, AddVector(ScaleVector(Wheel.ContactVelocity, 0.3), {0.0, 0.0, 2.2}), 55.0 * Options.Embers, 1350.0, 0.9, 2.5, 0.15, 1.6});
        }
    }

    // ② exhaust afterfire: while the window is open each tip fires 35–90 ms bursts with random gaps
    Popping_ = false;
    for (size_t Tip = 0; Tip < Telemetry.Exhausts.size() && Tip < 2; ++Tip)
    {
        PopSeconds_[Tip] = std::max(0.0, PopSeconds_[Tip] - Seconds);
        if (Options.Afterfire && Telemetry.AfterfireSeconds > 0.0 && PopSeconds_[Tip] <= 0.0 && ProjectUniform() < Seconds * 14.0)
        {
            PopSeconds_[Tip]  = 0.035 + 0.055 * ProjectUniform();
            PopStrength_[Tip] = 0.6 + 0.8 * ProjectUniform();
        }
        if (PopSeconds_[Tip] <= 0.0)
        {
            continue;
        }
        Popping_ = true;
        const PlumeExhaust& Exhaust   = Telemetry.Exhausts[Tip];
        const double        Strength  = PopStrength_[Tip];
        const double        Speed     = 8.0 + 6.0 * Strength;
        const Vector3       Velocity  = MultiplyAddVector(ScaleVector(Telemetry.Velocity, 0.7), Exhaust.Direction, Speed);
        const Vector3       Nozzle    = MultiplyAddVector(Exhaust.Position, Exhaust.Direction, 0.15);
        Emit(Nozzle, 0.1, Velocity, 1.0 * Strength * SmokeYield, 260.0 * Strength * HeatYield, 40.0 * Strength * Rate, 1.2 * Strength * SootYield);
        Lights_.push_back({MultiplyAddVector(Nozzle, Exhaust.Direction, 0.3), 0.25, ScaleVector({1.0, 0.55, 0.2}, 9.0 * Strength * Options.FireLight)});
        Embers_.push_back({Nozzle, Velocity, 260.0 * Strength * Options.Embers, 1700.0, 1.6, 0.4, 0.6, 0.35});
    }

    // ③ transient bursts until their time is spent
    for (PlumeBurst& Burst : Bursts_)
    {
        const double Share = std::min(1.0, Burst.Remaining / std::max(Burst.Seconds, 1.0e-3));
        Emit(Burst.Position, Burst.Radius, Burst.Velocity, Burst.Smoke * Share * Rate, Burst.Heat * Share * HeatYield,
             Burst.Flame * Share * Rate, Burst.Soot * Share * SootYield);
        Burst.Remaining -= Seconds;
    }
    Bursts_.erase(std::remove_if(Bursts_.begin(), Bursts_.end(), [](const PlumeBurst& Burst) { return !(Burst.Remaining > 0.0); }), Bursts_.end());
    std::stable_sort(Lights_.begin(), Lights_.end(), [](const PlumeFireLight& Left, const PlumeFireLight& Right)
    {
        return ((Right.Radiance[0] + Right.Radiance[1]) - (Left.Radiance[0] + Left.Radiance[1])) < 0.0;
    });
    return Count_;
}

PlumeBounds PlumeSourceProjection::ResolveBounds() const
{
    constexpr double Infinity = std::numeric_limits<double>::infinity();
    PlumeBounds      Bounds{{Infinity, Infinity, Infinity}, {-Infinity, -Infinity, -Infinity}};
    for (uint32_t Index = 0; Index < Count_; ++Index)
    {
        const uint32_t Offset = Index * PlumeSourceStride;
        const double   Reach  = std::max(static_cast<double>(Records_[Offset + 3]), 0.3) * 3.5;
        for (uint32_t Axis = 0; Axis < 3; ++Axis)
        {
            Bounds.Minimum[Axis] = std::min(Bounds.Minimum[Axis], static_cast<double>(Records_[Offset + Axis]) - Reach);
            Bounds.Maximum[Axis] = std::max(Bounds.Maximum[Axis], static_cast<double>(Records_[Offset + Axis]) + Reach);
        }
    }
    return Bounds;
}

} // namespace Frontier::FluidDynamics
