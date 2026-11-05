/*==========================================================================================================================================
                                                         PLUMESCHEDULE.CPP
==========================================================================================================================================*/
// 🧩 See PlumeSchedule.h. Doubles stay doubles; every store into a record is the JavaScript's typed-array store.

#include "PlumeSchedule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::PlumeSimulation
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

// 📝 Math.round: to nearest, ties toward +∞.
double RoundHalfUp(double Value) noexcept
{
    const double Floor = std::floor(Value);
    return (Value - Floor >= 0.5) ? Floor + 1.0 : Floor;
}

// 📝 A record's words, in the host's byte order, as the JavaScript's three typed views over one block of bytes.
template <size_t Size>
void StoreFloat(std::array<uint8_t, Size>& Bytes, size_t Word, double Value) noexcept
{
    const float Single = static_cast<float>(Value);
    std::memcpy(Bytes.data() + Word * 4, &Single, 4);
}

template <size_t Size>
void StoreInteger(std::array<uint8_t, Size>& Bytes, size_t Word, int32_t Value) noexcept
{
    std::memcpy(Bytes.data() + Word * 4, &Value, 4);
}

template <size_t Size>
void StoreUnsigned(std::array<uint8_t, Size>& Bytes, size_t Word, uint32_t Value) noexcept
{
    std::memcpy(Bytes.data() + Word * 4, &Value, 4);
}

} // namespace

PlumeSchedule::PlumeSchedule(const PlumeGrade& Configuration) : Configuration_(Configuration)
{
    AssignConfiguration(Configuration);
}

void PlumeSchedule::AssignConfiguration(const PlumeGrade& Configuration)
{
    Configuration_ = Configuration;
    ThermoExtent_  = Configuration.Extent;
    VelocityExtent_ = {Configuration.Extent[0] / 2, Configuration.Extent[1] / 2, Configuration.Extent[2] / 2};
    CoarseEdge_    = 2.0 * Configuration.CellExtent;
    Origin_        = {0, 0};
    PriorOrigin_   = {RestartOrigin, RestartOrigin};
}

void PlumeSchedule::AssignPressureSweeps(double Requested)
{
    Configuration_.PressureSweeps = static_cast<uint32_t>(std::max(4.0, 2.0 * RoundHalfUp(Requested / 2.0)));
}

void PlumeSchedule::Schedule(double Seconds, double RefreshSeconds, const Vector3& Focus, const PlumeSourceProjection& Sources,
                             const PlumeSettings& Settings, const Vector3& SunDirection)
{
    SunDirection_ = SunDirection;
    StepPending_  = false;
    const bool Active = Settings.PlumeEnabled && Sources.QueryCount() > 0;
    if (Active)
    {
        QuietSeconds_ = 0.0;
        if (!Resident_)
        {
            Resident_     = true;
            PriorOrigin_  = {RestartOrigin, RestartOrigin};
            Accumulator_  = StepSeconds;
            QuietPrior_   = 0.0;
            RecentreWindow(Focus, true);
        }
    }
    else if (Resident_)
    {
        QuietSeconds_ += Seconds;
        if (QuietSeconds_ >= FadeCeiling || !Settings.PlumeEnabled)
        {
            Resident_ = false;
        }
    }
    if (!Resident_)
    {
        Accumulator_ = 0.0;
        return;
    }
    Accumulator_    += Seconds;
    ElapsedSeconds_ += Seconds;
    if (Accumulator_ < StepSeconds)
    {
        return;
    }
    const double StepDuration = std::min(Accumulator_, StepCeiling);   // Δτ
    Accumulator_ = 0.0;
    RecentreWindow(Focus, false);
    EncodeRecord(StepDuration, RefreshSeconds, Sources, Settings);
    SourceCount_ = Sources.QueryCount();
    StepPending_ = true;
}

void PlumeSchedule::RecentreWindow(const Vector3& Focus, bool Force)
{
    const std::array<int32_t, 2> Cell   = {static_cast<int32_t>(std::floor(Focus[0] / CoarseEdge_)), static_cast<int32_t>(std::floor(Focus[1] / CoarseEdge_))};
    const std::array<int32_t, 2> Half   = {static_cast<int32_t>(VelocityExtent_[0] >> 1), static_cast<int32_t>(VelocityExtent_[1] >> 1)};
    const std::array<int32_t, 2> Centre = {Origin_[0] + Half[0], Origin_[1] + Half[1]};
    if (Force || std::abs(Cell[0] - Centre[0]) > Half[0] / 2.0 || std::abs(Cell[1] - Centre[1]) > Half[1] / 2.0)
    {
        Origin_ = {Cell[0] - Half[0], Cell[1] - Half[1]};
    }
}

void PlumeSchedule::EncodeRecord(double StepDuration, double RefreshSeconds, const PlumeSourceProjection& Sources, const PlumeSettings& Settings)
{
    const double Edge = Configuration_.CellExtent;
    const double Δτ   = StepDuration;
    // 📝 Frontier's continuous fade once the sources stop: whatever remains reaches zero at FadeCeiling.
    double Fade = 1.0;
    if (QuietSeconds_ > 0.0)
    {
        const double Remaining = std::max(0.0, FadeCeiling - QuietSeconds_);
        Fade = Remaining / std::max(Δτ, FadeCeiling - QuietPrior_);
    }
    QuietPrior_ = QuietSeconds_;
    const double Dissipation = std::min(0.95, Settings.PlumeDissipation);
    const double WindAngle   = Settings.PlumeWindDirection * Pi / 180.0;
    const double Wind[3]     = {std::cos(WindAngle) * Settings.PlumeWindSpeed, std::sin(WindAngle) * Settings.PlumeWindSpeed, 0.0};
    auto&        Bytes       = RecordBytes_;

    StoreInteger(Bytes, 0, Origin_[0]);
    StoreInteger(Bytes, 1, Origin_[1]);
    StoreInteger(Bytes, 2, 0);
    StoreInteger(Bytes, 3, static_cast<int32_t>(StepOrdinal_++));
    StoreInteger(Bytes, 4, PriorOrigin_[0]);
    StoreInteger(Bytes, 5, PriorOrigin_[1]);
    StoreInteger(Bytes, 6, 0);
    StoreInteger(Bytes, 7, static_cast<int32_t>(Sources.QueryCount()));
    StoreUnsigned(Bytes, 8, VelocityExtent_[0]);
    StoreUnsigned(Bytes, 9, VelocityExtent_[1]);
    StoreUnsigned(Bytes, 10, VelocityExtent_[2]);
    StoreUnsigned(Bytes, 11, 0);
    StoreUnsigned(Bytes, 12, ThermoExtent_[0]);
    StoreUnsigned(Bytes, 13, ThermoExtent_[1]);
    StoreUnsigned(Bytes, 14, ThermoExtent_[2]);
    StoreUnsigned(Bytes, 15, Settings.Inspection == 10 ? 1u : 0u);

    const auto Scalars = [&Bytes](size_t Word, double A, double B, double C, double D)
    {
        StoreFloat(Bytes, Word, A);
        StoreFloat(Bytes, Word + 1, B);
        StoreFloat(Bytes, Word + 2, C);
        StoreFloat(Bytes, Word + 3, D);
    };
    Scalars(16, Edge, 2.0 * Edge, FloorHeight, Δτ);
    Scalars(20, Settings.PlumeBuoyancy, 0.12, Settings.PlumeVorticity, Settings.PlumeTurbulence);
    Scalars(24, std::pow(1.0 - Dissipation, Δτ) * Fade, std::pow(1.0 - std::min(0.95, Settings.PlumeCooling), Δτ) * Fade,
            std::exp(-Settings.PlumeFlameDecay * Δτ) * Fade, std::pow(1.0 - 0.08, Δτ));
    Scalars(28, Wind[0], Wind[1], Wind[2], ElapsedSeconds_);
    const FluidDynamics::PlumeBounds Bounds = Sources.ResolveBounds();
    const auto Finite = [](double Component) { return std::isfinite(Component) ? Component : 0.0; };
    Scalars(32, Finite(Bounds.Minimum[0]), Finite(Bounds.Minimum[1]), Finite(Bounds.Minimum[2]), std::max(RefreshSeconds, 1.0e-3));
    Scalars(36, Finite(Bounds.Maximum[0]), Finite(Bounds.Maximum[1]), Finite(Bounds.Maximum[2]), FlameScale * Settings.PlumeFire);
    Scalars(40, Settings.PlumeExtinction, Settings.PlumeAlbedo, 3.0 * Settings.PlumeExtinction, static_cast<double>(Configuration_.MarchSamples));
    Scalars(44, SunDirection_[0], SunDirection_[1], SunDirection_[2], Settings.PlumeSootAlbedo);
    Scalars(48, 2.4 * Settings.PlumeSoot, 25.0 * Settings.PlumeHeat, std::pow(1.0 - 0.8 * Dissipation, Δτ) * Fade, 0.0);
    Scalars(52, Settings.PlumeScattering, Settings.PlumeAnisotropy, Settings.PlumeAmbient, Settings.PlumeFireLight);
    Scalars(56, Settings.PlumeDetail, Settings.PlumeDetailScale, Settings.PlumeTurbulenceScale, 0.0);
    Scalars(60, Settings.PlumeFlameCore, Settings.PlumeFlameTip, 30.0, 0.0);
    PriorOrigin_ = Origin_;
}

void PlumeSchedule::AssignFireLights(const std::vector<PlumeFireLight>& Lights)
{
    auto& Bytes = FireLightBytes_;
    Bytes.fill(0);   // Scalars.fill(0.0)
    const uint32_t Count = static_cast<uint32_t>(std::min<size_t>(FireLightLimit, Lights.size()));
    StoreUnsigned(Bytes, 0, Count);
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const PlumeFireLight& Light = Lights[Index];
        const size_t          First = 4 + Index * 4;
        StoreFloat(Bytes, First, Light.Position[0]);
        StoreFloat(Bytes, First + 1, Light.Position[1]);
        StoreFloat(Bytes, First + 2, Light.Position[2]);
        StoreFloat(Bytes, First + 3, Light.Radius);
        const size_t Second = 20 + Index * 4;
        StoreFloat(Bytes, Second, Light.Radiance[0]);
        StoreFloat(Bytes, Second + 1, Light.Radiance[1]);
        StoreFloat(Bytes, Second + 2, Light.Radiance[2]);
        StoreFloat(Bytes, Second + 3, 0.0);
    }
    FireLightCount_ = Count;
}

void PlumeSchedule::Clear()
{
    Resident_     = false;
    StepPending_  = false;
    QuietSeconds_ = 0.0;
    PriorOrigin_  = {RestartOrigin, RestartOrigin};
}

void PlumeSchedule::CompleteStep()
{
    State_ = 1u - State_;
    StepPending_ = false;
    ++StepsTaken_;
}

void PlumeSchedule::ResetState()
{
    State_ = 0;
    Clear();
}

PlumeDescription PlumeSchedule::QueryDescription() const
{
    PlumeDescription Description;
    Description.Thermo       = ThermoExtent_;
    Description.Velocity     = VelocityExtent_;
    Description.EdgeThermo   = Configuration_.CellExtent;
    Description.EdgeVelocity = 2.0 * Configuration_.CellExtent;
    for (size_t Axis = 0; Axis < 3; ++Axis)
    {
        Description.Span[Axis] = ThermoExtent_[Axis] * Configuration_.CellExtent;
    }
    const uint64_t Fine   = static_cast<uint64_t>(ThermoExtent_[0]) * ThermoExtent_[1] * ThermoExtent_[2];
    const uint64_t Coarse = static_cast<uint64_t>(VelocityExtent_[0]) * VelocityExtent_[1] * VelocityExtent_[2];
    Description.Cells   = Fine + Coarse;
    Description.Bytes   = Fine * BytesPerFineCell + Coarse * BytesPerCoarseCell;
    Description.Grade   = std::string(Configuration_.Name);
    Description.Sweeps  = Configuration_.PressureSweeps;
    Description.Samples = Configuration_.MarchSamples;
    return Description;
}

std::vector<uint8_t> PlumeSchedule::ConstructDetail()
{
    const int Edge = static_cast<int>(DetailExtent);
    // 📝 The hash is the JavaScript's: uint32 arithmetic is Math.imul and the int32 xors.
    const auto Hash = [](int X, int Y, int Z, int Period, int Seed) -> double
    {
        const auto Wrap = [Period](int Cell) { return static_cast<uint32_t>(((Cell % Period) + Period) % Period); };
        uint32_t Word = (Wrap(X) * 73856093u) ^ (Wrap(Y) * 19349663u) ^ (Wrap(Z) * 83492791u)
                      ^ static_cast<uint32_t>(static_cast<uint64_t>(Seed) * 2654435761ull);
        Word ^= Word >> 15;
        Word *= 0x2C1B3C6Du;
        Word ^= Word >> 12;
        Word *= 0x297A2D39u;
        return static_cast<double>(Word ^ (Word >> 15)) / 4294967296.0;
    };
    const auto Smooth = [](double Fraction) { return Fraction * Fraction * (3.0 - 2.0 * Fraction); };
    const auto Value = [&](int X, int Y, int Z, int Period, int Seed) -> double
    {
        const double Scale = static_cast<double>(Period) / static_cast<double>(Edge);
        const double U = X * Scale, V = Y * Scale, W = Z * Scale;
        const int    I = static_cast<int>(std::floor(U)), J = static_cast<int>(std::floor(V)), K = static_cast<int>(std::floor(W));
        const double A = Smooth(U - I), B = Smooth(V - J), C = Smooth(W - K);
        double Sum = 0.0;
        for (int Corner = 0; Corner < 8; ++Corner)
        {
            const int Dx = Corner & 1, Dy = (Corner >> 1) & 1, Dz = (Corner >> 2) & 1;
            Sum += Hash(I + Dx, J + Dy, K + Dz, Period, Seed) * (Dx ? A : 1 - A) * (Dy ? B : 1 - B) * (Dz ? C : 1 - C);
        }
        return Sum;
    };
    const auto Worley = [&](int X, int Y, int Z, int Period, int Seed) -> double
    {
        const double Scale = static_cast<double>(Period) / static_cast<double>(Edge);
        const double U = X * Scale, V = Y * Scale, W = Z * Scale;
        const int    I = static_cast<int>(std::floor(U)), J = static_cast<int>(std::floor(V)), K = static_cast<int>(std::floor(W));
        double       Nearest = 9.0;
        for (int Dz = -1; Dz <= 1; ++Dz)
        {
            for (int Dy = -1; Dy <= 1; ++Dy)
            {
                for (int Dx = -1; Dx <= 1; ++Dx)
                {
                    const int    Cx = I + Dx, Cy = J + Dy, Cz = K + Dz;
                    const double Px = Cx + Hash(Cx, Cy, Cz, Period, Seed);
                    const double Py = Cy + Hash(Cx, Cy, Cz, Period, Seed + 1);
                    const double Pz = Cz + Hash(Cx, Cy, Cz, Period, Seed + 2);
                    Nearest = std::min(Nearest, (Px - U) * (Px - U) + (Py - V) * (Py - V) + (Pz - W) * (Pz - W));
                }
            }
        }
        return std::max(0.0, 1.0 - std::sqrt(Nearest));
    };
    std::vector<uint8_t> Texels(static_cast<size_t>(Edge) * Edge * Edge * 4);
    for (int Z = 0; Z < Edge; ++Z)
    {
        for (int Y = 0; Y < Edge; ++Y)
        {
            for (int X = 0; X < Edge; ++X)
            {
                const size_t Offset = ((static_cast<size_t>(Z) * Edge + Y) * Edge + X) * 4;
                const double Billow = Worley(X, Y, Z, 4, 11);
                Texels[Offset]     = static_cast<uint8_t>(RoundHalfUp(255 * (0.55 * Value(X, Y, Z, 4, 1) + 0.45 * Billow)));
                Texels[Offset + 1] = static_cast<uint8_t>(RoundHalfUp(255 * (0.6 * Value(X, Y, Z, 8, 2) + 0.4 * Worley(X, Y, Z, 8, 21))));
                Texels[Offset + 2] = static_cast<uint8_t>(RoundHalfUp(255 * Value(X, Y, Z, 16, 3)));
                Texels[Offset + 3] = static_cast<uint8_t>(RoundHalfUp(255 * Value(X, Y, Z, 8, 4)));
            }
        }
    }
    return Texels;
}

} // namespace Frontier::PlumeSimulation
