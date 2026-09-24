//============================================================================================================================================
//                                                     CRANKCLICKINTEGRATOR.H
//============================================================================================================================================
// 🧩 Row A1 stand-in for the powertrain: a crank clock that emits one short click per firing event, plus an optional
//    sine sweep. It exercises everything the transport must get right before any synthesis exists — the seqlock'd demand
//    hand-off from the main thread, per-sample rpm slewing (no zipper, and no dependence on the block size the device
//    granted), sub-sample event placement, and the fixed-slice render contract that keeps the offline WAV identical to
//    the device output.
//
//    Firing frequency of a four-stroke: f = rpm / 60 × CylinderCount / 2   [Hz]
//    Each event places a 2-sample-wide raised-cosine click at its exact fractional time (linear split across the two
//    samples), which is what the Scratchpad proof measures: click spacing must equal 60 / (rpm · N/2) to within one sample.
//
//    Replaced by AcousticIntegrator in row A2; kept afterwards as the transport's self-test.

#pragma once

#include "../../../Engine/DeviceExchange/RelayQueue.h"
#include "../../../Engine/PlatformInterchange/AudioExchange.h"
#include "DynoSequence.h"

#include <atomic>
#include <cstdint>

namespace Frontier {

class CrankClickIntegrator final : public SignalIntegrator
{
public:
    explicit CrankClickIntegrator(uint32_t CylinderCount = 8u) noexcept : Cylinders(CylinderCount) { }

    // Main thread: publish the latest demand (wait-free triple-slot relay, latest wins, never blocks either side).
    void AssignDemand(const PowertrainRecord& Record) noexcept { Demand.Publish(Record); }

    void AssignSweep(bool Enabled) noexcept        { SweepEnabled.store(Enabled, std::memory_order_relaxed); }
    void AssignClicks(bool Enabled) noexcept       { ClicksEnabled.store(Enabled, std::memory_order_relaxed); }

    // SignalIntegrator
    void Prepare(uint32_t SampleRate, uint32_t ChannelCount) noexcept override;
    void Render(float* Output, uint32_t FrameCount) noexcept override;

    [[nodiscard]] uint64_t QueryEventCount() const noexcept { return EventCount.load(std::memory_order_relaxed); }

    // Realtime side only: the newest published demand (consumes the mailbox — what Render does at block entry).
    bool TakeDemand(PowertrainRecord& Out) noexcept { return Demand.Take(Out); }

private:

    uint32_t Cylinders;
    uint32_t Rate     = 48000u;   // [Hz]
    uint32_t Channels = 2u;       // [-]

    RelayQueue<PowertrainRecord> Demand;   // main → realtime, latest wins

    std::atomic<bool>     SweepEnabled  { false };
    std::atomic<bool>     ClicksEnabled { true };
    std::atomic<uint64_t> EventCount    { 0u };

    // Realtime-only integration
    double  Cycle        = 0.0;      // [-]    firing-cycle phase in [0, 1): an event fires each time it wraps
    double  CurrentRpm   = 900.0;    // [rpm]  slewed toward the demanded rpm per sample (rate-limited)
    double  SweepPhase   = 0.0;      // [rad]
    double  SweepSeconds = 0.0;      // [s]
    float   Carry        = 0.0f;     // [-]    second half of a click that straddles the block boundary
};

} // namespace Frontier
