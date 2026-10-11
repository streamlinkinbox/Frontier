//============================================================================================================================================
//                                                   INTERFACEAUDIOSEQUENCE.H
//============================================================================================================================================
// 🧩 Binds the world-space panel to the audio transport: turning the panel's controls changes what you hear.
//
//    Engine ⇄ project seam. Neither side knows about the other and neither gains a dependency here:
//        · SpatialInterface reports normalised figure values — it has never heard of rpm.
//        · PlatformInterchange renders a PowertrainRecord — it has never heard of a progress bar.
//    This class is the only place the two meet, and it lives in the PROJECT because deciding that "the bar is
//    throttle and the meter is engine speed" is game semantics, exactly like PhysicsInstanceSequence deciding
//    that instance 11 is a falling ball.
//
//    Threading. AudioExchange's realtime callback runs on a thread miniaudio owns, and it must never block: a
//    missed deadline is an audible click, and no graphics work is worth one. Demand therefore crosses through
//    CrankClickIntegrator's RelayQueue — wait-free on both sides, latest-wins — and this class only ever
//    publishes from the main thread. Nothing here is called from the audio thread.
//
//    Mapping. The panel speaks in 0…1 because that is all a fraction of a bar can mean. The ranges below are the
//    project's own choice and the reason this file exists.

#pragma once

#include "../../../Engine/PlatformInterchange/AudioExchange.h"
#include "CrankClickIntegrator.h"
#include "DynoSequence.h"
#include "InterfaceTrialSequence.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Frontier {
namespace ProjectZero {

struct InterfaceAudioConfiguration
{
    float    IdleRpm      =  900.0f;   // [rpm] panel fill 0.0
    float    RedlineRpm   = 7200.0f;   // [rpm] panel fill 1.0
    uint32_t CylinderCount =     8u;   // [-]   firing events per two revolutions
    bool     UseNullDriver = false;    // [-]   true → clocked silent device (CI, or a machine with no sound card)
};

class InterfaceAudioSequence
{
public:
    // Opens the device and attaches the click-train integrator. Returns false if no device could be opened, in
    //    which case the caller carries on silently — audio is a garnish here, never a reason to fail a frame.
    [[nodiscard]] bool Construct(const InterfaceAudioConfiguration& Configuration, std::string* Error = nullptr) noexcept;
    void               Retire() noexcept;

    // Reads the panel's current values, publishes the resulting demand, and services the device. Main thread
    //    only, once per frame, with the frame's Δτ.
    //
    //    ⚠️ The AudioExchange::Advance call inside is not optional bookkeeping. It drives device-loss recovery,
    //    and on the null driver it is what CLOCKS the callback at all — that device has no hardware timer, so a
    //    bridge that only published demand would fall silent with zero callbacks and look like a broken binding.
    void AdvanceAudio(const InterfaceTrialSequence& Trial, float DeltaSeconds) noexcept;

    [[nodiscard]] bool  IsRunning()      const noexcept { return Running; }
    [[nodiscard]] float QueryRpm()       const noexcept { return LastRecord.Rpm; }
    [[nodiscard]] float QueryThrottle()  const noexcept { return LastRecord.Throttle; }
    [[nodiscard]] const AudioMetrics& QueryMetrics() const noexcept { return Audio.QueryMetrics(); }

    // The mapping itself, exposed so a proof can assert it without opening a sound device. Fill 0…1 → rpm across
    //    the configured band; the meter sweep rides the same value so the needle and the pitch agree.
    [[nodiscard]] PowertrainRecord ComposeDemand(double Fill, double Sweep, bool Engaged) const noexcept;

private:
    InterfaceAudioConfiguration Config;
    AudioExchange               Audio;
    // Held by pointer, not by value: CrankClickIntegrator owns atomics and a RelayQueue, so it is neither
    //    copyable nor movable — correctly, since a relay that could be reassigned under a live realtime reader
    //    would be a race. The cylinder count is a constructor argument, so it is built once Construct knows it.
    std::unique_ptr<CrankClickIntegrator> Powertrain;
    PowertrainRecord            LastRecord;
    bool                        Running = false;
};

} // namespace ProjectZero
} // namespace Frontier
