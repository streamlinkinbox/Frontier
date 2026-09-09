//============================================================================================================================================
//                                                    CRANKCLICKINTEGRATOR.CPP
//============================================================================================================================================
// 🧩 Crank-locked click train + sine sweep (see CrankClickIntegrator.h).

#include "CrankClickIntegrator.h"

#include <algorithm>
#include <cmath>

namespace Frontier {

namespace {

constexpr double Pi          = 3.14159265358979323846;
constexpr float  ClickLevel  = 0.6f;    // [-]  peak of one click
constexpr float  SweepLevel  = 0.2f;    // [-]  sine amplitude
constexpr double SweepLowHz  = 40.0;    // [Hz] exponential sweep start
constexpr double SweepHighHz = 8000.0;  // [Hz] exponential sweep end
constexpr double SweepPeriod = 5.0;     // [s]  one sweep, then repeats
constexpr double SlewRpmPerSecond = 50000.0;   // [rpm/s] per-sample rate limit toward the demanded rpm (block-size invariant, exact arrival)

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   GENERATOR
//------------------------------------------------------------------------------------------------------------------------

void CrankClickIntegrator::Prepare(uint32_t SampleRate, uint32_t ChannelCount) noexcept
{
    Rate         = SampleRate;
    Channels     = ChannelCount;
    Cycle        = 0.0;
    SweepPhase   = 0.0;
    SweepSeconds = 0.0;
    Carry        = 0.0f;
    EventCount.store(0u, std::memory_order_relaxed);
    PowertrainRecord Initial; Initial.Rpm = float(CurrentRpm);
    Demand.Place(Initial);
}

void CrankClickIntegrator::Render(float* Output, uint32_t FrameCount) noexcept
{
    PowertrainRecord Latest = Demand.Latest();
    Demand.Take(Latest);

    const bool   Clicks  = ClicksEnabled.load(std::memory_order_relaxed);
    const bool   Sweep   = SweepEnabled.load(std::memory_order_relaxed);
    const double Δt      = 1.0 / double(Rate);
    const double Target  = double(Latest.Rpm);
    const double MaxStep = SlewRpmPerSecond * Δt;         // [rpm] largest per-sample change — the same whatever the block size
    const double PerRev  = double(Cylinders) * 0.5;       // firings per crank revolution

    float Pending = Carry;   // second sample of a click that started in the previous block
    Carry = 0.0f;

    for (uint32_t I = 0; I < FrameCount; ++I)
    {
        float Mono = Pending;
        Pending = 0.0f;

        {
            const double Diff = Target - CurrentRpm;
            CurrentRpm += Diff > MaxStep ? MaxStep : (Diff < -MaxStep ? -MaxStep : Diff);
        }

        if (Clicks)
        {
            const double FiringHz = CurrentRpm / 60.0 * PerRev;
            Cycle += FiringHz * Δt;
            if (Cycle >= 1.0)
            {
                Cycle -= 1.0;
                // Fractional position of the event inside this sample: 0 = at the start, 1 = at the end.
                const double Fraction = FiringHz > 0.0 ? 1.0 - Cycle / (FiringHz * Δt) : 0.0;
                const float  Late     = float(std::clamp(Fraction, 0.0, 1.0));
                Mono    += ClickLevel * (1.0f - Late);   // energy split linearly across the two samples ⇒ sub-sample centroid
                Pending += ClickLevel * Late;
                EventCount.fetch_add(1u, std::memory_order_relaxed);
            }
        }

        if (Sweep)
        {
            const double T  = std::fmod(SweepSeconds, SweepPeriod) / SweepPeriod;
            const double Hz = SweepLowHz * std::pow(SweepHighHz / SweepLowHz, T);
            SweepPhase += 2.0 * Pi * Hz * Δt;
            if (SweepPhase > 2.0 * Pi) SweepPhase -= 2.0 * Pi;
            SweepSeconds += Δt;
            Mono += SweepLevel * float(std::sin(SweepPhase));
        }

        float* Frame = Output + size_t(I) * Channels;
        for (uint32_t C = 0; C < Channels; ++C) Frame[C] += Mono;
    }

    Carry = Pending;
}

} // namespace Frontier
