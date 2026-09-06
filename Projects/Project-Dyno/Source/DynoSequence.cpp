//============================================================================================================================================
//                                                       DYNOSEQUENCE.CPP
//============================================================================================================================================
// 🧩 Keyframe timelines for the dyno cell (see DynoSequence.h). Linear interpolation between keyframes; the pull holds its
//    last keyframe once finished so a looping host can simply Restart(). The keyframes and the interpolation are the
//    editor's `DynoSequence.select / sample` verbatim (Tools/AudioEditor/index.html).

#include "DynoSequence.h"
#include "../../../Engine/PlatformInterchange/SignalSections.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

namespace {

const char* const PullNameSheet[] = { "idle", "sweep", "pull", "steady", "blip", "overrun", "limiter" };

} // namespace

DynoSequence::DynoSequence() noexcept
{
    Select("sweep", 9000.0, 900.0);
}

const char* const* DynoSequence::PullNames(uint32_t* Count) noexcept
{
    if (Count) *Count = uint32_t(sizeof(PullNameSheet) / sizeof(PullNameSheet[0]));
    return PullNameSheet;
}

bool DynoSequence::Select(std::string_view Requested, double RedlineRpm, double IdleRpm) noexcept
{
    const double Idle    = IdleRpm > 0.0 ? IdleRpm : 900.0;
    const double Redline = std::max(3000.0, RedlineRpm);
    Keyframes.clear();
    Elapsed = 0.0;
    bool Known = true;

    if (Requested == "idle")
    {
        Name = "idle";
        Keyframes = { { 0.0, Idle, 0.0 }, { 10.0, Idle, 0.0 } };
    }
    else if (Requested == "pull")
    {
        Name = "pull";
        Keyframes = { { 0.0, Idle,    0.0 }, { 1.0, Idle,    0.0 },
                      { 1.2, 2000.0, 1.0 }, { 7.2, Redline, 1.0 }, { 8.2, Redline, 1.0 },
                      { 8.3, Redline, 0.0 }, { 13.3, Idle,  0.0 }, { 15.0, Idle,   0.0 } };
    }
    else if (Requested == "steady")
    {
        Name = "steady";
        Keyframes = { { 0.0, 4000.0, 0.4 }, { 10.0, 4000.0, 0.4 } };
    }
    else if (Requested == "blip")
    {
        Name = "blip";
        Keyframes = { { 0.0, Idle, 0.0 }, { 1.0, Idle, 0.0 },
                      { 1.15, 4500.0, 1.0 }, { 1.30, 4500.0, 0.0 }, { 2.2, Idle, 0.0 },
                      { 3.0, Idle, 0.0 }, { 3.15, 5500.0, 1.0 }, { 3.30, 5500.0, 0.0 }, { 4.4, Idle, 0.0 },
                      { 5.2, Idle, 0.0 }, { 5.35, 6500.0, 1.0 }, { 5.50, 6500.0, 0.0 }, { 6.8, Idle, 0.0 },
                      { 8.0, Idle, 0.0 } };
    }
    else if (Requested == "overrun")
    {
        Name = "overrun";
        Keyframes = { { 0.0, Idle, 0.0 }, { 1.0, Idle, 0.0 }, { 1.2, 3500.0, 1.0 }, { 3.2, Redline * 0.85, 1.0 }, { 3.3, Redline * 0.85, 0.0 },
                      { 9.3, Idle, 0.0 }, { 11.0, Idle, 0.0 } };
    }
    else if (Requested == "limiter")
    {
        Name = "limiter";
        Keyframes = { { 0.0, 4000.0, 1.0 }, { 2.0, Redline, 1.0 }, { 6.0, Redline, 1.0 }, { 6.1, Redline, 0.0 }, { 10.0, Idle, 0.0 }, { 11.0, Idle, 0.0 } };
    }
    else
    {
        Name  = "sweep";
        Known = Requested == "sweep";
        Keyframes = { { 0.0, 1000.0, 0.5 }, { 6.0, Redline, 0.5 }, { 12.0, 1000.0, 0.5 } };
    }

    Record.Rpm = Keyframes.front().Rpm; Record.Throttle = Keyframes.front().Throttle;
    Sample(0.0);
    return Known;
}

void DynoSequence::Advance(double Δτ) noexcept
{
    Elapsed = std::min(Elapsed + std::max(0.0, Δτ), QueryDuration());
    Sample(Elapsed);
}

void DynoSequence::Sample(double Time) noexcept
{
    if (Keyframes.empty()) return;
    if (Time <= Keyframes.front().Time) { Record.Rpm = Keyframes.front().Rpm; Record.Throttle = Keyframes.front().Throttle; }
    else if (Time >= Keyframes.back().Time) { Record.Rpm = Keyframes.back().Rpm; Record.Throttle = Keyframes.back().Throttle; }
    else
    {
        size_t I = 1u;
        while (I < Keyframes.size() && Keyframes[I].Time < Time) ++I;
        const DynoKeyframe& A = Keyframes[I - 1u];
        const DynoKeyframe& B = Keyframes[I];
        const double Span = B.Time - A.Time;
        const double T    = Span > 0.0 ? (Time - A.Time) / Span : 1.0;
        Record.Rpm      = A.Rpm      + (B.Rpm      - A.Rpm)      * T;
        Record.Throttle = A.Throttle + (B.Throttle - A.Throttle) * T;
    }
    Record.Load          = Record.Throttle;   // the bare script; ScriptedRecord applies the fuel-cut rule
    Record.Gear          = 0;
    Record.ClutchEngaged = false;
    Record.Boost         = 0.0;
}

PowertrainRecord DynoSequence::ScriptedRecord(double Rpm, double Throttle, const AcousticStructure::VehicleSheet& Vehicle, double Time) noexcept
{
    const bool Overrun    = Throttle < 0.02 && Rpm > Vehicle.IdleRpm * 1.6;
    const bool AtLimiter  = Rpm >= Vehicle.RedlineRpm - 15.0 && Throttle > 0.5;
    const bool CutPhase   = AtLimiter && (int64_t(std::floor(Time * 14.0)) & 1) == 1;
    const double IdleLoad = ClampReal((Vehicle.FrictionNm + Vehicle.FrictionPerKrpmNm * Rpm * 0.001) / std::max(1.0, Vehicle.PeakTorqueNm), 0.0, 0.3);
    PowertrainRecord Record;
    Record.Rpm      = Rpm;
    Record.Throttle = Throttle;
    Record.Load     = (Overrun || CutPhase) ? 0.0 : std::max(Throttle, IdleLoad);
    return Record;
}

} // namespace Frontier
