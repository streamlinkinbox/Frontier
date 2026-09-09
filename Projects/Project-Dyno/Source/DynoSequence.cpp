//============================================================================================================================================
//                                                       DYNOSEQUENCE.CPP
//============================================================================================================================================
// 🧩 Keyframe timelines for the dyno cell (see DynoSequence.h). Linear interpolation between keyframes; the pull holds its
//    last keyframe once finished so a looping host can simply Restart().

#include "DynoSequence.h"

#include <algorithm>

namespace Frontier {

namespace {

const char* const PullNameSheet[] = { "idle", "sweep", "pull", "steady", "blip" };

} // namespace

DynoSequence::DynoSequence() noexcept
{
    Select("sweep", 9000.0f);
}

const char* const* DynoSequence::PullNames(uint32_t* Count) noexcept
{
    if (Count) *Count = uint32_t(sizeof(PullNameSheet) / sizeof(PullNameSheet[0]));
    return PullNameSheet;
}

bool DynoSequence::Select(std::string_view Requested, float RedlineRpm) noexcept
{
    const float Idle    = 900.0f;
    const float Redline = std::max(3000.0f, RedlineRpm);
    Keyframes.clear();
    Elapsed = 0.0f;
    bool Known = true;

    if (Requested == "idle")
    {
        Name = "idle";
        Keyframes = { { 0.0f, Idle, 0.0f }, { 10.0f, Idle, 0.0f } };
    }
    else if (Requested == "pull")
    {
        Name = "pull";
        Keyframes = { { 0.0f, Idle,    0.00f }, { 1.0f, Idle,    0.00f },
                      { 1.2f, 2000.0f, 1.00f }, { 7.2f, Redline, 1.00f }, { 8.2f, Redline, 1.00f },
                      { 8.3f, Redline, 0.00f }, { 13.3f, Idle,   0.00f }, { 15.0f, Idle,   0.00f } };
    }
    else if (Requested == "steady")
    {
        Name = "steady";
        Keyframes = { { 0.0f, 4000.0f, 0.4f }, { 10.0f, 4000.0f, 0.4f } };
    }
    else if (Requested == "blip")
    {
        Name = "blip";
        Keyframes = { { 0.0f, Idle, 0.0f }, { 1.0f, Idle, 0.0f },
                      { 1.15f, 4500.0f, 1.0f }, { 1.30f, 4500.0f, 0.0f }, { 2.2f, Idle, 0.0f },
                      { 3.0f, Idle, 0.0f }, { 3.15f, 5500.0f, 1.0f }, { 3.30f, 5500.0f, 0.0f }, { 4.4f, Idle, 0.0f },
                      { 5.2f, Idle, 0.0f }, { 5.35f, 6500.0f, 1.0f }, { 5.50f, 6500.0f, 0.0f }, { 6.8f, Idle, 0.0f },
                      { 8.0f, Idle, 0.0f } };
    }
    else
    {
        Name  = "sweep";
        Known = Requested == "sweep";
        Keyframes = { { 0.0f, 1000.0f, 0.5f }, { 6.0f, Redline, 0.5f }, { 12.0f, 1000.0f, 0.5f } };
    }

    Sample(0.0f);
    return Known;
}

void DynoSequence::Advance(float Δτ) noexcept
{
    Elapsed = std::min(Elapsed + std::max(0.0f, Δτ), QueryDuration());
    Sample(Elapsed);
}

void DynoSequence::Sample(float Time) noexcept
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
        const float Span = B.Time - A.Time;
        const float T    = Span > 0.0f ? (Time - A.Time) / Span : 1.0f;
        Record.Rpm      = A.Rpm      + (B.Rpm      - A.Rpm)      * T;
        Record.Throttle = A.Throttle + (B.Throttle - A.Throttle) * T;
    }
    Record.Load          = Record.Throttle;   // A1: no torque curve yet — A2's inertia integrator replaces this
    Record.Gear          = 0;
    Record.ClutchEngaged = false;
    Record.Boost         = 0.0f;
}

} // namespace Frontier
